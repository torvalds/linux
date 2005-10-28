/*
 *  Keyboard driver for Sharp Spitz, Borzoi and Akita (SL-Cxx00 series)
 *
 *  Copyright (c) 2005 Richard Purdie
 *
 *  Based on corgikbd.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/irq.h>

#include <asm/arch/spitz.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>

#define KB_ROWS			7
#define KB_COLS			11
#define KB_ROWMASK(r)		(1 << (r))
#define SCANCODE(r,c)		(((r)<<4) + (c) + 1)
#define	NR_SCANCODES		((KB_ROWS<<4) + 1)

#define HINGE_SCAN_INTERVAL	(150) /* ms */

#define SPITZ_KEY_CALENDER	KEY_F1
#define SPITZ_KEY_ADDRESS	KEY_F2
#define SPITZ_KEY_FN		KEY_F3
#define SPITZ_KEY_CANCEL	KEY_F4
#define SPITZ_KEY_EXOK		KEY_F5
#define SPITZ_KEY_EXCANCEL	KEY_F6
#define SPITZ_KEY_EXJOGDOWN	KEY_F7
#define SPITZ_KEY_EXJOGUP	KEY_F8
#define SPITZ_KEY_JAP1		KEY_LEFTALT
#define SPITZ_KEY_JAP2		KEY_RIGHTCTRL
#define SPITZ_KEY_SYNC		KEY_F9
#define SPITZ_KEY_MAIL		KEY_F10
#define SPITZ_KEY_OK		KEY_F11
#define SPITZ_KEY_MENU		KEY_F12

static unsigned char spitzkbd_keycode[NR_SCANCODES] = {
	0,                                                                                                                /* 0 */
	KEY_LEFTCTRL, KEY_1, KEY_3, KEY_5, KEY_6, KEY_7, KEY_9, KEY_0, KEY_BACKSPACE, SPITZ_KEY_EXOK, SPITZ_KEY_EXCANCEL, 0, 0, 0, 0, 0,  /* 1-16 */
	0, KEY_2, KEY_4, KEY_R, KEY_Y, KEY_8, KEY_I, KEY_O, KEY_P, SPITZ_KEY_EXJOGDOWN, SPITZ_KEY_EXJOGUP, 0, 0, 0, 0, 0, /* 17-32 */
	KEY_TAB, KEY_Q, KEY_E, KEY_T, KEY_G, KEY_U, KEY_J, KEY_K, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 33-48 */
	SPITZ_KEY_CALENDER, KEY_W, KEY_S, KEY_F, KEY_V, KEY_H, KEY_M, KEY_L, 0, KEY_RIGHTSHIFT, 0, 0, 0, 0, 0, 0,         /* 49-64 */
	SPITZ_KEY_ADDRESS, KEY_A, KEY_D, KEY_C, KEY_B, KEY_N, KEY_DOT, 0, KEY_ENTER, KEY_LEFTSHIFT, 0, 0, 0, 0, 0, 0,	  /* 65-80 */
	SPITZ_KEY_MAIL, KEY_Z, KEY_X, KEY_MINUS, KEY_SPACE, KEY_COMMA, 0, KEY_UP, 0, 0, SPITZ_KEY_FN, 0, 0, 0, 0, 0,      /* 81-96 */
	KEY_SYSRQ, SPITZ_KEY_JAP1, SPITZ_KEY_JAP2, SPITZ_KEY_CANCEL, SPITZ_KEY_OK, SPITZ_KEY_MENU, KEY_LEFT, KEY_DOWN, KEY_RIGHT, 0, 0, 0, 0, 0, 0, 0  /* 97-112 */
};

static int spitz_strobes[] = {
	SPITZ_GPIO_KEY_STROBE0,
	SPITZ_GPIO_KEY_STROBE1,
	SPITZ_GPIO_KEY_STROBE2,
	SPITZ_GPIO_KEY_STROBE3,
	SPITZ_GPIO_KEY_STROBE4,
	SPITZ_GPIO_KEY_STROBE5,
	SPITZ_GPIO_KEY_STROBE6,
	SPITZ_GPIO_KEY_STROBE7,
	SPITZ_GPIO_KEY_STROBE8,
	SPITZ_GPIO_KEY_STROBE9,
	SPITZ_GPIO_KEY_STROBE10,
};

static int spitz_senses[] = {
	SPITZ_GPIO_KEY_SENSE0,
	SPITZ_GPIO_KEY_SENSE1,
	SPITZ_GPIO_KEY_SENSE2,
	SPITZ_GPIO_KEY_SENSE3,
	SPITZ_GPIO_KEY_SENSE4,
	SPITZ_GPIO_KEY_SENSE5,
	SPITZ_GPIO_KEY_SENSE6,
};

struct spitzkbd {
	unsigned char keycode[ARRAY_SIZE(spitzkbd_keycode)];
	struct input_dev *input;
	char phys[32];

	spinlock_t lock;
	struct timer_list timer;
	struct timer_list htimer;

	unsigned int suspended;
	unsigned long suspend_jiffies;
};

#define KB_DISCHARGE_DELAY	10
#define KB_ACTIVATE_DELAY	10

/* Helper functions for reading the keyboard matrix
 * Note: We should really be using pxa_gpio_mode to alter GPDR but it
 *       requires a function call per GPIO bit which is excessive
 *       when we need to access 11 bits at once, multiple times.
 * These functions must be called within local_irq_save()/local_irq_restore()
 * or similar.
 */
static inline void spitzkbd_discharge_all(void)
{
	/* STROBE All HiZ */
	GPCR0  =  SPITZ_GPIO_G0_STROBE_BIT;
	GPDR0 &= ~SPITZ_GPIO_G0_STROBE_BIT;
	GPCR1  =  SPITZ_GPIO_G1_STROBE_BIT;
	GPDR1 &= ~SPITZ_GPIO_G1_STROBE_BIT;
	GPCR2  =  SPITZ_GPIO_G2_STROBE_BIT;
	GPDR2 &= ~SPITZ_GPIO_G2_STROBE_BIT;
	GPCR3  =  SPITZ_GPIO_G3_STROBE_BIT;
	GPDR3 &= ~SPITZ_GPIO_G3_STROBE_BIT;
}

static inline void spitzkbd_activate_all(void)
{
	/* STROBE ALL -> High */
	GPSR0  =  SPITZ_GPIO_G0_STROBE_BIT;
	GPDR0 |=  SPITZ_GPIO_G0_STROBE_BIT;
	GPSR1  =  SPITZ_GPIO_G1_STROBE_BIT;
	GPDR1 |=  SPITZ_GPIO_G1_STROBE_BIT;
	GPSR2  =  SPITZ_GPIO_G2_STROBE_BIT;
	GPDR2 |=  SPITZ_GPIO_G2_STROBE_BIT;
	GPSR3  =  SPITZ_GPIO_G3_STROBE_BIT;
	GPDR3 |=  SPITZ_GPIO_G3_STROBE_BIT;

	udelay(KB_DISCHARGE_DELAY);

	/* Clear any interrupts we may have triggered when altering the GPIO lines */
	GEDR0 = SPITZ_GPIO_G0_SENSE_BIT;
	GEDR1 = SPITZ_GPIO_G1_SENSE_BIT;
	GEDR2 = SPITZ_GPIO_G2_SENSE_BIT;
	GEDR3 = SPITZ_GPIO_G3_SENSE_BIT;
}

static inline void spitzkbd_activate_col(int col)
{
	int gpio = spitz_strobes[col];
	GPDR0 &= ~SPITZ_GPIO_G0_STROBE_BIT;
	GPDR1 &= ~SPITZ_GPIO_G1_STROBE_BIT;
	GPDR2 &= ~SPITZ_GPIO_G2_STROBE_BIT;
	GPDR3 &= ~SPITZ_GPIO_G3_STROBE_BIT;
	GPSR(gpio) = GPIO_bit(gpio);
	GPDR(gpio) |= GPIO_bit(gpio);
}

static inline void spitzkbd_reset_col(int col)
{
	int gpio = spitz_strobes[col];
	GPDR0 &= ~SPITZ_GPIO_G0_STROBE_BIT;
	GPDR1 &= ~SPITZ_GPIO_G1_STROBE_BIT;
	GPDR2 &= ~SPITZ_GPIO_G2_STROBE_BIT;
	GPDR3 &= ~SPITZ_GPIO_G3_STROBE_BIT;
	GPCR(gpio) = GPIO_bit(gpio);
	GPDR(gpio) |= GPIO_bit(gpio);
}

static inline int spitzkbd_get_row_status(int col)
{
	return ((GPLR0 >> 12) & 0x01) | ((GPLR0 >> 16) & 0x02)
		| ((GPLR2 >> 25) & 0x04) | ((GPLR1 << 1) & 0x08)
		| ((GPLR1 >> 0) & 0x10) | ((GPLR1 >> 1) & 0x60);
}

/*
 * The spitz keyboard only generates interrupts when a key is pressed.
 * When a key is pressed, we enable a timer which then scans the
 * keyboard to detect when the key is released.
 */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void spitzkbd_scankeyboard(struct spitzkbd *spitzkbd_data, struct pt_regs *regs)
{
	unsigned int row, col, rowd;
	unsigned long flags;
	unsigned int num_pressed, pwrkey = ((GPLR(SPITZ_GPIO_ON_KEY) & GPIO_bit(SPITZ_GPIO_ON_KEY)) != 0);

	if (spitzkbd_data->suspended)
		return;

	spin_lock_irqsave(&spitzkbd_data->lock, flags);

	input_regs(spitzkbd_data->input, regs);

	num_pressed = 0;
	for (col = 0; col < KB_COLS; col++) {
		/*
		 * Discharge the output driver capacitatance
		 * in the keyboard matrix. (Yes it is significant..)
		 */

		spitzkbd_discharge_all();
		udelay(KB_DISCHARGE_DELAY);

		spitzkbd_activate_col(col);
		udelay(KB_ACTIVATE_DELAY);

		rowd = spitzkbd_get_row_status(col);
		for (row = 0; row < KB_ROWS; row++) {
			unsigned int scancode, pressed;

			scancode = SCANCODE(row, col);
			pressed = rowd & KB_ROWMASK(row);

			input_report_key(spitzkbd_data->input, spitzkbd_data->keycode[scancode], pressed);

			if (pressed)
				num_pressed++;
		}
		spitzkbd_reset_col(col);
	}

	spitzkbd_activate_all();

	input_report_key(spitzkbd_data->input, SPITZ_KEY_SYNC, (GPLR(SPITZ_GPIO_SYNC) & GPIO_bit(SPITZ_GPIO_SYNC)) != 0 );
	input_report_key(spitzkbd_data->input, KEY_SUSPEND, pwrkey);

	if (pwrkey && time_after(jiffies, spitzkbd_data->suspend_jiffies + msecs_to_jiffies(1000))) {
		input_event(spitzkbd_data->input, EV_PWR, KEY_SUSPEND, 1);
		spitzkbd_data->suspend_jiffies = jiffies;
	}

	input_sync(spitzkbd_data->input);

	/* if any keys are pressed, enable the timer */
	if (num_pressed)
		mod_timer(&spitzkbd_data->timer, jiffies + msecs_to_jiffies(100));

	spin_unlock_irqrestore(&spitzkbd_data->lock, flags);
}

/*
 * spitz keyboard interrupt handler.
 */
static irqreturn_t spitzkbd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct spitzkbd *spitzkbd_data = dev_id;

	if (!timer_pending(&spitzkbd_data->timer)) {
		/** wait chattering delay **/
		udelay(20);
		spitzkbd_scankeyboard(spitzkbd_data, regs);
	}

	return IRQ_HANDLED;
}

/*
 * spitz timer checking for released keys
 */
static void spitzkbd_timer_callback(unsigned long data)
{
	struct spitzkbd *spitzkbd_data = (struct spitzkbd *) data;

	spitzkbd_scankeyboard(spitzkbd_data, NULL);
}

/*
 * The hinge switches generate an interrupt.
 * We debounce the switches and pass them to the input system.
 */

static irqreturn_t spitzkbd_hinge_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct spitzkbd *spitzkbd_data = dev_id;

	if (!timer_pending(&spitzkbd_data->htimer))
		mod_timer(&spitzkbd_data->htimer, jiffies + msecs_to_jiffies(HINGE_SCAN_INTERVAL));

	return IRQ_HANDLED;
}

#define HINGE_STABLE_COUNT 2
static int sharpsl_hinge_state;
static int hinge_count;

static void spitzkbd_hinge_timer(unsigned long data)
{
	struct spitzkbd *spitzkbd_data = (struct spitzkbd *) data;
	unsigned long state;
	unsigned long flags;

	state = GPLR(SPITZ_GPIO_SWA) & (GPIO_bit(SPITZ_GPIO_SWA)|GPIO_bit(SPITZ_GPIO_SWB));
	if (state != sharpsl_hinge_state) {
		hinge_count = 0;
		sharpsl_hinge_state = state;
	} else if (hinge_count < HINGE_STABLE_COUNT) {
		hinge_count++;
	}

	if (hinge_count >= HINGE_STABLE_COUNT) {
		spin_lock_irqsave(&spitzkbd_data->lock, flags);

		input_report_switch(spitzkbd_data->input, SW_0, ((GPLR(SPITZ_GPIO_SWA) & GPIO_bit(SPITZ_GPIO_SWA)) != 0));
		input_report_switch(spitzkbd_data->input, SW_1, ((GPLR(SPITZ_GPIO_SWB) & GPIO_bit(SPITZ_GPIO_SWB)) != 0));
		input_sync(spitzkbd_data->input);

		spin_unlock_irqrestore(&spitzkbd_data->lock, flags);
	} else {
		mod_timer(&spitzkbd_data->htimer, jiffies + msecs_to_jiffies(HINGE_SCAN_INTERVAL));
	}
}

#ifdef CONFIG_PM
static int spitzkbd_suspend(struct device *dev, pm_message_t state)
{
	int i;
	struct spitzkbd *spitzkbd = dev_get_drvdata(dev);
	spitzkbd->suspended = 1;

	/* Set Strobe lines as inputs - *except* strobe line 0 leave this
	   enabled so we can detect a power button press for resume */
	for (i = 1; i < SPITZ_KEY_STROBE_NUM; i++)
		pxa_gpio_mode(spitz_strobes[i] | GPIO_IN);

	return 0;
}

static int spitzkbd_resume(struct device *dev)
{
	int i;
	struct spitzkbd *spitzkbd = dev_get_drvdata(dev);

	for (i = 0; i < SPITZ_KEY_STROBE_NUM; i++)
		pxa_gpio_mode(spitz_strobes[i] | GPIO_OUT | GPIO_DFLT_HIGH);

	/* Upon resume, ignore the suspend key for a short while */
	spitzkbd->suspend_jiffies = jiffies;
	spitzkbd->suspended = 0;

	return 0;
}
#else
#define spitzkbd_suspend	NULL
#define spitzkbd_resume		NULL
#endif

static int __init spitzkbd_probe(struct device *dev)
{
	struct spitzkbd *spitzkbd;
	struct input_dev *input_dev;
	int i;

	spitzkbd = kzalloc(sizeof(struct spitzkbd), GFP_KERNEL);
	if (!spitzkbd)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(spitzkbd);
		return -ENOMEM;
	}

	dev_set_drvdata(dev, spitzkbd);
	strcpy(spitzkbd->phys, "spitzkbd/input0");

	spin_lock_init(&spitzkbd->lock);

	/* Init Keyboard rescan timer */
	init_timer(&spitzkbd->timer);
	spitzkbd->timer.function = spitzkbd_timer_callback;
	spitzkbd->timer.data = (unsigned long) spitzkbd;

	/* Init Hinge Timer */
	init_timer(&spitzkbd->htimer);
	spitzkbd->htimer.function = spitzkbd_hinge_timer;
	spitzkbd->htimer.data = (unsigned long) spitzkbd;

	spitzkbd->suspend_jiffies = jiffies;

	spitzkbd->input = input_dev;

	input_dev->private = spitzkbd;
	input_dev->name = "Spitz Keyboard";
	input_dev->phys = spitzkbd->phys;
	input_dev->cdev.dev = dev;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP) | BIT(EV_PWR) | BIT(EV_SW);
	input_dev->keycode = spitzkbd->keycode;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(spitzkbd_keycode);

	memcpy(spitzkbd->keycode, spitzkbd_keycode, sizeof(spitzkbd->keycode));
	for (i = 0; i < ARRAY_SIZE(spitzkbd_keycode); i++)
		set_bit(spitzkbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);
	set_bit(SW_0, input_dev->swbit);
	set_bit(SW_1, input_dev->swbit);

	input_register_device(input_dev);

	mod_timer(&spitzkbd->htimer, jiffies + msecs_to_jiffies(HINGE_SCAN_INTERVAL));

	/* Setup sense interrupts - RisingEdge Detect, sense lines as inputs */
	for (i = 0; i < SPITZ_KEY_SENSE_NUM; i++) {
		pxa_gpio_mode(spitz_senses[i] | GPIO_IN);
		if (request_irq(IRQ_GPIO(spitz_senses[i]), spitzkbd_interrupt,
						SA_INTERRUPT, "Spitzkbd Sense", spitzkbd))
			printk(KERN_WARNING "spitzkbd: Can't get Sense IRQ: %d!\n", i);
		else
			set_irq_type(IRQ_GPIO(spitz_senses[i]),IRQT_RISING);
	}

	/* Set Strobe lines as outputs - set high */
	for (i = 0; i < SPITZ_KEY_STROBE_NUM; i++)
		pxa_gpio_mode(spitz_strobes[i] | GPIO_OUT | GPIO_DFLT_HIGH);

	pxa_gpio_mode(SPITZ_GPIO_SYNC | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_ON_KEY | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_SWA | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_SWB | GPIO_IN);

	request_irq(SPITZ_IRQ_GPIO_SYNC, spitzkbd_interrupt, SA_INTERRUPT, "Spitzkbd Sync", spitzkbd);
	request_irq(SPITZ_IRQ_GPIO_ON_KEY, spitzkbd_interrupt, SA_INTERRUPT, "Spitzkbd PwrOn", spitzkbd);
	request_irq(SPITZ_IRQ_GPIO_SWA, spitzkbd_hinge_isr, SA_INTERRUPT, "Spitzkbd SWA", spitzkbd);
	request_irq(SPITZ_IRQ_GPIO_SWB, spitzkbd_hinge_isr, SA_INTERRUPT, "Spitzkbd SWB", spitzkbd);

	set_irq_type(SPITZ_IRQ_GPIO_SYNC, IRQT_BOTHEDGE);
	set_irq_type(SPITZ_IRQ_GPIO_ON_KEY, IRQT_BOTHEDGE);
	set_irq_type(SPITZ_IRQ_GPIO_SWA, IRQT_BOTHEDGE);
	set_irq_type(SPITZ_IRQ_GPIO_SWB, IRQT_BOTHEDGE);

	printk(KERN_INFO "input: Spitz Keyboard Registered\n");

	return 0;
}

static int spitzkbd_remove(struct device *dev)
{
	int i;
	struct spitzkbd *spitzkbd = dev_get_drvdata(dev);

	for (i = 0; i < SPITZ_KEY_SENSE_NUM; i++)
		free_irq(IRQ_GPIO(spitz_senses[i]), spitzkbd);

	free_irq(SPITZ_IRQ_GPIO_SYNC, spitzkbd);
	free_irq(SPITZ_IRQ_GPIO_ON_KEY, spitzkbd);
	free_irq(SPITZ_IRQ_GPIO_SWA, spitzkbd);
	free_irq(SPITZ_IRQ_GPIO_SWB, spitzkbd);

	del_timer_sync(&spitzkbd->htimer);
	del_timer_sync(&spitzkbd->timer);

	input_unregister_device(spitzkbd->input);

	kfree(spitzkbd);

	return 0;
}

static struct device_driver spitzkbd_driver = {
	.name		= "spitz-keyboard",
	.bus		= &platform_bus_type,
	.probe		= spitzkbd_probe,
	.remove		= spitzkbd_remove,
	.suspend	= spitzkbd_suspend,
	.resume		= spitzkbd_resume,
};

static int __devinit spitzkbd_init(void)
{
	return driver_register(&spitzkbd_driver);
}

static void __exit spitzkbd_exit(void)
{
	driver_unregister(&spitzkbd_driver);
}

module_init(spitzkbd_init);
module_exit(spitzkbd_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Spitz Keyboard Driver");
MODULE_LICENSE("GPLv2");
