/*************************************************************************/ /*!
@File           vmm_pvz_server.h
@Title          VM manager para-virtualization interface helper routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header provides API(s) available to VM manager, this must be
                called to close the loop during guest para-virtualization calls.
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

#ifndef VMM_PVZ_SERVER_H
#define VMM_PVZ_SERVER_H

#include "vmm_impl.h"
#include "img_types.h"
#include "pvrsrv_error.h"

/*!
*******************************************************************************
 @Function      PvzServerMapDevPhysHeap
 @Description   The VM manager calls this in response to guest PVZ interface
                call pfnMapDevPhysHeap.
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR
PvzServerMapDevPhysHeap(IMG_UINT32 ui32DriverID,
						IMG_UINT32 ui32DevID,
						IMG_UINT64 ui64Size,
						IMG_UINT64 ui64PAddr);

/*!
*******************************************************************************
 @Function      PvzServerUnmapDevPhysHeap
 @Description   The VM manager calls this in response to guest PVZ interface
                call pfnUnmapDevPhysHeap.
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR
PvzServerUnmapDevPhysHeap(IMG_UINT32 ui32DriverID,
						  IMG_UINT32 ui32DevID);

/*!
*******************************************************************************
 @Function      PvzServerOnVmOnline
 @Description   The VM manager calls this when guest VM machine comes online.
                The host driver will initialize the FW if it has not done so
                already.
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR
PvzServerOnVmOnline(IMG_UINT32 ui32DriverID,
					IMG_UINT32 ui32DevID);

/*!
*******************************************************************************
 @Function      PvzServerOnVmOffline
 @Description   The VM manager calls this when a guest VM machine is about to
                go offline. The VM manager might have unmapped the GPU kick
                register for such VM but not the GPU memory until the call
                returns. Once the function returns, the FW does not hold any
                reference for such VM and no workloads from it are running in
                the GPU and it is safe to remove the memory for such VM.
 @Return        PVRSRV_OK on success. PVRSRV_ERROR_TIMEOUT if for some reason
                the FW is taking too long to clean-up the resources of the
                DriverID. Otherwise, a PVRSRV_ERROR code.
******************************************************************************/
PVRSRV_ERROR
PvzServerOnVmOffline(IMG_UINT32 ui32DriverID,
					 IMG_UINT32 ui32DevID);

/*!
*******************************************************************************
 @Function      PvzServerVMMConfigure
 @Description   The VM manager calls this to configure several parameters like
                HCS or isolation.
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR
PvzServerVMMConfigure(VMM_CONF_PARAM eVMMParamType,
					  IMG_UINT32 ui32ParamValue,
					  IMG_UINT32 ui32DevID);

#endif /* VMM_PVZ_SERVER_H */

/******************************************************************************
 End of file (vmm_pvz_server.h)
******************************************************************************/
