/*
 * Based on spitz_pm.c and sharp code.
 *
 * Copyright (C) 2001  SHARP
 * Copyright 2005 Pavel Machek <pavel@suse.cz>
 *
 * Distribute under GPLv2.
 *
 * Li-ion batteries are angry beasts, and they like to explode. This driver is not finished,
 * and sometimes charges them when it should not. If it makes angry lithium to come your way...
 * ...well, you have been warned.
 *
 * Actually, this should be quite safe, it seems sharp leaves charger enabled by default,
 * and my collie did not explode (yet).
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm/hardware/scoop.h>
#include <mach/dma.h>
#include <mach/collie.h>
#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/sharpsl_pm.h>

#include "../drivers/mfd/ucb1x00.h"

static struct ucb1x00 *ucb;
static int ad_revise;

#define ADCtoPower(x)	       ((330 * x * 2) / 1024)

static void collie_charger_init(void)
{
	int err;

	if (sharpsl_param.adadj != -1)
		ad_revise = sharpsl_param.adadj;

	/* Register interrupt handler. */
	if ((err = request_irq(COLLIE_IRQ_GPIO_AC_IN, sharpsl_ac_isr, IRQF_DISABLED,
			       "ACIN", sharpsl_ac_isr))) {
		printk("Could not get irq %d.\n", COLLIE_IRQ_GPIO_AC_IN);
		return;
	}
	if ((err = request_irq(COLLIE_IRQ_GPIO_CO, sharpsl_chrg_full_isr, IRQF_DISABLED,
			       "CO", sharpsl_chrg_full_isr))) {
		free_irq(COLLIE_IRQ_GPIO_AC_IN, sharpsl_ac_isr);
		printk("Could not get irq %d.\n", COLLIE_IRQ_GPIO_CO);
		return;
	}

	ucb1x00_io_set_dir(ucb, 0, COLLIE_TC35143_GPIO_MBAT_ON | COLLIE_TC35143_GPIO_TMP_ON |
			           COLLIE_TC35143_GPIO_BBAT_ON);
	return;
}

static void collie_measure_temp(int on)
{
	if (on)
		ucb1x00_io_write(ucb, COLLIE_TC35143_GPIO_TMP_ON, 0);
	else
		ucb1x00_io_write(ucb, 0, COLLIE_TC35143_GPIO_TMP_ON);
}

static void collie_charge(int on)
{
	extern struct platform_device colliescoop_device;

	/* Zaurus seems to contain LTC1731; it should know when to
	 * stop charging itself, so setting charge on should be
	 * relatively harmless (as long as it is not done too often).
	 */
	if (on) {
		set_scoop_gpio(&colliescoop_device.dev, COLLIE_SCP_CHARGE_ON);
	} else {
		reset_scoop_gpio(&colliescoop_device.dev, COLLIE_SCP_CHARGE_ON);
	}
}

static void collie_discharge(int on)
{
}

static void collie_discharge1(int on)
{
}

static void collie_presuspend(void)
{
}

static void collie_postsuspend(void)
{
}

static int collie_should_wakeup(unsigned int resume_on_alarm)
{
	return 0;
}

static unsigned long collie_charger_wakeup(void)
{
	return 0;
}

int collie_read_backup_battery(void)
{
	int voltage;

	ucb1x00_adc_enable(ucb);

	ucb1x00_io_write(ucb, COLLIE_TC35143_GPIO_BBAT_ON, 0);
	voltage = ucb1x00_adc_read(ucb, UCB_ADC_INP_AD1, UCB_SYNC);

	ucb1x00_io_write(ucb, 0, COLLIE_TC35143_GPIO_BBAT_ON);
	ucb1x00_adc_disable(ucb);

	printk("Backup battery = %d(%d)\n", ADCtoPower(voltage), voltage);

	return ADCtoPower(voltage);
}

int collie_read_main_battery(void)
{
	int voltage, voltage_rev, voltage_volts;

	ucb1x00_adc_enable(ucb);
	ucb1x00_io_write(ucb, 0, COLLIE_TC35143_GPIO_BBAT_ON);
	ucb1x00_io_write(ucb, COLLIE_TC35143_GPIO_MBAT_ON, 0);

	mdelay(1);
	voltage = ucb1x00_adc_read(ucb, UCB_ADC_INP_AD1, UCB_SYNC);

	ucb1x00_io_write(ucb, 0, COLLIE_TC35143_GPIO_MBAT_ON);
	ucb1x00_adc_disable(ucb);

	voltage_rev = voltage + ((ad_revise * voltage) / 652);
	voltage_volts = ADCtoPower(voltage_rev);

	printk("Main battery = %d(%d)\n", voltage_volts, voltage);

	if (voltage != -1)
		return voltage_volts;
	else
		return voltage;
}

int collie_read_temp(void)
{
	int voltage;

	/* According to Sharp, temp must be > 973, main battery must be < 465,
	   FIXME: sharpsl_pm.c has both conditions negated? FIXME: values
	   are way out of range? */

	ucb1x00_adc_enable(ucb);
	ucb1x00_io_write(ucb, COLLIE_TC35143_GPIO_TMP_ON, 0);
	/* >1010 = battery removed, 460 = 22C ?, higher = lower temp ? */
	voltage = ucb1x00_adc_read(ucb, UCB_ADC_INP_AD0, UCB_SYNC);
	ucb1x00_io_write(ucb, 0, COLLIE_TC35143_GPIO_TMP_ON);
	ucb1x00_adc_disable(ucb);

	printk("Battery temp = %d\n", voltage);
	return voltage;
}

static unsigned long read_devdata(int which)
{
	switch (which) {
	case SHARPSL_BATT_VOLT:
		return collie_read_main_battery();
	case SHARPSL_BATT_TEMP:
		return collie_read_temp();
	case SHARPSL_ACIN_VOLT:
		return 500;
	case SHARPSL_STATUS_ACIN: {
		int ret = GPLR & COLLIE_GPIO_AC_IN;
		printk("AC status = %d\n", ret);
		return ret;
	}
	case SHARPSL_STATUS_FATAL: {
		int ret = GPLR & COLLIE_GPIO_MAIN_BAT_LOW;
		printk("Fatal bat = %d\n", ret);
		return ret;
	}
	default:
		return ~0;
	}
}

struct battery_thresh collie_battery_levels_acin[] = {
	{ 420, 100},
	{ 417,  95},
	{ 415,  90},
	{ 413,  80},
	{ 411,  75},
	{ 408,  70},
	{ 406,  60},
	{ 403,  50},
	{ 398,  40},
	{ 391,  25},
	{  10,   5},
	{   0,   0},
};

struct battery_thresh collie_battery_levels[] = {
	{ 394, 100},
	{ 390,  95},
	{ 380,  90},
	{ 370,  80},
	{ 368,  75},	/* From sharp code: battery high with frontlight */
	{ 366,  70},	/* 60..90 -- fake values invented by me for testing */
	{ 364,  60},
	{ 362,  50},
	{ 360,  40},
	{ 358,  25},	/* From sharp code: battery low with frontlight */
	{ 356,   5},	/* From sharp code: battery verylow with frontlight */
	{   0,   0},
};

struct sharpsl_charger_machinfo collie_pm_machinfo = {
	.init             = collie_charger_init,
	.read_devdata	  = read_devdata,
	.discharge        = collie_discharge,
	.discharge1       = collie_discharge1,
	.charge           = collie_charge,
	.measure_temp     = collie_measure_temp,
	.presuspend       = collie_presuspend,
	.postsuspend      = collie_postsuspend,
	.charger_wakeup   = collie_charger_wakeup,
	.should_wakeup    = collie_should_wakeup,
	.bat_levels       = 12,
	.bat_levels_noac  = collie_battery_levels,
	.bat_levels_acin  = collie_battery_levels_acin,
	.status_high_acin = 368,
	.status_low_acin  = 358,
	.status_high_noac = 368,
	.status_low_noac  = 358,
	.charge_on_volt	  = 350,	/* spitz uses 2.90V, but lets play it safe. */
	.charge_on_temp   = 550,
	.charge_acin_high = 550,	/* collie does not seem to have sensor for this, anyway */
	.charge_acin_low  = 450,	/* ignored, too */
	.fatal_acin_volt  = 356,
	.fatal_noacin_volt = 356,

	.batfull_irq = 1,		/* We do not want periodical charge restarts */
};

static int __init collie_pm_ucb_add(struct ucb1x00_dev *pdev)
{
	sharpsl_pm.machinfo = &collie_pm_machinfo;
	ucb = pdev->ucb;
	return 0;
}

static struct ucb1x00_driver collie_pm_ucb_driver = {
	.add	= collie_pm_ucb_add,
};

static struct platform_device *collie_pm_device;

static int __init collie_pm_init(void)
{
	int ret;

	collie_pm_device = platform_device_alloc("sharpsl-pm", -1);
	if (!collie_pm_device)
		return -ENOMEM;

	collie_pm_device->dev.platform_data = &collie_pm_machinfo;
	ret = platform_device_add(collie_pm_device);

	if (ret)
		platform_device_put(collie_pm_device);

	if (!ret)
		ret = ucb1x00_register_driver(&collie_pm_ucb_driver);

	return ret;
}

static void __exit collie_pm_exit(void)
{
	ucb1x00_unregister_driver(&collie_pm_ucb_driver);
	platform_device_unregister(collie_pm_device);
}

module_init(collie_pm_init);
module_exit(collie_pm_exit);
