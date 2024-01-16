// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"

void goya_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!hdev->pdev)
		return;

	switch (freq) {
	case PLL_HIGH:
		hl_fw_set_frequency(hdev, HL_GOYA_MME_PLL, hdev->high_pll);
		hl_fw_set_frequency(hdev, HL_GOYA_TPC_PLL, hdev->high_pll);
		hl_fw_set_frequency(hdev, HL_GOYA_IC_PLL, hdev->high_pll);
		break;
	case PLL_LOW:
		hl_fw_set_frequency(hdev, HL_GOYA_MME_PLL, GOYA_PLL_FREQ_LOW);
		hl_fw_set_frequency(hdev, HL_GOYA_TPC_PLL, GOYA_PLL_FREQ_LOW);
		hl_fw_set_frequency(hdev, HL_GOYA_IC_PLL, GOYA_PLL_FREQ_LOW);
		break;
	case PLL_LAST:
		hl_fw_set_frequency(hdev, HL_GOYA_MME_PLL, goya->mme_clk);
		hl_fw_set_frequency(hdev, HL_GOYA_TPC_PLL, goya->tpc_clk);
		hl_fw_set_frequency(hdev, HL_GOYA_IC_PLL, goya->ic_clk);
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

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_MME_PLL, false);

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

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	if (goya->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_fw_set_frequency(hdev, HL_GOYA_MME_PLL, value);
	goya->mme_clk = value;

fail:
	return count;
}

static ssize_t tpc_clk_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_TPC_PLL, false);

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

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	if (goya->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_fw_set_frequency(hdev, HL_GOYA_TPC_PLL, value);
	goya->tpc_clk = value;

fail:
	return count;
}

static ssize_t ic_clk_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_IC_PLL, false);

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

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	if (goya->pm_mng_profile == PM_AUTO) {
		count = -EPERM;
		goto fail;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hl_fw_set_frequency(hdev, HL_GOYA_IC_PLL, value);
	goya->ic_clk = value;

fail:
	return count;
}

static ssize_t mme_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_MME_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t tpc_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_TPC_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t ic_clk_curr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, HL_GOYA_IC_PLL, true);

	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", value);
}

static ssize_t pm_mng_profile_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct goya_device *goya = hdev->asic_specific;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	return sprintf(buf, "%s\n",
			(goya->pm_mng_profile == PM_AUTO) ? "auto" :
			(goya->pm_mng_profile == PM_MANUAL) ? "manual" :
			"unknown");
}

static ssize_t pm_mng_profile_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct goya_device *goya = hdev->asic_specific;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto out;
	}

	mutex_lock(&hdev->fpriv_list_lock);

	if (hdev->is_compute_ctx_active) {
		dev_err(hdev->dev,
			"Can't change PM profile while compute context is opened on the device\n");
		count = -EPERM;
		goto unlock_mutex;
	}

	if (strncmp("auto", buf, strlen("auto")) == 0) {
		/* Make sure we are in LOW PLL when changing modes */
		if (goya->pm_mng_profile == PM_MANUAL) {
			goya->curr_pll_profile = PLL_HIGH;
			goya->pm_mng_profile = PM_AUTO;
			goya_set_frequency(hdev, PLL_LOW);
		}
	} else if (strncmp("manual", buf, strlen("manual")) == 0) {
		if (goya->pm_mng_profile == PM_AUTO) {
			/* Must release the lock because the work thread also
			 * takes this lock. But before we release it, set
			 * the mode to manual so nothing will change if a user
			 * suddenly opens the device
			 */
			goya->pm_mng_profile = PM_MANUAL;

			mutex_unlock(&hdev->fpriv_list_lock);

			/* Flush the current work so we can return to the user
			 * knowing that he is the only one changing frequencies
			 */
			if (goya->goya_work)
				flush_delayed_work(&goya->goya_work->work_freq);

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

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	return sprintf(buf, "%u\n", hdev->high_pll);
}

static ssize_t high_pll_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	if (!hl_device_operational(hdev, NULL)) {
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

static struct attribute *goya_clk_dev_attrs[] = {
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

static ssize_t infineon_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct cpucp_info *cpucp_info;

	cpucp_info = &hdev->asic_prop.cpucp_info;

	return sprintf(buf, "%#04x\n", le32_to_cpu(cpucp_info->infineon_version));
}

static DEVICE_ATTR_RO(infineon_ver);

static struct attribute *goya_vrm_dev_attrs[] = {
	&dev_attr_infineon_ver.attr,
	NULL,
};

void goya_add_device_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp,
				struct attribute_group *dev_vrm_attr_grp)
{
	dev_clk_attr_grp->attrs = goya_clk_dev_attrs;
	dev_vrm_attr_grp->attrs = goya_vrm_dev_attrs;
}
