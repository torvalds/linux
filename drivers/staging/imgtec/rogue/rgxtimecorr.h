/*************************************************************************/ /*!
@File
@Title          RGX time correlation and calibration header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX time correlation and calibration routines
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

#if !defined(__RGXTIMECORR_H__)
#define __RGXTIMECORR_H__

#include "img_types.h"
#include "device.h"

typedef enum {
    RGXTIMECORR_CLOCK_MONO,
    RGXTIMECORR_CLOCK_MONO_RAW,
    RGXTIMECORR_CLOCK_SCHED,

    RGXTIMECORR_CLOCK_LAST
} RGXTIMECORR_CLOCK_TYPE;

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibratePrePowerOff

 @Description Manage GPU frequency and timer correlation data
              before a power off.

 @Input       hDevHandle : RGX Device Node

 @Return      void

******************************************************************************/
void RGXGPUFreqCalibratePrePowerOff(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibratePostPowerOn

 @Description Manage GPU frequency and timer correlation data
              after a power on.

 @Input       hDevHandle : RGX Device Node

 @Return      void

******************************************************************************/
void RGXGPUFreqCalibratePostPowerOn(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibratePreClockSpeedChange

 @Description Manage GPU frequency and timer correlation data
              before a DVFS transition.

 @Input       hDevHandle : RGX Device Node

 @Return      void

******************************************************************************/
void RGXGPUFreqCalibratePreClockSpeedChange(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibratePostClockSpeedChange

 @Description Manage GPU frequency and timer correlation data
              after a DVFS transition.

 @Input       hDevHandle        : RGX Device Node
 @Input       ui32NewClockSpeed : GPU clock speed after the DVFS transition

 @Return      IMG_UINT32 : Calibrated GPU clock speed after the DVFS transition

******************************************************************************/
IMG_UINT32 RGXGPUFreqCalibratePostClockSpeedChange(IMG_HANDLE hDevHandle, IMG_UINT32 ui32NewClockSpeed);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibratePeriodic

 @Description Calibrate the GPU clock speed and correlate the timers
              at regular intervals.

 @Input       hDevHandle : RGX Device Node

 @Return      void

******************************************************************************/
void RGXGPUFreqCalibrateCorrelatePeriodic(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibrateClockns64

 @Description Returns value of currently selected clock (in ns).

 @Return      clock value from currently selected clock source

******************************************************************************/
IMG_UINT64 RGXGPUFreqCalibrateClockns64(void);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibrateClockns64

 @Description Returns value of currently selected clock (in us).

 @Return      clock value from currently selected clock source

******************************************************************************/
IMG_UINT64 RGXGPUFreqCalibrateClockus64(void);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibrateClockSource

 @Description Returns currently selected clock source

 @Return      clock source type

******************************************************************************/
RGXTIMECORR_CLOCK_TYPE RGXGPUFreqCalibrateGetClockSource(void);

/*!
******************************************************************************

 @Function    RGXGPUFreqCalibrateSetClockSource

 @Description Sets clock source for correlation data.

 @Input       psDeviceNode : RGX Device Node
 @Input       eClockType : clock source type

 @Return      error code

******************************************************************************/
PVRSRV_ERROR RGXGPUFreqCalibrateSetClockSource(PVRSRV_DEVICE_NODE *psDeviceNode,
                                             RGXTIMECORR_CLOCK_TYPE eClockType);

void RGXGPUFreqCalibrationInitAppHintCallbacks(
                                        const PVRSRV_DEVICE_NODE *psDeviceNode);

#endif /* __RGXTIMECORR_H__ */
