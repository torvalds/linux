// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/coresight.h>
#include <soc/qcom/memory_dump.h>

#include "coresight-priv.h"
#include "coresight-common.h"

#define dbgui_writel(drvdata, val, off) \
			 __raw_writel((val), drvdata->base + off)
#define dbgui_readl(drvdata, off)	__raw_readl(drvdata->base + off)

#define DBGUI_LOCK(drvdata)						\
do {									\
	mb(); /* ensure configuration take effect before we lock it */	\
	dbgui_writel(drvdata, 0x0, CORESIGHT_LAR);			\
} while (0)

#define DBGUI_UNLOCK(drvdata)						\
do {									\
	dbgui_writel(drvdata, CORESIGHT_UNLOCK, CORESIGHT_LAR);	\
	mb(); /* ensure unlock take effect before we configure */	\
} while (0)

/* DBGUI registers */
#define DBGUI_SECURE		(0x000)
#define DBGUI_CTL		(0x004)
#define DBGUI_CTL_MASK		(0x008)
#define DBGUI_SWTRIG		(0x00C)
#define DBGUI_STATUS		(0x010)
#define DBGUI_HWE_MASK		(0x014)
#define DBGUI_CTR_VAL		(0x018)
#define DBGUI_CTR_EN		(0x01C)
#define DBGUI_NUM_REGS_RD	(0x020)
#define DBGUI_ATB_REG		(0x024)

#define DBGUI_ADDRn(drvdata, n)	(drvdata->addr_offset + 4*n)
#define DBGUI_DATAn(drvdata, n)	(drvdata->data_offset + 4*n)

#define DBGUI_TRIG_MASK		0xF0001
#define DBGUI_MAX_ADDR_VAL		64
#define DBGUI_TS_VALID			BIT(15)
#define DBGUI_ATB_TRACE_EN		BIT(0)
#define DBGUI_TIMER_CTR_OVERRIDE	BIT(1)
#define DBGUI_TIMER_CTR_EN		BIT(0)

/* ATID for DBGUI */
#define APB_ATID		50
#define AHB_ATID		52

enum dbgui_trig_type {
	DBGUI_TRIG_SW =	BIT(0),
	DBGUI_TRIG_TIMER =	BIT(16),
	DBGUI_TRIG_HWE =	BIT(17),
	DBGUI_TRIG_WDOG =	BIT(18),
	DBGUI_TRIG_CTI =	BIT(19),
};

struct dbgui_drvdata {
	void __iomem		*base;
	bool			enable;
	uint32_t		addr_offset;
	uint32_t		data_offset;
	uint32_t		size;
	struct device		*dev;
	struct coresight_device	*csdev;
	struct clk		*clk;
	struct mutex		mutex;
	uint32_t		trig_mask;
	bool			capture_enable;
	bool			ts_enable;
	bool			timer_override_enable;
	bool			handoff_enable;
	uint32_t		nr_apb_regs;
	uint32_t		nr_ahb_regs;
	uint32_t		hwe_mask;
	uint32_t		addr_idx;
	uint32_t		timeout_val;
	uint32_t		addr_val[DBGUI_MAX_ADDR_VAL];
	uint32_t		data_val[DBGUI_MAX_ADDR_VAL];
	struct  msm_dump_data	reg_data;
};

static struct dbgui_drvdata *dbgui_drvdata;

static void dbgui_enable_atb_trace(struct dbgui_drvdata *drvdata)
{
	uint32_t reg;

	reg = dbgui_readl(drvdata, DBGUI_ATB_REG);
	reg |= DBGUI_ATB_TRACE_EN | APB_ATID << 8 | AHB_ATID << 1;
	dbgui_writel(drvdata, reg, DBGUI_ATB_REG);
}

static void dbgui_disable_atb_trace(struct dbgui_drvdata *drvdata)
{
	uint32_t reg;

	reg = dbgui_readl(drvdata, DBGUI_ATB_REG);
	reg &= ~DBGUI_ATB_TRACE_EN;
	dbgui_writel(drvdata, reg, DBGUI_ATB_REG);
}

static void dbgui_enable_timestamp(struct dbgui_drvdata *drvdata)
{
	uint32_t reg;

	reg = dbgui_readl(drvdata, DBGUI_ATB_REG);
	reg |= DBGUI_TS_VALID;
	dbgui_writel(drvdata, reg, DBGUI_ATB_REG);
}

static void dbgui_disable_timestamp(struct dbgui_drvdata *drvdata)
{
	uint32_t reg;

	reg = dbgui_readl(drvdata, DBGUI_ATB_REG);
	reg &= ~DBGUI_TS_VALID;
	dbgui_writel(drvdata, reg, DBGUI_ATB_REG);
}

static void dbgui_wait_for_pending_actions(struct dbgui_drvdata *drvdata)
{
	int count;
	uint32_t reg_val;

	for (count = TIMEOUT_US; reg_val =
			dbgui_readl(drvdata, DBGUI_STATUS),
			BMVAL(reg_val, 4, 7) != 0
			&& BVAL(reg_val, 0) != 0 && count > 0; count--)
		udelay(1);

	WARN(count == 0,
		"timeout while waiting for pending action: STATUS %#x\n",
		dbgui_readl(drvdata, DBGUI_STATUS));
}

static void __dbgui_capture_enable(struct dbgui_drvdata *drvdata)
{
	int i;
	uint32_t reg_val;

	DBGUI_UNLOCK(drvdata);

	dbgui_wait_for_pending_actions(drvdata);
	dbgui_writel(drvdata, 0x1, DBGUI_SECURE);
	dbgui_writel(drvdata, 0x1, DBGUI_CTL);

	reg_val = dbgui_readl(drvdata, DBGUI_NUM_REGS_RD);
	reg_val &= ~0xFF;
	reg_val |= (drvdata->nr_apb_regs | drvdata->nr_ahb_regs << 8);
	dbgui_writel(drvdata, reg_val, DBGUI_NUM_REGS_RD);

	for (i = 0; i < drvdata->size; i++) {
		if (drvdata->addr_val[i])
			dbgui_writel(drvdata, drvdata->addr_val[i],
				     DBGUI_ADDRn(drvdata, i));
	}

	if (!(drvdata->trig_mask & DBGUI_TRIG_TIMER) && drvdata->timeout_val) {
		dbgui_writel(drvdata, drvdata->timeout_val, DBGUI_CTR_VAL);

		reg_val = dbgui_readl(drvdata, DBGUI_CTR_EN);
		if (drvdata->timer_override_enable)
			reg_val |= DBGUI_TIMER_CTR_OVERRIDE;

		reg_val |= DBGUI_TIMER_CTR_EN;
		dbgui_writel(drvdata, reg_val, DBGUI_CTR_EN);
	}

	if (!(drvdata->trig_mask & DBGUI_TRIG_HWE))
		dbgui_writel(drvdata, drvdata->hwe_mask, DBGUI_HWE_MASK);

	dbgui_writel(drvdata, drvdata->trig_mask, DBGUI_CTL_MASK);

	DBGUI_LOCK(drvdata);
};

static int dbgui_capture_enable(struct dbgui_drvdata *drvdata)
{
	int ret = 0;

	mutex_lock(&drvdata->mutex);
	if (drvdata->capture_enable)
		goto out;

	if (drvdata->trig_mask == DBGUI_TRIG_MASK) {
		ret = -EINVAL;
		goto out;
	}

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		goto out;

	if (!drvdata->handoff_enable)
		__dbgui_capture_enable(drvdata);
	drvdata->capture_enable = true;
	mutex_unlock(&drvdata->mutex);

	dev_info(drvdata->dev, "DebugUI capture enabled\n");
	return 0;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static void __dbgui_capture_disable(struct dbgui_drvdata *drvdata)
{
	DBGUI_UNLOCK(drvdata);

	dbgui_wait_for_pending_actions(drvdata);

	/* mask all the triggers */
	dbgui_writel(drvdata, DBGUI_TRIG_MASK, DBGUI_CTL_MASK);

	DBGUI_LOCK(drvdata);
}

static int dbgui_capture_disable(struct dbgui_drvdata *drvdata)
{
	int ret = 0;

	mutex_lock(&drvdata->mutex);
	if (!drvdata->capture_enable)
		goto out;

	/* don't allow capture disable while its enabled as a trace source */
	if (drvdata->enable) {
		ret = -EPERM;
		goto out;
	}

	__dbgui_capture_disable(drvdata);
	clk_disable_unprepare(drvdata->clk);
	drvdata->capture_enable = false;
	mutex_unlock(&drvdata->mutex);

	dev_info(drvdata->dev, "DebugUI capture disabled\n");
	return 0;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static int __dbgui_enable(struct dbgui_drvdata *drvdata)
{
	DBGUI_UNLOCK(drvdata);

	dbgui_enable_atb_trace(drvdata);
	if (drvdata->ts_enable)
		dbgui_enable_timestamp(drvdata);

	DBGUI_LOCK(drvdata);
	return 0;
}

static int dbgui_enable(struct coresight_device *csdev,
			struct perf_event *event, u32 mode)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	mutex_lock(&drvdata->mutex);

	if (!drvdata->capture_enable) {
		mutex_unlock(&drvdata->mutex);
		return -EPERM;
	}

	__dbgui_enable(drvdata);
	drvdata->enable = true;
	mutex_unlock(&drvdata->mutex);

	dev_info(drvdata->dev, "DebugUI tracing enabled\n");
	return 0;
}

static void __dbgui_disable(struct dbgui_drvdata *drvdata)
{
	DBGUI_UNLOCK(drvdata);

	dbgui_disable_atb_trace(drvdata);
	if (drvdata->ts_enable)
		dbgui_disable_timestamp(drvdata);

	DBGUI_LOCK(drvdata);
}

static void dbgui_disable(struct coresight_device *csdev,
			  struct perf_event *event)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	mutex_lock(&drvdata->mutex);
	__dbgui_disable(drvdata);
	drvdata->enable = false;
	mutex_unlock(&drvdata->mutex);

	dev_info(drvdata->dev, "DebugUI tracing disabled\n");
}

static int dbgui_trace_id(struct coresight_device *csdev)
{
	return 0;
}

static const struct coresight_ops_source dbgui_source_ops = {
	.trace_id = dbgui_trace_id,
	.enable = dbgui_enable,
	.disable = dbgui_disable,
};

/* DebugUI may already be configured for capture, so retrieve current state */
static void dbgui_handoff(struct dbgui_drvdata *drvdata)
{
	uint32_t val;
	int i;

	drvdata->handoff_enable = true;

	drvdata->trig_mask = dbgui_readl(drvdata, DBGUI_CTL_MASK);
	drvdata->hwe_mask = dbgui_readl(drvdata, DBGUI_HWE_MASK);
	drvdata->timeout_val = dbgui_readl(drvdata, DBGUI_CTR_VAL);

	val = dbgui_readl(drvdata, DBGUI_NUM_REGS_RD);
	drvdata->nr_ahb_regs = (val >> 8) & 0xF;
	drvdata->nr_apb_regs = val & 0xF;

	val = dbgui_readl(drvdata, DBGUI_ATB_REG);
	if (val & DBGUI_TS_VALID)
		drvdata->ts_enable = true;

	val = dbgui_readl(drvdata, DBGUI_CTR_EN);
	if (val & DBGUI_TIMER_CTR_OVERRIDE)
		drvdata->timer_override_enable = true;

	for (i = 0; i < drvdata->size; i++)
		drvdata->addr_val[i] = dbgui_readl(drvdata,
						   DBGUI_ADDRn(drvdata, i));

	if (drvdata->trig_mask != DBGUI_TRIG_MASK)
		dbgui_capture_enable(drvdata);

	drvdata->handoff_enable = false;
}

static const struct coresight_ops dbgui_cs_ops = {
	.source_ops = &dbgui_source_ops,
};

static ssize_t trig_mask_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->trig_mask = val & DBGUI_TRIG_MASK;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t trig_mask_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->trig_mask);
};
static DEVICE_ATTR_RW(trig_mask);

static ssize_t timer_override_enable_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf,
						 size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->timer_override_enable = val ? true : false;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t timer_override_enable_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			 drvdata->timer_override_enable);
};
static DEVICE_ATTR_RW(timer_override_enable);

static ssize_t ts_enable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->ts_enable = val ? true : false;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t ts_enable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->ts_enable);
};
static DEVICE_ATTR_RW(ts_enable);

static ssize_t hwe_mask_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->hwe_mask = val;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t hwe_mask_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->hwe_mask);
};
static DEVICE_ATTR_RW(hwe_mask);

static ssize_t sw_trig_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (!val)
		return 0;

	mutex_lock(&drvdata->mutex);
	if (!drvdata->capture_enable) {
		mutex_unlock(&drvdata->mutex);
		return -EINVAL;
	}

	dbgui_wait_for_pending_actions(drvdata);
	DBGUI_UNLOCK(drvdata);

	/* clear status register and free the sequencer */
	dbgui_writel(drvdata, 0x1, DBGUI_CTL);

	/* fire a software trigger */
	dbgui_writel(drvdata, 0x1, DBGUI_SWTRIG);

	DBGUI_LOCK(drvdata);
	mutex_unlock(&drvdata->mutex);

	return size;
}
static DEVICE_ATTR_WO(sw_trig);

static ssize_t nr_ahb_regs_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val > drvdata->size)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->nr_ahb_regs = val;

	/*
	 * Please make sure nr_ahb_regs + nr_apb_regs isn't greater than
	 * drvdata->size. If sum is greater than size, The last setting
	 * of nr_ahb_regs or nr_apb_regs takes high priority.
	 */
	if (drvdata->nr_apb_regs + drvdata->nr_ahb_regs > drvdata->size)
		drvdata->nr_apb_regs = drvdata->size -
					drvdata->nr_ahb_regs;

	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t nr_ahb_regs_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->nr_ahb_regs);
};
static DEVICE_ATTR_RW(nr_ahb_regs);

static ssize_t nr_apb_regs_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val > drvdata->size)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->nr_apb_regs = val;

	if (drvdata->nr_apb_regs + drvdata->nr_ahb_regs > drvdata->size)
		drvdata->nr_ahb_regs = drvdata->size -
					drvdata->nr_apb_regs;

	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t nr_apb_regs_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->nr_apb_regs);
};
static DEVICE_ATTR_RW(nr_apb_regs);

static ssize_t size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->size);
};
static DEVICE_ATTR_RO(size);

static ssize_t timeout_val_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->timeout_val = val;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t timeout_val_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->timeout_val);
};
static DEVICE_ATTR_RW(timeout_val);

static ssize_t addr_idx_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val >= drvdata->size)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->addr_idx = val;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t addr_idx_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->addr_idx);
};
static DEVICE_ATTR_RW(addr_idx);

static ssize_t addr_val_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	unsigned long val;
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->addr_val[drvdata->addr_idx] = val;
	mutex_unlock(&drvdata->mutex);

	return size;
}

static ssize_t addr_val_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t len = 0;
	int i;

	mutex_lock(&drvdata->mutex);
	for (i = 0; i < drvdata->size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "[%02d]:0x%08x%s\n",
				 i, drvdata->addr_val[i],
				 drvdata->addr_idx == i ?
				 " *" : "");
	mutex_unlock(&drvdata->mutex);

	return len;
};
static DEVICE_ATTR_RW(addr_val);

static ssize_t data_val_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t len = 0;
	int i;
	uint32_t val, trig_mask;

	if (!drvdata->capture_enable)
		return 0;

	dbgui_wait_for_pending_actions(drvdata);

	mutex_lock(&drvdata->mutex);

	DBGUI_UNLOCK(drvdata);

	/*
	 * If the timer trigger is enabled, data might change while we read it.
	 * We mask all the trggers here to avoid this.
	 */
	trig_mask = dbgui_readl(drvdata, DBGUI_CTL_MASK);
	dbgui_writel(drvdata, DBGUI_TRIG_MASK, DBGUI_CTL_MASK);

	for (i = 0; i < drvdata->size; i++) {
		val = dbgui_readl(drvdata, DBGUI_DATAn(drvdata, i));
		drvdata->data_val[i] = val;
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "[%02d]:0x%08x\n",
				 i, drvdata->data_val[i]);
	}
	dbgui_writel(drvdata, trig_mask, DBGUI_CTL_MASK);

	DBGUI_LOCK(drvdata);

	mutex_unlock(&drvdata->mutex);

	return len;
};
static DEVICE_ATTR_RO(data_val);

static ssize_t capture_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);
	uint32_t ret;
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val)
		ret = dbgui_capture_enable(drvdata);
	else
		ret = dbgui_capture_disable(drvdata);

	if (ret)
		return ret;
	return size;
}

static ssize_t capture_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dbgui_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", drvdata->capture_enable);
};
static DEVICE_ATTR_RW(capture_enable);

static struct attribute *dbgui_attrs[] = {
	&dev_attr_sw_trig.attr,
	&dev_attr_trig_mask.attr,
	&dev_attr_capture_enable.attr,
	&dev_attr_ts_enable.attr,
	&dev_attr_hwe_mask.attr,
	&dev_attr_timer_override_enable.attr,
	&dev_attr_size.attr,
	&dev_attr_nr_ahb_regs.attr,
	&dev_attr_nr_apb_regs.attr,
	&dev_attr_timeout_val.attr,
	&dev_attr_addr_idx.attr,
	&dev_attr_addr_val.attr,
	&dev_attr_data_val.attr,
	NULL,
};

static struct attribute_group dbgui_attr_grp = {
	.attrs = dbgui_attrs,
};

static const struct attribute_group *dbgui_attr_grps[] = {
	&dbgui_attr_grp,
	NULL,
};

static bool coresight_authstatus_enabled(void __iomem *addr)
{
	int ret;
	unsigned int auth_val;

	if (!addr)
		return false;

	auth_val = readl_relaxed(addr + CORESIGHT_AUTHSTATUS);

	if ((BMVAL(auth_val, 0, 1) == 0x2) ||
		(BMVAL(auth_val, 2, 3) == 0x2) ||
		(BMVAL(auth_val, 4, 5) == 0x2) ||
		(BMVAL(auth_val, 6, 7) == 0x2))
		ret = false;
	else
		ret = true;

	return ret;
}

static int dbgui_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata;
	struct dbgui_drvdata *drvdata;
	struct resource *res;
	struct coresight_desc desc = { 0 };

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &pdev->dev;
	platform_set_drvdata(pdev, drvdata);
	mutex_init(&drvdata->mutex);

	drvdata->clk = devm_clk_get(dev, "apb_pclk");
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbgui-base");
	if (!res) {
		dev_info(dev, "DBGUI base not specified\n");
		return -ENODEV;
	}

	drvdata->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!drvdata->base)
		return -ENOMEM;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	if (!coresight_authstatus_enabled(drvdata->base))
		goto err;

	clk_disable_unprepare(drvdata->clk);

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,dbgui-addr-offset",
			&drvdata->addr_offset);
	if (ret)
		return -EINVAL;

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,dbgui-data-offset",
			&drvdata->data_offset);
	if (ret)
		return -EINVAL;

	if (drvdata->addr_offset >= resource_size(res)
			|| drvdata->data_offset >= resource_size(res)) {
		dev_err(dev, "Invalid address or data offset\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,dbgui-size",
			&drvdata->size);
	if (ret || drvdata->size > DBGUI_MAX_ADDR_VAL)
		return -EINVAL;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	dbgui_handoff(drvdata);
	clk_disable_unprepare(drvdata->clk);
	/*
	 * To provide addr_offset, data_offset and size via a global variable.
	 * NOTE: Only single dbgui device is supported now.
	 */
	if (of_property_read_string(dev->of_node, "coresight-name", &desc.name))
		desc.name = "coresight-dbgui";

	dbgui_drvdata = drvdata;
	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &dbgui_cs_ops;
	desc.pdata = pdev->dev.platform_data;
	desc.dev = &pdev->dev;
	desc.groups = dbgui_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	dev_info(dev, "DebugUI initializaed\n");
	return 0;
err:
	clk_disable_unprepare(drvdata->clk);
	return -EPERM;
}

static int dbgui_remove(struct platform_device *pdev)
{
	struct dbgui_drvdata *drvdata = platform_get_drvdata(pdev);

	coresight_unregister(drvdata->csdev);
	return 0;
}

static const struct of_device_id dbgui_match[] = {
	{.compatible = "qcom,coresight-dbgui"},
	{}
};

static struct platform_driver dbgui_driver = {
	.probe          = dbgui_probe,
	.remove         = dbgui_remove,
	.driver = {
		.name   = "coresight-dbgui",
		.of_match_table = dbgui_match,
	},
};

static int __init dbgui_init(void)
{
	return platform_driver_register(&dbgui_driver);
}
module_init(dbgui_init);

static void __exit dbgui_exit(void)
{
	return platform_driver_unregister(&dbgui_driver);
}
module_exit(dbgui_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight DebugUI driver");
