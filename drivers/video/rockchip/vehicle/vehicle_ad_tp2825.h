/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_TP2825_H__
#define __VEHICLE_AD_TP2825_H__

int tp2825_ad_init(struct vehicle_ad_dev *ad);
int tp2825_ad_deinit(void);
int tp2825_ad_get_cfg(struct vehicle_cfg **cfg);
void tp2825_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int tp2825_check_id(struct vehicle_ad_dev *ad);
int tp2825_stream(struct vehicle_ad_dev *ad, int enable);
void tp2825_channel_set(struct vehicle_ad_dev *ad, int channel);

#endif
