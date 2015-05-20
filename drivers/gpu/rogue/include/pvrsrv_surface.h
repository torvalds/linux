/*************************************************************************/ /*!
@File
@Title          Device class external
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines DC specific structures which are externally visible
                (i.e. visible to clients of services), but are also required
                within services.
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

#ifndef _PVRSRV_SURFACE_H_
#define _PVRSRV_SURFACE_H_

#include "img_types.h"
#include "fbc_types.h"

#define PVRSRV_SURFACE_TRANSFORM_NONE	   (0 << 0)
#define PVRSRV_SURFACE_TRANSFORM_FLIP_H    (1 << 0)
#define PVRSRV_SURFACE_TRANSFORM_FLIP_V    (1 << 1)
#define PVRSRV_SURFACE_TRANSFORM_ROT_90    (1 << 2)
#define PVRSRV_SURFACE_TRANSFORM_ROT_180   ((1 << 0) + (1 << 1))
#define PVRSRV_SURFACE_TRANSFORM_ROT_270   ((1 << 0) + (1 << 1) + (1 << 2))

#define PVRSRV_SURFACE_BLENDING_NONE	   0
#define PVRSRV_SURFACE_BLENDING_PREMULT	   1
#define PVRSRV_SURFACE_BLENDING_COVERAGE   2

typedef enum _PVRSRV_SURFACE_MEMLAYOUT_ {
	PVRSRV_SURFACE_MEMLAYOUT_STRIDED = 0,		/*!< Strided memory buffer */
	PVRSRV_SURFACE_MEMLAYOUT_FBC,				/*!< Frame buffer compressed buffer */
	PVRSRV_SURFACE_MEMLAYOUT_BIF_PAGE_TILED,	/*!< BIF page tiled buffer */
} PVRSRV_SURFACE_MEMLAYOUT;

typedef struct _PVRSRV_SURFACE_FBC_LAYOUT_ {
	FB_COMPRESSION	eFBCompressionMode;
} PVRSRV_SURFACE_FBC_LAYOUT;

typedef struct _PVRSRV_SURFACE_FORMAT_
{
	IMG_UINT32					ePixFormat;
	PVRSRV_SURFACE_MEMLAYOUT	eMemLayout;
	union {
		PVRSRV_SURFACE_FBC_LAYOUT	sFBCLayout;
	} u;
} PVRSRV_SURFACE_FORMAT;

typedef struct _PVRSRV_SURFACE_DIMS_
{
	IMG_UINT32		ui32Width;
	IMG_UINT32		ui32Height;
} PVRSRV_SURFACE_DIMS;

typedef struct _PVRSRV_SURFACE_INFO_
{
	PVRSRV_SURFACE_DIMS		sDims;
	PVRSRV_SURFACE_FORMAT	sFormat;
} PVRSRV_SURFACE_INFO;

typedef struct _PVRSRV_SURFACE_RECT_
{
	IMG_INT32				i32XOffset;
	IMG_INT32				i32YOffset;
	PVRSRV_SURFACE_DIMS		sDims;
} PVRSRV_SURFACE_RECT;

typedef struct _PVRSRV_SURFACE_CONFIG_INFO_
{
	/*!< Crop applied to surface (BEFORE transformation) */
	PVRSRV_SURFACE_RECT		sCrop;

	/*!< Region of screen to display surface in (AFTER scaling) */
	PVRSRV_SURFACE_RECT		sDisplay;

	/*!< Surface rotation / flip / mirror */
	IMG_UINT32				ui32Transform;

	/*!< Alpha blending mode e.g. none / premult / coverage */
	IMG_UINT32				eBlendType;

	/*!< Custom data for the display engine */
	IMG_UINT32				ui32Custom;

	/*!< Plane alpha */
	IMG_UINT8				ui8PlaneAlpha;
	IMG_UINT8				ui8Reserved1[3];
} PVRSRV_SURFACE_CONFIG_INFO;

typedef struct _PVRSRV_PANEL_INFO_
{
	PVRSRV_SURFACE_INFO sSurfaceInfo;
	IMG_UINT32			ui32RefreshRate;
	IMG_UINT32			ui32XDpi;
	IMG_UINT32			ui32YDpi;
} PVRSRV_PANEL_INFO;

/*
	Helper function to create a Config Info based on a Surface Info
	to do a flip with no scale, transformation etc.
*/
static INLINE IMG_VOID SurfaceConfigFromSurfInfo(PVRSRV_SURFACE_INFO *psSurfaceInfo,
												 PVRSRV_SURFACE_CONFIG_INFO *psConfigInfo)
{
	psConfigInfo->sCrop.sDims = psSurfaceInfo->sDims;
	psConfigInfo->sCrop.i32XOffset = 0;
	psConfigInfo->sCrop.i32YOffset = 0;
	psConfigInfo->sDisplay.sDims = psSurfaceInfo->sDims;
	psConfigInfo->sDisplay.i32XOffset = 0;
	psConfigInfo->sDisplay.i32YOffset = 0;
	psConfigInfo->ui32Transform = PVRSRV_SURFACE_TRANSFORM_NONE;
	psConfigInfo->eBlendType = PVRSRV_SURFACE_BLENDING_NONE;
	psConfigInfo->ui32Custom = 0;
	psConfigInfo->ui8PlaneAlpha = 0xff;
}

#endif /* _PVRSRV_SURFACE_H_ */
