/*
 * Elo serial touchscreen driver
 *
 * Copyright (c) 2004 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * This driver can handle serial Elo touchscreens using either the Elo standard
 * 'E271-2210' 10-byte protocol, Elo legacy 'E281A-4002' 6-byte protocol, Elo
 * legacy 'E271-140' 4-byte protocol and Elo legacy 'E261-280' 3-byte protocol.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"Elo serial touchscreen driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define	ELO_MAX_LENGTH	10

static char *elo_name = "Elo Serial TouchScreen";

/*
 * Per-touchscreen data.
 */

struct elo {
	struct input_dev dev;
	struct serio *serio;
	int id;
	int idx;
	unsigned char csum;
	unsigned char data[ELO_MAX_LENGTH];
	char phys[32];
};

static void elo_process_data_10(struct elo* elo, unsigned char data, struct pt_regs *regs)
{
	struct input_dev *dev = &elo->dev;

	elo->csum += elo->data[elo->idx] = data;

	switch (elo->idx++) {

		case 0:
			if (data != 'U') {
				elo->idx = 0;
				elo->csum = 0;
			}
			break;

		case 1:
			if (data != 'T') {
				elo->idx = 0;
				elo->csum = 0;
			}
			break;

		case 9:
			if (elo->csum) {
				input_regs(dev, regs);
				input_report_abs(dev, ABS_X, (elo->data[4] << 8) | elo->data[3]);
				input_report_abs(dev, ABS_Y, (elo->data[6] << 8) | elo->data[5]);
				input_report_abs(dev, ABS_PRESSURE, (elo->data[8] << 8) | elo->data[7]);
				input_report_key(dev, BTN_TOUCH, elo->data[2] & 3);
				input_sync(dev);
			}
			elo->idx = 0;
			elo->csum = 0;
			break;
	}
}

static void elo_process_data_6(struct elo* elo, unsigned char data, struct pt_regs *regs)
{
	struct input_dev *dev = &elo->dev;

	elo->data[elo->idx] = data;

	switch (elo->idx++) {

		case 0: if ((data & 0xc0) != 0xc0) elo->idx = 0; break;
		case 1: if ((data & 0xc0) != 0x80) elo->idx = 0; break;
		case 2: if ((data & 0xc0) != 0x40) elo->idx = 0; break;

		case 3:
			if (data & 0xc0) {
				elo->idx = 0;
				break;
			}

			input_regs(dev, regs);
			input_report_abs(dev, ABS_X, ((elo->data[0] & 0x3f) << 6) | (elo->data[1] & 0x3f));
			input_report_abs(dev, ABS_Y, ((elo->data[2] & 0x3f) << 6) | (elo->data[3] & 0x3f));

			if (elo->id == 2) {
				input_report_key(dev, BTN_TOUCH, 1);
				input_sync(dev);
				elo->idx = 0;
			}

			break;

		case 4:
			if (data) {
				input_sync(dev);
				elo->idx = 0;
			}
			break;

		case 5:
			if ((data & 0xf0) == 0) {
				input_report_abs(dev, ABS_PRESSURE, elo->data[5]);
				input_report_key(dev, BTN_TOUCH, elo->data[5]);
			}
			input_sync(dev);
			elo->idx = 0;
			break;
	}
}

static void elo_process_data_3(struct elo* elo, unsigned char data, struct pt_regs *regs)
{
	struct input_dev *dev = &elo->dev;

	elo->data[elo->idx] = data;

	switch (elo->idx++) {

		case 0:
			if ((data & 0x7f) != 0x01)
				elo->idx = 0;
			break;
		case 2:
			input_regs(dev, regs);
			input_report_key(dev, BTN_TOUCH, !(elo->data[1] & 0x80));
			input_report_abs(dev, ABS_X, elo->data[1]);
			input_report_abs(dev, ABS_Y, elo->data[2]);
			input_sync(dev);
			elo->idx = 0;
			break;
	}
}

static irqreturn_t elo_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct elo* elo = serio_get_drvdata(serio);

	switch(elo->id) {
		case 0:
			elo_process_data_10(elo, data, regs);
			break;

		case 1:
		case 2:
			elo_process_data_6(elo, data, regs);
			break;

		case 3:
			elo_process_data_3(elo, data, regs);
			break;
	}

	return IRQ_HANDLED;
}

/*
 * elo_disconnect() is the opposite of elo_connect()
 */

static void elo_disconnect(struct serio *serio)
{
	struct elo* elo = serio_get_drvdata(serio);

	input_unregister_device(&elo->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(elo);
}

/*
 * elo_connect() is the routine that is called when someone adds a
 * new serio device that supports Gunze protocol and registers it as
 * an input device.
 */

static int elo_connect(struct serio *serio, struct serio_driver *drv)
{
	struct elo *elo;
	int err;

	if (!(elo = kmalloc(sizeof(struct elo), GFP_KERNEL)))
		return -ENOMEM;

	memset(elo, 0, sizeof(struct elo));

	init_input_dev(&elo->dev);
	elo->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	elo->dev.keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

	elo->id = serio->id.id;

	switch (elo->id) {

		case 0: /* 10-byte protocol */
			input_set_abs_params(&elo->dev, ABS_X, 96, 4000, 0, 0);
			input_set_abs_params(&elo->dev, ABS_Y, 96, 4000, 0, 0);
			input_set_abs_params(&elo->dev, ABS_PRESSURE, 0, 255, 0, 0);
			break;

		case 1: /* 6-byte protocol */
			input_set_abs_params(&elo->dev, ABS_PRESSURE, 0, 15, 0, 0);

		case 2: /* 4-byte protocol */
			input_set_abs_params(&elo->dev, ABS_X, 96, 4000, 0, 0);
			input_set_abs_params(&elo->dev, ABS_Y, 96, 4000, 0, 0);
			break;

		case 3: /* 3-byte protocol */
			input_set_abs_params(&elo->dev, ABS_X, 0, 255, 0, 0);
			input_set_abs_params(&elo->dev, ABS_Y, 0, 255, 0, 0);
			break;
	}

	elo->serio = serio;

	sprintf(elo->phys, "%s/input0", serio->phys);

	elo->dev.private = elo;
	elo->dev.name = elo_name;
	elo->dev.phys = elo->phys;
	elo->dev.id.bustype = BUS_RS232;
	elo->dev.id.vendor = SERIO_ELO;
	elo->dev.id.product = elo->id;
	elo->dev.id.version = 0x0100;

	serio_set_drvdata(serio, elo);

	err = serio_open(serio, drv);
	if (err) {
		serio_set_drvdata(serio, NULL);
		kfree(elo);
		return err;
	}

	input_register_device(&elo->dev);

	printk(KERN_INFO "input: %s on %s\n", elo_name, serio->phys);

	return 0;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id elo_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_ELO,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, elo_serio_ids);

static struct serio_driver elo_drv = {
	.driver		= {
		.name	= "elo",
	},
	.description	= DRIVER_DESC,
	.id_table	= elo_serio_ids,
	.interrupt	= elo_interrupt,
	.connect	= elo_connect,
	.disconnect	= elo_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init elo_init(void)
{
	serio_register_driver(&elo_drv);
	return 0;
}

static void __exit elo_exit(void)
{
	serio_unregister_driver(&elo_drv);
}

module_init(elo_init);
module_exit(elo_exit);
