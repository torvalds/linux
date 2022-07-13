// SPDX-License-Identifier: GPL-2.0
/*
 * DFL device driver for EMIF private feature
 *
 * Copyright (C) 2020 Intel Corporation, Inc.
 *
 */
#include <linux/bitfield.h>
#include <linux/dfl.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define FME_FEATURE_ID_EMIF		0x9

#define EMIF_STAT			0x8
#define EMIF_STAT_INIT_DONE_SFT		0
#define EMIF_STAT_CALC_FAIL_SFT		8
#define EMIF_STAT_CLEAR_BUSY_SFT	16
#define EMIF_CTRL			0x10
#define EMIF_CTRL_CLEAR_EN_SFT		0
#define EMIF_CTRL_CLEAR_EN_MSK		GENMASK_ULL(7, 0)

#define EMIF_POLL_INVL			10000 /* us */
#define EMIF_POLL_TIMEOUT		5000000 /* us */

/*
 * The Capability Register replaces the Control Register (at the same
 * offset) for EMIF feature revisions > 0. The bitmask that indicates
 * the presence of memory channels exists in both the Capability Register
 * and Control Register definitions. These can be thought of as a C union.
 * The Capability Register definitions are used to check for the existence
 * of a memory channel, and the Control Register definitions are used for
 * managing the memory-clear functionality in revision 0.
 */
#define EMIF_CAPABILITY_BASE		0x10
#define EMIF_CAPABILITY_CHN_MSK_V0	GENMASK_ULL(3, 0)
#define EMIF_CAPABILITY_CHN_MSK		GENMASK_ULL(7, 0)

struct dfl_emif {
	struct device *dev;
	void __iomem *base;
	spinlock_t lock;	/* Serialises access to EMIF_CTRL reg */
};

struct emif_attr {
	struct device_attribute attr;
	u32 shift;
	u32 index;
};

#define to_emif_attr(dev_attr) \
	container_of(dev_attr, struct emif_attr, attr)

static ssize_t emif_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct emif_attr *eattr = to_emif_attr(attr);
	struct dfl_emif *de = dev_get_drvdata(dev);
	u64 val;

	val = readq(de->base + EMIF_STAT);

	return sysfs_emit(buf, "%u\n",
			  !!(val & BIT_ULL(eattr->shift + eattr->index)));
}

static ssize_t emif_clear_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct emif_attr *eattr = to_emif_attr(attr);
	struct dfl_emif *de = dev_get_drvdata(dev);
	u64 clear_busy_msk, clear_en_msk, val;
	void __iomem *base = de->base;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	clear_busy_msk = BIT_ULL(EMIF_STAT_CLEAR_BUSY_SFT + eattr->index);
	clear_en_msk = BIT_ULL(EMIF_CTRL_CLEAR_EN_SFT + eattr->index);

	spin_lock(&de->lock);
	/* The CLEAR_EN field is WO, but other fields are RW */
	val = readq(base + EMIF_CTRL);
	val &= ~EMIF_CTRL_CLEAR_EN_MSK;
	val |= clear_en_msk;
	writeq(val, base + EMIF_CTRL);
	spin_unlock(&de->lock);

	if (readq_poll_timeout(base + EMIF_STAT, val,
			       !(val & clear_busy_msk),
			       EMIF_POLL_INVL, EMIF_POLL_TIMEOUT)) {
		dev_err(de->dev, "timeout, fail to clear\n");
		return -ETIMEDOUT;
	}

	return count;
}

#define emif_state_attr(_name, _shift, _index)				\
	static struct emif_attr emif_attr_##inf##_index##_##_name =	\
		{ .attr = __ATTR(inf##_index##_##_name, 0444,		\
				 emif_state_show, NULL),		\
		  .shift = (_shift), .index = (_index) }

#define emif_clear_attr(_index)						\
	static struct emif_attr emif_attr_##inf##_index##_clear =	\
		{ .attr = __ATTR(inf##_index##_clear, 0200,		\
				 NULL, emif_clear_store),		\
		  .index = (_index) }

emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 0);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 1);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 2);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 3);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 4);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 5);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 6);
emif_state_attr(init_done, EMIF_STAT_INIT_DONE_SFT, 7);

emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 0);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 1);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 2);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 3);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 4);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 5);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 6);
emif_state_attr(cal_fail, EMIF_STAT_CALC_FAIL_SFT, 7);


emif_clear_attr(0);
emif_clear_attr(1);
emif_clear_attr(2);
emif_clear_attr(3);
emif_clear_attr(4);
emif_clear_attr(5);
emif_clear_attr(6);
emif_clear_attr(7);


static struct attribute *dfl_emif_attrs[] = {
	&emif_attr_inf0_init_done.attr.attr,
	&emif_attr_inf0_cal_fail.attr.attr,
	&emif_attr_inf0_clear.attr.attr,

	&emif_attr_inf1_init_done.attr.attr,
	&emif_attr_inf1_cal_fail.attr.attr,
	&emif_attr_inf1_clear.attr.attr,

	&emif_attr_inf2_init_done.attr.attr,
	&emif_attr_inf2_cal_fail.attr.attr,
	&emif_attr_inf2_clear.attr.attr,

	&emif_attr_inf3_init_done.attr.attr,
	&emif_attr_inf3_cal_fail.attr.attr,
	&emif_attr_inf3_clear.attr.attr,

	&emif_attr_inf4_init_done.attr.attr,
	&emif_attr_inf4_cal_fail.attr.attr,
	&emif_attr_inf4_clear.attr.attr,

	&emif_attr_inf5_init_done.attr.attr,
	&emif_attr_inf5_cal_fail.attr.attr,
	&emif_attr_inf5_clear.attr.attr,

	&emif_attr_inf6_init_done.attr.attr,
	&emif_attr_inf6_cal_fail.attr.attr,
	&emif_attr_inf6_clear.attr.attr,

	&emif_attr_inf7_init_done.attr.attr,
	&emif_attr_inf7_cal_fail.attr.attr,
	&emif_attr_inf7_clear.attr.attr,

	NULL,
};

static umode_t dfl_emif_visible(struct kobject *kobj,
				struct attribute *attr, int n)
{
	struct dfl_emif *de = dev_get_drvdata(kobj_to_dev(kobj));
	struct emif_attr *eattr = container_of(attr, struct emif_attr,
					       attr.attr);
	struct dfl_device *ddev = to_dfl_dev(de->dev);
	u64 val;

	/*
	 * This device supports up to 8 memory interfaces, but not all
	 * interfaces are used on different platforms. The read out value of
	 * CAPABILITY_CHN_MSK field (which is a bitmap) indicates which
	 * interfaces are available.
	 */
	if (ddev->revision > 0 && strstr(attr->name, "_clear"))
		return 0;

	if (ddev->revision == 0)
		val = FIELD_GET(EMIF_CAPABILITY_CHN_MSK_V0,
				readq(de->base + EMIF_CAPABILITY_BASE));
	else
		val = FIELD_GET(EMIF_CAPABILITY_CHN_MSK,
				readq(de->base + EMIF_CAPABILITY_BASE));

	return (val & BIT_ULL(eattr->index)) ? attr->mode : 0;
}

static const struct attribute_group dfl_emif_group = {
	.is_visible = dfl_emif_visible,
	.attrs = dfl_emif_attrs,
};

static const struct attribute_group *dfl_emif_groups[] = {
	&dfl_emif_group,
	NULL,
};

static int dfl_emif_probe(struct dfl_device *ddev)
{
	struct device *dev = &ddev->dev;
	struct dfl_emif *de;

	de = devm_kzalloc(dev, sizeof(*de), GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	de->base = devm_ioremap_resource(dev, &ddev->mmio_res);
	if (IS_ERR(de->base))
		return PTR_ERR(de->base);

	de->dev = dev;
	spin_lock_init(&de->lock);
	dev_set_drvdata(dev, de);

	return 0;
}

static const struct dfl_device_id dfl_emif_ids[] = {
	{ FME_ID, FME_FEATURE_ID_EMIF },
	{ }
};
MODULE_DEVICE_TABLE(dfl, dfl_emif_ids);

static struct dfl_driver dfl_emif_driver = {
	.drv	= {
		.name       = "dfl-emif",
		.dev_groups = dfl_emif_groups,
	},
	.id_table = dfl_emif_ids,
	.probe   = dfl_emif_probe,
};
module_dfl_driver(dfl_emif_driver);

MODULE_DESCRIPTION("DFL EMIF driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
