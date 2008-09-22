/*
 * Battery and Power Management code for the Sharp SL-Cxx00
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
#include <linux/apm-emulation.h>

#include <asm/irq.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/hardware/scoop.h>

#include <mach/sharpsl.h>
#include <mach/spitz.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>
#include <mach/pxa2xx-gpio.h>
#include "sharpsl.h"

#define SHARPSL_CHARGE_ON_VOLT         0x99  /* 2.9V */
#define SHARPSL_CHARGE_ON_TEMP         0xe0  /* 2.9V */
#define SHARPSL_CHARGE_ON_ACIN_HIGH    0x9b  /* 6V */
#define SHARPSL_CHARGE_ON_ACIN_LOW     0x34  /* 2V */
#define SHARPSL_FATAL_ACIN_VOLT        182   /* 3.45V */
#define SHARPSL_FATAL_NOACIN_VOLT      170   /* 3.40V */

static int spitz_last_ac_status;

static void spitz_charger_init(void)
{
	pxa_gpio_mode(SPITZ_GPIO_KEY_INT | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_SYNC | GPIO_IN);
	sharpsl_pm_pxa_init();
}

static void spitz_measure_temp(int on)
{
	if (on)
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_ADC_TEMP_ON);
	else
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_ADC_TEMP_ON);
}

static void spitz_charge(int on)
{
	if (on) {
		if (sharpsl_pm.flags & SHARPSL_SUSPENDED) {
			set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_JK_B);
			reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CHRG_ON);
		} else {
			reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_JK_B);
			reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CHRG_ON);
		}
	} else {
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_JK_B);
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CHRG_ON);
	}
}

static void spitz_discharge(int on)
{
	if (on)
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_JK_A);
	else
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_JK_A);
}

/* HACK - For unknown reasons, accurate voltage readings are only made with a load
   on the power bus which the green led on spitz provides */
static void spitz_discharge1(int on)
{
	if (on)
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_GREEN);
	else
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_GREEN);
}

static void spitz_presuspend(void)
{
	spitz_last_ac_status = sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN);

	/* GPIO Sleep Register */
	PGSR0 = 0x00144018;
	PGSR1 = 0x00EF0000;
	if (machine_is_akita()) {
		PGSR2 = 0x2121C000;
		PGSR3 = 0x00600400;
	} else {
		PGSR2 = 0x0121C000;
		PGSR3 = 0x00600000;
	}

	PGSR0 &= ~SPITZ_GPIO_G0_STROBE_BIT;
	PGSR1 &= ~SPITZ_GPIO_G1_STROBE_BIT;
	PGSR2 &= ~SPITZ_GPIO_G2_STROBE_BIT;
	PGSR3 &= ~SPITZ_GPIO_G3_STROBE_BIT;
	PGSR2 |= GPIO_bit(SPITZ_GPIO_KEY_STROBE0);

	pxa_gpio_mode(GPIO18_RDY|GPIO_OUT | GPIO_DFLT_HIGH);

	PRER = GPIO_bit(SPITZ_GPIO_KEY_INT);
	PFER = GPIO_bit(SPITZ_GPIO_KEY_INT) | GPIO_bit(SPITZ_GPIO_RESET);
	PWER = GPIO_bit(SPITZ_GPIO_KEY_INT) | GPIO_bit(SPITZ_GPIO_RESET) | PWER_RTC;
	PKWR = GPIO_bit(SPITZ_GPIO_SYNC) | GPIO_bit(SPITZ_GPIO_KEY_INT) | GPIO_bit(SPITZ_GPIO_RESET);
	PKSR = 0xffffffff; // clear

	/* nRESET_OUT Disable */
	PSLR |= PSLR_SL_ROD;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR = PCFR_GPR_EN | PCFR_OPDE;
}

static void spitz_postsuspend(void)
{
	pxa_gpio_mode(GPIO18_RDY_MD);
	pxa_gpio_mode(10 | GPIO_IN);
}

static int spitz_should_wakeup(unsigned int resume_on_alarm)
{
	int is_resume = 0;
	int acin = sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN);

	if (spitz_last_ac_status != acin) {
		if (acin) {
			/* charge on */
			sharpsl_pm.flags |= SHARPSL_DO_OFFLINE_CHRG;
			dev_dbg(sharpsl_pm.dev, "AC Inserted\n");
		} else {
			/* charge off */
			dev_dbg(sharpsl_pm.dev, "AC Removed\n");
			sharpsl_pm_led(SHARPSL_LED_OFF);
			sharpsl_pm.machinfo->charge(0);
			sharpsl_pm.charge_mode = CHRG_OFF;
		}
		spitz_last_ac_status = acin;
		/* Return to suspend as this must be what we were woken for */
		return 0;
	}

	if (PEDR & GPIO_bit(SPITZ_GPIO_KEY_INT))
		is_resume |= GPIO_bit(SPITZ_GPIO_KEY_INT);

	if (PKSR & GPIO_bit(SPITZ_GPIO_SYNC))
		is_resume |= GPIO_bit(SPITZ_GPIO_SYNC);

	if (resume_on_alarm && (PEDR & PWER_RTC))
		is_resume |= PWER_RTC;

	dev_dbg(sharpsl_pm.dev, "is_resume: %x\n",is_resume);
	return is_resume;
}

static unsigned long spitz_charger_wakeup(void)
{
	return (~GPLR0 & GPIO_bit(SPITZ_GPIO_KEY_INT)) | (GPLR0 & GPIO_bit(SPITZ_GPIO_SYNC));
}

unsigned long spitzpm_read_devdata(int type)
{
	switch(type) {
	case SHARPSL_STATUS_ACIN:
		return (((~GPLR(SPITZ_GPIO_AC_IN)) & GPIO_bit(SPITZ_GPIO_AC_IN)) != 0);
	case SHARPSL_STATUS_LOCK:
		return READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_batlock);
	case SHARPSL_STATUS_CHRGFULL:
		return READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_batfull);
	case SHARPSL_STATUS_FATAL:
		return READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_fatal);
	case SHARPSL_ACIN_VOLT:
		return sharpsl_pm_pxa_read_max1111(MAX1111_ACIN_VOLT);
	case SHARPSL_BATT_TEMP:
		return sharpsl_pm_pxa_read_max1111(MAX1111_BATT_TEMP);
	case SHARPSL_BATT_VOLT:
	default:
		return sharpsl_pm_pxa_read_max1111(MAX1111_BATT_VOLT);
	}
}

struct sharpsl_charger_machinfo spitz_pm_machinfo = {
	.init             = spitz_charger_init,
	.exit             = sharpsl_pm_pxa_remove,
	.gpio_batlock     = SPITZ_GPIO_BAT_COVER,
	.gpio_acin        = SPITZ_GPIO_AC_IN,
	.gpio_batfull     = SPITZ_GPIO_CHRG_FULL,
	.batfull_irq	  = 1,
	.gpio_fatal       = SPITZ_GPIO_FATAL_BAT,
	.discharge        = spitz_discharge,
	.discharge1       = spitz_discharge1,
	.charge           = spitz_charge,
	.measure_temp     = spitz_measure_temp,
	.presuspend       = spitz_presuspend,
	.postsuspend      = spitz_postsuspend,
	.read_devdata     = spitzpm_read_devdata,
	.charger_wakeup   = spitz_charger_wakeup,
	.should_wakeup    = spitz_should_wakeup,
#ifdef CONFIG_BACKLIGHT_CORGI
        .backlight_limit  = corgibl_limit_intensity,
#endif
	.charge_on_volt	  = SHARPSL_CHARGE_ON_VOLT,
	.charge_on_temp	  = SHARPSL_CHARGE_ON_TEMP,
	.charge_acin_high = SHARPSL_CHARGE_ON_ACIN_HIGH,
	.charge_acin_low  = SHARPSL_CHARGE_ON_ACIN_LOW,
	.fatal_acin_volt  = SHARPSL_FATAL_ACIN_VOLT,
	.fatal_noacin_volt= SHARPSL_FATAL_NOACIN_VOLT,
	.bat_levels       = 40,
	.bat_levels_noac  = spitz_battery_levels_noac,
	.bat_levels_acin  = spitz_battery_levels_acin,
	.status_high_acin = 188,
	.status_low_acin  = 178,
	.status_high_noac = 185,
	.status_low_noac  = 175,
};

static struct platform_device *spitzpm_device;

static int __devinit spitzpm_init(void)
{
	int ret;

	if (!machine_is_spitz() && !machine_is_akita()
			&& !machine_is_borzoi())
		return -ENODEV;

	spitzpm_device = platform_device_alloc("sharpsl-pm", -1);
	if (!spitzpm_device)
		return -ENOMEM;

	spitzpm_device->dev.platform_data = &spitz_pm_machinfo;
	ret = platform_device_add(spitzpm_device);

	if (ret)
		platform_device_put(spitzpm_device);

	return ret;
}

static void spitzpm_exit(void)
{
 	platform_device_unregister(spitzpm_device);
}

module_init(spitzpm_init);
module_exit(spitzpm_exit);
