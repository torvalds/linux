/******************************************************************************
 *
 * Copyright(c) 2016-2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __INC_HAL8822BWIFIONLYHWCFG_H
#define __INC_HAL8822BWIFIONLYHWCFG_H

void ex_hal8822b_wifi_only_hw_config(struct wifi_only_cfg *wifionlycfg);
void ex_hal8822b_wifi_only_scannotify(struct wifi_only_cfg *wifionlycfg,
				      u8 is_5g);
void ex_hal8822b_wifi_only_switchbandnotify(struct wifi_only_cfg *wifionlycfg,
					    u8 is_5g);
void hal8822b_wifi_only_switch_antenna(struct wifi_only_cfg *wifionlycfg,
				       u8 is_5g);
#endif
