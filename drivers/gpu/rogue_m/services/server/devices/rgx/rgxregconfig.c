/*************************************************************************/ /*!
@File
@Title          RGX Register configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Regconfig routines
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

#include "rgxregconfig.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "device.h"
#include "sync_internal.h"
#include "pdump_km.h"
#include "pvrsrv.h"
PVRSRV_ERROR PVRSRVRGXSetRegConfigPIKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
					IMG_UINT8              ui8RegPowerIsland)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG          *psRegCfg = &psDevInfo->sRegCongfig;
	RGXFWIF_PWR_EVT		ePowerIsland = (RGXFWIF_PWR_EVT) ui8RegPowerIsland;


	if (ePowerIsland < psRegCfg->ePowerIslandToPush)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Register configuration must be in power island order."));
		return PVRSRV_ERROR_REG_CONFIG_INVALID_PI;
	}

	psRegCfg->ePowerIslandToPush = ePowerIsland;

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION"));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXAddRegConfigKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
					IMG_UINT32		ui32RegAddr,
					IMG_UINT64		ui64RegValue)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG          *psRegCfg = &psDevInfo->sRegCongfig;

	if (psRegCfg->bEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Cannot add record whilst register configuration active."));
		return PVRSRV_ERROR_REG_CONFIG_ENABLED;
	}
	if (psRegCfg->ui32NumRegRecords == RGXFWIF_REG_CFG_MAX_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Register configuration full."));
		return PVRSRV_ERROR_REG_CONFIG_FULL;
	}

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.sRegConfig.ui64Addr = (IMG_UINT64) ui32RegAddr;
	sRegCfgCmd.uCmdData.sRegConfigData.sRegConfig.ui64Value = ui64RegValue;
	sRegCfgCmd.uCmdData.sRegConfigData.eRegConfigPI = psRegCfg->ePowerIslandToPush;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_ADD;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				sizeof(sRegCfgCmd),
				IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXAddRegConfigKM: RGXScheduleCommand failed. Error:%u", eError));
		return eError;
	}

	psRegCfg->ui32NumRegRecords++;

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION"));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXClearRegConfigKM(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG          *psRegCfg = &psDevInfo->sRegCongfig;

	if (psRegCfg->bEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXSetRegConfigPIKM: Attempt to clear register configuration whilst active."));
		return PVRSRV_ERROR_REG_CONFIG_ENABLED;
	}

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_CLEAR;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				sizeof(sRegCfgCmd),
				IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXClearRegConfigKM: RGXScheduleCommand failed. Error:%u", eError));
		return eError;
	}

	psRegCfg->ui32NumRegRecords = 0;
	psRegCfg->ePowerIslandToPush = RGXFWIF_PWR_EVT_PWR_ON; /* Default first PI */

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXClearRegConfigKM: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION"));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXEnableRegConfigKM(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG          *psRegCfg = &psDevInfo->sRegCongfig;

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_ENABLE;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				sizeof(sRegCfgCmd),
				IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXEnableRegConfigKM: RGXScheduleCommand failed. Error:%u", eError));
		return eError;
	}

	psRegCfg->bEnabled = IMG_TRUE;

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXEnableRegConfigKM: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION"));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXDisableRegConfigKM(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD 	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG          *psRegCfg = &psDevInfo->sRegCongfig;

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_DISABLE;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				sizeof(sRegCfgCmd),
				IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXDisableRegConfigKM: RGXScheduleCommand failed. Error:%u", eError));
		return eError;
	}

	psRegCfg->bEnabled = IMG_FALSE;

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXDisableRegConfigKM: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION"));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}


/******************************************************************************
 End of file (rgxregconfig.c)
******************************************************************************/
