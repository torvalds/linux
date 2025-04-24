/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_RESOURCES_H__
#define __IRIS_RESOURCES_H__

struct iris_core;

int iris_enable_power_domains(struct iris_core *core, struct device *pd_dev);
int iris_disable_power_domains(struct iris_core *core, struct device *pd_dev);
int iris_unset_icc_bw(struct iris_core *core);
int iris_set_icc_bw(struct iris_core *core, unsigned long icc_bw);
int iris_disable_unprepare_clock(struct iris_core *core, enum platform_clk_type clk_type);
int iris_prepare_enable_clock(struct iris_core *core, enum platform_clk_type clk_type);

#endif
