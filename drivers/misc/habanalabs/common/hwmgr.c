// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

void hl_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	hl_set_frequency(hdev, hdev->asic_prop.clk_pll_index,
			hdev->asic_prop.max_freq_value);
}

int hl_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk)
{
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device max clock %ld\n",
			value);
		return value;
	}

	*max_clk = (value / 1000 / 1000);

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);

	if (value < 0) {
		dev_err(hdev->dev,
			"Failed to retrieve device current clock %ld\n",
			value);
		return value;
	}

	*cur_clk = (value / 1000 / 1000);

	return 0;
}

static ssize_t clk_max_freq_mhz_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);

	hdev->asic_prop.max_freq_value = value;

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static ssize_t clk_max_freq_mhz_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	int rc;
	u64 value;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	rc = kstrtoull(buf, 0, &value);
	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hdev->asic_prop.max_freq_value = value * 1000 * 1000;

	hl_set_frequency(hdev, hdev->asic_prop.clk_pll_index,
			hdev->asic_prop.max_freq_value);

fail:
	return count;
}

static ssize_t clk_cur_freq_mhz_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static DEVICE_ATTR_RW(clk_max_freq_mhz);
static DEVICE_ATTR_RO(clk_cur_freq_mhz);

static struct attribute *hl_dev_attrs[] = {
	&dev_attr_clk_max_freq_mhz.attr,
	&dev_attr_clk_cur_freq_mhz.attr,
	NULL,
};

void hl_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp)
{
	dev_attr_grp->attrs = hl_dev_attrs;
}
