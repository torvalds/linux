/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/stat.h>
#include <linux/vexpress.h>

static void vexpress_reset_do(struct device *dev, const char *what)
{
	int err = -ENOENT;
	struct vexpress_config_func *func =
			vexpress_config_func_get_by_dev(dev);

	if (func) {
		unsigned long timeout;

		err = vexpress_config_write(func, 0, 0);

		timeout = jiffies + HZ;
		while (time_before(jiffies, timeout))
			cpu_relax();
	}

	dev_emerg(dev, "Unable to %s (%d)\n", what, err);
}

static struct device *vexpress_power_off_device;

void vexpress_power_off(void)
{
	vexpress_reset_do(vexpress_power_off_device, "power off");
}

static struct device *vexpress_restart_device;

void vexpress_restart(char str, const char *cmd)
{
	vexpress_reset_do(vexpress_restart_device, "restart");
}

static ssize_t vexpress_reset_active_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vexpress_restart_device == dev);
}

static ssize_t vexpress_reset_active_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long value;
	int err = kstrtol(buf, 0, &value);

	if (!err && value)
		vexpress_restart_device = dev;

	return err ? err : count;
}

DEVICE_ATTR(active, S_IRUGO | S_IWUSR, vexpress_reset_active_show,
		vexpress_reset_active_store);


enum vexpress_reset_func { FUNC_RESET, FUNC_SHUTDOWN, FUNC_REBOOT };

static struct of_device_id vexpress_reset_of_match[] = {
	{
		.compatible = "arm,vexpress-reset",
		.data = (void *)FUNC_RESET,
	}, {
		.compatible = "arm,vexpress-shutdown",
		.data = (void *)FUNC_SHUTDOWN
	}, {
		.compatible = "arm,vexpress-reboot",
		.data = (void *)FUNC_REBOOT
	},
	{}
};

static int vexpress_reset_probe(struct platform_device *pdev)
{
	enum vexpress_reset_func func;
	const struct of_device_id *match =
			of_match_device(vexpress_reset_of_match, &pdev->dev);

	if (match)
		func = (enum vexpress_reset_func)match->data;
	else
		func = pdev->id_entry->driver_data;

	switch (func) {
	case FUNC_SHUTDOWN:
		vexpress_power_off_device = &pdev->dev;
		break;
	case FUNC_RESET:
		if (!vexpress_restart_device)
			vexpress_restart_device = &pdev->dev;
		device_create_file(&pdev->dev, &dev_attr_active);
		break;
	case FUNC_REBOOT:
		vexpress_restart_device = &pdev->dev;
		device_create_file(&pdev->dev, &dev_attr_active);
		break;
	};

	return 0;
}

static const struct platform_device_id vexpress_reset_id_table[] = {
	{ .name = "vexpress-reset", .driver_data = FUNC_RESET, },
	{ .name = "vexpress-shutdown", .driver_data = FUNC_SHUTDOWN, },
	{ .name = "vexpress-reboot", .driver_data = FUNC_REBOOT, },
	{}
};

static struct platform_driver vexpress_reset_driver = {
	.probe = vexpress_reset_probe,
	.driver = {
		.name = "vexpress-reset",
		.of_match_table = vexpress_reset_of_match,
	},
	.id_table = vexpress_reset_id_table,
};

static int __init vexpress_reset_init(void)
{
	return platform_driver_register(&vexpress_reset_driver);
}
device_initcall(vexpress_reset_init);
