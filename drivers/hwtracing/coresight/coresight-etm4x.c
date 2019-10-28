// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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
#include <linux/coresight-pmu.h>
#include <linux/pm_wakeup.h>
#include <linux/amba/bus.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/pm_runtime.h>
#include <asm/sections.h>
#include <asm/local.h>

#include "coresight-etm4x.h"
#include "coresight-etm-perf.h"

static int boot_enable;
module_param_named(boot_enable, boot_enable, int, S_IRUGO);

/* The number of ETMv4 currently registered */
static int etm4_count;
static struct etmv4_drvdata *etmdrvdata[NR_CPUS];
static void etm4_set_default_config(struct etmv4_config *config);
static int etm4_set_event_filters(struct etmv4_drvdata *drvdata,
				  struct perf_event *event);

static enum cpuhp_state hp_online;

static void etm4_os_unlock(struct etmv4_drvdata *drvdata)
{
	/* Writing any value to ETMOSLAR unlocks the trace registers */
	writel_relaxed(0x0, drvdata->base + TRCOSLAR);
	drvdata->os_unlock = true;
	isb();
}

static bool etm4_arch_supported(u8 arch)
{
	/* Mask out the minor version number */
	switch (arch & 0xf0) {
	case ETM_ARCH_V4:
		break;
	default:
		return false;
	}
	return true;
}

static int etm4_cpu_id(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->cpu;
}

static int etm4_trace_id(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->trcid;
}

static void etm4_enable_hw(void *info)
{
	int i;
	struct etmv4_drvdata *drvdata = info;
	struct etmv4_config *config = &drvdata->config;

	CS_UNLOCK(drvdata->base);

	etm4_os_unlock(drvdata);

	/* Disable the trace unit before programming trace registers */
	writel_relaxed(0, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go up */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 1))
		dev_err(drvdata->dev,
			"timeout while waiting for Idle Trace Status\n");

	writel_relaxed(config->pe_sel, drvdata->base + TRCPROCSELR);
	writel_relaxed(config->cfg, drvdata->base + TRCCONFIGR);
	/* nothing specific implemented */
	writel_relaxed(0x0, drvdata->base + TRCAUXCTLR);
	writel_relaxed(config->eventctrl0, drvdata->base + TRCEVENTCTL0R);
	writel_relaxed(config->eventctrl1, drvdata->base + TRCEVENTCTL1R);
	writel_relaxed(config->stall_ctrl, drvdata->base + TRCSTALLCTLR);
	writel_relaxed(config->ts_ctrl, drvdata->base + TRCTSCTLR);
	writel_relaxed(config->syncfreq, drvdata->base + TRCSYNCPR);
	writel_relaxed(config->ccctlr, drvdata->base + TRCCCCTLR);
	writel_relaxed(config->bb_ctrl, drvdata->base + TRCBBCTLR);
	writel_relaxed(drvdata->trcid, drvdata->base + TRCTRACEIDR);
	writel_relaxed(config->vinst_ctrl, drvdata->base + TRCVICTLR);
	writel_relaxed(config->viiectlr, drvdata->base + TRCVIIECTLR);
	writel_relaxed(config->vissctlr,
		       drvdata->base + TRCVISSCTLR);
	writel_relaxed(config->vipcssctlr,
		       drvdata->base + TRCVIPCSSCTLR);
	for (i = 0; i < drvdata->nrseqstate - 1; i++)
		writel_relaxed(config->seq_ctrl[i],
			       drvdata->base + TRCSEQEVRn(i));
	writel_relaxed(config->seq_rst, drvdata->base + TRCSEQRSTEVR);
	writel_relaxed(config->seq_state, drvdata->base + TRCSEQSTR);
	writel_relaxed(config->ext_inp, drvdata->base + TRCEXTINSELR);
	for (i = 0; i < drvdata->nr_cntr; i++) {
		writel_relaxed(config->cntrldvr[i],
			       drvdata->base + TRCCNTRLDVRn(i));
		writel_relaxed(config->cntr_ctrl[i],
			       drvdata->base + TRCCNTCTLRn(i));
		writel_relaxed(config->cntr_val[i],
			       drvdata->base + TRCCNTVRn(i));
	}

	/* Resource selector pair 0 is always implemented and reserved */
	for (i = 0; i < drvdata->nr_resource * 2; i++)
		writel_relaxed(config->res_ctrl[i],
			       drvdata->base + TRCRSCTLRn(i));

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		writel_relaxed(config->ss_ctrl[i],
			       drvdata->base + TRCSSCCRn(i));
		writel_relaxed(config->ss_status[i],
			       drvdata->base + TRCSSCSRn(i));
		writel_relaxed(config->ss_pe_cmp[i],
			       drvdata->base + TRCSSPCICRn(i));
	}
	for (i = 0; i < drvdata->nr_addr_cmp; i++) {
		writeq_relaxed(config->addr_val[i],
			       drvdata->base + TRCACVRn(i));
		writeq_relaxed(config->addr_acc[i],
			       drvdata->base + TRCACATRn(i));
	}
	for (i = 0; i < drvdata->numcidc; i++)
		writeq_relaxed(config->ctxid_pid[i],
			       drvdata->base + TRCCIDCVRn(i));
	writel_relaxed(config->ctxid_mask0, drvdata->base + TRCCIDCCTLR0);
	writel_relaxed(config->ctxid_mask1, drvdata->base + TRCCIDCCTLR1);

	for (i = 0; i < drvdata->numvmidc; i++)
		writeq_relaxed(config->vmid_val[i],
			       drvdata->base + TRCVMIDCVRn(i));
	writel_relaxed(config->vmid_mask0, drvdata->base + TRCVMIDCCTLR0);
	writel_relaxed(config->vmid_mask1, drvdata->base + TRCVMIDCCTLR1);

	/*
	 * Request to keep the trace unit powered and also
	 * emulation of powerdown
	 */
	writel_relaxed(readl_relaxed(drvdata->base + TRCPDCR) | TRCPDCR_PU,
		       drvdata->base + TRCPDCR);

	/* Enable the trace unit */
	writel_relaxed(1, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go back down to '0' */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 0))
		dev_err(drvdata->dev,
			"timeout while waiting for Idle Trace Status\n");
	/*
	 * As recommended by section 4.3.7 ("Synchronization when using the
	 * memory-mapped interface") of ARM IHI 0064D
	 */
	dsb(sy);
	isb();

	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d enable smp call done\n", drvdata->cpu);
}

static int etm4_parse_event_config(struct etmv4_drvdata *drvdata,
				   struct perf_event *event)
{
	int ret = 0;
	struct etmv4_config *config = &drvdata->config;
	struct perf_event_attr *attr = &event->attr;

	if (!attr) {
		ret = -EINVAL;
		goto out;
	}

	/* Clear configuration from previous run */
	memset(config, 0, sizeof(struct etmv4_config));

	if (attr->exclude_kernel)
		config->mode = ETM_MODE_EXCL_KERN;

	if (attr->exclude_user)
		config->mode = ETM_MODE_EXCL_USER;

	/* Always start from the default config */
	etm4_set_default_config(config);

	/* Configure filters specified on the perf cmd line, if any. */
	ret = etm4_set_event_filters(drvdata, event);
	if (ret)
		goto out;

	/* Go from generic option to ETMv4 specifics */
	if (attr->config & BIT(ETM_OPT_CYCACC)) {
		config->cfg |= BIT(4);
		/* TRM: Must program this for cycacc to work */
		config->ccctlr = ETM_CYC_THRESHOLD_DEFAULT;
	}
	if (attr->config & BIT(ETM_OPT_TS))
		/* bit[11], Global timestamp tracing bit */
		config->cfg |= BIT(11);
	/* return stack - enable if selected and supported */
	if ((attr->config & BIT(ETM_OPT_RETSTK)) && drvdata->retstack)
		/* bit[12], Return stack enable bit */
		config->cfg |= BIT(12);

out:
	return ret;
}

static int etm4_enable_perf(struct coresight_device *csdev,
			    struct perf_event *event)
{
	int ret = 0;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(drvdata->cpu != smp_processor_id())) {
		ret = -EINVAL;
		goto out;
	}

	/* Configure the tracer based on the session's specifics */
	ret = etm4_parse_event_config(drvdata, event);
	if (ret)
		goto out;
	/* And enable it */
	etm4_enable_hw(drvdata);

out:
	return ret;
}

static int etm4_enable_sysfs(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;

	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm4_enable_hw on the cpu whose ETM is being enabled
	 * ensures that register writes occur when cpu is powered.
	 */
	ret = smp_call_function_single(drvdata->cpu,
				       etm4_enable_hw, drvdata, 1);
	if (ret)
		goto err;

	drvdata->sticky_enable = true;
	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "ETM tracing enabled\n");
	return 0;

err:
	spin_unlock(&drvdata->spinlock);
	return ret;
}

static int etm4_enable(struct coresight_device *csdev,
		       struct perf_event *event, u32 mode)
{
	int ret;
	u32 val;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	val = local_cmpxchg(&drvdata->mode, CS_MODE_DISABLED, mode);

	/* Someone is already using the tracer */
	if (val)
		return -EBUSY;

	switch (mode) {
	case CS_MODE_SYSFS:
		ret = etm4_enable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		ret = etm4_enable_perf(csdev, event);
		break;
	default:
		ret = -EINVAL;
	}

	/* The tracer didn't start */
	if (ret)
		local_set(&drvdata->mode, CS_MODE_DISABLED);

	return ret;
}

static void etm4_disable_hw(void *info)
{
	u32 control;
	struct etmv4_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);

	/* power can be removed from the trace unit now */
	control = readl_relaxed(drvdata->base + TRCPDCR);
	control &= ~TRCPDCR_PU;
	writel_relaxed(control, drvdata->base + TRCPDCR);

	control = readl_relaxed(drvdata->base + TRCPRGCTLR);

	/* EN, bit[0] Trace unit enable bit */
	control &= ~0x1;

	/*
	 * Make sure everything completes before disabling, as recommended
	 * by section 7.3.77 ("TRCVICTLR, ViewInst Main Control Register,
	 * SSTATUS") of ARM IHI 0064D
	 */
	dsb(sy);
	isb();
	writel_relaxed(control, drvdata->base + TRCPRGCTLR);

	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d disable smp call done\n", drvdata->cpu);
}

static int etm4_disable_perf(struct coresight_device *csdev,
			     struct perf_event *event)
{
	u32 control;
	struct etm_filters *filters = event->hw.addr_filters;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(drvdata->cpu != smp_processor_id()))
		return -EINVAL;

	etm4_disable_hw(drvdata);

	/*
	 * Check if the start/stop logic was active when the unit was stopped.
	 * That way we can re-enable the start/stop logic when the process is
	 * scheduled again.  Configuration of the start/stop logic happens in
	 * function etm4_set_event_filters().
	 */
	control = readl_relaxed(drvdata->base + TRCVICTLR);
	/* TRCVICTLR::SSSTATUS, bit[9] */
	filters->ssstatus = (control & BIT(9));

	return 0;
}

static void etm4_disable_sysfs(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * Taking hotplug lock here protects from clocks getting disabled
	 * with tracing being left on (crash scenario) if user disable occurs
	 * after cpu online mask indicates the cpu is offline but before the
	 * DYING hotplug callback is serviced by the ETM driver.
	 */
	cpus_read_lock();
	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm4_disable_hw on the cpu whose ETM is being disabled
	 * ensures that register writes occur when cpu is powered.
	 */
	smp_call_function_single(drvdata->cpu, etm4_disable_hw, drvdata, 1);

	spin_unlock(&drvdata->spinlock);
	cpus_read_unlock();

	dev_info(drvdata->dev, "ETM tracing disabled\n");
}

static void etm4_disable(struct coresight_device *csdev,
			 struct perf_event *event)
{
	u32 mode;
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * For as long as the tracer isn't disabled another entity can't
	 * change its status.  As such we can read the status here without
	 * fearing it will change under us.
	 */
	mode = local_read(&drvdata->mode);

	switch (mode) {
	case CS_MODE_DISABLED:
		break;
	case CS_MODE_SYSFS:
		etm4_disable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		etm4_disable_perf(csdev, event);
		break;
	}

	if (mode)
		local_set(&drvdata->mode, CS_MODE_DISABLED);
}

static const struct coresight_ops_source etm4_source_ops = {
	.cpu_id		= etm4_cpu_id,
	.trace_id	= etm4_trace_id,
	.enable		= etm4_enable,
	.disable	= etm4_disable,
};

static const struct coresight_ops etm4_cs_ops = {
	.source_ops	= &etm4_source_ops,
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

	/* Make sure all registers are accessible */
	etm4_os_unlock(drvdata);

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
	/*
	 * NUMRSPAIR, bits[19:16]
	 * The number of resource pairs conveyed by the HW starts at 0, i.e a
	 * value of 0x0 indicate 1 resource pair, 0x1 indicate two and so on.
	 * As such add 1 to the value of NUMRSPAIR for a better representation.
	 */
	drvdata->nr_resource = BMVAL(etmidr4, 16, 19) + 1;
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

static void etm4_set_default_config(struct etmv4_config *config)
{
	/* disable all events tracing */
	config->eventctrl0 = 0x0;
	config->eventctrl1 = 0x0;

	/* disable stalling */
	config->stall_ctrl = 0x0;

	/* enable trace synchronization every 4096 bytes, if available */
	config->syncfreq = 0xC;

	/* disable timestamp event */
	config->ts_ctrl = 0x0;

	/* TRCVICTLR::EVENT = 0x01, select the always on logic */
	config->vinst_ctrl |= BIT(0);
}

static u64 etm4_get_access_type(struct etmv4_config *config)
{
	u64 access_type = 0;

	/*
	 * EXLEVEL_NS, bits[15:12]
	 * The Exception levels are:
	 *   Bit[12] Exception level 0 - Application
	 *   Bit[13] Exception level 1 - OS
	 *   Bit[14] Exception level 2 - Hypervisor
	 *   Bit[15] Never implemented
	 *
	 * Always stay away from hypervisor mode.
	 */
	access_type = ETM_EXLEVEL_NS_HYP;

	if (config->mode & ETM_MODE_EXCL_KERN)
		access_type |= ETM_EXLEVEL_NS_OS;

	if (config->mode & ETM_MODE_EXCL_USER)
		access_type |= ETM_EXLEVEL_NS_APP;

	/*
	 * EXLEVEL_S, bits[11:8], don't trace anything happening
	 * in secure state.
	 */
	access_type |= (ETM_EXLEVEL_S_APP	|
			ETM_EXLEVEL_S_OS	|
			ETM_EXLEVEL_S_HYP);

	return access_type;
}

static void etm4_set_comparator_filter(struct etmv4_config *config,
				       u64 start, u64 stop, int comparator)
{
	u64 access_type = etm4_get_access_type(config);

	/* First half of default address comparator */
	config->addr_val[comparator] = start;
	config->addr_acc[comparator] = access_type;
	config->addr_type[comparator] = ETM_ADDR_TYPE_RANGE;

	/* Second half of default address comparator */
	config->addr_val[comparator + 1] = stop;
	config->addr_acc[comparator + 1] = access_type;
	config->addr_type[comparator + 1] = ETM_ADDR_TYPE_RANGE;

	/*
	 * Configure the ViewInst function to include this address range
	 * comparator.
	 *
	 * @comparator is divided by two since it is the index in the
	 * etmv4_config::addr_val array but register TRCVIIECTLR deals with
	 * address range comparator _pairs_.
	 *
	 * Therefore:
	 *	index 0 -> compatator pair 0
	 *	index 2 -> comparator pair 1
	 *	index 4 -> comparator pair 2
	 *	...
	 *	index 14 -> comparator pair 7
	 */
	config->viiectlr |= BIT(comparator / 2);
}

static void etm4_set_start_stop_filter(struct etmv4_config *config,
				       u64 address, int comparator,
				       enum etm_addr_type type)
{
	int shift;
	u64 access_type = etm4_get_access_type(config);

	/* Configure the comparator */
	config->addr_val[comparator] = address;
	config->addr_acc[comparator] = access_type;
	config->addr_type[comparator] = type;

	/*
	 * Configure ViewInst Start-Stop control register.
	 * Addresses configured to start tracing go from bit 0 to n-1,
	 * while those configured to stop tracing from 16 to 16 + n-1.
	 */
	shift = (type == ETM_ADDR_TYPE_START ? 0 : 16);
	config->vissctlr |= BIT(shift + comparator);
}

static void etm4_set_default_filter(struct etmv4_config *config)
{
	u64 start, stop;

	/*
	 * Configure address range comparator '0' to encompass all
	 * possible addresses.
	 */
	start = 0x0;
	stop = ~0x0;

	etm4_set_comparator_filter(config, start, stop,
				   ETM_DEFAULT_ADDR_COMP);

	/*
	 * TRCVICTLR::SSSTATUS == 1, the start-stop logic is
	 * in the started state
	 */
	config->vinst_ctrl |= BIT(9);

	/* No start-stop filtering for ViewInst */
	config->vissctlr = 0x0;
}

static void etm4_set_default(struct etmv4_config *config)
{
	if (WARN_ON_ONCE(!config))
		return;

	/*
	 * Make default initialisation trace everything
	 *
	 * Select the "always true" resource selector on the
	 * "Enablign Event" line and configure address range comparator
	 * '0' to trace all the possible address range.  From there
	 * configure the "include/exclude" engine to include address
	 * range comparator '0'.
	 */
	etm4_set_default_config(config);
	etm4_set_default_filter(config);
}

static int etm4_get_next_comparator(struct etmv4_drvdata *drvdata, u32 type)
{
	int nr_comparator, index = 0;
	struct etmv4_config *config = &drvdata->config;

	/*
	 * nr_addr_cmp holds the number of comparator _pair_, so time 2
	 * for the total number of comparators.
	 */
	nr_comparator = drvdata->nr_addr_cmp * 2;

	/* Go through the tally of comparators looking for a free one. */
	while (index < nr_comparator) {
		switch (type) {
		case ETM_ADDR_TYPE_RANGE:
			if (config->addr_type[index] == ETM_ADDR_TYPE_NONE &&
			    config->addr_type[index + 1] == ETM_ADDR_TYPE_NONE)
				return index;

			/* Address range comparators go in pairs */
			index += 2;
			break;
		case ETM_ADDR_TYPE_START:
		case ETM_ADDR_TYPE_STOP:
			if (config->addr_type[index] == ETM_ADDR_TYPE_NONE)
				return index;

			/* Start/stop address can have odd indexes */
			index += 1;
			break;
		default:
			return -EINVAL;
		}
	}

	/* If we are here all the comparators have been used. */
	return -ENOSPC;
}

static int etm4_set_event_filters(struct etmv4_drvdata *drvdata,
				  struct perf_event *event)
{
	int i, comparator, ret = 0;
	u64 address;
	struct etmv4_config *config = &drvdata->config;
	struct etm_filters *filters = event->hw.addr_filters;

	if (!filters)
		goto default_filter;

	/* Sync events with what Perf got */
	perf_event_addr_filters_sync(event);

	/*
	 * If there are no filters to deal with simply go ahead with
	 * the default filter, i.e the entire address range.
	 */
	if (!filters->nr_filters)
		goto default_filter;

	for (i = 0; i < filters->nr_filters; i++) {
		struct etm_filter *filter = &filters->etm_filter[i];
		enum etm_addr_type type = filter->type;

		/* See if a comparator is free. */
		comparator = etm4_get_next_comparator(drvdata, type);
		if (comparator < 0) {
			ret = comparator;
			goto out;
		}

		switch (type) {
		case ETM_ADDR_TYPE_RANGE:
			etm4_set_comparator_filter(config,
						   filter->start_addr,
						   filter->stop_addr,
						   comparator);
			/*
			 * TRCVICTLR::SSSTATUS == 1, the start-stop logic is
			 * in the started state
			 */
			config->vinst_ctrl |= BIT(9);

			/* No start-stop filtering for ViewInst */
			config->vissctlr = 0x0;
			break;
		case ETM_ADDR_TYPE_START:
		case ETM_ADDR_TYPE_STOP:
			/* Get the right start or stop address */
			address = (type == ETM_ADDR_TYPE_START ?
				   filter->start_addr :
				   filter->stop_addr);

			/* Configure comparator */
			etm4_set_start_stop_filter(config, address,
						   comparator, type);

			/*
			 * If filters::ssstatus == 1, trace acquisition was
			 * started but the process was yanked away before the
			 * the stop address was hit.  As such the start/stop
			 * logic needs to be re-started so that tracing can
			 * resume where it left.
			 *
			 * The start/stop logic status when a process is
			 * scheduled out is checked in function
			 * etm4_disable_perf().
			 */
			if (filters->ssstatus)
				config->vinst_ctrl |= BIT(9);

			/* No include/exclude filtering for ViewInst */
			config->viiectlr = 0x0;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
	}

	goto out;


default_filter:
	etm4_set_default_filter(config);

out:
	return ret;
}

void etm4_config_trace_mode(struct etmv4_config *config)
{
	u32 addr_acc, mode;

	mode = config->mode;
	mode &= (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER);

	/* excluding kernel AND user space doesn't make sense */
	WARN_ON_ONCE(mode == (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER));

	/* nothing to do if neither flags are set */
	if (!(mode & ETM_MODE_EXCL_KERN) && !(mode & ETM_MODE_EXCL_USER))
		return;

	addr_acc = config->addr_acc[ETM_DEFAULT_ADDR_COMP];
	/* clear default config */
	addr_acc &= ~(ETM_EXLEVEL_NS_APP | ETM_EXLEVEL_NS_OS);

	/*
	 * EXLEVEL_NS, bits[15:12]
	 * The Exception levels are:
	 *   Bit[12] Exception level 0 - Application
	 *   Bit[13] Exception level 1 - OS
	 *   Bit[14] Exception level 2 - Hypervisor
	 *   Bit[15] Never implemented
	 */
	if (mode & ETM_MODE_EXCL_KERN)
		addr_acc |= ETM_EXLEVEL_NS_OS;
	else
		addr_acc |= ETM_EXLEVEL_NS_APP;

	config->addr_acc[ETM_DEFAULT_ADDR_COMP] = addr_acc;
	config->addr_acc[ETM_DEFAULT_ADDR_COMP + 1] = addr_acc;
}

static int etm4_online_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	if (etmdrvdata[cpu]->boot_enable && !etmdrvdata[cpu]->sticky_enable)
		coresight_enable(etmdrvdata[cpu]->csdev);
	return 0;
}

static int etm4_starting_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	spin_lock(&etmdrvdata[cpu]->spinlock);
	if (!etmdrvdata[cpu]->os_unlock) {
		etm4_os_unlock(etmdrvdata[cpu]);
		etmdrvdata[cpu]->os_unlock = true;
	}

	if (local_read(&etmdrvdata[cpu]->mode))
		etm4_enable_hw(etmdrvdata[cpu]);
	spin_unlock(&etmdrvdata[cpu]->spinlock);
	return 0;
}

static int etm4_dying_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	spin_lock(&etmdrvdata[cpu]->spinlock);
	if (local_read(&etmdrvdata[cpu]->mode))
		etm4_disable_hw(etmdrvdata[cpu]);
	spin_unlock(&etmdrvdata[cpu]->spinlock);
	return 0;
}

static void etm4_init_trace_id(struct etmv4_drvdata *drvdata)
{
	drvdata->trcid = coresight_get_trace_id(drvdata->cpu);
}

static int etm4_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct etmv4_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct coresight_desc desc = { 0 };
	struct device_node *np = adev->dev.of_node;

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

	cpus_read_lock();
	etmdrvdata[drvdata->cpu] = drvdata;

	if (smp_call_function_single(drvdata->cpu,
				etm4_init_arch_data,  drvdata, 1))
		dev_err(dev, "ETM arch init failed\n");

	if (!etm4_count++) {
		cpuhp_setup_state_nocalls_cpuslocked(CPUHP_AP_ARM_CORESIGHT_STARTING,
						     "arm/coresight4:starting",
						     etm4_starting_cpu, etm4_dying_cpu);
		ret = cpuhp_setup_state_nocalls_cpuslocked(CPUHP_AP_ONLINE_DYN,
							   "arm/coresight4:online",
							   etm4_online_cpu, NULL);
		if (ret < 0)
			goto err_arch_supported;
		hp_online = ret;
	}

	cpus_read_unlock();

	if (etm4_arch_supported(drvdata->arch) == false) {
		ret = -EINVAL;
		goto err_arch_supported;
	}

	etm4_init_trace_id(drvdata);
	etm4_set_default(&drvdata->config);

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &etm4_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = coresight_etmv4_groups;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto err_arch_supported;
	}

	ret = etm_perf_symlink(drvdata->csdev, true);
	if (ret) {
		coresight_unregister(drvdata->csdev);
		goto err_arch_supported;
	}

	pm_runtime_put(&adev->dev);
	dev_info(dev, "CPU%d: ETM v%d.%d initialized\n",
		 drvdata->cpu, drvdata->arch >> 4, drvdata->arch & 0xf);

	if (boot_enable) {
		coresight_enable(drvdata->csdev);
		drvdata->boot_enable = true;
	}

	return 0;

err_arch_supported:
	if (--etm4_count == 0) {
		cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING);
		if (hp_online)
			cpuhp_remove_state_nocalls(hp_online);
	}
	return ret;
}

#define ETM4x_AMBA_ID(pid)			\
	{					\
		.id	= pid,			\
		.mask	= 0x000fffff,		\
	}

static const struct amba_id etm4_ids[] = {
	ETM4x_AMBA_ID(0x000bb95d),		/* Cortex-A53 */
	ETM4x_AMBA_ID(0x000bb95e),		/* Cortex-A57 */
	ETM4x_AMBA_ID(0x000bb95a),		/* Cortex-A72 */
	ETM4x_AMBA_ID(0x000bb959),		/* Cortex-A73 */
	ETM4x_AMBA_ID(0x000bb9da),		/* Cortex-A35 */
	{},
};

static struct amba_driver etm4x_driver = {
	.drv = {
		.name   = "coresight-etm4x",
		.suppress_bind_attrs = true,
	},
	.probe		= etm4_probe,
	.id_table	= etm4_ids,
};
builtin_amba_driver(etm4x_driver);
