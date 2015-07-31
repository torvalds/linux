/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/coresight.h>
#include <linux/pm_wakeup.h>
#include <linux/amba/bus.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>
#include <asm/sections.h>

#include "coresight-etm4x.h"

static int boot_enable;
module_param_named(boot_enable, boot_enable, int, S_IRUGO);

/* The number of ETMv4 currently registered */
static int etm4_count;
static struct etmv4_drvdata *etmdrvdata[NR_CPUS];

static void etm4_os_unlock(void *info)
{
	struct etmv4_drvdata *drvdata = (struct etmv4_drvdata *)info;

	/* Writing any value to ETMOSLAR unlocks the trace registers */
	writel_relaxed(0x0, drvdata->base + TRCOSLAR);
	isb();
}

static bool etm4_arch_supported(u8 arch)
{
	switch (arch) {
	case ETM_ARCH_V4:
		break;
	default:
		return false;
	}
	return true;
}

static int etm4_trace_id(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	int trace_id = -1;

	if (!drvdata->enable)
		return drvdata->trcid;

	pm_runtime_get_sync(drvdata->dev);
	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	trace_id = readl_relaxed(drvdata->base + TRCTRACEIDR);
	trace_id &= ETM_TRACEID_MASK;
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(drvdata->dev);

	return trace_id;
}

static void etm4_enable_hw(void *info)
{
	int i;
	struct etmv4_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);

	etm4_os_unlock(drvdata);

	/* Disable the trace unit before programming trace registers */
	writel_relaxed(0, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go up */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 1))
		dev_err(drvdata->dev,
			"timeout observed when probing at offset %#x\n",
			TRCSTATR);

	writel_relaxed(drvdata->pe_sel, drvdata->base + TRCPROCSELR);
	writel_relaxed(drvdata->cfg, drvdata->base + TRCCONFIGR);
	/* nothing specific implemented */
	writel_relaxed(0x0, drvdata->base + TRCAUXCTLR);
	writel_relaxed(drvdata->eventctrl0, drvdata->base + TRCEVENTCTL0R);
	writel_relaxed(drvdata->eventctrl1, drvdata->base + TRCEVENTCTL1R);
	writel_relaxed(drvdata->stall_ctrl, drvdata->base + TRCSTALLCTLR);
	writel_relaxed(drvdata->ts_ctrl, drvdata->base + TRCTSCTLR);
	writel_relaxed(drvdata->syncfreq, drvdata->base + TRCSYNCPR);
	writel_relaxed(drvdata->ccctlr, drvdata->base + TRCCCCTLR);
	writel_relaxed(drvdata->bb_ctrl, drvdata->base + TRCBBCTLR);
	writel_relaxed(drvdata->trcid, drvdata->base + TRCTRACEIDR);
	writel_relaxed(drvdata->vinst_ctrl, drvdata->base + TRCVICTLR);
	writel_relaxed(drvdata->viiectlr, drvdata->base + TRCVIIECTLR);
	writel_relaxed(drvdata->vissctlr,
		       drvdata->base + TRCVISSCTLR);
	writel_relaxed(drvdata->vipcssctlr,
		       drvdata->base + TRCVIPCSSCTLR);
	for (i = 0; i < drvdata->nrseqstate - 1; i++)
		writel_relaxed(drvdata->seq_ctrl[i],
			       drvdata->base + TRCSEQEVRn(i));
	writel_relaxed(drvdata->seq_rst, drvdata->base + TRCSEQRSTEVR);
	writel_relaxed(drvdata->seq_state, drvdata->base + TRCSEQSTR);
	writel_relaxed(drvdata->ext_inp, drvdata->base + TRCEXTINSELR);
	for (i = 0; i < drvdata->nr_cntr; i++) {
		writel_relaxed(drvdata->cntrldvr[i],
			       drvdata->base + TRCCNTRLDVRn(i));
		writel_relaxed(drvdata->cntr_ctrl[i],
			       drvdata->base + TRCCNTCTLRn(i));
		writel_relaxed(drvdata->cntr_val[i],
			       drvdata->base + TRCCNTVRn(i));
	}
	for (i = 0; i < drvdata->nr_resource; i++)
		writel_relaxed(drvdata->res_ctrl[i],
			       drvdata->base + TRCRSCTLRn(i));

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		writel_relaxed(drvdata->ss_ctrl[i],
			       drvdata->base + TRCSSCCRn(i));
		writel_relaxed(drvdata->ss_status[i],
			       drvdata->base + TRCSSCSRn(i));
		writel_relaxed(drvdata->ss_pe_cmp[i],
			       drvdata->base + TRCSSPCICRn(i));
	}
	for (i = 0; i < drvdata->nr_addr_cmp; i++) {
		writeq_relaxed(drvdata->addr_val[i],
			       drvdata->base + TRCACVRn(i));
		writeq_relaxed(drvdata->addr_acc[i],
			       drvdata->base + TRCACATRn(i));
	}
	for (i = 0; i < drvdata->numcidc; i++)
		writeq_relaxed(drvdata->ctxid_pid[i],
			       drvdata->base + TRCCIDCVRn(i));
	writel_relaxed(drvdata->ctxid_mask0, drvdata->base + TRCCIDCCTLR0);
	writel_relaxed(drvdata->ctxid_mask1, drvdata->base + TRCCIDCCTLR1);

	for (i = 0; i < drvdata->numvmidc; i++)
		writeq_relaxed(drvdata->vmid_val[i],
			       drvdata->base + TRCVMIDCVRn(i));
	writel_relaxed(drvdata->vmid_mask0, drvdata->base + TRCVMIDCCTLR0);
	writel_relaxed(drvdata->vmid_mask1, drvdata->base + TRCVMIDCCTLR1);

	/* Enable the trace unit */
	writel_relaxed(1, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go back down to '0' */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 0))
		dev_err(drvdata->dev,
			"timeout observed when probing at offset %#x\n",
			TRCSTATR);

	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d enable smp call done\n", drvdata->cpu);
}

static int etm4_enable(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;

	pm_runtime_get_sync(drvdata->dev);
	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm4_enable_hw on the cpu whose ETM is being enabled
	 * ensures that register writes occur when cpu is powered.
	 */
	ret = smp_call_function_single(drvdata->cpu,
				       etm4_enable_hw, drvdata, 1);
	if (ret)
		goto err;
	drvdata->enable = true;
	drvdata->sticky_enable = true;

	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "ETM tracing enabled\n");
	return 0;
err:
	spin_unlock(&drvdata->spinlock);
	pm_runtime_put(drvdata->dev);
	return ret;
}

static void etm4_disable_hw(void *info)
{
	u32 control;
	struct etmv4_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);

	control = readl_relaxed(drvdata->base + TRCPRGCTLR);

	/* EN, bit[0] Trace unit enable bit */
	control &= ~0x1;

	/* make sure everything completes before disabling */
	mb();
	isb();
	writel_relaxed(control, drvdata->base + TRCPRGCTLR);

	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d disable smp call done\n", drvdata->cpu);
}

static void etm4_disable(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * Taking hotplug lock here protects from clocks getting disabled
	 * with tracing being left on (crash scenario) if user disable occurs
	 * after cpu online mask indicates the cpu is offline but before the
	 * DYING hotplug callback is serviced by the ETM driver.
	 */
	get_online_cpus();
	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm4_disable_hw on the cpu whose ETM is being disabled
	 * ensures that register writes occur when cpu is powered.
	 */
	smp_call_function_single(drvdata->cpu, etm4_disable_hw, drvdata, 1);
	drvdata->enable = false;

	spin_unlock(&drvdata->spinlock);
	put_online_cpus();

	pm_runtime_put(drvdata->dev);

	dev_info(drvdata->dev, "ETM tracing disabled\n");
}

static const struct coresight_ops_source etm4_source_ops = {
	.trace_id	= etm4_trace_id,
	.enable		= etm4_enable,
	.disable	= etm4_disable,
};

static const struct coresight_ops etm4_cs_ops = {
	.source_ops	= &etm4_source_ops,
};

static int etm4_set_mode_exclude(struct etmv4_drvdata *drvdata, bool exclude)
{
	u8 idx = drvdata->addr_idx;

	/*
	 * TRCACATRn.TYPE bit[1:0]: type of comparison
	 * the trace unit performs
	 */
	if (BMVAL(drvdata->addr_acc[idx], 0, 1) == ETM_INSTR_ADDR) {
		if (idx % 2 != 0)
			return -EINVAL;

		/*
		 * We are performing instruction address comparison. Set the
		 * relevant bit of ViewInst Include/Exclude Control register
		 * for corresponding address comparator pair.
		 */
		if (drvdata->addr_type[idx] != ETM_ADDR_TYPE_RANGE ||
		    drvdata->addr_type[idx + 1] != ETM_ADDR_TYPE_RANGE)
			return -EINVAL;

		if (exclude == true) {
			/*
			 * Set exclude bit and unset the include bit
			 * corresponding to comparator pair
			 */
			drvdata->viiectlr |= BIT(idx / 2 + 16);
			drvdata->viiectlr &= ~BIT(idx / 2);
		} else {
			/*
			 * Set include bit and unset exclude bit
			 * corresponding to comparator pair
			 */
			drvdata->viiectlr |= BIT(idx / 2);
			drvdata->viiectlr &= ~BIT(idx / 2 + 16);
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		drvdata->mode = 0x0;

	/* Disable data tracing: do not trace load and store data transfers */
	drvdata->mode &= ~(ETM_MODE_LOAD | ETM_MODE_STORE);
	drvdata->cfg &= ~(BIT(1) | BIT(2));

	/* Disable data value and data address tracing */
	drvdata->mode &= ~(ETM_MODE_DATA_TRACE_ADDR |
			   ETM_MODE_DATA_TRACE_VAL);
	drvdata->cfg &= ~(BIT(16) | BIT(17));

	/* Disable all events tracing */
	drvdata->eventctrl0 = 0x0;
	drvdata->eventctrl1 = 0x0;

	/* Disable timestamp event */
	drvdata->ts_ctrl = 0x0;

	/* Disable stalling */
	drvdata->stall_ctrl = 0x0;

	/* Reset trace synchronization period  to 2^8 = 256 bytes*/
	if (drvdata->syncpr == false)
		drvdata->syncfreq = 0x8;

	/*
	 * Enable ViewInst to trace everything with start-stop logic in
	 * started state. ARM recommends start-stop logic is set before
	 * each trace run.
	 */
	drvdata->vinst_ctrl |= BIT(0);
	if (drvdata->nr_addr_cmp == true) {
		drvdata->mode |= ETM_MODE_VIEWINST_STARTSTOP;
		/* SSSTATUS, bit[9] */
		drvdata->vinst_ctrl |= BIT(9);
	}

	/* No address range filtering for ViewInst */
	drvdata->viiectlr = 0x0;

	/* No start-stop filtering for ViewInst */
	drvdata->vissctlr = 0x0;

	/* Disable seq events */
	for (i = 0; i < drvdata->nrseqstate-1; i++)
		drvdata->seq_ctrl[i] = 0x0;
	drvdata->seq_rst = 0x0;
	drvdata->seq_state = 0x0;

	/* Disable external input events */
	drvdata->ext_inp = 0x0;

	drvdata->cntr_idx = 0x0;
	for (i = 0; i < drvdata->nr_cntr; i++) {
		drvdata->cntrldvr[i] = 0x0;
		drvdata->cntr_ctrl[i] = 0x0;
		drvdata->cntr_val[i] = 0x0;
	}

	drvdata->res_idx = 0x0;
	for (i = 0; i < drvdata->nr_resource; i++)
		drvdata->res_ctrl[i] = 0x0;

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		drvdata->ss_ctrl[i] = 0x0;
		drvdata->ss_pe_cmp[i] = 0x0;
	}

	drvdata->addr_idx = 0x0;
	for (i = 0; i < drvdata->nr_addr_cmp * 2; i++) {
		drvdata->addr_val[i] = 0x0;
		drvdata->addr_acc[i] = 0x0;
		drvdata->addr_type[i] = ETM_ADDR_TYPE_NONE;
	}

	drvdata->ctxid_idx = 0x0;
	for (i = 0; i < drvdata->numcidc; i++) {
		drvdata->ctxid_pid[i] = 0x0;
		drvdata->ctxid_vpid[i] = 0x0;
	}

	drvdata->ctxid_mask0 = 0x0;
	drvdata->ctxid_mask1 = 0x0;

	drvdata->vmid_idx = 0x0;
	for (i = 0; i < drvdata->numvmidc; i++)
		drvdata->vmid_val[i] = 0x0;
	drvdata->vmid_mask0 = 0x0;
	drvdata->vmid_mask1 = 0x0;

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

	val = drvdata->mode;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	unsigned long val, mode;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->mode = val & ETMv4_MODE_ALL;

	if (drvdata->mode & ETM_MODE_EXCLUDE)
		etm4_set_mode_exclude(drvdata, true);
	else
		etm4_set_mode_exclude(drvdata, false);

	if (drvdata->instrp0 == true) {
		/* start by clearing instruction P0 field */
		drvdata->cfg  &= ~(BIT(1) | BIT(2));
		if (drvdata->mode & ETM_MODE_LOAD)
			/* 0b01 Trace load instructions as P0 instructions */
			drvdata->cfg  |= BIT(1);
		if (drvdata->mode & ETM_MODE_STORE)
			/* 0b10 Trace store instructions as P0 instructions */
			drvdata->cfg  |= BIT(2);
		if (drvdata->mode & ETM_MODE_LOAD_STORE)
			/*
			 * 0b11 Trace load and store instructions
			 * as P0 instructions
			 */
			drvdata->cfg  |= BIT(1) | BIT(2);
	}

	/* bit[3], Branch broadcast mode */
	if ((drvdata->mode & ETM_MODE_BB) && (drvdata->trcbb == true))
		drvdata->cfg |= BIT(3);
	else
		drvdata->cfg &= ~BIT(3);

	/* bit[4], Cycle counting instruction trace bit */
	if ((drvdata->mode & ETMv4_MODE_CYCACC) &&
		(drvdata->trccci == true))
		drvdata->cfg |= BIT(4);
	else
		drvdata->cfg &= ~BIT(4);

	/* bit[6], Context ID tracing bit */
	if ((drvdata->mode & ETMv4_MODE_CTXID) && (drvdata->ctxid_size))
		drvdata->cfg |= BIT(6);
	else
		drvdata->cfg &= ~BIT(6);

	if ((drvdata->mode & ETM_MODE_VMID) && (drvdata->vmid_size))
		drvdata->cfg |= BIT(7);
	else
		drvdata->cfg &= ~BIT(7);

	/* bits[10:8], Conditional instruction tracing bit */
	mode = ETM_MODE_COND(drvdata->mode);
	if (drvdata->trccond == true) {
		drvdata->cfg &= ~(BIT(8) | BIT(9) | BIT(10));
		drvdata->cfg |= mode << 8;
	}

	/* bit[11], Global timestamp tracing bit */
	if ((drvdata->mode & ETMv4_MODE_TIMESTAMP) && (drvdata->ts_size))
		drvdata->cfg |= BIT(11);
	else
		drvdata->cfg &= ~BIT(11);

	/* bit[12], Return stack enable bit */
	if ((drvdata->mode & ETM_MODE_RETURNSTACK) &&
		(drvdata->retstack == true))
		drvdata->cfg |= BIT(12);
	else
		drvdata->cfg &= ~BIT(12);

	/* bits[14:13], Q element enable field */
	mode = ETM_MODE_QELEM(drvdata->mode);
	/* start by clearing QE bits */
	drvdata->cfg &= ~(BIT(13) | BIT(14));
	/* if supported, Q elements with instruction counts are enabled */
	if ((mode & BIT(0)) && (drvdata->q_support & BIT(0)))
		drvdata->cfg |= BIT(13);
	/*
	 * if supported, Q elements with and without instruction
	 * counts are enabled
	 */
	if ((mode & BIT(1)) && (drvdata->q_support & BIT(1)))
		drvdata->cfg |= BIT(14);

	/* bit[11], AMBA Trace Bus (ATB) trigger enable bit */
	if ((drvdata->mode & ETM_MODE_ATB_TRIGGER) &&
	    (drvdata->atbtrig == true))
		drvdata->eventctrl1 |= BIT(11);
	else
		drvdata->eventctrl1 &= ~BIT(11);

	/* bit[12], Low-power state behavior override bit */
	if ((drvdata->mode & ETM_MODE_LPOVERRIDE) &&
	    (drvdata->lpoverride == true))
		drvdata->eventctrl1 |= BIT(12);
	else
		drvdata->eventctrl1 &= ~BIT(12);

	/* bit[8], Instruction stall bit */
	if (drvdata->mode & ETM_MODE_ISTALL_EN)
		drvdata->stall_ctrl |= BIT(8);
	else
		drvdata->stall_ctrl &= ~BIT(8);

	/* bit[10], Prioritize instruction trace bit */
	if (drvdata->mode & ETM_MODE_INSTPRIO)
		drvdata->stall_ctrl |= BIT(10);
	else
		drvdata->stall_ctrl &= ~BIT(10);

	/* bit[13], Trace overflow prevention bit */
	if ((drvdata->mode & ETM_MODE_NOOVERFLOW) &&
		(drvdata->nooverflow == true))
		drvdata->stall_ctrl |= BIT(13);
	else
		drvdata->stall_ctrl &= ~BIT(13);

	/* bit[9] Start/stop logic control bit */
	if (drvdata->mode & ETM_MODE_VIEWINST_STARTSTOP)
		drvdata->vinst_ctrl |= BIT(9);
	else
		drvdata->vinst_ctrl &= ~BIT(9);

	/* bit[10], Whether a trace unit must trace a Reset exception */
	if (drvdata->mode & ETM_MODE_TRACE_RESET)
		drvdata->vinst_ctrl |= BIT(10);
	else
		drvdata->vinst_ctrl &= ~BIT(10);

	/* bit[11], Whether a trace unit must trace a system error exception */
	if ((drvdata->mode & ETM_MODE_TRACE_ERR) &&
		(drvdata->trc_error == true))
		drvdata->vinst_ctrl |= BIT(11);
	else
		drvdata->vinst_ctrl &= ~BIT(11);

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

	val = drvdata->pe_sel;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t pe_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val > drvdata->nr_pe) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	drvdata->pe_sel = val;
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

	val = drvdata->eventctrl0;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	switch (drvdata->nr_event) {
	case 0x0:
		/* EVENT0, bits[7:0] */
		drvdata->eventctrl0 = val & 0xFF;
		break;
	case 0x1:
		 /* EVENT1, bits[15:8] */
		drvdata->eventctrl0 = val & 0xFFFF;
		break;
	case 0x2:
		/* EVENT2, bits[23:16] */
		drvdata->eventctrl0 = val & 0xFFFFFF;
		break;
	case 0x3:
		/* EVENT3, bits[31:24] */
		drvdata->eventctrl0 = val;
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

	val = BMVAL(drvdata->eventctrl1, 0, 3);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_instren_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* start by clearing all instruction event enable bits */
	drvdata->eventctrl1 &= ~(BIT(0) | BIT(1) | BIT(2) | BIT(3));
	switch (drvdata->nr_event) {
	case 0x0:
		/* generate Event element for event 1 */
		drvdata->eventctrl1 |= val & BIT(1);
		break;
	case 0x1:
		/* generate Event element for event 1 and 2 */
		drvdata->eventctrl1 |= val & (BIT(0) | BIT(1));
		break;
	case 0x2:
		/* generate Event element for event 1, 2 and 3 */
		drvdata->eventctrl1 |= val & (BIT(0) | BIT(1) | BIT(2));
		break;
	case 0x3:
		/* generate Event element for all 4 events */
		drvdata->eventctrl1 |= val & 0xF;
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

	val = drvdata->ts_ctrl;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_ts_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!drvdata->ts_size)
		return -EINVAL;

	drvdata->ts_ctrl = val & ETMv4_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(event_ts);

static ssize_t syncfreq_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->syncfreq;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t syncfreq_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (drvdata->syncpr == true)
		return -EINVAL;

	drvdata->syncfreq = val & ETMv4_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(syncfreq);

static ssize_t cyc_threshold_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ccctlr;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cyc_threshold_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val < drvdata->ccitmin)
		return -EINVAL;

	drvdata->ccctlr = val & ETM_CYC_THRESHOLD_MASK;
	return size;
}
static DEVICE_ATTR_RW(cyc_threshold);

static ssize_t bb_ctrl_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->bb_ctrl;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t bb_ctrl_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (drvdata->trcbb == false)
		return -EINVAL;
	if (!drvdata->nr_addr_cmp)
		return -EINVAL;
	/*
	 * Bit[7:0] selects which address range comparator is used for
	 * branch broadcast control.
	 */
	if (BMVAL(val, 0, 7) > drvdata->nr_addr_cmp)
		return -EINVAL;

	drvdata->bb_ctrl = val;
	return size;
}
static DEVICE_ATTR_RW(bb_ctrl);

static ssize_t event_vinst_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->vinst_ctrl & ETMv4_EVENT_MASK;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t event_vinst_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	val &= ETMv4_EVENT_MASK;
	drvdata->vinst_ctrl &= ~ETMv4_EVENT_MASK;
	drvdata->vinst_ctrl |= val;
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

	val = BMVAL(drvdata->vinst_ctrl, 16, 19);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t s_exlevel_vinst_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* clear all EXLEVEL_S bits (bit[18] is never implemented) */
	drvdata->vinst_ctrl &= ~(BIT(16) | BIT(17) | BIT(19));
	/* enable instruction tracing for corresponding exception level */
	val &= drvdata->s_ex_level;
	drvdata->vinst_ctrl |= (val << 16);
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

	/* EXLEVEL_NS, bits[23:20] */
	val = BMVAL(drvdata->vinst_ctrl, 20, 23);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ns_exlevel_vinst_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/* clear EXLEVEL_NS bits (bit[23] is never implemented */
	drvdata->vinst_ctrl &= ~(BIT(20) | BIT(21) | BIT(22));
	/* enable instruction tracing for corresponding exception level */
	val &= drvdata->ns_ex_level;
	drvdata->vinst_ctrl |= (val << 20);
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

	val = drvdata->addr_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nr_addr_cmp * 2)
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

static ssize_t addr_instdatatype_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	ssize_t len;
	u8 val, idx;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	val = BMVAL(drvdata->addr_acc[idx], 0, 1);
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

	if (strlen(buf) >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!strcmp(str, "instr"))
		/* TYPE, bits[1:0] */
		drvdata->addr_acc[idx] &= ~(BIT(0) | BIT(1));

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

	idx = drvdata->addr_idx;
	spin_lock(&drvdata->spinlock);
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}
	val = (unsigned long)drvdata->addr_val[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = (u64)val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
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

	val1 = (unsigned long)drvdata->addr_val[idx];
	val2 = (unsigned long)drvdata->addr_val[idx + 1];
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

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* lower address comparator cannot have a higher address value */
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

	drvdata->addr_val[idx] = (u64)val1;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	drvdata->addr_val[idx + 1] = (u64)val2;
	drvdata->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	/*
	 * Program include or exclude control bits for vinst or vdata
	 * whenever we change addr comparators to ETM_ADDR_TYPE_RANGE
	 */
	if (drvdata->mode & ETM_MODE_EXCLUDE)
		etm4_set_mode_exclude(drvdata, true);
	else
		etm4_set_mode_exclude(drvdata, false);

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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;

	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = (unsigned long)drvdata->addr_val[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!drvdata->nr_addr_cmp) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = (u64)val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_START;
	drvdata->vissctlr |= BIT(idx);
	/* SSSTATUS, bit[9] - turn on start/stop logic */
	drvdata->vinst_ctrl |= BIT(9);
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;

	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = (unsigned long)drvdata->addr_val[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!drvdata->nr_addr_cmp) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	       drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = (u64)val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	drvdata->vissctlr |= BIT(idx + 16);
	/* SSSTATUS, bit[9] - turn on start/stop logic */
	drvdata->vinst_ctrl |= BIT(9);
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	/* CONTEXTTYPE, bits[3:2] */
	val = BMVAL(drvdata->addr_acc[idx], 2, 3);
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

	if (strlen(buf) >= 10)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!strcmp(str, "none"))
		/* start by clearing context type bits */
		drvdata->addr_acc[idx] &= ~(BIT(2) | BIT(3));
	else if (!strcmp(str, "ctxid")) {
		/* 0b01 The trace unit performs a Context ID */
		if (drvdata->numcidc) {
			drvdata->addr_acc[idx] |= BIT(2);
			drvdata->addr_acc[idx] &= ~BIT(3);
		}
	} else if (!strcmp(str, "vmid")) {
		/* 0b10 The trace unit performs a VMID */
		if (drvdata->numvmidc) {
			drvdata->addr_acc[idx] &= ~BIT(2);
			drvdata->addr_acc[idx] |= BIT(3);
		}
	} else if (!strcmp(str, "all")) {
		/*
		 * 0b11 The trace unit performs a Context ID
		 * comparison and a VMID
		 */
		if (drvdata->numcidc)
			drvdata->addr_acc[idx] |= BIT(2);
		if (drvdata->numvmidc)
			drvdata->addr_acc[idx] |= BIT(3);
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	/* context ID comparator bits[6:4] */
	val = BMVAL(drvdata->addr_acc[idx], 4, 6);
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if ((drvdata->numcidc <= 1) && (drvdata->numvmidc <= 1))
		return -EINVAL;
	if (val >=  (drvdata->numcidc >= drvdata->numvmidc ?
		     drvdata->numcidc : drvdata->numvmidc))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	/* clear context ID comparator bits[6:4] */
	drvdata->addr_acc[idx] &= ~(BIT(4) | BIT(5) | BIT(6));
	drvdata->addr_acc[idx] |= (val << 4);
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(addr_context);

static ssize_t seq_idx_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_idx_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nrseqstate - 1)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->seq_idx = val;
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

	val = drvdata->seq_state;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_state_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->nrseqstate)
		return -EINVAL;

	drvdata->seq_state = val;
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->seq_idx;
	val = drvdata->seq_ctrl[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->seq_idx;
	/* RST, bits[7:0] */
	drvdata->seq_ctrl[idx] = val & 0xFF;
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

	val = drvdata->seq_rst;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t seq_reset_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(drvdata->nrseqstate))
		return -EINVAL;

	drvdata->seq_rst = val & ETMv4_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_reset_event);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->cntr_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
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

static ssize_t cntrldvr_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	val = drvdata->cntrldvr[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val > ETM_CNTR_MAX_VAL)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	drvdata->cntrldvr[idx] = val;
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	val = drvdata->cntr_val[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val > ETM_CNTR_MAX_VAL)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	drvdata->cntr_val[idx] = val;
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	val = drvdata->cntr_ctrl[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->cntr_idx;
	drvdata->cntr_ctrl[idx] = val;
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

	val = drvdata->res_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t res_idx_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	/* Resource selector pair 0 is always implemented and reserved */
	if ((val == 0) || (val >= drvdata->nr_resource))
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->res_idx = val;
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->res_idx;
	val = drvdata->res_ctrl[idx];
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

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->res_idx;
	/* For odd idx pair inversal bit is RES0 */
	if (idx % 2 != 0)
		/* PAIRINV, bit[21] */
		val &= ~BIT(21);
	drvdata->res_ctrl[idx] = val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(res_ctrl);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ctxid_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->numcidc)
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
			      struct device_attribute *attr,
			      char *buf)
{
	u8 idx;
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->ctxid_idx;
	val = (unsigned long)drvdata->ctxid_vpid[idx];
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t ctxid_pid_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	u8 idx;
	unsigned long vpid, pid;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	/*
	 * only implemented when ctxid tracing is enabled, i.e. at least one
	 * ctxid comparator is implemented and ctxid is greater than 0 bits
	 * in length
	 */
	if (!drvdata->ctxid_size || !drvdata->numcidc)
		return -EINVAL;
	if (kstrtoul(buf, 16, &vpid))
		return -EINVAL;

	pid = coresight_vpid_to_pid(vpid);

	spin_lock(&drvdata->spinlock);
	idx = drvdata->ctxid_idx;
	drvdata->ctxid_pid[idx] = (u64)pid;
	drvdata->ctxid_vpid[idx] = (u64)vpid;
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

	spin_lock(&drvdata->spinlock);
	val1 = drvdata->ctxid_mask0;
	val2 = drvdata->ctxid_mask1;
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

	/*
	 * only implemented when ctxid tracing is enabled, i.e. at least one
	 * ctxid comparator is implemented and ctxid is greater than 0 bits
	 * in length
	 */
	if (!drvdata->ctxid_size || !drvdata->numcidc)
		return -EINVAL;
	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	/*
	 * each byte[0..3] controls mask value applied to ctxid
	 * comparator[0..3]
	 */
	switch (drvdata->numcidc) {
	case 0x1:
		/* COMP0, bits[7:0] */
		drvdata->ctxid_mask0 = val1 & 0xFF;
		break;
	case 0x2:
		/* COMP1, bits[15:8] */
		drvdata->ctxid_mask0 = val1 & 0xFFFF;
		break;
	case 0x3:
		/* COMP2, bits[23:16] */
		drvdata->ctxid_mask0 = val1 & 0xFFFFFF;
		break;
	case 0x4:
		 /* COMP3, bits[31:24] */
		drvdata->ctxid_mask0 = val1;
		break;
	case 0x5:
		/* COMP4, bits[7:0] */
		drvdata->ctxid_mask0 = val1;
		drvdata->ctxid_mask1 = val2 & 0xFF;
		break;
	case 0x6:
		/* COMP5, bits[15:8] */
		drvdata->ctxid_mask0 = val1;
		drvdata->ctxid_mask1 = val2 & 0xFFFF;
		break;
	case 0x7:
		/* COMP6, bits[23:16] */
		drvdata->ctxid_mask0 = val1;
		drvdata->ctxid_mask1 = val2 & 0xFFFFFF;
		break;
	case 0x8:
		/* COMP7, bits[31:24] */
		drvdata->ctxid_mask0 = val1;
		drvdata->ctxid_mask1 = val2;
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
	mask = drvdata->ctxid_mask0;
	for (i = 0; i < drvdata->numcidc; i++) {
		/* mask value of corresponding ctxid comparator */
		maskbyte = mask & ETMv4_EVENT_MASK;
		/*
		 * each bit corresponds to a byte of respective ctxid comparator
		 * value register
		 */
		for (j = 0; j < 8; j++) {
			if (maskbyte & 1)
				drvdata->ctxid_pid[i] &= ~(0xFF << (j * 8));
			maskbyte >>= 1;
		}
		/* Select the next ctxid comparator mask value */
		if (i == 3)
			/* ctxid comparators[4-7] */
			mask = drvdata->ctxid_mask1;
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

	val = drvdata->vmid_idx;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t vmid_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val >= drvdata->numvmidc)
		return -EINVAL;

	/*
	 * Use spinlock to ensure index doesn't change while it gets
	 * dereferenced multiple times within a spinlock block elsewhere.
	 */
	spin_lock(&drvdata->spinlock);
	drvdata->vmid_idx = val;
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

	val = (unsigned long)drvdata->vmid_val[drvdata->vmid_idx];
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t vmid_val_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	/*
	 * only implemented when vmid tracing is enabled, i.e. at least one
	 * vmid comparator is implemented and at least 8 bit vmid size
	 */
	if (!drvdata->vmid_size || !drvdata->numvmidc)
		return -EINVAL;
	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	drvdata->vmid_val[drvdata->vmid_idx] = (u64)val;
	spin_unlock(&drvdata->spinlock);
	return size;
}
static DEVICE_ATTR_RW(vmid_val);

static ssize_t vmid_masks_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val1, val2;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val1 = drvdata->vmid_mask0;
	val2 = drvdata->vmid_mask1;
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
	/*
	 * only implemented when vmid tracing is enabled, i.e. at least one
	 * vmid comparator is implemented and at least 8 bit vmid size
	 */
	if (!drvdata->vmid_size || !drvdata->numvmidc)
		return -EINVAL;
	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;

	spin_lock(&drvdata->spinlock);

	/*
	 * each byte[0..3] controls mask value applied to vmid
	 * comparator[0..3]
	 */
	switch (drvdata->numvmidc) {
	case 0x1:
		/* COMP0, bits[7:0] */
		drvdata->vmid_mask0 = val1 & 0xFF;
		break;
	case 0x2:
		/* COMP1, bits[15:8] */
		drvdata->vmid_mask0 = val1 & 0xFFFF;
		break;
	case 0x3:
		/* COMP2, bits[23:16] */
		drvdata->vmid_mask0 = val1 & 0xFFFFFF;
		break;
	case 0x4:
		/* COMP3, bits[31:24] */
		drvdata->vmid_mask0 = val1;
		break;
	case 0x5:
		/* COMP4, bits[7:0] */
		drvdata->vmid_mask0 = val1;
		drvdata->vmid_mask1 = val2 & 0xFF;
		break;
	case 0x6:
		/* COMP5, bits[15:8] */
		drvdata->vmid_mask0 = val1;
		drvdata->vmid_mask1 = val2 & 0xFFFF;
		break;
	case 0x7:
		/* COMP6, bits[23:16] */
		drvdata->vmid_mask0 = val1;
		drvdata->vmid_mask1 = val2 & 0xFFFFFF;
		break;
	case 0x8:
		/* COMP7, bits[31:24] */
		drvdata->vmid_mask0 = val1;
		drvdata->vmid_mask1 = val2;
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
	mask = drvdata->vmid_mask0;
	for (i = 0; i < drvdata->numvmidc; i++) {
		/* mask value of corresponding vmid comparator */
		maskbyte = mask & ETMv4_EVENT_MASK;
		/*
		 * each bit corresponds to a byte of respective vmid comparator
		 * value register
		 */
		for (j = 0; j < 8; j++) {
			if (maskbyte & 1)
				drvdata->vmid_val[i] &= ~(0xFF << (j * 8));
			maskbyte >>= 1;
		}
		/* Select the next vmid comparator mask value */
		if (i == 3)
			/* vmid comparators[4-7] */
			mask = drvdata->vmid_mask1;
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

#define coresight_simple_func(name, offset)				\
static ssize_t name##_show(struct device *_dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct etmv4_drvdata *drvdata = dev_get_drvdata(_dev->parent);	\
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",			\
			 readl_relaxed(drvdata->base + offset));	\
}									\
DEVICE_ATTR_RO(name)

coresight_simple_func(trcoslsr, TRCOSLSR);
coresight_simple_func(trcpdcr, TRCPDCR);
coresight_simple_func(trcpdsr, TRCPDSR);
coresight_simple_func(trclsr, TRCLSR);
coresight_simple_func(trcauthstatus, TRCAUTHSTATUS);
coresight_simple_func(trcdevid, TRCDEVID);
coresight_simple_func(trcdevtype, TRCDEVTYPE);
coresight_simple_func(trcpidr0, TRCPIDR0);
coresight_simple_func(trcpidr1, TRCPIDR1);
coresight_simple_func(trcpidr2, TRCPIDR2);
coresight_simple_func(trcpidr3, TRCPIDR3);

static struct attribute *coresight_etmv4_mgmt_attrs[] = {
	&dev_attr_trcoslsr.attr,
	&dev_attr_trcpdcr.attr,
	&dev_attr_trcpdsr.attr,
	&dev_attr_trclsr.attr,
	&dev_attr_trcauthstatus.attr,
	&dev_attr_trcdevid.attr,
	&dev_attr_trcdevtype.attr,
	&dev_attr_trcpidr0.attr,
	&dev_attr_trcpidr1.attr,
	&dev_attr_trcpidr2.attr,
	&dev_attr_trcpidr3.attr,
	NULL,
};

coresight_simple_func(trcidr0, TRCIDR0);
coresight_simple_func(trcidr1, TRCIDR1);
coresight_simple_func(trcidr2, TRCIDR2);
coresight_simple_func(trcidr3, TRCIDR3);
coresight_simple_func(trcidr4, TRCIDR4);
coresight_simple_func(trcidr5, TRCIDR5);
/* trcidr[6,7] are reserved */
coresight_simple_func(trcidr8, TRCIDR8);
coresight_simple_func(trcidr9, TRCIDR9);
coresight_simple_func(trcidr10, TRCIDR10);
coresight_simple_func(trcidr11, TRCIDR11);
coresight_simple_func(trcidr12, TRCIDR12);
coresight_simple_func(trcidr13, TRCIDR13);

static struct attribute *coresight_etmv4_trcidr_attrs[] = {
	&dev_attr_trcidr0.attr,
	&dev_attr_trcidr1.attr,
	&dev_attr_trcidr2.attr,
	&dev_attr_trcidr3.attr,
	&dev_attr_trcidr4.attr,
	&dev_attr_trcidr5.attr,
	/* trcidr[6,7] are reserved */
	&dev_attr_trcidr8.attr,
	&dev_attr_trcidr9.attr,
	&dev_attr_trcidr10.attr,
	&dev_attr_trcidr11.attr,
	&dev_attr_trcidr12.attr,
	&dev_attr_trcidr13.attr,
	NULL,
};

static const struct attribute_group coresight_etmv4_group = {
	.attrs = coresight_etmv4_attrs,
};

static const struct attribute_group coresight_etmv4_mgmt_group = {
	.attrs = coresight_etmv4_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group coresight_etmv4_trcidr_group = {
	.attrs = coresight_etmv4_trcidr_attrs,
	.name = "trcidr",
};

static const struct attribute_group *coresight_etmv4_groups[] = {
	&coresight_etmv4_group,
	&coresight_etmv4_mgmt_group,
	&coresight_etmv4_trcidr_group,
	NULL,
};

static void etm4_init_arch_data(void *info)
{
	u32 etmidr0;
	u32 etmidr1;
	u32 etmidr2;
	u32 etmidr3;
	u32 etmidr4;
	u32 etmidr5;
	struct etmv4_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);

	/* find all capabilities of the tracing unit */
	etmidr0 = readl_relaxed(drvdata->base + TRCIDR0);

	/* INSTP0, bits[2:1] P0 tracing support field */
	if (BMVAL(etmidr0, 1, 1) && BMVAL(etmidr0, 2, 2))
		drvdata->instrp0 = true;
	else
		drvdata->instrp0 = false;

	/* TRCBB, bit[5] Branch broadcast tracing support bit */
	if (BMVAL(etmidr0, 5, 5))
		drvdata->trcbb = true;
	else
		drvdata->trcbb = false;

	/* TRCCOND, bit[6] Conditional instruction tracing support bit */
	if (BMVAL(etmidr0, 6, 6))
		drvdata->trccond = true;
	else
		drvdata->trccond = false;

	/* TRCCCI, bit[7] Cycle counting instruction bit */
	if (BMVAL(etmidr0, 7, 7))
		drvdata->trccci = true;
	else
		drvdata->trccci = false;

	/* RETSTACK, bit[9] Return stack bit */
	if (BMVAL(etmidr0, 9, 9))
		drvdata->retstack = true;
	else
		drvdata->retstack = false;

	/* NUMEVENT, bits[11:10] Number of events field */
	drvdata->nr_event = BMVAL(etmidr0, 10, 11);
	/* QSUPP, bits[16:15] Q element support field */
	drvdata->q_support = BMVAL(etmidr0, 15, 16);
	/* TSSIZE, bits[28:24] Global timestamp size field */
	drvdata->ts_size = BMVAL(etmidr0, 24, 28);

	/* base architecture of trace unit */
	etmidr1 = readl_relaxed(drvdata->base + TRCIDR1);
	/*
	 * TRCARCHMIN, bits[7:4] architecture the minor version number
	 * TRCARCHMAJ, bits[11:8] architecture major versin number
	 */
	drvdata->arch = BMVAL(etmidr1, 4, 11);

	/* maximum size of resources */
	etmidr2 = readl_relaxed(drvdata->base + TRCIDR2);
	/* CIDSIZE, bits[9:5] Indicates the Context ID size */
	drvdata->ctxid_size = BMVAL(etmidr2, 5, 9);
	/* VMIDSIZE, bits[14:10] Indicates the VMID size */
	drvdata->vmid_size = BMVAL(etmidr2, 10, 14);
	/* CCSIZE, bits[28:25] size of the cycle counter in bits minus 12 */
	drvdata->ccsize = BMVAL(etmidr2, 25, 28);

	etmidr3 = readl_relaxed(drvdata->base + TRCIDR3);
	/* CCITMIN, bits[11:0] minimum threshold value that can be programmed */
	drvdata->ccitmin = BMVAL(etmidr3, 0, 11);
	/* EXLEVEL_S, bits[19:16] Secure state instruction tracing */
	drvdata->s_ex_level = BMVAL(etmidr3, 16, 19);
	/* EXLEVEL_NS, bits[23:20] Non-secure state instruction tracing */
	drvdata->ns_ex_level = BMVAL(etmidr3, 20, 23);

	/*
	 * TRCERR, bit[24] whether a trace unit can trace a
	 * system error exception.
	 */
	if (BMVAL(etmidr3, 24, 24))
		drvdata->trc_error = true;
	else
		drvdata->trc_error = false;

	/* SYNCPR, bit[25] implementation has a fixed synchronization period? */
	if (BMVAL(etmidr3, 25, 25))
		drvdata->syncpr = true;
	else
		drvdata->syncpr = false;

	/* STALLCTL, bit[26] is stall control implemented? */
	if (BMVAL(etmidr3, 26, 26))
		drvdata->stallctl = true;
	else
		drvdata->stallctl = false;

	/* SYSSTALL, bit[27] implementation can support stall control? */
	if (BMVAL(etmidr3, 27, 27))
		drvdata->sysstall = true;
	else
		drvdata->sysstall = false;

	/* NUMPROC, bits[30:28] the number of PEs available for tracing */
	drvdata->nr_pe = BMVAL(etmidr3, 28, 30);

	/* NOOVERFLOW, bit[31] is trace overflow prevention supported */
	if (BMVAL(etmidr3, 31, 31))
		drvdata->nooverflow = true;
	else
		drvdata->nooverflow = false;

	/* number of resources trace unit supports */
	etmidr4 = readl_relaxed(drvdata->base + TRCIDR4);
	/* NUMACPAIRS, bits[0:3] number of addr comparator pairs for tracing */
	drvdata->nr_addr_cmp = BMVAL(etmidr4, 0, 3);
	/* NUMPC, bits[15:12] number of PE comparator inputs for tracing */
	drvdata->nr_pe_cmp = BMVAL(etmidr4, 12, 15);
	/* NUMRSPAIR, bits[19:16] the number of resource pairs for tracing */
	drvdata->nr_resource = BMVAL(etmidr4, 16, 19);
	/*
	 * NUMSSCC, bits[23:20] the number of single-shot
	 * comparator control for tracing
	 */
	drvdata->nr_ss_cmp = BMVAL(etmidr4, 20, 23);
	/* NUMCIDC, bits[27:24] number of Context ID comparators for tracing */
	drvdata->numcidc = BMVAL(etmidr4, 24, 27);
	/* NUMVMIDC, bits[31:28] number of VMID comparators for tracing */
	drvdata->numvmidc = BMVAL(etmidr4, 28, 31);

	etmidr5 = readl_relaxed(drvdata->base + TRCIDR5);
	/* NUMEXTIN, bits[8:0] number of external inputs implemented */
	drvdata->nr_ext_inp = BMVAL(etmidr5, 0, 8);
	/* TRACEIDSIZE, bits[21:16] indicates the trace ID width */
	drvdata->trcid_size = BMVAL(etmidr5, 16, 21);
	/* ATBTRIG, bit[22] implementation can support ATB triggers? */
	if (BMVAL(etmidr5, 22, 22))
		drvdata->atbtrig = true;
	else
		drvdata->atbtrig = false;
	/*
	 * LPOVERRIDE, bit[23] implementation supports
	 * low-power state override
	 */
	if (BMVAL(etmidr5, 23, 23))
		drvdata->lpoverride = true;
	else
		drvdata->lpoverride = false;
	/* NUMSEQSTATE, bits[27:25] number of sequencer states implemented */
	drvdata->nrseqstate = BMVAL(etmidr5, 25, 27);
	/* NUMCNTR, bits[30:28] number of counters available for tracing */
	drvdata->nr_cntr = BMVAL(etmidr5, 28, 30);
	CS_LOCK(drvdata->base);
}

static void etm4_init_default_data(struct etmv4_drvdata *drvdata)
{
	int i;

	drvdata->pe_sel = 0x0;
	drvdata->cfg = (ETMv4_MODE_CTXID | ETM_MODE_VMID |
			ETMv4_MODE_TIMESTAMP | ETM_MODE_RETURNSTACK);

	/* disable all events tracing */
	drvdata->eventctrl0 = 0x0;
	drvdata->eventctrl1 = 0x0;

	/* disable stalling */
	drvdata->stall_ctrl = 0x0;

	/* disable timestamp event */
	drvdata->ts_ctrl = 0x0;

	/* enable trace synchronization every 4096 bytes for trace */
	if (drvdata->syncpr == false)
		drvdata->syncfreq = 0xC;

	/*
	 *  enable viewInst to trace everything with start-stop logic in
	 *  started state
	 */
	drvdata->vinst_ctrl |= BIT(0);
	/* set initial state of start-stop logic */
	if (drvdata->nr_addr_cmp)
		drvdata->vinst_ctrl |= BIT(9);

	/* no address range filtering for ViewInst */
	drvdata->viiectlr = 0x0;
	/* no start-stop filtering for ViewInst */
	drvdata->vissctlr = 0x0;

	/* disable seq events */
	for (i = 0; i < drvdata->nrseqstate-1; i++)
		drvdata->seq_ctrl[i] = 0x0;
	drvdata->seq_rst = 0x0;
	drvdata->seq_state = 0x0;

	/* disable external input events */
	drvdata->ext_inp = 0x0;

	for (i = 0; i < drvdata->nr_cntr; i++) {
		drvdata->cntrldvr[i] = 0x0;
		drvdata->cntr_ctrl[i] = 0x0;
		drvdata->cntr_val[i] = 0x0;
	}

	for (i = 2; i < drvdata->nr_resource * 2; i++)
		drvdata->res_ctrl[i] = 0x0;

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		drvdata->ss_ctrl[i] = 0x0;
		drvdata->ss_pe_cmp[i] = 0x0;
	}

	if (drvdata->nr_addr_cmp >= 1) {
		drvdata->addr_val[0] = (unsigned long)_stext;
		drvdata->addr_val[1] = (unsigned long)_etext;
		drvdata->addr_type[0] = ETM_ADDR_TYPE_RANGE;
		drvdata->addr_type[1] = ETM_ADDR_TYPE_RANGE;
	}

	for (i = 0; i < drvdata->numcidc; i++) {
		drvdata->ctxid_pid[i] = 0x0;
		drvdata->ctxid_vpid[i] = 0x0;
	}

	drvdata->ctxid_mask0 = 0x0;
	drvdata->ctxid_mask1 = 0x0;

	for (i = 0; i < drvdata->numvmidc; i++)
		drvdata->vmid_val[i] = 0x0;
	drvdata->vmid_mask0 = 0x0;
	drvdata->vmid_mask1 = 0x0;

	/*
	 * A trace ID value of 0 is invalid, so let's start at some
	 * random value that fits in 7 bits.  ETMv3.x has 0x10 so let's
	 * start at 0x20.
	 */
	drvdata->trcid = 0x20 + drvdata->cpu;
}

static int etm4_cpu_callback(struct notifier_block *nfb, unsigned long action,
			    void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	if (!etmdrvdata[cpu])
		goto out;

	switch (action & (~CPU_TASKS_FROZEN)) {
	case CPU_STARTING:
		spin_lock(&etmdrvdata[cpu]->spinlock);
		if (!etmdrvdata[cpu]->os_unlock) {
			etm4_os_unlock(etmdrvdata[cpu]);
			etmdrvdata[cpu]->os_unlock = true;
		}

		if (etmdrvdata[cpu]->enable)
			etm4_enable_hw(etmdrvdata[cpu]);
		spin_unlock(&etmdrvdata[cpu]->spinlock);
		break;

	case CPU_ONLINE:
		if (etmdrvdata[cpu]->boot_enable &&
			!etmdrvdata[cpu]->sticky_enable)
			coresight_enable(etmdrvdata[cpu]->csdev);
		break;

	case CPU_DYING:
		spin_lock(&etmdrvdata[cpu]->spinlock);
		if (etmdrvdata[cpu]->enable)
			etm4_disable_hw(etmdrvdata[cpu]);
		spin_unlock(&etmdrvdata[cpu]->spinlock);
		break;
	}
out:
	return NOTIFY_OK;
}

static struct notifier_block etm4_cpu_notifier = {
	.notifier_call = etm4_cpu_callback,
};

static int etm4_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct etmv4_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct coresight_desc *desc;
	struct device_node *np = adev->dev.of_node;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		adev->dev.platform_data = pdata;
	}

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	spin_lock_init(&drvdata->spinlock);

	drvdata->cpu = pdata ? pdata->cpu : 0;

	get_online_cpus();
	etmdrvdata[drvdata->cpu] = drvdata;

	if (!smp_call_function_single(drvdata->cpu, etm4_os_unlock, drvdata, 1))
		drvdata->os_unlock = true;

	if (smp_call_function_single(drvdata->cpu,
				etm4_init_arch_data,  drvdata, 1))
		dev_err(dev, "ETM arch init failed\n");

	if (!etm4_count++)
		register_hotcpu_notifier(&etm4_cpu_notifier);

	put_online_cpus();

	if (etm4_arch_supported(drvdata->arch) == false) {
		ret = -EINVAL;
		goto err_arch_supported;
	}
	etm4_init_default_data(drvdata);

	pm_runtime_put(&adev->dev);

	desc->type = CORESIGHT_DEV_TYPE_SOURCE;
	desc->subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc->ops = &etm4_cs_ops;
	desc->pdata = pdata;
	desc->dev = dev;
	desc->groups = coresight_etmv4_groups;
	drvdata->csdev = coresight_register(desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto err_coresight_register;
	}

	dev_info(dev, "%s initialized\n", (char *)id->data);

	if (boot_enable) {
		coresight_enable(drvdata->csdev);
		drvdata->boot_enable = true;
	}

	return 0;

err_arch_supported:
	pm_runtime_put(&adev->dev);
err_coresight_register:
	if (--etm4_count == 0)
		unregister_hotcpu_notifier(&etm4_cpu_notifier);
	return ret;
}

static int etm4_remove(struct amba_device *adev)
{
	struct etmv4_drvdata *drvdata = amba_get_drvdata(adev);

	coresight_unregister(drvdata->csdev);
	if (--etm4_count == 0)
		unregister_hotcpu_notifier(&etm4_cpu_notifier);

	return 0;
}

static struct amba_id etm4_ids[] = {
	{       /* ETM 4.0 - Qualcomm */
		.id	= 0x0003b95d,
		.mask	= 0x0003ffff,
		.data	= "ETM 4.0",
	},
	{       /* ETM 4.0 - Juno board */
		.id	= 0x000bb95e,
		.mask	= 0x000fffff,
		.data	= "ETM 4.0",
	},
	{ 0, 0},
};

static struct amba_driver etm4x_driver = {
	.drv = {
		.name   = "coresight-etm4x",
	},
	.probe		= etm4_probe,
	.remove		= etm4_remove,
	.id_table	= etm4_ids,
};

module_amba_driver(etm4x_driver);
