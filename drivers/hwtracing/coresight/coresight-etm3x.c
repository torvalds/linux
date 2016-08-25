/* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Program Flow Trace driver
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
#include <linux/pm_runtime.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/coresight.h>
#include <linux/coresight-pmu.h>
#include <linux/amba/bus.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/perf_event.h>
#include <asm/sections.h>

#include "coresight-etm.h"
#include "coresight-etm-perf.h"

/*
 * Not really modular but using module_param is the easiest way to
 * remain consistent with existing use cases for now.
 */
static int boot_enable;
module_param_named(boot_enable, boot_enable, int, S_IRUGO);

/* The number of ETM/PTM currently registered */
static int etm_count;
static struct etm_drvdata *etmdrvdata[NR_CPUS];

static enum cpuhp_state hp_online;

/*
 * Memory mapped writes to clear os lock are not supported on some processors
 * and OS lock must be unlocked before any memory mapped access on such
 * processors, otherwise memory mapped reads/writes will be invalid.
 */
static void etm_os_unlock(struct etm_drvdata *drvdata)
{
	/* Writing any value to ETMOSLAR unlocks the trace registers */
	etm_writel(drvdata, 0x0, ETMOSLAR);
	drvdata->os_unlock = true;
	isb();
}

static void etm_set_pwrdwn(struct etm_drvdata *drvdata)
{
	u32 etmcr;

	/* Ensure pending cp14 accesses complete before setting pwrdwn */
	mb();
	isb();
	etmcr = etm_readl(drvdata, ETMCR);
	etmcr |= ETMCR_PWD_DWN;
	etm_writel(drvdata, etmcr, ETMCR);
}

static void etm_clr_pwrdwn(struct etm_drvdata *drvdata)
{
	u32 etmcr;

	etmcr = etm_readl(drvdata, ETMCR);
	etmcr &= ~ETMCR_PWD_DWN;
	etm_writel(drvdata, etmcr, ETMCR);
	/* Ensure pwrup completes before subsequent cp14 accesses */
	mb();
	isb();
}

static void etm_set_pwrup(struct etm_drvdata *drvdata)
{
	u32 etmpdcr;

	etmpdcr = readl_relaxed(drvdata->base + ETMPDCR);
	etmpdcr |= ETMPDCR_PWD_UP;
	writel_relaxed(etmpdcr, drvdata->base + ETMPDCR);
	/* Ensure pwrup completes before subsequent cp14 accesses */
	mb();
	isb();
}

static void etm_clr_pwrup(struct etm_drvdata *drvdata)
{
	u32 etmpdcr;

	/* Ensure pending cp14 accesses complete before clearing pwrup */
	mb();
	isb();
	etmpdcr = readl_relaxed(drvdata->base + ETMPDCR);
	etmpdcr &= ~ETMPDCR_PWD_UP;
	writel_relaxed(etmpdcr, drvdata->base + ETMPDCR);
}

/**
 * coresight_timeout_etm - loop until a bit has changed to a specific state.
 * @drvdata: etm's private data structure.
 * @offset: address of a register, starting from @addr.
 * @position: the position of the bit of interest.
 * @value: the value the bit should have.
 *
 * Basically the same as @coresight_timeout except for the register access
 * method where we have to account for CP14 configurations.

 * Return: 0 as soon as the bit has taken the desired state or -EAGAIN if
 * TIMEOUT_US has elapsed, which ever happens first.
 */

static int coresight_timeout_etm(struct etm_drvdata *drvdata, u32 offset,
				  int position, int value)
{
	int i;
	u32 val;

	for (i = TIMEOUT_US; i > 0; i--) {
		val = etm_readl(drvdata, offset);
		/* Waiting on the bit to go from 0 to 1 */
		if (value) {
			if (val & BIT(position))
				return 0;
		/* Waiting on the bit to go from 1 to 0 */
		} else {
			if (!(val & BIT(position)))
				return 0;
		}

		/*
		 * Delay is arbitrary - the specification doesn't say how long
		 * we are expected to wait.  Extra check required to make sure
		 * we don't wait needlessly on the last iteration.
		 */
		if (i - 1)
			udelay(1);
	}

	return -EAGAIN;
}


static void etm_set_prog(struct etm_drvdata *drvdata)
{
	u32 etmcr;

	etmcr = etm_readl(drvdata, ETMCR);
	etmcr |= ETMCR_ETM_PRG;
	etm_writel(drvdata, etmcr, ETMCR);
	/*
	 * Recommended by spec for cp14 accesses to ensure etmcr write is
	 * complete before polling etmsr
	 */
	isb();
	if (coresight_timeout_etm(drvdata, ETMSR, ETMSR_PROG_BIT, 1)) {
		dev_err(drvdata->dev,
			"%s: timeout observed when probing at offset %#x\n",
			__func__, ETMSR);
	}
}

static void etm_clr_prog(struct etm_drvdata *drvdata)
{
	u32 etmcr;

	etmcr = etm_readl(drvdata, ETMCR);
	etmcr &= ~ETMCR_ETM_PRG;
	etm_writel(drvdata, etmcr, ETMCR);
	/*
	 * Recommended by spec for cp14 accesses to ensure etmcr write is
	 * complete before polling etmsr
	 */
	isb();
	if (coresight_timeout_etm(drvdata, ETMSR, ETMSR_PROG_BIT, 0)) {
		dev_err(drvdata->dev,
			"%s: timeout observed when probing at offset %#x\n",
			__func__, ETMSR);
	}
}

void etm_set_default(struct etm_config *config)
{
	int i;

	if (WARN_ON_ONCE(!config))
		return;

	/*
	 * Taken verbatim from the TRM:
	 *
	 * To trace all memory:
	 *  set bit [24] in register 0x009, the ETMTECR1, to 1
	 *  set all other bits in register 0x009, the ETMTECR1, to 0
	 *  set all bits in register 0x007, the ETMTECR2, to 0
	 *  set register 0x008, the ETMTEEVR, to 0x6F (TRUE).
	 */
	config->enable_ctrl1 = BIT(24);
	config->enable_ctrl2 = 0x0;
	config->enable_event = ETM_HARD_WIRE_RES_A;

	config->trigger_event = ETM_DEFAULT_EVENT_VAL;
	config->enable_event = ETM_HARD_WIRE_RES_A;

	config->seq_12_event = ETM_DEFAULT_EVENT_VAL;
	config->seq_21_event = ETM_DEFAULT_EVENT_VAL;
	config->seq_23_event = ETM_DEFAULT_EVENT_VAL;
	config->seq_31_event = ETM_DEFAULT_EVENT_VAL;
	config->seq_32_event = ETM_DEFAULT_EVENT_VAL;
	config->seq_13_event = ETM_DEFAULT_EVENT_VAL;
	config->timestamp_event = ETM_DEFAULT_EVENT_VAL;

	for (i = 0; i < ETM_MAX_CNTR; i++) {
		config->cntr_rld_val[i] = 0x0;
		config->cntr_event[i] = ETM_DEFAULT_EVENT_VAL;
		config->cntr_rld_event[i] = ETM_DEFAULT_EVENT_VAL;
		config->cntr_val[i] = 0x0;
	}

	config->seq_curr_state = 0x0;
	config->ctxid_idx = 0x0;
	for (i = 0; i < ETM_MAX_CTXID_CMP; i++) {
		config->ctxid_pid[i] = 0x0;
		config->ctxid_vpid[i] = 0x0;
	}

	config->ctxid_mask = 0x0;
}

void etm_config_trace_mode(struct etm_config *config)
{
	u32 flags, mode;

	mode = config->mode;

	mode &= (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER);

	/* excluding kernel AND user space doesn't make sense */
	if (mode == (ETM_MODE_EXCL_KERN | ETM_MODE_EXCL_USER))
		return;

	/* nothing to do if neither flags are set */
	if (!(mode & ETM_MODE_EXCL_KERN) && !(mode & ETM_MODE_EXCL_USER))
		return;

	flags = (1 << 0 |	/* instruction execute */
		 3 << 3 |	/* ARM instruction */
		 0 << 5 |	/* No data value comparison */
		 0 << 7 |	/* No exact mach */
		 0 << 8);	/* Ignore context ID */

	/* No need to worry about single address comparators. */
	config->enable_ctrl2 = 0x0;

	/* Bit 0 is address range comparator 1 */
	config->enable_ctrl1 = ETMTECR1_ADDR_COMP_1;

	/*
	 * On ETMv3.5:
	 * ETMACTRn[13,11] == Non-secure state comparison control
	 * ETMACTRn[12,10] == Secure state comparison control
	 *
	 * b00 == Match in all modes in this state
	 * b01 == Do not match in any more in this state
	 * b10 == Match in all modes excepts user mode in this state
	 * b11 == Match only in user mode in this state
	 */

	/* Tracing in secure mode is not supported at this time */
	flags |= (0 << 12 | 1 << 10);

	if (mode & ETM_MODE_EXCL_USER) {
		/* exclude user, match all modes except user mode */
		flags |= (1 << 13 | 0 << 11);
	} else {
		/* exclude kernel, match only in user mode */
		flags |= (1 << 13 | 1 << 11);
	}

	/*
	 * The ETMEEVR register is already set to "hard wire A".  As such
	 * all there is to do is setup an address comparator that spans
	 * the entire address range and configure the state and mode bits.
	 */
	config->addr_val[0] = (u32) 0x0;
	config->addr_val[1] = (u32) ~0x0;
	config->addr_acctype[0] = flags;
	config->addr_acctype[1] = flags;
	config->addr_type[0] = ETM_ADDR_TYPE_RANGE;
	config->addr_type[1] = ETM_ADDR_TYPE_RANGE;
}

#define ETM3X_SUPPORTED_OPTIONS (ETMCR_CYC_ACC | ETMCR_TIMESTAMP_EN)

static int etm_parse_event_config(struct etm_drvdata *drvdata,
				  struct perf_event_attr *attr)
{
	struct etm_config *config = &drvdata->config;

	if (!attr)
		return -EINVAL;

	/* Clear configuration from previous run */
	memset(config, 0, sizeof(struct etm_config));

	if (attr->exclude_kernel)
		config->mode = ETM_MODE_EXCL_KERN;

	if (attr->exclude_user)
		config->mode = ETM_MODE_EXCL_USER;

	/* Always start from the default config */
	etm_set_default(config);

	/*
	 * By default the tracers are configured to trace the whole address
	 * range.  Narrow the field only if requested by user space.
	 */
	if (config->mode)
		etm_config_trace_mode(config);

	/*
	 * At this time only cycle accurate and timestamp options are
	 * available.
	 */
	if (attr->config & ~ETM3X_SUPPORTED_OPTIONS)
		return -EINVAL;

	config->ctrl = attr->config;

	return 0;
}

static void etm_enable_hw(void *info)
{
	int i;
	u32 etmcr;
	struct etm_drvdata *drvdata = info;
	struct etm_config *config = &drvdata->config;

	CS_UNLOCK(drvdata->base);

	/* Turn engine on */
	etm_clr_pwrdwn(drvdata);
	/* Apply power to trace registers */
	etm_set_pwrup(drvdata);
	/* Make sure all registers are accessible */
	etm_os_unlock(drvdata);

	etm_set_prog(drvdata);

	etmcr = etm_readl(drvdata, ETMCR);
	/* Clear setting from a previous run if need be */
	etmcr &= ~ETM3X_SUPPORTED_OPTIONS;
	etmcr |= drvdata->port_size;
	etmcr |= ETMCR_ETM_EN;
	etm_writel(drvdata, config->ctrl | etmcr, ETMCR);
	etm_writel(drvdata, config->trigger_event, ETMTRIGGER);
	etm_writel(drvdata, config->startstop_ctrl, ETMTSSCR);
	etm_writel(drvdata, config->enable_event, ETMTEEVR);
	etm_writel(drvdata, config->enable_ctrl1, ETMTECR1);
	etm_writel(drvdata, config->fifofull_level, ETMFFLR);
	for (i = 0; i < drvdata->nr_addr_cmp; i++) {
		etm_writel(drvdata, config->addr_val[i], ETMACVRn(i));
		etm_writel(drvdata, config->addr_acctype[i], ETMACTRn(i));
	}
	for (i = 0; i < drvdata->nr_cntr; i++) {
		etm_writel(drvdata, config->cntr_rld_val[i], ETMCNTRLDVRn(i));
		etm_writel(drvdata, config->cntr_event[i], ETMCNTENRn(i));
		etm_writel(drvdata, config->cntr_rld_event[i],
			   ETMCNTRLDEVRn(i));
		etm_writel(drvdata, config->cntr_val[i], ETMCNTVRn(i));
	}
	etm_writel(drvdata, config->seq_12_event, ETMSQ12EVR);
	etm_writel(drvdata, config->seq_21_event, ETMSQ21EVR);
	etm_writel(drvdata, config->seq_23_event, ETMSQ23EVR);
	etm_writel(drvdata, config->seq_31_event, ETMSQ31EVR);
	etm_writel(drvdata, config->seq_32_event, ETMSQ32EVR);
	etm_writel(drvdata, config->seq_13_event, ETMSQ13EVR);
	etm_writel(drvdata, config->seq_curr_state, ETMSQR);
	for (i = 0; i < drvdata->nr_ext_out; i++)
		etm_writel(drvdata, ETM_DEFAULT_EVENT_VAL, ETMEXTOUTEVRn(i));
	for (i = 0; i < drvdata->nr_ctxid_cmp; i++)
		etm_writel(drvdata, config->ctxid_pid[i], ETMCIDCVRn(i));
	etm_writel(drvdata, config->ctxid_mask, ETMCIDCMR);
	etm_writel(drvdata, config->sync_freq, ETMSYNCFR);
	/* No external input selected */
	etm_writel(drvdata, 0x0, ETMEXTINSELR);
	etm_writel(drvdata, config->timestamp_event, ETMTSEVR);
	/* No auxiliary control selected */
	etm_writel(drvdata, 0x0, ETMAUXCR);
	etm_writel(drvdata, drvdata->traceid, ETMTRACEIDR);
	/* No VMID comparator value selected */
	etm_writel(drvdata, 0x0, ETMVMIDCVR);

	etm_clr_prog(drvdata);
	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d enable smp call done\n", drvdata->cpu);
}

static int etm_cpu_id(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->cpu;
}

int etm_get_trace_id(struct etm_drvdata *drvdata)
{
	unsigned long flags;
	int trace_id = -1;

	if (!drvdata)
		goto out;

	if (!local_read(&drvdata->mode))
		return drvdata->traceid;

	pm_runtime_get_sync(drvdata->dev);

	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	trace_id = (etm_readl(drvdata, ETMTRACEIDR) & ETM_TRACEID_MASK);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	pm_runtime_put(drvdata->dev);

out:
	return trace_id;

}

static int etm_trace_id(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return etm_get_trace_id(drvdata);
}

static int etm_enable_perf(struct coresight_device *csdev,
			   struct perf_event_attr *attr)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(drvdata->cpu != smp_processor_id()))
		return -EINVAL;

	/* Configure the tracer based on the session's specifics */
	etm_parse_event_config(drvdata, attr);
	/* And enable it */
	etm_enable_hw(drvdata);

	return 0;
}

static int etm_enable_sysfs(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;

	spin_lock(&drvdata->spinlock);

	/*
	 * Configure the ETM only if the CPU is online.  If it isn't online
	 * hw configuration will take place on the local CPU during bring up.
	 */
	if (cpu_online(drvdata->cpu)) {
		ret = smp_call_function_single(drvdata->cpu,
					       etm_enable_hw, drvdata, 1);
		if (ret)
			goto err;
	}

	drvdata->sticky_enable = true;
	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "ETM tracing enabled\n");
	return 0;

err:
	spin_unlock(&drvdata->spinlock);
	return ret;
}

static int etm_enable(struct coresight_device *csdev,
		      struct perf_event_attr *attr, u32 mode)
{
	int ret;
	u32 val;
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	val = local_cmpxchg(&drvdata->mode, CS_MODE_DISABLED, mode);

	/* Someone is already using the tracer */
	if (val)
		return -EBUSY;

	switch (mode) {
	case CS_MODE_SYSFS:
		ret = etm_enable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		ret = etm_enable_perf(csdev, attr);
		break;
	default:
		ret = -EINVAL;
	}

	/* The tracer didn't start */
	if (ret)
		local_set(&drvdata->mode, CS_MODE_DISABLED);

	return ret;
}

static void etm_disable_hw(void *info)
{
	int i;
	struct etm_drvdata *drvdata = info;
	struct etm_config *config = &drvdata->config;

	CS_UNLOCK(drvdata->base);
	etm_set_prog(drvdata);

	/* Read back sequencer and counters for post trace analysis */
	config->seq_curr_state = (etm_readl(drvdata, ETMSQR) & ETM_SQR_MASK);

	for (i = 0; i < drvdata->nr_cntr; i++)
		config->cntr_val[i] = etm_readl(drvdata, ETMCNTVRn(i));

	etm_set_pwrdwn(drvdata);
	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d disable smp call done\n", drvdata->cpu);
}

static void etm_disable_perf(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(drvdata->cpu != smp_processor_id()))
		return;

	CS_UNLOCK(drvdata->base);

	/* Setting the prog bit disables tracing immediately */
	etm_set_prog(drvdata);

	/*
	 * There is no way to know when the tracer will be used again so
	 * power down the tracer.
	 */
	etm_set_pwrdwn(drvdata);

	CS_LOCK(drvdata->base);
}

static void etm_disable_sysfs(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * Taking hotplug lock here protects from clocks getting disabled
	 * with tracing being left on (crash scenario) if user disable occurs
	 * after cpu online mask indicates the cpu is offline but before the
	 * DYING hotplug callback is serviced by the ETM driver.
	 */
	get_online_cpus();
	spin_lock(&drvdata->spinlock);

	/*
	 * Executing etm_disable_hw on the cpu whose ETM is being disabled
	 * ensures that register writes occur when cpu is powered.
	 */
	smp_call_function_single(drvdata->cpu, etm_disable_hw, drvdata, 1);

	spin_unlock(&drvdata->spinlock);
	put_online_cpus();

	dev_info(drvdata->dev, "ETM tracing disabled\n");
}

static void etm_disable(struct coresight_device *csdev)
{
	u32 mode;
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

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
		etm_disable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		etm_disable_perf(csdev);
		break;
	default:
		WARN_ON_ONCE(mode);
		return;
	}

	if (mode)
		local_set(&drvdata->mode, CS_MODE_DISABLED);
}

static const struct coresight_ops_source etm_source_ops = {
	.cpu_id		= etm_cpu_id,
	.trace_id	= etm_trace_id,
	.enable		= etm_enable,
	.disable	= etm_disable,
};

static const struct coresight_ops etm_cs_ops = {
	.source_ops	= &etm_source_ops,
};

static int etm_online_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	if (etmdrvdata[cpu]->boot_enable && !etmdrvdata[cpu]->sticky_enable)
		coresight_enable(etmdrvdata[cpu]->csdev);
	return 0;
}

static int etm_starting_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	spin_lock(&etmdrvdata[cpu]->spinlock);
	if (!etmdrvdata[cpu]->os_unlock) {
		etm_os_unlock(etmdrvdata[cpu]);
		etmdrvdata[cpu]->os_unlock = true;
	}

	if (local_read(&etmdrvdata[cpu]->mode))
		etm_enable_hw(etmdrvdata[cpu]);
	spin_unlock(&etmdrvdata[cpu]->spinlock);
	return 0;
}

static int etm_dying_cpu(unsigned int cpu)
{
	if (!etmdrvdata[cpu])
		return 0;

	spin_lock(&etmdrvdata[cpu]->spinlock);
	if (local_read(&etmdrvdata[cpu]->mode))
		etm_disable_hw(etmdrvdata[cpu]);
	spin_unlock(&etmdrvdata[cpu]->spinlock);
	return 0;
}

static bool etm_arch_supported(u8 arch)
{
	switch (arch) {
	case ETM_ARCH_V3_3:
		break;
	case ETM_ARCH_V3_5:
		break;
	case PFT_ARCH_V1_0:
		break;
	case PFT_ARCH_V1_1:
		break;
	default:
		return false;
	}
	return true;
}

static void etm_init_arch_data(void *info)
{
	u32 etmidr;
	u32 etmccr;
	struct etm_drvdata *drvdata = info;

	/* Make sure all registers are accessible */
	etm_os_unlock(drvdata);

	CS_UNLOCK(drvdata->base);

	/* First dummy read */
	(void)etm_readl(drvdata, ETMPDSR);
	/* Provide power to ETM: ETMPDCR[3] == 1 */
	etm_set_pwrup(drvdata);
	/*
	 * Clear power down bit since when this bit is set writes to
	 * certain registers might be ignored.
	 */
	etm_clr_pwrdwn(drvdata);
	/*
	 * Set prog bit. It will be set from reset but this is included to
	 * ensure it is set
	 */
	etm_set_prog(drvdata);

	/* Find all capabilities */
	etmidr = etm_readl(drvdata, ETMIDR);
	drvdata->arch = BMVAL(etmidr, 4, 11);
	drvdata->port_size = etm_readl(drvdata, ETMCR) & PORT_SIZE_MASK;

	drvdata->etmccer = etm_readl(drvdata, ETMCCER);
	etmccr = etm_readl(drvdata, ETMCCR);
	drvdata->etmccr = etmccr;
	drvdata->nr_addr_cmp = BMVAL(etmccr, 0, 3) * 2;
	drvdata->nr_cntr = BMVAL(etmccr, 13, 15);
	drvdata->nr_ext_inp = BMVAL(etmccr, 17, 19);
	drvdata->nr_ext_out = BMVAL(etmccr, 20, 22);
	drvdata->nr_ctxid_cmp = BMVAL(etmccr, 24, 25);

	etm_set_pwrdwn(drvdata);
	etm_clr_pwrup(drvdata);
	CS_LOCK(drvdata->base);
}

static void etm_init_trace_id(struct etm_drvdata *drvdata)
{
	drvdata->traceid = coresight_get_trace_id(drvdata->cpu);
}

static int etm_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct etm_drvdata *drvdata;
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
		drvdata->use_cp14 = of_property_read_bool(np, "arm,cp14");
	}

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	spin_lock_init(&drvdata->spinlock);

	drvdata->atclk = devm_clk_get(&adev->dev, "atclk"); /* optional */
	if (!IS_ERR(drvdata->atclk)) {
		ret = clk_prepare_enable(drvdata->atclk);
		if (ret)
			return ret;
	}

	drvdata->cpu = pdata ? pdata->cpu : 0;

	get_online_cpus();
	etmdrvdata[drvdata->cpu] = drvdata;

	if (smp_call_function_single(drvdata->cpu,
				     etm_init_arch_data,  drvdata, 1))
		dev_err(dev, "ETM arch init failed\n");

	if (!etm_count++) {
		cpuhp_setup_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING,
					  "AP_ARM_CORESIGHT_STARTING",
					  etm_starting_cpu, etm_dying_cpu);
		ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
						"AP_ARM_CORESIGHT_ONLINE",
						etm_online_cpu, NULL);
		if (ret < 0)
			goto err_arch_supported;
		hp_online = ret;
	}
	put_online_cpus();

	if (etm_arch_supported(drvdata->arch) == false) {
		ret = -EINVAL;
		goto err_arch_supported;
	}

	etm_init_trace_id(drvdata);
	etm_set_default(&drvdata->config);

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &etm_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = coresight_etm_groups;
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
	dev_info(dev, "%s initialized\n", (char *)id->data);
	if (boot_enable) {
		coresight_enable(drvdata->csdev);
		drvdata->boot_enable = true;
	}

	return 0;

err_arch_supported:
	if (--etm_count == 0) {
		cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_STARTING);
		if (hp_online)
			cpuhp_remove_state_nocalls(hp_online);
	}
	return ret;
}

#ifdef CONFIG_PM
static int etm_runtime_suspend(struct device *dev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_disable_unprepare(drvdata->atclk);

	return 0;
}

static int etm_runtime_resume(struct device *dev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_prepare_enable(drvdata->atclk);

	return 0;
}
#endif

static const struct dev_pm_ops etm_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(etm_runtime_suspend, etm_runtime_resume, NULL)
};

static struct amba_id etm_ids[] = {
	{	/* ETM 3.3 */
		.id	= 0x0003b921,
		.mask	= 0x0003ffff,
		.data	= "ETM 3.3",
	},
	{	/* ETM 3.5 - Cortex-A5 */
		.id	= 0x0003b955,
		.mask	= 0x0003ffff,
		.data	= "ETM 3.5",
	},
	{	/* ETM 3.5 */
		.id	= 0x0003b956,
		.mask	= 0x0003ffff,
		.data	= "ETM 3.5",
	},
	{	/* PTM 1.0 */
		.id	= 0x0003b950,
		.mask	= 0x0003ffff,
		.data	= "PTM 1.0",
	},
	{	/* PTM 1.1 */
		.id	= 0x0003b95f,
		.mask	= 0x0003ffff,
		.data	= "PTM 1.1",
	},
	{	/* PTM 1.1 Qualcomm */
		.id	= 0x0003006f,
		.mask	= 0x0003ffff,
		.data	= "PTM 1.1",
	},
	{ 0, 0},
};

static struct amba_driver etm_driver = {
	.drv = {
		.name	= "coresight-etm3x",
		.owner	= THIS_MODULE,
		.pm	= &etm_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe		= etm_probe,
	.id_table	= etm_ids,
};
builtin_amba_driver(etm_driver);
