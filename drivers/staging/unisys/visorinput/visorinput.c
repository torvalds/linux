/* visorinput.c
 *
 * Copyright (C) 2011 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * This driver lives in a generic guest Linux partition, and registers to
 * receive keyboard and mouse channels from the visorbus driver.  It reads
 * inputs from such channels, and delivers it to the Linux OS in the
 * standard way the Linux expects for input drivers.
 */

#include <linux/buffer_head.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/uuid.h>

#include "visorbus.h"
#include "ultrainputreport.h"

/* Keyboard channel {c73416d0-b0b8-44af-b304-9d2ae99f1b3d} */
#define VISOR_KEYBOARD_CHANNEL_UUID \
	UUID_LE(0xc73416d0, 0xb0b8, 0x44af, \
		0xb3, 0x4, 0x9d, 0x2a, 0xe9, 0x9f, 0x1b, 0x3d)
#define VISOR_KEYBOARD_CHANNEL_UUID_STR "c73416d0-b0b8-44af-b304-9d2ae99f1b3d"

/* Mouse channel {addf07d4-94a9-46e2-81c3-61abcdbdbd87} */
#define VISOR_MOUSE_CHANNEL_UUID \
	UUID_LE(0xaddf07d4, 0x94a9, 0x46e2, \
		0x81, 0xc3, 0x61, 0xab, 0xcd, 0xbd, 0xbd, 0x87)
#define VISOR_MOUSE_CHANNEL_UUID_STR "addf07d4-94a9-46e2-81c3-61abcdbdbd87"

#define PIXELS_ACROSS_DEFAULT 800
#define PIXELS_DOWN_DEFAULT   600
#define KEYCODE_TABLE_BYTES   256

enum visorinput_device_type {
	visorinput_keyboard,
	visorinput_mouse,
};

/*
 * This is the private data that we store for each device.
 * A pointer to this struct is maintained via
 * dev_get_drvdata() / dev_set_drvdata() for each struct device.
 */
struct visorinput_devdata {
	struct visor_device *dev;
	/* lock for dev */
	struct mutex lock_visor_dev;
	struct input_dev *visorinput_dev;
	bool paused;
	bool interrupts_enabled;
	/* size of following array */
	unsigned int keycode_table_bytes;
	/* for keyboard devices: visorkbd_keycode[] + visorkbd_ext_keycode[] */
	unsigned char keycode_table[0];
};

static const uuid_le visor_keyboard_channel_uuid = VISOR_KEYBOARD_CHANNEL_UUID;
static const uuid_le visor_mouse_channel_uuid = VISOR_MOUSE_CHANNEL_UUID;

/*
 * Borrowed from drivers/input/keyboard/atakbd.c
 * This maps 1-byte scancodes to keycodes.
 */
static const unsigned char visorkbd_keycode[KEYCODE_TABLE_BYTES] = {
	/* American layout */
	[0] = KEY_GRAVE,
	[1] = KEY_ESC,
	[2] = KEY_1,
	[3] = KEY_2,
	[4] = KEY_3,
	[5] = KEY_4,
	[6] = KEY_5,
	[7] = KEY_6,
	[8] = KEY_7,
	[9] = KEY_8,
	[10] = KEY_9,
	[11] = KEY_0,
	[12] = KEY_MINUS,
	[13] = KEY_EQUAL,
	[14] = KEY_BACKSPACE,
	[15] = KEY_TAB,
	[16] = KEY_Q,
	[17] = KEY_W,
	[18] = KEY_E,
	[19] = KEY_R,
	[20] = KEY_T,
	[21] = KEY_Y,
	[22] = KEY_U,
	[23] = KEY_I,
	[24] = KEY_O,
	[25] = KEY_P,
	[26] = KEY_LEFTBRACE,
	[27] = KEY_RIGHTBRACE,
	[28] = KEY_ENTER,
	[29] = KEY_LEFTCTRL,
	[30] = KEY_A,
	[31] = KEY_S,
	[32] = KEY_D,
	[33] = KEY_F,
	[34] = KEY_G,
	[35] = KEY_H,
	[36] = KEY_J,
	[37] = KEY_K,
	[38] = KEY_L,
	[39] = KEY_SEMICOLON,
	[40] = KEY_APOSTROPHE,
	[41] = KEY_GRAVE,
	[42] = KEY_LEFTSHIFT,
	[43] = KEY_BACKSLASH,
	[44] = KEY_Z,
	[45] = KEY_X,
	[46] = KEY_C,
	[47] = KEY_V,
	[48] = KEY_B,
	[49] = KEY_N,
	[50] = KEY_M,
	[51] = KEY_COMMA,
	[52] = KEY_DOT,
	[53] = KEY_SLASH,
	[54] = KEY_RIGHTSHIFT,
	[55] = KEY_KPASTERISK,
	[56] = KEY_LEFTALT,
	[57] = KEY_SPACE,
	[58] = KEY_CAPSLOCK,
	[59] = KEY_F1,
	[60] = KEY_F2,
	[61] = KEY_F3,
	[62] = KEY_F4,
	[63] = KEY_F5,
	[64] = KEY_F6,
	[65] = KEY_F7,
	[66] = KEY_F8,
	[67] = KEY_F9,
	[68] = KEY_F10,
	[69] = KEY_NUMLOCK,
	[70] = KEY_SCROLLLOCK,
	[71] = KEY_KP7,
	[72] = KEY_KP8,
	[73] = KEY_KP9,
	[74] = KEY_KPMINUS,
	[75] = KEY_KP4,
	[76] = KEY_KP5,
	[77] = KEY_KP6,
	[78] = KEY_KPPLUS,
	[79] = KEY_KP1,
	[80] = KEY_KP2,
	[81] = KEY_KP3,
	[82] = KEY_KP0,
	[83] = KEY_KPDOT,
	/* enables UK backslash+pipe key and FR lessthan+greaterthan key */
	[86] = KEY_102ND,
	[87] = KEY_F11,
	[88] = KEY_F12,
	[90] = KEY_KPLEFTPAREN,
	[91] = KEY_KPRIGHTPAREN,
	[92] = KEY_KPASTERISK,
	[93] = KEY_KPASTERISK,
	[94] = KEY_KPPLUS,
	[95] = KEY_HELP,
	[96] = KEY_KPENTER,
	[97] = KEY_RIGHTCTRL,
	[98] = KEY_KPSLASH,
	[99] = KEY_KPLEFTPAREN,
	[100] = KEY_KPRIGHTPAREN,
	[101] = KEY_KPSLASH,
	[102] = KEY_HOME,
	[103] = KEY_UP,
	[104] = KEY_PAGEUP,
	[105] = KEY_LEFT,
	[106] = KEY_RIGHT,
	[107] = KEY_END,
	[108] = KEY_DOWN,
	[109] = KEY_PAGEDOWN,
	[110] = KEY_INSERT,
	[111] = KEY_DELETE,
	[112] = KEY_MACRO,
	[113] = KEY_MUTE
};

/*
 * This maps the <xx> in extended scancodes of the form "0xE0 <xx>" into
 * keycodes.
 */
static const unsigned char visorkbd_ext_keycode[KEYCODE_TABLE_BYTES] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		    /* 0x00 */
	0, 0, 0, 0, 0, 0, 0, 0,					    /* 0x10 */
	0, 0, 0, 0, KEY_KPENTER, KEY_RIGHTCTRL, 0, 0,		    /* 0x18 */
	0, 0, 0, 0, 0, 0, 0, 0,					    /* 0x20 */
	KEY_RIGHTALT, 0, 0, 0, 0, 0, 0, 0,			    /* 0x28 */
	0, 0, 0, 0, 0, 0, 0, 0,					    /* 0x30 */
	KEY_RIGHTALT /* AltGr */, 0, 0, 0, 0, 0, 0, 0,		    /* 0x38 */
	0, 0, 0, 0, 0, 0, 0, KEY_HOME,				    /* 0x40 */
	KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0, KEY_END,  /* 0x48 */
	KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, 0, /* 0x50 */
	0, 0, 0, 0, 0, 0, 0, 0,					    /* 0x58 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		    /* 0x60 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		    /* 0x70 */
};

static int visorinput_open(struct input_dev *visorinput_dev)
{
	struct visorinput_devdata *devdata = input_get_drvdata(visorinput_dev);

	if (!devdata) {
		dev_err(&visorinput_dev->dev,
			"%s input_get_drvdata(%p) returned NULL\n",
			__func__, visorinput_dev);
		return -EINVAL;
	}
	dev_dbg(&visorinput_dev->dev, "%s opened\n", __func__);

	/*
	 * If we're not paused, really enable interrupts.
	 * Regardless of whether we are paused, set a flag indicating
	 * interrupts should be enabled so when we resume, interrupts
	 * will really be enabled.
	 */
	mutex_lock(&devdata->lock_visor_dev);
	devdata->interrupts_enabled = true;
	if (devdata->paused)
		goto out_unlock;
	visorbus_enable_channel_interrupts(devdata->dev);

out_unlock:
	mutex_unlock(&devdata->lock_visor_dev);
	return 0;
}

static void visorinput_close(struct input_dev *visorinput_dev)
{
	struct visorinput_devdata *devdata = input_get_drvdata(visorinput_dev);

	if (!devdata) {
		dev_err(&visorinput_dev->dev,
			"%s input_get_drvdata(%p) returned NULL\n",
			__func__, visorinput_dev);
		return;
	}
	dev_dbg(&visorinput_dev->dev, "%s closed\n", __func__);

	/*
	 * If we're not paused, really disable interrupts.
	 * Regardless of whether we are paused, set a flag indicating
	 * interrupts should be disabled so when we resume we will
	 * not re-enable them.
	 */

	mutex_lock(&devdata->lock_visor_dev);
	devdata->interrupts_enabled = false;
	if (devdata->paused)
		goto out_unlock;
	visorbus_disable_channel_interrupts(devdata->dev);

out_unlock:
	mutex_unlock(&devdata->lock_visor_dev);
}

/*
 * setup_client_keyboard() initializes and returns a Linux input node that
 * we can use to deliver keyboard inputs to Linux.  We of course do this when
 * we see keyboard inputs coming in on a keyboard channel.
 */
static struct input_dev *
setup_client_keyboard(void *devdata,  /* opaque on purpose */
		      unsigned char *keycode_table)

{
	int i;
	struct input_dev *visorinput_dev;

	visorinput_dev = input_allocate_device();
	if (!visorinput_dev)
		return NULL;

	visorinput_dev->name = "visor Keyboard";
	visorinput_dev->phys = "visorkbd:input0";
	visorinput_dev->id.bustype = BUS_VIRTUAL;
	visorinput_dev->id.vendor = 0x0001;
	visorinput_dev->id.product = 0x0001;
	visorinput_dev->id.version = 0x0100;

	visorinput_dev->evbit[0] = BIT_MASK(EV_KEY) |
				   BIT_MASK(EV_REP) |
				   BIT_MASK(EV_LED);
	visorinput_dev->ledbit[0] = BIT_MASK(LED_CAPSL) |
				    BIT_MASK(LED_SCROLLL) |
				    BIT_MASK(LED_NUML);
	visorinput_dev->keycode = keycode_table;
	/* sizeof(unsigned char) */
	visorinput_dev->keycodesize = 1;
	visorinput_dev->keycodemax = KEYCODE_TABLE_BYTES;

	for (i = 1; i < visorinput_dev->keycodemax; i++)
		set_bit(keycode_table[i], visorinput_dev->keybit);
	for (i = 1; i < visorinput_dev->keycodemax; i++)
		set_bit(keycode_table[i + KEYCODE_TABLE_BYTES],
			visorinput_dev->keybit);

	visorinput_dev->open = visorinput_open;
	visorinput_dev->close = visorinput_close;
	/* pre input_register! */
	input_set_drvdata(visorinput_dev, devdata);

	return visorinput_dev;
}

static struct input_dev *
setup_client_mouse(void *devdata /* opaque on purpose */)
{
	struct input_dev *visorinput_dev = NULL;
	int xres, yres;
	struct fb_info *fb0;

	visorinput_dev = input_allocate_device();
	if (!visorinput_dev)
		return NULL;

	visorinput_dev->name = "visor Mouse";
	visorinput_dev->phys = "visormou:input0";
	visorinput_dev->id.bustype = BUS_VIRTUAL;
	visorinput_dev->id.vendor = 0x0001;
	visorinput_dev->id.product = 0x0002;
	visorinput_dev->id.version = 0x0100;

	visorinput_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	set_bit(BTN_LEFT, visorinput_dev->keybit);
	set_bit(BTN_RIGHT, visorinput_dev->keybit);
	set_bit(BTN_MIDDLE, visorinput_dev->keybit);

	if (registered_fb[0]) {
		fb0 = registered_fb[0];
		xres = fb0->var.xres_virtual;
		yres = fb0->var.yres_virtual;
	} else {
		xres = PIXELS_ACROSS_DEFAULT;
		yres = PIXELS_DOWN_DEFAULT;
	}
	input_set_abs_params(visorinput_dev, ABS_X, 0, xres, 0, 0);
	input_set_abs_params(visorinput_dev, ABS_Y, 0, yres, 0, 0);

	visorinput_dev->open = visorinput_open;
	visorinput_dev->close = visorinput_close;
	/* pre input_register! */
	input_set_drvdata(visorinput_dev, devdata);
	input_set_capability(visorinput_dev, EV_REL, REL_WHEEL);

	return visorinput_dev;
}

static struct visorinput_devdata *
devdata_create(struct visor_device *dev, enum visorinput_device_type devtype)
{
	struct visorinput_devdata *devdata = NULL;
	unsigned int extra_bytes = 0;

	if (devtype == visorinput_keyboard)
		/* allocate room for devdata->keycode_table, filled in below */
		extra_bytes = KEYCODE_TABLE_BYTES * 2;
	devdata = kzalloc(sizeof(*devdata) + extra_bytes, GFP_KERNEL);
	if (!devdata)
		return NULL;
	mutex_init(&devdata->lock_visor_dev);
	mutex_lock(&devdata->lock_visor_dev);
	devdata->dev = dev;

	/*
	 * visorinput_open() can be called as soon as input_register_device()
	 * happens, and that will enable channel interrupts.  Setting paused
	 * prevents us from getting into visorinput_channel_interrupt() prior
	 * to the device structure being totally initialized.
	 */
	devdata->paused = true;

	/*
	 * This is an input device in a client guest partition,
	 * so we need to create whatever input nodes are necessary to
	 * deliver our inputs to the guest OS.
	 */
	switch (devtype) {
	case visorinput_keyboard:
		devdata->keycode_table_bytes = extra_bytes;
		memcpy(devdata->keycode_table, visorkbd_keycode,
		       KEYCODE_TABLE_BYTES);
		memcpy(devdata->keycode_table + KEYCODE_TABLE_BYTES,
		       visorkbd_ext_keycode, KEYCODE_TABLE_BYTES);
		devdata->visorinput_dev = setup_client_keyboard
			(devdata, devdata->keycode_table);
		if (!devdata->visorinput_dev)
			goto cleanups_register;
		break;
	case visorinput_mouse:
		devdata->visorinput_dev = setup_client_mouse(devdata);
		if (!devdata->visorinput_dev)
			goto cleanups_register;
		break;
	default:
		/* No other input devices supported */
		break;
	}

	dev_set_drvdata(&dev->device, devdata);
	mutex_unlock(&devdata->lock_visor_dev);

	/*
	 * Device struct is completely set up now, with the exception of
	 * visorinput_dev being registered.
	 * We need to unlock before we register the device, because this
	 * can cause an on-stack call of visorinput_open(), which would
	 * deadlock if we had the lock.
	 */
	if (input_register_device(devdata->visorinput_dev)) {
		input_free_device(devdata->visorinput_dev);
		goto err_kfree_devdata;
	}

	mutex_lock(&devdata->lock_visor_dev);
	/*
	 * Establish calls to visorinput_channel_interrupt() if that is
	 * the desired state that we've kept track of in interrupts_enabled
	 * while the device was being created.
	 */
	devdata->paused = false;
	if (devdata->interrupts_enabled)
		visorbus_enable_channel_interrupts(dev);
	mutex_unlock(&devdata->lock_visor_dev);

	return devdata;

cleanups_register:
	mutex_unlock(&devdata->lock_visor_dev);
err_kfree_devdata:
	kfree(devdata);
	return NULL;
}

static int
visorinput_probe(struct visor_device *dev)
{
	uuid_le guid;
	enum visorinput_device_type devtype;

	guid = visorchannel_get_uuid(dev->visorchannel);
	if (uuid_le_cmp(guid, visor_mouse_channel_uuid) == 0)
		devtype = visorinput_mouse;
	else if (uuid_le_cmp(guid, visor_keyboard_channel_uuid) == 0)
		devtype = visorinput_keyboard;
	else
		return -ENODEV;
	visorbus_disable_channel_interrupts(dev);
	if (!devdata_create(dev, devtype))
		return -ENOMEM;
	return 0;
}

static void
unregister_client_input(struct input_dev *visorinput_dev)
{
	if (visorinput_dev)
		input_unregister_device(visorinput_dev);
}

static void
visorinput_remove(struct visor_device *dev)
{
	struct visorinput_devdata *devdata = dev_get_drvdata(&dev->device);

	if (!devdata)
		return;

	mutex_lock(&devdata->lock_visor_dev);
	visorbus_disable_channel_interrupts(dev);

	/*
	 * due to above, at this time no thread of execution will be
	 * in visorinput_channel_interrupt()
	 */

	dev_set_drvdata(&dev->device, NULL);
	mutex_unlock(&devdata->lock_visor_dev);

	unregister_client_input(devdata->visorinput_dev);
	kfree(devdata);
}

/*
 * Make it so the current locking state of the locking key indicated by
 * <keycode> is as indicated by <desired_state> (1=locked, 0=unlocked).
 */
static void
handle_locking_key(struct input_dev *visorinput_dev,
		   int keycode, int desired_state)
{
	int led;

	switch (keycode) {
	case KEY_CAPSLOCK:
		led = LED_CAPSL;
		break;
	case KEY_SCROLLLOCK:
		led = LED_SCROLLL;
		break;
	case KEY_NUMLOCK:
		led = LED_NUML;
		break;
	default:
		led = -1;
		return;
	}
	if (test_bit(led, visorinput_dev->led) != desired_state) {
		input_report_key(visorinput_dev, keycode, 1);
		input_sync(visorinput_dev);
		input_report_key(visorinput_dev, keycode, 0);
		input_sync(visorinput_dev);
		__change_bit(led, visorinput_dev->led);
	}
}

/*
 * <scancode> is either a 1-byte scancode, or an extended 16-bit scancode
 * with 0xE0 in the low byte and the extended scancode value in the next
 * higher byte.
 */
static int
scancode_to_keycode(int scancode)
{
	if (scancode > 0xff)
		return visorkbd_ext_keycode[(scancode >> 8) & 0xff];

	return  visorkbd_keycode[scancode];
}

static int
calc_button(int x)
{
	switch (x) {
	case 1:
		return BTN_LEFT;
	case 2:
		return BTN_MIDDLE;
	case 3:
		return BTN_RIGHT;
	default:
		return -EINVAL;
	}
}

/*
 * This is used only when this driver is active as an input driver in the
 * client guest partition.  It is called periodically so we can obtain inputs
 * from the channel, and deliver them to the guest OS.
 */
static void
visorinput_channel_interrupt(struct visor_device *dev)
{
	struct visor_inputreport r;
	int scancode, keycode;
	struct input_dev *visorinput_dev;
	int xmotion, ymotion, button;
	int i;

	struct visorinput_devdata *devdata = dev_get_drvdata(&dev->device);

	if (!devdata)
		return;

	visorinput_dev = devdata->visorinput_dev;

	while (!visorchannel_signalremove(dev->visorchannel, 0, &r)) {
		scancode = r.activity.arg1;
		keycode = scancode_to_keycode(scancode);
		switch (r.activity.action) {
		case INPUTACTION_KEY_DOWN:
			input_report_key(visorinput_dev, keycode, 1);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_KEY_UP:
			input_report_key(visorinput_dev, keycode, 0);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_KEY_DOWN_UP:
			input_report_key(visorinput_dev, keycode, 1);
			input_sync(visorinput_dev);
			input_report_key(visorinput_dev, keycode, 0);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_SET_LOCKING_KEY_STATE:
			handle_locking_key(visorinput_dev, keycode,
					   r.activity.arg2);
			break;
		case INPUTACTION_XY_MOTION:
			xmotion = r.activity.arg1;
			ymotion = r.activity.arg2;
			input_report_abs(visorinput_dev, ABS_X, xmotion);
			input_report_abs(visorinput_dev, ABS_Y, ymotion);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_MOUSE_BUTTON_DOWN:
			button = calc_button(r.activity.arg1);
			if (button < 0)
				break;
			input_report_key(visorinput_dev, button, 1);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_MOUSE_BUTTON_UP:
			button = calc_button(r.activity.arg1);
			if (button < 0)
				break;
			input_report_key(visorinput_dev, button, 0);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_MOUSE_BUTTON_CLICK:
			button = calc_button(r.activity.arg1);
			if (button < 0)
				break;
			input_report_key(visorinput_dev, button, 1);

			input_sync(visorinput_dev);
			input_report_key(visorinput_dev, button, 0);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_MOUSE_BUTTON_DCLICK:
			button = calc_button(r.activity.arg1);
			if (button < 0)
				break;
			for (i = 0; i < 2; i++) {
				input_report_key(visorinput_dev, button, 1);
				input_sync(visorinput_dev);
				input_report_key(visorinput_dev, button, 0);
				input_sync(visorinput_dev);
			}
			break;
		case INPUTACTION_WHEEL_ROTATE_AWAY:
			input_report_rel(visorinput_dev, REL_WHEEL, 1);
			input_sync(visorinput_dev);
			break;
		case INPUTACTION_WHEEL_ROTATE_TOWARD:
			input_report_rel(visorinput_dev, REL_WHEEL, -1);
			input_sync(visorinput_dev);
			break;
		default:
			/* Unsupported input action */
			break;
		}
	}
}

static int
visorinput_pause(struct visor_device *dev,
		 visorbus_state_complete_func complete_func)
{
	int rc;
	struct visorinput_devdata *devdata = dev_get_drvdata(&dev->device);

	if (!devdata) {
		rc = -ENODEV;
		goto out;
	}

	mutex_lock(&devdata->lock_visor_dev);
	if (devdata->paused) {
		rc = -EBUSY;
		goto out_locked;
	}
	if (devdata->interrupts_enabled)
		visorbus_disable_channel_interrupts(dev);

	/*
	 * due to above, at this time no thread of execution will be
	 * in visorinput_channel_interrupt()
	 */

	devdata->paused = true;
	complete_func(dev, 0);
	rc = 0;
out_locked:
	mutex_unlock(&devdata->lock_visor_dev);
out:
	return rc;
}

static int
visorinput_resume(struct visor_device *dev,
		  visorbus_state_complete_func complete_func)
{
	int rc;
	struct visorinput_devdata *devdata = dev_get_drvdata(&dev->device);

	if (!devdata) {
		rc = -ENODEV;
		goto out;
	}
	mutex_lock(&devdata->lock_visor_dev);
	if (!devdata->paused) {
		rc = -EBUSY;
		goto out_locked;
	}
	devdata->paused = false;
	complete_func(dev, 0);

	/*
	 * Re-establish calls to visorinput_channel_interrupt() if that is
	 * the desired state that we've kept track of in interrupts_enabled
	 * while the device was paused.
	 */
	if (devdata->interrupts_enabled)
		visorbus_enable_channel_interrupts(dev);

	rc = 0;
out_locked:
	mutex_unlock(&devdata->lock_visor_dev);
out:
	return rc;
}

/* GUIDS for all channel types supported by this driver. */
static struct visor_channeltype_descriptor visorinput_channel_types[] = {
	{ VISOR_KEYBOARD_CHANNEL_UUID, "keyboard"},
	{ VISOR_MOUSE_CHANNEL_UUID, "mouse"},
	{ NULL_UUID_LE, NULL }
};

static struct visor_driver visorinput_driver = {
	.name = "visorinput",
	.owner = THIS_MODULE,
	.channel_types = visorinput_channel_types,
	.probe = visorinput_probe,
	.remove = visorinput_remove,
	.channel_interrupt = visorinput_channel_interrupt,
	.pause = visorinput_pause,
	.resume = visorinput_resume,
};

static int
visorinput_init(void)
{
	return visorbus_register_visor_driver(&visorinput_driver);
}

static void
visorinput_cleanup(void)
{
	visorbus_unregister_visor_driver(&visorinput_driver);
}

module_init(visorinput_init);
module_exit(visorinput_cleanup);

MODULE_DEVICE_TABLE(visorbus, visorinput_channel_types);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("s-Par human input driver for virtual keyboard/mouse");

MODULE_ALIAS("visorbus:" VISOR_MOUSE_CHANNEL_UUID_STR);
MODULE_ALIAS("visorbus:" VISOR_KEYBOARD_CHANNEL_UUID_STR);
