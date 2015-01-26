/* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
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
#include <linux/of.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <asm/sections.h>

#include "coresight-etm.h"

static int boot_enable;
module_param_named(boot_enable, boot_enable, int, S_IRUGO);

/* The number of ETM/PTM currently registered */
static int etm_count;
static struct etm_drvdata *etmdrvdata[NR_CPUS];

static inline void etm_writel(struct etm_drvdata *drvdata,
			      u32 val, u32 off)
{
	if (drvdata->use_cp14) {
		if (etm_writel_cp14(off, val)) {
			dev_err(drvdata->dev,
				"invalid CP14 access to ETM reg: %#x", off);
		}
	} else {
		writel_relaxed(val, drvdata->base + off);
	}
}

static inline unsigned int etm_readl(struct etm_drvdata *drvdata, u32 off)
{
	u32 val;

	if (drvdata->use_cp14) {
		if (etm_readl_cp14(off, &val)) {
			dev_err(drvdata->dev,
				"invalid CP14 access to ETM reg: %#x", off);
		}
	} else {
		val = readl_relaxed(drvdata->base + off);
	}

	return val;
}

/*
 * Memory mapped writes to clear os lock are not supported on some processors
 * and OS lock must be unlocked before any memory mapped access on such
 * processors, otherwise memory mapped reads/writes will be invalid.
 */
static void etm_os_unlock(void *info)
{
	struct etm_drvdata *drvdata = (struct etm_drvdata *)info;
	/* Writing any value to ETMOSLAR unlocks the trace registers */
	etm_writel(drvdata, 0x0, ETMOSLAR);
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
			"timeout observed when probing at offset %#x\n", ETMSR);
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
			"timeout observed when probing at offset %#x\n", ETMSR);
	}
}

static void etm_set_default(struct etm_drvdata *drvdata)
{
	int i;

	drvdata->trigger_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->enable_event = ETM_HARD_WIRE_RES_A;

	drvdata->seq_12_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->seq_21_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->seq_23_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->seq_31_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->seq_32_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->seq_13_event = ETM_DEFAULT_EVENT_VAL;
	drvdata->timestamp_event = ETM_DEFAULT_EVENT_VAL;

	for (i = 0; i < drvdata->nr_cntr; i++) {
		drvdata->cntr_rld_val[i] = 0x0;
		drvdata->cntr_event[i] = ETM_DEFAULT_EVENT_VAL;
		drvdata->cntr_rld_event[i] = ETM_DEFAULT_EVENT_VAL;
		drvdata->cntr_val[i] = 0x0;
	}

	drvdata->seq_curr_state = 0x0;
	drvdata->ctxid_idx = 0x0;
	for (i = 0; i < drvdata->nr_ctxid_cmp; i++)
		drvdata->ctxid_val[i] = 0x0;
	drvdata->ctxid_mask = 0x0;
}

static void etm_enable_hw(void *info)
{
	int i;
	u32 etmcr;
	struct etm_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);

	/* Turn engine on */
	etm_clr_pwrdwn(drvdata);
	/* Apply power to trace registers */
	etm_set_pwrup(drvdata);
	/* Make sure all registers are accessible */
	etm_os_unlock(drvdata);

	etm_set_prog(drvdata);

	etmcr = etm_readl(drvdata, ETMCR);
	etmcr &= (ETMCR_PWD_DWN | ETMCR_ETM_PRG);
	etmcr |= drvdata->port_size;
	etm_writel(drvdata, drvdata->ctrl | etmcr, ETMCR);
	etm_writel(drvdata, drvdata->trigger_event, ETMTRIGGER);
	etm_writel(drvdata, drvdata->startstop_ctrl, ETMTSSCR);
	etm_writel(drvdata, drvdata->enable_event, ETMTEEVR);
	etm_writel(drvdata, drvdata->enable_ctrl1, ETMTECR1);
	etm_writel(drvdata, drvdata->fifofull_level, ETMFFLR);
	for (i = 0; i < drvdata->nr_addr_cmp; i++) {
		etm_writel(drvdata, drvdata->addr_val[i], ETMACVRn(i));
		etm_writel(drvdata, drvdata->addr_acctype[i], ETMACTRn(i));
	}
	for (i = 0; i < drvdata->nr_cntr; i++) {
		etm_writel(drvdata, drvdata->cntr_rld_val[i], ETMCNTRLDVRn(i));
		etm_writel(drvdata, drvdata->cntr_event[i], ETMCNTENRn(i));
		etm_writel(drvdata, drvdata->cntr_rld_event[i],
			   ETMCNTRLDEVRn(i));
		etm_writel(drvdata, drvdata->cntr_val[i], ETMCNTVRn(i));
	}
	etm_writel(drvdata, drvdata->seq_12_event, ETMSQ12EVR);
	etm_writel(drvdata, drvdata->seq_21_event, ETMSQ21EVR);
	etm_writel(drvdata, drvdata->seq_23_event, ETMSQ23EVR);
	etm_writel(drvdata, drvdata->seq_31_event, ETMSQ31EVR);
	etm_writel(drvdata, drvdata->seq_32_event, ETMSQ32EVR);
	etm_writel(drvdata, drvdata->seq_13_event, ETMSQ13EVR);
	etm_writel(drvdata, drvdata->seq_curr_state, ETMSQR);
	for (i = 0; i < drvdata->nr_ext_out; i++)
		etm_writel(drvdata, ETM_DEFAULT_EVENT_VAL, ETMEXTOUTEVRn(i));
	for (i = 0; i < drvdata->nr_ctxid_cmp; i++)
		etm_writel(drvdata, drvdata->ctxid_val[i], ETMCIDCVRn(i));
	etm_writel(drvdata, drvdata->ctxid_mask, ETMCIDCMR);
	etm_writel(drvdata, drvdata->sync_freq, ETMSYNCFR);
	/* No external input selected */
	etm_writel(drvdata, 0x0, ETMEXTINSELR);
	etm_writel(drvdata, drvdata->timestamp_event, ETMTSEVR);
	/* No auxiliary control selected */
	etm_writel(drvdata, 0x0, ETMAUXCR);
	etm_writel(drvdata, drvdata->traceid, ETMTRACEIDR);
	/* No VMID comparator value selected */
	etm_writel(drvdata, 0x0, ETMVMIDCVR);

	/* Ensures trace output is enabled from this ETM */
	etm_writel(drvdata, drvdata->ctrl | ETMCR_ETM_EN | etmcr, ETMCR);

	etm_clr_prog(drvdata);
	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d enable smp call done\n", drvdata->cpu);
}

static int etm_trace_id_simple(struct etm_drvdata *drvdata)
{
	if (!drvdata->enable)
		return drvdata->traceid;

	return (etm_readl(drvdata, ETMTRACEIDR) & ETM_TRACEID_MASK);
}

static int etm_trace_id(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	int trace_id = -1;

	if (!drvdata->enable)
		return drvdata->traceid;

	if (clk_prepare_enable(drvdata->clk))
		goto out;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	trace_id = (etm_readl(drvdata, ETMTRACEIDR) & ETM_TRACEID_MASK);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	clk_disable_unprepare(drvdata->clk);
out:
	return trace_id;
}

static int etm_enable(struct coresight_device *csdev)
{
	struct etm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		goto err_clk;

	spin_lock(&drvdata->spinlock);

	/*
	 * Configure the ETM only if the CPU is online.  If it isn't online
	 * hw configuration will take place when 'CPU_STARTING' is received
	 * in @etm_cpu_callback.
	 */
	if (cpu_online(drvdata->cpu)) {
		ret = smp_call_function_single(drvdata->cpu,
					       etm_enable_hw, drvdata, 1);
		if (ret)
			goto err;
	}

	drvdata->enable = true;
	drvdata->sticky_enable = true;

	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "ETM tracing enabled\n");
	return 0;
err:
	spin_unlock(&drvdata->spinlock);
	clk_disable_unprepare(drvdata->clk);
err_clk:
	return ret;
}

static void etm_disable_hw(void *info)
{
	int i;
	struct etm_drvdata *drvdata = info;

	CS_UNLOCK(drvdata->base);
	etm_set_prog(drvdata);

	/* Program trace enable to low by using always false event */
	etm_writel(drvdata, ETM_HARD_WIRE_RES_A | ETM_EVENT_NOT_A, ETMTEEVR);

	/* Read back sequencer and counters for post trace analysis */
	drvdata->seq_curr_state = (etm_readl(drvdata, ETMSQR) & ETM_SQR_MASK);

	for (i = 0; i < drvdata->nr_cntr; i++)
		drvdata->cntr_val[i] = etm_readl(drvdata, ETMCNTVRn(i));

	etm_set_pwrdwn(drvdata);
	CS_LOCK(drvdata->base);

	dev_dbg(drvdata->dev, "cpu: %d disable smp call done\n", drvdata->cpu);
}

static void etm_disable(struct coresight_device *csdev)
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
	drvdata->enable = false;

	spin_unlock(&drvdata->spinlock);
	put_online_cpus();

	clk_disable_unprepare(drvdata->clk);

	dev_info(drvdata->dev, "ETM tracing disabled\n");
}

static const struct coresight_ops_source etm_source_ops = {
	.trace_id	= etm_trace_id,
	.enable		= etm_enable,
	.disable	= etm_disable,
};

static const struct coresight_ops etm_cs_ops = {
	.source_ops	= &etm_source_ops,
};

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
	int ret;
	unsigned long flags, val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	val = etm_readl(drvdata, ETMSR);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	clk_disable_unprepare(drvdata->clk);

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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val) {
		spin_lock(&drvdata->spinlock);
		drvdata->mode = ETM_MODE_EXCLUDE;
		drvdata->ctrl = 0x0;
		drvdata->trigger_event = ETM_DEFAULT_EVENT_VAL;
		drvdata->startstop_ctrl = 0x0;
		drvdata->addr_idx = 0x0;
		for (i = 0; i < drvdata->nr_addr_cmp; i++) {
			drvdata->addr_val[i] = 0x0;
			drvdata->addr_acctype[i] = 0x0;
			drvdata->addr_type[i] = ETM_ADDR_TYPE_NONE;
		}
		drvdata->cntr_idx = 0x0;

		etm_set_default(drvdata);
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

	val = drvdata->mode;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->mode = val & ETM_MODE_ALL;

	if (drvdata->mode & ETM_MODE_EXCLUDE)
		drvdata->enable_ctrl1 |= ETMTECR1_INC_EXC;
	else
		drvdata->enable_ctrl1 &= ~ETMTECR1_INC_EXC;

	if (drvdata->mode & ETM_MODE_CYCACC)
		drvdata->ctrl |= ETMCR_CYC_ACC;
	else
		drvdata->ctrl &= ~ETMCR_CYC_ACC;

	if (drvdata->mode & ETM_MODE_STALL) {
		if (!(drvdata->etmccr & ETMCCR_FIFOFULL)) {
			dev_warn(drvdata->dev, "stall mode not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		drvdata->ctrl |= ETMCR_STALL_MODE;
	 } else
		drvdata->ctrl &= ~ETMCR_STALL_MODE;

	if (drvdata->mode & ETM_MODE_TIMESTAMP) {
		if (!(drvdata->etmccer & ETMCCER_TIMESTAMP)) {
			dev_warn(drvdata->dev, "timestamp not supported\n");
			ret = -EINVAL;
			goto err_unlock;
		}
		drvdata->ctrl |= ETMCR_TIMESTAMP_EN;
	} else
		drvdata->ctrl &= ~ETMCR_TIMESTAMP_EN;

	if (drvdata->mode & ETM_MODE_CTXID)
		drvdata->ctrl |= ETMCR_CTXID_SIZE;
	else
		drvdata->ctrl &= ~ETMCR_CTXID_SIZE;
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

	val = drvdata->trigger_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_event_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->trigger_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(trigger_event);

static ssize_t enable_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->enable_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t enable_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->enable_event = val & ETM_EVENT_MASK;

	return size;
}
static DEVICE_ATTR_RW(enable_event);

static ssize_t fifofull_level_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->fifofull_level;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t fifofull_level_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->fifofull_level = val;

	return size;
}
static DEVICE_ATTR_RW(fifofull_level);

static ssize_t addr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->addr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t addr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

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
	drvdata->addr_idx = val;
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	val = drvdata->addr_val[idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_SINGLE)) {
		spin_unlock(&drvdata->spinlock);
		return -EINVAL;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_SINGLE;
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

	val1 = drvdata->addr_val[idx];
	val2 = drvdata->addr_val[idx + 1];
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

	if (sscanf(buf, "%lx %lx", &val1, &val2) != 2)
		return -EINVAL;
	/* Lower address comparator cannot have a higher address value */
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

	drvdata->addr_val[idx] = val1;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_RANGE;
	drvdata->addr_val[idx + 1] = val2;
	drvdata->addr_type[idx + 1] = ETM_ADDR_TYPE_RANGE;
	drvdata->enable_ctrl1 |= (1 << (idx/2));
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = drvdata->addr_val[idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_START)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_START;
	drvdata->startstop_ctrl |= (1 << idx);
	drvdata->enable_ctrl1 |= BIT(25);
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

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = drvdata->addr_val[idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	idx = drvdata->addr_idx;
	if (!(drvdata->addr_type[idx] == ETM_ADDR_TYPE_NONE ||
	      drvdata->addr_type[idx] == ETM_ADDR_TYPE_STOP)) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	drvdata->addr_val[idx] = val;
	drvdata->addr_type[idx] = ETM_ADDR_TYPE_STOP;
	drvdata->startstop_ctrl |= (1 << (idx + 16));
	drvdata->enable_ctrl1 |= ETMTECR1_START_STOP;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_stop);

static ssize_t addr_acctype_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->addr_acctype[drvdata->addr_idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->addr_acctype[drvdata->addr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(addr_acctype);

static ssize_t cntr_idx_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->cntr_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t cntr_idx_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

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
	drvdata->cntr_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_idx);

static ssize_t cntr_rld_val_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_rld_val[drvdata->cntr_idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_rld_val[drvdata->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_rld_val);

static ssize_t cntr_event_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_event[drvdata->cntr_idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_event[drvdata->cntr_idx] = val & ETM_EVENT_MASK;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_event);

static ssize_t cntr_rld_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->cntr_rld_event[drvdata->cntr_idx];
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_rld_event[drvdata->cntr_idx] = val & ETM_EVENT_MASK;
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

	if (!drvdata->enable) {
		spin_lock(&drvdata->spinlock);
		for (i = 0; i < drvdata->nr_cntr; i++)
			ret += sprintf(buf, "counter %d: %x\n",
				       i, drvdata->cntr_val[i]);
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->cntr_val[drvdata->cntr_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(cntr_val);

static ssize_t seq_12_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_12_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_12_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_12_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_12_event);

static ssize_t seq_21_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_21_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_21_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_21_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_21_event);

static ssize_t seq_23_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_23_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_23_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_23_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_23_event);

static ssize_t seq_31_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_31_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_31_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_31_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_31_event);

static ssize_t seq_32_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_32_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_32_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_32_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_32_event);

static ssize_t seq_13_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->seq_13_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t seq_13_event_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->seq_13_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(seq_13_event);

static ssize_t seq_curr_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long val, flags;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!drvdata->enable) {
		val = drvdata->seq_curr_state;
		goto out;
	}

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	val = (etm_readl(drvdata, ETMSQR) & ETM_SQR_MASK);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	clk_disable_unprepare(drvdata->clk);
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

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val > ETM_SEQ_STATE_MAX_VAL)
		return -EINVAL;

	drvdata->seq_curr_state = val;

	return size;
}
static DEVICE_ATTR_RW(seq_curr_state);

static ssize_t ctxid_idx_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ctxid_idx;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_idx_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

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
	drvdata->ctxid_idx = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_idx);

static ssize_t ctxid_val_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	val = drvdata->ctxid_val[drvdata->ctxid_idx];
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_val_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->ctxid_val[drvdata->ctxid_idx] = val;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(ctxid_val);

static ssize_t ctxid_mask_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->ctxid_mask;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t ctxid_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->ctxid_mask = val;
	return size;
}
static DEVICE_ATTR_RW(ctxid_mask);

static ssize_t sync_freq_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->sync_freq;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t sync_freq_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->sync_freq = val & ETM_SYNC_MASK;
	return size;
}
static DEVICE_ATTR_RW(sync_freq);

static ssize_t timestamp_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->timestamp_event;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t timestamp_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->timestamp_event = val & ETM_EVENT_MASK;
	return size;
}
static DEVICE_ATTR_RW(timestamp_event);

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long flags;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	CS_UNLOCK(drvdata->base);
	ret = sprintf(buf,
		      "ETMCCR: 0x%08x\n"
		      "ETMCCER: 0x%08x\n"
		      "ETMSCR: 0x%08x\n"
		      "ETMIDR: 0x%08x\n"
		      "ETMCR: 0x%08x\n"
		      "ETMTRACEIDR: 0x%08x\n"
		      "Enable event: 0x%08x\n"
		      "Enable start/stop: 0x%08x\n"
		      "Enable control: CR1 0x%08x CR2 0x%08x\n"
		      "CPU affinity: %d\n",
		      drvdata->etmccr, drvdata->etmccer,
		      etm_readl(drvdata, ETMSCR), etm_readl(drvdata, ETMIDR),
		      etm_readl(drvdata, ETMCR), etm_trace_id_simple(drvdata),
		      etm_readl(drvdata, ETMTEEVR),
		      etm_readl(drvdata, ETMTSSCR),
		      etm_readl(drvdata, ETMTECR1),
		      etm_readl(drvdata, ETMTECR2),
		      drvdata->cpu);
	CS_LOCK(drvdata->base);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	clk_disable_unprepare(drvdata->clk);

	return ret;
}
static DEVICE_ATTR_RO(status);

static ssize_t traceid_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long val, flags;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!drvdata->enable) {
		val = drvdata->traceid;
		goto out;
	}

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	val = (etm_readl(drvdata, ETMTRACEIDR) & ETM_TRACEID_MASK);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	clk_disable_unprepare(drvdata->clk);
out:
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t traceid_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->traceid = val & ETM_TRACEID_MASK;
	return size;
}
static DEVICE_ATTR_RW(traceid);

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
	&dev_attr_ctxid_val.attr,
	&dev_attr_ctxid_mask.attr,
	&dev_attr_sync_freq.attr,
	&dev_attr_timestamp_event.attr,
	&dev_attr_status.attr,
	&dev_attr_traceid.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_etm);

static int etm_cpu_callback(struct notifier_block *nfb, unsigned long action,
			    void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	if (!etmdrvdata[cpu])
		goto out;

	switch (action & (~CPU_TASKS_FROZEN)) {
	case CPU_STARTING:
		spin_lock(&etmdrvdata[cpu]->spinlock);
		if (!etmdrvdata[cpu]->os_unlock) {
			etm_os_unlock(etmdrvdata[cpu]);
			etmdrvdata[cpu]->os_unlock = true;
		}

		if (etmdrvdata[cpu]->enable)
			etm_enable_hw(etmdrvdata[cpu]);
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
			etm_disable_hw(etmdrvdata[cpu]);
		spin_unlock(&etmdrvdata[cpu]->spinlock);
		break;
	}
out:
	return NOTIFY_OK;
}

static struct notifier_block etm_cpu_notifier = {
	.notifier_call = etm_cpu_callback,
};

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

static void etm_init_default_data(struct etm_drvdata *drvdata)
{
	/*
	 * A trace ID of value 0 is invalid, so let's start at some
	 * random value that fits in 7 bits and will be just as good.
	 */
	static int etm3x_traceid = 0x10;

	u32 flags = (1 << 0 | /* instruction execute*/
		     3 << 3 | /* ARM instruction */
		     0 << 5 | /* No data value comparison */
		     0 << 7 | /* No exact mach */
		     0 << 8 | /* Ignore context ID */
		     0 << 10); /* Security ignored */

	/*
	 * Initial configuration only - guarantees sources handled by
	 * this driver have a unique ID at startup time but not between
	 * all other types of sources.  For that we lean on the core
	 * framework.
	 */
	drvdata->traceid = etm3x_traceid++;
	drvdata->ctrl = (ETMCR_CYC_ACC | ETMCR_TIMESTAMP_EN);
	drvdata->enable_ctrl1 = ETMTECR1_ADDR_COMP_1;
	if (drvdata->nr_addr_cmp >= 2) {
		drvdata->addr_val[0] = (u32) _stext;
		drvdata->addr_val[1] = (u32) _etext;
		drvdata->addr_acctype[0] = flags;
		drvdata->addr_acctype[1] = flags;
		drvdata->addr_type[0] = ETM_ADDR_TYPE_RANGE;
		drvdata->addr_type[1] = ETM_ADDR_TYPE_RANGE;
	}

	etm_set_default(drvdata);
}

static int etm_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct etm_drvdata *drvdata;
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

	drvdata->clk = adev->pclk;
	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	drvdata->cpu = pdata ? pdata->cpu : 0;

	get_online_cpus();
	etmdrvdata[drvdata->cpu] = drvdata;

	if (!smp_call_function_single(drvdata->cpu, etm_os_unlock, drvdata, 1))
		drvdata->os_unlock = true;

	if (smp_call_function_single(drvdata->cpu,
				     etm_init_arch_data,  drvdata, 1))
		dev_err(dev, "ETM arch init failed\n");

	if (!etm_count++)
		register_hotcpu_notifier(&etm_cpu_notifier);

	put_online_cpus();

	if (etm_arch_supported(drvdata->arch) == false) {
		ret = -EINVAL;
		goto err_arch_supported;
	}
	etm_init_default_data(drvdata);

	clk_disable_unprepare(drvdata->clk);

	desc->type = CORESIGHT_DEV_TYPE_SOURCE;
	desc->subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc->ops = &etm_cs_ops;
	desc->pdata = pdata;
	desc->dev = dev;
	desc->groups = coresight_etm_groups;
	drvdata->csdev = coresight_register(desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto err_arch_supported;
	}

	dev_info(dev, "ETM initialized\n");

	if (boot_enable) {
		coresight_enable(drvdata->csdev);
		drvdata->boot_enable = true;
	}

	return 0;

err_arch_supported:
	clk_disable_unprepare(drvdata->clk);
	if (--etm_count == 0)
		unregister_hotcpu_notifier(&etm_cpu_notifier);
	return ret;
}

static int etm_remove(struct amba_device *adev)
{
	struct etm_drvdata *drvdata = amba_get_drvdata(adev);

	coresight_unregister(drvdata->csdev);
	if (--etm_count == 0)
		unregister_hotcpu_notifier(&etm_cpu_notifier);

	return 0;
}

static struct amba_id etm_ids[] = {
	{	/* ETM 3.3 */
		.id	= 0x0003b921,
		.mask	= 0x0003ffff,
	},
	{	/* ETM 3.5 */
		.id	= 0x0003b956,
		.mask	= 0x0003ffff,
	},
	{	/* PTM 1.0 */
		.id	= 0x0003b950,
		.mask	= 0x0003ffff,
	},
	{	/* PTM 1.1 */
		.id	= 0x0003b95f,
		.mask	= 0x0003ffff,
	},
	{ 0, 0},
};

static struct amba_driver etm_driver = {
	.drv = {
		.name	= "coresight-etm3x",
		.owner	= THIS_MODULE,
	},
	.probe		= etm_probe,
	.remove		= etm_remove,
	.id_table	= etm_ids,
};

int __init etm_init(void)
{
	return amba_driver_register(&etm_driver);
}
module_init(etm_init);

void __exit etm_exit(void)
{
	amba_driver_unregister(&etm_driver);
}
module_exit(etm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CoreSight Program Flow Trace driver");
