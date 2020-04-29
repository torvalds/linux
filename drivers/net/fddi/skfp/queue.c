// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
	SMT Event Queue Management
*/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)queue.c	2.9 97/08/04 (C) SK " ;
#endif

#define PRINTF(a,b,c)

/*
 * init event queue management
 */
void ev_init(struct s_smc *smc)
{
	smc->q.ev_put = smc->q.ev_get = smc->q.ev_queue ;
}

/*
 * add event to queue
 */
void queue_event(struct s_smc *smc, int class, int event)
{
	PRINTF("queue class %d event %d\n",class,event) ;
	smc->q.ev_put->class = class ;
	smc->q.ev_put->event = event ;
	if (++smc->q.ev_put == &smc->q.ev_queue[MAX_EVENT])
		smc->q.ev_put = smc->q.ev_queue ;

	if (smc->q.ev_put == smc->q.ev_get) {
		SMT_ERR_LOG(smc,SMT_E0137, SMT_E0137_MSG) ;
	}
}

/*
 * timer_event is called from HW timer package.
 */
void timer_event(struct s_smc *smc, u_long token)
{
	PRINTF("timer event class %d token %d\n",
		EV_T_CLASS(token),
		EV_T_EVENT(token)) ;
	queue_event(smc,EV_T_CLASS(token),EV_T_EVENT(token));
}

/*
 * event dispatcher
 *	while event queue is not empty
 *		get event from queue
 *		send command to state machine
 *	end
 */
void ev_dispatcher(struct s_smc *smc)
{
	struct event_queue *ev ;	/* pointer into queue */
	int		class ;

	ev = smc->q.ev_get ;
	PRINTF("dispatch get %x put %x\n",ev,smc->q.ev_put) ;
	while (ev != smc->q.ev_put) {
		PRINTF("dispatch class %d event %d\n",ev->class,ev->event) ;
		switch(class = ev->class) {
		case EVENT_ECM :		/* Entity Corordination  Man. */
			ecm(smc,(int)ev->event) ;
			break ;
		case EVENT_CFM :		/* Configuration Man. */
			cfm(smc,(int)ev->event) ;
			break ;
		case EVENT_RMT :		/* Ring Man. */
			rmt(smc,(int)ev->event) ;
			break ;
		case EVENT_SMT :
			smt_event(smc,(int)ev->event) ;
			break ;
#ifdef	CONCENTRATOR
		case 99 :
			timer_test_event(smc,(int)ev->event) ;
			break ;
#endif
		case EVENT_PCMA :		/* PHY A */
		case EVENT_PCMB :		/* PHY B */
		default :
			if (class >= EVENT_PCMA &&
			    class < EVENT_PCMA + NUMPHYS) {
				pcm(smc,class - EVENT_PCMA,(int)ev->event) ;
				break ;
			}
			SMT_PANIC(smc,SMT_E0121, SMT_E0121_MSG) ;
			return ;
		}

		if (++ev == &smc->q.ev_queue[MAX_EVENT])
			ev = smc->q.ev_queue ;

		/* Renew get: it is used in queue_events to detect overruns */
		smc->q.ev_get = ev;
	}
}

/*
 * smt_online connects to or disconnects from the ring
 * MUST be called to initiate connection establishment
 *
 *	on	0	disconnect
 *	on	1	connect
 */
u_short smt_online(struct s_smc *smc, int on)
{
	queue_event(smc,EVENT_ECM,on ? EC_CONNECT : EC_DISCONNECT) ;
	ev_dispatcher(smc) ;
	return smc->mib.fddiSMTCF_State;
}

/*
 * set SMT flag to value
 *	flag		flag name
 *	value		flag value
 * dump current flag setting
 */
#ifdef	CONCENTRATOR
void do_smt_flag(struct s_smc *smc, char *flag, int value)
{
#ifdef	DEBUG
	struct smt_debug	*deb;

	SK_UNUSED(smc) ;

#ifdef	DEBUG_BRD
	deb = &smc->debug;
#else
	deb = &debug;
#endif
	if (!strcmp(flag,"smt"))
		deb->d_smt = value ;
	else if (!strcmp(flag,"smtf"))
		deb->d_smtf = value ;
	else if (!strcmp(flag,"pcm"))
		deb->d_pcm = value ;
	else if (!strcmp(flag,"rmt"))
		deb->d_rmt = value ;
	else if (!strcmp(flag,"cfm"))
		deb->d_cfm = value ;
	else if (!strcmp(flag,"ecm"))
		deb->d_ecm = value ;
	printf("smt	%d\n",deb->d_smt) ;
	printf("smtf	%d\n",deb->d_smtf) ;
	printf("pcm	%d\n",deb->d_pcm) ;
	printf("rmt	%d\n",deb->d_rmt) ;
	printf("cfm	%d\n",deb->d_cfm) ;
	printf("ecm	%d\n",deb->d_ecm) ;
#endif	/* DEBUG */
}
#endif
