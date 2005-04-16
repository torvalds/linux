/******************************************************************************
 *
 * Name:	skhwt.h
 * Project:	Gigabit Ethernet Adapters, Event Scheduler Module
 * Version:	$Revision: 1.7 $
 * Date:	$Date: 2003/09/16 12:55:08 $
 * Purpose:	Defines for the hardware timer functions
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
 * SKGEHWT.H	contains all defines and types for the timer functions
 */

#ifndef	_SKGEHWT_H_
#define _SKGEHWT_H_

/*
 * SK Hardware Timer
 * - needed wherever the HWT module is used
 * - use in Adapters context name pAC->Hwt
 */
typedef	struct s_Hwt {
	SK_U32		TStart;	/* HWT start */
	SK_U32		TStop;	/* HWT stop */
	int		TActive;	/* HWT: flag : active/inactive */
} SK_HWT;

extern void SkHwtInit(SK_AC *pAC, SK_IOC Ioc);
extern void SkHwtStart(SK_AC *pAC, SK_IOC Ioc, SK_U32 Time);
extern void SkHwtStop(SK_AC *pAC, SK_IOC Ioc);
extern SK_U32 SkHwtRead(SK_AC *pAC, SK_IOC Ioc);
extern void SkHwtIsr(SK_AC *pAC, SK_IOC Ioc);
#endif	/* _SKGEHWT_H_ */
