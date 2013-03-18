/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Hamish Macdonald
 */

/*
 * Amiga keyboard driver for Linux/m68k
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
#include <linux/keyboard.h>
#include <linux/platform_device.h>

#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/irq.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Amiga keyboard driver");
MODULE_LICENSE("GPL");

static unsigned char amikbd_keycode[0x78] __initdata = {
	[0]	 = KEY_GRAVE,
	[1]	 = KEY_1,
	[2]	 = KEY_2,
	[3]	 = KEY_3,
	[4]	 = KEY_4,
	[5]	 = KEY_5,
	[6]	 = KEY_6,
	[7]	 = KEY_7,
	[8]	 = KEY_8,
	[9]	 = KEY_9,
	[10]	 = KEY_0,
	[11]	 = KEY_MINUS,
	[12]	 = KEY_EQUAL,
	[13]	 = KEY_BACKSLASH,
	[15]	 = KEY_KP0,
	[16]	 = KEY_Q,
	[17]	 = KEY_W,
	[18]	 = KEY_E,
	[19]	 = KEY_R,
	[20]	 = KEY_T,
	[21]	 = KEY_Y,
	[22]	 = KEY_U,
	[23]	 = KEY_I,
	[24]	 = KEY_O,
	[25]	 = KEY_P,
	[26]	 = KEY_LEFTBRACE,
	[27]	 = KEY_RIGHTBRACE,
	[29]	 = KEY_KP1,
	[30]	 = KEY_KP2,
	[31]	 = KEY_KP3,
	[32]	 = KEY_A,
	[33]	 = KEY_S,
	[34]	 = KEY_D,
	[35]	 = KEY_F,
	[36]	 = KEY_G,
	[37]	 = KEY_H,
	[38]	 = KEY_J,
	[39]	 = KEY_K,
	[40]	 = KEY_L,
	[41]	 = KEY_SEMICOLON,
	[42]	 = KEY_APOSTROPHE,
	[43]	 = KEY_BACKSLASH,
	[45]	 = KEY_KP4,
	[46]	 = KEY_KP5,
	[47]	 = KEY_KP6,
	[48]	 = KEY_102ND,
	[49]	 = KEY_Z,
	[50]	 = KEY_X,
	[51]	 = KEY_C,
	[52]	 = KEY_V,
	[53]	 = KEY_B,
	[54]	 = KEY_N,
	[55]	 = KEY_M,
	[56]	 = KEY_COMMA,
	[57]	 = KEY_DOT,
	[58]	 = KEY_SLASH,
	[60]	 = KEY_KPDOT,
	[61]	 = KEY_KP7,
	[62]	 = KEY_KP8,
	[63]	 = KEY_KP9,
	[64]	 = KEY_SPACE,
	[65]	 = KEY_BACKSPACE,
	[66]	 = KEY_TAB,
	[67]	 = KEY_KPENTER,
	[68]	 = KEY_ENTER,
	[69]	 = KEY_ESC,
	[70]	 = KEY_DELETE,
	[74]	 = KEY_KPMINUS,
	[76]	 = KEY_UP,
	[77]	 = KEY_DOWN,
	[78]	 = KEY_RIGHT,
	[79]	 = KEY_LEFT,
	[80]	 = KEY_F1,
	[81]	 = KEY_F2,
	[82]	 = KEY_F3,
	[83]	 = KEY_F4,
	[84]	 = KEY_F5,
	[85]	 = KEY_F6,
	[86]	 = KEY_F7,
	[87]	 = KEY_F8,
	[88]	 = KEY_F9,
	[89]	 = KEY_F10,
	[90]	 = KEY_KPLEFTPAREN,
	[91]	 = KEY_KPRIGHTPAREN,
	[92]	 = KEY_KPSLASH,
	[93]	 = KEY_KPASTERISK,
	[94]	 = KEY_KPPLUS,
	[95]	 = KEY_HELP,
	[96]	 = KEY_LEFTSHIFT,
	[97]	 = KEY_RIGHTSHIFT,
	[98]	 = KEY_CAPSLOCK,
	[99]	 = KEY_LEFTCTRL,
	[100]	 = KEY_LEFTALT,
	[101]	 = KEY_RIGHTALT,
	[102]	 = KEY_LEFTMETA,
	[103]	 = KEY_RIGHTMETA
};

static const char *amikbd_messages[8] = {
	[0] = KERN_ALERT "amikbd: Ctrl-Amiga-Amiga reset warning!!\n",
	[1] = KERN_WARNING "amikbd: keyboard lost sync\n",
	[2] = KERN_WARNING "amikbd: keyboard buffer overflow\n",
	[3] = KERN_WARNING "amikbd: keyboard controller failure\n",
	[4] = KERN_ERR "amikbd: keyboard selftest failure\n",
	[5] = KERN_INFO "amikbd: initiate power-up key stream\n",
	[6] = KERN_INFO "amikbd: terminate power-up key stream\n",
	[7] = KERN_WARNING "amikbd: keyboard interrupt\n"
};

static irqreturn_t amikbd_interrupt(int irq, void *data)
{
	struct input_dev *dev = data;
	unsigned char scancode, down;

	scancode = ~ciaa.sdr;		/* get and invert scancode (keyboard is active low) */
	ciaa.cra |= 0x40;		/* switch SP pin to output for handshake */
	udelay(85);			/* wait until 85 us have expired */
	ciaa.cra &= ~0x40;		/* switch CIA serial port to input mode */

	down = !(scancode & 1);		/* lowest bit is release bit */
	scancode >>= 1;

	if (scancode < 0x78) {		/* scancodes < 0x78 are keys */
		if (scancode == 98) {	/* CapsLock is a toggle switch key on Amiga */
			input_report_key(dev, scancode, 1);
			input_report_key(dev, scancode, 0);
		} else {
			input_report_key(dev, scancode, down);
		}

		input_sync(dev);
	} else				/* scancodes >= 0x78 are error codes */
		printk(amikbd_messages[scancode - 0x78]);

	return IRQ_HANDLED;
}

static int __init amikbd_probe(struct platform_device *pdev)
{
	struct input_dev *dev;
	int i, j, err;

	dev = input_allocate_device();
	if (!dev) {
		dev_err(&pdev->dev, "Not enough memory for input device\n");
		return -ENOMEM;
	}

	dev->name = pdev->name;
	dev->phys = "amikbd/input0";
	dev->id.bustype = BUS_AMIGA;
	dev->id.vendor = 0x0001;
	dev->id.product = 0x0001;
	dev->id.version = 0x0100;
	dev->dev.parent = &pdev->dev;

	dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);

	for (i = 0; i < 0x78; i++)
		set_bit(i, dev->keybit);

	for (i = 0; i < MAX_NR_KEYMAPS; i++) {
		static u_short temp_map[NR_KEYS] __initdata;
		if (!key_maps[i])
			continue;
		memset(temp_map, 0, sizeof(temp_map));
		for (j = 0; j < 0x78; j++) {
			if (!amikbd_keycode[j])
				continue;
			temp_map[j] = key_maps[i][amikbd_keycode[j]];
		}
		for (j = 0; j < NR_KEYS; j++) {
			if (!temp_map[j])
				temp_map[j] = 0xf200;
		}
		memcpy(key_maps[i], temp_map, sizeof(temp_map));
	}
	ciaa.cra &= ~0x41;	 /* serial data in, turn off TA */
	err = request_irq(IRQ_AMIGA_CIAA_SP, amikbd_interrupt, 0, "amikbd",
			  dev);
	if (err)
		goto fail2;

	err = input_register_device(dev);
	if (err)
		goto fail3;

	platform_set_drvdata(pdev, dev);

	return 0;

 fail3:	free_irq(IRQ_AMIGA_CIAA_SP, dev);
 fail2:	input_free_device(dev);
	return err;
}

static int __exit amikbd_remove(struct platform_device *pdev)
{
	struct input_dev *dev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	free_irq(IRQ_AMIGA_CIAA_SP, dev);
	input_unregister_device(dev);
	return 0;
}

static struct platform_driver amikbd_driver = {
	.remove = __exit_p(amikbd_remove),
	.driver   = {
		.name	= "amiga-keyboard",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver_probe(amikbd_driver, amikbd_probe);

MODULE_ALIAS("platform:amiga-keyboard");
