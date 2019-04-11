/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2016-2017  Realtek Corporation.*/

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
