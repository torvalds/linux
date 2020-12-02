// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Link Layer for Samsung S3FWRN5 NCI based Driver
 *
 * Copyright (C) 2015 Samsung Electrnoics
 * Robert Baldyga <r.baldyga@samsung.com>
 * Copyright (C) 2020 Samsung Electrnoics
 * Bongsu Jeon <bongsu.jeon@samsung.com>
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "phy_common.h"

void s3fwrn5_phy_set_wake(void *phy_id, bool wake)
{
	struct phy_common *phy = phy_id;

	mutex_lock(&phy->mutex);
	gpio_set_value(phy->gpio_fw_wake, wake);
	msleep(S3FWRN5_EN_WAIT_TIME);
	mutex_unlock(&phy->mutex);
}
EXPORT_SYMBOL(s3fwrn5_phy_set_wake);

bool s3fwrn5_phy_power_ctrl(struct phy_common *phy, enum s3fwrn5_mode mode)
{
	if (phy->mode == mode)
		return false;

	phy->mode = mode;

	gpio_set_value(phy->gpio_en, 1);
	gpio_set_value(phy->gpio_fw_wake, 0);
	if (mode == S3FWRN5_MODE_FW)
		gpio_set_value(phy->gpio_fw_wake, 1);

	if (mode != S3FWRN5_MODE_COLD) {
		msleep(S3FWRN5_EN_WAIT_TIME);
		gpio_set_value(phy->gpio_en, 0);
		msleep(S3FWRN5_EN_WAIT_TIME);
	}

	return true;
}
EXPORT_SYMBOL(s3fwrn5_phy_power_ctrl);

void s3fwrn5_phy_set_mode(void *phy_id, enum s3fwrn5_mode mode)
{
	struct phy_common *phy = phy_id;

	mutex_lock(&phy->mutex);

	s3fwrn5_phy_power_ctrl(phy, mode);

	mutex_unlock(&phy->mutex);
}
EXPORT_SYMBOL(s3fwrn5_phy_set_mode);

enum s3fwrn5_mode s3fwrn5_phy_get_mode(void *phy_id)
{
	struct phy_common *phy = phy_id;
	enum s3fwrn5_mode mode;

	mutex_lock(&phy->mutex);

	mode = phy->mode;

	mutex_unlock(&phy->mutex);

	return mode;
}
EXPORT_SYMBOL(s3fwrn5_phy_get_mode);
