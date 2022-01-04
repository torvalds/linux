/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common code for Intel Cherry Trail ACPI INT33FE pseudo device drivers
 * (USB Micro-B and Type-C connector variants), header file
 *
 * Copyright (c) 2019 Yauhen Kharuzhy <jekhor@gmail.com>
 */

#ifndef _INTEL_CHT_INT33FE_COMMON_H
#define _INTEL_CHT_INT33FE_COMMON_H

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/i2c.h>

enum int33fe_hw_type {
	INT33FE_HW_MICROB,
	INT33FE_HW_TYPEC,
};

struct cht_int33fe_data {
	struct device *dev;

	int (*probe)(struct cht_int33fe_data *data);
	int (*remove)(struct cht_int33fe_data *data);

	struct i2c_client *battery_fg;

	/* Type-C only */
	struct i2c_client *fusb302;
	struct i2c_client *pi3usb30532;

	struct fwnode_handle *dp;
};

int cht_int33fe_microb_probe(struct cht_int33fe_data *data);
int cht_int33fe_microb_remove(struct cht_int33fe_data *data);
int cht_int33fe_typec_probe(struct cht_int33fe_data *data);
int cht_int33fe_typec_remove(struct cht_int33fe_data *data);

#endif /* _INTEL_CHT_INT33FE_COMMON_H */
