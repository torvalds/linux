/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2026 Linaro Ltd.
 */
#ifndef __EXYNOS_ACPM_TMU_H__
#define __EXYNOS_ACPM_TMU_H__

#include <linux/types.h>

struct acpm_handle;

int acpm_tmu_init(struct acpm_handle *handle, unsigned int acpm_chan_id);
int acpm_tmu_read_temp(struct acpm_handle *handle, unsigned int acpm_chan_id,
		       u8 tz, int *temp);
int acpm_tmu_set_threshold(struct acpm_handle *handle,
			   unsigned int acpm_chan_id, u8 tz,
			   const u8 temperature[8], size_t tlen);
int acpm_tmu_set_interrupt_enable(struct acpm_handle *handle,
				  unsigned int acpm_chan_id, u8 tz, u8 inten);
int acpm_tmu_tz_control(struct acpm_handle *handle, unsigned int acpm_chan_id,
			u8 tz, bool enable);
int acpm_tmu_clear_tz_irq(struct acpm_handle *handle, unsigned int acpm_chan_id,
			  u8 tz);
int acpm_tmu_suspend(struct acpm_handle *handle, unsigned int acpm_chan_id);
int acpm_tmu_resume(struct acpm_handle *handle, unsigned int acpm_chan_id);
#endif /* __EXYNOS_ACPM_TMU_H__ */
