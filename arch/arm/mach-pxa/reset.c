// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <asm/proc-fns.h>
#include <asm/system_misc.h>

#include "regs-ost.h"
#include "reset.h"
#include "smemc.h"
#include "generic.h"

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
	writel_relaxed(OWER_WME, OWER);
	writel_relaxed(OSSR_M3, OSSR);
	/* ... in 100 ms */
	writel_relaxed(readl_relaxed(OSCR) + 368640, OSMR3);
	/*
	 * SDRAM hangs on watchdog reset on Marvell PXA270 (erratum 71)
	 * we put SDRAM into self-refresh to prevent that
	 */
	while (1)
		writel_relaxed(MDREFR_SLFRSH, MDREFR);
}

void pxa_restart(enum reboot_mode mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

	clear_reset_status(RESET_STATUS_ALL);

	switch (mode) {
	case REBOOT_SOFT:
		/* Jump into ROM at address 0 */
		soft_restart(0);
		break;
	case REBOOT_GPIO:
		do_gpio_reset();
		break;
	case REBOOT_HARD:
	default:
		do_hw_reset();
		break;
	}
}
