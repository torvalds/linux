/*
 * bios-less APM driver for hp680
 *
 * Copyright 2005 (c) Andriy Skulysh <askulysh@gmail.com>
 * Copyright 2008 (c) Kristoffer Ericson <kristoffer.ericson@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/apm-emulation.h>
#include <linux/io.h>
#include <asm/adc.h>
#include <mach/hp6xx.h>

/* percentage values */
#define APM_CRITICAL			10
#define APM_LOW				30

/* resonably sane values */
#define HP680_BATTERY_MAX		898
#define HP680_BATTERY_MIN		486
#define HP680_BATTERY_AC_ON		1023

#define MODNAME "hp6x0_apm"

#define PGDR	0xa400012c

static void hp6x0_apm_get_power_status(struct apm_power_info *info)
{
	int battery, backup, charging, percentage;
	u8 pgdr;

	battery		= adc_single(ADC_CHANNEL_BATTERY);
	backup		= adc_single(ADC_CHANNEL_BACKUP);
	charging	= adc_single(ADC_CHANNEL_CHARGE);

	percentage = 100 * (battery - HP680_BATTERY_MIN) /
			   (HP680_BATTERY_MAX - HP680_BATTERY_MIN);

	/* % of full battery */
	info->battery_life = percentage;

	/* We want our estimates in minutes */
	info->units = 0;

	/* Extremely(!!) rough estimate, we will replace this with a datalist later on */
	info->time = (2 * battery);

	info->ac_line_status = (battery > HP680_BATTERY_AC_ON) ?
			 APM_AC_ONLINE : APM_AC_OFFLINE;

	pgdr = ctrl_inb(PGDR);
	if (pgdr & PGDR_MAIN_BATTERY_OUT) {
		info->battery_status	= APM_BATTERY_STATUS_NOT_PRESENT;
		info->battery_flag	= 0x80;
	} else if (charging < 8) {
		info->battery_status	= APM_BATTERY_STATUS_CHARGING;
		info->battery_flag	= 0x08;
		info->ac_line_status	= 0x01;
	} else if (percentage <= APM_CRITICAL) {
		info->battery_status	= APM_BATTERY_STATUS_CRITICAL;
		info->battery_flag	= 0x04;
	} else if (percentage <= APM_LOW) {
		info->battery_status	= APM_BATTERY_STATUS_LOW;
		info->battery_flag	= 0x02;
	} else {
		info->battery_status	= APM_BATTERY_STATUS_HIGH;
		info->battery_flag	= 0x01;
	}
}

static irqreturn_t hp6x0_apm_interrupt(int irq, void *dev)
{
	if (!APM_DISABLED)
		apm_queue_event(APM_USER_SUSPEND);

	return IRQ_HANDLED;
}

static int __init hp6x0_apm_init(void)
{
	int ret;

	ret = request_irq(HP680_BTN_IRQ, hp6x0_apm_interrupt,
			  IRQF_DISABLED, MODNAME, NULL);
	if (unlikely(ret < 0)) {
		printk(KERN_ERR MODNAME ": IRQ %d request failed\n",
		       HP680_BTN_IRQ);
		return ret;
	}

	apm_get_power_status = hp6x0_apm_get_power_status;

	return ret;
}

static void __exit hp6x0_apm_exit(void)
{
	free_irq(HP680_BTN_IRQ, 0);
}

module_init(hp6x0_apm_init);
module_exit(hp6x0_apm_exit);

MODULE_AUTHOR("Adriy Skulysh");
MODULE_DESCRIPTION("hp6xx Advanced Power Management");
MODULE_LICENSE("GPL");
