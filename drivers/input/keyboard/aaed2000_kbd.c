/*
 *  Keyboard driver for the AAED-2000 dev board
 *
 *  Copyright (c) 2006 Nicolas Bellido Y Ortega
 *
 *  Based on corgikbd.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/input-polldev.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/arch/hardware.h>
#include <asm/arch/aaed2000.h>

#define KB_ROWS			12
#define KB_COLS			8
#define KB_ROWMASK(r)		(1 << (r))
#define SCANCODE(r,c)		(((c) * KB_ROWS) + (r))
#define NR_SCANCODES		(KB_COLS * KB_ROWS)

#define SCAN_INTERVAL		(50) /* ms */
#define KB_ACTIVATE_DELAY	(20) /* us */

static unsigned char aaedkbd_keycode[NR_SCANCODES] = {
	KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, 0, KEY_SPACE, KEY_KP6, 0, KEY_KPDOT, 0, 0,
	KEY_K, KEY_M, KEY_O, KEY_DOT, KEY_SLASH, 0, KEY_F, 0, 0, 0, KEY_LEFTSHIFT, 0,
	KEY_I, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH, 0, 0, 0, 0, 0, KEY_RIGHTSHIFT, 0,
	KEY_8, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_ENTER, 0, 0, 0, 0, 0, 0, 0,
	KEY_J, KEY_H, KEY_B, KEY_KP8, KEY_KP4, 0, KEY_C, KEY_D, KEY_S, KEY_A, 0, KEY_CAPSLOCK,
	KEY_Y, KEY_U, KEY_N, KEY_T, 0, 0, KEY_R, KEY_E, KEY_W, KEY_Q, 0, KEY_TAB,
	KEY_7, KEY_6, KEY_G, 0, KEY_5, 0, KEY_4, KEY_3, KEY_2, KEY_1, 0, KEY_GRAVE,
	0, 0, KEY_COMMA, 0, KEY_KP2, 0, KEY_V, KEY_LEFTALT, KEY_X, KEY_Z, 0, KEY_LEFTCTRL
};

struct aaedkbd {
	unsigned char keycode[ARRAY_SIZE(aaedkbd_keycode)];
	struct input_polled_dev *poll_dev;
	int kbdscan_state[KB_COLS];
	int kbdscan_count[KB_COLS];
};

#define KBDSCAN_STABLE_COUNT 2

static void aaedkbd_report_col(struct aaedkbd *aaedkbd,
				unsigned int col, unsigned int rowd)
{
	unsigned int scancode, pressed;
	unsigned int row;

	for (row = 0; row < KB_ROWS; row++) {
		scancode = SCANCODE(row, col);
		pressed = rowd & KB_ROWMASK(row);

		input_report_key(aaedkbd->poll_dev->input,
				 aaedkbd->keycode[scancode], pressed);
	}
}

/* Scan the hardware keyboard and push any changes up through the input layer */
static void aaedkbd_poll(struct input_polled_dev *dev)
{
	struct aaedkbd *aaedkbd = dev->private;
	unsigned int col, rowd;

	col = 0;
	do {
		AAEC_GPIO_KSCAN = col + 8;
		udelay(KB_ACTIVATE_DELAY);
		rowd = AAED_EXT_GPIO & AAED_EGPIO_KBD_SCAN;

		if (rowd != aaedkbd->kbdscan_state[col]) {
			aaedkbd->kbdscan_count[col] = 0;
			aaedkbd->kbdscan_state[col] = rowd;
		} else if (++aaedkbd->kbdscan_count[col] >= KBDSCAN_STABLE_COUNT) {
			aaedkbd_report_col(aaedkbd, col, rowd);
			col++;
		}
	} while (col < KB_COLS);

	AAEC_GPIO_KSCAN = 0x07;
	input_sync(dev->input);
}

static int __devinit aaedkbd_probe(struct platform_device *pdev)
{
	struct aaedkbd *aaedkbd;
	struct input_polled_dev *poll_dev;
	struct input_dev *input_dev;
	int i;
	int error;

	aaedkbd = kzalloc(sizeof(struct aaedkbd), GFP_KERNEL);
	poll_dev = input_allocate_polled_device();
	if (!aaedkbd || !poll_dev) {
		error = -ENOMEM;
		goto fail;
	}

	platform_set_drvdata(pdev, aaedkbd);

	aaedkbd->poll_dev = poll_dev;
	memcpy(aaedkbd->keycode, aaedkbd_keycode, sizeof(aaedkbd->keycode));

	poll_dev->private = aaedkbd;
	poll_dev->poll = aaedkbd_poll;
	poll_dev->poll_interval = SCAN_INTERVAL;

	input_dev = poll_dev->input;
	input_dev->name = "AAED-2000 Keyboard";
	input_dev->phys = "aaedkbd/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->keycode = aaedkbd->keycode;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(aaedkbd_keycode);

	for (i = 0; i < ARRAY_SIZE(aaedkbd_keycode); i++)
		set_bit(aaedkbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	error = input_register_polled_device(aaedkbd->poll_dev);
	if (error)
		goto fail;

	return 0;

 fail:	kfree(aaedkbd);
	input_free_polled_device(poll_dev);
	return error;
}

static int __devexit aaedkbd_remove(struct platform_device *pdev)
{
	struct aaedkbd *aaedkbd = platform_get_drvdata(pdev);

	input_unregister_polled_device(aaedkbd->poll_dev);
	input_free_polled_device(aaedkbd->poll_dev);
	kfree(aaedkbd);

	return 0;
}

static struct platform_driver aaedkbd_driver = {
	.probe		= aaedkbd_probe,
	.remove		= __devexit_p(aaedkbd_remove),
	.driver		= {
		.name	= "aaed2000-keyboard",
	},
};

static int __init aaedkbd_init(void)
{
	return platform_driver_register(&aaedkbd_driver);
}

static void __exit aaedkbd_exit(void)
{
	platform_driver_unregister(&aaedkbd_driver);
}

module_init(aaedkbd_init);
module_exit(aaedkbd_exit);

MODULE_AUTHOR("Nicolas Bellido Y Ortega");
MODULE_DESCRIPTION("AAED-2000 Keyboard Driver");
MODULE_LICENSE("GPLv2");
