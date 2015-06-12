/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef _RTL_DEBUG_H
#define _RTL_DEBUG_H

/* Allow files to override DRV_NAME */
#ifndef DRV_NAME
#define DRV_NAME "rtllib_92e"
#endif

#define DMESG(x, a...)

extern u32 rt_global_debug_component;

/* These are the defines for rt_global_debug_component */
enum RTL_DEBUG {
	COMP_TRACE		= (1 << 0),
	COMP_DBG		= (1 << 1),
	COMP_INIT		= (1 << 2),
	COMP_RECV		= (1 << 3),
	COMP_SEND		= (1 << 4),
	COMP_CMD		= (1 << 5),
	COMP_POWER		= (1 << 6),
	COMP_EPROM		= (1 << 7),
	COMP_SWBW		= (1 << 8),
	COMP_SEC		= (1 << 9),
	COMP_LPS		= (1 << 10),
	COMP_QOS		= (1 << 11),
	COMP_RATE		= (1 << 12),
	COMP_RXDESC		= (1 << 13),
	COMP_PHY		= (1 << 14),
	COMP_DIG		= (1 << 15),
	COMP_TXAGC		= (1 << 16),
	COMP_HALDM		= (1 << 17),
	COMP_POWER_TRACKING	= (1 << 18),
	COMP_CH			= (1 << 19),
	COMP_RF			= (1 << 20),
	COMP_FIRMWARE		= (1 << 21),
	COMP_HT			= (1 << 22),
	COMP_RESET		= (1 << 23),
	COMP_CMDPKT		= (1 << 24),
	COMP_SCAN		= (1 << 25),
	COMP_PS			= (1 << 26),
	COMP_DOWN		= (1 << 27),
	COMP_INTR		= (1 << 28),
	COMP_LED		= (1 << 29),
	COMP_MLME		= (1 << 30),
	COMP_ERR		= (1 << 31)
};

#define RT_TRACE(component, x, args...)		\
do {			\
	if (rt_global_debug_component & component) \
		printk(KERN_DEBUG DRV_NAME ":" x "\n" , \
		       ##args);\
} while (0)

#define assert(expr) \
do {	\
	if (!(expr)) {				  \
		pr_info("Assertion failed! %s,%s,%s,line=%d\n", \
		#expr, __FILE__, __func__, __LINE__);	  \
	}	\
} while (0)

#endif
