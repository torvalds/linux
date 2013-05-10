/*
 * APM emulation for PMU-based machines
 *
 * Copyright 2001 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/apm-emulation.h>
#include <linux/adb.h>
#include <linux/pmu.h>

#define APM_CRITICAL		10
#define APM_LOW			30

static void pmu_apm_get_power_status(struct apm_power_info *info)
{
	int percentage = -1;
	int batteries = 0;
	int time_units = -1;
	int real_count = 0;
	int i;
	char charging = 0;
	long charge = -1;
	long amperage = 0;
	unsigned long btype = 0;

	info->battery_status = APM_BATTERY_STATUS_UNKNOWN;
	info->battery_flag = APM_BATTERY_FLAG_UNKNOWN;
	info->units = APM_UNITS_MINS;

	if (pmu_power_flags & PMU_PWR_AC_PRESENT)
		info->ac_line_status = APM_AC_ONLINE;
	else
		info->ac_line_status = APM_AC_OFFLINE;

	for (i=0; i<pmu_battery_count; i++) {
		if (pmu_batteries[i].flags & PMU_BATT_PRESENT) {
			batteries++;
			if (percentage < 0)
				percentage = 0;
			if (charge < 0)
				charge = 0;
			percentage += (pmu_batteries[i].charge * 100) /
				pmu_batteries[i].max_charge;
			charge += pmu_batteries[i].charge;
			amperage += pmu_batteries[i].amperage;
			if (btype == 0)
				btype = (pmu_batteries[i].flags & PMU_BATT_TYPE_MASK);
			real_count++;
			if ((pmu_batteries[i].flags & PMU_BATT_CHARGING))
				charging++;
		}
	}
	if (batteries == 0)
		info->ac_line_status = APM_AC_ONLINE;

	if (real_count) {
		if (amperage < 0) {
			if (btype == PMU_BATT_TYPE_SMART)
				time_units = (charge * 59) / (amperage * -1);
			else
				time_units = (charge * 16440) / (amperage * -60);
		}
		percentage /= real_count;
		if (charging > 0) {
			info->battery_status = APM_BATTERY_STATUS_CHARGING;
			info->battery_flag = APM_BATTERY_FLAG_CHARGING;
		} else if (percentage <= APM_CRITICAL) {
			info->battery_status = APM_BATTERY_STATUS_CRITICAL;
			info->battery_flag = APM_BATTERY_FLAG_CRITICAL;
		} else if (percentage <= APM_LOW) {
			info->battery_status = APM_BATTERY_STATUS_LOW;
			info->battery_flag = APM_BATTERY_FLAG_LOW;
		} else {
			info->battery_status = APM_BATTERY_STATUS_HIGH;
			info->battery_flag = APM_BATTERY_FLAG_HIGH;
		}
	}

	info->battery_life = percentage;
	info->time = time_units;
}

static int __init apm_emu_init(void)
{
	apm_get_power_status = pmu_apm_get_power_status;

	printk(KERN_INFO "apm_emu: PMU APM Emulation initialized.\n");

	return 0;
}

static void __exit apm_emu_exit(void)
{
	if (apm_get_power_status == pmu_apm_get_power_status)
		apm_get_power_status = NULL;

	printk(KERN_INFO "apm_emu: PMU APM Emulation removed.\n");
}

module_init(apm_emu_init);
module_exit(apm_emu_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt");
MODULE_DESCRIPTION("APM emulation for PowerMac");
MODULE_LICENSE("GPL");
