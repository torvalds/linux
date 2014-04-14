/*
 * ON pin driver for Dialog DA9052 PMICs
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>

struct da9052_onkey {
	struct da9052 *da9052;
	struct input_dev *input;
	struct delayed_work work;
};

static void da9052_onkey_query(struct da9052_onkey *onkey)
{
	int ret;

	ret = da9052_reg_read(onkey->da9052, DA9052_STATUS_A_REG);
	if (ret < 0) {
		dev_err(onkey->da9052->dev,
			"Failed to read onkey event err=%d\n", ret);
	} else {
		/*
		 * Since interrupt for deassertion of ONKEY pin is not
		 * generated, onkey event state determines the onkey
		 * button state.
		 */
		bool pressed = !(ret & DA9052_STATUSA_NONKEY);

		input_report_key(onkey->input, KEY_POWER, pressed);
		input_sync(onkey->input);

		/*
		 * Interrupt is generated only when the ONKEY pin
		 * is asserted.  Hence the deassertion of the pin
		 * is simulated through work queue.
		 */
		if (pressed)
			schedule_delayed_work(&onkey->work,
						msecs_to_jiffies(50));
	}
}

static void da9052_onkey_work(struct work_struct *work)
{
	struct da9052_onkey *onkey = container_of(work, struct da9052_onkey,
						  work.work);

	da9052_onkey_query(onkey);
}

static irqreturn_t da9052_onkey_irq(int irq, void *data)
{
	struct da9052_onkey *onkey = data;

	da9052_onkey_query(onkey);

	return IRQ_HANDLED;
}

static int da9052_onkey_probe(struct platform_device *pdev)
{
	struct da9052 *da9052 = dev_get_drvdata(pdev->dev.parent);
	struct da9052_onkey *onkey;
	struct input_dev *input_dev;
	int error;

	if (!da9052) {
		dev_err(&pdev->dev, "Failed to get the driver's data\n");
		return -EINVAL;
	}

	onkey = kzalloc(sizeof(*onkey), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!onkey || !input_dev) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	onkey->input = input_dev;
	onkey->da9052 = da9052;
	INIT_DELAYED_WORK(&onkey->work, da9052_onkey_work);

	input_dev->name = "da9052-onkey";
	input_dev->phys = "da9052-onkey/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, input_dev->keybit);

	error = da9052_request_irq(onkey->da9052, DA9052_IRQ_NONKEY, "ONKEY",
			    da9052_onkey_irq, onkey);
	if (error < 0) {
		dev_err(onkey->da9052->dev,
			"Failed to register ONKEY IRQ: %d\n", error);
		goto err_free_mem;
	}

	error = input_register_device(onkey->input);
	if (error) {
		dev_err(&pdev->dev, "Unable to register input device, %d\n",
			error);
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, onkey);
	return 0;

err_free_irq:
	da9052_free_irq(onkey->da9052, DA9052_IRQ_NONKEY, onkey);
	cancel_delayed_work_sync(&onkey->work);
err_free_mem:
	input_free_device(input_dev);
	kfree(onkey);

	return error;
}

static int da9052_onkey_remove(struct platform_device *pdev)
{
	struct da9052_onkey *onkey = platform_get_drvdata(pdev);

	da9052_free_irq(onkey->da9052, DA9052_IRQ_NONKEY, onkey);
	cancel_delayed_work_sync(&onkey->work);

	input_unregister_device(onkey->input);
	kfree(onkey);

	return 0;
}

static struct platform_driver da9052_onkey_driver = {
	.probe	= da9052_onkey_probe,
	.remove	= da9052_onkey_remove,
	.driver = {
		.name	= "da9052-onkey",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(da9052_onkey_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Onkey driver for DA9052");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-onkey");
