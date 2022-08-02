/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_MAIN_H
#define __VEHICLE_MAIN_H

/* impl by vehicle_main, call by ad detect */
void vehicle_ad_stat_change_notify(void);
void vehicle_cif_stat_change_notify(void);
void vehicle_gpio_stat_change_notify(void);
void vehicle_cif_error_notify(int last_line);
void vehicle_android_is_ready_notify(void);
void vehicle_apk_state_change(char crtc[22]);

#endif
