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

int goya_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk)
{
	long value;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	value = hl_get_frequency(hdev, MME_PLL, false);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device max clock %ld\n",
			value);
		return value;
	}

	*max_clk = (value / 1000 / 1000);

	value = hl_get_frequency(hdev, MME_PLL, true);

	if (value < 0) {
		dev_err(hdev->dev,
			"Failed to retrieve device current clock %ld\n",
			value);
		return value;
	}

	*cur_clk = (value / 1000 / 1000);

	return 0;
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

static ssize_t pm_mng_profile_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	return sprintf(buf, "%s\n",
			(hdev->pm_mng_profile == PM_AUTO) ? "auto" :
			(hdev->pm_mng_profile == PM_MANUAL) ? "manual" :
			"unknown");
}

static ssize_t pm_mng_profile_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto out;
	}

	mutex_lock(&hdev->fpriv_list_lock);

	if (hdev->compute_ctx) {
		dev_err(hdev->dev,
			"Can't change PM profile while compute context is opened on the device\n");
		count = -EPERM;
		goto unlock_mutex;
	}

	if (strncmp("auto", buf, strlen("auto")) == 0) {
		/* Make sure we are in LOW PLL when changing modes */
		if (hdev->pm_mng_profile == PM_MANUAL) {
			hdev->curr_pll_profile = PLL_HIGH;
			hl_device_set_frequency(hdev, PLL_LOW);
			hdev->pm_mng_profile = PM_AUTO;
		}
	} else if (strncmp("manual", buf, strlen("manual")) == 0) {
		if (hdev->pm_mng_profile == PM_AUTO) {
			/* Must release the lock because the work thread also
			 * takes this lock. But before we release it, set
			 * the mode to manual so nothing will change if a user
			 * suddenly opens the device
			 */
			hdev->pm_mng_profile = PM_MANUAL;

			mutex_unlock(&hdev->fpriv_list_lock);

			/* Flush the current work so we can return to the user
			 * knowing that he is the only one changing frequencies
			 */
			flush_delayed_work(&hdev->work_freq);

			return count;
		}
	} else {
		dev_err(hdev->dev, "value should be auto or manual\n");
		count = -EINVAL;
	}

unlock_mutex:
	mutex_unlock(&hdev->fpriv_list_lock);
out:
	return count;
}

static ssize_t high_pll_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	return sprintf(buf, "%u\n", hdev->high_pll);
}

static ssize_t high_pll_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto out;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	hdev->high_pll = value;

out:
	return count;
}

static DEVICE_ATTR_RW(high_pll);
static DEVICE_ATTR_RW(ic_clk);
static DEVICE_ATTR_RO(ic_clk_curr);
static DEVICE_ATTR_RW(mme_clk);
static DEVICE_ATTR_RO(mme_clk_curr);
static DEVICE_ATTR_RW(pm_mng_profile);
static DEVICE_ATTR_RW(tpc_clk);
static DEVICE_ATTR_RO(tpc_clk_curr);

static struct attribute *goya_dev_attrs[] = {
	&dev_attr_high_pll.attr,
	&dev_attr_ic_clk.attr,
	&dev_attr_ic_clk_curr.attr,
	&dev_attr_mme_clk.attr,
	&dev_attr_mme_clk_curr.attr,
	&dev_attr_pm_mng_profile.attr,
	&dev_attr_tpc_clk.attr,
	&dev_attr_tpc_clk_curr.attr,
	NULL,
};

void goya_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp)
{
	dev_attr_grp->attrs = goya_dev_attrs;
}
