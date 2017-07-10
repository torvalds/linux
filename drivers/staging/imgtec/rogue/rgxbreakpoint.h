/*************************************************************************/ /*!
@File
@Title          RGX breakpoint functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX breakpoint functionality
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

#if !defined(__RGXBREAKPOINT_H__)
#define __RGXBREAKPOINT_H__

#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgx_fwif_km.h"

/*!
*******************************************************************************
 @Function	PVRSRVRGXSetBreakpointKM

 @Description
	Server-side implementation of RGXSetBreakpoint

 @Input psDeviceNode - RGX Device node
 @Input eDataMaster - Data Master to schedule command for
 @Input hMemCtxPrivData - memory context private data
 @Input ui32BPAddr - Address of breakpoint
 @Input ui32HandlerAddr - Address of breakpoint handler
 @Input ui32BPCtl - Breakpoint controls

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXSetBreakpointKM(CONNECTION_DATA    * psConnection,
                                      PVRSRV_DEVICE_NODE * psDeviceNode,
                                      IMG_HANDLE           hMemCtxPrivData,
                                      RGXFWIF_DM           eFWDataMaster,
                                      IMG_UINT32           ui32BPAddr,
                                      IMG_UINT32           ui32HandlerAddr,
                                      IMG_UINT32           ui32DataMaster);

/*!
*******************************************************************************
 @Function	PVRSRVRGXClearBreakpointKM

 @Description
	Server-side implementation of RGXClearBreakpoint

 @Input psDeviceNode - RGX Device node
 @Input hMemCtxPrivData - memory context private data

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXClearBreakpointKM(CONNECTION_DATA    * psConnection,
                                        PVRSRV_DEVICE_NODE * psDeviceNode,
                                        IMG_HANDLE           hMemCtxPrivData);

/*!
*******************************************************************************
 @Function	PVRSRVRGXEnableBreakpointKM

 @Description
	Server-side implementation of RGXEnableBreakpoint

 @Input psDeviceNode - RGX Device node
 @Input hMemCtxPrivData - memory context private data

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXEnableBreakpointKM(CONNECTION_DATA    * psConnection,
                                         PVRSRV_DEVICE_NODE * psDeviceNode,
                                         IMG_HANDLE           hMemCtxPrivData);

/*!
*******************************************************************************
 @Function	PVRSRVRGXDisableBreakpointKM

 @Description
	Server-side implementation of RGXDisableBreakpoint

 @Input psDeviceNode - RGX Device node
 @Input hMemCtxPrivData - memory context private data

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXDisableBreakpointKM(CONNECTION_DATA    * psConnection,
                                          PVRSRV_DEVICE_NODE * psDeviceNode,
                                          IMG_HANDLE           hMemCtxPrivData);

/*!
*******************************************************************************
 @Function	PVRSRVRGXOverallocateBPRegistersKM

 @Description
	Server-side implementation of RGXOverallocateBPRegisters

 @Input psDeviceNode - RGX Device node
 @Input ui32TempRegs - Number of temporary registers to overallocate
 @Input ui32SharedRegs - Number of shared registers to overallocate

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXOverallocateBPRegistersKM(CONNECTION_DATA    * psConnection,
                                                PVRSRV_DEVICE_NODE * psDeviceNode,
                                                IMG_UINT32           ui32TempRegs,
                                                IMG_UINT32           ui32SharedRegs);
#endif /* __RGXBREAKPOINT_H__ */
