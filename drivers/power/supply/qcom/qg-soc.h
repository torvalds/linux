/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QG_SOC_H__
#define __QG_SOC_H__

int qg_scale_soc(struct qpnp_qg *chip, bool force_soc);
int qg_soc_init(struct qpnp_qg *chip);
void qg_soc_exit(struct qpnp_qg *chip);
int qg_adjust_sys_soc(struct qpnp_qg *chip);

extern struct device_attribute dev_attr_soc_interval_ms;
extern struct device_attribute dev_attr_soc_cold_interval_ms;
extern struct device_attribute dev_attr_maint_soc_update_ms;
extern struct device_attribute dev_attr_fvss_delta_soc_interval_ms;
extern struct device_attribute dev_attr_fvss_vbat_scaling;
extern struct device_attribute dev_attr_qg_ss_feature;

#endif /* __QG_SOC_H__ */
