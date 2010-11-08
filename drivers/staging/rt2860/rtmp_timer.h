/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
	rtmp_timer.h

    Abstract:
	Ralink Wireless Driver timer related data structures and declarations 

    Revision History:
	Who          		When                 What
	--------    ----------      ----------------------------------------------
	Name          		Date                 Modification logs
	Shiang Tu    		Aug-28-2008 	     init version
	Justin P. Mattock	11/07/2010	     Fix a typo

*/

#ifndef __RTMP_TIMER_H__
#define  __RTMP_TIMER_H__

#include "rtmp_os.h"

#define DECLARE_TIMER_FUNCTION(_func)			\
	void rtmp_timer_##_func(unsigned long data)

#define GET_TIMER_FUNCTION(_func)				\
	rtmp_timer_##_func

/* ----------------- Timer Related MARCO ---------------*/
/* In some os or chipset, we have a lot of timer functions and will read/write register, */
/* it's not allowed in Linux USB sub-system to do it ( because of sleep issue when */
/* submit to ctrl pipe). So we need a wrapper function to take care it. */

#ifdef RTMP_TIMER_TASK_SUPPORT
typedef void(*RTMP_TIMER_TASK_HANDLE) (void *SystemSpecific1,
				       void *FunctionContext,
				       void *SystemSpecific2,
				       void *SystemSpecific3);
#endif /* RTMP_TIMER_TASK_SUPPORT // */

struct rt_ralink_timer {
	struct timer_list TimerObj;	/* Ndis Timer object */
	BOOLEAN Valid;		/* Set to True when call RTMPInitTimer */
	BOOLEAN State;		/* True if timer cancelled */
	BOOLEAN PeriodicType;	/* True if timer is periodic timer */
	BOOLEAN Repeat;		/* True if periodic timer */
	unsigned long TimerValue;	/* Timer value in milliseconds */
	unsigned long cookie;		/* os specific object */
#ifdef RTMP_TIMER_TASK_SUPPORT
	RTMP_TIMER_TASK_HANDLE handle;
	void *pAd;
#endif				/* RTMP_TIMER_TASK_SUPPORT // */
};

#ifdef RTMP_TIMER_TASK_SUPPORT
struct rt_rtmp_timer_task_entry {
	struct rt_ralink_timer *pRaTimer;
	struct rt_rtmp_timer_task_entry *pNext;
};

#define TIMER_QUEUE_SIZE_MAX	128
struct rt_rtmp_timer_task_queue {
	unsigned int status;
	unsigned char *pTimerQPoll;
	struct rt_rtmp_timer_task_entry *pQPollFreeList;
	struct rt_rtmp_timer_task_entry *pQHead;
	struct rt_rtmp_timer_task_entry *pQTail;
};

#define BUILD_TIMER_FUNCTION(_func)										\
void rtmp_timer_##_func(unsigned long data)										\
{																			\
	struct rt_ralink_timer *_pTimer = (struct rt_ralink_timer *)data;				\
	struct rt_rtmp_timer_task_entry *_pQNode;										\
	struct rt_rtmp_adapter *_pAd;											\
																			\
	_pTimer->handle = _func;													\
	_pAd = (struct rt_rtmp_adapter *)_pTimer->pAd;										\
	_pQNode = RtmpTimerQInsert(_pAd, _pTimer);								\
	if ((_pQNode == NULL) && (_pAd->TimerQ.status & RTMP_TASK_CAN_DO_INSERT))	\
		RTMP_OS_Add_Timer(&_pTimer->TimerObj, OS_HZ);							\
}
#else
#define BUILD_TIMER_FUNCTION(_func)										\
void rtmp_timer_##_func(unsigned long data)										\
{																			\
	struct rt_ralink_timer *pTimer = (struct rt_ralink_timer *)data;				\
																			\
	_func(NULL, (void *)pTimer->cookie, NULL, pTimer);							\
	if (pTimer->Repeat)														\
		RTMP_OS_Add_Timer(&pTimer->TimerObj, pTimer->TimerValue);			\
}
#endif /* RTMP_TIMER_TASK_SUPPORT // */

DECLARE_TIMER_FUNCTION(MlmePeriodicExec);
DECLARE_TIMER_FUNCTION(MlmeRssiReportExec);
DECLARE_TIMER_FUNCTION(AsicRxAntEvalTimeout);
DECLARE_TIMER_FUNCTION(APSDPeriodicExec);
DECLARE_TIMER_FUNCTION(AsicRfTuningExec);
#ifdef RTMP_MAC_USB
DECLARE_TIMER_FUNCTION(BeaconUpdateExec);
#endif /* RTMP_MAC_USB // */

DECLARE_TIMER_FUNCTION(BeaconTimeout);
DECLARE_TIMER_FUNCTION(ScanTimeout);
DECLARE_TIMER_FUNCTION(AuthTimeout);
DECLARE_TIMER_FUNCTION(AssocTimeout);
DECLARE_TIMER_FUNCTION(ReassocTimeout);
DECLARE_TIMER_FUNCTION(DisassocTimeout);
DECLARE_TIMER_FUNCTION(LinkDownExec);
DECLARE_TIMER_FUNCTION(StaQuickResponeForRateUpExec);
DECLARE_TIMER_FUNCTION(WpaDisassocApAndBlockAssoc);
DECLARE_TIMER_FUNCTION(PsPollWakeExec);
DECLARE_TIMER_FUNCTION(RadioOnExec);

#ifdef RTMP_MAC_USB
DECLARE_TIMER_FUNCTION(RtmpUsbStaAsicForceWakeupTimeout);
#endif /* RTMP_MAC_USB // */

#if defined(AP_LED) || defined(STA_LED)
DECLARE_TIMER_FUNCTION(LedCtrlMain);
#endif

#endif /* __RTMP_TIMER_H__ // */
