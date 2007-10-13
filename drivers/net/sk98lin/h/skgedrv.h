/******************************************************************************
 *
 * Name:	skgedrv.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.10 $
 * Date:	$Date: 2003/07/04 12:25:01 $
 * Purpose:	Interface with the driver
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

#ifndef __INC_SKGEDRV_H_
#define __INC_SKGEDRV_H_

/* defines ********************************************************************/

/*
 * Define the driver events.
 * Usually the events are defined by the destination module.
 * In case of the driver we put the definition of the events here.
 */
#define SK_DRV_PORT_RESET		 1	/* The port needs to be reset */
#define SK_DRV_NET_UP   		 2	/* The net is operational */
#define SK_DRV_NET_DOWN			 3	/* The net is down */
#define SK_DRV_SWITCH_SOFT		 4	/* Ports switch with both links connected */
#define SK_DRV_SWITCH_HARD		 5	/* Port switch due to link failure */
#define SK_DRV_RLMT_SEND		 6	/* Send a RLMT packet */
#define SK_DRV_ADAP_FAIL		 7	/* The whole adapter fails */
#define SK_DRV_PORT_FAIL		 8	/* One port fails */
#define SK_DRV_SWITCH_INTERN	 9	/* Port switch by the driver itself */
#define SK_DRV_POWER_DOWN		10	/* Power down mode */
#define SK_DRV_TIMER			11	/* Timer for free use */
#ifdef SK_NO_RLMT
#define SK_DRV_LINK_UP  		12	/* Link Up event for driver */
#define SK_DRV_LINK_DOWN		13	/* Link Down event for driver */
#endif
#define SK_DRV_DOWNSHIFT_DET	14	/* Downshift 4-Pair / 2-Pair (YUKON only) */
#endif /* __INC_SKGEDRV_H_ */
