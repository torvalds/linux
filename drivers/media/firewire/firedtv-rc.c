// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 */

#include <linux/bitops.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "firedtv.h"

/* fixed table with older keycodes, geared towards MythTV */
static const u16 oldtable[] = {

	/* code from device: 0x4501...0x451f */

	KEY_ESC,
	KEY_F9,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_I,
	KEY_0,
	KEY_ENTER,
	KEY_RED,
	KEY_UP,
	KEY_GREEN,
	KEY_F10,
	KEY_SPACE,
	KEY_F11,
	KEY_YELLOW,
	KEY_DOWN,
	KEY_BLUE,
	KEY_Z,
	KEY_P,
	KEY_PAGEDOWN,
	KEY_LEFT,
	KEY_W,
	KEY_RIGHT,
	KEY_P,
	KEY_M,

	/* code from device: 0x4540...0x4542 */

	KEY_R,
	KEY_V,
	KEY_C,
};

/* user-modifiable table for a remote as sold in 2008 */
static const u16 keytable[] = {

	/* code from device: 0x0300...0x031f */

	[0x00] = KEY_POWER,
	[0x01] = KEY_SLEEP,
	[0x02] = KEY_STOP,
	[0x03] = KEY_OK,
	[0x04] = KEY_RIGHT,
	[0x05] = KEY_1,
	[0x06] = KEY_2,
	[0x07] = KEY_3,
	[0x08] = KEY_LEFT,
	[0x09] = KEY_4,
	[0x0a] = KEY_5,
	[0x0b] = KEY_6,
	[0x0c] = KEY_UP,
	[0x0d] = KEY_7,
	[0x0e] = KEY_8,
	[0x0f] = KEY_9,
	[0x10] = KEY_DOWN,
	[0x11] = KEY_TITLE,	/* "OSD" - fixme */
	[0x12] = KEY_0,
	[0x13] = KEY_F20,	/* "16:9" - fixme */
	[0x14] = KEY_SCREEN,	/* "FULL" - fixme */
	[0x15] = KEY_MUTE,
	[0x16] = KEY_SUBTITLE,
	[0x17] = KEY_RECORD,
	[0x18] = KEY_TEXT,
	[0x19] = KEY_AUDIO,
	[0x1a] = KEY_RED,
	[0x1b] = KEY_PREVIOUS,
	[0x1c] = KEY_REWIND,
	[0x1d] = KEY_PLAYPAUSE,
	[0x1e] = KEY_NEXT,
	[0x1f] = KEY_VOLUMEUP,

	/* code from device: 0x0340...0x0354 */

	[0x20] = KEY_CHANNELUP,
	[0x21] = KEY_F21,	/* "4:3" - fixme */
	[0x22] = KEY_TV,
	[0x23] = KEY_DVD,
	[0x24] = KEY_VCR,
	[0x25] = KEY_AUX,
	[0x26] = KEY_GREEN,
	[0x27] = KEY_YELLOW,
	[0x28] = KEY_BLUE,
	[0x29] = KEY_CHANNEL,	/* "CH.LIST" */
	[0x2a] = KEY_VENDOR,	/* "CI" - fixme */
	[0x2b] = KEY_VOLUMEDOWN,
	[0x2c] = KEY_CHANNELDOWN,
	[0x2d] = KEY_LAST,
	[0x2e] = KEY_INFO,
	[0x2f] = KEY_FORWARD,
	[0x30] = KEY_LIST,
	[0x31] = KEY_FAVORITES,
	[0x32] = KEY_MENU,
	[0x33] = KEY_EPG,
	[0x34] = KEY_EXIT,
};

int fdtv_register_rc(struct firedtv *fdtv, struct device *dev)
{
	struct input_dev *idev;
	int i, err;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;

	fdtv->remote_ctrl_dev = idev;
	idev->name = "FireDTV remote control";
	idev->dev.parent = dev;
	idev->evbit[0] = BIT_MASK(EV_KEY);
	idev->keycode = kmemdup(keytable, sizeof(keytable), GFP_KERNEL);
	if (!idev->keycode) {
		err = -ENOMEM;
		goto fail;
	}
	idev->keycodesize = sizeof(keytable[0]);
	idev->keycodemax = ARRAY_SIZE(keytable);

	for (i = 0; i < ARRAY_SIZE(keytable); i++)
		set_bit(keytable[i], idev->keybit);

	err = input_register_device(idev);
	if (err)
		goto fail_free_keymap;

	return 0;

fail_free_keymap:
	kfree(idev->keycode);
fail:
	input_free_device(idev);
	return err;
}

void fdtv_unregister_rc(struct firedtv *fdtv)
{
	cancel_work_sync(&fdtv->remote_ctrl_work);
	kfree(fdtv->remote_ctrl_dev->keycode);
	input_unregister_device(fdtv->remote_ctrl_dev);
}

void fdtv_handle_rc(struct firedtv *fdtv, unsigned int code)
{
	struct input_dev *idev = fdtv->remote_ctrl_dev;
	u16 *keycode = idev->keycode;

	if (code >= 0x0300 && code <= 0x031f)
		code = keycode[code - 0x0300];
	else if (code >= 0x0340 && code <= 0x0354)
		code = keycode[code - 0x0320];
	else if (code >= 0x4501 && code <= 0x451f)
		code = oldtable[code - 0x4501];
	else if (code >= 0x4540 && code <= 0x4542)
		code = oldtable[code - 0x4521];
	else {
		dev_dbg(fdtv->device,
			"invalid key code 0x%04x from remote control\n",
			code);
		return;
	}

	input_report_key(idev, code, 1);
	input_sync(idev);
	input_report_key(idev, code, 0);
	input_sync(idev);
}
