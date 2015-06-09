/*
 * ChromeOS EC multi-function device
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The ChromeOS EC multi function device is used to mux all the requests
 * to the EC device for its multiple features: keyboard controller,
 * battery charging and regulator control, firmware update.
 */

#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/cros_ec.h>

static const struct mfd_cell cros_devs[] = {
	{
		.name = "cros-ec-ctl",
		.id = PLATFORM_DEVID_AUTO,
	},
};

int cros_ec_register(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	int err = 0;

	if (ec_dev->din_size) {
		ec_dev->din = devm_kzalloc(dev, ec_dev->din_size, GFP_KERNEL);
		if (!ec_dev->din)
			return -ENOMEM;
	}
	if (ec_dev->dout_size) {
		ec_dev->dout = devm_kzalloc(dev, ec_dev->dout_size, GFP_KERNEL);
		if (!ec_dev->dout)
			return -ENOMEM;
	}

	mutex_init(&ec_dev->lock);

	err = mfd_add_devices(dev, 0, cros_devs,
			      ARRAY_SIZE(cros_devs),
			      NULL, ec_dev->irq, NULL);
	if (err) {
		dev_err(dev, "failed to add mfd devices\n");
		return err;
	}

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		err = of_platform_populate(dev->of_node, NULL, NULL, dev);
		if (err) {
			mfd_remove_devices(dev);
			dev_err(dev, "Failed to register sub-devices\n");
			return err;
		}
	}

	dev_info(dev, "Chrome EC device registered\n");

	return 0;
}
EXPORT_SYMBOL(cros_ec_register);

int cros_ec_remove(struct cros_ec_device *ec_dev)
{
	mfd_remove_devices(ec_dev->dev);

	return 0;
}
EXPORT_SYMBOL(cros_ec_remove);

#ifdef CONFIG_PM_SLEEP
int cros_ec_suspend(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;

	if (device_may_wakeup(dev))
		ec_dev->wake_enabled = !enable_irq_wake(ec_dev->irq);

	disable_irq(ec_dev->irq);
	ec_dev->was_wake_device = ec_dev->wake_enabled;

	return 0;
}
EXPORT_SYMBOL(cros_ec_suspend);

int cros_ec_resume(struct cros_ec_device *ec_dev)
{
	enable_irq(ec_dev->irq);

	if (ec_dev->wake_enabled) {
		disable_irq_wake(ec_dev->irq);
		ec_dev->wake_enabled = 0;
	}

	return 0;
}
EXPORT_SYMBOL(cros_ec_resume);

#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC core driver");
