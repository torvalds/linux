/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  target specific implementation of
                high resolution timer module for X86 under Linux
                The Linux kernel has to be compiled with high resolution
                timers enabled. This is done by configuring the kernel
                with CONFIG_HIGH_RES_TIMERS enabled.

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: TimerHighReskX86.c,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/04/17 21:38:01 $

                $State: Exp $

                Build Environment:
                    GNU

  -------------------------------------------------------------------------

  Revision History:

****************************************************************************/

#include "EplInc.h"
#include "kernel/EplTimerHighResk.h"
#include "Benchmark.h"

//#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

#define TIMER_COUNT           2	/* max 15 timers selectable */
#define TIMER_MIN_VAL_SINGLE  5000	/* min 5us */
#define TIMER_MIN_VAL_CYCLE   100000	/* min 100us */

#define PROVE_OVERRUN

#ifndef CONFIG_HIGH_RES_TIMERS
#error "Kernel symbol CONFIG_HIGH_RES_TIMERS is required."
#endif

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
void PUBLIC TgtDbgPostTraceValue(DWORD dwTraceValue_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#define TGT_DBG_POST_TRACE_VALUE(v)     TgtDbgPostTraceValue(v)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#define TGT_DBG_POST_TRACE_VALUE(v)
#endif
#define HRT_DBG_POST_TRACE_VALUE(Event_p, uiNodeId_p, wErrorCode_p) \
    TGT_DBG_POST_TRACE_VALUE((0xE << 28) | (Event_p << 24) \
                             | (uiNodeId_p << 16) | wErrorCode_p)

#define TIMERHDL_MASK         0x0FFFFFFF
#define TIMERHDL_SHIFT        28
#define HDL_TO_IDX(Hdl)       ((Hdl >> TIMERHDL_SHIFT) - 1)
#define HDL_INIT(Idx)         ((Idx + 1) << TIMERHDL_SHIFT)
#define HDL_INC(Hdl)          (((Hdl + 1) & TIMERHDL_MASK) \
                               | (Hdl & ~TIMERHDL_MASK))

//---------------------------------------------------------------------------
// modul global types
//---------------------------------------------------------------------------

typedef struct {
	tEplTimerEventArg m_EventArg;
	tEplTimerkCallback m_pfnCallback;
	struct hrtimer m_Timer;
	BOOL m_fContinuously;
	unsigned long long m_ullPeriod;

} tEplTimerHighReskTimerInfo;

typedef struct {
	tEplTimerHighReskTimerInfo m_aTimerInfo[TIMER_COUNT];

} tEplTimerHighReskInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplTimerHighReskInstance EplTimerHighReskInstance_l;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

enum hrtimer_restart EplTimerHighReskCallback(struct hrtimer *pTimer_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskInit()
//
// Description: initializes the high resolution timer module.
//
// Parameters:  void
//
// Return:      tEplKernel      = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplTimerHighReskInit(void)
{
	tEplKernel Ret;

	Ret = EplTimerHighReskAddInstance();

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskAddInstance()
//
// Description: initializes the high resolution timer module.
//
// Parameters:  void
//
// Return:      tEplKernel      = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplTimerHighReskAddInstance(void)
{
	tEplKernel Ret;
	unsigned int uiIndex;

	Ret = kEplSuccessful;

	EPL_MEMSET(&EplTimerHighReskInstance_l, 0,
		   sizeof(EplTimerHighReskInstance_l));

#ifndef CONFIG_HIGH_RES_TIMERS
	printk
	    ("EplTimerHighResk: Kernel symbol CONFIG_HIGH_RES_TIMERS is required.\n");
	Ret = kEplNoResource;
	return Ret;
#endif

	/*
	 * Initialize hrtimer structures for all usable timers.
	 */
	for (uiIndex = 0; uiIndex < TIMER_COUNT; uiIndex++) {
		tEplTimerHighReskTimerInfo *pTimerInfo;
		struct hrtimer *pTimer;

		pTimerInfo = &EplTimerHighReskInstance_l.m_aTimerInfo[uiIndex];
		pTimer = &pTimerInfo->m_Timer;
		hrtimer_init(pTimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

		pTimer->function = EplTimerHighReskCallback;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskDelInstance()
//
// Description: shuts down the high resolution timer module.
//
// Parameters:  void
//
// Return:      tEplKernel      = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplTimerHighReskDelInstance(void)
{
	tEplTimerHighReskTimerInfo *pTimerInfo;
	tEplKernel Ret;
	unsigned int uiIndex;

	Ret = kEplSuccessful;

	for (uiIndex = 0; uiIndex < TIMER_COUNT; uiIndex++) {
		pTimerInfo = &EplTimerHighReskInstance_l.m_aTimerInfo[0];
		pTimerInfo->m_pfnCallback = NULL;
		pTimerInfo->m_EventArg.m_TimerHdl = 0;
		/*
		 * In this case we can not just try to cancel the timer.
		 * We actually have to wait until its callback function
		 * has returned.
		 */
		hrtimer_cancel(&pTimerInfo->m_Timer);
	}

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskModifyTimerNs()
//
// Description: modifies the timeout of the timer with the specified handle.
//              If the handle the pointer points to is zero, the timer must
//              be created first.
//              If it is not possible to stop the old timer,
//              this function always assures that the old timer does not
//              trigger the callback function with the same handle as the new
//              timer. That means the callback function must check the passed
//              handle with the one returned by this function. If these are
//              unequal, the call can be discarded.
//
// Parameters:  pTimerHdl_p     = pointer to timer handle
//              ullTimeNs_p     = relative timeout in [ns]
//              pfnCallback_p   = callback function, which is called mutual
//                                exclusive with the Edrv callback functions
//                                (Rx and Tx).
//              ulArgument_p    = user-specific argument
//              fContinuously_p = if TRUE, callback function will be called
//                                continuously;
//                                otherwise, it is a oneshot timer.
//
// Return:      tEplKernel      = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplTimerHighReskModifyTimerNs(tEplTimerHdl * pTimerHdl_p,
						unsigned long long ullTimeNs_p,
						tEplTimerkCallback
						pfnCallback_p,
						unsigned long ulArgument_p,
						BOOL fContinuously_p)
{
	tEplKernel Ret;
	unsigned int uiIndex;
	tEplTimerHighReskTimerInfo *pTimerInfo;
	ktime_t RelTime;

	Ret = kEplSuccessful;

	// check pointer to handle
	if (pTimerHdl_p == NULL) {
		Ret = kEplTimerInvalidHandle;
		goto Exit;
	}

	if (*pTimerHdl_p == 0) {	// no timer created yet

		// search free timer info structure
		pTimerInfo = &EplTimerHighReskInstance_l.m_aTimerInfo[0];
		for (uiIndex = 0; uiIndex < TIMER_COUNT;
		     uiIndex++, pTimerInfo++) {
			if (pTimerInfo->m_EventArg.m_TimerHdl == 0) {	// free structure found
				break;
			}
		}
		if (uiIndex >= TIMER_COUNT) {	// no free structure found
			Ret = kEplTimerNoTimerCreated;
			goto Exit;
		}

		pTimerInfo->m_EventArg.m_TimerHdl = HDL_INIT(uiIndex);
	} else {
		uiIndex = HDL_TO_IDX(*pTimerHdl_p);
		if (uiIndex >= TIMER_COUNT) {	// invalid handle
			Ret = kEplTimerInvalidHandle;
			goto Exit;
		}

		pTimerInfo = &EplTimerHighReskInstance_l.m_aTimerInfo[uiIndex];
	}

	/*
	 * increment timer handle
	 * (if timer expires right after this statement, the user
	 * would detect an unknown timer handle and discard it)
	 */
	pTimerInfo->m_EventArg.m_TimerHdl =
	    HDL_INC(pTimerInfo->m_EventArg.m_TimerHdl);
	*pTimerHdl_p = pTimerInfo->m_EventArg.m_TimerHdl;

	// reject too small time values
	if ((fContinuously_p && (ullTimeNs_p < TIMER_MIN_VAL_CYCLE))
	    || (!fContinuously_p && (ullTimeNs_p < TIMER_MIN_VAL_SINGLE))) {
		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}

	pTimerInfo->m_EventArg.m_ulArg = ulArgument_p;
	pTimerInfo->m_pfnCallback = pfnCallback_p;
	pTimerInfo->m_fContinuously = fContinuously_p;
	pTimerInfo->m_ullPeriod = ullTimeNs_p;

	/*
	 * HRTIMER_MODE_REL does not influence general handling of this timer.
	 * It only sets relative mode for this start operation.
	 * -> Expire time is calculated by: Now + RelTime
	 * hrtimer_start also skips pending timer events.
	 * The state HRTIMER_STATE_CALLBACK is ignored.
	 * We have to cope with that in our callback function.
	 */
	RelTime = ktime_add_ns(ktime_set(0, 0), ullTimeNs_p);
	hrtimer_start(&pTimerInfo->m_Timer, RelTime, HRTIMER_MODE_REL);

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskDeleteTimer()
//
// Description: deletes the timer with the specified handle. Afterward the
//              handle is set to zero.
//
// Parameters:  pTimerHdl_p     = pointer to timer handle
//
// Return:      tEplKernel      = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplTimerHighReskDeleteTimer(tEplTimerHdl * pTimerHdl_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiIndex;
	tEplTimerHighReskTimerInfo *pTimerInfo;

	// check pointer to handle
	if (pTimerHdl_p == NULL) {
		Ret = kEplTimerInvalidHandle;
		goto Exit;
	}

	if (*pTimerHdl_p == 0) {	// no timer created yet
		goto Exit;
	} else {
		uiIndex = HDL_TO_IDX(*pTimerHdl_p);
		if (uiIndex >= TIMER_COUNT) {	// invalid handle
			Ret = kEplTimerInvalidHandle;
			goto Exit;
		}
		pTimerInfo = &EplTimerHighReskInstance_l.m_aTimerInfo[uiIndex];
		if (pTimerInfo->m_EventArg.m_TimerHdl != *pTimerHdl_p) {	// invalid handle
			goto Exit;
		}
	}

	*pTimerHdl_p = 0;
	pTimerInfo->m_EventArg.m_TimerHdl = 0;
	pTimerInfo->m_pfnCallback = NULL;

	/*
	 * Three return cases of hrtimer_try_to_cancel have to be tracked:
	 *  1 - timer has been removed
	 *  0 - timer was not active
	 *      We need not do anything. hrtimer timers just consist of
	 *      a hrtimer struct, which we might enqueue in the hrtimers
	 *      event list by calling hrtimer_start().
	 *      If a timer is not enqueued, it is not present in hrtimers.
	 * -1 - callback function is running
	 *      In this case we have to ensure that the timer is not
	 *      continuously restarted. This has been done by clearing
	 *      its handle.
	 */
	hrtimer_try_to_cancel(&pTimerInfo->m_Timer);

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplTimerHighReskCallback()
//
// Description: Callback function commonly used for all timers.
//
// Parameters:  pTimer_p = pointer to hrtimer
//
// Return:
//
// State:       not tested
//
//---------------------------------------------------------------------------

enum hrtimer_restart EplTimerHighReskCallback(struct hrtimer *pTimer_p)
{
	unsigned int uiIndex;
	tEplTimerHighReskTimerInfo *pTimerInfo;
	tEplTimerHdl OrgTimerHdl;
	enum hrtimer_restart Ret;

	BENCHMARK_MOD_24_SET(4);

	Ret = HRTIMER_NORESTART;
	pTimerInfo =
	    container_of(pTimer_p, tEplTimerHighReskTimerInfo, m_Timer);
	uiIndex = HDL_TO_IDX(pTimerInfo->m_EventArg.m_TimerHdl);
	if (uiIndex >= TIMER_COUNT) {	// invalid handle
		goto Exit;
	}

	/*
	 * We store the timer handle before calling the callback function
	 * as the timer can be modified inside it.
	 */
	OrgTimerHdl = pTimerInfo->m_EventArg.m_TimerHdl;

	if (pTimerInfo->m_pfnCallback != NULL) {
		pTimerInfo->m_pfnCallback(&pTimerInfo->m_EventArg);
	}

	if (pTimerInfo->m_fContinuously) {
		ktime_t Interval;
#ifdef PROVE_OVERRUN
		ktime_t Now;
		unsigned long Overruns;
#endif

		if (OrgTimerHdl != pTimerInfo->m_EventArg.m_TimerHdl) {
			/* modified timer has already been restarted */
			goto Exit;
		}
#ifdef PROVE_OVERRUN
		Now = ktime_get();
		Interval =
		    ktime_add_ns(ktime_set(0, 0), pTimerInfo->m_ullPeriod);
		Overruns = hrtimer_forward(pTimer_p, Now, Interval);
		if (Overruns > 1) {
			printk
			    ("EplTimerHighResk: Continuous timer (handle 0x%lX) had to skip %lu interval(s)!\n",
			     pTimerInfo->m_EventArg.m_TimerHdl, Overruns - 1);
		}
#else
		pTimer_p->expires = ktime_add_ns(pTimer_p->expires,
						 pTimerInfo->m_ullPeriod);
#endif

		Ret = HRTIMER_RESTART;
	}

      Exit:
	BENCHMARK_MOD_24_RESET(4);
	return Ret;
}
