/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
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
*    Copyright (C) 2014 - 2015 Vivante Corporation
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


#ifndef __gc_hal_eglplatform_type_h_
#define __gc_hal_eglplatform_type_h_

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
** Events. *********************************************************************
*/

typedef enum _halEventType
{
    /* Keyboard event. */
    HAL_KEYBOARD,

    /* Mouse move event. */
    HAL_POINTER,

    /* Mouse button event. */
    HAL_BUTTON,

    /* Application close event. */
    HAL_CLOSE,

    /* Application window has been updated. */
    HAL_WINDOW_UPDATE
}
halEventType;

/* Scancodes for keyboard. */
typedef enum _halKeys
{
    HAL_UNKNOWN = -1,

    HAL_BACKSPACE = 0x08,
    HAL_TAB,
    HAL_ENTER = 0x0D,
    HAL_ESCAPE = 0x1B,

    HAL_SPACE = 0x20,
    HAL_SINGLEQUOTE = 0x27,
    HAL_PAD_ASTERISK = 0x2A,
    HAL_COMMA = 0x2C,
    HAL_HYPHEN,
    HAL_PERIOD,
    HAL_SLASH,
    HAL_0,
    HAL_1,
    HAL_2,
    HAL_3,
    HAL_4,
    HAL_5,
    HAL_6,
    HAL_7,
    HAL_8,
    HAL_9,
    HAL_SEMICOLON = 0x3B,
    HAL_EQUAL = 0x3D,
    HAL_A = 0x41,
    HAL_B,
    HAL_C,
    HAL_D,
    HAL_E,
    HAL_F,
    HAL_G,
    HAL_H,
    HAL_I,
    HAL_J,
    HAL_K,
    HAL_L,
    HAL_M,
    HAL_N,
    HAL_O,
    HAL_P,
    HAL_Q,
    HAL_R,
    HAL_S,
    HAL_T,
    HAL_U,
    HAL_V,
    HAL_W,
    HAL_X,
    HAL_Y,
    HAL_Z,
    HAL_LBRACKET,
    HAL_BACKSLASH,
    HAL_RBRACKET,
    HAL_BACKQUOTE = 0x60,

    HAL_F1 = 0x80,
    HAL_F2,
    HAL_F3,
    HAL_F4,
    HAL_F5,
    HAL_F6,
    HAL_F7,
    HAL_F8,
    HAL_F9,
    HAL_F10,
    HAL_F11,
    HAL_F12,

    HAL_LCTRL,
    HAL_RCTRL,
    HAL_LSHIFT,
    HAL_RSHIFT,
    HAL_LALT,
    HAL_RALT,
    HAL_CAPSLOCK,
    HAL_NUMLOCK,
    HAL_SCROLLLOCK,
    HAL_PAD_0,
    HAL_PAD_1,
    HAL_PAD_2,
    HAL_PAD_3,
    HAL_PAD_4,
    HAL_PAD_5,
    HAL_PAD_6,
    HAL_PAD_7,
    HAL_PAD_8,
    HAL_PAD_9,
    HAL_PAD_HYPHEN,
    HAL_PAD_PLUS,
    HAL_PAD_SLASH,
    HAL_PAD_PERIOD,
    HAL_PAD_ENTER,
    HAL_SYSRQ,
    HAL_PRNTSCRN,
    HAL_BREAK,
    HAL_UP,
    HAL_LEFT,
    HAL_RIGHT,
    HAL_DOWN,
    HAL_HOME,
    HAL_END,
    HAL_PGUP,
    HAL_PGDN,
    HAL_INSERT,
    HAL_DELETE,
    HAL_LWINDOW,
    HAL_RWINDOW,
    HAL_MENU,
    HAL_POWER,
    HAL_SLEEP,
    HAL_WAKE
}
halKeys;

/* Structure that defined keyboard mapping. */
typedef struct _halKeyMap
{
    /* Normal key. */
    halKeys normal;

    /* Extended key. */
    halKeys extended;
}
halKeyMap;

/* Event structure. */
typedef struct _halEvent
{
    /* Event type. */
    halEventType type;

    /* Event data union. */
    union _halEventData
    {
        /* Event data for keyboard. */
        struct _halKeyboard
        {
            /* Scancode. */
            halKeys scancode;

            /* ASCII characte of the key pressed. */
            char    key;

            /* Flag whether the key was pressed (1) or released (0). */
            char    pressed;
        }
        keyboard;

        /* Event data for pointer. */
        struct _halPointer
        {
            /* Current pointer coordinate. */
            int     x;
            int     y;
        }
        pointer;

        /* Event data for mouse buttons. */
        struct _halButton
        {
            /* Left button state. */
            int     left;

            /* Middle button state. */
            int     middle;

            /* Right button state. */
            int     right;

            /* Current pointer coordinate. */
            int     x;
            int     y;
        }
        button;
    }
    data;
}
halEvent;

/* Tiling layouts. */
typedef enum _halTiling
{
    HAL_INVALIDTILED = 0x0,         /* Invalid tiling */
    /* Tiling basic modes enum'ed in power of 2. */
    HAL_LINEAR       = 0x1,         /* No    tiling. */
    HAL_TILED        = 0x2,         /* 4x4   tiling. */
    HAL_SUPERTILED   = 0x4,         /* 64x64 tiling. */
    HAL_MINORTILED   = 0x8,         /* 2x2   tiling. */

    /* Tiling special layouts. */
    HAL_TILING_SPLIT_BUFFER = 0x100,

    /* Tiling combination layouts. */
    HAL_MULTI_TILED      = HAL_TILED
                         | HAL_TILING_SPLIT_BUFFER,

    HAL_MULTI_SUPERTILED = HAL_SUPERTILED
                         | HAL_TILING_SPLIT_BUFFER,
}
halTiling;

/* VFK_DISPLAY_INFO structure defining information returned by
   vdkGetDisplayInfoEx. */
typedef struct _halDISPLAY_INFO
{
    /* The size of the display in pixels. */
    int                         width;
    int                         height;

    /* The stride of the dispay. -1 is returned if the stride is not known
    ** for the specified display.*/
    int                         stride;

    /* The tiling layout of the display. */
    int                         tiling;

    /* The color depth of the display in bits per pixel. */
    int                         bitsPerPixel;

    /* The logical pointer to the display memory buffer. NULL is returned
    ** if the pointer is not known for the specified display. */
    void *                      logical;

    /* The physical address of the display memory buffer. ~0 is returned
    ** if the address is not known for the specified display. */
    unsigned long               physical;

    /* True if requires buffer wrapping. */
    int                         wrapFB;

    /* FB_MULTI_BUFFER */
    int                         multiBuffer;
    int                         backBufferY;

    /* The color info of the display. */
    unsigned int                alphaLength;
    unsigned int                alphaOffset;
    unsigned int                redLength;
    unsigned int                redOffset;
    unsigned int                greenLength;
    unsigned int                greenOffset;
    unsigned int                blueLength;
    unsigned int                blueOffset;

    /* Display flip support. */
    int                         flip;
}
halDISPLAY_INFO;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_eglplatform_type_h_ */
