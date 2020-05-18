// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/coresight.h>

#include "coresight-cti.h"

/*
 * Declare the number of static declared attribute groups
 * Value includes groups + NULL value at end of table.
 */
#define CORESIGHT_CTI_STATIC_GROUPS_MAX 5

/*
 * List of trigger signal type names. Match the constants declared in
 * include\dt-bindings\arm\coresight-cti-dt.h
 */
static const char * const sig_type_names[] = {
	"genio",	/* GEN_IO */
	"intreq",	/* GEN_INTREQ */
	"intack",	/* GEN_INTACK */
	"haltreq",	/* GEN_HALTREQ */
	"restartreq",	/* GEN_RESTARTREQ */
	"pe_edbgreq",	/* PE_EDBGREQ */
	"pe_dbgrestart",/* PE_DBGRESTART */
	"pe_ctiirq",	/* PE_CTIIRQ */
	"pe_pmuirq",	/* PE_PMUIRQ */
	"pe_dbgtrigger",/* PE_DBGTRIGGER */
	"etm_extout",	/* ETM_EXTOUT */
	"etm_extin",	/* ETM_EXTIN */
	"snk_full",	/* SNK_FULL */
	"snk_acqcomp",	/* SNK_ACQCOMP */
	"snk_flushcomp",/* SNK_FLUSHCOMP */
	"snk_flushin",	/* SNK_FLUSHIN */
	"snk_trigin",	/* SNK_TRIGIN */
	"stm_asyncout",	/* STM_ASYNCOUT */
	"stm_tout_spte",/* STM_TOUT_SPTE */
	"stm_tout_sw",	/* STM_TOUT_SW */
	"stm_tout_hete",/* STM_TOUT_HETE */
	"stm_hwevent",	/* STM_HWEVENT */
	"ela_tstart",	/* ELA_TSTART */
	"ela_tstop",	/* ELA_TSTOP */
	"ela_dbgreq",	/* ELA_DBGREQ */
};

/* Show function pointer used in the connections dynamic declared attributes*/
typedef ssize_t (*p_show_fn)(struct device *dev, struct device_attribute *attr,
			     char *buf);

/* Connection attribute types */
enum cti_conn_attr_type {
	CTI_CON_ATTR_NAME,
	CTI_CON_ATTR_TRIGIN_SIG,
	CTI_CON_ATTR_TRIGOUT_SIG,
	CTI_CON_ATTR_TRIGIN_TYPES,
	CTI_CON_ATTR_TRIGOUT_TYPES,
	CTI_CON_ATTR_MAX,
};

/* Names for the connection attributes */
static const char * const con_attr_names[CTI_CON_ATTR_MAX] = {
	"name",
	"in_signals",
	"out_signals",
	"in_types",
	"out_types",
};

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

static ssize_t ctmid_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%d\n", drvdata->ctidev.ctm_id);
}
static DEVICE_ATTR_RO(ctmid);

static ssize_t nr_trigger_cons_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%d\n", drvdata->ctidev.nr_trig_con);
}
static DEVICE_ATTR_RO(nr_trigger_cons);

/* attribute and group sysfs tables. */
static struct attribute *coresight_cti_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_powered.attr,
	&dev_attr_ctmid.attr,
	&dev_attr_nr_trigger_cons.attr,
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

/* CTI channel x-trigger programming */
static int
cti_trig_op_parse(struct device *dev, enum cti_chan_op op,
		  enum cti_trig_dir dir, const char *buf, size_t size)
{
	u32 chan_idx;
	u32 trig_idx;
	int items, err = -EINVAL;

	/* extract chan idx and trigger idx */
	items = sscanf(buf, "%d %d", &chan_idx, &trig_idx);
	if (items == 2) {
		err = cti_channel_trig_op(dev, op, dir, chan_idx, trig_idx);
		if (!err)
			err = size;
	}
	return err;
}

static ssize_t trigin_attach_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	return cti_trig_op_parse(dev, CTI_CHAN_ATTACH, CTI_TRIG_IN,
				 buf, size);
}
static DEVICE_ATTR_WO(trigin_attach);

static ssize_t trigin_detach_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	return cti_trig_op_parse(dev, CTI_CHAN_DETACH, CTI_TRIG_IN,
				 buf, size);
}
static DEVICE_ATTR_WO(trigin_detach);

static ssize_t trigout_attach_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	return cti_trig_op_parse(dev, CTI_CHAN_ATTACH, CTI_TRIG_OUT,
				 buf, size);
}
static DEVICE_ATTR_WO(trigout_attach);

static ssize_t trigout_detach_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	return cti_trig_op_parse(dev, CTI_CHAN_DETACH, CTI_TRIG_OUT,
				 buf, size);
}
static DEVICE_ATTR_WO(trigout_detach);


static ssize_t chan_gate_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	int err = 0, channel = 0;

	if (kstrtoint(buf, 0, &channel))
		return -EINVAL;

	err = cti_channel_gate_op(dev, CTI_GATE_CHAN_ENABLE, channel);
	return err ? err : size;
}

static ssize_t chan_gate_enable_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	unsigned long ctigate_bitmask = cfg->ctigate;
	int size = 0;

	if (cfg->ctigate == 0)
		size = sprintf(buf, "\n");
	else
		size = bitmap_print_to_pagebuf(true, buf, &ctigate_bitmask,
					       cfg->nr_ctm_channels);
	return size;
}
static DEVICE_ATTR_RW(chan_gate_enable);

static ssize_t chan_gate_disable_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	int err = 0, channel = 0;

	if (kstrtoint(buf, 0, &channel))
		return -EINVAL;

	err = cti_channel_gate_op(dev, CTI_GATE_CHAN_DISABLE, channel);
	return err ? err : size;
}
static DEVICE_ATTR_WO(chan_gate_disable);

static int
chan_op_parse(struct device *dev, enum cti_chan_set_op op, const char *buf)
{
	int err = 0, channel = 0;

	if (kstrtoint(buf, 0, &channel))
		return -EINVAL;

	err = cti_channel_setop(dev, op, channel);
	return err;

}

static ssize_t chan_set_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int err = chan_op_parse(dev, CTI_CHAN_SET, buf);

	return err ? err : size;
}
static DEVICE_ATTR_WO(chan_set);

static ssize_t chan_clear_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int err = chan_op_parse(dev, CTI_CHAN_CLR, buf);

	return err ? err : size;
}
static DEVICE_ATTR_WO(chan_clear);

static ssize_t chan_pulse_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int err = chan_op_parse(dev, CTI_CHAN_PULSE, buf);

	return err ? err : size;
}
static DEVICE_ATTR_WO(chan_pulse);

static ssize_t trig_filter_enable_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	u32 val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->config.trig_filter_enable;
	spin_unlock(&drvdata->spinlock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t trig_filter_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->config.trig_filter_enable = !!val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(trig_filter_enable);

static ssize_t trigout_filtered_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	int size = 0, nr_trig_max = cfg->nr_trig_max;
	unsigned long mask = cfg->trig_out_filter;

	if (mask)
		size = bitmap_print_to_pagebuf(true, buf, &mask, nr_trig_max);
	return size;
}
static DEVICE_ATTR_RO(trigout_filtered);

/* clear all xtrigger / channel programming */
static ssize_t chan_xtrigs_reset_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	int i;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);

	/* clear the CTI trigger / channel programming registers */
	for (i = 0; i < config->nr_trig_max; i++) {
		config->ctiinen[i] = 0;
		config->ctiouten[i] = 0;
	}

	/* clear the other regs */
	config->ctigate = GENMASK(config->nr_ctm_channels - 1, 0);
	config->asicctl = 0;
	config->ctiappset = 0;
	config->ctiinout_sel = 0;
	config->xtrig_rchan_sel = 0;

	/* if enabled then write through */
	if (cti_active(config))
		cti_write_all_hw_regs(drvdata);

	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_WO(chan_xtrigs_reset);

/*
 * Write to select a channel to view, read to display the
 * cross triggers for the selected channel.
 */
static ssize_t chan_xtrigs_sel_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;
	if (val > (drvdata->config.nr_ctm_channels - 1))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->config.xtrig_rchan_sel = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}

static ssize_t chan_xtrigs_sel_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->config.xtrig_rchan_sel;
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%ld\n", val);
}
static DEVICE_ATTR_RW(chan_xtrigs_sel);

static ssize_t chan_xtrigs_in_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	int used = 0, reg_idx;
	int nr_trig_max = drvdata->config.nr_trig_max;
	u32 chan_mask = BIT(cfg->xtrig_rchan_sel);

	for (reg_idx = 0; reg_idx < nr_trig_max; reg_idx++) {
		if (chan_mask & cfg->ctiinen[reg_idx])
			used += sprintf(buf + used, "%d ", reg_idx);
	}

	used += sprintf(buf + used, "\n");
	return used;
}
static DEVICE_ATTR_RO(chan_xtrigs_in);

static ssize_t chan_xtrigs_out_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	int used = 0, reg_idx;
	int nr_trig_max = drvdata->config.nr_trig_max;
	u32 chan_mask = BIT(cfg->xtrig_rchan_sel);

	for (reg_idx = 0; reg_idx < nr_trig_max; reg_idx++) {
		if (chan_mask & cfg->ctiouten[reg_idx])
			used += sprintf(buf + used, "%d ", reg_idx);
	}

	used += sprintf(buf + used, "\n");
	return used;
}
static DEVICE_ATTR_RO(chan_xtrigs_out);

static ssize_t print_chan_list(struct device *dev,
			       char *buf, bool inuse)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;
	int size, i;
	unsigned long inuse_bits = 0, chan_mask;

	/* scan regs to get bitmap of channels in use. */
	spin_lock(&drvdata->spinlock);
	for (i = 0; i < config->nr_trig_max; i++) {
		inuse_bits |= config->ctiinen[i];
		inuse_bits |= config->ctiouten[i];
	}
	spin_unlock(&drvdata->spinlock);

	/* inverse bits if printing free channels */
	if (!inuse)
		inuse_bits = ~inuse_bits;

	/* list of channels, or 'none' */
	chan_mask = GENMASK(config->nr_ctm_channels - 1, 0);
	if (inuse_bits & chan_mask)
		size = bitmap_print_to_pagebuf(true, buf, &inuse_bits,
					       config->nr_ctm_channels);
	else
		size = sprintf(buf, "\n");
	return size;
}

static ssize_t chan_inuse_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return print_chan_list(dev, buf, true);
}
static DEVICE_ATTR_RO(chan_inuse);

static ssize_t chan_free_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return print_chan_list(dev, buf, false);
}
static DEVICE_ATTR_RO(chan_free);

static struct attribute *coresight_cti_channel_attrs[] = {
	&dev_attr_trigin_attach.attr,
	&dev_attr_trigin_detach.attr,
	&dev_attr_trigout_attach.attr,
	&dev_attr_trigout_detach.attr,
	&dev_attr_trig_filter_enable.attr,
	&dev_attr_trigout_filtered.attr,
	&dev_attr_chan_gate_enable.attr,
	&dev_attr_chan_gate_disable.attr,
	&dev_attr_chan_set.attr,
	&dev_attr_chan_clear.attr,
	&dev_attr_chan_pulse.attr,
	&dev_attr_chan_inuse.attr,
	&dev_attr_chan_free.attr,
	&dev_attr_chan_xtrigs_sel.attr,
	&dev_attr_chan_xtrigs_in.attr,
	&dev_attr_chan_xtrigs_out.attr,
	&dev_attr_chan_xtrigs_reset.attr,
	NULL,
};

/* Create the connections trigger groups and attrs dynamically */
/*
 * Each connection has dynamic group triggers<N> + name, trigin/out sigs/types
 * attributes, + each device has static nr_trigger_cons giving the number
 * of groups. e.g. in sysfs:-
 * /cti_<name>/triggers0
 * /cti_<name>/triggers1
 * /cti_<name>/nr_trigger_cons
 * where nr_trigger_cons = 2
 */
static ssize_t con_name_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct dev_ext_attribute *ext_attr =
		container_of(attr, struct dev_ext_attribute, attr);
	struct cti_trig_con *con = (struct cti_trig_con *)ext_attr->var;

	return sprintf(buf, "%s\n", con->con_dev_name);
}

static ssize_t trigin_sig_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct dev_ext_attribute *ext_attr =
		container_of(attr, struct dev_ext_attribute, attr);
	struct cti_trig_con *con = (struct cti_trig_con *)ext_attr->var;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	unsigned long mask = con->con_in->used_mask;

	return bitmap_print_to_pagebuf(true, buf, &mask, cfg->nr_trig_max);
}

static ssize_t trigout_sig_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct dev_ext_attribute *ext_attr =
		container_of(attr, struct dev_ext_attribute, attr);
	struct cti_trig_con *con = (struct cti_trig_con *)ext_attr->var;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *cfg = &drvdata->config;
	unsigned long mask = con->con_out->used_mask;

	return bitmap_print_to_pagebuf(true, buf, &mask, cfg->nr_trig_max);
}

/* convert a sig type id to a name */
static const char *
cti_sig_type_name(struct cti_trig_con *con, int used_count, bool in)
{
	int idx = 0;
	struct cti_trig_grp *grp = in ? con->con_in : con->con_out;

	if (used_count < grp->nr_sigs)
		idx = grp->sig_types[used_count];
	return sig_type_names[idx];
}

static ssize_t trigin_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct dev_ext_attribute *ext_attr =
		container_of(attr, struct dev_ext_attribute, attr);
	struct cti_trig_con *con = (struct cti_trig_con *)ext_attr->var;
	int sig_idx, used = 0;
	const char *name;

	for (sig_idx = 0; sig_idx < con->con_in->nr_sigs; sig_idx++) {
		name = cti_sig_type_name(con, sig_idx, true);
		used += sprintf(buf + used, "%s ", name);
	}
	used += sprintf(buf + used, "\n");
	return used;
}

static ssize_t trigout_type_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct dev_ext_attribute *ext_attr =
		container_of(attr, struct dev_ext_attribute, attr);
	struct cti_trig_con *con = (struct cti_trig_con *)ext_attr->var;
	int sig_idx, used = 0;
	const char *name;

	for (sig_idx = 0; sig_idx < con->con_out->nr_sigs; sig_idx++) {
		name = cti_sig_type_name(con, sig_idx, false);
		used += sprintf(buf + used, "%s ", name);
	}
	used += sprintf(buf + used, "\n");
	return used;
}

/*
 * Array of show function names declared above to allow selection
 * for the connection attributes
 */
static p_show_fn show_fns[CTI_CON_ATTR_MAX] = {
	con_name_show,
	trigin_sig_show,
	trigout_sig_show,
	trigin_type_show,
	trigout_type_show,
};

static int cti_create_con_sysfs_attr(struct device *dev,
				     struct cti_trig_con *con,
				     enum cti_conn_attr_type attr_type,
				     int attr_idx)
{
	struct dev_ext_attribute *eattr;
	char *name;

	eattr = devm_kzalloc(dev, sizeof(struct dev_ext_attribute),
				    GFP_KERNEL);
	if (eattr) {
		name = devm_kstrdup(dev, con_attr_names[attr_type],
				    GFP_KERNEL);
		if (name) {
			/* fill out the underlying attribute struct */
			eattr->attr.attr.name = name;
			eattr->attr.attr.mode = 0444;

			/* now the device_attribute struct */
			eattr->attr.show = show_fns[attr_type];
		} else {
			return -ENOMEM;
		}
	} else {
		return -ENOMEM;
	}
	eattr->var = con;
	con->con_attrs[attr_idx] = &eattr->attr.attr;
	return 0;
}

static struct attribute_group *
cti_create_con_sysfs_group(struct device *dev, struct cti_device *ctidev,
			   int con_idx, struct cti_trig_con *tc)
{
	struct attribute_group *group = NULL;
	int grp_idx;

	group = devm_kzalloc(dev, sizeof(struct attribute_group), GFP_KERNEL);
	if (!group)
		return NULL;

	group->name = devm_kasprintf(dev, GFP_KERNEL, "triggers%d", con_idx);
	if (!group->name)
		return NULL;

	grp_idx = con_idx + CORESIGHT_CTI_STATIC_GROUPS_MAX - 1;
	ctidev->con_groups[grp_idx] = group;
	tc->attr_group = group;
	return group;
}

/* create a triggers connection group and the attributes for that group */
static int cti_create_con_attr_set(struct device *dev, int con_idx,
				   struct cti_device *ctidev,
				   struct cti_trig_con *tc)
{
	struct attribute_group *attr_group = NULL;
	int attr_idx = 0;
	int err = -ENOMEM;

	attr_group = cti_create_con_sysfs_group(dev, ctidev, con_idx, tc);
	if (!attr_group)
		return -ENOMEM;

	/* allocate NULL terminated array of attributes */
	tc->con_attrs = devm_kcalloc(dev, CTI_CON_ATTR_MAX + 1,
				     sizeof(struct attribute *), GFP_KERNEL);
	if (!tc->con_attrs)
		return -ENOMEM;

	err = cti_create_con_sysfs_attr(dev, tc, CTI_CON_ATTR_NAME,
					attr_idx++);
	if (err)
		return err;

	if (tc->con_in->nr_sigs > 0) {
		err = cti_create_con_sysfs_attr(dev, tc,
						CTI_CON_ATTR_TRIGIN_SIG,
						attr_idx++);
		if (err)
			return err;

		err = cti_create_con_sysfs_attr(dev, tc,
						CTI_CON_ATTR_TRIGIN_TYPES,
						attr_idx++);
		if (err)
			return err;
	}

	if (tc->con_out->nr_sigs > 0) {
		err = cti_create_con_sysfs_attr(dev, tc,
						CTI_CON_ATTR_TRIGOUT_SIG,
						attr_idx++);
		if (err)
			return err;

		err = cti_create_con_sysfs_attr(dev, tc,
						CTI_CON_ATTR_TRIGOUT_TYPES,
						attr_idx++);
		if (err)
			return err;
	}
	attr_group->attrs = tc->con_attrs;
	return 0;
}

/* create the array of group pointers for the CTI sysfs groups */
static int cti_create_cons_groups(struct device *dev, struct cti_device *ctidev)
{
	int nr_groups;

	/* nr groups = dynamic + static + NULL terminator */
	nr_groups = ctidev->nr_trig_con + CORESIGHT_CTI_STATIC_GROUPS_MAX;
	ctidev->con_groups = devm_kcalloc(dev, nr_groups,
					  sizeof(struct attribute_group *),
					  GFP_KERNEL);
	if (!ctidev->con_groups)
		return -ENOMEM;
	return 0;
}

int cti_create_cons_sysfs(struct device *dev, struct cti_drvdata *drvdata)
{
	struct cti_device *ctidev = &drvdata->ctidev;
	int err, con_idx = 0, i;
	struct cti_trig_con *tc;

	err = cti_create_cons_groups(dev, ctidev);
	if (err)
		return err;

	/* populate first locations with the static set of groups */
	for (i = 0; i < (CORESIGHT_CTI_STATIC_GROUPS_MAX - 1); i++)
		ctidev->con_groups[i] = coresight_cti_groups[i];

	/* add dynamic set for each connection */
	list_for_each_entry(tc, &ctidev->trig_cons, node) {
		err = cti_create_con_attr_set(dev, con_idx++, ctidev, tc);
		if (err)
			break;
	}
	return err;
}

/* attribute and group sysfs tables. */
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

static const struct attribute_group coresight_cti_channels_group = {
	.attrs = coresight_cti_channel_attrs,
	.name = "channels",
};

const struct attribute_group *
coresight_cti_groups[CORESIGHT_CTI_STATIC_GROUPS_MAX] = {
	&coresight_cti_group,
	&coresight_cti_mgmt_group,
	&coresight_cti_regs_group,
	&coresight_cti_channels_group,
	NULL,
};
