
/* Definitions for RTL8187SE hardware
 *
 * Copyright 2009 Larry Finger <Larry.Finger@lwfinger.net>
 * Copyright 2014 Andrea Merello <andrea.merello@gmail.com>
 *
 * Based on the r8180 and Realtek r8187se drivers, which are:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Also based on the rtl8187 driver, which is:
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andrea.merello@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL8187SE_RTL8225_H
#define RTL8187SE_RTL8225_H

#define RTL8225SE_ANAPARAM_ON	0xb0054d00
#define RTL8225SE_ANAPARAM2_ON	0x000004c6

/* all off except PLL */
#define RTL8225SE_ANAPARAM_OFF	0xb0054dec
/* all on including PLL */
#define RTL8225SE_ANAPARAM_OFF2	0xb0054dfc

#define RTL8225SE_ANAPARAM2_OFF	0x00ff04c6

#define RTL8225SE_ANAPARAM3	0x10

enum rtl8187se_power_state {
	RTL8187SE_POWER_ON,
	RTL8187SE_POWER_OFF,
	RTL8187SE_POWER_SLEEP
};

static inline void rtl8225se_write_phy_ofdm(struct ieee80211_hw *dev,
					  u8 addr, u8 data)
{
	rtl8180_write_phy(dev, addr, data);
}

static inline void rtl8225se_write_phy_cck(struct ieee80211_hw *dev,
					 u8 addr, u8 data)
{
	rtl8180_write_phy(dev, addr, data | 0x10000);
}


const struct rtl818x_rf_ops *rtl8187se_detect_rf(struct ieee80211_hw *);
void rtl8225se_rf_stop(struct ieee80211_hw *dev);
void rtl8225se_rf_set_channel(struct ieee80211_hw *dev,
				     struct ieee80211_conf *conf);
void rtl8225se_rf_conf_erp(struct ieee80211_hw *dev,
				  struct ieee80211_bss_conf *info);
void rtl8225se_rf_init(struct ieee80211_hw *dev);

#endif /* RTL8187SE_RTL8225_H */
