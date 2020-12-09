// SPDX-License-Identifier: GPL-2.0+
// Expose the vboot context nvram to userspace
//
// Copyright (C) 2012 Google, Inc.
// Copyright (C) 2015 Collabora Ltd.

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/slab.h>

#define DRV_NAME "cros-ec-vbc"

static ssize_t vboot_context_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *att, char *buf,
				  loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);
	struct cros_ec_device *ecdev = ec->ec_dev;
	struct ec_params_vbnvcontext *params;
	struct cros_ec_command *msg;
	int err;
	const size_t para_sz = sizeof(params->op);
	const size_t resp_sz = sizeof(struct ec_response_vbnvcontext);
	const size_t payload = max(para_sz, resp_sz);

	msg = kmalloc(sizeof(*msg) + payload, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	/* NB: we only kmalloc()ated enough space for the op field */
	params = (struct ec_params_vbnvcontext *)msg->data;
	params->op = EC_VBNV_CONTEXT_OP_READ;

	msg->version = EC_VER_VBNV_CONTEXT;
	msg->command = EC_CMD_VBNV_CONTEXT;
	msg->outsize = para_sz;
	msg->insize = resp_sz;

	err = cros_ec_cmd_xfer_status(ecdev, msg);
	if (err < 0) {
		dev_err(dev, "Error sending read request: %d\n", err);
		kfree(msg);
		return err;
	}

	memcpy(buf, msg->data, resp_sz);

	kfree(msg);
	return resp_sz;
}

static ssize_t vboot_context_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf,
				   loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);
	struct cros_ec_device *ecdev = ec->ec_dev;
	struct ec_params_vbnvcontext *params;
	struct cros_ec_command *msg;
	int err;
	const size_t para_sz = sizeof(*params);
	const size_t data_sz = sizeof(params->block);

	/* Only write full values */
	if (count != data_sz)
		return -EINVAL;

	msg = kmalloc(sizeof(*msg) + para_sz, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	params = (struct ec_params_vbnvcontext *)msg->data;
	params->op = EC_VBNV_CONTEXT_OP_WRITE;
	memcpy(params->block, buf, data_sz);

	msg->version = EC_VER_VBNV_CONTEXT;
	msg->command = EC_CMD_VBNV_CONTEXT;
	msg->outsize = para_sz;
	msg->insize = 0;

	err = cros_ec_cmd_xfer_status(ecdev, msg);
	if (err < 0) {
		dev_err(dev, "Error sending write request: %d\n", err);
		kfree(msg);
		return err;
	}

	kfree(msg);
	return data_sz;
}

static BIN_ATTR_RW(vboot_context, 16);

static struct bin_attribute *cros_ec_vbc_bin_attrs[] = {
	&bin_attr_vboot_context,
	NULL
};

static struct attribute_group cros_ec_vbc_attr_group = {
	.name = "vbc",
	.bin_attrs = cros_ec_vbc_bin_attrs,
};

static int cros_ec_vbc_probe(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);
	struct device *dev = &pd->dev;
	int ret;

	ret = sysfs_create_group(&ec_dev->class_dev.kobj,
				 &cros_ec_vbc_attr_group);
	if (ret < 0)
		dev_err(dev, "failed to create %s attributes. err=%d\n",
			cros_ec_vbc_attr_group.name, ret);

	return ret;
}

static int cros_ec_vbc_remove(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);

	sysfs_remove_group(&ec_dev->class_dev.kobj,
			   &cros_ec_vbc_attr_group);

	return 0;
}

static struct platform_driver cros_ec_vbc_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_ec_vbc_probe,
	.remove = cros_ec_vbc_remove,
};

module_platform_driver(cros_ec_vbc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Expose the vboot context nvram to userspace");
MODULE_ALIAS("platform:" DRV_NAME);
