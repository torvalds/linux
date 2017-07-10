/*************************************************************************/ /*!
@File
@Title          Device specific utility routines
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

#include "rgx_fwif_km.h"
#include "pdump_km.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "power.h"
#include "pvrsrv.h"
#include "sync_internal.h"
#include "rgxfwutils.h"


PVRSRV_ERROR RGXQueryAPMState(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_UINT32 *pui32State)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = psDeviceNode->pvDevice;
	*pui32State = psDevInfo->eActivePMConf;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXSetAPMState(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_UINT32 ui32State)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	if (RGX_ACTIVEPM_FORCE_OFF != ui32State
		|| !psDevInfo->pvAPMISRData)
	{
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

#if !defined(NO_HARDWARE)
	eError = OSUninstallMISR(psDevInfo->pvAPMISRData);
	if (PVRSRV_OK == eError)
	{
		psDevInfo->eActivePMConf = RGX_ACTIVEPM_FORCE_OFF;
		psDevInfo->pvAPMISRData = NULL;
		eError = PVRSRVSetDeviceDefaultPowerState(psDeviceNode,
		                                          PVRSRV_DEV_POWER_STATE_ON);
	}
#endif

	return eError;
}

PVRSRV_ERROR RGXQueryPdumpPanicEnable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL *pbEnabled)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	*pbEnabled = psDevInfo->bPDPEnabled;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXSetPdumpPanicEnable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL bEnable)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	psDevInfo->bPDPEnabled = bEnable;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXGetDeviceFlags(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 *pui32DeviceFlags)
{
	if (!pui32DeviceFlags || !psDevInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32DeviceFlags = psDevInfo->ui32DeviceFlags;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXSetDeviceFlags(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 ui32Config,
				IMG_BOOL bSetNotClear)
{
	IMG_UINT32 ui32DeviceFlags = 0;

	if (!psDevInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32Config & RGXKMIF_DEVICE_STATE_ZERO_FREELIST)
	{
		ui32DeviceFlags |= RGXKM_DEVICE_STATE_ZERO_FREELIST;
	}

	if (ui32Config & RGXKMIF_DEVICE_STATE_DISABLE_DW_LOGGING_EN)
	{
		ui32DeviceFlags |= RGXKM_DEVICE_STATE_DISABLE_DW_LOGGING_EN;
	}

	if (ui32Config & RGXKMIF_DEVICE_STATE_DUST_REQUEST_INJECT_EN)
	{
		ui32DeviceFlags |= RGXKM_DEVICE_STATE_DUST_REQUEST_INJECT_EN;
	}

	if (bSetNotClear)
	{
		psDevInfo->ui32DeviceFlags |= ui32DeviceFlags;
	}
	else
	{
		psDevInfo->ui32DeviceFlags &= ~ui32DeviceFlags;
	}

	return PVRSRV_OK;
}

/*
 * RGXRunScript
 */
PVRSRV_ERROR RGXRunScript(PVRSRV_RGXDEV_INFO *psDevInfo,
				RGX_INIT_COMMAND *psScript,
				IMG_UINT32 ui32NumCommands,
				IMG_UINT32 ui32PdumpFlags,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	IMG_UINT32 ui32PC;
#if !defined(NO_HARDWARE)
	IMG_UINT32 ui32LastLoopPoint = 0xFFFFFFFF;
#endif /* NO_HARDWARE */

	for (ui32PC = 0;  ui32PC < ui32NumCommands;  ui32PC++)
	{
		RGX_INIT_COMMAND *psComm = &psScript[ui32PC];

		switch (psComm->eOp)
		{
			case RGX_INIT_OP_DBG_READ32_HW_REG:
			{
				IMG_UINT32	ui32RegVal;
				ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM,  psComm->sDBGReadHWReg.ui32Offset);
				PVR_DUMPDEBUG_LOG("%s: 0x%08X", psComm->sDBGReadHWReg.aszName, ui32RegVal);
				break;
			}
			case RGX_INIT_OP_DBG_READ64_HW_REG:
			{
				IMG_UINT64	ui64RegVal;
				ui64RegVal = OSReadHWReg64(psDevInfo->pvRegsBaseKM, psComm->sDBGReadHWReg.ui32Offset);
				PVR_DUMPDEBUG_LOG("%s: 0x%016llX", psComm->sDBGReadHWReg.aszName, ui64RegVal);
				break;
			}
			case RGX_INIT_OP_WRITE_HW_REG:
			{
				if( !(ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
				{
					OSWriteHWReg32(psDevInfo->pvRegsBaseKM, psComm->sWriteHWReg.ui32Offset, psComm->sWriteHWReg.ui32Value);
				}
				PDUMPCOMMENT("RGXRunScript: Write HW reg operation");
				PDUMPREG32(RGX_PDUMPREG_NAME,
						psComm->sWriteHWReg.ui32Offset,
						psComm->sWriteHWReg.ui32Value,
						ui32PdumpFlags);
				break;
			}
			case RGX_INIT_OP_PDUMP_HW_REG:
			{
				PDUMPCOMMENT("RGXRunScript: Dump HW reg operation");
				PDUMPREG32(RGX_PDUMPREG_NAME, psComm->sPDumpHWReg.ui32Offset,
						psComm->sPDumpHWReg.ui32Value, ui32PdumpFlags);
				break;
			}
			case RGX_INIT_OP_COND_POLL_HW_REG:
			{
#if !defined(NO_HARDWARE)
				IMG_UINT32	ui32RegVal;

				if( !(ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
				{
					/* read the register used as condition */
					ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM,  psComm->sCondPollHWReg.ui32CondOffset);

					/* if the conditions succeeds, poll the register */
					if ((ui32RegVal & psComm->sCondPollHWReg.ui32CondMask) == psComm->sCondPollHWReg.ui32CondValue)
					{
						if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + psComm->sCondPollHWReg.ui32Offset),
								psComm->sCondPollHWReg.ui32Value,
								psComm->sCondPollHWReg.ui32Mask) != PVRSRV_OK)
						{
							PVR_DPF((PVR_DBG_ERROR, "RGXRunScript: Cond Poll for Reg (0x%x) failed -> Cancel script.", psComm->sCondPollHWReg.ui32Offset));
							return PVRSRV_ERROR_TIMEOUT;
						}

					}
					else
					{
						PVR_DPF((PVR_DBG_WARNING, 
						"RGXRunScript: Skipping Poll for Reg (0x%x) because the condition is not met (Reg 0x%x ANDed with mask 0x%x equal to 0x%x but value 0x%x found instead).",
						psComm->sCondPollHWReg.ui32Offset,
						psComm->sCondPollHWReg.ui32CondOffset,
						psComm->sCondPollHWReg.ui32CondMask,
						psComm->sCondPollHWReg.ui32CondValue,
						ui32RegVal));
					}
				}
#endif
				break;
			}
			case RGX_INIT_OP_POLL_64_HW_REG:
			{
				/* Split lower and upper words */
				IMG_UINT32 ui32UpperValue = (IMG_UINT32) (psComm->sPoll64HWReg.ui64Value >> 32);
				IMG_UINT32 ui32LowerValue = (IMG_UINT32) (psComm->sPoll64HWReg.ui64Value);

				IMG_UINT32 ui32UpperMask = (IMG_UINT32) (psComm->sPoll64HWReg.ui64Mask >> 32);
				IMG_UINT32 ui32LowerMask = (IMG_UINT32) (psComm->sPoll64HWReg.ui64Mask);

				PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "RGXRunScript: 64 bit HW offset: %x", psComm->sPoll64HWReg.ui32Offset);

				if( !(ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
				{
					if (PVRSRVPollForValueKM((IMG_UINT32 *)(((IMG_UINT8*)psDevInfo->pvRegsBaseKM) + psComm->sPoll64HWReg.ui32Offset + 4),
										 ui32UpperValue,
										 ui32UpperMask) != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR, "RGXRunScript: Poll for upper part of Reg (0x%x) failed -> Cancel script.", psComm->sPoll64HWReg.ui32Offset));
						return PVRSRV_ERROR_TIMEOUT;
					}
				}
				PDUMPREGPOL(RGX_PDUMPREG_NAME,
							psComm->sPoll64HWReg.ui32Offset + 4,
							ui32UpperValue,
							ui32UpperMask,
							ui32PdumpFlags,
							PDUMP_POLL_OPERATOR_EQUAL);

				if( !(ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
				{
					if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + psComm->sPoll64HWReg.ui32Offset),
										 ui32LowerValue,
										 ui32LowerMask) != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR, "RGXRunScript: Poll for lower part of Reg (0x%x) failed -> Cancel script.", psComm->sPoll64HWReg.ui32Offset));
						return PVRSRV_ERROR_TIMEOUT;
					}
				}
				PDUMPREGPOL(RGX_PDUMPREG_NAME,
							psComm->sPoll64HWReg.ui32Offset,
							ui32LowerValue,
							ui32LowerMask,
							ui32PdumpFlags,
							PDUMP_POLL_OPERATOR_EQUAL);

				break;
			}
			case RGX_INIT_OP_POLL_HW_REG:
			{
				if( !(ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
				{
					if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + psComm->sPollHWReg.ui32Offset),
										 psComm->sPollHWReg.ui32Value,
										 psComm->sPollHWReg.ui32Mask) != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR, "RGXRunScript: Poll for Reg (0x%x) failed -> Cancel script.", psComm->sPollHWReg.ui32Offset));
						return PVRSRV_ERROR_TIMEOUT;
					}
				}
				PDUMPREGPOL(RGX_PDUMPREG_NAME,
							psComm->sPollHWReg.ui32Offset,
							psComm->sPollHWReg.ui32Value,
							psComm->sPollHWReg.ui32Mask,
							ui32PdumpFlags,
							PDUMP_POLL_OPERATOR_EQUAL);

				break;
			}

			case RGX_INIT_OP_LOOP_POINT:
			{
#if !defined(NO_HARDWARE)
				ui32LastLoopPoint = ui32PC;
#endif /* NO_HARDWARE */
				break;
			}

			case RGX_INIT_OP_COND_BRANCH:
			{
#if !defined(NO_HARDWARE)
				IMG_UINT32 ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM,
													  psComm->sConditionalBranchPoint.ui32Offset);

				if((ui32RegVal & psComm->sConditionalBranchPoint.ui32Mask) != psComm->sConditionalBranchPoint.ui32Value)
				{
					ui32PC = ui32LastLoopPoint - 1;
				}
#endif /* NO_HARDWARE */

				PDUMPIDLWITHFLAGS(30, ui32PdumpFlags);
				break;
			}
			case RGX_INIT_OP_DBG_CALC:
			{
				IMG_UINT32 ui32RegVal1;
				IMG_UINT32 ui32RegVal2;
				IMG_UINT32 ui32RegVal3;
				ui32RegVal1 = OSReadHWReg32(psDevInfo->pvRegsBaseKM,  psComm->sDBGCalc.ui32Offset1);
				ui32RegVal2 = OSReadHWReg32(psDevInfo->pvRegsBaseKM,  psComm->sDBGCalc.ui32Offset2);
				ui32RegVal3 = OSReadHWReg32(psDevInfo->pvRegsBaseKM,  psComm->sDBGCalc.ui32Offset3);
				if (ui32RegVal1 + ui32RegVal2 > ui32RegVal3)
				{
					PVR_DUMPDEBUG_LOG("%s: 0x%08X", psComm->sDBGCalc.aszName, ui32RegVal1 + ui32RegVal2 - ui32RegVal3);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("%s: 0x%08X", psComm->sDBGCalc.aszName, 0);
				}
				break;
			}
			case RGX_INIT_OP_DBG_WAIT:
			{
				OSWaitus(psComm->sDBGWait.ui32WaitInUs);
				break;
			}
			case RGX_INIT_OP_DBG_STRING:
			{
				PVR_DUMPDEBUG_LOG("%s", psComm->sDBGString.aszString);
				break;
			}
			case RGX_INIT_OP_HALT:
			{
				return PVRSRV_OK;
			}
			case RGX_INIT_OP_ILLEGAL:
			/* FALLTHROUGH */
			default:
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXRunScript: PC %d: Illegal command: %d", ui32PC, psComm->eOp));
				return PVRSRV_ERROR_UNKNOWN_SCRIPT_OPERATION;
			}
		}

	}

	return PVRSRV_ERROR_UNKNOWN_SCRIPT_OPERATION;
}

inline const char * RGXStringifyKickTypeDM(RGX_KICK_TYPE_DM eKickTypeDM)
{
	/*      
	 *  This is based on the currently defined DMs.             
	 *  If you need to modify the enum in include/rgx_common.h
	 *  please keep this function up-to-date too.
	 *
	 *       typedef enum _RGXFWIF_DM_
	 *       {
	 *           RGXFWIF_DM_GP        = 0,
	 *           RGXFWIF_DM_2D        = 1, 
	 *           RGXFWIF_DM_TDM       = 1,
	 *           RGXFWIF_DM_TA        = 2,
	 *           RGXFWIF_DM_3D        = 3,
	 *           RGXFWIF_DM_CDM       = 4,
	 *           RGXFWIF_DM_RTU       = 5,
	 *           RGXFWIF_DM_SHG       = 6,
	 *           RGXFWIF_DM_LAST,
	 *           RGXFWIF_DM_FORCE_I32 = 0x7fffffff   
	 *       } RGXFWIF_DM;
	 */
	PVR_ASSERT(eKickTypeDM < RGX_KICK_TYPE_DM_LAST);

	switch(eKickTypeDM) {
		case RGX_KICK_TYPE_DM_GP:
			return "GP ";
		case RGX_KICK_TYPE_DM_TDM_2D:   
			return "TDM/2D ";
		case RGX_KICK_TYPE_DM_TA:   
			return "TA ";
		case RGX_KICK_TYPE_DM_3D:
			return "3D ";
		case RGX_KICK_TYPE_DM_CDM:
			return "CDM ";
		case RGX_KICK_TYPE_DM_RTU:
			return "RTU ";
		case RGX_KICK_TYPE_DM_SHG:
			return "SHG ";
		case RGX_KICK_TYPE_DM_TQ2D:
			return "TQ2D ";
		case RGX_KICK_TYPE_DM_TQ3D:
			return "TQ3D ";
		default:
			return "Invalid DM ";
	}
}

/******************************************************************************
 End of file (rgxutils.c)
******************************************************************************/
