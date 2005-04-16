/******************************************************************************
 *
 * Name:	skgesirq.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.30 $
 * Date:	$Date: 2003/07/04 12:34:13 $
 * Purpose:	SK specific Gigabit Ethernet special IRQ functions
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
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

#ifndef _INC_SKGESIRQ_H_
#define _INC_SKGESIRQ_H_

/* Define return codes of SkGePortCheckUp and CheckShort */
#define	SK_HW_PS_NONE		0	/* No action needed */
#define	SK_HW_PS_RESTART	1	/* Restart needed */
#define	SK_HW_PS_LINK		2	/* Link Up actions needed */

/*
 * Define the Event the special IRQ/INI module can handle
 */
#define SK_HWEV_WATIM			1	/* Timeout for WA Errata #2 XMAC */
#define SK_HWEV_PORT_START		2	/* Port Start Event by RLMT */
#define SK_HWEV_PORT_STOP		3	/* Port Stop Event by RLMT */
#define SK_HWEV_CLEAR_STAT		4	/* Clear Statistics by PNMI */
#define SK_HWEV_UPDATE_STAT		5	/* Update Statistics by PNMI */
#define SK_HWEV_SET_LMODE		6	/* Set Link Mode by PNMI */
#define SK_HWEV_SET_FLOWMODE	7	/* Set Flow Control Mode by PNMI */
#define SK_HWEV_SET_ROLE		8	/* Set Master/Slave (Role) by PNMI */
#define SK_HWEV_SET_SPEED		9	/* Set Link Speed by PNMI */
#define SK_HWEV_HALFDUP_CHK		10	/* Half Duplex Hangup Workaround */

#define SK_WA_ACT_TIME		(5000000UL)	/* 5 sec */
#define SK_WA_INA_TIME		(100000UL)	/* 100 msec */

#define SK_HALFDUP_CHK_TIME	(10000UL)	/* 10 msec */

/*
 * Define the error numbers and messages
 */
#define SKERR_SIRQ_E001		(SK_ERRBASE_SIRQ+0)
#define SKERR_SIRQ_E001MSG	"Unknown event"
#define SKERR_SIRQ_E002		(SKERR_SIRQ_E001+1)
#define SKERR_SIRQ_E002MSG	"Packet timeout RX1"
#define SKERR_SIRQ_E003		(SKERR_SIRQ_E002+1)
#define SKERR_SIRQ_E003MSG	"Packet timeout RX2"
#define SKERR_SIRQ_E004		(SKERR_SIRQ_E003+1)
#define SKERR_SIRQ_E004MSG	"MAC 1 not correctly initialized"
#define SKERR_SIRQ_E005		(SKERR_SIRQ_E004+1)
#define SKERR_SIRQ_E005MSG	"MAC 2 not correctly initialized"
#define SKERR_SIRQ_E006		(SKERR_SIRQ_E005+1)
#define SKERR_SIRQ_E006MSG	"CHECK failure R1"
#define SKERR_SIRQ_E007		(SKERR_SIRQ_E006+1)
#define SKERR_SIRQ_E007MSG	"CHECK failure R2"
#define SKERR_SIRQ_E008		(SKERR_SIRQ_E007+1)
#define SKERR_SIRQ_E008MSG	"CHECK failure XS1"
#define SKERR_SIRQ_E009		(SKERR_SIRQ_E008+1)
#define SKERR_SIRQ_E009MSG	"CHECK failure XA1"
#define SKERR_SIRQ_E010		(SKERR_SIRQ_E009+1)
#define SKERR_SIRQ_E010MSG	"CHECK failure XS2"
#define SKERR_SIRQ_E011		(SKERR_SIRQ_E010+1)
#define SKERR_SIRQ_E011MSG	"CHECK failure XA2"
#define SKERR_SIRQ_E012		(SKERR_SIRQ_E011+1)
#define SKERR_SIRQ_E012MSG	"unexpected IRQ Master error"
#define SKERR_SIRQ_E013		(SKERR_SIRQ_E012+1)
#define SKERR_SIRQ_E013MSG	"unexpected IRQ Status error"
#define SKERR_SIRQ_E014		(SKERR_SIRQ_E013+1)
#define SKERR_SIRQ_E014MSG	"Parity error on RAM (read)"
#define SKERR_SIRQ_E015		(SKERR_SIRQ_E014+1)
#define SKERR_SIRQ_E015MSG	"Parity error on RAM (write)"
#define SKERR_SIRQ_E016		(SKERR_SIRQ_E015+1)
#define SKERR_SIRQ_E016MSG	"Parity error MAC 1"
#define SKERR_SIRQ_E017		(SKERR_SIRQ_E016+1)
#define SKERR_SIRQ_E017MSG	"Parity error MAC 2"
#define SKERR_SIRQ_E018		(SKERR_SIRQ_E017+1)
#define SKERR_SIRQ_E018MSG	"Parity error RX 1"
#define SKERR_SIRQ_E019		(SKERR_SIRQ_E018+1)
#define SKERR_SIRQ_E019MSG	"Parity error RX 2"
#define SKERR_SIRQ_E020		(SKERR_SIRQ_E019+1)
#define SKERR_SIRQ_E020MSG	"MAC transmit FIFO underrun"
#define SKERR_SIRQ_E021		(SKERR_SIRQ_E020+1)
#define SKERR_SIRQ_E021MSG	"Spurious TWSI interrupt"
#define SKERR_SIRQ_E022		(SKERR_SIRQ_E021+1)
#define SKERR_SIRQ_E022MSG	"Cable pair swap error"
#define SKERR_SIRQ_E023		(SKERR_SIRQ_E022+1)
#define SKERR_SIRQ_E023MSG	"Auto-negotiation error"
#define SKERR_SIRQ_E024		(SKERR_SIRQ_E023+1)
#define SKERR_SIRQ_E024MSG	"FIFO overflow error"
#define SKERR_SIRQ_E025		(SKERR_SIRQ_E024+1)
#define SKERR_SIRQ_E025MSG	"2 Pair Downshift detected"

extern void SkGeSirqIsr(SK_AC *pAC, SK_IOC IoC, SK_U32 Istatus);
extern int  SkGeSirqEvent(SK_AC *pAC, SK_IOC IoC, SK_U32 Event, SK_EVPARA Para);
extern void SkHWLinkUp(SK_AC *pAC, SK_IOC IoC, int Port);
extern void SkHWLinkDown(SK_AC *pAC, SK_IOC IoC, int Port);

#endif	/* _INC_SKGESIRQ_H_ */
