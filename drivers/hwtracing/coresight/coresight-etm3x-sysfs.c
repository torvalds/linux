/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include "coresight-etm.h"

static ssize_t nr_addr_cmp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_addr_cmp;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_addr_cmp);

static ssize_t nr_cntr_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_cntr;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_cntr);

static ssize_t nr_ctxid_cmp_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_ctxid_cmp;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_ctxid_cmp);

static ssize_t etmsr_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	unsigned long flags, val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	pm_runtime_get_sync(drvdata->dev);
	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	val = etm_readl(drvdata, ETMSR);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(drvdata->dev);

	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(etmsr);

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	int i, ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val) {
		spin_lock(&drvdata->spinlock);
		drvdata->mode = ETM_MODE_EXCLUDE;
		drvdata->ctrl = 0x0;
		drvdata->trigger_event = ETM_DEFAULT_EVENT_VAL;
		drvdata->startstop_ctrl = 0x0;
		drvdata->addr_idx = 0x0;
		for (i = 0; i < drvdata->nr_addr_cmp; i++) {
			drvdata->addr_val[i] = 0x0;
			drvdata->addr_acctype[i] = 0x0;
			drvdata->addr_type[i] = ETM_ADDR_TYPE_NONE;
		}
		drvdata->cntr_idx = 0x0;

		etm_set_default(drvdata);
		spin_unlock(&drvdata->spinlock);
	}

	return size;
}
static DEVICE_ATTR_WO(reset);

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->mode;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->mode = val & ETM_MODE_ALL;

	if (drvdata->mode & ETM_MODE_EXCLUDE)
		drvdata->enable_ctrl1 |= ETMTECR1_INC_EXC;
	else
		drvdata->enable_ctrl1 &= ~ETMTECR1_INC_EXC;

	if (drvdata->mode & ETM_MODE_CYCACC)
		drvdata->ctrl |= ETMCR_CYC_ACC;
	else
		drvdata->ctrl &= ~ETMCR_CYC_ACC;

	if (drvdata->mode & ETM_MODE_STALL) {
		if (!(drvdata->etmccr & ETMCCR_FIFOFULL)) {
			dev_warn(drvdata->dev, "stall mode not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		drvdata->ctrl |= ETMCR_STALL_MODE;
	 } else
		drvdata->ctrl &= ~ETMCR_STALL_MODE;

	if (drvdata->mode & ETM_MODE_TIMESTAMP) {
		if (!(drvdata->etmccer & ETMCCER_TIMESTAMP)) {
			dev_warn(drvdata->dev, "timestamp not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		drvdata->ctrl |= ETMCR_TIMESTAMP_EN;
	} else
		drvdata->ctrl &= ~ETMCR_TIMESTAMP_EN;

	if (drvdata->mode & ETM_MODE_CTXID)
		drvdata->ctrl |= ETMCR_CTXID_SIZE;
	else
		drvdata->ctrl &= ~ETMCR_CTXID_SIZE;
	spin_unlock(&drvdata->spinlock);

	return size;

err_unlock:
	spin_unlock(&drvdata->spinlock);
	return ret;
}
static DEVICE_ATTR_RW(mode);

static ssize_t trigger_event_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->trigger_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_event_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->trigger_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(trigger_event);

static ssize_t enable_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->enable_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t enable_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->enable_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(enable_event);

static ssize_t fifofull_level_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->fifofull_level;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t fifofull_level_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->fifofull_level = val;

	return size;
}
static DEVICE_ATTR_RW(fifofull_level);

static ssize_t addr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->addr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= drvdata->nr_addr_cmp)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->addr_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_idx);

static ssize_t addr_single_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 idx;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	val = drvdata->addr_val[idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_single_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	u8 idx;
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_single);

static ssize_t addr_range_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u8 idx;
	unsigned long val1, val2;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       drvdata->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (drvdata->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       drvdata->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val1 = drvdata->addr_val[idx];
	val2 = drvdata->addr_val[idx + 1];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx %#lx\n", val1, val2);
}

static ssize_t addr_range_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	u8 idx;
	unsigned long val1, val2;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* Lower address comparator cannot have a higher address value */
	if (val1 > val2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       drvdata->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (drvdata->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       drvdata->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = val1;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	drvdata->addr_val[idx + 1] = val2;
	drvdata->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	drvdata->enable_ctrl1 |= (1 << (idx/2));
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_range);

static ssize_t addr_start_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u8 idx;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = drvdata->addr_val[idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_start_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 idx;
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_START;
	drvdata->startstop_ctrl |= (1 << idx);
	drvdata->enable_ctrl1 |= BIT(25);
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_start);

static ssize_t addr_stop_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	u8 idx;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = drvdata->addr_val[idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_stop_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	drvdata->startstop_ctrl |= (1 << (idx + 16));
	drvdata->enable_ctrl1 |= ETMTECR1_START_STOP;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_stop);

static ssize_t addr_acctype_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->addr_acctype[drvdata->addr_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_acctype_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->addr_acctype[drvdata->addr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_acctype);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->cntr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= drvdata->nr_cntr)
		return -EINVAL;
	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->cntr_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_idx);

static ssize_t cntr_rld_val_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_rld_val[drvdata->cntr_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_rld_val_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_rld_val[drvdata->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_val);

static ssize_t cntr_event_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_event[drvdata->cntr_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_event_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_event[drvdata->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_event);

static ssize_t cntr_rld_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_rld_event[drvdata->cntr_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_rld_event_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_rld_event[drvdata->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_event);

static ssize_t cntr_val_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int i, ret = 0;
	u32 val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!drvdata->enable) {
		spin_lock(&drvdata->spinlock);
		for (i = 0; i < drvdata->nr_cntr; i++)
			ret += sprintf(buf, "counter %d: %x\n",
				       i, drvdata->cntr_val[i]);
		spin_unlock(&drvdata->spinlock);
		return ret;
	}

	for (i = 0; i < drvdata->nr_cntr; i++) {
		val = etm_readl(drvdata, ETMCNTVRn(i));
		ret += sprintf(buf, "counter %d: %x\n", i, val);
	}

	return ret;
}

static ssize_t cntr_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_val[drvdata->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_val);

static ssize_t seq_12_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_12_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_12_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_12_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_12_event);

static ssize_t seq_21_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_21_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_21_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_21_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_21_event);

static ssize_t seq_23_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_23_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_23_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_23_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_23_event);

static ssize_t seq_31_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_31_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_31_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_31_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_31_event);

static ssize_t seq_32_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_32_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_32_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_32_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_32_event);

static ssize_t seq_13_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_13_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_13_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_13_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_13_event);

static ssize_t seq_curr_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val, flags;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!drvdata->enable) {
		val = drvdata->seq_curr_state;
		goto out;
	}

	pm_runtime_get_sync(drvdata->dev);
	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	val = (etm_readl(drvdata, ETMSQR) & ETM_SQR_MASK);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(drvdata->dev);
out:
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_curr_state_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val > ETM_SEQ_STATE_MAX_VAL)
		return -EINVAL;

	drvdata->seq_curr_state = val;

	return size;
}
static DEVICE_ATTR_RW(seq_curr_state);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ctxid_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= drvdata->nr_ctxid_cmp)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->ctxid_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_idx);

static ssize_t ctxid_pid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->ctxid_vpid[drvdata->ctxid_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_pid_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long vpid, pid;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &vpid);
	if (ret)
		return ret;

	pid = coresight_vpid_to_pid(vpid);

	spin_lock(&drvdata->spinlock);
	drvdata->ctxid_pid[drvdata->ctxid_idx] = pid;
	drvdata->ctxid_vpid[drvdata->ctxid_idx] = vpid;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_pid);

static ssize_t ctxid_mask_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ctxid_mask;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->ctxid_mask = val;
	return size;
}
static DEVICE_ATTR_RW(ctxid_mask);

static ssize_t sync_freq_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->sync_freq;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t sync_freq_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->sync_freq = val & ETM_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(sync_freq);

static ssize_t timestamp_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->timestamp_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t timestamp_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->timestamp_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(timestamp_event);

static ssize_t cpu_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->cpu;
	return scnprintf(buf, PAGE_SIZE, "%d\n", val);

}
static DEVICE_ATTR_RO(cpu);

static ssize_t traceid_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = etm_get_trace_id(drvdata);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t traceid_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->traceid = val & ETM_TRACEID_MASK;
	return size;
}
static DEVICE_ATTR_RW(traceid);

static struct attribute *coresight_etm_attrs[] = {
	&dev_attr_nr_addr_cmp.attr,
	&dev_attr_nr_cntr.attr,
	&dev_attr_nr_ctxid_cmp.attr,
	&dev_attr_etmsr.attr,
	&dev_attr_reset.attr,
	&dev_attr_mode.attr,
	&dev_attr_trigger_event.attr,
	&dev_attr_enable_event.attr,
	&dev_attr_fifofull_level.attr,
	&dev_attr_addr_idx.attr,
	&dev_attr_addr_single.attr,
	&dev_attr_addr_range.attr,
	&dev_attr_addr_start.attr,
	&dev_attr_addr_stop.attr,
	&dev_attr_addr_acctype.attr,
	&dev_attr_cntr_idx.attr,
	&dev_attr_cntr_rld_val.attr,
	&dev_attr_cntr_event.attr,
	&dev_attr_cntr_rld_event.attr,
	&dev_attr_cntr_val.attr,
	&dev_attr_seq_12_event.attr,
	&dev_attr_seq_21_event.attr,
	&dev_attr_seq_23_event.attr,
	&dev_attr_seq_31_event.attr,
	&dev_attr_seq_32_event.attr,
	&dev_attr_seq_13_event.attr,
	&dev_attr_seq_curr_state.attr,
	&dev_attr_ctxid_idx.attr,
	&dev_attr_ctxid_pid.attr,
	&dev_attr_ctxid_mask.attr,
	&dev_attr_sync_freq.attr,
	&dev_attr_timestamp_event.attr,
	&dev_attr_traceid.attr,
	&dev_attr_cpu.attr,
	NULL,
};

#define coresight_simple_func(name, offset)                             \
static ssize_t name##_show(struct device *_dev,                         \
			   struct device_attribute *attr, char *buf)    \
{                                                                       \
	struct etm_drvdata *drvdata = dev_get_drvdata(_dev->parent);    \
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",                      \
			 readl_relaxed(drvdata->base + offset));        \
}                                                                       \
DEVICE_ATTR_RO(name)

coresight_simple_func(etmccr, ETMCCR);
coresight_simple_func(etmccer, ETMCCER);
coresight_simple_func(etmscr, ETMSCR);
coresight_simple_func(etmidr, ETMIDR);
coresight_simple_func(etmcr, ETMCR);
coresight_simple_func(etmtraceidr, ETMTRACEIDR);
coresight_simple_func(etmteevr, ETMTEEVR);
coresight_simple_func(etmtssvr, ETMTSSCR);
coresight_simple_func(etmtecr1, ETMTECR1);
coresight_simple_func(etmtecr2, ETMTECR2);

static struct attribute *coresight_etm_mgmt_attrs[] = {
	&dev_attr_etmccr.attr,
	&dev_attr_etmccer.attr,
	&dev_attr_etmscr.attr,
	&dev_attr_etmidr.attr,
	&dev_attr_etmcr.attr,
	&dev_attr_etmtraceidr.attr,
	&dev_attr_etmteevr.attr,
	&dev_attr_etmtssvr.attr,
	&dev_attr_etmtecr1.attr,
	&dev_attr_etmtecr2.attr,
	NULL,
};

static const struct attribute_group coresight_etm_group = {
	.attrs = coresight_etm_attrs,
};

static const struct attribute_group coresight_etm_mgmt_group = {
	.attrs = coresight_etm_mgmt_attrs,
	.name = "mgmt",
};

const struct attribute_group *coresight_etm_groups[] = {
	&coresight_etm_group,
	&coresight_etm_mgmt_group,
	NULL,
};
