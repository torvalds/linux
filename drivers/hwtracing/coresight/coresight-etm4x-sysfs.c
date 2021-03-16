// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/pid_namespace.h>
#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include "coresight-etm4x.h"
#include "coresight-priv.h"

static int etm4_set_mode_exclude(struct etmv4_drvdata *drvdata, bool exclude)
{
	u8 idx;
	struct etmv4_config *config = &drvdata->config;

	idx = config->addr_idx;

	/*
	 * TRCACATRn.TYPE bit[1:0]: type of comparison
	 * the trace unit performs
	 */
	if (BMVAL(config->addr_acc[idx], 0, 1) == ETM_INSTR_ADDR) {
		if (idx % 2 != 0)
			return -EINVAL;

		/*
		 * We are performing instruction address comparison. Set the
		 * relevant bit of ViewInst Include/Exclude Control register
		 * for corresponding address comparator pair.
		 */
		if (config->addr_type[idx] != ETM_ADDR_TYPE_RANGE ||
		    config->addr_type[idx + 1] != ETM_ADDR_TYPE_RANGE)
			return -EINVAL;

		if (exclude == true) {
			/*
			 * Set exclude bit and unset the include bit
			 * corresponding to comparator pair
			 */
			config->viiectlr |= BIT(idx / 2 + 16);
			config->viiectlr &= ~BIT(idx / 2);
		} else {
			/*
			 * Set include bit and unset exclude bit
			 * corresponding to comparator pair
			 */
			config->viiectlr |= BIT(idx / 2);
			config->viiectlr &= ~BIT(idx / 2 + 16);
		}
	}
	return 0;
}

static ssize_t nr_pe_cmp_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_pe_cmp;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_pe_cmp);

static ssize_t nr_addr_cmp_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_addr_cmp;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_addr_cmp);

static ssize_t nr_cntr_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_cntr;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_cntr);

static ssize_t nr_ext_inp_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_ext_inp;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_ext_inp);

static ssize_t numcidc_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->numcidc;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(numcidc);

static ssize_t numvmidc_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->numvmidc;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(numvmidc);

static ssize_t nrseqstate_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nrseqstate;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nrseqstate);

static ssize_t nr_resource_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_resource;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_resource);

static ssize_t nr_ss_cmp_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->nr_ss_cmp;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(nr_ss_cmp);

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	int i;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		config->mode = 0x0;

	/* Disable data tracing: do not trace load and store data transfers */
	config->mode &= ~(ETM_MODE_LOAD | ETM_MODE_STORE);
	config->cfg &= ~(BIT(1) | BIT(2));

	/* Disable data value and data address tracing */
	config->mode &= ~(ETM_MODE_DATA_TRACE_ADDR |
			   ETM_MODE_DATA_TRACE_VAL);
	config->cfg &= ~(BIT(16) | BIT(17));

	/* Disable all events tracing */
	config->eventctrl0 = 0x0;
	config->eventctrl1 = 0x0;

	/* Disable timestamp event */
	config->ts_ctrl = 0x0;

	/* Disable stalling */
	config->stall_ctrl = 0x0;

	/* Reset trace synchronization period  to 2^8 = 256 bytes*/
	if (drvdata->syncpr == false)
		config->syncfreq = 0x8;

	/*
	 * Enable ViewInst to trace everything with start-stop logic in
	 * started state. ARM recommends start-stop logic is set before
	 * each trace run.
	 */
	config->vinst_ctrl = BIT(0);
	if (drvdata->nr_addr_cmp > 0) {
		config->mode |= ETM_MODE_VIEWINST_STARTSTOP;
		/* SSSTATUS, bit[9] */
		config->vinst_ctrl |= BIT(9);
	}

	/* No address range filtering for ViewInst */
	config->viiectlr = 0x0;

	/* No start-stop filtering for ViewInst */
	config->vissctlr = 0x0;
	config->vipcssctlr = 0x0;

	/* Disable seq events */
	for (i = 0; i < drvdata->nrseqstate-1; i++)
		config->seq_ctrl[i] = 0x0;
	config->seq_rst = 0x0;
	config->seq_state = 0x0;

	/* Disable external input events */
	config->ext_inp = 0x0;

	config->cntr_idx = 0x0;
	for (i = 0; i < drvdata->nr_cntr; i++) {
		config->cntrldvr[i] = 0x0;
		config->cntr_ctrl[i] = 0x0;
		config->cntr_val[i] = 0x0;
	}

	config->res_idx = 0x0;
	for (i = 2; i < 2 * drvdata->nr_resource; i++)
		config->res_ctrl[i] = 0x0;

	config->ss_idx = 0x0;
	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		config->ss_ctrl[i] = 0x0;
		config->ss_pe_cmp[i] = 0x0;
	}

	config->addr_idx = 0x0;
	for (i = 0; i < drvdata->nr_addr_cmp * 2; i++) {
		config->addr_val[i] = 0x0;
		config->addr_acc[i] = 0x0;
		config->addr_type[i] = ETM_ADDR_TYPE_NONE;
	}

	config->ctxid_idx = 0x0;
	for (i = 0; i < drvdata->numcidc; i++)
		config->ctxid_pid[i] = 0x0;

	config->ctxid_mask0 = 0x0;
	config->ctxid_mask1 = 0x0;

	config->vmid_idx = 0x0;
	for (i = 0; i < drvdata->numvmidc; i++)
		config->vmid_val[i] = 0x0;
	config->vmid_mask0 = 0x0;
	config->vmid_mask1 = 0x0;

	drvdata->trcid = drvdata->cpu + 1;

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(reset);

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->mode;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	unsigned long val, mode;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	config->mode = val & ETMv4_MODE_ALL;

	if (drvdata->instrp0 == true) {
		/* start by clearing instruction P0 field */
		config->cfg  &= ~(BIT(1) | BIT(2));
		if (config->mode & ETM_MODE_LOAD)
			/* 0b01 Trace load instructions as P0 instructions */
			config->cfg  |= BIT(1);
		if (config->mode & ETM_MODE_STORE)
			/* 0b10 Trace store instructions as P0 instructions */
			config->cfg  |= BIT(2);
		if (config->mode & ETM_MODE_LOAD_STORE)
			/*
			 * 0b11 Trace load and store instructions
			 * as P0 instructions
			 */
			config->cfg  |= BIT(1) | BIT(2);
	}

	/* bit[3], Branch broadcast mode */
	if ((config->mode & ETM_MODE_BB) && (drvdata->trcbb == true))
		config->cfg |= BIT(3);
	else
		config->cfg &= ~BIT(3);

	/* bit[4], Cycle counting instruction trace bit */
	if ((config->mode & ETMv4_MODE_CYCACC) &&
		(drvdata->trccci == true))
		config->cfg |= BIT(4);
	else
		config->cfg &= ~BIT(4);

	/* bit[6], Context ID tracing bit */
	if ((config->mode & ETMv4_MODE_CTXID) && (drvdata->ctxid_size))
		config->cfg |= BIT(6);
	else
		config->cfg &= ~BIT(6);

	if ((config->mode & ETM_MODE_VMID) && (drvdata->vmid_size))
		config->cfg |= BIT(7);
	else
		config->cfg &= ~BIT(7);

	/* bits[10:8], Conditional instruction tracing bit */
	mode = ETM_MODE_COND(config->mode);
	if (drvdata->trccond == true) {
		config->cfg &= ~(BIT(8) | BIT(9) | BIT(10));
		config->cfg |= mode << 8;
	}

	/* bit[11], Global timestamp tracing bit */
	if ((config->mode & ETMv4_MODE_TIMESTAMP) && (drvdata->ts_size))
		config->cfg |= BIT(11);
	else
		config->cfg &= ~BIT(11);

	/* bit[12], Return stack enable bit */
	if ((config->mode & ETM_MODE_RETURNSTACK) &&
					(drvdata->retstack == true))
		config->cfg |= BIT(12);
	else
		config->cfg &= ~BIT(12);

	/* bits[14:13], Q element enable field */
	mode = ETM_MODE_QELEM(config->mode);
	/* start by clearing QE bits */
	config->cfg &= ~(BIT(13) | BIT(14));
	/* if supported, Q elements with instruction counts are enabled */
	if ((mode & BIT(0)) && (drvdata->q_support & BIT(0)))
		config->cfg |= BIT(13);
	/*
	 * if supported, Q elements with and without instruction
	 * counts are enabled
	 */
	if ((mode & BIT(1)) && (drvdata->q_support & BIT(1)))
		config->cfg |= BIT(14);

	/* bit[11], AMBA Trace Bus (ATB) trigger enable bit */
	if ((config->mode & ETM_MODE_ATB_TRIGGER) &&
	    (drvdata->atbtrig == true))
		config->eventctrl1 |= BIT(11);
	else
		config->eventctrl1 &= ~BIT(11);

	/* bit[12], Low-power state behavior override bit */
	if ((config->mode & ETM_MODE_LPOVERRIDE) &&
	    (drvdata->lpoverride == true))
		config->eventctrl1 |= BIT(12);
	else
		config->eventctrl1 &= ~BIT(12);

	/* bit[8], Instruction stall bit */
	if ((config->mode & ETM_MODE_ISTALL_EN) && (drvdata->stallctl == true))
		config->stall_ctrl |= BIT(8);
	else
		config->stall_ctrl &= ~BIT(8);

	/* bit[10], Prioritize instruction trace bit */
	if (config->mode & ETM_MODE_INSTPRIO)
		config->stall_ctrl |= BIT(10);
	else
		config->stall_ctrl &= ~BIT(10);

	/* bit[13], Trace overflow prevention bit */
	if ((config->mode & ETM_MODE_NOOVERFLOW) &&
		(drvdata->nooverflow == true))
		config->stall_ctrl |= BIT(13);
	else
		config->stall_ctrl &= ~BIT(13);

	/* bit[9] Start/stop logic control bit */
	if (config->mode & ETM_MODE_VIEWINST_STARTSTOP)
		config->vinst_ctrl |= BIT(9);
	else
		config->vinst_ctrl &= ~BIT(9);

	/* bit[10], Whether a trace unit must trace a Reset exception */
	if (config->mode & ETM_MODE_TRACE_RESET)
		config->vinst_ctrl |= BIT(10);
	else
		config->vinst_ctrl &= ~BIT(10);

	/* bit[11], Whether a trace unit must trace a system error exception */
	if ((config->mode & ETM_MODE_TRACE_ERR) &&
		(drvdata->trc_error == true))
		config->vinst_ctrl |= BIT(11);
	else
		config->vinst_ctrl &= ~BIT(11);

	if (config->mode & (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER))
		etm4_config_trace_mode(config);

	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(mode);

static ssize_t pe_show(struct device *dev,
		       struct device_attribute *attr,
		       char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->pe_sel;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t pe_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val > drvdata->nr_pe) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	config->pe_sel = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(pe);

static ssize_t event_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->eventctrl0;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	switch (drvdata->nr_event) {
	case 0x0:
		/* EVENT0, bits[7:0] */
		config->eventctrl0 = val & 0xFF;
		break;
	case 0x1:
		 /* EVENT1, bits[15:8] */
		config->eventctrl0 = val & 0xFFFF;
		break;
	case 0x2:
		/* EVENT2, bits[23:16] */
		config->eventctrl0 = val & 0xFFFFFF;
		break;
	case 0x3:
		/* EVENT3, bits[31:24] */
		config->eventctrl0 = val;
		break;
	default:
		break;
	}
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(event);

static ssize_t event_instren_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = BMVAL(config->eventctrl1, 0, 3);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_instren_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* start by clearing all instruction event enable bits */
	config->eventctrl1 &= ~(BIT(0) | BIT(1) | BIT(2) | BIT(3));
	switch (drvdata->nr_event) {
	case 0x0:
		/* generate Event element for event 1 */
		config->eventctrl1 |= val & BIT(1);
		break;
	case 0x1:
		/* generate Event element for event 1 and 2 */
		config->eventctrl1 |= val & (BIT(0) | BIT(1));
		break;
	case 0x2:
		/* generate Event element for event 1, 2 and 3 */
		config->eventctrl1 |= val & (BIT(0) | BIT(1) | BIT(2));
		break;
	case 0x3:
		/* generate Event element for all 4 events */
		config->eventctrl1 |= val & 0xF;
		break;
	default:
		break;
	}
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(event_instren);

static ssize_t event_ts_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->ts_ctrl;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_ts_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!drvdata->ts_size)
		return -EINVAL;

	config->ts_ctrl = val & ETMv4_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(event_ts);

static ssize_t syncfreq_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->syncfreq;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t syncfreq_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (drvdata->syncpr == true)
		return -EINVAL;

	config->syncfreq = val & ETMv4_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(syncfreq);

static ssize_t cyc_threshold_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->ccctlr;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cyc_threshold_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	/* mask off max threshold before checking min value */
	val &= ETM_CYC_THRESHOLD_MASK;
	if (val < drvdata->ccitmin)
		return -EINVAL;

	config->ccctlr = val;
	return size;
}
static DEVICE_ATTR_RW(cyc_threshold);

static ssize_t bb_ctrl_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->bb_ctrl;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t bb_ctrl_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (drvdata->trcbb == false)
		return -EINVAL;
	if (!drvdata->nr_addr_cmp)
		return -EINVAL;

	/*
	 * Bit[8] controls include(1) / exclude(0), bits[0-7] select
	 * individual range comparators. If include then at least 1
	 * range must be selected.
	 */
	if ((val & BIT(8)) && (BMVAL(val, 0, 7) == 0))
		return -EINVAL;

	config->bb_ctrl = val & GENMASK(8, 0);
	return size;
}
static DEVICE_ATTR_RW(bb_ctrl);

static ssize_t event_vinst_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->vinst_ctrl & ETMv4_EVENT_MASK;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_vinst_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	val &= ETMv4_EVENT_MASK;
	config->vinst_ctrl &= ~ETMv4_EVENT_MASK;
	config->vinst_ctrl |= val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(event_vinst);

static ssize_t s_exlevel_vinst_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = (config->vinst_ctrl & TRCVICTLR_EXLEVEL_S_MASK) >> TRCVICTLR_EXLEVEL_S_SHIFT;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t s_exlevel_vinst_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* clear all EXLEVEL_S bits  */
	config->vinst_ctrl &= ~(TRCVICTLR_EXLEVEL_S_MASK);
	/* enable instruction tracing for corresponding exception level */
	val &= drvdata->s_ex_level;
	config->vinst_ctrl |= (val << TRCVICTLR_EXLEVEL_S_SHIFT);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(s_exlevel_vinst);

static ssize_t ns_exlevel_vinst_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	/* EXLEVEL_NS, bits[23:20] */
	val = (config->vinst_ctrl & TRCVICTLR_EXLEVEL_NS_MASK) >> TRCVICTLR_EXLEVEL_NS_SHIFT;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ns_exlevel_vinst_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* clear EXLEVEL_NS bits  */
	config->vinst_ctrl &= ~(TRCVICTLR_EXLEVEL_NS_MASK);
	/* enable instruction tracing for corresponding exception level */
	val &= drvdata->ns_ex_level;
	config->vinst_ctrl |= (val << TRCVICTLR_EXLEVEL_NS_SHIFT);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(ns_exlevel_vinst);

static ssize_t addr_idx_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->addr_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nr_addr_cmp * 2)
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

static ssize_t addr_instdatatype_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	ssize_t len;
	u8 val, idx;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	val = BMVAL(config->addr_acc[idx], 0, 1);
	len = scnprintf(buf, PAGE_SIZE, "%s\n",
			val == ETM_INSTR_ADDR ? "instr" :
			(val == ETM_DATA_LOAD_ADDR ? "data_load" :
			(val == ETM_DATA_STORE_ADDR ? "data_store" :
			"data_load_store")));
	spin_unlock(&drvdata->spinlock);
	return len;
}

static ssize_t addr_instdatatype_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	u8 idx;
	char str[20] = "";
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (strlen(buf) >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!strcmp(str, "instr"))
		/* TYPE, bits[1:0] */
		config->addr_acc[idx] &= ~(BIT(0) | BIT(1));

	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_instdatatype);

static ssize_t addr_single_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	idx = config->addr_idx;
	spin_lock(&drvdata->spinlock);
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	val = (unsigned long)config->addr_val[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_single_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = (u64)val;
	config->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_single);

static ssize_t addr_range_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u8 idx;
	unsigned long val1, val2;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

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

	val1 = (unsigned long)config->addr_val[idx];
	val2 = (unsigned long)config->addr_val[idx + 1];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx %#lx\n", val1, val2);
}

static ssize_t addr_range_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 idx;
	unsigned long val1, val2;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;
	int elements, exclude;

	elements = sscanf(buf, "%lx %lx %x", &val1, &val2, &exclude);

	/*  exclude is optional, but need at least two parameter */
	if (elements < 2)
		return -EINVAL;
	/* lower address comparator cannot have a higher address value */
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

	config->addr_val[idx] = (u64)val1;
	config->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	config->addr_val[idx + 1] = (u64)val2;
	config->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	/*
	 * Program include or exclude control bits for vinst or vdata
	 * whenever we change addr comparators to ETM_ADDR_TYPE_RANGE
	 * use supplied value, or default to bit set in 'mode'
	 */
	if (elements != 3)
		exclude = config->mode & ETM_MODE_EXCLUDE;
	etm4_set_mode_exclude(drvdata, exclude ? true : false);

	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_range);

static ssize_t addr_start_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;

	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = (unsigned long)config->addr_val[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_start_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!drvdata->nr_addr_cmp) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = (u64)val;
	config->addr_type[idx] = ETM_ADDR_TYPE_START;
	config->vissctlr |= BIT(idx);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_start);

static ssize_t addr_stop_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;

	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = (unsigned long)config->addr_val[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_stop_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!drvdata->nr_addr_cmp) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}
	if (!(config->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	       config->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	config->addr_val[idx] = (u64)val;
	config->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	config->vissctlr |= BIT(idx + 16);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_stop);

static ssize_t addr_ctxtype_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	ssize_t len;
	u8 idx, val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	/* CONTEXTTYPE, bits[3:2] */
	val = BMVAL(config->addr_acc[idx], 2, 3);
	len = scnprintf(buf, PAGE_SIZE, "%s\n", val == ETM_CTX_NONE ? "none" :
			(val == ETM_CTX_CTXID ? "ctxid" :
			(val == ETM_CTX_VMID ? "vmid" : "all")));
	spin_unlock(&drvdata->spinlock);
	return len;
}

static ssize_t addr_ctxtype_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	u8 idx;
	char str[10] = "";
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (strlen(buf) >= 10)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	if (!strcmp(str, "none"))
		/* start by clearing context type bits */
		config->addr_acc[idx] &= ~(BIT(2) | BIT(3));
	else if (!strcmp(str, "ctxid")) {
		/* 0b01 The trace unit performs a Context ID */
		if (drvdata->numcidc) {
			config->addr_acc[idx] |= BIT(2);
			config->addr_acc[idx] &= ~BIT(3);
		}
	} else if (!strcmp(str, "vmid")) {
		/* 0b10 The trace unit performs a VMID */
		if (drvdata->numvmidc) {
			config->addr_acc[idx] &= ~BIT(2);
			config->addr_acc[idx] |= BIT(3);
		}
	} else if (!strcmp(str, "all")) {
		/*
		 * 0b11 The trace unit performs a Context ID
		 * comparison and a VMID
		 */
		if (drvdata->numcidc)
			config->addr_acc[idx] |= BIT(2);
		if (drvdata->numvmidc)
			config->addr_acc[idx] |= BIT(3);
	}
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_ctxtype);

static ssize_t addr_context_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	/* context ID comparator bits[6:4] */
	val = BMVAL(config->addr_acc[idx], 4, 6);
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_context_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if ((drvdata->numcidc <= 1) && (drvdata->numvmidc <= 1))
		return -EINVAL;
	if (val >=  (drvdata->numcidc >= drvdata->numvmidc ?
		     drvdata->numcidc : drvdata->numvmidc))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	/* clear context ID comparator bits[6:4] */
	config->addr_acc[idx] &= ~(BIT(4) | BIT(5) | BIT(6));
	config->addr_acc[idx] |= (val << 4);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_context);

static ssize_t addr_exlevel_s_ns_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	val = BMVAL(config->addr_acc[idx], 8, 14);
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_exlevel_s_ns_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val & ~((GENMASK(14, 8) >> 8)))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	/* clear Exlevel_ns & Exlevel_s bits[14:12, 11:8], bit[15] is res0 */
	config->addr_acc[idx] &= ~(GENMASK(14, 8));
	config->addr_acc[idx] |= (val << 8);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_exlevel_s_ns);

static const char * const addr_type_names[] = {
	"unused",
	"single",
	"range",
	"start",
	"stop"
};

static ssize_t addr_cmp_view_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u8 idx, addr_type;
	unsigned long addr_v, addr_v2, addr_ctrl;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;
	int size = 0;
	bool exclude = false;

	spin_lock(&drvdata->spinlock);
	idx = config->addr_idx;
	addr_v = config->addr_val[idx];
	addr_ctrl = config->addr_acc[idx];
	addr_type = config->addr_type[idx];
	if (addr_type == ETM_ADDR_TYPE_RANGE) {
		if (idx & 0x1) {
			idx -= 1;
			addr_v2 = addr_v;
			addr_v = config->addr_val[idx];
		} else {
			addr_v2 = config->addr_val[idx + 1];
		}
		exclude = config->viiectlr & BIT(idx / 2 + 16);
	}
	spin_unlock(&drvdata->spinlock);
	if (addr_type) {
		size = scnprintf(buf, PAGE_SIZE, "addr_cmp[%i] %s %#lx", idx,
				 addr_type_names[addr_type], addr_v);
		if (addr_type == ETM_ADDR_TYPE_RANGE) {
			size += scnprintf(buf + size, PAGE_SIZE - size,
					  " %#lx %s", addr_v2,
					  exclude ? "exclude" : "include");
		}
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  " ctrl(%#lx)\n", addr_ctrl);
	} else {
		size = scnprintf(buf, PAGE_SIZE, "addr_cmp[%i] unused\n", idx);
	}
	return size;
}
static DEVICE_ATTR_RO(addr_cmp_view);

static ssize_t vinst_pe_cmp_start_stop_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (!drvdata->nr_pe_cmp)
		return -EINVAL;
	val = config->vipcssctlr;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static ssize_t vinst_pe_cmp_start_stop_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!drvdata->nr_pe_cmp)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	config->vipcssctlr = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(vinst_pe_cmp_start_stop);

static ssize_t seq_idx_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->seq_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_idx_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nrseqstate - 1)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	config->seq_idx = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(seq_idx);

static ssize_t seq_state_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->seq_state;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_state_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nrseqstate)
		return -EINVAL;

	config->seq_state = val;
	return size;
}
static DEVICE_ATTR_RW(seq_state);

static ssize_t seq_event_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->seq_idx;
	val = config->seq_ctrl[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_event_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->seq_idx;
	/* Seq control has two masks B[15:8] F[7:0] */
	config->seq_ctrl[idx] = val & 0xFFFF;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(seq_event);

static ssize_t seq_reset_event_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->seq_rst;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_reset_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(drvdata->nrseqstate))
		return -EINVAL;

	config->seq_rst = val & ETMv4_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_reset_event);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->cntr_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
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

static ssize_t cntrldvr_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	val = config->cntrldvr[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cntrldvr_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val > ETM_CNTR_MAX_VAL)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	config->cntrldvr[idx] = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(cntrldvr);

static ssize_t cntr_val_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	val = config->cntr_val[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cntr_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val > ETM_CNTR_MAX_VAL)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	config->cntr_val[idx] = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(cntr_val);

static ssize_t cntr_ctrl_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	val = config->cntr_ctrl[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cntr_ctrl_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->cntr_idx;
	config->cntr_ctrl[idx] = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(cntr_ctrl);

static ssize_t res_idx_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->res_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t res_idx_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	/*
	 * Resource selector pair 0 is always implemented and reserved,
	 * namely an idx with 0 and 1 is illegal.
	 */
	if ((val < 2) || (val >= 2 * drvdata->nr_resource))
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	config->res_idx = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(res_idx);

static ssize_t res_ctrl_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	idx = config->res_idx;
	val = config->res_ctrl[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t res_ctrl_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->res_idx;
	/* For odd idx pair inversal bit is RES0 */
	if (idx % 2 != 0)
		/* PAIRINV, bit[21] */
		val &= ~BIT(21);
	config->res_ctrl[idx] = val & GENMASK(21, 0);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(res_ctrl);

static ssize_t sshot_idx_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->ss_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t sshot_idx_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nr_ss_cmp)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	config->ss_idx = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(sshot_idx);

static ssize_t sshot_ctrl_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->ss_ctrl[config->ss_idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t sshot_ctrl_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->ss_idx;
	config->ss_ctrl[idx] = val & GENMASK(24, 0);
	/* must clear bit 31 in related status register on programming */
	config->ss_status[idx] &= ~BIT(31);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(sshot_ctrl);

static ssize_t sshot_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->ss_status[config->ss_idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(sshot_status);

static ssize_t sshot_pe_ctrl_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val = config->ss_pe_cmp[config->ss_idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t sshot_pe_ctrl_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->ss_idx;
	config->ss_pe_cmp[idx] = val & GENMASK(7, 0);
	/* must clear bit 31 in related status register on programming */
	config->ss_status[idx] &= ~BIT(31);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(sshot_pe_ctrl);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->ctxid_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->numcidc)
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
			      struct device_attribute *attr,
			      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->ctxid_idx;
	val = (unsigned long)config->ctxid_pid[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ctxid_pid_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	unsigned long pid;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

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

	/*
	 * only implemented when ctxid tracing is enabled, i.e. at least one
	 * ctxid comparator is implemented and ctxid is greater than 0 bits
	 * in length
	 */
	if (!drvdata->ctxid_size || !drvdata->numcidc)
		return -EINVAL;
	if (kstrtoul(buf, 16, &pid))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = config->ctxid_idx;
	config->ctxid_pid[idx] = (u64)pid;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(ctxid_pid);

static ssize_t ctxid_masks_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long val1, val2;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	val1 = config->ctxid_mask0;
	val2 = config->ctxid_mask1;
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx %#lx\n", val1, val2);
}

static ssize_t ctxid_masks_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 i, j, maskbyte;
	unsigned long val1, val2, mask;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;
	int nr_inputs;

	/*
	 * Don't use contextID tracing if coming from a PID namespace.  See
	 * comment in ctxid_pid_store().
	 */
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	/*
	 * only implemented when ctxid tracing is enabled, i.e. at least one
	 * ctxid comparator is implemented and ctxid is greater than 0 bits
	 * in length
	 */
	if (!drvdata->ctxid_size || !drvdata->numcidc)
		return -EINVAL;
	/* one mask if <= 4 comparators, two for up to 8 */
	nr_inputs = sscanf(buf, "%lx %lx", &val1, &val2);
	if ((drvdata->numcidc > 4) && (nr_inputs != 2))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/*
	 * each byte[0..3] controls mask value applied to ctxid
	 * comparator[0..3]
	 */
	switch (drvdata->numcidc) {
	case 0x1:
		/* COMP0, bits[7:0] */
		config->ctxid_mask0 = val1 & 0xFF;
		break;
	case 0x2:
		/* COMP1, bits[15:8] */
		config->ctxid_mask0 = val1 & 0xFFFF;
		break;
	case 0x3:
		/* COMP2, bits[23:16] */
		config->ctxid_mask0 = val1 & 0xFFFFFF;
		break;
	case 0x4:
		 /* COMP3, bits[31:24] */
		config->ctxid_mask0 = val1;
		break;
	case 0x5:
		/* COMP4, bits[7:0] */
		config->ctxid_mask0 = val1;
		config->ctxid_mask1 = val2 & 0xFF;
		break;
	case 0x6:
		/* COMP5, bits[15:8] */
		config->ctxid_mask0 = val1;
		config->ctxid_mask1 = val2 & 0xFFFF;
		break;
	case 0x7:
		/* COMP6, bits[23:16] */
		config->ctxid_mask0 = val1;
		config->ctxid_mask1 = val2 & 0xFFFFFF;
		break;
	case 0x8:
		/* COMP7, bits[31:24] */
		config->ctxid_mask0 = val1;
		config->ctxid_mask1 = val2;
		break;
	default:
		break;
	}
	/*
	 * If software sets a mask bit to 1, it must program relevant byte
	 * of ctxid comparator value 0x0, otherwise behavior is unpredictable.
	 * For example, if bit[3] of ctxid_mask0 is 1, we must clear bits[31:24]
	 * of ctxid comparator0 value (corresponding to byte 0) register.
	 */
	mask = config->ctxid_mask0;
	for (i = 0; i < drvdata->numcidc; i++) {
		/* mask value of corresponding ctxid comparator */
		maskbyte = mask & ETMv4_EVENT_MASK;
		/*
		 * each bit corresponds to a byte of respective ctxid comparator
		 * value register
		 */
		for (j = 0; j < 8; j++) {
			if (maskbyte & 1)
				config->ctxid_pid[i] &= ~(0xFFUL << (j * 8));
			maskbyte >>= 1;
		}
		/* Select the next ctxid comparator mask value */
		if (i == 3)
			/* ctxid comparators[4-7] */
			mask = config->ctxid_mask1;
		else
			mask >>= 0x8;
	}

	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(ctxid_masks);

static ssize_t vmid_idx_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = config->vmid_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t vmid_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->numvmidc)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	config->vmid_idx = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(vmid_idx);

static ssize_t vmid_val_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	val = (unsigned long)config->vmid_val[config->vmid_idx];
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t vmid_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	/*
	 * only implemented when vmid tracing is enabled, i.e. at least one
	 * vmid comparator is implemented and at least 8 bit vmid size
	 */
	if (!drvdata->vmid_size || !drvdata->numvmidc)
		return -EINVAL;
	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	config->vmid_val[config->vmid_idx] = (u64)val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(vmid_val);

static ssize_t vmid_masks_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val1, val2;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	val1 = config->vmid_mask0;
	val2 = config->vmid_mask1;
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx %#lx\n", val1, val2);
}

static ssize_t vmid_masks_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	u8 i, j, maskbyte;
	unsigned long val1, val2, mask;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct etmv4_config *config = &drvdata->config;
	int nr_inputs;

	/*
	 * only implemented when vmid tracing is enabled, i.e. at least one
	 * vmid comparator is implemented and at least 8 bit vmid size
	 */
	if (!drvdata->vmid_size || !drvdata->numvmidc)
		return -EINVAL;
	/* one mask if <= 4 comparators, two for up to 8 */
	nr_inputs = sscanf(buf, "%lx %lx", &val1, &val2);
	if ((drvdata->numvmidc > 4) && (nr_inputs != 2))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);

	/*
	 * each byte[0..3] controls mask value applied to vmid
	 * comparator[0..3]
	 */
	switch (drvdata->numvmidc) {
	case 0x1:
		/* COMP0, bits[7:0] */
		config->vmid_mask0 = val1 & 0xFF;
		break;
	case 0x2:
		/* COMP1, bits[15:8] */
		config->vmid_mask0 = val1 & 0xFFFF;
		break;
	case 0x3:
		/* COMP2, bits[23:16] */
		config->vmid_mask0 = val1 & 0xFFFFFF;
		break;
	case 0x4:
		/* COMP3, bits[31:24] */
		config->vmid_mask0 = val1;
		break;
	case 0x5:
		/* COMP4, bits[7:0] */
		config->vmid_mask0 = val1;
		config->vmid_mask1 = val2 & 0xFF;
		break;
	case 0x6:
		/* COMP5, bits[15:8] */
		config->vmid_mask0 = val1;
		config->vmid_mask1 = val2 & 0xFFFF;
		break;
	case 0x7:
		/* COMP6, bits[23:16] */
		config->vmid_mask0 = val1;
		config->vmid_mask1 = val2 & 0xFFFFFF;
		break;
	case 0x8:
		/* COMP7, bits[31:24] */
		config->vmid_mask0 = val1;
		config->vmid_mask1 = val2;
		break;
	default:
		break;
	}

	/*
	 * If software sets a mask bit to 1, it must program relevant byte
	 * of vmid comparator value 0x0, otherwise behavior is unpredictable.
	 * For example, if bit[3] of vmid_mask0 is 1, we must clear bits[31:24]
	 * of vmid comparator0 value (corresponding to byte 0) register.
	 */
	mask = config->vmid_mask0;
	for (i = 0; i < drvdata->numvmidc; i++) {
		/* mask value of corresponding vmid comparator */
		maskbyte = mask & ETMv4_EVENT_MASK;
		/*
		 * each bit corresponds to a byte of respective vmid comparator
		 * value register
		 */
		for (j = 0; j < 8; j++) {
			if (maskbyte & 1)
				config->vmid_val[i] &= ~(0xFFUL << (j * 8));
			maskbyte >>= 1;
		}
		/* Select the next vmid comparator mask value */
		if (i == 3)
			/* vmid comparators[4-7] */
			mask = config->vmid_mask1;
		else
			mask >>= 0x8;
	}
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(vmid_masks);

static ssize_t cpu_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->cpu;
	return scnprintf(buf, PAGE_SIZE, "%d\n", val);

}
static DEVICE_ATTR_RO(cpu);

static struct attribute *coresight_etmv4_attrs[] = {
	&dev_attr_nr_pe_cmp.attr,
	&dev_attr_nr_addr_cmp.attr,
	&dev_attr_nr_cntr.attr,
	&dev_attr_nr_ext_inp.attr,
	&dev_attr_numcidc.attr,
	&dev_attr_numvmidc.attr,
	&dev_attr_nrseqstate.attr,
	&dev_attr_nr_resource.attr,
	&dev_attr_nr_ss_cmp.attr,
	&dev_attr_reset.attr,
	&dev_attr_mode.attr,
	&dev_attr_pe.attr,
	&dev_attr_event.attr,
	&dev_attr_event_instren.attr,
	&dev_attr_event_ts.attr,
	&dev_attr_syncfreq.attr,
	&dev_attr_cyc_threshold.attr,
	&dev_attr_bb_ctrl.attr,
	&dev_attr_event_vinst.attr,
	&dev_attr_s_exlevel_vinst.attr,
	&dev_attr_ns_exlevel_vinst.attr,
	&dev_attr_addr_idx.attr,
	&dev_attr_addr_instdatatype.attr,
	&dev_attr_addr_single.attr,
	&dev_attr_addr_range.attr,
	&dev_attr_addr_start.attr,
	&dev_attr_addr_stop.attr,
	&dev_attr_addr_ctxtype.attr,
	&dev_attr_addr_context.attr,
	&dev_attr_addr_exlevel_s_ns.attr,
	&dev_attr_addr_cmp_view.attr,
	&dev_attr_vinst_pe_cmp_start_stop.attr,
	&dev_attr_sshot_idx.attr,
	&dev_attr_sshot_ctrl.attr,
	&dev_attr_sshot_pe_ctrl.attr,
	&dev_attr_sshot_status.attr,
	&dev_attr_seq_idx.attr,
	&dev_attr_seq_state.attr,
	&dev_attr_seq_event.attr,
	&dev_attr_seq_reset_event.attr,
	&dev_attr_cntr_idx.attr,
	&dev_attr_cntrldvr.attr,
	&dev_attr_cntr_val.attr,
	&dev_attr_cntr_ctrl.attr,
	&dev_attr_res_idx.attr,
	&dev_attr_res_ctrl.attr,
	&dev_attr_ctxid_idx.attr,
	&dev_attr_ctxid_pid.attr,
	&dev_attr_ctxid_masks.attr,
	&dev_attr_vmid_idx.attr,
	&dev_attr_vmid_val.attr,
	&dev_attr_vmid_masks.attr,
	&dev_attr_cpu.attr,
	NULL,
};

struct etmv4_reg {
	struct coresight_device *csdev;
	u32 offset;
	u32 data;
};

static void do_smp_cross_read(void *data)
{
	struct etmv4_reg *reg = data;

	reg->data = etm4x_relaxed_read32(&reg->csdev->access, reg->offset);
}

static u32 etmv4_cross_read(const struct etmv4_drvdata *drvdata, u32 offset)
{
	struct etmv4_reg reg;

	reg.offset = offset;
	reg.csdev = drvdata->csdev;

	/*
	 * smp cross call ensures the CPU will be powered up before
	 * accessing the ETMv4 trace core registers
	 */
	smp_call_function_single(drvdata->cpu, do_smp_cross_read, &reg, 1);
	return reg.data;
}

static inline u32 coresight_etm4x_attr_to_offset(struct device_attribute *attr)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return (u32)(unsigned long)eattr->var;
}

static ssize_t coresight_etm4x_reg_show(struct device *dev,
					struct device_attribute *d_attr,
					char *buf)
{
	u32 val, offset;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	offset = coresight_etm4x_attr_to_offset(d_attr);

	pm_runtime_get_sync(dev->parent);
	val = etmv4_cross_read(drvdata, offset);
	pm_runtime_put_sync(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", val);
}

static inline bool
etm4x_register_implemented(struct etmv4_drvdata *drvdata, u32 offset)
{
	switch (offset) {
	ETM4x_SYSREG_LIST_CASES
		/*
		 * Registers accessible via system instructions are always
		 * implemented.
		 */
		return true;
	ETM4x_MMAP_LIST_CASES
		/*
		 * Registers accessible only via memory-mapped registers
		 * must not be accessed via system instructions.
		 * We cannot access the drvdata->csdev here, as this
		 * function is called during the device creation, via
		 * coresight_register() and the csdev is not initialized
		 * until that is done. So rely on the drvdata->base to
		 * detect if we have a memory mapped access.
		 */
		return !!drvdata->base;
	}

	return false;
}

/*
 * Hide the ETM4x registers that may not be available on the
 * hardware.
 * There are certain management registers unavailable via system
 * instructions. Make those sysfs attributes hidden on such
 * systems.
 */
static umode_t
coresight_etm4x_attr_reg_implemented(struct kobject *kobj,
				     struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct device_attribute *d_attr;
	u32 offset;

	d_attr = container_of(attr, struct device_attribute, attr);
	offset = coresight_etm4x_attr_to_offset(d_attr);

	if (etm4x_register_implemented(drvdata, offset))
		return attr->mode;
	return 0;
}

#define coresight_etm4x_reg(name, offset)				\
	&((struct dev_ext_attribute[]) {				\
	   {								\
		__ATTR(name, 0444, coresight_etm4x_reg_show, NULL),	\
		(void *)(unsigned long)offset				\
	   }								\
	})[0].attr.attr

static struct attribute *coresight_etmv4_mgmt_attrs[] = {
	coresight_etm4x_reg(trcpdcr, TRCPDCR),
	coresight_etm4x_reg(trcpdsr, TRCPDSR),
	coresight_etm4x_reg(trclsr, TRCLSR),
	coresight_etm4x_reg(trcauthstatus, TRCAUTHSTATUS),
	coresight_etm4x_reg(trcdevid, TRCDEVID),
	coresight_etm4x_reg(trcdevtype, TRCDEVTYPE),
	coresight_etm4x_reg(trcpidr0, TRCPIDR0),
	coresight_etm4x_reg(trcpidr1, TRCPIDR1),
	coresight_etm4x_reg(trcpidr2, TRCPIDR2),
	coresight_etm4x_reg(trcpidr3, TRCPIDR3),
	coresight_etm4x_reg(trcoslsr, TRCOSLSR),
	coresight_etm4x_reg(trcconfig, TRCCONFIGR),
	coresight_etm4x_reg(trctraceid, TRCTRACEIDR),
	coresight_etm4x_reg(trcdevarch, TRCDEVARCH),
	NULL,
};

static struct attribute *coresight_etmv4_trcidr_attrs[] = {
	coresight_etm4x_reg(trcidr0, TRCIDR0),
	coresight_etm4x_reg(trcidr1, TRCIDR1),
	coresight_etm4x_reg(trcidr2, TRCIDR2),
	coresight_etm4x_reg(trcidr3, TRCIDR3),
	coresight_etm4x_reg(trcidr4, TRCIDR4),
	coresight_etm4x_reg(trcidr5, TRCIDR5),
	/* trcidr[6,7] are reserved */
	coresight_etm4x_reg(trcidr8, TRCIDR8),
	coresight_etm4x_reg(trcidr9, TRCIDR9),
	coresight_etm4x_reg(trcidr10, TRCIDR10),
	coresight_etm4x_reg(trcidr11, TRCIDR11),
	coresight_etm4x_reg(trcidr12, TRCIDR12),
	coresight_etm4x_reg(trcidr13, TRCIDR13),
	NULL,
};

static const struct attribute_group coresight_etmv4_group = {
	.attrs = coresight_etmv4_attrs,
};

static const struct attribute_group coresight_etmv4_mgmt_group = {
	.is_visible = coresight_etm4x_attr_reg_implemented,
	.attrs = coresight_etmv4_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group coresight_etmv4_trcidr_group = {
	.attrs = coresight_etmv4_trcidr_attrs,
	.name = "trcidr",
};

const struct attribute_group *coresight_etmv4_groups[] = {
	&coresight_etmv4_group,
	&coresight_etmv4_mgmt_group,
	&coresight_etmv4_trcidr_group,
	NULL,
};
