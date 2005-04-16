/******************************************************************************
 *
 * Name:	sktimer.c
 * Project:	Gigabit Ethernet Adapters, Event Scheduler Module
 * Version:	$Revision: 1.14 $
 * Date:	$Date: 2003/09/16 13:46:51 $
 * Purpose:	High level timer functions.
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
 *	Event queue and dispatcher
 */
#if (defined(DEBUG) || ((!defined(LINT)) && (!defined(SK_SLIM))))
static const char SysKonnectFileId[] =
	"@(#) $Id: sktimer.c,v 1.14 2003/09/16 13:46:51 rschmidt Exp $ (C) Marvell.";
#endif

#include "h/skdrv1st.h"		/* Driver Specific Definitions */
#include "h/skdrv2nd.h"		/* Adapter Control- and Driver specific Def. */

#ifdef __C2MAN__
/*
	Event queue management.

	General Description:

 */
intro()
{}
#endif


/* Forward declaration */
static void timer_done(SK_AC *pAC,SK_IOC Ioc,int Restart);


/*
 * Inits the software timer
 *
 * needs to be called during Init level 1.
 */
void	SkTimerInit(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	Ioc,		/* IoContext */
int		Level)		/* Init Level */
{
	switch (Level) {
	case SK_INIT_DATA:
		pAC->Tim.StQueue = NULL;
		break;
	case SK_INIT_IO:
		SkHwtInit(pAC, Ioc);
		SkTimerDone(pAC, Ioc);
		break;
	default:
		break;
	}
}

/*
 * Stops a high level timer
 * - If a timer is not in the queue the function returns normally, too.
 */
void	SkTimerStop(
SK_AC		*pAC,		/* Adapters context */
SK_IOC		Ioc,		/* IoContext */
SK_TIMER	*pTimer)	/* Timer Pointer to be started */
{
	SK_TIMER	**ppTimPrev;
	SK_TIMER	*pTm;

	/*
	 * remove timer from queue
	 */
	pTimer->TmActive = SK_FALSE;
	
	if (pAC->Tim.StQueue == pTimer && !pTimer->TmNext) {
		SkHwtStop(pAC, Ioc);
	}
	
	for (ppTimPrev = &pAC->Tim.StQueue; (pTm = *ppTimPrev);
		ppTimPrev = &pTm->TmNext ) {
		
		if (pTm == pTimer) {
			/*
			 * Timer found in queue
			 * - dequeue it and
			 * - correct delta of the next timer
			 */
			*ppTimPrev = pTm->TmNext;

			if (pTm->TmNext) {
				/* correct delta of next timer in queue */
				pTm->TmNext->TmDelta += pTm->TmDelta;
			}
			return;
		}
	}
}

/*
 * Start a high level software timer
 */
void	SkTimerStart(
SK_AC		*pAC,		/* Adapters context */
SK_IOC		Ioc,		/* IoContext */
SK_TIMER	*pTimer,	/* Timer Pointer to be started */
SK_U32		Time,		/* Time value */
SK_U32		Class,		/* Event Class for this timer */
SK_U32		Event,		/* Event Value for this timer */
SK_EVPARA	Para)		/* Event Parameter for this timer */
{
	SK_TIMER	**ppTimPrev;
	SK_TIMER	*pTm;
	SK_U32		Delta;

	Time /= 16;		/* input is uS, clock ticks are 16uS */
	
	if (!Time)
		Time = 1;

	SkTimerStop(pAC, Ioc, pTimer);

	pTimer->TmClass = Class;
	pTimer->TmEvent = Event;
	pTimer->TmPara = Para;
	pTimer->TmActive = SK_TRUE;

	if (!pAC->Tim.StQueue) {
		/* First Timer to be started */
		pAC->Tim.StQueue = pTimer;
		pTimer->TmNext = NULL;
		pTimer->TmDelta = Time;
		
		SkHwtStart(pAC, Ioc, Time);
		
		return;
	}

	/*
	 * timer correction
	 */
	timer_done(pAC, Ioc, 0);

	/*
	 * find position in queue
	 */
	Delta = 0;
	for (ppTimPrev = &pAC->Tim.StQueue; (pTm = *ppTimPrev);
		ppTimPrev = &pTm->TmNext ) {
		
		if (Delta + pTm->TmDelta > Time) {
			/* Position found */
			/* Here the timer needs to be inserted. */
			break;
		}
		Delta += pTm->TmDelta;
	}

	/* insert in queue */
	*ppTimPrev = pTimer;
	pTimer->TmNext = pTm;
	pTimer->TmDelta = Time - Delta;

	if (pTm) {
		/* There is a next timer
		 * -> correct its Delta value.
		 */
		pTm->TmDelta -= pTimer->TmDelta;
	}

	/* restart with first */
	SkHwtStart(pAC, Ioc, pAC->Tim.StQueue->TmDelta);
}


void	SkTimerDone(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	Ioc)		/* IoContext */
{
	timer_done(pAC, Ioc, 1);
}


static void	timer_done(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	Ioc,		/* IoContext */
int		Restart)	/* Do we need to restart the Hardware timer ? */
{
	SK_U32		Delta;
	SK_TIMER	*pTm;
	SK_TIMER	*pTComp;	/* Timer completed now now */
	SK_TIMER	**ppLast;	/* Next field of Last timer to be deq */
	int		Done = 0;

	Delta = SkHwtRead(pAC, Ioc);
	
	ppLast = &pAC->Tim.StQueue;
	pTm = pAC->Tim.StQueue;
	while (pTm && !Done) {
		if (Delta >= pTm->TmDelta) {
			/* Timer ran out */
			pTm->TmActive = SK_FALSE;
			Delta -= pTm->TmDelta;
			ppLast = &pTm->TmNext;
			pTm = pTm->TmNext;
		}
		else {
			/* We found the first timer that did not run out */
			pTm->TmDelta -= Delta;
			Delta = 0;
			Done = 1;
		}
	}
	*ppLast = NULL;
	/*
	 * pTm points to the first Timer that did not run out.
	 * StQueue points to the first Timer that run out.
	 */

	for ( pTComp = pAC->Tim.StQueue; pTComp; pTComp = pTComp->TmNext) {
		SkEventQueue(pAC,pTComp->TmClass, pTComp->TmEvent, pTComp->TmPara);
	}

	/* Set head of timer queue to the first timer that did not run out */
	pAC->Tim.StQueue = pTm;

	if (Restart && pAC->Tim.StQueue) {
		/* Restart HW timer */
		SkHwtStart(pAC, Ioc, pAC->Tim.StQueue->TmDelta);
	}
}

/* End of file */
