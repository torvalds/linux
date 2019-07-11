// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"

void goya_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	struct goya_device *goya = hdev->asic_specific;

	switch (freq) {
	case PLL_HIGH:
		hl_set_frequency(hdev, MME_PLL, hdev->high_pll);
		hl_set_frequency(hdev, TPC_PLL, hdev->high_pll);
		hl_set_frequency(hdev, IC_PLL, hdev->high_pll);
		break;
	case PLL_LOW:
		hl_set_frequency(hdev, MME_PLL, GOYA_PLL_FREQ_LOW);
		hl_set_frequency(hdev, TPC_PLL, GOYA_PLL_FREQ_LOW);
		hl_set_frequency(hdev, IC_PLL, GOYA_PLL_FREQ_LOW);
		break;
	case PLL_LAST:
		hl_set_frequency(hdev, MME_PLL, goya->mme_clk);
		hl_set_frequency(hdev, TPC_PLL, goya->tpc_clk);
		hl_set_frequency(hdev, IC_PLL, goya->ic_clk);
		break;
	default:
		dev_err(hdev->dev, "unknown frequency setting\n");
	}
}

static ssize_t mme_clk_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, MME_PLL, false);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t mme_clk_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct goya_device *goya = hdev->asic_specific;
	int rc;
	long value;

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto fail;
	}

	if (hdev->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_set_frequency(hdev, MME_PLL, value);
	goya->mme_clk = value;

fail:
	return count;
}

static ssize_t tpc_clk_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, TPC_PLL, false);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t tpc_clk_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct goya_device *goya = hdev->asic_specific;
	int rc;
	long value;

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto fail;
	}

	if (hdev->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_set_frequency(hdev, TPC_PLL, value);
	goya->tpc_clk = value;

fail:
	return count;
}

static ssize_t ic_clk_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, IC_PLL, false);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t ic_clk_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct goya_device *goya = hdev->asic_specific;
	int rc;
	long value;

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto fail;
	}

	if (hdev->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_set_frequency(hdev, IC_PLL, value);
	goya->ic_clk = value;

fail:
	return count;
}

static ssize_t mme_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, MME_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t tpc_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, TPC_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t ic_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, IC_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static DEVICE_ATTR_RW(ic_clk);
static DEVICE_ATTR_RO(ic_clk_curr);
static DEVICE_ATTR_RW(mme_clk);
static DEVICE_ATTR_RO(mme_clk_curr);
static DEVICE_ATTR_RW(tpc_clk);
static DEVICE_ATTR_RO(tpc_clk_curr);

static struct attribute *goya_dev_attrs[] = {
	&dev_attr_ic_clk.attr,
	&dev_attr_ic_clk_curr.attr,
	&dev_attr_mme_clk.attr,
	&dev_attr_mme_clk_curr.attr,
	&dev_attr_tpc_clk.attr,
	&dev_attr_tpc_clk_curr.attr,
	NULL,
};

void goya_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp)
{
	dev_attr_grp->attrs = goya_dev_attrs;
}
