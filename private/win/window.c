#include <win/platform.h>

#include <platform.h>
#include <internal.h>
#include <utils.h>
#include <platform_gl.h>
#include <platform_vk.h>

#include <wingdi.h>

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

static mimas_u32 get_window_styles(Mimas_Window const* const window) {
    return WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
}

static mimas_u32 get_window_extended_styles(Mimas_Window const* const window) {
    return WS_EX_APPWINDOW;
}

// Capture cursor in the client area of window.
//
static void capture_cursor(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    RECT client;
    GetClientRect(native_window->handle, &client);
    ClientToScreen(native_window->handle, (POINT*)&client.left);
    ClientToScreen(native_window->handle, (POINT*)&client.right);
    ClipCursor(&client);
}

static void release_captured_cursor() {
    ClipCursor(NULL);
}

static void enable_virtual_cursor(Mimas_Window* const window) {
    // Capture in case we lag and the cursor moves out of the window.
    capture_cursor(window);

}

static void disable_virtual_cursor(Mimas_Window* const window) {
    release_captured_cursor();
}

static mimas_i32 window_hit_test(mimas_i32 const cursor_x, mimas_i32 const cursor_y, RECT const window_rect) {
    enum Region_Mask {
        client = 0, 
        left = 1, 
        right = 2, 
        top = 4, 
        bottom = 8
    };

    mimas_i32 const border_width = 8;

    mimas_u32 result = 0;
    result |= left * (cursor_x < window_rect.left + border_width);
    result |= right * (cursor_x >= window_rect.right - border_width);
    result |= top * (cursor_y < window_rect.top + border_width);
    result |= bottom * (cursor_y >= window_rect.bottom - border_width);

    switch(result) {
        case left          : return HTLEFT;
        case right         : return HTRIGHT;
        case top           : return HTTOP;
        case bottom        : return HTBOTTOM;
        case top | left    : return HTTOPLEFT;
        case top | right   : return HTTOPRIGHT;
        case bottom | left : return HTBOTTOMLEFT;
        case bottom | right: return HTBOTTOMRIGHT;
        case client        : return HTCLIENT;
        default            : return HTNOWHERE;
    }
}

static Mimas_Key translate_key(mimas_u32 const vk, mimas_bool const extended) {
    Mimas_Win_Platform* const platform = (Mimas_Win_Platform*)_mimas_get_mimas_internal()->platform;
    Mimas_Key const key = platform->keys[vk];
    if(extended) {
        switch(key) {
            case MIMAS_KEY_ENTER: 
                return MIMAS_KEY_NUMPAD_ENTER;
            default:
                return key;
        }
    } else {
        return key;
    }
}

static LRESULT window_proc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) {
    Mimas_Window* const window = GetPropW(hwnd, L"Mimas_Window");
    switch(msg) {
        case WM_ACTIVATE: {
            if(!window->decorated) {
                MARGINS const margins = {1, 1, 1, 1};
                DwmExtendFrameIntoClientArea(hwnd, &margins);
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER);
            }

            LRESULT const res = DefWindowProc(hwnd, msg, wparam, lparam);
            if(window->callbacks.window_activate) {
                window->callbacks.window_activate(window, wparam != 0, window->callbacks.window_activate_data);
            }
            return res;
        } break;

        case WM_NCCALCSIZE: {
            if(wparam == TRUE && !window->decorated) {
                return 0;
            }
        } break;

        case WM_NCHITTEST: {
            RECT window_rect;
            GetWindowRect(hwnd, &window_rect);
            RECT client_rect;
            GetClientRect(hwnd, &client_rect);
            ClientToScreen(hwnd, (POINT*)&client_rect.left);
            ClientToScreen(hwnd, (POINT*)&client_rect.right);
            if(window->callbacks.hittest) {
                Mimas_Rect const _window_rect = {window_rect.left, window_rect.top, window_rect.bottom, window_rect.right};
                Mimas_Rect const _client_rect = {client_rect.left, client_rect.top, client_rect.bottom, client_rect.right};
                Mimas_Hittest_Result const r = window->callbacks.hittest(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), _window_rect, _client_rect);
                mimas_i32 const hit[] = {
                    [MIMAS_HITTEST_TOP] = HTTOP,
                    [MIMAS_HITTEST_BOTTOM] = HTBOTTOM,
                    [MIMAS_HITTEST_LEFT] = HTLEFT,
                    [MIMAS_HITTEST_RIGHT] = HTRIGHT,
                    [MIMAS_HITTEST_TOP_LEFT] = HTTOPLEFT,
                    [MIMAS_HITTEST_TOP_RIGHT] = HTTOPRIGHT,
                    [MIMAS_HITTEST_BOTTOM_LEFT] = HTBOTTOMLEFT,
                    [MIMAS_HITTEST_BOTTOM_RIGHT] = HTBOTTOMRIGHT,
                    [MIMAS_HITTEST_CLIENT] = HTCLIENT,
                    [MIMAS_HITTEST_TITLEBAR] = HTCAPTION,
                    [MIMAS_HITTEST_MINIMIZE] = HTMINBUTTON,
                    [MIMAS_HITTEST_MAXIMIZE] = HTMAXBUTTON,
                    [MIMAS_HITTEST_CLOSE] = HTCLOSE,
                    [MIMAS_HITTEST_NOWHERE] = HTNOWHERE,
                };
                return hit[r];
            } else if(!window->decorated) {
                return window_hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), window_rect);
            }
        } break;

        case WM_SETFOCUS: {
            if(window->cursor_mode == MIMAS_CURSOR_CAPTURED) {
                capture_cursor(window);
            } else if(window->cursor_mode == MIMAS_CURSOR_VIRTUAL) {
                enable_virtual_cursor(window);
            }

            if(window->callbacks.window_activate) {
                window->callbacks.window_activate(window, mimas_true, window->callbacks.window_activate_data);
            }
        } break;

        case WM_KILLFOCUS: {
            if(window->cursor_mode == MIMAS_CURSOR_CAPTURED) {
                release_captured_cursor();
            } else if(window->cursor_mode == MIMAS_CURSOR_VIRTUAL) {
                disable_virtual_cursor(window);
            }

            if(window->callbacks.key) {
                for(mimas_u32 i = 0; i < ARRAY_SIZE(window->keys); ++i) {
                    if(window->keys[i] != MIMAS_KEY_RELEASE) {
                        window->keys[i] = MIMAS_KEY_RELEASE;
                        window->callbacks.key(window, i, MIMAS_KEY_RELEASE, window->callbacks.key_data);
                    }
                }
            }

            if(window->callbacks.window_activate) {
                window->callbacks.window_activate(window, mimas_false, window->callbacks.window_activate_data);
            }
        } break;

        case WM_KEYUP:
        case WM_KEYDOWN: {
            mimas_bool const extended = lparam & 0x800000;
            mimas_bool const key_was_up = lparam & 0x80000000;
            Mimas_Key const key = translate_key(wparam, extended);
            Mimas_Key_Action action;
            if(key_was_up && msg == WM_KEYDOWN) {
                action = MIMAS_KEY_PRESS;
            } else if(msg == WM_KEYDOWN) {
                action = MIMAS_KEY_REPEAT;
            } else {
                action = MIMAS_KEY_RELEASE;
            }

            window->keys[key] = action;

            if(window->callbacks.key) {
                window->callbacks.key(window, key, action, window->callbacks.key_data);
            }
        } break;

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_XBUTTONDOWN: 
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_XBUTTONUP: {
            if(window->callbacks.mouse_button) {
                Mimas_Mouse_Button_Action const action = (msg == WM_LBUTTONUP || msg == WM_MBUTTONUP || msg == WM_RBUTTONUP || msg == WM_XBUTTONUP);
                mimas_bool const is_lmb = (msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN);
                mimas_bool const is_rmb = (msg == WM_RBUTTONUP || msg == WM_RBUTTONDOWN);
                mimas_bool const is_mmb = (msg == WM_MBUTTONUP || msg == WM_MBUTTONDOWN);
                mimas_bool const is_xmb = (msg == WM_XBUTTONUP || msg == WM_XBUTTONDOWN);
                Mimas_Mouse_Button const button = MIMAS_MOUSE_BUTTON_LEFT * is_lmb | MIMAS_MOUSE_BUTTON_RIGHT * is_rmb | MIMAS_MOUSE_BUTTON_MIDDLE * is_mmb;
                // TODO: Temporarily because we don't have enough buttons.
                if(is_lmb || is_rmb || is_mmb) {
                    window->callbacks.mouse_button(window, button, action, window->callbacks.mouse_button_data);
                }
            }

            return 0;
        } break;

        case WM_MOUSEMOVE: {
            mimas_i32 const x = GET_X_LPARAM(lparam);
            mimas_i32 const y = GET_Y_LPARAM(lparam);

            if(window->callbacks.cursor_pos) {
                window->callbacks.cursor_pos(window, x, y, window->callbacks.cursor_pos_data);
            }

            return 0;
        } break;

        case WM_CLOSE: {
            window->close_requested = mimas_true;
            return 0;
        } break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Returns 1 if succeeded, 0 otherwise
static mimas_bool register_window_class() {
    WNDCLASSEX wndclass = {
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_proc,
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = MIMAS_WINDOW_CLASS_NAME,
        .hInstance = NULL,
    };
    return RegisterClassEx(&wndclass);
}

// Returns 1 if succeeded, 0 otherwise.
static mimas_bool unregister_window_class() {
    return UnregisterClass(MIMAS_WINDOW_CLASS_NAME, NULL);
}

static Mimas_Window* create_native_window(Mimas_Window_Create_Info const info) {
    Mimas_Window* const window = (Mimas_Window*)malloc(sizeof(Mimas_Window));
    memset(window, 0, sizeof(Mimas_Window));

    window->decorated = info.decorated;

    mimas_u32 const style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    int const wtitle_buffer_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, info.title, -1, NULL, 0);
    wchar_t* wtitle = malloc(sizeof(wchar_t) * wtitle_buffer_size);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, info.title, -1, wtitle, wtitle_buffer_size);
    HWND const hwnd = CreateWindowEx(WS_EX_APPWINDOW, MIMAS_WINDOW_CLASS_NAME, wtitle, style, CW_USEDEFAULT, CW_USEDEFAULT, info.width, info.height, NULL, NULL, NULL, NULL);
    free(wtitle);

    if(!hwnd) {
        free(window);
        // TODO: Error
        return NULL;
    }
    
    Mimas_Internal* const _mimas = _mimas_get_mimas_internal();
    HDC const hdc = GetDC(hwnd);
    if(_mimas->backend == MIMAS_BACKEND_GL) {
        PIXELFORMATDESCRIPTOR pfd = {
            .nSize = sizeof(PIXELFORMATDESCRIPTOR),
            .nVersion = 1,
            .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            .iPixelType = PFD_TYPE_RGBA,
            .cColorBits = 24,
            .cRedBits = 8,
            .cGreenBits = 8,
            .cBlueBits = 8,
            .cAlphaBits = 8,
            .cDepthBits = 24,
            .cStencilBits = 8,
        };
        int const pixf = ChoosePixelFormat(hdc, &pfd);
        if(!SetPixelFormat(hdc, pixf, &pfd)) {
            DestroyWindow(hwnd);
            free(window);
            // TODO: Error
            return NULL;
        }
    }

    Mimas_Win_Window* native_window = (Mimas_Win_Window*)malloc(sizeof(Mimas_Win_Window));
    native_window->handle = hwnd;
    native_window->hdc = hdc;
    window->native_window = native_window;

    SetProp(hwnd, L"Mimas_Window", (HANDLE)window);

    if(!info.decorated) {
        MARGINS const margins = {1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER);
    }

    return window;
}

static void destroy_native_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    DestroyWindow(native_window->handle);
    free(native_window);
    free(window);
}

mimas_bool mimas_platform_init() {
    Mimas_Win_Platform* const platform = (Mimas_Win_Platform*)malloc(sizeof(Mimas_Win_Platform));
    memset(platform, 0, sizeof(Mimas_Win_Platform));
    memset(platform->keys, -1, sizeof(platform->keys));

    platform->keys[0x30] = MIMAS_KEY_0;
    platform->keys[0x31] = MIMAS_KEY_1;
    platform->keys[0x32] = MIMAS_KEY_2;
    platform->keys[0x33] = MIMAS_KEY_3;
    platform->keys[0x34] = MIMAS_KEY_4;
    platform->keys[0x35] = MIMAS_KEY_5;
    platform->keys[0x36] = MIMAS_KEY_6;
    platform->keys[0x37] = MIMAS_KEY_7;
    platform->keys[0x38] = MIMAS_KEY_8;
    platform->keys[0x39] = MIMAS_KEY_9;

    platform->keys[0x41] = MIMAS_KEY_A;
    platform->keys[0x42] = MIMAS_KEY_B;
    platform->keys[0x43] = MIMAS_KEY_C;
    platform->keys[0x44] = MIMAS_KEY_D;
    platform->keys[0x45] = MIMAS_KEY_E;
    platform->keys[0x46] = MIMAS_KEY_F;
    platform->keys[0x47] = MIMAS_KEY_G;
    platform->keys[0x48] = MIMAS_KEY_H;
    platform->keys[0x49] = MIMAS_KEY_I;
    platform->keys[0x4A] = MIMAS_KEY_J;
    platform->keys[0x4B] = MIMAS_KEY_K;
    platform->keys[0x4C] = MIMAS_KEY_L;
    platform->keys[0x4D] = MIMAS_KEY_M;
    platform->keys[0x4E] = MIMAS_KEY_N;
    platform->keys[0x4F] = MIMAS_KEY_O;
    platform->keys[0x50] = MIMAS_KEY_P;
    platform->keys[0x51] = MIMAS_KEY_Q;
    platform->keys[0x52] = MIMAS_KEY_R;
    platform->keys[0x53] = MIMAS_KEY_S;
    platform->keys[0x54] = MIMAS_KEY_T;
    platform->keys[0x55] = MIMAS_KEY_U;
    platform->keys[0x56] = MIMAS_KEY_V;
    platform->keys[0x57] = MIMAS_KEY_W;
    platform->keys[0x58] = MIMAS_KEY_X;
    platform->keys[0x59] = MIMAS_KEY_Y;
    platform->keys[0x5A] = MIMAS_KEY_Z;

    platform->keys[VK_UP] = MIMAS_KEY_UP;
    platform->keys[VK_DOWN] = MIMAS_KEY_DOWN;
    platform->keys[VK_LEFT] = MIMAS_KEY_LEFT;
    platform->keys[VK_RIGHT] = MIMAS_KEY_RIGHT;

    platform->keys[VK_TAB] = MIMAS_KEY_TAB;
    platform->keys[VK_PRIOR] = MIMAS_KEY_PAGE_UP;
    platform->keys[VK_NEXT] = MIMAS_KEY_PAGE_DOWN;
    platform->keys[VK_HOME] = MIMAS_KEY_HOME;
    platform->keys[VK_END] = MIMAS_KEY_END;
    platform->keys[VK_INSERT] = MIMAS_KEY_INSERT;
    platform->keys[VK_DELETE] = MIMAS_KEY_DELETE;
    platform->keys[VK_BACK] = MIMAS_KEY_BACKSPACE;
    platform->keys[VK_SPACE] = MIMAS_KEY_SPACE;
    platform->keys[VK_RETURN] = MIMAS_KEY_ENTER;
    platform->keys[VK_ESCAPE] = MIMAS_KEY_ESCAPE;

    mimas_bool const register_res = register_window_class();
    if(!register_res) {
        return mimas_false;
    }
    Mimas_Internal* const _mimas = _mimas_get_mimas_internal();

    if(_mimas->backend == MIMAS_BACKEND_GL) {
         Mimas_Window* const dummy_window = create_native_window((Mimas_Window_Create_Info){.width = 1280, .height = 720, .title = "MIMAS_HELPER_WINDOW", .decorated = mimas_false});
        if(!dummy_window) {
            unregister_window_class();
            free(platform);
            // TODO: Error
            return mimas_false;
        }

        HWND const dummy_hwnd = ((Mimas_Win_Window*)dummy_window->native_window)->handle;
        // If the program is launched with STARTUPINFO, the first call to ShowWindow will ignore the nCmdShow param,
        //   therefore we call it here to clear that behaviour...
        ShowWindow(dummy_hwnd, SW_HIDE);
        // ... and call it again to make sure it's hidden.
        ShowWindow(dummy_hwnd, SW_HIDE);

        MSG msg;
        while (PeekMessageW(&msg, dummy_hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        platform->dummy_window = dummy_window;
        if(!mimas_platform_init_gl_backend()) {
            destroy_native_window(dummy_window);
            unregister_window_class();
            free(platform);
            return mimas_false;
        }
    } else {
        if(!mimas_platform_init_vk_backend()) {
            unregister_window_class();
            free(platform);
            return mimas_false;
        }
    }

    _mimas->platform = platform;

    return mimas_true;
}

void mimas_platform_terminate(Mimas_Backend const backend) {
    if(backend == MIMAS_BACKEND_GL) {
        mimas_platform_terminate_gl_backend();
    } else {
        mimas_platform_terminate_vk_backend();
    }

    Mimas_Internal* const _mimas = _mimas_get_mimas_internal();
    Mimas_Win_Platform* const platform = (Mimas_Win_Platform*)_mimas->platform;
    destroy_native_window(platform->dummy_window);
    unregister_window_class();
    free(platform);
    _mimas->platform = NULL;
}

void mimas_platform_poll_events() {
    MSG msg;
    while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Mimas_Win_Platform* const platform = (Mimas_Win_Platform*)_mimas_get_mimas_internal()->platform;
    GetKeyboardState(platform->keyboard_state);
    platform->mouse_state[MIMAS_MOUSE_BUTTON_LEFT] = GetKeyState(VK_LBUTTON) & 0x8000;
    platform->mouse_state[MIMAS_MOUSE_BUTTON_RIGHT] = GetKeyState(VK_RBUTTON) & 0x8000;
    platform->mouse_state[MIMAS_MOUSE_BUTTON_MIDDLE] = GetKeyState(VK_MBUTTON) & 0x8000;
}

Mimas_Window* mimas_platform_create_window(Mimas_Window_Create_Info const info) {
    return create_native_window(info);
}

void mimas_platform_destroy_window(Mimas_Window* const window) {
    destroy_native_window(window);
}

void mimas_platform_set_window_pos(Mimas_Window* const window, mimas_i32 const x, mimas_i32 const y) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    SetWindowPos(native_window->handle, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
}

void mimas_platform_get_window_pos(Mimas_Window*const window, mimas_i32* const x, mimas_i32* const y) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    RECT window_rect;
    GetWindowRect(native_window->handle, &window_rect);
    *x = window_rect.left;
    *y = window_rect.top;
}

void mimas_platform_set_window_content_pos(Mimas_Window* const window, mimas_i32 const x, mimas_i32 const y) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    if(window->decorated) {
        RECT pos = {x, y, x, y};
        // TODO: Make DPI aware.
        AdjustWindowRectEx(&pos, get_window_styles(window), FALSE, get_window_extended_styles(window));
        SetWindowPos(native_window->handle, NULL, pos.left, pos.top, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(native_window->handle, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void mimas_platform_get_window_content_pos(Mimas_Window*const window, mimas_i32* const x, mimas_i32* const y) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    POINT pos = {0, 0};
    ClientToScreen(native_window->handle, &pos);
    *x = pos.x;
    *y = pos.y;
}

void mimas_platform_set_window_content_size(Mimas_Window*const window, mimas_i32 const width, mimas_i32 const height) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    if(window->decorated) {
        RECT rect = {0, 0, width, height};
        // TODO: Make DPI aware.
        AdjustWindowRectEx(&rect, get_window_styles(window), FALSE, get_window_extended_styles(window));
        SetWindowPos(native_window->handle, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(native_window->handle, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void mimas_platform_get_window_content_size(Mimas_Window*const window, mimas_i32* const width, mimas_i32* const height) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    RECT client_rect;
    GetClientRect(native_window->handle, &client_rect);
    *width = client_rect.right;
    *height = client_rect.bottom;
}

void mimas_platform_show_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    ShowWindow(native_window->handle, SW_SHOW);
}

void mimas_platform_hide_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    ShowWindow(native_window->handle, SW_HIDE);
}

void mimas_platform_restore_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    ShowWindow(native_window->handle, SW_RESTORE);
}

void mimas_platform_minimize_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    ShowWindow(native_window->handle, SW_MINIMIZE);
}

void mimas_platform_maximize_window(Mimas_Window* const window) {
    Mimas_Win_Window* const native_window = (Mimas_Win_Window*)window->native_window;
    ShowWindow(native_window->handle, SW_MAXIMIZE);
}

// TODO: Move to input.c
void mimas_platform_set_cursor_mode(Mimas_Window* const window, Mimas_Cursor_Mode const cursor_mode) {
    window->cursor_mode = cursor_mode;
}
