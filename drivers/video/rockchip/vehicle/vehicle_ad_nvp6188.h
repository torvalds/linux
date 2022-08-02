/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_NVP6188_H__
#define __VEHICLE_AD_NVP6188_H__

int nvp6188_ad_init(struct vehicle_ad_dev *ad);
int nvp6188_ad_deinit(void);
int nvp6188_ad_get_cfg(struct vehicle_cfg **cfg);
void nvp6188_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int nvp6188_check_id(struct vehicle_ad_dev *ad);
int nvp6188_stream(struct vehicle_ad_dev *ad, int enable);
void nvp6188_channel_set(struct vehicle_ad_dev *ad, int channel);

#endif
