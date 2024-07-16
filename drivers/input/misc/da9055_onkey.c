// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ON pin driver for Dialog DA9055 PMICs
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>

struct da9055_onkey {
	struct da9055 *da9055;
	struct input_dev *input;
	struct delayed_work work;
};

static void da9055_onkey_query(struct da9055_onkey *onkey)
{
	int key_stat;

	key_stat = da9055_reg_read(onkey->da9055, DA9055_REG_STATUS_A);
	if (key_stat < 0) {
		dev_err(onkey->da9055->dev,
			"Failed to read onkey event %d\n", key_stat);
	} else {
		key_stat &= DA9055_NOKEY_STS;
		/*
		 * Onkey status bit is cleared when onkey button is released.
		 */
		if (!key_stat) {
			input_report_key(onkey->input, KEY_POWER, 0);
			input_sync(onkey->input);
		}
	}

	/*
	 * Interrupt is generated only when the ONKEY pin is asserted.
	 * Hence the deassertion of the pin is simulated through work queue.
	 */
	if (key_stat)
		schedule_delayed_work(&onkey->work, msecs_to_jiffies(10));

}

static void da9055_onkey_work(struct work_struct *work)
{
	struct da9055_onkey *onkey = container_of(work, struct da9055_onkey,
						  work.work);

	da9055_onkey_query(onkey);
}

static irqreturn_t da9055_onkey_irq(int irq, void *data)
{
	struct da9055_onkey *onkey = data;

	input_report_key(onkey->input, KEY_POWER, 1);
	input_sync(onkey->input);

	da9055_onkey_query(onkey);

	return IRQ_HANDLED;
}

static int da9055_onkey_probe(struct platform_device *pdev)
{
	struct da9055 *da9055 = dev_get_drvdata(pdev->dev.parent);
	struct da9055_onkey *onkey;
	struct input_dev *input_dev;
	int irq, err;

	irq = platform_get_irq_byname(pdev, "ONKEY");
	if (irq < 0)
		return -EINVAL;

	onkey = devm_kzalloc(&pdev->dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	onkey->input = input_dev;
	onkey->da9055 = da9055;
	input_dev->name = "da9055-onkey";
	input_dev->phys = "da9055-onkey/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, input_dev->keybit);

	INIT_DELAYED_WORK(&onkey->work, da9055_onkey_work);

	err = request_threaded_irq(irq, NULL, da9055_onkey_irq,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   "ONKEY", onkey);
	if (err < 0) {
		dev_err(&pdev->dev,
			"Failed to register ONKEY IRQ %d, error = %d\n",
			irq, err);
		goto err_free_input;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register input device, %d\n",
			err);
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, onkey);

	return 0;

err_free_irq:
	free_irq(irq, onkey);
	cancel_delayed_work_sync(&onkey->work);
err_free_input:
	input_free_device(input_dev);

	return err;
}

static int da9055_onkey_remove(struct platform_device *pdev)
{
	struct da9055_onkey *onkey = platform_get_drvdata(pdev);
	int irq = platform_get_irq_byname(pdev, "ONKEY");

	irq = regmap_irq_get_virq(onkey->da9055->irq_data, irq);
	free_irq(irq, onkey);
	cancel_delayed_work_sync(&onkey->work);
	input_unregister_device(onkey->input);

	return 0;
}

static struct platform_driver da9055_onkey_driver = {
	.probe	= da9055_onkey_probe,
	.remove	= da9055_onkey_remove,
	.driver = {
		.name	= "da9055-onkey",
	},
};

module_platform_driver(da9055_onkey_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Onkey driver for DA9055");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-onkey");
