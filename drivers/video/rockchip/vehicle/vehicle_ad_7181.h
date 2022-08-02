/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_7181_H__
#define __VEHICLE_AD_7181_H__

int adv7181_ad_init(struct vehicle_ad_dev *ad);
int adv7181_ad_deinit(void);
int adv7181_ad_get_cfg(struct vehicle_cfg **cfg);
int adv7181_stream(struct vehicle_ad_dev *ad, int value);
void adv7181_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int adv7181_check_id(struct vehicle_ad_dev *ad);
void adv7181_channel_set(struct vehicle_ad_dev *ad, int channel);

#endif

