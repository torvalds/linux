/* arch/arm/mach-rk30/rk_pm_tests.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


/*************************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.*/
/*************************************************************************
FILE		:	  rk_pm_tests.c
DESC		:	  Power management in dynning state
AUTHOR		:	  chenxing
DATE		:	  2012-7-2
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
#include <linux/clk.h>
#include <linux/kobject.h>
 ***************************************************************************/
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/machine.h>
#include <plat/dma-pl330.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
//#include <mach/ddr.h>

#include "rk_pm_tests.h"

#include "clk_rate.h"
#include "clk_volt.h"
#include "freq_limit.h"
#include "cpu_usage.h"
#include "rk_suspend_test.h"
#include "clk_auto_volt.h"
//#include "rk2928_freq.h"
//#include "rk2928_max_freq.h"
//#include "cpu_calc.h"
//#include "ddr_scale_freq.h"
//#include "pmic_delay.h"
//#include "rk30_volt_diff.h"

static struct kobject *pm_tests_kobj;
struct pm_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct pm_attribute pm_attrs[] = {
	/* node_name	permision	show_func	store_func*/
#ifdef CONFIG_PM_TEST_CLK_RATE
	__ATTR(clk_rate, S_IRUGO | S_IWUSR, clk_rate_show, clk_rate_store),
#endif
#ifdef CONFIG_PM_TEST_CLK_VOLT
	__ATTR(clk_volt, S_IRUGO | S_IWUSR, clk_volt_show, clk_volt_store),
#endif
#ifdef CONFIG_PM_TEST_FREQ_LIMIT
	__ATTR(freq_limit, S_IRUGO | S_IWUSR, freq_limit_show, freq_limit_store),
#endif
#ifdef CONFIG_PM_TEST_CPU_USAGE
	__ATTR(cpu_usage, S_IRUGO | S_IWUSR, cpu_usage_show, cpu_usage_store),
#endif
#ifdef CONFIG_PM_TEST_SUSPEND_DBG
	__ATTR(auto_wakeup, S_IRUGO | S_IWUSR, auto_wakeup_show, auto_wakeup_store),
	__ATTR(suspend_test,   S_IRUGO | S_IWUSR,   suspend_test_show, suspend_test_store),
#endif
#ifdef CONFIG_PM_TEST_CLK_AUTO_VOLT
	__ATTR(clk_auto_volt, S_IRUGO | S_IWUSR, clk_auto_volt_show, clk_auto_volt_store),
#endif
#if 0
	__ATTR(ddr_scale_freq, S_IRUGO | S_IWUSR, ddr_scale_freq_show, ddr_scale_freq_store),
	__ATTR(pmic_delay, S_IRUGO | S_IWUSR, pmic_delay_show, pmic_delay_store),
#endif
//	__ATTR(rk2928_freq, S_IRUGO | S_IWUSR, rk2928_freq_show, rk2928_freq_store),
//	__ATTR(rk2928_max_freq, S_IRUGO | S_IWUSR, rk2928_max_freq_show, rk2928_max_freq_store),
//	__ATTR(cpu_calc, S_IRUGO | S_IWUSR, cpu_calc_show, cpu_calc_store),
//	__ATTR(rk30_volt_diff, S_IRUGO | S_IWUSR, rk30_volt_diff_show, rk30_volt_diff_store),
};

static void __exit rk_pm_tests_exit(void)
{
	kobject_put(pm_tests_kobj);
}

static int __init rk_pm_tests_init(void)
{
	int i, ret = 0;
	pm_tests_kobj = kobject_create_and_add("pm_tests", NULL);

	if (!pm_tests_kobj)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(pm_attrs); i++) {
		ret = sysfs_create_file(pm_tests_kobj, &pm_attrs[i].attr);
		if (ret != 0) {
			PM_ERR("create index %d error\n", i);
			return ret;
		}
	}
	
	return ret;
}

late_initcall(rk_pm_tests_init);
module_exit(rk_pm_tests_exit);
