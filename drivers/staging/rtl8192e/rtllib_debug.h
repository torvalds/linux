/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL_DEBUG_H
#define _RTL_DEBUG_H

#include <linux/bits.h>

/* Allow files to override DRV_NAME */
#ifndef DRV_NAME
#define DRV_NAME "rtllib_92e"
#endif

extern u32 rt_global_debug_component;

/* These are the defines for rt_global_debug_component */
enum RTL_DEBUG {
	COMP_TRACE		= BIT(0),
	COMP_DBG		= BIT(1),
	COMP_INIT		= BIT(2),
	COMP_RECV		= BIT(3),
	COMP_POWER		= BIT(6),
	COMP_SWBW		= BIT(8),
	COMP_SEC		= BIT(9),
	COMP_LPS		= BIT(10),
	COMP_QOS		= BIT(11),
	COMP_RATE		= BIT(12),
	COMP_RXDESC		= BIT(13),
	COMP_PHY		= BIT(14),
	COMP_DIG		= BIT(15),
	COMP_TXAGC		= BIT(16),
	COMP_HALDM		= BIT(17),
	COMP_POWER_TRACKING	= BIT(18),
	COMP_CH			= BIT(19),
	COMP_RF			= BIT(20),
	COMP_FIRMWARE		= BIT(21),
	COMP_RESET		= BIT(23),
	COMP_CMDPKT		= BIT(24),
	COMP_SCAN		= BIT(25),
	COMP_PS			= BIT(26),
	COMP_DOWN		= BIT(27),
	COMP_INTR		= BIT(28),
	COMP_ERR		= BIT(31)
};

#endif
