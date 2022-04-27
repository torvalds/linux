/*******************************************************************************
@File
@Title          Common bridge header for rgxta3d
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxta3d
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

#ifndef COMMON_RGXTA3D_BRIDGE_H
#define COMMON_RGXTA3D_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "rgx_fwif_shared.h"
#include "devicemem_typedefs.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATASET			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATASET			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+7
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+8
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+9
#define PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+10
#define PVRSRV_BRIDGE_RGXTA3D_RGXRENDERCONTEXTSTALLED			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+11
#define PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D2			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+12
#define PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPROPERTY			PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+13
#define PVRSRV_BRIDGE_RGXTA3D_CMD_LAST			(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+13)

/*******************************************
            RGXCreateHWRTDataSet
 *******************************************/

/* Bridge in structure for RGXCreateHWRTDataSet */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATASET_TAG
{
	IMG_DEV_VIRTADDR sPMMlistDevVAddr0;
	IMG_DEV_VIRTADDR sPMMlistDevVAddr1;
	IMG_DEV_VIRTADDR sTailPtrsDevVAddr;
	IMG_DEV_VIRTADDR ssMacrotileArrayDevVAddr0;
	IMG_DEV_VIRTADDR ssMacrotileArrayDevVAddr1;
	IMG_DEV_VIRTADDR ssRTCDevVAddr;
	IMG_DEV_VIRTADDR ssRgnHeaderDevVAddr0;
	IMG_DEV_VIRTADDR ssRgnHeaderDevVAddr1;
	IMG_DEV_VIRTADDR ssVHeapTableDevVAddr;
	IMG_UINT64 ui64FlippedMultiSampleCtl;
	IMG_UINT64 ui64MultiSampleCtl;
	IMG_UINT64 ui64uiRgnHeaderSize;
	IMG_HANDLE *phapsFreeLists;
	IMG_UINT32 ui32MTileStride;
	IMG_UINT32 ui32PPPScreen;
	IMG_UINT32 ui32TEAA;
	IMG_UINT32 ui32TEMTILE1;
	IMG_UINT32 ui32TEMTILE2;
	IMG_UINT32 ui32TEScreen;
	IMG_UINT32 ui32TPCSize;
	IMG_UINT32 ui32TPCStride;
	IMG_UINT32 ui32ui32ISPMergeLowerX;
	IMG_UINT32 ui32ui32ISPMergeLowerY;
	IMG_UINT32 ui32ui32ISPMergeScaleX;
	IMG_UINT32 ui32ui32ISPMergeScaleY;
	IMG_UINT32 ui32ui32ISPMergeUpperX;
	IMG_UINT32 ui32ui32ISPMergeUpperY;
	IMG_UINT32 ui32ui32ISPMtileSize;
	IMG_UINT16 ui16MaxRTs;
} __packed PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATASET;

/* Bridge out structure for RGXCreateHWRTDataSet */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATASET_TAG
{
	IMG_HANDLE hKmHwRTDataSet0;
	IMG_HANDLE hKmHwRTDataSet1;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATASET;

/*******************************************
            RGXDestroyHWRTDataSet
 *******************************************/

/* Bridge in structure for RGXDestroyHWRTDataSet */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATASET_TAG
{
	IMG_HANDLE hKmHwRTDataSet;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATASET;

/* Bridge out structure for RGXDestroyHWRTDataSet */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATASET_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATASET;

/*******************************************
            RGXCreateZSBuffer
 *******************************************/

/* Bridge in structure for RGXCreateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER_TAG
{
	IMG_HANDLE hPMR;
	IMG_HANDLE hReservation;
	PVRSRV_MEMALLOCFLAGS_T uiMapFlags;
} __packed PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER;

/* Bridge out structure for RGXCreateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferKM;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER;

/*******************************************
            RGXDestroyZSBuffer
 *******************************************/

/* Bridge in structure for RGXDestroyZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferMemDesc;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER;

/* Bridge out structure for RGXDestroyZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER;

/*******************************************
            RGXPopulateZSBuffer
 *******************************************/

/* Bridge in structure for RGXPopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferKM;
} __packed PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER;

/* Bridge out structure for RGXPopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsPopulation;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER;

/*******************************************
            RGXUnpopulateZSBuffer
 *******************************************/

/* Bridge in structure for RGXUnpopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsPopulation;
} __packed PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER;

/* Bridge out structure for RGXUnpopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER;

/*******************************************
            RGXCreateFreeList
 *******************************************/

/* Bridge in structure for RGXCreateFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEFREELIST_TAG
{
	IMG_DEV_VIRTADDR spsFreeListDevVAddr;
	IMG_DEVMEM_OFFSET_T uiPMROffset;
	IMG_HANDLE hMemCtxPrivData;
	IMG_HANDLE hsFreeListPMR;
	IMG_HANDLE hsGlobalFreeList;
	IMG_BOOL bbFreeListCheck;
	IMG_UINT32 ui32GrowFLPages;
	IMG_UINT32 ui32GrowParamThreshold;
	IMG_UINT32 ui32InitFLPages;
	IMG_UINT32 ui32MaxFLPages;
} __packed PVRSRV_BRIDGE_IN_RGXCREATEFREELIST;

/* Bridge out structure for RGXCreateFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST;

/*******************************************
            RGXDestroyFreeList
 *******************************************/

/* Bridge in structure for RGXDestroyFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST;

/* Bridge out structure for RGXDestroyFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST;

/*******************************************
            RGXCreateRenderContext
 *******************************************/

/* Bridge in structure for RGXCreateRenderContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT_TAG
{
	IMG_DEV_VIRTADDR sVDMCallStackAddr;
	IMG_UINT64 ui64RobustnessAddress;
	IMG_HANDLE hPrivData;
	IMG_BYTE *pui8FrameworkCmd;
	IMG_BYTE *pui8StaticRenderContextState;
	IMG_UINT32 ui32ContextFlags;
	IMG_UINT32 ui32FrameworkCmdSize;
	IMG_UINT32 ui32Max3DDeadlineMS;
	IMG_UINT32 ui32MaxTADeadlineMS;
	IMG_UINT32 ui32PackedCCBSizeU8888;
	IMG_UINT32 ui32Priority;
	IMG_UINT32 ui32StaticRenderContextStateSize;
} __packed PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT;

/* Bridge out structure for RGXCreateRenderContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT_TAG
{
	IMG_HANDLE hRenderContext;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT;

/*******************************************
            RGXDestroyRenderContext
 *******************************************/

/* Bridge in structure for RGXDestroyRenderContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT_TAG
{
	IMG_HANDLE hCleanupCookie;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT;

/* Bridge out structure for RGXDestroyRenderContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT;

/*******************************************
            RGXSetRenderContextPriority
 *******************************************/

/* Bridge in structure for RGXSetRenderContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hRenderContext;
	IMG_UINT32 ui32Priority;
} __packed PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY;

/* Bridge out structure for RGXSetRenderContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY;

/*******************************************
            RGXRenderContextStalled
 *******************************************/

/* Bridge in structure for RGXRenderContextStalled */
typedef struct PVRSRV_BRIDGE_IN_RGXRENDERCONTEXTSTALLED_TAG
{
	IMG_HANDLE hRenderContext;
} __packed PVRSRV_BRIDGE_IN_RGXRENDERCONTEXTSTALLED;

/* Bridge out structure for RGXRenderContextStalled */
typedef struct PVRSRV_BRIDGE_OUT_RGXRENDERCONTEXTSTALLED_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXRENDERCONTEXTSTALLED;

/*******************************************
            RGXKickTA3D2
 *******************************************/

/* Bridge in structure for RGXKickTA3D2 */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKTA3D2_TAG
{
	IMG_UINT64 ui64Deadline;
	IMG_HANDLE hKMHWRTDataSet;
	IMG_HANDLE hMSAAScratchBuffer;
	IMG_HANDLE hPRFenceUFOSyncPrimBlock;
	IMG_HANDLE hRenderContext;
	IMG_HANDLE hZSBuffer;
	IMG_UINT32 *pui32Client3DUpdateSyncOffset;
	IMG_UINT32 *pui32Client3DUpdateValue;
	IMG_UINT32 *pui32ClientTAFenceSyncOffset;
	IMG_UINT32 *pui32ClientTAFenceValue;
	IMG_UINT32 *pui32ClientTAUpdateSyncOffset;
	IMG_UINT32 *pui32ClientTAUpdateValue;
	IMG_UINT32 *pui32SyncPMRFlags;
	IMG_BYTE *pui83DCmd;
	IMG_BYTE *pui83DPRCmd;
	IMG_BYTE *pui8TACmd;
	IMG_CHAR *puiUpdateFenceName;
	IMG_CHAR *puiUpdateFenceName3D;
	IMG_HANDLE *phClient3DUpdateSyncPrimBlock;
	IMG_HANDLE *phClientTAFenceSyncPrimBlock;
	IMG_HANDLE *phClientTAUpdateSyncPrimBlock;
	IMG_HANDLE *phSyncPMRs;
	IMG_BOOL bbAbort;
	IMG_BOOL bbKick3D;
	IMG_BOOL bbKickPR;
	IMG_BOOL bbKickTA;
	PVRSRV_FENCE hCheckFence;
	PVRSRV_FENCE hCheckFence3D;
	PVRSRV_TIMELINE hUpdateTimeline;
	PVRSRV_TIMELINE hUpdateTimeline3D;
	IMG_UINT32 ui323DCmdSize;
	IMG_UINT32 ui323DPRCmdSize;
	IMG_UINT32 ui32Client3DUpdateCount;
	IMG_UINT32 ui32ClientCacheOpSeqNum;
	IMG_UINT32 ui32ClientTAFenceCount;
	IMG_UINT32 ui32ClientTAUpdateCount;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32FRFenceUFOSyncOffset;
	IMG_UINT32 ui32FRFenceValue;
	IMG_UINT32 ui32NumberOfDrawCalls;
	IMG_UINT32 ui32NumberOfIndices;
	IMG_UINT32 ui32NumberOfMRTs;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32RenderTargetSize;
	IMG_UINT32 ui32SyncPMRCount;
	IMG_UINT32 ui32TACmdSize;
} __packed PVRSRV_BRIDGE_IN_RGXKICKTA3D2;

/* Bridge out structure for RGXKickTA3D2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKTA3D2_TAG
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE hUpdateFence;
	PVRSRV_FENCE hUpdateFence3D;
} __packed PVRSRV_BRIDGE_OUT_RGXKICKTA3D2;

/*******************************************
            RGXSetRenderContextProperty
 *******************************************/

/* Bridge in structure for RGXSetRenderContextProperty */
typedef struct PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Input;
	IMG_HANDLE hRenderContext;
	IMG_UINT32 ui32Property;
} __packed PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPROPERTY;

/* Bridge out structure for RGXSetRenderContextProperty */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Output;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPROPERTY;

#endif /* COMMON_RGXTA3D_BRIDGE_H */
