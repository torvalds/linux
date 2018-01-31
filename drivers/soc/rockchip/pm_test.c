/*
 * Rockchip pm_test Driver
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/uaccess.h>
#include "../../../../drivers/regulator/internal.h"

#define CLK_NR_CLKS 550

static int cpu_usage_run;

static DEFINE_PER_CPU(struct work_struct, work_cpu_usage);
static DEFINE_PER_CPU(struct workqueue_struct *, workqueue_cpu_usage);

static const char pi_result[] = "3141592653589793238462643383279528841971693993751058209749445923078164062862089986280348253421170679821480865132823664709384469555822317253594081284811174502841270193852115559644622948954930381964428810975665933446128475648233786783165271201991456485669234634861045432664821339360726024914127372458706606315588174881520920962829254917153643678925903611330530548820466521384146951941511609433057273657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566438602139494639522473719070217986943702770539217176293176752384674818467669451320005681271452635608277857713427577896091736371787214684409012249534301465495853710579227968925892354201995611212902196864344181598136297747713099605187072113499999983729780499510597317328160963185";

struct pm_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n);
};

static ssize_t clk_rate_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	char *str = buf;

	str += sprintf(str, "get clk rate:\n");
	str += sprintf(str,
		"         echo get [clk_name] > /sys/pm_tests/clk_rate\n");
	str += sprintf(str, "set clk rate:\n");
	str += sprintf(str,
		"         echo rawset [clk_name] [rate(Hz)] > /sys/pm_tests/clk_rate\n");
	str += sprintf(str, "enable clk:\n");
	str += sprintf(str,
		"         echo open [clk_name] > /sys/pm_tests/clk_rate\n");
	str += sprintf(str, "disable clk:\n");
	str += sprintf(str,
		"         echo close [clk_name] > /sys/pm_tests/clk_rate\n");
	if (str != buf)
		*(str - 1) = '\n';

	return (str - buf);
}

static struct clk *clk_get_by_name(const char *clk_name)
{
	const char *name;
	struct clk *clk;
	struct device_node *np;
	struct of_phandle_args clkspec;
	int i;

	np = of_find_node_by_name(NULL, "clock-controller");
	if (!np)
		return ERR_PTR(-ENODEV);

	clkspec.np = np;
	clkspec.args_count = 1;
	for (i = 1; i < CLK_NR_CLKS; i++) {
		clkspec.args[0] = i;
		clk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR_OR_NULL(clk))
			continue;
		name = __clk_get_name(clk);
		if (strlen(name) != strlen(clk_name)) {
			clk_put(clk);
			continue;
		}
		if (!strncmp(name, clk_name, strlen(clk_name)))
			break;

		clk_put(clk);
	}

	of_node_put(np);

	if (i == CLK_NR_CLKS)
		clk = NULL;

	return clk;
}

static int clk_rate_get(const char *buf)
{
	char cmd[20], clk_name[20];
	unsigned long rate;
	struct clk *clk;
	int ret;

	ret = sscanf(buf, "%s %s", cmd, clk_name);
	if (ret != 2)
		return -EINVAL;

	clk = clk_get_by_name(clk_name);
	if (IS_ERR_OR_NULL(clk)) {
		pr_err("get clock error\n");
		return PTR_ERR(clk);
	}

	rate = clk_get_rate(clk);

	pr_info("%s %lu Hz\n", clk_name, rate);

	clk_put(clk);

	return 0;
}

static int clk_rate_rawset(const char *buf)
{
	char cmd[20], clk_name[20];
	struct clk *clk;
	unsigned long rate;
	int ret;

	ret = sscanf(buf, "%s %s %lu", cmd, clk_name, &rate);
	if (ret != 3)
		return -EINVAL;

	clk = clk_get_by_name(clk_name);
	if (IS_ERR_OR_NULL(clk)) {
		pr_err("get %s error\n", clk_name);
		return PTR_ERR(clk);
	}

	ret = clk_set_rate(clk, rate);
	if (ret) {
		pr_err("set %s rate %d error\n", clk_name, ret);
		clk_put(clk);
		return ret;
	}

	pr_debug("%s %s %lu\n", cmd, clk_name, rate);

	clk_put(clk);

	return ret;
}

static int clk_open(const char *buf)
{
	char cmd[20], clk_name[20];
	struct clk *clk;
	int ret;

	ret = sscanf(buf, "%s %s", cmd, clk_name);
	if (ret != 2)
		return -EINVAL;

	clk = clk_get_by_name(clk_name);
	if (IS_ERR_OR_NULL(clk)) {
		pr_err("get clock %s error\n", clk_name);
		return PTR_ERR(clk);
	}

	clk_prepare_enable(clk);

	pr_debug("%s %s\n", cmd, clk_name);

	clk_put(clk);

	return 0;
}

static int clk_close(const char *buf)
{
	char cmd[20], clk_name[20];
	struct clk *clk;
	int ret;

	ret = sscanf(buf, "%s %s", cmd, clk_name);
	if (ret != 2)
		return -EINVAL;

	clk = clk_get_by_name(clk_name);
	if (IS_ERR_OR_NULL(clk)) {
		pr_err("get clock %s error\n", clk_name);
		return PTR_ERR(clk);
	}

	clk_disable_unprepare(clk);

	pr_debug("%s %s\n", cmd, clk_name);

	clk_put(clk);

	return 0;
}

static ssize_t clk_rate_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t n)
{
	char cmd[20];
	int ret;

	ret = sscanf(buf, "%s", cmd);
	if (ret != 1)
		return -EINVAL;

	if (!strncmp(cmd, "get", strlen("get"))) {
		ret = clk_rate_get(buf);
		if (ret)
			pr_err("get clk err\n");
	} else if (!strncmp(cmd, "rawset", strlen("rawset"))) {
		ret = clk_rate_rawset(buf);
		if (ret)
			pr_err("rawset clk err\n");
	} else if (!strncmp(cmd, "open", strlen("open"))) {
		ret = clk_open(buf);
		if (ret)
			pr_err("open clk err\n");
	} else if (!strncmp(cmd, "close", strlen("close"))) {
		ret = clk_close(buf);
		if (ret)
			pr_err("close clk err\n");
	} else {
		pr_info("unsupported cmd(%s)\n", cmd);
	}

	return n;
}

static ssize_t clk_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	char *str = buf;

	str += sprintf(str, "get voltage:\n");
	str += sprintf(str,
		"            echo get [regulaotr_name] > /sys/pm_tests/clk_volt\n");
	str += sprintf(str, "set voltage:\n");
	str += sprintf(str,
		"            echo set [regulaotr_name] [voltage(uV)] > /sys/pm_tests/clk_volt\n");
	if (str != buf)
		*(str - 1) = '\n';

	return (str - buf);
}

static int clk_volt_get(const char *buf)
{
	char cmd[20], reg_name[20];
	unsigned int ret;
	struct regulator *regulator;

	ret = sscanf(buf, "%s %s", cmd, reg_name);
	if (ret != 2)
		return -EINVAL;

	regulator = regulator_get(NULL, reg_name);
	if (IS_ERR_OR_NULL(regulator)) {
		pr_err("get regulator %s error\n", reg_name);
		return PTR_ERR(regulator);
	}

	ret = regulator_get_voltage(regulator);

	pr_info("%s %duV\n", reg_name, ret);

	regulator_put(regulator);

	return 0;
}

static void break_up_regulator_limit(struct regulator_dev *rdev, int min_uV,
				     int max_uV)
{
	struct regulator *regulator;

	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		if (!regulator->min_uV && !regulator->max_uV)
			continue;
		pr_debug("%s min=%d, max=%d", dev_name(regulator->dev),
			 regulator->min_uV, regulator->max_uV);
		regulator->min_uV = min_uV;
		regulator->max_uV = max_uV;
	}
}

static int clk_volt_set(const char *buf)
{
	char cmd[20], reg_name[20];
	unsigned int ret;
	struct regulator *regulator;
	unsigned int volt;
	struct regulator_dev *rdev;
	int max_uV;

	ret = sscanf(buf, "%s %s %u", cmd, reg_name, &volt);
	if (ret != 3)
		return -EINVAL;

	regulator = regulator_get(NULL, reg_name);
	if (IS_ERR_OR_NULL(regulator)) {
		pr_info("get regulator %s error\n", reg_name);
		return PTR_ERR(regulator);
	}

	rdev = regulator->rdev;
	max_uV = rdev->constraints->max_uV;

	if (volt > max_uV) {
		pr_err("invalid volt, max %d uV\n", max_uV);
		return -EINVAL;
	}

	mutex_lock(&rdev->mutex);
	break_up_regulator_limit(rdev, volt, max_uV);
	mutex_unlock(&rdev->mutex);

	ret = regulator_set_voltage(regulator, volt, max_uV);
	if (ret) {
		pr_err("set voltage %d error\n", ret);
		regulator_put(regulator);
		return ret;
	}

	pr_debug("set %s %duV\n", reg_name, volt);

	regulator_put(regulator);

	return 0;
}

static ssize_t clk_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t n)
{
	unsigned int ret;
	char cmd[20];

	ret = sscanf(buf, "%s", cmd);
	if (ret != 1)
		return -EINVAL;

	if (!strncmp(cmd, "get", strlen("get"))) {
		ret = clk_volt_get(buf);
		if (ret)
			pr_err("get volt err\n");
	} else if (!strncmp(cmd, "set", strlen("set"))) {
		ret = clk_volt_set(buf);
		if (ret)
			pr_err("set volt err\n");
	} else {
		pr_err("unsupported cmd(%s)\n", cmd);
	}

	return n;
}

static ssize_t cpu_usage_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	char *str = buf;

	str += sprintf(str,
		"while(1) test:\n");
	str += sprintf(str,
		"              cpu 0-3:  echo start 0 0 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"              cpu 4-7:  echo start 0 1 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"              cpu 0-7:  echo start 0 2 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"calc_pi  test:\n");
	str += sprintf(str,
		"              cpu 0-3:  echo start 1 0 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"              cpu 4-7:  echo start 1 1 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"              cpu 0-7:  echo start 1 2 > /sys/pm_tests/cpu_usage\n");
	str += sprintf(str,
		"stop     test:\n");
	str += sprintf(str,
		"              cpu 0-7:  echo stop -1 -1 > /sys/pm_tests/cpu_usage\n");
	if (str != buf)
		*(str - 1) = '\n';

	return (str - buf);
}

static inline int calc_pi(void)
{
	int bit = 0, i = 0;
	long a = 10000, b = 0, c = 2800, d = 0, e = 0, g = 0;
	int *result;
	long *f;
	int len = 0;
	char *pi_calc, *pi_temp;
	char *pi_just = (char *)&pi_result[0];
	size_t pi_just_size = sizeof(pi_result);

	result = vmalloc(10000 * sizeof(int));
	if (!result)
		return -ENOMEM;

	 f = vmalloc(2801 * sizeof(long));
	if (!f)
		return -ENOMEM;

	 pi_calc = vmalloc(1000 * sizeof(char));
	if (!pi_calc)
		return -ENOMEM;

	for (; b - c; )
		f[b++] = a / 5;
	for (; d = 0, g = c * 2; c -= 14, result[bit++] = e + d / a, e = d % a)
		for (b = c; d += f[b] * a, f[b] = d % --g, d /= g--, --b;
		     d *= b)
			;

	pi_temp = pi_calc;
	for (i = 0; i < bit; i++)
		len += sprintf(pi_temp + len, "%d", result[i]);

	if (strncmp(pi_just, pi_calc, pi_just_size) == 0) {
		vfree(result);
		vfree(f);
		vfree(pi_calc);
	} else {
		vfree(result);
		vfree(f);
		vfree(pi_calc);

		while (1)
			pr_err("calc_pi error\n");
	}

	return 0;
}

static void calc_pi2(void)
{
	calc_pi();
	calc_pi();
}

static void calc_pi4(void)
{
	calc_pi2();
	calc_pi2();
}

static void calc_pi8(void)
{
	calc_pi4();
	calc_pi4();
}

static void calc_pi16(void)
{
	calc_pi8();
	calc_pi8();
}

/* 0xffffffc000097468 - 0xffffffc0000949c4 =  10916 byte*/
static void calc_pi32(void)
{
	calc_pi16();
	calc_pi16();
}

/* 0xffffffc000099ee8 - 0xffffffc0000949c4 =  21796 byte*/
static void calc_pi64(void)
{
	calc_pi32();
	calc_pi32();
}

/* 0xffffffc00009f3e8 - 0xffffffc0000949c4 =  43556 byte*/
static void calc_pi128(void)
{
	calc_pi64();
	calc_pi64();
}

/* 0xffffffc0000a9de8 - 0xffffffc0000949c4 =  87076 byte*/
static void calc_pi256(void)
{
	calc_pi128();
	calc_pi128();
}

/* 0xffffffc0000bf1e8 - 0xffffffc0000949c4 =  174116 byte*/
static void calc_pi512(void)
{
	calc_pi256();
	calc_pi256();
}

/* 0xffffffc0000ea9e8 - 0xffffffc0000949c4 =  352295 byte*/
static void calc_pi1024(void)
{
	calc_pi512();
	calc_pi512();
}

static void calc_pi_large(void)
{
	calc_pi1024();
}

static void handler_cpu_usage(struct work_struct *work)
{
	while (cpu_usage_run == 0)
		barrier();

	while (cpu_usage_run == 1)
		calc_pi_large();
}

static ssize_t cpu_usage_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t n)
{
	struct workqueue_struct	*workqueue;
	struct work_struct *work;
	char cmd[20];
	int cpu;
	int ret;
	int usage = 0;
	int cluster = 0;

	ret = sscanf(buf, "%s %d %d", cmd, &usage, &cluster);
	if (ret != 3)
		return -EINVAL;

	if (!strncmp(cmd, "start", strlen("start"))) {
		if (usage == 0)
			pr_info("start while(1) test\n");
		else if (usage == 1)
			pr_info("start pi test\n");
		else
			return 0;

		cpu_usage_run = usage;
		for_each_online_cpu(cpu) {
			work = &per_cpu(work_cpu_usage, cpu);
			workqueue = per_cpu(workqueue_cpu_usage, cpu);
			if (!work || !workqueue) {
				pr_err("work or workqueue NULL\n");
				return n;
			}

			if (cluster == 0 && cpu < 4)
				queue_work_on(cpu, workqueue, work);
			else if (cluster == 1 && cpu >= 4)
				queue_work_on(cpu, workqueue, work);
			else if (cluster == 2)
				queue_work_on(cpu, workqueue, work);
		}
	} else if (!strncmp(cmd, "stop", strlen("stop"))) {
		if (cpu_usage_run == 0)
			pr_info("stop while(1) test\n");
		else if (cpu_usage_run == 1)
			pr_info("stop pi test\n");
		cpu_usage_run = usage;
	}

	return n;
}

static struct pm_attribute pm_attrs[] = {
	__ATTR(clk_rate, 0644, clk_rate_show, clk_rate_store),
	__ATTR(clk_volt, 0644, clk_volt_show, clk_volt_store),
	__ATTR(cpu_usage, 0644, cpu_usage_show, cpu_usage_store),
};

static int __init pm_test_init(void)
{
	struct kobject *kobject;
	struct workqueue_struct	*workqueue;
	struct work_struct *work;
	int i, cpu, ret = 0;

	kobject = kobject_create_and_add("pm_tests", NULL);
	if (!kobject)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(pm_attrs); i++) {
		ret = sysfs_create_file(kobject, &pm_attrs[i].attr);
		if (ret) {
			pr_err("create file index %d error\n", i);
			return ret;
		}
	}

	workqueue = create_workqueue("workqueue_cpu_usage");
	if (!workqueue) {
		pr_err("workqueue NULL\n");
		return -EINVAL;
	}

	for_each_online_cpu(cpu) {
		work = &per_cpu(work_cpu_usage, cpu);
		if (!work) {
			pr_err("work NULL\n");
			return -EINVAL;
		}
		INIT_WORK(work, handler_cpu_usage);
		per_cpu(workqueue_cpu_usage, cpu) = workqueue;
	}

	return ret;
}

late_initcall(pm_test_init);

MODULE_DESCRIPTION("Rockchip pm_test driver");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_LICENSE("GPL v2");
