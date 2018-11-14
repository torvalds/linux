// SPDX-License-Identifier: GPL-2.0
/*
 * Generic serial GNSS receiver driver
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#include <linux/errno.h>
#include <linux/gnss.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/serdev.h>
#include <linux/slab.h>

#include "serial.h"

static int gnss_serial_open(struct gnss_device *gdev)
{
	struct gnss_serial *gserial = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = gserial->serdev;
	int ret;

	ret = serdev_device_open(serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, gserial->speed);
	serdev_device_set_flow_control(serdev, false);

	ret = pm_runtime_get_sync(&serdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&serdev->dev);
		goto err_close;
	}

	return 0;

err_close:
	serdev_device_close(serdev);

	return ret;
}

static void gnss_serial_close(struct gnss_device *gdev)
{
	struct gnss_serial *gserial = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = gserial->serdev;

	serdev_device_close(serdev);

	pm_runtime_put(&serdev->dev);
}

static int gnss_serial_write_raw(struct gnss_device *gdev,
		const unsigned char *buf, size_t count)
{
	struct gnss_serial *gserial = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = gserial->serdev;
	int ret;

	/* write is only buffered synchronously */
	ret = serdev_device_write(serdev, buf, count, MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ret;

	/* FIXME: determine if interrupted? */
	serdev_device_wait_until_sent(serdev, 0);

	return count;
}

static const struct gnss_operations gnss_serial_gnss_ops = {
	.open		= gnss_serial_open,
	.close		= gnss_serial_close,
	.write_raw	= gnss_serial_write_raw,
};

static int gnss_serial_receive_buf(struct serdev_device *serdev,
					const unsigned char *buf, size_t count)
{
	struct gnss_serial *gserial = serdev_device_get_drvdata(serdev);
	struct gnss_device *gdev = gserial->gdev;

	return gnss_insert_raw(gdev, buf, count);
}

static const struct serdev_device_ops gnss_serial_serdev_ops = {
	.receive_buf	= gnss_serial_receive_buf,
	.write_wakeup	= serdev_device_write_wakeup,
};

static int gnss_serial_set_power(struct gnss_serial *gserial,
					enum gnss_serial_pm_state state)
{
	if (!gserial->ops || !gserial->ops->set_power)
		return 0;

	return gserial->ops->set_power(gserial, state);
}

/*
 * FIXME: need to provide subdriver defaults or separate dt parsing from
 * allocation.
 */
static int gnss_serial_parse_dt(struct serdev_device *serdev)
{
	struct gnss_serial *gserial = serdev_device_get_drvdata(serdev);
	struct device_node *node = serdev->dev.of_node;
	u32 speed = 4800;

	of_property_read_u32(node, "current-speed", &speed);

	gserial->speed = speed;

	return 0;
}

struct gnss_serial *gnss_serial_allocate(struct serdev_device *serdev,
						size_t data_size)
{
	struct gnss_serial *gserial;
	struct gnss_device *gdev;
	int ret;

	gserial = kzalloc(sizeof(*gserial) + data_size, GFP_KERNEL);
	if (!gserial)
		return ERR_PTR(-ENOMEM);

	gdev = gnss_allocate_device(&serdev->dev);
	if (!gdev) {
		ret = -ENOMEM;
		goto err_free_gserial;
	}

	gdev->ops = &gnss_serial_gnss_ops;
	gnss_set_drvdata(gdev, gserial);

	gserial->serdev = serdev;
	gserial->gdev = gdev;

	serdev_device_set_drvdata(serdev, gserial);
	serdev_device_set_client_ops(serdev, &gnss_serial_serdev_ops);

	ret = gnss_serial_parse_dt(serdev);
	if (ret)
		goto err_put_device;

	return gserial;

err_put_device:
	gnss_put_device(gserial->gdev);
err_free_gserial:
	kfree(gserial);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(gnss_serial_allocate);

void gnss_serial_free(struct gnss_serial *gserial)
{
	gnss_put_device(gserial->gdev);
	kfree(gserial);
};
EXPORT_SYMBOL_GPL(gnss_serial_free);

int gnss_serial_register(struct gnss_serial *gserial)
{
	struct serdev_device *serdev = gserial->serdev;
	int ret;

	if (IS_ENABLED(CONFIG_PM)) {
		pm_runtime_enable(&serdev->dev);
	} else {
		ret = gnss_serial_set_power(gserial, GNSS_SERIAL_ACTIVE);
		if (ret < 0)
			return ret;
	}

	ret = gnss_register_device(gserial->gdev);
	if (ret)
		goto err_disable_rpm;

	return 0;

err_disable_rpm:
	if (IS_ENABLED(CONFIG_PM))
		pm_runtime_disable(&serdev->dev);
	else
		gnss_serial_set_power(gserial, GNSS_SERIAL_OFF);

	return ret;
}
EXPORT_SYMBOL_GPL(gnss_serial_register);

void gnss_serial_deregister(struct gnss_serial *gserial)
{
	struct serdev_device *serdev = gserial->serdev;

	gnss_deregister_device(gserial->gdev);

	if (IS_ENABLED(CONFIG_PM))
		pm_runtime_disable(&serdev->dev);
	else
		gnss_serial_set_power(gserial, GNSS_SERIAL_OFF);
}
EXPORT_SYMBOL_GPL(gnss_serial_deregister);

#ifdef CONFIG_PM
static int gnss_serial_runtime_suspend(struct device *dev)
{
	struct gnss_serial *gserial = dev_get_drvdata(dev);

	return gnss_serial_set_power(gserial, GNSS_SERIAL_STANDBY);
}

static int gnss_serial_runtime_resume(struct device *dev)
{
	struct gnss_serial *gserial = dev_get_drvdata(dev);

	return gnss_serial_set_power(gserial, GNSS_SERIAL_ACTIVE);
}
#endif /* CONFIG_PM */

static int gnss_serial_prepare(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 1;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gnss_serial_suspend(struct device *dev)
{
	struct gnss_serial *gserial = dev_get_drvdata(dev);
	int ret = 0;

	/*
	 * FIXME: serdev currently lacks support for managing the underlying
	 * device's wakeup settings. A workaround would be to close the serdev
	 * device here if it is open.
	 */

	if (!pm_runtime_suspended(dev))
		ret = gnss_serial_set_power(gserial, GNSS_SERIAL_STANDBY);

	return ret;
}

static int gnss_serial_resume(struct device *dev)
{
	struct gnss_serial *gserial = dev_get_drvdata(dev);
	int ret = 0;

	if (!pm_runtime_suspended(dev))
		ret = gnss_serial_set_power(gserial, GNSS_SERIAL_ACTIVE);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

const struct dev_pm_ops gnss_serial_pm_ops = {
	.prepare	= gnss_serial_prepare,
	SET_SYSTEM_SLEEP_PM_OPS(gnss_serial_suspend, gnss_serial_resume)
	SET_RUNTIME_PM_OPS(gnss_serial_runtime_suspend, gnss_serial_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(gnss_serial_pm_ops);

MODULE_AUTHOR("Johan Hovold <johan@kernel.org>");
MODULE_DESCRIPTION("Generic serial GNSS receiver driver");
MODULE_LICENSE("GPL v2");
