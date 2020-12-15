/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for hmc5843 driver
 *
 * Split from hmc5843.c
 * Copyright (C) Josef Gajdusek <atx@atx.name>
 */

#ifndef HMC5843_CORE_H
#define HMC5843_CORE_H

#include <linux/regmap.h>
#include <linux/iio/iio.h>

#define HMC5843_CONFIG_REG_A			0x00
#define HMC5843_CONFIG_REG_B			0x01
#define HMC5843_MODE_REG			0x02
#define HMC5843_DATA_OUT_MSB_REGS		0x03
#define HMC5843_STATUS_REG			0x09
#define HMC5843_ID_REG				0x0a
#define HMC5843_ID_END				0x0c

enum hmc5843_ids {
	HMC5843_ID,
	HMC5883_ID,
	HMC5883L_ID,
	HMC5983_ID,
};

/**
 * struct hmc5843_data	- device specific data
 * @dev:		actual device
 * @lock:		update and read regmap data
 * @regmap:		hardware access register maps
 * @variant:		describe chip variants
 * @buffer:		3x 16-bit channels + padding + 64-bit timestamp
 */
struct hmc5843_data {
	struct device *dev;
	struct mutex lock;
	struct regmap *regmap;
	const struct hmc5843_chip_info *variant;
	struct iio_mount_matrix orientation;
	__be16 buffer[8];
};

int hmc5843_common_probe(struct device *dev, struct regmap *regmap,
			 enum hmc5843_ids id, const char *name);
int hmc5843_common_remove(struct device *dev);

int hmc5843_common_suspend(struct device *dev);
int hmc5843_common_resume(struct device *dev);

#ifdef CONFIG_PM_SLEEP
static __maybe_unused SIMPLE_DEV_PM_OPS(hmc5843_pm_ops,
					hmc5843_common_suspend,
					hmc5843_common_resume);
#define HMC5843_PM_OPS (&hmc5843_pm_ops)
#else
#define HMC5843_PM_OPS NULL
#endif

#endif /* HMC5843_CORE_H */
