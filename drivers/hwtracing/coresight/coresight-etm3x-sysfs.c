// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/pid_namespace.h>
#include <linux/pm_runtime.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include "coresight-etm.h"
#include "coresight-priv.h"

struct etm_readl_cslocked_arg {
	struct etm_drvdata *drvdata;
	u32 off;
	unsigned long *val;
};

static void etm_readl_cslocked_smp_call(void *info)
{
	struct etm_readl_cslocked_arg *arg = info;

	CS_UNLOCK(arg->drvdata->csa.base);
	*arg->val = etm_readl(arg->drvdata, arg->off);
	CS_LOCK(arg->drvdata->csa.base);
}

static int etm_readl_cslocked(struct etm_drvdata *drvdata, u32 off,
			      unsigned long *val)
{
	int ret;
	struct etm_readl_cslocked_arg arg;

	ret = pm_runtime_get_sync(drvdata->csdev->dev.parent);
	if (ret < 0)
		goto out;

	arg.drvdata = drvdata;
	arg.off = off;
	arg.val = val;

	ret = smp_call_function_single(drvdata->cpu,
				       etm_readl_cslocked_smp_call,
				       (void *)&arg, 1);
out:
	pm_runtime_put(drvdata->csdev->dev.parent);
	return ret;
}

static ssize_t nr_addr_cmp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;

	val = caps->nr_addr_cmp;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_addr_cmp);

static ssize_t nr_cntr_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;

	val = caps->nr_cntr;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_cntr);

static ssize_t nr_ctxid_cmp_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->caps.nr_ctxid_cmp;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_ctxid_cmp);

static ssize_t etmsr_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = etm_readl_cslocked(drvdata, ETMSR, &val);
	if (ret)
		return ret;

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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (IS_ERR_OR_NULL(drvdata->csdev) ||
	    coresight_get_mode(drvdata->csdev) == CS_MODE_SYSFS)
		return -EBUSY;

	if (val) {
		spin_lock(&drvdata->spinlock);
		memset(sysfs_config, 0, sizeof(struct etm_config));
		sysfs_config->mode = ETM_MODE_EXCLUDE;
		sysfs_config->trigger_event = ETM_DEFAULT_EVENT_VAL;
		for (i = 0; i < drvdata->caps.nr_addr_cmp; i++) {
			sysfs_config->addr_type[i] = ETM_ADDR_TYPE_NONE;
		}

		etm_set_default(sysfs_config);
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->mode;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	sysfs_config->mode = val & ETM_MODE_ALL;

	if (sysfs_config->mode & ETM_MODE_EXCLUDE)
		sysfs_config->enable_ctrl1 |= ETMTECR1_INC_EXC;
	else
		sysfs_config->enable_ctrl1 &= ~ETMTECR1_INC_EXC;

	if (sysfs_config->mode & ETM_MODE_CYCACC)
		sysfs_config->ctrl |= ETMCR_CYC_ACC;
	else
		sysfs_config->ctrl &= ~ETMCR_CYC_ACC;

	if (sysfs_config->mode & ETM_MODE_STALL) {
		if (!caps->fifofull) {
			dev_warn(dev, "stall mode not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		sysfs_config->ctrl |= ETMCR_STALL_MODE;
	} else
		sysfs_config->ctrl &= ~ETMCR_STALL_MODE;

	if (sysfs_config->mode & ETM_MODE_TIMESTAMP) {
		if (!caps->timestamp) {
			dev_warn(dev, "timestamp not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		sysfs_config->ctrl |= ETMCR_TIMESTAMP_EN;
	} else
		sysfs_config->ctrl &= ~ETMCR_TIMESTAMP_EN;

	if (sysfs_config->mode & ETM_MODE_CTXID)
		sysfs_config->ctrl |= ETMCR_CTXID_SIZE;
	else
		sysfs_config->ctrl &= ~ETMCR_CTXID_SIZE;

	if (sysfs_config->mode & ETM_MODE_BBROAD)
		sysfs_config->ctrl |= ETMCR_BRANCH_BROADCAST;
	else
		sysfs_config->ctrl &= ~ETMCR_BRANCH_BROADCAST;

	if (sysfs_config->mode & ETM_MODE_RET_STACK)
		sysfs_config->ctrl |= ETMCR_RETURN_STACK;
	else
		sysfs_config->ctrl &= ~ETMCR_RETURN_STACK;

	if (sysfs_config->mode & (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER))
		etm_config_trace_mode(sysfs_config);

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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->trigger_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_event_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->trigger_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(trigger_event);

static ssize_t enable_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->enable_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t enable_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->enable_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(enable_event);

static ssize_t fifofull_level_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->fifofull_level;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t fifofull_level_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->fifofull_level = val;

	return size;
}
static DEVICE_ATTR_RW(fifofull_level);

static ssize_t addr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->addr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= caps->nr_addr_cmp)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	sysfs_config->addr_idx = val;
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	val = sysfs_config->addr_val[idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	sysfs_config->addr_val[idx] = val;
	sysfs_config->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       sysfs_config->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       sysfs_config->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val1 = sysfs_config->addr_val[idx];
	val2 = sysfs_config->addr_val[idx + 1];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* Lower address comparator cannot have a higher address value */
	if (val1 > val2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (idx % 2 != 0) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	if (!((sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE &&
	       sysfs_config->addr_type[idx + 1] == ETM_ADDR_TYPE_NONE) ||
	      (sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_RANGE &&
	       sysfs_config->addr_type[idx + 1] == ETM_ADDR_TYPE_RANGE))) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	sysfs_config->addr_val[idx] = val1;
	sysfs_config->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	sysfs_config->addr_val[idx + 1] = val2;
	sysfs_config->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	sysfs_config->enable_ctrl1 |= (1 << (idx/2));
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = sysfs_config->addr_val[idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	sysfs_config->addr_val[idx] = val;
	sysfs_config->addr_type[idx] = ETM_ADDR_TYPE_START;
	sysfs_config->startstop_ctrl |= (1 << idx);
	sysfs_config->enable_ctrl1 |= ETMTECR1_START_STOP;
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = sysfs_config->addr_val[idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = sysfs_config->addr_idx;
	if (!(sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      sysfs_config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	sysfs_config->addr_val[idx] = val;
	sysfs_config->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	sysfs_config->startstop_ctrl |= (1 << (idx + 16));
	sysfs_config->enable_ctrl1 |= ETMTECR1_START_STOP;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_stop);

static ssize_t addr_acctype_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	val = sysfs_config->addr_acctype[sysfs_config->addr_idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	sysfs_config->addr_acctype[sysfs_config->addr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_acctype);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->cntr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= caps->nr_cntr)
		return -EINVAL;
	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	sysfs_config->cntr_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_idx);

static ssize_t cntr_rld_val_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	val = sysfs_config->cntr_rld_val[sysfs_config->cntr_idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	sysfs_config->cntr_rld_val[sysfs_config->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_val);

static ssize_t cntr_event_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	val = sysfs_config->cntr_event[sysfs_config->cntr_idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	sysfs_config->cntr_event[sysfs_config->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_event);

static ssize_t cntr_rld_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	spin_lock(&drvdata->spinlock);
	val = sysfs_config->cntr_rld_event[sysfs_config->cntr_idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	sysfs_config->cntr_rld_event[sysfs_config->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_event);

static ssize_t cntr_val_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int ret;
	u32 val;
	unsigned long val2;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	if (!coresight_get_mode(drvdata->csdev)) {
		spin_lock(&drvdata->spinlock);
		val = sysfs_config->cntr_val[sysfs_config->cntr_idx];
		spin_unlock(&drvdata->spinlock);
	} else {
		ret = etm_readl_cslocked(drvdata, ETMCNTVRn(sysfs_config->cntr_idx), &val2);
		if (ret)
			return ret;
		val = val2;
	}

	return sysfs_emit(buf, "%#x\n", val);
}

static ssize_t cntr_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;
	if (IS_ERR_OR_NULL(drvdata->csdev) ||
	    coresight_get_mode(drvdata->csdev) == CS_MODE_SYSFS)
		return -EBUSY;

	spin_lock(&drvdata->spinlock);
	sysfs_config->cntr_val[sysfs_config->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_val);

static ssize_t seq_12_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_12_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_12_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_12_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_12_event);

static ssize_t seq_21_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_21_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_21_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_21_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_21_event);

static ssize_t seq_23_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_23_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_23_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_23_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_23_event);

static ssize_t seq_31_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_31_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_31_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_31_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_31_event);

static ssize_t seq_32_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_32_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_32_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_32_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_32_event);

static ssize_t seq_13_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->seq_13_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_13_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->seq_13_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_13_event);

static ssize_t seq_curr_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	if (!coresight_get_mode(drvdata->csdev)) {
		val = sysfs_config->seq_curr_state;
		goto out;
	}

	ret = etm_readl_cslocked(drvdata, ETMSQR, &val);
	if (ret)
		return ret;
	val &= ETM_SQR_MASK;
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val > ETM_SEQ_STATE_MAX_VAL)
		return -EINVAL;

	sysfs_config->seq_curr_state = val;

	return size;
}
static DEVICE_ATTR_RW(seq_curr_state);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->ctxid_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	const struct etm_caps *caps = &drvdata->caps;
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val >= caps->nr_ctxid_cmp)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	sysfs_config->ctxid_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_idx);

static ssize_t ctxid_pid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	val = sysfs_config->ctxid_pid[sysfs_config->ctxid_idx];
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
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

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
	sysfs_config->ctxid_pid[sysfs_config->ctxid_idx] = pid;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_pid);

static ssize_t ctxid_mask_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	val = sysfs_config->ctxid_mask;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->ctxid_mask = val;
	return size;
}
static DEVICE_ATTR_RW(ctxid_mask);

static ssize_t sync_freq_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->sync_freq;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t sync_freq_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->sync_freq = val & ETM_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(sync_freq);

static ssize_t timestamp_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	val = sysfs_config->timestamp_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t timestamp_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etm_config *sysfs_config = &drvdata->sysfs_config;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	sysfs_config->timestamp_event = val & ETM_EVENT_MASK;
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
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	int trace_id = coresight_etm_get_trace_id(drvdata->csdev, CS_MODE_SYSFS, NULL);

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
