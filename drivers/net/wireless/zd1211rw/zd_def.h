/* zd_def.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ZD_DEF_H
#define _ZD_DEF_H

#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/device.h>
#include <linux/kernel.h>

#define dev_printk_f(level, dev, fmt, args...) \
	dev_printk(level, dev, "%s() " fmt, __func__, ##args)

#ifdef DEBUG
#  define dev_dbg_f(dev, fmt, args...) \
	  dev_printk_f(KERN_DEBUG, dev, fmt, ## args)
#else
#  define dev_dbg_f(dev, fmt, args...) do { (void)(dev); } while (0)
#endif /* DEBUG */

#ifdef DEBUG
#  define ZD_ASSERT(x) \
do { \
	if (!(x)) { \
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
