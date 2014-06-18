/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *
 * AB8500 Power-On Key handler
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/of.h>
#include <linux/slab.h>

/**
 * struct ab8500_ponkey - ab8500 ponkey information
 * @input_dev: pointer to input device
 * @ab8500: ab8500 parent
 * @irq_dbf: irq number for falling transition
 * @irq_dbr: irq number for rising transition
 */
struct ab8500_ponkey {
	struct input_dev	*idev;
	struct ab8500		*ab8500;
	int			irq_dbf;
	int			irq_dbr;
};

/* AB8500 gives us an interrupt when ONKEY is held */
static irqreturn_t ab8500_ponkey_handler(int irq, void *data)
{
	struct ab8500_ponkey *ponkey = data;

	if (irq == ponkey->irq_dbf)
		input_report_key(ponkey->idev, KEY_POWER, true);
	else if (irq == ponkey->irq_dbr)
		input_report_key(ponkey->idev, KEY_POWER, false);

	input_sync(ponkey->idev);

	return IRQ_HANDLED;
}

static int ab8500_ponkey_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_ponkey *ponkey;
	struct input_dev *input;
	int irq_dbf, irq_dbr;
	int error;

	irq_dbf = platform_get_irq_byname(pdev, "ONKEY_DBF");
	if (irq_dbf < 0) {
		dev_err(&pdev->dev, "No IRQ for ONKEY_DBF, error=%d\n", irq_dbf);
		return irq_dbf;
	}

	irq_dbr = platform_get_irq_byname(pdev, "ONKEY_DBR");
	if (irq_dbr < 0) {
		dev_err(&pdev->dev, "No IRQ for ONKEY_DBR, error=%d\n", irq_dbr);
		return irq_dbr;
	}

	ponkey = devm_kzalloc(&pdev->dev, sizeof(struct ab8500_ponkey),
			      GFP_KERNEL);
	if (!ponkey)
		return -ENOMEM;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	ponkey->idev = input;
	ponkey->ab8500 = ab8500;
	ponkey->irq_dbf = irq_dbf;
	ponkey->irq_dbr = irq_dbr;

	input->name = "AB8500 POn(PowerOn) Key";
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, KEY_POWER);

	error = devm_request_any_context_irq(&pdev->dev, ponkey->irq_dbf,
					     ab8500_ponkey_handler, 0,
					     "ab8500-ponkey-dbf", ponkey);
	if (error < 0) {
		dev_err(ab8500->dev, "Failed to request dbf IRQ#%d: %d\n",
			ponkey->irq_dbf, error);
		return error;
	}

	error = devm_request_any_context_irq(&pdev->dev, ponkey->irq_dbr,
					     ab8500_ponkey_handler, 0,
					     "ab8500-ponkey-dbr", ponkey);
	if (error < 0) {
		dev_err(ab8500->dev, "Failed to request dbr IRQ#%d: %d\n",
			ponkey->irq_dbr, error);
		return error;
	}

	error = input_register_device(ponkey->idev);
	if (error) {
		dev_err(ab8500->dev, "Can't register input device: %d\n", error);
		return error;
	}

	platform_set_drvdata(pdev, ponkey);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ab8500_ponkey_match[] = {
	{ .compatible = "stericsson,ab8500-ponkey", },
	{}
};
#endif

static struct platform_driver ab8500_ponkey_driver = {
	.driver		= {
		.name	= "ab8500-poweron-key",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ab8500_ponkey_match),
	},
	.probe		= ab8500_ponkey_probe,
};
module_platform_driver(ab8500_ponkey_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson AB8500 Power-ON(Pon) Key driver");
