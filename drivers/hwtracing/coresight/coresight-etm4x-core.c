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
#include <linux/cpu_pm.h>
#include <linux/coresight.h>
#include <linux/coresight-pmu.h>
#include <linux/pm_wakeup.h>
#include <linux/amba/bus.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <asm/sections.h>
#include <asm/local.h>
#include <asm/virt.h>

#include "coresight-etm4x.h"
#include "coresight-etm-perf.h"

static int boot_enable;
module_param(boot_enable, int, 0444);
MODULE_PARM_DESC(boot_enable, "Enable tracing on boot");

#define PARAM_PM_SAVE_FIRMWARE	  0 /* save self-hosted state as per firmware */
#define PARAM_PM_SAVE_NEVER	  1 /* never save any state */
#define PARAM_PM_SAVE_SELF_HOSTED 2 /* save self-hosted state only */

static int pm_save_enable = PARAM_PM_SAVE_FIRMWARE;
module_param(pm_save_enable, int, 0444);
MODULE_PARM_DESC(pm_save_enable,
	"Save/restore state on power down: 1 = never, 2 = self-hosted");

static struct etmv4_drvdata *etmdrvdata[NR_CPUS];
static void etm4_set_default_config(struct etmv4_config *config);
static int etm4_set_event_filters(struct etmv4_drvdata *drvdata,
				  struct perf_event *event);
static u64 etm4_get_access_type(struct etmv4_config *config);

static enum cpuhp_state hp_online;

static void etm4_os_unlock(struct etmv4_drvdata *drvdata)
{
	/* Writing 0 to TRCOSLAR unlocks the trace registers */
	writel_relaxed(0x0, drvdata->base + TRCOSLAR);
	drvdata->os_unlock = true;
	isb();
}

static void etm4_os_lock(struct etmv4_drvdata *drvdata)
{
	/* Writing 0x1 to TRCOSLAR locks the trace registers */
	writel_relaxed(0x1, drvdata->base + TRCOSLAR);
	drvdata->os_unlock = false;
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

struct etm4_enable_arg {
	struct etmv4_drvdata *drvdata;
	int rc;
};

static int etm4_enable_hw(struct etmv4_drvdata *drvdata)
{
	int i, rc;
	struct etmv4_config *config = &drvdata->config;
	struct device *etm_dev = &drvdata->csdev->dev;

	CS_UNLOCK(drvdata->base);

	etm4_os_unlock(drvdata);

	rc = coresight_claim_device_unlocked(drvdata->base);
	if (rc)
		goto done;

	/* Disable the trace unit before programming trace registers */
	writel_relaxed(0, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go up */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 1))
		dev_err(etm_dev,
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

	/*
	 * Resource selector pair 0 is always implemented and reserved.  As
	 * such start at 2.
	 */
	for (i = 2; i < drvdata->nr_resource * 2; i++)
		writel_relaxed(config->res_ctrl[i],
			       drvdata->base + TRCRSCTLRn(i));

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		/* always clear status bit on restart if using single-shot */
		if (config->ss_ctrl[i] || config->ss_pe_cmp[i])
			config->ss_status[i] &= ~BIT(31);
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

	if (!drvdata->skip_power_up) {
		/*
		 * Request to keep the trace unit powered and also
		 * emulation of powerdown
		 */
		writel_relaxed(readl_relaxed(drvdata->base + TRCPDCR) |
			       TRCPDCR_PU, drvdata->base + TRCPDCR);
	}

	/* Enable the trace unit */
	writel_relaxed(1, drvdata->base + TRCPRGCTLR);

	/* wait for TRCSTATR.IDLE to go back down to '0' */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 0))
		dev_err(etm_dev,
			"timeout while waiting for Idle Trace Status\n");

	/*
	 * As recommended by section 4.3.7 ("Synchronization when using the
	 * memory-mapped interface") of ARM IHI 0064D
	 */
	dsb(sy);
	isb();

done:
	CS_LOCK(drvdata->base);

	dev_dbg(etm_dev, "cpu: %d enable smp call done: %d\n",
		drvdata->cpu, rc);
	return rc;
}

static void etm4_enable_hw_smp_call(void *info)
{
	struct etm4_enable_arg *arg = info;

	if (WARN_ON(!arg))
		return;
	arg->rc = etm4_enable_hw(arg->drvdata);
}

/*
 * The goal of function etm4_config_timestamp_event() is to configure a
 * counter that will tell the tracer to emit a timestamp packet when it
 * reaches zero.  This is done in order to get a more fine grained idea
 * of when instructions are executed so that they can be correlated
 * with execution on other CPUs.
 *
 * To do this the counter itself is configured to self reload and
 * TRCRSCTLR1 (always true) used to get the counter to decrement.  From
 * there a resource selector is configured with the counter and the
 * timestamp control register to use the resource selector to trigger the
 * event that will insert a timestamp packet in the stream.
 */
static int etm4_config_timestamp_event(struct etmv4_drvdata *drvdata)
{
	int ctridx, ret = -EINVAL;
	int counter, rselector;
	u32 val = 0;
	struct etmv4_config *config = &drvdata->config;

	/* No point in trying if we don't have at least one counter */
	if (!drvdata->nr_cntr)
		goto out;

	/* Find a counter that hasn't been initialised */
	for (ctridx = 0; ctridx < drvdata->nr_cntr; ctridx++)
		if (config->cntr_val[ctridx] == 0)
			break;

	/* All the counters have been configured already, bail out */
	if (ctridx == drvdata->nr_cntr) {
		pr_debug("%s: no available counter found\n", __func__);
		ret = -ENOSPC;
		goto out;
	}

	/*
	 * Searching for an available resource selector to use, starting at
	 * '2' since every implementation has at least 2 resource selector.
	 * ETMIDR4 gives the number of resource selector _pairs_,
	 * hence multiply by 2.
	 */
	for (rselector = 2; rselector < drvdata->nr_resource * 2; rselector++)
		if (!config->res_ctrl[rselector])
			break;

	if (rselector == drvdata->nr_resource * 2) {
		pr_debug("%s: no available resource selector found\n",
			 __func__);
		ret = -ENOSPC;
		goto out;
	}

	/* Remember what counter we used */
	counter = 1 << ctridx;

	/*
	 * Initialise original and reload counter value to the smallest
	 * possible value in order to get as much precision as we can.
	 */
	config->cntr_val[ctridx] = 1;
	config->cntrldvr[ctridx] = 1;

	/* Set the trace counter control register */
	val =  0x1 << 16	|  /* Bit 16, reload counter automatically */
	       0x0 << 7		|  /* Select single resource selector */
	       0x1;		   /* Resource selector 1, i.e always true */

	config->cntr_ctrl[ctridx] = val;

	val = 0x2 << 16		| /* Group 0b0010 - Counter and sequencers */
	      counter << 0;	  /* Counter to use */

	config->res_ctrl[rselector] = val;

	val = 0x0 << 7		| /* Select single resource selector */
	      rselector;	  /* Resource selector */

	config->ts_ctrl = val;

	ret = 0;
out:
	return ret;
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
	if (attr->config & BIT(ETM_OPT_TS)) {
		/*
		 * Configure timestamps to be emitted at regular intervals in
		 * order to correlate instructions executed on different CPUs
		 * (CPU-wide trace scenarios).
		 */
		ret = etm4_config_timestamp_event(drvdata);

		/*
		 * No need to go further if timestamp intervals can't
		 * be configured.
		 */
		if (ret)
			goto out;

		/* bit[11], Global timestamp tracing bit */
		config->cfg |= BIT(11);
	}

	if (attr->config & BIT(ETM_OPT_CTXTID))
		/* bit[6], Context ID tracing bit */
		config->cfg |= BIT(ETM4_CFG_BIT_CTXTID);

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
	ret = etm4_enable_hw(drvdata);

out:
	return ret;
}

static int etm4_enable_sysfs(struct coresight_device *csdev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct etm4_enable_arg arg = { };
	int ret;

	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm4_enable_hw on the cpu whose ETM is being enabled
	 * ensures that register writes occur when cpu is powered.
	 */
	arg.drvdata = drvdata;
	ret = smp_call_function_single(drvdata->cpu,
				       etm4_enable_hw_smp_call, &arg, 1);
	if (!ret)
		ret = arg.rc;
	if (!ret)
		drvdata->sticky_enable = true;
	spin_unlock(&drvdata->spinlock);

	if (!ret)
		dev_dbg(&csdev->dev, "ETM tracing enabled\n");
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
	struct etmv4_config *config = &drvdata->config;
	struct device *etm_dev = &drvdata->csdev->dev;
	int i;

	CS_UNLOCK(drvdata->base);

	if (!drvdata->skip_power_up) {
		/* power can be removed from the trace unit now */
		control = readl_relaxed(drvdata->base + TRCPDCR);
		control &= ~TRCPDCR_PU;
		writel_relaxed(control, drvdata->base + TRCPDCR);
	}

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

	/* wait for TRCSTATR.PMSTABLE to go to '1' */
	if (coresight_timeout(drvdata->base, TRCSTATR,
			      TRCSTATR_PMSTABLE_BIT, 1))
		dev_err(etm_dev,
			"timeout while waiting for PM stable Trace Status\n");

	/* read the status of the single shot comparators */
	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		config->ss_status[i] =
			readl_relaxed(drvdata->base + TRCSSCSRn(i));
	}

	/* read back the current counter values */
	for (i = 0; i < drvdata->nr_cntr; i++) {
		config->cntr_val[i] =
			readl_relaxed(drvdata->base + TRCCNTVRn(i));
	}

	coresight_disclaim_device_unlocked(drvdata->base);

	CS_LOCK(drvdata->base);

	dev_dbg(&drvdata->csdev->dev,
		"cpu: %d disable smp call done\n", drvdata->cpu);
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

	dev_dbg(&csdev->dev, "ETM tracing disabled\n");
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
	int i;

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
	drvdata->config.arch = drvdata->arch;

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
	 *
	 * For ETM v4.3 and later, 0x0 means 0, and no pairs are available -
	 * the default TRUE and FALSE resource selectors are omitted.
	 * Otherwise for values 0x1 and above the number is N + 1 as per v4.2.
	 */
	drvdata->nr_resource = BMVAL(etmidr4, 16, 19);
	if ((drvdata->arch < ETM4X_ARCH_4V3) || (drvdata->nr_resource > 0))
		drvdata->nr_resource += 1;
	/*
	 * NUMSSCC, bits[23:20] the number of single-shot
	 * comparator control for tracing. Read any status regs as these
	 * also contain RO capability data.
	 */
	drvdata->nr_ss_cmp = BMVAL(etmidr4, 20, 23);
	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		drvdata->config.ss_status[i] =
			readl_relaxed(drvdata->base + TRCSSCSRn(i));
	}
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

/* Set ELx trace filter access in the TRCVICTLR register */
static void etm4_set_victlr_access(struct etmv4_config *config)
{
	u64 access_type;

	config->vinst_ctrl &= ~(ETM_EXLEVEL_S_VICTLR_MASK | ETM_EXLEVEL_NS_VICTLR_MASK);

	/*
	 * TRCVICTLR::EXLEVEL_NS:EXLEVELS: Set kernel / user filtering
	 * bits in vinst_ctrl, same bit pattern as TRCACATRn values returned by
	 * etm4_get_access_type() but with a relative shift in this register.
	 */
	access_type = etm4_get_access_type(config) << ETM_EXLEVEL_LSHIFT_TRCVICTLR;
	config->vinst_ctrl |= (u32)access_type;
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
	config->vinst_ctrl = BIT(0);

	/* TRCVICTLR::EXLEVEL_NS:EXLEVELS: Set kernel / user filtering */
	etm4_set_victlr_access(config);
}

static u64 etm4_get_ns_access_type(struct etmv4_config *config)
{
	u64 access_type = 0;

	/*
	 * EXLEVEL_NS, bits[15:12]
	 * The Exception levels are:
	 *   Bit[12] Exception level 0 - Application
	 *   Bit[13] Exception level 1 - OS
	 *   Bit[14] Exception level 2 - Hypervisor
	 *   Bit[15] Never implemented
	 */
	if (!is_kernel_in_hyp_mode()) {
		/* Stay away from hypervisor mode for non-VHE */
		access_type =  ETM_EXLEVEL_NS_HYP;
		if (config->mode & ETM_MODE_EXCL_KERN)
			access_type |= ETM_EXLEVEL_NS_OS;
	} else if (config->mode & ETM_MODE_EXCL_KERN) {
		access_type = ETM_EXLEVEL_NS_HYP;
	}

	if (config->mode & ETM_MODE_EXCL_USER)
		access_type |= ETM_EXLEVEL_NS_APP;

	return access_type;
}

static u64 etm4_get_access_type(struct etmv4_config *config)
{
	u64 access_type = etm4_get_ns_access_type(config);
	u64 s_hyp = (config->arch & 0x0f) >= 0x4 ? ETM_EXLEVEL_S_HYP : 0;

	/*
	 * EXLEVEL_S, bits[11:8], don't trace anything happening
	 * in secure state.
	 */
	access_type |= (ETM_EXLEVEL_S_APP	|
			ETM_EXLEVEL_S_OS	|
			s_hyp			|
			ETM_EXLEVEL_S_MON);

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
	/* Trace everything 'default' filter achieved by no filtering */
	config->viiectlr = 0x0;

	/*
	 * TRCVICTLR::SSSTATUS == 1, the start-stop logic is
	 * in the started state
	 */
	config->vinst_ctrl |= BIT(9);
	config->mode |= ETM_MODE_VIEWINST_STARTSTOP;

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
	 * This is done by a minimum default config sufficient to enable
	 * full instruction trace - with a default filter for trace all
	 * achieved by having no filtering.
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
	u32 mode;

	mode = config->mode;
	mode &= (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER);

	/* excluding kernel AND user space doesn't make sense */
	WARN_ON_ONCE(mode == (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER));

	/* nothing to do if neither flags are set */
	if (!(mode & ETM_MODE_EXCL_KERN) && !(mode & ETM_MODE_EXCL_USER))
		return;

	etm4_set_victlr_access(config);
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
	if (!etmdrvdata[cpu]->os_unlock)
		etm4_os_unlock(etmdrvdata[cpu]);

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

static int etm4_cpu_save(struct etmv4_drvdata *drvdata)
{
	int i, ret = 0;
	struct etmv4_save_state *state;
	struct device *etm_dev = &drvdata->csdev->dev;

	/*
	 * As recommended by 3.4.1 ("The procedure when powering down the PE")
	 * of ARM IHI 0064D
	 */
	dsb(sy);
	isb();

	CS_UNLOCK(drvdata->base);

	/* Lock the OS lock to disable trace and external debugger access */
	etm4_os_lock(drvdata);

	/* wait for TRCSTATR.PMSTABLE to go up */
	if (coresight_timeout(drvdata->base, TRCSTATR,
			      TRCSTATR_PMSTABLE_BIT, 1)) {
		dev_err(etm_dev,
			"timeout while waiting for PM Stable Status\n");
		etm4_os_unlock(drvdata);
		ret = -EBUSY;
		goto out;
	}

	state = drvdata->save_state;

	state->trcprgctlr = readl(drvdata->base + TRCPRGCTLR);
	state->trcprocselr = readl(drvdata->base + TRCPROCSELR);
	state->trcconfigr = readl(drvdata->base + TRCCONFIGR);
	state->trcauxctlr = readl(drvdata->base + TRCAUXCTLR);
	state->trceventctl0r = readl(drvdata->base + TRCEVENTCTL0R);
	state->trceventctl1r = readl(drvdata->base + TRCEVENTCTL1R);
	state->trcstallctlr = readl(drvdata->base + TRCSTALLCTLR);
	state->trctsctlr = readl(drvdata->base + TRCTSCTLR);
	state->trcsyncpr = readl(drvdata->base + TRCSYNCPR);
	state->trcccctlr = readl(drvdata->base + TRCCCCTLR);
	state->trcbbctlr = readl(drvdata->base + TRCBBCTLR);
	state->trctraceidr = readl(drvdata->base + TRCTRACEIDR);
	state->trcqctlr = readl(drvdata->base + TRCQCTLR);

	state->trcvictlr = readl(drvdata->base + TRCVICTLR);
	state->trcviiectlr = readl(drvdata->base + TRCVIIECTLR);
	state->trcvissctlr = readl(drvdata->base + TRCVISSCTLR);
	state->trcvipcssctlr = readl(drvdata->base + TRCVIPCSSCTLR);
	state->trcvdctlr = readl(drvdata->base + TRCVDCTLR);
	state->trcvdsacctlr = readl(drvdata->base + TRCVDSACCTLR);
	state->trcvdarcctlr = readl(drvdata->base + TRCVDARCCTLR);

	for (i = 0; i < drvdata->nrseqstate - 1; i++)
		state->trcseqevr[i] = readl(drvdata->base + TRCSEQEVRn(i));

	state->trcseqrstevr = readl(drvdata->base + TRCSEQRSTEVR);
	state->trcseqstr = readl(drvdata->base + TRCSEQSTR);
	state->trcextinselr = readl(drvdata->base + TRCEXTINSELR);

	for (i = 0; i < drvdata->nr_cntr; i++) {
		state->trccntrldvr[i] = readl(drvdata->base + TRCCNTRLDVRn(i));
		state->trccntctlr[i] = readl(drvdata->base + TRCCNTCTLRn(i));
		state->trccntvr[i] = readl(drvdata->base + TRCCNTVRn(i));
	}

	for (i = 0; i < drvdata->nr_resource * 2; i++)
		state->trcrsctlr[i] = readl(drvdata->base + TRCRSCTLRn(i));

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		state->trcssccr[i] = readl(drvdata->base + TRCSSCCRn(i));
		state->trcsscsr[i] = readl(drvdata->base + TRCSSCSRn(i));
		state->trcsspcicr[i] = readl(drvdata->base + TRCSSPCICRn(i));
	}

	for (i = 0; i < drvdata->nr_addr_cmp * 2; i++) {
		state->trcacvr[i] = readq(drvdata->base + TRCACVRn(i));
		state->trcacatr[i] = readq(drvdata->base + TRCACATRn(i));
	}

	/*
	 * Data trace stream is architecturally prohibited for A profile cores
	 * so we don't save (or later restore) trcdvcvr and trcdvcmr - As per
	 * section 1.3.4 ("Possible functional configurations of an ETMv4 trace
	 * unit") of ARM IHI 0064D.
	 */

	for (i = 0; i < drvdata->numcidc; i++)
		state->trccidcvr[i] = readq(drvdata->base + TRCCIDCVRn(i));

	for (i = 0; i < drvdata->numvmidc; i++)
		state->trcvmidcvr[i] = readq(drvdata->base + TRCVMIDCVRn(i));

	state->trccidcctlr0 = readl(drvdata->base + TRCCIDCCTLR0);
	state->trccidcctlr1 = readl(drvdata->base + TRCCIDCCTLR1);

	state->trcvmidcctlr0 = readl(drvdata->base + TRCVMIDCCTLR0);
	state->trcvmidcctlr0 = readl(drvdata->base + TRCVMIDCCTLR1);

	state->trcclaimset = readl(drvdata->base + TRCCLAIMCLR);

	state->trcpdcr = readl(drvdata->base + TRCPDCR);

	/* wait for TRCSTATR.IDLE to go up */
	if (coresight_timeout(drvdata->base, TRCSTATR, TRCSTATR_IDLE_BIT, 1)) {
		dev_err(etm_dev,
			"timeout while waiting for Idle Trace Status\n");
		etm4_os_unlock(drvdata);
		ret = -EBUSY;
		goto out;
	}

	drvdata->state_needs_restore = true;

	/*
	 * Power can be removed from the trace unit now. We do this to
	 * potentially save power on systems that respect the TRCPDCR_PU
	 * despite requesting software to save/restore state.
	 */
	writel_relaxed((state->trcpdcr & ~TRCPDCR_PU),
			drvdata->base + TRCPDCR);

out:
	CS_LOCK(drvdata->base);
	return ret;
}

static void etm4_cpu_restore(struct etmv4_drvdata *drvdata)
{
	int i;
	struct etmv4_save_state *state = drvdata->save_state;

	CS_UNLOCK(drvdata->base);

	writel_relaxed(state->trcclaimset, drvdata->base + TRCCLAIMSET);

	writel_relaxed(state->trcprgctlr, drvdata->base + TRCPRGCTLR);
	writel_relaxed(state->trcprocselr, drvdata->base + TRCPROCSELR);
	writel_relaxed(state->trcconfigr, drvdata->base + TRCCONFIGR);
	writel_relaxed(state->trcauxctlr, drvdata->base + TRCAUXCTLR);
	writel_relaxed(state->trceventctl0r, drvdata->base + TRCEVENTCTL0R);
	writel_relaxed(state->trceventctl1r, drvdata->base + TRCEVENTCTL1R);
	writel_relaxed(state->trcstallctlr, drvdata->base + TRCSTALLCTLR);
	writel_relaxed(state->trctsctlr, drvdata->base + TRCTSCTLR);
	writel_relaxed(state->trcsyncpr, drvdata->base + TRCSYNCPR);
	writel_relaxed(state->trcccctlr, drvdata->base + TRCCCCTLR);
	writel_relaxed(state->trcbbctlr, drvdata->base + TRCBBCTLR);
	writel_relaxed(state->trctraceidr, drvdata->base + TRCTRACEIDR);
	writel_relaxed(state->trcqctlr, drvdata->base + TRCQCTLR);

	writel_relaxed(state->trcvictlr, drvdata->base + TRCVICTLR);
	writel_relaxed(state->trcviiectlr, drvdata->base + TRCVIIECTLR);
	writel_relaxed(state->trcvissctlr, drvdata->base + TRCVISSCTLR);
	writel_relaxed(state->trcvipcssctlr, drvdata->base + TRCVIPCSSCTLR);
	writel_relaxed(state->trcvdctlr, drvdata->base + TRCVDCTLR);
	writel_relaxed(state->trcvdsacctlr, drvdata->base + TRCVDSACCTLR);
	writel_relaxed(state->trcvdarcctlr, drvdata->base + TRCVDARCCTLR);

	for (i = 0; i < drvdata->nrseqstate - 1; i++)
		writel_relaxed(state->trcseqevr[i],
			       drvdata->base + TRCSEQEVRn(i));

	writel_relaxed(state->trcseqrstevr, drvdata->base + TRCSEQRSTEVR);
	writel_relaxed(state->trcseqstr, drvdata->base + TRCSEQSTR);
	writel_relaxed(state->trcextinselr, drvdata->base + TRCEXTINSELR);

	for (i = 0; i < drvdata->nr_cntr; i++) {
		writel_relaxed(state->trccntrldvr[i],
			       drvdata->base + TRCCNTRLDVRn(i));
		writel_relaxed(state->trccntctlr[i],
			       drvdata->base + TRCCNTCTLRn(i));
		writel_relaxed(state->trccntvr[i],
			       drvdata->base + TRCCNTVRn(i));
	}

	for (i = 0; i < drvdata->nr_resource * 2; i++)
		writel_relaxed(state->trcrsctlr[i],
			       drvdata->base + TRCRSCTLRn(i));

	for (i = 0; i < drvdata->nr_ss_cmp; i++) {
		writel_relaxed(state->trcssccr[i],
			       drvdata->base + TRCSSCCRn(i));
		writel_relaxed(state->trcsscsr[i],
			       drvdata->base + TRCSSCSRn(i));
		writel_relaxed(state->trcsspcicr[i],
			       drvdata->base + TRCSSPCICRn(i));
	}

	for (i = 0; i < drvdata->nr_addr_cmp * 2; i++) {
		writeq_relaxed(state->trcacvr[i],
			       drvdata->base + TRCACVRn(i));
		writeq_relaxed(state->trcacatr[i],
			       drvdata->base + TRCACATRn(i));
	}

	for (i = 0; i < drvdata->numcidc; i++)
		writeq_relaxed(state->trccidcvr[i],
			       drvdata->base + TRCCIDCVRn(i));

	for (i = 0; i < drvdata->numvmidc; i++)
		writeq_relaxed(state->trcvmidcvr[i],
			       drvdata->base + TRCVMIDCVRn(i));

	writel_relaxed(state->trccidcctlr0, drvdata->base + TRCCIDCCTLR0);
	writel_relaxed(state->trccidcctlr1, drvdata->base + TRCCIDCCTLR1);

	writel_relaxed(state->trcvmidcctlr0, drvdata->base + TRCVMIDCCTLR0);
	writel_relaxed(state->trcvmidcctlr0, drvdata->base + TRCVMIDCCTLR1);

	writel_relaxed(state->trcclaimset, drvdata->base + TRCCLAIMSET);

	writel_relaxed(state->trcpdcr, drvdata->base + TRCPDCR);

	drvdata->state_needs_restore = false;

	/*
	 * As recommended by section 4.3.7 ("Synchronization when using the
	 * memory-mapped interface") of ARM IHI 0064D
	 */
	dsb(sy);
	isb();

	/* Unlock the OS lock to re-enable trace and external debug access */
	etm4_os_unlock(drvdata);
	CS_LOCK(drvdata->base);
}

static int etm4_cpu_pm_notify(struct notifier_block *nb, unsigned long cmd,
			      void *v)
{
	struct etmv4_drvdata *drvdata;
	unsigned int cpu = smp_processor_id();

	if (!etmdrvdata[cpu])
		return NOTIFY_OK;

	drvdata = etmdrvdata[cpu];

	if (!drvdata->save_state)
		return NOTIFY_OK;

	if (WARN_ON_ONCE(drvdata->cpu != cpu))
		return NOTIFY_BAD;

	switch (cmd) {
	case CPU_PM_ENTER:
		/* save the state if self-hosted coresight is in use */
		if (local_read(&drvdata->mode))
			if (etm4_cpu_save(drvdata))
				return NOTIFY_BAD;
		break;
	case CPU_PM_EXIT:
	case CPU_PM_ENTER_FAILED:
		if (drvdata->state_needs_restore)
			etm4_cpu_restore(drvdata);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block etm4_cpu_pm_nb = {
	.notifier_call = etm4_cpu_pm_notify,
};

/* Setup PM. Deals with error conditions and counts */
static int __init etm4_pm_setup(void)
{
	int ret;

	ret = cpu_pm_register_notifier(&etm4_cpu_pm_nb);
	if (ret)
		return ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING,
					"arm/coresight4:starting",
					etm4_starting_cpu, etm4_dying_cpu);

	if (ret)
		goto unregister_notifier;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"arm/coresight4:online",
					etm4_online_cpu, NULL);

	/* HP dyn state ID returned in ret on success */
	if (ret > 0) {
		hp_online = ret;
		return 0;
	}

	/* failed dyn state - remove others */
	cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING);

unregister_notifier:
	cpu_pm_unregister_notifier(&etm4_cpu_pm_nb);
	return ret;
}

static void etm4_pm_clear(void)
{
	cpu_pm_unregister_notifier(&etm4_cpu_pm_nb);
	cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING);
	if (hp_online) {
		cpuhp_remove_state_nocalls(hp_online);
		hp_online = 0;
	}
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

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	dev_set_drvdata(dev, drvdata);

	if (pm_save_enable == PARAM_PM_SAVE_FIRMWARE)
		pm_save_enable = coresight_loses_context_with_cpu(dev) ?
			       PARAM_PM_SAVE_SELF_HOSTED : PARAM_PM_SAVE_NEVER;

	if (pm_save_enable != PARAM_PM_SAVE_NEVER) {
		drvdata->save_state = devm_kmalloc(dev,
				sizeof(struct etmv4_save_state), GFP_KERNEL);
		if (!drvdata->save_state)
			return -ENOMEM;
	}

	if (fwnode_property_present(dev_fwnode(dev), "qcom,skip-power-up"))
		drvdata->skip_power_up = true;

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	spin_lock_init(&drvdata->spinlock);

	drvdata->cpu = coresight_get_cpu(dev);
	if (drvdata->cpu < 0)
		return drvdata->cpu;

	desc.name = devm_kasprintf(dev, GFP_KERNEL, "etm%d", drvdata->cpu);
	if (!desc.name)
		return -ENOMEM;

	if (smp_call_function_single(drvdata->cpu,
				etm4_init_arch_data,  drvdata, 1))
		dev_err(dev, "ETM arch init failed\n");

	if (etm4_arch_supported(drvdata->arch) == false)
		return -EINVAL;

	etm4_init_trace_id(drvdata);
	etm4_set_default(&drvdata->config);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	adev->dev.platform_data = pdata;

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &etm4_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = coresight_etmv4_groups;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	ret = etm_perf_symlink(drvdata->csdev, true);
	if (ret) {
		coresight_unregister(drvdata->csdev);
		return ret;
	}

	etmdrvdata[drvdata->cpu] = drvdata;

	pm_runtime_put(&adev->dev);
	dev_info(&drvdata->csdev->dev, "CPU%d: ETM v%d.%d initialized\n",
		 drvdata->cpu, drvdata->arch >> 4, drvdata->arch & 0xf);

	if (boot_enable) {
		coresight_enable(drvdata->csdev);
		drvdata->boot_enable = true;
	}

	return 0;
}

static struct amba_cs_uci_id uci_id_etm4[] = {
	{
		/*  ETMv4 UCI data */
		.devarch	= 0x47704a13,
		.devarch_mask	= 0xfff0ffff,
		.devtype	= 0x00000013,
	}
};

static void __exit clear_etmdrvdata(void *info)
{
	int cpu = *(int *)info;

	etmdrvdata[cpu] = NULL;
}

static int __exit etm4_remove(struct amba_device *adev)
{
	struct etmv4_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	etm_perf_symlink(drvdata->csdev, false);

	/*
	 * Taking hotplug lock here to avoid racing between etm4_remove and
	 * CPU hotplug call backs.
	 */
	cpus_read_lock();
	/*
	 * The readers for etmdrvdata[] are CPU hotplug call backs
	 * and PM notification call backs. Change etmdrvdata[i] on
	 * CPU i ensures these call backs has consistent view
	 * inside one call back function.
	 */
	if (smp_call_function_single(drvdata->cpu, clear_etmdrvdata, &drvdata->cpu, 1))
		etmdrvdata[drvdata->cpu] = NULL;

	cpus_read_unlock();

	coresight_unregister(drvdata->csdev);

	return 0;
}

static const struct amba_id etm4_ids[] = {
	CS_AMBA_ID(0x000bb95d),			/* Cortex-A53 */
	CS_AMBA_ID(0x000bb95e),			/* Cortex-A57 */
	CS_AMBA_ID(0x000bb95a),			/* Cortex-A72 */
	CS_AMBA_ID(0x000bb959),			/* Cortex-A73 */
	CS_AMBA_UCI_ID(0x000bb9da, uci_id_etm4),/* Cortex-A35 */
	CS_AMBA_UCI_ID(0x000bbd0c, uci_id_etm4),/* Neoverse N1 */
	CS_AMBA_UCI_ID(0x000f0205, uci_id_etm4),/* Qualcomm Kryo */
	CS_AMBA_UCI_ID(0x000f0211, uci_id_etm4),/* Qualcomm Kryo */
	CS_AMBA_UCI_ID(0x000bb802, uci_id_etm4),/* Qualcomm Kryo 385 Cortex-A55 */
	CS_AMBA_UCI_ID(0x000bb803, uci_id_etm4),/* Qualcomm Kryo 385 Cortex-A75 */
	CS_AMBA_UCI_ID(0x000bb805, uci_id_etm4),/* Qualcomm Kryo 4XX Cortex-A55 */
	CS_AMBA_UCI_ID(0x000bb804, uci_id_etm4),/* Qualcomm Kryo 4XX Cortex-A76 */
	CS_AMBA_UCI_ID(0x000cc0af, uci_id_etm4),/* Marvell ThunderX2 */
	CS_AMBA_UCI_ID(0x000b6d01, uci_id_etm4),/* HiSilicon-Hip08 */
	CS_AMBA_UCI_ID(0x000b6d02, uci_id_etm4),/* HiSilicon-Hip09 */
	{},
};

MODULE_DEVICE_TABLE(amba, etm4_ids);

static struct amba_driver etm4x_driver = {
	.drv = {
		.name   = "coresight-etm4x",
		.owner  = THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe		= etm4_probe,
	.remove         = etm4_remove,
	.id_table	= etm4_ids,
};

static int __init etm4x_init(void)
{
	int ret;

	ret = etm4_pm_setup();

	/* etm4_pm_setup() does its own cleanup - exit on error */
	if (ret)
		return ret;

	ret = amba_driver_register(&etm4x_driver);
	if (ret) {
		pr_err("Error registering etm4x driver\n");
		etm4_pm_clear();
	}

	return ret;
}

static void __exit etm4x_exit(void)
{
	amba_driver_unregister(&etm4x_driver);
	etm4_pm_clear();
}

module_init(etm4x_init);
module_exit(etm4x_exit);

MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight Program Flow Trace v4.x driver");
MODULE_LICENSE("GPL v2");
