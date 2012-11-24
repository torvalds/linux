/*
 * HTC Shift touchscreen driver
 *
 * Copyright (C) 2008 Pau Oliva Fora <pof@eslack.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/isa.h>
#include <linux/ioport.h>
#include <linux/dmi.h>

MODULE_AUTHOR("Pau Oliva Fora <pau@eslack.org>");
MODULE_DESCRIPTION("HTC Shift touchscreen driver");
MODULE_LICENSE("GPL");

#define HTCPEN_PORT_IRQ_CLEAR	0x068
#define HTCPEN_PORT_INIT	0x06c
#define HTCPEN_PORT_INDEX	0x0250
#define HTCPEN_PORT_DATA	0x0251
#define HTCPEN_IRQ		3

#define DEVICE_ENABLE		0xa2
#define DEVICE_DISABLE		0xa3

#define X_INDEX			3
#define Y_INDEX			5
#define TOUCH_INDEX		0xb
#define LSB_XY_INDEX		0xc
#define X_AXIS_MAX		2040
#define Y_AXIS_MAX		2040

static bool invert_x;
module_param(invert_x, bool, 0644);
MODULE_PARM_DESC(invert_x, "If set, X axis is inverted");
static bool invert_y;
module_param(invert_y, bool, 0644);
MODULE_PARM_DESC(invert_y, "If set, Y axis is inverted");

static irqreturn_t htcpen_interrupt(int irq, void *handle)
{
	struct input_dev *htcpen_dev = handle;
	unsigned short x, y, xy;

	/* 0 = press; 1 = release */
	outb_p(TOUCH_INDEX, HTCPEN_PORT_INDEX);

	if (inb_p(HTCPEN_PORT_DATA)) {
		input_report_key(htcpen_dev, BTN_TOUCH, 0);
	} else {
		outb_p(X_INDEX, HTCPEN_PORT_INDEX);
		x = inb_p(HTCPEN_PORT_DATA);

		outb_p(Y_INDEX, HTCPEN_PORT_INDEX);
		y = inb_p(HTCPEN_PORT_DATA);

		outb_p(LSB_XY_INDEX, HTCPEN_PORT_INDEX);
		xy = inb_p(HTCPEN_PORT_DATA);

		/* get high resolution value of X and Y using LSB */
		x = X_AXIS_MAX - ((x * 8) + ((xy >> 4) & 0xf));
		y = (y * 8) + (xy & 0xf);
		if (invert_x)
			x = X_AXIS_MAX - x;
		if (invert_y)
			y = Y_AXIS_MAX - y;

		if (x != X_AXIS_MAX && x != 0) {
			input_report_key(htcpen_dev, BTN_TOUCH, 1);
			input_report_abs(htcpen_dev, ABS_X, x);
			input_report_abs(htcpen_dev, ABS_Y, y);
		}
	}

	input_sync(htcpen_dev);

	inb_p(HTCPEN_PORT_IRQ_CLEAR);

	return IRQ_HANDLED;
}

static int htcpen_open(struct input_dev *dev)
{
	outb_p(DEVICE_ENABLE, HTCPEN_PORT_INIT);

	return 0;
}

static void htcpen_close(struct input_dev *dev)
{
	outb_p(DEVICE_DISABLE, HTCPEN_PORT_INIT);
	synchronize_irq(HTCPEN_IRQ);
}

static int __devinit htcpen_isa_probe(struct device *dev, unsigned int id)
{
	struct input_dev *htcpen_dev;
	int err = -EBUSY;

	if (!request_region(HTCPEN_PORT_IRQ_CLEAR, 1, "htcpen")) {
		printk(KERN_ERR "htcpen: unable to get IO region 0x%x\n",
			HTCPEN_PORT_IRQ_CLEAR);
		goto request_region1_failed;
	}

	if (!request_region(HTCPEN_PORT_INIT, 1, "htcpen")) {
		printk(KERN_ERR "htcpen: unable to get IO region 0x%x\n",
			HTCPEN_PORT_INIT);
		goto request_region2_failed;
	}

	if (!request_region(HTCPEN_PORT_INDEX, 2, "htcpen")) {
		printk(KERN_ERR "htcpen: unable to get IO region 0x%x\n",
			HTCPEN_PORT_INDEX);
		goto request_region3_failed;
	}

	htcpen_dev = input_allocate_device();
	if (!htcpen_dev) {
		printk(KERN_ERR "htcpen: can't allocate device\n");
		err = -ENOMEM;
		goto input_alloc_failed;
	}

	htcpen_dev->name = "HTC Shift EC TouchScreen";
	htcpen_dev->id.bustype = BUS_ISA;

	htcpen_dev->evbit[0] = BIT_MASK(EV_ABS) | BIT_MASK(EV_KEY);
	htcpen_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(htcpen_dev, ABS_X, 0, X_AXIS_MAX, 0, 0);
	input_set_abs_params(htcpen_dev, ABS_Y, 0, Y_AXIS_MAX, 0, 0);

	htcpen_dev->open = htcpen_open;
	htcpen_dev->close = htcpen_close;

	err = request_irq(HTCPEN_IRQ, htcpen_interrupt, 0, "htcpen",
			htcpen_dev);
	if (err) {
		printk(KERN_ERR "htcpen: irq busy\n");
		goto request_irq_failed;
	}

	inb_p(HTCPEN_PORT_IRQ_CLEAR);

	err = input_register_device(htcpen_dev);
	if (err)
		goto input_register_failed;

	dev_set_drvdata(dev, htcpen_dev);

	return 0;

 input_register_failed:
	free_irq(HTCPEN_IRQ, htcpen_dev);
 request_irq_failed:
	input_free_device(htcpen_dev);
 input_alloc_failed:
	release_region(HTCPEN_PORT_INDEX, 2);
 request_region3_failed:
	release_region(HTCPEN_PORT_INIT, 1);
 request_region2_failed:
	release_region(HTCPEN_PORT_IRQ_CLEAR, 1);
 request_region1_failed:
	return err;
}

static int __devexit htcpen_isa_remove(struct device *dev, unsigned int id)
{
	struct input_dev *htcpen_dev = dev_get_drvdata(dev);

	input_unregister_device(htcpen_dev);

	free_irq(HTCPEN_IRQ, htcpen_dev);

	release_region(HTCPEN_PORT_INDEX, 2);
	release_region(HTCPEN_PORT_INIT, 1);
	release_region(HTCPEN_PORT_IRQ_CLEAR, 1);

	dev_set_drvdata(dev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int htcpen_isa_suspend(struct device *dev, unsigned int n,
				pm_message_t state)
{
	outb_p(DEVICE_DISABLE, HTCPEN_PORT_INIT);

	return 0;
}

static int htcpen_isa_resume(struct device *dev, unsigned int n)
{
	outb_p(DEVICE_ENABLE, HTCPEN_PORT_INIT);

	return 0;
}
#endif

static struct isa_driver htcpen_isa_driver = {
	.probe		= htcpen_isa_probe,
	.remove		= htcpen_isa_remove,
#ifdef CONFIG_PM
	.suspend	= htcpen_isa_suspend,
	.resume		= htcpen_isa_resume,
#endif
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "htcpen",
	}
};

static struct dmi_system_id __initdata htcshift_dmi_table[] = {
	{
		.ident = "Shift",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "High Tech Computer Corp"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Shift"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, htcshift_dmi_table);

static int __init htcpen_isa_init(void)
{
	if (!dmi_check_system(htcshift_dmi_table))
		return -ENODEV;

	return isa_register_driver(&htcpen_isa_driver, 1);
}

static void __exit htcpen_isa_exit(void)
{
	isa_unregister_driver(&htcpen_isa_driver);
}

module_init(htcpen_isa_init);
module_exit(htcpen_isa_exit);
