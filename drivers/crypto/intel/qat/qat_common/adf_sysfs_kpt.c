// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation */
#include <linux/sysfs.h>
#include <linux/types.h>

#include "adf_cfg.h"
#include "adf_cfg_services.h"
#include "adf_common_drv.h"
#include "adf_kpt.h"
#include "adf_sysfs_kpt.h"

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	user_data = GET_KPT_USER_DATA(accel_dev);

	return sysfs_emit(buf, "%d\n", user_data->enable);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_hw_device_data *hw_data;
	struct adf_accel_dev *accel_dev;
	bool enable;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down before enabling KPT\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	if (adf_get_service_enabled(accel_dev) != SVC_ASYM) {
		dev_info(dev, "KPT can only be enabled when the asymmetric service is enabled\n");
		return -EINVAL;
	}

	hw_data = GET_HW_DATA(accel_dev);

	/*
	 * Restore the KPT capability bit in the device's capabilities mask
	 * before processing user input, as the bit may have been cleared if
	 * KPT was previously disabled by the user.
	 */
	hw_data->accel_capabilities_mask = hw_data->get_accel_cap(accel_dev);
	if (!hw_data->accel_capabilities_mask)
		return -EINVAL;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	user_data = GET_KPT_USER_DATA(accel_dev);
	user_data->enable = enable;

	return count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t swk_shared_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	user_data = GET_KPT_USER_DATA(accel_dev);

	return sysfs_emit(buf, "%d\n", user_data->swk_shared);
}

static ssize_t swk_shared_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;
	bool swk_shared;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down before setting swk_shared\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	ret = kstrtobool(buf, &swk_shared);
	if (ret)
		return ret;

	user_data = GET_KPT_USER_DATA(accel_dev);
	user_data->swk_shared = swk_shared;

	return count;
}
static DEVICE_ATTR_RW(swk_shared);

static ssize_t swk_max_ttl_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	user_data = GET_KPT_USER_DATA(accel_dev);

	return sysfs_emit(buf, "%u\n", user_data->swk_max_ttl);
}

static ssize_t swk_max_ttl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct adf_kpt_hw_data *kpt_data;
	struct adf_accel_dev *accel_dev;
	unsigned int swk_max_ttl;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down before setting swk_max_ttl\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	ret = kstrtouint(buf, 10, &swk_max_ttl);
	if (ret)
		return ret;

	kpt_data = GET_KPT_CFG_DATA(accel_dev);

	if (swk_max_ttl > kpt_data->max_swk_ttl) {
		dev_info(dev, "Configuration value is out of range (%u - %u)\n",
			 0, kpt_data->max_swk_ttl);
		return -EINVAL;
	}

	kpt_data->user_input.swk_max_ttl = swk_max_ttl;

	return count;
}
static DEVICE_ATTR_RW(swk_max_ttl);

static ssize_t swk_cnt_per_fn_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	user_data = GET_KPT_USER_DATA(accel_dev);

	return sysfs_emit(buf, "%u\n", user_data->swk_cnt_per_fn);
}

static ssize_t swk_cnt_per_fn_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct adf_kpt_hw_data *kpt_data;
	struct adf_accel_dev *accel_dev;
	unsigned int swk_cnt_per_fn;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down before setting swk_cnt_per_fn\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	ret = kstrtouint(buf, 10, &swk_cnt_per_fn);
	if (ret)
		return ret;

	kpt_data = GET_KPT_CFG_DATA(accel_dev);

	if (swk_cnt_per_fn > kpt_data->max_swk_cnt_per_fn_pasid) {
		dev_info(dev, "swk_cnt_per_fn: value out of range (0 - %u)\n",
			 kpt_data->max_swk_cnt_per_fn_pasid);
		return -EINVAL;
	}

	kpt_data->user_input.swk_cnt_per_fn = swk_cnt_per_fn;

	return count;
}
static DEVICE_ATTR_RW(swk_cnt_per_fn);

static ssize_t swk_cnt_per_pasid_show(struct device *dev, struct device_attribute *attr,
				      char *buf)
{
	struct adf_kpt_interface_data *user_data;
	struct adf_accel_dev *accel_dev;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	user_data = GET_KPT_USER_DATA(accel_dev);

	return sysfs_emit(buf, "%u\n", user_data->swk_cnt_per_pasid);
}

static ssize_t swk_cnt_per_pasid_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct adf_kpt_hw_data *kpt_data;
	struct adf_accel_dev *accel_dev;
	unsigned int swk_cnt_per_pasid;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down before setting swk_cnt_per_pasid\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	ret = kstrtouint(buf, 10, &swk_cnt_per_pasid);
	if (ret)
		return ret;

	kpt_data = GET_KPT_CFG_DATA(accel_dev);

	if (swk_cnt_per_pasid > kpt_data->max_swk_cnt_per_fn_pasid) {
		dev_info(dev, "swk_cnt_per_pasid: value out of range (0 - %u)\n",
			 kpt_data->max_swk_cnt_per_fn_pasid);
		return -EINVAL;
	}

	kpt_data->user_input.swk_cnt_per_pasid = swk_cnt_per_pasid;

	return count;
}
static DEVICE_ATTR_RW(swk_cnt_per_pasid);

static struct attribute *qat_kpt_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_swk_shared.attr,
	&dev_attr_swk_max_ttl.attr,
	&dev_attr_swk_cnt_per_fn.attr,
	&dev_attr_swk_cnt_per_pasid.attr,
	NULL,
};

static const struct attribute_group qat_kpt_group = {
	.attrs = qat_kpt_attrs,
	.name = "qat_kpt",
};

int adf_sysfs_init_kpt(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = devm_device_add_group(&GET_DEV(accel_dev), &qat_kpt_group);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to create qat_kpt attribute group\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adf_sysfs_init_kpt);
