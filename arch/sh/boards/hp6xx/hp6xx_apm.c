/*
 * bios-less APM driver for hp680
 *
 * Copyright 2005 (c) Andriy Skulysh <askulysh@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/module.h>
#include <linux/apm_bios.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/apm.h>
#include <asm/adc.h>
#include <asm/hp6xx/hp6xx.h>

#define SH7709_PGDR			0xa400012c

#define APM_CRITICAL			10
#define APM_LOW				30

#define HP680_BATTERY_MAX		875
#define HP680_BATTERY_MIN		600
#define HP680_BATTERY_AC_ON		900

#define MODNAME "hp6x0_apm"

static int hp6x0_apm_get_info(char *buf, char **start, off_t fpos, int length)
{
	u8 pgdr;
	char *p;
	int battery_status;
	int battery_flag;
	int ac_line_status;
	int time_units = APM_BATTERY_LIFE_UNKNOWN;

	int battery = adc_single(ADC_CHANNEL_BATTERY);
	int backup = adc_single(ADC_CHANNEL_BACKUP);
	int charging = adc_single(ADC_CHANNEL_CHARGE);
	int percentage;

	percentage = 100 * (battery - HP680_BATTERY_MIN) /
			   (HP680_BATTERY_MAX - HP680_BATTERY_MIN);

	ac_line_status = (battery > HP680_BATTERY_AC_ON) ?
			 APM_AC_ONLINE : APM_AC_OFFLINE;

	p = buf;

	pgdr = ctrl_inb(SH7709_PGDR);
	if (pgdr & PGDR_MAIN_BATTERY_OUT) {
		battery_status = APM_BATTERY_STATUS_NOT_PRESENT;
		battery_flag = 0x80;
		percentage = -1;
	} else if (charging < 8 ) {
		battery_status = APM_BATTERY_STATUS_CHARGING;
		battery_flag = 0x08;
		ac_line_status = 0xff;
	} else if (percentage <= APM_CRITICAL) {
		battery_status = APM_BATTERY_STATUS_CRITICAL;
		battery_flag = 0x04;
	} else if (percentage <= APM_LOW) {
		battery_status = APM_BATTERY_STATUS_LOW;
		battery_flag = 0x02;
	} else {
		battery_status = APM_BATTERY_STATUS_HIGH;
		battery_flag = 0x01;
	}

	p += sprintf(p, "1.0 1.2 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     APM_32_BIT_SUPPORT,
		     ac_line_status,
		     battery_status,
		     battery_flag,
		     percentage,
		     time_units,
		     "min");
	p += sprintf(p, "bat=%d backup=%d charge=%d\n",
		     battery, backup, charging);

	return p - buf;
}

static irqreturn_t hp6x0_apm_interrupt(int irq, void *dev)
{
	if (!apm_suspended)
		apm_queue_event(APM_USER_SUSPEND);

	return IRQ_HANDLED;
}

static int __init hp6x0_apm_init(void)
{
	int ret;

	ret = request_irq(HP680_BTN_IRQ, hp6x0_apm_interrupt,
			  IRQF_DISABLED, MODNAME, 0);
	if (unlikely(ret < 0)) {
		printk(KERN_ERR MODNAME ": IRQ %d request failed\n",
		       HP680_BTN_IRQ);
		return ret;
	}

	apm_get_info = hp6x0_apm_get_info;

	return ret;
}

static void __exit hp6x0_apm_exit(void)
{
	free_irq(HP680_BTN_IRQ, 0);
	apm_get_info = 0;
}

module_init(hp6x0_apm_init);
module_exit(hp6x0_apm_exit);

MODULE_AUTHOR("Adriy Skulysh");
MODULE_DESCRIPTION("hp6xx Advanced Power Management");
MODULE_LICENSE("GPL");
