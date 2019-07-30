// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OnKey device driver for DA9063, DA9062 and DA9061 PMICs
 * Copyright (C) 2015  Dialog Semiconductor Ltd.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/registers.h>
#include <linux/mfd/da9062/core.h>
#include <linux/mfd/da9062/registers.h>

struct da906x_chip_config {
	/* REGS */
	int onkey_status;
	int onkey_pwr_signalling;
	int onkey_fault_log;
	int onkey_shutdown;
	/* MASKS */
	int onkey_nonkey_mask;
	int onkey_nonkey_lock_mask;
	int onkey_key_reset_mask;
	int onkey_shutdown_mask;
	/* NAMES */
	const char *name;
};

struct da9063_onkey {
	struct delayed_work work;
	struct input_dev *input;
	struct device *dev;
	struct regmap *regmap;
	const struct da906x_chip_config *config;
	char phys[32];
	bool key_power;
};

static const struct da906x_chip_config da9063_regs = {
	/* REGS */
	.onkey_status = DA9063_REG_STATUS_A,
	.onkey_pwr_signalling = DA9063_REG_CONTROL_B,
	.onkey_fault_log = DA9063_REG_FAULT_LOG,
	.onkey_shutdown = DA9063_REG_CONTROL_F,
	/* MASKS */
	.onkey_nonkey_mask = DA9063_NONKEY,
	.onkey_nonkey_lock_mask = DA9063_NONKEY_LOCK,
	.onkey_key_reset_mask = DA9063_KEY_RESET,
	.onkey_shutdown_mask = DA9063_SHUTDOWN,
	/* NAMES */
	.name = DA9063_DRVNAME_ONKEY,
};

static const struct da906x_chip_config da9062_regs = {
	/* REGS */
	.onkey_status = DA9062AA_STATUS_A,
	.onkey_pwr_signalling = DA9062AA_CONTROL_B,
	.onkey_fault_log = DA9062AA_FAULT_LOG,
	.onkey_shutdown = DA9062AA_CONTROL_F,
	/* MASKS */
	.onkey_nonkey_mask = DA9062AA_NONKEY_MASK,
	.onkey_nonkey_lock_mask = DA9062AA_NONKEY_LOCK_MASK,
	.onkey_key_reset_mask = DA9062AA_KEY_RESET_MASK,
	.onkey_shutdown_mask = DA9062AA_SHUTDOWN_MASK,
	/* NAMES */
	.name = "da9062-onkey",
};

static const struct of_device_id da9063_compatible_reg_id_table[] = {
	{ .compatible = "dlg,da9063-onkey", .data = &da9063_regs },
	{ .compatible = "dlg,da9062-onkey", .data = &da9062_regs },
	{ },
};
MODULE_DEVICE_TABLE(of, da9063_compatible_reg_id_table);

static void da9063_poll_on(struct work_struct *work)
{
	struct da9063_onkey *onkey = container_of(work,
						struct da9063_onkey,
						work.work);
	const struct da906x_chip_config *config = onkey->config;
	unsigned int val;
	int fault_log = 0;
	bool poll = true;
	int error;

	/* Poll to see when the pin is released */
	error = regmap_read(onkey->regmap,
			    config->onkey_status,
			    &val);
	if (error) {
		dev_err(onkey->dev,
			"Failed to read ON status: %d\n", error);
		goto err_poll;
	}

	if (!(val & config->onkey_nonkey_mask)) {
		error = regmap_update_bits(onkey->regmap,
					   config->onkey_pwr_signalling,
					   config->onkey_nonkey_lock_mask,
					   0);
		if (error) {
			dev_err(onkey->dev,
				"Failed to reset the Key Delay %d\n", error);
			goto err_poll;
		}

		input_report_key(onkey->input, KEY_POWER, 0);
		input_sync(onkey->input);

		poll = false;
	}

	/*
	 * If the fault log KEY_RESET is detected, then clear it
	 * and shut down the system.
	 */
	error = regmap_read(onkey->regmap,
			    config->onkey_fault_log,
			    &fault_log);
	if (error) {
		dev_warn(&onkey->input->dev,
			 "Cannot read FAULT_LOG: %d\n", error);
	} else if (fault_log & config->onkey_key_reset_mask) {
		error = regmap_write(onkey->regmap,
				     config->onkey_fault_log,
				     config->onkey_key_reset_mask);
		if (error) {
			dev_warn(&onkey->input->dev,
				 "Cannot reset KEY_RESET fault log: %d\n",
				 error);
		} else {
			/* at this point we do any S/W housekeeping
			 * and then send shutdown command
			 */
			dev_dbg(&onkey->input->dev,
				"Sending SHUTDOWN to PMIC ...\n");
			error = regmap_write(onkey->regmap,
					     config->onkey_shutdown,
					     config->onkey_shutdown_mask);
			if (error)
				dev_err(&onkey->input->dev,
					"Cannot SHUTDOWN PMIC: %d\n",
					error);
		}
	}

err_poll:
	if (poll)
		schedule_delayed_work(&onkey->work, msecs_to_jiffies(50));
}

static irqreturn_t da9063_onkey_irq_handler(int irq, void *data)
{
	struct da9063_onkey *onkey = data;
	const struct da906x_chip_config *config = onkey->config;
	unsigned int val;
	int error;

	error = regmap_read(onkey->regmap,
			    config->onkey_status,
			    &val);
	if (onkey->key_power && !error && (val & config->onkey_nonkey_mask)) {
		input_report_key(onkey->input, KEY_POWER, 1);
		input_sync(onkey->input);
		schedule_delayed_work(&onkey->work, 0);
		dev_dbg(onkey->dev, "KEY_POWER long press.\n");
	} else {
		input_report_key(onkey->input, KEY_POWER, 1);
		input_sync(onkey->input);
		input_report_key(onkey->input, KEY_POWER, 0);
		input_sync(onkey->input);
		dev_dbg(onkey->dev, "KEY_POWER short press.\n");
	}

	return IRQ_HANDLED;
}

static void da9063_cancel_poll(void *data)
{
	struct da9063_onkey *onkey = data;

	cancel_delayed_work_sync(&onkey->work);
}

static int da9063_onkey_probe(struct platform_device *pdev)
{
	struct da9063_onkey *onkey;
	const struct of_device_id *match;
	int irq;
	int error;

	match = of_match_node(da9063_compatible_reg_id_table,
			      pdev->dev.of_node);
	if (!match)
		return -ENXIO;

	onkey = devm_kzalloc(&pdev->dev, sizeof(struct da9063_onkey),
			     GFP_KERNEL);
	if (!onkey) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		return -ENOMEM;
	}

	onkey->config = match->data;
	onkey->dev = &pdev->dev;

	onkey->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!onkey->regmap) {
		dev_err(&pdev->dev, "Parent regmap unavailable.\n");
		return -ENXIO;
	}

	onkey->key_power = !of_property_read_bool(pdev->dev.of_node,
						  "dlg,disable-key-power");

	onkey->input = devm_input_allocate_device(&pdev->dev);
	if (!onkey->input) {
		dev_err(&pdev->dev, "Failed to allocated input device.\n");
		return -ENOMEM;
	}

	onkey->input->name = onkey->config->name;
	snprintf(onkey->phys, sizeof(onkey->phys), "%s/input0",
		 onkey->config->name);
	onkey->input->phys = onkey->phys;
	onkey->input->dev.parent = &pdev->dev;

	if (onkey->key_power)
		input_set_capability(onkey->input, EV_KEY, KEY_POWER);

	input_set_capability(onkey->input, EV_KEY, KEY_SLEEP);

	INIT_DELAYED_WORK(&onkey->work, da9063_poll_on);

	error = devm_add_action(&pdev->dev, da9063_cancel_poll, onkey);
	if (error) {
		dev_err(&pdev->dev,
			"Failed to add cancel poll action: %d\n",
			error);
		return error;
	}

	irq = platform_get_irq_byname(pdev, "ONKEY");
	if (irq < 0) {
		error = irq;
		dev_err(&pdev->dev, "Failed to get platform IRQ: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&pdev->dev, irq,
					  NULL, da9063_onkey_irq_handler,
					  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					  "ONKEY", onkey);
	if (error) {
		dev_err(&pdev->dev,
			"Failed to request IRQ %d: %d\n", irq, error);
		return error;
	}

	error = input_register_device(onkey->input);
	if (error) {
		dev_err(&pdev->dev,
			"Failed to register input device: %d\n", error);
		return error;
	}

	return 0;
}

static struct platform_driver da9063_onkey_driver = {
	.probe	= da9063_onkey_probe,
	.driver	= {
		.name	= DA9063_DRVNAME_ONKEY,
		.of_match_table = da9063_compatible_reg_id_table,
	},
};
module_platform_driver(da9063_onkey_driver);

MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("Onkey device driver for Dialog DA9063, DA9062 and DA9061");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DA9063_DRVNAME_ONKEY);
