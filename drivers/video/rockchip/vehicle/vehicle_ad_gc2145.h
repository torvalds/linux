/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_GC2145_H__
#define __VEHICLE_AD_GC2145_H__

int gc2145_ad_init(struct vehicle_ad_dev *ad);
int gc2145_ad_deinit(void);
int gc2145_ad_get_cfg(struct vehicle_cfg **cfg);
void gc2145_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int gc2145_check_id(struct vehicle_ad_dev *ad);
int gc2145_stream(struct vehicle_ad_dev *ad, int enable);
void gc2145_channel_set(struct vehicle_ad_dev *ad, int channel);

#endif
