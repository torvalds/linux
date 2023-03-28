// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>

#define MPAM_ALGO_STR	0x4D50414D4558544E  /* "MPAMEXTN" */

enum mpam_profiling_param_ids {
	PARAM_CACHE_PORTION = 1,
};

struct mpam_cache_portion {
	uint32_t part_id;
	uint32_t cache_portion;
};

static struct mpam_cache_portion cur_cache_portion;
static struct kobject mpam_kobj;
static struct scmi_protocol_handle *ph;
static const struct qcom_scmi_vendor_ops *ops;
static struct scmi_device *sdev;


static int qcom_mpam_set_cache_portion(struct mpam_cache_portion *msg)
{
	int ret = 0;

	ret = ops->set_param(ph, msg, MPAM_ALGO_STR, PARAM_CACHE_PORTION,
				sizeof(*msg));

	if (!ret)
		memcpy(&cur_cache_portion, msg, sizeof(*msg));

	return ret;
}

struct qcom_mpam_attr {
	struct attribute		attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

#define to_mpam_attr(_attr) \
	container_of(_attr, struct qcom_mpam_attr, attr)
#define MPAM_ATTR_RW(_name)						\
static struct qcom_mpam_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)			\


static ssize_t store_set_cache_portion(struct kobject *kobj,
				  struct attribute *attr, const char *buf,
				  size_t count)
{
	int i, ret = -EINVAL;
	unsigned int val[2];
	char *str, *s = kstrdup(buf, GFP_KERNEL);
	struct mpam_cache_portion msg;

	for (i = 0; i < 2; i++) {
		str = strsep(&s, " ");
		if (!str)
			goto out;
		ret = kstrtouint(str, 10, &val[i]);
		if (ret < 0) {
			pr_err("Invalid value :%d\n", ret);
			goto out;
		}
	}

	msg.part_id = val[0];
	msg.cache_portion = val[1];

	ret = qcom_mpam_set_cache_portion(&msg);

out:
	kfree(s);
	return ((ret < 0) ? ret : count);
}

static ssize_t show_set_cache_portion(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\t%u\n", cur_cache_portion.part_id,
					cur_cache_portion.cache_portion);
}

MPAM_ATTR_RW(set_cache_portion);

static struct attribute *mpam_settings_attrs[] = {
	&set_cache_portion.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mpam_settings);

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
			 char *buf)
{
	struct qcom_mpam_attr *mpam_attr = to_mpam_attr(attr);
	ssize_t ret = -EIO;

	if (mpam_attr->show)
		ret = mpam_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
			  const char *buf, size_t count)
{
	struct qcom_mpam_attr *mpam_attr = to_mpam_attr(attr);
	ssize_t ret = -EIO;

	if (mpam_attr->store)
		ret = mpam_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops mpam_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};
static struct kobj_type mpam_settings_ktype = {
	.sysfs_ops		= &mpam_sysfs_ops,
	.default_groups		= mpam_settings_groups,
};

static int mpam_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	sdev = get_qcom_scmi_device();
	if (IS_ERR(sdev)) {
		ret = PTR_ERR(sdev);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error getting scmi_dev ret=%d\n", ret);
		return ret;
	}
	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_VENDOR_PROTOCOL, &ph);
	if (IS_ERR(ops)) {
		ret = PTR_ERR(ops);
		dev_err(dev, "Error getting vendor protocol ops: %d\n", ret);
		return ret;
	}

	ret = kobject_init_and_add(&mpam_kobj, &mpam_settings_ktype,
				   &cpu_subsys.dev_root->kobj, "mpam");
	if (ret < 0) {
		dev_err(dev, "failed to init mpam kobj: %d\n", ret);
		kobject_put(&mpam_kobj);
	}

	return ret;
}

static const struct of_device_id qcom_mpam_table[] = {
	{ .compatible = "qcom,mpam" },
	{},
};

static struct platform_driver qcom_mpam_driver = {
	.driver = {
		.name = "qcom-mpam",
		.of_match_table = qcom_mpam_table,
	},
	.probe = mpam_dev_probe,
};

module_platform_driver(qcom_mpam_driver);
MODULE_SOFTDEP("pre: qcom_scmi_client");
MODULE_DESCRIPTION("QCOM MPAM driver");
MODULE_LICENSE("GPL");
