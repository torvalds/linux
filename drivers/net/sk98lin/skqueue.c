/******************************************************************************
 *
 * Name:	skqueue.c
 * Project:	Gigabit Ethernet Adapters, Event Scheduler Module
 * Version:	$Revision: 1.20 $
 * Date:	$Date: 2003/09/16 13:44:00 $
 * Purpose:	Management of an event queue.
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
	"@(#) $Id: skqueue.c,v 1.20 2003/09/16 13:44:00 rschmidt Exp $ (C) Marvell.";
#endif

#include "h/skdrv1st.h"		/* Driver Specific Definitions */
#include "h/skqueue.h"		/* Queue Definitions */
#include "h/skdrv2nd.h"		/* Adapter Control- and Driver specific Def. */

#ifdef __C2MAN__
/*
	Event queue management.

	General Description:

 */
intro()
{}
#endif

#define PRINTF(a,b,c)

/*
 * init event queue management
 *
 * Must be called during init level 0.
 */
void	SkEventInit(
SK_AC	*pAC,	/* Adapter context */
SK_IOC	Ioc,	/* IO context */
int		Level)	/* Init level */
{
	switch (Level) {
	case SK_INIT_DATA:
		pAC->Event.EvPut = pAC->Event.EvGet = pAC->Event.EvQueue;
		break;
	default:
		break;
	}
}

/*
 * add event to queue
 */
void	SkEventQueue(
SK_AC		*pAC,	/* Adapters context */
SK_U32		Class,	/* Event Class */
SK_U32		Event,	/* Event to be queued */
SK_EVPARA	Para)	/* Event parameter */
{
	pAC->Event.EvPut->Class = Class;
	pAC->Event.EvPut->Event = Event;
	pAC->Event.EvPut->Para = Para;
	
	if (++pAC->Event.EvPut == &pAC->Event.EvQueue[SK_MAX_EVENT])
		pAC->Event.EvPut = pAC->Event.EvQueue;

	if (pAC->Event.EvPut == pAC->Event.EvGet) {
		SK_ERR_LOG(pAC, SK_ERRCL_NORES, SKERR_Q_E001, SKERR_Q_E001MSG);
	}
}

/*
 * event dispatcher
 *	while event queue is not empty
 *		get event from queue
 *		send command to state machine
 *	end
 *	return error reported by individual Event function
 *		0 if no error occured.
 */
int	SkEventDispatcher(
SK_AC	*pAC,	/* Adapters Context */
SK_IOC	Ioc)	/* Io context */
{
	SK_EVENTELEM	*pEv;	/* pointer into queue */
	SK_U32			Class;
	int			Rtv;

	pEv = pAC->Event.EvGet;
	
	PRINTF("dispatch get %x put %x\n", pEv, pAC->Event.ev_put);
	
	while (pEv != pAC->Event.EvPut) {
		PRINTF("dispatch Class %d Event %d\n", pEv->Class, pEv->Event);

		switch (Class = pEv->Class) {
#ifndef SK_USE_LAC_EV
#ifndef SK_SLIM
		case SKGE_RLMT:		/* RLMT Event */
			Rtv = SkRlmtEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
		case SKGE_I2C:		/* I2C Event */
			Rtv = SkI2cEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
		case SKGE_PNMI:		/* PNMI Event */
			Rtv = SkPnmiEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#endif	/* not SK_SLIM */
#endif	/* not SK_USE_LAC_EV */
		case SKGE_DRV:		/* Driver Event */
			Rtv = SkDrvEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#ifndef SK_USE_SW_TIMER
		case SKGE_HWAC:
			Rtv = SkGeSirqEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#else /* !SK_USE_SW_TIMER */
        case SKGE_SWT :
			Rtv = SkSwtEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#endif /* !SK_USE_SW_TIMER */
#ifdef SK_USE_LAC_EV
		case SKGE_LACP :
			Rtv = SkLacpEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
		case SKGE_RSF :
			Rtv = SkRsfEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
		case SKGE_MARKER :
			Rtv = SkMarkerEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
		case SKGE_FD :
			Rtv = SkFdEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#endif /* SK_USE_LAC_EV */
#ifdef	SK_USE_CSUM
		case SKGE_CSUM :
			Rtv = SkCsEvent(pAC, Ioc, pEv->Event, pEv->Para);
			break;
#endif	/* SK_USE_CSUM */
		default :
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_Q_E002, SKERR_Q_E002MSG);
			Rtv = 0;
		}

		if (Rtv != 0) {
			return(Rtv);
		}

		if (++pEv == &pAC->Event.EvQueue[SK_MAX_EVENT])
			pEv = pAC->Event.EvQueue;

		/* Renew get: it is used in queue_events to detect overruns */
		pAC->Event.EvGet = pEv;
	}

	return(0);
}

/* End of file */
