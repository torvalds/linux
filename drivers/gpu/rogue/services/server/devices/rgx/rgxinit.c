/*************************************************************************/ /*!
@File
@Title          Device specific initialisation routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#include <stddef.h>

#include "pvrsrv.h"
#include "rgxheapconfig.h"
#include "rgxpower.h"

#include "rgxinit.h"

#include "pdump_km.h"
#include "handle.h"
#include "allocmem.h"
#include "devicemem_pdump.h"
#include "rgxmem.h"
#include "sync_internal.h"

#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgx_fwif_km.h"

#include "rgxmmuinit.h"
#include "devicemem_utils.h"
#include "devicemem_server.h"
#include "physmem_osmem.h"

#include "rgxdebug.h"
#include "rgxhwperf.h"

#include "rgx_options_km.h"
#include "pvrversion.h"

#include "rgx_compat_bvnc.h"

#include "rgx_heaps.h"

#include "rgxta3d.h"
#include "debug_request_ids.h"

static PVRSRV_ERROR RGXDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT32 ui32ClientBuildOptions);
static PVRSRV_ERROR RGXDevVersionString(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_CHAR **ppszVersionString);
static PVRSRV_ERROR RGXDevClockSpeed(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_PUINT32  pui32RGXClockSpeed);
static PVRSRV_ERROR RGXSoftReset(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT64  ui64ResetValue);

#define RGX_MMU_LOG2_PAGE_SIZE_4KB   (12)
#define RGX_MMU_LOG2_PAGE_SIZE_16KB  (14)
#define RGX_MMU_LOG2_PAGE_SIZE_64KB  (16)
#define RGX_MMU_LOG2_PAGE_SIZE_256KB (18)
#define RGX_MMU_LOG2_PAGE_SIZE_1MB   (20)
#define RGX_MMU_LOG2_PAGE_SIZE_2MB   (21)

#define RGX_MMU_PAGE_SIZE_4KB   (   4 * 1024)
#define RGX_MMU_PAGE_SIZE_16KB  (  16 * 1024)
#define RGX_MMU_PAGE_SIZE_64KB  (  64 * 1024)
#define RGX_MMU_PAGE_SIZE_256KB ( 256 * 1024)
#define RGX_MMU_PAGE_SIZE_1MB   (1024 * 1024)
#define RGX_MMU_PAGE_SIZE_2MB   (2048 * 1024)
#define RGX_MMU_PAGE_SIZE_MIN RGX_MMU_PAGE_SIZE_4KB
#define RGX_MMU_PAGE_SIZE_MAX RGX_MMU_PAGE_SIZE_2MB

#define VAR(x) #x


static IMG_BOOL g_bDevInit2Done = IMG_FALSE;


static IMG_VOID RGX_DeInitHeaps(DEVICE_MEMORY_INFO *psDevMemoryInfo);

IMG_UINT32 g_ui32HostSampleIRQCount = 0;

#if !defined(NO_HARDWARE)

/*
	RGX LISR Handler
*/
static IMG_BOOL RGX_LISRHandler (IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32IRQStatus;
	IMG_BOOL bInterruptProcessed = IMG_FALSE;

	psDeviceNode = pvData;
	psDevConfig = psDeviceNode->psDevConfig;
	psDevInfo = psDeviceNode->pvDevice;

	if (psDevInfo->bIgnoreFurtherIRQs)
	{
		return IMG_FALSE;
	}

	ui32IRQStatus = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVIRQSTATUS);

	if (ui32IRQStatus & RGX_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_EN)
	{
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVIRQSTATUS, RGX_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_CLRMSK);
		
#if defined(RGX_FEATURE_OCPBUS)
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_OCP_IRQSTATUS_2, RGX_CR_OCP_IRQSTATUS_2_RGX_IRQ_STATUS_EN);
#endif

		if (psDevConfig->pfnInterruptHandled)
		{
			psDevConfig->pfnInterruptHandled(psDevConfig);
		}

		bInterruptProcessed = IMG_TRUE;
		
		/* Sample the current count from the FW _after_ we've cleared the interrupt. */
		g_ui32HostSampleIRQCount = psDevInfo->psRGXFWIfTraceBuf->ui32InterruptCount;

		OSScheduleMISR(psDevInfo->pvMISRData);

		if (psDevInfo->pvAPMISRData != IMG_NULL)
		{
			OSScheduleMISR(psDevInfo->pvAPMISRData);
		}
	}
	return bInterruptProcessed;
}

static IMG_VOID RGXCheckFWActivePowerState(IMG_VOID *psDevice)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = psDevice;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_TRACEBUF *psFWTraceBuf = psDevInfo->psRGXFWIfTraceBuf;
	PVRSRV_ERROR eError = PVRSRV_OK;
	
	if (psFWTraceBuf->ePowState == RGXFWIF_POW_IDLE)
	{
		/* The FW is IDLE and therefore could be shut down */
		eError = RGXActivePowerRequest(psDeviceNode);

		if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED))
		{
			PVR_DPF((PVR_DBG_WARNING,"RGXCheckFWActivePowerState: Failed RGXActivePowerRequest call (device index: %d) with %s", 
						psDeviceNode->sDevId.ui32DeviceIndex,
						PVRSRVGetErrorStringKM(eError)));
			
			PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);
		}
	}

}

static RGXFWIF_GPU_UTIL_STATS RGXGetGpuUtilStats(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO		*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_GPU_UTIL_FWCB	*psUtilFWCb = psDevInfo->psRGXFWIfGpuUtilFWCb;
	IMG_UINT32				ui32StatActiveLow = 0, ui32StatActiveHigh = 0, ui32StatBlocked = 0, ui32StatIdle = 0;
	IMG_UINT32				ui32StatCumulative = 0;
	IMG_UINT32				ui32WOffSample, ui32PrevWOffSample, ui32PriorWOffSample;
	IMG_UINT32				ui32WOffSampleSaved;
	IMG_UINT64				ui64CurrentTimer;
	IMG_UINT32				ui32Remainder;
	RGXFWIF_GPU_UTIL_STATS	sRet;
	PVRSRV_DEV_POWER_STATE  ePowerState;
//	PVRSRV_ERROR            eError;
	IMG_UINT32 				ui32Type;
	IMG_UINT32				ui32NextType;
	
	static RGXFWIF_GPU_UTIL_STATS	sPreviousStats;
    static IMG_UINT32       ui32WarningTicks = 0;

	/* init response */
	sRet.ui32GpuStatActiveHigh = 0;
	sRet.ui32GpuStatActiveLow  = 0;
	sRet.ui32GpuStatBlocked    = 0;
	sRet.ui32GpuStatIdle       = 0;
	sRet.bValid                = IMG_FALSE;
	sRet.bIncompleteData	   = IMG_FALSE;

	/* take the power lock as we might issue an OSReadHWReg64 below */
/*	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		return sRet;
	}*/

	/* write offset is incremented after writing to FWCB, so subtract 1 */
	ui32WOffSample = psUtilFWCb->ui32WriteOffset;
	if(ui32WOffSample == 0)
	{
		ui32WOffSample = RGXFWIF_GPU_UTIL_FWCB_SIZE;
	}
	ui32WOffSample--;
	ui32WOffSampleSaved = ui32PrevWOffSample = ui32PriorWOffSample = ui32WOffSample;

	PVRSRVGetDevicePowerState(psDeviceNode->sDevId.ui32DeviceIndex, &ePowerState);
	if (ePowerState != PVRSRV_DEV_POWER_STATE_ON)	/* GPU powered off */
	{
		ui64CurrentTimer = OSClockus64() & RGXFWIF_GPU_UTIL_FWCB_OS_TIMER_MASK;
		ui32NextType = RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_ON;
	}
	else											/* GPU powered on */
	{
		ui64CurrentTimer = RGXReadHWTimerReg(psDevInfo);
		ui32NextType = RGXFWIF_GPU_UTIL_FWCB_TYPE_CRTIME;
	}
	//PVRSRVPowerUnlock();

	do
	{
		IMG_UINT64	ui64FWCbEntryCurrent = psUtilFWCb->aui64CB[ui32WOffSample];

		if (ui64FWCbEntryCurrent != RGXFWIF_GPU_UTIL_FWCB_RESERVED)
		{
			IMG_UINT64 ui64Period = 0;

			ui32Type = RGXFWIF_GPU_UTIL_FWCB_ENTRY_TYPE(ui64FWCbEntryCurrent);

			switch(ui32Type)
			{
				case RGXFWIF_GPU_UTIL_FWCB_TYPE_CRTIME:
				{
					IMG_UINT32 ui32DVFSHistClock =
							psDevInfo->psGpuDVFSHistory->aui32DVFSClockCB[RGXFWIF_GPU_UTIL_FWCB_ENTRY_ID(ui64FWCbEntryCurrent)];

					if (ui32DVFSHistClock < 256)
					{
						/* DVFS frequency is 0 in DVFS history entry, which means that
								system layer doesn't define core clock frequency */
						ui32StatCumulative = 0;

						goto gpuutilstats_endloop;
					}

					/* Calculate the difference between current CR timer and CR timer at DVFS transition */
					ui64Period = ui64CurrentTimer - RGXFWIF_GPU_UTIL_FWCB_ENTRY_CR_TIMER(ui64FWCbEntryCurrent);

					/* Scale the difference to microseconds */
					ui64Period = OSDivide64((ui64Period * 1000000), (ui32DVFSHistClock / 256), &ui32Remainder);

					/* Update "now" to CR Timer of current entry */
					ui64CurrentTimer = RGXFWIF_GPU_UTIL_FWCB_ENTRY_CR_TIMER(ui64FWCbEntryCurrent);

					break;
				}

				case RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_OFF:

					/* Calculate the difference between OS timers at power-on/power-off transitions */
					ui64Period = ui64CurrentTimer - RGXFWIF_GPU_UTIL_FWCB_ENTRY_OS_TIMER(ui64FWCbEntryCurrent);

					break;

				case RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_ON:
				case RGXFWIF_GPU_UTIL_FWCB_TYPE_END_CRTIME:

					/* Update "now" to the Timer of current entry */
					if(ui32Type == RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_ON)
					{
						ui64CurrentTimer = RGXFWIF_GPU_UTIL_FWCB_ENTRY_OS_TIMER(ui64FWCbEntryCurrent);
					}
					else
					{
						ui64CurrentTimer = RGXFWIF_GPU_UTIL_FWCB_ENTRY_CR_TIMER(ui64FWCbEntryCurrent);
					}

					/* Move to next-previous state transition */
					if(ui32WOffSample == 0)
					{
						ui32WOffSample = RGXFWIF_GPU_UTIL_FWCB_SIZE;
					}
					ui32WOffSample--;

					/* Remember the next-previous entry type */
					ui32NextType = ui32Type;

					continue;

				default:
					PVR_DPF((PVR_DBG_WARNING,"RGXGetGpuUtilStats: Wrong type: %8.8X\n", ui32Type));
					break;
			}

			/* If calculated period goes beyond the time window that we want to look at to calculate stats,
				cut it down to this window */
			if (((IMG_UINT64)ui32StatCumulative + ui64Period) > (IMG_UINT64)RGXFWIF_GPU_STATS_WINDOW_SIZE_US)
			{
				ui64Period = RGXFWIF_GPU_STATS_WINDOW_SIZE_US - ui32StatCumulative;
			}
		
			/* Update cumulative time of state transition */
			ui32StatCumulative += (IMG_UINT32)ui64Period;

			/* Update per-state cumulative times */
			switch (RGXFWIF_GPU_UTIL_FWCB_ENTRY_STATE(ui64FWCbEntryCurrent))
			{
				case RGXFWIF_GPU_UTIL_FWCB_STATE_ACTIVE_LOW:
					ui32StatActiveLow += (IMG_UINT32)ui64Period;
					break;
				case RGXFWIF_GPU_UTIL_FWCB_STATE_IDLE:
					ui32StatIdle += (IMG_UINT32)ui64Period;
					break;
				case RGXFWIF_GPU_UTIL_FWCB_STATE_ACTIVE_HIGH:
					ui32StatActiveHigh += (IMG_UINT32)ui64Period;
					break;
				case RGXFWIF_GPU_UTIL_FWCB_STATE_BLOCKED:
					ui32StatBlocked += (IMG_UINT32)ui64Period;
					break;
			}
		}
		else
		{
			/* current sample is reserved */
			break;
		}

		/* Move to next-previous state transition */
		ui32PriorWOffSample = ui32PrevWOffSample;
		ui32PrevWOffSample = ui32WOffSample;
		if(ui32WOffSample == 0)
		{
			ui32WOffSample = RGXFWIF_GPU_UTIL_FWCB_SIZE;
		}
		ui32WOffSample--;

		/* Remember the next-previous entry type */
		ui32NextType = ui32Type;

		ui32WOffSampleSaved = psUtilFWCb->ui32WriteOffset;
	}
	/* break if the FW or the Host wrote something to the CB while we were reading it
	 * or if we have already calculated the whole window */
	while ((ui32WOffSample != ui32WOffSampleSaved) && 
		   (ui32PrevWOffSample != ui32WOffSampleSaved) && 
		   (ui32PriorWOffSample != ui32WOffSampleSaved) && 
		   (ui32StatCumulative < RGXFWIF_GPU_STATS_WINDOW_SIZE_US));

gpuutilstats_endloop:

	if (ui32StatCumulative)
	{
		/* Update stats */
		sRet.ui32GpuStatActiveLow	= OSDivide64(((IMG_UINT64)ui32StatActiveLow * RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE), ui32StatCumulative, &ui32Remainder);
		sRet.ui32GpuStatActiveHigh	= OSDivide64(((IMG_UINT64)ui32StatActiveHigh * RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE), ui32StatCumulative, &ui32Remainder);
		sRet.ui32GpuStatBlocked     = OSDivide64(((IMG_UINT64)ui32StatBlocked * RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE), ui32StatCumulative, &ui32Remainder);
		sRet.ui32GpuStatIdle        = OSDivide64(((IMG_UINT64)ui32StatIdle * RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE), ui32StatCumulative, &ui32Remainder);
		sRet.bValid                 = IMG_TRUE;

		if(ui32StatCumulative < RGXFWIF_GPU_STATS_WINDOW_SIZE_US)
		{
#define RGX_GPU_UTIL_STAT_MULTIPLIER 1000	/* Multiply everything to get some better accuracy when weighting last and previous values */
#define RGX_GPU_UTIL_STAT_SCALE 4			/* Give less importance to the values just computed */
#define RGX_GPU_UTIL_STAT_WARNING_PERIOD 8
			IMG_UINT32 ui32LastStatWeight = (RGX_GPU_UTIL_STAT_MULTIPLIER * ui32StatCumulative)/(RGX_GPU_UTIL_STAT_SCALE * RGXFWIF_GPU_STATS_WINDOW_SIZE_US);
			IMG_UINT32 ui32PrevStatWeight = (RGX_GPU_UTIL_STAT_MULTIPLIER - ui32LastStatWeight);

			sRet.ui32GpuStatActiveHigh  = ( (sRet.ui32GpuStatActiveHigh * ui32LastStatWeight) + (sPreviousStats.ui32GpuStatActiveHigh * ui32PrevStatWeight) ) / RGX_GPU_UTIL_STAT_MULTIPLIER;
			sRet.ui32GpuStatActiveLow   = ( (sRet.ui32GpuStatActiveLow * ui32LastStatWeight) + (sPreviousStats.ui32GpuStatActiveLow * ui32PrevStatWeight) ) / RGX_GPU_UTIL_STAT_MULTIPLIER;
			sRet.ui32GpuStatBlocked     = ( (sRet.ui32GpuStatBlocked * ui32LastStatWeight) + (sPreviousStats.ui32GpuStatBlocked * ui32PrevStatWeight) ) / RGX_GPU_UTIL_STAT_MULTIPLIER;
			sRet.ui32GpuStatIdle        = ( (sRet.ui32GpuStatIdle * ui32LastStatWeight) + (sPreviousStats.ui32GpuStatIdle * ui32PrevStatWeight) ) / RGX_GPU_UTIL_STAT_MULTIPLIER;
			sRet.bIncompleteData	    = IMG_TRUE;
            
            if((ui32WarningTicks % RGX_GPU_UTIL_STAT_WARNING_PERIOD) == 0)
            {
                PVR_DPF((PVR_DBG_WARNING,"RGXGetGpuUtilStats: Time window shorter than expected, returned data may be inaccurate\n"));
            }
            ui32WarningTicks++;
		}

		sPreviousStats.ui32GpuStatActiveLow  = sRet.ui32GpuStatActiveLow;
		sPreviousStats.ui32GpuStatActiveHigh = sRet.ui32GpuStatActiveHigh;
		sPreviousStats.ui32GpuStatBlocked    = sRet.ui32GpuStatBlocked;
		sPreviousStats.ui32GpuStatIdle       = sRet.ui32GpuStatIdle;
	}

	return sRet;
}

/*
	RGX MISR Handler
*/
static IMG_VOID RGX_MISRHandler (IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = pvData;

	/* Inform other services devices that we have finished an operation */
	PVRSRVCheckStatus(psDeviceNode);

	/* Give the HWPerf service a chance to transfer some data from the FW
	 * buffer to the host driver transport layer buffer.
	 */
	RGXHWPerfDataStoreCB(psDeviceNode);

	/* Process all firmware CCBs for pending commands */
	RGXCheckFirmwareCCBs(psDeviceNode->pvDevice);
}
#endif


/*
 * PVRSRVRGXInitDevPart2KM
 */ 
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitDevPart2KM (PVRSRV_DEVICE_NODE	*psDeviceNode,
									  RGX_INIT_COMMAND		*psInitScript,
									  RGX_INIT_COMMAND		*psDbgScript,
									  RGX_INIT_COMMAND		*psDbgBusScript,
									  RGX_INIT_COMMAND		*psDeinitScript,
									  IMG_UINT32			ui32KernelCatBaseIdReg,
									  IMG_UINT32			ui32KernelCatBaseId,
									  IMG_UINT32			ui32KernelCatBaseReg,
									  IMG_UINT32			ui32KernelCatBaseWordSize,
									  IMG_UINT32			ui32KernelCatBaseAlignShift,
									  IMG_UINT32			ui32KernelCatBaseShift,
									  IMG_UINT64			ui64KernelCatBaseMask,
									  IMG_UINT32			ui32DeviceFlags,
									  RGX_ACTIVEPM_CONF		eActivePMConf,
								 	  DEVMEM_EXPORTCOOKIE	*psFWCodeAllocServerExportCookie,
								 	  DEVMEM_EXPORTCOOKIE	*psFWDataAllocServerExportCookie,
								 	  DEVMEM_EXPORTCOOKIE	*psFWCorememAllocServerExportCookie)
{
	PVRSRV_ERROR			eError;
	PVRSRV_RGXDEV_INFO		*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_DEV_POWER_STATE	eDefaultPowerState;
	PVRSRV_DEVICE_CONFIG	*psDevConfig = psDeviceNode->psDevConfig;

	PDUMPCOMMENT("RGX Initialisation Part 2");

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
    psDevInfo->bEnableProcessStats=IMG_TRUE;
#else
    psDevInfo->bEnableProcessStats=IMG_FALSE;
#endif

	psDevInfo->ui32KernelCatBaseIdReg = ui32KernelCatBaseIdReg;
	psDevInfo->ui32KernelCatBaseId = ui32KernelCatBaseId;
	psDevInfo->ui32KernelCatBaseReg = ui32KernelCatBaseReg;
	psDevInfo->ui32KernelCatBaseAlignShift = ui32KernelCatBaseAlignShift;
	psDevInfo->ui32KernelCatBaseShift = ui32KernelCatBaseShift;
	psDevInfo->ui32KernelCatBaseWordSize = ui32KernelCatBaseWordSize;
	psDevInfo->ui64KernelCatBaseMask = ui64KernelCatBaseMask;

	/*
	 * Map RGX Registers
	 */
#if !defined(NO_HARDWARE)
	psDevInfo->pvRegsBaseKM = OSMapPhysToLin(psDevConfig->sRegsCpuPBase,
										     psDevConfig->ui32RegsSize,
										     0);

	if (psDevInfo->pvRegsBaseKM == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitDevPart2KM: Failed to create RGX register mapping\n"));
		return PVRSRV_ERROR_BAD_MAPPING;
	}
#else
	psDevInfo->pvRegsBaseKM = IMG_NULL;
#endif /* !NO_HARDWARE */

	/* free the export cookies provided to srvinit */
	DevmemUnexport(psDevInfo->psRGXFWCodeMemDesc, psFWCodeAllocServerExportCookie);
	DevmemUnexport(psDevInfo->psRGXFWDataMemDesc, psFWDataAllocServerExportCookie);
	if (DevmemIsValidExportCookie(psFWCorememAllocServerExportCookie))
	{
		DevmemUnexport(psDevInfo->psRGXFWCorememMemDesc, psFWCorememAllocServerExportCookie);
	}

	/*
	 * Copy scripts
	 */
	OSMemCopy(psDevInfo->psScripts->asInitCommands, psInitScript,
			  RGX_MAX_INIT_COMMANDS * sizeof(*psInitScript));

	OSMemCopy(psDevInfo->psScripts->asDbgCommands, psDbgScript,
			  RGX_MAX_INIT_COMMANDS * sizeof(*psDbgScript));

	OSMemCopy(psDevInfo->psScripts->asDbgBusCommands, psDbgBusScript,
			  RGX_MAX_DBGBUS_COMMANDS * sizeof(*psDbgBusScript));

	OSMemCopy(psDevInfo->psScripts->asDeinitCommands, psDeinitScript,
			  RGX_MAX_DEINIT_COMMANDS * sizeof(*psDeinitScript));

#if defined(PDUMP)
	/* Run the deinit script to feed the last-frame deinit buffer */
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_DEINIT, "RGX deinitialisation script");
	RGXRunScript(psDevInfo, psDevInfo->psScripts->asDeinitCommands, RGX_MAX_DEINIT_COMMANDS, PDUMP_FLAGS_DEINIT | PDUMP_FLAGS_NOHW, IMG_NULL);
#endif


	psDevInfo->ui32RegSize = psDevConfig->ui32RegsSize;
	psDevInfo->sRegsPhysBase = psDevConfig->sRegsCpuPBase;

	/* Initialise Device Flags */
	psDevInfo->ui32DeviceFlags = 0;
	if (ui32DeviceFlags & RGXKMIF_DEVICE_STATE_ZERO_FREELIST)
	{
		psDevInfo->ui32DeviceFlags |= RGXKM_DEVICE_STATE_ZERO_FREELIST;
	}

	if (ui32DeviceFlags & RGXKMIF_DEVICE_STATE_DISABLE_DW_LOGGING_EN)
	{
		psDevInfo->ui32DeviceFlags |= RGXKM_DEVICE_STATE_DISABLE_DW_LOGGING_EN;
	}

	#if defined(SUPPORT_GPUTRACE_EVENTS)
	/* If built, always setup FTrace consumer thread. */
	RGXHWPerfFTraceGPUInit(psDeviceNode->pvDevice);

	RGXHWPerfFTraceGPUEventsEnabledSet((ui32DeviceFlags & RGXKMIF_DEVICE_STATE_FTRACE_EN) ? IMG_TRUE: IMG_FALSE);
#endif

	/* Initialise lists of ZSBuffers */
	eError = OSLockCreate(&psDevInfo->hLockZSBuffer,LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);
	dllist_init(&psDevInfo->sZSBufferHead);
	psDevInfo->ui32ZSBufferCurrID = 1;

	/* Initialise lists of growable Freelists */
	eError = OSLockCreate(&psDevInfo->hLockFreeList,LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);
	dllist_init(&psDevInfo->sFreeListHead);
	psDevInfo->ui32FreelistCurrID = 1;

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	eError = OSLockCreate(&psDevInfo->hDebugFaultInfoLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = OSLockCreate(&psDevInfo->hMMUCtxUnregLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	/* Allocate DVFS History */
	psDevInfo->psGpuDVFSHistory = OSAllocZMem(sizeof(*(psDevInfo->psGpuDVFSHistory)));
	if (psDevInfo->psGpuDVFSHistory == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitDevPart2KM: failed to allocate gpu dvfs history storage"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Reset DVFS history */
	psDevInfo->psGpuDVFSHistory->ui32CurrentDVFSId = 0;
	psDevInfo->psGpuDVFSHistory->aui32DVFSClockCB[0] = 0;

	/* Setup GPU Utilization stat update callback */
#if !defined(NO_HARDWARE)
	psDevInfo->pfnGetGpuUtilStats = RGXGetGpuUtilStats;
#endif

	eDefaultPowerState = PVRSRV_DEV_POWER_STATE_ON;

	/* set-up the Active Power Mgmt callback */
#if !defined(NO_HARDWARE)
	{
		RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;
		IMG_BOOL bSysEnableAPM = psRGXData->psRGXTimingInfo->bEnableActivePM;
		IMG_BOOL bEnableAPM = ((eActivePMConf == RGX_ACTIVEPM_DEFAULT) && bSysEnableAPM) ||
							   (eActivePMConf == RGX_ACTIVEPM_FORCE_ON);

		if (bEnableAPM)
		{
			eError = OSInstallMISR(&psDevInfo->pvAPMISRData, RGXCheckFWActivePowerState, psDeviceNode);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}

			/* Prevent the device being woken up before there is something to do. */
			eDefaultPowerState = PVRSRV_DEV_POWER_STATE_OFF;
		}
	}
#endif

	/* Register the device with the power manager. */
	eError = PVRSRVRegisterPowerDevice (psDeviceNode->sDevId.ui32DeviceIndex,
										&RGXPrePowerState, &RGXPostPowerState,
										psDevConfig->pfnPrePowerState, psDevConfig->pfnPostPowerState,
										&RGXPreClockSpeedChange, &RGXPostClockSpeedChange,
										(IMG_HANDLE)psDeviceNode,
										PVRSRV_DEV_POWER_STATE_OFF,
										eDefaultPowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitDevPart2KM: failed to register device with power manager"));
		return eError;
	}

#if !defined(NO_HARDWARE)
	eError = RGXInstallProcessQueuesMISR(&psDevInfo->hProcessQueuesMISR, psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		if (psDevInfo->pvAPMISRData != IMG_NULL)
		{
			(IMG_VOID) OSUninstallMISR(psDevInfo->pvAPMISRData);
		}
		return eError;
	}

	/* Register the interrupt handlers */
	eError = OSInstallMISR(&psDevInfo->pvMISRData,
									RGX_MISRHandler, psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		if (psDevInfo->pvAPMISRData != IMG_NULL)
		{
			(IMG_VOID) OSUninstallMISR(psDevInfo->pvAPMISRData);
		}
		(IMG_VOID) OSUninstallMISR(psDevInfo->hProcessQueuesMISR);
		return eError;
	}

	eError = OSInstallDeviceLISR(psDevConfig, &psDevInfo->pvLISRData,
								 RGX_LISRHandler, psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		if (psDevInfo->pvAPMISRData != IMG_NULL)
		{
			(IMG_VOID) OSUninstallMISR(psDevInfo->pvAPMISRData);
		}
		(IMG_VOID) OSUninstallMISR(psDevInfo->hProcessQueuesMISR);
		(IMG_VOID) OSUninstallMISR(psDevInfo->pvMISRData);
		return eError;
	}

#endif
	g_bDevInit2Done = IMG_TRUE;

	return PVRSRV_OK;
}

static
PVRSRV_ERROR RGXAllocateFWCodeRegion(PVRSRV_DEVICE_NODE	*psDeviceNode,
                                     IMG_DEVMEM_SIZE_T ui32FWCodeAllocSize,
                                     IMG_UINT32 uiMemAllocFlags)
{
 	PVRSRV_ERROR eError;

#if ! defined(TDMETACODE)
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;

	uiMemAllocFlags |= PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
	                   PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate and export code memory for fw");

	eError = DevmemFwAllocateExportable(psDeviceNode,
										ui32FWCodeAllocSize,
										uiMemAllocFlags,
										"FirmwareCodeRegion",
	                                    &psDevInfo->psRGXFWCodeMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevmemFwAllocateExportable failed (%u)",
				eError));
	}

	return eError;

#else
	PMR *psTDMetaCodePMR;
	IMG_DEVMEM_SIZE_T uiMemDescSize;
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;

	PDUMPCOMMENT("Allocate TD META code memory for fw");

	eError = PhysmemNewTDMetaCodePMR(psDeviceNode,
	                                 ui32FWCodeAllocSize,
	                                 12,
	                                 uiMemAllocFlags,
	                                 &psTDMetaCodePMR);
	if(eError != PVRSRV_OK)
	{
		goto PMRCreateError;
	}

	PDUMPCOMMENT("Import TD META code memory for fw");

	/* NB: psTDMetaCodePMR refcount: 1 -> 2 */
	eError = DevmemLocalImport(IMG_NULL, /* bridge handle not applicable here */
	                           psTDMetaCodePMR,
	                           uiMemAllocFlags,
	                           &psDevInfo->psRGXFWCodeMemDesc,
	                           &uiMemDescSize);
	if(eError != PVRSRV_OK)
	{
		goto ImportError;
	}

	eError = DevmemMapToDevice(psDevInfo->psRGXFWCodeMemDesc,
							   psDevInfo->psFirmwareHeap,
							   &sTmpDevVAddr);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to map TD META code PMR (%u)", eError));
		goto MapError;
	}

	/* Caution, oddball code follows:
	   When doing the DevmemLocalImport above, we wrap the PMR in a memdesc and increment
	   the PMR's refcount. We would like to implicitly say now, that memdesc is our
	   tracking mechanism for the PMR, and no longer the original pointer to it. The call
	   to PMRUnimportPMR below does that. For reasons explained below, this is only done
	   if this function will return successfully.

	   NB: i.e., psTDMetaCodePMR refcount: 2 -> 1
	*/
	PMRUnimportPMR(psTDMetaCodePMR);

	return eError;

MapError:
	DevmemFree(psDevInfo->psRGXFWCodeMemDesc);

ImportError:
	/* This is done even after the DevmemFree above because as a result of the PMRUnimportPMR
	   at the end of the function never getting hit on an error condition, the PMR must be
	   unreferenced "again" as part of the cleanup */
	PMRUnimportPMR(psTDMetaCodePMR);

PMRCreateError:

	return eError;
#endif
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitAllocFWImgMemKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
										  IMG_DEVMEM_SIZE_T 	uiFWCodeLen,
									 	  IMG_DEVMEM_SIZE_T 	uiFWDataLen,
									 	  IMG_DEVMEM_SIZE_T 	uiFWCorememLen,
									 	  DEVMEM_EXPORTCOOKIE	**ppsFWCodeAllocServerExportCookie,
									 	  IMG_DEV_VIRTADDR		*psFWCodeDevVAddrBase,
									 	  DEVMEM_EXPORTCOOKIE	**ppsFWDataAllocServerExportCookie,
									 	  IMG_DEV_VIRTADDR		*psFWDataDevVAddrBase,
									 	  DEVMEM_EXPORTCOOKIE	**ppsFWCorememAllocServerExportCookie,
									 	  IMG_DEV_VIRTADDR		*psFWCorememDevVAddrBase,
										  RGXFWIF_DEV_VIRTADDR	*psFWCorememMetaVAddrBase)
{
	DEVMEM_FLAGS_T		uiMemAllocFlags;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR        eError;

	/* set up memory contexts */

	/* Register callbacks for creation of device memory contexts */
	psDeviceNode->pfnRegisterMemoryContext = RGXRegisterMemoryContext;
	psDeviceNode->pfnUnregisterMemoryContext = RGXUnregisterMemoryContext;

	/* Create the memory context for the firmware. */
	eError = DevmemCreateContext(IMG_NULL, psDeviceNode,
								 DEVMEM_HEAPCFG_META,
								 &psDevInfo->psKernelDevmemCtx);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitAllocFWImgMemKM: Failed DevmemCreateContext (%u)", eError));
		goto failed_to_create_ctx;
	}
	
	eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx,
								  "Firmware", 
								  &psDevInfo->psFirmwareHeap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitAllocFWImgMemKM: Failed DevmemFindHeapByName (%u)", eError));
		goto failed_to_find_heap;
	}

	/* 
	 * Set up Allocation for FW code section 
	 */
	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
	                  PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
	                  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
	                  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                  PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                  PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
	                  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE;


	eError = RGXAllocateFWCodeRegion(psDeviceNode,
                                     uiFWCodeLen,
	                                 uiMemAllocFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to allocate fw code mem (%u)",
				eError));
		goto failFWCodeMemDescAlloc;
	}

	eError = DevmemExport(psDevInfo->psRGXFWCodeMemDesc,
	                      &psDevInfo->sRGXFWCodeExportCookie);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to export fw code mem (%u)",
				eError));
		goto failFWCodeMemDescExport;
	}

	eError = DevmemAcquireDevVirtAddr(psDevInfo->psRGXFWCodeMemDesc,
	                                  psFWCodeDevVAddrBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to acquire devVAddr for fw code mem (%u)",
				eError));
		goto failFWCodeMemDescAqDevVirt;
	}

	/*
	* The FW code must be the first allocation in the firmware heap, otherwise
	* the bootloader will not work (META will not be able to find the bootloader).
	*/
	PVR_ASSERT(psFWCodeDevVAddrBase->uiAddr == RGX_FIRMWARE_HEAP_BASE);

	/* 
	 * Set up Allocation for FW data section 
	 */
	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
	                  PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
	                  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
	                  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                  PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                  PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
	                  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
	                  PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
	                  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate and export data memory for fw");

	eError = DevmemFwAllocateExportable(psDeviceNode,
										uiFWDataLen,
										uiMemAllocFlags,
										"FirmwareDataRegion",
	                                    &psDevInfo->psRGXFWDataMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to allocate fw data mem (%u)",
				eError));
		goto failFWDataMemDescAlloc;
	}

	eError = DevmemExport(psDevInfo->psRGXFWDataMemDesc,
	                      &psDevInfo->sRGXFWDataExportCookie);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to export fw data mem (%u)",
				eError));
		goto failFWDataMemDescExport;
	}

	eError = DevmemAcquireDevVirtAddr(psDevInfo->psRGXFWDataMemDesc,
	                                  psFWDataDevVAddrBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to acquire devVAddr for fw data mem (%u)",
				eError));
		goto failFWDataMemDescAqDevVirt;
	}

	if (uiFWCorememLen != 0)
	{
		/* 
		 * Set up Allocation for FW coremem section 
		 */
		uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
		                  PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) |
			PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
			PVRSRV_MEMALLOCFLAG_CPU_READABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
			PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
			PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

		PDUMPCOMMENT("Allocate and export coremem memory for fw");

		eError = DevmemFwAllocateExportable(psDeviceNode,
				uiFWCorememLen,
				uiMemAllocFlags,
				"FirmwareCorememRegion",
				&psDevInfo->psRGXFWCorememMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Failed to allocate fw coremem mem, size: %lld, flags: %x (%u)",
						uiFWCorememLen, uiMemAllocFlags, eError));
			goto failFWCorememMemDescAlloc;
		}

		eError = DevmemExport(psDevInfo->psRGXFWCorememMemDesc,
				&psDevInfo->sRGXFWCorememExportCookie);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Failed to export fw coremem mem (%u)",
						eError));
			goto failFWCorememMemDescExport;
		}

		eError = DevmemAcquireDevVirtAddr(psDevInfo->psRGXFWCorememMemDesc,
				psFWCorememDevVAddrBase);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Failed to acquire devVAddr for fw coremem mem (%u)",
						eError));
			goto failFWCorememMemDescAqDevVirt;
		}

		RGXSetFirmwareAddress(psFWCorememMetaVAddrBase,
				psDevInfo->psRGXFWCorememMemDesc,
				0, RFW_FWADDR_NOREF_FLAG);

#if defined(HW_ERN_45914)
		/* temporarily make sure the coremem is init using the SLC */
		psFWCorememMetaVAddrBase->ui32Addr &= ~RGXFW_SEGMMU_DMAP_ADDR_START;
		psFWCorememMetaVAddrBase->ui32Addr |= RGXFW_BOOTLDR_META_ADDR;
#endif

	}

	*ppsFWCodeAllocServerExportCookie = &psDevInfo->sRGXFWCodeExportCookie;
	*ppsFWDataAllocServerExportCookie = &psDevInfo->sRGXFWDataExportCookie;
	/* Set all output arguments to ensure safe use in Part2 initialisation */
	*ppsFWCorememAllocServerExportCookie = &psDevInfo->sRGXFWCorememExportCookie;

	return PVRSRV_OK;


failFWCorememMemDescAqDevVirt:

	if (uiFWCorememLen != 0)
	{
		DevmemUnexport(psDevInfo->psRGXFWCorememMemDesc, &psDevInfo->sRGXFWCorememExportCookie);
	}
failFWCorememMemDescExport:

	if (uiFWCorememLen != 0)
	{
		DevmemFwFree(psDevInfo->psRGXFWCorememMemDesc);
	}
failFWCorememMemDescAlloc:

	DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWDataMemDesc);
failFWDataMemDescAqDevVirt:

	DevmemUnexport(psDevInfo->psRGXFWDataMemDesc, &psDevInfo->sRGXFWDataExportCookie);
failFWDataMemDescExport:

	DevmemFwFree(psDevInfo->psRGXFWDataMemDesc);
failFWDataMemDescAlloc:

	DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWCodeMemDesc);
failFWCodeMemDescAqDevVirt:

	DevmemUnexport(psDevInfo->psRGXFWCodeMemDesc, &psDevInfo->sRGXFWCodeExportCookie);
failFWCodeMemDescExport:

	DevmemFwFree(psDevInfo->psRGXFWCodeMemDesc);
failFWCodeMemDescAlloc:

failed_to_find_heap:
	/*
	 * Clear the mem context create callbacks before destroying the RGX firmware
	 * context to avoid a spurious callback.
	 */
	psDeviceNode->pfnRegisterMemoryContext = IMG_NULL;
	psDeviceNode->pfnUnregisterMemoryContext = IMG_NULL;
	DevmemDestroyContext(psDevInfo->psKernelDevmemCtx);
failed_to_create_ctx:

	return eError;
}

/*
 * PVRSRVRGXInitFirmwareKM
 */ 
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitFirmwareKM(PVRSRV_DEVICE_NODE			*psDeviceNode, 
									    RGXFWIF_DEV_VIRTADDR		*psRGXFwInit,
									    IMG_BOOL					bEnableSignatureChecks,
									    IMG_UINT32					ui32SignatureChecksBufSize,
									    IMG_UINT32					ui32HWPerfFWBufSizeKB,
									    IMG_UINT64					ui64HWPerfFilter,
									    IMG_UINT32					ui32RGXFWAlignChecksSize,
									    IMG_UINT32					*pui32RGXFWAlignChecks,
									    IMG_UINT32					ui32ConfigFlags,
									    IMG_UINT32					ui32LogType,
									    IMG_UINT32					ui32FilterFlags,
									    RGXFWIF_COMPCHECKS_BVNC     *psClientBVNC)
{
	PVRSRV_ERROR				eError;
	RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(sBVNC);
	IMG_BOOL bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV;
	IMG_UINT32 ui32NumBIFTilingConfigs, *pui32BIFTilingXStrides, i;


	/* Check if BVNC numbers of client and driver are compatible */
	rgx_bvnc_packed(&sBVNC.ui32BNC, sBVNC.aszV, sBVNC.ui32VLenMax, RGX_BVNC_KM_B, RGX_BVNC_KM_V_ST, RGX_BVNC_KM_N, RGX_BVNC_KM_C);

	RGX_BVNC_EQUAL(sBVNC, *psClientBVNC, bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV);
	
	if (!bCompatibleAll)
	{
		if (!bCompatibleVersion)
		{
			PVR_LOG(("(FAIL) %s: Incompatible compatibility struct version of driver (%d) and client (%d).",
					__FUNCTION__, 
					sBVNC.ui32LayoutVersion, 
					psClientBVNC->ui32LayoutVersion));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			PVR_DBG_BREAK;
			goto failed_to_pass_compatibility_check;
		}

		if (!bCompatibleLenMax)
		{
			PVR_LOG(("(FAIL) %s: Incompatible V maxlen of driver (%d) and client (%d).",
					__FUNCTION__, 
					sBVNC.ui32VLenMax, 
					psClientBVNC->ui32VLenMax));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			PVR_DBG_BREAK;
			goto failed_to_pass_compatibility_check;
		}

		if (!bCompatibleBNC)
		{
			PVR_LOG(("(FAIL) %s: Incompatible driver BNC (%d._.%d.%d) / client BNC (%d._.%d.%d).",
					__FUNCTION__, 
					RGX_BVNC_PACKED_EXTR_B(sBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sBVNC), 
					RGX_BVNC_PACKED_EXTR_B(*psClientBVNC), 
					RGX_BVNC_PACKED_EXTR_N(*psClientBVNC), 
					RGX_BVNC_PACKED_EXTR_C(*psClientBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			PVR_DBG_BREAK;
			goto failed_to_pass_compatibility_check;
		}
		
		if (!bCompatibleV)
		{
			PVR_LOG(("(FAIL) %s: Incompatible driver BVNC (%d.%s.%d.%d) / client BVNC (%d.%s.%d.%d).",
					__FUNCTION__, 
					RGX_BVNC_PACKED_EXTR_B(sBVNC), 
					RGX_BVNC_PACKED_EXTR_V(sBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sBVNC), 
					RGX_BVNC_PACKED_EXTR_B(*psClientBVNC), 
					RGX_BVNC_PACKED_EXTR_V(*psClientBVNC), 
					RGX_BVNC_PACKED_EXTR_N(*psClientBVNC), 
					RGX_BVNC_PACKED_EXTR_C(*psClientBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			PVR_DBG_BREAK;
			goto failed_to_pass_compatibility_check;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver BVNC (%d.%s.%d.%d) and client BVNC (%d.%s.%d.%d) match. [ OK ]",
				__FUNCTION__, 
				RGX_BVNC_PACKED_EXTR_B(sBVNC), 
				RGX_BVNC_PACKED_EXTR_V(sBVNC), 
				RGX_BVNC_PACKED_EXTR_N(sBVNC), 
				RGX_BVNC_PACKED_EXTR_C(sBVNC), 
				RGX_BVNC_PACKED_EXTR_B(*psClientBVNC), 
				RGX_BVNC_PACKED_EXTR_V(*psClientBVNC), 
				RGX_BVNC_PACKED_EXTR_N(*psClientBVNC), 
				RGX_BVNC_PACKED_EXTR_C(*psClientBVNC)));
	}

	GetNumBifTilingHeapConfigs(&ui32NumBIFTilingConfigs);
	pui32BIFTilingXStrides = OSAllocMem(sizeof(IMG_UINT32) * ui32NumBIFTilingConfigs);
	if(pui32BIFTilingXStrides == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitFirmwareKM: OSAllocMem failed (%u)", eError));
		goto failed_BIF_tiling_alloc;
	}
	for(i = 0; i < ui32NumBIFTilingConfigs; i++)
	{
		eError = GetBIFTilingHeapXStride(i+1, &pui32BIFTilingXStrides[i]);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitFirmwareKM: GetBIFTilingHeapXStride for heap %u failed (%u)",
			         i + 1, eError));
			goto failed_BIF_heap_init;
		}
	}

	eError = RGXSetupFirmware(psDeviceNode, 
							     bEnableSignatureChecks, 
							     ui32SignatureChecksBufSize,
							     ui32HWPerfFWBufSizeKB,
							     ui64HWPerfFilter,
							     ui32RGXFWAlignChecksSize,
							     pui32RGXFWAlignChecks,
							     ui32ConfigFlags,
							     ui32LogType,
							     ui32NumBIFTilingConfigs,
							     pui32BIFTilingXStrides,
							     ui32FilterFlags,
							     psRGXFwInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXInitFirmwareKM: RGXSetupFirmware failed (%u)", eError));
		goto failed_init_firmware;
	}

	OSFreeMem(pui32BIFTilingXStrides);
	return PVRSRV_OK;

failed_init_firmware:
failed_BIF_heap_init:
	OSFreeMem(pui32BIFTilingXStrides);
failed_BIF_tiling_alloc:
failed_to_pass_compatibility_check:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}




/* See device.h for function declaration */
static PVRSRV_ERROR RGXAllocUFOBlock(PVRSRV_DEVICE_NODE *psDeviceNode,
									 DEVMEM_MEMDESC **psMemDesc,
									 IMG_UINT32 *puiSyncPrimVAddr,
									 IMG_UINT32 *puiSyncPrimBlockSize)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR eError;
	RGXFWIF_DEV_VIRTADDR pFirmwareAddr;
	IMG_DEVMEM_SIZE_T uiUFOBlockSize = sizeof(IMG_UINT32);
	IMG_DEVMEM_ALIGN_T ui32UFOBlockAlign = sizeof(IMG_UINT32);

	psDevInfo = psDeviceNode->pvDevice;

	/* Size and align are 'expanded' because we request an Exportalign allocation */
	DevmemExportalignAdjustSizeAndAlign(psDevInfo->psFirmwareHeap,
										&uiUFOBlockSize,
										&ui32UFOBlockAlign);

	eError = DevmemFwAllocateExportable(psDeviceNode,
										uiUFOBlockSize,
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
										PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
										PVRSRV_MEMALLOCFLAG_CACHE_COHERENT | 
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_CPU_READABLE |
										PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE,
										"UFOBlock",
										psMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	DevmemPDumpLoadMem(*psMemDesc,
					   0,
					   uiUFOBlockSize,
					   PDUMP_FLAGS_CONTINUOUS);

	RGXSetFirmwareAddress(&pFirmwareAddr, *psMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	*puiSyncPrimVAddr = pFirmwareAddr.ui32Addr;
	*puiSyncPrimBlockSize = TRUNCATE_64BITS_TO_32BITS(uiUFOBlockSize);

	return PVRSRV_OK;

	DevmemFwFree(*psMemDesc);
e0:
	return eError;
}

/* See device.h for function declaration */
static IMG_VOID RGXFreeUFOBlock(PVRSRV_DEVICE_NODE *psDeviceNode,
								DEVMEM_MEMDESC *psMemDesc)
{
	/*
		If the system has snooping of the device cache then the UFO block
		might be in the cache so we need to flush it out before freeing
		the memory
	*/
	if (PVRSRVSystemSnoopingOfDeviceCache())
	{
		RGXFWIF_KCCB_CMD sFlushInvalCmd;
		PVRSRV_ERROR eError;

		/* Schedule the SLC flush command ... */
#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Submit SLC flush and invalidate");
#endif
		sFlushInvalCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCFLUSHINVAL;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bInval = IMG_TRUE;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.eDM = RGXFWIF_DM_2D; //Covers all of Sidekick
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.psContext.ui32Addr = 0;
		
		eError = RGXSendCommandWithPowLock(psDeviceNode->pvDevice,
											RGXFWIF_DM_GP,
											&sFlushInvalCmd,
											sizeof(sFlushInvalCmd),
											IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXFreeUFOBlock: Failed to schedule SLC flush command with error (%u)", eError));
		}
		else
		{
			/* Wait for the SLC flush to complete */
			eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, IMG_TRUE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXFreeUFOBlock: SLC flush and invalidate aborted with error (%u)", eError));
			}
		}
	}

	RGXUnsetFirmwareAddress(psMemDesc);
	DevmemFwFree(psMemDesc);
}

/*
	DevDeInitRGX
*/
PVRSRV_ERROR DevDeInitRGX (PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO			*psDevInfo = (PVRSRV_RGXDEV_INFO*)psDeviceNode->pvDevice;
	PVRSRV_ERROR				eError;
	DEVICE_MEMORY_INFO		    *psDevMemoryInfo;

	if (!psDevInfo)
	{
		/* Can happen if DevInitRGX failed */
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: Null DevInfo"));
		return PVRSRV_OK;
	}

	/* Unregister debug request notifiers first as they could depend on anything. */
	PVRSRVUnregisterDbgRequestNotify(psDeviceNode->hDbgReqNotify);

	/* Cancel notifications to this device */
	PVRSRVUnregisterCmdCompleteNotify(psDeviceNode->hCmdCompNotify);
	psDeviceNode->hCmdCompNotify = IMG_NULL;

	/*
	 *  De-initialise in reverse order, so stage 2 init is undone first.
	 */
	if (g_bDevInit2Done)
	{
		g_bDevInit2Done = IMG_FALSE;

#if !defined(NO_HARDWARE)
		(IMG_VOID) OSUninstallDeviceLISR(psDevInfo->pvLISRData);
		(IMG_VOID) OSUninstallMISR(psDevInfo->pvMISRData);
		(IMG_VOID) OSUninstallMISR(psDevInfo->hProcessQueuesMISR);
		if (psDevInfo->pvAPMISRData != IMG_NULL)
		{
			(IMG_VOID) OSUninstallMISR(psDevInfo->pvAPMISRData);
		}
#endif /* !NO_HARDWARE */

		/* Remove the device from the power manager */
		eError = PVRSRVRemovePowerDevice(psDeviceNode->sDevId.ui32DeviceIndex);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		/* Free DVFS History */
		if (psDevInfo->psGpuDVFSHistory != IMG_NULL)
		{
			OSFreeMem(psDevInfo->psGpuDVFSHistory);
			psDevInfo->psGpuDVFSHistory = IMG_NULL;
		}

		/* De-init Freelists/ZBuffers... */
		OSLockDestroy(psDevInfo->hLockFreeList);
		OSLockDestroy(psDevInfo->hLockZSBuffer);

		/* De-init HWPerf Ftrace thread resources for the RGX device */
#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUDeInit(psDevInfo);
#endif

		/* Unregister MMU related stuff */
		eError = RGXMMUInit_Unregister(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: Failed RGXMMUInit_Unregister (0x%x)", eError));
			return eError;
		}

		/* UnMap Regs */
		if (psDevInfo->pvRegsBaseKM != IMG_NULL)
		{
#if !defined(NO_HARDWARE)
			OSUnMapPhysToLin(psDevInfo->pvRegsBaseKM,
							 psDevInfo->ui32RegSize,
							 0);
#endif /* !NO_HARDWARE */
			psDevInfo->pvRegsBaseKM = IMG_NULL;
		}
	}

#if 0 /* not required at this time */
	if (psDevInfo->hTimer)
	{
		eError = OSRemoveTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: Failed to remove timer"));
			return 	eError;
		}
		psDevInfo->hTimer = IMG_NULL;
	}
#endif

    psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;

	RGX_DeInitHeaps(psDevMemoryInfo);

	if (DevmemIsValidExportCookie(&psDevInfo->sRGXFWCodeExportCookie))
	{
		/* if the export cookie is valid, the init sequence failed */
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: FW Code Export cookie still valid (should have been unexported at init time)"));
		DevmemUnexport(psDevInfo->psRGXFWCodeMemDesc, &psDevInfo->sRGXFWCodeExportCookie);
	}

	if (DevmemIsValidExportCookie(&psDevInfo->sRGXFWDataExportCookie))
	{
		/* if the export cookie is valid, the init sequence failed */
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: FW Data Export cookie still valid (should have been unexported at init time)"));
		DevmemUnexport(psDevInfo->psRGXFWDataMemDesc, &psDevInfo->sRGXFWDataExportCookie);
	}

	if (DevmemIsValidExportCookie(&psDevInfo->sRGXFWCorememExportCookie))
	{
		/* if the export cookie is valid, the init sequence failed */
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitRGX: FW Coremem Export cookie still valid (should have been unexported at init time)"));
		DevmemUnexport(psDevInfo->psRGXFWCorememMemDesc, &psDevInfo->sRGXFWCorememExportCookie);
	}

	/*
	   Free the firmware allocations.
	 */
	RGXFreeFirmware(psDevInfo);

	/*
	 * Clear the mem context create callbacks before destroying the RGX firmware
	 * context to avoid a spurious callback.
	 */
	psDeviceNode->pfnRegisterMemoryContext = IMG_NULL;
	psDeviceNode->pfnUnregisterMemoryContext = IMG_NULL;

	if (psDevInfo->psKernelDevmemCtx)
	{
		eError = DevmemDestroyContext(psDevInfo->psKernelDevmemCtx);
		
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	/* destroy the context list locks */
	OSWRLockDestroy(psDevInfo->hRenderCtxListLock);
	OSWRLockDestroy(psDevInfo->hComputeCtxListLock);
	OSWRLockDestroy(psDevInfo->hTransferCtxListLock);
	OSWRLockDestroy(psDevInfo->hRaytraceCtxListLock);
	OSWRLockDestroy(psDevInfo->hMemoryCtxListLock);

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	OSLockDestroy(psDevInfo->hDebugFaultInfoLock);
	OSLockDestroy(psDevInfo->hMMUCtxUnregLock);
#endif

	/* Free the init scripts. */
	OSFreeMem(psDevInfo->psScripts);

	/* DeAllocate devinfo */
	OSFreeMem(psDevInfo);

	psDeviceNode->pvDevice = IMG_NULL;

	return PVRSRV_OK;
}

/*!
******************************************************************************
 
 @Function	RGXDebugRequestNotify

 @Description Dump the debug data for RGX
  
******************************************************************************/
static IMG_VOID RGXDebugRequestNotify(PVRSRV_DBGREQ_HANDLE hDbgReqestHandle, IMG_UINT32 ui32VerbLevel)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = hDbgReqestHandle;

	/* Only action the request if we've fully init'ed */
	if (g_bDevInit2Done)
	{
		RGXDebugRequestProcess(g_pfnDumpDebugPrintf, psDeviceNode->pvDevice, ui32VerbLevel);
	}
}

#if defined(PDUMP)
static
PVRSRV_ERROR RGXResetPDump(PVRSRV_DEVICE_NODE *psDeviceNode)
{
 	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)(psDeviceNode->pvDevice);
	IMG_UINT32			ui32Idx;

	for (ui32Idx = 0; ui32Idx < RGXFWIF_DM_MAX; ui32Idx++)
	{
		psDevInfo->abDumpedKCCBCtlAlready[ui32Idx] = IMG_FALSE;
	}


	return PVRSRV_OK;
}
#endif /* PDUMP */


static PVRSRV_ERROR RGX_InitHeaps(DEVICE_MEMORY_INFO *psNewMemoryInfo)
{
    DEVMEM_HEAP_BLUEPRINT *psDeviceMemoryHeapCursor;

    
	psNewMemoryInfo->psDeviceMemoryHeap = OSAllocMem(sizeof(DEVMEM_HEAP_BLUEPRINT) * RGX_MAX_HEAP_ID);
    if(psNewMemoryInfo->psDeviceMemoryHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRegisterDevice : Failed to alloc memory for DEVMEM_HEAP_BLUEPRINT"));
		goto e0;
	}

	psDeviceMemoryHeapCursor = psNewMemoryInfo->psDeviceMemoryHeap;

	/************* general ***************/
    psDeviceMemoryHeapCursor->pszName = RGX_GENERAL_HEAP_IDENT;
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_GENERAL_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_GENERAL_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */

	/************* PDS code and data ***************/
    psDeviceMemoryHeapCursor->pszName = RGX_PDSCODEDATA_HEAP_IDENT;
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_PDSCODEDATA_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_PDSCODEDATA_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */
	
	/************* USC code ***************/
    psDeviceMemoryHeapCursor->pszName = RGX_USCCODE_HEAP_IDENT;
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_USCCODE_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_USCCODE_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */

	/************* TQ 3D Parameters ***************/
	psDeviceMemoryHeapCursor->pszName = RGX_TQ3DPARAMETERS_HEAP_IDENT;
	psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_TQ3DPARAMETERS_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_TQ3DPARAMETERS_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */

	/************ Tiling Heaps ************/
	#define INIT_TILING_HEAP(N) \
	do { \
   		psDeviceMemoryHeapCursor->pszName = RGX_BIF_TILING_HEAP_ ## N ## _IDENT; \
   		psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_BIF_TILING_HEAP_ ## N ## _BASE; \
		psDeviceMemoryHeapCursor->uiHeapLength = RGX_BIF_TILING_HEAP_SIZE; \
		psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE(); \
		psDeviceMemoryHeapCursor++; \
	} while (0)
	INIT_TILING_HEAP(1);
	INIT_TILING_HEAP(2);
	INIT_TILING_HEAP(3);
	INIT_TILING_HEAP(4);
	#undef INIT_TILING_HEAP

	/************* Doppler ***************/
    psDeviceMemoryHeapCursor->pszName = RGX_DOPPLER_HEAP_IDENT;
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_DOPPLER_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_DOPPLER_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */

	/************* Doppler Overflow ***************/
    psDeviceMemoryHeapCursor->pszName = RGX_DOPPLER_OVERFLOW_HEAP_IDENT;
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_DOPPLER_OVERFLOW_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_DOPPLER_OVERFLOW_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */
	
	/************* HWBRN37200 ***************/
#if defined(FIX_HW_BRN_37200)
    psDeviceMemoryHeapCursor->pszName = "HWBRN37200";
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_HWBRN37200_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_HWBRN37200_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */
#endif

	/************* Firmware ***************/
    psDeviceMemoryHeapCursor->pszName = "Firmware";
    psDeviceMemoryHeapCursor->sHeapBaseAddr.uiAddr = RGX_FIRMWARE_HEAP_BASE;
	psDeviceMemoryHeapCursor->uiHeapLength = RGX_FIRMWARE_HEAP_SIZE;
	psDeviceMemoryHeapCursor->uiLog2DataPageSize = GET_LOG2_PAGESIZE();

	psDeviceMemoryHeapCursor++;/* advance to the next heap */

	/* set the heap count */
	psNewMemoryInfo->ui32HeapCount = (IMG_UINT32)(psDeviceMemoryHeapCursor - psNewMemoryInfo->psDeviceMemoryHeap);

	PVR_ASSERT(psNewMemoryInfo->ui32HeapCount <= RGX_MAX_HEAP_ID);

    /* the new way: we'll set up 2 heap configs: one will be for Meta
       only, and has only the firmware heap in it. 
       The remaining one shall be for clients only, and shall have all
       the other heaps in it */

    psNewMemoryInfo->uiNumHeapConfigs = 2;
	psNewMemoryInfo->psDeviceMemoryHeapConfigArray = OSAllocMem(sizeof(DEVMEM_HEAP_CONFIG) * psNewMemoryInfo->uiNumHeapConfigs);
    if (psNewMemoryInfo->psDeviceMemoryHeapConfigArray == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRegisterDevice : Failed to alloc memory for DEVMEM_HEAP_CONFIG"));
		goto e1;
	}
    
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[0].pszName = "Default Heap Configuration";
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[0].uiNumHeaps = psNewMemoryInfo->ui32HeapCount-1;
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[0].psHeapBlueprintArray = psNewMemoryInfo->psDeviceMemoryHeap;

    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[1].pszName = "Firmware Heap Configuration";
#if defined(FIX_HW_BRN_37200)
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[1].uiNumHeaps = 2;
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[1].psHeapBlueprintArray = psDeviceMemoryHeapCursor-2;
#else
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[1].uiNumHeaps = 1;
    psNewMemoryInfo->psDeviceMemoryHeapConfigArray[1].psHeapBlueprintArray = psDeviceMemoryHeapCursor-1;
#endif

	return PVRSRV_OK;
e1:
	OSFreeMem(psNewMemoryInfo->psDeviceMemoryHeap);
e0:
	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

static IMG_VOID RGX_DeInitHeaps(DEVICE_MEMORY_INFO *psDevMemoryInfo)
{
	OSFreeMem(psDevMemoryInfo->psDeviceMemoryHeapConfigArray);
	OSFreeMem(psDevMemoryInfo->psDeviceMemoryHeap);
}


/*
	RGXRegisterDevice
*/
PVRSRV_ERROR RGXRegisterDevice (PVRSRV_DEVICE_NODE *psDeviceNode)
{
    PVRSRV_ERROR eError;
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	PVRSRV_RGXDEV_INFO	*psDevInfo;

	/* pdump info about the core */
	PDUMPCOMMENT("RGX Version Information (KM): %s", RGX_BVNC_KM);
	
	#if defined(RGX_FEATURE_SYSTEM_CACHE)
	PDUMPCOMMENT("RGX System Level Cache is present");
	#endif /* RGX_FEATURE_SYSTEM_CACHE */

	PDUMPCOMMENT("RGX Initialisation (Part 1)");

	/*********************
	 * Device node setup *
	 *********************/
	/* Setup static data and callbacks on the device agnostic device node */
	psDeviceNode->sDevId.eDeviceType		= DEV_DEVICE_TYPE;
	psDeviceNode->sDevId.eDeviceClass		= DEV_DEVICE_CLASS;
#if defined(PDUMP)
	psDeviceNode->sDevId.pszPDumpRegName	= RGX_PDUMPREG_NAME;
	
	psDeviceNode->sDevId.pszPDumpDevName	= PhysHeapPDumpMemspaceName(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL]);
	psDeviceNode->pfnPDumpInitDevice = &RGXResetPDump;
#endif /* PDUMP */

	psDeviceNode->eHealthStatus = PVRSRV_DEVICE_HEALTH_STATUS_OK;

#if defined(SUPPORT_MEMORY_TILING)
	psDeviceNode->pfnAllocMemTilingRange = RGX_AllocMemTilingRange;
	psDeviceNode->pfnFreeMemTilingRange = RGX_FreeMemTilingRange;
#endif

	/* Configure MMU specific stuff */
	RGXMMUInit_Register(psDeviceNode);

	psDeviceNode->pfnMMUCacheInvalidate = RGXMMUCacheInvalidate;

	psDeviceNode->pfnSLCCacheInvalidateRequest = RGXSLCCacheInvalidateRequest;

	/* Register RGX to receive notifies when other devices complete some work */
	PVRSRVRegisterCmdCompleteNotify(&psDeviceNode->hCmdCompNotify, &RGXScheduleProcessQueuesKM, psDeviceNode);

	psDeviceNode->pfnInitDeviceCompatCheck	= &RGXDevInitCompatCheck;

	/* Register callbacks for creation of device memory contexts */
	psDeviceNode->pfnRegisterMemoryContext = RGXRegisterMemoryContext;
	psDeviceNode->pfnUnregisterMemoryContext = RGXUnregisterMemoryContext;

	/* Register callbacks for Unified Fence Objects */
	psDeviceNode->pfnAllocUFOBlock = RGXAllocUFOBlock;
	psDeviceNode->pfnFreeUFOBlock = RGXFreeUFOBlock;

	/* Register callback for dumping debug info */
	PVRSRVRegisterDbgRequestNotify(&psDeviceNode->hDbgReqNotify, &RGXDebugRequestNotify, DEBUG_REQUEST_RGX, psDeviceNode);
	
	/* Register callback for checking the device's health */
	psDeviceNode->pfnUpdateHealthStatus = RGXUpdateHealthStatus;

	/* Register method to service the FW HWPerf buffer */
	psDeviceNode->pfnServiceHWPerf = RGXHWPerfDataStoreCB;

	/* Register callback for getting the device version information string */
	psDeviceNode->pfnDeviceVersionString = RGXDevVersionString;

	/* Register callback for getting the device clock speed */
	psDeviceNode->pfnDeviceClockSpeed = RGXDevClockSpeed;

	/* Register callback for soft resetting some device modules */
	psDeviceNode->pfnSoftReset = RGXSoftReset;

	/* Register callback for resetting the HWR logs */
	psDeviceNode->pfnResetHWRLogs = RGXResetHWRLogs;


	/*********************
	 * Device info setup *
	 *********************/
	/* Allocate device control block */
	psDevInfo = OSAllocMem(sizeof(*psDevInfo));
	if (psDevInfo == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitRGXPart1 : Failed to alloc memory for DevInfo"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet (psDevInfo, 0, sizeof(*psDevInfo));

	/* create locks for the context lists stored in the DevInfo structure.
	 * these lists are modified on context create/destroy and read by the
	 * watchdog thread
	 */

	eError = OSWRLockCreate(&(psDevInfo->hRenderCtxListLock));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create render context list lock", __func__));
		goto e0;
	}

	eError = OSWRLockCreate(&(psDevInfo->hComputeCtxListLock));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create compute context list lock", __func__));
		goto e1;
	}

	eError = OSWRLockCreate(&(psDevInfo->hTransferCtxListLock));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create transfer context list lock", __func__));
		goto e2;
	}

	eError = OSWRLockCreate(&(psDevInfo->hRaytraceCtxListLock));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create raytrace context list lock", __func__));
		goto e3;
	}

	eError = OSWRLockCreate(&(psDevInfo->hMemoryCtxListLock));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create memory context list lock", __func__));
		goto e4;
	}

	dllist_init(&(psDevInfo->sRenderCtxtListHead));
	dllist_init(&(psDevInfo->sComputeCtxtListHead));
	dllist_init(&(psDevInfo->sTransferCtxtListHead));
	dllist_init(&(psDevInfo->sRaytraceCtxtListHead));

	dllist_init(&(psDevInfo->sCommonCtxtListHead));
	psDevInfo->ui32CommonCtxtCurrentID = 1;

	psDeviceNode->pvDevice = psDevInfo;
	dllist_init(&psDevInfo->sMemoryContextList);

	/* Allocate space for scripts. */
	psDevInfo->psScripts = OSAllocMem(sizeof(*psDevInfo->psScripts));
	if (!psDevInfo->psScripts)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_LOGG_IF_ERROR(PVRSRV_ERROR_OUT_OF_MEMORY, "OSAllocMem", e5);
	}

	/* Setup static data and callbacks on the device specific device info */
	psDevInfo->eDeviceType 		= DEV_DEVICE_TYPE;
	psDevInfo->eDeviceClass 	= DEV_DEVICE_CLASS;
	psDevInfo->psDeviceNode		= psDeviceNode;

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	psDevMemoryInfo->ui32AddressSpaceSizeLog2 = RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS;
	psDevInfo->pvDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

	/* flags, backing store details to be specified by system */
	psDevMemoryInfo->ui32Flags = 0;

	eError = RGX_InitHeaps(psDevMemoryInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "RGX_InitHeaps", e6);
	}

	return PVRSRV_OK;

e6:
	OSFreeMem(psDevInfo->psScripts);
	psDevInfo->psScripts = NULL;
e5:
	OSWRLockDestroy(psDevInfo->hMemoryCtxListLock);
e4:
	OSWRLockDestroy(psDevInfo->hRaytraceCtxListLock);
e3:
	OSWRLockDestroy(psDevInfo->hTransferCtxListLock);
e2:
	OSWRLockDestroy(psDevInfo->hComputeCtxListLock);
e1:
	OSWRLockDestroy(psDevInfo->hRenderCtxListLock);
e0:
	psDeviceNode->pvDevice = NULL;
	OSFreeMem(psDevInfo);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_KMBuildOptions_FWAgainstDriver

 @Description

 Validate the FW build options against KM driver build options (KM build options only)

 Following check is reduntant, because next check checks the same bits.
 Redundancy occurs because if client-server are build-compatible and client-firmware are 
 build-compatible then server-firmware are build-compatible as well.
 
 This check is left for clarity in error messages if any incompatibility occurs.

 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck_KMBuildOptions_FWAgainstDriver(RGXFWIF_INIT *psRGXFWInit)
{
#if !defined(NO_HARDWARE)
	IMG_UINT32			ui32BuildOptions, ui32BuildOptionsFWKMPart, ui32BuildOptionsMismatch;

	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);

	ui32BuildOptionsFWKMPart = psRGXFWInit->sRGXCompChecks.ui32BuildOptions & RGX_BUILD_OPTIONS_MASK_KM;
	
	if (ui32BuildOptions != ui32BuildOptionsFWKMPart)
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ ui32BuildOptionsFWKMPart;
		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in Firmware and KM driver build options; "
				"extra options present in the KM driver: (0x%x). Please check rgx_options_km.h",
				ui32BuildOptions & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptionsFWKMPart & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in Firmware-side and KM driver build options; "
				"extra options present in Firmware: (0x%x). Please check rgx_options_km.h",
				ui32BuildOptionsFWKMPart & ui32BuildOptionsMismatch ));
		}
		return PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: Firmware and KM driver build options match. [ OK ]"));
	}
#endif

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_BuildOptions_FWAgainstClient

 @Description

 Validate the FW build options against client build options (KM and non-KM)

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data
 @Input ui32ClientBuildOptions - client build options flags

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck_BuildOptions_FWAgainstClient(PVRSRV_RGXDEV_INFO 	*psDevInfo,
																			RGXFWIF_INIT *psRGXFWInit,
																			IMG_UINT32 ui32ClientBuildOptions)
{
#if !defined(NO_HARDWARE)
	IMG_UINT32			ui32BuildOptionsMismatch;
	IMG_UINT32			ui32BuildOptionsFW;
#endif
#if defined(PDUMP)
	PVRSRV_ERROR		eError;
#endif

#if defined(PDUMP)
	PDUMPCOMMENT("Compatibility check: client and FW build options");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, ui32BuildOptions),
												ui32ClientBuildOptions,
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}
#endif

#if !defined(NO_HARDWARE)
	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;
	
	ui32BuildOptionsFW = psRGXFWInit->sRGXCompChecks.ui32BuildOptions;
	
	if (ui32ClientBuildOptions != ui32BuildOptionsFW)
	{
		ui32BuildOptionsMismatch = ui32ClientBuildOptions ^ ui32BuildOptionsFW;
		if ( (ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in Firmware and client build options; "
				"extra options present in client: (0x%x). Please check rgx_options.h",
				ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptionsFW & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in Firmware and client build options; "
				"extra options present in Firmware: (0x%x). Please check rgx_options.h",
				ui32BuildOptionsFW & ui32BuildOptionsMismatch ));
		}
		return PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: Firmware and client build options match. [ OK ]"));
	}
#endif

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_DDKVersion_FWAgainstDriver

 @Description

 Validate FW DDK version against driver DDK version

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck_DDKVersion_FWAgainstDriver(PVRSRV_RGXDEV_INFO *psDevInfo,
																			RGXFWIF_INIT *psRGXFWInit)
{
#if defined(PDUMP)||(!defined(NO_HARDWARE))
	IMG_UINT32			ui32DDKVersion;
	PVRSRV_ERROR		eError;
	
	ui32DDKVersion = PVRVERSION_PACK(PVRVERSION_MAJ, PVRVERSION_MIN);
#endif

#if defined(PDUMP)
	PDUMPCOMMENT("Compatibility check: KM driver and FW DDK version");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, ui32DDKVersion),
												ui32DDKVersion,
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}
#endif

#if !defined(NO_HARDWARE)
	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psRGXFWInit->sRGXCompChecks.ui32DDKVersion != ui32DDKVersion)
	{
		PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Incompatible driver DDK revision (%u.%u) / Firmware DDK revision (%u.%u).",
				PVRVERSION_MAJ, PVRVERSION_MIN, 
				PVRVERSION_UNPACK_MAJ(psRGXFWInit->sRGXCompChecks.ui32DDKVersion),
				PVRVERSION_UNPACK_MIN(psRGXFWInit->sRGXCompChecks.ui32DDKVersion)));
		eError = PVRSRV_ERROR_DDK_VERSION_MISMATCH;
		PVR_DBG_BREAK;
		return eError;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: driver DDK revision (%u.%u) and Firmware DDK revision (%u.%u) match. [ OK ]",
				PVRVERSION_MAJ, PVRVERSION_MIN, 
				PVRVERSION_MAJ, PVRVERSION_MIN));
	}
#endif	

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_DDKBuild_FWAgainstDriver

 @Description

 Validate FW DDK build against driver DDK build

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck_DDKBuild_FWAgainstDriver(PVRSRV_RGXDEV_INFO *psDevInfo,
																			RGXFWIF_INIT *psRGXFWInit)
{
#if defined(PDUMP)||(!defined(NO_HARDWARE))
	IMG_UINT32			ui32DDKBuild;
	PVRSRV_ERROR		eError;
	
	ui32DDKBuild = PVRVERSION_BUILD;
#endif

#if defined(PDUMP)
	PDUMPCOMMENT("Compatibility check: KM driver and FW DDK build");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, ui32DDKBuild),
												ui32DDKBuild,
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}
#endif

#if !defined(NO_HARDWARE)
	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psRGXFWInit->sRGXCompChecks.ui32DDKBuild != ui32DDKBuild)
	{
		PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Incompatible driver DDK build (%d) / Firmware DDK build (%d).",
				ui32DDKBuild, psRGXFWInit->sRGXCompChecks.ui32DDKBuild));
		eError = PVRSRV_ERROR_DDK_BUILD_MISMATCH;
		PVR_DBG_BREAK;
		return eError;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: driver DDK build (%d) and Firmware DDK build (%d) match. [ OK ]",
				ui32DDKBuild, psRGXFWInit->sRGXCompChecks.ui32DDKBuild));
	}
#endif
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_BVNC_FWAgainstDriver

 @Description

 Validate FW BVNC against driver BVNC

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck_BVNC_FWAgainstDriver(PVRSRV_RGXDEV_INFO *psDevInfo,
																			RGXFWIF_INIT *psRGXFWInit)
{
#if defined(PDUMP)
	IMG_UINT32					i;
#endif
#if !defined(NO_HARDWARE)
	IMG_BOOL bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV;
#endif
#if defined(PDUMP)||(!defined(NO_HARDWARE))
	RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(sBVNC);
	PVRSRV_ERROR				eError;
	
	rgx_bvnc_packed(&sBVNC.ui32BNC, sBVNC.aszV, sBVNC.ui32VLenMax, RGX_BVNC_KM_B, RGX_BVNC_KM_V_ST, RGX_BVNC_KM_N, RGX_BVNC_KM_C);
#endif

#if defined(PDUMP)
	PDUMPCOMMENT("Compatibility check: KM driver and FW BVNC (struct version)");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
											offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
											offsetof(RGXFWIF_COMPCHECKS, sFWBVNC) +
											offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32LayoutVersion),
											sBVNC.ui32LayoutVersion,
											0xffffffff,
											PDUMP_POLL_OPERATOR_EQUAL,
											PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
	}

	PDUMPCOMMENT("Compatibility check: KM driver and FW BVNC (maxlen)");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
											offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
											offsetof(RGXFWIF_COMPCHECKS, sFWBVNC) +
											offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32VLenMax),
											sBVNC.ui32VLenMax,
											0xffffffff,
											PDUMP_POLL_OPERATOR_EQUAL,
											PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
	}

	PDUMPCOMMENT("Compatibility check: KM driver and FW BVNC (BNC part)");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
											offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
											offsetof(RGXFWIF_COMPCHECKS, sFWBVNC) +
											offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32BNC),
											sBVNC.ui32BNC,
											0xffffffff,
											PDUMP_POLL_OPERATOR_EQUAL,
											PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
	}

	for (i = 0; i < sBVNC.ui32VLenMax; i += sizeof(IMG_UINT32))
	{
		PDUMPCOMMENT("Compatibility check: KM driver and FW BVNC (V part)");
		eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, sFWBVNC) +
												offsetof(RGXFWIF_COMPCHECKS_BVNC, aszV) + 
												i,
												*((IMG_UINT32 *)(sBVNC.aszV + i)),
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		}
	}
#endif

#if !defined(NO_HARDWARE)
	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	RGX_BVNC_EQUAL(sBVNC, psRGXFWInit->sRGXCompChecks.sFWBVNC, bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV);
	
	if (!bCompatibleAll)
	{
		if (!bCompatibleVersion)
		{
			PVR_LOG(("(FAIL) %s: Incompatible compatibility struct version of driver (%d) and firmware (%d).",
					__FUNCTION__, 
					sBVNC.ui32LayoutVersion, 
					psRGXFWInit->sRGXCompChecks.sFWBVNC.ui32LayoutVersion));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}

		if (!bCompatibleLenMax)
		{
			PVR_LOG(("(FAIL) %s: Incompatible V maxlen of driver (%d) and firmware (%d).",
					__FUNCTION__, 
					sBVNC.ui32VLenMax, 
					psRGXFWInit->sRGXCompChecks.sFWBVNC.ui32VLenMax));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}

		if (!bCompatibleBNC)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in KM driver BNC (%d._.%d.%d) and Firmware BNC (%d._.%d.%d)",
					RGX_BVNC_PACKED_EXTR_B(sBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sBVNC), 
					RGX_BVNC_PACKED_EXTR_B(psRGXFWInit->sRGXCompChecks.sFWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(psRGXFWInit->sRGXCompChecks.sFWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(psRGXFWInit->sRGXCompChecks.sFWBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}
		
		if (!bCompatibleV)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Mismatch in KM driver BVNC (%d.%s.%d.%d) and Firmware BVNC (%d.%s.%d.%d)",
					RGX_BVNC_PACKED_EXTR_B(sBVNC), 
					RGX_BVNC_PACKED_EXTR_V(sBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sBVNC), 
					RGX_BVNC_PACKED_EXTR_B(psRGXFWInit->sRGXCompChecks.sFWBVNC), 
					RGX_BVNC_PACKED_EXTR_V(psRGXFWInit->sRGXCompChecks.sFWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(psRGXFWInit->sRGXCompChecks.sFWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(psRGXFWInit->sRGXCompChecks.sFWBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: Firmware BVNC and KM driver BNVC match. [ OK ]"));
	}
#endif
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_BVNC_HWAgainstDriver

 @Description

 Validate HW BVNC against driver BVNC

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
#if ((!defined(NO_HARDWARE))&&(!defined(EMULATOR)))
#define TARGET_SILICON  /* definition for everything that is not emu and not nohw configuration */
#endif

#if defined(FIX_HW_BRN_38835)
#define COMPAT_BVNC_MASK_B
#define COMPAT_BVNC_MASK_V
#endif

static PVRSRV_ERROR RGXDevInitCompatCheck_BVNC_HWAgainstDriver(PVRSRV_RGXDEV_INFO *psDevInfo,
																	RGXFWIF_INIT *psRGXFWInit)
{
#if defined(PDUMP) || defined(TARGET_SILICON)
	IMG_UINT32 ui32MaskBNC = RGX_BVNC_PACK_MASK_B |
								RGX_BVNC_PACK_MASK_N |
								RGX_BVNC_PACK_MASK_C;

	IMG_UINT32 bMaskV = IMG_FALSE;

	PVRSRV_ERROR				eError;
	RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(sSWBVNC);
#endif

#if defined(TARGET_SILICON)
	RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(sHWBVNC);
	IMG_BOOL bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV;
#endif

#if defined(PDUMP) || defined(TARGET_SILICON)

#if defined(COMPAT_BVNC_MASK_B)
	ui32MaskBNC &= ~RGX_BVNC_PACK_MASK_B;
#endif
#if defined(COMPAT_BVNC_MASK_V)
	bMaskV = IMG_TRUE;
#endif
#if defined(COMPAT_BVNC_MASK_N)
	ui32MaskBNC &= ~RGX_BVNC_PACK_MASK_N;
#endif
#if defined(COMPAT_BVNC_MASK_C)
	ui32MaskBNC &= ~RGX_BVNC_PACK_MASK_C;
#endif
	
	rgx_bvnc_packed(&sSWBVNC.ui32BNC, sSWBVNC.aszV, sSWBVNC.ui32VLenMax, RGX_BVNC_KM_B, RGX_BVNC_KM_V_ST, RGX_BVNC_KM_N, RGX_BVNC_KM_C);

#if defined(FIX_HW_BRN_38344)
	if (RGX_BVNC_KM_C >= 10)
	{
		ui32MaskBNC &= ~RGX_BVNC_PACK_MASK_C;
	}
#endif

	if ((ui32MaskBNC != (RGX_BVNC_PACK_MASK_B | RGX_BVNC_PACK_MASK_N | RGX_BVNC_PACK_MASK_C)) || bMaskV)
	{
		PVR_LOG(("Compatibility checks: Ignoring fields: '%s%s%s%s' of HW BVNC.",
				((!(ui32MaskBNC & RGX_BVNC_PACK_MASK_B))?("B"):("")), 
				((bMaskV)?("V"):("")), 
				((!(ui32MaskBNC & RGX_BVNC_PACK_MASK_N))?("N"):("")), 
				((!(ui32MaskBNC & RGX_BVNC_PACK_MASK_C))?("C"):(""))));
	}
#endif

#if defined(EMULATOR)
	PVR_LOG(("Compatibility checks for emu target: Ignoring HW BVNC checks."));
#endif

#if defined(PDUMP)
	PDUMPCOMMENT("Compatibility check: Layout version of compchecks struct");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
											offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
											offsetof(RGXFWIF_COMPCHECKS, sHWBVNC) +
											offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32LayoutVersion),
											sSWBVNC.ui32LayoutVersion,
											0xffffffff,
											PDUMP_POLL_OPERATOR_EQUAL,
											PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}

	PDUMPCOMMENT("Compatibility check: HW V max len and FW V max len");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
											offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
											offsetof(RGXFWIF_COMPCHECKS, sHWBVNC) +
											offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32VLenMax),
											sSWBVNC.ui32VLenMax,
											0xffffffff,
											PDUMP_POLL_OPERATOR_EQUAL,
											PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}

	if (ui32MaskBNC != 0)
	{
		PDUMPIF("DISABLE_HWBNC_CHECK");
		PDUMPELSE("DISABLE_HWBNC_CHECK");
		PDUMPCOMMENT("Compatibility check: HW BNC and FW BNC");
		eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, sHWBVNC) +
												offsetof(RGXFWIF_COMPCHECKS_BVNC, ui32BNC),
												sSWBVNC.ui32BNC,
												ui32MaskBNC,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
			return eError;
		}
		PDUMPFI("DISABLE_HWBNC_CHECK");
	}
	if (!bMaskV)
	{
		IMG_UINT32 i;
		PDUMPIF("DISABLE_HWV_CHECK");
		PDUMPELSE("DISABLE_HWV_CHECK");
		for (i = 0; i < sSWBVNC.ui32VLenMax; i += sizeof(IMG_UINT32))
		{
			PDUMPCOMMENT("Compatibility check: HW V and FW V");
			eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
												offsetof(RGXFWIF_INIT, sRGXCompChecks) + 
												offsetof(RGXFWIF_COMPCHECKS, sHWBVNC) +
												offsetof(RGXFWIF_COMPCHECKS_BVNC, aszV) + 
												i,
												*((IMG_UINT32 *)(sSWBVNC.aszV + i)),
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
				return eError;
			}
		}
		PDUMPFI("DISABLE_HWV_CHECK");
	}
#endif

#if defined(TARGET_SILICON)
	if (psRGXFWInit == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	
	sHWBVNC = psRGXFWInit->sRGXCompChecks.sHWBVNC;

	sHWBVNC.ui32BNC &= ui32MaskBNC;
	sSWBVNC.ui32BNC &= ui32MaskBNC;

	if (bMaskV)
	{
		sHWBVNC.aszV[0] = '\0';
		sSWBVNC.aszV[0] = '\0';
	}

	RGX_BVNC_EQUAL(sSWBVNC, sHWBVNC, bCompatibleAll, bCompatibleVersion, bCompatibleLenMax, bCompatibleBNC, bCompatibleV);

#if defined(FIX_HW_BRN_42480)
	if (!bCompatibleAll && bCompatibleVersion)
	{
		if ((RGX_BVNC_PACKED_EXTR_B(sSWBVNC) == 1) &&
			!(OSStringCompare(RGX_BVNC_PACKED_EXTR_V(sSWBVNC),"76")) &&
			(RGX_BVNC_PACKED_EXTR_N(sSWBVNC) == 4) &&
			(RGX_BVNC_PACKED_EXTR_C(sSWBVNC) == 6))
		{
			if ((RGX_BVNC_PACKED_EXTR_B(sHWBVNC) == 1) &&
				!(OSStringCompare(RGX_BVNC_PACKED_EXTR_V(sHWBVNC),"69")) &&
				(RGX_BVNC_PACKED_EXTR_N(sHWBVNC) == 4) &&
				(RGX_BVNC_PACKED_EXTR_C(sHWBVNC) == 4))
			{
				bCompatibleBNC = IMG_TRUE;
				bCompatibleLenMax = IMG_TRUE;
				bCompatibleV = IMG_TRUE;
				bCompatibleAll = IMG_TRUE;
			}
		}
	}
#endif

	if (!bCompatibleAll)
	{
		if (!bCompatibleVersion)
		{
			PVR_LOG(("(FAIL) %s: Incompatible compatibility struct version of HW (%d) and FW (%d).",
					__FUNCTION__, 
					sHWBVNC.ui32LayoutVersion, 
					sSWBVNC.ui32LayoutVersion));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}

		if (!bCompatibleLenMax)
		{
			PVR_LOG(("(FAIL) %s: Incompatible V maxlen of HW (%d) and FW (%d).",
					__FUNCTION__, 
					sHWBVNC.ui32VLenMax, 
					sSWBVNC.ui32VLenMax));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}

		if (!bCompatibleBNC)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Incompatible HW BNC (%d._.%d.%d) and FW BNC (%d._.%d.%d).",
					RGX_BVNC_PACKED_EXTR_B(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_B(sSWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sSWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sSWBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}
		
		if (!bCompatibleV)
		{
			PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Incompatible HW BVNC (%d.%s.%d.%d) and FW BVNC (%d.%s.%d.%d).",
					RGX_BVNC_PACKED_EXTR_B(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_V(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sHWBVNC), 
					RGX_BVNC_PACKED_EXTR_B(sSWBVNC), 
					RGX_BVNC_PACKED_EXTR_V(sSWBVNC), 
					RGX_BVNC_PACKED_EXTR_N(sSWBVNC), 
					RGX_BVNC_PACKED_EXTR_C(sSWBVNC)));
			eError = PVRSRV_ERROR_BVNC_MISMATCH;
			return eError;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: HW BVNC (%d.%s.%d.%d) and FW BVNC (%d.%s.%d.%d) match. [ OK ]", 
				RGX_BVNC_PACKED_EXTR_B(sHWBVNC), 
				RGX_BVNC_PACKED_EXTR_V(sHWBVNC), 
				RGX_BVNC_PACKED_EXTR_N(sHWBVNC), 
				RGX_BVNC_PACKED_EXTR_C(sHWBVNC), 
				RGX_BVNC_PACKED_EXTR_B(sSWBVNC), 
				RGX_BVNC_PACKED_EXTR_V(sSWBVNC), 
				RGX_BVNC_PACKED_EXTR_N(sSWBVNC), 
				RGX_BVNC_PACKED_EXTR_C(sSWBVNC)));
	}
#endif

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck_METACoreVersion_AgainstDriver

 @Description

 Validate HW META version against driver META version

 @Input psDevInfo - device info
 @Input psRGXFWInit - FW init data

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/

static PVRSRV_ERROR RGXDevInitCompatCheck_METACoreVersion_AgainstDriver(PVRSRV_RGXDEV_INFO *psDevInfo,
									RGXFWIF_INIT *psRGXFWInit)
{
#if defined(PDUMP)||(!defined(NO_HARDWARE))
	PVRSRV_ERROR		eError;
#endif

#if defined(PDUMP)
	PDUMPIF("DISABLE_HWMETA_CHECK");
	PDUMPELSE("DISABLE_HWMETA_CHECK");
	PDUMPCOMMENT("Compatibility check: KM driver and HW META version");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfInitMemDesc,
					offsetof(RGXFWIF_INIT, sRGXCompChecks) +
					offsetof(RGXFWIF_COMPCHECKS, ui32METAVersion),
					RGX_CR_META_CORE_ID_VALUE,
					0xffffffff,
					PDUMP_POLL_OPERATOR_EQUAL,
					PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDevInitCompatCheck: problem pdumping POL for psRGXFWIfInitMemDesc (%d)", eError));
		return eError;
	}
	PDUMPFI("DISABLE_HWMETA_CHECK");
#endif

#if !defined(NO_HARDWARE)
	if (psRGXFWInit == IMG_NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psRGXFWInit->sRGXCompChecks.ui32METAVersion != RGX_CR_META_CORE_ID_VALUE)
	{
		PVR_LOG(("(FAIL) RGXDevInitCompatCheck: Incompatible driver META version (%d) / HW META version (%d).",
				RGX_CR_META_CORE_ID_VALUE, psRGXFWInit->sRGXCompChecks.ui32METAVersion));
		eError = PVRSRV_ERROR_META_MISMATCH;
		PVR_DBG_BREAK;
		return eError;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "RGXDevInitCompatCheck: driver META version (%d) and HW META version (%d) match. [ OK ]",
				RGX_CR_META_CORE_ID_VALUE, psRGXFWInit->sRGXCompChecks.ui32METAVersion));
	}
#endif
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	RGXDevInitCompatCheck

 @Description

 Check compatibility of host driver and firmware (DDK and build options)
 for RGX devices at services/device initialisation

 @Input psDeviceNode - device node

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
static PVRSRV_ERROR RGXDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT32 ui32ClientBuildOptions)
{
	PVRSRV_ERROR		eError;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_INIT		*psRGXFWInit = IMG_NULL;
#if !defined(NO_HARDWARE)
	IMG_UINT32			ui32RegValue;
#endif

	/* Ensure it's a RGX device */
	if(psDeviceNode->sDevId.eDeviceType != PVRSRV_DEVICE_TYPE_RGX)
	{
		PVR_LOG(("(FAIL) %s: Device not of type RGX", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto chk_exit;
	}

	/* 
	 * Retrieve the FW information
	 */
	
#if !defined(NO_HARDWARE)
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc,
												(IMG_VOID **)&psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to acquire kernel fw compatibility check info (%u)",
				__FUNCTION__, eError));
		return eError;
	}

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		if(*((volatile IMG_BOOL *)&psRGXFWInit->sRGXCompChecks.bUpdated))
		{
			/* No need to wait if the FW has already updated the values */
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	ui32RegValue = 0;
	eError = RGXReadMETAReg(psDevInfo, META_CR_T0ENABLE_OFFSET, &ui32RegValue);

	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Reading RGX META register failed. Is the GPU correctly powered up? (%u)",
				__FUNCTION__, eError));
		goto chk_exit;
	}

	if (!(ui32RegValue & META_CR_TXENABLE_ENABLE_BIT))
	{
		eError = PVRSRV_ERROR_META_THREAD0_NOT_ENABLED;
		PVR_DPF((PVR_DBG_ERROR,"%s: RGX META is not running. Is the GPU correctly powered up? %d (%u)",
				__FUNCTION__, psRGXFWInit->sRGXCompChecks.bUpdated, eError));
		goto chk_exit;
	}
	
	if (!*((volatile IMG_BOOL *)&psRGXFWInit->sRGXCompChecks.bUpdated))
	{
		eError = PVRSRV_ERROR_TIMEOUT;
		PVR_DPF((PVR_DBG_ERROR,"%s: Missing compatibility info from FW (%u)",
				__FUNCTION__, eError));
		goto chk_exit;
	}
#endif

	eError = RGXDevInitCompatCheck_KMBuildOptions_FWAgainstDriver(psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = RGXDevInitCompatCheck_BuildOptions_FWAgainstClient(psDevInfo, psRGXFWInit, ui32ClientBuildOptions);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}
	
	eError = RGXDevInitCompatCheck_DDKVersion_FWAgainstDriver(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = RGXDevInitCompatCheck_DDKBuild_FWAgainstDriver(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = RGXDevInitCompatCheck_BVNC_FWAgainstDriver(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = RGXDevInitCompatCheck_BVNC_HWAgainstDriver(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = RGXDevInitCompatCheck_METACoreVersion_AgainstDriver(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		goto chk_exit;
	}

	eError = PVRSRV_OK;
chk_exit:
#if !defined(NO_HARDWARE)
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
#endif
	return eError;
}

#define	MAKESTRING(x) #x
#define TOSTRING(x) MAKESTRING(x)





static PVRSRV_ERROR
ValidateFWImage(
	IMG_CHAR *pcFWImgDestAddr,
	IMG_CHAR *pcFWImgSrcAddr,
	IMG_SIZE_T uiFWImgLen,
	IMG_CHAR *pcFWImgSigAddr,
	IMG_UINT64 ui64FWSigLen)
{
#if defined(DEBUG)
	if(OSMemCmp(pcFWImgDestAddr, pcFWImgSrcAddr, uiFWImgLen) != 0)
	{
		return PVRSRV_ERROR_INIT_TDMETACODE_PAGES_FAIL;
	}

	PVR_UNREFERENCED_PARAMETER(pcFWImgSigAddr);
	PVR_UNREFERENCED_PARAMETER(ui64FWSigLen);
#else
	PVR_UNREFERENCED_PARAMETER(pcFWImgDestAddr);
	PVR_UNREFERENCED_PARAMETER(uiFWImgLen);
	PVR_UNREFERENCED_PARAMETER(pcFWImgSigAddr);
	PVR_UNREFERENCED_PARAMETER(ui64FWSigLen);
#endif

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRCopy(PMR *psDstPMR, PMR *psSrcPMR, IMG_SIZE_T uiMaxCopyLen)
{
	IMG_CHAR acBuf[512];
	IMG_UINT64 uiBytesCopied;
	PVRSRV_ERROR eStatus;
	
	uiBytesCopied = 0;
	while(uiBytesCopied < uiMaxCopyLen)
	{
		IMG_SIZE_T uiRead, uiWritten;
		IMG_SIZE_T uiCopyAmt;
		uiCopyAmt = sizeof(acBuf) > uiMaxCopyLen ? uiMaxCopyLen : sizeof(acBuf);
		eStatus = PMR_ReadBytes(psSrcPMR,
		                        uiBytesCopied,
		                        acBuf,
		                        uiCopyAmt,
		                        &uiRead);
		if(eStatus != PVRSRV_OK)
		{
			return eStatus;
		}
		eStatus = PMR_WriteBytes(psDstPMR,
		                         uiBytesCopied,
		                         acBuf,
		                         uiCopyAmt,
		                         &uiWritten);
		if(eStatus != PVRSRV_OK)
		{
			return eStatus;
		}
		PVR_ASSERT(uiRead == uiWritten);
		PVR_ASSERT(uiRead == uiCopyAmt);
		uiBytesCopied += uiCopyAmt;
	}

	return PVRSRV_OK;
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXInitLoadFWImageKM(
	PMR *psFWImgDestPMR,
	PMR *psFWImgSrcPMR,
	IMG_UINT64 ui64FWImgLen,
	PMR *psFWImgSigPMR,
	IMG_UINT64 ui64FWSigLen)
{
	IMG_CHAR *pcFWImgSigAddr, *pcFWImgDestAddr, *pcFWImgSrcAddr;
	IMG_HANDLE hFWImgSigHdl, hFWImgDestHdl, hFWImgSrcHdl;
	IMG_SIZE_T uiLen;
	PVRSRV_ERROR eStatus;

	/* The purpose of this function is to do the following:
	   - copy the data contained in psFWImgSrcPMR into psFWImgDestPMR
	   - use the data contained in psFWImgSigPMR to validate the contents of psFWImgDestPMR

	   This is a functional placeholder that is meant to be overridden when actually using
	   the protected META code feature. As a result, normally, the memory backed by 
	   psFWImgDestPMR will not be read/writeable from this layer. Thus the operation of
	   actually doing the copy and verify must be handled in a mode with more privilege,
	   typically a hypervisor.

	   Because psFWImgSrcPMR and psFWImgSigPMR are normal OS-memory controlled PMR's, it
	   should be sufficient to acquire their kernel mappings and pass the pointers to
	   their mapped addressed into the hypervisor. However, since psFWImgDestPMR references
	   a region of memory that would typically be allocated (and writeable) by a hypervisor,
	   it will be necessary to pass the psFWImgDestPMR->pvFlavourData (or a field contained
	   within it) to the hypervisor to identify the region of memory to copy to and validate.

	   In the example function provided below, the following things happen:
	   - kernel mappings are acquired for the destination and signature PMRs
	   - a copy is done using the PMR_ReadBytes / PMR_WriteBytes callback functionality in
	     the PMR layer
	   - a validation is done by reading back the destination buffer and comparing it against
	     the source buffer.

	   c.f. a real implementation, where the following things would likely happen:
	   - kernel mappings are acquired for the source and signature PMRs
	   - the source/signature mapped addresses and lengths, and psFWImgDestPMR->pvFlavourData
	     are passed into the hypervisor to do the copy/validate.
	*/

	eStatus = PMRAcquireKernelMappingData(psFWImgDestPMR,
	                                      0,
	                                      0,
	                                      (IMG_VOID **) &pcFWImgDestAddr,
	                                      &uiLen,
                                          &hFWImgDestHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Acquire mapping for dest failed (%u)", eStatus));
		goto error;
	}
	if(ui64FWImgLen > uiLen)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: PMR dst len (%llu) > mapped len (%llu)",
		         ui64FWImgLen, (unsigned long long)uiLen));
		goto error;
	}

	eStatus = PMRAcquireKernelMappingData(psFWImgSrcPMR,
	                                      0,
	                                      0,
	                                      (IMG_VOID **) &pcFWImgSrcAddr,
	                                      &uiLen,
                                          &hFWImgSrcHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Acquire mapping for src failed (%u)", eStatus));
		goto error;
	}
	if(ui64FWImgLen > uiLen)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: PMR dst len (%llu) > mapped len (%llu)",
		         ui64FWImgLen, (unsigned long long)uiLen));
		goto error;
	}

	eStatus = PMRAcquireKernelMappingData(psFWImgSigPMR,
	                                      0,
	                                      0,
	                                      (IMG_VOID **) &pcFWImgSigAddr,
	                                      &uiLen,
                                          &hFWImgSigHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Acquire mapping for sig failed (%u)", eStatus));
		goto error;
	}
	if(ui64FWSigLen > uiLen)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: sig len (%llu) > mapped len (%llu)",
		         ui64FWSigLen, (unsigned long long)uiLen));
		goto error;
	}

	/* Copy the firmware image from the intermediate buffer to the real firmware memory allocation. */
	PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVDebugMiscInitFWImageKM: copying %llu bytes from PMR %p to PMR %p",
	                        ui64FWImgLen, psFWImgSrcPMR, psFWImgDestPMR));
	PMRCopy(psFWImgDestPMR, psFWImgSrcPMR, TRUNCATE_64BITS_TO_SIZE_T(ui64FWImgLen));

	/* validate the firmware image after it has been copied into place */
	eStatus = ValidateFWImage(pcFWImgDestAddr, pcFWImgSrcAddr, TRUNCATE_64BITS_TO_SIZE_T(ui64FWImgLen), pcFWImgSigAddr, ui64FWSigLen);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Signature check failed"));
		goto error;
	}

	eStatus = PMRReleaseKernelMappingData(psFWImgDestPMR,
	                                      hFWImgDestHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Release mapping for dest failed (%u)", eStatus));
		goto error;
	}

	eStatus = PMRReleaseKernelMappingData(psFWImgSrcPMR,
	                                      hFWImgSrcHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Release mapping for src failed (%u)", eStatus));
		goto error;
	}

	eStatus = PMRReleaseKernelMappingData(psFWImgSigPMR,
	                                      hFWImgSigHdl);
	if(eStatus != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscInitFWImageKM: Release mapping for sig failed (%u)", eStatus));
		goto error;
	}

	return PVRSRV_OK;

error:
	return PVRSRV_ERROR_INIT_TDMETACODE_PAGES_FAIL;
}



/*************************************************************************/ /*!
@Function       RGXDevVersionString
@Description    Gets the version string for the given device node and returns 
                a pointer to it in ppszVersionString. It is then the 
                responsibility of the caller to free this memory.
@Input          psDeviceNode            Device node from which to obtain the 
                                        version string
@Output	        ppszVersionString	Contains the version string upon return
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXDevVersionString(PVRSRV_DEVICE_NODE *psDeviceNode, 
					IMG_CHAR **ppszVersionString)
{
#if defined(COMPAT_BVNC_MASK_B) || defined(COMPAT_BVNC_MASK_V) || defined(COMPAT_BVNC_MASK_N) || defined(COMPAT_BVNC_MASK_C) || defined(NO_HARDWARE) || defined(EMULATOR)
	IMG_CHAR pszFormatString[] = "Rogue Version: %d.%s.%d.%d (SW)";
#else
	IMG_CHAR pszFormatString[] = "Rogue Version: %d.%s.%d.%d (HW)";
#endif
	IMG_SIZE_T uiStringLength;

	if (psDeviceNode == NULL || ppszVersionString == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	uiStringLength = OSStringLength(pszFormatString);
	uiStringLength += OSStringLength(TOSTRING(RGX_BVNC_KM_B));
	uiStringLength += OSStringLength(TOSTRING(RGX_BVNC_KM_V));
	uiStringLength += OSStringLength(TOSTRING(RGX_BVNC_KM_N));
	uiStringLength += OSStringLength(TOSTRING(RGX_BVNC_KM_C));

	*ppszVersionString = OSAllocZMem(uiStringLength * sizeof(IMG_CHAR));
	if (*ppszVersionString == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSSNPrintf(*ppszVersionString, uiStringLength, pszFormatString, 
		   RGX_BVNC_KM_B, TOSTRING(RGX_BVNC_KM_V), RGX_BVNC_KM_N, RGX_BVNC_KM_C);

	return PVRSRV_OK;
}

/**************************************************************************/ /*!
@Function       RGXDevClockSpeed
@Description    Gets the clock speed for the given device node and returns
                it in pui32RGXClockSpeed.
@Input          psDeviceNode		Device node
@Output         pui32RGXClockSpeed  Variable for storing the clock speed
@Return         PVRSRV_ERROR
*/ /***************************************************************************/
static PVRSRV_ERROR RGXDevClockSpeed(PVRSRV_DEVICE_NODE *psDeviceNode,
					IMG_PUINT32  pui32RGXClockSpeed)
{
	RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

	/* get clock speed */
	*pui32RGXClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	return PVRSRV_OK;
}


/**************************************************************************/ /*!
@Function       RGXSoftReset
@Description    Resets some modules of the RGX device
@Input          psDeviceNode		Device node
@Output         ui64ResetValue  a mask for which each bit set correspond 
                                to a module to reset.
@Return         PVRSRV_ERROR
*/ /***************************************************************************/
static PVRSRV_ERROR RGXSoftReset(PVRSRV_DEVICE_NODE *psDeviceNode,
					IMG_UINT64  ui64ResetValue)
{
	PVRSRV_RGXDEV_INFO        *psDevInfo;

	PVR_ASSERT(psDeviceNode != NULL);
	PVR_ASSERT(psDeviceNode->pvDevice != NULL);

	if ((ui64ResetValue & RGX_CR_SOFT_RESET_MASKFULL) != ui64ResetValue)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* the device info */
	psDevInfo = psDeviceNode->pvDevice;

	/* Set in soft-reset */
	OSWriteHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_SOFT_RESET, ui64ResetValue);

	/* Read soft-reset to fence previos write in order to clear the SOCIF pipeline */
	(IMG_VOID) OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_SOFT_RESET);

	return PVRSRV_OK;
}


/******************************************************************************
 End of file (rgxinit.c)
******************************************************************************/
