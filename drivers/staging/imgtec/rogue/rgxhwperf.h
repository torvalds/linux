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
#include "connection_server.h"
#include "rgxdevice.h"
#include "rgx_hwperf_km.h"


/******************************************************************************
 * RGX HW Performance Data Transport Routines
 *****************************************************************************/

PVRSRV_ERROR RGXHWPerfDataStoreCB(PVRSRV_DEVICE_NODE* psDevInfo);

PVRSRV_ERROR RGXHWPerfInit(PVRSRV_DEVICE_NODE *psRgxDevInfo);
PVRSRV_ERROR RGXHWPerfInitOnDemandResources(void);
void RGXHWPerfDeinit(void);
void RGXHWPerfInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode);

/******************************************************************************
 * RGX HW Performance Profiling API(s)
 *****************************************************************************/

PVRSRV_ERROR PVRSRVRGXCtrlHWPerfKM(
	CONNECTION_DATA      * psConnection,
	PVRSRV_DEVICE_NODE   * psDeviceNode,
	 RGX_HWPERF_STREAM_ID  eStreamId,
	IMG_BOOL               bToggle,
	IMG_UINT64             ui64Mask);


PVRSRV_ERROR PVRSRVRGXConfigEnableHWPerfCountersKM(
	CONNECTION_DATA    * psConnection,
	PVRSRV_DEVICE_NODE * psDeviceNode,
	IMG_UINT32         ui32ArrayLen,
	RGX_HWPERF_CONFIG_CNTBLK * psBlockConfigs);

PVRSRV_ERROR PVRSRVRGXCtrlHWPerfCountersKM(
	CONNECTION_DATA    * psConnection,
	PVRSRV_DEVICE_NODE * psDeviceNode,
	IMG_BOOL           bEnable,
	IMG_UINT32         ui32ArrayLen,
	IMG_UINT16         * psBlockIDs);

PVRSRV_ERROR PVRSRVRGXConfigCustomCountersKM(
	CONNECTION_DATA    * psConnection,
	PVRSRV_DEVICE_NODE * psDeviceNode,
	IMG_UINT16           ui16CustomBlockID,
	IMG_UINT16           ui16NumCustomCounters,
	IMG_UINT32         * pui32CustomCounterIDs);

/******************************************************************************
 * RGX HW Performance Host Stream API
 *****************************************************************************/

PVRSRV_ERROR RGXHWPerfHostInit(IMG_UINT32 ui32BufSizeKB);
PVRSRV_ERROR RGXHWPerfHostInitOnDemandResources(void);
void RGXHWPerfHostDeInit(void);

void RGXHWPerfHostSetEventFilter(IMG_UINT32 ui32Filter);

void RGXHWPerfHostPostCtrlEvent(RGX_HWPERF_HOST_CTRL_TYPE eEvType,
                                IMG_UINT32 ui32Pid);

void RGXHWPerfHostPostEnqEvent(RGX_HWPERF_KICK_TYPE eEnqType,
                               IMG_UINT32 ui32Pid,
                               IMG_UINT32 ui32FWDMContext,
                               IMG_UINT32 ui32ExtJobRef,
                               IMG_UINT32 ui32IntJobRef);

void RGXHWPerfHostPostAllocEvent(RGX_HWPERF_HOST_RESOURCE_TYPE eAllocType,
                                 IMG_UINT32 ui32FWAddr,
                                 const IMG_CHAR *psName,
                                 IMG_UINT32 ui32NameSize);

void RGXHWPerfHostPostFreeEvent(RGX_HWPERF_HOST_RESOURCE_TYPE eFreeType,
                                IMG_UINT32 ui32FWAddr);

void RGXHWPerfHostPostUfoEvent(RGX_HWPERF_UFO_EV eUfoType,
                               RGX_HWPERF_UFO_DATA_ELEMENT psUFOData[],
                               IMG_UINT uiNoOfUFOs);

void RGXHWPerfHostPostClkSyncEvent(void);

IMG_BOOL RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_EVENT_TYPE eEvent);

#define _RGX_HWPERF_HOST_FILTER(CTX, EV) \
		(((PVRSRV_RGXDEV_INFO *)CTX->psDeviceNode->pvDevice)->ui32HWPerfHostFilter \
		& RGX_HWPERF_EVENT_MASK_VALUE(EV))

/**
 * This macro checks if HWPerfHost and the event are enabled and if they are
 * it posts event to the HWPerfHost stream.
 *
 * @param C context
 * @param P process id (PID)
 * @param X firmware context
 * @param E ExtJobRef
 * @param I IntJobRef
 * @param K kick type
 */
#if defined(PVRSRV_GPUVIRT_GUESTDRV)
#define RGX_HWPERF_HOST_CTRL(E, P) \
		do { \
			PVR_UNREFERENCED_PARAMETER(P); \
		} while (0)

#define RGX_HWPERF_HOST_ENQ(C, P, X, E, I, K) \
		do { \
			PVR_UNREFERENCED_PARAMETER(X); \
			PVR_UNREFERENCED_PARAMETER(E); \
			PVR_UNREFERENCED_PARAMETER(I); \
		} while (0)

#define RGX_HWPERF_HOST_UFO(T, D, N) \
		do { \
			PVR_UNREFERENCED_PARAMETER(T); \
			PVR_UNREFERENCED_PARAMETER(D); \
			PVR_UNREFERENCED_PARAMETER(N); \
		} while (0)

#define RGX_HWPERF_HOST_ALLOC(T, F, N, Z) \
		do { \
			PVR_UNREFERENCED_PARAMETER(RGX_HWPERF_HOST_RESOURCE_TYPE_##T); \
			PVR_UNREFERENCED_PARAMETER(F); \
			PVR_UNREFERENCED_PARAMETER(N); \
			PVR_UNREFERENCED_PARAMETER(Z); \
		} while (0)

#define RGX_HWPERF_HOST_FREE(T, F) \
		do { \
			PVR_UNREFERENCED_PARAMETER(RGX_HWPERF_HOST_RESOURCE_TYPE_##T); \
			PVR_UNREFERENCED_PARAMETER(F); \
		} while (0)

#define RGX_HWPERF_HOST_CLK_SYNC()
#else
/**
 * @param E event type
 * @param P PID
 */
#define RGX_HWPERF_HOST_CTRL(E, P) \
		do { \
			if (RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_CTRL)) \
			{ \
				RGXHWPerfHostPostCtrlEvent(RGX_HWPERF_CTRL_TYPE_##E, (P)); \
			} \
		} while (0)

#define RGX_HWPERF_HOST_ENQ(C, P, X, E, I, K) \
		do { \
			if (_RGX_HWPERF_HOST_FILTER(C, RGX_HWPERF_HOST_ENQ)) \
			{ \
				RGXHWPerfHostPostEnqEvent((K), (P), (X), (E), (I)); \
			} \
		} while (0)

/**
 * This macro checks if HWPerfHost and the event are enabled and if they are
 * it posts event to the HWPerfHost stream.
 *
 * @param T Host UFO event type
 * @param D UFO data array
 * @param N number of syncs in data array
 */
#define RGX_HWPERF_HOST_UFO(T, D, N) \
		do { \
			if (RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_UFO)) \
			{ \
				RGXHWPerfHostPostUfoEvent((T), (D), (N)); \
			} \
		} while (0)

/**
 * This macro checks if HWPerfHost and the event are enabled and if they are
 * it posts event to the HWPerfHost stream.
 *
 * @param F sync firmware address
 * @param S boolean value telling if this is a server sync
 * @param N string containing sync name
 * @param Z string size including null terminating character
 */
#define RGX_HWPERF_HOST_ALLOC(T, F, N, Z) \
		do { \
			if (RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_ALLOC)) \
			{ \
				RGXHWPerfHostPostAllocEvent(RGX_HWPERF_HOST_RESOURCE_TYPE_##T, \
				                            (F), (N), (Z)); \
			} \
		} while (0)

/**
 * This macro checks if HWPerfHost and the event are enabled and if they are
 * it posts event to the HWPerfHost stream.
 *
 * @param F sync firmware address
 */
#define RGX_HWPERF_HOST_FREE(T, F) \
		do { \
			if (RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_FREE)) \
			{ \
				RGXHWPerfHostPostFreeEvent(RGX_HWPERF_HOST_RESOURCE_TYPE_##T, \
				                           (F)); \
			} \
		} while (0)

/**
 * This macro checks if HWPerfHost and the event are enabled and if they are
 * it posts event to the HWPerfHost stream.
 */
#define RGX_HWPERF_HOST_CLK_SYNC() \
		do { \
			if (RGXHWPerfHostIsEventEnabled(RGX_HWPERF_HOST_CLK_SYNC)) \
			{ \
				RGXHWPerfHostPostClkSyncEvent(); \
			} \
		} while (0)
#endif

/******************************************************************************
 * RGX HW Performance To FTrace Profiling API(s)
 *****************************************************************************/

#if defined(SUPPORT_GPUTRACE_EVENTS)

PVRSRV_ERROR RGXHWPerfFTraceGPUInit(PVRSRV_DEVICE_NODE *psDeviceNode);
void RGXHWPerfFTraceGPUDeInit(PVRSRV_DEVICE_NODE *psDeviceNode);

void RGXHWPerfFTraceGPUEnqueueEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		IMG_UINT32 ui32ExternalJobRef, IMG_UINT32 ui32InternalJobRef,
		RGX_HWPERF_KICK_TYPE eKickType);

PVRSRV_ERROR RGXHWPerfFTraceGPUEventsEnabledSet(IMG_BOOL bNewValue);

void RGXHWPerfFTraceGPUThread(void *pvData);

#endif

/******************************************************************************
 * RGX HW utils functions
 *****************************************************************************/

const IMG_CHAR *RGXHWPerfKickTypeToStr(RGX_HWPERF_KICK_TYPE eKickType);

#endif /* RGXHWPERF_H_ */
