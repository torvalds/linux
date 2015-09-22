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

#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/stat.h>
#include <linux/vexpress.h>

static void vexpress_reset_do(struct device *dev, const char *what)
{
	int err = -ENOENT;
	struct regmap *reg = dev_get_drvdata(dev);

	if (reg) {
		err = regmap_write(reg, 0, 0);
		if (!err)
			mdelay(1000);
	}

	dev_emerg(dev, "Unable to %s (%d)\n", what, err);
}

static struct device *vexpress_power_off_device;

static void vexpress_power_off(void)
{
	vexpress_reset_do(vexpress_power_off_device, "power off");
}

static struct device *vexpress_restart_device;

static int vexpress_restart(struct notifier_block *this, unsigned long mode,
			     void *cmd)
{
	vexpress_reset_do(vexpress_restart_device, "restart");

	return NOTIFY_DONE;
}

static struct notifier_block vexpress_restart_nb = {
	.notifier_call = vexpress_restart,
	.priority = 128,
};

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

static const struct of_device_id vexpress_reset_of_match[] = {
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

static int _vexpress_register_restart_handler(struct device *dev)
{
	int err;

	vexpress_restart_device = dev;
	err = register_restart_handler(&vexpress_restart_nb);
	if (err) {
		dev_err(dev, "cannot register restart handler (err=%d)\n", err);
		return err;
	}
	device_create_file(dev, &dev_attr_active);

	return 0;
}

static int vexpress_reset_probe(struct platform_device *pdev)
{
	const struct of_device_id *match =
			of_match_device(vexpress_reset_of_match, &pdev->dev);
	struct regmap *regmap;
	int ret = 0;

	if (!match)
		return -EINVAL;

	regmap = devm_regmap_init_vexpress_config(&pdev->dev);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	dev_set_drvdata(&pdev->dev, regmap);

	switch ((enum vexpress_reset_func)match->data) {
	case FUNC_SHUTDOWN:
		vexpress_power_off_device = &pdev->dev;
		pm_power_off = vexpress_power_off;
		break;
	case FUNC_RESET:
		if (!vexpress_restart_device)
			ret = _vexpress_register_restart_handler(&pdev->dev);
		break;
	case FUNC_REBOOT:
		ret = _vexpress_register_restart_handler(&pdev->dev);
		break;
	};

	return ret;
}

static struct platform_driver vexpress_reset_driver = {
	.probe = vexpress_reset_probe,
	.driver = {
		.name = "vexpress-reset",
		.of_match_table = vexpress_reset_of_match,
	},
};

static int __init vexpress_reset_init(void)
{
	return platform_driver_register(&vexpress_reset_driver);
}
device_initcall(vexpress_reset_init);
