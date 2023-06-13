/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QG_BATTERY_PROFILE_H__
#define __QG_BATTERY_PROFILE_H__

int qg_batterydata_init(struct device_node *node);
void qg_batterydata_exit(void);
int lookup_soc_ocv(u32 *soc, u32 ocv_uv, int batt_temp, bool charging);
int qg_get_nominal_capacity(u32 *nom_cap_uah, int batt_temp, bool charging);

#endif /* __QG_BATTERY_PROFILE_H__ */
