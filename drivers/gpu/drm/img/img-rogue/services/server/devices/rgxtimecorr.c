/*************************************************************************/ /*!
@File
@Title          Device specific time correlation and calibration routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific time correlation and calibration routines
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

#include "img_defs.h"
#include "rgxtimecorr.h"
#include "rgxfwutils.h"
#include "htbserver.h"
#include "pvrsrv_apphint.h"

/******************************************************************************
 *
 * - A calibration period is started on power-on and after a DVFS transition,
 *   and it's closed before a power-off and before a DVFS transition
 *   (so power-on -> dfvs -> dvfs -> power-off , power on -> dvfs -> dvfs...,
 *   where each arrow is a calibration period).
 *
 * - The timers on the Host and on the FW are correlated at the beginning of
 *   each period together with the current GPU frequency.
 *
 * - Correlation and calibration are also done at regular intervals using
 *   a best effort approach.
 *
 *****************************************************************************/

static IMG_UINT32 g_ui32ClockSource = PVRSRV_APPHINT_TIMECORRCLOCK;

/*
	AppHint interfaces
*/

static PVRSRV_ERROR _SetClock(const PVRSRV_DEVICE_NODE *psDeviceNode,
                              const void *psPrivate,
                              IMG_UINT32 ui32Value)
{
	static __maybe_unused const char* const apszClocks[] = {
		"mono", "mono_raw", "sched"
	};

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	if (ui32Value >= RGXTIMECORR_CLOCK_LAST)
	{
		PVR_DPF((PVR_DBG_ERROR, "Invalid clock source type (%u)", ui32Value));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	RGXTimeCorrEnd((PVRSRV_DEVICE_NODE *) psDeviceNode,
	               RGXTIMECORR_EVENT_CLOCK_CHANGE);

	PVR_DPF((PVR_DBG_WARNING, "Setting time correlation clock from \"%s\" to \"%s\"",
			apszClocks[g_ui32ClockSource],
			apszClocks[ui32Value]));

	g_ui32ClockSource = ui32Value;

	RGXTimeCorrBegin((PVRSRV_DEVICE_NODE *) psDeviceNode,
	                 RGXTIMECORR_EVENT_CLOCK_CHANGE);

	return PVRSRV_OK;
}

static PVRSRV_ERROR _GetClock(const PVRSRV_DEVICE_NODE *psDeviceNode,
                              const void *psPrivate,
                              IMG_UINT32 *pui32Value)
{
	*pui32Value = g_ui32ClockSource;

	PVR_UNREFERENCED_PARAMETER(psPrivate);

	return PVRSRV_OK;
}

void RGXTimeCorrInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_TimeCorrClock, _GetClock,
	                                    _SetClock, psDeviceNode, NULL);
}

/*
	End of AppHint interface
*/

IMG_UINT64 RGXTimeCorrGetClockns64(void)
{
	IMG_UINT64 ui64Clock;

	switch (g_ui32ClockSource) {
		case RGXTIMECORR_CLOCK_MONO:
			return ((void) OSClockMonotonicns64(&ui64Clock), ui64Clock);
		case RGXTIMECORR_CLOCK_MONO_RAW:
			return OSClockMonotonicRawns64();
		case RGXTIMECORR_CLOCK_SCHED:
			return OSClockns64();
		default:
			PVR_ASSERT(IMG_FALSE);
			return 0;
	}
}

IMG_UINT64 RGXTimeCorrGetClockus64(void)
{
	IMG_UINT32 rem;
	return OSDivide64r64(RGXTimeCorrGetClockns64(), 1000, &rem);
}

void RGXGetTimeCorrData(PVRSRV_DEVICE_NODE *psDeviceNode,
							RGXFWIF_TIME_CORR *psTimeCorrs,
							IMG_UINT32 ui32NumOut)
{
	PVRSRV_RGXDEV_INFO    *psDevInfo     = psDeviceNode->pvDevice;
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32 ui32CurrentIndex = psGpuUtilFWCB->ui32TimeCorrSeqCount;

	while (ui32NumOut--)
	{
		*(psTimeCorrs++) = psGpuUtilFWCB->sTimeCorr[RGXFWIF_TIME_CORR_CURR_INDEX(ui32CurrentIndex)];
		ui32CurrentIndex--;
	}
}

static __maybe_unused const IMG_CHAR* _EventToString(RGXTIMECORR_EVENT eEvent)
{
	switch (eEvent)
	{
		case RGXTIMECORR_EVENT_POWER:
			return "power";
		case RGXTIMECORR_EVENT_DVFS:
			return "dvfs";
		case RGXTIMECORR_EVENT_PERIODIC:
			return "periodic";
		case RGXTIMECORR_EVENT_CLOCK_CHANGE:
			return "clock source";
		default:
			return "n/a";
	}
}

static inline IMG_UINT32 _RGXGetSystemLayerGPUClockSpeed(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	RGX_DATA *psRGXData = (RGX_DATA*)psDeviceNode->psDevConfig->hDevData;

	return psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;
}

static inline IMG_UINT32 _RGXGetEstimatedGPUClockSpeed(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGX_GPU_DVFS_TABLE *psGpuDVFSTable = psDevInfo->psGpuDVFSTable;
	GPU_FREQ_TRACKING_DATA *psTrackingData;

	psTrackingData = &psGpuDVFSTable->asTrackingData[psGpuDVFSTable->ui32FreqIndex];

	return psTrackingData->ui32EstCoreClockSpeed;
}

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
static inline void _DumpTimerCorrelationHistory(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGX_GPU_DVFS_TABLE *psGpuDVFSTable = psDevInfo->psGpuDVFSTable;
	IMG_UINT32 i = psGpuDVFSTable->ui32HistoryIndex;

	PVR_DPF((PVR_DBG_ERROR, "Dumping history of timer correlation data (latest first):"));

	do
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "  Begin times: OS %" IMG_UINT64_FMTSPEC ", CR %" IMG_UINT64_FMTSPEC ", "
				 "End times: OS %" IMG_UINT64_FMTSPEC ", CR %" IMG_UINT64_FMTSPEC ", "
				 "Core clk %u, Estimated clk %u",
				 psGpuDVFSTable->asTrackingHistory[i].ui64BeginOSTimestamp,
				 psGpuDVFSTable->asTrackingHistory[i].ui64BeginCRTimestamp,
				 psGpuDVFSTable->asTrackingHistory[i].ui64EndOSTimestamp,
				 psGpuDVFSTable->asTrackingHistory[i].ui64EndCRTimestamp,
				 psGpuDVFSTable->asTrackingHistory[i].ui32CoreClockSpeed,
				 psGpuDVFSTable->asTrackingHistory[i].ui32EstCoreClockSpeed));

		i = (i - 1) % RGX_GPU_FREQ_TRACKING_SIZE;

	} while (i != psGpuDVFSTable->ui32HistoryIndex);
}
#endif

static void _RGXMakeTimeCorrData(PVRSRV_DEVICE_NODE *psDeviceNode, RGXTIMECORR_EVENT eEvent)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32 ui32NewSeqCount = psGpuUtilFWCB->ui32TimeCorrSeqCount + 1;
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[RGXFWIF_TIME_CORR_CURR_INDEX(ui32NewSeqCount)];

	/*
	 * The following reads must be done as close together as possible, because
	 * they represent the same current time sampled from different clock sources.
	 */
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (OSClockMonotonicns64(&psTimeCorr->ui64OSMonoTimeStamp) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "_RGXMakeTimeCorrData: System Monotonic Clock not available."));
		PVR_ASSERT(0);
	}
#endif
	psTimeCorr->ui64CRTimeStamp = RGXReadHWTimerReg(psDevInfo);
	psTimeCorr->ui64OSTimeStamp = RGXTimeCorrGetClockns64();
	psTimeCorr->ui32CoreClockSpeed = _RGXGetEstimatedGPUClockSpeed(psDevInfo);
	psTimeCorr->ui64CRDeltaToOSDeltaKNs = RGXTimeCorrGetConversionFactor(psTimeCorr->ui32CoreClockSpeed);

	if (psTimeCorr->ui64CRDeltaToOSDeltaKNs == 0)
	{
#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
		_DumpTimerCorrelationHistory(psDevInfo);
#endif

		/* Revert to original clock speed (error already printed) */
		psTimeCorr->ui32CoreClockSpeed = _RGXGetSystemLayerGPUClockSpeed(psDeviceNode);
		psTimeCorr->ui64CRDeltaToOSDeltaKNs = RGXTimeCorrGetConversionFactor(psTimeCorr->ui32CoreClockSpeed);
	}

	/* Make sure the values are written to memory before updating the index of the current entry */
	OSWriteMemoryBarrier();

	/* Update the index of the current entry in the timer correlation array */
	psGpuUtilFWCB->ui32TimeCorrSeqCount = ui32NewSeqCount;

	PVR_DPF((PVR_DBG_MESSAGE,
	         "Timer correlation data (post %s event): OS %" IMG_UINT64_FMTSPEC " ns, "
	         "CR %" IMG_UINT64_FMTSPEC ", GPU freq. %u Hz (given as %u Hz)",
	         _EventToString(eEvent),
	         psTimeCorr->ui64OSTimeStamp,
	         psTimeCorr->ui64CRTimeStamp,
	         RGXFWIF_ROUND_TO_KHZ(psTimeCorr->ui32CoreClockSpeed),
	         _RGXGetSystemLayerGPUClockSpeed(psDeviceNode)));

	/*
	 * Don't log timing data to the HTB log after a power(-on) event.
	 * Otherwise this will be logged before the HTB partition marker, breaking
	 * the log sync grammar. This data will be automatically repeated when the
	 * partition marker is written.
	 */
	HTBSyncScale(eEvent != RGXTIMECORR_EVENT_POWER,
	             psTimeCorr->ui64OSTimeStamp,
	             psTimeCorr->ui64CRTimeStamp,
	             psTimeCorr->ui32CoreClockSpeed);
}

static void _RGXCheckTimeCorrData(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  RGX_GPU_DVFS_TABLE *psGpuDVFSTable)
{
#if !defined(NO_HARDWARE) && !defined(VIRTUAL_PLATFORM) && defined(DEBUG)
#define SCALING_FACTOR (10)
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_GPU_UTIL_FWCB *psGpuUtilFWCB = psDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32 ui32Index = RGXFWIF_TIME_CORR_CURR_INDEX(psGpuUtilFWCB->ui32TimeCorrSeqCount);
	RGXFWIF_TIME_CORR *psTimeCorr = &psGpuUtilFWCB->sTimeCorr[ui32Index];
	IMG_UINT64 ui64EstimatedTime, ui64CRTimeStamp, ui64OSTimeStamp;
	IMG_UINT64 ui64CRTimeDiff, ui64OSTimeDiff;
	IMG_INT64 i64Diff;
	IMG_UINT32 ui32Ratio, ui32Remainder;

	/*
	 * The following reads must be done as close together as possible, because
	 * they represent the same current time sampled from different clock sources.
	 */
	ui64CRTimeStamp = RGXReadHWTimerReg(psDevInfo);
	ui64OSTimeStamp = RGXTimeCorrGetClockns64();

	if ((ui64OSTimeStamp - psTimeCorr->ui64OSTimeStamp) < (1 << SCALING_FACTOR))
	{
		/*
		 * Less than ~1us has passed since the timer correlation data was generated.
		 * A time frame this short is probably not enough to get an estimate
		 * of how good the timer correlation data was.
		 * Skip calculations for the above reason and to avoid a division by 0 below.
		 */
		return;
	}


	/* Calculate an estimated timestamp based on the latest timer correlation data */
	ui64CRTimeDiff = ui64CRTimeStamp - psTimeCorr->ui64CRTimeStamp;
	ui64OSTimeDiff = RGXFWIF_GET_DELTA_OSTIME_NS(ui64CRTimeDiff,
	                                             psTimeCorr->ui64CRDeltaToOSDeltaKNs);
	ui64EstimatedTime = psTimeCorr->ui64OSTimeStamp + ui64OSTimeDiff;

	/* Get difference between estimated timestamp and current timestamp, in ns */
	i64Diff = ui64EstimatedTime - ui64OSTimeStamp;

	/*
	 * Calculate ratio between estimated time diff and real time diff:
	 * ratio% : 100% = (OSestimate - OStimecorr) : (OSreal - OStimecorr)
	 *
	 * The operands are scaled down (approximately from ns to us) so at least
	 * the divisor fits on 32 bit.
	 */
	ui32Ratio = OSDivide64(((ui64EstimatedTime - psTimeCorr->ui64OSTimeStamp) * 100ULL) >> SCALING_FACTOR,
	                       (ui64OSTimeStamp - psTimeCorr->ui64OSTimeStamp) >> SCALING_FACTOR,
	                       &ui32Remainder);

	PVR_DPF((PVR_DBG_MESSAGE,
	         "Estimated timestamp check: diff %" IMG_INT64_FMTSPECd " ns over "
	         "period %" IMG_UINT64_FMTSPEC " ns, estimated timer speed %u%%",
	         i64Diff,
	         ui64OSTimeStamp - psTimeCorr->ui64OSTimeStamp,
	         ui32Ratio));

	/* Warn if the estimated timestamp is not within +/- 1% of the current time */
	if (ui32Ratio < 99 || ui32Ratio > 101)
	{
		PVR_DPF((PVR_DBG_WARNING,
		         "Estimated timestamps generated in the last %" IMG_UINT64_FMTSPEC " ns "
		         "were %s the real time (increasing at %u%% speed)",
		         ui64OSTimeStamp - psTimeCorr->ui64OSTimeStamp,
		         i64Diff > 0 ? "ahead of" : "behind",
		         ui32Ratio));

		/* Higher ratio == higher delta OS == higher delta CR == frequency higher than expected (and viceversa) */
		PVR_DPF((PVR_DBG_WARNING,
		         "Current GPU frequency %u Hz (given as %u Hz) is probably %s than expected",
		         RGXFWIF_ROUND_TO_KHZ(psTimeCorr->ui32CoreClockSpeed),
		         _RGXGetSystemLayerGPUClockSpeed(psDeviceNode),
		         i64Diff > 0 ? "lower" : "higher"));
	}
#else
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psGpuDVFSTable);
#endif
}

static inline IMG_UINT32 _RGXGPUFreqGetIndex(RGX_GPU_DVFS_TABLE *psGpuDVFSTable, IMG_UINT32 ui32CoreClockSpeed)
{
	IMG_UINT32 *paui32GPUFrequencies = psGpuDVFSTable->aui32GPUFrequency;
	IMG_UINT32 i;

	for (i = 0; i < RGX_GPU_DVFS_TABLE_SIZE; i++)
	{
		if (paui32GPUFrequencies[i] == ui32CoreClockSpeed)
		{
			return i;
		}

		if (paui32GPUFrequencies[i] == 0)
		{
			paui32GPUFrequencies[i] = ui32CoreClockSpeed;
			return i;
		}
	}

	i--;

	PVR_DPF((PVR_DBG_ERROR, "GPU frequency table in the driver is full! "
	         "Table size should be increased! Overriding last entry (%u) with %u",
	         paui32GPUFrequencies[i], ui32CoreClockSpeed));

	paui32GPUFrequencies[i] = ui32CoreClockSpeed;

	return i;
}

static void _RGXGPUFreqCalibrationPeriodStart(PVRSRV_DEVICE_NODE *psDeviceNode, RGX_GPU_DVFS_TABLE *psGpuDVFSTable)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	GPU_FREQ_TRACKING_DATA *psTrackingData;
	IMG_UINT32 ui32CoreClockSpeed, ui32Index;

	IMG_UINT64 ui64CRTimestamp = RGXReadHWTimerReg(psDevInfo);
	IMG_UINT64 ui64OSTimestamp = RGXTimeCorrGetClockus64();

	psGpuDVFSTable->ui64CalibrationCRTimestamp = ui64CRTimestamp;
	psGpuDVFSTable->ui64CalibrationOSTimestamp = ui64OSTimestamp;

	ui32CoreClockSpeed = _RGXGetSystemLayerGPUClockSpeed(psDeviceNode);
	ui32Index          = _RGXGPUFreqGetIndex(psGpuDVFSTable, ui32CoreClockSpeed);
	psTrackingData     = &psGpuDVFSTable->asTrackingData[ui32Index];

	/* Set the time needed to (re)calibrate the GPU frequency */
	if (psTrackingData->ui32CalibrationCount == 0) /* We never met this frequency */
	{
		psTrackingData->ui32EstCoreClockSpeed = ui32CoreClockSpeed;
		psGpuDVFSTable->ui32CalibrationPeriod = RGX_GPU_DVFS_FIRST_CALIBRATION_TIME_US;
	}
	else if (psTrackingData->ui32CalibrationCount == 1) /* We calibrated this frequency only once */
	{
		psGpuDVFSTable->ui32CalibrationPeriod = RGX_GPU_DVFS_TRANSITION_CALIBRATION_TIME_US;
	}
	else
	{
		psGpuDVFSTable->ui32CalibrationPeriod = RGX_GPU_DVFS_PERIODIC_CALIBRATION_TIME_US;
	}

	/* Update the index to the DVFS table */
	psGpuDVFSTable->ui32FreqIndex = ui32Index;

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
	/* Update tracking history */
	{
		GPU_FREQ_TRACKING_HISTORY *psTrackingHistory;

		psTrackingHistory = &psGpuDVFSTable->asTrackingHistory[psGpuDVFSTable->ui32HistoryIndex];
		psTrackingHistory->ui32CoreClockSpeed    = ui32CoreClockSpeed;
		psTrackingHistory->ui32EstCoreClockSpeed = psTrackingData->ui32EstCoreClockSpeed;
		psTrackingHistory->ui64BeginCRTimestamp  = ui64CRTimestamp;
		psTrackingHistory->ui64BeginOSTimestamp  = ui64OSTimestamp;
		psTrackingHistory->ui64EndCRTimestamp    = 0ULL;
		psTrackingHistory->ui64EndOSTimestamp    = 0ULL;
	}
#endif
}

static void _RGXGPUFreqCalibrationPeriodStop(PVRSRV_DEVICE_NODE *psDeviceNode,
											 RGX_GPU_DVFS_TABLE *psGpuDVFSTable)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	IMG_UINT64 ui64CRTimestamp = RGXReadHWTimerReg(psDevInfo);
	IMG_UINT64 ui64OSTimestamp = RGXTimeCorrGetClockus64();

	psGpuDVFSTable->ui64CalibrationCRTimediff =
	    ui64CRTimestamp - psGpuDVFSTable->ui64CalibrationCRTimestamp;
	psGpuDVFSTable->ui64CalibrationOSTimediff =
	    ui64OSTimestamp - psGpuDVFSTable->ui64CalibrationOSTimestamp;

	/* Check if the current timer correlation data is good enough */
	_RGXCheckTimeCorrData(psDeviceNode, psGpuDVFSTable);

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
	/* Update tracking history */
	{
		GPU_FREQ_TRACKING_HISTORY *psTrackingHistory;

		psTrackingHistory = &psGpuDVFSTable->asTrackingHistory[psGpuDVFSTable->ui32HistoryIndex];
		psTrackingHistory->ui64EndCRTimestamp = ui64CRTimestamp;
		psTrackingHistory->ui64EndOSTimestamp = ui64OSTimestamp;
	}
#endif
}

static void _RGXGPUFreqCalibrationCalculate(PVRSRV_DEVICE_NODE *psDeviceNode,
                                            RGX_GPU_DVFS_TABLE *psGpuDVFSTable,
                                            RGXTIMECORR_EVENT   eEvent)
{
#if !defined(DISABLE_GPU_FREQUENCY_CALIBRATION)
	GPU_FREQ_TRACKING_DATA *psTrackingData;
	IMG_UINT32 ui32EstCoreClockSpeed, ui32PrevCoreClockSpeed;
	IMG_INT32  i32Diff;
	IMG_UINT32 ui32Remainder;

	/*
	 * Find out what the GPU frequency was in the last period.
	 * This should return a value very close to the frequency passed by the system layer.
	 */
	ui32EstCoreClockSpeed =
	    RGXFWIF_GET_GPU_CLOCK_FREQUENCY_HZ(psGpuDVFSTable->ui64CalibrationCRTimediff,
	                                       psGpuDVFSTable->ui64CalibrationOSTimediff,
	                                       ui32Remainder);

	/* Update GPU frequency used by the driver for a given system layer frequency */
	psTrackingData = &psGpuDVFSTable->asTrackingData[psGpuDVFSTable->ui32FreqIndex];

	ui32PrevCoreClockSpeed = psTrackingData->ui32EstCoreClockSpeed;
	psTrackingData->ui32EstCoreClockSpeed = ui32EstCoreClockSpeed;
	psTrackingData->ui32CalibrationCount++;

	i32Diff = (IMG_INT32) (ui32EstCoreClockSpeed - ui32PrevCoreClockSpeed);

	if ((i32Diff < -1000000) || (i32Diff > 1000000))
	{
		/* Warn if the frequency changed by more than 1 MHz between recalculations */
		PVR_DPF((PVR_DBG_WARNING,
		         "GPU frequency calibration of system layer frequency %u Hz (pre %s event): "
		         "more than 1 MHz difference between old and new value "
		         "(%u Hz -> %u Hz over %"  IMG_UINT64_FMTSPEC " us)",
		         _RGXGetSystemLayerGPUClockSpeed(psDeviceNode),
		         _EventToString(eEvent),
		         RGXFWIF_ROUND_TO_KHZ(ui32PrevCoreClockSpeed),
		         RGXFWIF_ROUND_TO_KHZ(ui32EstCoreClockSpeed),
		         psGpuDVFSTable->ui64CalibrationOSTimediff));
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE,
		         "GPU frequency calibration of system layer frequency %u Hz (pre %s event): "
		         "%u Hz -> %u Hz done over %" IMG_UINT64_FMTSPEC " us",
		         _RGXGetSystemLayerGPUClockSpeed(psDeviceNode),
		         _EventToString(eEvent),
		         RGXFWIF_ROUND_TO_KHZ(ui32PrevCoreClockSpeed),
		         RGXFWIF_ROUND_TO_KHZ(ui32EstCoreClockSpeed),
		         psGpuDVFSTable->ui64CalibrationOSTimediff));
	}

	/* Reset time deltas to avoid recalibrating the same frequency over and over again */
	psGpuDVFSTable->ui64CalibrationCRTimediff = 0;
	psGpuDVFSTable->ui64CalibrationOSTimediff = 0;

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
	/* Update tracking history */
	{
		GPU_FREQ_TRACKING_HISTORY *psTrackingHistory;

		psTrackingHistory = &psGpuDVFSTable->asTrackingHistory[psGpuDVFSTable->ui32HistoryIndex];
		psTrackingHistory->ui32EstCoreClockSpeed = ui32EstCoreClockSpeed;
		psGpuDVFSTable->ui32HistoryIndex =
			(psGpuDVFSTable->ui32HistoryIndex + 1) % RGX_GPU_FREQ_TRACKING_SIZE;
	}
#endif

#else
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psGpuDVFSTable);
	PVR_UNREFERENCED_PARAMETER(eEvent);
#endif
}

void RGXTimeCorrBegin(IMG_HANDLE hDevHandle, RGXTIMECORR_EVENT eEvent)
{
	PVRSRV_DEVICE_NODE  *psDeviceNode   = hDevHandle;
	PVRSRV_RGXDEV_INFO  *psDevInfo      = psDeviceNode->pvDevice;
	RGX_GPU_DVFS_TABLE  *psGpuDVFSTable = psDevInfo->psGpuDVFSTable;
	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	_RGXGPUFreqCalibrationPeriodStart(psDeviceNode, psGpuDVFSTable);
	_RGXMakeTimeCorrData(psDeviceNode, eEvent);
}

void RGXTimeCorrEnd(IMG_HANDLE hDevHandle, RGXTIMECORR_EVENT eEvent)
{
	PVRSRV_DEVICE_NODE  *psDeviceNode   = hDevHandle;
	PVRSRV_RGXDEV_INFO  *psDevInfo      = psDeviceNode->pvDevice;
	RGX_GPU_DVFS_TABLE  *psGpuDVFSTable = psDevInfo->psGpuDVFSTable;
	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	_RGXGPUFreqCalibrationPeriodStop(psDeviceNode, psGpuDVFSTable);

	if (psGpuDVFSTable->ui64CalibrationOSTimediff >= psGpuDVFSTable->ui32CalibrationPeriod)
	{
		_RGXGPUFreqCalibrationCalculate(psDeviceNode, psGpuDVFSTable, eEvent);
	}
}

void RGXTimeCorrRestartPeriodic(IMG_HANDLE hDevHandle)
{
	PVRSRV_DEVICE_NODE     *psDeviceNode   = hDevHandle;
	PVRSRV_RGXDEV_INFO     *psDevInfo      = psDeviceNode->pvDevice;
	RGX_GPU_DVFS_TABLE     *psGpuDVFSTable = psDevInfo->psGpuDVFSTable;
	IMG_UINT64             ui64TimeNow     = RGXTimeCorrGetClockus64();
	PVRSRV_DEV_POWER_STATE ePowerState = PVRSRV_DEV_POWER_STATE_DEFAULT;
	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	if (psGpuDVFSTable == NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Required data not initialised yet", __func__));
		return;
	}

	/* Check if it's the right time to recalibrate the GPU clock frequency */
	if ((ui64TimeNow - psGpuDVFSTable->ui64CalibrationOSTimestamp) < psGpuDVFSTable->ui32CalibrationPeriod) return;

	/* Try to acquire the powerlock, if not possible then don't wait */
	if (PVRSRVPowerTryLock(psDeviceNode) != PVRSRV_OK) return;

	/* If the GPU is off then we can't do anything */
	PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);
	if (ePowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		PVRSRVPowerUnlock(psDeviceNode);
		return;
	}

	/* All checks passed, we can calibrate and correlate */
	RGXTimeCorrEnd(psDeviceNode, RGXTIMECORR_EVENT_PERIODIC);
	RGXTimeCorrBegin(psDeviceNode, RGXTIMECORR_EVENT_PERIODIC);

	PVRSRVPowerUnlock(psDeviceNode);
}

/*
	RGXTimeCorrGetClockSource
*/
RGXTIMECORR_CLOCK_TYPE RGXTimeCorrGetClockSource(void)
{
	return g_ui32ClockSource;
}

/*
	RGXTimeCorrSetClockSource
*/
PVRSRV_ERROR RGXTimeCorrSetClockSource(PVRSRV_DEVICE_NODE *psDeviceNode,
                                       RGXTIMECORR_CLOCK_TYPE eClockType)
{
	return _SetClock(psDeviceNode, NULL, eClockType);
}

PVRSRV_ERROR
PVRSRVRGXCurrentTime(CONNECTION_DATA    * psConnection,
                     PVRSRV_DEVICE_NODE * psDeviceNode,
                     IMG_UINT64         * pui64Time)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	*pui64Time = RGXTimeCorrGetClockns64();

	return PVRSRV_OK;
}

/******************************************************************************
 End of file (rgxtimecorr.c)
******************************************************************************/
