/*
 *  atakbd.c
 *
 *  Copyright (c) 2005 Michael Schmitz
 *
 * Based on amikbd.c, which is
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Hamish Macdonald
 */

/*
 * Atari keyboard driver for Linux/m68k
 *
 * The low level init and interrupt stuff is handled in arch/mm68k/atari/atakeyb.c
 * (the keyboard ACIA also handles the mouse and joystick data, and the keyboard
 * interrupt is shared with the MIDI ACIA so MIDI data also get handled there).
 * This driver only deals with handing key events off to the input layer.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/atariints.h>
#include <asm/atarihw.h>
#include <asm/atarikb.h>
#include <asm/irq.h>

MODULE_AUTHOR("Michael Schmitz <schmitz@biophys.uni-duesseldorf.de>");
MODULE_DESCRIPTION("Atari keyboard driver");
MODULE_LICENSE("GPL");

static unsigned char atakbd_keycode[0x72];

static struct input_dev *atakbd_dev;

static void atakbd_interrupt(unsigned char scancode, char down)
{

	if (scancode < 0x72) {		/* scancodes < 0xf2 are keys */

		// report raw events here?

		scancode = atakbd_keycode[scancode];

		if (scancode == KEY_CAPSLOCK) {	/* CapsLock is a toggle switch key on Amiga */
			input_report_key(atakbd_dev, scancode, 1);
			input_report_key(atakbd_dev, scancode, 0);
			input_sync(atakbd_dev);
		} else {
			input_report_key(atakbd_dev, scancode, down);
			input_sync(atakbd_dev);
		}
	} else				/* scancodes >= 0xf2 are mouse data, most likely */
		printk(KERN_INFO "atakbd: unhandled scancode %x\n", scancode);

	return;
}

static int __init atakbd_init(void)
{
	int i;

	if (!ATARIHW_PRESENT(ST_MFP))
		return -EIO;

	// TODO: request_mem_region if not done in arch code

	if (!(atakbd_dev = input_allocate_device()))
		return -ENOMEM;

	// need to init core driver if not already done so
	if (atari_keyb_init())
		return -ENODEV;

	atakbd_dev->name = "Atari Keyboard";
	atakbd_dev->phys = "atakbd/input0";
	atakbd_dev->id.bustype = BUS_ATARI;
	atakbd_dev->id.vendor = 0x0001;
	atakbd_dev->id.product = 0x0001;
	atakbd_dev->id.version = 0x0100;

	atakbd_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	atakbd_dev->keycode = atakbd_keycode;
	atakbd_dev->keycodesize = sizeof(unsigned char);
	atakbd_dev->keycodemax = ARRAY_SIZE(atakbd_keycode);

	for (i = 1; i < 0x72; i++) {
		atakbd_keycode[i] = i;
		set_bit(atakbd_keycode[i], atakbd_dev->keybit);
	}

	input_register_device(atakbd_dev);

	atari_input_keyboard_interrupt_hook = atakbd_interrupt;

	printk(KERN_INFO "input: %s at IKBD ACIA\n", atakbd_dev->name);

	return 0;
}

static void __exit atakbd_exit(void)
{
	atari_input_keyboard_interrupt_hook = NULL;
	input_unregister_device(atakbd_dev);
}

module_init(atakbd_init);
module_exit(atakbd_exit);
