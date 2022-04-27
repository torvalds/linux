/*************************************************************************/ /*!
@File           pvr_gputrace.c
@Title          PVR GPU Trace module Linux implementation
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

#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
#include <linux/trace_events.h>
#else
#include <linux/ftrace_event.h>
#endif

#include "pvrsrv_error.h"
#include "pvrsrv_apphint.h"
#include "pvr_debug.h"
#include "ospvr_gputrace.h"
#include "rgxhwperf.h"
#include "rgxtimecorr.h"
#include "device.h"
#include "trace_events.h"
#include "pvrsrv.h"
#include "pvrsrv_tlstreams.h"
#include "tlclient.h"
#include "pvr_debug.h"
#define CREATE_TRACE_POINTS
#include "rogue_trace_events.h"

/******************************************************************************
 Module internal implementation
******************************************************************************/

typedef enum {
	PVR_GPUTRACE_SWITCH_TYPE_UNDEF = 0,

	PVR_GPUTRACE_SWITCH_TYPE_BEGIN = 1,
	PVR_GPUTRACE_SWITCH_TYPE_END = 2,
	PVR_GPUTRACE_SWITCH_TYPE_SINGLE = 3
} PVR_GPUTRACE_SWITCH_TYPE;

typedef struct RGX_HWPERF_FTRACE_DATA {
	/* This lock ensures the HWPerf TL stream reading resources are not destroyed
	 * by one thread disabling it while another is reading from it. Keeps the
	 * state and resource create/destroy atomic and consistent. */
	POS_LOCK    hFTraceResourceLock;

	IMG_HANDLE  hGPUTraceCmdCompleteHandle;
	IMG_HANDLE  hGPUTraceTLStream;
	IMG_UINT64  ui64LastSampledTimeCorrOSTimeStamp;
	IMG_UINT32  ui32FTraceLastOrdinal;
} RGX_HWPERF_FTRACE_DATA;

/* This lock ensures state change of GPU_TRACING on/off is done atomically */
static POS_LOCK ghGPUTraceStateLock;
static IMG_BOOL gbFTraceGPUEventsEnabled = PVRSRV_APPHINT_ENABLEFTRACEGPU;

/* Saved value of the clock source before the trace was enabled. We're keeping
 * it here so that we know which clock should be selected after we disable the
 * gpu ftrace. */
#if defined(SUPPORT_RGX)
static RGXTIMECORR_CLOCK_TYPE geLastTimeCorrClock = PVRSRV_APPHINT_TIMECORRCLOCK;
#endif

/* This lock ensures that the reference counting operation on the FTrace UFO
 * events and enable/disable operation on firmware event are performed as
 * one atomic operation. This should ensure that there are no race conditions
 * between reference counting and firmware event state change.
 * See below comment for guiUfoEventRef.
 */
static POS_LOCK ghLockFTraceEventLock;

/* Multiple FTrace UFO events are reflected in the firmware as only one event. When
 * we enable FTrace UFO event we want to also at the same time enable it in
 * the firmware. Since there is a multiple-to-one relation between those events
 * we count how many FTrace UFO events is enabled. If at least one event is
 * enabled we enabled the firmware event. When all FTrace UFO events are disabled
 * we disable firmware event. */
static IMG_UINT guiUfoEventRef;

/******************************************************************************
 Module In-bound API
******************************************************************************/

static PVRSRV_ERROR _GpuTraceDisable(
	PVRSRV_RGXDEV_INFO *psRgxDevInfo,
	IMG_BOOL bDeInit);

static void _GpuTraceCmdCompleteNotify(PVRSRV_CMDCOMP_HANDLE);

PVRSRV_ERROR PVRGpuTraceSupportInit(void)
{
	PVRSRV_ERROR eError;

	if (ghLockFTraceEventLock != NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "FTrace Support is already initialized"));
		return PVRSRV_OK;
	}

	/* common module params initialization */
	eError = OSLockCreate(&ghLockFTraceEventLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSLockCreate");

	eError = OSLockCreate(&ghGPUTraceStateLock);
	PVR_LOG_RETURN_IF_ERROR (eError, "OSLockCreate");

	return PVRSRV_OK;
}

void PVRGpuTraceSupportDeInit(void)
{
	if (ghGPUTraceStateLock)
	{
		OSLockDestroy(ghGPUTraceStateLock);
	}

	if (ghLockFTraceEventLock)
	{
		OSLockDestroy(ghLockFTraceEventLock);
		ghLockFTraceEventLock = NULL;
	}
}

PVRSRV_ERROR PVRGpuTraceInitDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	RGX_HWPERF_FTRACE_DATA *psData;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	psData = OSAllocZMem(sizeof(RGX_HWPERF_FTRACE_DATA));
	psDevInfo->pvGpuFtraceData = psData;
	PVR_LOG_GOTO_IF_NOMEM(psData, eError, e0);

	/* We initialise it only once because we want to track if any
	 * packets were dropped. */
	psData->ui32FTraceLastOrdinal = IMG_UINT32_MAX - 1;

	eError = OSLockCreate(&psData->hFTraceResourceLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);

	return PVRSRV_OK;

e0:
	PVRGpuTraceDeInitDevice(psDeviceNode);
	return eError;
}

void PVRGpuTraceDeInitDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_HWPERF_FTRACE_DATA *psData = psDevInfo->pvGpuFtraceData;

	PVRSRV_VZ_RETN_IF_MODE(GUEST);
	if (psData)
	{
		/* first disable the tracing, to free up TL resources */
		if (psData->hFTraceResourceLock)
		{
			OSLockAcquire(psData->hFTraceResourceLock);
			_GpuTraceDisable(psDeviceNode->pvDevice, IMG_TRUE);
			OSLockRelease(psData->hFTraceResourceLock);

			/* now free all the FTrace resources */
			OSLockDestroy(psData->hFTraceResourceLock);
		}
		OSFreeMem(psData);
		psDevInfo->pvGpuFtraceData = NULL;
	}
}

IMG_BOOL PVRGpuTraceIsEnabled(void)
{
	return gbFTraceGPUEventsEnabled;
}

void PVRGpuTraceInitIfEnabled(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (PVRGpuTraceIsEnabled())
	{
		PVRSRV_ERROR eError = PVRGpuTraceSetEnabled(psDeviceNode, IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to initialise GPU event tracing"
					" (%s)", PVRSRVGetErrorString(eError)));
		}

		/* below functions will enable FTrace events which in turn will
		 * execute HWPerf callbacks that set appropriate filter values
		 * note: unfortunately the functions don't allow to pass private
		 *       data so they enable events for all of the devices
		 *       at once, which means that this can happen more than once
		 *       if there is more than one device */

		/* single events can be enabled by calling trace_set_clr_event()
		 * with the event name, e.g.:
		 * trace_set_clr_event("rogue", "rogue_ufo_update", 1) */
#if defined(CONFIG_EVENT_TRACING) /* this is a kernel config option */
#if defined(ANDROID) || defined(CHROMIUMOS_KERNEL)
		if (trace_set_clr_event("gpu", NULL, 1))
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to enable \"gpu\" event"
					" group"));
		}
		else
		{
			PVR_LOG(("FTrace events from \"gpu\" group enabled"));
		}
#endif /* defined(ANDROID) || defined(CHROMIUMOS_KERNEL) */
		if (trace_set_clr_event("rogue", NULL, 1))
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to enable \"rogue\" event"
					" group"));
		}
		else
		{
			PVR_LOG(("FTrace events from \"rogue\" group enabled"));
		}
#endif /* defined(CONFIG_EVENT_TRACING) */
	}
}

/* Caller must now hold hFTraceResourceLock before calling this method.
 */
static PVRSRV_ERROR _GpuTraceEnable(PVRSRV_RGXDEV_INFO *psRgxDevInfo)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;
	PVRSRV_DEVICE_NODE *psRgxDevNode = psRgxDevInfo->psDeviceNode;
	IMG_CHAR pszHWPerfStreamName[sizeof(PVRSRV_TL_HWPERF_RGX_FW_STREAM) + 5];

	PVR_DPF_ENTERED;

	PVR_ASSERT(psRgxDevInfo);

	psFtraceData = psRgxDevInfo->pvGpuFtraceData;

	PVR_ASSERT(OSLockIsLocked(psFtraceData->hFTraceResourceLock));

	/* return if already enabled */
	if (psFtraceData->hGPUTraceTLStream)
	{
		return PVRSRV_OK;
	}

#if defined(SUPPORT_RGX)
	/* Signal FW to enable event generation */
	if (psRgxDevInfo->bFirmwareInitialised)
	{
		IMG_UINT64 ui64UFOFilter = psRgxDevInfo->ui64HWPerfFilter &
		        (RGX_HWPERF_EVENT_MASK_FW_SED | RGX_HWPERF_EVENT_MASK_FW_UFO);

		eError = PVRSRVRGXCtrlHWPerfKM(NULL, psRgxDevNode,
		                               RGX_HWPERF_STREAM_ID0_FW, IMG_FALSE,
		                               RGX_HWPERF_EVENT_MASK_HW_KICKFINISH |
		                               ui64UFOFilter);
		PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVRGXCtrlHWPerfKM", err_out);
	}
	else
#endif
	{
		/* only set filter and exit */
		psRgxDevInfo->ui64HWPerfFilter = RGX_HWPERF_EVENT_MASK_HW_KICKFINISH |
		        ((RGX_HWPERF_EVENT_MASK_FW_SED | RGX_HWPERF_EVENT_MASK_FW_UFO) &
		        psRgxDevInfo->ui64HWPerfFilter);

		PVR_DPF((PVR_DBG_WARNING,
				 "HWPerfFW mask has been SET to (%" IMG_UINT64_FMTSPECx ")",
				 psRgxDevInfo->ui64HWPerfFilter));

		return PVRSRV_OK;
	}

	/* form the HWPerf stream name, corresponding to this DevNode; which can make sense in the UM */
	if (OSSNPrintf(pszHWPerfStreamName, sizeof(pszHWPerfStreamName), "%s%d",
					PVRSRV_TL_HWPERF_RGX_FW_STREAM, psRgxDevNode->sDevId.i32OsDeviceID) < 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to form HWPerf stream name for device %d",
		         __func__,
		         psRgxDevNode->sDevId.i32OsDeviceID));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Open the TL Stream for HWPerf data consumption */
	eError = TLClientOpenStream(DIRECT_BRIDGE_HANDLE,
								pszHWPerfStreamName,
								PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING,
								&psFtraceData->hGPUTraceTLStream);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLClientOpenStream", err_out);

#if defined(SUPPORT_RGX)
	if (RGXTimeCorrGetClockSource() != RGXTIMECORR_CLOCK_SCHED)
	{
		/* Set clock source for timer correlation data to sched_clock */
		geLastTimeCorrClock = RGXTimeCorrGetClockSource();
		RGXTimeCorrSetClockSource(psRgxDevNode, RGXTIMECORR_CLOCK_SCHED);
	}
#endif

	/* Reset the OS timestamp coming from the timer correlation data
	 * associated with the latest HWPerf event we processed.
	 */
	psFtraceData->ui64LastSampledTimeCorrOSTimeStamp = 0;

	/* Register a notifier to collect HWPerf data whenever the HW completes
	 * an operation.
	 */
	eError = PVRSRVRegisterCmdCompleteNotify(
		&psFtraceData->hGPUTraceCmdCompleteHandle,
		&_GpuTraceCmdCompleteNotify,
		psRgxDevInfo);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVRegisterCmdCompleteNotify", err_close_stream);

err_out:
	PVR_DPF_RETURN_RC(eError);

err_close_stream:
	TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
						psFtraceData->hGPUTraceTLStream);
	psFtraceData->hGPUTraceTLStream = NULL;
	goto err_out;
}

/* Caller must now hold hFTraceResourceLock before calling this method.
 */
static PVRSRV_ERROR _GpuTraceDisable(PVRSRV_RGXDEV_INFO *psRgxDevInfo, IMG_BOOL bDeInit)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;
#if defined(SUPPORT_RGX)
	PVRSRV_DEVICE_NODE *psRgxDevNode = psRgxDevInfo->psDeviceNode;
#endif

	PVR_DPF_ENTERED;

	PVR_ASSERT(psRgxDevInfo);

	psFtraceData = psRgxDevInfo->pvGpuFtraceData;

	PVR_ASSERT(OSLockIsLocked(psFtraceData->hFTraceResourceLock));

	/* if FW is not yet initialised, just set filter and exit */
	if (!psRgxDevInfo->bFirmwareInitialised)
	{
		psRgxDevInfo->ui64HWPerfFilter = RGX_HWPERF_EVENT_MASK_NONE;
		PVR_DPF((PVR_DBG_WARNING,
				 "HWPerfFW mask has been SET to (%" IMG_UINT64_FMTSPECx ")",
				 psRgxDevInfo->ui64HWPerfFilter));

		return PVRSRV_OK;
	}

	if (NULL == psFtraceData->hGPUTraceTLStream)
	{
		/* Tracing already disabled, just return */
		return PVRSRV_OK;
	}

#if defined(SUPPORT_RGX)
	if (!bDeInit)
	{
		eError = PVRSRVRGXCtrlHWPerfKM(NULL, psRgxDevNode,
		                               RGX_HWPERF_STREAM_ID0_FW, IMG_FALSE,
		                               (RGX_HWPERF_EVENT_MASK_NONE));
		PVR_LOG_IF_ERROR(eError, "PVRSRVRGXCtrlHWPerfKM");
	}
#endif

	if (psFtraceData->hGPUTraceCmdCompleteHandle)
	{
		/* Tracing is being turned off. Unregister the notifier. */
		eError = PVRSRVUnregisterCmdCompleteNotify(
				psFtraceData->hGPUTraceCmdCompleteHandle);
		PVR_LOG_IF_ERROR(eError, "PVRSRVUnregisterCmdCompleteNotify");
		psFtraceData->hGPUTraceCmdCompleteHandle = NULL;
	}

	if (psFtraceData->hGPUTraceTLStream)
	{
		IMG_PBYTE pbTmp = NULL;
		IMG_UINT32 ui32Tmp = 0;

		/* We have to flush both the L1 (FW) and L2 (Host) buffers in case there
		 * are some events left unprocessed in this FTrace/systrace "session"
		 * (note that even if we have just disabled HWPerf on the FW some packets
		 * could have been generated and already copied to L2 by the MISR handler).
		 *
		 * With the following calls we will both copy new data to the Host buffer
		 * (done by the producer callback in TLClientAcquireData) and advance
		 * the read offset in the buffer to catch up with the latest events.
		 */
		eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE,
		                             psFtraceData->hGPUTraceTLStream,
		                             &pbTmp, &ui32Tmp);
		PVR_LOG_IF_ERROR(eError, "TLClientCloseStream");

		/* Let close stream perform the release data on the outstanding acquired data */
		eError = TLClientCloseStream(DIRECT_BRIDGE_HANDLE,
		                             psFtraceData->hGPUTraceTLStream);
		PVR_LOG_IF_ERROR(eError, "TLClientCloseStream");

		psFtraceData->hGPUTraceTLStream = NULL;
	}

#if defined(SUPPORT_RGX)
	if (geLastTimeCorrClock != RGXTIMECORR_CLOCK_SCHED)
	{
		RGXTimeCorrSetClockSource(psRgxDevNode, geLastTimeCorrClock);
	}
#endif

	PVR_DPF_RETURN_RC(eError);
}

static PVRSRV_ERROR _GpuTraceSetEnabled(PVRSRV_RGXDEV_INFO *psRgxDevInfo,
                                        IMG_BOOL bNewValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGX_HWPERF_FTRACE_DATA *psFtraceData;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	PVR_DPF_ENTERED;

	PVR_ASSERT(psRgxDevInfo);
	psFtraceData = psRgxDevInfo->pvGpuFtraceData;

	/* About to create/destroy FTrace resources, lock critical section
	 * to avoid HWPerf MISR thread contention.
	 */
	OSLockAcquire(psFtraceData->hFTraceResourceLock);

	eError = (bNewValue ? _GpuTraceEnable(psRgxDevInfo)
					   : _GpuTraceDisable(psRgxDevInfo, IMG_FALSE));

	OSLockRelease(psFtraceData->hFTraceResourceLock);

	PVR_DPF_RETURN_RC(eError);
}

static PVRSRV_ERROR _GpuTraceSetEnabledForAllDevices(IMG_BOOL bNewValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE *psDeviceNode;

	psDeviceNode = psPVRSRVData->psDeviceNodeList;
	/* enable/disable GPU trace on all devices */
	while (psDeviceNode)
	{
		eError = _GpuTraceSetEnabled(psDeviceNode->pvDevice, bNewValue);
		if (eError != PVRSRV_OK)
		{
			break;
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR PVRGpuTraceSetEnabled(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   IMG_BOOL bNewValue)
{
	return _GpuTraceSetEnabled(psDeviceNode->pvDevice, bNewValue);
}

/* ----- HWPerf to FTrace packet processing and events injection ------------ */

static const IMG_CHAR *_HWPerfKickTypeToStr(RGX_HWPERF_KICK_TYPE eKickType)
{
	static const IMG_CHAR *aszKickType[RGX_HWPERF_KICK_TYPE_LAST+1] = {
#if defined(HWPERF_PACKET_V2C_SIG)
		"TA3D", "CDM", "RS", "SHG", "TQTDM", "SYNC", "LAST"
#else
		"TA3D", "TQ2D", "TQ3D", "CDM", "RS", "VRDM", "TQTDM", "SYNC", "LAST"
#endif
	};

	/* cast in case of negative value */
	if (((IMG_UINT32) eKickType) >= RGX_HWPERF_KICK_TYPE_LAST)
	{
		return "<UNKNOWN>";
	}

	return aszKickType[eKickType];
}

void PVRGpuTraceEnqueueEvent(
		PVRSRV_DEVICE_NODE *psDevNode,
		IMG_UINT32 ui32FirmwareCtx,
		IMG_UINT32 ui32ExtJobRef,
		IMG_UINT32 ui32IntJobRef,
		RGX_HWPERF_KICK_TYPE eKickType)
{
	const IMG_CHAR *pszKickType = _HWPerfKickTypeToStr(eKickType);

	PVR_DPF((PVR_DBG_MESSAGE, "PVRGpuTraceEnqueueEvent(%s): contextId %u, "
	        "jobId %u", pszKickType, ui32FirmwareCtx, ui32IntJobRef));

	if (PVRGpuTraceIsEnabled())
	{
		trace_rogue_job_enqueue(ui32FirmwareCtx, ui32IntJobRef, ui32ExtJobRef,
					pszKickType);
	}
}

static void _GpuTraceWorkSwitch(
		IMG_UINT64 ui64HWTimestampInOSTime,
		IMG_UINT32 ui32CtxId,
		IMG_UINT32 ui32CtxPriority,
		IMG_UINT32 ui32ExtJobRef,
		IMG_UINT32 ui32IntJobRef,
		const IMG_CHAR* pszWorkType,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)
{
	PVR_ASSERT(pszWorkType);
	trace_rogue_sched_switch(pszWorkType, eSwType, ui64HWTimestampInOSTime,
			ui32CtxId, 2-ui32CtxPriority, ui32IntJobRef, ui32ExtJobRef);
}

static void _GpuTraceUfo(
		IMG_UINT64 ui64OSTimestamp,
		const RGX_HWPERF_UFO_EV eEvType,
		const IMG_UINT32 ui32CtxId,
		const IMG_UINT32 ui32ExtJobRef,
		const IMG_UINT32 ui32IntJobRef,
		const IMG_UINT32 ui32UFOCount,
		const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
	switch (eEvType) {
		case RGX_HWPERF_UFO_EV_UPDATE:
			trace_rogue_ufo_updates(ui64OSTimestamp, ui32CtxId,
					ui32ExtJobRef, ui32IntJobRef, ui32UFOCount, puData);
			break;
		case RGX_HWPERF_UFO_EV_CHECK_SUCCESS:
			trace_rogue_ufo_checks_success(ui64OSTimestamp, ui32CtxId,
					ui32ExtJobRef, ui32IntJobRef, IMG_FALSE, ui32UFOCount,
					puData);
			break;
		case RGX_HWPERF_UFO_EV_PRCHECK_SUCCESS:
			trace_rogue_ufo_checks_success(ui64OSTimestamp, ui32CtxId,
					ui32ExtJobRef, ui32IntJobRef, IMG_TRUE, ui32UFOCount,
					puData);
			break;
		case RGX_HWPERF_UFO_EV_CHECK_FAIL:
			trace_rogue_ufo_checks_fail(ui64OSTimestamp, ui32CtxId,
					ui32ExtJobRef, ui32IntJobRef, IMG_FALSE, ui32UFOCount,
					puData);
			break;
		case RGX_HWPERF_UFO_EV_PRCHECK_FAIL:
			trace_rogue_ufo_checks_fail(ui64OSTimestamp, ui32CtxId,
					ui32ExtJobRef, ui32IntJobRef, IMG_TRUE, ui32UFOCount,
					puData);
			break;
		default:
			break;
	}
}

static void _GpuTraceFirmware(
		IMG_UINT64 ui64HWTimestampInOSTime,
		const IMG_CHAR* pszWorkType,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)
{
	trace_rogue_firmware_activity(ui64HWTimestampInOSTime, pszWorkType, eSwType);
}

static void _GpuTraceEventsLost(
		const RGX_HWPERF_STREAM_ID eStreamId,
		const IMG_UINT32 ui32LastOrdinal,
		const IMG_UINT32 ui32CurrOrdinal)
{
	trace_rogue_events_lost(eStreamId, ui32LastOrdinal, ui32CurrOrdinal);
}

/* Calculate the OS timestamp given an RGX timestamp in the HWPerf event. */
static uint64_t CalculateEventTimestamp(
	PVRSRV_RGXDEV_INFO *psDevInfo,
	uint32_t ui32TimeCorrIndex,
	uint64_t ui64EventTimestamp)
{
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psDevInfo->psRGXFWIfGpuUtilFWCb;
	RGX_HWPERF_FTRACE_DATA *psFtraceData = psDevInfo->pvGpuFtraceData;
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[ui32TimeCorrIndex];
	uint64_t ui64CRTimeStamp = psTimeCorr->ui64CRTimeStamp;
	uint64_t ui64OSTimeStamp = psTimeCorr->ui64OSTimeStamp;
	uint64_t ui64CRDeltaToOSDeltaKNs = psTimeCorr->ui64CRDeltaToOSDeltaKNs;
	uint64_t ui64EventOSTimestamp, deltaRgxTimer, delta_ns;

	if (psFtraceData->ui64LastSampledTimeCorrOSTimeStamp > ui64OSTimeStamp)
	{
		/* The previous packet had a time reference (time correlation data) more
		 * recent than the one in the current packet, it means the timer
		 * correlation array wrapped too quickly (buffer too small) and in the
		 * previous call to _GpuTraceUfoEvent we read one of the
		 * newest timer correlations rather than one of the oldest ones.
		 */
		PVR_DPF((PVR_DBG_ERROR, "%s: The timestamps computed so far could be "
				 "wrong! The time correlation array size should be increased "
				 "to avoid this.", __func__));
	}

	psFtraceData->ui64LastSampledTimeCorrOSTimeStamp = ui64OSTimeStamp;

	/* RGX CR timer ticks delta */
	deltaRgxTimer = ui64EventTimestamp - ui64CRTimeStamp;
	/* RGX time delta in nanoseconds */
	delta_ns = RGXFWIF_GET_DELTA_OSTIME_NS(deltaRgxTimer, ui64CRDeltaToOSDeltaKNs);
	/* Calculate OS time of HWPerf event */
	ui64EventOSTimestamp = ui64OSTimeStamp + delta_ns;

	PVR_DPF((PVR_DBG_VERBOSE, "%s: psCurrentDvfs RGX %llu, OS %llu, DVFSCLK %u",
			 __func__, ui64CRTimeStamp, ui64OSTimeStamp,
			 psTimeCorr->ui32CoreClockSpeed));

	return ui64EventOSTimestamp;
}

static void _GpuTraceSwitchEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt, const IMG_CHAR* pszWorkName,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)
{
	IMG_UINT64 ui64Timestamp;
	RGX_HWPERF_HW_DATA* psHWPerfPktData;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psHWPerfPkt);
	PVR_ASSERT(pszWorkName);

	psHWPerfPktData = RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	PVR_DPF((PVR_DBG_VERBOSE, "_GpuTraceSwitchEvent: %s ui32ExtJobRef=%d, ui32IntJobRef=%d, eSwType=%d",
			pszWorkName, psHWPerfPktData->ui32DMContext, psHWPerfPktData->ui32IntJobRef, eSwType));

	_GpuTraceWorkSwitch(ui64Timestamp,
	                    psHWPerfPktData->ui32DMContext,
	                    psHWPerfPktData->ui32CtxPriority,
	                    psHWPerfPktData->ui32ExtJobRef,
	                    psHWPerfPktData->ui32IntJobRef,
	                    pszWorkName,
	                    eSwType);

	PVR_DPF_RETURN;
}

static void _GpuTraceUfoEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
                              RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt)
{
	IMG_UINT64 ui64Timestamp;
	RGX_HWPERF_UFO_DATA *psHWPerfPktData;
	IMG_UINT32 ui32UFOCount;
	RGX_HWPERF_UFO_DATA_ELEMENT *puData;

	psHWPerfPktData = RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui32UFOCount = RGX_HWPERF_GET_UFO_STREAMSIZE(psHWPerfPktData->ui32StreamInfo);
	puData = (RGX_HWPERF_UFO_DATA_ELEMENT *) IMG_OFFSET_ADDR(psHWPerfPktData, RGX_HWPERF_GET_UFO_STREAMOFFSET(psHWPerfPktData->ui32StreamInfo));

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	PVR_DPF((PVR_DBG_VERBOSE, "_GpuTraceUfoEvent: ui32ExtJobRef=%d, "
	        "ui32IntJobRef=%d", psHWPerfPktData->ui32ExtJobRef,
	        psHWPerfPktData->ui32IntJobRef));

	_GpuTraceUfo(ui64Timestamp, psHWPerfPktData->eEvType,
	             psHWPerfPktData->ui32DMContext, psHWPerfPktData->ui32ExtJobRef,
	             psHWPerfPktData->ui32IntJobRef, ui32UFOCount, puData);
}

static void _GpuTraceFirmwareEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt, const IMG_CHAR* pszWorkName,
		PVR_GPUTRACE_SWITCH_TYPE eSwType)

{
	uint64_t ui64Timestamp;
	RGX_HWPERF_FW_DATA *psHWPerfPktData = RGX_HWPERF_GET_PACKET_DATA_BYTES(psHWPerfPkt);

	ui64Timestamp = CalculateEventTimestamp(psDevInfo, psHWPerfPktData->ui32TimeCorrIndex,
											psHWPerfPkt->ui64Timestamp);

	_GpuTraceFirmware(ui64Timestamp, pszWorkName, eSwType);
}

static IMG_BOOL ValidAndEmitFTraceEvent(PVRSRV_RGXDEV_INFO *psDevInfo,
		RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt)
{
	RGX_HWPERF_EVENT_TYPE eType;
	RGX_HWPERF_FTRACE_DATA *psFtraceData = psDevInfo->pvGpuFtraceData;
	IMG_UINT32 ui32HwEventTypeIndex;
	static const struct {
		IMG_CHAR* pszName;
		PVR_GPUTRACE_SWITCH_TYPE eSwType;
	} aszHwEventTypeMap[] = {
#define _T(T) PVR_GPUTRACE_SWITCH_TYPE_##T
		{ "BG",             _T(BEGIN)  }, /* RGX_HWPERF_FW_BGSTART */
		{ "BG",             _T(END)    }, /* RGX_HWPERF_FW_BGEND */
		{ "IRQ",            _T(BEGIN)  }, /* RGX_HWPERF_FW_IRQSTART */
		{ "IRQ",            _T(END)    }, /* RGX_HWPERF_FW_IRQEND */
		{ "DBG",            _T(BEGIN)  }, /* RGX_HWPERF_FW_DBGSTART */
		{ "DBG",            _T(END)    }, /* RGX_HWPERF_FW_DBGEND */
		{ "PMOOM_TAPAUSE",  _T(END)    }, /* RGX_HWPERF_HW_PMOOM_TAPAUSE */
		{ "TA",             _T(BEGIN)  }, /* RGX_HWPERF_HW_TAKICK */
		{ "TA",             _T(END)    }, /* RGX_HWPERF_HW_TAFINISHED */
		{ "TQ3D",           _T(BEGIN)  }, /* RGX_HWPERF_HW_3DTQKICK */
		{ "3D",             _T(BEGIN)  }, /* RGX_HWPERF_HW_3DKICK */
		{ "3D",             _T(END)    }, /* RGX_HWPERF_HW_3DFINISHED */
		{ "CDM",            _T(BEGIN)  }, /* RGX_HWPERF_HW_CDMKICK */
		{ "CDM",            _T(END)    }, /* RGX_HWPERF_HW_CDMFINISHED */
		{ "TQ2D",           _T(BEGIN)  }, /* RGX_HWPERF_HW_TLAKICK */
		{ "TQ2D",           _T(END)    }, /* RGX_HWPERF_HW_TLAFINISHED */
		{ "3DSPM",          _T(BEGIN)  }, /* RGX_HWPERF_HW_3DSPMKICK */
		{ NULL,             0          }, /* RGX_HWPERF_HW_PERIODIC (unsupported) */
		{ "RTU",            _T(BEGIN)  }, /* RGX_HWPERF_HW_RTUKICK */
		{ "RTU",            _T(END)    }, /* RGX_HWPERF_HW_RTUFINISHED */
		{ "SHG",            _T(BEGIN)  }, /* RGX_HWPERF_HW_SHGKICK */
		{ "SHG",            _T(END)    }, /* RGX_HWPERF_HW_SHGFINISHED */
		{ "TQ3D",           _T(END)    }, /* RGX_HWPERF_HW_3DTQFINISHED */
		{ "3DSPM",          _T(END)    }, /* RGX_HWPERF_HW_3DSPMFINISHED */
		{ "PMOOM_TARESUME", _T(BEGIN)  }, /* RGX_HWPERF_HW_PMOOM_TARESUME */
		{ "TDM",            _T(BEGIN)  }, /* RGX_HWPERF_HW_TDMKICK */
		{ "TDM",            _T(END)    }, /* RGX_HWPERF_HW_TDMFINISHED */
		{ "NULL",           _T(SINGLE) }, /* RGX_HWPERF_HW_NULLKICK */
#undef _T
	};
	static_assert(RGX_HWPERF_HW_EVENT_RANGE0_FIRST_TYPE == RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE + 1,
				  "FW and HW events are not contiguous in RGX_HWPERF_EVENT_TYPE");

	PVR_ASSERT(psHWPerfPkt);
	eType = RGX_HWPERF_GET_TYPE(psHWPerfPkt);

	if (psFtraceData->ui32FTraceLastOrdinal != psHWPerfPkt->ui32Ordinal - 1)
	{
		RGX_HWPERF_STREAM_ID eStreamId = RGX_HWPERF_GET_STREAM_ID(psHWPerfPkt);
		_GpuTraceEventsLost(eStreamId,
		                    psFtraceData->ui32FTraceLastOrdinal,
		                    psHWPerfPkt->ui32Ordinal);
		PVR_DPF((PVR_DBG_ERROR, "FTrace events lost (stream_id = %u, ordinal: last = %u, current = %u)",
		         eStreamId, psFtraceData->ui32FTraceLastOrdinal, psHWPerfPkt->ui32Ordinal));
	}

	psFtraceData->ui32FTraceLastOrdinal = psHWPerfPkt->ui32Ordinal;

	/* Process UFO packets */
	if (eType == RGX_HWPERF_UFO)
	{
		_GpuTraceUfoEvent(psDevInfo, psHWPerfPkt);
		return IMG_TRUE;
	}

	if (eType <= RGX_HWPERF_HW_EVENT_RANGE0_LAST_TYPE)
	{
		/* this ID belongs to range 0, so index directly in range 0 */
		ui32HwEventTypeIndex = eType - RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
	}
	else
	{
		/* this ID belongs to range 1, so first index in range 1 and skip number of slots used up for range 0 */
		ui32HwEventTypeIndex = (eType - RGX_HWPERF_HW_EVENT_RANGE1_FIRST_TYPE) +
		                       (RGX_HWPERF_HW_EVENT_RANGE0_LAST_TYPE - RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE + 1);
	}

	if (ui32HwEventTypeIndex >= ARRAY_SIZE(aszHwEventTypeMap))
		goto err_unsupported;

	if (aszHwEventTypeMap[ui32HwEventTypeIndex].pszName == NULL)
	{
		/* Not supported map entry, ignore event */
		goto err_unsupported;
	}

	if (HWPERF_PACKET_IS_HW_TYPE(eType))
	{
		if (aszHwEventTypeMap[ui32HwEventTypeIndex].eSwType == PVR_GPUTRACE_SWITCH_TYPE_SINGLE)
		{
			_GpuTraceSwitchEvent(psDevInfo, psHWPerfPkt,
			                     aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
			                     PVR_GPUTRACE_SWITCH_TYPE_BEGIN);
			_GpuTraceSwitchEvent(psDevInfo, psHWPerfPkt,
			                     aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
			                     PVR_GPUTRACE_SWITCH_TYPE_END);
		}
		else
		{
			_GpuTraceSwitchEvent(psDevInfo, psHWPerfPkt,
			                     aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
			                     aszHwEventTypeMap[ui32HwEventTypeIndex].eSwType);
		}
	}
	else if (HWPERF_PACKET_IS_FW_TYPE(eType))
	{
		_GpuTraceFirmwareEvent(psDevInfo, psHWPerfPkt,
										aszHwEventTypeMap[ui32HwEventTypeIndex].pszName,
										aszHwEventTypeMap[ui32HwEventTypeIndex].eSwType);
	}
	else
	{
		goto err_unsupported;
	}

	return IMG_TRUE;

err_unsupported:
	PVR_DPF((PVR_DBG_VERBOSE, "%s: Unsupported event type %d", __func__, eType));
	return IMG_FALSE;
}


static void _GpuTraceProcessPackets(PVRSRV_RGXDEV_INFO *psDevInfo,
		void *pBuffer, IMG_UINT32 ui32ReadLen)
{
	IMG_UINT32			ui32TlPackets = 0;
	IMG_UINT32			ui32HWPerfPackets = 0;
	IMG_UINT32			ui32HWPerfPacketsSent = 0;
	void				*pBufferEnd;
	PVRSRVTL_PPACKETHDR psHDRptr;
	PVRSRVTL_PACKETTYPE ui16TlType;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDevInfo);
	PVR_ASSERT(pBuffer);
	PVR_ASSERT(ui32ReadLen);

	/* Process the TL Packets
	 */
	pBufferEnd = IMG_OFFSET_ADDR(pBuffer, ui32ReadLen);
	psHDRptr = GET_PACKET_HDR(pBuffer);
	while ( psHDRptr < (PVRSRVTL_PPACKETHDR)pBufferEnd )
	{
		ui16TlType = GET_PACKET_TYPE(psHDRptr);
		if (ui16TlType == PVRSRVTL_PACKETTYPE_DATA)
		{
			IMG_UINT16 ui16DataLen = GET_PACKET_DATA_LEN(psHDRptr);
			if (0 == ui16DataLen)
			{
				PVR_DPF((PVR_DBG_ERROR, "_GpuTraceProcessPackets: ZERO Data in TL data packet: %p", psHDRptr));
			}
			else
			{
				RGX_HWPERF_V2_PACKET_HDR* psHWPerfPkt;
				RGX_HWPERF_V2_PACKET_HDR* psHWPerfEnd;

				/* Check for lost hwperf data packets */
				psHWPerfEnd = RGX_HWPERF_GET_PACKET(GET_PACKET_DATA_PTR(psHDRptr)+ui16DataLen);
				psHWPerfPkt = RGX_HWPERF_GET_PACKET(GET_PACKET_DATA_PTR(psHDRptr));
				do
				{
					if (ValidAndEmitFTraceEvent(psDevInfo, psHWPerfPkt))
					{
						ui32HWPerfPacketsSent++;
					}
					ui32HWPerfPackets++;
					psHWPerfPkt = RGX_HWPERF_GET_NEXT_PACKET(psHWPerfPkt);
				}
				while (psHWPerfPkt < psHWPerfEnd);
			}
		}
		else if (ui16TlType == PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "_GpuTraceProcessPackets: Indication that the transport buffer was full"));
		}
		else
		{
			/* else Ignore padding packet type and others */
			PVR_DPF((PVR_DBG_MESSAGE, "_GpuTraceProcessPackets: Ignoring TL packet, type %d", ui16TlType ));
		}

		psHDRptr = GET_NEXT_PACKET_ADDR(psHDRptr);
		ui32TlPackets++;
	}

	PVR_DPF((PVR_DBG_VERBOSE, "_GpuTraceProcessPackets: TL "
			"Packets processed %03d, HWPerf packets %03d, sent %03d",
			ui32TlPackets, ui32HWPerfPackets, ui32HWPerfPacketsSent));

	PVR_DPF_RETURN;
}


static void _GpuTraceCmdCompleteNotify(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle)
{
	PVRSRV_RGXDEV_INFO* psDeviceInfo = hCmdCompHandle;
	RGX_HWPERF_FTRACE_DATA* psFtraceData;
	PVRSRV_ERROR		eError;
	IMG_PBYTE			pBuffer;
	IMG_UINT32			ui32ReadLen;
	IMG_BOOL			bFTraceLockAcquired = IMG_FALSE;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psDeviceInfo != NULL);

	psFtraceData = psDeviceInfo->pvGpuFtraceData;

	/* Command-complete notifiers can run concurrently. If this is
	 * happening, just bail out and let the previous call finish.
	 * This is ok because we can process the queued packets on the next call.
	 */
	bFTraceLockAcquired = OSTryLockAcquire(psFtraceData->hFTraceResourceLock);
	if (IMG_FALSE == bFTraceLockAcquired)
	{
		PVR_DPF_RETURN;
	}

	/* If this notifier is called, it means the TL resources will be valid at-least
	 * until the end of this call, since the DeInit function will wait on the hFTraceResourceLock
	 * to clean-up the TL resources and un-register the notifier, so just assert here.
	 */
	PVR_ASSERT(psFtraceData->hGPUTraceTLStream);

	/* If we have a valid stream attempt to acquire some data */
	eError = TLClientAcquireData(DIRECT_BRIDGE_HANDLE, psFtraceData->hGPUTraceTLStream, &pBuffer, &ui32ReadLen);
	if (eError == PVRSRV_OK)
	{
		/* Process the HWPerf packets and release the data */
		if (ui32ReadLen > 0)
		{
			PVR_DPF((PVR_DBG_VERBOSE, "_GpuTraceCmdCompleteNotify: DATA AVAILABLE offset=%p, length=%d", pBuffer, ui32ReadLen));

			/* Process the transport layer data for HWPerf packets... */
			_GpuTraceProcessPackets(psDeviceInfo, pBuffer, ui32ReadLen);

			eError = TLClientReleaseData(DIRECT_BRIDGE_HANDLE, psFtraceData->hGPUTraceTLStream);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "TLClientReleaseData");

				/* Serious error, disable FTrace GPU events */

				/* Release TraceLock so we always have the locking
				 * order BridgeLock->TraceLock to prevent AB-BA deadlocks*/
				OSLockRelease(psFtraceData->hFTraceResourceLock);
				OSLockAcquire(psFtraceData->hFTraceResourceLock);
				_GpuTraceDisable(psDeviceInfo, IMG_FALSE);
				OSLockRelease(psFtraceData->hFTraceResourceLock);
				goto out;

			}
		} /* else no data, ignore */
	}
	else if (eError != PVRSRV_ERROR_TIMEOUT)
	{
		PVR_LOG_ERROR(eError, "TLClientAcquireData");
	}
	if (bFTraceLockAcquired)
	{
		OSLockRelease(psFtraceData->hFTraceResourceLock);
	}
out:
	PVR_DPF_RETURN;
}

/* ----- AppHint interface -------------------------------------------------- */

static PVRSRV_ERROR _GpuTraceIsEnabledCallback(
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data,
	IMG_BOOL *value)
{
	PVR_UNREFERENCED_PARAMETER(device);
	PVR_UNREFERENCED_PARAMETER(private_data);

	*value = gbFTraceGPUEventsEnabled;

	return PVRSRV_OK;
}

static PVRSRV_ERROR _GpuTraceSetEnabledCallback(
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data,
	IMG_BOOL value)
{
	PVR_UNREFERENCED_PARAMETER(device);

	/* Lock down the state to avoid concurrent writes */
	OSLockAcquire(ghGPUTraceStateLock);

	if (value != gbFTraceGPUEventsEnabled)
	{
		PVRSRV_ERROR eError;
		if ((eError = _GpuTraceSetEnabledForAllDevices(value)) == PVRSRV_OK)
		{
			PVR_TRACE(("%s GPU FTrace", value ? "ENABLED" : "DISABLED"));
			gbFTraceGPUEventsEnabled = value;
		}
		else
		{
			PVR_TRACE(("FAILED to %s GPU FTrace", value ? "enable" : "disable"));
			/* On failure, partial enable/disable might have resulted.
			 * Try best to restore to previous state. Ignore error */
			_GpuTraceSetEnabledForAllDevices(gbFTraceGPUEventsEnabled);

			OSLockRelease(ghGPUTraceStateLock);
			return eError;
		}
	}
	else
	{
		PVR_TRACE(("GPU FTrace already %s!", value ? "enabled" : "disabled"));
	}

	OSLockRelease(ghGPUTraceStateLock);

	return PVRSRV_OK;
}

void PVRGpuTraceInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRVAppHintRegisterHandlersBOOL(APPHINT_ID_EnableFTraceGPU,
	                                  _GpuTraceIsEnabledCallback,
	                                  _GpuTraceSetEnabledCallback,
	                                  psDeviceNode, NULL);
}

/* ----- FTrace event callbacks -------------------------------------------- */

void PVRGpuTraceEnableUfoCallback(void)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetPVRSRVData()->psDeviceNodeList;
#if defined(SUPPORT_RGX)
	PVRSRV_RGXDEV_INFO *psRgxDevInfo;
	PVRSRV_ERROR eError;
#endif

	/* Lock down events state, for consistent value of guiUfoEventRef */
	OSLockAcquire(ghLockFTraceEventLock);
	if (guiUfoEventRef++ == 0)
	{
		/* make sure UFO events are enabled on all rogue devices */
		while (psDeviceNode)
		{
#if defined(SUPPORT_RGX)
			IMG_UINT64 ui64Filter;

			psRgxDevInfo = psDeviceNode->pvDevice;
			ui64Filter = RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO) |
							psRgxDevInfo->ui64HWPerfFilter;
			/* Small chance exists that ui64HWPerfFilter can be changed here and
			 * the newest filter value will be changed to the old one + UFO event.
			 * This is not a critical problem. */
			eError = PVRSRVRGXCtrlHWPerfKM(NULL, psDeviceNode, RGX_HWPERF_STREAM_ID0_FW,
											IMG_FALSE, ui64Filter);
			if (eError == PVRSRV_ERROR_NOT_INITIALISED)
			{
				/* If we land here that means that the FW is not initialised yet.
				 * We stored the filter and it will be passed to the firmware
				 * during its initialisation phase. So ignore. */
			}
			else if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "Could not enable UFO HWPerf events on device %d", psDeviceNode->sDevId.i32OsDeviceID));
			}
#endif
			psDeviceNode = psDeviceNode->psNext;
		}
	}
	OSLockRelease(ghLockFTraceEventLock);
}

void PVRGpuTraceDisableUfoCallback(void)
{
#if defined(SUPPORT_RGX)
	PVRSRV_ERROR eError;
#endif
	PVRSRV_DEVICE_NODE *psDeviceNode;

	/* We have to check if lock is valid because on driver unload
	 * PVRGpuTraceSupportDeInit is called before kernel disables the ftrace
	 * events. This means that the lock will be destroyed before this callback
	 * is called.
	 * We can safely return if that situation happens because driver will be
	 * unloaded so we don't care about HWPerf state anymore. */
	if (ghLockFTraceEventLock == NULL)
		return;

	psDeviceNode = PVRSRVGetPVRSRVData()->psDeviceNodeList;

	/* Lock down events state, for consistent value of guiUfoEventRef */
	OSLockAcquire(ghLockFTraceEventLock);
	if (--guiUfoEventRef == 0)
	{
		/* make sure UFO events are disabled on all rogue devices */
		while (psDeviceNode)
		{
#if defined(SUPPORT_RGX)
			IMG_UINT64 ui64Filter;
			PVRSRV_RGXDEV_INFO *psRgxDevInfo = psDeviceNode->pvDevice;

			ui64Filter = ~(RGX_HWPERF_EVENT_MASK_VALUE(RGX_HWPERF_UFO)) &
					psRgxDevInfo->ui64HWPerfFilter;
			/* Small chance exists that ui64HWPerfFilter can be changed here and
			 * the newest filter value will be changed to the old one + UFO event.
			 * This is not a critical problem. */
			eError = PVRSRVRGXCtrlHWPerfKM(NULL, psDeviceNode, RGX_HWPERF_STREAM_ID0_FW,
			                               IMG_FALSE, ui64Filter);
			if (eError == PVRSRV_ERROR_NOT_INITIALISED)
			{
				/* If we land here that means that the FW is not initialised yet.
				 * We stored the filter and it will be passed to the firmware
				 * during its initialisation phase. So ignore. */
			}
			else if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "Could not disable UFO HWPerf events on device %d",
				        psDeviceNode->sDevId.i32OsDeviceID));
			}
#endif
			psDeviceNode = psDeviceNode->psNext;
		}
	}
	OSLockRelease(ghLockFTraceEventLock);
}

void PVRGpuTraceEnableFirmwareActivityCallback(void)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetPVRSRVData()->psDeviceNodeList;
#if defined(SUPPORT_RGX)
	PVRSRV_RGXDEV_INFO *psRgxDevInfo;
	uint64_t ui64Filter, ui64FWEventsFilter = 0;
	int i;

	for (i = RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
		 i <= RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE; i++)
	{
		ui64FWEventsFilter |= RGX_HWPERF_EVENT_MASK_VALUE(i);
	}
#endif
	OSLockAcquire(ghLockFTraceEventLock);
	/* Enable all FW events on all the devices */
	while (psDeviceNode)
	{
#if defined(SUPPORT_RGX)
		PVRSRV_ERROR eError;
		psRgxDevInfo = psDeviceNode->pvDevice;
		ui64Filter = psRgxDevInfo->ui64HWPerfFilter | ui64FWEventsFilter;

		eError = PVRSRVRGXCtrlHWPerfKM(NULL, psDeviceNode, RGX_HWPERF_STREAM_ID0_FW,
		                               IMG_FALSE, ui64Filter);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Could not enable HWPerf event for firmware"
			        " task timings (%s).", PVRSRVGetErrorString(eError)));
		}
#endif
		psDeviceNode = psDeviceNode->psNext;
	}
	OSLockRelease(ghLockFTraceEventLock);
}

void PVRGpuTraceDisableFirmwareActivityCallback(void)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
#if defined(SUPPORT_RGX)
	IMG_UINT64 ui64FWEventsFilter = ~0;
	int i;
#endif

	/* We have to check if lock is valid because on driver unload
	 * PVRGpuTraceSupportDeInit is called before kernel disables the ftrace
	 * events. This means that the lock will be destroyed before this callback
	 * is called.
	 * We can safely return if that situation happens because driver will be
	 * unloaded so we don't care about HWPerf state anymore. */
	if (ghLockFTraceEventLock == NULL)
		return;

	psDeviceNode = PVRSRVGetPVRSRVData()->psDeviceNodeList;

#if defined(SUPPORT_RGX)
	for (i = RGX_HWPERF_FW_EVENT_RANGE_FIRST_TYPE;
		 i <= RGX_HWPERF_FW_EVENT_RANGE_LAST_TYPE; i++)
	{
		ui64FWEventsFilter &= ~RGX_HWPERF_EVENT_MASK_VALUE(i);
	}
#endif

	OSLockAcquire(ghLockFTraceEventLock);

	/* Disable all FW events on all the devices */
	while (psDeviceNode)
	{
#if defined(SUPPORT_RGX)
		PVRSRV_RGXDEV_INFO *psRgxDevInfo = psDeviceNode->pvDevice;
		IMG_UINT64 ui64Filter = psRgxDevInfo->ui64HWPerfFilter & ui64FWEventsFilter;

		if (PVRSRVRGXCtrlHWPerfKM(NULL, psDeviceNode, RGX_HWPERF_STREAM_ID0_FW,
		                          IMG_FALSE, ui64Filter) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Could not disable HWPerf event for firmware task timings."));
		}
#endif
		psDeviceNode = psDeviceNode->psNext;
	}

	OSLockRelease(ghLockFTraceEventLock);
}

/******************************************************************************
 End of file (pvr_gputrace.c)
******************************************************************************/
