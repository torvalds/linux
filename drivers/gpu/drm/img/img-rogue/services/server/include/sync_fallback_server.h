/**************************************************************************/ /*!
@File
@Title          Fallback sync interface
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

#ifndef SYNC_FALLBACK_SERVER_H
#define SYNC_FALLBACK_SERVER_H

#include "img_types.h"
#include "sync_checkpoint.h"
#include "device.h"
#include "connection_server.h"


typedef struct _PVRSRV_TIMELINE_SERVER_ PVRSRV_TIMELINE_SERVER;
typedef struct _PVRSRV_FENCE_SERVER_ PVRSRV_FENCE_SERVER;
typedef struct _PVRSRV_FENCE_EXPORT_ PVRSRV_FENCE_EXPORT;

typedef struct _PVRSRV_SYNC_PT_ PVRSRV_SYNC_PT;

#define SYNC_FB_TIMELINE_MAX_LENGTH PVRSRV_SYNC_NAME_LENGTH
#define SYNC_FB_FENCE_MAX_LENGTH PVRSRV_SYNC_NAME_LENGTH

/*****************************************************************************/
/*                                                                           */
/*                         SW SPECIFIC FUNCTIONS                             */
/*                                                                           */
/*****************************************************************************/

PVRSRV_ERROR SyncFbTimelineCreateSW(IMG_UINT32 uiTimelineNameSize,
                                    const IMG_CHAR *pszTimelineName,
                                    PVRSRV_TIMELINE_SERVER **ppsTimeline);

PVRSRV_ERROR SyncFbFenceCreateSW(PVRSRV_TIMELINE_SERVER *psTimeline,
                                 IMG_UINT32 uiFenceNameSize,
                                 const IMG_CHAR *pszFenceName,
                                 PVRSRV_FENCE_SERVER **ppsOutputFence,
                                 IMG_UINT64 *pui64SyncPtIdx);
PVRSRV_ERROR SyncFbSWTimelineFenceCreateKM(PVRSRV_TIMELINE iSWTimeline,
                                           const IMG_CHAR *pszFenceName,
                                           PVRSRV_FENCE *piOutputFence,
                                           IMG_UINT64* pui64SyncPtIdx);

PVRSRV_ERROR SyncFbTimelineAdvanceSW(PVRSRV_TIMELINE_SERVER *psTimeline,
                                     IMG_UINT64 *pui64SyncPtIdx);
PVRSRV_ERROR SyncFbSWTimelineAdvanceKM(void *pvSWTimelineObj,
                                       IMG_UINT64* pui64SyncPtIdx);

/*****************************************************************************/
/*                                                                           */
/*                         PVR SPECIFIC FUNCTIONS                            */
/*                                                                           */
/*****************************************************************************/

PVRSRV_ERROR SyncFbTimelineCreatePVR(IMG_UINT32 uiTimelineNameSize,
                                     const IMG_CHAR *pszTimelineName,
                                     PVRSRV_TIMELINE_SERVER **ppsTimeline);

PVRSRV_ERROR SyncFbFenceCreatePVR(const IMG_CHAR *pszName,
                                  PVRSRV_TIMELINE iTl,
                                  PSYNC_CHECKPOINT_CONTEXT hSyncCheckpointContext,
                                  PVRSRV_FENCE *piOutFence,
                                  IMG_UINT64 *puiFenceUID,
                                  void **ppvFenceFinaliseData,
                                  PSYNC_CHECKPOINT *ppsOutCheckpoint,
                                  void **ppvTimelineUpdateSync,
                                  IMG_UINT32 *puiTimelineUpdateValue);

PVRSRV_ERROR SyncFbFenceResolvePVR(PSYNC_CHECKPOINT_CONTEXT psContext,
                                   PVRSRV_FENCE iFence,
                                   IMG_UINT32 *puiNumCheckpoints,
                                   PSYNC_CHECKPOINT **papsCheckpoints,
                                   IMG_UINT64 *puiFenceUID);

/*****************************************************************************/
/*                                                                           */
/*                         GENERIC FUNCTIONS                                 */
/*                                                                           */
/*****************************************************************************/

PVRSRV_ERROR SyncFbGetFenceObj(PVRSRV_FENCE iFence,
                               void **ppvFenceObj);

PVRSRV_ERROR SyncFbSWGetTimelineObj(PVRSRV_TIMELINE iSWTimeline,
                                    void **ppvSWTimelineObj);

PVRSRV_ERROR SyncFbTimelineRelease(PVRSRV_TIMELINE_SERVER *psTl);

PVRSRV_ERROR SyncFbFenceRelease(PVRSRV_FENCE_SERVER *psFence);
PVRSRV_ERROR SyncFbFenceReleaseKM(void *pvFenceObj);

PVRSRV_ERROR SyncFbFenceDup(PVRSRV_FENCE_SERVER *psInFence,
                            PVRSRV_FENCE_SERVER **ppsOutFence);

PVRSRV_ERROR SyncFbFenceMerge(PVRSRV_FENCE_SERVER *psInFence1,
                              PVRSRV_FENCE_SERVER *psInFence2,
                              IMG_UINT32 uiFenceNameSize,
                              const IMG_CHAR *pszFenceName,
                              PVRSRV_FENCE_SERVER **ppsOutFence);

PVRSRV_ERROR SyncFbFenceWait(PVRSRV_FENCE_SERVER *psFence,
                             IMG_UINT32 uiTimeout);

PVRSRV_ERROR SyncFbFenceDump(PVRSRV_FENCE_SERVER *psFence,
                             IMG_UINT32 uiLine,
                             IMG_UINT32 uiFileNameLength,
                             const IMG_CHAR *pszFile,
                             IMG_UINT32 uiModuleLength,
                             const IMG_CHAR *pszModule,
                             IMG_UINT32 uiDescLength,
                             const IMG_CHAR *pszDesc);

PVRSRV_ERROR SyncFbDumpFenceKM(void *pvSWFenceObj,
	                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
	                           void *pvDumpDebugFile);

PVRSRV_ERROR SyncFbSWDumpTimelineKM(void *pvSWTimelineObj,
                                    DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                    void *pvDumpDebugFile);

PVRSRV_ERROR SyncFbRegisterSyncFunctions(void);

PVRSRV_ERROR SyncFbRegisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

PVRSRV_ERROR SyncFbDeregisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_UINT32 SyncFbDumpInfoOnStalledUFOs(IMG_UINT32 nr_ufos, IMG_UINT32 *vaddrs);

IMG_BOOL SyncFbCheckpointHasSignalled(IMG_UINT32 ui32FwAddr, IMG_UINT32 ui32Value);

/*****************************************************************************/
/*                                                                           */
/*                       IMPORT/EXPORT FUNCTIONS                             */
/*                                                                           */
/*****************************************************************************/

#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR SyncFbFenceExportInsecure(PVRSRV_FENCE_SERVER *psFence,
                                       PVRSRV_FENCE_EXPORT **ppExport);

PVRSRV_ERROR SyncFbFenceExportDestroyInsecure(PVRSRV_FENCE_EXPORT *psExport);

PVRSRV_ERROR SyncFbFenceImportInsecure(CONNECTION_DATA *psConnection,
                                       PVRSRV_DEVICE_NODE *psDevice,
                                       PVRSRV_FENCE_EXPORT *psImport,
                                       PVRSRV_FENCE_SERVER **psFence);
#endif /* defined(SUPPORT_INSECURE_EXPORT) */

PVRSRV_ERROR SyncFbFenceExportSecure(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE * psDevNode,
                                     PVRSRV_FENCE_SERVER *psFence,
                                     IMG_SECURE_TYPE *phSecure,
                                     PVRSRV_FENCE_EXPORT **ppsExport,
                                     CONNECTION_DATA **ppsSecureConnection);

PVRSRV_ERROR SyncFbFenceExportDestroySecure(PVRSRV_FENCE_EXPORT *psExport);

PVRSRV_ERROR SyncFbFenceImportSecure(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDevice,
                                     IMG_SECURE_TYPE hSecure,
                                     PVRSRV_FENCE_SERVER **psFence);

#endif /* SYNC_FALLBACK_SERVER_H */
