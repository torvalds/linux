/* linux/arch/arm/plat-s3c24xx/pm.c
 *
 * Copyright (c) 2004-2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX Power Manager (Suspend-To-RAM) support
 *
 * See Documentation/arm/Samsung-S3C24XX/Suspend.txt for more information
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Parts based on arch/arm/mach-pxa/pm.c
 *
 * Thanks to Dimitry Andric for debugging
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <plat/regs-serial.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-irq.h>

#include <asm/mach/time.h>

#include <plat/pm.h>

#define PFX "s3c24xx-pm: "

static struct sleep_save core_save[] = {
	SAVE_ITEM(S3C2410_LOCKTIME),
	SAVE_ITEM(S3C2410_CLKCON),

	/* we restore the timings here, with the proviso that the board
	 * brings the system up in an slower, or equal frequency setting
	 * to the original system.
	 *
	 * if we cannot guarantee this, then things are going to go very
	 * wrong here, as we modify the refresh and both pll settings.
	 */

	SAVE_ITEM(S3C2410_BWSCON),
	SAVE_ITEM(S3C2410_BANKCON0),
	SAVE_ITEM(S3C2410_BANKCON1),
	SAVE_ITEM(S3C2410_BANKCON2),
	SAVE_ITEM(S3C2410_BANKCON3),
	SAVE_ITEM(S3C2410_BANKCON4),
	SAVE_ITEM(S3C2410_BANKCON5),

#ifndef CONFIG_CPU_FREQ
	SAVE_ITEM(S3C2410_CLKDIVN),
	SAVE_ITEM(S3C2410_MPLLCON),
	SAVE_ITEM(S3C2410_REFRESH),
#endif
	SAVE_ITEM(S3C2410_UPLLCON),
	SAVE_ITEM(S3C2410_CLKSLOW),
};

static struct sleep_save misc_save[] = {
	SAVE_ITEM(S3C2410_DCLKCON),
};

/* s3c_pm_check_resume_pin
 *
 * check to see if the pin is configured correctly for sleep mode, and
 * make any necessary adjustments if it is not
*/

static void s3c_pm_check_resume_pin(unsigned int pin, unsigned int irqoffs)
{
	unsigned long irqstate;
	unsigned long pinstate;
	int irq = s3c2410_gpio_getirq(pin);

	if (irqoffs < 4)
		irqstate = s3c_irqwake_intmask & (1L<<irqoffs);
	else
		irqstate = s3c_irqwake_eintmask & (1L<<irqoffs);

	pinstate = s3c2410_gpio_getcfg(pin);

	if (!irqstate) {
		if (pinstate == S3C2410_GPIO_IRQ)
			S3C_PMDBG("Leaving IRQ %d (pin %d) enabled\n", irq, pin);
	} else {
		if (pinstate == S3C2410_GPIO_IRQ) {
			S3C_PMDBG("Disabling IRQ %d (pin %d)\n", irq, pin);
			s3c2410_gpio_cfgpin(pin, S3C2410_GPIO_INPUT);
		}
	}
}

/* s3c_pm_configure_extint
 *
 * configure all external interrupt pins
*/

void s3c_pm_configure_extint(void)
{
	int pin;

	/* for each of the external interrupts (EINT0..EINT15) we
	 * need to check wether it is an external interrupt source,
	 * and then configure it as an input if it is not
	*/

	for (pin = S3C2410_GPF(0); pin <= S3C2410_GPF(7); pin++) {
		s3c_pm_check_resume_pin(pin, pin - S3C2410_GPF(0));
	}

	for (pin = S3C2410_GPG(0); pin <= S3C2410_GPG(7); pin++) {
		s3c_pm_check_resume_pin(pin, (pin - S3C2410_GPG(0))+8);
	}
}


void s3c_pm_restore_core(void)
{
	s3c_pm_do_restore_core(core_save, ARRAY_SIZE(core_save));
	s3c_pm_do_restore(misc_save, ARRAY_SIZE(misc_save));
}

void s3c_pm_save_core(void)
{
	s3c_pm_do_save(misc_save, ARRAY_SIZE(misc_save));
	s3c_pm_do_save(core_save, ARRAY_SIZE(core_save));
}

