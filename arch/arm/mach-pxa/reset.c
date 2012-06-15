/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <asm/proc-fns.h>
#include <asm/system_misc.h>

#include <mach/regs-ost.h>
#include <mach/reset.h>

unsigned int reset_status;
EXPORT_SYMBOL(reset_status);

static void do_hw_reset(void);

static int reset_gpio = -1;

int init_gpio_reset(int gpio, int output, int level)
{
	int rc;

	rc = gpio_request(gpio, "reset generator");
	if (rc) {
		printk(KERN_ERR "Can't request reset_gpio\n");
		goto out;
	}

	if (output)
		rc = gpio_direction_output(gpio, level);
	else
		rc = gpio_direction_input(gpio);
	if (rc) {
		printk(KERN_ERR "Can't configure reset_gpio\n");
		gpio_free(gpio);
		goto out;
	}

out:
	if (!rc)
		reset_gpio = gpio;

	return rc;
}

/*
 * Trigger GPIO reset.
 * This covers various types of logic connecting gpio pin
 * to RESET pins (nRESET or GPIO_RESET):
 */
static void do_gpio_reset(void)
{
	BUG_ON(reset_gpio == -1);

	/* drive it low */
	gpio_direction_output(reset_gpio, 0);
	mdelay(2);
	/* rising edge or drive high */
	gpio_set_value(reset_gpio, 1);
	mdelay(2);
	/* falling edge */
	gpio_set_value(reset_gpio, 0);

	/* give it some time */
	mdelay(10);

	WARN_ON(1);
	/* fallback */
	do_hw_reset();
}

static void do_hw_reset(void)
{
	/* Initialize the watchdog and let it fire */
	OWER = OWER_WME;
	OSSR = OSSR_M3;
	OSMR3 = OSCR + 368640;	/* ... in 100 ms */
}

void pxa_restart(char mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

	clear_reset_status(RESET_STATUS_ALL);

	switch (mode) {
	case 's':
		/* Jump into ROM at address 0 */
		soft_restart(0);
		break;
	case 'g':
		do_gpio_reset();
		break;
	case 'h':
	default:
		do_hw_reset();
		break;
	}
}

