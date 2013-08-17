/*************************************************************************/ /*!
@Title          PDUMP definitions header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    PDUMP definitions header
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#if !defined (__PDUMPDEFS_H__)
#define __PDUMPDEFS_H__

typedef enum _PDUMP_PIXEL_FORMAT_
{
	PVRSRV_PDUMP_PIXEL_FORMAT_UNSUPPORTED = 0,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGB8 = 1,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGB332 = 2,
	PVRSRV_PDUMP_PIXEL_FORMAT_KRGB555 = 3,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGB565 = 4,
	PVRSRV_PDUMP_PIXEL_FORMAT_ARGB4444 = 5,
	PVRSRV_PDUMP_PIXEL_FORMAT_ARGB1555 = 6,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGB888 = 7,
	PVRSRV_PDUMP_PIXEL_FORMAT_ARGB8888 = 8,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV8 = 9,
	PVRSRV_PDUMP_PIXEL_FORMAT_AYUV4444 = 10,
	PVRSRV_PDUMP_PIXEL_FORMAT_VY0UY1_8888 = 11,
	PVRSRV_PDUMP_PIXEL_FORMAT_UY0VY1_8888 = 12,
	PVRSRV_PDUMP_PIXEL_FORMAT_Y0UY1V_8888 = 13,
	PVRSRV_PDUMP_PIXEL_FORMAT_Y0VY1U_8888 = 14,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV888 = 15,
	PVRSRV_PDUMP_PIXEL_FORMAT_UYVY10101010 = 16,
	PVRSRV_PDUMP_PIXEL_FORMAT_VYAUYA8888 = 17,
	PVRSRV_PDUMP_PIXEL_FORMAT_AYUV8888 = 18,
	PVRSRV_PDUMP_PIXEL_FORMAT_AYUV2101010 = 19,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV101010 = 20,
	PVRSRV_PDUMP_PIXEL_FORMAT_PL12Y8 = 21,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV_IMC2 = 22,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV_YV12 = 23,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV_PL8 = 24,
	PVRSRV_PDUMP_PIXEL_FORMAT_YUV_PL12 = 25,
	PVRSRV_PDUMP_PIXEL_FORMAT_422PL12YUV8 = 26,
	PVRSRV_PDUMP_PIXEL_FORMAT_420PL12YUV8 = 27,
	PVRSRV_PDUMP_PIXEL_FORMAT_PL12Y10 = 28,
	PVRSRV_PDUMP_PIXEL_FORMAT_422PL12YUV10 = 29,
	PVRSRV_PDUMP_PIXEL_FORMAT_420PL12YUV10 = 30,
	PVRSRV_PDUMP_PIXEL_FORMAT_ABGR8888 = 31,
	PVRSRV_PDUMP_PIXEL_FORMAT_BGRA8888 = 32,
	PVRSRV_PDUMP_PIXEL_FORMAT_ARGB8332 = 33,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGB555 = 34,
	PVRSRV_PDUMP_PIXEL_FORMAT_F16 = 35,
	PVRSRV_PDUMP_PIXEL_FORMAT_F32 = 36,
	PVRSRV_PDUMP_PIXEL_FORMAT_L16 = 37,
	PVRSRV_PDUMP_PIXEL_FORMAT_L32 = 38,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGBA8888 = 39,
	PVRSRV_PDUMP_PIXEL_FORMAT_ABGR4444 = 40,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGBA4444 = 41,
	PVRSRV_PDUMP_PIXEL_FORMAT_BGRA4444 = 42,
	PVRSRV_PDUMP_PIXEL_FORMAT_ABGR1555 = 43,
	PVRSRV_PDUMP_PIXEL_FORMAT_RGBA5551 = 44,
	PVRSRV_PDUMP_PIXEL_FORMAT_BGRA5551 = 45,
	PVRSRV_PDUMP_PIXEL_FORMAT_BGR565 = 46,
	PVRSRV_PDUMP_PIXEL_FORMAT_A8 = 47,
	
	PVRSRV_PDUMP_PIXEL_FORMAT_FORCE_I32 = 0x7fffffff

} PDUMP_PIXEL_FORMAT;

typedef enum _PDUMP_MEM_FORMAT_
{
	PVRSRV_PDUMP_MEM_FORMAT_STRIDE = 0,
	PVRSRV_PDUMP_MEM_FORMAT_RESERVED = 1,
	PVRSRV_PDUMP_MEM_FORMAT_TILED = 8,
	PVRSRV_PDUMP_MEM_FORMAT_TWIDDLED = 9,
	PVRSRV_PDUMP_MEM_FORMAT_HYBRID = 10,
	
	PVRSRV_PDUMP_MEM_FORMAT_FORCE_I32 = 0x7fffffff
} PDUMP_MEM_FORMAT;

typedef enum _PDUMP_POLL_OPERATOR
{
	PDUMP_POLL_OPERATOR_EQUAL = 0,
	PDUMP_POLL_OPERATOR_LESS = 1,
	PDUMP_POLL_OPERATOR_LESSEQUAL = 2,
	PDUMP_POLL_OPERATOR_GREATER = 3,
	PDUMP_POLL_OPERATOR_GREATEREQUAL = 4,
	PDUMP_POLL_OPERATOR_NOTEQUAL = 5,
} PDUMP_POLL_OPERATOR;


#endif /* __PDUMPDEFS_H__ */

/*****************************************************************************
 End of file (pdumpdefs.h)
*****************************************************************************/
