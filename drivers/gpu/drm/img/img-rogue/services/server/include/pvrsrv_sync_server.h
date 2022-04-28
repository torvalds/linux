/**************************************************************************/ /*!
@File
@Title          Fence sync server interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
*/ /***************************************************************************/

#ifndef PVRSRV_SYNC_SERVER_H
#define PVRSRV_SYNC_SERVER_H

#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
#include "sync_fallback_server.h"
#include "pvr_notifier.h"
#include "img_types.h"
#include "pvrsrv_sync_km.h"
#elif defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif

#include "rgxhwperf.h"

#define SYNC_SW_TIMELINE_MAX_LENGTH PVRSRV_SYNC_NAME_LENGTH
#define SYNC_SW_FENCE_MAX_LENGTH PVRSRV_SYNC_NAME_LENGTH

typedef struct _SYNC_TIMELINE_OBJ_
{
	void *pvTlObj; /* Implementation specific timeline object */

	PVRSRV_TIMELINE hTimeline; /* Reference to implementation-independent timeline object */
} SYNC_TIMELINE_OBJ;

typedef struct _SYNC_FENCE_OBJ_
{
	void *pvFenceObj; /* Implementation specific fence object */

	PVRSRV_FENCE hFence; /* Reference to implementation-independent fence object */
} SYNC_FENCE_OBJ;

static inline void SyncClearTimelineObj(SYNC_TIMELINE_OBJ *psSTO)
{
	psSTO->pvTlObj = NULL;
	psSTO->hTimeline = PVRSRV_NO_TIMELINE;
}

static inline IMG_BOOL SyncIsTimelineObjValid(const SYNC_TIMELINE_OBJ *psSTO)
{
	return psSTO->pvTlObj != NULL;
}

static inline void SyncClearFenceObj(SYNC_FENCE_OBJ *psSFO)
{
	psSFO->pvFenceObj = NULL;
	psSFO->hFence = PVRSRV_NO_FENCE;
}

static inline IMG_BOOL SyncIsFenceObjValid(const SYNC_FENCE_OBJ *psSFO)
{
	return psSFO->pvFenceObj != NULL;
}


/* Mapping of each required function to its appropriate sync-implementation function */
#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
	#define SyncFenceWaitKM_                SyncFbFenceWait
	#define SyncGetFenceObj_                SyncFbGetFenceObj
	#define SyncFenceReleaseKM_             SyncFbFenceReleaseKM
	#define SyncSWTimelineFenceCreateKM_    SyncFbSWTimelineFenceCreateKM
	#define SyncSWTimelineAdvanceKM_        SyncFbSWTimelineAdvanceKM
	#define SyncSWGetTimelineObj_           SyncFbSWGetTimelineObj
	#define SyncSWTimelineReleaseKM_        SyncFbTimelineRelease
	#define SyncDumpFence_                  SyncFbDumpFenceKM
	#define SyncSWDumpTimeline_             SyncFbSWDumpTimelineKM
#elif defined(SUPPORT_NATIVE_FENCE_SYNC)
	#define SyncFenceWaitKM_                pvr_sync_fence_wait
	#define SyncGetFenceObj_                pvr_sync_fence_get
	#define SyncFenceReleaseKM_             pvr_sync_fence_release
	#define SyncSWTimelineFenceCreateKM_    pvr_sync_sw_timeline_fence_create
	#define SyncSWTimelineAdvanceKM_        pvr_sync_sw_timeline_advance
	#define SyncSWGetTimelineObj_           pvr_sync_sw_timeline_get
	#define SyncSWTimelineReleaseKM_        pvr_sync_sw_timeline_release
	#define SyncDumpFence_                  sync_dump_fence
	#define SyncSWDumpTimeline_             sync_sw_dump_timeline
#endif

/*************************************************************************/ /*!
@Function       SyncFenceWaitKM

@Description    Wait for all the sync points in the fence to be signalled.

@Input          psFenceObj          Fence to wait on

@Input          ui32TimeoutInMs     Maximum time to wait (in milliseconds)

@Return         PVRSRV_OK               once the fence has been passed (all
                                        containing check points have either
                                        signalled or errored)
                PVRSRV_ERROR_TIMEOUT    if the poll has exceeded the timeout
                PVRSRV_ERROR_FAILED_DEPENDENCIES Other sync-impl specific error
*/ /**************************************************************************/
static inline PVRSRV_ERROR
SyncFenceWaitKM(PVRSRV_DEVICE_NODE *psDevNode,
                const SYNC_FENCE_OBJ *psFenceObj,
                IMG_UINT32 ui32TimeoutInMs)
{
	PVRSRV_ERROR eError;

	RGXSRV_HWPERF_SYNC_FENCE_WAIT(psDevNode->pvDevice,
								  BEGIN,
								  OSGetCurrentProcessID(),
								  psFenceObj->hFence,
								  ui32TimeoutInMs);

	eError = SyncFenceWaitKM_(psFenceObj->pvFenceObj, ui32TimeoutInMs);

	RGXSRV_HWPERF_SYNC_FENCE_WAIT(psDevNode->pvDevice,
								  END,
								  OSGetCurrentProcessID(),
								  psFenceObj->hFence,
								  ((eError == PVRSRV_OK) ?
									  RGX_HWPERF_HOST_SYNC_FENCE_WAIT_RESULT_PASSED :
									  ((eError == PVRSRV_ERROR_TIMEOUT) ?
										  RGX_HWPERF_HOST_SYNC_FENCE_WAIT_RESULT_TIMEOUT :
										  RGX_HWPERF_HOST_SYNC_FENCE_WAIT_RESULT_ERROR)));
	return eError;
}

/*************************************************************************/ /*!
@Function       SyncGetFenceObj

@Description    Get the implementation specific server fence object from
                opaque implementation independent PVRSRV_FENCE type.
                When successful, this function gets a reference on the base
                fence, which needs to be dropped using SyncFenceReleaseKM,
                when fence object is no longer in use.

@Input          iFence        Input opaque fence object

@Output         psFenceObj    Pointer to implementation specific fence object

@Return         PVRSRV_ERROR  PVRSRV_OK, on success
*/ /**************************************************************************/
static inline PVRSRV_ERROR
SyncGetFenceObj(PVRSRV_FENCE iFence,
                SYNC_FENCE_OBJ *psFenceObj)
{
	psFenceObj->hFence = iFence;
	return SyncGetFenceObj_(iFence, &psFenceObj->pvFenceObj);
}

/*************************************************************************/ /*!
@Function       SyncFenceReleaseKM

@Description    Release reference on this fence.

@Input          psFenceObj     Fence to be released

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static inline
PVRSRV_ERROR SyncFenceReleaseKM(const SYNC_FENCE_OBJ *psFenceObj)
{
	return SyncFenceReleaseKM_(psFenceObj->pvFenceObj);
}

/*****************************************************************************/
/*                                                                           */
/*                      SW TIMELINE SPECIFIC FUNCTIONS                       */
/*                                                                           */
/*****************************************************************************/

static inline PVRSRV_ERROR
SyncSWTimelineFenceCreateKM(PVRSRV_DEVICE_NODE *psDevNode,
                            PVRSRV_TIMELINE hSWTimeline,
                            const IMG_CHAR *pszFenceName,
                            PVRSRV_FENCE *phOutFence)
{
	IMG_UINT64 ui64SyncPtIdx;
	PVRSRV_ERROR eError;
	eError = SyncSWTimelineFenceCreateKM_(hSWTimeline,
	                                      pszFenceName,
	                                      phOutFence,
	                                      &ui64SyncPtIdx);
	if (eError == PVRSRV_OK)
	{
		RGXSRV_HWPERF_ALLOC_SW_FENCE(psDevNode, OSGetCurrentProcessID(),
		                             *phOutFence, hSWTimeline, ui64SyncPtIdx,
		                             pszFenceName, OSStringLength(pszFenceName));
	}
	return eError;
}

static inline PVRSRV_ERROR
SyncSWTimelineAdvanceKM(PVRSRV_DEVICE_NODE *psDevNode,
                        const SYNC_TIMELINE_OBJ *psSWTimelineObj)
{
	IMG_UINT64 ui64SyncPtIdx;
	PVRSRV_ERROR eError;
	eError = SyncSWTimelineAdvanceKM_(psSWTimelineObj->pvTlObj,
	                                  &ui64SyncPtIdx);

	if (eError == PVRSRV_OK)
	{
		RGXSRV_HWPERF_SYNC_SW_TL_ADV(psDevNode->pvDevice,
		                             OSGetCurrentProcessID(),
		                             psSWTimelineObj->hTimeline,
		                             ui64SyncPtIdx);
	}
	return eError;
}

static inline PVRSRV_ERROR
SyncSWGetTimelineObj(PVRSRV_TIMELINE hSWTimeline,
                     SYNC_TIMELINE_OBJ *psSWTimelineObj)
{
	psSWTimelineObj->hTimeline = hSWTimeline;
	return SyncSWGetTimelineObj_(hSWTimeline, &psSWTimelineObj->pvTlObj);
}

static inline PVRSRV_ERROR
SyncSWTimelineReleaseKM(const SYNC_TIMELINE_OBJ *psSWTimelineObj)
{
	return SyncSWTimelineReleaseKM_(psSWTimelineObj->pvTlObj);
}

static inline PVRSRV_ERROR
SyncDumpFence(const SYNC_FENCE_OBJ *psFenceObj,
              DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
              void *pvDumpDebugFile)
{
	return SyncDumpFence_(psFenceObj->pvFenceObj, pfnDumpDebugPrintf, pvDumpDebugFile);
}

static inline PVRSRV_ERROR
SyncSWDumpTimeline(const SYNC_TIMELINE_OBJ *psSWTimelineObj,
                   DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                   void *pvDumpDebugFile)
{
	return SyncSWDumpTimeline_(psSWTimelineObj->pvTlObj, pfnDumpDebugPrintf, pvDumpDebugFile);
}


#endif /* PVRSRV_SYNC_SERVER_H */
