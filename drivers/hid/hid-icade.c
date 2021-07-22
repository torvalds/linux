// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ION iCade input driver
 *
 *  Copyright (c) 2012 Bastien Nocera <hadess@hadess.net>
 *  Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

/*
 *   ↑      A C Y L
 *  ← →
 *   ↓      B X Z R
 *
 *
 *  UP ON,OFF  = w,e
 *  RT ON,OFF  = d,c
 *  DN ON,OFF  = x,z
 *  LT ON,OFF  = a,q
 *  A  ON,OFF  = y,t
 *  B  ON,OFF  = h,r
 *  C  ON,OFF  = u,f
 *  X  ON,OFF  = j,n
 *  Y  ON,OFF  = i,m
 *  Z  ON,OFF  = k,p
 *  L  ON,OFF  = o,g
 *  R  ON,OFF  = l,v
 */

/* The translation code uses HID usage instead of input layer
 * keys. This code generates a lookup table that makes
 * translation quick.
 *
 * #include <linux/input.h>
 * #include <stdio.h>
 * #include <assert.h>
 *
 * #define unk     KEY_UNKNOWN
 *
 * < copy of hid_keyboard[] from hid-input.c >
 *
 * struct icade_key_translation {
 *     int         from;
 *     const char *to;
 *     int         press;
 * };
 *
 * static const struct icade_key_translation icade_keys[] = {
 *    { KEY_W,        "KEY_UP",         1 },
 *    { KEY_E,        "KEY_UP",         0 },
 *    { KEY_D,        "KEY_RIGHT",      1 },
 *    { KEY_C,        "KEY_RIGHT",      0 },
 *    { KEY_X,        "KEY_DOWN",       1 },
 *    { KEY_Z,        "KEY_DOWN",       0 },
 *    { KEY_A,        "KEY_LEFT",       1 },
 *    { KEY_Q,        "KEY_LEFT",       0 },
 *    { KEY_Y,        "BTN_A",          1 },
 *    { KEY_T,        "BTN_A",          0 },
 *    { KEY_H,        "BTN_B",          1 },
 *    { KEY_R,        "BTN_B",          0 },
 *    { KEY_U,        "BTN_C",          1 },
 *    { KEY_F,        "BTN_C",          0 },
 *    { KEY_J,        "BTN_X",          1 },
 *    { KEY_N,        "BTN_X",          0 },
 *    { KEY_I,        "BTN_Y",          1 },
 *    { KEY_M,        "BTN_Y",          0 },
 *    { KEY_K,        "BTN_Z",          1 },
 *    { KEY_P,        "BTN_Z",          0 },
 *    { KEY_O,        "BTN_THUMBL",     1 },
 *    { KEY_G,        "BTN_THUMBL",     0 },
 *    { KEY_L,        "BTN_THUMBR",     1 },
 *    { KEY_V,        "BTN_THUMBR",     0 },
 *
 *    { }
 * };
 *
 * static int
 * usage_for_key (int key)
 * {
 *     int i;
 *     for (i = 0; i < 256; i++) {
 *     if (hid_keyboard[i] == key)
 *         return i;
 *     }
 *     assert(0);
 * }
 *
 * int main (int argc, char **argv)
 * {
 *     const struct icade_key_translation *trans;
 *     int max_usage = 0;
 *
 *     for (trans = icade_keys; trans->from; trans++) {
 *         int usage = usage_for_key (trans->from);
 *         max_usage = usage > max_usage ? usage : max_usage;
 *     }
 *
 *     printf ("#define ICADE_MAX_USAGE %d\n\n", max_usage);
 *     printf ("struct icade_key {\n");
 *     printf ("\tu16 to;\n");
 *     printf ("\tu8 press:1;\n");
 *     printf ("};\n\n");
 *     printf ("static const struct icade_key "
 *             "icade_usage_table[%d] = {\n", max_usage + 1);
 *     for (trans = icade_keys; trans->from; trans++) {
 *         printf ("\t[%d] = { %s, %d },\n",
 *                 usage_for_key (trans->from), trans->to, trans->press);
 *     }
 *     printf ("};\n");
 *
 *     return 0;
 * }
 */

#define ICADE_MAX_USAGE 29

struct icade_key {
	u16 to;
	u8 press:1;
};

static const struct icade_key icade_usage_table[30] = {
	[26] = { KEY_UP, 1 },
	[8] = { KEY_UP, 0 },
	[7] = { KEY_RIGHT, 1 },
	[6] = { KEY_RIGHT, 0 },
	[27] = { KEY_DOWN, 1 },
	[29] = { KEY_DOWN, 0 },
	[4] = { KEY_LEFT, 1 },
	[20] = { KEY_LEFT, 0 },
	[28] = { BTN_A, 1 },
	[23] = { BTN_A, 0 },
	[11] = { BTN_B, 1 },
	[21] = { BTN_B, 0 },
	[24] = { BTN_C, 1 },
	[9] = { BTN_C, 0 },
	[13] = { BTN_X, 1 },
	[17] = { BTN_X, 0 },
	[12] = { BTN_Y, 1 },
	[16] = { BTN_Y, 0 },
	[14] = { BTN_Z, 1 },
	[19] = { BTN_Z, 0 },
	[18] = { BTN_THUMBL, 1 },
	[10] = { BTN_THUMBL, 0 },
	[15] = { BTN_THUMBR, 1 },
	[25] = { BTN_THUMBR, 0 },
};

static const struct icade_key *icade_find_translation(u16 from)
{
	if (from > ICADE_MAX_USAGE)
		return NULL;
	return &icade_usage_table[from];
}

static int icade_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	const struct icade_key *trans;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type)
		return 0;

	/* We ignore the fake key up, and act only on key down */
	if (!value)
		return 1;

	trans = icade_find_translation(usage->hid & HID_USAGE);

	if (!trans)
		return 1;

	input_event(field->hidinput->input, usage->type,
			trans->to, trans->press);

	return 1;
}

static int icade_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	const struct icade_key *trans;

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_KEYBOARD) {
		trans = icade_find_translation(usage->hid & HID_USAGE);

		if (!trans)
			return -1;

		hid_map_usage(hi, usage, bit, max, EV_KEY, trans->to);
		set_bit(trans->to, hi->input->keybit);

		return 1;
	}

	/* ignore others */
	return -1;

}

static int icade_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->type == EV_KEY)
		set_bit(usage->type, hi->input->evbit);

	return -1;
}

static const struct hid_device_id icade_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ION, USB_DEVICE_ID_ICADE) },

	{ }
};
MODULE_DEVICE_TABLE(hid, icade_devices);

static struct hid_driver icade_driver = {
	.name = "icade",
	.id_table = icade_devices,
	.event = icade_event,
	.input_mapped = icade_input_mapped,
	.input_mapping = icade_input_mapping,
};
module_hid_driver(icade_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_DESCRIPTION("ION iCade input driver");
