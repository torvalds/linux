/*************************************************************************/ /*!
@File           vz_validation.c
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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

#include "sysconfig.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "vz_validation.h"
#include "virt_validation_defs.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
typedef struct _VALIDATION_CTRL_
{
	IMG_BOOL	bUnload;
	IMG_HANDLE	hMPUWatchdogThread;
	IMG_HANDLE	hSystemWatchdogEvObj;
} VALIDATION_CTRL;

static void SysPrintAndResetFaultStatusRegister(void)
{
	IMG_CPU_PHYADDR sSoCRegBase = {SOC_REGBANK_BASE};
	IMG_UINT32 ui32MPUEventStatus;

	void* pvSocRegs = OSMapPhysToLin(sSoCRegBase,
	                                 SOC_REGBANK_SIZE,
	                                 PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

	ui32MPUEventStatus = OSReadHWReg32(pvSocRegs, MPU_EVENT_STATUS_REG);

	if (ui32MPUEventStatus & MPU_GPU_BUS_REQUESTER)
	{
		IMG_UINT32 ui32MPUEventOSID      = OSReadHWReg32(pvSocRegs, MPU_EVENT_OSID_REG);
		IMG_UINT32 ui32MPUEventAddress   = OSReadHWReg32(pvSocRegs, MPU_EVENT_ADDRESS_REG);
		IMG_UINT32 ui32MPUEventDirection = OSReadHWReg32(pvSocRegs, MPU_EVENT_DIRECTION_REG);

		PVR_DPF((PVR_DBG_ERROR, "MPU fault event: GPU attempted an illegal %s access to address 0x%08X while emitting OSID %u",
		                        (ui32MPUEventDirection == MPU_WRITE_ACCESS) ? "WRITE" : "READ",
		                        ui32MPUEventAddress,
		                        ui32MPUEventOSID));

		OSWriteHWReg32(pvSocRegs, MPU_EVENT_CLEAR_REG, 1);
	}

	OSUnMapPhysToLin(pvSocRegs, SOC_REGBANK_SIZE);
}

void SysInitValidation(IMG_HANDLE hSysData,
                       IMG_UINT64 aui64OSidMin[GPUVIRT_VALIDATION_NUM_REGIONS][GPUVIRT_VALIDATION_NUM_OS],
                       IMG_UINT64 aui64OSidMax[GPUVIRT_VALIDATION_NUM_REGIONS][GPUVIRT_VALIDATION_NUM_OS])
{
	IMG_CPU_PHYADDR sSoCRegBase = {SOC_REGBANK_BASE};
	IMG_UINT32 ui32OSID, ui32Region;

	void* pvSocRegs = OSMapPhysToLin(sSoCRegBase,
	                                 SOC_REGBANK_SIZE,
	                                 PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

	IMG_UINT64 aui64RegMin[GPUVIRT_VALIDATION_MAX_OS][GPUVIRT_VALIDATION_NUM_REGIONS] =
	{
		{MPU_PROTECTED_RANGE0_START_REG, MPU_PROTECTED_RANGE8_START_REG},
		{MPU_PROTECTED_RANGE1_START_REG, MPU_PROTECTED_RANGE9_START_REG},
		{MPU_PROTECTED_RANGE2_START_REG, MPU_PROTECTED_RANGE10_START_REG},
		{MPU_PROTECTED_RANGE3_START_REG, MPU_PROTECTED_RANGE11_START_REG},
		{MPU_PROTECTED_RANGE4_START_REG, MPU_PROTECTED_RANGE12_START_REG},
		{MPU_PROTECTED_RANGE5_START_REG, MPU_PROTECTED_RANGE13_START_REG},
		{MPU_PROTECTED_RANGE6_START_REG, MPU_PROTECTED_RANGE14_START_REG},
		{MPU_PROTECTED_RANGE7_START_REG, MPU_PROTECTED_RANGE15_START_REG}
	};

	IMG_UINT64 aui64RegMax[GPUVIRT_VALIDATION_MAX_OS][GPUVIRT_VALIDATION_NUM_REGIONS] =
	{
		{MPU_PROTECTED_RANGE0_END_REG, MPU_PROTECTED_RANGE8_END_REG},
		{MPU_PROTECTED_RANGE1_END_REG, MPU_PROTECTED_RANGE9_END_REG},
		{MPU_PROTECTED_RANGE2_END_REG, MPU_PROTECTED_RANGE10_END_REG},
		{MPU_PROTECTED_RANGE3_END_REG, MPU_PROTECTED_RANGE11_END_REG},
		{MPU_PROTECTED_RANGE4_END_REG, MPU_PROTECTED_RANGE12_END_REG},
		{MPU_PROTECTED_RANGE5_END_REG, MPU_PROTECTED_RANGE13_END_REG},
		{MPU_PROTECTED_RANGE6_END_REG, MPU_PROTECTED_RANGE14_END_REG},
		{MPU_PROTECTED_RANGE7_END_REG, MPU_PROTECTED_RANGE15_END_REG}
	};

	IMG_UINT64 aui64RangeOSIDAccessReg[] =
	{
		MPU_PROTECTED_RANGE0_OSID_REG,
		MPU_PROTECTED_RANGE1_OSID_REG,
		MPU_PROTECTED_RANGE2_OSID_REG,
		MPU_PROTECTED_RANGE3_OSID_REG,
		MPU_PROTECTED_RANGE4_OSID_REG,
		MPU_PROTECTED_RANGE5_OSID_REG,
		MPU_PROTECTED_RANGE6_OSID_REG,
		MPU_PROTECTED_RANGE7_OSID_REG,
		MPU_PROTECTED_RANGE8_OSID_REG,
		MPU_PROTECTED_RANGE9_OSID_REG,
		MPU_PROTECTED_RANGE10_OSID_REG,
		MPU_PROTECTED_RANGE11_OSID_REG,
		MPU_PROTECTED_RANGE12_OSID_REG,
		MPU_PROTECTED_RANGE13_OSID_REG,
		MPU_PROTECTED_RANGE14_OSID_REG,
		MPU_PROTECTED_RANGE15_OSID_REG
	};

	PVR_UNREFERENCED_PARAMETER(hSysData);

	FOREACH_VALIDATION_OSID(ui32OSID)
	{
		/* every OSID gets access to 2 ranges: one secure range and one shared with all the other OSIDs
		 * e.g. OSID 0: secure RANGE 0 & shared RANGE 8 */
		OSWriteHWReg64(pvSocRegs, aui64RangeOSIDAccessReg[ui32OSID], ui32OSID);
		OSWriteHWReg64(pvSocRegs, aui64RangeOSIDAccessReg[ui32OSID+GPUVIRT_VALIDATION_MAX_OS], ui32OSID);

		for (ui32Region=0; ui32Region < GPUVIRT_VALIDATION_NUM_REGIONS; ui32Region++)
		{
			OSWriteHWReg64(pvSocRegs, aui64RegMin[ui32OSID][ui32Region], aui64OSidMin[ui32Region][ui32OSID]);
			OSWriteHWReg64(pvSocRegs, aui64RegMax[ui32OSID][ui32Region], aui64OSidMax[ui32Region][ui32OSID]);
		}
	}

	OSWriteHWReg32(pvSocRegs, MPU_PROTECTION_ENABLE_REG, 1);

	OSUnMapPhysToLin(pvSocRegs, SOC_REGBANK_SIZE);
}

static void MPUWatchdogThread(void *pvData)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hOSEvent;
	IMG_UINT32 ui32Timeout = DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT;
	VALIDATION_CTRL *psValidCtrl = (VALIDATION_CTRL*)pvData;

	if (psValidCtrl == NULL)
	{
		return;
	}

	/* Open an event on the system watchdog event object so we can listen on it
	   and abort the system watchdog thread. */
	eError = OSEventObjectOpen(psValidCtrl->hSystemWatchdogEvObj, &hOSEvent);
	PVR_LOG_RETURN_VOID_IF_ERROR(eError, "OSEventObjectOpen");

	while (!psValidCtrl->bUnload)
	{
		eError = OSEventObjectWaitKernel(hOSEvent, (IMG_UINT64)ui32Timeout * 1000);
		if (eError == PVRSRV_OK)
		{
			if (psValidCtrl->bUnload)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "%s: Shutdown event received.", __func__));
				break;
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE, "%s: Power state change event received.", __func__));
			}
		}
		else if (eError != PVRSRV_ERROR_TIMEOUT)
		{
			/* If timeout do nothing otherwise print warning message. */
			PVR_DPF((PVR_DBG_ERROR, "%s: "
					"Error (%d) when waiting for event!", __func__, eError));
		}

		SysPrintAndResetFaultStatusRegister();
	}
	eError = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");
}

PVRSRV_ERROR CreateMPUWatchdogThread(IMG_HANDLE *phValidationData)
{
	VALIDATION_CTRL *psValidCtrl;
	PVRSRV_ERROR eError;

	psValidCtrl = OSAllocZMem(sizeof(*psValidCtrl));
	PVR_LOG_RETURN_IF_NOMEM(psValidCtrl, "psValidCtrl");

	/* Create the SysWatchdog Event Object */
	eError = OSEventObjectCreate("PVRSRV_SYSWDG_EVENTOBJECT", &psValidCtrl->hSystemWatchdogEvObj);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", error_eventobject);

	/* Create a thread which is used to detect fatal errors */
	eError = OSThreadCreatePriority(&psValidCtrl->hMPUWatchdogThread,
	                       "emu_check_fault_wdg",
	                       MPUWatchdogThread,
	                       NULL,
	                       IMG_TRUE,
	                       psValidCtrl,
	                       0);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSThreadCreatePriority", error_threadcreate);

	*phValidationData = (IMG_HANDLE)psValidCtrl;

	return PVRSRV_OK;

error_threadcreate:
	OSEventObjectDestroy(psValidCtrl->hSystemWatchdogEvObj);
error_eventobject:
	OSFreeMem(psValidCtrl);

	return eError;
}

void DestroyMPUWatchdogThread(IMG_HANDLE hValidationData)
{
	VALIDATION_CTRL *psValidCtrl = (VALIDATION_CTRL*)hValidationData;

	if (psValidCtrl == NULL)
	{
		return;
	}

	psValidCtrl->bUnload = IMG_TRUE;

	if (psValidCtrl->hMPUWatchdogThread)
	{
		if (psValidCtrl->hSystemWatchdogEvObj)
		{
			OSEventObjectSignal(psValidCtrl->hSystemWatchdogEvObj);
		}
		LOOP_UNTIL_TIMEOUT(OS_THREAD_DESTROY_TIMEOUT_US)
		{
			OSThreadDestroy(psValidCtrl->hMPUWatchdogThread);
			psValidCtrl->hMPUWatchdogThread = NULL;
			OSWaitus(OS_THREAD_DESTROY_TIMEOUT_US/OS_THREAD_DESTROY_RETRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
	}

	if (psValidCtrl->hSystemWatchdogEvObj)
	{
		OSEventObjectDestroy(psValidCtrl->hSystemWatchdogEvObj);
		psValidCtrl->hSystemWatchdogEvObj = NULL;
	}

	OSFreeMem(psValidCtrl);
}

#endif
