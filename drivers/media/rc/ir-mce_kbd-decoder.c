/* ir-mce_kbd-decoder.c - A decoder for the RC6-ish keyboard/mouse IR protocol
 * used by the Microsoft Remote Keyboard for Windows Media Center Edition,
 * referred to by Microsoft's Windows Media Center remote specification docs
 * as "an internal protocol called MCIR-2".
 *
 * Copyright (C) 2011 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>

#include "rc-core-priv.h"

/*
 * This decoder currently supports:
 * - MCIR-2 29-bit IR signals used for mouse movement and buttons
 * - MCIR-2 32-bit IR signals used for standard keyboard keys
 *
 * The media keys on the keyboard send RC-6 signals that are inditinguishable
 * from the keys of the same name on the stock MCE remote, and will be handled
 * by the standard RC-6 decoder, and be made available to the system via the
 * input device for the remote, rather than the keyboard/mouse one.
 */

#define MCIR2_UNIT		333333	/* ns */
#define MCIR2_HEADER_NBITS	5
#define MCIR2_MOUSE_NBITS	29
#define MCIR2_KEYBOARD_NBITS	32
#define MCIR2_PREFIX_PULSE	(8 * MCIR2_UNIT)
#define MCIR2_PREFIX_SPACE	(1 * MCIR2_UNIT)
#define MCIR2_MAX_LEN		(3 * MCIR2_UNIT)
#define MCIR2_BIT_START		(1 * MCIR2_UNIT)
#define MCIR2_BIT_END		(1 * MCIR2_UNIT)
#define MCIR2_BIT_0		(1 * MCIR2_UNIT)
#define MCIR2_BIT_SET		(2 * MCIR2_UNIT)
#define MCIR2_MODE_MASK		0xf	/* for the header bits */
#define MCIR2_KEYBOARD_HEADER	0x4
#define MCIR2_MOUSE_HEADER	0x1
#define MCIR2_MASK_KEYS_START	0xe0

enum mce_kbd_mode {
	MCIR2_MODE_KEYBOARD,
	MCIR2_MODE_MOUSE,
	MCIR2_MODE_UNKNOWN,
};

enum mce_kbd_state {
	STATE_INACTIVE,
	STATE_HEADER_BIT_START,
	STATE_HEADER_BIT_END,
	STATE_BODY_BIT_START,
	STATE_BODY_BIT_END,
	STATE_FINISHED,
};

static unsigned char kbd_keycodes[256] = {
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_A,
	KEY_B,		KEY_C,		KEY_D,		KEY_E,		KEY_F,
	KEY_G,		KEY_H,		KEY_I,		KEY_J,		KEY_K,
	KEY_L,		KEY_M,		KEY_N,		KEY_O,		KEY_P,
	KEY_Q,		KEY_R,		KEY_S,		KEY_T,		KEY_U,
	KEY_V,		KEY_W,		KEY_X,		KEY_Y,		KEY_Z,
	KEY_1,		KEY_2,		KEY_3,		KEY_4,		KEY_5,
	KEY_6,		KEY_7,		KEY_8,		KEY_9,		KEY_0,
	KEY_ENTER,	KEY_ESC,	KEY_BACKSPACE,	KEY_TAB,	KEY_SPACE,
	KEY_MINUS,	KEY_EQUAL,	KEY_LEFTBRACE,	KEY_RIGHTBRACE,	KEY_BACKSLASH,
	KEY_RESERVED,	KEY_SEMICOLON,	KEY_APOSTROPHE,	KEY_GRAVE,	KEY_COMMA,
	KEY_DOT,	KEY_SLASH,	KEY_CAPSLOCK,	KEY_F1,		KEY_F2,
	KEY_F3,		KEY_F4,		KEY_F5,		KEY_F6,		KEY_F7,
	KEY_F8,		KEY_F9,		KEY_F10,	KEY_F11,	KEY_F12,
	KEY_SYSRQ,	KEY_SCROLLLOCK,	KEY_PAUSE,	KEY_INSERT,	KEY_HOME,
	KEY_PAGEUP,	KEY_DELETE,	KEY_END,	KEY_PAGEDOWN,	KEY_RIGHT,
	KEY_LEFT,	KEY_DOWN,	KEY_UP,		KEY_NUMLOCK,	KEY_KPSLASH,
	KEY_KPASTERISK,	KEY_KPMINUS,	KEY_KPPLUS,	KEY_KPENTER,	KEY_KP1,
	KEY_KP2,	KEY_KP3,	KEY_KP4,	KEY_KP5,	KEY_KP6,
	KEY_KP7,	KEY_KP8,	KEY_KP9,	KEY_KP0,	KEY_KPDOT,
	KEY_102ND,	KEY_COMPOSE,	KEY_POWER,	KEY_KPEQUAL,	KEY_F13,
	KEY_F14,	KEY_F15,	KEY_F16,	KEY_F17,	KEY_F18,
	KEY_F19,	KEY_F20,	KEY_F21,	KEY_F22,	KEY_F23,
	KEY_F24,	KEY_OPEN,	KEY_HELP,	KEY_PROPS,	KEY_FRONT,
	KEY_STOP,	KEY_AGAIN,	KEY_UNDO,	KEY_CUT,	KEY_COPY,
	KEY_PASTE,	KEY_FIND,	KEY_MUTE,	KEY_VOLUMEUP,	KEY_VOLUMEDOWN,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_KPCOMMA,	KEY_RESERVED,
	KEY_RO,		KEY_KATAKANAHIRAGANA, KEY_YEN,	KEY_HENKAN,	KEY_MUHENKAN,
	KEY_KPJPCOMMA,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_HANGUEL,
	KEY_HANJA,	KEY_KATAKANA,	KEY_HIRAGANA,	KEY_ZENKAKUHANKAKU, KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,	KEY_LEFTCTRL,
	KEY_LEFTSHIFT,	KEY_LEFTALT,	KEY_LEFTMETA,	KEY_RIGHTCTRL,	KEY_RIGHTSHIFT,
	KEY_RIGHTALT,	KEY_RIGHTMETA,	KEY_PLAYPAUSE,	KEY_STOPCD,	KEY_PREVIOUSSONG,
	KEY_NEXTSONG,	KEY_EJECTCD,	KEY_VOLUMEUP,	KEY_VOLUMEDOWN,	KEY_MUTE,
	KEY_WWW,	KEY_BACK,	KEY_FORWARD,	KEY_STOP,	KEY_FIND,
	KEY_SCROLLUP,	KEY_SCROLLDOWN,	KEY_EDIT,	KEY_SLEEP,	KEY_COFFEE,
	KEY_REFRESH,	KEY_CALC,	KEY_RESERVED,	KEY_RESERVED,	KEY_RESERVED,
	KEY_RESERVED
};

static void mce_kbd_rx_timeout(unsigned long data)
{
	struct mce_kbd_dec *mce_kbd = (struct mce_kbd_dec *)data;
	int i;
	unsigned char maskcode;

	IR_dprintk(2, "timer callback clearing all keys\n");

	for (i = 0; i < 7; i++) {
		maskcode = kbd_keycodes[MCIR2_MASK_KEYS_START + i];
		input_report_key(mce_kbd->idev, maskcode, 0);
	}

	for (i = 0; i < MCIR2_MASK_KEYS_START; i++)
		input_report_key(mce_kbd->idev, kbd_keycodes[i], 0);
}

static enum mce_kbd_mode mce_kbd_mode(struct mce_kbd_dec *data)
{
	switch (data->header & MCIR2_MODE_MASK) {
	case MCIR2_KEYBOARD_HEADER:
		return MCIR2_MODE_KEYBOARD;
	case MCIR2_MOUSE_HEADER:
		return MCIR2_MODE_MOUSE;
	default:
		return MCIR2_MODE_UNKNOWN;
	}
}

static void ir_mce_kbd_process_keyboard_data(struct input_dev *idev,
					     u32 scancode)
{
	u8 keydata   = (scancode >> 8) & 0xff;
	u8 shiftmask = scancode & 0xff;
	unsigned char keycode, maskcode;
	int i, keystate;

	IR_dprintk(1, "keyboard: keydata = 0x%02x, shiftmask = 0x%02x\n",
		   keydata, shiftmask);

	for (i = 0; i < 7; i++) {
		maskcode = kbd_keycodes[MCIR2_MASK_KEYS_START + i];
		if (shiftmask & (1 << i))
			keystate = 1;
		else
			keystate = 0;
		input_report_key(idev, maskcode, keystate);
	}

	if (keydata) {
		keycode = kbd_keycodes[keydata];
		input_report_key(idev, keycode, 1);
	} else {
		for (i = 0; i < MCIR2_MASK_KEYS_START; i++)
			input_report_key(idev, kbd_keycodes[i], 0);
	}
}

static void ir_mce_kbd_process_mouse_data(struct input_dev *idev, u32 scancode)
{
	/* raw mouse coordinates */
	u8 xdata = (scancode >> 7) & 0x7f;
	u8 ydata = (scancode >> 14) & 0x7f;
	int x, y;
	/* mouse buttons */
	bool right = scancode & 0x40;
	bool left  = scancode & 0x20;

	if (xdata & 0x40)
		x = -((~xdata & 0x7f) + 1);
	else
		x = xdata;

	if (ydata & 0x40)
		y = -((~ydata & 0x7f) + 1);
	else
		y = ydata;

	IR_dprintk(1, "mouse: x = %d, y = %d, btns = %s%s\n",
		   x, y, left ? "L" : "", right ? "R" : "");

	input_report_rel(idev, REL_X, x);
	input_report_rel(idev, REL_Y, y);

	input_report_key(idev, BTN_LEFT, left);
	input_report_key(idev, BTN_RIGHT, right);
}

/**
 * ir_mce_kbd_decode() - Decode one mce_kbd pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_mce_kbd_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct mce_kbd_dec *data = &dev->raw->mce_kbd;
	u32 scancode;
	unsigned long delay;

	if (!rc_protocols_enabled(dev, RC_BIT_MCE_KBD))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, MCIR2_UNIT, MCIR2_UNIT / 2))
		goto out;

again:
	IR_dprintk(2, "started at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	if (!geq_margin(ev.duration, MCIR2_UNIT, MCIR2_UNIT / 2))
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		/* Note: larger margin on first pulse since each MCIR2_UNIT
		   is quite short and some hardware takes some time to
		   adjust to the signal */
		if (!eq_margin(ev.duration, MCIR2_PREFIX_PULSE, MCIR2_UNIT))
			break;

		data->state = STATE_HEADER_BIT_START;
		data->count = 0;
		data->header = 0;
		return 0;

	case STATE_HEADER_BIT_START:
		if (geq_margin(ev.duration, MCIR2_MAX_LEN, MCIR2_UNIT / 2))
			break;

		data->header <<= 1;
		if (ev.pulse)
			data->header |= 1;
		data->count++;
		data->state = STATE_HEADER_BIT_END;
		return 0;

	case STATE_HEADER_BIT_END:
		if (!is_transition(&ev, &dev->raw->prev_ev))
			break;

		decrease_duration(&ev, MCIR2_BIT_END);

		if (data->count != MCIR2_HEADER_NBITS) {
			data->state = STATE_HEADER_BIT_START;
			goto again;
		}

		switch (mce_kbd_mode(data)) {
		case MCIR2_MODE_KEYBOARD:
			data->wanted_bits = MCIR2_KEYBOARD_NBITS;
			break;
		case MCIR2_MODE_MOUSE:
			data->wanted_bits = MCIR2_MOUSE_NBITS;
			break;
		default:
			IR_dprintk(1, "not keyboard or mouse data\n");
			goto out;
		}

		data->count = 0;
		data->body = 0;
		data->state = STATE_BODY_BIT_START;
		goto again;

	case STATE_BODY_BIT_START:
		if (geq_margin(ev.duration, MCIR2_MAX_LEN, MCIR2_UNIT / 2))
			break;

		data->body <<= 1;
		if (ev.pulse)
			data->body |= 1;
		data->count++;
		data->state = STATE_BODY_BIT_END;
		return 0;

	case STATE_BODY_BIT_END:
		if (!is_transition(&ev, &dev->raw->prev_ev))
			break;

		if (data->count == data->wanted_bits)
			data->state = STATE_FINISHED;
		else
			data->state = STATE_BODY_BIT_START;

		decrease_duration(&ev, MCIR2_BIT_END);
		goto again;

	case STATE_FINISHED:
		if (ev.pulse)
			break;

		switch (data->wanted_bits) {
		case MCIR2_KEYBOARD_NBITS:
			scancode = data->body & 0xffff;
			IR_dprintk(1, "keyboard data 0x%08x\n", data->body);
			if (dev->timeout)
				delay = usecs_to_jiffies(dev->timeout / 1000);
			else
				delay = msecs_to_jiffies(100);
			mod_timer(&data->rx_timeout, jiffies + delay);
			/* Pass data to keyboard buffer parser */
			ir_mce_kbd_process_keyboard_data(data->idev, scancode);
			break;
		case MCIR2_MOUSE_NBITS:
			scancode = data->body & 0x1fffff;
			IR_dprintk(1, "mouse data 0x%06x\n", scancode);
			/* Pass data to mouse buffer parser */
			ir_mce_kbd_process_mouse_data(data->idev, scancode);
			break;
		default:
			IR_dprintk(1, "not keyboard or mouse data\n");
			goto out;
		}

		data->state = STATE_INACTIVE;
		input_sync(data->idev);
		return 0;
	}

out:
	IR_dprintk(1, "failed at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	input_sync(data->idev);
	return -EINVAL;
}

static int ir_mce_kbd_register(struct rc_dev *dev)
{
	struct mce_kbd_dec *mce_kbd = &dev->raw->mce_kbd;
	struct input_dev *idev;
	int i, ret;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;

	snprintf(mce_kbd->name, sizeof(mce_kbd->name),
		 "MCE IR Keyboard/Mouse (%s)", dev->driver_name);
	strlcat(mce_kbd->phys, "/input0", sizeof(mce_kbd->phys));

	idev->name = mce_kbd->name;
	idev->phys = mce_kbd->phys;

	/* Keyboard bits */
	set_bit(EV_KEY, idev->evbit);
	set_bit(EV_REP, idev->evbit);
	for (i = 0; i < sizeof(kbd_keycodes); i++)
		set_bit(kbd_keycodes[i], idev->keybit);

	/* Mouse bits */
	set_bit(EV_REL, idev->evbit);
	set_bit(REL_X, idev->relbit);
	set_bit(REL_Y, idev->relbit);
	set_bit(BTN_LEFT, idev->keybit);
	set_bit(BTN_RIGHT, idev->keybit);

	/* Report scancodes too */
	set_bit(EV_MSC, idev->evbit);
	set_bit(MSC_SCAN, idev->mscbit);

	setup_timer(&mce_kbd->rx_timeout, mce_kbd_rx_timeout,
		    (unsigned long)mce_kbd);

	input_set_drvdata(idev, mce_kbd);

#if 0
	/* Adding this reference means two input devices are associated with
	 * this rc-core device, which ir-keytable doesn't cope with yet */
	idev->dev.parent = &dev->dev;
#endif

	ret = input_register_device(idev);
	if (ret < 0) {
		input_free_device(idev);
		return -EIO;
	}

	mce_kbd->idev = idev;

	return 0;
}

static int ir_mce_kbd_unregister(struct rc_dev *dev)
{
	struct mce_kbd_dec *mce_kbd = &dev->raw->mce_kbd;
	struct input_dev *idev = mce_kbd->idev;

	del_timer_sync(&mce_kbd->rx_timeout);
	input_unregister_device(idev);

	return 0;
}

static struct ir_raw_handler mce_kbd_handler = {
	.protocols	= RC_BIT_MCE_KBD,
	.decode		= ir_mce_kbd_decode,
	.raw_register	= ir_mce_kbd_register,
	.raw_unregister	= ir_mce_kbd_unregister,
};

static int __init ir_mce_kbd_decode_init(void)
{
	ir_raw_handler_register(&mce_kbd_handler);

	printk(KERN_INFO "IR MCE Keyboard/mouse protocol handler initialized\n");
	return 0;
}

static void __exit ir_mce_kbd_decode_exit(void)
{
	ir_raw_handler_unregister(&mce_kbd_handler);
}

module_init(ir_mce_kbd_decode_init);
module_exit(ir_mce_kbd_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_DESCRIPTION("MCE Keyboard/mouse IR protocol decoder");
