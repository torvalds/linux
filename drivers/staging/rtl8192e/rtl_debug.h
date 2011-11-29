/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
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

#define DMESG(x, a...)

extern u32 rt_global_debug_component;

#define RT_TRACE(component, x, args...)		\
do {			\
	if (rt_global_debug_component & component) \
		printk(KERN_DEBUG DRV_NAME ":" x "\n" , \
		       ##args);\
} while (0);

#define assert(expr) \
	if (!(expr)) {				  \
		printk(KERN_INFO "Assertion failed! %s,%s,%s,line=%d\n", \
		#expr, __FILE__, __func__, __LINE__);	  \
	}

/* proc stuff */
void rtl8192_proc_init_one(struct net_device *dev);
void rtl8192_proc_remove_one(struct net_device *dev);
void rtl8192_proc_module_init(void);
void rtl8192_proc_module_remove(void);

#endif
