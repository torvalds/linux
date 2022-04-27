/*************************************************************************/ /*!
@File           ospvr_gputrace.h
@Title          PVR GPU Trace module common environment interface
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
*/ /**************************************************************************/

#ifndef PVR_GPUTRACE_H_
#define PVR_GPUTRACE_H_

#include "img_types.h"
#include "img_defs.h"
#include "rgx_hwperf.h"
#include "device.h"

#if defined(__linux__)

void PVRGpuTraceEnqueueEvent(
		PVRSRV_DEVICE_NODE *psDevNode,
		IMG_UINT32 ui32FirmwareCtx,
		IMG_UINT32 ui32ExternalJobRef,
		IMG_UINT32 ui32InternalJobRef,
		RGX_HWPERF_KICK_TYPE eKickType);

/* Early initialisation of GPU Trace events logic.
 * This function is called on *driver* initialisation. */
PVRSRV_ERROR PVRGpuTraceSupportInit(void);

/* GPU Trace resources final cleanup.
 * This function is called on driver de-initialisation. */
void PVRGpuTraceSupportDeInit(void);

/* Initialisation for AppHints callbacks.
 * This function is called during the late stage of driver initialisation but
 * before the device initialisation but after the debugfs sub-system has been
 * initialised. */
void PVRGpuTraceInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode);

/* Per-device initialisation of the GPU Trace resources */
PVRSRV_ERROR PVRGpuTraceInitDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

/* Per-device cleanup for the GPU Trace resources. */
void PVRGpuTraceDeInitDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

/* Enables the gpu trace sub-system for a given device. */
PVRSRV_ERROR PVRGpuTraceSetEnabled(
		PVRSRV_DEVICE_NODE *psDeviceNode,
		IMG_BOOL bNewValue);

/* Returns IMG_TRUE if the gpu trace sub-system has been enabled (but not
 * necessarily initialised). */
IMG_BOOL PVRGpuTraceIsEnabled(void);

/* Performs some initialisation steps if the feature was enabled
 * on driver startup. */
void PVRGpuTraceInitIfEnabled(PVRSRV_DEVICE_NODE *psDeviceNode);

/* FTrace events callbacks interface */

void PVRGpuTraceEnableUfoCallback(void);
void PVRGpuTraceDisableUfoCallback(void);

void PVRGpuTraceEnableFirmwareActivityCallback(void);
void PVRGpuTraceDisableFirmwareActivityCallback(void);

#else /* defined(__linux__) */

static inline void PVRGpuTraceEnqueueEvent(
		PVRSRV_DEVICE_NODE *psDevNode,
		IMG_UINT32 ui32FirmwareCtx,
		IMG_UINT32 ui32ExternalJobRef,
		IMG_UINT32 ui32InternalJobRef,
		RGX_HWPERF_KICK_TYPE eKickType)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(ui32ExternalJobRef);
	PVR_UNREFERENCED_PARAMETER(ui32InternalJobRef);
	PVR_UNREFERENCED_PARAMETER(eKickType);
}

static inline PVRSRV_ERROR PVRGpuTraceSupportInit(void) {
	return PVRSRV_OK;
}

static inline void PVRGpuTraceSupportDeInit(void) {}

static inline void PVRGpuTraceInitAppHintCallbacks(
		const PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

static inline PVRSRV_ERROR PVRGpuTraceInitDevice(
		PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	return PVRSRV_OK;
}

static inline void PVRGpuTraceDeInitDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

static inline PVRSRV_ERROR PVRGpuTraceSetEnabled(
		PVRSRV_DEVICE_NODE *psDeviceNode,
		IMG_BOOL bNewValue)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(bNewValue);
	return PVRSRV_OK;
}

static inline IMG_BOOL PVRGpuTraceIsEnabled(void)
{
	return IMG_FALSE;
}

static inline void PVRGpuTraceInitIfEnabled(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

static inline void PVRGpuTraceEnableUfoCallback(void) {}
static inline void PVRGpuTraceDisableUfoCallback(void) {}

static inline void PVRGpuTraceEnableFirmwareActivityCallback(void) {}
static inline void PVRGpuTraceDisableFirmwareActivityCallback(void) {}

#endif /* defined(__linux__) */

#endif /* PVR_GPUTRACE_H_ */
