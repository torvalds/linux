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
#ifndef __INC_HAL8822BWIFIONLYHWCFG_H
#define __INC_HAL8822BWIFIONLYHWCFG_H

VOID
ex_hal8822b_wifi_only_hw_config(
	IN struct wifi_only_cfg *pwifionlycfg
	);
VOID
ex_hal8822b_wifi_only_scannotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
VOID
ex_hal8822b_wifi_only_switchbandnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
VOID
ex_hal8822b_wifi_only_connectnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
VOID
hal8822b_wifi_only_switch_antenna(IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	);
#endif
