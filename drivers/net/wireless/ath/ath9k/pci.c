/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/nl80211.h>
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/ath9k_platform.h>
#include <linux/module.h>
#include "ath9k.h"

static const struct pci_device_id ath_pci_id_table[] = {
	{ PCI_VDEVICE(ATHEROS, 0x0023) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0024) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0027) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0029) }, /* PCI   */

#ifdef CONFIG_ATH9K_PCOEM
	/* Mini PCI AR9220 MB92 cards: Compex WLM200NX, Wistron DNMA-92 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0029,
			 PCI_VENDOR_ID_ATHEROS,
			 0x2096),
	  .driver_data = ATH9K_PCI_LED_ACT_HI },
#endif

	{ PCI_VDEVICE(ATHEROS, 0x002A) }, /* PCI-E */

#ifdef CONFIG_ATH9K_PCOEM
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1C71),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE01F),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x11AD, /* LITEON */
			 0x6632),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x11AD, /* LITEON */
			 0x6642),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 PCI_VENDOR_ID_QMI,
			 0x0306),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x185F, /* WNC */
			 0x309D),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x10CF, /* Fujitsu */
			 0x147C),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x10CF, /* Fujitsu */
			 0x147D),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002A,
			 0x10CF, /* Fujitsu */
			 0x1536),
	  .driver_data = ATH9K_PCI_D3_L1_WAR },

	/* AR9285 card for Asus */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x002B,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2C37),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
#endif

	{ PCI_VDEVICE(ATHEROS, 0x002B) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002C) }, /* PCI-E 802.11n bonded out */
	{ PCI_VDEVICE(ATHEROS, 0x002D) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002E) }, /* PCI-E */

	/* Killer Wireless (3x3) */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0030,
			 0x1A56,
			 0x2000),
	  .driver_data = ATH9K_PCI_KILLER },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0030,
			 0x1A56,
			 0x2001),
	  .driver_data = ATH9K_PCI_KILLER },

	{ PCI_VDEVICE(ATHEROS, 0x0030) }, /* PCI-E  AR9300 */

#ifdef CONFIG_ATH9K_PCOEM
	/* PCI-E CUS198 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2086),
	  .driver_data = ATH9K_PCI_CUS198 | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1237),
	  .driver_data = ATH9K_PCI_CUS198 | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2126),
	  .driver_data = ATH9K_PCI_CUS198 | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x126A),
	  .driver_data = ATH9K_PCI_CUS198 | ATH9K_PCI_BT_ANT_DIV },

	/* PCI-E CUS230 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2152),
	  .driver_data = ATH9K_PCI_CUS230 | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE075),
	  .driver_data = ATH9K_PCI_CUS230 | ATH9K_PCI_BT_ANT_DIV },

	/* WB225 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3119),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3122),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x185F, /* WNC */
			 0x3119),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x185F, /* WNC */
			 0x3027),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x4105),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x4106),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x410D),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x410E),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x410F),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0xC706),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0xC680),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_SAMSUNG,
			 0xC708),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_LENOVO,
			 0x3218),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_LENOVO,
			 0x3219),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },

	/* AR9485 cards with PLL power-save disabled by default. */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2C97),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2100),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x1C56, /* ASKEY */
			 0x4001),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x11AD, /* LITEON */
			 0x6627),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x11AD, /* LITEON */
			 0x6628),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE04E),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE04F),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x144F, /* ASKEY */
			 0x7197),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x1B9A, /* XAVI */
			 0x2000),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x1B9A, /* XAVI */
			 0x2001),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1186),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1F86),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1195),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_AZWAVE,
			 0x1F95),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x1B9A, /* XAVI */
			 0x1C00),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 0x1B9A, /* XAVI */
			 0x1C01),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0032,
			 PCI_VENDOR_ID_ASUSTEK,
			 0x850D),
	  .driver_data = ATH9K_PCI_NO_PLL_PWRSAVE },
#endif

	{ PCI_VDEVICE(ATHEROS, 0x0032) }, /* PCI-E  AR9485 */
	{ PCI_VDEVICE(ATHEROS, 0x0033) }, /* PCI-E  AR9580 */

#ifdef CONFIG_ATH9K_PCOEM
	/* PCI-E CUS217 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2116),
	  .driver_data = ATH9K_PCI_CUS217 },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x11AD, /* LITEON */
			 0x6661),
	  .driver_data = ATH9K_PCI_CUS217 },

	/* AR9462 with WoW support */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3117),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_LENOVO,
			 0x3214),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_ATTANSIC,
			 0x0091),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2110),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_ASUSTEK,
			 0x850E),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x11AD, /* LITEON */
			 0x6631),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x11AD, /* LITEON */
			 0x6641),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 PCI_VENDOR_ID_HP,
			 0x1864),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x14CD, /* USI */
			 0x0063),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x14CD, /* USI */
			 0x0064),
	  .driver_data = ATH9K_PCI_WOW },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0034,
			 0x10CF, /* Fujitsu */
			 0x1783),
	  .driver_data = ATH9K_PCI_WOW },

	/* Killer Wireless (2x2) */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0030,
			 0x1A56,
			 0x2003),
	  .driver_data = ATH9K_PCI_KILLER },

	{ PCI_VDEVICE(ATHEROS, 0x0034) }, /* PCI-E  AR9462 */
	{ PCI_VDEVICE(ATHEROS, 0x0037) }, /* PCI-E  AR1111/AR9485 */

	/* CUS252 */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3028),
	  .driver_data = ATH9K_PCI_CUS252 |
			 ATH9K_PCI_AR9565_2ANT |
			 ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2176),
	  .driver_data = ATH9K_PCI_CUS252 |
			 ATH9K_PCI_AR9565_2ANT |
			 ATH9K_PCI_BT_ANT_DIV },

	/* WB335 1-ANT */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE068),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x185F, /* WNC */
			 0xA119),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0632),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x06B2),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0842),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x1842),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x6671),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x2811),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x2812),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x28A1),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x28A3),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x218A),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2F8A),
	  .driver_data = ATH9K_PCI_AR9565_1ANT },

	/* WB335 1-ANT / Antenna Diversity */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3025),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3026),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x302B),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE069),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x185F, /* WNC */
			 0x3028),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0622),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0672),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0662),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x06A2),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0682),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x213A),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x213C),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_HP,
			 0x18E3),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_HP,
			 0x217F),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_HP,
			 0x2005),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_DELL,
			 0x020C),
	  .driver_data = ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_BT_ANT_DIV },

	/* WB335 2-ANT / Antenna-Diversity */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x411A),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x411B),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x411C),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x411D),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x411E),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x4129),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_SAMSUNG,
			 0x412A),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x3027),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ATHEROS,
			 0x302C),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0642),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0652),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0612),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0832),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x1832),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0692),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0803),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x11AD, /* LITEON */
			 0x0813),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2130),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x213B),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2182),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x218B),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x218C),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_AZWAVE,
			 0x2F82),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x144F, /* ASKEY */
			 0x7202),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x2810),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x2813),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x28A2),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x1B9A, /* XAVI */
			 0x28A4),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x185F, /* WNC */
			 0x3027),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 0x185F, /* WNC */
			 0xA120),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE07F),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE08F),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE081),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE091),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_FOXCONN,
			 0xE099),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_LENOVO,
			 0x3026),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_LENOVO,
			 0x4026),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_ASUSTEK,
			 0x85F2),
	  .driver_data = ATH9K_PCI_AR9565_2ANT | ATH9K_PCI_BT_ANT_DIV },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ATHEROS,
			 0x0036,
			 PCI_VENDOR_ID_DELL,
			 0x020E),
	  .driver_data = ATH9K_PCI_AR9565_2ANT |
			 ATH9K_PCI_BT_ANT_DIV |
			 ATH9K_PCI_LED_ACT_HI},

	/* PCI-E AR9565 (WB335) */
	{ PCI_VDEVICE(ATHEROS, 0x0036),
	  .driver_data = ATH9K_PCI_BT_ANT_DIV },
#endif

	{ 0 }
};


/* return bus cachesize in 4B word units */
static void ath_pci_read_cachesize(struct ath_common *common, int *csz)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	u8 u8tmp;

	pci_read_config_byte(to_pci_dev(sc->dev), PCI_CACHE_LINE_SIZE, &u8tmp);
	*csz = (int)u8tmp;

	/*
	 * This check was put in to avoid "unpleasant" consequences if
	 * the bootrom has not fully initialized all PCI devices.
	 * Sometimes the cache line size register is not set
	 */

	if (*csz == 0)
		*csz = DEFAULT_CACHELINE >> 2;   /* Use the default size */
}

static bool ath_pci_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct ath9k_platform_data *pdata = sc->dev->platform_data;

	if (pdata && !pdata->use_eeprom) {
		if (off >= (ARRAY_SIZE(pdata->eeprom_data))) {
			ath_err(common,
				"%s: eeprom read failed, offset %08x is out of range\n",
				__func__, off);
		}

		*data = pdata->eeprom_data[off];
	} else {
		struct ath_hw *ah = (struct ath_hw *) common->ah;

		common->ops->read(ah, AR5416_EEPROM_OFFSET +
				      (off << AR5416_EEPROM_S));

		if (!ath9k_hw_wait(ah,
				   AR_EEPROM_STATUS_DATA,
				   AR_EEPROM_STATUS_DATA_BUSY |
				   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0,
				   AH_WAIT_TIMEOUT)) {
			return false;
		}

		*data = MS(common->ops->read(ah, AR_EEPROM_STATUS_DATA),
			   AR_EEPROM_STATUS_DATA_VAL);
	}

	return true;
}

/* Need to be called after we discover btcoex capabilities */
static void ath_pci_aspm_init(struct ath_common *common)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct pci_dev *pdev = to_pci_dev(sc->dev);
	struct pci_dev *parent;
	u16 aspm;

	if (!ah->is_pciexpress)
		return;

	parent = pdev->bus->self;
	if (!parent)
		return;

	if ((ath9k_hw_get_btcoex_scheme(ah) != ATH_BTCOEX_CFG_NONE) &&
	    (AR_SREV_9285(ah))) {
		/* Bluetooth coexistence requires disabling ASPM. */
		pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL,
			PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1);

		/*
		 * Both upstream and downstream PCIe components should
		 * have the same ASPM settings.
		 */
		pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
			PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1);

		ath_info(common, "Disabling ASPM since BTCOEX is enabled\n");
		return;
	}

	/*
	 * 0x70c - Ack Frequency Register.
	 *
	 * Bits 27:29 - DEFAULT_L1_ENTRANCE_LATENCY.
	 *
	 * 000 : 1 us
	 * 001 : 2 us
	 * 010 : 4 us
	 * 011 : 8 us
	 * 100 : 16 us
	 * 101 : 32 us
	 * 110/111 : 64 us
	 */
	if (AR_SREV_9462(ah))
		pci_read_config_dword(pdev, 0x70c, &ah->config.aspm_l1_fix);

	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &aspm);
	if (aspm & (PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1)) {
		ah->aspm_enabled = true;
		/* Initialize PCIe PM and SERDES registers. */
		ath9k_hw_configpcipowersave(ah, false);
		ath_info(common, "ASPM enabled: 0x%x\n", aspm);
	}
}

static const struct ath_bus_ops ath_pci_bus_ops = {
	.ath_bus_type = ATH_PCI,
	.read_cachesize = ath_pci_read_cachesize,
	.eeprom_read = ath_pci_eeprom_read,
	.aspm_init = ath_pci_aspm_init,
};

static int ath_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	u8 csz;
	u32 val;
	int ret = 0;
	char hw_name[64];

	if (pcim_enable_device(pdev))
		return -EIO;

	ret =  pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("32-bit DMA not available\n");
		return ret;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("32-bit DMA consistent DMA enable failed\n");
		return ret;
	}

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &csz);
	if (csz == 0) {
		/*
		 * Linux 2.4.18 (at least) writes the cache line size
		 * register as a 16-bit wide register which is wrong.
		 * We must have this setup properly for rx buffer
		 * DMA to work so force a reasonable value here if it
		 * comes up zero.
		 */
		csz = L1_CACHE_BYTES / sizeof(u32);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems. It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	ret = pcim_iomap_regions(pdev, BIT(0), "ath9k");
	if (ret) {
		dev_err(&pdev->dev, "PCI memory region reserve error\n");
		return -ENODEV;
	}

	ath9k_fill_chanctx_ops();
	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (!hw) {
		dev_err(&pdev->dev, "No memory for ieee80211_hw\n");
		return -ENOMEM;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->mem = pcim_iomap_table(pdev)[0];
	sc->driver_data = id->driver_data;

	ret = request_irq(pdev->irq, ath_isr, IRQF_SHARED, "ath9k", sc);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_irq;
	}

	sc->irq = pdev->irq;

	ret = ath9k_init_device(id->device, sc, &ath_pci_bus_ops);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize device\n");
		goto err_init;
	}

	ath9k_hw_name(sc->sc_ah, hw_name, sizeof(hw_name));
	wiphy_info(hw->wiphy, "%s mem=0x%lx, irq=%d\n",
		   hw_name, (unsigned long)sc->mem, pdev->irq);

	return 0;

err_init:
	free_irq(sc->irq, sc);
err_irq:
	ieee80211_free_hw(hw);
	return ret;
}

static void ath_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;

	if (!is_ath9k_unloaded)
		sc->sc_ah->ah_flags |= AH_UNPLUGGED;
	ath9k_deinit_device(sc);
	free_irq(sc->irq, sc);
	ieee80211_free_hw(sc->hw);
}

#ifdef CONFIG_PM_SLEEP

static int ath_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (test_bit(ATH_OP_WOW_ENABLED, &common->op_flags)) {
		dev_info(&pdev->dev, "WOW is enabled, bypassing PCI suspend\n");
		return 0;
	}

	/* The device has to be moved to FULLSLEEP forcibly.
	 * Otherwise the chip never moved to full sleep,
	 * when no interface is up.
	 */
	ath9k_stop_btcoex(sc);
	ath9k_hw_disable(sc->sc_ah);
	del_timer_sync(&sc->sleep_timer);
	ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_FULL_SLEEP);

	return 0;
}

static int ath_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 val;

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	ath_pci_aspm_init(common);
	ah->reset_power_on = false;

	return 0;
}

static SIMPLE_DEV_PM_OPS(ath9k_pm_ops, ath_pci_suspend, ath_pci_resume);

#define ATH9K_PM_OPS	(&ath9k_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define ATH9K_PM_OPS	NULL

#endif /* !CONFIG_PM_SLEEP */


MODULE_DEVICE_TABLE(pci, ath_pci_id_table);

static struct pci_driver ath_pci_driver = {
	.name       = "ath9k",
	.id_table   = ath_pci_id_table,
	.probe      = ath_pci_probe,
	.remove     = ath_pci_remove,
	.driver.pm  = ATH9K_PM_OPS,
};

int ath_pci_init(void)
{
	return pci_register_driver(&ath_pci_driver);
}

void ath_pci_exit(void)
{
	pci_unregister_driver(&ath_pci_driver);
}
