/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_MAX96714_H__
#define __VEHICLE_AD_MAX96714_H__

int max96714_ad_init(struct vehicle_ad_dev *ad);
int max96714_ad_deinit(void);
int max96714_ad_get_cfg(struct vehicle_cfg **cfg);
void max96714_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int max96714_check_id(struct vehicle_ad_dev *ad);
int max96714_stream(struct vehicle_ad_dev *ad, int enable);
void max96714_channel_set(struct vehicle_ad_dev *ad, int channel);

#endif
