/*
 * Radio tuning definitions for RTL8225 on RTL8187
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
 *
 * Based on the r8187 driver, which is:
 * Copyright 2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL8187_RTL8225_H
#define RTL8187_RTL8225_H

#define RTL8225_ANAPARAM_ON	0xa0000a59
#define RTL8225_ANAPARAM2_ON	0x860c7312
#define RTL8225_ANAPARAM_OFF	0xa00beb59
#define RTL8225_ANAPARAM2_OFF	0x840dec11

const struct rtl818x_rf_ops * rtl8187_detect_rf(struct ieee80211_hw *);

static inline void rtl8225_write_phy_ofdm(struct ieee80211_hw *dev,
					  u8 addr, u32 data)
{
	rtl8187_write_phy(dev, addr, data);
}

static inline void rtl8225_write_phy_cck(struct ieee80211_hw *dev,
					 u8 addr, u32 data)
{
	rtl8187_write_phy(dev, addr, data | 0x10000);
}

#endif /* RTL8187_RTL8225_H */
