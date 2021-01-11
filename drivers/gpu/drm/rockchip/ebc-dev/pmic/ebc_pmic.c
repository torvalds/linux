// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include "ebc_pmic.h"

#define EINK_VCOM_MAX 64
static int vcom = 0;

int ebc_pmic_set_vcom(struct ebc_pmic *pmic, int value)
{
	int ret;
	char data[EINK_VCOM_MAX] = { 0 };

	/* check vcom value */
	if (value <= VCOM_MIN_MV || value > VCOM_MAX_MV) {
		dev_err(pmic->dev, "vcom value should be %d~%d\n", VCOM_MIN_MV, VCOM_MAX_MV);
		return -1;
	}
	dev_info(pmic->dev, "set chip vcom to: %dmV\n", value);

	/* set pmic vcom */
	pmic->pmic_set_vcom(pmic, value);

	/* store vendor storage */
	memset(data, 0, EINK_VCOM_MAX);
	sprintf(data, "%d", value);
	dev_info(pmic->dev, "store vcom %d to vendor storage\n", value);

	ret = rk_vendor_write(EINK_VCOM_ID, (void *)data, EINK_VCOM_MAX);
	if (ret < 0) {
		dev_err(pmic->dev, "%s failed to write vendor storage\n", __func__);
		return ret;
	}

	return 0;
}

void ebc_pmic_verity_vcom(struct ebc_pmic *pmic)
{
	int ret;
	int value_chip;
	int value_vendor;

	//check vcom value
	value_vendor = vcom;
	if (value_vendor <= VCOM_MIN_MV || value_vendor > VCOM_MAX_MV) {
		dev_err(pmic->dev, "invaild vcom value %d from vendor storage\n", value_vendor);
		return;
	}
	value_chip = pmic->pmic_get_vcom(pmic);
	if (value_chip != value_vendor) {
		dev_info(pmic->dev, "chip_vcom %d != vendor_vcom %d, set vcom from vendor\n", value_chip, value_vendor);
		ret = pmic->pmic_set_vcom(pmic, value_vendor);
		if (ret) {
			dev_err(pmic->dev, "set vcom value failed\n");
		}
	}

	return;
}

module_param(vcom, int, 0644);
