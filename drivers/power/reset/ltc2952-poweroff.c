// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LTC2952 (PowerPath) driver
 *
 * Copyright (C) 2014, Xsens Technologies BV <info@xsens.com>
 * Maintainer: René Moll <linux@r-moll.nl>
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
 *     executed. If no pin is assigned to this input, the driver will start the
 *     watchdog toggle immediately. The chip will only power off the system if
 *     it is requested to do so through the kill line.
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
#include <linux/panic_notifier.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio/consumer.h>
#include <linux/reboot.h>
#include <linux/property.h>

struct ltc2952_poweroff {
	struct hrtimer timer_trigger;
	struct hrtimer timer_wde;

	ktime_t trigger_delay;
	ktime_t wde_interval;

	struct device *dev;

	struct gpio_desc *gpio_trigger;
	struct gpio_desc *gpio_watchdog;
	struct gpio_desc *gpio_kill;

	bool kernel_panic;
	struct notifier_block panic_notifier;
};

#define to_ltc2952(p, m) container_of(p, struct ltc2952_poweroff, m)

/*
 * This global variable is only needed for pm_power_off. We should
 * remove it entirely once we don't need the global state anymore.
 */
static struct ltc2952_poweroff *ltc2952_data;

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
	struct ltc2952_poweroff *data = to_ltc2952(timer, timer_wde);

	if (data->kernel_panic)
		return HRTIMER_NORESTART;

	state = gpiod_get_value(data->gpio_watchdog);
	gpiod_set_value(data->gpio_watchdog, !state);

	now = hrtimer_cb_get_time(timer);
	hrtimer_forward(timer, now, data->wde_interval);

	return HRTIMER_RESTART;
}

static void ltc2952_poweroff_start_wde(struct ltc2952_poweroff *data)
{
	hrtimer_start(&data->timer_wde, data->wde_interval, HRTIMER_MODE_REL);
}

static enum hrtimer_restart
ltc2952_poweroff_timer_trigger(struct hrtimer *timer)
{
	struct ltc2952_poweroff *data = to_ltc2952(timer, timer_trigger);

	ltc2952_poweroff_start_wde(data);
	dev_info(data->dev, "executing shutdown\n");
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
	struct ltc2952_poweroff *data = dev_id;

	if (data->kernel_panic || hrtimer_active(&data->timer_wde)) {
		/* shutdown is already triggered, nothing to do any more */
		return IRQ_HANDLED;
	}

	if (gpiod_get_value(data->gpio_trigger)) {
		hrtimer_start(&data->timer_trigger, data->trigger_delay,
			      HRTIMER_MODE_REL);
	} else {
		hrtimer_cancel(&data->timer_trigger);
	}
	return IRQ_HANDLED;
}

static void ltc2952_poweroff_kill(void)
{
	gpiod_set_value(ltc2952_data->gpio_kill, 1);
}

static void ltc2952_poweroff_default(struct ltc2952_poweroff *data)
{
	data->wde_interval = 300L * NSEC_PER_MSEC;
	data->trigger_delay = ktime_set(2, 500L * NSEC_PER_MSEC);

	hrtimer_init(&data->timer_trigger, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->timer_trigger.function = ltc2952_poweroff_timer_trigger;

	hrtimer_init(&data->timer_wde, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->timer_wde.function = ltc2952_poweroff_timer_wde;
}

static int ltc2952_poweroff_init(struct platform_device *pdev)
{
	int ret;
	u32 trigger_delay_ms;
	struct ltc2952_poweroff *data = platform_get_drvdata(pdev);

	ltc2952_poweroff_default(data);

	if (!device_property_read_u32(&pdev->dev, "trigger-delay-ms",
				      &trigger_delay_ms)) {
		data->trigger_delay = ktime_set(trigger_delay_ms / MSEC_PER_SEC,
			(trigger_delay_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
	}

	data->gpio_watchdog = devm_gpiod_get(&pdev->dev, "watchdog",
					     GPIOD_OUT_LOW);
	if (IS_ERR(data->gpio_watchdog)) {
		ret = PTR_ERR(data->gpio_watchdog);
		dev_err(&pdev->dev, "unable to claim gpio \"watchdog\"\n");
		return ret;
	}

	data->gpio_kill = devm_gpiod_get(&pdev->dev, "kill", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpio_kill)) {
		ret = PTR_ERR(data->gpio_kill);
		dev_err(&pdev->dev, "unable to claim gpio \"kill\"\n");
		return ret;
	}

	data->gpio_trigger = devm_gpiod_get_optional(&pdev->dev, "trigger",
						     GPIOD_IN);
	if (IS_ERR(data->gpio_trigger)) {
		/*
		 * It's not a problem if the trigger gpio isn't available, but
		 * it is worth a warning if its use was defined in the device
		 * tree.
		 */
		dev_err(&pdev->dev, "unable to claim gpio \"trigger\"\n");
		data->gpio_trigger = NULL;
	}

	if (devm_request_irq(&pdev->dev, gpiod_to_irq(data->gpio_trigger),
			     ltc2952_poweroff_handler,
			     (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING),
			     "ltc2952-poweroff",
			     data)) {
		/*
		 * Some things may have happened:
		 * - No trigger input was defined
		 * - Claiming the GPIO failed
		 * - We could not map to an IRQ
		 * - We couldn't register an interrupt handler
		 *
		 * None of these really are problems, but all of them
		 * disqualify the push button from controlling the power.
		 *
		 * It is therefore important to note that if the ltc2952
		 * detects a button press for long enough, it will still start
		 * its own powerdown window and cut the power on us if we don't
		 * start the watchdog trigger.
		 */
		if (data->gpio_trigger) {
			dev_warn(&pdev->dev,
				 "unable to configure the trigger interrupt\n");
			devm_gpiod_put(&pdev->dev, data->gpio_trigger);
			data->gpio_trigger = NULL;
		}
		dev_info(&pdev->dev,
			 "power down trigger input will not be used\n");
		ltc2952_poweroff_start_wde(data);
	}

	return 0;
}

static int ltc2952_poweroff_notify_panic(struct notifier_block *nb,
					 unsigned long code, void *unused)
{
	struct ltc2952_poweroff *data = to_ltc2952(nb, panic_notifier);

	data->kernel_panic = true;
	return NOTIFY_DONE;
}

static int ltc2952_poweroff_probe(struct platform_device *pdev)
{
	int ret;
	struct ltc2952_poweroff *data;

	if (pm_power_off) {
		dev_err(&pdev->dev, "pm_power_off already registered");
		return -EBUSY;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);

	ret = ltc2952_poweroff_init(pdev);
	if (ret)
		return ret;

	/* TODO: remove ltc2952_data */
	ltc2952_data = data;
	pm_power_off = ltc2952_poweroff_kill;

	data->panic_notifier.notifier_call = ltc2952_poweroff_notify_panic;
	atomic_notifier_chain_register(&panic_notifier_list,
				       &data->panic_notifier);
	dev_info(&pdev->dev, "probe successful\n");

	return 0;
}

static int ltc2952_poweroff_remove(struct platform_device *pdev)
{
	struct ltc2952_poweroff *data = platform_get_drvdata(pdev);

	pm_power_off = NULL;
	hrtimer_cancel(&data->timer_trigger);
	hrtimer_cancel(&data->timer_wde);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &data->panic_notifier);
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
};

module_platform_driver(ltc2952_poweroff_driver);

MODULE_AUTHOR("René Moll <rene.moll@xsens.com>");
MODULE_DESCRIPTION("LTC PowerPath power-off driver");
MODULE_LICENSE("GPL v2");
