/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2010-2024 Analog Devices Inc.
 * Copyright (c) 2025 Baylibre, SAS
 */
#ifndef __LINUX_PLATFORM_DATA_AD7606_H__
#define __LINUX_PLATFORM_DATA_AD7606_H__

struct iio_backend;

struct ad7606_platform_data {
	int (*bus_reg_read)(struct iio_backend *back, u32 reg, u32 *val);
	int (*bus_reg_write)(struct iio_backend *back, u32 reg, u32 val);
};

#endif /* __LINUX_PLATFORM_DATA_AD7606_H__ */
