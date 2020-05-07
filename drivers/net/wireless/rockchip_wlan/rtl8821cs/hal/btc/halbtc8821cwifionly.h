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
#ifndef __INC_HAL8821CWIFIONLYHWCFG_H
#define __INC_HAL8821CWIFIONLYHWCFG_H


struct rfe_type_8821c_wifi_only {

	u8			rfe_module_type;
	boolean		ext_ant_switch_exist;
	u8			ext_ant_switch_type;			/* 0:DPDT, 1:SPDT */
	u8			ext_ant_switch_ctrl_polarity;		/*  iF 0: DPDT_P=0, DPDT_N=1 => BTG to Main, WL_A+G to Aux */

	boolean		ant_at_main_port;

	boolean		wlg_Locate_at_btg;				/*  If true:  WLG at BTG, If false: WLG at WLAG */

	boolean		ext_ant_switch_diversity;		/* If diversity on */
};

enum bt_8821c_wifi_only_ext_ant_switch_type {
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_DPDT		= 0x0,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_SPDT		= 0x1,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_NONE			= 0x2,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_MAX
};

enum bt_8821c_wifi_only_ext_ant_switch_ctrl_type {
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_BBSW		= 0x0,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_PTA		= 0x1,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_ANTDIV	= 0x2,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_MAC		= 0x3,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_BT		= 0x4,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_MAX
};

enum bt_8821c_wifi_only_ext_ant_switch_pos_type {
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_BT				= 0x0,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLG			= 0x1,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLA			= 0x2,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_NOCARE			= 0x3,
	BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_MAX
};


VOID
hal8821c_wifi_only_switch_antenna(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);

VOID
halbtc8821c_wifi_only_set_rfe_type(
	IN struct wifi_only_cfg *pwifionlycfg
	);


VOID
ex_hal8821c_wifi_only_hw_config(
	IN struct wifi_only_cfg *pwifionlycfg
	);
VOID
ex_hal8821c_wifi_only_scannotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
VOID
ex_hal8821c_wifi_only_switchbandnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
VOID
ex_hal8821c_wifi_only_connectnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);

#endif
