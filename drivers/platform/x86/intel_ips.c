/*
 * Copyright (c) 2009-2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Authors:
 *	Jesse Barnes <jbarnes@virtuousgeek.org>
 */

/*
 * Some Intel Ibex Peak based platforms support so-called "intelligent
 * power sharing", which allows the CPU and GPU to cooperate to maximize
 * performance within a given TDP (thermal design point).  This driver
 * performs the coordination between the CPU and GPU, monitors thermal and
 * power statistics in the platform, and initializes power monitoring
 * hardware.  It also provides a few tunables to control behavior.  Its
 * primary purpose is to safely allow CPU and GPU turbo modes to be enabled
 * by tracking power and thermal budget; secondarily it can boost turbo
 * performance by allocating more power or thermal budget to the CPU or GPU
 * based on available headroom and activity.
 *
 * The basic algorithm is driven by a 5s moving average of temperature.  If
 * thermal headroom is available, the CPU and/or GPU power clamps may be
 * adjusted upwards.  If we hit the thermal ceiling or a thermal trigger,
 * we scale back the clamp.  Aside from trigger events (when we're critically
 * close or over our TDP) we don't adjust the clamps more than once every
 * five seconds.
 *
 * The thermal device (device 31, function 6) has a set of registers that
 * are updated by the ME firmware.  The ME should also take the clamp values
 * written to those registers and write them to the CPU, but we currently
 * bypass that functionality and write the CPU MSR directly.
 *
 * UNSUPPORTED:
 *   - dual MCP configs
 *
 * TODO:
 *   - handle CPU hotplug
 *   - provide turbo enable/disable api
 *
 * Related documents:
 *   - CDI 403777, 403778 - Auburndale EDS vol 1 & 2
 *   - CDI 401376 - Ibex Peak EDS
 *   - ref 26037, 26641 - IPS BIOS spec
 *   - ref 26489 - Nehalem BIOS writer's guide
 *   - ref 26921 - Ibex Peak BIOS Specification
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/tick.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <drm/i915_drm.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include "intel_ips.h"

#include <linux/io-64-nonatomic-lo-hi.h>

#define PCI_DEVICE_ID_INTEL_THERMAL_SENSOR 0x3b32

/*
 * Package level MSRs for monitor/control
 */
#define PLATFORM_INFO	0xce
#define   PLATFORM_TDP		(1<<29)
#define   PLATFORM_RATIO	(1<<28)

#define IA32_MISC_ENABLE	0x1a0
#define   IA32_MISC_TURBO_EN	(1ULL<<38)

#define TURBO_POWER_CURRENT_LIMIT	0x1ac
#define   TURBO_TDC_OVR_EN	(1UL<<31)
#define   TURBO_TDC_MASK	(0x000000007fff0000UL)
#define   TURBO_TDC_SHIFT	(16)
#define   TURBO_TDP_OVR_EN	(1UL<<15)
#define   TURBO_TDP_MASK	(0x0000000000003fffUL)

/*
 * Core/thread MSRs for monitoring
 */
#define IA32_PERF_CTL		0x199
#define   IA32_PERF_TURBO_DIS	(1ULL<<32)

/*
 * Thermal PCI device regs
 */
#define THM_CFG_TBAR	0x10
#define THM_CFG_TBAR_HI	0x14

#define THM_TSIU	0x00
#define THM_TSE		0x01
#define   TSE_EN	0xb8
#define THM_TSS		0x02
#define THM_TSTR	0x03
#define THM_TSTTP	0x04
#define THM_TSCO	0x08
#define THM_TSES	0x0c
#define THM_TSGPEN	0x0d
#define   TSGPEN_HOT_LOHI	(1<<1)
#define   TSGPEN_CRIT_LOHI	(1<<2)
#define THM_TSPC	0x0e
#define THM_PPEC	0x10
#define THM_CTA		0x12
#define THM_PTA		0x14
#define   PTA_SLOPE_MASK	(0xff00)
#define   PTA_SLOPE_SHIFT	8
#define   PTA_OFFSET_MASK	(0x00ff)
#define THM_MGTA	0x16
#define   MGTA_SLOPE_MASK	(0xff00)
#define   MGTA_SLOPE_SHIFT	8
#define   MGTA_OFFSET_MASK	(0x00ff)
#define THM_TRC		0x1a
#define   TRC_CORE2_EN	(1<<15)
#define   TRC_THM_EN	(1<<12)
#define   TRC_C6_WAR	(1<<8)
#define   TRC_CORE1_EN	(1<<7)
#define   TRC_CORE_PWR	(1<<6)
#define   TRC_PCH_EN	(1<<5)
#define   TRC_MCH_EN	(1<<4)
#define   TRC_DIMM4	(1<<3)
#define   TRC_DIMM3	(1<<2)
#define   TRC_DIMM2	(1<<1)
#define   TRC_DIMM1	(1<<0)
#define THM_TES		0x20
#define THM_TEN		0x21
#define   TEN_UPDATE_EN	1
#define THM_PSC		0x24
#define   PSC_NTG	(1<<0) /* No GFX turbo support */
#define   PSC_NTPC	(1<<1) /* No CPU turbo support */
#define   PSC_PP_DEF	(0<<2) /* Perf policy up to driver */
#define   PSP_PP_PC	(1<<2) /* BIOS prefers CPU perf */
#define   PSP_PP_BAL	(2<<2) /* BIOS wants balanced perf */
#define   PSP_PP_GFX	(3<<2) /* BIOS prefers GFX perf */
#define   PSP_PBRT	(1<<4) /* BIOS run time support */
#define THM_CTV1	0x30
#define   CTV_TEMP_ERROR (1<<15)
#define   CTV_TEMP_MASK	0x3f
#define   CTV_
#define THM_CTV2	0x32
#define THM_CEC		0x34 /* undocumented power accumulator in joules */
#define THM_AE		0x3f
#define THM_HTS		0x50 /* 32 bits */
#define   HTS_PCPL_MASK	(0x7fe00000)
#define   HTS_PCPL_SHIFT 21
#define   HTS_GPL_MASK  (0x001ff000)
#define   HTS_GPL_SHIFT 12
#define   HTS_PP_MASK	(0x00000c00)
#define   HTS_PP_SHIFT  10
#define   HTS_PP_DEF	0
#define   HTS_PP_PROC	1
#define   HTS_PP_BAL	2
#define   HTS_PP_GFX	3
#define   HTS_PCTD_DIS	(1<<9)
#define   HTS_GTD_DIS	(1<<8)
#define   HTS_PTL_MASK  (0x000000fe)
#define   HTS_PTL_SHIFT 1
#define   HTS_NVV	(1<<0)
#define THM_HTSHI	0x54 /* 16 bits */
#define   HTS2_PPL_MASK		(0x03ff)
#define   HTS2_PRST_MASK	(0x3c00)
#define   HTS2_PRST_SHIFT	10
#define   HTS2_PRST_UNLOADED	0
#define   HTS2_PRST_RUNNING	1
#define   HTS2_PRST_TDISOP	2 /* turbo disabled due to power */
#define   HTS2_PRST_TDISHT	3 /* turbo disabled due to high temp */
#define   HTS2_PRST_TDISUSR	4 /* user disabled turbo */
#define   HTS2_PRST_TDISPLAT	5 /* platform disabled turbo */
#define   HTS2_PRST_TDISPM	6 /* power management disabled turbo */
#define   HTS2_PRST_TDISERR	7 /* some kind of error disabled turbo */
#define THM_PTL		0x56
#define THM_MGTV	0x58
#define   TV_MASK	0x000000000000ff00
#define   TV_SHIFT	8
#define THM_PTV		0x60
#define   PTV_MASK	0x00ff
#define THM_MMGPC	0x64
#define THM_MPPC	0x66
#define THM_MPCPC	0x68
#define THM_TSPIEN	0x82
#define   TSPIEN_AUX_LOHI	(1<<0)
#define   TSPIEN_HOT_LOHI	(1<<1)
#define   TSPIEN_CRIT_LOHI	(1<<2)
#define   TSPIEN_AUX2_LOHI	(1<<3)
#define THM_TSLOCK	0x83
#define THM_ATR		0x84
#define THM_TOF		0x87
#define THM_STS		0x98
#define   STS_PCPL_MASK		(0x7fe00000)
#define   STS_PCPL_SHIFT	21
#define   STS_GPL_MASK		(0x001ff000)
#define   STS_GPL_SHIFT		12
#define   STS_PP_MASK		(0x00000c00)
#define   STS_PP_SHIFT		10
#define   STS_PP_DEF		0
#define   STS_PP_PROC		1
#define   STS_PP_BAL		2
#define   STS_PP_GFX		3
#define   STS_PCTD_DIS		(1<<9)
#define   STS_GTD_DIS		(1<<8)
#define   STS_PTL_MASK		(0x000000fe)
#define   STS_PTL_SHIFT		1
#define   STS_NVV		(1<<0)
#define THM_SEC		0x9c
#define   SEC_ACK	(1<<0)
#define THM_TC3		0xa4
#define THM_TC1		0xa8
#define   STS_PPL_MASK		(0x0003ff00)
#define   STS_PPL_SHIFT		16
#define THM_TC2		0xac
#define THM_DTV		0xb0
#define THM_ITV		0xd8
#define   ITV_ME_SEQNO_MASK 0x00ff0000 /* ME should update every ~200ms */
#define   ITV_ME_SEQNO_SHIFT (16)
#define   ITV_MCH_TEMP_MASK 0x0000ff00
#define   ITV_MCH_TEMP_SHIFT (8)
#define   ITV_PCH_TEMP_MASK 0x000000ff

#define thm_readb(off) readb(ips->regmap + (off))
#define thm_readw(off) readw(ips->regmap + (off))
#define thm_readl(off) readl(ips->regmap + (off))
#define thm_readq(off) readq(ips->regmap + (off))

#define thm_writeb(off, val) writeb((val), ips->regmap + (off))
#define thm_writew(off, val) writew((val), ips->regmap + (off))
#define thm_writel(off, val) writel((val), ips->regmap + (off))

static const int IPS_ADJUST_PERIOD = 5000; /* ms */
static bool late_i915_load = false;

/* For initial average collection */
static const int IPS_SAMPLE_PERIOD = 200; /* ms */
static const int IPS_SAMPLE_WINDOW = 5000; /* 5s moving window of samples */
#define IPS_SAMPLE_COUNT (IPS_SAMPLE_WINDOW / IPS_SAMPLE_PERIOD)

/* Per-SKU limits */
struct ips_mcp_limits {
	int mcp_power_limit; /* mW units */
	int core_power_limit;
	int mch_power_limit;
	int core_temp_limit; /* degrees C */
	int mch_temp_limit;
};

/* Max temps are -10 degrees C to avoid PROCHOT# */

static struct ips_mcp_limits ips_sv_limits = {
	.mcp_power_limit = 35000,
	.core_power_limit = 29000,
	.mch_power_limit = 20000,
	.core_temp_limit = 95,
	.mch_temp_limit = 90
};

static struct ips_mcp_limits ips_lv_limits = {
	.mcp_power_limit = 25000,
	.core_power_limit = 21000,
	.mch_power_limit = 13000,
	.core_temp_limit = 95,
	.mch_temp_limit = 90
};

static struct ips_mcp_limits ips_ulv_limits = {
	.mcp_power_limit = 18000,
	.core_power_limit = 14000,
	.mch_power_limit = 11000,
	.core_temp_limit = 95,
	.mch_temp_limit = 90
};

struct ips_driver {
	struct device *dev;
	void __iomem *regmap;
	int irq;

	struct task_struct *monitor;
	struct task_struct *adjust;
	struct dentry *debug_root;
	struct timer_list timer;

	/* Average CPU core temps (all averages in .01 degrees C for precision) */
	u16 ctv1_avg_temp;
	u16 ctv2_avg_temp;
	/* GMCH average */
	u16 mch_avg_temp;
	/* Average for the CPU (both cores?) */
	u16 mcp_avg_temp;
	/* Average power consumption (in mW) */
	u32 cpu_avg_power;
	u32 mch_avg_power;

	/* Offset values */
	u16 cta_val;
	u16 pta_val;
	u16 mgta_val;

	/* Maximums & prefs, protected by turbo status lock */
	spinlock_t turbo_status_lock;
	u16 mcp_temp_limit;
	u16 mcp_power_limit;
	u16 core_power_limit;
	u16 mch_power_limit;
	bool cpu_turbo_enabled;
	bool __cpu_turbo_on;
	bool gpu_turbo_enabled;
	bool __gpu_turbo_on;
	bool gpu_preferred;
	bool poll_turbo_status;
	bool second_cpu;
	bool turbo_toggle_allowed;
	struct ips_mcp_limits *limits;

	/* Optional MCH interfaces for if i915 is in use */
	unsigned long (*read_mch_val)(void);
	bool (*gpu_raise)(void);
	bool (*gpu_lower)(void);
	bool (*gpu_busy)(void);
	bool (*gpu_turbo_disable)(void);

	/* For restoration at unload */
	u64 orig_turbo_limit;
	u64 orig_turbo_ratios;
};

static bool
ips_gpu_turbo_enabled(struct ips_driver *ips);

/**
 * ips_cpu_busy - is CPU busy?
 * @ips: IPS driver struct
 *
 * Check CPU for load to see whether we should increase its thermal budget.
 *
 * RETURNS:
 * True if the CPU could use more power, false otherwise.
 */
static bool ips_cpu_busy(struct ips_driver *ips)
{
	if ((avenrun[0] >> FSHIFT) > 1)
		return true;

	return false;
}

/**
 * ips_cpu_raise - raise CPU power clamp
 * @ips: IPS driver struct
 *
 * Raise the CPU power clamp by %IPS_CPU_STEP, in accordance with TDP for
 * this platform.
 *
 * We do this by adjusting the TURBO_POWER_CURRENT_LIMIT MSR upwards (as
 * long as we haven't hit the TDP limit for the SKU).
 */
static void ips_cpu_raise(struct ips_driver *ips)
{
	u64 turbo_override;
	u16 cur_tdp_limit, new_tdp_limit;

	if (!ips->cpu_turbo_enabled)
		return;

	rdmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);

	cur_tdp_limit = turbo_override & TURBO_TDP_MASK;
	new_tdp_limit = cur_tdp_limit + 8; /* 1W increase */

	/* Clamp to SKU TDP limit */
	if (((new_tdp_limit * 10) / 8) > ips->core_power_limit)
		new_tdp_limit = cur_tdp_limit;

	thm_writew(THM_MPCPC, (new_tdp_limit * 10) / 8);

	turbo_override |= TURBO_TDC_OVR_EN | TURBO_TDP_OVR_EN;
	wrmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);

	turbo_override &= ~TURBO_TDP_MASK;
	turbo_override |= new_tdp_limit;

	wrmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);
}

/**
 * ips_cpu_lower - lower CPU power clamp
 * @ips: IPS driver struct
 *
 * Lower CPU power clamp b %IPS_CPU_STEP if possible.
 *
 * We do this by adjusting the TURBO_POWER_CURRENT_LIMIT MSR down, going
 * as low as the platform limits will allow (though we could go lower there
 * wouldn't be much point).
 */
static void ips_cpu_lower(struct ips_driver *ips)
{
	u64 turbo_override;
	u16 cur_limit, new_limit;

	rdmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);

	cur_limit = turbo_override & TURBO_TDP_MASK;
	new_limit = cur_limit - 8; /* 1W decrease */

	/* Clamp to SKU TDP limit */
	if (new_limit  < (ips->orig_turbo_limit & TURBO_TDP_MASK))
		new_limit = ips->orig_turbo_limit & TURBO_TDP_MASK;

	thm_writew(THM_MPCPC, (new_limit * 10) / 8);

	turbo_override |= TURBO_TDC_OVR_EN | TURBO_TDP_OVR_EN;
	wrmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);

	turbo_override &= ~TURBO_TDP_MASK;
	turbo_override |= new_limit;

	wrmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);
}

/**
 * do_enable_cpu_turbo - internal turbo enable function
 * @data: unused
 *
 * Internal function for actually updating MSRs.  When we enable/disable
 * turbo, we need to do it on each CPU; this function is the one called
 * by on_each_cpu() when needed.
 */
static void do_enable_cpu_turbo(void *data)
{
	u64 perf_ctl;

	rdmsrl(IA32_PERF_CTL, perf_ctl);
	if (perf_ctl & IA32_PERF_TURBO_DIS) {
		perf_ctl &= ~IA32_PERF_TURBO_DIS;
		wrmsrl(IA32_PERF_CTL, perf_ctl);
	}
}

/**
 * ips_enable_cpu_turbo - enable turbo mode on all CPUs
 * @ips: IPS driver struct
 *
 * Enable turbo mode by clearing the disable bit in IA32_PERF_CTL on
 * all logical threads.
 */
static void ips_enable_cpu_turbo(struct ips_driver *ips)
{
	/* Already on, no need to mess with MSRs */
	if (ips->__cpu_turbo_on)
		return;

	if (ips->turbo_toggle_allowed)
		on_each_cpu(do_enable_cpu_turbo, ips, 1);

	ips->__cpu_turbo_on = true;
}

/**
 * do_disable_cpu_turbo - internal turbo disable function
 * @data: unused
 *
 * Internal function for actually updating MSRs.  When we enable/disable
 * turbo, we need to do it on each CPU; this function is the one called
 * by on_each_cpu() when needed.
 */
static void do_disable_cpu_turbo(void *data)
{
	u64 perf_ctl;

	rdmsrl(IA32_PERF_CTL, perf_ctl);
	if (!(perf_ctl & IA32_PERF_TURBO_DIS)) {
		perf_ctl |= IA32_PERF_TURBO_DIS;
		wrmsrl(IA32_PERF_CTL, perf_ctl);
	}
}

/**
 * ips_disable_cpu_turbo - disable turbo mode on all CPUs
 * @ips: IPS driver struct
 *
 * Disable turbo mode by setting the disable bit in IA32_PERF_CTL on
 * all logical threads.
 */
static void ips_disable_cpu_turbo(struct ips_driver *ips)
{
	/* Already off, leave it */
	if (!ips->__cpu_turbo_on)
		return;

	if (ips->turbo_toggle_allowed)
		on_each_cpu(do_disable_cpu_turbo, ips, 1);

	ips->__cpu_turbo_on = false;
}

/**
 * ips_gpu_busy - is GPU busy?
 * @ips: IPS driver struct
 *
 * Check GPU for load to see whether we should increase its thermal budget.
 * We need to call into the i915 driver in this case.
 *
 * RETURNS:
 * True if the GPU could use more power, false otherwise.
 */
static bool ips_gpu_busy(struct ips_driver *ips)
{
	if (!ips_gpu_turbo_enabled(ips))
		return false;

	return ips->gpu_busy();
}

/**
 * ips_gpu_raise - raise GPU power clamp
 * @ips: IPS driver struct
 *
 * Raise the GPU frequency/power if possible.  We need to call into the
 * i915 driver in this case.
 */
static void ips_gpu_raise(struct ips_driver *ips)
{
	if (!ips_gpu_turbo_enabled(ips))
		return;

	if (!ips->gpu_raise())
		ips->gpu_turbo_enabled = false;

	return;
}

/**
 * ips_gpu_lower - lower GPU power clamp
 * @ips: IPS driver struct
 *
 * Lower GPU frequency/power if possible.  Need to call i915.
 */
static void ips_gpu_lower(struct ips_driver *ips)
{
	if (!ips_gpu_turbo_enabled(ips))
		return;

	if (!ips->gpu_lower())
		ips->gpu_turbo_enabled = false;

	return;
}

/**
 * ips_enable_gpu_turbo - notify the gfx driver turbo is available
 * @ips: IPS driver struct
 *
 * Call into the graphics driver indicating that it can safely use
 * turbo mode.
 */
static void ips_enable_gpu_turbo(struct ips_driver *ips)
{
	if (ips->__gpu_turbo_on)
		return;
	ips->__gpu_turbo_on = true;
}

/**
 * ips_disable_gpu_turbo - notify the gfx driver to disable turbo mode
 * @ips: IPS driver struct
 *
 * Request that the graphics driver disable turbo mode.
 */
static void ips_disable_gpu_turbo(struct ips_driver *ips)
{
	/* Avoid calling i915 if turbo is already disabled */
	if (!ips->__gpu_turbo_on)
		return;

	if (!ips->gpu_turbo_disable())
		dev_err(ips->dev, "failed to disable graphics turbo\n");
	else
		ips->__gpu_turbo_on = false;
}

/**
 * mcp_exceeded - check whether we're outside our thermal & power limits
 * @ips: IPS driver struct
 *
 * Check whether the MCP is over its thermal or power budget.
 */
static bool mcp_exceeded(struct ips_driver *ips)
{
	unsigned long flags;
	bool ret = false;
	u32 temp_limit;
	u32 avg_power;

	spin_lock_irqsave(&ips->turbo_status_lock, flags);

	temp_limit = ips->mcp_temp_limit * 100;
	if (ips->mcp_avg_temp > temp_limit)
		ret = true;

	avg_power = ips->cpu_avg_power + ips->mch_avg_power;
	if (avg_power > ips->mcp_power_limit)
		ret = true;

	spin_unlock_irqrestore(&ips->turbo_status_lock, flags);

	return ret;
}

/**
 * cpu_exceeded - check whether a CPU core is outside its limits
 * @ips: IPS driver struct
 * @cpu: CPU number to check
 *
 * Check a given CPU's average temp or power is over its limit.
 */
static bool cpu_exceeded(struct ips_driver *ips, int cpu)
{
	unsigned long flags;
	int avg;
	bool ret = false;

	spin_lock_irqsave(&ips->turbo_status_lock, flags);
	avg = cpu ? ips->ctv2_avg_temp : ips->ctv1_avg_temp;
	if (avg > (ips->limits->core_temp_limit * 100))
		ret = true;
	if (ips->cpu_avg_power > ips->core_power_limit * 100)
		ret = true;
	spin_unlock_irqrestore(&ips->turbo_status_lock, flags);

	if (ret)
		dev_info(ips->dev, "CPU power or thermal limit exceeded\n");

	return ret;
}

/**
 * mch_exceeded - check whether the GPU is over budget
 * @ips: IPS driver struct
 *
 * Check the MCH temp & power against their maximums.
 */
static bool mch_exceeded(struct ips_driver *ips)
{
	unsigned long flags;
	bool ret = false;

	spin_lock_irqsave(&ips->turbo_status_lock, flags);
	if (ips->mch_avg_temp > (ips->limits->mch_temp_limit * 100))
		ret = true;
	if (ips->mch_avg_power > ips->mch_power_limit)
		ret = true;
	spin_unlock_irqrestore(&ips->turbo_status_lock, flags);

	return ret;
}

/**
 * verify_limits - verify BIOS provided limits
 * @ips: IPS structure
 *
 * BIOS can optionally provide non-default limits for power and temp.  Check
 * them here and use the defaults if the BIOS values are not provided or
 * are otherwise unusable.
 */
static void verify_limits(struct ips_driver *ips)
{
	if (ips->mcp_power_limit < ips->limits->mcp_power_limit ||
	    ips->mcp_power_limit > 35000)
		ips->mcp_power_limit = ips->limits->mcp_power_limit;

	if (ips->mcp_temp_limit < ips->limits->core_temp_limit ||
	    ips->mcp_temp_limit < ips->limits->mch_temp_limit ||
	    ips->mcp_temp_limit > 150)
		ips->mcp_temp_limit = min(ips->limits->core_temp_limit,
					  ips->limits->mch_temp_limit);
}

/**
 * update_turbo_limits - get various limits & settings from regs
 * @ips: IPS driver struct
 *
 * Update the IPS power & temp limits, along with turbo enable flags,
 * based on latest register contents.
 *
 * Used at init time and for runtime BIOS support, which requires polling
 * the regs for updates (as a result of AC->DC transition for example).
 *
 * LOCKING:
 * Caller must hold turbo_status_lock (outside of init)
 */
static void update_turbo_limits(struct ips_driver *ips)
{
	u32 hts = thm_readl(THM_HTS);

	ips->cpu_turbo_enabled = !(hts & HTS_PCTD_DIS);
	/* 
	 * Disable turbo for now, until we can figure out why the power figures
	 * are wrong
	 */
	ips->cpu_turbo_enabled = false;

	if (ips->gpu_busy)
		ips->gpu_turbo_enabled = !(hts & HTS_GTD_DIS);

	ips->core_power_limit = thm_readw(THM_MPCPC);
	ips->mch_power_limit = thm_readw(THM_MMGPC);
	ips->mcp_temp_limit = thm_readw(THM_PTL);
	ips->mcp_power_limit = thm_readw(THM_MPPC);

	verify_limits(ips);
	/* Ignore BIOS CPU vs GPU pref */
}

/**
 * ips_adjust - adjust power clamp based on thermal state
 * @data: ips driver structure
 *
 * Wake up every 5s or so and check whether we should adjust the power clamp.
 * Check CPU and GPU load to determine which needs adjustment.  There are
 * several things to consider here:
 *   - do we need to adjust up or down?
 *   - is CPU busy?
 *   - is GPU busy?
 *   - is CPU in turbo?
 *   - is GPU in turbo?
 *   - is CPU or GPU preferred? (CPU is default)
 *
 * So, given the above, we do the following:
 *   - up (TDP available)
 *     - CPU not busy, GPU not busy - nothing
 *     - CPU busy, GPU not busy - adjust CPU up
 *     - CPU not busy, GPU busy - adjust GPU up
 *     - CPU busy, GPU busy - adjust preferred unit up, taking headroom from
 *       non-preferred unit if necessary
 *   - down (at TDP limit)
 *     - adjust both CPU and GPU down if possible
 *
		cpu+ gpu+	cpu+gpu-	cpu-gpu+	cpu-gpu-
cpu < gpu <	cpu+gpu+	cpu+		gpu+		nothing
cpu < gpu >=	cpu+gpu-(mcp<)	cpu+gpu-(mcp<)	gpu-		gpu-
cpu >= gpu <	cpu-gpu+(mcp<)	cpu-		cpu-gpu+(mcp<)	cpu-
cpu >= gpu >=	cpu-gpu-	cpu-gpu-	cpu-gpu-	cpu-gpu-
 *
 */
static int ips_adjust(void *data)
{
	struct ips_driver *ips = data;
	unsigned long flags;

	dev_dbg(ips->dev, "starting ips-adjust thread\n");

	/*
	 * Adjust CPU and GPU clamps every 5s if needed.  Doing it more
	 * often isn't recommended due to ME interaction.
	 */
	do {
		bool cpu_busy = ips_cpu_busy(ips);
		bool gpu_busy = ips_gpu_busy(ips);

		spin_lock_irqsave(&ips->turbo_status_lock, flags);
		if (ips->poll_turbo_status)
			update_turbo_limits(ips);
		spin_unlock_irqrestore(&ips->turbo_status_lock, flags);

		/* Update turbo status if necessary */
		if (ips->cpu_turbo_enabled)
			ips_enable_cpu_turbo(ips);
		else
			ips_disable_cpu_turbo(ips);

		if (ips->gpu_turbo_enabled)
			ips_enable_gpu_turbo(ips);
		else
			ips_disable_gpu_turbo(ips);

		/* We're outside our comfort zone, crank them down */
		if (mcp_exceeded(ips)) {
			ips_cpu_lower(ips);
			ips_gpu_lower(ips);
			goto sleep;
		}

		if (!cpu_exceeded(ips, 0) && cpu_busy)
			ips_cpu_raise(ips);
		else
			ips_cpu_lower(ips);

		if (!mch_exceeded(ips) && gpu_busy)
			ips_gpu_raise(ips);
		else
			ips_gpu_lower(ips);

sleep:
		schedule_timeout_interruptible(msecs_to_jiffies(IPS_ADJUST_PERIOD));
	} while (!kthread_should_stop());

	dev_dbg(ips->dev, "ips-adjust thread stopped\n");

	return 0;
}

/*
 * Helpers for reading out temp/power values and calculating their
 * averages for the decision making and monitoring functions.
 */

static u16 calc_avg_temp(struct ips_driver *ips, u16 *array)
{
	u64 total = 0;
	int i;
	u16 avg;

	for (i = 0; i < IPS_SAMPLE_COUNT; i++)
		total += (u64)(array[i] * 100);

	do_div(total, IPS_SAMPLE_COUNT);

	avg = (u16)total;

	return avg;
}

static u16 read_mgtv(struct ips_driver *ips)
{
	u16 ret;
	u64 slope, offset;
	u64 val;

	val = thm_readq(THM_MGTV);
	val = (val & TV_MASK) >> TV_SHIFT;

	slope = offset = thm_readw(THM_MGTA);
	slope = (slope & MGTA_SLOPE_MASK) >> MGTA_SLOPE_SHIFT;
	offset = offset & MGTA_OFFSET_MASK;

	ret = ((val * slope + 0x40) >> 7) + offset;

	return 0; /* MCH temp reporting buggy */
}

static u16 read_ptv(struct ips_driver *ips)
{
	u16 val;

	val = thm_readw(THM_PTV) & PTV_MASK;

	return val;
}

static u16 read_ctv(struct ips_driver *ips, int cpu)
{
	int reg = cpu ? THM_CTV2 : THM_CTV1;
	u16 val;

	val = thm_readw(reg);
	if (!(val & CTV_TEMP_ERROR))
		val = (val) >> 6; /* discard fractional component */
	else
		val = 0;

	return val;
}

static u32 get_cpu_power(struct ips_driver *ips, u32 *last, int period)
{
	u32 val;
	u32 ret;

	/*
	 * CEC is in joules/65535.  Take difference over time to
	 * get watts.
	 */
	val = thm_readl(THM_CEC);

	/* period is in ms and we want mW */
	ret = (((val - *last) * 1000) / period);
	ret = (ret * 1000) / 65535;
	*last = val;

	return 0;
}

static const u16 temp_decay_factor = 2;
static u16 update_average_temp(u16 avg, u16 val)
{
	u16 ret;

	/* Multiply by 100 for extra precision */
	ret = (val * 100 / temp_decay_factor) +
		(((temp_decay_factor - 1) * avg) / temp_decay_factor);
	return ret;
}

static const u16 power_decay_factor = 2;
static u16 update_average_power(u32 avg, u32 val)
{
	u32 ret;

	ret = (val / power_decay_factor) +
		(((power_decay_factor - 1) * avg) / power_decay_factor);

	return ret;
}

static u32 calc_avg_power(struct ips_driver *ips, u32 *array)
{
	u64 total = 0;
	u32 avg;
	int i;

	for (i = 0; i < IPS_SAMPLE_COUNT; i++)
		total += array[i];

	do_div(total, IPS_SAMPLE_COUNT);
	avg = (u32)total;

	return avg;
}

static void monitor_timeout(struct timer_list *t)
{
	struct ips_driver *ips = from_timer(ips, t, timer);
	wake_up_process(ips->monitor);
}

/**
 * ips_monitor - temp/power monitoring thread
 * @data: ips driver structure
 *
 * This is the main function for the IPS driver.  It monitors power and
 * tempurature in the MCP and adjusts CPU and GPU power clams accordingly.
 *
 * We keep a 5s moving average of power consumption and tempurature.  Using
 * that data, along with CPU vs GPU preference, we adjust the power clamps
 * up or down.
 */
static int ips_monitor(void *data)
{
	struct ips_driver *ips = data;
	unsigned long seqno_timestamp, expire, last_msecs, last_sample_period;
	int i;
	u32 *cpu_samples, *mchp_samples, old_cpu_power;
	u16 *mcp_samples, *ctv1_samples, *ctv2_samples, *mch_samples;
	u8 cur_seqno, last_seqno;

	mcp_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u16), GFP_KERNEL);
	ctv1_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u16), GFP_KERNEL);
	ctv2_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u16), GFP_KERNEL);
	mch_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u16), GFP_KERNEL);
	cpu_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u32), GFP_KERNEL);
	mchp_samples = kcalloc(IPS_SAMPLE_COUNT, sizeof(u32), GFP_KERNEL);
	if (!mcp_samples || !ctv1_samples || !ctv2_samples || !mch_samples ||
			!cpu_samples || !mchp_samples) {
		dev_err(ips->dev,
			"failed to allocate sample array, ips disabled\n");
		kfree(mcp_samples);
		kfree(ctv1_samples);
		kfree(ctv2_samples);
		kfree(mch_samples);
		kfree(cpu_samples);
		kfree(mchp_samples);
		return -ENOMEM;
	}

	last_seqno = (thm_readl(THM_ITV) & ITV_ME_SEQNO_MASK) >>
		ITV_ME_SEQNO_SHIFT;
	seqno_timestamp = get_jiffies_64();

	old_cpu_power = thm_readl(THM_CEC);
	schedule_timeout_interruptible(msecs_to_jiffies(IPS_SAMPLE_PERIOD));

	/* Collect an initial average */
	for (i = 0; i < IPS_SAMPLE_COUNT; i++) {
		u32 mchp, cpu_power;
		u16 val;

		mcp_samples[i] = read_ptv(ips);

		val = read_ctv(ips, 0);
		ctv1_samples[i] = val;

		val = read_ctv(ips, 1);
		ctv2_samples[i] = val;

		val = read_mgtv(ips);
		mch_samples[i] = val;

		cpu_power = get_cpu_power(ips, &old_cpu_power,
					  IPS_SAMPLE_PERIOD);
		cpu_samples[i] = cpu_power;

		if (ips->read_mch_val) {
			mchp = ips->read_mch_val();
			mchp_samples[i] = mchp;
		}

		schedule_timeout_interruptible(msecs_to_jiffies(IPS_SAMPLE_PERIOD));
		if (kthread_should_stop())
			break;
	}

	ips->mcp_avg_temp = calc_avg_temp(ips, mcp_samples);
	ips->ctv1_avg_temp = calc_avg_temp(ips, ctv1_samples);
	ips->ctv2_avg_temp = calc_avg_temp(ips, ctv2_samples);
	ips->mch_avg_temp = calc_avg_temp(ips, mch_samples);
	ips->cpu_avg_power = calc_avg_power(ips, cpu_samples);
	ips->mch_avg_power = calc_avg_power(ips, mchp_samples);
	kfree(mcp_samples);
	kfree(ctv1_samples);
	kfree(ctv2_samples);
	kfree(mch_samples);
	kfree(cpu_samples);
	kfree(mchp_samples);

	/* Start the adjustment thread now that we have data */
	wake_up_process(ips->adjust);

	/*
	 * Ok, now we have an initial avg.  From here on out, we track the
	 * running avg using a decaying average calculation.  This allows
	 * us to reduce the sample frequency if the CPU and GPU are idle.
	 */
	old_cpu_power = thm_readl(THM_CEC);
	schedule_timeout_interruptible(msecs_to_jiffies(IPS_SAMPLE_PERIOD));
	last_sample_period = IPS_SAMPLE_PERIOD;

	timer_setup(&ips->timer, monitor_timeout, TIMER_DEFERRABLE);
	do {
		u32 cpu_val, mch_val;
		u16 val;

		/* MCP itself */
		val = read_ptv(ips);
		ips->mcp_avg_temp = update_average_temp(ips->mcp_avg_temp, val);

		/* Processor 0 */
		val = read_ctv(ips, 0);
		ips->ctv1_avg_temp =
			update_average_temp(ips->ctv1_avg_temp, val);
		/* Power */
		cpu_val = get_cpu_power(ips, &old_cpu_power,
					last_sample_period);
		ips->cpu_avg_power =
			update_average_power(ips->cpu_avg_power, cpu_val);

		if (ips->second_cpu) {
			/* Processor 1 */
			val = read_ctv(ips, 1);
			ips->ctv2_avg_temp =
				update_average_temp(ips->ctv2_avg_temp, val);
		}

		/* MCH */
		val = read_mgtv(ips);
		ips->mch_avg_temp = update_average_temp(ips->mch_avg_temp, val);
		/* Power */
		if (ips->read_mch_val) {
			mch_val = ips->read_mch_val();
			ips->mch_avg_power =
				update_average_power(ips->mch_avg_power,
						     mch_val);
		}

		/*
		 * Make sure ME is updating thermal regs.
		 * Note:
		 * If it's been more than a second since the last update,
		 * the ME is probably hung.
		 */
		cur_seqno = (thm_readl(THM_ITV) & ITV_ME_SEQNO_MASK) >>
			ITV_ME_SEQNO_SHIFT;
		if (cur_seqno == last_seqno &&
		    time_after(jiffies, seqno_timestamp + HZ)) {
			dev_warn(ips->dev,
				 "ME failed to update for more than 1s, likely hung\n");
		} else {
			seqno_timestamp = get_jiffies_64();
			last_seqno = cur_seqno;
		}

		last_msecs = jiffies_to_msecs(jiffies);
		expire = jiffies + msecs_to_jiffies(IPS_SAMPLE_PERIOD);

		__set_current_state(TASK_INTERRUPTIBLE);
		mod_timer(&ips->timer, expire);
		schedule();

		/* Calculate actual sample period for power averaging */
		last_sample_period = jiffies_to_msecs(jiffies) - last_msecs;
		if (!last_sample_period)
			last_sample_period = 1;
	} while (!kthread_should_stop());

	del_timer_sync(&ips->timer);

	dev_dbg(ips->dev, "ips-monitor thread stopped\n");

	return 0;
}

#if 0
#define THM_DUMPW(reg) \
	{ \
	u16 val = thm_readw(reg); \
	dev_dbg(ips->dev, #reg ": 0x%04x\n", val); \
	}
#define THM_DUMPL(reg) \
	{ \
	u32 val = thm_readl(reg); \
	dev_dbg(ips->dev, #reg ": 0x%08x\n", val); \
	}
#define THM_DUMPQ(reg) \
	{ \
	u64 val = thm_readq(reg); \
	dev_dbg(ips->dev, #reg ": 0x%016x\n", val); \
	}

static void dump_thermal_info(struct ips_driver *ips)
{
	u16 ptl;

	ptl = thm_readw(THM_PTL);
	dev_dbg(ips->dev, "Processor temp limit: %d\n", ptl);

	THM_DUMPW(THM_CTA);
	THM_DUMPW(THM_TRC);
	THM_DUMPW(THM_CTV1);
	THM_DUMPL(THM_STS);
	THM_DUMPW(THM_PTV);
	THM_DUMPQ(THM_MGTV);
}
#endif

/**
 * ips_irq_handler - handle temperature triggers and other IPS events
 * @irq: irq number
 * @arg: unused
 *
 * Handle temperature limit trigger events, generally by lowering the clamps.
 * If we're at a critical limit, we clamp back to the lowest possible value
 * to prevent emergency shutdown.
 */
static irqreturn_t ips_irq_handler(int irq, void *arg)
{
	struct ips_driver *ips = arg;
	u8 tses = thm_readb(THM_TSES);
	u8 tes = thm_readb(THM_TES);

	if (!tses && !tes)
		return IRQ_NONE;

	dev_info(ips->dev, "TSES: 0x%02x\n", tses);
	dev_info(ips->dev, "TES: 0x%02x\n", tes);

	/* STS update from EC? */
	if (tes & 1) {
		u32 sts, tc1;

		sts = thm_readl(THM_STS);
		tc1 = thm_readl(THM_TC1);

		if (sts & STS_NVV) {
			spin_lock(&ips->turbo_status_lock);
			ips->core_power_limit = (sts & STS_PCPL_MASK) >>
				STS_PCPL_SHIFT;
			ips->mch_power_limit = (sts & STS_GPL_MASK) >>
				STS_GPL_SHIFT;
			/* ignore EC CPU vs GPU pref */
			ips->cpu_turbo_enabled = !(sts & STS_PCTD_DIS);
			/* 
			 * Disable turbo for now, until we can figure
			 * out why the power figures are wrong
			 */
			ips->cpu_turbo_enabled = false;
			if (ips->gpu_busy)
				ips->gpu_turbo_enabled = !(sts & STS_GTD_DIS);
			ips->mcp_temp_limit = (sts & STS_PTL_MASK) >>
				STS_PTL_SHIFT;
			ips->mcp_power_limit = (tc1 & STS_PPL_MASK) >>
				STS_PPL_SHIFT;
			verify_limits(ips);
			spin_unlock(&ips->turbo_status_lock);

			thm_writeb(THM_SEC, SEC_ACK);
		}
		thm_writeb(THM_TES, tes);
	}

	/* Thermal trip */
	if (tses) {
		dev_warn(ips->dev, "thermal trip occurred, tses: 0x%04x\n",
			 tses);
		thm_writeb(THM_TSES, tses);
	}

	return IRQ_HANDLED;
}

#ifndef CONFIG_DEBUG_FS
static void ips_debugfs_init(struct ips_driver *ips) { return; }
static void ips_debugfs_cleanup(struct ips_driver *ips) { return; }
#else

/* Expose current state and limits in debugfs if possible */

struct ips_debugfs_node {
	struct ips_driver *ips;
	char *name;
	int (*show)(struct seq_file *m, void *data);
};

static int show_cpu_temp(struct seq_file *m, void *data)
{
	struct ips_driver *ips = m->private;

	seq_printf(m, "%d.%02d\n", ips->ctv1_avg_temp / 100,
		   ips->ctv1_avg_temp % 100);

	return 0;
}

static int show_cpu_power(struct seq_file *m, void *data)
{
	struct ips_driver *ips = m->private;

	seq_printf(m, "%dmW\n", ips->cpu_avg_power);

	return 0;
}

static int show_cpu_clamp(struct seq_file *m, void *data)
{
	u64 turbo_override;
	int tdp, tdc;

	rdmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);

	tdp = (int)(turbo_override & TURBO_TDP_MASK);
	tdc = (int)((turbo_override & TURBO_TDC_MASK) >> TURBO_TDC_SHIFT);

	/* Convert to .1W/A units */
	tdp = tdp * 10 / 8;
	tdc = tdc * 10 / 8;

	/* Watts Amperes */
	seq_printf(m, "%d.%dW %d.%dA\n", tdp / 10, tdp % 10,
		   tdc / 10, tdc % 10);

	return 0;
}

static int show_mch_temp(struct seq_file *m, void *data)
{
	struct ips_driver *ips = m->private;

	seq_printf(m, "%d.%02d\n", ips->mch_avg_temp / 100,
		   ips->mch_avg_temp % 100);

	return 0;
}

static int show_mch_power(struct seq_file *m, void *data)
{
	struct ips_driver *ips = m->private;

	seq_printf(m, "%dmW\n", ips->mch_avg_power);

	return 0;
}

static struct ips_debugfs_node ips_debug_files[] = {
	{ NULL, "cpu_temp", show_cpu_temp },
	{ NULL, "cpu_power", show_cpu_power },
	{ NULL, "cpu_clamp", show_cpu_clamp },
	{ NULL, "mch_temp", show_mch_temp },
	{ NULL, "mch_power", show_mch_power },
};

static int ips_debugfs_open(struct inode *inode, struct file *file)
{
	struct ips_debugfs_node *node = inode->i_private;

	return single_open(file, node->show, node->ips);
}

static const struct file_operations ips_debugfs_ops = {
	.owner = THIS_MODULE,
	.open = ips_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void ips_debugfs_cleanup(struct ips_driver *ips)
{
	if (ips->debug_root)
		debugfs_remove_recursive(ips->debug_root);
	return;
}

static void ips_debugfs_init(struct ips_driver *ips)
{
	int i;

	ips->debug_root = debugfs_create_dir("ips", NULL);
	if (!ips->debug_root) {
		dev_err(ips->dev, "failed to create debugfs entries: %ld\n",
			PTR_ERR(ips->debug_root));
		return;
	}

	for (i = 0; i < ARRAY_SIZE(ips_debug_files); i++) {
		struct dentry *ent;
		struct ips_debugfs_node *node = &ips_debug_files[i];

		node->ips = ips;
		ent = debugfs_create_file(node->name, S_IFREG | S_IRUGO,
					  ips->debug_root, node,
					  &ips_debugfs_ops);
		if (!ent) {
			dev_err(ips->dev, "failed to create debug file: %ld\n",
				PTR_ERR(ent));
			goto err_cleanup;
		}
	}

	return;

err_cleanup:
	ips_debugfs_cleanup(ips);
	return;
}
#endif /* CONFIG_DEBUG_FS */

/**
 * ips_detect_cpu - detect whether CPU supports IPS
 *
 * Walk our list and see if we're on a supported CPU.  If we find one,
 * return the limits for it.
 */
static struct ips_mcp_limits *ips_detect_cpu(struct ips_driver *ips)
{
	u64 turbo_power, misc_en;
	struct ips_mcp_limits *limits = NULL;
	u16 tdp;

	if (!(boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model == 37)) {
		dev_info(ips->dev, "Non-IPS CPU detected.\n");
		return NULL;
	}

	rdmsrl(IA32_MISC_ENABLE, misc_en);
	/*
	 * If the turbo enable bit isn't set, we shouldn't try to enable/disable
	 * turbo manually or we'll get an illegal MSR access, even though
	 * turbo will still be available.
	 */
	if (misc_en & IA32_MISC_TURBO_EN)
		ips->turbo_toggle_allowed = true;
	else
		ips->turbo_toggle_allowed = false;

	if (strstr(boot_cpu_data.x86_model_id, "CPU       M"))
		limits = &ips_sv_limits;
	else if (strstr(boot_cpu_data.x86_model_id, "CPU       L"))
		limits = &ips_lv_limits;
	else if (strstr(boot_cpu_data.x86_model_id, "CPU       U"))
		limits = &ips_ulv_limits;
	else {
		dev_info(ips->dev, "No CPUID match found.\n");
		return NULL;
	}

	rdmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_power);
	tdp = turbo_power & TURBO_TDP_MASK;

	/* Sanity check TDP against CPU */
	if (limits->core_power_limit != (tdp / 8) * 1000) {
		dev_info(ips->dev,
			 "CPU TDP doesn't match expected value (found %d, expected %d)\n",
			 tdp / 8, limits->core_power_limit / 1000);
		limits->core_power_limit = (tdp / 8) * 1000;
	}

	return limits;
}

/**
 * ips_get_i915_syms - try to get GPU control methods from i915 driver
 * @ips: IPS driver
 *
 * The i915 driver exports several interfaces to allow the IPS driver to
 * monitor and control graphics turbo mode.  If we can find them, we can
 * enable graphics turbo, otherwise we must disable it to avoid exceeding
 * thermal and power limits in the MCP.
 */
static bool ips_get_i915_syms(struct ips_driver *ips)
{
	ips->read_mch_val = symbol_get(i915_read_mch_val);
	if (!ips->read_mch_val)
		goto out_err;
	ips->gpu_raise = symbol_get(i915_gpu_raise);
	if (!ips->gpu_raise)
		goto out_put_mch;
	ips->gpu_lower = symbol_get(i915_gpu_lower);
	if (!ips->gpu_lower)
		goto out_put_raise;
	ips->gpu_busy = symbol_get(i915_gpu_busy);
	if (!ips->gpu_busy)
		goto out_put_lower;
	ips->gpu_turbo_disable = symbol_get(i915_gpu_turbo_disable);
	if (!ips->gpu_turbo_disable)
		goto out_put_busy;

	return true;

out_put_busy:
	symbol_put(i915_gpu_busy);
out_put_lower:
	symbol_put(i915_gpu_lower);
out_put_raise:
	symbol_put(i915_gpu_raise);
out_put_mch:
	symbol_put(i915_read_mch_val);
out_err:
	return false;
}

static bool
ips_gpu_turbo_enabled(struct ips_driver *ips)
{
	if (!ips->gpu_busy && late_i915_load) {
		if (ips_get_i915_syms(ips)) {
			dev_info(ips->dev,
				 "i915 driver attached, reenabling gpu turbo\n");
			ips->gpu_turbo_enabled = !(thm_readl(THM_HTS) & HTS_GTD_DIS);
		}
	}

	return ips->gpu_turbo_enabled;
}

void
ips_link_to_i915_driver(void)
{
	/* We can't cleanly get at the various ips_driver structs from
	 * this caller (the i915 driver), so just set a flag saying
	 * that it's time to try getting the symbols again.
	 */
	late_i915_load = true;
}
EXPORT_SYMBOL_GPL(ips_link_to_i915_driver);

static const struct pci_device_id ips_id_table[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_THERMAL_SENSOR), },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, ips_id_table);

static int ips_blacklist_callback(const struct dmi_system_id *id)
{
	pr_info("Blacklisted intel_ips for %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id ips_blacklist[] = {
	{
		.callback = ips_blacklist_callback,
		.ident = "HP ProBook",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook"),
		},
	},
	{ }	/* terminating entry */
};

static int ips_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	u64 platform_info;
	struct ips_driver *ips;
	u32 hts;
	int ret = 0;
	u16 htshi, trc, trc_required_mask;
	u8 tse;

	if (dmi_check_system(ips_blacklist))
		return -ENODEV;

	ips = devm_kzalloc(&dev->dev, sizeof(*ips), GFP_KERNEL);
	if (!ips)
		return -ENOMEM;

	spin_lock_init(&ips->turbo_status_lock);
	ips->dev = &dev->dev;

	ips->limits = ips_detect_cpu(ips);
	if (!ips->limits) {
		dev_info(&dev->dev, "IPS not supported on this CPU\n");
		return -ENXIO;
	}

	ret = pcim_enable_device(dev);
	if (ret) {
		dev_err(&dev->dev, "can't enable PCI device, aborting\n");
		return ret;
	}

	ret = pcim_iomap_regions(dev, 1 << 0, pci_name(dev));
	if (ret) {
		dev_err(&dev->dev, "failed to map thermal regs, aborting\n");
		return ret;
	}
	ips->regmap = pcim_iomap_table(dev)[0];

	pci_set_drvdata(dev, ips);

	tse = thm_readb(THM_TSE);
	if (tse != TSE_EN) {
		dev_err(&dev->dev, "thermal device not enabled (0x%02x), aborting\n", tse);
		return -ENXIO;
	}

	trc = thm_readw(THM_TRC);
	trc_required_mask = TRC_CORE1_EN | TRC_CORE_PWR | TRC_MCH_EN;
	if ((trc & trc_required_mask) != trc_required_mask) {
		dev_err(&dev->dev, "thermal reporting for required devices not enabled, aborting\n");
		return -ENXIO;
	}

	if (trc & TRC_CORE2_EN)
		ips->second_cpu = true;

	update_turbo_limits(ips);
	dev_dbg(&dev->dev, "max cpu power clamp: %dW\n",
		ips->mcp_power_limit / 10);
	dev_dbg(&dev->dev, "max core power clamp: %dW\n",
		ips->core_power_limit / 10);
	/* BIOS may update limits at runtime */
	if (thm_readl(THM_PSC) & PSP_PBRT)
		ips->poll_turbo_status = true;

	if (!ips_get_i915_syms(ips)) {
		dev_info(&dev->dev, "failed to get i915 symbols, graphics turbo disabled until i915 loads\n");
		ips->gpu_turbo_enabled = false;
	} else {
		dev_dbg(&dev->dev, "graphics turbo enabled\n");
		ips->gpu_turbo_enabled = true;
	}

	/*
	 * Check PLATFORM_INFO MSR to make sure this chip is
	 * turbo capable.
	 */
	rdmsrl(PLATFORM_INFO, platform_info);
	if (!(platform_info & PLATFORM_TDP)) {
		dev_err(&dev->dev, "platform indicates TDP override unavailable, aborting\n");
		return -ENODEV;
	}

	/*
	 * IRQ handler for ME interaction
	 * Note: don't use MSI here as the PCH has bugs.
	 */
	ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_LEGACY);
	if (ret < 0)
		return ret;

	ips->irq = pci_irq_vector(dev, 0);

	ret = request_irq(ips->irq, ips_irq_handler, IRQF_SHARED, "ips", ips);
	if (ret) {
		dev_err(&dev->dev, "request irq failed, aborting\n");
		return ret;
	}

	/* Enable aux, hot & critical interrupts */
	thm_writeb(THM_TSPIEN, TSPIEN_AUX2_LOHI | TSPIEN_CRIT_LOHI |
		   TSPIEN_HOT_LOHI | TSPIEN_AUX_LOHI);
	thm_writeb(THM_TEN, TEN_UPDATE_EN);

	/* Collect adjustment values */
	ips->cta_val = thm_readw(THM_CTA);
	ips->pta_val = thm_readw(THM_PTA);
	ips->mgta_val = thm_readw(THM_MGTA);

	/* Save turbo limits & ratios */
	rdmsrl(TURBO_POWER_CURRENT_LIMIT, ips->orig_turbo_limit);

	ips_disable_cpu_turbo(ips);
	ips->cpu_turbo_enabled = false;

	/* Create thermal adjust thread */
	ips->adjust = kthread_create(ips_adjust, ips, "ips-adjust");
	if (IS_ERR(ips->adjust)) {
		dev_err(&dev->dev,
			"failed to create thermal adjust thread, aborting\n");
		ret = -ENOMEM;
		goto error_free_irq;

	}

	/*
	 * Set up the work queue and monitor thread. The monitor thread
	 * will wake up ips_adjust thread.
	 */
	ips->monitor = kthread_run(ips_monitor, ips, "ips-monitor");
	if (IS_ERR(ips->monitor)) {
		dev_err(&dev->dev,
			"failed to create thermal monitor thread, aborting\n");
		ret = -ENOMEM;
		goto error_thread_cleanup;
	}

	hts = (ips->core_power_limit << HTS_PCPL_SHIFT) |
		(ips->mcp_temp_limit << HTS_PTL_SHIFT) | HTS_NVV;
	htshi = HTS2_PRST_RUNNING << HTS2_PRST_SHIFT;

	thm_writew(THM_HTSHI, htshi);
	thm_writel(THM_HTS, hts);

	ips_debugfs_init(ips);

	dev_info(&dev->dev, "IPS driver initialized, MCP temp limit %d\n",
		 ips->mcp_temp_limit);
	return ret;

error_thread_cleanup:
	kthread_stop(ips->adjust);
error_free_irq:
	free_irq(ips->irq, ips);
	pci_free_irq_vectors(dev);
	return ret;
}

static void ips_remove(struct pci_dev *dev)
{
	struct ips_driver *ips = pci_get_drvdata(dev);
	u64 turbo_override;

	if (!ips)
		return;

	ips_debugfs_cleanup(ips);

	/* Release i915 driver */
	if (ips->read_mch_val)
		symbol_put(i915_read_mch_val);
	if (ips->gpu_raise)
		symbol_put(i915_gpu_raise);
	if (ips->gpu_lower)
		symbol_put(i915_gpu_lower);
	if (ips->gpu_busy)
		symbol_put(i915_gpu_busy);
	if (ips->gpu_turbo_disable)
		symbol_put(i915_gpu_turbo_disable);

	rdmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);
	turbo_override &= ~(TURBO_TDC_OVR_EN | TURBO_TDP_OVR_EN);
	wrmsrl(TURBO_POWER_CURRENT_LIMIT, turbo_override);
	wrmsrl(TURBO_POWER_CURRENT_LIMIT, ips->orig_turbo_limit);

	free_irq(ips->irq, ips);
	pci_free_irq_vectors(dev);
	if (ips->adjust)
		kthread_stop(ips->adjust);
	if (ips->monitor)
		kthread_stop(ips->monitor);
	dev_dbg(&dev->dev, "IPS driver removed\n");
}

static struct pci_driver ips_pci_driver = {
	.name = "intel ips",
	.id_table = ips_id_table,
	.probe = ips_probe,
	.remove = ips_remove,
};

module_pci_driver(ips_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesse Barnes <jbarnes@virtuousgeek.org>");
MODULE_DESCRIPTION("Intelligent Power Sharing Driver");
