/*************************************************************************/ /*!
@Title          Device specific power routines
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

#include <stddef.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "sgxapi_km.h"
#include "sgx_mkif_km.h"
#include "sgxutils.h"
#include "pdump_km.h"

extern IMG_UINT32 g_ui32HostIRQCountSample;

#if defined(SUPPORT_HW_RECOVERY)
static PVRSRV_ERROR SGXAddTimer(PVRSRV_DEVICE_NODE		*psDeviceNode,
								SGX_TIMING_INFORMATION	*psSGXTimingInfo,
								IMG_HANDLE				*phTimer)
{
	/*
		Install timer callback for HW recovery at 50 times lower
		frequency than the microkernel timer.
	*/
	*phTimer = OSAddTimer(SGXOSTimer, psDeviceNode,
						  1000 * 50 / psSGXTimingInfo->ui32uKernelFreq);
	if(*phTimer == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXAddTimer : Failed to register timer callback function"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	return PVRSRV_OK;
}
#endif /* SUPPORT_HW_RECOVERY*/


/*!
******************************************************************************

 @Function	SGXUpdateTimingInfo

 @Description

 	Derives the microkernel timing info from the system-supplied values

 @Input	   psDeviceNode : SGX Device node

 @Return   PVRSRV_ERROR :

******************************************************************************/
static PVRSRV_ERROR SGXUpdateTimingInfo(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
#if defined(SGX_DYNAMIC_TIMING_INFO)
	SGX_TIMING_INFORMATION	sSGXTimingInfo = {0};
#else
	SGX_DEVICE_MAP		*psSGXDeviceMap;
#endif
	IMG_UINT32		ui32ActivePowManSampleRate;
	SGX_TIMING_INFORMATION	*psSGXTimingInfo;


#if defined(SGX_DYNAMIC_TIMING_INFO)
	psSGXTimingInfo = &sSGXTimingInfo;
	SysGetSGXTimingInformation(psSGXTimingInfo);
#else
	SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
						  (IMG_VOID**)&psSGXDeviceMap);
	psSGXTimingInfo = &psSGXDeviceMap->sTimingInfo;
#endif

#if defined(SUPPORT_HW_RECOVERY)
	{
		PVRSRV_ERROR			eError;
		IMG_UINT32	ui32OlduKernelFreq;

		if (psDevInfo->hTimer != IMG_NULL)
		{
			ui32OlduKernelFreq = psDevInfo->ui32CoreClockSpeed / psDevInfo->ui32uKernelTimerClock;
			if (ui32OlduKernelFreq != psSGXTimingInfo->ui32uKernelFreq)
			{
				/*
					The ukernel timer frequency has changed.
				*/
				IMG_HANDLE hNewTimer;
				
				eError = SGXAddTimer(psDeviceNode, psSGXTimingInfo, &hNewTimer);
				if (eError == PVRSRV_OK)
				{
					eError = OSRemoveTimer(psDevInfo->hTimer);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,"SGXUpdateTimingInfo: Failed to remove timer"));
					}
					psDevInfo->hTimer = hNewTimer;
				}
				else
				{
					/* Failed to allocate the new timer, leave the old one. */
				}
			}
		}
		else
		{
			eError = SGXAddTimer(psDeviceNode, psSGXTimingInfo, &psDevInfo->hTimer);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}

		psDevInfo->psSGXHostCtl->ui32HWRecoverySampleRate =
			psSGXTimingInfo->ui32uKernelFreq / psSGXTimingInfo->ui32HWRecoveryFreq;
	}
#endif /* SUPPORT_HW_RECOVERY*/

	/* Copy the SGX clock speed for use in the kernel */
	psDevInfo->ui32CoreClockSpeed = psSGXTimingInfo->ui32CoreClockSpeed;
	psDevInfo->ui32uKernelTimerClock = psSGXTimingInfo->ui32CoreClockSpeed / psSGXTimingInfo->ui32uKernelFreq;

	/* FIXME: no need to duplicate - remove it from psDevInfo */
	psDevInfo->psSGXHostCtl->ui32uKernelTimerClock = psDevInfo->ui32uKernelTimerClock;
#if defined(PDUMP)
	PDUMPCOMMENT("Host Control - Microkernel clock");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(SGXMKIF_HOST_CTL, ui32uKernelTimerClock),
			 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif /* PDUMP */

	if (psSGXTimingInfo->bEnableActivePM)
	{
		ui32ActivePowManSampleRate =
			psSGXTimingInfo->ui32uKernelFreq * psSGXTimingInfo->ui32ActivePowManLatencyms / 1000;
		/*
			ui32ActivePowerCounter has the value 0 when SGX is not idle.
			When SGX becomes idle, the value of ui32ActivePowerCounter is changed from 0 to ui32ActivePowManSampleRate.
			The ukernel timer routine decrements the value of ui32ActivePowerCounter if it is not 0.
			When the ukernel timer decrements ui32ActivePowerCounter from 1 to 0, the ukernel timer will
				request power down.
			Therefore the minimum value of ui32ActivePowManSampleRate is 1.
		*/
		ui32ActivePowManSampleRate += 1;
	}
	else
	{
		ui32ActivePowManSampleRate = 0;
	}

	psDevInfo->psSGXHostCtl->ui32ActivePowManSampleRate = ui32ActivePowManSampleRate;
#if defined(PDUMP)
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(SGXMKIF_HOST_CTL, ui32ActivePowManSampleRate),
			 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif /* PDUMP */

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SGXStartTimer

 @Description

	Start the microkernel timer

 @Input	   psDevInfo : SGX Device Info

 @Return   IMG_VOID :

******************************************************************************/
static IMG_VOID SGXStartTimer(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	#if defined(SUPPORT_HW_RECOVERY)
	PVRSRV_ERROR	eError;

	eError = OSEnableTimer(psDevInfo->hTimer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXStartTimer : Failed to enable host timer"));
	}
	#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	#endif /* SUPPORT_HW_RECOVERY */
}


/*!
******************************************************************************

 @Function	SGXPollForClockGating

 @Description

 	Wait until the SGX core clocks have gated.

 @Input	   psDevInfo : SGX Device Info
 @Input	   ui32Register : Offset of register to poll
 @Input	   ui32Register : Value of register to poll for
 @Input	   pszComment : Description of poll

 @Return   IMG_VOID :

******************************************************************************/
static IMG_VOID SGXPollForClockGating (PVRSRV_SGXDEV_INFO	*psDevInfo,
									   IMG_UINT32			ui32Register,
									   IMG_UINT32			ui32RegisterValue,
									   IMG_CHAR				*pszComment)
{
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(ui32Register);
	PVR_UNREFERENCED_PARAMETER(ui32RegisterValue);
	PVR_UNREFERENCED_PARAMETER(pszComment);

	#if !defined(NO_HARDWARE)
	PVR_ASSERT(psDevInfo != IMG_NULL);

	/* PRQA S 0505 1 */ /* QAC does not like assert() */
	if (PollForValueKM((IMG_UINT32 *)psDevInfo->pvRegsBaseKM + (ui32Register >> 2),
						0,
						ui32RegisterValue,
						MAX_HW_TIME_US,
						MAX_HW_TIME_US/WAIT_TRY_COUNT,
						IMG_FALSE) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "SGXPollForClockGating: %s failed.", pszComment));
	}
	#endif /* NO_HARDWARE */

	PDUMPCOMMENT("%s", pszComment);
	PDUMPREGPOL(SGX_PDUMPREG_NAME, ui32Register, 0, ui32RegisterValue, PDUMP_POLL_OPERATOR_EQUAL);
}


/*!
******************************************************************************

 @Function	SGXPrePowerState

 @Description

 does necessary preparation before power state transition

 @Input	   hDevHandle : SGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SGXPrePowerState (IMG_HANDLE				hDevHandle,
							   PVRSRV_DEV_POWER_STATE	eNewPowerState,
							   PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eNewPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		IMG_UINT32			ui32PowerCmd, ui32CompleteStatus;
		SGXMKIF_COMMAND		sCommand = {0};
		IMG_UINT32			ui32Core;
		IMG_UINT32			ui32CoresEnabled;

		#if defined(SUPPORT_HW_RECOVERY)
		/* Disable timer callback for HW recovery */
		eError = OSDisableTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Failed to disable timer"));
			return eError;
		}
		#endif /* SUPPORT_HW_RECOVERY */

		if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/* Request the ukernel to idle SGX and save its state. */
			ui32PowerCmd = PVRSRV_POWERCMD_POWEROFF;
			ui32CompleteStatus = PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE;
			PDUMPCOMMENT("SGX power off request");
		}
		else
		{
			/* Request the ukernel to idle SGX. */
			ui32PowerCmd = PVRSRV_POWERCMD_IDLE;
			ui32CompleteStatus = PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE;
			PDUMPCOMMENT("SGX idle request");
		}

		sCommand.ui32Data[1] = ui32PowerCmd;

		eError = SGXScheduleCCBCommand(psDeviceNode, SGXMKIF_CMD_POWER, &sCommand, KERNEL_ID, 0, IMG_NULL, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Failed to submit power down command"));
			return eError;
		}

		/* Wait for the ukernel to complete processing. */
		#if !defined(NO_HARDWARE)
		if (PollForValueKM(&psDevInfo->psSGXHostCtl->ui32PowerStatus,
							ui32CompleteStatus,
							ui32CompleteStatus,
							MAX_HW_TIME_US,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Wait for SGX ukernel power transition failed."));
			SGXDumpDebugInfo(psDevInfo, IMG_TRUE);
			PVR_DBG_BREAK;
		}
		#endif /* NO_HARDWARE */

		#if defined(PDUMP)
		PDUMPCOMMENT("TA/3D CCB Control - Wait for power event on uKernel.");
		PDUMPMEMPOL(psDevInfo->psKernelSGXHostCtlMemInfo,
					offsetof(SGXMKIF_HOST_CTL, ui32PowerStatus),
					ui32CompleteStatus,
					ui32CompleteStatus,
					PDUMP_POLL_OPERATOR_EQUAL,
					0,
					MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif /* PDUMP */

		/* Wait for the pending ukernel to host interrupts to come back. */
		#if !defined(NO_HARDWARE)
		if (g_ui32HostIRQCountSample != psDevInfo->psSGXHostCtl->ui32InterruptCount)
			PVR_LOG(("%s: IRQ Count - %d, Interrupt Count - %d.", __func__,
							g_ui32HostIRQCountSample, psDevInfo->psSGXHostCtl->ui32InterruptCount));
		if (PollForValueKM(&g_ui32HostIRQCountSample,
							psDevInfo->psSGXHostCtl->ui32InterruptCount,
							0xffffffff,
							MAX_HW_TIME_US,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Wait for pending interrupts failed."));
			SGXDumpDebugInfo(psDevInfo, IMG_TRUE);
			PVR_DBG_BREAK;
		}
		#if defined(EUR_CR_USE0_DM_SLOT)
		if (PollForValueKM((IMG_UINT32 *)psDevInfo->pvRegsBaseKM + (SGX_MP_CORE_SELECT(EUR_CR_USE0_DM_SLOT, 0) >> 2),
						0xAAAAAAAA,
						0xFFFFFFFF,
						MAX_HW_TIME_US,
						MAX_HW_TIME_US/WAIT_TRY_COUNT,
						IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Wait for USE to be idle."));
			SGXDumpDebugInfo(psDevInfo, IMG_FALSE);
			PVR_DBG_BREAK;
		}
		#else
		if (PollForValueKM((IMG_UINT32 *)psDevInfo->pvRegsBaseKM + (SGX_MP_CORE_SELECT(EUR_CR_USE1_DM_SLOT, 0) >> 2),
						0xAAAAAAAA,
						0xFFFFFFFF,
						MAX_HW_TIME_US,
						MAX_HW_TIME_US/WAIT_TRY_COUNT,
						IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Wait for USE to be idle."));
			SGXDumpDebugInfo(psDevInfo, IMG_FALSE);
			PVR_DBG_BREAK;
		}
		#endif /* EUR_CR_USE0_DM_SLOT */
		#endif /* NO_HARDWARE */
#if defined(SGX_FEATURE_MP)
		ui32CoresEnabled = ((OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_CORE) & EUR_CR_MASTER_CORE_ENABLE_MASK) >> EUR_CR_MASTER_CORE_ENABLE_SHIFT) + 1;
#else
		ui32CoresEnabled = 1;
#endif

		for (ui32Core = 0; ui32Core < ui32CoresEnabled; ui32Core++)
		{
			/* Wait for SGX clock gating. */
			SGXPollForClockGating(psDevInfo,
								  SGX_MP_CORE_SELECT(psDevInfo->ui32ClkGateStatusReg, ui32Core),
								  psDevInfo->ui32ClkGateStatusMask,
								  "Wait for SGX clock gating");
		}

		#if defined(SGX_FEATURE_MP)
		/* Wait for SGX master clock gating. */
		SGXPollForClockGating(psDevInfo,
							  psDevInfo->ui32MasterClkGateStatusReg,
							  psDevInfo->ui32MasterClkGateStatusMask,
							  "Wait for SGX master clock gating");

		SGXPollForClockGating(psDevInfo,
							  psDevInfo->ui32MasterClkGateStatus2Reg,
							  psDevInfo->ui32MasterClkGateStatus2Mask,
							  "Wait for SGX master clock gating (2)");
		#endif /* SGX_FEATURE_MP */

		if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/* Finally, de-initialise some registers. */
			eError = SGXDeinitialise(psDevInfo);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: SGXDeinitialise failed: %u", eError));
				return eError;
			}
		}
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SGXPostPowerState

 @Description

 does necessary preparation after power state transition

 @Input	   hDevHandle : SGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SGXPostPowerState (IMG_HANDLE				hDevHandle,
								PVRSRV_DEV_POWER_STATE	eNewPowerState,
								PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		SGXMKIF_HOST_CTL *psSGXHostCtl = psDevInfo->psSGXHostCtl;

		/* Reset the power manager flags. */
		psSGXHostCtl->ui32PowerStatus = 0;
		#if defined(PDUMP)
		PDUMPCOMMENT("Host Control - Reset power status");
		PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
				 offsetof(SGXMKIF_HOST_CTL, ui32PowerStatus),
				 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif /* PDUMP */

		if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/*
				Coming up from off, re-initialise SGX.
			*/

			/*
				Re-generate the timing data required by SGX.
			*/
			eError = SGXUpdateTimingInfo(psDeviceNode);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState: SGXUpdateTimingInfo failed"));
				return eError;
			}

			/*
				Run the SGX init script.
			*/
			eError = SGXInitialise(psDevInfo, IMG_FALSE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState: SGXInitialise failed"));
				return eError;
			}
		}
		else
		{
			/*
				Coming up from idle, restart the ukernel.
			*/
			SGXMKIF_COMMAND		sCommand = {0};

			sCommand.ui32Data[1] = PVRSRV_POWERCMD_RESUME;
			eError = SGXScheduleCCBCommand(psDeviceNode, SGXMKIF_CMD_POWER, &sCommand, ISR_ID, 0, IMG_NULL, IMG_FALSE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState failed to schedule CCB command: %u", eError));
				return eError;
			}
		}

		SGXStartTimer(psDevInfo);
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SGXPreClockSpeedChange

 @Description

	Does processing required before an SGX clock speed change.

 @Input	   hDevHandle : SGX Device Node
 @Input	   bIdleDevice : Whether the microkernel needs to be idled
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SGXPreClockSpeedChange (IMG_HANDLE				hDevHandle,
									 IMG_BOOL				bIdleDevice,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON)
	{
		if (bIdleDevice)
		{
			/*
			 * Idle SGX.
			 */
			PDUMPSUSPEND();

			eError = SGXPrePowerState(hDevHandle, PVRSRV_DEV_POWER_STATE_IDLE,
									  PVRSRV_DEV_POWER_STATE_ON);

			if (eError != PVRSRV_OK)
			{
				PDUMPRESUME();
				return eError;
			}
		}
		else
		{
			#if defined(SUPPORT_HW_RECOVERY)
			PVRSRV_ERROR	eError;

			eError = OSDisableTimer(psDevInfo->hTimer);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXStartTimer : Failed to enable host timer"));
			}
			#endif /* SUPPORT_HW_RECOVERY */
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE,"SGXPreClockSpeedChange: SGX clock speed was %uHz",
			psDevInfo->ui32CoreClockSpeed));

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SGXPostClockSpeedChange

 @Description

	Does processing required after an SGX clock speed change.

 @Input	   hDevHandle : SGX Device Node
 @Input	   bIdleDevice : Whether the microkernel had been idled previously
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SGXPostClockSpeedChange (IMG_HANDLE				hDevHandle,
									  IMG_BOOL					bIdleDevice,
									  PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32			ui32OldClockSpeed = psDevInfo->ui32CoreClockSpeed;

	PVR_UNREFERENCED_PARAMETER(ui32OldClockSpeed);

	if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON)
	{
		PVRSRV_ERROR eError;

		/*
			Re-generate the timing data required by SGX.
		*/
		eError = SGXUpdateTimingInfo(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState: SGXUpdateTimingInfo failed"));
			return eError;
		}

		if (bIdleDevice)
		{
			/*
			 * Resume SGX.
			 */
			eError = SGXPostPowerState(hDevHandle, PVRSRV_DEV_POWER_STATE_ON,
									   PVRSRV_DEV_POWER_STATE_IDLE);

			PDUMPRESUME();

			if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}
		else
		{
			SGXStartTimer(psDevInfo);
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE,"SGXPostClockSpeedChange: SGX clock speed changed from %uHz to %uHz",
			ui32OldClockSpeed, psDevInfo->ui32CoreClockSpeed));

	return PVRSRV_OK;
}


/******************************************************************************
 End of file (sgxpower.c)
******************************************************************************/
