/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_eglplatform_h_
#define __gc_hal_eglplatform_h_

/* Include VDK types. */
#include "gc_hal_types.h"
#include "gc_hal_base.h"
#include "gc_hal_eglplatform_type.h"

#ifdef __cplusplus
extern "C" {
#endif


#if defined(_WIN32) || defined(__VC32__) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__)
#ifndef WIN32_LEAN_AND_MEAN
/* #define WIN32_LEAN_AND_MEAN 1 */
#endif
#include <windows.h>

typedef HDC                             HALNativeDisplayType;
typedef HWND                            HALNativeWindowType;
typedef HBITMAP                         HALNativePixmapType;

typedef struct __BITFIELDINFO
{
    BITMAPINFO    bmi;
    RGBQUAD       bmiColors[2];
}
BITFIELDINFO;

#elif /* defined(__APPLE__) || */ defined(__WINSCW__) || defined(__SYMBIAN32__)  /* Symbian */

typedef int                             EGLNativeDisplayType;
typedef void *                          EGLNativeWindowType;
typedef void *                          EGLNativePixmapType;

#elif defined(WL_EGL_PLATFORM) || defined(EGL_API_WL) /* Wayland */

#if defined(__GNUC__)
#   define inline            __inline__  /* GNU keyword. */
#endif

/* Wayland platform. */
#include <wayland-egl.h>
#include <pthread.h>

#define WL_COMPOSITOR_SIGNATURE (0x31415926)
#define WL_CLIENT_SIGNATURE             (0x27182818)
#define WL_LOCAL_DISPLAY_SIGNATURE      (0x27182991)

typedef struct _gcsWL_VIV_BUFFER
{
   struct wl_resource *wl_buffer;
   gcoSURF surface;
   gctINT32 width, height;
} gcsWL_VIV_BUFFER;

typedef struct _gcsWL_EGL_DISPLAY
{
   struct wl_display* wl_display;
   struct wl_viv* wl_viv;
   struct wl_registry *registry;
   struct wl_event_queue    *wl_queue;
   struct wl_event_queue    *wl_swap_queue;
   gctINT swapInterval;
   gctINT file;
} gcsWL_EGL_DISPLAY;

typedef struct _gcsWL_LOCAL_DISPLAY {
    gctUINT wl_signature;
    gctPOINTER localInfo;
} gcsWL_LOCAL_DISPLAY;

typedef struct _gcsWL_EGL_BUFFER_INFO
{
   gctINT32 width;
   gctINT32 height;
   gctINT32 stride;
   gceSURF_FORMAT format;
   gceSURF_TYPE   type;
   gcuVIDMEM_NODE_PTR node;
   gcePOOL pool;
   gctUINT bytes;
   gcoSURF surface;
   gctINT32 invalidate;
   gctBOOL locked;
} gcsWL_EGL_BUFFER_INFO;

typedef struct _gcsWL_EGL_BUFFER
{
   gctUINT wl_signature;
   gcsWL_EGL_BUFFER_INFO info;
   struct wl_buffer* wl_buffer;
   struct wl_callback* frame_callback;
   struct wl_list link;
} gcsWL_EGL_BUFFER;

typedef struct _gcsWL_EGL_WINDOW_INFO
{
   gctINT32 dx;
   gctINT32 dy;
   gctUINT width;
   gctUINT height;
   gceSURF_FORMAT format;
   gctUINT bpp;
   gctINT  bufferCount;
   gctUINT current;
} gcsWL_EGL_WINDOW_INFO;

struct wl_egl_window
{
   gctUINT wl_signature;
   gcsWL_EGL_DISPLAY* display;
   gcsWL_EGL_BUFFER **backbuffers;
   gcsWL_EGL_WINDOW_INFO* info;
   gctINT  noResolve;
   gctINT32 attached_width;
   gctINT32 attached_height;
   gcsATOM_PTR reference;
   pthread_mutex_t window_mutex;
   struct wl_surface* surface;
   struct wl_list link;
};

typedef void *                          HALNativeDisplayType;
typedef void *                          HALNativeWindowType;
typedef void *                          HALNativePixmapType;

#elif defined(__GBM__) /* GBM */

typedef struct gbm_device *             HALNativeDisplayType;
typedef struct gbm_bo *                 HALNativePixmapType;
typedef void *                          HALNativeWindowType;

#elif defined(__ANDROID__) || defined(ANDROID)

#include <android/native_window.h>
struct egl_native_pixmap_t;

typedef struct ANativeWindow*           HALNativeWindowType;
typedef struct egl_native_pixmap_t*     HALNativePixmapType;
typedef void*                           HALNativeDisplayType;

#elif defined(MIR_EGL_PLATFORM) /* Mir */

#include <mir_toolkit/mir_client_library.h>
typedef MirEGLNativeDisplayType         HALNativeDisplayType;
typedef void                   *        HALNativePixmapType;
typedef MirEGLNativeWindowType          HALNativeWindowType;

#elif defined(__QNXNTO__)

#include <screen/screen.h>
typedef int                             HALNativeDisplayType;
typedef screen_window_t                 HALNativeWindowType;
typedef screen_pixmap_t                 HALNativePixmapType;

#elif defined(__unix__) || defined(__APPLE__)

#if defined(EGL_API_DFB)

/* Vivante DFB. */
#include <directfb.h>
typedef struct _DFBDisplay *            HALNativeDisplayType;
typedef struct _DFBWindow *             HALNativeWindowType;
typedef struct _DFBPixmap *             HALNativePixmapType;

#elif defined(EGL_API_FB)

/* Vivante FBDEV */
struct _FBDisplay;
struct _FBWindow;
struct _FBPixmap;

typedef struct _FBDisplay *             HALNativeDisplayType;
typedef struct _FBWindow *              HALNativeWindowType;
typedef struct _FBPixmap *              HALNativePixmapType;

#else

/* X11 (tetative). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef Display *                       HALNativeDisplayType;
typedef Window                          HALNativeWindowType;
typedef Pixmap                          HALNativePixmapType;

/* Rename some badly named X defines. */
#ifdef Status
#   define XStatus      int
#   undef Status
#endif
#ifdef Always
#   define XAlways      2
#   undef Always
#endif
#ifdef CurrentTime
#   undef CurrentTime
#   define XCurrentTime 0
#endif

#endif

#else
#error "Platform not recognized"
#endif

/* define DUMMY according to the system */
#if defined(EGL_API_WL)
#   define WL_DUMMY (31415926)
#   define EGL_DUMMY WL_DUMMY
#elif defined(__ANDROID__) || defined(ANDROID)
#   define ANDROID_DUMMY (31415926)
#   define EGL_DUMMY ANDROID_DUMMY
#else
#   define EGL_DUMMY (31415926)
#endif

/*******************************************************************************
** Display. ********************************************************************
*/

gceSTATUS
gcoOS_GetDisplay(
    OUT HALNativeDisplayType * Display,
    IN gctPOINTER Context
    );

gceSTATUS
gcoOS_GetDisplayByIndex(
    IN gctINT DisplayIndex,
    OUT HALNativeDisplayType * Display,
    IN gctPOINTER Context
    );

gceSTATUS
gcoOS_GetDisplayInfo(
    IN HALNativeDisplayType Display,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctSIZE_T * Physical,
    OUT gctINT * Stride,
    OUT gctINT * BitsPerPixel
    );



gceSTATUS
gcoOS_GetDisplayInfoEx(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctUINT DisplayInfoSize,
    OUT halDISPLAY_INFO * DisplayInfo
    );

gceSTATUS
gcoOS_GetDisplayVirtual(
    IN HALNativeDisplayType Display,
    OUT gctINT * Width,
    OUT gctINT * Height
    );

gceSTATUS
gcoOS_GetDisplayBackbuffer(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    OUT gctPOINTER  *  context,
    OUT gcoSURF     *  surface,
    OUT gctUINT * Offset,
    OUT gctINT * X,
    OUT gctINT * Y
    );

gceSTATUS
gcoOS_SetDisplayVirtual(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctUINT Offset,
    IN gctINT X,
    IN gctINT Y
    );

gceSTATUS
gcoOS_SetDisplayVirtualEx(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctPOINTER Context,
    IN gcoSURF Surface,
    IN gctUINT Offset,
    IN gctINT X,
    IN gctINT Y
    );

gceSTATUS
gcoOS_CancelDisplayBackbuffer(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctPOINTER Context,
    IN gcoSURF Surface,
    IN gctUINT Offset,
    IN gctINT X,
    IN gctINT Y
    );

gceSTATUS
gcoOS_SetSwapInterval(
    IN HALNativeDisplayType Display,
    IN gctINT Interval
);

gceSTATUS
gcoOS_SetSwapIntervalEx(
    IN HALNativeDisplayType Display,
    IN gctINT Interval,
    IN gctPOINTER localDisplay);

gceSTATUS
gcoOS_GetSwapInterval(
    IN HALNativeDisplayType Display,
    IN gctINT_PTR Min,
    IN gctINT_PTR Max
);

gceSTATUS
gcoOS_DisplayBufferRegions(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctINT NumRects,
    IN gctINT_PTR Rects
    );

gceSTATUS
gcoOS_DestroyDisplay(
    IN HALNativeDisplayType Display
    );

gceSTATUS
gcoOS_InitLocalDisplayInfo(
    IN HALNativeDisplayType Display,
    IN OUT gctPOINTER * localDisplay
    );

gceSTATUS
gcoOS_DeinitLocalDisplayInfo(
    IN HALNativeDisplayType Display,
    IN OUT gctPOINTER * localDisplay
    );

gceSTATUS
gcoOS_GetDisplayInfoEx2(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctPOINTER  localDisplay,
    IN gctUINT DisplayInfoSize,
    OUT halDISPLAY_INFO * DisplayInfo
    );

gceSTATUS
gcoOS_GetDisplayBackbufferEx(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctPOINTER  localDisplay,
    OUT gctPOINTER  *  context,
    OUT gcoSURF     *  surface,
    OUT gctUINT * Offset,
    OUT gctINT * X,
    OUT gctINT * Y
    );

gceSTATUS
gcoOS_IsValidDisplay(
    IN HALNativeDisplayType Display
    );

gceSTATUS
gcoOS_GetNativeVisualId(
    IN HALNativeDisplayType Display,
    OUT gctINT* nativeVisualId
    );

gctBOOL
gcoOS_SynchronousFlip(
    IN HALNativeDisplayType Display
    );

/*******************************************************************************
** Windows. ********************************************************************
*/

gceSTATUS
gcoOS_CreateWindow(
    IN HALNativeDisplayType Display,
    IN gctINT X,
    IN gctINT Y,
    IN gctINT Width,
    IN gctINT Height,
    OUT HALNativeWindowType * Window
    );

gceSTATUS
gcoOS_GetWindowInfo(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    OUT gctINT * X,
    OUT gctINT * Y,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctINT * BitsPerPixel,
    OUT gctUINT * Offset
    );

gceSTATUS
gcoOS_DestroyWindow(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window
    );

gceSTATUS
gcoOS_DrawImage(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctINT Left,
    IN gctINT Top,
    IN gctINT Right,
    IN gctINT Bottom,
    IN gctINT Width,
    IN gctINT Height,
    IN gctINT BitsPerPixel,
    IN gctPOINTER Bits
    );

gceSTATUS
gcoOS_GetImage(
    IN HALNativeWindowType Window,
    IN gctINT Left,
    IN gctINT Top,
    IN gctINT Right,
    IN gctINT Bottom,
    OUT gctINT * BitsPerPixel,
    OUT gctPOINTER * Bits
    );

gceSTATUS
gcoOS_GetWindowInfoEx(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    OUT gctINT * X,
    OUT gctINT * Y,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctINT * BitsPerPixel,
    OUT gctUINT * Offset,
    OUT gceSURF_FORMAT * Format,
    OUT gceSURF_TYPE * Type
    );

gceSTATUS
gcoOS_DrawImageEx(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctINT Left,
    IN gctINT Top,
    IN gctINT Right,
    IN gctINT Bottom,
    IN gctINT Width,
    IN gctINT Height,
    IN gctINT BitsPerPixel,
    IN gctPOINTER Bits,
    IN gceSURF_FORMAT  Format
    );

/*
 * Possiable types:
 *   gcvSURF_BITMAP
 *   gcvSURF_RENDER_TARGET
 *   gcvSURF_RENDER_TARGET_NO_COMPRESSION
 *   gcvSURF_RENDER_TARGET_NO_TILE_STATUS
 */
gceSTATUS
gcoOS_SetWindowFormat(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gceSURF_TYPE Type,
    IN gceSURF_FORMAT Format
    );


/*******************************************************************************
** Pixmaps. ********************************************************************
*/

gceSTATUS
gcoOS_CreatePixmap(
    IN HALNativeDisplayType Display,
    IN gctINT Width,
    IN gctINT Height,
    IN gctINT BitsPerPixel,
    OUT HALNativePixmapType * Pixmap
    );

gceSTATUS
gcoOS_GetPixmapInfo(
    IN HALNativeDisplayType Display,
    IN HALNativePixmapType Pixmap,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctINT * BitsPerPixel,
    OUT gctINT * Stride,
    OUT gctPOINTER * Bits
    );

gceSTATUS
gcoOS_DrawPixmap(
    IN HALNativeDisplayType Display,
    IN HALNativePixmapType Pixmap,
    IN gctINT Left,
    IN gctINT Top,
    IN gctINT Right,
    IN gctINT Bottom,
    IN gctINT Width,
    IN gctINT Height,
    IN gctINT BitsPerPixel,
    IN gctPOINTER Bits
    );

gceSTATUS
gcoOS_DestroyPixmap(
    IN HALNativeDisplayType Display,
    IN HALNativePixmapType Pixmap
    );

gceSTATUS
gcoOS_GetPixmapInfoEx(
    IN HALNativeDisplayType Display,
    IN HALNativePixmapType Pixmap,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctINT * BitsPerPixel,
    OUT gctINT * Stride,
    OUT gctPOINTER * Bits,
    OUT gceSURF_FORMAT * Format
    );

gceSTATUS
gcoOS_CopyPixmapBits(
    IN HALNativeDisplayType Display,
    IN HALNativePixmapType Pixmap,
    IN gctUINT DstWidth,
    IN gctUINT DstHeight,
    IN gctINT DstStride,
    IN gceSURF_FORMAT DstFormat,
    OUT gctPOINTER DstBits
    );

/*******************************************************************************
** OS relative. ****************************************************************
*/
gceSTATUS
gcoOS_LoadEGLLibrary(
    OUT gctHANDLE * Handle
    );

gceSTATUS
gcoOS_FreeEGLLibrary(
    IN gctHANDLE Handle
    );

gceSTATUS
gcoOS_ShowWindow(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window
    );

gceSTATUS
gcoOS_HideWindow(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window
    );

gceSTATUS
gcoOS_SetWindowTitle(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    IN gctCONST_STRING Title
    );

gceSTATUS
gcoOS_CapturePointer(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window
    );

gceSTATUS
gcoOS_GetEvent(
    IN HALNativeDisplayType Display,
    IN HALNativeWindowType Window,
    OUT halEvent * Event
    );

gceSTATUS
gcoOS_CreateClientBuffer(
    IN gctINT Width,
    IN gctINT Height,
    IN gctINT Format,
    IN gctINT Type,
    OUT gctPOINTER * ClientBuffer
    );

gceSTATUS
gcoOS_GetClientBufferInfo(
    IN gctPOINTER ClientBuffer,
    OUT gctINT * Width,
    OUT gctINT * Height,
    OUT gctINT * Stride,
    OUT gctPOINTER * Bits
    );

gceSTATUS
gcoOS_DestroyClientBuffer(
    IN gctPOINTER ClientBuffer
    );

gceSTATUS
gcoOS_DestroyContext(
    IN gctPOINTER Display,
    IN gctPOINTER Context
    );

gceSTATUS
gcoOS_CreateContext(
    IN gctPOINTER LocalDisplay,
    IN gctPOINTER Context
    );

gceSTATUS
gcoOS_MakeCurrent(
    IN gctPOINTER LocalDisplay,
    IN HALNativeWindowType DrawDrawable,
    IN HALNativeWindowType ReadDrawable,
    IN gctPOINTER Context,
    IN gcoSURF ResolveTarget
    );

gceSTATUS
gcoOS_CreateDrawable(
    IN gctPOINTER LocalDisplay,
    IN HALNativeWindowType Drawable
    );

gceSTATUS
gcoOS_DestroyDrawable(
    IN gctPOINTER LocalDisplay,
    IN HALNativeWindowType Drawable
    );
gceSTATUS
gcoOS_SwapBuffers(
    IN gctPOINTER LocalDisplay,
    IN HALNativeWindowType Drawable,
    IN gcoSURF RenderTarget,
    IN gcoSURF ResolveTarget,
    IN gctPOINTER ResolveBits,
    OUT gctUINT *Width,
    OUT gctUINT *Height
    );

gceSTATUS
gcoOS_ResizeWindow(
    IN gctPOINTER localDisplay,
    IN HALNativeWindowType Drawable,
    IN gctUINT Width,
    IN gctUINT Height
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_eglplatform_h_ */

