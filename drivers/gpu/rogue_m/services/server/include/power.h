/*************************************************************************/ /*!
@File
@Title          Power Management Functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for power management functions
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
#ifndef POWER_H
#define POWER_H


#include "pvrsrv_device.h"

/*!
 *****************************************************************************
 *	Power management
 *****************************************************************************/
 
typedef struct _PVRSRV_POWER_DEV_TAG_
{
	PFN_PRE_POWER					pfnDevicePrePower;
	PFN_POST_POWER					pfnDevicePostPower;
	PFN_SYS_DEV_PRE_POWER			pfnSystemPrePower;
	PFN_SYS_DEV_POST_POWER			pfnSystemPostPower;
	PFN_PRE_CLOCKSPEED_CHANGE		pfnPreClockSpeedChange;
	PFN_POST_CLOCKSPEED_CHANGE		pfnPostClockSpeedChange;
	PFN_FORCED_IDLE_REQUEST			pfnForcedIdleRequest;
	PFN_FORCED_IDLE_CANCEL_REQUEST		pfnForcedIdleCancelRequest;
	PFN_DUST_COUNT_REQUEST			pfnDustCountRequest;
	IMG_HANDLE						hDevCookie;
	IMG_UINT32						ui32DeviceIndex;
	PVRSRV_DEV_POWER_STATE 			eDefaultPowerState;
	PVRSRV_DEV_POWER_STATE 			eCurrentPowerState;
	struct _PVRSRV_POWER_DEV_TAG_	*psNext;
	struct _PVRSRV_POWER_DEV_TAG_	**ppsThis;

} PVRSRV_POWER_DEV;

typedef enum _PVRSRV_INIT_SERVER_STATE_
{
	PVRSRV_INIT_SERVER_Unspecified		= -1,	
	PVRSRV_INIT_SERVER_RUNNING			= 0,	
	PVRSRV_INIT_SERVER_RAN				= 1,	
	PVRSRV_INIT_SERVER_SUCCESSFUL		= 2,	
	PVRSRV_INIT_SERVER_NUM				= 3,	
	PVRSRV_INIT_SERVER_FORCE_I32 = 0x7fffffff

} PVRSRV_INIT_SERVER_STATE, *PPVRSRV_INIT_SERVER_STATE;

typedef IMG_BOOL (*PFN_SYS_DEV_IS_DEFAULT_STATE_OFF)(PVRSRV_POWER_DEV *psPowerDevice);

IMG_IMPORT
IMG_BOOL PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_STATE	eInitServerState);

IMG_IMPORT
PVRSRV_ERROR PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_STATE	eInitServerState, IMG_BOOL bState);



IMG_IMPORT PVRSRV_ERROR PVRSRVPowerLock(IMG_VOID);
IMG_IMPORT IMG_VOID PVRSRVForcedPowerLock(IMG_VOID);
IMG_IMPORT IMG_VOID PVRSRVPowerUnlock(IMG_VOID);

IMG_BOOL PVRSRVDeviceIsDefaultStateOFF(PVRSRV_POWER_DEV *psPowerDevice);

IMG_IMPORT
PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(IMG_UINT32				ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced);

IMG_IMPORT
PVRSRV_ERROR PVRSRVSetPowerStateKM (PVRSRV_SYS_POWER_STATE ePVRState, IMG_BOOL bForced);

/* Type PFN_DC_REGISTER_POWER */
IMG_IMPORT
PVRSRV_ERROR PVRSRVRegisterPowerDevice(IMG_UINT32					ui32DeviceIndex,
									   PFN_PRE_POWER				pfnDevicePrePower,
									   PFN_POST_POWER				pfnDevicePostPower,
									   PFN_SYS_DEV_PRE_POWER		pfnSystemPrePower,
									   PFN_SYS_DEV_POST_POWER		pfnSystemPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   PFN_FORCED_IDLE_REQUEST		pfnForcedIdleRequest,
									   PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest,
									   PFN_DUST_COUNT_REQUEST	pfnDustCountRequest,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState);

IMG_IMPORT
PVRSRV_ERROR PVRSRVRemovePowerDevice (IMG_UINT32 ui32DeviceIndex);

IMG_IMPORT
PVRSRV_ERROR PVRSRVGetDevicePowerState(IMG_UINT32 ui32DeviceIndex, PPVRSRV_DEV_POWER_STATE pePowerState);

IMG_IMPORT
IMG_BOOL PVRSRVIsDevicePowered(IMG_UINT32 ui32DeviceIndex);

IMG_IMPORT
PVRSRV_ERROR PVRSRVDevicePreClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
											 IMG_BOOL	bIdleDevice,
											 IMG_VOID	*pvInfo);

IMG_IMPORT
IMG_VOID PVRSRVDevicePostClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
										  IMG_BOOL		bIdleDevice,
										  IMG_VOID		*pvInfo);

PVRSRV_ERROR PVRSRVDeviceIdleRequestKM(IMG_BOOL					bAllDevices,
					IMG_UINT32				ui32DeviceIndex,
					PFN_SYS_DEV_IS_DEFAULT_STATE_OFF	pfnCheckIdleReq,
					IMG_BOOL				bDeviceOffPermitted);

PVRSRV_ERROR PVRSRVDeviceIdleCancelRequestKM(IMG_BOOL			bAllDevices,
						IMG_UINT32		ui32DeviceIndex);

PVRSRV_ERROR PVRSRVDeviceDustCountChange(IMG_UINT32	ui32DeviceIndex,
						IMG_UINT32	ui32DustCount);


#endif /* POWER_H */

/******************************************************************************
 End of file (power.h)
******************************************************************************/
