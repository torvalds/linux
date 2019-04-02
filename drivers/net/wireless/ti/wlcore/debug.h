/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2011 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <coelho@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __DE_H__
#define __DE_H__

#include <linux/bitops.h>
#include <linux/printk.h>

#define DRIVER_NAME "wlcore"
#define DRIVER_PREFIX DRIVER_NAME ": "

enum {
	DE_NONE	= 0,
	DE_IRQ	= BIT(0),
	DE_SPI	= BIT(1),
	DE_BOOT	= BIT(2),
	DE_MAILBOX	= BIT(3),
	DE_TESTMODE	= BIT(4),
	DE_EVENT	= BIT(5),
	DE_TX	= BIT(6),
	DE_RX	= BIT(7),
	DE_SCAN	= BIT(8),
	DE_CRYPT	= BIT(9),
	DE_PSM	= BIT(10),
	DE_MAC80211	= BIT(11),
	DE_CMD	= BIT(12),
	DE_ACX	= BIT(13),
	DE_SDIO	= BIT(14),
	DE_FILTERS   = BIT(15),
	DE_ADHOC     = BIT(16),
	DE_AP	= BIT(17),
	DE_PROBE	= BIT(18),
	DE_IO	= BIT(19),
	DE_MASTER	= (DE_ADHOC | DE_AP),
	DE_ALL	= ~0,
};

extern u32 wl12xx_de_level;

#define DE_DUMP_LIMIT 1024

#define wl1271_error(fmt, arg...) \
	pr_err(DRIVER_PREFIX "ERROR " fmt "\n", ##arg)

#define wl1271_warning(fmt, arg...) \
	pr_warn(DRIVER_PREFIX "WARNING " fmt "\n", ##arg)

#define wl1271_notice(fmt, arg...) \
	pr_info(DRIVER_PREFIX fmt "\n", ##arg)

#define wl1271_info(fmt, arg...) \
	pr_info(DRIVER_PREFIX fmt "\n", ##arg)

/* define the de macro differently if dynamic de is supported */
#if defined(CONFIG_DYNAMIC_DE)
#define wl1271_de(level, fmt, arg...) \
	do { \
		if (unlikely(level & wl12xx_de_level)) \
			dynamic_pr_de(DRIVER_PREFIX fmt "\n", ##arg); \
	} while (0)
#else
#define wl1271_de(level, fmt, arg...) \
	do { \
		if (unlikely(level & wl12xx_de_level)) \
			printk(KERN_DE pr_fmt(DRIVER_PREFIX fmt "\n"), \
			       ##arg); \
	} while (0)
#endif

#define wl1271_dump(level, prefix, buf, len)				      \
	do {								      \
		if (level & wl12xx_de_level)				      \
			print_hex_dump_de(DRIVER_PREFIX prefix,	      \
					DUMP_PREFIX_OFFSET, 16, 1,	      \
					buf,				      \
					min_t(size_t, len, DE_DUMP_LIMIT), \
					0);				      \
	} while (0)

#define wl1271_dump_ascii(level, prefix, buf, len)			      \
	do {								      \
		if (level & wl12xx_de_level)				      \
			print_hex_dump_de(DRIVER_PREFIX prefix,	      \
					DUMP_PREFIX_OFFSET, 16, 1,	      \
					buf,				      \
					min_t(size_t, len, DE_DUMP_LIMIT), \
					true);				      \
	} while (0)

#endif /* __DE_H__ */
