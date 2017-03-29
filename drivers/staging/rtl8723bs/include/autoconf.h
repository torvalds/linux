/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/


/*
 * Automatically generated C config: don't edit
 */

/*
 * Functions Config
 */
/* define CONFIG_DEBUG_CFG80211 */

#ifndef CONFIG_WIRELESS_EXT
#error CONFIG_WIRELESS_EXT needs to be enabled for this driver to work
#endif

/*
 * Auto Config Section
 */
#define LPS_RPWM_WAIT_MS 300
#ifndef DISABLE_BB_RF
#define DISABLE_BB_RF	0
#endif

#if DISABLE_BB_RF
	#define HAL_MAC_ENABLE	0
	#define HAL_BB_ENABLE		0
	#define HAL_RF_ENABLE		0
#else
	#define HAL_MAC_ENABLE	1
	#define HAL_BB_ENABLE		1
	#define HAL_RF_ENABLE		1
#endif

/*
 * Platform dependent
 */
#define WAKEUP_GPIO_IDX	12	/* WIFI Chip Side */
#ifdef CONFIG_WOWLAN
#define CONFIG_GTK_OL
#endif /* CONFIG_WOWLAN */

/*
 * Debug Related Config
 */
#undef CONFIG_DEBUG

#ifdef CONFIG_DEBUG
#define DBG	1	/*  for ODM & BTCOEX debug */
/*#define CONFIG_DEBUG_RTL871X */
#else /*  !CONFIG_DEBUG */
#define DBG	0	/*  for ODM & BTCOEX debug */
#endif /*  !CONFIG_DEBUG */

#define CONFIG_PROC_DEBUG

/* define DBG_XMIT_BUF */
/* define DBG_XMIT_BUF_EXT */
