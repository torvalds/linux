/*
 * HID driver for the apple ir device
 *
 * Original driver written by James McKenzie
 * Ported to recent 2.6 kernel versions by Greg Kroah-Hartman <gregkh@suse.de>
 * Updated to support newer remotes by Bastien Nocera <hadess@hadess.net>
 * Ported to HID subsystem by Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *
 * Copyright (C) 2006 James McKenzie
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2008 Novell Inc.
 * Copyright (C) 2010, 2012 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (C) 2013 Red Hat Inc. All Rights Reserved
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

MODULE_AUTHOR("James McKenzie");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@redhat.com>");
MODULE_DESCRIPTION("HID Apple IR remote controls");
MODULE_LICENSE("GPL");

#define KEY_MASK		0x0F
#define TWO_PACKETS_MASK	0x40

/*
 * James McKenzie has two devices both of which report the following
 * 25 87 ee 83 0a	+
 * 25 87 ee 83 0c	-
 * 25 87 ee 83 09	<<
 * 25 87 ee 83 06	>>
 * 25 87 ee 83 05	>"
 * 25 87 ee 83 03	menu
 * 26 00 00 00 00	for key repeat
 */

/*
 * Thomas Glanzmann reports the following responses
 * 25 87 ee ca 0b	+
 * 25 87 ee ca 0d	-
 * 25 87 ee ca 08	<<
 * 25 87 ee ca 07	>>
 * 25 87 ee ca 04	>"
 * 25 87 ee ca 02	menu
 * 26 00 00 00 00       for key repeat
 *
 * He also observes the following event sometimes
 * sent after a key is release, which I interpret
 * as a flat battery message
 * 25 87 e0 ca 06	flat battery
 */

/*
 * Alexandre Karpenko reports the following responses for Device ID 0x8242
 * 25 87 ee 47 0b	+
 * 25 87 ee 47 0d	-
 * 25 87 ee 47 08	<<
 * 25 87 ee 47 07	>>
 * 25 87 ee 47 04	>"
 * 25 87 ee 47 02	menu
 * 26 87 ee 47 **	for key repeat (** is the code of the key being held)
 */

/*
 * Bastien Nocera's remote
 * 25 87 ee 91 5f	followed by
 * 25 87 ee 91 05	gives you >"
 *
 * 25 87 ee 91 5c	followed by
 * 25 87 ee 91 05	gives you the middle button
 */

/*
 * Fabien Andre's remote
 * 25 87 ee a3 5e	followed by
 * 25 87 ee a3 04	gives you >"
 *
 * 25 87 ee a3 5d	followed by
 * 25 87 ee a3 04	gives you the middle button
 */

static const unsigned short appleir_key_table[] = {
	KEY_RESERVED,
	KEY_MENU,
	KEY_PLAYPAUSE,
	KEY_FORWARD,
	KEY_BACK,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_ENTER,
	KEY_PLAYPAUSE,
	KEY_RESERVED,
};

struct appleir {
	struct input_dev *input_dev;
	struct hid_device *hid;
	unsigned short keymap[ARRAY_SIZE(appleir_key_table)];
	struct timer_list key_up_timer;	/* timer for key up */
	spinlock_t lock;		/* protects .current_key */
	int current_key;		/* the currently pressed key */
	int prev_key_idx;		/* key index in a 2 packets message */
};

static int get_key(int data)
{
	/*
	 * The key is coded accross bits 2..9:
	 *
	 * 0x00 or 0x01 (        )	key:  0		-> KEY_RESERVED
	 * 0x02 or 0x03 (  menu  )	key:  1		-> KEY_MENU
	 * 0x04 or 0x05 (   >"   )	key:  2		-> KEY_PLAYPAUSE
	 * 0x06 or 0x07 (   >>   )	key:  3		-> KEY_FORWARD
	 * 0x08 or 0x09 (   <<   )	key:  4		-> KEY_BACK
	 * 0x0a or 0x0b (    +   )	key:  5		-> KEY_VOLUMEUP
	 * 0x0c or 0x0d (    -   )	key:  6		-> KEY_VOLUMEDOWN
	 * 0x0e or 0x0f (        )	key:  7		-> KEY_RESERVED
	 * 0x50 or 0x51 (        )	key:  8		-> KEY_RESERVED
	 * 0x52 or 0x53 (        )	key:  9		-> KEY_RESERVED
	 * 0x54 or 0x55 (        )	key: 10		-> KEY_RESERVED
	 * 0x56 or 0x57 (        )	key: 11		-> KEY_RESERVED
	 * 0x58 or 0x59 (        )	key: 12		-> KEY_RESERVED
	 * 0x5a or 0x5b (        )	key: 13		-> KEY_RESERVED
	 * 0x5c or 0x5d ( middle )	key: 14		-> KEY_ENTER
	 * 0x5e or 0x5f (   >"   )	key: 15		-> KEY_PLAYPAUSE
	 *
	 * Packets starting with 0x5 are part of a two-packets message,
	 * we notify the caller by sending a negative value.
	 */
	int key = (data >> 1) & KEY_MASK;

	if ((data & TWO_PACKETS_MASK))
		/* Part of a 2 packets-command */
		key = -key;

	return key;
}

static void key_up(struct hid_device *hid, struct appleir *appleir, int key)
{
	input_report_key(appleir->input_dev, key, 0);
	input_sync(appleir->input_dev);
}

static void key_down(struct hid_device *hid, struct appleir *appleir, int key)
{
	input_report_key(appleir->input_dev, key, 1);
	input_sync(appleir->input_dev);
}

static void battery_flat(struct appleir *appleir)
{
	dev_err(&appleir->input_dev->dev, "possible flat battery?\n");
}

static void key_up_tick(unsigned long data)
{
	struct appleir *appleir = (struct appleir *)data;
	struct hid_device *hid = appleir->hid;
	unsigned long flags;

	spin_lock_irqsave(&appleir->lock, flags);
	if (appleir->current_key) {
		key_up(hid, appleir, appleir->current_key);
		appleir->current_key = 0;
	}
	spin_unlock_irqrestore(&appleir->lock, flags);
}

static int appleir_raw_event(struct hid_device *hid, struct hid_report *report,
	 u8 *data, int len)
{
	struct appleir *appleir = hid_get_drvdata(hid);
	static const u8 keydown[] = { 0x25, 0x87, 0xee };
	static const u8 keyrepeat[] = { 0x26, };
	static const u8 flatbattery[] = { 0x25, 0x87, 0xe0 };
	unsigned long flags;

	if (len != 5)
		goto out;

	if (!memcmp(data, keydown, sizeof(keydown))) {
		int index;

		spin_lock_irqsave(&appleir->lock, flags);
		/*
		 * If we already have a key down, take it up before marking
		 * this one down
		 */
		if (appleir->current_key)
			key_up(hid, appleir, appleir->current_key);

		/* Handle dual packet commands */
		if (appleir->prev_key_idx > 0)
			index = appleir->prev_key_idx;
		else
			index = get_key(data[4]);

		if (index >= 0) {
			appleir->current_key = appleir->keymap[index];

			key_down(hid, appleir, appleir->current_key);
			/*
			 * Remote doesn't do key up, either pull them up, in
			 * the test above, or here set a timer which pulls
			 * them up after 1/8 s
			 */
			mod_timer(&appleir->key_up_timer, jiffies + HZ / 8);
			appleir->prev_key_idx = 0;
		} else
			/* Remember key for next packet */
			appleir->prev_key_idx = -index;
		spin_unlock_irqrestore(&appleir->lock, flags);
		goto out;
	}

	appleir->prev_key_idx = 0;

	if (!memcmp(data, keyrepeat, sizeof(keyrepeat))) {
		key_down(hid, appleir, appleir->current_key);
		/*
		 * Remote doesn't do key up, either pull them up, in the test
		 * above, or here set a timer which pulls them up after 1/8 s
		 */
		mod_timer(&appleir->key_up_timer, jiffies + HZ / 8);
		goto out;
	}

	if (!memcmp(data, flatbattery, sizeof(flatbattery))) {
		battery_flat(appleir);
		/* Fall through */
	}

out:
	/* let hidraw and hiddev handle the report */
	return 0;
}

static int appleir_input_configured(struct hid_device *hid,
		struct hid_input *hidinput)
{
	struct input_dev *input_dev = hidinput->input;
	struct appleir *appleir = hid_get_drvdata(hid);
	int i;

	appleir->input_dev = input_dev;

	input_dev->keycode = appleir->keymap;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = ARRAY_SIZE(appleir->keymap);

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	memcpy(appleir->keymap, appleir_key_table, sizeof(appleir->keymap));
	for (i = 0; i < ARRAY_SIZE(appleir_key_table); i++)
		set_bit(appleir->keymap[i], input_dev->keybit);
	clear_bit(KEY_RESERVED, input_dev->keybit);

	return 0;
}

static int appleir_input_mapping(struct hid_device *hid,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	return -1;
}

static int appleir_probe(struct hid_device *hid, const struct hid_device_id *id)
{
	int ret;
	struct appleir *appleir;

	appleir = kzalloc(sizeof(struct appleir), GFP_KERNEL);
	if (!appleir) {
		ret = -ENOMEM;
		goto allocfail;
	}

	appleir->hid = hid;

	/* force input as some remotes bypass the input registration */
	hid->quirks |= HID_QUIRK_HIDINPUT_FORCE;

	spin_lock_init(&appleir->lock);
	setup_timer(&appleir->key_up_timer,
		    key_up_tick, (unsigned long) appleir);

	hid_set_drvdata(hid, appleir);

	ret = hid_parse(hid);
	if (ret) {
		hid_err(hid, "parse failed\n");
		goto fail;
	}

	ret = hid_hw_start(hid, HID_CONNECT_DEFAULT | HID_CONNECT_HIDDEV_FORCE);
	if (ret) {
		hid_err(hid, "hw start failed\n");
		goto fail;
	}

	return 0;
fail:
	kfree(appleir);
allocfail:
	return ret;
}

static void appleir_remove(struct hid_device *hid)
{
	struct appleir *appleir = hid_get_drvdata(hid);
	hid_hw_stop(hid);
	del_timer_sync(&appleir->key_up_timer);
	kfree(appleir);
}

static const struct hid_device_id appleir_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_IRCONTROL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_IRCONTROL2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_IRCONTROL3) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_IRCONTROL4) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_IRCONTROL5) },
	{ }
};
MODULE_DEVICE_TABLE(hid, appleir_devices);

static struct hid_driver appleir_driver = {
	.name = "appleir",
	.id_table = appleir_devices,
	.raw_event = appleir_raw_event,
	.input_configured = appleir_input_configured,
	.probe = appleir_probe,
	.remove = appleir_remove,
	.input_mapping = appleir_input_mapping,
};
module_hid_driver(appleir_driver);
