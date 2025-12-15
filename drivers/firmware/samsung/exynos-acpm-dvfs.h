/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2025 Linaro Ltd.
 */
#ifndef __EXYNOS_ACPM_DVFS_H__
#define __EXYNOS_ACPM_DVFS_H__

#include <linux/types.h>

struct acpm_handle;

int acpm_dvfs_set_rate(const struct acpm_handle *handle,
		       unsigned int acpm_chan_id, unsigned int id,
		       unsigned long rate);
unsigned long acpm_dvfs_get_rate(const struct acpm_handle *handle,
				 unsigned int acpm_chan_id,
				 unsigned int clk_id);

#endif /* __EXYNOS_ACPM_DVFS_H__ */
