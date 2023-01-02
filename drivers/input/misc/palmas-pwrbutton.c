// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instruments' Palmas Power Button Input Driver
 *
 * Copyright (C) 2012-2014 Texas Instruments Incorporated - http://www.ti.com/
 *	Girish S Ghongdemath
 *	Nishanth Menon
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/palmas.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PALMAS_LPK_TIME_MASK		0x0c
#define PALMAS_PWRON_DEBOUNCE_MASK	0x03
#define PALMAS_PWR_KEY_Q_TIME_MS	20

/**
 * struct palmas_pwron - Palmas power on data
 * @palmas:		pointer to palmas device
 * @input_dev:		pointer to input device
 * @input_work:		work for detecting release of key
 * @irq:		irq that we are hooked on to
 */
struct palmas_pwron {
	struct palmas *palmas;
	struct input_dev *input_dev;
	struct delayed_work input_work;
	int irq;
};

/**
 * struct palmas_pwron_config - configuration of palmas power on
 * @long_press_time_val:	value for long press h/w shutdown event
 * @pwron_debounce_val:		value for debounce of power button
 */
struct palmas_pwron_config {
	u8 long_press_time_val;
	u8 pwron_debounce_val;
};

/**
 * palmas_power_button_work() - Detects the button release event
 * @work:	work item to detect button release
 */
static void palmas_power_button_work(struct work_struct *work)
{
	struct palmas_pwron *pwron = container_of(work,
						  struct palmas_pwron,
						  input_work.work);
	struct input_dev *input_dev = pwron->input_dev;
	unsigned int reg;
	int error;

	error = palmas_read(pwron->palmas, PALMAS_INTERRUPT_BASE,
			    PALMAS_INT1_LINE_STATE, &reg);
	if (error) {
		dev_err(input_dev->dev.parent,
			"Cannot read palmas PWRON status: %d\n", error);
	} else if (reg & BIT(1)) {
		/* The button is released, report event. */
		input_report_key(input_dev, KEY_POWER, 0);
		input_sync(input_dev);
	} else {
		/* The button is still depressed, keep checking. */
		schedule_delayed_work(&pwron->input_work,
				msecs_to_jiffies(PALMAS_PWR_KEY_Q_TIME_MS));
	}
}

/**
 * pwron_irq() - button press isr
 * @irq:		irq
 * @palmas_pwron:	pwron struct
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t pwron_irq(int irq, void *palmas_pwron)
{
	struct palmas_pwron *pwron = palmas_pwron;
	struct input_dev *input_dev = pwron->input_dev;

	input_report_key(input_dev, KEY_POWER, 1);
	pm_wakeup_event(input_dev->dev.parent, 0);
	input_sync(input_dev);

	mod_delayed_work(system_wq, &pwron->input_work,
			 msecs_to_jiffies(PALMAS_PWR_KEY_Q_TIME_MS));

	return IRQ_HANDLED;
}

/**
 * palmas_pwron_params_ofinit() - device tree parameter parser
 * @dev:	palmas button device
 * @config:	configuration params that this fills up
 */
static void palmas_pwron_params_ofinit(struct device *dev,
				       struct palmas_pwron_config *config)
{
	struct device_node *np;
	u32 val;
	int i, error;
	static const u8 lpk_times[] = { 6, 8, 10, 12 };
	static const int pwr_on_deb_ms[] = { 15, 100, 500, 1000 };

	memset(config, 0, sizeof(*config));

	/* Default config parameters */
	config->long_press_time_val = ARRAY_SIZE(lpk_times) - 1;

	np = dev->of_node;
	if (!np)
		return;

	error = of_property_read_u32(np, "ti,palmas-long-press-seconds", &val);
	if (!error) {
		for (i = 0; i < ARRAY_SIZE(lpk_times); i++) {
			if (val <= lpk_times[i]) {
				config->long_press_time_val = i;
				break;
			}
		}
	}

	error = of_property_read_u32(np,
				     "ti,palmas-pwron-debounce-milli-seconds",
				     &val);
	if (!error) {
		for (i = 0; i < ARRAY_SIZE(pwr_on_deb_ms); i++) {
			if (val <= pwr_on_deb_ms[i]) {
				config->pwron_debounce_val = i;
				break;
			}
		}
	}

	dev_info(dev, "h/w controlled shutdown duration=%d seconds\n",
		 lpk_times[config->long_press_time_val]);
}

/**
 * palmas_pwron_probe() - probe
 * @pdev:	platform device for the button
 *
 * Return: 0 for successful probe else appropriate error
 */
static int palmas_pwron_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct input_dev *input_dev;
	struct palmas_pwron *pwron;
	struct palmas_pwron_config config;
	int val;
	int error;

	palmas_pwron_params_ofinit(dev, &config);

	pwron = kzalloc(sizeof(*pwron), GFP_KERNEL);
	if (!pwron)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(dev, "Can't allocate power button\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	input_dev->name = "palmas_pwron";
	input_dev->phys = "palmas_pwron/input0";
	input_dev->dev.parent = dev;

	input_set_capability(input_dev, EV_KEY, KEY_POWER);

	/*
	 * Setup default hardware shutdown option (long key press)
	 * and debounce.
	 */
	val = FIELD_PREP(PALMAS_LPK_TIME_MASK, config.long_press_time_val) |
	      FIELD_PREP(PALMAS_PWRON_DEBOUNCE_MASK, config.pwron_debounce_val);
	error = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
				   PALMAS_LONG_PRESS_KEY,
				   PALMAS_LPK_TIME_MASK |
					PALMAS_PWRON_DEBOUNCE_MASK,
				   val);
	if (error) {
		dev_err(dev, "LONG_PRESS_KEY_UPDATE failed: %d\n", error);
		goto err_free_input;
	}

	pwron->palmas = palmas;
	pwron->input_dev = input_dev;

	INIT_DELAYED_WORK(&pwron->input_work, palmas_power_button_work);

	pwron->irq = platform_get_irq(pdev, 0);
	if (pwron->irq < 0) {
		error = pwron->irq;
		goto err_free_input;
	}

	error = request_threaded_irq(pwron->irq, NULL, pwron_irq,
				     IRQF_TRIGGER_HIGH |
					IRQF_TRIGGER_LOW |
					IRQF_ONESHOT,
				     dev_name(dev), pwron);
	if (error) {
		dev_err(dev, "Can't get IRQ for pwron: %d\n", error);
		goto err_free_input;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, pwron);
	device_init_wakeup(dev, true);

	return 0;

err_free_irq:
	cancel_delayed_work_sync(&pwron->input_work);
	free_irq(pwron->irq, pwron);
err_free_input:
	input_free_device(input_dev);
err_free_mem:
	kfree(pwron);
	return error;
}

/**
 * palmas_pwron_remove() - Cleanup on removal
 * @pdev:	platform device for the button
 *
 * Return: 0
 */
static int palmas_pwron_remove(struct platform_device *pdev)
{
	struct palmas_pwron *pwron = platform_get_drvdata(pdev);

	free_irq(pwron->irq, pwron);
	cancel_delayed_work_sync(&pwron->input_work);

	input_unregister_device(pwron->input_dev);
	kfree(pwron);

	return 0;
}

/**
 * palmas_pwron_suspend() - suspend handler
 * @dev:	power button device
 *
 * Cancel all pending work items for the power button, setup irq for wakeup
 *
 * Return: 0
 */
static int palmas_pwron_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct palmas_pwron *pwron = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&pwron->input_work);

	if (device_may_wakeup(dev))
		enable_irq_wake(pwron->irq);

	return 0;
}

/**
 * palmas_pwron_resume() - resume handler
 * @dev:	power button device
 *
 * Just disable the wakeup capability of irq here.
 *
 * Return: 0
 */
static int palmas_pwron_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct palmas_pwron *pwron = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev))
		disable_irq_wake(pwron->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(palmas_pwron_pm,
				palmas_pwron_suspend, palmas_pwron_resume);

#ifdef CONFIG_OF
static const struct of_device_id of_palmas_pwr_match[] = {
	{ .compatible = "ti,palmas-pwrbutton" },
	{ },
};

MODULE_DEVICE_TABLE(of, of_palmas_pwr_match);
#endif

static struct platform_driver palmas_pwron_driver = {
	.probe	= palmas_pwron_probe,
	.remove	= palmas_pwron_remove,
	.driver	= {
		.name	= "palmas_pwrbutton",
		.of_match_table = of_match_ptr(of_palmas_pwr_match),
		.pm	= pm_sleep_ptr(&palmas_pwron_pm),
	},
};
module_platform_driver(palmas_pwron_driver);

MODULE_ALIAS("platform:palmas-pwrbutton");
MODULE_DESCRIPTION("Palmas Power Button");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments Inc.");
