/*************************************************************************/ /*!
@File           rgxworkest.h
@Title          RGX Workload Estimation Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the kernel mode workload estimation functionality.
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

#ifndef RGXWORKEST_H
#define RGXWORKEST_H

#include "img_types.h"
#include "rgxta3d.h"


void WorkEstInitTA3D(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

void WorkEstDeInitTA3D(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

void WorkEstInitCompute(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

void WorkEstDeInitCompute(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

void WorkEstInitTDM(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

void WorkEstDeInitTDM(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData);

PVRSRV_ERROR WorkEstPrepare(PVRSRV_RGXDEV_INFO        *psDevInfo,
                            WORKEST_HOST_DATA         *psWorkEstHostData,
                            WORKLOAD_MATCHING_DATA    *psWorkloadMatchingData,
                            const RGXFWIF_CCB_CMD_TYPE eDMCmdType,
                            const RGX_WORKLOAD        *psWorkloadCharsIn,
                            IMG_UINT64                ui64DeadlineInus,
                            RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData);

PVRSRV_ERROR WorkEstRetire(PVRSRV_RGXDEV_INFO *psDevInfo,
						   RGXFWIF_WORKEST_FWCCB_CMD *psReturnCmd);

void WorkEstHashLockCreate(POS_LOCK *ppsHashLock);

void WorkEstHashLockDestroy(POS_LOCK psHashLock);

void WorkEstCheckFirmwareCCB(PVRSRV_RGXDEV_INFO *psDevInfo);

#endif /* RGXWORKEST_H */
