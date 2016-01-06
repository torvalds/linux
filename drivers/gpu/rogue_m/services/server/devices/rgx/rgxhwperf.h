/*************************************************************************/ /*!
@File
@Title          RGX HW Performance header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX HWPerf functions
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

#ifndef RGXHWPERF_H_
#define RGXHWPERF_H_
  
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"

#include "device.h"
#include "rgxdevice.h"
#include "rgx_hwperf_km.h"


/******************************************************************************
 * RGX HW Performance Data Transport Routines
 *****************************************************************************/

PVRSRV_ERROR RGXHWPerfDataStoreCB(PVRSRV_DEVICE_NODE* psDevInfo);

PVRSRV_ERROR RGXHWPerfInit(PVRSRV_DEVICE_NODE *psRgxDevInfo, IMG_BOOL bEnable);
IMG_VOID RGXHWPerfDeinit(void);


/******************************************************************************
 * RGX HW Performance Profiling API(s)
 *****************************************************************************/

PVRSRV_ERROR PVRSRVRGXCtrlHWPerfKM(
		PVRSRV_DEVICE_NODE*	psDeviceNode,
		IMG_BOOL			bToggle,
		IMG_UINT64 			ui64Mask);


PVRSRV_ERROR PVRSRVRGXConfigEnableHWPerfCountersKM(
		PVRSRV_DEVICE_NODE* 		psDeviceNode,
		IMG_UINT32 					ui32ArrayLen,
		RGX_HWPERF_CONFIG_CNTBLK* 	psBlockConfigs);

PVRSRV_ERROR PVRSRVRGXCtrlHWPerfCountersKM(
		PVRSRV_DEVICE_NODE*		psDeviceNode,
		IMG_BOOL			bEnable,
	    IMG_UINT32 			ui32ArrayLen,
	    IMG_UINT16*			psBlockIDs);

PVRSRV_ERROR PVRSRVRGXConfigCustomCountersKM(
		PVRSRV_DEVICE_NODE*     psDeviceNode,
		IMG_UINT16              ui16CustomBlockID,
		IMG_UINT16              ui16NumCustomCounters,
		IMG_UINT32*             pui32CustomCounterIDs);

/******************************************************************************
 * RGX HW Performance To FTrace Profiling API(s)
 *****************************************************************************/

#if defined(SUPPORT_GPUTRACE_EVENTS)

PVRSRV_ERROR RGXHWPerfFTraceGPUInit(PVRSRV_RGXDEV_INFO *psDevInfo);
IMG_VOID RGXHWPerfFTraceGPUDeInit(PVRSRV_RGXDEV_INFO *psDevInfo);

IMG_VOID RGXHWPerfFTraceGPUEnqueueEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		IMG_UINT32 ui32ExternalJobRef, IMG_UINT32 ui32InternalJobRef,
		const IMG_CHAR* pszJobType);

PVRSRV_ERROR RGXHWPerfFTraceGPUEventsEnabledSet(IMG_BOOL bNewValue);
IMG_BOOL RGXHWPerfFTraceGPUEventsEnabled(IMG_VOID);

IMG_VOID RGXHWPerfFTraceGPUThread(IMG_PVOID pvData);

#endif


#endif /* RGXHWPERF_H_ */
