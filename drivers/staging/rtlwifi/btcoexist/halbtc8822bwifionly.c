// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#include "halbt_precomp.h"

void ex_hal8822b_wifi_only_hw_config(struct wifi_only_cfg *wifionlycfg)
{
	/*BB control*/
	halwifionly_phy_set_bb_reg(wifionlycfg, 0x4c, 0x01800000, 0x2);
	/*SW control*/
	halwifionly_phy_set_bb_reg(wifionlycfg, 0xcb4, 0xff, 0x77);
	/*antenna mux switch */
	halwifionly_phy_set_bb_reg(wifionlycfg, 0x974, 0x300, 0x3);

	halwifionly_phy_set_bb_reg(wifionlycfg, 0x1990, 0x300, 0x0);

	halwifionly_phy_set_bb_reg(wifionlycfg, 0xcbc, 0x80000, 0x0);
	/*switch to WL side controller and gnt_wl gnt_bt debug signal */
	halwifionly_phy_set_bb_reg(wifionlycfg, 0x70, 0xff000000, 0x0e);
	/*gnt_wl=1 , gnt_bt=0*/
	halwifionly_phy_set_bb_reg(wifionlycfg, 0x1704, 0xffffffff, 0x7700);
	halwifionly_phy_set_bb_reg(wifionlycfg, 0x1700, 0xffffffff, 0xc00f0038);
}

void ex_hal8822b_wifi_only_scannotify(struct wifi_only_cfg *wifionlycfg,
				      u8 is_5g)
{
	hal8822b_wifi_only_switch_antenna(wifionlycfg, is_5g);
}

void ex_hal8822b_wifi_only_switchbandnotify(struct wifi_only_cfg *wifionlycfg,
					    u8 is_5g)
{
	hal8822b_wifi_only_switch_antenna(wifionlycfg, is_5g);
}

void hal8822b_wifi_only_switch_antenna(struct wifi_only_cfg *wifionlycfg,
				       u8 is_5g)
{
	if (is_5g)
		halwifionly_phy_set_bb_reg(wifionlycfg, 0xcbc, 0x300, 0x1);
	else
		halwifionly_phy_set_bb_reg(wifionlycfg, 0xcbc, 0x300, 0x2);
}
