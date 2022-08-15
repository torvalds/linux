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

PVRSRV_ERROR PVRSRVRGXSetRegConfigTypeKM(CONNECTION_DATA * psDevConnection,
                                         PVRSRV_DEVICE_NODE	 *psDeviceNode,
                                         IMG_UINT8           ui8RegCfgType)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR          eError      = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO    *psDevInfo  = psDeviceNode->pvDevice;
	RGX_REG_CONFIG        *psRegCfg   = &psDevInfo->sRegCongfig;
	RGXFWIF_REG_CFG_TYPE  eRegCfgType = (RGXFWIF_REG_CFG_TYPE) ui8RegCfgType;

	PVR_UNREFERENCED_PARAMETER(psDevConnection);

	OSLockAcquire(psRegCfg->hLock);

	if (eRegCfgType < psRegCfg->eRegCfgTypeToPush)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Register configuration requested (%d) is not valid since it has to be at least %d."
			 " Configurations of different types need to go in order",
			 __func__,
			 eRegCfgType,
			 psRegCfg->eRegCfgTypeToPush));
		OSLockRelease(psRegCfg->hLock);
		return PVRSRV_ERROR_REG_CONFIG_INVALID_TYPE;
	}

	psRegCfg->eRegCfgTypeToPush = eRegCfgType;

	OSLockRelease(psRegCfg->hLock);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(psDevConnection);

	PVR_DPF((PVR_DBG_ERROR,
		 "%s: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION",
		 __func__));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXAddRegConfigKM(CONNECTION_DATA * psConnection,
                                     PVRSRV_DEVICE_NODE	*psDeviceNode,
                                     IMG_UINT32		ui32RegAddr,
                                     IMG_UINT64		ui64RegValue,
                                     IMG_UINT64		ui64RegMask)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG		*psRegCfg = &psDevInfo->sRegCongfig;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	OSLockAcquire(psRegCfg->hLock);

	if (psRegCfg->bEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Cannot add record whilst register configuration active.",
			 __func__));
		OSLockRelease(psRegCfg->hLock);
		return PVRSRV_ERROR_REG_CONFIG_ENABLED;
	}
	if (psRegCfg->ui32NumRegRecords == RGXFWIF_REG_CFG_MAX_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Register configuration full.",
			 __func__));
		OSLockRelease(psRegCfg->hLock);
		return PVRSRV_ERROR_REG_CONFIG_FULL;
	}

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.sRegConfig.ui64Addr = (IMG_UINT64) ui32RegAddr;
	sRegCfgCmd.uCmdData.sRegConfigData.sRegConfig.ui64Value = ui64RegValue;
	sRegCfgCmd.uCmdData.sRegConfigData.sRegConfig.ui64Mask = ui64RegMask;
	sRegCfgCmd.uCmdData.sRegConfigData.eRegConfigType = psRegCfg->eRegCfgTypeToPush;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_ADD;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: RGXScheduleCommand failed. Error:%u",
			 __func__,
			 eError));
		OSLockRelease(psRegCfg->hLock);
		return eError;
	}

	psRegCfg->ui32NumRegRecords++;

	OSLockRelease(psRegCfg->hLock);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF((PVR_DBG_ERROR,
		 "%s: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION",
		 __func__));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXClearRegConfigKM(CONNECTION_DATA * psConnection,
                                       PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG		*psRegCfg = &psDevInfo->sRegCongfig;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	OSLockAcquire(psRegCfg->hLock);

	if (psRegCfg->bEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Attempt to clear register configuration whilst active.",
			 __func__));
		OSLockRelease(psRegCfg->hLock);
		return PVRSRV_ERROR_REG_CONFIG_ENABLED;
	}

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_CLEAR;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: RGXScheduleCommand failed. Error:%u",
			 __func__,
			 eError));
		OSLockRelease(psRegCfg->hLock);
		return eError;
	}

	psRegCfg->ui32NumRegRecords = 0;
	psRegCfg->eRegCfgTypeToPush = RGXFWIF_REG_CFG_TYPE_PWR_ON;

	OSLockRelease(psRegCfg->hLock);

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR,
		 "%s: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION",
		 __func__));

	PVR_UNREFERENCED_PARAMETER(psConnection);

	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXEnableRegConfigKM(CONNECTION_DATA * psConnection,
                                        PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG		*psRegCfg = &psDevInfo->sRegCongfig;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	OSLockAcquire(psRegCfg->hLock);

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_ENABLE;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: RGXScheduleCommand failed. Error:%u",
			 __func__,
			 eError));
		OSLockRelease(psRegCfg->hLock);
		return eError;
	}

	psRegCfg->bEnabled = IMG_TRUE;

	OSLockRelease(psRegCfg->hLock);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF((PVR_DBG_ERROR,
		 "%s: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION",
		 __func__));
	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

PVRSRV_ERROR PVRSRVRGXDisableRegConfigKM(CONNECTION_DATA * psConnection,
                                         PVRSRV_DEVICE_NODE	*psDeviceNode)
{
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sRegCfgCmd;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGX_REG_CONFIG		*psRegCfg = &psDevInfo->sRegCongfig;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	OSLockAcquire(psRegCfg->hLock);

	sRegCfgCmd.eCmdType = RGXFWIF_KCCB_CMD_REGCONFIG;
	sRegCfgCmd.uCmdData.sRegConfigData.eCmdType = RGXFWIF_REGCFG_CMD_DISABLE;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sRegCfgCmd,
				PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: RGXScheduleCommand failed. Error:%u",
			 __func__,
			 eError));
		OSLockRelease(psRegCfg->hLock);
		return eError;
	}

	psRegCfg->bEnabled = IMG_FALSE;

	OSLockRelease(psRegCfg->hLock);

	return eError;
#else
	PVR_DPF((PVR_DBG_ERROR,
		 "%s: Feature disabled. Compile with SUPPORT_USER_REGISTER_CONFIGURATION",
		 __func__));
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return PVRSRV_ERROR_FEATURE_DISABLED;
#endif
}

/******************************************************************************
 End of file (rgxregconfig.c)
******************************************************************************/
