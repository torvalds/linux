// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/amba/bus.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/coresight.h>
#include <linux/coresight-pmu.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "coresight-priv.h"
#include "coresight-tpdm.h"

DEFINE_CORESIGHT_DEVLIST(tpdm_devs, "tpdm");

/* Read dataset array member with the index number */
static ssize_t tpdm_simple_dataset_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct tpdm_dataset_attribute *tpdm_attr =
		container_of(attr, struct tpdm_dataset_attribute, attr);

	switch (tpdm_attr->mem) {
	case DSB_EDGE_CTRL:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_EDCR)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->edge_ctrl[tpdm_attr->idx]);
	case DSB_EDGE_CTRL_MASK:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_EDCMR)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->edge_ctrl_mask[tpdm_attr->idx]);
	case DSB_TRIG_PATT:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_PATT)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->trig_patt[tpdm_attr->idx]);
	case DSB_TRIG_PATT_MASK:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_PATT)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->trig_patt_mask[tpdm_attr->idx]);
	case DSB_PATT:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_PATT)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->patt_val[tpdm_attr->idx]);
	case DSB_PATT_MASK:
		if (tpdm_attr->idx >= TPDM_DSB_MAX_PATT)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
			drvdata->dsb->patt_mask[tpdm_attr->idx]);
	case DSB_MSR:
		if (tpdm_attr->idx >= drvdata->dsb_msr_num)
			return -EINVAL;
		return sysfs_emit(buf, "0x%x\n",
				drvdata->dsb->msr[tpdm_attr->idx]);
	}
	return -EINVAL;
}

/* Write dataset array member with the index number */
static ssize_t tpdm_simple_dataset_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	unsigned long val;
	ssize_t ret = size;

	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct tpdm_dataset_attribute *tpdm_attr =
		container_of(attr, struct tpdm_dataset_attribute, attr);

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	switch (tpdm_attr->mem) {
	case DSB_TRIG_PATT:
		if (tpdm_attr->idx < TPDM_DSB_MAX_PATT)
			drvdata->dsb->trig_patt[tpdm_attr->idx] = val;
		else
			ret = -EINVAL;
		break;
	case DSB_TRIG_PATT_MASK:
		if (tpdm_attr->idx < TPDM_DSB_MAX_PATT)
			drvdata->dsb->trig_patt_mask[tpdm_attr->idx] = val;
		else
			ret = -EINVAL;
		break;
	case DSB_PATT:
		if (tpdm_attr->idx < TPDM_DSB_MAX_PATT)
			drvdata->dsb->patt_val[tpdm_attr->idx] = val;
		else
			ret = -EINVAL;
		break;
	case DSB_PATT_MASK:
		if (tpdm_attr->idx < TPDM_DSB_MAX_PATT)
			drvdata->dsb->patt_mask[tpdm_attr->idx] = val;
		else
			ret = -EINVAL;
		break;
	case DSB_MSR:
		if (tpdm_attr->idx < drvdata->dsb_msr_num)
			drvdata->dsb->msr[tpdm_attr->idx] = val;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock(&drvdata->spinlock);

	return ret;
}

static bool tpdm_has_dsb_dataset(struct tpdm_drvdata *drvdata)
{
	return (drvdata->datasets & TPDM_PIDR0_DS_DSB);
}

static umode_t tpdm_dsb_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (drvdata && tpdm_has_dsb_dataset(drvdata))
		return attr->mode;

	return 0;
}

static umode_t tpdm_dsb_msr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct device_attribute *dev_attr =
		container_of(attr, struct device_attribute, attr);
	struct tpdm_dataset_attribute *tpdm_attr =
		container_of(dev_attr, struct tpdm_dataset_attribute, attr);

	if (tpdm_attr->idx < drvdata->dsb_msr_num)
		return attr->mode;

	return 0;
}

static void tpdm_reset_datasets(struct tpdm_drvdata *drvdata)
{
	if (tpdm_has_dsb_dataset(drvdata)) {
		memset(drvdata->dsb, 0, sizeof(struct dsb_dataset));

		drvdata->dsb->trig_ts = true;
		drvdata->dsb->trig_type = false;
	}
}

static void set_dsb_mode(struct tpdm_drvdata *drvdata, u32 *val)
{
	u32 mode;

	/* Set the test accurate mode */
	mode = TPDM_DSB_MODE_TEST(drvdata->dsb->mode);
	*val &= ~TPDM_DSB_CR_TEST_MODE;
	*val |= FIELD_PREP(TPDM_DSB_CR_TEST_MODE, mode);

	/* Set the byte lane for high-performance mode */
	mode = TPDM_DSB_MODE_HPBYTESEL(drvdata->dsb->mode);
	*val &= ~TPDM_DSB_CR_HPSEL;
	*val |= FIELD_PREP(TPDM_DSB_CR_HPSEL, mode);

	/* Set the performance mode */
	if (drvdata->dsb->mode & TPDM_DSB_MODE_PERF)
		*val |= TPDM_DSB_CR_MODE;
	else
		*val &= ~TPDM_DSB_CR_MODE;
}

static void set_dsb_tier(struct tpdm_drvdata *drvdata)
{
	u32 val;

	val = readl_relaxed(drvdata->base + TPDM_DSB_TIER);

	/* Clear all relevant fields */
	val &= ~(TPDM_DSB_TIER_PATT_TSENAB | TPDM_DSB_TIER_PATT_TYPE |
		 TPDM_DSB_TIER_XTRIG_TSENAB);

	/* Set pattern timestamp type and enablement */
	if (drvdata->dsb->patt_ts) {
		val |= TPDM_DSB_TIER_PATT_TSENAB;
		if (drvdata->dsb->patt_type)
			val |= TPDM_DSB_TIER_PATT_TYPE;
		else
			val &= ~TPDM_DSB_TIER_PATT_TYPE;
	} else {
		val &= ~TPDM_DSB_TIER_PATT_TSENAB;
	}

	/* Set trigger timestamp */
	if (drvdata->dsb->trig_ts)
		val |= TPDM_DSB_TIER_XTRIG_TSENAB;
	else
		val &= ~TPDM_DSB_TIER_XTRIG_TSENAB;

	writel_relaxed(val, drvdata->base + TPDM_DSB_TIER);
}

static void set_dsb_msr(struct tpdm_drvdata *drvdata)
{
	int i;

	for (i = 0; i < drvdata->dsb_msr_num; i++)
		writel_relaxed(drvdata->dsb->msr[i],
			   drvdata->base + TPDM_DSB_MSR(i));
}

static void tpdm_enable_dsb(struct tpdm_drvdata *drvdata)
{
	u32 val, i;

	for (i = 0; i < TPDM_DSB_MAX_EDCR; i++)
		writel_relaxed(drvdata->dsb->edge_ctrl[i],
			   drvdata->base + TPDM_DSB_EDCR(i));
	for (i = 0; i < TPDM_DSB_MAX_EDCMR; i++)
		writel_relaxed(drvdata->dsb->edge_ctrl_mask[i],
			   drvdata->base + TPDM_DSB_EDCMR(i));
	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		writel_relaxed(drvdata->dsb->patt_val[i],
			   drvdata->base + TPDM_DSB_TPR(i));
		writel_relaxed(drvdata->dsb->patt_mask[i],
			   drvdata->base + TPDM_DSB_TPMR(i));
		writel_relaxed(drvdata->dsb->trig_patt[i],
			   drvdata->base + TPDM_DSB_XPR(i));
		writel_relaxed(drvdata->dsb->trig_patt_mask[i],
			   drvdata->base + TPDM_DSB_XPMR(i));
	}

	set_dsb_tier(drvdata);

	set_dsb_msr(drvdata);

	val = readl_relaxed(drvdata->base + TPDM_DSB_CR);
	/* Set the mode of DSB dataset */
	set_dsb_mode(drvdata, &val);
	/* Set trigger type */
	if (drvdata->dsb->trig_type)
		val |= TPDM_DSB_CR_TRIG_TYPE;
	else
		val &= ~TPDM_DSB_CR_TRIG_TYPE;
	/* Set the enable bit of DSB control register to 1 */
	val |= TPDM_DSB_CR_ENA;
	writel_relaxed(val, drvdata->base + TPDM_DSB_CR);
}

/*
 * TPDM enable operations
 * The TPDM or Monitor serves as data collection component for various
 * dataset types. It covers Basic Counts(BC), Tenure Counts(TC),
 * Continuous Multi-Bit(CMB), Multi-lane CMB(MCMB) and Discrete Single
 * Bit(DSB). This function will initialize the configuration according
 * to the dataset type supported by the TPDM.
 */
static void __tpdm_enable(struct tpdm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	if (tpdm_has_dsb_dataset(drvdata))
		tpdm_enable_dsb(drvdata);

	CS_LOCK(drvdata->base);
}

static int tpdm_enable(struct coresight_device *csdev, struct perf_event *event,
		       enum cs_mode mode)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock(&drvdata->spinlock);
	if (drvdata->enable) {
		spin_unlock(&drvdata->spinlock);
		return -EBUSY;
	}

	__tpdm_enable(drvdata);
	drvdata->enable = true;
	spin_unlock(&drvdata->spinlock);

	dev_dbg(drvdata->dev, "TPDM tracing enabled\n");
	return 0;
}

static void tpdm_disable_dsb(struct tpdm_drvdata *drvdata)
{
	u32 val;

	/* Set the enable bit of DSB control register to 0 */
	val = readl_relaxed(drvdata->base + TPDM_DSB_CR);
	val &= ~TPDM_DSB_CR_ENA;
	writel_relaxed(val, drvdata->base + TPDM_DSB_CR);
}

/* TPDM disable operations */
static void __tpdm_disable(struct tpdm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	if (tpdm_has_dsb_dataset(drvdata))
		tpdm_disable_dsb(drvdata);

	CS_LOCK(drvdata->base);
}

static void tpdm_disable(struct coresight_device *csdev,
			 struct perf_event *event)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock(&drvdata->spinlock);
	if (!drvdata->enable) {
		spin_unlock(&drvdata->spinlock);
		return;
	}

	__tpdm_disable(drvdata);
	drvdata->enable = false;
	spin_unlock(&drvdata->spinlock);

	dev_dbg(drvdata->dev, "TPDM tracing disabled\n");
}

static const struct coresight_ops_source tpdm_source_ops = {
	.enable		= tpdm_enable,
	.disable	= tpdm_disable,
};

static const struct coresight_ops tpdm_cs_ops = {
	.source_ops	= &tpdm_source_ops,
};

static int tpdm_datasets_setup(struct tpdm_drvdata *drvdata)
{
	u32 pidr;

	/*  Get the datasets present on the TPDM. */
	pidr = readl_relaxed(drvdata->base + CORESIGHT_PERIPHIDR0);
	drvdata->datasets |= pidr & GENMASK(TPDM_DATASETS - 1, 0);

	if (tpdm_has_dsb_dataset(drvdata) && (!drvdata->dsb)) {
		drvdata->dsb = devm_kzalloc(drvdata->dev,
						sizeof(*drvdata->dsb), GFP_KERNEL);
		if (!drvdata->dsb)
			return -ENOMEM;
	}
	tpdm_reset_datasets(drvdata);

	return 0;
}

static ssize_t reset_dataset_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	int ret = 0;
	unsigned long val;
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 0, &val);
	if (ret || val != 1)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	tpdm_reset_datasets(drvdata);
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(reset_dataset);

/*
 * value 1: 64 bits test data
 * value 2: 32 bits test data
 */
static ssize_t integration_test_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	int i, ret = 0;
	unsigned long val;
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val != 1 && val != 2)
		return -EINVAL;

	if (!drvdata->enable)
		return -EINVAL;

	if (val == 1)
		val = ATBCNTRL_VAL_64;
	else
		val = ATBCNTRL_VAL_32;
	CS_UNLOCK(drvdata->base);
	writel_relaxed(0x1, drvdata->base + TPDM_ITCNTRL);

	for (i = 0; i < INTEGRATION_TEST_CYCLE; i++)
		writel_relaxed(val, drvdata->base + TPDM_ITATBCNTRL);

	writel_relaxed(0, drvdata->base + TPDM_ITCNTRL);
	CS_LOCK(drvdata->base);
	return size;
}
static DEVICE_ATTR_WO(integration_test);

static struct attribute *tpdm_attrs[] = {
	&dev_attr_reset_dataset.attr,
	&dev_attr_integration_test.attr,
	NULL,
};

static struct attribute_group tpdm_attr_grp = {
	.attrs = tpdm_attrs,
};

static ssize_t dsb_mode_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%x\n", drvdata->dsb->mode);
}

static ssize_t dsb_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val < 0) ||
			(val & ~TPDM_DSB_MODE_MASK))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->dsb->mode = val & TPDM_DSB_MODE_MASK;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(dsb_mode);

static ssize_t ctrl_idx_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n",
			(unsigned int)drvdata->dsb->edge_ctrl_idx);
}

/*
 * The EDCR registers can include up to 16 32-bit registers, and each
 * one can be configured to control up to 16 edge detections(2 bits
 * control one edge detection). So a total 256 edge detections can be
 * configured. This function provides a way to set the index number of
 * the edge detection which needs to be configured.
 */
static ssize_t ctrl_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val >= TPDM_DSB_MAX_LINES))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->dsb->edge_ctrl_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctrl_idx);

/*
 * This function is used to control the edge detection according
 * to the index number that has been set.
 * "edge_ctrl" should be one of the following values.
 * 0 - Rising edge detection
 * 1 - Falling edge detection
 * 2 - Rising and falling edge detection (toggle detection)
 */
static ssize_t ctrl_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, edge_ctrl;
	int reg;

	if ((kstrtoul(buf, 0, &edge_ctrl)) || (edge_ctrl > 0x2))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/*
	 * There are 2 bit per DSB Edge Control line.
	 * Thus we have 16 lines in a 32bit word.
	 */
	reg = EDCR_TO_WORD_IDX(drvdata->dsb->edge_ctrl_idx);
	val = drvdata->dsb->edge_ctrl[reg];
	val &= ~EDCR_TO_WORD_MASK(drvdata->dsb->edge_ctrl_idx);
	val |= EDCR_TO_WORD_VAL(edge_ctrl, drvdata->dsb->edge_ctrl_idx);
	drvdata->dsb->edge_ctrl[reg] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(ctrl_val);

static ssize_t ctrl_mask_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	u32 set;
	int reg;

	if ((kstrtoul(buf, 0, &val)) || (val & ~1UL))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/*
	 * There is 1 bit per DSB Edge Control Mark line.
	 * Thus we have 32 lines in a 32bit word.
	 */
	reg = EDCMR_TO_WORD_IDX(drvdata->dsb->edge_ctrl_idx);
	set = drvdata->dsb->edge_ctrl_mask[reg];
	if (val)
		set |= BIT(EDCMR_TO_WORD_SHIFT(drvdata->dsb->edge_ctrl_idx));
	else
		set &= ~BIT(EDCMR_TO_WORD_SHIFT(drvdata->dsb->edge_ctrl_idx));
	drvdata->dsb->edge_ctrl_mask[reg] = set;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(ctrl_mask);

static ssize_t enable_ts_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n",
			 (unsigned int)drvdata->dsb->patt_ts);
}

/*
 * value 1: Enable/Disable DSB pattern timestamp
 */
static ssize_t enable_ts_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val & ~1UL))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->dsb->patt_ts = !!val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(enable_ts);

static ssize_t set_type_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n",
			 (unsigned int)drvdata->dsb->patt_type);
}

/*
 * value 1: Set DSB pattern type
 */
static ssize_t set_type_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val & ~1UL))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->dsb->patt_type = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(set_type);

static ssize_t dsb_trig_type_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n",
			 (unsigned int)drvdata->dsb->trig_type);
}

/*
 * Trigger type (boolean):
 * false - Disable trigger type.
 * true  - Enable trigger type.
 */
static ssize_t dsb_trig_type_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val & ~1UL))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		drvdata->dsb->trig_type = true;
	else
		drvdata->dsb->trig_type = false;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_type);

static ssize_t dsb_trig_ts_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n",
			 (unsigned int)drvdata->dsb->trig_ts);
}

/*
 * Trigger timestamp (boolean):
 * false - Disable trigger timestamp.
 * true  - Enable trigger timestamp.
 */
static ssize_t dsb_trig_ts_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if ((kstrtoul(buf, 0, &val)) || (val & ~1UL))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		drvdata->dsb->trig_ts = true;
	else
		drvdata->dsb->trig_ts = false;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_ts);

static struct attribute *tpdm_dsb_edge_attrs[] = {
	&dev_attr_ctrl_idx.attr,
	&dev_attr_ctrl_val.attr,
	&dev_attr_ctrl_mask.attr,
	DSB_EDGE_CTRL_ATTR(0),
	DSB_EDGE_CTRL_ATTR(1),
	DSB_EDGE_CTRL_ATTR(2),
	DSB_EDGE_CTRL_ATTR(3),
	DSB_EDGE_CTRL_ATTR(4),
	DSB_EDGE_CTRL_ATTR(5),
	DSB_EDGE_CTRL_ATTR(6),
	DSB_EDGE_CTRL_ATTR(7),
	DSB_EDGE_CTRL_ATTR(8),
	DSB_EDGE_CTRL_ATTR(9),
	DSB_EDGE_CTRL_ATTR(10),
	DSB_EDGE_CTRL_ATTR(11),
	DSB_EDGE_CTRL_ATTR(12),
	DSB_EDGE_CTRL_ATTR(13),
	DSB_EDGE_CTRL_ATTR(14),
	DSB_EDGE_CTRL_ATTR(15),
	DSB_EDGE_CTRL_MASK_ATTR(0),
	DSB_EDGE_CTRL_MASK_ATTR(1),
	DSB_EDGE_CTRL_MASK_ATTR(2),
	DSB_EDGE_CTRL_MASK_ATTR(3),
	DSB_EDGE_CTRL_MASK_ATTR(4),
	DSB_EDGE_CTRL_MASK_ATTR(5),
	DSB_EDGE_CTRL_MASK_ATTR(6),
	DSB_EDGE_CTRL_MASK_ATTR(7),
	NULL,
};

static struct attribute *tpdm_dsb_trig_patt_attrs[] = {
	DSB_TRIG_PATT_ATTR(0),
	DSB_TRIG_PATT_ATTR(1),
	DSB_TRIG_PATT_ATTR(2),
	DSB_TRIG_PATT_ATTR(3),
	DSB_TRIG_PATT_ATTR(4),
	DSB_TRIG_PATT_ATTR(5),
	DSB_TRIG_PATT_ATTR(6),
	DSB_TRIG_PATT_ATTR(7),
	DSB_TRIG_PATT_MASK_ATTR(0),
	DSB_TRIG_PATT_MASK_ATTR(1),
	DSB_TRIG_PATT_MASK_ATTR(2),
	DSB_TRIG_PATT_MASK_ATTR(3),
	DSB_TRIG_PATT_MASK_ATTR(4),
	DSB_TRIG_PATT_MASK_ATTR(5),
	DSB_TRIG_PATT_MASK_ATTR(6),
	DSB_TRIG_PATT_MASK_ATTR(7),
	NULL,
};

static struct attribute *tpdm_dsb_patt_attrs[] = {
	DSB_PATT_ATTR(0),
	DSB_PATT_ATTR(1),
	DSB_PATT_ATTR(2),
	DSB_PATT_ATTR(3),
	DSB_PATT_ATTR(4),
	DSB_PATT_ATTR(5),
	DSB_PATT_ATTR(6),
	DSB_PATT_ATTR(7),
	DSB_PATT_MASK_ATTR(0),
	DSB_PATT_MASK_ATTR(1),
	DSB_PATT_MASK_ATTR(2),
	DSB_PATT_MASK_ATTR(3),
	DSB_PATT_MASK_ATTR(4),
	DSB_PATT_MASK_ATTR(5),
	DSB_PATT_MASK_ATTR(6),
	DSB_PATT_MASK_ATTR(7),
	&dev_attr_enable_ts.attr,
	&dev_attr_set_type.attr,
	NULL,
};

static struct attribute *tpdm_dsb_msr_attrs[] = {
	DSB_MSR_ATTR(0),
	DSB_MSR_ATTR(1),
	DSB_MSR_ATTR(2),
	DSB_MSR_ATTR(3),
	DSB_MSR_ATTR(4),
	DSB_MSR_ATTR(5),
	DSB_MSR_ATTR(6),
	DSB_MSR_ATTR(7),
	DSB_MSR_ATTR(8),
	DSB_MSR_ATTR(9),
	DSB_MSR_ATTR(10),
	DSB_MSR_ATTR(11),
	DSB_MSR_ATTR(12),
	DSB_MSR_ATTR(13),
	DSB_MSR_ATTR(14),
	DSB_MSR_ATTR(15),
	DSB_MSR_ATTR(16),
	DSB_MSR_ATTR(17),
	DSB_MSR_ATTR(18),
	DSB_MSR_ATTR(19),
	DSB_MSR_ATTR(20),
	DSB_MSR_ATTR(21),
	DSB_MSR_ATTR(22),
	DSB_MSR_ATTR(23),
	DSB_MSR_ATTR(24),
	DSB_MSR_ATTR(25),
	DSB_MSR_ATTR(26),
	DSB_MSR_ATTR(27),
	DSB_MSR_ATTR(28),
	DSB_MSR_ATTR(29),
	DSB_MSR_ATTR(30),
	DSB_MSR_ATTR(31),
	NULL,
};

static struct attribute *tpdm_dsb_attrs[] = {
	&dev_attr_dsb_mode.attr,
	&dev_attr_dsb_trig_ts.attr,
	&dev_attr_dsb_trig_type.attr,
	NULL,
};

static struct attribute_group tpdm_dsb_attr_grp = {
	.attrs = tpdm_dsb_attrs,
	.is_visible = tpdm_dsb_is_visible,
};

static struct attribute_group tpdm_dsb_edge_grp = {
	.attrs = tpdm_dsb_edge_attrs,
	.is_visible = tpdm_dsb_is_visible,
	.name = "dsb_edge",
};

static struct attribute_group tpdm_dsb_trig_patt_grp = {
	.attrs = tpdm_dsb_trig_patt_attrs,
	.is_visible = tpdm_dsb_is_visible,
	.name = "dsb_trig_patt",
};

static struct attribute_group tpdm_dsb_patt_grp = {
	.attrs = tpdm_dsb_patt_attrs,
	.is_visible = tpdm_dsb_is_visible,
	.name = "dsb_patt",
};

static struct attribute_group tpdm_dsb_msr_grp = {
	.attrs = tpdm_dsb_msr_attrs,
	.is_visible = tpdm_dsb_msr_is_visible,
	.name = "dsb_msr",
};

static const struct attribute_group *tpdm_attr_grps[] = {
	&tpdm_attr_grp,
	&tpdm_dsb_attr_grp,
	&tpdm_dsb_edge_grp,
	&tpdm_dsb_trig_patt_grp,
	&tpdm_dsb_patt_grp,
	&tpdm_dsb_msr_grp,
	NULL,
};

static int tpdm_probe(struct amba_device *adev, const struct amba_id *id)
{
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct tpdm_drvdata *drvdata;
	struct coresight_desc desc = { 0 };
	int ret;

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	/* driver data*/
	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	ret = tpdm_datasets_setup(drvdata);
	if (ret)
		return ret;

	if (drvdata && tpdm_has_dsb_dataset(drvdata))
		of_property_read_u32(drvdata->dev->of_node,
			   "qcom,dsb-msrs-num", &drvdata->dsb_msr_num);

	/* Set up coresight component description */
	desc.name = coresight_alloc_device_name(&tpdm_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_TPDM;
	desc.ops = &tpdm_cs_ops;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.access = CSDEV_ACCESS_IOMEM(base);
	desc.groups = tpdm_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	spin_lock_init(&drvdata->spinlock);

	/* Decrease pm refcount when probe is done.*/
	pm_runtime_put(&adev->dev);

	return 0;
}

static void tpdm_remove(struct amba_device *adev)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
}

/*
 * Different TPDM has different periph id.
 * The difference is 0-7 bits' value. So ignore 0-7 bits.
 */
static struct amba_id tpdm_ids[] = {
	{
		.id = 0x000f0e00,
		.mask = 0x000fff00,
	},
	{ 0, 0},
};

static struct amba_driver tpdm_driver = {
	.drv = {
		.name   = "coresight-tpdm",
		.owner	= THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe          = tpdm_probe,
	.id_table	= tpdm_ids,
	.remove		= tpdm_remove,
};

module_amba_driver(tpdm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace, Profiling & Diagnostic Monitor driver");
