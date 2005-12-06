/*
 * Battery and Power Management code for the Sharp SL-C7xx
 *
 * Copyright (c) 2005 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/apm.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/hardware/scoop.h>

#include <asm/arch/sharpsl.h>
#include <asm/arch/corgi.h>
#include <asm/arch/pxa-regs.h>
#include "sharpsl.h"

static void corgi_charger_init(void)
{
	pxa_gpio_mode(CORGI_GPIO_ADC_TEMP_ON | GPIO_OUT);
	pxa_gpio_mode(CORGI_GPIO_CHRG_ON | GPIO_OUT);
	pxa_gpio_mode(CORGI_GPIO_CHRG_UKN | GPIO_OUT);
	pxa_gpio_mode(CORGI_GPIO_KEY_INT | GPIO_IN);
}

static void corgi_charge_led(int val)
{
	if (val == SHARPSL_LED_ERROR) {
		dev_dbg(sharpsl_pm.dev, "Charge LED Error\n");
	} else if (val == SHARPSL_LED_ON) {
		dev_dbg(sharpsl_pm.dev, "Charge LED On\n");
		GPSR0 = GPIO_bit(CORGI_GPIO_LED_ORANGE);
	} else {
		dev_dbg(sharpsl_pm.dev, "Charge LED Off\n");
		GPCR0 = GPIO_bit(CORGI_GPIO_LED_ORANGE);
	}
}

static void corgi_measure_temp(int on)
{
	if (on)
		GPSR(CORGI_GPIO_ADC_TEMP_ON) = GPIO_bit(CORGI_GPIO_ADC_TEMP_ON);
	else
		GPCR(CORGI_GPIO_ADC_TEMP_ON) = GPIO_bit(CORGI_GPIO_ADC_TEMP_ON);
}

static void corgi_charge(int on)
{
	if (on) {
		if (machine_is_corgi() && (sharpsl_pm.flags & SHARPSL_SUSPENDED)) {
			GPCR(CORGI_GPIO_CHRG_ON) = GPIO_bit(CORGI_GPIO_CHRG_ON);
			GPSR(CORGI_GPIO_CHRG_UKN) = GPIO_bit(CORGI_GPIO_CHRG_UKN);
		} else {
			GPSR(CORGI_GPIO_CHRG_ON) = GPIO_bit(CORGI_GPIO_CHRG_ON);
			GPCR(CORGI_GPIO_CHRG_UKN) = GPIO_bit(CORGI_GPIO_CHRG_UKN);
		}
	} else {
		GPCR(CORGI_GPIO_CHRG_ON) = GPIO_bit(CORGI_GPIO_CHRG_ON);
		GPCR(CORGI_GPIO_CHRG_UKN) = GPIO_bit(CORGI_GPIO_CHRG_UKN);
	}
}

static void corgi_discharge(int on)
{
	if (on)
		GPSR(CORGI_GPIO_DISCHARGE_ON) = GPIO_bit(CORGI_GPIO_DISCHARGE_ON);
	else
		GPCR(CORGI_GPIO_DISCHARGE_ON) = GPIO_bit(CORGI_GPIO_DISCHARGE_ON);
}

static void corgi_presuspend(void)
{
	int i;
	unsigned long wakeup_mask;

	/* charging , so CHARGE_ON bit is HIGH during OFF. */
	if (READ_GPIO_BIT(CORGI_GPIO_CHRG_ON))
		PGSR1 |= GPIO_bit(CORGI_GPIO_CHRG_ON);
	else
		PGSR1 &= ~GPIO_bit(CORGI_GPIO_CHRG_ON);

	if (READ_GPIO_BIT(CORGI_GPIO_LED_ORANGE))
		PGSR0 |= GPIO_bit(CORGI_GPIO_LED_ORANGE);
	else
		PGSR0 &= ~GPIO_bit(CORGI_GPIO_LED_ORANGE);

	if (READ_GPIO_BIT(CORGI_GPIO_CHRG_UKN))
		PGSR1 |= GPIO_bit(CORGI_GPIO_CHRG_UKN);
	else
		PGSR1 &= ~GPIO_bit(CORGI_GPIO_CHRG_UKN);

	/* Resume on keyboard power key */
	PGSR2 = (PGSR2 & ~CORGI_GPIO_ALL_STROBE_BIT) | CORGI_GPIO_STROBE_BIT(0);

	wakeup_mask = GPIO_bit(CORGI_GPIO_KEY_INT) | GPIO_bit(CORGI_GPIO_WAKEUP) | GPIO_bit(CORGI_GPIO_AC_IN) | GPIO_bit(CORGI_GPIO_CHRG_FULL);

	if (!machine_is_corgi())
		wakeup_mask |= GPIO_bit(CORGI_GPIO_MAIN_BAT_LOW);

	PWER = wakeup_mask | PWER_RTC;
	PRER = wakeup_mask;
	PFER = wakeup_mask;

	for (i = 0; i <=15; i++) {
		if (PRER & PFER & GPIO_bit(i)) {
			if (GPLR0 & GPIO_bit(i) )
				PRER &= ~GPIO_bit(i);
			else
				PFER &= ~GPIO_bit(i);
		}
	}
}

static void corgi_postsuspend(void)
{
}

/*
 * Check what brought us out of the suspend.
 * Return: 0 to sleep, otherwise wake
 */
static int corgi_should_wakeup(unsigned int resume_on_alarm)
{
	int is_resume = 0;

	dev_dbg(sharpsl_pm.dev, "GPLR0 = %x,%x\n", GPLR0, PEDR);

	if ((PEDR & GPIO_bit(CORGI_GPIO_AC_IN))) {
		if (STATUS_AC_IN()) {
			/* charge on */
			dev_dbg(sharpsl_pm.dev, "ac insert\n");
			sharpsl_pm.flags |= SHARPSL_DO_OFFLINE_CHRG;
		} else {
			/* charge off */
			dev_dbg(sharpsl_pm.dev, "ac remove\n");
			CHARGE_LED_OFF();
			CHARGE_OFF();
			sharpsl_pm.charge_mode = CHRG_OFF;
		}
	}

	if ((PEDR & GPIO_bit(CORGI_GPIO_CHRG_FULL)))
		dev_dbg(sharpsl_pm.dev, "Charge full interrupt\n");

	if (PEDR & GPIO_bit(CORGI_GPIO_KEY_INT))
		is_resume |= GPIO_bit(CORGI_GPIO_KEY_INT);

	if (PEDR & GPIO_bit(CORGI_GPIO_WAKEUP))
		is_resume |= GPIO_bit(CORGI_GPIO_WAKEUP);

	if (resume_on_alarm && (PEDR & PWER_RTC))
		is_resume |= PWER_RTC;

	dev_dbg(sharpsl_pm.dev, "is_resume: %x\n",is_resume);
	return is_resume;
}

static unsigned long corgi_charger_wakeup(void)
{
	return ~GPLR0 & ( GPIO_bit(CORGI_GPIO_AC_IN) | GPIO_bit(CORGI_GPIO_KEY_INT) | GPIO_bit(CORGI_GPIO_WAKEUP) );
}

static int corgi_acin_status(void)
{
	return ((GPLR(CORGI_GPIO_AC_IN) & GPIO_bit(CORGI_GPIO_AC_IN)) != 0);
}

static struct sharpsl_charger_machinfo corgi_pm_machinfo = {
	.init            = corgi_charger_init,
	.gpio_batlock    = CORGI_GPIO_BAT_COVER,
	.gpio_acin       = CORGI_GPIO_AC_IN,
	.gpio_batfull    = CORGI_GPIO_CHRG_FULL,
	.status_acin     = corgi_acin_status,
	.discharge       = corgi_discharge,
	.charge          = corgi_charge,
	.chargeled       = corgi_charge_led,
	.measure_temp    = corgi_measure_temp,
	.presuspend      = corgi_presuspend,
	.postsuspend     = corgi_postsuspend,
	.charger_wakeup  = corgi_charger_wakeup,
	.should_wakeup   = corgi_should_wakeup,
	.bat_levels      = 40,
	.bat_levels_noac = spitz_battery_levels_noac,
	.bat_levels_acin = spitz_battery_levels_acin,
	.status_high_acin = 188,
	.status_low_acin  = 178,
	.status_high_noac = 185,
	.status_low_noac  = 175,
};

static struct platform_device *corgipm_device;

static int __devinit corgipm_init(void)
{
	int ret;

	corgipm_device = platform_device_alloc("sharpsl-pm", -1);
	if (!corgipm_device)
		return -ENOMEM;

	corgipm_device->dev.platform_data = &corgi_pm_machinfo;
	ret = platform_device_add(corgipm_device);

	if (ret)
		platform_device_put(corgipm_device);

	return ret;
}

static void corgipm_exit(void)
{
	platform_device_unregister(corgipm_device);
}

module_init(corgipm_init);
module_exit(corgipm_exit);
