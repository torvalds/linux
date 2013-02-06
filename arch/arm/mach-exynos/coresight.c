/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>

#include "coresight.h"

enum {
	CORESIGHT_CLK_OFF,
	CORESIGHT_CLK_ON_DBG,
	CORESIGHT_CLK_ON_HSDBG,
};

struct coresight_ctx {
	struct kobject	*modulekobj;
	uint8_t		max_clk;
};

static struct coresight_ctx coresight;


struct kobject *coresight_get_modulekobj(void)
{
	return coresight.modulekobj;
}

#define CORESIGHT_ATTR(name)						\
static struct kobj_attribute name##_attr =				\
		__ATTR(name, S_IRUGO | S_IWUSR, name##_show, name##_store)

static ssize_t max_clk_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	coresight.max_clk = val;
	return n;
}
static ssize_t max_clk_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	unsigned long val = coresight.max_clk;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
CORESIGHT_ATTR(max_clk);

static int __init coresight_sysfs_init(void)
{
	int ret;

	coresight.modulekobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!coresight.modulekobj) {
		pr_err("failed to find CORESIGHT sysfs module kobject\n");
		ret = -ENOENT;
		goto err;
	}

	ret = sysfs_create_file(coresight.modulekobj, &max_clk_attr.attr);
	if (ret) {
		pr_err("failed to create CORESIGHT sysfs max_clk attribute\n");
		goto err;
	}

	return 0;
err:
	return ret;
}

static void coresight_sysfs_exit(void)
{
	sysfs_remove_file(coresight.modulekobj, &max_clk_attr.attr);
}

static int __init coresight_init(void)
{
	int ret;

	ret = coresight_sysfs_init();
	if (ret)
		goto err_sysfs;
	ret = etb_init();
	if (ret)
		goto err_etb;
	ret = tpiu_init();
	if (ret)
		goto err_tpiu;
	ret = funnel_init();
	if (ret)
		goto err_funnel;
	ret = etm_init();
	if (ret)
		goto err_etm;

	pr_info("CORESIGHT initialized\n");
	return 0;
err_etm:
	funnel_exit();
err_funnel:
	tpiu_exit();
err_tpiu:
	etb_exit();
err_etb:
	coresight_sysfs_exit();
err_sysfs:
	pr_err("CORESIGHT init failed\n");
	return ret;
}
module_init(coresight_init);

static void __exit coresight_exit(void)
{
	coresight_sysfs_exit();
	etm_exit();
	funnel_exit();
	tpiu_exit();
	etb_exit();
}
module_exit(coresight_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CoreSight ETM Debug System Driver");
