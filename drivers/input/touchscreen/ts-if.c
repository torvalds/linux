/*
 * linux/drivers/input/touchscreen/ts-if.c
 *
 * Copyright (c) 2011 FriendlyARM (www.arm9.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>

#define S3CFB_HRES		800		/* horizon pixel  x resolition */
#define S3CFB_VRES		480		/* line cnt       y resolution */
extern void mini2451_get_lcd_res(int *w, int *h);

#define S3C_TSVERSION	0x0101
#define DEBUG_LVL		KERN_DEBUG

static struct input_dev *input_dev;
static char phys[] = "input(ts)";

#define DEVICE_NAME		"ts-if"

static long _ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned is_down;

	is_down = (((unsigned)(arg)) >> 31);
	if (is_down) {
		unsigned x, y;

		x = (arg >> 16) & 0x7FFF;
		y = arg & 0x7FFF;
		input_report_abs(input_dev, ABS_X, x);
		input_report_abs(input_dev, ABS_Y, y);

		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_abs(input_dev, ABS_PRESSURE, 1);
		input_sync(input_dev);
	} else {
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_report_abs(input_dev, ABS_PRESSURE, 0);
		input_sync(input_dev);
	}

	return 0;
}

static struct file_operations dev_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl	= _ioctl,
};

static struct miscdevice misc = {
	.minor	= 185,
	.name	= DEVICE_NAME,
	.fops	= &dev_fops,
};

static int __init dev_init(void)
{
	int width = 0, height = 0;
	int ret;

	input_dev = input_allocate_device();
	if (!input_dev) {
		ret = -ENOMEM;
		return ret;
	}

	mini2451_get_lcd_res(&width, &height);
	if (!width)
		width = S3CFB_HRES;
	if (!height)
		height = S3CFB_VRES;
	
	input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, width, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, height, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1, 0, 0);

	input_dev->name = "fa_ts_input";
	input_dev->phys = phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = 0xDEAD;
	input_dev->id.product = 0xBEEF;
	input_dev->id.version = S3C_TSVERSION;

	/* All went ok, so register to the input system */
	ret = input_register_device(input_dev);
	if (ret) {
		printk("ts-if: Could not register input device(touchscreen)!\n");
		input_free_device(input_dev);
		return ret;
	}

	ret = misc_register(&misc);
	if (ret) {
		input_unregister_device(input_dev);
		input_free_device(input_dev);
		return ret;
	}

	printk (DEVICE_NAME"\tinitialized\n");
	return ret;
}

static void __exit dev_exit(void)
{
	input_unregister_device(input_dev);
	misc_deregister(&misc);
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_AUTHOR("FriendlyARM Inc.");
MODULE_DESCRIPTION("MINI2451 Touch Screen Interface Driver");
MODULE_LICENSE("GPL");

