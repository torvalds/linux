/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */
#ifndef __EXYNOS_ACPM_PMIC_H__
#define __EXYNOS_ACPM_PMIC_H__

#include <linux/types.h>

struct acpm_handle;

int acpm_pmic_read_reg(const struct acpm_handle *handle,
		       unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
		       u8 *buf);
int acpm_pmic_bulk_read(const struct acpm_handle *handle,
			unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			u8 count, u8 *buf);
int acpm_pmic_write_reg(const struct acpm_handle *handle,
			unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			u8 value);
int acpm_pmic_bulk_write(const struct acpm_handle *handle,
			 unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			 u8 count, const u8 *buf);
int acpm_pmic_update_reg(const struct acpm_handle *handle,
			 unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			 u8 value, u8 mask);
#endif /* __EXYNOS_ACPM_PMIC_H__ */
