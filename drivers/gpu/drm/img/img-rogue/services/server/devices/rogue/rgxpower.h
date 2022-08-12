/*************************************************************************/ /*!
@File
@Title          RGX power header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX power
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

#if !defined(RGXPOWER_H)
#define RGXPOWER_H

#include "pvrsrv_error.h"
#include "img_types.h"
#include "servicesext.h"
#include "rgxdevice.h"


/*!
******************************************************************************

 @Function	RGXPrePowerState

 @Description

 does necessary preparation before power state transition

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPrePowerState(IMG_HANDLE				hDevHandle,
							  PVRSRV_DEV_POWER_STATE	eNewPowerState,
							  PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
							  PVRSRV_POWER_FLAGS		ePwrFlags);

/*!
******************************************************************************

 @Function	RGXPostPowerState

 @Description

 does necessary preparation after power state transition

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPostPowerState(IMG_HANDLE				hDevHandle,
							   PVRSRV_DEV_POWER_STATE	eNewPowerState,
							   PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
							   PVRSRV_POWER_FLAGS		ePwrFlags);

/*!
******************************************************************************

 @Function	RGXVzPrePowerState

 @Description

 does necessary preparation before power state transition on a vz driver

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXVzPrePowerState(IMG_HANDLE				hDevHandle,
								PVRSRV_DEV_POWER_STATE	eNewPowerState,
								PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
								PVRSRV_POWER_FLAGS		ePwrFlags);

/*!
******************************************************************************

 @Function	RGXVzPostPowerState

 @Description

 does necessary preparation after power state transition on a vz driver

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXVzPostPowerState(IMG_HANDLE				hDevHandle,
								 PVRSRV_DEV_POWER_STATE	eNewPowerState,
								 PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
								 PVRSRV_POWER_FLAGS		ePwrFlags);

/*!
******************************************************************************

 @Function	RGXPreClockSpeedChange

 @Description

	Does processing required before an RGX clock speed change.

 @Input	   hDevHandle : RGX Device Node
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPreClockSpeedChange(IMG_HANDLE				hDevHandle,
									PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

/*!
******************************************************************************

 @Function	RGXPostClockSpeedChange

 @Description

	Does processing required after an RGX clock speed change.

 @Input	   hDevHandle : RGX Device Node
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPostClockSpeedChange(IMG_HANDLE				hDevHandle,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState);


/*!
******************************************************************************

 @Function	RGXDustCountChange

 @Description Change of number of DUSTs

 @Input	   hDevHandle : RGX Device Node
 @Input	   ui32NumberOfDusts : Number of DUSTs to make transition to

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXDustCountChange(IMG_HANDLE hDevHandle,
								IMG_UINT32 ui32NumberOfDusts);

/*!
******************************************************************************

 @Function	RGXAPMLatencyChange

 @Description

	Changes the wait duration used before firmware indicates IDLE.
	Reducing this value will cause the firmware to shut off faster and
	more often but may increase bubbles in GPU scheduling due to the added
	power management activity. If bPersistent is NOT set, APM latency will
	return back to system default on power up.

 @Input	   hDevHandle : RGX Device Node
 @Input	   ui32ActivePMLatencyms : Number of milliseconds to wait
 @Input	   bActivePMLatencyPersistant : Set to ensure new value is not reset

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXAPMLatencyChange(IMG_HANDLE				hDevHandle,
				IMG_UINT32				ui32ActivePMLatencyms,
				IMG_BOOL				bActivePMLatencyPersistant);

/*!
******************************************************************************

 @Function	RGXActivePowerRequest

 @Description Initiate a handshake with the FW to power off the GPU

 @Input	   hDevHandle : RGX Device Node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXActivePowerRequest(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function	RGXForcedIdleRequest

 @Description Initiate a handshake with the FW to idle the GPU

 @Input	   hDevHandle : RGX Device Node

 @Input    bDeviceOffPermitted : Set to indicate device state being off is not
                                 erroneous.

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXForcedIdleRequest(IMG_HANDLE hDevHandle, IMG_BOOL bDeviceOffPermitted);

/*!
******************************************************************************

 @Function	RGXCancelForcedIdleRequest

 @Description Send a request to cancel idle to the firmware.

 @Input	   hDevHandle : RGX Device Node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXCancelForcedIdleRequest(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function	PVRSRVGetNextDustCount

 @Description

	Calculate a sequence of dust counts to achieve full transition coverage.
	We increment two counts of dusts and switch up and down between them.
	It does contain a few redundant transitions. If two dust exist, the
	output transitions should be as follows.

	0->1, 0<-1, 0->2, 0<-2, (0->1)
	1->1, 1->2, 1<-2, (1->2)
	2->2, (2->0),
	0->0. Repeat.

	Redundant transitions in brackets.

 @Input		psDustReqState : Counter state used to calculate next dust count
 @Input		ui32DustCount : Number of dusts in the core

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_UINT32 RGXGetNextDustCount(RGX_DUST_STATE *psDustState, IMG_UINT32 ui32DustCount);

#endif /* RGXPOWER_H */
