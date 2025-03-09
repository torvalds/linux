/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_berlin_berlin driver.
 */

#ifndef __GOODIX_BERLIN_H_
#define __GOODIX_BERLIN_H_

#include <linux/pm.h>

#define GOODIX_BERLIN_FW_VERSION_INFO_ADDR_A	0x1000C
#define GOODIX_BERLIN_FW_VERSION_INFO_ADDR_D	0x10014

#define GOODIX_BERLIN_IC_INFO_ADDR_A		0x10068
#define GOODIX_BERLIN_IC_INFO_ADDR_D		0x10070

struct goodix_berlin_ic_data {
	int fw_version_info_addr;
	int ic_info_addr;
	ssize_t read_dummy_len;
	ssize_t read_prefix_len;
};

struct device;
struct input_id;
struct regmap;

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap,
			const struct goodix_berlin_ic_data *ic_data);

extern const struct dev_pm_ops goodix_berlin_pm_ops;
extern const struct attribute_group *goodix_berlin_groups[];

#endif
