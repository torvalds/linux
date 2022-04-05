// SPDX-License-Identifier: GPL-2.0
/*
 * HID driver for the Creative SB0540 receiver
 *
 * Copyright (C) 2019 Red Hat Inc. All Rights Reserved
 *
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_DESCRIPTION("HID Creative SB0540 receiver");
MODULE_LICENSE("GPL");

static const unsigned short creative_sb0540_key_table[] = {
	KEY_POWER,
	KEY_RESERVED,		/* text: 24bit */
	KEY_RESERVED,		/* 24bit wheel up */
	KEY_RESERVED,		/* 24bit wheel down */
	KEY_RESERVED,		/* text: CMSS */
	KEY_RESERVED,		/* CMSS wheel Up */
	KEY_RESERVED,		/* CMSS wheel Down */
	KEY_RESERVED,		/* text: EAX */
	KEY_RESERVED,		/* EAX wheel up */
	KEY_RESERVED,		/* EAX wheel down */
	KEY_RESERVED,		/* text: 3D Midi */
	KEY_RESERVED,		/* 3D Midi wheel up */
	KEY_RESERVED,		/* 3D Midi wheel down */
	KEY_MUTE,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_UP,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_REWIND,
	KEY_OK,
	KEY_FASTFORWARD,
	KEY_DOWN,
	KEY_AGAIN,		/* text: Return, symbol: Jump to */
	KEY_PLAY,		/* text: Start */
	KEY_ESC,		/* text: Cancel */
	KEY_RECORD,
	KEY_OPTION,
	KEY_MENU,		/* text: Display */
	KEY_PREVIOUS,
	KEY_PLAYPAUSE,
	KEY_NEXT,
	KEY_SLOW,
	KEY_STOP,
	KEY_NUMERIC_1,
	KEY_NUMERIC_2,
	KEY_NUMERIC_3,
	KEY_NUMERIC_4,
	KEY_NUMERIC_5,
	KEY_NUMERIC_6,
	KEY_NUMERIC_7,
	KEY_NUMERIC_8,
	KEY_NUMERIC_9,
	KEY_NUMERIC_0
};

/*
 * Codes and keys from lirc's
 * remotes/creative/lircd.conf.alsa_usb
 * order and size must match creative_sb0540_key_table[] above
 */
static const unsigned short creative_sb0540_codes[] = {
	0x619E,
	0x916E,
	0x926D,
	0x936C,
	0x718E,
	0x946B,
	0x956A,
	0x8C73,
	0x9669,
	0x9768,
	0x9867,
	0x9966,
	0x9A65,
	0x6E91,
	0x629D,
	0x639C,
	0x7B84,
	0x6B94,
	0x728D,
	0x8778,
	0x817E,
	0x758A,
	0x8D72,
	0x8E71,
	0x8877,
	0x7C83,
	0x738C,
	0x827D,
	0x7689,
	0x7F80,
	0x7986,
	0x7A85,
	0x7D82,
	0x857A,
	0x8B74,
	0x8F70,
	0x906F,
	0x8A75,
	0x847B,
	0x7887,
	0x8976,
	0x837C,
	0x7788,
	0x807F
};

struct creative_sb0540 {
	struct input_dev *input_dev;
	struct hid_device *hid;
	unsigned short keymap[ARRAY_SIZE(creative_sb0540_key_table)];
};

static inline u64 reverse(u64 data, int bits)
{
	int i;
	u64 c;

	c = 0;
	for (i = 0; i < bits; i++) {
		c |= (u64) (((data & (((u64) 1) << i)) ? 1 : 0))
			<< (bits - 1 - i);
	}
	return (c);
}

static int get_key(struct creative_sb0540 *creative_sb0540, u64 keycode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(creative_sb0540_codes); i++) {
		if (creative_sb0540_codes[i] == keycode)
			return creative_sb0540->keymap[i];
	}

	return 0;

}

static int creative_sb0540_raw_event(struct hid_device *hid,
	struct hid_report *report, u8 *data, int len)
{
	struct creative_sb0540 *creative_sb0540 = hid_get_drvdata(hid);
	u64 code, main_code;
	int key;

	if (len != 6)
		return 0;

	/* From daemons/hw_hiddev.c sb0540_rec() in lirc */
	code = reverse(data[5], 8);
	main_code = (code << 8) + ((~code) & 0xff);

	/*
	 * Flip to get values in the same format as
	 * remotes/creative/lircd.conf.alsa_usb in lirc
	 */
	main_code = ((main_code & 0xff) << 8) +
		((main_code & 0xff00) >> 8);

	key = get_key(creative_sb0540, main_code);
	if (key == 0 || key == KEY_RESERVED) {
		hid_err(hid, "Could not get a key for main_code %llX\n",
			main_code);
		return 0;
	}

	input_report_key(creative_sb0540->input_dev, key, 1);
	input_report_key(creative_sb0540->input_dev, key, 0);
	input_sync(creative_sb0540->input_dev);

	/* let hidraw and hiddev handle the report */
	return 0;
}

static int creative_sb0540_input_configured(struct hid_device *hid,
		struct hid_input *hidinput)
{
	struct input_dev *input_dev = hidinput->input;
	struct creative_sb0540 *creative_sb0540 = hid_get_drvdata(hid);
	int i;

	creative_sb0540->input_dev = input_dev;

	input_dev->keycode = creative_sb0540->keymap;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = ARRAY_SIZE(creative_sb0540->keymap);

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	memcpy(creative_sb0540->keymap, creative_sb0540_key_table,
		sizeof(creative_sb0540->keymap));
	for (i = 0; i < ARRAY_SIZE(creative_sb0540_key_table); i++)
		set_bit(creative_sb0540->keymap[i], input_dev->keybit);
	clear_bit(KEY_RESERVED, input_dev->keybit);

	return 0;
}

static int creative_sb0540_input_mapping(struct hid_device *hid,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/*
	 * We are remapping the keys ourselves, so ignore the hid-input
	 * keymap processing.
	 */
	return -1;
}

static int creative_sb0540_probe(struct hid_device *hid,
		const struct hid_device_id *id)
{
	int ret;
	struct creative_sb0540 *creative_sb0540;

	creative_sb0540 = devm_kzalloc(&hid->dev,
		sizeof(struct creative_sb0540), GFP_KERNEL);

	if (!creative_sb0540)
		return -ENOMEM;

	creative_sb0540->hid = hid;

	/* force input as some remotes bypass the input registration */
	hid->quirks |= HID_QUIRK_HIDINPUT_FORCE;

	hid_set_drvdata(hid, creative_sb0540);

	ret = hid_parse(hid);
	if (ret) {
		hid_err(hid, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hid, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hid, "hw start failed\n");
		return ret;
	}

	return ret;
}

static const struct hid_device_id creative_sb0540_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CREATIVELABS, USB_DEVICE_ID_CREATIVE_SB0540) },
	{ }
};
MODULE_DEVICE_TABLE(hid, creative_sb0540_devices);

static struct hid_driver creative_sb0540_driver = {
	.name = "creative-sb0540",
	.id_table = creative_sb0540_devices,
	.raw_event = creative_sb0540_raw_event,
	.input_configured = creative_sb0540_input_configured,
	.probe = creative_sb0540_probe,
	.input_mapping = creative_sb0540_input_mapping,
};
module_hid_driver(creative_sb0540_driver);
