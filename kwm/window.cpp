#include "kwm.h"

static CGWindowListOption OsxWindowListOption = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;

extern kwm_screen KWMScreen;
extern kwm_focus KWMFocus;
extern kwm_toggles KWMToggles;
extern kwm_mode KWMMode;
extern kwm_tiling KWMTiling;
extern kwm_cache KWMCache;

bool GetTagForCurrentSpace(std::string &Tag)
{
    if(KWMScreen.Current && IsSpaceInitializedForScreen(KWMScreen.Current))
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        if(Space->Mode == SpaceModeBSP)
        {
            Tag = "[bsp]";
            return true;
        }
        else if(Space->Mode == SpaceModeFloating)
        {
            Tag = "[float]";
            return true;
        }

        tree_node *Node = Space->RootNode; 
        bool FoundFocusedWindow = false;
        int FocusedIndex = 0;
        int NumberOfWindows = 0;

        if(Node)
        {
            FocusedIndex = 1;
            NumberOfWindows = 1;

            if(Node->WindowID == KWMFocus.Window->WID)
                FoundFocusedWindow = true;

            while(Node->RightChild)
            {
                if(Node->WindowID == KWMFocus.Window->WID)
                    FoundFocusedWindow = true;

                if(!FoundFocusedWindow)
                    ++FocusedIndex;

                ++NumberOfWindows;

                Node = Node->RightChild;
            }

            if(Node->WindowID == KWMFocus.Window->WID)
                FoundFocusedWindow = true;
        }

        if(FoundFocusedWindow)
            Tag = "[" + std::to_string(FocusedIndex) + "/" + std::to_string(NumberOfWindows) + "]";
        else
            Tag = "[" + std::to_string(NumberOfWindows) + "]";

        return true;
    }

    return false;
}

bool WindowsAreEqual(window_info *Window, window_info *Match)
{
    bool Result = false;

    if(!KWMScreen.ForceRefreshFocus && Window && Match)
    {
        if(Window->PID == Match->PID &&
           Window->WID == Match->WID &&
           Window->Layer == Match->Layer)
        {
            Result = true;
        }
    }

    return Result;
}

void AllowRoleForApplication(std::string Application, std::string Role)
{
    std::map<std::string, std::vector<CFTypeRef> >::iterator It = KWMTiling.AllowedWindowRoles.find(Application);
    if(It == KWMTiling.AllowedWindowRoles.end())
        KWMTiling.AllowedWindowRoles[Application] = std::vector<CFTypeRef>();

    CFStringRef RoleRef = CFStringCreateWithCString(NULL, Role.c_str(), kCFStringEncodingMacRoman);
    KWMTiling.AllowedWindowRoles[Application].push_back(RoleRef);
}

bool IsAppSpecificWindowRole(window_info *Window, CFTypeRef Role, CFTypeRef SubRole)
{
    std::map<std::string, std::vector<CFTypeRef> >::iterator It = KWMTiling.AllowedWindowRoles.find(Window->Owner);
    if(It != KWMTiling.AllowedWindowRoles.end())
    {
        std::vector<CFTypeRef> &WindowRoles = It->second;
        for(std::size_t RoleIndex = 0; RoleIndex < WindowRoles.size(); ++RoleIndex)
        {
            if(CFEqual(Role, WindowRoles[RoleIndex]) || CFEqual(SubRole, WindowRoles[RoleIndex]))
                return true;
        }
    }

    return false;
}

bool IsContextMenusAndSimilarVisible()
{
    bool Result = false;

    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        if((KWMTiling.WindowLst[WindowIndex].Owner != "Dock" ||
            KWMTiling.WindowLst[WindowIndex].Name != "Dock") &&
            KWMTiling.WindowLst[WindowIndex].Layer != 0)
        {
            Result = true;
            break;
        }
    }

    return Result;
}

bool FilterWindowList(screen_info *Screen)
{
    bool Result = true;
    std::vector<window_info> FilteredWindowLst;

    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        // Mission-Control mode is on and so we do not try to tile windows
        if(KWMTiling.WindowLst[WindowIndex].Owner == "Dock" &&
           KWMTiling.WindowLst[WindowIndex].Name == "")
               Result = false;

        if(KWMToggles.UseContextMenuFix)
            KWMTiling.NonZeroLayer = IsContextMenusAndSimilarVisible();

        CaptureApplication(&KWMTiling.WindowLst[WindowIndex]);
        if(KWMTiling.WindowLst[WindowIndex].Layer == 0 &&
           Screen == GetDisplayOfWindow(&KWMTiling.WindowLst[WindowIndex]))
        {
            CFTypeRef Role, SubRole;
            if(GetWindowRole(&KWMTiling.WindowLst[WindowIndex], &Role, &SubRole))
            {
                if((CFEqual(Role, kAXWindowRole) && CFEqual(SubRole, kAXStandardWindowSubrole)) ||
                   IsAppSpecificWindowRole(&KWMTiling.WindowLst[WindowIndex], Role, SubRole))
                        FilteredWindowLst.push_back(KWMTiling.WindowLst[WindowIndex]);
            }
        }
    }

    KWMTiling.WindowLst = FilteredWindowLst;
    return Result;
}

bool IsCursorInsideFocusedWindow()
{
    bool Result = false;

    if(KWMScreen.Current && DoesSpaceExistInMapOfScreen(KWMScreen.Current) && KWMFocus.Window)
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        tree_node *Node = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
        if(Node)
        {
            CGPoint Cursor = GetCursorPos();
            if((Cursor.x >= Node->Container.X) &&
                (Cursor.x <= Node->Container.X + Node->Container.Width) &&
                (Cursor.y >= Node->Container.Y) &&
                (Cursor.y <= Node->Container.Y + Node->Container.Height))
                    Result = true;
        }
    }

    return Result;
}

bool IsSpaceFloating(int SpaceID)
{
    bool Result = false;

    if(IsSpaceInitializedForScreen(KWMScreen.Current))
    {
        std::map<int, space_info>::iterator It = KWMScreen.Current->Space.find(KWMScreen.Current->ActiveSpace);
        if(It != KWMScreen.Current->Space.end())
            Result = KWMScreen.Current->Space[SpaceID].Mode == SpaceModeFloating;
    }

    return Result;
}

bool IsApplicationCapturedByScreen(window_info *Window)
{
    bool Result = false;

    std::map<std::string, int>::iterator It = KWMTiling.CapturedAppLst.find(Window->Owner);
    if(It != KWMTiling.CapturedAppLst.end())
        Result = true;

    return Result;
}

void CaptureApplication(window_info *Window)
{
    if(IsApplicationCapturedByScreen(Window))
    {
        int CapturedID = KWMTiling.CapturedAppLst[Window->Owner];
        screen_info *Screen = GetDisplayFromScreenID(CapturedID);
        if(Screen && Screen != GetDisplayOfWindow(Window))
        {
            MoveWindowToDisplay(Window, CapturedID, false);
            SetWindowFocus(Window);
            MoveCursorToCenterOfFocusedWindow();
        }
    }
}

bool IsApplicationFloating(window_info *Window)
{
    bool Result = false;

    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.FloatingAppLst.size(); ++WindowIndex)
    {
        if(Window->Owner == KWMTiling.FloatingAppLst[WindowIndex])
        {
            Result = true;
            break;
        }
    }

    return Result;
}

bool IsWindowFloating(int WindowID, int *Index)
{
    bool Result = false;

    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.FloatingWindowLst.size(); ++WindowIndex)
    {
        if(WindowID == KWMTiling.FloatingWindowLst[WindowIndex])
        {
            DEBUG("IsWindowFloating(): floating " << WindowID)
            Result = true;

            if(Index)
                *Index = WindowIndex;

            break;
        }
    }

    return Result;
}

bool IsAnyWindowBelowCursor()
{
    CGPoint Cursor = GetCursorPos();
    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        window_info *Window = &KWMTiling.WindowLst[WindowIndex];
        if(Cursor.x >= Window->X &&
           Cursor.x <= Window->X + Window->Width &&
           Cursor.y >= Window->Y &&
           Cursor.y <= Window->Y + Window->Height)
        {
            return true;
        }
    }

    return false;
}

bool IsWindowBelowCursor(window_info *Window)
{
    bool Result = false;

    if(Window)
    {
        CGPoint Cursor = GetCursorPos();
        if(Cursor.x >= Window->X && 
           Cursor.x <= Window->X + Window->Width &&
           Cursor.y >= Window->Y &&
           Cursor.y <= Window->Y + Window->Height)
        {
            Result = true;
        }
    }
        
    return Result;
}

bool IsSpaceInitializedForScreen(screen_info *Screen)
{
    std::map<int, space_info>::iterator It = Screen->Space.find(Screen->ActiveSpace);
    if(It == Screen->Space.end())
        return false;
    else
        return It->second.Initialized;
}

bool DoesSpaceExistInMapOfScreen(screen_info *Screen)
{
    std::map<int, space_info>::iterator It = Screen->Space.find(Screen->ActiveSpace);
    if(It == Screen->Space.end())
        return false;
    else
        return It->second.RootNode != NULL && It->second.Initialized;
}

bool IsWindowOnActiveSpace(int WindowID)
{
    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        if(WindowID == KWMTiling.WindowLst[WindowIndex].WID)
        {
            DEBUG("IsWindowOnActiveSpace() window found")
            return true;
        }
    }

    DEBUG("IsWindowOnActiveSpace() window was not found")
    return false;
}

bool IsSpaceTransitionInProgress()
{
    int CurrentSpace = CGSGetActiveSpace(CGSDefaultConnection);
    CFStringRef Identifier = CGSCopyManagedDisplayForSpace(CGSDefaultConnection, CurrentSpace);
    bool Result = CGSManagedDisplayIsAnimating(CGSDefaultConnection, (CFStringRef)Identifier);
    if(Result)
    {
        DEBUG("IsSpaceTransitionInProgress() Space transition detected")
        KWMScreen.UpdateSpace = true;
    }

    return Result;
}

bool IsSpaceSystemOrFullscreen()
{
    bool Result = false;

    if(IsSpaceInitializedForScreen(KWMScreen.Current))
    {
        bool Result = CGSSpaceGetType(CGSDefaultConnection, KWMScreen.Current->ActiveSpace) != CGSSpaceTypeUser;
        if(Result)
            DEBUG("IsSpaceSystemOrFullscreen() Space is not user created")
    }

    return Result;
}

bool FocusWindowOfOSX()
{
    int WindowID;
    if(GetWindowFocusedByOSX(&WindowID))
    {
        if(IsSpaceTransitionInProgress() ||
           IsSpaceSystemOrFullscreen() ||
           !IsSpaceInitializedForScreen(KWMScreen.Current))
                return false;

        for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
        {
            if(KWMTiling.WindowLst[WindowIndex].WID == WindowID)
            {
                SetWindowFocus(&KWMTiling.WindowLst[WindowIndex]);
                return true;
            }
        }
    }

    return false;
}

void FocusWindowBelowCursor()
{
    if(IsSpaceTransitionInProgress() ||
       IsSpaceSystemOrFullscreen() ||
       !IsSpaceInitializedForScreen(KWMScreen.Current) ||
       (KWMToggles.UseContextMenuFix && KWMTiling.NonZeroLayer))
           return;

    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        if(IsWindowBelowCursor(&KWMTiling.WindowLst[WindowIndex]))
        {
            if(WindowsAreEqual(KWMFocus.Window, &KWMTiling.WindowLst[WindowIndex]))
                KWMFocus.Cache = KWMTiling.WindowLst[WindowIndex];
            else
                SetWindowFocus(&KWMTiling.WindowLst[WindowIndex]);

            break;
        }
    }
}

void UpdateWindowTree()
{
    KWMScreen.OldScreenID = KWMScreen.Current->ID;
    KWMScreen.Current = GetDisplayOfMousePointer();
    if(!KWMScreen.Current)
        return;

    UpdateActiveWindowList(KWMScreen.Current);

    if(KWMToggles.EnableTilingMode &&
       !IsSpaceTransitionInProgress() &&
       !IsSpaceSystemOrFullscreen() &&
       FilterWindowList(KWMScreen.Current))
    {
        if(!IsSpaceFloating(KWMScreen.Current->ActiveSpace))
        {
            std::map<int, space_info>::iterator It = KWMScreen.Current->Space.find(KWMScreen.Current->ActiveSpace);
            std::vector<window_info*> WindowsOnDisplay = GetAllWindowsOnDisplay(KWMScreen.Current->ID);

            if(It == KWMScreen.Current->Space.end() && !WindowsOnDisplay.empty())
            {
                CreateWindowNodeTree(KWMScreen.Current, &WindowsOnDisplay);
            }
            else if(It != KWMScreen.Current->Space.end() && !WindowsOnDisplay.empty() &&
                    KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].RootNode == NULL)
            {
                CreateWindowNodeTree(KWMScreen.Current, &WindowsOnDisplay);
            }
            else if(It != KWMScreen.Current->Space.end() && !WindowsOnDisplay.empty() &&
                    KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].RootNode != NULL)
            {
                ShouldWindowNodeTreeUpdate(KWMScreen.Current);
            }
            else if(It != KWMScreen.Current->Space.end() && WindowsOnDisplay.empty())
            {
                DestroyNodeTree(KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].RootNode, 
                                KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].Mode);
                KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].RootNode = NULL;
            }
        }
    }
}

void UpdateActiveWindowList(screen_info *Screen)
{
    Screen->OldWindowListCount = KWMTiling.WindowLst.size();
    KWMTiling.WindowLst.clear();

    CFArrayRef OsxWindowLst = CGWindowListCopyWindowInfo(OsxWindowListOption, kCGNullWindowID);
    if(!OsxWindowLst)
        return;

    CFIndex OsxWindowCount = CFArrayGetCount(OsxWindowLst);
    for(CFIndex WindowIndex = 0; WindowIndex < OsxWindowCount; ++WindowIndex)
    {
        CFDictionaryRef Elem = (CFDictionaryRef)CFArrayGetValueAtIndex(OsxWindowLst, WindowIndex);
        KWMTiling.WindowLst.push_back(window_info());
        CFDictionaryApplyFunction(Elem, GetWindowInfo, NULL);
    }
    CFRelease(OsxWindowLst);

    bool WindowBelowCursor = IsAnyWindowBelowCursor();
    KWMScreen.ForceRefreshFocus = true;
    KWMScreen.PrevSpace = Screen->ActiveSpace;

    if(Screen->ForceContainerUpdate)
    {
        space_info *Space = &Screen->Space[Screen->ActiveSpace];
        ApplyNodeContainer(Space->RootNode, Space->Mode);
        Screen->ForceContainerUpdate = false;
    }

    if(KWMScreen.OldScreenID != Screen->ID)
    {
        DEBUG("UpdateActiveWindowList() Active Display Changed")
        GiveFocusToScreen(Screen->ID, NULL, true);
    }
    else if(KWMScreen.UpdateSpace)
    {
        Screen->ActiveSpace = CGSGetActiveSpace(CGSDefaultConnection);
        if(KWMScreen.PrevSpace != Screen->ActiveSpace)
        {
            DEBUG("UpdateActiveWindowList() Space transition ended " << KWMScreen.PrevSpace << " -> " << Screen->ActiveSpace)
            if(WindowBelowCursor && KWMMode.Focus != FocusModeDisabled)
                FocusWindowBelowCursor();
            else if(FocusWindowOfOSX())
                MoveCursorToCenterOfFocusedWindow();
        }
        KWMScreen.UpdateSpace = false;
    }

    KWMScreen.ForceRefreshFocus = false;
}

void CreateWindowNodeTree(screen_info *Screen, std::vector<window_info*> *Windows)
{
    for(std::size_t WindowIndex = 0; WindowIndex < Windows->size(); ++WindowIndex)
    {
        if(Screen != GetDisplayOfWindow((*Windows)[WindowIndex]))
            return;
    }

    space_info *Space;
    DEBUG("CreateWindowNodeTree() Create Tree")
    if(!IsSpaceInitializedForScreen(Screen))
    {
        Space = &Screen->Space[Screen->ActiveSpace];
        DEBUG("CreateWindowNodeTree() Create Space " << Screen->ActiveSpace)

        Space->Mode = GetSpaceModeOfDisplay(Screen->ID);
        if(Space->Mode == SpaceModeDefault)
            Space->Mode = KWMMode.Space;

        Space->Initialized = true;
        Space->Offset = Screen->Offset;
        Space->RootNode = CreateTreeFromWindowIDList(Screen, Windows);
    }
    else
    {
        Space = &Screen->Space[Screen->ActiveSpace];
        Space->RootNode = CreateTreeFromWindowIDList(Screen, Windows);
        if(Space->RootNode)
        {
            if(Space->Mode == SpaceModeBSP)
            {
                SetRootNodeContainer(Screen, Space->RootNode);
                CreateNodeContainers(Screen, Space->RootNode, true);
            }
            else if(Space->Mode == SpaceModeMonocle)
            {
                tree_node *CurrentNode = Space->RootNode;
                while(CurrentNode)
                {
                    SetRootNodeContainer(Screen, CurrentNode);
                    CurrentNode = CurrentNode->RightChild;
                }
            }
        }
    }

    if(Space->RootNode)
    {
        ApplyNodeContainer(Space->RootNode, Space->Mode);
        FocusWindowBelowCursor();
    }
}

void ShouldWindowNodeTreeUpdate(screen_info *Screen)
{
    if(Screen->ActiveSpace == -1 || KWMScreen.PrevSpace != Screen->ActiveSpace || Screen->OldWindowListCount == -1)
        return;

    space_info *Space = &Screen->Space[Screen->ActiveSpace];
    if(Space->Mode == SpaceModeBSP)
        ShouldBSPTreeUpdate(Screen, Space);
    else if(Space->Mode == SpaceModeMonocle)
        ShouldMonocleTreeUpdate(Screen, Space);
}

void ShouldBSPTreeUpdate(screen_info *Screen, space_info *Space)
{
    if(KWMTiling.WindowLst.size() > Screen->OldWindowListCount)
    {
        DEBUG("ShouldBSPTreeUpdate() Add Window")
        for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
        {
            if(GetNodeFromWindowID(Space->RootNode, KWMTiling.WindowLst[WindowIndex].WID, Space->Mode) == NULL)
            {
                if(!IsApplicationFloating(&KWMTiling.WindowLst[WindowIndex]) &&
                   !IsWindowFloating(KWMTiling.WindowLst[WindowIndex].WID, NULL))
                {
                    tree_node *Insert = GetFirstPseudoLeafNode(Space->RootNode);
                    if(Insert)
                    {
                        Insert->WindowID = KWMTiling.WindowLst[WindowIndex].WID;
                        ApplyNodeContainer(Insert, SpaceModeBSP);
                    }
                    else
                    {
                        AddWindowToBSPTree(Screen, KWMTiling.WindowLst[WindowIndex].WID);
                    }

                    SetWindowFocus(&KWMTiling.WindowLst[WindowIndex]);
                    MoveCursorToCenterOfFocusedWindow();
                }
            }
        }
    }
    else if(KWMTiling.WindowLst.size() < Screen->OldWindowListCount)
    {
        DEBUG("ShouldBSPTreeUpdate() Remove Window")
        std::vector<int> WindowIDsInTree;

        tree_node *CurrentNode = GetFirstLeafNode(Space->RootNode);
        while(CurrentNode)
        {
            WindowIDsInTree.push_back(CurrentNode->WindowID);
            CurrentNode = GetNearestNodeToTheRight(CurrentNode, SpaceModeBSP);
        }

        for(std::size_t IDIndex = 0; IDIndex < WindowIDsInTree.size(); ++IDIndex)
        {
            bool Found = false;
            for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
            {
                if(KWMTiling.WindowLst[WindowIndex].WID == WindowIDsInTree[IDIndex])
                {
                    Found = true;
                    break;
                }
            }

            if(!Found)
                RemoveWindowFromBSPTree(Screen, WindowIDsInTree[IDIndex], false);
        }
    }
}

void AddWindowToBSPTree(screen_info *Screen, int WindowID)
{
    if(!Screen || !DoesSpaceExistInMapOfScreen(Screen))
        return;

    space_info *Space = &Screen->Space[Screen->ActiveSpace];
    tree_node *RootNode = Space->RootNode;
    tree_node *CurrentNode = RootNode;

    DEBUG("AddWindowToBSPTree() Create pair of leafs")
    bool UseFocusedContainer = KWMFocus.Window &&
                               IsWindowOnActiveSpace(KWMFocus.Window->WID) &&
                               KWMFocus.Window->WID != WindowID;

    bool DoNotUseMarkedContainer = IsWindowFloating(KWMScreen.MarkedWindow, NULL) ||
                                   (KWMScreen.MarkedWindow == WindowID);

    if(KWMScreen.MarkedWindow == -1 && UseFocusedContainer)
    {
        CurrentNode = GetNodeFromWindowID(RootNode, KWMFocus.Window->WID, Space->Mode);
    }
    else if(DoNotUseMarkedContainer || (KWMScreen.MarkedWindow == -1 && !UseFocusedContainer))
    {
        while(!IsLeafNode(CurrentNode))
        {
            if(!IsLeafNode(CurrentNode->LeftChild) && IsLeafNode(CurrentNode->RightChild))
                CurrentNode = CurrentNode->RightChild;
            else
                CurrentNode = CurrentNode->LeftChild;
        }
    }
    else
    {
        CurrentNode = GetNodeFromWindowID(RootNode, KWMScreen.MarkedWindow, Space->Mode);
        KWMScreen.MarkedWindow = -1;
    }

    if(CurrentNode)
    {
        int SplitMode = KWMScreen.SplitMode == -1 ? GetOptimalSplitMode(CurrentNode) : KWMScreen.SplitMode;
        CreateLeafNodePair(Screen, CurrentNode, CurrentNode->WindowID, WindowID, SplitMode);
        ApplyNodeContainer(CurrentNode, Space->Mode);
    }
}

void AddWindowToBSPTree()
{
    if(!KWMScreen.Current)
        return;

    AddWindowToBSPTree(KWMScreen.Current, KWMFocus.Window->WID);
}

void RemoveWindowFromBSPTree(screen_info *Screen, int WindowID, bool Center)
{
    if(!DoesSpaceExistInMapOfScreen(Screen))
        return;

    space_info *Space = &Screen->Space[Screen->ActiveSpace];
    tree_node *WindowNode = GetNodeFromWindowID(Space->RootNode, WindowID, Space->Mode);
    if(!WindowNode)
        return;

    tree_node *Parent = WindowNode->Parent;
    if(Parent && Parent->LeftChild && Parent->RightChild)
    {
        tree_node *AccessChild = IsRightChild(WindowNode) ? Parent->LeftChild : Parent->RightChild;
        tree_node *NewFocusNode = NULL;
        Parent->LeftChild = NULL;
        Parent->RightChild = NULL;

        DEBUG("RemoveWindowFromBSPTree()")
        Parent->WindowID = AccessChild->WindowID;
        if(AccessChild->LeftChild && AccessChild->RightChild)
        {
            Parent->LeftChild = AccessChild->LeftChild;
            Parent->LeftChild->Parent = Parent;

            Parent->RightChild = AccessChild->RightChild;
            Parent->RightChild->Parent = Parent;

            CreateNodeContainers(Screen, Parent, true);
            NewFocusNode = IsLeafNode(Parent->LeftChild) ? Parent->LeftChild : Parent->RightChild;
        }

        free(AccessChild);
        free(WindowNode);
        ApplyNodeContainer(Parent, Space->Mode);

        if(Center)
        {
            CenterWindow(Screen, KWMFocus.Window);
        }
        else
        {
            if(!NewFocusNode)
                NewFocusNode = Parent;

            SetWindowFocusByNode(NewFocusNode);
        }
    }
    else if(!Parent)
    {
        DEBUG("RemoveWindowFromBSPTree()")

        free(WindowNode);
        Screen->Space[Screen->ActiveSpace].RootNode = NULL;
        if(Center)
            CenterWindow(Screen, KWMFocus.Window);
    }
}

void RemoveWindowFromBSPTree()
{
    if(!KWMScreen.Current)
        return;

    RemoveWindowFromBSPTree(KWMScreen.Current, KWMFocus.Window->WID, true);
}

void ShouldMonocleTreeUpdate(screen_info *Screen, space_info *Space)
{
    if(KWMTiling.WindowLst.size() > Screen->OldWindowListCount)
    {
        DEBUG("ShouldMonocleTreeUpdate() Add Window")
        for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
        {
            if(GetNodeFromWindowID(Space->RootNode, KWMTiling.WindowLst[WindowIndex].WID, Space->Mode) == NULL)
            {
                if(!IsApplicationFloating(&KWMTiling.WindowLst[WindowIndex]))
                {
                    tree_node *CurrentNode = GetLastLeafNode(Space->RootNode);
                    tree_node *NewNode = CreateRootNode();
                    SetRootNodeContainer(Screen, NewNode);

                    NewNode->WindowID = KWMTiling.WindowLst[WindowIndex].WID;
                    CurrentNode->RightChild = NewNode;
                    NewNode->LeftChild = CurrentNode;

                    ApplyNodeContainer(NewNode, SpaceModeMonocle);
                    SetWindowFocus(&KWMTiling.WindowLst[WindowIndex]);
                }
            }
        }
    }
    else if(KWMTiling.WindowLst.size() < Screen->OldWindowListCount)
    {
        DEBUG("ShouldMonocleTreeUpdate() Remove Window")
        std::vector<int> WindowIDsInTree;

        tree_node *CurrentNode = Space->RootNode;
        while(CurrentNode)
        {
            WindowIDsInTree.push_back(CurrentNode->WindowID);
            CurrentNode = GetNearestNodeToTheRight(CurrentNode, SpaceModeMonocle);
        }

        if(WindowIDsInTree.size() >= 2)
        {
            for(std::size_t IDIndex = 0; IDIndex < WindowIDsInTree.size(); ++IDIndex)
            {
                bool Found = false;
                for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
                {
                    if(KWMTiling.WindowLst[WindowIndex].WID == WindowIDsInTree[IDIndex])
                    {
                        Found = true;
                        break;
                    }
                }

                if(!Found)
                {
                    tree_node *WindowNode = GetNodeFromWindowID(Space->RootNode, WindowIDsInTree[IDIndex], SpaceModeMonocle);
                    if(!WindowNode)
                        return;

                    tree_node *NewFocusNode = NULL;
                    tree_node *Prev = WindowNode->LeftChild;
                    tree_node *Next = WindowNode->RightChild;

                    if(Prev)
                    {
                        Prev->RightChild = Next;
                        NewFocusNode = Prev;
                    }

                    if(Next)
                        Next->LeftChild = Prev;

                    if(WindowNode == Space->RootNode)
                    {
                        Space->RootNode = Next;
                        NewFocusNode = Next;
                    }

                    free(WindowNode);
                    SetWindowFocusByNode(NewFocusNode);
                }
            }
        }
        else
        {
            tree_node *WindowNode = GetNodeFromWindowID(Space->RootNode, WindowIDsInTree[0], SpaceModeMonocle);
            if(!WindowNode)
                return;

            free(WindowNode);
            Space->RootNode = NULL;
        }
    }
}

void AddWindowToTreeOfUnfocusedMonitor(screen_info *Screen, window_info *Window)
{
    if(!Screen || !Window || Screen == GetDisplayOfWindow(Window))
        return;

    if(!IsSpaceInitializedForScreen(Screen))
    {
        CenterWindow(Screen, Window);
        Screen->ForceContainerUpdate = true;
        return;
    }

    space_info *Space = &Screen->Space[Screen->ActiveSpace];
    if(Space->RootNode)
    {
        if(Space->Mode == SpaceModeBSP)
        {
            DEBUG("AddWindowToTreeOfUnfocusedMonitor() BSP Space")
            tree_node *RootNode = Space->RootNode;
            tree_node *CurrentNode = RootNode;

            while(!IsLeafNode(CurrentNode))
            {
                if(!IsLeafNode(CurrentNode->LeftChild) && IsLeafNode(CurrentNode->RightChild))
                    CurrentNode = CurrentNode->RightChild;
                else
                    CurrentNode = CurrentNode->LeftChild;
            }

            int SplitMode = KWMScreen.SplitMode == -1 ? GetOptimalSplitMode(CurrentNode) : KWMScreen.SplitMode;
            CreateLeafNodePair(Screen, CurrentNode, CurrentNode->WindowID, Window->WID, SplitMode);
            ResizeWindowToContainerSize(CurrentNode->RightChild);
            Screen->ForceContainerUpdate = true;
        }
        else if(Space->Mode == SpaceModeMonocle)
        {
            DEBUG("AddWindowToTreeOfUnfocusedMonitor() Monocle Space")
            tree_node *CurrentNode = GetLastLeafNode(Space->RootNode);
            tree_node *NewNode = CreateRootNode();
            SetRootNodeContainer(Screen, NewNode);

            NewNode->WindowID = Window->WID;
            CurrentNode->RightChild = NewNode;
            NewNode->LeftChild = CurrentNode;
            ResizeWindowToContainerSize(NewNode);
        }
    }
    else
    {
        CenterWindow(Screen, Window);
        Screen->ForceContainerUpdate = true;
    }
}

void FloatFocusedSpace()
{
    if(KWMScreen.Current &&
       IsSpaceInitializedForScreen(KWMScreen.Current) &&
       KWMToggles.EnableTilingMode &&
       !IsSpaceTransitionInProgress() &&
       !IsSpaceSystemOrFullscreen() &&
       FilterWindowList(KWMScreen.Current))
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        DestroyNodeTree(Space->RootNode, Space->Mode);
        Space->RootNode = NULL;
        Space->Mode = SpaceModeFloating;
    }
}

void TileFocusedSpace(space_tiling_option Mode)
{
    if(KWMScreen.Current &&
       IsSpaceInitializedForScreen(KWMScreen.Current) &&
       KWMToggles.EnableTilingMode &&
       !IsSpaceTransitionInProgress() &&
       !IsSpaceSystemOrFullscreen() &&
       FilterWindowList(KWMScreen.Current))
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        if(Space->Mode == Mode)
            return;

        DestroyNodeTree(Space->RootNode, Space->Mode);
        Space->RootNode = NULL;

        Space->Mode = Mode;
        std::vector<window_info*> WindowsOnDisplay = GetAllWindowsOnDisplay(KWMScreen.Current->ID);
        CreateWindowNodeTree(KWMScreen.Current, &WindowsOnDisplay);
    }
}

void ToggleFocusedSpaceFloating()
{
    if(KWMScreen.Current && IsSpaceInitializedForScreen(KWMScreen.Current))
    {
        if(!IsSpaceFloating(KWMScreen.Current->ActiveSpace))
            FloatFocusedSpace();
        else
            TileFocusedSpace(SpaceModeBSP);
    }
}

void ToggleFocusedWindowFloating()
{
    if(KWMFocus.Window &&
       IsWindowOnActiveSpace(KWMFocus.Window->WID) &&
       KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace].Mode == SpaceModeBSP)
    {
        int WindowIndex;
        if(IsWindowFloating(KWMFocus.Window->WID, &WindowIndex))
        {
            KWMTiling.FloatingWindowLst.erase(KWMTiling.FloatingWindowLst.begin() + WindowIndex);
            AddWindowToBSPTree();
        }
        else
        {
            KWMTiling.FloatingWindowLst.push_back(KWMFocus.Window->WID);
            RemoveWindowFromBSPTree();
        }
    }
}

void ToggleFocusedWindowParentContainer()
{
    if(!KWMFocus.Window || !KWMScreen.Current || !DoesSpaceExistInMapOfScreen(KWMScreen.Current))
        return;

    space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
    if(Space->Mode != SpaceModeBSP)
        return;

    tree_node *Node = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
    if(Node && Node->Parent)
    {
        if(IsLeafNode(Node) && Node->Parent->WindowID == -1)
        {
            DEBUG("ToggleFocusedWindowParentContainer() Set Parent Container")
            Node->Parent->WindowID = Node->WindowID;
            ResizeWindowToContainerSize(Node->Parent);
        }
        else
        {
            DEBUG("ToggleFocusedWindowParentContainer() Restore Window Container")
            Node->Parent->WindowID = -1;
            ResizeWindowToContainerSize(Node);
        }
    }
}

void ToggleFocusedWindowFullscreen()
{
    if(!KWMFocus.Window || !KWMScreen.Current || !DoesSpaceExistInMapOfScreen(KWMScreen.Current))
        return;

    space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
    if(Space->Mode == SpaceModeBSP && !IsLeafNode(Space->RootNode))
    {
        tree_node *Node;
        if(Space->RootNode->WindowID == -1)
        {
            Node = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
            if(Node)
            {
                DEBUG("ToggleFocusedWindowFullscreen() Set fullscreen")
                Space->RootNode->WindowID = Node->WindowID;
                ResizeWindowToContainerSize(Space->RootNode);
            }
        }
        else
        {
            DEBUG("ToggleFocusedWindowFullscreen() Restore old size")
            Space->RootNode->WindowID = -1;

            Node = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
            if(Node)
                ResizeWindowToContainerSize(Node);
        }
    }
}

void SwapFocusedWindowWithMarked()
{
    if(!KWMFocus.Window || KWMScreen.MarkedWindow == KWMFocus.Window->WID || KWMScreen.MarkedWindow == -1)
        return;

    if(KWMScreen.Current && DoesSpaceExistInMapOfScreen(KWMScreen.Current))
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        tree_node *FocusedWindowNode = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
        if(FocusedWindowNode)
        {
            tree_node *NewFocusNode = GetNodeFromWindowID(Space->RootNode, KWMScreen.MarkedWindow, Space->Mode);
            if(NewFocusNode)
            {
                SwapNodeWindowIDs(FocusedWindowNode, NewFocusNode);
                MoveCursorToCenterOfFocusedWindow();
            }
        }
    }

    KWMScreen.MarkedWindow = -1;
}

void SwapFocusedWindowWithNearest(int Shift)
{
    if(!KWMFocus.Window || !KWMScreen.Current || !DoesSpaceExistInMapOfScreen(KWMScreen.Current))
        return;

    space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
    tree_node *FocusedWindowNode = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
    if(FocusedWindowNode)
    {
        tree_node *NewFocusNode = NULL;;

        if(Shift == 1)
            NewFocusNode = GetNearestNodeToTheRight(FocusedWindowNode, Space->Mode);
        else if(Shift == -1)
            NewFocusNode = GetNearestNodeToTheLeft(FocusedWindowNode, Space->Mode);

        if(NewFocusNode)
        {
            SwapNodeWindowIDs(FocusedWindowNode, NewFocusNode);
            MoveCursorToCenterOfFocusedWindow();
        }
    }
}

void ShiftWindowFocus(int Shift)
{
    if(!KWMFocus.Window || !KWMScreen.Current || !DoesSpaceExistInMapOfScreen(KWMScreen.Current))
        return;

    space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
    tree_node *FocusedWindowNode = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
    if(FocusedWindowNode)
    {
        tree_node *FocusNode = NULL;

        if(Shift == 1)
        {
            FocusNode = GetNearestNodeToTheRight(FocusedWindowNode, Space->Mode);
            if(KWMMode.Cycle == CycleModeScreen && !FocusNode)
            {
                FocusNode = GetFirstLeafNode(Space->RootNode);
            }
            else if(KWMMode.Cycle == CycleModeAll && !FocusNode)
            {
                int ScreenIndex = GetIndexOfNextScreen();
                screen_info *Screen = GetDisplayFromScreenID(ScreenIndex);
                FocusNode = GetFirstLeafNode(Screen->Space[Screen->ActiveSpace].RootNode);
                if(FocusNode)
                {
                    GiveFocusToScreen(ScreenIndex, FocusNode, false);
                    return;
                }
            }
        }
        else if(Shift == -1)
        {
            FocusNode = GetNearestNodeToTheLeft(FocusedWindowNode, Space->Mode);
            if(KWMMode.Cycle == CycleModeScreen && !FocusNode)
            {
                FocusNode = GetLastLeafNode(Space->RootNode);
            }
            else if(KWMMode.Cycle == CycleModeAll && !FocusNode)
            {
                int ScreenIndex = GetIndexOfPrevScreen();
                screen_info *Screen = GetDisplayFromScreenID(ScreenIndex);
                FocusNode = GetLastLeafNode(Screen->Space[Screen->ActiveSpace].RootNode);
                if(FocusNode)
                {
                    GiveFocusToScreen(ScreenIndex, FocusNode, false);
                    return;
                }
            }
        }

        SetWindowFocusByNode(FocusNode);
    }
}

void MoveCursorToCenterOfWindow(window_info *Window)
{
    if(Window)
        CGWarpMouseCursorPosition(CGPointMake(Window->X + Window->Width / 2,
                                              Window->Y + Window->Height / 2));
}

void MoveCursorToCenterOfFocusedWindow()
{
    if(KWMToggles.UseMouseFollowsFocus)
        MoveCursorToCenterOfWindow(KWMFocus.Window);
}

void MarkWindowContainer()
{
    if(KWMFocus.Window)
    {
        DEBUG("MarkWindowContainer() Marked " << KWMFocus.Window->Name)
        KWMScreen.MarkedWindow = KWMFocus.Window->WID;
    }
}

void CloseWindowByRef(AXUIElementRef WindowRef)
{
    AXUIElementRef ActionClose;
    AXUIElementCopyAttributeValue(WindowRef, kAXCloseButtonAttribute, (CFTypeRef*)&ActionClose);
    AXUIElementPerformAction(ActionClose, kAXPressAction);
}

void CloseWindow(window_info *Window)
{
    AXUIElementRef WindowRef;
    if(GetWindowRef(Window, &WindowRef))
        CloseWindowByRef(WindowRef);
}

void SetWindowRefFocus(AXUIElementRef WindowRef, window_info *Window)
{
    ProcessSerialNumber NewPSN;
    GetProcessForPID(Window->PID, &NewPSN);

    KWMFocus.PSN = NewPSN;
    KWMFocus.Cache = *Window;
    KWMFocus.Window = &KWMFocus.Cache;

    AXUIElementSetAttributeValue(WindowRef, kAXMainAttribute, kCFBooleanTrue);
    AXUIElementSetAttributeValue(WindowRef, kAXFocusedAttribute, kCFBooleanTrue);
    AXUIElementPerformAction(WindowRef, kAXRaiseAction);

    if(KWMMode.Focus != FocusModeAutofocus)
        SetFrontProcessWithOptions(&KWMFocus.PSN, kSetFrontProcessFrontWindowOnly);

    DEBUG("SetWindowRefFocus() Focused Window: " << KWMFocus.Window->Name)
}

void SetWindowFocus(window_info *Window)
{
    AXUIElementRef WindowRef;
    if(GetWindowRef(Window, &WindowRef))
        SetWindowRefFocus(WindowRef, Window);
}

void SetWindowFocusByNode(tree_node *Node)
{
    if(Node)
    {
        window_info *Window = GetWindowByID(Node->WindowID);
        if(Window)
        {
            DEBUG("SetWindowFocusByNode()")
            KWMScreen.ForceRefreshFocus = true;
            SetWindowFocus(Window);
            MoveCursorToCenterOfFocusedWindow();
            KWMScreen.ForceRefreshFocus = false;
        }
    }
}

void SetWindowDimensions(AXUIElementRef WindowRef, window_info *Window, int X, int Y, int Width, int Height)
{
    CGPoint WindowPos = CGPointMake(X, Y);
    CFTypeRef NewWindowPos = (CFTypeRef)AXValueCreate(kAXValueCGPointType, (const void*)&WindowPos);

    CGSize WindowSize = CGSizeMake(Width, Height);
    CFTypeRef NewWindowSize = (CFTypeRef)AXValueCreate(kAXValueCGSizeType, (void*)&WindowSize);

    AXUIElementSetAttributeValue(WindowRef, kAXPositionAttribute, NewWindowPos);
    AXUIElementSetAttributeValue(WindowRef, kAXSizeAttribute, NewWindowSize);

    Window->X = X;
    Window->Y = Y;
    Window->Width = Width;
    Window->Height = Height;

    DEBUG("SetWindowDimensions() Window " << Window->Name << ": " << Window->X << "," << Window->Y)

    if(NewWindowPos != NULL)
        CFRelease(NewWindowPos);
    if(NewWindowSize != NULL)
        CFRelease(NewWindowSize);
}

void CenterWindow(screen_info *Screen, window_info *Window)
{
    AXUIElementRef WindowRef;
    if(GetWindowRef(Window, &WindowRef))
    {
        int NewX = Screen->X + Screen->Width / 4;
        int NewY = Screen->Y + Screen->Height / 4;
        int NewWidth = Screen->Width / 2;
        int NewHeight = Screen->Height / 2;
        SetWindowDimensions(WindowRef, Window, NewX, NewY, NewWidth, NewHeight);
    }
}

void ModifyContainerSplitRatio(double Offset)
{
    if(KWMScreen.Current && DoesSpaceExistInMapOfScreen(KWMScreen.Current))
    {
        space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
        tree_node *Root = Space->RootNode;
        if(IsLeafNode(Root) || Root->WindowID != -1)
            return;

        tree_node *Node = GetNodeFromWindowID(Root, KWMFocus.Window->WID, Space->Mode);
        if(Node && Node->Parent)
        {
            if(Node->Parent->SplitRatio + Offset > 0.0 &&
               Node->Parent->SplitRatio + Offset < 1.0)
            {
                Node->Parent->SplitRatio += Offset;
                ResizeNodeContainer(KWMScreen.Current, Node->Parent);
                ApplyNodeContainer(Node->Parent, Space->Mode);
            }
        }
    }
}

void ResizeWindowToContainerSize(tree_node *Node)
{
    window_info *Window = GetWindowByID(Node->WindowID);

    if(Window)
    {
        AXUIElementRef WindowRef;
        if(GetWindowRef(Window, &WindowRef))
        {
            SetWindowDimensions(WindowRef, Window,
                        Node->Container.X, Node->Container.Y, 
                        Node->Container.Width, Node->Container.Height);

            if(WindowsAreEqual(Window, KWMFocus.Window))
                KWMFocus.Cache = *Window;
        }
        else
        {
            DEBUG("GetWindowRef() Failed for window " << Window->Name)
        }
    }
}

void ResizeWindowToContainerSize()
{
    if(KWMFocus.Window)
    {
        if(KWMScreen.Current && DoesSpaceExistInMapOfScreen(KWMScreen.Current))
        {
            space_info *Space = &KWMScreen.Current->Space[KWMScreen.Current->ActiveSpace];
            tree_node *Node = GetNodeFromWindowID(Space->RootNode, KWMFocus.Window->WID, Space->Mode);
            if(Node)
                ResizeWindowToContainerSize(Node);
        }
    }
}

CGPoint GetCursorPos()
{
    CGEventRef Event = CGEventCreate(NULL);
    CGPoint Cursor = CGEventGetLocation(Event);
    CFRelease(Event);

    return Cursor;
}

std::string GetWindowTitle(AXUIElementRef WindowRef)
{
    CFStringRef Temp;
    std::string WindowTitle;

    AXUIElementCopyAttributeValue(WindowRef, kAXTitleAttribute, (CFTypeRef*)&Temp);
    if(CFStringGetCStringPtr(Temp, kCFStringEncodingMacRoman))
        WindowTitle = CFStringGetCStringPtr(Temp, kCFStringEncodingMacRoman);

    if(Temp != NULL)
        CFRelease(Temp);

    return WindowTitle;
}

CGSize GetWindowSize(AXUIElementRef WindowRef)
{
    AXValueRef Temp;
    CGSize WindowSize;

    AXUIElementCopyAttributeValue(WindowRef, kAXSizeAttribute, (CFTypeRef*)&Temp);
    AXValueGetValue(Temp, kAXValueCGSizeType, &WindowSize);

    if(Temp != NULL)
        CFRelease(Temp);

    return WindowSize;
}

CGPoint GetWindowPos(AXUIElementRef WindowRef)
{
    AXValueRef Temp;
    CGPoint WindowPos;

    AXUIElementCopyAttributeValue(WindowRef, kAXPositionAttribute, (CFTypeRef*)&Temp);
    AXValueGetValue(Temp, kAXValueCGPointType, &WindowPos);

    if(Temp != NULL)
        CFRelease(Temp);

    return WindowPos;
}

window_info *GetWindowByID(int WindowID)
{
    for(std::size_t WindowIndex = 0; WindowIndex < KWMTiling.WindowLst.size(); ++WindowIndex)
    {
        if(KWMTiling.WindowLst[WindowIndex].WID == WindowID)
            return &KWMTiling.WindowLst[WindowIndex];
    }

    return NULL;
}

bool GetWindowRole(window_info *Window, CFTypeRef *Role, CFTypeRef *SubRole)
{
    bool Result = false;

    std::map<int, window_role>::iterator It = KWMCache.WindowRole.find(Window->WID);
    if(It != KWMCache.WindowRole.end())
    {
        *Role = KWMCache.WindowRole[Window->WID].Role;
        *SubRole = KWMCache.WindowRole[Window->WID].SubRole;
        Result = true;
    }
    else
    {
        AXUIElementRef WindowRef;
        if(GetWindowRef(Window, &WindowRef))
        {
            AXUIElementCopyAttributeValue(WindowRef, kAXRoleAttribute, (CFTypeRef *)Role);
            AXUIElementCopyAttributeValue(WindowRef, kAXSubroleAttribute, (CFTypeRef *)SubRole);
            window_role RoleEntry = { *Role, *SubRole };
            KWMCache.WindowRole[Window->WID] = RoleEntry;
            Result = true;
        }
    }

    return Result;
}

bool GetWindowRef(window_info *Window, AXUIElementRef *WindowRef)
{
    if(GetWindowRefFromCache(Window, WindowRef))
        return true;

    AXUIElementRef App = AXUIElementCreateApplication(Window->PID);
    if(!App)
    {
        DEBUG("GetWindowRef() Failed to get App for: " << Window->Name)
        return false;
    }

    CFArrayRef AppWindowLst;
    AXUIElementCopyAttributeValue(App, kAXWindowsAttribute, (CFTypeRef*)&AppWindowLst);
    if(!AppWindowLst)
    {
        DEBUG("GetWindowRef() Could not get AppWindowLst")
        return false;
    }

    bool Found = false;
    FreeWindowRefCache(Window->PID);
    CFIndex AppWindowCount = CFArrayGetCount(AppWindowLst);
    for(CFIndex WindowIndex = 0; WindowIndex < AppWindowCount; ++WindowIndex)
    {
        AXUIElementRef AppWindowRef = (AXUIElementRef)CFArrayGetValueAtIndex(AppWindowLst, WindowIndex);
        if(AppWindowRef != NULL)
        {
            KWMCache.WindowRefs[Window->PID].push_back(AppWindowRef);
            if(!Found)
            {
                int AppWindowRefWID = -1;
                _AXUIElementGetWindow(AppWindowRef, &AppWindowRefWID);
                if(AppWindowRefWID == Window->WID)
                {
                    *WindowRef = AppWindowRef;
                    Found = true;
                }
            }
        }
    }

    CFRelease(App);
    return Found;
}

bool IsApplicationInCache(int PID, std::vector<AXUIElementRef> *Elements)
{
    bool Result = false;
    std::map<int, std::vector<AXUIElementRef> >::iterator It = KWMCache.WindowRefs.find(PID);

    if(It != KWMCache.WindowRefs.end())
    {
        *Elements = It->second;
        Result = true;
    }

    return Result;
}

bool GetWindowRefFromCache(window_info *Window, AXUIElementRef *WindowRef)
{
    bool Result = false;
    std::vector<AXUIElementRef> Elements;
    bool IsCached = IsApplicationInCache(Window->PID, &Elements);

    if(IsCached)
    {
        for(std::size_t ElementIndex = 0; ElementIndex < Elements.size(); ++ElementIndex)
        {
            int AppWindowRefWID = -1;
            _AXUIElementGetWindow(Elements[ElementIndex], &AppWindowRefWID);
            if(AppWindowRefWID == Window->WID)
            {
                *WindowRef = Elements[ElementIndex];
                Result = true;
                break;
            }
        }
    }

    if(!IsCached)
        KWMCache.WindowRefs[Window->PID] = std::vector<AXUIElementRef>();

    return Result;
}

void FreeWindowRefCache(int PID)
{
    std::map<int, std::vector<AXUIElementRef> >::iterator It = KWMCache.WindowRefs.find(PID);

    if(It != KWMCache.WindowRefs.end())
    {
        int NumElements = KWMCache.WindowRefs[PID].size();
        for(int RefIndex = 0; RefIndex < NumElements; ++RefIndex)
            CFRelease(KWMCache.WindowRefs[PID][RefIndex]);

        KWMCache.WindowRefs[PID].clear();
    }
}

bool GetWindowFocusedByOSX(int *WindowWID)
{
    static AXUIElementRef SystemWideElement = AXUIElementCreateSystemWide();

    AXUIElementRef App;
    AXUIElementCopyAttributeValue(SystemWideElement, kAXFocusedApplicationAttribute, (CFTypeRef*)&App);
    if(App)
    {
        AXUIElementRef WindowRef;
        AXError Error = AXUIElementCopyAttributeValue(App, kAXFocusedWindowAttribute, (CFTypeRef*)&WindowRef);
        CFRelease(App);

        if (Error == kAXErrorSuccess)
        {
            _AXUIElementGetWindow(WindowRef, WindowWID);
            CFRelease(WindowRef);
            return true;
        }
    }

    return false;
}

void GetWindowInfo(const void *Key, const void *Value, void *Context)
{
    CFStringRef K = (CFStringRef)Key;
    std::string KeyStr = CFStringGetCStringPtr(K, kCFStringEncodingMacRoman);

    CFTypeID ID = CFGetTypeID(Value);
    if(ID == CFStringGetTypeID())
    {
        CFStringRef V = (CFStringRef)Value;
        if(CFStringGetCStringPtr(V, kCFStringEncodingMacRoman))
        {
            std::string ValueStr = CFStringGetCStringPtr(V, kCFStringEncodingMacRoman);
            if(KeyStr == "kCGWindowName")
                KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Name = ValueStr;
            else if(KeyStr == "kCGWindowOwnerName")
                KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Owner = ValueStr;
        }
    }
    else if(ID == CFNumberGetTypeID())
    {
        int MyInt;
        CFNumberRef V = (CFNumberRef)Value;
        CFNumberGetValue(V, kCFNumberSInt64Type, &MyInt);
        if(KeyStr == "kCGWindowNumber")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].WID = MyInt;
        else if(KeyStr == "kCGWindowOwnerPID")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].PID = MyInt;
        else if(KeyStr == "kCGWindowLayer")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Layer = MyInt;
        else if(KeyStr == "X")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].X = MyInt;
        else if(KeyStr == "Y")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Y = MyInt;
        else if(KeyStr == "Width")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Width = MyInt;
        else if(KeyStr == "Height")
            KWMTiling.WindowLst[KWMTiling.WindowLst.size()-1].Height = MyInt;
    }
    else if(ID == CFDictionaryGetTypeID())
    {
        CFDictionaryRef Elem = (CFDictionaryRef)Value;
        CFDictionaryApplyFunction(Elem, GetWindowInfo, NULL);
    } 
}
