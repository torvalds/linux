/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#ifndef COMMON_RGXRAY_BRIDGE_H
#define COMMON_RGXRAY_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_devmem.h"
#include "devicemem_typedefs.h"


#define PVRSRV_BRIDGE_RGXRAY_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMFREELIST			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMFREELIST			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXRAY_RGXKICKRS			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXRAY_RGXKICKVRDM			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXRAY_RGXCREATERAYCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRAYCONTEXT			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+7
#define PVRSRV_BRIDGE_RGXRAY_RGXSETRAYCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+8
#define PVRSRV_BRIDGE_RGXRAY_CMD_LAST			(PVRSRV_BRIDGE_RGXRAY_CMD_FIRST+8)


/*******************************************
            RGXCreateRPMFreeList          
 *******************************************/

/* Bridge in structure for RGXCreateRPMFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERPMFREELIST_TAG
{
	IMG_HANDLE hRPMContext;
	IMG_UINT32 ui32InitFLPages;
	IMG_UINT32 ui32GrowFLPages;
	IMG_DEV_VIRTADDR sFreeListDevVAddr;
	IMG_BOOL bIsExternal;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCREATERPMFREELIST;

/* Bridge out structure for RGXCreateRPMFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERPMFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
	IMG_UINT32 ui32HWFreeList;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCREATERPMFREELIST;


/*******************************************
            RGXDestroyRPMFreeList          
 *******************************************/

/* Bridge in structure for RGXDestroyRPMFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRPMFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDESTROYRPMFREELIST;

/* Bridge out structure for RGXDestroyRPMFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRPMFREELIST_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDESTROYRPMFREELIST;


/*******************************************
            RGXCreateRPMContext          
 *******************************************/

/* Bridge in structure for RGXCreateRPMContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERPMCONTEXT_TAG
{
	IMG_UINT32 ui32TotalRPMPages;
	IMG_UINT32 ui32Log2DopplerPageSize;
	IMG_DEV_VIRTADDR sSceneMemoryBaseAddr;
	IMG_DEV_VIRTADDR sDopplerHeapBaseAddr;
	IMG_HANDLE hSceneHeap;
	IMG_DEV_VIRTADDR sRPMPageTableBaseAddr;
	IMG_HANDLE hRPMPageTableHeap;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCREATERPMCONTEXT;

/* Bridge out structure for RGXCreateRPMContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERPMCONTEXT_TAG
{
	IMG_HANDLE hCleanupCookie;
	IMG_HANDLE hHWMemDesc;
	IMG_UINT32 ui32HWFrameData;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCREATERPMCONTEXT;


/*******************************************
            RGXDestroyRPMContext          
 *******************************************/

/* Bridge in structure for RGXDestroyRPMContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRPMCONTEXT_TAG
{
	IMG_HANDLE hCleanupCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDESTROYRPMCONTEXT;

/* Bridge out structure for RGXDestroyRPMContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRPMCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDESTROYRPMCONTEXT;


/*******************************************
            RGXKickRS          
 *******************************************/

/* Bridge in structure for RGXKickRS */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKRS_TAG
{
	IMG_HANDLE hRayContext;
	IMG_UINT32 ui32ClientCacheOpSeqNum;
	IMG_UINT32 ui32ClientFenceCount;
	IMG_HANDLE * phClientFenceUFOSyncPrimBlock;
	IMG_UINT32 * pui32ClientFenceSyncOffset;
	IMG_UINT32 * pui32ClientFenceValue;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_HANDLE * phClientUpdateUFOSyncPrimBlock;
	IMG_UINT32 * pui32ClientUpdateSyncOffset;
	IMG_UINT32 * pui32ClientUpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerSyncFlags;
	IMG_HANDLE * phServerSyncs;
	IMG_UINT32 ui32CmdSize;
	IMG_BYTE * psDMCmd;
	IMG_UINT32 ui32FCCmdSize;
	IMG_BYTE * psFCDMCmd;
	IMG_UINT32 ui32FrameContext;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32ExtJobRef;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXKICKRS;

/* Bridge out structure for RGXKickRS */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKRS_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXKICKRS;


/*******************************************
            RGXKickVRDM          
 *******************************************/

/* Bridge in structure for RGXKickVRDM */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKVRDM_TAG
{
	IMG_HANDLE hRayContext;
	IMG_UINT32 ui32ClientCacheOpSeqNum;
	IMG_UINT32 ui32ClientFenceCount;
	IMG_HANDLE * phClientFenceUFOSyncPrimBlock;
	IMG_UINT32 * pui32ClientFenceSyncOffset;
	IMG_UINT32 * pui32ClientFenceValue;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_HANDLE * phClientUpdateUFOSyncPrimBlock;
	IMG_UINT32 * pui32ClientUpdateSyncOffset;
	IMG_UINT32 * pui32ClientUpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerSyncFlags;
	IMG_HANDLE * phServerSyncs;
	IMG_UINT32 ui32CmdSize;
	IMG_BYTE * psDMCmd;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32ExtJobRef;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXKICKVRDM;

/* Bridge out structure for RGXKickVRDM */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKVRDM_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXKICKVRDM;


/*******************************************
            RGXCreateRayContext          
 *******************************************/

/* Bridge in structure for RGXCreateRayContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT_TAG
{
	IMG_UINT32 ui32Priority;
	IMG_DEV_VIRTADDR sMCUFenceAddr;
	IMG_DEV_VIRTADDR sVRMCallStackAddr;
	IMG_UINT32 ui32FrameworkCmdSize;
	IMG_BYTE * psFrameworkCmd;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT;

/* Bridge out structure for RGXCreateRayContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT_TAG
{
	IMG_HANDLE hRayContext;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT;


/*******************************************
            RGXDestroyRayContext          
 *******************************************/

/* Bridge in structure for RGXDestroyRayContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT_TAG
{
	IMG_HANDLE hRayContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT;

/* Bridge out structure for RGXDestroyRayContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT;


/*******************************************
            RGXSetRayContextPriority          
 *******************************************/

/* Bridge in structure for RGXSetRayContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETRAYCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hRayContext;
	IMG_UINT32 ui32Priority;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXSETRAYCONTEXTPRIORITY;

/* Bridge out structure for RGXSetRayContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETRAYCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXSETRAYCONTEXTPRIORITY;


#endif /* COMMON_RGXRAY_BRIDGE_H */
