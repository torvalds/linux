/*
 * LTC2952 (PowerPath) driver
 *
 * Copyright (C) 2014, Xsens Technologies BV <info@xsens.com>
 * Maintainer: René Moll <linux@r-moll.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ----------------------------------------
 * - Description
 * ----------------------------------------
 *
 * This driver is to be used with an external PowerPath Controller (LTC2952).
 * Its function is to determine when a external shut down is triggered
 * and react by properly shutting down the system.
 *
 * This driver expects a device tree with a ltc2952 entry for pin mapping.
 *
 * ----------------------------------------
 * - GPIO
 * ----------------------------------------
 *
 * The following GPIOs are used:
 * - trigger (input)
 *     A level change indicates the shut-down trigger. If it's state reverts
 *     within the time-out defined by trigger_delay, the shut down is not
 *     executed.
 *
 * - watchdog (output)
 *     Once a shut down is triggered, the driver will toggle this signal,
 *     with an internal (wde_interval) to stall the hardware shut down.
 *
 * - kill (output)
 *     The last action during shut down is triggering this signalling, such
 *     that the PowerPath Control will power down the hardware.
 *
 * ----------------------------------------
 * - Interrupts
 * ----------------------------------------
 *
 * The driver requires a non-shared, edge-triggered interrupt on the trigger
 * GPIO.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/reboot.h>

struct ltc2952_poweroff_data {
	struct hrtimer timer_trigger;
	struct hrtimer timer_wde;

	ktime_t trigger_delay;
	ktime_t wde_interval;

	struct device *dev;

	unsigned int virq;

	/**
	 * 0: trigger
	 * 1: watchdog
	 * 2: kill
	 */
	struct gpio_desc *gpio[3];
};

static int ltc2952_poweroff_panic;
static struct ltc2952_poweroff_data *ltc2952_data;

#define POWERPATH_IO_TRIGGER	0
#define POWERPATH_IO_WATCHDOG	1
#define POWERPATH_IO_KILL	2

/**
 * ltc2952_poweroff_timer_wde - Timer callback
 * Toggles the watchdog reset signal each wde_interval
 *
 * @timer: corresponding timer
 *
 * Returns HRTIMER_RESTART for an infinite loop which will only stop when the
 * machine actually shuts down
 */
static enum hrtimer_restart ltc2952_poweroff_timer_wde(struct hrtimer *timer)
{
	ktime_t now;
	int state;
	unsigned long overruns;

	if (ltc2952_poweroff_panic)
		return HRTIMER_NORESTART;

	state = gpiod_get_value(ltc2952_data->gpio[POWERPATH_IO_WATCHDOG]);
	gpiod_set_value(ltc2952_data->gpio[POWERPATH_IO_WATCHDOG], !state);

	now = hrtimer_cb_get_time(timer);
	overruns = hrtimer_forward(timer, now, ltc2952_data->wde_interval);

	return HRTIMER_RESTART;
}

static enum hrtimer_restart ltc2952_poweroff_timer_trigger(
	struct hrtimer *timer)
{
	int ret;

	ret = hrtimer_start(&ltc2952_data->timer_wde,
		ltc2952_data->wde_interval, HRTIMER_MODE_REL);

	if (ret) {
		dev_err(ltc2952_data->dev, "unable to start the timer\n");
		/*
		 * The device will not toggle the watchdog reset,
		 * thus shut down is only safe if the PowerPath controller
		 * has a long enough time-off before triggering a hardware
		 * power-off.
		 *
		 * Only sending a warning as the system will power-off anyway
		 */
	}

	dev_info(ltc2952_data->dev, "executing shutdown\n");

	orderly_poweroff(true);

	return HRTIMER_NORESTART;
}

/**
 * ltc2952_poweroff_handler - Interrupt handler
 * Triggered each time the trigger signal changes state and (de)activates a
 * time-out (timer_trigger). Once the time-out is actually reached the shut
 * down is executed.
 *
 * @irq: IRQ number
 * @dev_id: pointer to the main data structure
 */
static irqreturn_t ltc2952_poweroff_handler(int irq, void *dev_id)
{
	int ret;
	struct ltc2952_poweroff_data *data = dev_id;

	if (ltc2952_poweroff_panic)
		goto irq_ok;

	if (hrtimer_active(&data->timer_wde)) {
		/* shutdown is already triggered, nothing to do any more */
		goto irq_ok;
	}

	if (!hrtimer_active(&data->timer_trigger)) {
		ret = hrtimer_start(&data->timer_trigger, data->trigger_delay,
			HRTIMER_MODE_REL);

		if (ret)
			dev_err(data->dev, "unable to start the wait timer\n");
	} else {
		ret = hrtimer_cancel(&data->timer_trigger);
		/* omitting return value check, timer should have been valid */
	}

irq_ok:
	return IRQ_HANDLED;
}

static void ltc2952_poweroff_kill(void)
{
	gpiod_set_value(ltc2952_data->gpio[POWERPATH_IO_KILL], 1);
}

static int ltc2952_poweroff_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	return -ENOSYS;
}

static int ltc2952_poweroff_resume(struct platform_device *pdev)
{
	return -ENOSYS;
}

static void ltc2952_poweroff_default(struct ltc2952_poweroff_data *data)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(data->gpio); i++)
		data->gpio[i] = NULL;

	data->wde_interval = ktime_set(0, 300L*1E6L);
	data->trigger_delay = ktime_set(2, 500L*1E6L);

	hrtimer_init(&data->timer_trigger, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->timer_trigger.function = &ltc2952_poweroff_timer_trigger;

	hrtimer_init(&data->timer_wde, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->timer_wde.function = &ltc2952_poweroff_timer_wde;
}

static int ltc2952_poweroff_init(struct platform_device *pdev)
{
	int ret, virq;
	unsigned int i;
	struct ltc2952_poweroff_data *data;

	static char *name[] = {
		"trigger",
		"watchdog",
		"kill",
		NULL
	};

	data = ltc2952_data;
	ltc2952_poweroff_default(ltc2952_data);

	for (i = 0; i < ARRAY_SIZE(ltc2952_data->gpio); i++) {
		ltc2952_data->gpio[i] = gpiod_get(&pdev->dev, name[i]);

		if (IS_ERR(ltc2952_data->gpio[i])) {
			ret = PTR_ERR(ltc2952_data->gpio[i]);
			dev_err(&pdev->dev,
				"unable to claim the following gpio: %s\n",
				name[i]);
			goto err_io;
		}
	}

	ret = gpiod_direction_output(
		ltc2952_data->gpio[POWERPATH_IO_WATCHDOG], 0);
	if (ret) {
		dev_err(&pdev->dev, "unable to use watchdog-gpio as output\n");
		goto err_io;
	}

	ret = gpiod_direction_output(ltc2952_data->gpio[POWERPATH_IO_KILL], 0);
	if (ret) {
		dev_err(&pdev->dev, "unable to use kill-gpio as output\n");
		goto err_io;
	}

	virq = gpiod_to_irq(ltc2952_data->gpio[POWERPATH_IO_TRIGGER]);
	if (virq < 0) {
		dev_err(&pdev->dev, "cannot map GPIO as interrupt");
		goto err_io;
	}

	ltc2952_data->virq = virq;
	ret = request_irq(virq,
		ltc2952_poweroff_handler,
		(IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING),
		"ltc2952-poweroff",
		ltc2952_data
	);

	if (ret) {
		dev_err(&pdev->dev, "cannot configure an interrupt handler\n");
		goto err_io;
	}

	return 0;

err_io:
	for (i = 0; i < ARRAY_SIZE(ltc2952_data->gpio); i++)
		if (ltc2952_data->gpio[i])
			gpiod_put(ltc2952_data->gpio[i]);

	return ret;
}

static int ltc2952_poweroff_probe(struct platform_device *pdev)
{
	int ret;

	if (pm_power_off) {
		dev_err(&pdev->dev, "pm_power_off already registered");
		return -EBUSY;
	}

	ltc2952_data = kzalloc(sizeof(*ltc2952_data), GFP_KERNEL);
	if (!ltc2952_data)
		return -ENOMEM;

	ltc2952_data->dev = &pdev->dev;

	ret = ltc2952_poweroff_init(pdev);
	if (ret)
		goto err;

	pm_power_off = &ltc2952_poweroff_kill;

	dev_info(&pdev->dev, "probe successful\n");

	return 0;

err:
	kfree(ltc2952_data);
	return ret;
}

static int ltc2952_poweroff_remove(struct platform_device *pdev)
{
	unsigned int i;

	pm_power_off = NULL;

	if (ltc2952_data) {
		free_irq(ltc2952_data->virq, ltc2952_data);

		for (i = 0; i < ARRAY_SIZE(ltc2952_data->gpio); i++)
			gpiod_put(ltc2952_data->gpio[i]);

		kfree(ltc2952_data);
	}

	return 0;
}

static const struct of_device_id of_ltc2952_poweroff_match[] = {
	{ .compatible = "lltc,ltc2952"},
	{},
};
MODULE_DEVICE_TABLE(of, of_ltc2952_poweroff_match);

static struct platform_driver ltc2952_poweroff_driver = {
	.probe = ltc2952_poweroff_probe,
	.remove = ltc2952_poweroff_remove,
	.driver = {
		.name = "ltc2952-poweroff",
		.of_match_table = of_ltc2952_poweroff_match,
	},
	.suspend = ltc2952_poweroff_suspend,
	.resume = ltc2952_poweroff_resume,
};

static int ltc2952_poweroff_notify_panic(struct notifier_block *nb,
	unsigned long code, void *unused)
{
	ltc2952_poweroff_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block ltc2952_poweroff_panic_nb = {
	.notifier_call = ltc2952_poweroff_notify_panic,
};

static int __init ltc2952_poweroff_platform_init(void)
{
	ltc2952_poweroff_panic = 0;

	atomic_notifier_chain_register(&panic_notifier_list,
		&ltc2952_poweroff_panic_nb);

	return platform_driver_register(&ltc2952_poweroff_driver);
}

static void __exit ltc2952_poweroff_platform_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
		&ltc2952_poweroff_panic_nb);

	platform_driver_unregister(&ltc2952_poweroff_driver);
}

module_init(ltc2952_poweroff_platform_init);
module_exit(ltc2952_poweroff_platform_exit);

MODULE_AUTHOR("René Moll <rene.moll@xsens.com>");
MODULE_DESCRIPTION("LTC PowerPath power-off driver");
MODULE_LICENSE("GPL v2");
