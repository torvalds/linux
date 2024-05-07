// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/blkdev.h>
#define PATH_SIZE	64


static void rename_blk_device_name(struct device_node *np)
{
	dev_t devt;
	int index = 0;
	struct device_node *node = np;
	char dev_path[PATH_SIZE];
	const char *actual_name;
	char *modified_name;
	struct device *dev;
	struct block_device *bdev;

	while (!of_property_read_string_index(node, "actual-dev", index,
						&actual_name)) {
		memset(dev_path, '\0', PATH_SIZE);
		snprintf(dev_path, PATH_SIZE, "/dev/%s", actual_name);
		devt = name_to_dev_t(dev_path);
		if (!devt) {
			pr_err("rename-devices: No device path : %s\n", dev_path);
			return;
		}
		bdev = blkdev_get_by_dev(devt, FMODE_WRITE | FMODE_READ, NULL);
		dev = &bdev->bd_device;
		if (!dev) {
			pr_err("rename-devices: No device with dev path : %s\n", dev_path);
			return;
		}
		if (!of_property_read_string_index(node, "rename-dev", index,
							(const char **)&modified_name)) {
			device_rename(dev, modified_name);
		} else {
			pr_err("rename-devices: rename-dev for actual-dev = %s is missing\n",
								 actual_name);
			return;
		}
		index++;
	}
}

static int __init rename_devices_init(void)
{
	struct device_node *node = NULL, *child = NULL;
	const char *device_type;

	node = of_find_compatible_node(NULL, NULL, "qcom,rename-devices");
	if (!node) {
		pr_err("rename-devices: qcom,rename-devices node is missing\n");
		goto out;
	}

	for_each_child_of_node(node, child) {
		if (!of_property_read_string(child, "device-type", &device_type)) {
			if (strcmp(device_type, "block") == 0)
				rename_blk_device_name(child);
			else
				pr_err("rename-devices: unsupported device\n");
		} else
			pr_err("rename-devices: device-type is missing\n");
	}

out:
	of_node_put(node);
	return  0;
}

late_initcall(rename_devices_init);
MODULE_DESCRIPTION("Rename devices");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: virtio_blk");
MODULE_SOFTDEP("pre: virtio_mmio");
