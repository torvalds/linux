/*************************************************************************/ /*!
@File           tutils_km.h
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel services functions for calls to tutils (testing utils)
                layer in the server
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
#ifndef TUTILS_KM_H
#define TUTILS_KM_H

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "connection_server.h"
#include "device.h"
#include "pvrsrv_sync_km.h"


PVRSRV_ERROR ServerTestIoctlKM(CONNECTION_DATA *psConnection,
				PVRSRV_DEVICE_NODE *psDeviceNode,
				IMG_UINT32  uiCmd,
				IMG_PBYTE   uiIn1,
				IMG_UINT32  uiIn2,
				IMG_UINT32*	puiOut1,
				IMG_UINT32* puiOut2);

PVRSRV_ERROR PowMonTestIoctlKM(IMG_UINT32  uiCmd,
				  IMG_UINT32  uiIn1,
				  IMG_UINT32  uiIn2,
				  IMG_UINT32  *puiOut1,
				  IMG_UINT32  *puiOut2);

PVRSRV_ERROR SyncCheckpointTestIoctlKM(CONNECTION_DATA *psConnection,
				  PVRSRV_DEVICE_NODE *psDeviceNode,
				  IMG_UINT32  uiCmd,
				  IMG_UINT32  uiIn1,
				  IMG_UINT32  uiIn2,
				  const IMG_CHAR *pszInName,
				  IMG_UINT32  *puiOut1,
				  IMG_UINT32  *puiOut2,
				  IMG_UINT8   *puiOut3);

IMG_EXPORT
PVRSRV_ERROR DevmemIntAllocHostMemKM(IMG_DEVMEM_SIZE_T ui32Size,
                                     PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                     IMG_UINT32 ui32LableLength,
                                     const IMG_CHAR *pszAllocLabel,
                                     PMR **ppsPMR);

PVRSRV_ERROR DevmemIntFreeHostMemKM(PMR *psPMR);

IMG_EXPORT
PVRSRV_ERROR PowerTestIoctlKM(IMG_UINT32  uiCmd,
				  IMG_UINT32  uiIn1,
				  IMG_UINT32  uiIn2,
				  IMG_UINT32  *puiOut1,
				  IMG_UINT32  *puiOut2);

PVRSRV_ERROR TestIOCTLSyncFbFenceSignalPVR(CONNECTION_DATA * psConnection,
                                           PVRSRV_DEVICE_NODE *psDevNode,
                                           void *psFence);

PVRSRV_ERROR TestIOCTLSyncFbFenceCreatePVR(CONNECTION_DATA * psConnection,
                                            PVRSRV_DEVICE_NODE *psDevNode,
                                            IMG_UINT32 uiNameLength,
                                            const IMG_CHAR *pszName,
                                            PVRSRV_TIMELINE iTL,
                                            PVRSRV_FENCE *piOutFence);

PVRSRV_ERROR TestIOCTLSyncFbFenceResolvePVR(CONNECTION_DATA * psConnection,
                                            PVRSRV_DEVICE_NODE *psDevNode,
                                            PVRSRV_FENCE iFence);
PVRSRV_ERROR TestIOCTLSyncFbSWTimelineAdvance(CONNECTION_DATA * psConnection,
                                              PVRSRV_DEVICE_NODE *psDevNode,
                                              PVRSRV_TIMELINE iSWTl);

PVRSRV_ERROR TestIOCTLSyncFbSWFenceCreate(CONNECTION_DATA * psConnection,
                                          PVRSRV_DEVICE_NODE *psDevNode,
                                          PVRSRV_TIMELINE iTl,
                                          IMG_UINT32 uiFenceNameLength,
                                          const IMG_CHAR *pszFenceName,
                                          PVRSRV_FENCE *piFence);



PVRSRV_ERROR TestIOCTLSyncSWTimelineFenceCreateKM(CONNECTION_DATA * psConnection,
                                                  PVRSRV_DEVICE_NODE *psDevNode,
                                                  PVRSRV_TIMELINE sTimeline,
                                                  IMG_UINT32 uiNameLength,
                                                  const IMG_CHAR *pszFenceName,
                                                  PVRSRV_FENCE *psOutFence);

PVRSRV_ERROR TestIOCTLSyncSWTimelineAdvanceKM(CONNECTION_DATA * psConnection,
                                              PVRSRV_DEVICE_NODE *psDevNode,
                                              PVRSRV_TIMELINE sTimeline);

PVRSRV_ERROR TestIOCTLIsTimelineValidKM(PVRSRV_TIMELINE sTimeline,
                                        IMG_BOOL *bResult);

PVRSRV_ERROR TestIOCTLIsFenceValidKM(PVRSRV_FENCE sFence,
                                     IMG_BOOL *bResult);

PVRSRV_ERROR TestIOCTLSyncCheckpointResolveFenceKM(CONNECTION_DATA * psConnection,
                                                   PVRSRV_DEVICE_NODE *psDevNode,
                                                   PVRSRV_FENCE hFence,
                                                   IMG_UINT32 *pui32NumSyncCheckpoints);

PVRSRV_ERROR TestIOCTLSyncCheckpointCreateFenceKM(CONNECTION_DATA *psConnection,
                                                  PVRSRV_DEVICE_NODE *psDevNode,
                                                  IMG_CHAR *pszFenceName,
                                                  PVRSRV_TIMELINE hTimeline,
                                                  PVRSRV_FENCE *phOutFence,
                                                  IMG_UINT64 *puiUpdateFenceUID);

PVRSRV_ERROR TestIOCTLWriteByteKM(IMG_BYTE ui8WriteData);

PVRSRV_ERROR TestIOCTLReadByteKM(IMG_BYTE *pui8ReadData);
#endif	/* TUTILS_KM_H */
