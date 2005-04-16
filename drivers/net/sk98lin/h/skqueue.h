/******************************************************************************
 *
 * Name:	skqueue.h
 * Project:	Gigabit Ethernet Adapters, Event Scheduler Module
 * Version:	$Revision: 1.16 $
 * Date:	$Date: 2003/09/16 12:50:32 $
 * Purpose:	Defines for the Event queue
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
 * SKQUEUE.H	contains all defines and types for the event queue
 */

#ifndef _SKQUEUE_H_
#define _SKQUEUE_H_


/*
 * define the event classes to be served
 */
#define	SKGE_DRV	1	/* Driver Event Class */
#define	SKGE_RLMT	2	/* RLMT Event Class */
#define	SKGE_I2C	3	/* I2C Event Class */
#define	SKGE_PNMI	4	/* PNMI Event Class */
#define	SKGE_CSUM	5	/* Checksum Event Class */
#define	SKGE_HWAC	6	/* Hardware Access Event Class */

#define	SKGE_SWT	9	/* Software Timer Event Class */
#define	SKGE_LACP	10	/* LACP Aggregation Event Class */
#define	SKGE_RSF	11	/* RSF Aggregation Event Class */
#define	SKGE_MARKER	12	/* MARKER Aggregation Event Class */
#define	SKGE_FD		13	/* FD Distributor Event Class */

/*
 * define event queue as circular buffer
 */
#define SK_MAX_EVENT	64

/*
 * Parameter union for the Para stuff
 */
typedef	union u_EvPara {
	void	*pParaPtr;	/* Parameter Pointer */
	SK_U64	Para64;		/* Parameter 64bit version */
	SK_U32	Para32[2];	/* Parameter Array of 32bit parameters */
} SK_EVPARA;

/*
 * Event Queue
 *	skqueue.c
 * events are class/value pairs
 *	class	is addressee, e.g. RLMT, PNMI etc.
 *	value	is command, e.g. line state change, ring op change etc.
 */
typedef	struct s_EventElem {
	SK_U32		Class;			/* Event class */
	SK_U32		Event;			/* Event value */
	SK_EVPARA	Para;			/* Event parameter */
} SK_EVENTELEM;

typedef	struct s_Queue {
	SK_EVENTELEM	EvQueue[SK_MAX_EVENT];
	SK_EVENTELEM	*EvPut;
	SK_EVENTELEM	*EvGet;
} SK_QUEUE;

extern	void SkEventInit(SK_AC *pAC, SK_IOC Ioc, int Level);
extern	void SkEventQueue(SK_AC *pAC, SK_U32 Class, SK_U32 Event,
	SK_EVPARA Para);
extern	int SkEventDispatcher(SK_AC *pAC, SK_IOC Ioc);


/* Define Error Numbers and messages */
#define	SKERR_Q_E001	(SK_ERRBASE_QUEUE+0)
#define	SKERR_Q_E001MSG	"Event queue overflow"
#define	SKERR_Q_E002	(SKERR_Q_E001+1)
#define	SKERR_Q_E002MSG	"Undefined event class"
#endif	/* _SKQUEUE_H_ */

