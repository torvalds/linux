/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2024 Analog Devices Inc.
 * Copyright (c) 2024 Baylibre, SAS
 */
#ifndef __LINUX_PLATFORM_DATA_AD3552R_HS_H__
#define __LINUX_PLATFORM_DATA_AD3552R_HS_H__

struct iio_backend;

struct ad3552r_hs_platform_data {
	int (*bus_reg_read)(struct iio_backend *back, u32 reg, u32 *val,
			    size_t data_size);
	int (*bus_reg_write)(struct iio_backend *back, u32 reg, u32 val,
			     size_t data_size);
	u32 bus_sample_data_clock_hz;
};

#endif /* __LINUX_PLATFORM_DATA_AD3552R_HS_H__ */
