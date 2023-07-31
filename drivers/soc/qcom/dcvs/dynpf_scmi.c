// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/slab.h>

static struct kobject dynpf_kobj;
static struct scmi_protocol_handle *ph;
static const struct qcom_scmi_vendor_ops *ops;
#define DYNPF_ALGO_STR	0x53434d495f444750 /* "SCMI_DGP" */
static struct scmi_device *sdev;

struct qcom_dynpf_attr {
	struct attribute		attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

enum dynpf_param_ids {
	PARAM_ENABLE_DYNPF = 1,
};

#define to_dynpf_attr(_attr) \
	container_of(_attr, struct qcom_dynpf_attr, attr)
#define DYNPF_ATTR_RW(_name)						\
static struct qcom_dynpf_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)			\

static ssize_t store_enable_dynpf(struct kobject *kobj,
				  struct attribute *attr, const char *buf,
				  size_t count)
{
	u32 val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = ops->set_param(ph, &val, DYNPF_ALGO_STR, PARAM_ENABLE_DYNPF,
				sizeof(u32));

	return ((ret < 0) ? ret : count);
}

static ssize_t show_enable_dynpf(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
	u32 val;
	int ret;

	ret = ops->get_param(ph, &val, DYNPF_ALGO_STR, PARAM_ENABLE_DYNPF, 0,
				sizeof(u32));
	if (ret < 0)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%lu\n", le32_to_cpu(val));
}

DYNPF_ATTR_RW(enable_dynpf);

static struct attribute *dynpf_settings_attrs[] = {
	&enable_dynpf.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dynpf_settings);

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
			 char *buf)
{
	struct qcom_dynpf_attr *dynpf_attr = to_dynpf_attr(attr);
	ssize_t ret = -EIO;

	if (dynpf_attr->show)
		ret = dynpf_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
			  const char *buf, size_t count)
{
	struct qcom_dynpf_attr *dynpf_attr = to_dynpf_attr(attr);
	ssize_t ret = -EIO;

	if (dynpf_attr->store)
		ret = dynpf_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops dynpf_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};
static struct kobj_type dynpf_settings_ktype = {
	.sysfs_ops		= &dynpf_sysfs_ops,
	.default_groups		= dynpf_settings_groups,
};

static int qcom_dynpf_probe(struct platform_device *pdev)
{
	int ret;

	sdev = get_qcom_scmi_device();
	if (IS_ERR(sdev)) {
		ret = PTR_ERR(sdev);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Error getting scmi_dev ret = %d\n", ret);
		return ret;
	}
	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_VENDOR_PROTOCOL, &ph);
	if (IS_ERR(ops)) {
		ret = PTR_ERR(ops);
		ops = NULL;
		return ret;
	}

	ret = kobject_init_and_add(&dynpf_kobj, &dynpf_settings_ktype,
				   &cpu_subsys.dev_root->kobj, "dynpf");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to init dynpf kobj: %d\n", ret);
		kobject_put(&dynpf_kobj);
	}

	return 0;
}

static const struct of_device_id dynpf_table[] = {
	{.compatible = "qcom,dynpf"},
	{},
};

static struct platform_driver dynpf_driver = {
	.driver = {
		.name = "qcom-dynpf",
		.of_match_table = dynpf_table,
	},
	.probe = qcom_dynpf_probe,
};

module_platform_driver(dynpf_driver);
MODULE_SOFTDEP("pre: qcom_scmi_client");
MODULE_DESCRIPTION("Qcom DYNPF SCMI driver");
MODULE_LICENSE("GPL");
