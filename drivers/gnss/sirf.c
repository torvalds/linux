// SPDX-License-Identifier: GPL-2.0
/*
 * SiRFstar GNSS receiver driver
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#include <linux/errno.h>
#include <linux/gnss.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/wait.h>

#define SIRF_BOOT_DELAY			500
#define SIRF_ON_OFF_PULSE_TIME		100
#define SIRF_ACTIVATE_TIMEOUT		200
#define SIRF_HIBERNATE_TIMEOUT		200

struct sirf_data {
	struct gnss_device *gdev;
	struct serdev_device *serdev;
	speed_t	speed;
	struct regulator *vcc;
	struct gpio_desc *on_off;
	struct gpio_desc *wakeup;
	int irq;
	bool active;
	wait_queue_head_t power_wait;
};

static int sirf_open(struct gnss_device *gdev)
{
	struct sirf_data *data = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = data->serdev;
	int ret;

	ret = serdev_device_open(serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, data->speed);
	serdev_device_set_flow_control(serdev, false);

	ret = pm_runtime_get_sync(&serdev->dev);
	if (ret < 0) {
		dev_err(&gdev->dev, "failed to runtime resume: %d\n", ret);
		pm_runtime_put_noidle(&serdev->dev);
		goto err_close;
	}

	return 0;

err_close:
	serdev_device_close(serdev);

	return ret;
}

static void sirf_close(struct gnss_device *gdev)
{
	struct sirf_data *data = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = data->serdev;

	serdev_device_close(serdev);

	pm_runtime_put(&serdev->dev);
}

static int sirf_write_raw(struct gnss_device *gdev, const unsigned char *buf,
				size_t count)
{
	struct sirf_data *data = gnss_get_drvdata(gdev);
	struct serdev_device *serdev = data->serdev;
	int ret;

	/* write is only buffered synchronously */
	ret = serdev_device_write(serdev, buf, count, MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ret;

	/* FIXME: determine if interrupted? */
	serdev_device_wait_until_sent(serdev, 0);

	return count;
}

static const struct gnss_operations sirf_gnss_ops = {
	.open		= sirf_open,
	.close		= sirf_close,
	.write_raw	= sirf_write_raw,
};

static int sirf_receive_buf(struct serdev_device *serdev,
				const unsigned char *buf, size_t count)
{
	struct sirf_data *data = serdev_device_get_drvdata(serdev);
	struct gnss_device *gdev = data->gdev;

	return gnss_insert_raw(gdev, buf, count);
}

static const struct serdev_device_ops sirf_serdev_ops = {
	.receive_buf	= sirf_receive_buf,
	.write_wakeup	= serdev_device_write_wakeup,
};

static irqreturn_t sirf_wakeup_handler(int irq, void *dev_id)
{
	struct sirf_data *data = dev_id;
	struct device *dev = &data->serdev->dev;
	int ret;

	ret = gpiod_get_value_cansleep(data->wakeup);
	dev_dbg(dev, "%s - wakeup = %d\n", __func__, ret);
	if (ret < 0)
		goto out;

	data->active = !!ret;
	wake_up_interruptible(&data->power_wait);
out:
	return IRQ_HANDLED;
}

static int sirf_wait_for_power_state(struct sirf_data *data, bool active,
					unsigned long timeout)
{
	int ret;

	ret = wait_event_interruptible_timeout(data->power_wait,
			data->active == active, msecs_to_jiffies(timeout));
	if (ret < 0)
		return ret;

	if (ret == 0) {
		dev_warn(&data->serdev->dev, "timeout waiting for active state = %d\n",
				active);
		return -ETIMEDOUT;
	}

	return 0;
}

static void sirf_pulse_on_off(struct sirf_data *data)
{
	gpiod_set_value_cansleep(data->on_off, 1);
	msleep(SIRF_ON_OFF_PULSE_TIME);
	gpiod_set_value_cansleep(data->on_off, 0);
}

static int sirf_set_active(struct sirf_data *data, bool active)
{
	unsigned long timeout;
	int retries = 3;
	int ret;

	if (active)
		timeout = SIRF_ACTIVATE_TIMEOUT;
	else
		timeout = SIRF_HIBERNATE_TIMEOUT;

	while (retries-- > 0) {
		sirf_pulse_on_off(data);
		ret = sirf_wait_for_power_state(data, active, timeout);
		if (ret < 0) {
			if (ret == -ETIMEDOUT)
				continue;

			return ret;
		}

		break;
	}

	if (retries == 0)
		return -ETIMEDOUT;

	return 0;
}

static int sirf_runtime_suspend(struct device *dev)
{
	struct sirf_data *data = dev_get_drvdata(dev);

	if (!data->on_off)
		return regulator_disable(data->vcc);

	return sirf_set_active(data, false);
}

static int sirf_runtime_resume(struct device *dev)
{
	struct sirf_data *data = dev_get_drvdata(dev);

	if (!data->on_off)
		return regulator_enable(data->vcc);

	return sirf_set_active(data, true);
}

static int __maybe_unused sirf_suspend(struct device *dev)
{
	struct sirf_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (!pm_runtime_suspended(dev))
		ret = sirf_runtime_suspend(dev);

	if (data->wakeup)
		disable_irq(data->irq);

	return ret;
}

static int __maybe_unused sirf_resume(struct device *dev)
{
	struct sirf_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (data->wakeup)
		enable_irq(data->irq);

	if (!pm_runtime_suspended(dev))
		ret = sirf_runtime_resume(dev);

	return ret;
}

static const struct dev_pm_ops sirf_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_suspend, sirf_resume)
	SET_RUNTIME_PM_OPS(sirf_runtime_suspend, sirf_runtime_resume, NULL)
};

static int sirf_parse_dt(struct serdev_device *serdev)
{
	struct sirf_data *data = serdev_device_get_drvdata(serdev);
	struct device_node *node = serdev->dev.of_node;
	u32 speed = 9600;

	of_property_read_u32(node, "current-speed", &speed);

	data->speed = speed;

	return 0;
}

static int sirf_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct gnss_device *gdev;
	struct sirf_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	gdev = gnss_allocate_device(dev);
	if (!gdev)
		return -ENOMEM;

	gdev->type = GNSS_TYPE_SIRF;
	gdev->ops = &sirf_gnss_ops;
	gnss_set_drvdata(gdev, data);

	data->serdev = serdev;
	data->gdev = gdev;

	init_waitqueue_head(&data->power_wait);

	serdev_device_set_drvdata(serdev, data);
	serdev_device_set_client_ops(serdev, &sirf_serdev_ops);

	ret = sirf_parse_dt(serdev);
	if (ret)
		goto err_put_device;

	data->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(data->vcc)) {
		ret = PTR_ERR(data->vcc);
		goto err_put_device;
	}

	data->on_off = devm_gpiod_get_optional(dev, "sirf,onoff",
			GPIOD_OUT_LOW);
	if (IS_ERR(data->on_off))
		goto err_put_device;

	if (data->on_off) {
		data->wakeup = devm_gpiod_get_optional(dev, "sirf,wakeup",
				GPIOD_IN);
		if (IS_ERR(data->wakeup))
			goto err_put_device;

		/*
		 * Configurations where WAKEUP has been left not connected,
		 * are currently not supported.
		 */
		if (!data->wakeup) {
			dev_err(dev, "no wakeup gpio specified\n");
			ret = -ENODEV;
			goto err_put_device;
		}
	}

	if (data->wakeup) {
		ret = gpiod_to_irq(data->wakeup);
		if (ret < 0)
			goto err_put_device;

		data->irq = ret;

		ret = devm_request_threaded_irq(dev, data->irq, NULL,
				sirf_wakeup_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"wakeup", data);
		if (ret)
			goto err_put_device;
	}

	if (data->on_off) {
		ret = regulator_enable(data->vcc);
		if (ret)
			goto err_put_device;

		/* Wait for chip to boot into hibernate mode */
		msleep(SIRF_BOOT_DELAY);
	}

	if (IS_ENABLED(CONFIG_PM)) {
		pm_runtime_set_suspended(dev);	/* clear runtime_error flag */
		pm_runtime_enable(dev);
	} else {
		ret = sirf_runtime_resume(dev);
		if (ret < 0)
			goto err_disable_vcc;
	}

	ret = gnss_register_device(gdev);
	if (ret)
		goto err_disable_rpm;

	return 0;

err_disable_rpm:
	if (IS_ENABLED(CONFIG_PM))
		pm_runtime_disable(dev);
	else
		sirf_runtime_suspend(dev);
err_disable_vcc:
	if (data->on_off)
		regulator_disable(data->vcc);
err_put_device:
	gnss_put_device(data->gdev);

	return ret;
}

static void sirf_remove(struct serdev_device *serdev)
{
	struct sirf_data *data = serdev_device_get_drvdata(serdev);

	gnss_deregister_device(data->gdev);

	if (IS_ENABLED(CONFIG_PM))
		pm_runtime_disable(&serdev->dev);
	else
		sirf_runtime_suspend(&serdev->dev);

	if (data->on_off)
		regulator_disable(data->vcc);

	gnss_put_device(data->gdev);
};

#ifdef CONFIG_OF
static const struct of_device_id sirf_of_match[] = {
	{ .compatible = "fastrax,uc430" },
	{ .compatible = "linx,r4" },
	{ .compatible = "wi2wi,w2sg0008i" },
	{ .compatible = "wi2wi,w2sg0084i" },
	{},
};
MODULE_DEVICE_TABLE(of, sirf_of_match);
#endif

static struct serdev_device_driver sirf_driver = {
	.driver	= {
		.name		= "gnss-sirf",
		.of_match_table	= of_match_ptr(sirf_of_match),
		.pm		= &sirf_pm_ops,
	},
	.probe	= sirf_probe,
	.remove	= sirf_remove,
};
module_serdev_device_driver(sirf_driver);

MODULE_AUTHOR("Johan Hovold <johan@kernel.org>");
MODULE_DESCRIPTION("SiRFstar GNSS receiver driver");
MODULE_LICENSE("GPL v2");
