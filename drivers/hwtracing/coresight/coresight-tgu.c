// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/amba/bus.h>
#include <linux/topology.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-priv.h"

#define tgu_writel(drvdata, val, off)	__raw_writel((val), drvdata->base + off)
#define tgu_readl(drvdata, off)		__raw_readl(drvdata->base + off)

#define TGU_LOCK(drvdata)						\
do {									\
	mb(); /* ensure configuration take effect before we lock it */	\
	tgu_writel(drvdata, 0x0, CORESIGHT_LAR);			\
} while (0)
#define TGU_UNLOCK(drvdata)						\
do {									\
	tgu_writel(drvdata, CORESIGHT_UNLOCK, CORESIGHT_LAR);		\
	mb(); /* ensure unlock take effect before we configure */	\
} while (0)

#define TGU_CONTROL			0x0000
#define TIMER0_STATUS			0x0004
#define COUNTER0_STATUS			0x000C
#define TGU_STATUS			0x0014
#define TIMER0_COMPARE_STEP(n)		(0x0040 + 0x1D8 * n)
#define COUNTER0_COMPARE_STEP(n)	(0x0048 + 0x1D8 * n)
#define GROUP_REG_STEP(grp, reg, step)	(0x0074 + 0x60 * grp + 0x4 * reg + \
								 0x1D8 * step)
#define CONDITION_DECODE_STEP(m, n)	(0x0050 + 0x4 * m + 0x1D8 * n)
#define CONDITION_SELECT_STEP(m, n)	(0x0060 + 0x4 * m + 0x1D8 * n)
#define GROUP0				0x0074
#define GROUP1				0x00D4
#define GROUP2				0x0134
#define GROUP3				0x0194
#define TGU_LAR				0x0FB0

#define MAX_GROUP_SETS			256
#define MAX_GROUPS			4
#define MAX_CONDITION_SETS		64
#define MAX_TIMER_COUNTER_SETS		8

#define to_tgu_drvdata(c)		container_of(c, struct tgu_drvdata, tgu)

struct Trigger_group_data {
	unsigned long grpaddr;
	unsigned long value;
};

struct Trigger_condition_data {
	unsigned long condaddr;
	unsigned long value;
};

struct Trigger_select_data {
	unsigned long selectaddr;
	unsigned long value;
};

struct Trigger_timer_data {
	unsigned long timeraddr;
	unsigned long value;
};

struct Trigger_counter_data {
	unsigned long counteraddr;
	unsigned long value;
};
struct tgu_drvdata {
	void __iomem			*base;
	struct device			*dev;
	struct coresight_device		*csdev;
	struct clk			*clk;
	spinlock_t			spinlock;
	int				max_steps;
	int				max_conditions;
	int				max_regs;
	int				max_timer_counter;
	struct Trigger_group_data	*grp_data;
	struct Trigger_condition_data	*condition_data;
	struct Trigger_select_data	*select_data;
	struct Trigger_timer_data	*timer_data;
	struct Trigger_counter_data	*counter_data;
	int				grp_refcnt;
	int				cond_refcnt;
	int				select_refcnt;
	int				timer_refcnt;
	int				counter_refcnt;
	bool				enable;
};

DEFINE_CORESIGHT_DEVLIST(tgu_devs, "tgu");

static ssize_t enable_tgu_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long value;
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	int ret, i, j;

	if (kstrtoul(buf, 16, &value))
		return -EINVAL;

	/* Enable clock */
	ret = pm_runtime_get_sync(drvdata->dev);
	if (ret < 0) {
		pm_runtime_put(drvdata->dev);
		return ret;
	}

	spin_lock(&drvdata->spinlock);
	/* Unlock the TGU LAR */
	TGU_UNLOCK(drvdata);

	if (value) {

		/* Disable TGU to program the triggers */
		tgu_writel(drvdata, 0, TGU_CONTROL);

		/* program the TGU Group data for the desired use case*/

		for (i = 0; i <= drvdata->grp_refcnt; i++)
			tgu_writel(drvdata, drvdata->grp_data[i].value,
						drvdata->grp_data[i].grpaddr);

		/* program the unused Condition Decode registers NOT bits to 1*/
		for (i = 0; i <= drvdata->max_conditions; i++) {
			for (j = 0; j <= drvdata->max_steps; j++)
				tgu_writel(drvdata, 0x1000000,
						CONDITION_DECODE_STEP(i, j));
		}
		/* program the TGU Condition Decode for the desired use case*/
		for (i = 0; i <= drvdata->cond_refcnt; i++)
			tgu_writel(drvdata, drvdata->condition_data[i].value,
					drvdata->condition_data[i].condaddr);

		/* program the TGU Condition Select for the desired use case*/
		for (i = 0; i <= drvdata->select_refcnt; i++)
			tgu_writel(drvdata, drvdata->select_data[i].value,
					drvdata->select_data[i].selectaddr);

		/*  Timer and Counter Check */
		for (i = 0; i <= drvdata->timer_refcnt; i++)
			tgu_writel(drvdata, drvdata->timer_data[i].value,
					drvdata->timer_data[i].timeraddr);

		for (i = 0; i <= drvdata->counter_refcnt; i++)
			tgu_writel(drvdata, drvdata->counter_data[i].value,
					drvdata->counter_data[i].counteraddr);

		/* Enable TGU to program the triggers */
		tgu_writel(drvdata, 1, TGU_CONTROL);

		drvdata->enable = true;
		dev_dbg(dev, "Coresight-TGU enabled\n");

	} else {
		/* Disable TGU to program the triggers */
		tgu_writel(drvdata, 0, TGU_CONTROL);

		pm_runtime_put(drvdata->dev);
		dev_dbg(dev, "Coresight-TGU disabled\n");
	}

	TGU_LOCK(drvdata);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_WO(enable_tgu);

static ssize_t reset_tgu_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long value;
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	int ret;

	if (kstrtoul(buf, 16, &value))
		return -EINVAL;

	if (!drvdata->enable) {
		/* Enable clock */
		ret = pm_runtime_get_sync(drvdata->dev);
		if (ret < 0) {
			pm_runtime_put(drvdata->dev);
			return ret;
		}
	}

	spin_lock(&drvdata->spinlock);
	/* Unlock the TGU LAR */
	TGU_UNLOCK(drvdata);

	if (value) {
		/* Disable TGU to program the triggers */
		tgu_writel(drvdata, 0, TGU_CONTROL);

		/* Reset the Reference counters*/
		drvdata->grp_refcnt = 0;
		drvdata->cond_refcnt = 0;
		drvdata->select_refcnt = 0;
		drvdata->timer_refcnt = 0;
		drvdata->counter_refcnt = 0;

		dev_dbg(dev, "Coresight-TGU disabled\n");
	} else
		dev_dbg(dev, "Invalid input to reset the TGU\n");

	TGU_LOCK(drvdata);
	spin_unlock(&drvdata->spinlock);
	pm_runtime_put(drvdata->dev);
	return size;
}
static DEVICE_ATTR_WO(reset_tgu);

static ssize_t set_group_store(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	int grp, reg, step;
	unsigned long value;

	if (drvdata->grp_refcnt >= MAX_GROUP_SETS) {
		dev_err(drvdata->dev, " Too many groups are being configured\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %d %d %lx", &grp, &reg, &step, &value) != 4)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if ((grp <= MAX_GROUPS) && (reg <= drvdata->max_regs)) {
		drvdata->grp_data[drvdata->grp_refcnt].grpaddr =
						GROUP_REG_STEP(grp, reg, step);
		drvdata->grp_data[drvdata->grp_refcnt].value = value;
		drvdata->grp_refcnt++;
	} else
		dev_err(drvdata->dev, "Invalid group data\n");

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(set_group);

static ssize_t set_condition_store(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long value;
	int cond, step;

	if (drvdata->cond_refcnt >= MAX_CONDITION_SETS) {
		dev_err(drvdata->dev, " Too many groups are being configured\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %d %lx", &cond, &step, &value) != 3)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if ((cond <= drvdata->max_conditions) && (step <=
						drvdata->max_steps)) {
		drvdata->condition_data[drvdata->cond_refcnt].condaddr =
					CONDITION_DECODE_STEP(cond, step);
		drvdata->condition_data[drvdata->cond_refcnt].value = value;
		drvdata->cond_refcnt++;
	} else
		dev_err(drvdata->dev, "Invalid condition decode data\n");

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(set_condition);

static ssize_t set_select_store(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long value;
	int select, step;

	if (drvdata->select_refcnt >= MAX_CONDITION_SETS) {
		dev_err(drvdata->dev, " Too many groups are being configured\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %d %lx", &select, &step, &value) != 3)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);

	if ((select <= drvdata->max_conditions) && (step <=
					drvdata->max_steps)) {
		drvdata->select_data[drvdata->select_refcnt].selectaddr =
					CONDITION_SELECT_STEP(select, step);
		drvdata->select_data[drvdata->select_refcnt].value = value;
		drvdata->select_refcnt++;
	} else
		dev_err(drvdata->dev, "Invalid select decode data\n");

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(set_select);

static ssize_t set_timer_store(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long value;
	int step;

	if (drvdata->timer_refcnt >= MAX_TIMER_COUNTER_SETS) {
		dev_err(drvdata->dev, " Too many groups are being configured\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %lx", &step, &value) != 2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (step <= drvdata->max_timer_counter) {
		drvdata->timer_data[drvdata->timer_refcnt].timeraddr =
						TIMER0_COMPARE_STEP(step);
		drvdata->timer_data[drvdata->timer_refcnt].value = value;
		drvdata->timer_refcnt++;
	} else
		dev_err(drvdata->dev, "Invalid TGU timer data\n");

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(set_timer);

static ssize_t set_counter_store(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long value;
	int step;

	if (drvdata->counter_refcnt >= MAX_TIMER_COUNTER_SETS) {
		dev_err(drvdata->dev, " Too many groups are being configured\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %lx", &step, &value) != 2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (step <= drvdata->max_timer_counter) {
		drvdata->counter_data[drvdata->counter_refcnt].counteraddr =
						COUNTER0_COMPARE_STEP(step);
		drvdata->counter_data[drvdata->counter_refcnt].value = value;
		drvdata->counter_refcnt++;
	} else
		dev_err(drvdata->dev, "Invalid TGU counter data\n");

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(set_counter);

static struct attribute *tgu_attrs[] = {
	&dev_attr_enable_tgu.attr,
	&dev_attr_reset_tgu.attr,
	&dev_attr_set_group.attr,
	&dev_attr_set_condition.attr,
	&dev_attr_set_select.attr,
	&dev_attr_set_timer.attr,
	&dev_attr_set_counter.attr,
	NULL,
};

static struct attribute_group tgu_attr_grp = {
	.attrs = tgu_attrs,
};

static const struct attribute_group *tgu_attr_grps[] = {
	&tgu_attr_grp,
	NULL,
};

static int tgu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct tgu_drvdata *drvdata;
	struct coresight_desc desc = { 0 };

	desc.name = coresight_alloc_device_name(&tgu_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;

	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (!drvdata->base)
		return -ENOMEM;

	spin_lock_init(&drvdata->spinlock);

	ret = of_property_read_u32(adev->dev.of_node, "tgu-steps",
						&drvdata->max_steps);
	if (ret)
		return -EINVAL;

	ret = of_property_read_u32(adev->dev.of_node, "tgu-conditions",
						&drvdata->max_conditions);
	if (ret)
		return -EINVAL;

	ret = of_property_read_u32(adev->dev.of_node, "tgu-regs",
							&drvdata->max_regs);
	if (ret)
		return -EINVAL;

	ret = of_property_read_u32(adev->dev.of_node, "tgu-timer-counters",
						&drvdata->max_timer_counter);
	if (ret)
		return -EINVAL;

	/* Alloc memory for Grps, Conditions and Steps */
	drvdata->grp_data = devm_kzalloc(dev, MAX_GROUP_SETS *
				       sizeof(*drvdata->grp_data),
				       GFP_KERNEL);
	if (!drvdata->grp_data)
		return -ENOMEM;

	drvdata->condition_data = devm_kzalloc(dev, MAX_CONDITION_SETS *
				       sizeof(*drvdata->condition_data),
				       GFP_KERNEL);

	if (!drvdata->condition_data)
		return -ENOMEM;

	drvdata->select_data = devm_kzalloc(dev, MAX_CONDITION_SETS *
				       sizeof(*drvdata->select_data),
				       GFP_KERNEL);
	if (!drvdata->select_data)
		return -ENOMEM;

	drvdata->timer_data = devm_kzalloc(dev, MAX_TIMER_COUNTER_SETS *
				       sizeof(*drvdata->timer_data),
				       GFP_KERNEL);
	if (!drvdata->timer_data)
		return -ENOMEM;

	drvdata->counter_data = devm_kzalloc(dev, MAX_TIMER_COUNTER_SETS *
				       sizeof(*drvdata->counter_data),
				       GFP_KERNEL);
	if (!drvdata->counter_data)
		return -ENOMEM;

	drvdata->enable = false;

	desc.type = CORESIGHT_DEV_TYPE_HELPER;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.groups = tgu_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto err;
	}

	pm_runtime_put(&adev->dev);
	dev_dbg(dev, "TGU initialized\n");
	return 0;
err:
	pm_runtime_put(&adev->dev);
	return ret;
}

static struct amba_id tgu_ids[] = {
	{
		.id	=	0x0003b999,
		.mask	=	0x0003ffff,
		.data	=	"TGU",
	},
	{ 0, 0},
};

static struct amba_driver tgu_driver = {
	.drv = {
		.name			=	"coresight-tgu",
		.owner			=	THIS_MODULE,
		.suppress_bind_attrs	=	true,
	},
	.probe		=	tgu_probe,
	.id_table	=	tgu_ids,
};

builtin_amba_driver(tgu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight TGU driver");
