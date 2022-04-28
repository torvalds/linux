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
		eError = PVRSRVSetDeviceDefaultPowerState((const PPVRSRV_DEVICE_NODE)psDeviceNode,
		                                          PVRSRV_DEV_POWER_STATE_ON);
	}
#endif

	return eError;
}

PVRSRV_ERROR RGXQueryPdumpPanicDisable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL *pbDisabled)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	*pbDisabled = !psDevInfo->bPDPEnabled;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXSetPdumpPanicDisable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL bDisable)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(pvPrivateData);

	if (!psDeviceNode || !psDeviceNode->pvDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	psDevInfo->bPDPEnabled = !bDisable;

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
	if (!psDevInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if ((ui32Config & ~RGXKM_DEVICE_STATE_MASK) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Bits outside of device state mask set (input: 0x%x, mask: 0x%x)",
				 __func__, ui32Config, RGXKM_DEVICE_STATE_MASK));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (bSetNotClear)
	{
		psDevInfo->ui32DeviceFlags |= ui32Config;
	}
	else
	{
		psDevInfo->ui32DeviceFlags &= ~ui32Config;
	}

	return PVRSRV_OK;
}

inline const char * RGXStringifyKickTypeDM(RGX_KICK_TYPE_DM eKickTypeDM)
{
	PVR_ASSERT(eKickTypeDM < RGX_KICK_TYPE_DM_LAST);

	switch (eKickTypeDM) {
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
