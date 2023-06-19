/*******************************************************************************
@File
@Title          Common bridge header for rgxray
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxray
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
*******************************************************************************/

#ifndef COMMON_RGXRAY_BRIDGE_H
#define COMMON_RGXRAY_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_RGXRAY_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXRAY_RGXCREATERAYCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRAYCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXRAY_RGXKICKRDM			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXRAY_CMD_LAST			(PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+2)

/*******************************************
            RGXCreateRayContext
 *******************************************/

/* Bridge in structure for RGXCreateRayContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT_TAG
{
	IMG_UINT64 ui64RobustnessAddress;
	IMG_HANDLE hPrivData;
	IMG_BYTE *pui8sStaticRayContextState;
	IMG_INT32 i32Priority;
	IMG_UINT32 ui32ContextFlags;
	IMG_UINT32 ui32MaxDeadlineMS;
	IMG_UINT32 ui32StaticRayContextStateSize;
} __packed PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT;

/* Bridge out structure for RGXCreateRayContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT_TAG
{
	IMG_HANDLE hRayContext;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT;

/*******************************************
            RGXDestroyRayContext
 *******************************************/

/* Bridge in structure for RGXDestroyRayContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT_TAG
{
	IMG_HANDLE hRayContext;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT;

/* Bridge out structure for RGXDestroyRayContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT;

/*******************************************
            RGXKickRDM
 *******************************************/

/* Bridge in structure for RGXKickRDM */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKRDM_TAG
{
	IMG_UINT64 ui64ui64DeadlineInus;
	IMG_HANDLE hRayContext;
	IMG_UINT32 *pui32ClientUpdateOffset;
	IMG_UINT32 *pui32ClientUpdateValue;
	IMG_BYTE *pui8DMCmd;
	IMG_CHAR *puiUpdateFenceName;
	IMG_HANDLE *phClientUpdateUFOSyncPrimBlock;
	PVRSRV_FENCE hCheckFenceFd;
	PVRSRV_TIMELINE hUpdateTimeline;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_UINT32 ui32CmdSize;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32ui32AccStructSizeInBytes;
	IMG_UINT32 ui32ui32DispatchSize;
} __packed PVRSRV_BRIDGE_IN_RGXKICKRDM;

/* Bridge out structure for RGXKickRDM */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKRDM_TAG
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE hUpdateFence;
} __packed PVRSRV_BRIDGE_OUT_RGXKICKRDM;

#endif /* COMMON_RGXRAY_BRIDGE_H */
