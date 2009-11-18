/*
 *  Keyboard driver for Sharp Tosa models (SL-6000x)
 *
 *  Copyright (c) 2005 Dirk Opfer
 *  Copyright (c) 2007 Dmitry Baryshkov
 *
 *  Based on xtkbd.c/locomkbd.c/corgikbd.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <mach/gpio.h>
#include <mach/tosa.h>

#define KB_ROWMASK(r)		(1 << (r))
#define SCANCODE(r, c)		(((r)<<4) + (c) + 1)
#define NR_SCANCODES		SCANCODE(TOSA_KEY_SENSE_NUM - 1, TOSA_KEY_STROBE_NUM - 1) + 1

#define SCAN_INTERVAL		(HZ/10)

#define KB_DISCHARGE_DELAY	10
#define KB_ACTIVATE_DELAY	10

static unsigned short tosakbd_keycode[NR_SCANCODES] = {
0,
0, KEY_W, 0, 0, 0, KEY_K, KEY_BACKSPACE, KEY_P,
0, 0, 0, 0, 0, 0, 0, 0,
KEY_Q, KEY_E, KEY_T, KEY_Y, 0, KEY_O, KEY_I, KEY_COMMA,
0, 0, 0, 0, 0, 0, 0, 0,
KEY_A, KEY_D, KEY_G, KEY_U, 0, KEY_L, KEY_ENTER, KEY_DOT,
0, 0, 0, 0, 0, 0, 0, 0,
KEY_Z, KEY_C, KEY_V, KEY_J, TOSA_KEY_ADDRESSBOOK, TOSA_KEY_CANCEL, TOSA_KEY_CENTER, TOSA_KEY_OK,
KEY_LEFTSHIFT, 0, 0, 0, 0, 0, 0, 0,
KEY_S, KEY_R, KEY_B, KEY_N, TOSA_KEY_CALENDAR, TOSA_KEY_HOMEPAGE, KEY_LEFTCTRL, TOSA_KEY_LIGHT,
0, KEY_RIGHTSHIFT, 0, 0, 0, 0, 0, 0,
KEY_TAB, KEY_SLASH, KEY_H, KEY_M, TOSA_KEY_MENU, 0, KEY_UP, 0,
0, 0, TOSA_KEY_FN, 0, 0, 0, 0, 0,
KEY_X, KEY_F, KEY_SPACE, KEY_APOSTROPHE, TOSA_KEY_MAIL, KEY_LEFT, KEY_DOWN, KEY_RIGHT,
0, 0, 0,
};

struct tosakbd {
	unsigned short keycode[ARRAY_SIZE(tosakbd_keycode)];
	struct input_dev *input;
	bool suspended;
	spinlock_t lock; /* protect kbd scanning */
	struct timer_list timer;
};


/* Helper functions for reading the keyboard matrix
 * Note: We should really be using the generic gpio functions to alter
 *       GPDR but it requires a function call per GPIO bit which is
 *       excessive when we need to access 12 bits at once, multiple times.
 * These functions must be called within local_irq_save()/local_irq_restore()
 * or similar.
 */
#define GET_ROWS_STATUS(c)	((GPLR2 & TOSA_GPIO_ALL_SENSE_BIT) >> TOSA_GPIO_ALL_SENSE_RSHIFT)

static inline void tosakbd_discharge_all(void)
{
	/* STROBE All HiZ */
	GPCR1  = TOSA_GPIO_HIGH_STROBE_BIT;
	GPDR1 &= ~TOSA_GPIO_HIGH_STROBE_BIT;
	GPCR2  = TOSA_GPIO_LOW_STROBE_BIT;
	GPDR2 &= ~TOSA_GPIO_LOW_STROBE_BIT;
}

static inline void tosakbd_activate_all(void)
{
	/* STROBE ALL -> High */
	GPSR1  = TOSA_GPIO_HIGH_STROBE_BIT;
	GPDR1 |= TOSA_GPIO_HIGH_STROBE_BIT;
	GPSR2  = TOSA_GPIO_LOW_STROBE_BIT;
	GPDR2 |= TOSA_GPIO_LOW_STROBE_BIT;

	udelay(KB_DISCHARGE_DELAY);

	/* STATE CLEAR */
	GEDR2 |= TOSA_GPIO_ALL_SENSE_BIT;
}

static inline void tosakbd_activate_col(int col)
{
	if (col <= 5) {
		/* STROBE col -> High, not col -> HiZ */
		GPSR1 = TOSA_GPIO_STROBE_BIT(col);
		GPDR1 = (GPDR1 & ~TOSA_GPIO_HIGH_STROBE_BIT) | TOSA_GPIO_STROBE_BIT(col);
	} else {
		/* STROBE col -> High, not col -> HiZ */
		GPSR2 = TOSA_GPIO_STROBE_BIT(col);
		GPDR2 = (GPDR2 & ~TOSA_GPIO_LOW_STROBE_BIT) | TOSA_GPIO_STROBE_BIT(col);
	}
}

static inline void tosakbd_reset_col(int col)
{
	if (col <= 5) {
		/* STROBE col -> Low */
		GPCR1 = TOSA_GPIO_STROBE_BIT(col);
		/* STROBE col -> out, not col -> HiZ */
		GPDR1 = (GPDR1 & ~TOSA_GPIO_HIGH_STROBE_BIT) | TOSA_GPIO_STROBE_BIT(col);
	} else {
		/* STROBE col -> Low */
		GPCR2 = TOSA_GPIO_STROBE_BIT(col);
		/* STROBE col -> out, not col -> HiZ */
		GPDR2 = (GPDR2 & ~TOSA_GPIO_LOW_STROBE_BIT) | TOSA_GPIO_STROBE_BIT(col);
	}
}
/*
 * The tosa keyboard only generates interrupts when a key is pressed.
 * So when a key is pressed, we enable a timer.  This timer scans the
 * keyboard, and this is how we detect when the key is released.
 */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void tosakbd_scankeyboard(struct platform_device *dev)
{
	struct tosakbd *tosakbd = platform_get_drvdata(dev);
	unsigned int row, col, rowd;
	unsigned long flags;
	unsigned int num_pressed = 0;

	spin_lock_irqsave(&tosakbd->lock, flags);

	if (tosakbd->suspended)
		goto out;

	for (col = 0; col < TOSA_KEY_STROBE_NUM; col++) {
		/*
		 * Discharge the output driver capacitatance
		 * in the keyboard matrix. (Yes it is significant..)
		 */
		tosakbd_discharge_all();
		udelay(KB_DISCHARGE_DELAY);

		tosakbd_activate_col(col);
		udelay(KB_ACTIVATE_DELAY);

		rowd = GET_ROWS_STATUS(col);

		for (row = 0; row < TOSA_KEY_SENSE_NUM; row++) {
			unsigned int scancode, pressed;
			scancode = SCANCODE(row, col);
			pressed = rowd & KB_ROWMASK(row);

			if (pressed && !tosakbd->keycode[scancode])
				dev_warn(&dev->dev,
						"unhandled scancode: 0x%02x\n",
						scancode);

			input_report_key(tosakbd->input,
					tosakbd->keycode[scancode],
					pressed);
			if (pressed)
				num_pressed++;
		}

		tosakbd_reset_col(col);
	}

	tosakbd_activate_all();

	input_sync(tosakbd->input);

	/* if any keys are pressed, enable the timer */
	if (num_pressed)
		mod_timer(&tosakbd->timer, jiffies + SCAN_INTERVAL);

 out:
	spin_unlock_irqrestore(&tosakbd->lock, flags);
}

/*
 * tosa keyboard interrupt handler.
 */
static irqreturn_t tosakbd_interrupt(int irq, void *__dev)
{
	struct platform_device *dev = __dev;
	struct tosakbd *tosakbd = platform_get_drvdata(dev);

	if (!timer_pending(&tosakbd->timer)) {
		/** wait chattering delay **/
		udelay(20);
		tosakbd_scankeyboard(dev);
	}

	return IRQ_HANDLED;
}

/*
 * tosa timer checking for released keys
 */
static void tosakbd_timer_callback(unsigned long __dev)
{
	struct platform_device *dev = (struct platform_device *)__dev;

	tosakbd_scankeyboard(dev);
}

#ifdef CONFIG_PM
static int tosakbd_suspend(struct platform_device *dev, pm_message_t state)
{
	struct tosakbd *tosakbd = platform_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&tosakbd->lock, flags);
	tosakbd->suspended = true;
	spin_unlock_irqrestore(&tosakbd->lock, flags);

	del_timer_sync(&tosakbd->timer);

	return 0;
}

static int tosakbd_resume(struct platform_device *dev)
{
	struct tosakbd *tosakbd = platform_get_drvdata(dev);

	tosakbd->suspended = false;
	tosakbd_scankeyboard(dev);

	return 0;
}
#else
#define tosakbd_suspend		NULL
#define tosakbd_resume		NULL
#endif

static int __devinit tosakbd_probe(struct platform_device *pdev) {

	int i;
	struct tosakbd *tosakbd;
	struct input_dev *input_dev;
	int error;

	tosakbd = kzalloc(sizeof(struct tosakbd), GFP_KERNEL);
	if (!tosakbd)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(tosakbd);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, tosakbd);

	spin_lock_init(&tosakbd->lock);

	/* Init Keyboard rescan timer */
	init_timer(&tosakbd->timer);
	tosakbd->timer.function = tosakbd_timer_callback;
	tosakbd->timer.data = (unsigned long) pdev;

	tosakbd->input = input_dev;

	input_set_drvdata(input_dev, tosakbd);
	input_dev->name = "Tosa Keyboard";
	input_dev->phys = "tosakbd/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	input_dev->keycode = tosakbd->keycode;
	input_dev->keycodesize = sizeof(tosakbd->keycode[0]);
	input_dev->keycodemax = ARRAY_SIZE(tosakbd_keycode);

	memcpy(tosakbd->keycode, tosakbd_keycode, sizeof(tosakbd_keycode));

	for (i = 0; i < ARRAY_SIZE(tosakbd_keycode); i++)
		__set_bit(tosakbd->keycode[i], input_dev->keybit);
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	/* Setup sense interrupts - RisingEdge Detect, sense lines as inputs */
	for (i = 0; i < TOSA_KEY_SENSE_NUM; i++) {
		int gpio = TOSA_GPIO_KEY_SENSE(i);
		int irq;
		error = gpio_request(gpio, "tosakbd");
		if (error < 0) {
			printk(KERN_ERR "tosakbd: failed to request GPIO %d, "
				" error %d\n", gpio, error);
			goto fail;
		}

		error = gpio_direction_input(TOSA_GPIO_KEY_SENSE(i));
		if (error < 0) {
			printk(KERN_ERR "tosakbd: failed to configure input"
				" direction for GPIO %d, error %d\n",
				gpio, error);
			gpio_free(gpio);
			goto fail;
		}

		irq = gpio_to_irq(gpio);
		if (irq < 0) {
			error = irq;
			printk(KERN_ERR "gpio-keys: Unable to get irq number"
				" for GPIO %d, error %d\n",
				gpio, error);
			gpio_free(gpio);
			goto fail;
		}

		error = request_irq(irq, tosakbd_interrupt,
					IRQF_DISABLED | IRQF_TRIGGER_RISING,
					"tosakbd", pdev);

		if (error) {
			printk("tosakbd: Can't get IRQ: %d: error %d!\n",
					irq, error);
			gpio_free(gpio);
			goto fail;
		}
	}

	/* Set Strobe lines as outputs - set high */
	for (i = 0; i < TOSA_KEY_STROBE_NUM; i++) {
		int gpio = TOSA_GPIO_KEY_STROBE(i);
		error = gpio_request(gpio, "tosakbd");
		if (error < 0) {
			printk(KERN_ERR "tosakbd: failed to request GPIO %d, "
				" error %d\n", gpio, error);
			goto fail2;
		}

		error = gpio_direction_output(gpio, 1);
		if (error < 0) {
			printk(KERN_ERR "tosakbd: failed to configure input"
				" direction for GPIO %d, error %d\n",
				gpio, error);
			gpio_free(gpio);
			goto fail2;
		}

	}

	error = input_register_device(input_dev);
	if (error) {
		printk(KERN_ERR "tosakbd: Unable to register input device, "
			"error: %d\n", error);
		goto fail2;
	}

	printk(KERN_INFO "input: Tosa Keyboard Registered\n");

	return 0;

fail2:
	while (--i >= 0)
		gpio_free(TOSA_GPIO_KEY_STROBE(i));

	i = TOSA_KEY_SENSE_NUM;
fail:
	while (--i >= 0) {
		free_irq(gpio_to_irq(TOSA_GPIO_KEY_SENSE(i)), pdev);
		gpio_free(TOSA_GPIO_KEY_SENSE(i));
	}

	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
	kfree(tosakbd);

	return error;
}

static int __devexit tosakbd_remove(struct platform_device *dev)
{
	int i;
	struct tosakbd *tosakbd = platform_get_drvdata(dev);

	for (i = 0; i < TOSA_KEY_STROBE_NUM; i++)
		gpio_free(TOSA_GPIO_KEY_STROBE(i));

	for (i = 0; i < TOSA_KEY_SENSE_NUM; i++) {
		free_irq(gpio_to_irq(TOSA_GPIO_KEY_SENSE(i)), dev);
		gpio_free(TOSA_GPIO_KEY_SENSE(i));
	}

	del_timer_sync(&tosakbd->timer);

	input_unregister_device(tosakbd->input);

	kfree(tosakbd);

	return 0;
}

static struct platform_driver tosakbd_driver = {
	.probe		= tosakbd_probe,
	.remove		= __devexit_p(tosakbd_remove),
	.suspend	= tosakbd_suspend,
	.resume		= tosakbd_resume,
	.driver		= {
		.name	= "tosa-keyboard",
		.owner	= THIS_MODULE,
	},
};

static int __devinit tosakbd_init(void)
{
	return platform_driver_register(&tosakbd_driver);
}

static void __exit tosakbd_exit(void)
{
	platform_driver_unregister(&tosakbd_driver);
}

module_init(tosakbd_init);
module_exit(tosakbd_exit);

MODULE_AUTHOR("Dirk Opfer <Dirk@Opfer-Online.de>");
MODULE_DESCRIPTION("Tosa Keyboard Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tosa-keyboard");
