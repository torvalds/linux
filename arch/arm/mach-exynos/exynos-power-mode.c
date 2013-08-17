/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Power mode for user space
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm_qos.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/cpu.h>

#define POWER_MODE_LEN	(16)

struct exynos_power_info {
	char		*mode_name;
	unsigned int	cpu_freq_lock;
	unsigned int	mif_freq_lock;
	unsigned int	int_freq_lock;
	bool		cpu_online_core0;
	bool		cpu_online_core1;
	bool		cpu_online_core2;
	bool		cpu_online_core3;
};

enum exynos_power_mode_idx {
	INIT_POWER_MODE,
	POWER_MODE_0,
	POWER_MODE_1,
	POWER_MODE_2,
	POWER_MODE_END,
};

/* Power mode setting information */
struct exynos_power_info exynos_power_mode[POWER_MODE_END] = {
/* cpu_lock, mif_lock, int_lock, cpu0_online, cpu1_online, cpu2_online, cpu3_online */
	{ "init",	0,	0,	0, 1, 1, 1, 1 },
	{ "quad", 1600000, 800000, 400000, 1, 1, 1, 1 },
	{ "quad_io", 1800000, 800000, 400000, 1, 1, 0, 0 },
	{ "quad_mem", 1800000, 800000, 400000, 1, 1, 0, 0 },
};

/* Information for PM_QOS */
static struct pm_qos_request exynos_qos_cpu;
static struct pm_qos_request exynos_qos_int;
static struct pm_qos_request exynos_qos_mif;

static struct class *exynos_power_mode_class;
static struct device *exynos_power_mode_dev;

static struct exynos_power_info *cur_power_mode;

static ssize_t exynos_power_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return snprintf(buf, POWER_MODE_LEN, "%s\n", cur_power_mode->mode_name);
}

static unsigned int exynos_get_power_mode(char *target_mode, unsigned int *cpu_lock,
				unsigned int *int_lock, unsigned int *mif_lock, unsigned int *cpu_hotplug_mask)
{
	unsigned int i;

	for (i = INIT_POWER_MODE; i < POWER_MODE_END; i++) {
		if (!strnicmp(exynos_power_mode[i].mode_name, target_mode, POWER_MODE_LEN)) {
			goto set_power_mode_info;
		}
	}

	return -EINVAL;
set_power_mode_info:
	cur_power_mode = &exynos_power_mode[i];

	*cpu_lock = exynos_power_mode[i].cpu_freq_lock;
	*int_lock = exynos_power_mode[i].int_freq_lock;
	*mif_lock = exynos_power_mode[i].mif_freq_lock;

	*cpu_hotplug_mask = ((exynos_power_mode[i].cpu_online_core0 << 0) |
			     (exynos_power_mode[i].cpu_online_core1 << 1) |
			     (exynos_power_mode[i].cpu_online_core2 << 2) |
			     (exynos_power_mode[i].cpu_online_core3 << 3));

	return 0;
}

static ssize_t exynos_power_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	char str_power_mode[POWER_MODE_LEN];
	int ret;
	unsigned int cpu_lock, int_lock, mif_lock, cpu_hotplug_mask;
	unsigned int i;

	ret = sscanf(buf, "%15s", str_power_mode);
	if (ret != 1)
		return -EINVAL;

	if (exynos_get_power_mode(str_power_mode, &cpu_lock, &int_lock, &mif_lock, &cpu_hotplug_mask)) {
		pr_err("%s Can not get power mode", __func__);
		return ret;
	}

	/* Hotplug in or out for per cpu */
	for (i = 0; i < num_possible_cpus(); i++) {
		if (cpu_online(i) != ((cpu_hotplug_mask >> i) & 0x1)) {
			if ((cpu_hotplug_mask >> i) & 0x1)
				cpu_up(i);
			else
				cpu_down(i);
		}
	}

	/* Update PM_QOS */
	pm_qos_update_request(&exynos_qos_cpu, cpu_lock);
	pm_qos_update_request(&exynos_qos_int, int_lock);
	pm_qos_update_request(&exynos_qos_mif, mif_lock);

	return count;
}

static DEVICE_ATTR(cur_power_mode, S_IRUGO | S_IWUSR, exynos_power_show, exynos_power_store);

static int __init exynos_power_mode_init(void)
{
	unsigned int i;

	exynos_power_mode_class = class_create(THIS_MODULE, "exynos_power");

	if (IS_ERR(exynos_power_mode_class)) {
		pr_err("%s: couldn't create class\n", __FILE__);
		return PTR_ERR(exynos_power_mode_class);
	}

	exynos_power_mode_dev = device_create(exynos_power_mode_class, NULL, 0, NULL, "exynos_power_mode");
	if (IS_ERR(exynos_power_mode_dev)) {
		pr_err("%s: couldn't create device\n", __FILE__);
		return PTR_ERR(exynos_power_mode_dev);
	}

	device_create_file(exynos_power_mode_dev, &dev_attr_cur_power_mode);

	/* Setting init power mode info */
	cur_power_mode = &exynos_power_mode[INIT_POWER_MODE];

	/* setting QOS information */
	pm_qos_add_request(&exynos_qos_cpu, PM_QOS_CPU_FREQ_MIN,
			exynos_power_mode[INIT_POWER_MODE].cpu_freq_lock);

	pm_qos_add_request(&exynos_qos_int, PM_QOS_DEVICE_THROUGHPUT,
			exynos_power_mode[INIT_POWER_MODE].int_freq_lock);

	pm_qos_add_request(&exynos_qos_mif, PM_QOS_BUS_THROUGHPUT,
			exynos_power_mode[INIT_POWER_MODE].mif_freq_lock);

	pr_info("Exynos Power mode List");
	pr_info("NAME\tCPU\tMIF\tINT\tCPU0\tCPU1\tCPU2\tCPU3\n");
	/* Show Exynos Power mode list */
	for (i = 0; i < POWER_MODE_END; i++ ) {
		pr_info("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", exynos_power_mode[i].mode_name,
			exynos_power_mode[i].cpu_freq_lock, exynos_power_mode[i].mif_freq_lock,
			exynos_power_mode[i].int_freq_lock, exynos_power_mode[i].cpu_online_core0,
			exynos_power_mode[i].cpu_online_core1, exynos_power_mode[i].cpu_online_core2,
			exynos_power_mode[i].cpu_online_core3);
	}

	return 0;
}
subsys_initcall(exynos_power_mode_init);
