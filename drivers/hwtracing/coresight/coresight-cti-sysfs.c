// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/coresight.h>

#include "coresight-cti.h"

/* basic attributes */
static ssize_t enable_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int enable_req;
	bool enabled, powered;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	enable_req = atomic_read(&drvdata->config.enable_req_count);
	spin_lock(&drvdata->spinlock);
	powered = drvdata->config.hw_powered;
	enabled = drvdata->config.hw_enabled;
	spin_unlock(&drvdata->spinlock);

	if (powered)
		return sprintf(buf, "%d\n", enabled);
	else
		return sprintf(buf, "%d\n", !!enable_req);
}

static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val)
		ret = cti_enable(drvdata->csdev);
	else
		ret = cti_disable(drvdata->csdev);
	if (ret)
		return ret;
	return size;
}
static DEVICE_ATTR_RW(enable);

static ssize_t powered_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	bool powered;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	powered = drvdata->config.hw_powered;
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%d\n", powered);
}
static DEVICE_ATTR_RO(powered);

/* attribute and group sysfs tables. */
static struct attribute *coresight_cti_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_powered.attr,
	NULL,
};

/* register based attributes */

/* macro to access RO registers with power check only (no enable check). */
#define coresight_cti_reg(name, offset)			\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	u32 val = 0;							\
	pm_runtime_get_sync(dev->parent);				\
	spin_lock(&drvdata->spinlock);					\
	if (drvdata->config.hw_powered)					\
		val = readl_relaxed(drvdata->base + offset);		\
	spin_unlock(&drvdata->spinlock);				\
	pm_runtime_put_sync(dev->parent);				\
	return sprintf(buf, "0x%x\n", val);				\
}									\
static DEVICE_ATTR_RO(name)

/* coresight management registers */
coresight_cti_reg(devaff0, CTIDEVAFF0);
coresight_cti_reg(devaff1, CTIDEVAFF1);
coresight_cti_reg(authstatus, CORESIGHT_AUTHSTATUS);
coresight_cti_reg(devarch, CORESIGHT_DEVARCH);
coresight_cti_reg(devid, CORESIGHT_DEVID);
coresight_cti_reg(devtype, CORESIGHT_DEVTYPE);
coresight_cti_reg(pidr0, CORESIGHT_PERIPHIDR0);
coresight_cti_reg(pidr1, CORESIGHT_PERIPHIDR1);
coresight_cti_reg(pidr2, CORESIGHT_PERIPHIDR2);
coresight_cti_reg(pidr3, CORESIGHT_PERIPHIDR3);
coresight_cti_reg(pidr4, CORESIGHT_PERIPHIDR4);

static struct attribute *coresight_cti_mgmt_attrs[] = {
	&dev_attr_devaff0.attr,
	&dev_attr_devaff1.attr,
	&dev_attr_authstatus.attr,
	&dev_attr_devarch.attr,
	&dev_attr_devid.attr,
	&dev_attr_devtype.attr,
	&dev_attr_pidr0.attr,
	&dev_attr_pidr1.attr,
	&dev_attr_pidr2.attr,
	&dev_attr_pidr3.attr,
	&dev_attr_pidr4.attr,
	NULL,
};

/* CTI low level programming registers */

/*
 * Show a simple 32 bit value if enabled and powered.
 * If inaccessible & pcached_val not NULL then show cached value.
 */
static ssize_t cti_reg32_show(struct device *dev, char *buf,
			      u32 *pcached_val, int reg_offset)
{
	u32 val = 0;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	if ((reg_offset >= 0) && cti_active(config)) {
		CS_UNLOCK(drvdata->base);
		val = readl_relaxed(drvdata->base + reg_offset);
		if (pcached_val)
			*pcached_val = val;
		CS_LOCK(drvdata->base);
	} else if (pcached_val) {
		val = *pcached_val;
	}
	spin_unlock(&drvdata->spinlock);
	return sprintf(buf, "%#x\n", val);
}

/*
 * Store a simple 32 bit value.
 * If pcached_val not NULL, then copy to here too,
 * if reg_offset >= 0 then write through if enabled.
 */
static ssize_t cti_reg32_store(struct device *dev, const char *buf,
			       size_t size, u32 *pcached_val, int reg_offset)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* local store */
	if (pcached_val)
		*pcached_val = (u32)val;

	/* write through if offset and enabled */
	if ((reg_offset >= 0) && cti_active(config))
		cti_write_single_reg(drvdata, reg_offset, val);
	spin_unlock(&drvdata->spinlock);
	return size;
}

/* Standard macro for simple rw cti config registers */
#define cti_config_reg32_rw(name, cfgname, offset)			\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	return cti_reg32_show(dev, buf,					\
			      &drvdata->config.cfgname, offset);	\
}									\
									\
static ssize_t name##_store(struct device *dev,				\
			    struct device_attribute *attr,		\
			    const char *buf, size_t size)		\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	return cti_reg32_store(dev, buf, size,				\
			       &drvdata->config.cfgname, offset);	\
}									\
static DEVICE_ATTR_RW(name)

static ssize_t inout_sel_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	u32 val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = (u32)drvdata->config.ctiinout_sel;
	return sprintf(buf, "%d\n", val);
}

static ssize_t inout_sel_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;
	if (val > (CTIINOUTEN_MAX - 1))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->config.ctiinout_sel = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(inout_sel);

static ssize_t inen_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	unsigned long val;
	int index;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	index = drvdata->config.ctiinout_sel;
	val = drvdata->config.ctiinen[index];
	spin_unlock(&drvdata->spinlock);
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t inen_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	unsigned long val;
	int index;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	index = config->ctiinout_sel;
	config->ctiinen[index] = val;

	/* write through if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, CTIINEN(index), val);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(inen);

static ssize_t outen_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	unsigned long val;
	int index;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	index = drvdata->config.ctiinout_sel;
	val = drvdata->config.ctiouten[index];
	spin_unlock(&drvdata->spinlock);
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t outen_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	unsigned long val;
	int index;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	index = config->ctiinout_sel;
	config->ctiouten[index] = val;

	/* write through if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, CTIOUTEN(index), val);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(outen);

static ssize_t intack_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	cti_write_intack(dev, val);
	return size;
}
static DEVICE_ATTR_WO(intack);

cti_config_reg32_rw(gate, ctigate, CTIGATE);
cti_config_reg32_rw(asicctl, asicctl, ASICCTL);
cti_config_reg32_rw(appset, ctiappset, CTIAPPSET);

static ssize_t appclear_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);

	/* a 1'b1 in appclr clears down the same bit in appset*/
	config->ctiappset &= ~val;

	/* write through if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, CTIAPPCLEAR, val);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_WO(appclear);

static ssize_t apppulse_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);

	/* write through if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, CTIAPPPULSE, val);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_WO(apppulse);

coresight_cti_reg(triginstatus, CTITRIGINSTATUS);
coresight_cti_reg(trigoutstatus, CTITRIGOUTSTATUS);
coresight_cti_reg(chinstatus, CTICHINSTATUS);
coresight_cti_reg(choutstatus, CTICHOUTSTATUS);

/*
 * Define CONFIG_CORESIGHT_CTI_INTEGRATION_REGS to enable the access to the
 * integration control registers. Normally only used to investigate connection
 * data.
 */
#ifdef CONFIG_CORESIGHT_CTI_INTEGRATION_REGS

/* macro to access RW registers with power check only (no enable check). */
#define coresight_cti_reg_rw(name, offset)				\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	u32 val = 0;							\
	pm_runtime_get_sync(dev->parent);				\
	spin_lock(&drvdata->spinlock);					\
	if (drvdata->config.hw_powered)					\
		val = readl_relaxed(drvdata->base + offset);		\
	spin_unlock(&drvdata->spinlock);				\
	pm_runtime_put_sync(dev->parent);				\
	return sprintf(buf, "0x%x\n", val);				\
}									\
									\
static ssize_t name##_store(struct device *dev,				\
			    struct device_attribute *attr,		\
			    const char *buf, size_t size)		\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	unsigned long val = 0;						\
	if (kstrtoul(buf, 0, &val))					\
		return -EINVAL;						\
									\
	pm_runtime_get_sync(dev->parent);				\
	spin_lock(&drvdata->spinlock);					\
	if (drvdata->config.hw_powered)					\
		cti_write_single_reg(drvdata, offset, val);		\
	spin_unlock(&drvdata->spinlock);				\
	pm_runtime_put_sync(dev->parent);				\
	return size;							\
}									\
static DEVICE_ATTR_RW(name)

/* macro to access WO registers with power check only (no enable check). */
#define coresight_cti_reg_wo(name, offset)				\
static ssize_t name##_store(struct device *dev,				\
			    struct device_attribute *attr,		\
			    const char *buf, size_t size)		\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	unsigned long val = 0;						\
	if (kstrtoul(buf, 0, &val))					\
		return -EINVAL;						\
									\
	pm_runtime_get_sync(dev->parent);				\
	spin_lock(&drvdata->spinlock);					\
	if (drvdata->config.hw_powered)					\
		cti_write_single_reg(drvdata, offset, val);		\
	spin_unlock(&drvdata->spinlock);				\
	pm_runtime_put_sync(dev->parent);				\
	return size;							\
}									\
static DEVICE_ATTR_WO(name)

coresight_cti_reg_rw(itchout, ITCHOUT);
coresight_cti_reg_rw(ittrigout, ITTRIGOUT);
coresight_cti_reg_rw(itctrl, CORESIGHT_ITCTRL);
coresight_cti_reg_wo(itchinack, ITCHINACK);
coresight_cti_reg_wo(ittriginack, ITTRIGINACK);
coresight_cti_reg(ittrigin, ITTRIGIN);
coresight_cti_reg(itchin, ITCHIN);
coresight_cti_reg(itchoutack, ITCHOUTACK);
coresight_cti_reg(ittrigoutack, ITTRIGOUTACK);

#endif /* CORESIGHT_CTI_INTEGRATION_REGS */

static struct attribute *coresight_cti_regs_attrs[] = {
	&dev_attr_inout_sel.attr,
	&dev_attr_inen.attr,
	&dev_attr_outen.attr,
	&dev_attr_gate.attr,
	&dev_attr_asicctl.attr,
	&dev_attr_intack.attr,
	&dev_attr_appset.attr,
	&dev_attr_appclear.attr,
	&dev_attr_apppulse.attr,
	&dev_attr_triginstatus.attr,
	&dev_attr_trigoutstatus.attr,
	&dev_attr_chinstatus.attr,
	&dev_attr_choutstatus.attr,
#ifdef CONFIG_CORESIGHT_CTI_INTEGRATION_REGS
	&dev_attr_itctrl.attr,
	&dev_attr_ittrigin.attr,
	&dev_attr_itchin.attr,
	&dev_attr_ittrigout.attr,
	&dev_attr_itchout.attr,
	&dev_attr_itchoutack.attr,
	&dev_attr_ittrigoutack.attr,
	&dev_attr_ittriginack.attr,
	&dev_attr_itchinack.attr,
#endif
	NULL,
};

/* sysfs groups */
static const struct attribute_group coresight_cti_group = {
	.attrs = coresight_cti_attrs,
};

static const struct attribute_group coresight_cti_mgmt_group = {
	.attrs = coresight_cti_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group coresight_cti_regs_group = {
	.attrs = coresight_cti_regs_attrs,
	.name = "regs",
};

const struct attribute_group *coresight_cti_groups[] = {
	&coresight_cti_group,
	&coresight_cti_mgmt_group,
	&coresight_cti_regs_group,
	NULL,
};
