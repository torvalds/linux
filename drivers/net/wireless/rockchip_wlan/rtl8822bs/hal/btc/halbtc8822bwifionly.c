/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#include "mp_precomp.h"


VOID
ex_hal8822b_wifi_only_hw_config(
	IN struct wifi_only_cfg  *pwifionlycfg
	)
{
	/*BB control*/
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x4c, 0x01800000, 0x2);
	/*SW control*/
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcb4, 0xff, 0x77);
	/*antenna mux switch */
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x974, 0x300, 0x3);

	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1990, 0x300, 0x0);

	halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcbc, 0x80000, 0x0);
	/*switch to WL side controller and gnt_wl gnt_bt debug signal */
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x70, 0xff000000, 0x0e);
	/*gnt_wl=1 , gnt_bt=0*/
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1704, 0xffffffff, 0x7700);
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1700, 0xffffffff, 0xc00f0038);
}

VOID
ex_hal8822b_wifi_only_scannotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{
	hal8822b_wifi_only_switch_antenna(pwifionlycfg, is_5g);
}

VOID
ex_hal8822b_wifi_only_switchbandnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{
	hal8822b_wifi_only_switch_antenna(pwifionlycfg, is_5g);
}

VOID
ex_hal8822b_wifi_only_connectnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{
	hal8822b_wifi_only_switch_antenna(pwifionlycfg, is_5g);
}


VOID
hal8822b_wifi_only_switch_antenna(IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{

	if (is_5g)
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcbc, 0x300, 0x1);
	else
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcbc, 0x300, 0x2);
}
