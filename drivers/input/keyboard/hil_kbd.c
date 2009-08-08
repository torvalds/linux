/*
 * Generic linux-input device driver for keyboard devices
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * HP-HIL Technical Reference Manual.  Hewlett Packard Product No. 45918A
 *
 */

#include <linux/hil.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/pci_ids.h>

#define PREFIX "HIL KEYB: "
#define HIL_GENERIC_NAME "HIL keyboard"

MODULE_AUTHOR("Brian S. Julin <bri@calyx.com>");
MODULE_DESCRIPTION(HIL_GENERIC_NAME " driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("serio:ty03pr25id00ex*");

#define HIL_KBD_MAX_LENGTH 16

#define HIL_KBD_SET1_UPBIT 0x01
#define HIL_KBD_SET1_SHIFT 1
static unsigned int hil_kbd_set1[HIL_KEYCODES_SET1_TBLSIZE] __read_mostly =
	{ HIL_KEYCODES_SET1 };

#define HIL_KBD_SET2_UPBIT 0x01
#define HIL_KBD_SET2_SHIFT 1
/* Set2 is user defined */

#define HIL_KBD_SET3_UPBIT 0x80
#define HIL_KBD_SET3_SHIFT 0
static unsigned int hil_kbd_set3[HIL_KEYCODES_SET3_TBLSIZE] __read_mostly =
	{ HIL_KEYCODES_SET3 };

static const char hil_language[][16] = { HIL_LOCALE_MAP };

struct hil_kbd {
	struct input_dev *dev;
	struct serio *serio;

	/* Input buffer and index for packets from HIL bus. */
	hil_packet data[HIL_KBD_MAX_LENGTH];
	int idx4; /* four counts per packet */

	/* Raw device info records from HIL bus, see hil.h for fields. */
	char	idd[HIL_KBD_MAX_LENGTH];	/* DID byte and IDD record */
	char	rsc[HIL_KBD_MAX_LENGTH];	/* RSC record */
	char	exd[HIL_KBD_MAX_LENGTH];	/* EXD record */
	char	rnm[HIL_KBD_MAX_LENGTH + 1];	/* RNM record + NULL term. */

	struct completion cmd_done;
};

static bool hil_kbd_is_command_response(hil_packet p)
{
	if ((p & ~HIL_CMDCT_POL) == (HIL_ERR_INT | HIL_PKT_CMD | HIL_CMD_POL))
		return false;

	if ((p & ~HIL_CMDCT_RPL) == (HIL_ERR_INT | HIL_PKT_CMD | HIL_CMD_RPL))
		return false;

	return true;
}

static void hil_kbd_handle_command_response(struct hil_kbd *kbd)
{
	hil_packet p;
	char *buf;
	int i, idx;

	idx = kbd->idx4 / 4;
	p = kbd->data[idx - 1];

	switch (p & HIL_PKT_DATA_MASK) {
	case HIL_CMD_IDD:
		buf = kbd->idd;
		break;

	case HIL_CMD_RSC:
		buf = kbd->rsc;
		break;

	case HIL_CMD_EXD:
		buf = kbd->exd;
		break;

	case HIL_CMD_RNM:
		kbd->rnm[HIL_KBD_MAX_LENGTH] = 0;
		buf = kbd->rnm;
		break;

	default:
		/* These occur when device isn't present */
		if (p != (HIL_ERR_INT | HIL_PKT_CMD)) {
			/* Anything else we'd like to know about. */
			printk(KERN_WARNING PREFIX "Device sent unknown record %x\n", p);
		}
		goto out;
	}

	for (i = 0; i < idx; i++)
		buf[i] = kbd->data[i] & HIL_PKT_DATA_MASK;
	for (; i < HIL_KBD_MAX_LENGTH; i++)
		buf[i] = 0;
 out:
	complete(&kbd->cmd_done);
}

static void hil_kbd_handle_key_events(struct hil_kbd *kbd)
{
	struct input_dev *dev = kbd->dev;
	int idx = kbd->idx4 / 4;
	int i;

	switch (kbd->data[0] & HIL_POL_CHARTYPE_MASK) {
	case HIL_POL_CHARTYPE_NONE:
		return;

	case HIL_POL_CHARTYPE_ASCII:
		for (i = 1; i < idx - 1; i++)
			input_report_key(dev, kbd->data[i] & 0x7f, 1);
		break;

	case HIL_POL_CHARTYPE_RSVD1:
	case HIL_POL_CHARTYPE_RSVD2:
	case HIL_POL_CHARTYPE_BINARY:
		for (i = 1; i < idx - 1; i++)
			input_report_key(dev, kbd->data[i], 1);
		break;

	case HIL_POL_CHARTYPE_SET1:
		for (i = 1; i < idx - 1; i++) {
			unsigned int key = kbd->data[i];
			int up = key & HIL_KBD_SET1_UPBIT;

			key &= (~HIL_KBD_SET1_UPBIT & 0xff);
			key = hil_kbd_set1[key >> HIL_KBD_SET1_SHIFT];
			input_report_key(dev, key, !up);
		}
		break;

	case HIL_POL_CHARTYPE_SET2:
		for (i = 1; i < idx - 1; i++) {
			unsigned int key = kbd->data[i];
			int up = key & HIL_KBD_SET2_UPBIT;

			key &= (~HIL_KBD_SET1_UPBIT & 0xff);
			key = key >> HIL_KBD_SET2_SHIFT;
			input_report_key(dev, key, !up);
		}
		break;

	case HIL_POL_CHARTYPE_SET3:
		for (i = 1; i < idx - 1; i++) {
			unsigned int key = kbd->data[i];
			int up = key & HIL_KBD_SET3_UPBIT;

			key &= (~HIL_KBD_SET1_UPBIT & 0xff);
			key = hil_kbd_set3[key >> HIL_KBD_SET3_SHIFT];
			input_report_key(dev, key, !up);
		}
		break;
	}

	input_sync(dev);
}

static void hil_kbd_process_err(struct hil_kbd *kbd)
{
	printk(KERN_WARNING PREFIX "errored HIL packet\n");
	kbd->idx4 = 0;
	complete(&kbd->cmd_done); /* just in case somebody is waiting */
}

static irqreturn_t hil_kbd_interrupt(struct serio *serio,
				unsigned char data, unsigned int flags)
{
	struct hil_kbd *kbd;
	hil_packet packet;
	int idx;

	kbd = serio_get_drvdata(serio);
	BUG_ON(kbd == NULL);

	if (kbd->idx4 >= HIL_KBD_MAX_LENGTH * sizeof(hil_packet)) {
		hil_kbd_process_err(kbd);
		goto out;
	}

	idx = kbd->idx4 / 4;
	if (!(kbd->idx4 % 4))
		kbd->data[idx] = 0;
	packet = kbd->data[idx];
	packet |= ((hil_packet)data) << ((3 - (kbd->idx4 % 4)) * 8);
	kbd->data[idx] = packet;

	/* Records of N 4-byte hil_packets must terminate with a command. */
	if ((++kbd->idx4 % 4) == 0) {
		if ((packet & 0xffff0000) != HIL_ERR_INT) {
			hil_kbd_process_err(kbd);
		} else if (packet & HIL_PKT_CMD) {
			if (hil_kbd_is_command_response(packet))
				hil_kbd_handle_command_response(kbd);
			else
				hil_kbd_handle_key_events(kbd);
			kbd->idx4 = 0;
		}
	}
 out:
	return IRQ_HANDLED;
}

static void hil_kbd_disconnect(struct serio *serio)
{
	struct hil_kbd *kbd = serio_get_drvdata(serio);

	BUG_ON(kbd == NULL);

	serio_close(serio);
	input_unregister_device(kbd->dev);
	kfree(kbd);
}

static int hil_kbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct hil_kbd *kbd;
	struct input_dev *input_dev;
	uint8_t did, *idd;
	int i;
	int error;

	kbd = kzalloc(sizeof(*kbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!kbd || !input_dev) {
		error = -ENOMEM;
		goto bail0;
	}

	kbd->serio = serio;
	kbd->dev = input_dev;

	error = serio_open(serio, drv);
	if (error)
		goto bail0;

	serio_set_drvdata(serio, kbd);

	/* Get device info.  MLC driver supplies devid/status/etc. */
	init_completion(&kbd->cmd_done);
	serio_write(serio, 0);
	serio_write(serio, 0);
	serio_write(serio, HIL_PKT_CMD >> 8);
	serio_write(serio, HIL_CMD_IDD);
	error = wait_for_completion_killable(&kbd->cmd_done);
	if (error)
		goto bail1;

	init_completion(&kbd->cmd_done);
	serio_write(serio, 0);
	serio_write(serio, 0);
	serio_write(serio, HIL_PKT_CMD >> 8);
	serio_write(serio, HIL_CMD_RSC);
	error = wait_for_completion_killable(&kbd->cmd_done);
	if (error)
		goto bail1;

	init_completion(&kbd->cmd_done);
	serio_write(serio, 0);
	serio_write(serio, 0);
	serio_write(serio, HIL_PKT_CMD >> 8);
	serio_write(serio, HIL_CMD_RNM);
	error = wait_for_completion_killable(&kbd->cmd_done);
	if (error)
		goto bail1;

	init_completion(&kbd->cmd_done);
	serio_write(serio, 0);
	serio_write(serio, 0);
	serio_write(serio, HIL_PKT_CMD >> 8);
	serio_write(serio, HIL_CMD_EXD);
	error = wait_for_completion_killable(&kbd->cmd_done);
	if (error)
		goto bail1;

	did = kbd->idd[0];
	idd = kbd->idd + 1;
	switch (did & HIL_IDD_DID_TYPE_MASK) {
	case HIL_IDD_DID_TYPE_KB_INTEGRAL:
	case HIL_IDD_DID_TYPE_KB_ITF:
	case HIL_IDD_DID_TYPE_KB_RSVD:
	case HIL_IDD_DID_TYPE_CHAR:
		printk(KERN_INFO PREFIX "HIL keyboard found (did = 0x%02x, lang = %s)\n",
			did, hil_language[did & HIL_IDD_DID_TYPE_KB_LANG_MASK]);
		break;
	default:
		goto bail1;
	}

	if (HIL_IDD_NUM_BUTTONS(idd) || HIL_IDD_NUM_AXES_PER_SET(*idd)) {
		printk(KERN_INFO PREFIX "keyboards only, no combo devices supported.\n");
		goto bail1;
	}

	input_dev->evbit[0]	= BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->ledbit[0]	= BIT_MASK(LED_NUML) | BIT_MASK(LED_CAPSL) |
				  BIT_MASK(LED_SCROLLL);
	input_dev->keycodemax	= HIL_KEYCODES_SET1_TBLSIZE;
	input_dev->keycodesize	= sizeof(hil_kbd_set1[0]);
	input_dev->keycode	= hil_kbd_set1;
	input_dev->name		= strlen(kbd->rnm) ? kbd->rnm : HIL_GENERIC_NAME;
	input_dev->phys		= "hpkbd/input0";	/* XXX */

	input_dev->id.bustype	= BUS_HIL;
	input_dev->id.vendor	= PCI_VENDOR_ID_HP;
	input_dev->id.product	= 0x0001; /* TODO: get from kbd->rsc */
	input_dev->id.version	= 0x0100; /* TODO: get from kbd->rsc */
	input_dev->dev.parent	= &serio->dev;

	for (i = 0; i < 128; i++) {
		__set_bit(hil_kbd_set1[i], input_dev->keybit);
		__set_bit(hil_kbd_set3[i], input_dev->keybit);
	}
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	serio_write(serio, 0);
	serio_write(serio, 0);
	serio_write(serio, HIL_PKT_CMD >> 8);
	serio_write(serio, HIL_CMD_EK1); /* Enable Keyswitch Autorepeat 1 */
	/* No need to wait for completion */

	error = input_register_device(kbd->dev);
	if (error)
		goto bail1;

	return 0;

 bail1:
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
 bail0:
	input_free_device(input_dev);
	kfree(kbd);
	return error;
}

static struct serio_device_id hil_kbd_ids[] = {
	{
		.type = SERIO_HIL_MLC,
		.proto = SERIO_HIL,
		.id = SERIO_ANY,
		.extra = SERIO_ANY,
	},
	{ 0 }
};

static struct serio_driver hil_kbd_serio_drv = {
	.driver		= {
		.name	= "hil_kbd",
	},
	.description	= "HP HIL keyboard driver",
	.id_table	= hil_kbd_ids,
	.connect	= hil_kbd_connect,
	.disconnect	= hil_kbd_disconnect,
	.interrupt	= hil_kbd_interrupt
};

static int __init hil_kbd_init(void)
{
	return serio_register_driver(&hil_kbd_serio_drv);
}

static void __exit hil_kbd_exit(void)
{
	serio_unregister_driver(&hil_kbd_serio_drv);
}

module_init(hil_kbd_init);
module_exit(hil_kbd_exit);
