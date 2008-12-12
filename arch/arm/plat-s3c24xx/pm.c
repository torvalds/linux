/* linux/arch/arm/plat-s3c24xx/pm.c
 *
 * Copyright (c) 2004,2006 Simtec Electronics
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

static struct gpio_sleep {
	void __iomem	*base;
	unsigned int	 gpcon;
	unsigned int	 gpdat;
	unsigned int	 gpup;
} gpio_save[] = {
	[0] = {
		.base	= S3C2410_GPACON,
	},
	[1] = {
		.base	= S3C2410_GPBCON,
	},
	[2] = {
		.base	= S3C2410_GPCCON,
	},
	[3] = {
		.base	= S3C2410_GPDCON,
	},
	[4] = {
		.base	= S3C2410_GPECON,
	},
	[5] = {
		.base	= S3C2410_GPFCON,
	},
	[6] = {
		.base	= S3C2410_GPGCON,
	},
	[7] = {
		.base	= S3C2410_GPHCON,
	},
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

	for (pin = S3C2410_GPF0; pin <= S3C2410_GPF7; pin++) {
		s3c_pm_check_resume_pin(pin, pin - S3C2410_GPF0);
	}

	for (pin = S3C2410_GPG0; pin <= S3C2410_GPG7; pin++) {
		s3c_pm_check_resume_pin(pin, (pin - S3C2410_GPG0)+8);
	}
}

/* offsets for CON/DAT/UP registers */

#define OFFS_CON	(S3C2410_GPACON - S3C2410_GPACON)
#define OFFS_DAT	(S3C2410_GPADAT - S3C2410_GPACON)
#define OFFS_UP		(S3C2410_GPBUP  - S3C2410_GPBCON)

/* s3c_pm_save_gpios()
 *
 * Save the state of the GPIOs
 */

void s3c_pm_save_gpios(void)
{
	struct gpio_sleep *gps = gpio_save;
	unsigned int gpio;

	for (gpio = 0; gpio < ARRAY_SIZE(gpio_save); gpio++, gps++) {
		void __iomem *base = gps->base;

		gps->gpcon = __raw_readl(base + OFFS_CON);
		gps->gpdat = __raw_readl(base + OFFS_DAT);

		if (gpio > 0)
			gps->gpup = __raw_readl(base + OFFS_UP);

	}
}

/* Test whether the given masked+shifted bits of an GPIO configuration
 * are one of the SFN (special function) modes. */

static inline int is_sfn(unsigned long con)
{
	return (con == 2 || con == 3);
}

/* Test if the given masked+shifted GPIO configuration is an input */

static inline int is_in(unsigned long con)
{
	return con == 0;
}

/* Test if the given masked+shifted GPIO configuration is an output */

static inline int is_out(unsigned long con)
{
	return con == 1;
}

/**
 * s3c2410_pm_restore_gpio() - restore the given GPIO bank
 * @index: The number of the GPIO bank being resumed.
 * @gps: The sleep confgiuration for the bank.
 *
 * Restore one of the GPIO banks that was saved during suspend. This is
 * not as simple as once thought, due to the possibility of glitches
 * from the order that the CON and DAT registers are set in.
 *
 * The three states the pin can be are {IN,OUT,SFN} which gives us 9
 * combinations of changes to check. Three of these, if the pin stays
 * in the same configuration can be discounted. This leaves us with
 * the following:
 *
 * { IN => OUT }  Change DAT first
 * { IN => SFN }  Change CON first
 * { OUT => SFN } Change CON first, so new data will not glitch
 * { OUT => IN }  Change CON first, so new data will not glitch
 * { SFN => IN }  Change CON first
 * { SFN => OUT } Change DAT first, so new data will not glitch [1]
 *
 * We do not currently deal with the UP registers as these control
 * weak resistors, so a small delay in change should not need to bring
 * these into the calculations.
 *
 * [1] this assumes that writing to a pin DAT whilst in SFN will set the
 *     state for when it is next output.
 */

static void s3c2410_pm_restore_gpio(int index, struct gpio_sleep *gps)
{
	void __iomem *base = gps->base;
	unsigned long gps_gpcon = gps->gpcon;
	unsigned long gps_gpdat = gps->gpdat;
	unsigned long old_gpcon;
	unsigned long old_gpdat;
	unsigned long old_gpup = 0x0;
	unsigned long gpcon;
	int nr;

	old_gpcon = __raw_readl(base + OFFS_CON);
	old_gpdat = __raw_readl(base + OFFS_DAT);

	if (base == S3C2410_GPACON) {
		/* GPACON only has one bit per control / data and no PULLUPs.
		 * GPACON[x] = 0 => Output, 1 => SFN */

		/* first set all SFN bits to SFN */

		gpcon = old_gpcon | gps->gpcon;
		__raw_writel(gpcon, base + OFFS_CON);

		/* now set all the other bits */

		__raw_writel(gps_gpdat, base + OFFS_DAT);
		__raw_writel(gps_gpcon, base + OFFS_CON);
	} else {
		unsigned long old, new, mask;
		unsigned long change_mask = 0x0;

		old_gpup = __raw_readl(base + OFFS_UP);

		/* Create a change_mask of all the items that need to have
		 * their CON value changed before their DAT value, so that
		 * we minimise the work between the two settings.
		 */

		for (nr = 0, mask = 0x03; nr < 32; nr += 2, mask <<= 2) {
			old = (old_gpcon & mask) >> nr;
			new = (gps_gpcon & mask) >> nr;

			/* If there is no change, then skip */

			if (old == new)
				continue;

			/* If both are special function, then skip */

			if (is_sfn(old) && is_sfn(new))
				continue;

			/* Change is IN => OUT, do not change now */

			if (is_in(old) && is_out(new))
				continue;

			/* Change is SFN => OUT, do not change now */

			if (is_sfn(old) && is_out(new))
				continue;

			/* We should now be at the case of IN=>SFN,
			 * OUT=>SFN, OUT=>IN, SFN=>IN. */

			change_mask |= mask;
		}

		/* Write the new CON settings */

		gpcon = old_gpcon & ~change_mask;
		gpcon |= gps_gpcon & change_mask;

		__raw_writel(gpcon, base + OFFS_CON);

		/* Now change any items that require DAT,CON */

		__raw_writel(gps_gpdat, base + OFFS_DAT);
		__raw_writel(gps_gpcon, base + OFFS_CON);
		__raw_writel(gps->gpup, base + OFFS_UP);
	}

	S3C_PMDBG("GPIO[%d] CON %08lx => %08lx, DAT %08lx => %08lx\n",
		  index, old_gpcon, gps_gpcon, old_gpdat, gps_gpdat);
}


/** s3c2410_pm_restore_gpios()
 *
 * Restore the state of the GPIOs
 */

void s3c_pm_restore_gpios(void)
{
	struct gpio_sleep *gps = gpio_save;
	int gpio;

	for (gpio = 0; gpio < ARRAY_SIZE(gpio_save); gpio++, gps++) {
		s3c2410_pm_restore_gpio(gpio, gps);
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

