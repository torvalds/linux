// SPDX-License-Identifier: GPL-2.0-or-later
/* NXP PCF50633 Input Driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 * Broken down from monstrous PCF50633 driver mainly by
 * Harald Welte, Andy Green and Werner Almesberger
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>

#include <linux/mfd/pcf50633/core.h>

#define PCF50633_OOCSTAT_ONKEY	0x01
#define PCF50633_REG_OOCSTAT	0x12
#define PCF50633_REG_OOCMODE	0x10

struct pcf50633_input {
	struct pcf50633 *pcf;
	struct input_dev *input_dev;
};

static void
pcf50633_input_irq(int irq, void *data)
{
	struct pcf50633_input *input;
	int onkey_released;

	input = data;

	/* We report only one event depending on the key press status */
	onkey_released = pcf50633_reg_read(input->pcf, PCF50633_REG_OOCSTAT)
						& PCF50633_OOCSTAT_ONKEY;

	if (irq == PCF50633_IRQ_ONKEYF && !onkey_released)
		input_report_key(input->input_dev, KEY_POWER, 1);
	else if (irq == PCF50633_IRQ_ONKEYR && onkey_released)
		input_report_key(input->input_dev, KEY_POWER, 0);

	input_sync(input->input_dev);
}

static int pcf50633_input_probe(struct platform_device *pdev)
{
	struct pcf50633_input *input;
	struct input_dev *input_dev;
	int ret;


	input = kzalloc(sizeof(*input), GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(input);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, input);
	input->pcf = dev_to_pcf50633(pdev->dev.parent);
	input->input_dev = input_dev;

	input_dev->name = "PCF50633 PMU events";
	input_dev->id.bustype = BUS_I2C;
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_PWR);
	set_bit(KEY_POWER, input_dev->keybit);

	ret = input_register_device(input_dev);
	if (ret) {
		input_free_device(input_dev);
		kfree(input);
		return ret;
	}
	pcf50633_register_irq(input->pcf, PCF50633_IRQ_ONKEYR,
				pcf50633_input_irq, input);
	pcf50633_register_irq(input->pcf, PCF50633_IRQ_ONKEYF,
				pcf50633_input_irq, input);

	return 0;
}

static void pcf50633_input_remove(struct platform_device *pdev)
{
	struct pcf50633_input *input  = platform_get_drvdata(pdev);

	pcf50633_free_irq(input->pcf, PCF50633_IRQ_ONKEYR);
	pcf50633_free_irq(input->pcf, PCF50633_IRQ_ONKEYF);

	input_unregister_device(input->input_dev);
	kfree(input);
}

static struct platform_driver pcf50633_input_driver = {
	.driver = {
		.name = "pcf50633-input",
	},
	.probe = pcf50633_input_probe,
	.remove_new = pcf50633_input_remove,
};
module_platform_driver(pcf50633_input_driver);

MODULE_AUTHOR("Balaji Rao <balajirrao@openmoko.org>");
MODULE_DESCRIPTION("PCF50633 input driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcf50633-input");
