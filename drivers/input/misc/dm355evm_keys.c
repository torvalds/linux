/*
 * dm355evm_keys.c - support buttons and IR remote on DM355 EVM board
 *
 * Copyright (c) 2008 by David Brownell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <linux/i2c/dm355evm_msp.h>


/*
 * The MSP430 firmware on the DM355 EVM monitors on-board pushbuttons
 * and an IR receptor used for the remote control.  When any key is
 * pressed, or its autorepeat kicks in, an event is sent.  This driver
 * read those events from the small (32 event) queue and reports them.
 *
 * Note that physically there can only be one of these devices.
 *
 * This driver was tested with firmware revision A4.
 */
struct dm355evm_keys {
	struct input_dev	*input;
	struct device		*dev;
	int			irq;
};

/* These initial keycodes can be remapped by dm355evm_setkeycode(). */
static struct {
	u16	event;
	u16	keycode;
} dm355evm_keys[] = {

	/*
	 * Pushbuttons on the EVM board ... note that the labels for these
	 * are SW10/SW11/etc on the PC board.  The left/right orientation
	 * comes only from the firmware's documentation, and presumes the
	 * power connector is immediately in front of you and the IR sensor
	 * is to the right.  (That is, rotate the board counter-clockwise
	 * by 90 degrees from the SW10/etc and "DM355 EVM" labels.)
	 */
	{ 0x00d8, KEY_OK, },		/* SW12 */
	{ 0x00b8, KEY_UP, },		/* SW13 */
	{ 0x00e8, KEY_DOWN, },		/* SW11 */
	{ 0x0078, KEY_LEFT, },		/* SW14 */
	{ 0x00f0, KEY_RIGHT, },		/* SW10 */

	/*
	 * IR buttons ... codes assigned to match the universal remote
	 * provided with the EVM (Philips PM4S) using DVD code 0020.
	 *
	 * These event codes match firmware documentation, but other
	 * remote controls could easily send more RC5-encoded events.
	 * The PM4S manual was used in several cases to help select
	 * a keycode reflecting the intended usage.
	 *
	 * RC5 codes are 14 bits, with two start bits (0x3 prefix)
	 * and a toggle bit (masked out below).
	 */
	{ 0x300c, KEY_POWER, },		/* NOTE: docs omit this */
	{ 0x3000, KEY_NUMERIC_0, },
	{ 0x3001, KEY_NUMERIC_1, },
	{ 0x3002, KEY_NUMERIC_2, },
	{ 0x3003, KEY_NUMERIC_3, },
	{ 0x3004, KEY_NUMERIC_4, },
	{ 0x3005, KEY_NUMERIC_5, },
	{ 0x3006, KEY_NUMERIC_6, },
	{ 0x3007, KEY_NUMERIC_7, },
	{ 0x3008, KEY_NUMERIC_8, },
	{ 0x3009, KEY_NUMERIC_9, },
	{ 0x3022, KEY_ENTER, },
	{ 0x30ec, KEY_MODE, },		/* "tv/vcr/..." */
	{ 0x300f, KEY_SELECT, },	/* "info" */
	{ 0x3020, KEY_CHANNELUP, },	/* "up" */
	{ 0x302e, KEY_MENU, },		/* "in/out" */
	{ 0x3011, KEY_VOLUMEDOWN, },	/* "left" */
	{ 0x300d, KEY_MUTE, },		/* "ok" */
	{ 0x3010, KEY_VOLUMEUP, },	/* "right" */
	{ 0x301e, KEY_SUBTITLE, },	/* "cc" */
	{ 0x3021, KEY_CHANNELDOWN, },	/* "down" */
	{ 0x3022, KEY_PREVIOUS, },
	{ 0x3026, KEY_SLEEP, },
	{ 0x3172, KEY_REWIND, },	/* NOTE: docs wrongly say 0x30ca */
	{ 0x3175, KEY_PLAY, },
	{ 0x3174, KEY_FASTFORWARD, },
	{ 0x3177, KEY_RECORD, },
	{ 0x3176, KEY_STOP, },
	{ 0x3169, KEY_PAUSE, },
};

/*
 * Because we communicate with the MSP430 using I2C, and all I2C calls
 * in Linux sleep, we use a threaded IRQ handler.  The IRQ itself is
 * active low, but we go through the GPIO controller so we can trigger
 * on falling edges and not worry about enabling/disabling the IRQ in
 * the keypress handling path.
 */
static irqreturn_t dm355evm_keys_irq(int irq, void *_keys)
{
	struct dm355evm_keys	*keys = _keys;
	int			status;

	/* For simplicity we ignore INPUT_COUNT and just read
	 * events until we get the "queue empty" indicator.
	 * Reading INPUT_LOW decrements the count.
	 */
	for (;;) {
		static u16	last_event;
		u16		event;
		int		keycode;
		int		i;

		status = dm355evm_msp_read(DM355EVM_MSP_INPUT_HIGH);
		if (status < 0) {
			dev_dbg(keys->dev, "input high err %d\n",
					status);
			break;
		}
		event = status << 8;

		status = dm355evm_msp_read(DM355EVM_MSP_INPUT_LOW);
		if (status < 0) {
			dev_dbg(keys->dev, "input low err %d\n",
					status);
			break;
		}
		event |= status;
		if (event == 0xdead)
			break;

		/* Press and release a button:  two events, same code.
		 * Press and hold (autorepeat), then release: N events
		 * (N > 2), same code.  For RC5 buttons the toggle bits
		 * distinguish (for example) "1-autorepeat" from "1 1";
		 * but PCB buttons don't support that bit.
		 *
		 * So we must synthesize release events.  We do that by
		 * mapping events to a press/release event pair; then
		 * to avoid adding extra events, skip the second event
		 * of each pair.
		 */
		if (event == last_event) {
			last_event = 0;
			continue;
		}
		last_event = event;

		/* ignore the RC5 toggle bit */
		event &= ~0x0800;

		/* find the key, or leave it as unknown */
		keycode = KEY_UNKNOWN;
		for (i = 0; i < ARRAY_SIZE(dm355evm_keys); i++) {
			if (dm355evm_keys[i].event != event)
				continue;
			keycode = dm355evm_keys[i].keycode;
			break;
		}
		dev_dbg(keys->dev,
			"input event 0x%04x--> keycode %d\n",
			event, keycode);

		/* report press + release */
		input_report_key(keys->input, keycode, 1);
		input_sync(keys->input);
		input_report_key(keys->input, keycode, 0);
		input_sync(keys->input);
	}
	return IRQ_HANDLED;
}

static int dm355evm_setkeycode(struct input_dev *dev, int index, int keycode)
{
	u16		old_keycode;
	unsigned	i;

	if (((unsigned)index) >= ARRAY_SIZE(dm355evm_keys))
		return -EINVAL;

	old_keycode = dm355evm_keys[index].keycode;
	dm355evm_keys[index].keycode = keycode;
	set_bit(keycode, dev->keybit);

	for (i = 0; i < ARRAY_SIZE(dm355evm_keys); i++) {
		if (dm355evm_keys[index].keycode == old_keycode)
			goto done;
	}
	clear_bit(old_keycode, dev->keybit);
done:
	return 0;
}

static int dm355evm_getkeycode(struct input_dev *dev, int index, int *keycode)
{
	if (((unsigned)index) >= ARRAY_SIZE(dm355evm_keys))
		return -EINVAL;

	return dm355evm_keys[index].keycode;
}

/*----------------------------------------------------------------------*/

static int __devinit dm355evm_keys_probe(struct platform_device *pdev)
{
	struct dm355evm_keys	*keys;
	struct input_dev	*input;
	int			status;
	int			i;

	/* allocate instance struct and input dev */
	keys = kzalloc(sizeof *keys, GFP_KERNEL);
	input = input_allocate_device();
	if (!keys || !input) {
		status = -ENOMEM;
		goto fail1;
	}

	keys->dev = &pdev->dev;
	keys->input = input;

	/* set up "threaded IRQ handler" */
	status = platform_get_irq(pdev, 0);
	if (status < 0)
		goto fail1;
	keys->irq = status;

	input_set_drvdata(input, keys);

	input->name = "DM355 EVM Controls";
	input->phys = "dm355evm/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_I2C;
	input->id.product = 0x0355;
	input->id.version = dm355evm_msp_read(DM355EVM_MSP_FIRMREV);

	input->evbit[0] = BIT(EV_KEY);
	for (i = 0; i < ARRAY_SIZE(dm355evm_keys); i++)
		__set_bit(dm355evm_keys[i].keycode, input->keybit);

	input->setkeycode = dm355evm_setkeycode;
	input->getkeycode = dm355evm_getkeycode;

	/* REVISIT:  flush the event queue? */

	status = request_threaded_irq(keys->irq, NULL, dm355evm_keys_irq,
			IRQF_TRIGGER_FALLING, dev_name(&pdev->dev), keys);
	if (status < 0)
		goto fail1;

	/* register */
	status = input_register_device(input);
	if (status < 0)
		goto fail2;

	platform_set_drvdata(pdev, keys);

	return 0;

fail2:
	free_irq(keys->irq, keys);
fail1:
	input_free_device(input);
	kfree(keys);
	dev_err(&pdev->dev, "can't register, err %d\n", status);

	return status;
}

static int __devexit dm355evm_keys_remove(struct platform_device *pdev)
{
	struct dm355evm_keys	*keys = platform_get_drvdata(pdev);

	free_irq(keys->irq, keys);
	input_unregister_device(keys->input);
	kfree(keys);

	return 0;
}

/* REVISIT:  add suspend/resume when DaVinci supports it.  The IRQ should
 * be able to wake up the system.  When device_may_wakeup(&pdev->dev), call
 * enable_irq_wake() on suspend, and disable_irq_wake() on resume.
 */

/*
 * I2C is used to talk to the MSP430, but this platform device is
 * exposed by an MFD driver that manages I2C communications.
 */
static struct platform_driver dm355evm_keys_driver = {
	.probe		= dm355evm_keys_probe,
	.remove		= __devexit_p(dm355evm_keys_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "dm355evm_keys",
	},
};

static int __init dm355evm_keys_init(void)
{
	return platform_driver_register(&dm355evm_keys_driver);
}
module_init(dm355evm_keys_init);

static void __exit dm355evm_keys_exit(void)
{
	platform_driver_unregister(&dm355evm_keys_driver);
}
module_exit(dm355evm_keys_exit);

MODULE_LICENSE("GPL");
