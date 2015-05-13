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
		writeq_relaxed(drvdata->ctxid_val[i],
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
	&dev_attr_cpu.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_etmv4);

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

	for (i = 0; i < drvdata->numcidc; i++)
		drvdata->ctxid_val[i] = 0x0;
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
