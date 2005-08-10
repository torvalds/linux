/*
 *  Keyboard driver for Sharp Corgi models (SL-C7xx)
 *
 *  Copyright (c) 2004-2005 Richard Purdie
 *
 *  Based on xtkbd.c/locomkbd.c
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
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/irq.h>

#include <asm/arch/corgi.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/hardware/scoop.h>

#define KB_ROWS				8
#define KB_COLS				12
#define KB_ROWMASK(r)		(1 << (r))
#define SCANCODE(r,c)		( ((r)<<4) + (c) + 1 )
/* zero code, 124 scancodes + 3 hinge combinations */
#define	NR_SCANCODES		( SCANCODE(KB_ROWS-1,KB_COLS-1) +1 +1 +3 )
#define SCAN_INTERVAL		(HZ/10)
#define CORGIKBD_PRESSED	1

#define HINGE_SCAN_INTERVAL		(HZ/4)

#define CORGI_KEY_CALENDER	KEY_F1
#define CORGI_KEY_ADDRESS	KEY_F2
#define CORGI_KEY_FN		KEY_F3
#define CORGI_KEY_CANCEL	KEY_F4
#define CORGI_KEY_OFF		KEY_SUSPEND
#define CORGI_KEY_EXOK		KEY_F5
#define CORGI_KEY_EXCANCEL	KEY_F6
#define CORGI_KEY_EXJOGDOWN	KEY_F7
#define CORGI_KEY_EXJOGUP	KEY_F8
#define CORGI_KEY_JAP1		KEY_LEFTCTRL
#define CORGI_KEY_JAP2		KEY_LEFTALT
#define CORGI_KEY_MAIL		KEY_F10
#define CORGI_KEY_OK		KEY_F11
#define CORGI_KEY_MENU		KEY_F12
#define CORGI_HINGE_0		KEY_KP0
#define CORGI_HINGE_1		KEY_KP1
#define CORGI_HINGE_2		KEY_KP2

static unsigned char corgikbd_keycode[NR_SCANCODES] = {
	0,                                                                                                                /* 0 */
	0, KEY_1, KEY_3, KEY_5, KEY_6, KEY_7, KEY_9, KEY_0, KEY_BACKSPACE, 0, 0, 0, 0, 0, 0, 0, 	                      /* 1-16 */
	0, KEY_2, KEY_4, KEY_R, KEY_Y, KEY_8, KEY_I, KEY_O, KEY_P, 0, 0, 0, 0, 0, 0, 0,                                   /* 17-32 */
	KEY_TAB, KEY_Q, KEY_E, KEY_T, KEY_G, KEY_U, KEY_J, KEY_K, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 33-48 */
	CORGI_KEY_CALENDER, KEY_W, KEY_S, KEY_F, KEY_V, KEY_H, KEY_M, KEY_L, 0, KEY_RIGHTSHIFT, 0, 0, 0, 0, 0, 0,         /* 49-64 */
	CORGI_KEY_ADDRESS, KEY_A, KEY_D, KEY_C, KEY_B, KEY_N, KEY_DOT, 0, KEY_ENTER, 0, KEY_LEFTSHIFT, 0, 0, 0, 0, 0, 	  /* 65-80 */
	CORGI_KEY_MAIL, KEY_Z, KEY_X, KEY_MINUS, KEY_SPACE, KEY_COMMA, 0, KEY_UP, 0, 0, 0, CORGI_KEY_FN, 0, 0, 0, 0,            /* 81-96 */
	KEY_SYSRQ, CORGI_KEY_JAP1, CORGI_KEY_JAP2, CORGI_KEY_CANCEL, CORGI_KEY_OK, CORGI_KEY_MENU, KEY_LEFT, KEY_DOWN, KEY_RIGHT, 0, 0, 0, 0, 0, 0, 0,  /* 97-112 */
	CORGI_KEY_OFF, CORGI_KEY_EXOK, CORGI_KEY_EXCANCEL, CORGI_KEY_EXJOGDOWN, CORGI_KEY_EXJOGUP, 0, 0, 0, 0, 0, 0, 0,   /* 113-124 */
	CORGI_HINGE_0, CORGI_HINGE_1, CORGI_HINGE_2	  /* 125-127 */
};


struct corgikbd {
	unsigned char keycode[ARRAY_SIZE(corgikbd_keycode)];
	struct input_dev input;
	char phys[32];

	unsigned char state[ARRAY_SIZE(corgikbd_keycode)];
	spinlock_t lock;

	struct timer_list timer;
	struct timer_list htimer;
};

static void handle_scancode(unsigned int pressed,unsigned int scancode, struct corgikbd *corgikbd_data)
{
	if (pressed && !(corgikbd_data->state[scancode] & CORGIKBD_PRESSED)) {
		corgikbd_data->state[scancode] |= CORGIKBD_PRESSED;
		input_report_key(&corgikbd_data->input, corgikbd_data->keycode[scancode], 1);
		if (corgikbd_data->keycode[scancode] == CORGI_KEY_OFF)
			input_event(&corgikbd_data->input, EV_PWR, CORGI_KEY_OFF, 1);
	} else if (!pressed && corgikbd_data->state[scancode] & CORGIKBD_PRESSED) {
		corgikbd_data->state[scancode] &= ~CORGIKBD_PRESSED;
		input_report_key(&corgikbd_data->input, corgikbd_data->keycode[scancode], 0);
	}
}

#define KB_DISCHARGE_DELAY	10
#define KB_ACTIVATE_DELAY	10

/* Helper functions for reading the keyboard matrix
 * Note: We should really be using pxa_gpio_mode to alter GPDR but it
 *       requires a function call per GPIO bit which is excessive
 *       when we need to access 12 bits at once multiple times.
 * These functions must be called within local_irq_save()/local_irq_restore()
 * or similar.
 */
static inline void corgikbd_discharge_all(void)
{
	// STROBE All HiZ
	GPCR2  = CORGI_GPIO_ALL_STROBE_BIT;
	GPDR2 &= ~CORGI_GPIO_ALL_STROBE_BIT;
}

static inline void corgikbd_activate_all(void)
{
	// STROBE ALL -> High
	GPSR2  = CORGI_GPIO_ALL_STROBE_BIT;
	GPDR2 |= CORGI_GPIO_ALL_STROBE_BIT;

	udelay(KB_DISCHARGE_DELAY);

	// Clear any interrupts we may have triggered when altering the GPIO lines
	GEDR1 = CORGI_GPIO_HIGH_SENSE_BIT;
	GEDR2 = CORGI_GPIO_LOW_SENSE_BIT;
}

static inline void corgikbd_activate_col(int col)
{
	// STROBE col -> High, not col -> HiZ
	GPSR2 = CORGI_GPIO_STROBE_BIT(col);
	GPDR2 = (GPDR2 & ~CORGI_GPIO_ALL_STROBE_BIT) | CORGI_GPIO_STROBE_BIT(col);
}

static inline void corgikbd_reset_col(int col)
{
	// STROBE col -> Low
	GPCR2 = CORGI_GPIO_STROBE_BIT(col);
	// STROBE col -> out, not col -> HiZ
	GPDR2 = (GPDR2 & ~CORGI_GPIO_ALL_STROBE_BIT) | CORGI_GPIO_STROBE_BIT(col);
}

#define GET_ROWS_STATUS(c)	(((GPLR1 & CORGI_GPIO_HIGH_SENSE_BIT) >> CORGI_GPIO_HIGH_SENSE_RSHIFT) | ((GPLR2 & CORGI_GPIO_LOW_SENSE_BIT) << CORGI_GPIO_LOW_SENSE_LSHIFT))

/*
 * The corgi keyboard only generates interrupts when a key is pressed.
 * When a key is pressed, we enable a timer which then scans the
 * keyboard to detect when the key is released.
 */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void corgikbd_scankeyboard(struct corgikbd *corgikbd_data, struct pt_regs *regs)
{
	unsigned int row, col, rowd, scancode;
	unsigned long flags;
	unsigned int num_pressed;

	spin_lock_irqsave(&corgikbd_data->lock, flags);

	if (regs)
		input_regs(&corgikbd_data->input, regs);

	num_pressed = 0;
	for (col = 0; col < KB_COLS; col++) {
		/*
		 * Discharge the output driver capacitatance
		 * in the keyboard matrix. (Yes it is significant..)
		 */

		corgikbd_discharge_all();
		udelay(KB_DISCHARGE_DELAY);

		corgikbd_activate_col(col);
		udelay(KB_ACTIVATE_DELAY);

		rowd = GET_ROWS_STATUS(col);
		for (row = 0; row < KB_ROWS; row++) {
			scancode = SCANCODE(row, col);
			handle_scancode((rowd & KB_ROWMASK(row)), scancode, corgikbd_data);
			if (rowd & KB_ROWMASK(row))
				num_pressed++;
		}
		corgikbd_reset_col(col);
	}

	corgikbd_activate_all();

	input_sync(&corgikbd_data->input);

	/* if any keys are pressed, enable the timer */
	if (num_pressed)
		mod_timer(&corgikbd_data->timer, jiffies + SCAN_INTERVAL);

	spin_unlock_irqrestore(&corgikbd_data->lock, flags);
}

/*
 * corgi keyboard interrupt handler.
 */
static irqreturn_t corgikbd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct corgikbd *corgikbd_data = dev_id;

	if (!timer_pending(&corgikbd_data->timer)) {
		/** wait chattering delay **/
		udelay(20);
		corgikbd_scankeyboard(corgikbd_data, regs);
	}

	return IRQ_HANDLED;
}

/*
 * corgi timer checking for released keys
 */
static void corgikbd_timer_callback(unsigned long data)
{
	struct corgikbd *corgikbd_data = (struct corgikbd *) data;
	corgikbd_scankeyboard(corgikbd_data, NULL);
}

/*
 * The hinge switches generate no interrupt so they need to be
 * monitored by a timer.
 *
 * When we detect changes, we debounce it and then pass the three
 * positions the system can take as keypresses to the input system.
 */

#define HINGE_STABLE_COUNT 2
static int sharpsl_hinge_state;
static int hinge_count;

static void corgikbd_hinge_timer(unsigned long data)
{
	struct corgikbd *corgikbd_data = (struct corgikbd *) data;
	unsigned long gprr;
	unsigned long flags;

	gprr = read_scoop_reg(SCOOP_GPRR) & (CORGI_SCP_SWA | CORGI_SCP_SWB);
	if (gprr != sharpsl_hinge_state) {
		hinge_count = 0;
		sharpsl_hinge_state = gprr;
	} else if (hinge_count < HINGE_STABLE_COUNT) {
		hinge_count++;
		if (hinge_count >= HINGE_STABLE_COUNT) {
			spin_lock_irqsave(&corgikbd_data->lock, flags);

			handle_scancode((sharpsl_hinge_state == 0x00), 125, corgikbd_data); /* Keyboard with Landscape Screen */
			handle_scancode((sharpsl_hinge_state == 0x08), 126, corgikbd_data); /* No Keyboard with Portrait Screen */
			handle_scancode((sharpsl_hinge_state == 0x0c), 127, corgikbd_data); /* Keyboard and Screen Closed  */
			input_sync(&corgikbd_data->input);

			spin_unlock_irqrestore(&corgikbd_data->lock, flags);
		}
	}
	mod_timer(&corgikbd_data->htimer, jiffies + HINGE_SCAN_INTERVAL);
}

static int __init corgikbd_probe(struct device *dev)
{
	int i;
	struct corgikbd *corgikbd;

	corgikbd = kcalloc(1, sizeof(struct corgikbd), GFP_KERNEL);
	if (!corgikbd)
		return -ENOMEM;

	dev_set_drvdata(dev,corgikbd);
	strcpy(corgikbd->phys, "corgikbd/input0");

	spin_lock_init(corgikbd->lock);

	/* Init Keyboard rescan timer */
	init_timer(&corgikbd->timer);
	corgikbd->timer.function = corgikbd_timer_callback;
	corgikbd->timer.data = (unsigned long) corgikbd;

	/* Init Hinge Timer */
	init_timer(&corgikbd->htimer);
	corgikbd->htimer.function = corgikbd_hinge_timer;
	corgikbd->htimer.data = (unsigned long) corgikbd;

	init_input_dev(&corgikbd->input);
	corgikbd->input.private = corgikbd;
	corgikbd->input.name = "Corgi Keyboard";
	corgikbd->input.dev = dev;
	corgikbd->input.phys = corgikbd->phys;
	corgikbd->input.id.bustype = BUS_HOST;
	corgikbd->input.id.vendor = 0x0001;
	corgikbd->input.id.product = 0x0001;
	corgikbd->input.id.version = 0x0100;
	corgikbd->input.evbit[0] = BIT(EV_KEY) | BIT(EV_REP) | BIT(EV_PWR);
	corgikbd->input.keycode = corgikbd->keycode;
	corgikbd->input.keycodesize = sizeof(unsigned char);
	corgikbd->input.keycodemax = ARRAY_SIZE(corgikbd_keycode);

	memcpy(corgikbd->keycode, corgikbd_keycode, sizeof(corgikbd->keycode));
	for (i = 0; i < ARRAY_SIZE(corgikbd_keycode); i++)
		set_bit(corgikbd->keycode[i], corgikbd->input.keybit);
	clear_bit(0, corgikbd->input.keybit);

	input_register_device(&corgikbd->input);
	mod_timer(&corgikbd->htimer, jiffies + HINGE_SCAN_INTERVAL);

	/* Setup sense interrupts - RisingEdge Detect, sense lines as inputs */
	for (i = 0; i < CORGI_KEY_SENSE_NUM; i++) {
		pxa_gpio_mode(CORGI_GPIO_KEY_SENSE(i) | GPIO_IN);
		if (request_irq(CORGI_IRQ_GPIO_KEY_SENSE(i), corgikbd_interrupt,
						SA_INTERRUPT, "corgikbd", corgikbd))
			printk(KERN_WARNING "corgikbd: Can't get IRQ: %d!\n", i);
		else
			set_irq_type(CORGI_IRQ_GPIO_KEY_SENSE(i),IRQT_RISING);
	}

	/* Set Strobe lines as outputs - set high */
	for (i = 0; i < CORGI_KEY_STROBE_NUM; i++)
		pxa_gpio_mode(CORGI_GPIO_KEY_STROBE(i) | GPIO_OUT | GPIO_DFLT_HIGH);

	printk(KERN_INFO "input: Corgi Keyboard Registered\n");

	return 0;
}

static int corgikbd_remove(struct device *dev)
{
	int i;
	struct corgikbd *corgikbd = dev_get_drvdata(dev);

	for (i = 0; i < CORGI_KEY_SENSE_NUM; i++)
		free_irq(CORGI_IRQ_GPIO_KEY_SENSE(i), corgikbd);

	del_timer_sync(&corgikbd->htimer);
	del_timer_sync(&corgikbd->timer);

	input_unregister_device(&corgikbd->input);

	kfree(corgikbd);

	return 0;
}

static struct device_driver corgikbd_driver = {
	.name		= "corgi-keyboard",
	.bus		= &platform_bus_type,
	.probe		= corgikbd_probe,
	.remove		= corgikbd_remove,
};

static int __devinit corgikbd_init(void)
{
	return driver_register(&corgikbd_driver);
}

static void __exit corgikbd_exit(void)
{
	driver_unregister(&corgikbd_driver);
}

module_init(corgikbd_init);
module_exit(corgikbd_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Corgi Keyboard Driver");
MODULE_LICENSE("GPLv2");
