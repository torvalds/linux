/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL_PS_H
#define _RTL_PS_H

#include <linux/types.h>

struct net_device;

#define RT_CHECK_FOR_HANG_PERIOD 2

void rtl92e_hw_wakeup(struct net_device *dev);
void rtl92e_enter_sleep(struct net_device *dev, u64 time);
void rtl92e_rtllib_ips_leave_wq(struct net_device *dev);
void rtl92e_rtllib_ips_leave(struct net_device *dev);
void rtl92e_ips_leave_wq(void *data);

void rtl92e_ips_enter(struct net_device *dev);
void rtl92e_ips_leave(struct net_device *dev);

void rtl92e_leisure_ps_enter(struct net_device *dev);
void rtl92e_leisure_ps_leave(struct net_device *dev);

#endif
