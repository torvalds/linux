/******************************************************************************
 *
 * Name:	sktimer.h
 * Project:	Gigabit Ethernet Adapters, Event Scheduler Module
 * Version:	$Revision: 1.11 $
 * Date:	$Date: 2003/09/16 12:58:18 $
 * Purpose:	Defines for the timer functions
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * SKTIMER.H	contains all defines and types for the timer functions
 */

#ifndef	_SKTIMER_H_
#define _SKTIMER_H_

#include "h/skqueue.h"

/*
 * SK timer
 * - needed wherever a timer is used. Put this in your data structure
 *   wherever you want.
 */
typedef	struct s_Timer SK_TIMER;

struct s_Timer {
	SK_TIMER	*TmNext;	/* linked list */
	SK_U32		TmClass;	/* Timer Event class */
	SK_U32		TmEvent;	/* Timer Event value */
	SK_EVPARA	TmPara;		/* Timer Event parameter */
	SK_U32		TmDelta;	/* delta time */
	int			TmActive;	/* flag: active/inactive */
};

/*
 * Timer control struct.
 * - use in Adapters context name pAC->Tim
 */
typedef	struct s_TimCtrl {
	SK_TIMER	*StQueue;	/* Head of Timer queue */
} SK_TIMCTRL;

extern void SkTimerInit(SK_AC *pAC, SK_IOC Ioc, int Level);
extern void SkTimerStop(SK_AC *pAC, SK_IOC Ioc, SK_TIMER *pTimer);
extern void SkTimerStart(SK_AC *pAC, SK_IOC Ioc, SK_TIMER *pTimer,
	SK_U32 Time, SK_U32 Class, SK_U32 Event, SK_EVPARA Para);
extern void SkTimerDone(SK_AC *pAC, SK_IOC Ioc);
#endif	/* _SKTIMER_H_ */
