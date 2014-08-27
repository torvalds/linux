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
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>

#include "rk_pm_tests.h"
#include "clk_rate.h"
#include "clk_volt.h"
#include "cpu_usage.h"
#include "dvfs_table_scan.h"

struct kobject *rk_pm_tests_kobj=NULL;
struct pm_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

extern ssize_t pvtm_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
extern ssize_t pvtm_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n);

static struct pm_attribute pm_attrs[] = {
	/* node_name	permision	show_func	store_func*/
	__ATTR(clk_rate, S_IRUGO | S_IWUSR | S_IWUGO, clk_rate_show, clk_rate_store),
	__ATTR(clk_volt, S_IRUGO | S_IWUSR | S_IWUGO, clk_volt_show, clk_volt_store),
	__ATTR(dvfs_table_scan, S_IRUGO | S_IWUSR | S_IWUGO, dvfs_table_scan_show, dvfs_table_scan_store),
	__ATTR(cpu_usage, S_IRUGO | S_IWUSR, cpu_usage_show, cpu_usage_store),
	__ATTR(pvtm, S_IRUGO | S_IWUSR, pvtm_show, pvtm_store),
/*
	__ATTR(maxfreq_volt, S_IRUGO | S_IWUSR, maxfreq_show, maxfreq_store),
	__ATTR(freq_limit, S_IRUGO | S_IWUSR, freq_limit_show, freq_limit_store),
	__ATTR(cpu_usage, S_IRUGO | S_IWUSR, cpu_usage_show, cpu_usage_store),
	__ATTR(auto_wakeup, S_IRUGO | S_IWUSR, auto_wakeup_show, auto_wakeup_store),
	__ATTR(suspend_test,   S_IRUGO | S_IWUSR,   suspend_test_show, suspend_test_store),
	__ATTR(clk_auto_volt, S_IRUGO | S_IWUSR, clk_auto_volt_show, clk_auto_volt_store),
*/
};

static void __exit rk_pm_tests_exit(void)
{
	kobject_put(rk_pm_tests_kobj);
}

struct kobject *get_rk_pm_tests_kobj(void)
{

    if(rk_pm_tests_kobj==NULL)
    {
        rk_pm_tests_kobj = kobject_create_and_add("pm_tests", NULL);

    	if (!rk_pm_tests_kobj)
    		return NULL;
    }
    return rk_pm_tests_kobj;
}

int rk_pm_tests_kobj_atrradd(const struct attribute *attr)
{
    int ret;
    struct kobject * pm_kobj;
    pm_kobj =get_rk_pm_tests_kobj();

    if (!pm_kobj)
    return -ENOMEM;

    ret = sysfs_create_file(pm_kobj,attr);
    if (ret != 0) {
        PM_ERR("pm tests create %s node error\n",attr->name);
        return ret;
    }
    return ret;
}

static int __init rk_pm_tests_init(void)
{
	int i, ret = 0;
         struct kobject * pm_kobj;
	pm_kobj =get_rk_pm_tests_kobj();

	if (!pm_kobj)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(pm_attrs); i++) {
		ret = sysfs_create_file(pm_kobj, &pm_attrs[i].attr);
		if (ret != 0) {
			PM_ERR("create index %d error\n", i);
			return ret;
		}
	}
	
	return ret;
}

late_initcall(rk_pm_tests_init);
module_exit(rk_pm_tests_exit);
