/* linux/arch/arm/mach-exynos/dvfs-hotplug.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Integrated DVFS CPU hotplug
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/io.h>

#include <plat/cpu.h>

static unsigned int total_num_target_freq;
static unsigned int consecutv_highestlevel_cnt;
static unsigned int consecutv_lowestlevel_cnt;

static unsigned int freq_max;
static unsigned int freq_in_trg;
static unsigned int freq_min = -1UL;

static unsigned int can_hotplug;

static void exynos4_integrated_dvfs_hotplug(unsigned int freq_old,
					unsigned int freq_new)
{
	total_num_target_freq++;
	freq_in_trg = 800000;

	if ((freq_old >= freq_in_trg) && (freq_new >= freq_in_trg)) {
		if (soc_is_exynos4412()) {
			if (cpu_online(3) == 0) {
				if (consecutv_highestlevel_cnt >= 5) {
					cpu_up(3);
					consecutv_highestlevel_cnt = 0;
				}
			} else if (cpu_online(2) == 0) {
				if (consecutv_highestlevel_cnt >= 5) {
					cpu_up(2);
					consecutv_highestlevel_cnt = 0;
				}
			} else if (cpu_online(1) == 0) {
				if (consecutv_highestlevel_cnt >= 5) {
					cpu_up(1);
					consecutv_highestlevel_cnt = 0;
				}
			}
			consecutv_highestlevel_cnt++;
		} else {
			if (cpu_online(1) == 0) {
				if (consecutv_highestlevel_cnt >= 5) {
					cpu_up(1);
					consecutv_highestlevel_cnt = 0;
				}
			}
			consecutv_highestlevel_cnt++;
		}
	} else if ((freq_old <= freq_min) && (freq_new <= freq_min)) {
		if (soc_is_exynos4412()) {
			if (cpu_online(1) == 1) {
				if (consecutv_lowestlevel_cnt >= 5) {
					cpu_down(1);
					consecutv_lowestlevel_cnt = 0;
				} else
					consecutv_lowestlevel_cnt++;
			} else if (cpu_online(2) == 1) {
				if (consecutv_lowestlevel_cnt >= 5) {
					cpu_down(2);
					consecutv_lowestlevel_cnt = 0;
				} else
					consecutv_lowestlevel_cnt++;
			} else if (cpu_online(3) == 1) {
				if (consecutv_lowestlevel_cnt >= 5) {
					cpu_down(3);
					consecutv_lowestlevel_cnt = 0;
				} else
					consecutv_lowestlevel_cnt++;
			}
		} else {
			if (cpu_online(1) == 1) {
				if (consecutv_lowestlevel_cnt >= 5) {
					cpu_down(1);
					consecutv_lowestlevel_cnt = 0;
				} else
					consecutv_lowestlevel_cnt++;
			}
		}
	} else {
		consecutv_highestlevel_cnt = 0;
		consecutv_lowestlevel_cnt = 0;
	}
}

static int hotplug_cpufreq_transition(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;

	if ((val == CPUFREQ_POSTCHANGE) && can_hotplug)
		exynos4_integrated_dvfs_hotplug(freqs->old, freqs->new);

	return 0;
}

static struct notifier_block dvfs_hotplug = {
	.notifier_call = hotplug_cpufreq_transition,
};

static int hotplug_pm_transition(struct notifier_block *nb,
					unsigned long val, void *data)
{
	switch (val) {
	case PM_SUSPEND_PREPARE:
		can_hotplug = 0;
		consecutv_highestlevel_cnt = 0;
		consecutv_lowestlevel_cnt = 0;
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		can_hotplug = 1;
		break;
	}

	return 0;
}

static struct notifier_block pm_hotplug = {
	.notifier_call = hotplug_pm_transition,
};

/*
 * Note : This function should be called after intialization of CPUFreq
 * driver for exynos4. The cpufreq_frequency_table for exynos4 should be
 * established before calling this function.
 */
static int __init exynos4_integrated_dvfs_hotplug_init(void)
{
	int i;
	struct cpufreq_frequency_table *table;
	unsigned int freq;

	total_num_target_freq = 0;
	consecutv_highestlevel_cnt = 0;
	consecutv_lowestlevel_cnt = 0;
	can_hotplug = 1;

	table = cpufreq_frequency_get_table(0);
	if (IS_ERR(table)) {
		printk(KERN_ERR "%s: Check loading cpufreq before\n", __func__);
		return PTR_ERR(table);
	}

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		freq = table[i].frequency;

		if (freq != CPUFREQ_ENTRY_INVALID && freq > freq_max)
			freq_max = freq;
		else if (freq != CPUFREQ_ENTRY_INVALID && freq_min > freq)
			freq_min = freq;
	}

	printk(KERN_INFO "%s, max(%d),min(%d)\n", __func__, freq_max, freq_min);

	register_pm_notifier(&pm_hotplug);

	return cpufreq_register_notifier(&dvfs_hotplug,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

late_initcall(exynos4_integrated_dvfs_hotplug_init);
