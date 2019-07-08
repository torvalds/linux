// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenCores Keyboard Controller Driver
 * http://www.opencores.org/project,keyboardcontroller
 *
 * Copyright 2007-2009 HV Sistemas S.L.
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct opencores_kbd {
	struct input_dev *input;
	void __iomem *addr;
	int irq;
	unsigned short keycodes[128];
};

static irqreturn_t opencores_kbd_isr(int irq, void *dev_id)
{
	struct opencores_kbd *opencores_kbd = dev_id;
	struct input_dev *input = opencores_kbd->input;
	unsigned char c;

	c = readb(opencores_kbd->addr);
	input_report_key(input, c & 0x7f, c & 0x80 ? 0 : 1);
	input_sync(input);

	return IRQ_HANDLED;
}

static int opencores_kbd_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	struct opencores_kbd *opencores_kbd;
	struct resource *res;
	int irq, i, error;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing board memory resource\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "missing board IRQ resource\n");
		return -EINVAL;
	}

	opencores_kbd = devm_kzalloc(&pdev->dev, sizeof(*opencores_kbd),
				     GFP_KERNEL);
	if (!opencores_kbd)
		return -ENOMEM;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	opencores_kbd->input = input;

	opencores_kbd->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(opencores_kbd->addr))
		return PTR_ERR(opencores_kbd->addr);

	input->name = pdev->name;
	input->phys = "opencores-kbd/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	input->keycode = opencores_kbd->keycodes;
	input->keycodesize = sizeof(opencores_kbd->keycodes[0]);
	input->keycodemax = ARRAY_SIZE(opencores_kbd->keycodes);

	__set_bit(EV_KEY, input->evbit);

	for (i = 0; i < ARRAY_SIZE(opencores_kbd->keycodes); i++) {
		/*
		 * OpenCores controller happens to have scancodes match
		 * our KEY_* definitions.
		 */
		opencores_kbd->keycodes[i] = i;
		__set_bit(opencores_kbd->keycodes[i], input->keybit);
	}
	__clear_bit(KEY_RESERVED, input->keybit);

	error = devm_request_irq(&pdev->dev, irq, &opencores_kbd_isr,
				 IRQF_TRIGGER_RISING,
				 pdev->name, opencores_kbd);
	if (error) {
		dev_err(&pdev->dev, "unable to claim irq %d\n", irq);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "unable to register input device\n");
		return error;
	}

	return 0;
}

static struct platform_driver opencores_kbd_device_driver = {
	.probe    = opencores_kbd_probe,
	.driver   = {
		.name = "opencores-kbd",
	},
};
module_platform_driver(opencores_kbd_device_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Javier Herrero <jherrero@hvsistemas.es>");
MODULE_DESCRIPTION("Keyboard driver for OpenCores Keyboard Controller");
