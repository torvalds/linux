/*************************************************************************/ /*!
@Title          Time measuring functions.
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

#include "services_headers.h"
#include "metrics.h"

/* VGX: */
#if defined(SUPPORT_VGX)
#include "vgxapi_km.h"
#endif

/* SGX: */
#if defined(SUPPORT_SGX)
#include "sgxapi_km.h"
#endif

#if defined(DEBUG) || defined(TIMING)

static volatile IMG_UINT32 *pui32TimerRegister = 0;

#define PVRSRV_TIMER_TOTAL_IN_TICKS(X)	asTimers[X].ui32Total
#define PVRSRV_TIMER_TOTAL_IN_MS(X)		((1000*asTimers[X].ui32Total)/ui32TicksPerMS)
#define PVRSRV_TIMER_COUNT(X)			asTimers[X].ui32Count


Temporal_Data asTimers[PVRSRV_NUM_TIMERS]; 


/***********************************************************************************
 Function Name      : PVRSRVTimeNow
 Inputs             : None
 Outputs            : None
 Returns            : Current timer register value
 Description        : Returns the current timer register value
************************************************************************************/
IMG_UINT32 PVRSRVTimeNow(IMG_VOID)
{
	if (!pui32TimerRegister)
	{
		static IMG_BOOL bFirstTime = IMG_TRUE;

		if (bFirstTime)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVTimeNow: No timer register set up"));

			bFirstTime = IMG_FALSE;
		}

		return 0;
	}

#if defined(__sh__)

	return (0xffffffff-*pui32TimerRegister);

#else /* defined(__sh__) */

	return 0;

#endif /* defined(__sh__) */
}


/***********************************************************************************
 Function Name      : PVRSRVGetCPUFreq
 Inputs             : None
 Outputs            : None
 Returns            : CPU timer frequency
 Description        : Returns the CPU timer frequency
************************************************************************************/
static IMG_UINT32 PVRSRVGetCPUFreq(IMG_VOID)
{
	IMG_UINT32 ui32Time1, ui32Time2;

	ui32Time1 = PVRSRVTimeNow();

	OSWaitus(1000000);

	ui32Time2 = PVRSRVTimeNow();

	PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetCPUFreq: timer frequency = %d Hz", ui32Time2 - ui32Time1));

	return (ui32Time2 - ui32Time1);
}


/***********************************************************************************
 Function Name      : PVRSRVSetupMetricTimers
 Inputs             : pvDevInfo
 Outputs            : None
 Returns            : None
 Description        : Resets metric timers and sets up the timer register
************************************************************************************/
IMG_VOID PVRSRVSetupMetricTimers(IMG_VOID *pvDevInfo)
{
	IMG_UINT32 ui32Loop;

	PVR_UNREFERENCED_PARAMETER(pvDevInfo);

	for(ui32Loop=0; ui32Loop < (PVRSRV_NUM_TIMERS); ui32Loop++)
	{
		asTimers[ui32Loop].ui32Total = 0;
		asTimers[ui32Loop].ui32Count = 0;
	}

	#if defined(__sh__)

		/* timer control register */
		// clock / 1024 when TIMER_DIVISOR = 4
		// underflow int disabled
		// we get approx 38 uS per timer tick 
		*TCR_2 = TIMER_DIVISOR;

		/* reset the timer counter to 0 */
		*TCOR_2 = *TCNT_2 = (IMG_UINT)0xffffffff;

		/* start timer 2 */
		*TST_REG |= (IMG_UINT8)0x04;

		pui32TimerRegister = (IMG_UINT32 *)TCNT_2;

	#else /* defined(__sh__) */

		pui32TimerRegister = 0;

	#endif /* defined(__sh__) */
}


/***********************************************************************************
 Function Name      : PVRSRVOutputMetricTotals
 Inputs             : None
 Outputs            : None
 Returns            : None
 Description        : Displays final metric data
************************************************************************************/
IMG_VOID PVRSRVOutputMetricTotals(IMG_VOID)
{
	IMG_UINT32 ui32TicksPerMS, ui32Loop;

	ui32TicksPerMS = PVRSRVGetCPUFreq();

	if (!ui32TicksPerMS)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOutputMetricTotals: Failed to get CPU Freq"));
		return;
	}

	for(ui32Loop=0; ui32Loop < (PVRSRV_NUM_TIMERS); ui32Loop++)
	{
		if (asTimers[ui32Loop].ui32Count & 0x80000000L)
		{
			PVR_DPF((PVR_DBG_WARNING,"PVRSRVOutputMetricTotals: Timer %u is still ON", ui32Loop));
		}
	}
#if 0
	/*
	** EXAMPLE TIMER OUTPUT
	*/
	PVR_DPF((PVR_DBG_ERROR," Timer(%u): Total = %u",PVRSRV_TIMER_EXAMPLE_1, PVRSRV_TIMER_TOTAL_IN_TICKS(PVRSRV_TIMER_EXAMPLE_1)));
	PVR_DPF((PVR_DBG_ERROR," Timer(%u): Time = %ums",PVRSRV_TIMER_EXAMPLE_1, PVRSRV_TIMER_TOTAL_IN_MS(PVRSRV_TIMER_EXAMPLE_1)));
	PVR_DPF((PVR_DBG_ERROR," Timer(%u): Count = %u",PVRSRV_TIMER_EXAMPLE_1, PVRSRV_TIMER_COUNT(PVRSRV_TIMER_EXAMPLE_1)));
#endif
}

#endif /* defined(DEBUG) || defined(TIMING) */

/******************************************************************************
 End of file (metrics.c)
******************************************************************************/

