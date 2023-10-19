// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LoCoMo keyboard driver for Linux-based ARM PDAs:
 * 	- SHARP Zaurus Collie (SL-5500)
 * 	- SHARP Zaurus Poodle (SL-5600)
 *
 * Copyright (c) 2005 John Lenz
 * Based on from xtkbd.c
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/hardware/locomo.h>
#include <asm/irq.h>

MODULE_AUTHOR("John Lenz <lenz@cs.wisc.edu>");
MODULE_DESCRIPTION("LoCoMo keyboard driver");
MODULE_LICENSE("GPL");

#define LOCOMOKBD_NUMKEYS	128

#define KEY_ACTIVITY		KEY_F16
#define KEY_CONTACT		KEY_F18
#define KEY_CENTER		KEY_F15

static const unsigned char
locomokbd_keycode[LOCOMOKBD_NUMKEYS] = {
	0, KEY_ESC, KEY_ACTIVITY, 0, 0, 0, 0, 0, 0, 0,				/* 0 - 9 */
	0, 0, 0, 0, 0, 0, 0, KEY_MENU, KEY_HOME, KEY_CONTACT,			/* 10 - 19 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,						/* 20 - 29 */
	0, 0, 0, KEY_CENTER, 0, KEY_MAIL, 0, 0, 0, 0,				/* 30 - 39 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RIGHT,					/* 40 - 49 */
	KEY_UP, KEY_LEFT, 0, 0, KEY_P, 0, KEY_O, KEY_I, KEY_Y, KEY_T,		/* 50 - 59 */
	KEY_E, KEY_W, 0, 0, 0, 0, KEY_DOWN, KEY_ENTER, 0, 0,			/* 60 - 69 */
	KEY_BACKSPACE, 0, KEY_L, KEY_U, KEY_H, KEY_R, KEY_D, KEY_Q, 0, 0,	/* 70 - 79 */
	0, 0, 0, 0, 0, 0, KEY_ENTER, KEY_RIGHTSHIFT, KEY_K, KEY_J,		/* 80 - 89 */
	KEY_G, KEY_F, KEY_X, KEY_S, 0, 0, 0, 0, 0, 0,				/* 90 - 99 */
	0, 0, KEY_DOT, 0, KEY_COMMA, KEY_N, KEY_B, KEY_C, KEY_Z, KEY_A,		/* 100 - 109 */
	KEY_LEFTSHIFT, KEY_TAB, KEY_LEFTCTRL, 0, 0, 0, 0, 0, 0, 0,		/* 110 - 119 */
	KEY_M, KEY_SPACE, KEY_V, KEY_APOSTROPHE, KEY_SLASH, 0, 0, 0		/* 120 - 128 */
};

#define KB_ROWS			16
#define KB_COLS			8
#define KB_ROWMASK(r)		(1 << (r))
#define SCANCODE(c,r)		( ((c)<<4) + (r) + 1 )

#define KB_DELAY		8
#define SCAN_INTERVAL		(HZ/10)

struct locomokbd {
	unsigned char keycode[LOCOMOKBD_NUMKEYS];
	struct input_dev *input;
	char phys[32];

	unsigned long base;
	spinlock_t lock;

	struct timer_list timer;
	unsigned long suspend_jiffies;
	unsigned int count_cancel;
};

/* helper functions for reading the keyboard matrix */
static inline void locomokbd_charge_all(unsigned long membase)
{
	locomo_writel(0x00FF, membase + LOCOMO_KSC);
}

static inline void locomokbd_activate_all(unsigned long membase)
{
	unsigned long r;

	locomo_writel(0, membase + LOCOMO_KSC);
	r = locomo_readl(membase + LOCOMO_KIC);
	r &= 0xFEFF;
	locomo_writel(r, membase + LOCOMO_KIC);
}

static inline void locomokbd_activate_col(unsigned long membase, int col)
{
	unsigned short nset;
	unsigned short nbset;

	nset = 0xFF & ~(1 << col);
	nbset = (nset << 8) + nset;
	locomo_writel(nbset, membase + LOCOMO_KSC);
}

static inline void locomokbd_reset_col(unsigned long membase, int col)
{
	unsigned short nbset;

	nbset = ((0xFF & ~(1 << col)) << 8) + 0xFF;
	locomo_writel(nbset, membase + LOCOMO_KSC);
}

/*
 * The LoCoMo keyboard only generates interrupts when a key is pressed.
 * So when a key is pressed, we enable a timer.  This timer scans the
 * keyboard, and this is how we detect when the key is released.
 */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void locomokbd_scankeyboard(struct locomokbd *locomokbd)
{
	unsigned int row, col, rowd;
	unsigned long flags;
	unsigned int num_pressed;
	unsigned long membase = locomokbd->base;

	spin_lock_irqsave(&locomokbd->lock, flags);

	locomokbd_charge_all(membase);

	num_pressed = 0;
	for (col = 0; col < KB_COLS; col++) {

		locomokbd_activate_col(membase, col);
		udelay(KB_DELAY);

		rowd = ~locomo_readl(membase + LOCOMO_KIB);
		for (row = 0; row < KB_ROWS; row++) {
			unsigned int scancode, pressed, key;

			scancode = SCANCODE(col, row);
			pressed = rowd & KB_ROWMASK(row);
			key = locomokbd->keycode[scancode];

			input_report_key(locomokbd->input, key, pressed);
			if (likely(!pressed))
				continue;

			num_pressed++;

			/* The "Cancel/ESC" key is labeled "On/Off" on
			 * Collie and Poodle and should suspend the device
			 * if it was pressed for more than a second. */
			if (unlikely(key == KEY_ESC)) {
				if (!time_after(jiffies,
					locomokbd->suspend_jiffies + HZ))
					continue;
				if (locomokbd->count_cancel++
					!= (HZ/SCAN_INTERVAL + 1))
					continue;
				input_event(locomokbd->input, EV_PWR,
					KEY_SUSPEND, 1);
				locomokbd->suspend_jiffies = jiffies;
			} else
				locomokbd->count_cancel = 0;
		}
		locomokbd_reset_col(membase, col);
	}
	locomokbd_activate_all(membase);

	input_sync(locomokbd->input);

	/* if any keys are pressed, enable the timer */
	if (num_pressed)
		mod_timer(&locomokbd->timer, jiffies + SCAN_INTERVAL);
	else
		locomokbd->count_cancel = 0;

	spin_unlock_irqrestore(&locomokbd->lock, flags);
}

/*
 * LoCoMo keyboard interrupt handler.
 */
static irqreturn_t locomokbd_interrupt(int irq, void *dev_id)
{
	struct locomokbd *locomokbd = dev_id;
	u16 r;

	r = locomo_readl(locomokbd->base + LOCOMO_KIC);
	if ((r & 0x0001) == 0)
		return IRQ_HANDLED;

	locomo_writel(r & ~0x0100, locomokbd->base + LOCOMO_KIC); /* Ack */

	/** wait chattering delay **/
	udelay(100);

	locomokbd_scankeyboard(locomokbd);
	return IRQ_HANDLED;
}

/*
 * LoCoMo timer checking for released keys
 */
static void locomokbd_timer_callback(struct timer_list *t)
{
	struct locomokbd *locomokbd = from_timer(locomokbd, t, timer);

	locomokbd_scankeyboard(locomokbd);
}

static int locomokbd_open(struct input_dev *dev)
{
	struct locomokbd *locomokbd = input_get_drvdata(dev);
	u16 r;
	
	r = locomo_readl(locomokbd->base + LOCOMO_KIC) | 0x0010;
	locomo_writel(r, locomokbd->base + LOCOMO_KIC);
	return 0;
}

static void locomokbd_close(struct input_dev *dev)
{
	struct locomokbd *locomokbd = input_get_drvdata(dev);
	u16 r;
	
	r = locomo_readl(locomokbd->base + LOCOMO_KIC) & ~0x0010;
	locomo_writel(r, locomokbd->base + LOCOMO_KIC);
}

static int locomokbd_probe(struct locomo_dev *dev)
{
	struct locomokbd *locomokbd;
	struct input_dev *input_dev;
	int i, err;

	locomokbd = kzalloc(sizeof(struct locomokbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!locomokbd || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	/* try and claim memory region */
	if (!request_mem_region((unsigned long) dev->mapbase,
				dev->length,
				LOCOMO_DRIVER_NAME(dev))) {
		err = -EBUSY;
		printk(KERN_ERR "locomokbd: Can't acquire access to io memory for keyboard\n");
		goto err_free_mem;
	}

	locomo_set_drvdata(dev, locomokbd);

	locomokbd->base = (unsigned long) dev->mapbase;

	spin_lock_init(&locomokbd->lock);

	timer_setup(&locomokbd->timer, locomokbd_timer_callback, 0);

	locomokbd->suspend_jiffies = jiffies;

	locomokbd->input = input_dev;
	strcpy(locomokbd->phys, "locomokbd/input0");

	input_dev->name = "LoCoMo keyboard";
	input_dev->phys = locomokbd->phys;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->open = locomokbd_open;
	input_dev->close = locomokbd_close;
	input_dev->dev.parent = &dev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) |
				BIT_MASK(EV_PWR);
	input_dev->keycode = locomokbd->keycode;
	input_dev->keycodesize = sizeof(locomokbd_keycode[0]);
	input_dev->keycodemax = ARRAY_SIZE(locomokbd_keycode);

	input_set_drvdata(input_dev, locomokbd);

	memcpy(locomokbd->keycode, locomokbd_keycode, sizeof(locomokbd->keycode));
	for (i = 0; i < LOCOMOKBD_NUMKEYS; i++)
		set_bit(locomokbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	/* attempt to get the interrupt */
	err = request_irq(dev->irq[0], locomokbd_interrupt, 0, "locomokbd", locomokbd);
	if (err) {
		printk(KERN_ERR "locomokbd: Can't get irq for keyboard\n");
		goto err_release_region;
	}

	err = input_register_device(locomokbd->input);
	if (err)
		goto err_free_irq;

	return 0;

 err_free_irq:
	free_irq(dev->irq[0], locomokbd);
 err_release_region:
	release_mem_region((unsigned long) dev->mapbase, dev->length);
	locomo_set_drvdata(dev, NULL);
 err_free_mem:
	input_free_device(input_dev);
	kfree(locomokbd);

	return err;
}

static void locomokbd_remove(struct locomo_dev *dev)
{
	struct locomokbd *locomokbd = locomo_get_drvdata(dev);

	free_irq(dev->irq[0], locomokbd);

	del_timer_sync(&locomokbd->timer);

	input_unregister_device(locomokbd->input);
	locomo_set_drvdata(dev, NULL);

	release_mem_region((unsigned long) dev->mapbase, dev->length);

	kfree(locomokbd);
}

static struct locomo_driver keyboard_driver = {
	.drv = {
		.name = "locomokbd"
	},
	.devid	= LOCOMO_DEVID_KEYBOARD,
	.probe	= locomokbd_probe,
	.remove	= locomokbd_remove,
};

static int __init locomokbd_init(void)
{
	return locomo_driver_register(&keyboard_driver);
}

static void __exit locomokbd_exit(void)
{
	locomo_driver_unregister(&keyboard_driver);
}

module_init(locomokbd_init);
module_exit(locomokbd_exit);
