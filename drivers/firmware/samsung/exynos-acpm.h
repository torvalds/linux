/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */
#ifndef __EXYNOS_ACPM_H__
#define __EXYNOS_ACPM_H__

struct acpm_xfer {
	const u32 *txd;
	u32 *rxd;
	size_t txlen;
	size_t rxlen;
	unsigned int acpm_chan_id;
};

struct acpm_handle;

int acpm_do_xfer(const struct acpm_handle *handle,
		 const struct acpm_xfer *xfer);

#endif /* __EXYNOS_ACPM_H__ */
