/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL_CAM_H
#define _RTL_CAM_H

#include <linux/types.h>
struct net_device;

void rtl92e_cam_reset(struct net_device *dev);
void rtl92e_enable_hw_security_config(struct net_device *dev);
void rtl92e_set_key(struct net_device *dev, u8 EntryNo, u8 KeyIndex,
		    u16 KeyType, const u8 *MacAddr, u8 DefaultKey,
		    u32 *KeyContent);
void rtl92e_set_swcam(struct net_device *dev, u8 EntryNo, u8 KeyIndex,
		      u16 KeyType, const u8 *MacAddr, u32 *KeyContent);

#endif
