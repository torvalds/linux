/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Support EXYNOS specific sysfs interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/cpumask.h>
#include <linux/stat.h>
#include <linux/cpu.h>

#include <mach/exynos-interface.h>

#define MIN_NUM_ONLINE_CPU	1
#define MAX_NUM_ONLINE_CPU	NR_CPUS

unsigned int min_num_cpu;
unsigned int max_num_cpu;

static ssize_t show_cpucore_table(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	int i, num_cpu, count = 0;

	num_cpu = num_online_cpus();
	for (i = num_cpu; i > 0; i--)
		count += snprintf(&buf[count], 3, "%d ", i);

	count += snprintf(&buf[count], 3, "\n");
	return count;
}

static ssize_t show_cpucore_min_num_limit(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	return snprintf(buf, 5, "%d\n", min_num_cpu);
}

static ssize_t show_cpucore_max_num_limit(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	return snprintf(buf, 5, "%d\n", max_num_cpu);
}

static ssize_t store_cpucore_min_num_limit(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int input;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input < 0 || input > 3) {
		pr_err("Must keep input range 0 ~ 3\n");
		return -EINVAL;
	}

	pr_info("Not yet supported\n");

	min_num_cpu = input;

	return count;
}

static ssize_t store_cpucore_max_num_limit(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int input, delta, cpu;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input < 1 || input > 4) {
		pr_err("Must keep input range 1 ~ 4\n");
		return -EINVAL;
	}

	delta = input - num_online_cpus();

	if (delta > 0) {
		cpu = 1;
		while (delta) {
			if (!cpu_online(cpu)) {
				cpu_up(cpu);
				delta--;
			}
			cpu++;
		}
	} else if (delta < 0) {
		cpu = 3;
		while (delta) {
			if (cpu_online(cpu)) {
				cpu_down(cpu);
				delta++;
			}
			cpu--;
		}
	}

	max_num_cpu = input;

	return count;
}

static struct sysfs_attr cpucore_table =
		__ATTR(cpucore_table, S_IRUGO,
			show_cpucore_table, NULL);
static struct sysfs_attr cpucore_min_num_limit =
		__ATTR(cpucore_min_num_limit, S_IRUGO | S_IWUSR,
			show_cpucore_min_num_limit,
			store_cpucore_min_num_limit);
static struct sysfs_attr cpucore_max_num_limit =
		__ATTR(cpucore_max_num_limit, S_IRUGO | S_IWUSR,
			show_cpucore_max_num_limit,
			store_cpucore_max_num_limit);

static int __init exynos_interface_init(void)
{
	int ret = 0;

	min_num_cpu = 0;
	max_num_cpu = NR_CPUS;

	ret = sysfs_create_file(power_kobj, &cpucore_table.attr);
	if (ret)
		goto err;

	ret = sysfs_create_file(power_kobj, &cpucore_min_num_limit.attr);
	if (ret)
		goto err;

	ret = sysfs_create_file(power_kobj, &cpucore_max_num_limit.attr);
	if (ret)
		goto err;

err:
	pr_err("%s: failed to create sysfs interface\n", __func__);

	return ret;
}
subsys_initcall(exynos_interface_init);
