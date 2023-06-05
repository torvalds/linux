// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/pid_namespace.h>
#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include "coresight-etm.h"
#include "coresight-priv.h"

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

	pm_runtime_get_sync(dev->parent);
	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	val = etm_readl(drvdata, ETMSR);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(dev->parent);

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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val) {
		spin_lock(&drvdata->spinlock);
		memset(config, 0, sizeof(struct etm_config));
		config->mode = ETM_MODE_EXCLUDE;
		config->trigger_event = ETM_DEFAULT_EVENT_VAL;
		for (i = 0; i < drvdata->nr_addr_cmp; i++) {
			config->addr_type[i] = ETM_ADDR_TYPE_NONE;
		}

		etm_set_default(config);
		etm_release_trace_id(drvdata);
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
	struct etm_config *config = &drvdata->config;

	val = config->mode;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->mode = val & ETM_MODE_ALL;

	if (config->mode & ETM_MODE_EXCLUDE)
		config->enable_ctrl1 |= ETMTECR1_INC_EXC;
	else
		config->enable_ctrl1 &= ~ETMTECR1_INC_EXC;

	if (config->mode & ETM_MODE_CYCACC)
		config->ctrl |= ETMCR_CYC_ACC;
	else
		config->ctrl &= ~ETMCR_CYC_ACC;

	if (config->mode & ETM_MODE_STALL) {
		if (!(drvdata->etmccr & ETMCCR_FIFOFULL)) {
			dev_warn(dev, "stall mode not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		config->ctrl |= ETMCR_STALL_MODE;
	} else
		config->ctrl &= ~ETMCR_STALL_MODE;

	if (config->mode & ETM_MODE_TIMESTAMP) {
		if (!(drvdata->etmccer & ETMCCER_TIMESTAMP)) {
			dev_warn(dev, "timestamp not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		config->ctrl |= ETMCR_TIMESTAMP_EN;
	} else
		config->ctrl &= ~ETMCR_TIMESTAMP_EN;

	if (config->mode & ETM_MODE_CTXID)
		config->ctrl |= ETMCR_CTXID_SIZE;
	else
		config->ctrl &= ~ETMCR_CTXID_SIZE;

	if (config->mode & ETM_MODE_BBROAD)
		config->ctrl |= ETMCR_BRANCH_BROADCAST;
	else
		config->ctrl &= ~ETMCR_BRANCH_BROADCAST;

	if (config->mode & ETM_MODE_RET_STACK)
		config->ctrl |= ETMCR_RETURN_STACK;
	else
		config->ctrl &= ~ETMCR_RETURN_STACK;

	if (config->mode & (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER))
		etm_config_trace_mode(config);

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
	struct etm_config *config = &drvdata->config;

	val = config->trigger_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_event_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->trigger_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(trigger_event);

static ssize_t enable_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->enable_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t enable_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->enable_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(enable_event);

static ssize_t fifofull_level_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->fifofull_level;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t fifofull_level_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->fifofull_level = val;

	return size;
}
static DEVICE_ATTR_RW(fifofull_level);

static ssize_t addr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->addr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

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
	config->addr_idx = val;
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
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	val = config->addr_val[idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	config->addr_val[idx] = val;
	config->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
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
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((config->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       config->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (config->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       config->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val1 = config->addr_val[idx];
	val2 = config->addr_val[idx + 1];
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
	struct etm_config *config = &drvdata->config;

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* Lower address comparator cannot have a higher address value */
	if (val1 > val2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((config->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       config->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (config->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       config->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = val1;
	config->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	config->addr_val[idx + 1] = val2;
	config->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	config->enable_ctrl1 |= (1 << (idx/2));
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
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = config->addr_val[idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = val;
	config->addr_type[idx] = ETM_ADDR_TYPE_START;
	config->startstop_ctrl |= (1 << idx);
	config->enable_ctrl1 |= ETMTECR1_START_STOP;
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
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = config->addr_val[idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = val;
	config->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	config->startstop_ctrl |= (1 << (idx + 16));
	config->enable_ctrl1 |= ETMTECR1_START_STOP;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_stop);

static ssize_t addr_acctype_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->addr_acctype[config->addr_idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->addr_acctype[config->addr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_acctype);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->cntr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

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
	config->cntr_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_idx);

static ssize_t cntr_rld_val_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->cntr_rld_val[config->cntr_idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->cntr_rld_val[config->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_val);

static ssize_t cntr_event_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->cntr_event[config->cntr_idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->cntr_event[config->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_event);

static ssize_t cntr_rld_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->cntr_rld_event[config->cntr_idx];
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->cntr_rld_event[config->cntr_idx] = val & ETM_EVENT_MASK;
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
	struct etm_config *config = &drvdata->config;

	if (!local_read(&drvdata->mode)) {
		spin_lock(&drvdata->spinlock);
		for (i = 0; i < drvdata->nr_cntr; i++)
			ret += sprintf(buf, "counter %d: %x\n",
				       i, config->cntr_val[i]);
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->cntr_val[config->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_val);

static ssize_t seq_12_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_12_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_12_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_12_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_12_event);

static ssize_t seq_21_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_21_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_21_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_21_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_21_event);

static ssize_t seq_23_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_23_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_23_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_23_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_23_event);

static ssize_t seq_31_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_31_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_31_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_31_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_31_event);

static ssize_t seq_32_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_32_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_32_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_32_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_32_event);

static ssize_t seq_13_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->seq_13_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_13_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->seq_13_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_13_event);

static ssize_t seq_curr_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val, flags;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	if (!local_read(&drvdata->mode)) {
		val = config->seq_curr_state;
		goto out;
	}

	pm_runtime_get_sync(dev->parent);
	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	val = (etm_readl(drvdata, ETMSQR) & ETM_SQR_MASK);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(dev->parent);
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
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val > ETM_SEQ_STATE_MAX_VAL)
		return -EINVAL;

	config->seq_curr_state = val;

	return size;
}
static DEVICE_ATTR_RW(seq_curr_state);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->ctxid_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

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
	config->ctxid_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_idx);

static ssize_t ctxid_pid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	val = config->ctxid_pid[config->ctxid_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_pid_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long pid;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	/*
	 * When contextID tracing is enabled the tracers will insert the
	 * value found in the contextID register in the trace stream.  But if
	 * a process is in a namespace the PID of that process as seen from the
	 * namespace won't be what the kernel sees, something that makes the
	 * feature confusing and can potentially leak kernel only information.
	 * As such refuse to use the feature if @current is not in the initial
	 * PID namespace.
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	ret = kstrtoul(buf, 16, &pid);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	config->ctxid_pid[config->ctxid_idx] = pid;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_pid);

static ssize_t ctxid_mask_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	val = config->ctxid_mask;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->ctxid_mask = val;
	return size;
}
static DEVICE_ATTR_RW(ctxid_mask);

static ssize_t sync_freq_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->sync_freq;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t sync_freq_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->sync_freq = val & ETM_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(sync_freq);

static ssize_t timestamp_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	val = config->timestamp_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t timestamp_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *config = &drvdata->config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	config->timestamp_event = val & ETM_EVENT_MASK;
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
	int trace_id;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	trace_id = etm_read_alloc_trace_id(drvdata);
	if (trace_id < 0)
		return trace_id;

	return sysfs_emit(buf, "%#x\n", trace_id);
}
static DEVICE_ATTR_RO(traceid);

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

static struct attribute *coresight_etm_mgmt_attrs[] = {
	coresight_simple_reg32(etmccr, ETMCCR),
	coresight_simple_reg32(etmccer, ETMCCER),
	coresight_simple_reg32(etmscr, ETMSCR),
	coresight_simple_reg32(etmidr, ETMIDR),
	coresight_simple_reg32(etmcr, ETMCR),
	coresight_simple_reg32(etmtraceidr, ETMTRACEIDR),
	coresight_simple_reg32(etmteevr, ETMTEEVR),
	coresight_simple_reg32(etmtssvr, ETMTSSCR),
	coresight_simple_reg32(etmtecr1, ETMTECR1),
	coresight_simple_reg32(etmtecr2, ETMTECR2),
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
