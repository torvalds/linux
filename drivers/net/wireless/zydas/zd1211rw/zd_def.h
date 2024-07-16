/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 */

#ifndef _ZD_DEF_H
#define _ZD_DEF_H

#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/device.h>

typedef u16 __nocast zd_addr_t;

#define dev_printk_f(level, dev, fmt, args...) \
	dev_printk(level, dev, "%s() " fmt, __func__, ##args)

#ifdef DEBUG
#  define dev_dbg_f(dev, fmt, args...) \
	  dev_printk_f(KERN_DEBUG, dev, fmt, ## args)
#  define dev_dbg_f_limit(dev, fmt, args...) do { \
	if (net_ratelimit()) \
		dev_printk_f(KERN_DEBUG, dev, fmt, ## args); \
} while (0)
#  define dev_dbg_f_cond(dev, cond, fmt, args...) ({ \
	bool __cond = !!(cond); \
	if (unlikely(__cond)) \
		dev_printk_f(KERN_DEBUG, dev, fmt, ## args); \
})
#else
#  define dev_dbg_f(dev, fmt, args...) do { (void)(dev); } while (0)
#  define dev_dbg_f_limit(dev, fmt, args...) do { (void)(dev); } while (0)
#  define dev_dbg_f_cond(dev, cond, fmt, args...) do { (void)(dev); } while (0)
#endif /* DEBUG */

#ifdef DEBUG
#  define ZD_ASSERT(x) \
do { \
	if (unlikely(!(x))) { \
		pr_debug("%s:%d ASSERT %s VIOLATED!\n", \
			__FILE__, __LINE__, __stringify(x)); \
		dump_stack(); \
	} \
} while (0)
#else
#  define ZD_ASSERT(x) do { } while (0)
#endif

#ifdef DEBUG
#  define ZD_MEMCLEAR(pointer, size) memset((pointer), 0xff, (size))
#else
#  define ZD_MEMCLEAR(pointer, size) do { } while (0)
#endif

#endif /* _ZD_DEF_H */
