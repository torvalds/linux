/*
 * Linux RFKILL support for RTL8187
 *
 * Copyright (c) 2009 Herton Ronaldo Krzesinski <herton@mandriva.com.br>
 *
 * Based on the RFKILL handling in the r8187 driver, which is:
 * Copyright (c) Realtek Semiconductor Corp. All rights reserved.
 *
 * Thanks to Realtek for their support!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/usb.h>
#include <net/mac80211.h>

#include "rtl8187.h"
#include "rtl8187_rfkill.h"

static bool rtl8187_is_radio_enabled(struct rtl8187_priv *priv)
{
	u8 gpio;

	gpio = rtl818x_ioread8(priv, &priv->map->GPIO0);
	rtl818x_iowrite8(priv, &priv->map->GPIO0, gpio & ~0x02);
	gpio = rtl818x_ioread8(priv, &priv->map->GPIO1);

	return gpio & 0x02;
}

void rtl8187_rfkill_init(struct ieee80211_hw *hw)
{
	struct rtl8187_priv *priv = hw->priv;

	priv->rfkill_off = rtl8187_is_radio_enabled(priv);
	printk(KERN_INFO "rtl8187: wireless switch is %s\n",
	       priv->rfkill_off ? "on" : "off");
	wiphy_rfkill_set_hw_state(hw->wiphy, !priv->rfkill_off);
	wiphy_rfkill_start_polling(hw->wiphy);
}

void rtl8187_rfkill_poll(struct ieee80211_hw *hw)
{
	bool enabled;
	struct rtl8187_priv *priv = hw->priv;

	mutex_lock(&priv->conf_mutex);
	enabled = rtl8187_is_radio_enabled(priv);
	if (unlikely(enabled != priv->rfkill_off)) {
		priv->rfkill_off = enabled;
		printk(KERN_INFO "rtl8187: wireless radio switch turned %s\n",
		       enabled ? "on" : "off");
		wiphy_rfkill_set_hw_state(hw->wiphy, !enabled);
	}
	mutex_unlock(&priv->conf_mutex);
}

void rtl8187_rfkill_exit(struct ieee80211_hw *hw)
{
	wiphy_rfkill_stop_polling(hw->wiphy);
}
