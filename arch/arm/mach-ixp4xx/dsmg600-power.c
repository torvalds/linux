/*
 * arch/arm/mach-ixp4xx/dsmg600-power.c
 *
 * DSM-G600 Power/Reset driver
 * Author: Michael Westerhof <mwester@dls.net>
 *
 * Based on nslu2-power.c
 *  Copyright (C) 2005 Tower Technologies
 *  Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * which was based on nslu2-io.c
 *  Copyright (C) 2004 Karen Spearel
 *
 * Maintainers: http://www.nslu2-linux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#include <asm/mach-types.h>

extern void ctrl_alt_del(void);

/* This is used to make sure the power-button pusher is serious.  The button
 * must be held until the value of this counter reaches zero.
 */
static volatile int power_button_countdown;

/* Must hold the button down for at least this many counts to be processed */
#define PBUTTON_HOLDDOWN_COUNT 4 /* 2 secs */

static void dsmg600_power_handler(unsigned long data);
static DEFINE_TIMER(dsmg600_power_timer, dsmg600_power_handler, 0, 0);

static void dsmg600_power_handler(unsigned long data)
{
	/* This routine is called twice per second to check the
	 * state of the power button.
	 */

	if (*IXP4XX_GPIO_GPINR & DSMG600_PB_BM) {

		/* IO Pin is 1 (button pushed) */
		if (power_button_countdown == 0) {
			/* Signal init to do the ctrlaltdel action, this will bypass
			 * init if it hasn't started and do a kernel_restart.
			 */
			ctrl_alt_del();

			/* Change the state of the power LED to "blink" */
			gpio_line_set(DSMG600_LED_PWR_GPIO, IXP4XX_GPIO_LOW);
		}
		power_button_countdown--;

	} else {
		power_button_countdown = PBUTTON_HOLDDOWN_COUNT;
	}

	mod_timer(&dsmg600_power_timer, jiffies + msecs_to_jiffies(500));
}

static irqreturn_t dsmg600_reset_handler(int irq, void *dev_id)
{
	/* This is the paper-clip reset, it shuts the machine down directly. */
	machine_power_off();

	return IRQ_HANDLED;
}

static int __init dsmg600_power_init(void)
{
	if (!(machine_is_dsmg600()))
		return 0;

	if (request_irq(DSMG600_RB_IRQ, &dsmg600_reset_handler,
		IRQF_DISABLED | IRQF_TRIGGER_LOW, "DSM-G600 reset button",
		NULL) < 0) {

		printk(KERN_DEBUG "Reset Button IRQ %d not available\n",
			DSMG600_RB_IRQ);

		return -EIO;
	}

	/* The power button on the D-Link DSM-G600 is on GPIO 15, but
	 * it cannot handle interrupts on that GPIO line.  So we'll
	 * have to poll it with a kernel timer.
	 */

	/* Make sure that the power button GPIO is set up as an input */
	gpio_line_config(DSMG600_PB_GPIO, IXP4XX_GPIO_IN);

	/* Set the initial value for the power button IRQ handler */
	power_button_countdown = PBUTTON_HOLDDOWN_COUNT;

	mod_timer(&dsmg600_power_timer, jiffies + msecs_to_jiffies(500));

	return 0;
}

static void __exit dsmg600_power_exit(void)
{
	if (!(machine_is_dsmg600()))
		return;

	del_timer_sync(&dsmg600_power_timer);

	free_irq(DSMG600_RB_IRQ, NULL);
}

module_init(dsmg600_power_init);
module_exit(dsmg600_power_exit);

MODULE_AUTHOR("Michael Westerhof <mwester@dls.net>");
MODULE_DESCRIPTION("DSM-G600 Power/Reset driver");
MODULE_LICENSE("GPL");
