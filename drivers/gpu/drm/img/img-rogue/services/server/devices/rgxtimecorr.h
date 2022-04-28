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

#if !defined(RGXTIMECORR_H)
#define RGXTIMECORR_H

#include "img_types.h"
#include "device.h"
#include "osfunc.h"
#include "connection_server.h"

typedef enum
{
	RGXTIMECORR_CLOCK_MONO,
	RGXTIMECORR_CLOCK_MONO_RAW,
	RGXTIMECORR_CLOCK_SCHED,

	RGXTIMECORR_CLOCK_LAST
} RGXTIMECORR_CLOCK_TYPE;

typedef enum
{
	RGXTIMECORR_EVENT_POWER,
	RGXTIMECORR_EVENT_DVFS,
	RGXTIMECORR_EVENT_PERIODIC,
	RGXTIMECORR_EVENT_CLOCK_CHANGE
} RGXTIMECORR_EVENT;

/*
 * Calibrated GPU frequencies are rounded to the nearest multiple of 1 KHz
 * before use, to reduce the noise introduced by calculations done with
 * imperfect operands (correlated timers not sampled at exactly the same
 * time, GPU CR timer incrementing only once every 256 GPU cycles).
 * This also helps reducing the variation between consecutive calculations.
 */
#define RGXFWIF_CONVERT_TO_KHZ(freq)   (((freq) + 500) / 1000)
#define RGXFWIF_ROUND_TO_KHZ(freq)    ((((freq) + 500) / 1000) * 1000)

/* Constants used in different calculations */
#define SECONDS_TO_MICROSECONDS          (1000000ULL)
#define CRTIME_TO_CYCLES_WITH_US_SCALE   (RGX_CRTIME_TICK_IN_CYCLES * SECONDS_TO_MICROSECONDS)

/*
 * Use this macro to get a more realistic GPU core clock speed than the one
 * given by the upper layers (used when doing GPU frequency calibration)
 */
#define RGXFWIF_GET_GPU_CLOCK_FREQUENCY_HZ(deltacr_us, deltaos_us, remainder) \
    OSDivide64((deltacr_us) * CRTIME_TO_CYCLES_WITH_US_SCALE, (deltaos_us), &(remainder))


/*!
******************************************************************************

 @Function    RGXTimeCorrGetConversionFactor

 @Description Generate constant used to convert a GPU time difference into
              an OS time difference (for more info see rgx_fwif_km.h).

 @Input       ui32ClockSpeed : GPU clock speed

 @Return      0 on failure, conversion factor otherwise

******************************************************************************/
static inline IMG_UINT64 RGXTimeCorrGetConversionFactor(IMG_UINT32 ui32ClockSpeed)
{
	IMG_UINT32 ui32Remainder;

	if (RGXFWIF_CONVERT_TO_KHZ(ui32ClockSpeed) == 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: GPU clock frequency %u is too low",
				 __func__, ui32ClockSpeed));

		return 0;
	}

	return OSDivide64r64(CRTIME_TO_CYCLES_WITH_US_SCALE << RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT,
	                     RGXFWIF_CONVERT_TO_KHZ(ui32ClockSpeed), &ui32Remainder);
}

/*!
******************************************************************************

 @Function    RGXTimeCorrBegin

 @Description Generate new timer correlation data, and start tracking
              the current GPU frequency.

 @Input       hDevHandle : RGX Device Node
 @Input       eEvent     : Event associated with the beginning of a timer
                           correlation period

 @Return      void

******************************************************************************/
void RGXTimeCorrBegin(IMG_HANDLE hDevHandle, RGXTIMECORR_EVENT eEvent);

/*!
******************************************************************************

 @Function    RGXTimeCorrEnd

 @Description Stop tracking the CPU and GPU timers, and if possible
              recalculate the GPU frequency to a value which makes the timer
              correlation data more accurate.

 @Input       hDevHandle : RGX Device Node
 @Input       eEvent     : Event associated with the end of a timer
                           correlation period

 @Return      void

******************************************************************************/
void RGXTimeCorrEnd(IMG_HANDLE hDevHandle, RGXTIMECORR_EVENT eEvent);

/*!
******************************************************************************

 @Function    RGXTimeCorrRestartPeriodic

 @Description Perform actions from RGXTimeCorrEnd and RGXTimeCorrBegin,
              but only if enough time has passed since the last timer
              correlation data was generated.

 @Input       hDevHandle : RGX Device Node

 @Return      void

******************************************************************************/
void RGXTimeCorrRestartPeriodic(IMG_HANDLE hDevHandle);

/*!
******************************************************************************

 @Function    RGXTimeCorrGetClockns64

 @Description Returns value of currently selected clock (in ns).

 @Return      clock value from currently selected clock source

******************************************************************************/
IMG_UINT64 RGXTimeCorrGetClockns64(void);

/*!
******************************************************************************

 @Function    RGXTimeCorrGetClockus64

 @Description Returns value of currently selected clock (in us).

 @Return      clock value from currently selected clock source

******************************************************************************/
IMG_UINT64 RGXTimeCorrGetClockus64(void);

/*!
******************************************************************************

 @Function    RGXTimeCorrGetClockSource

 @Description Returns currently selected clock source

 @Return      clock source type

******************************************************************************/
RGXTIMECORR_CLOCK_TYPE RGXTimeCorrGetClockSource(void);

/*!
******************************************************************************

 @Function    RGXTimeCorrSetClockSource

 @Description Sets clock source for correlation data.

 @Input       psDeviceNode : RGX Device Node
 @Input       eClockType : clock source type

 @Return      error code

******************************************************************************/
PVRSRV_ERROR RGXTimeCorrSetClockSource(PVRSRV_DEVICE_NODE *psDeviceNode,
                                       RGXTIMECORR_CLOCK_TYPE eClockType);

/*!
******************************************************************************

 @Function    RGXTimeCorrInitAppHintCallbacks

 @Description Initialise apphint callbacks for timer correlation
              related apphints.

 @Input       psDeviceNode : RGX Device Node

 @Return      void

******************************************************************************/
void RGXTimeCorrInitAppHintCallbacks(const PVRSRV_DEVICE_NODE *psDeviceNode);

/*!
******************************************************************************

 @Function    RGXGetTimeCorrData

 @Description Get a number of the most recent time correlation data points

 @Input       psDeviceNode : RGX Device Node
 @Output      psTimeCorrs  : Output array of RGXFWIF_TIME_CORR elements
                             for data to be written to
 @Input       ui32NumOut   : Number of elements to be written out

 @Return      void

******************************************************************************/
void RGXGetTimeCorrData(PVRSRV_DEVICE_NODE *psDeviceNode,
							RGXFWIF_TIME_CORR *psTimeCorrs,
							IMG_UINT32 ui32NumOut);

/**************************************************************************/ /*!
@Function       PVRSRVRGXCurrentTime
@Description    Returns the current state of the device timer
@Input          psDevData  Device data.
@Out            pui64Time
@Return         PVRSRV_OK on success.
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRGXCurrentTime(CONNECTION_DATA    * psConnection,
                     PVRSRV_DEVICE_NODE * psDeviceNode,
                     IMG_UINT64         * pui64Time);

#endif /* RGXTIMECORR_H */
