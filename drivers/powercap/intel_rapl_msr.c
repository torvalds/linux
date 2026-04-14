// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Running Average Power Limit (RAPL) Driver via MSR interface
 * Copyright (c) 2019, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/cpu.h>
#include <linux/powercap.h>
#include <linux/suspend.h>
#include <linux/intel_rapl.h>
#include <linux/processor.h>
#include <linux/platform_device.h>
#include <linux/units.h>
#include <linux/bits.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/iosf_mbi.h>
#include <asm/msr.h>

/* Local defines */
#define MSR_PLATFORM_POWER_LIMIT	0x0000065C
#define MSR_VR_CURRENT_CONFIG		0x00000601

#define ENERGY_UNIT_SCALE		1000	/* scale from driver unit to powercap unit */

#define POWER_UNIT_OFFSET		0x00
#define POWER_UNIT_MASK			GENMASK(3, 0)

#define ENERGY_UNIT_OFFSET		0x08
#define ENERGY_UNIT_MASK		GENMASK(12, 8)

#define TIME_UNIT_OFFSET		0x10
#define TIME_UNIT_MASK			GENMASK(19, 16)

/* bitmasks for RAPL MSRs, used by primitive access functions */
#define ENERGY_STATUS_MASK		GENMASK(31, 0)

#define POWER_LIMIT1_MASK		GENMASK(14, 0)
#define POWER_LIMIT1_ENABLE		BIT(15)
#define POWER_LIMIT1_CLAMP		BIT(16)

#define POWER_LIMIT2_MASK		GENMASK_ULL(46, 32)
#define POWER_LIMIT2_ENABLE		BIT_ULL(47)
#define POWER_LIMIT2_CLAMP		BIT_ULL(48)
#define POWER_HIGH_LOCK			BIT_ULL(63)
#define POWER_LOW_LOCK			BIT(31)

#define POWER_LIMIT4_MASK		GENMASK(12, 0)

#define TIME_WINDOW1_MASK		GENMASK_ULL(23, 17)
#define TIME_WINDOW2_MASK		GENMASK_ULL(55, 49)

#define POWER_INFO_MAX_MASK		GENMASK_ULL(46, 32)
#define POWER_INFO_MIN_MASK		GENMASK_ULL(30, 16)
#define POWER_INFO_MAX_TIME_WIN_MASK	GENMASK_ULL(53, 48)
#define POWER_INFO_THERMAL_SPEC_MASK	GENMASK(14, 0)

#define PERF_STATUS_THROTTLE_TIME_MASK	GENMASK(31, 0)
#define PP_POLICY_MASK			GENMASK(4, 0)

/*
 * SPR has different layout for Psys Domain PowerLimit registers.
 * There are 17 bits of PL1 and PL2 instead of 15 bits.
 * The Enable bits and TimeWindow bits are also shifted as a result.
 */
#define PSYS_POWER_LIMIT1_MASK		GENMASK_ULL(16, 0)
#define PSYS_POWER_LIMIT1_ENABLE	BIT(17)

#define PSYS_POWER_LIMIT2_MASK		GENMASK_ULL(48, 32)
#define PSYS_POWER_LIMIT2_ENABLE	BIT_ULL(49)

#define PSYS_TIME_WINDOW1_MASK		GENMASK_ULL(25, 19)
#define PSYS_TIME_WINDOW2_MASK		GENMASK_ULL(57, 51)

/* Sideband MBI registers */
#define IOSF_CPU_POWER_BUDGET_CTL_BYT	0x02
#define IOSF_CPU_POWER_BUDGET_CTL_TNG	0xDF

/* private data for RAPL MSR Interface */
static struct rapl_if_priv *rapl_msr_priv;

static bool rapl_msr_pmu __ro_after_init;

static struct rapl_if_priv rapl_msr_priv_intel = {
	.type = RAPL_IF_MSR,
	.reg_unit.msr = MSR_RAPL_POWER_UNIT,
	.regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_LIMIT].msr	= MSR_PKG_POWER_LIMIT,
	.regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_STATUS].msr	= MSR_PKG_ENERGY_STATUS,
	.regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_PERF].msr	= MSR_PKG_PERF_STATUS,
	.regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_INFO].msr	= MSR_PKG_POWER_INFO,
	.regs[RAPL_DOMAIN_PP0][RAPL_DOMAIN_REG_LIMIT].msr	= MSR_PP0_POWER_LIMIT,
	.regs[RAPL_DOMAIN_PP0][RAPL_DOMAIN_REG_STATUS].msr	= MSR_PP0_ENERGY_STATUS,
	.regs[RAPL_DOMAIN_PP0][RAPL_DOMAIN_REG_POLICY].msr	= MSR_PP0_POLICY,
	.regs[RAPL_DOMAIN_PP1][RAPL_DOMAIN_REG_LIMIT].msr	= MSR_PP1_POWER_LIMIT,
	.regs[RAPL_DOMAIN_PP1][RAPL_DOMAIN_REG_STATUS].msr	= MSR_PP1_ENERGY_STATUS,
	.regs[RAPL_DOMAIN_PP1][RAPL_DOMAIN_REG_POLICY].msr	= MSR_PP1_POLICY,
	.regs[RAPL_DOMAIN_DRAM][RAPL_DOMAIN_REG_LIMIT].msr	= MSR_DRAM_POWER_LIMIT,
	.regs[RAPL_DOMAIN_DRAM][RAPL_DOMAIN_REG_STATUS].msr	= MSR_DRAM_ENERGY_STATUS,
	.regs[RAPL_DOMAIN_DRAM][RAPL_DOMAIN_REG_PERF].msr	= MSR_DRAM_PERF_STATUS,
	.regs[RAPL_DOMAIN_DRAM][RAPL_DOMAIN_REG_INFO].msr	= MSR_DRAM_POWER_INFO,
	.regs[RAPL_DOMAIN_PLATFORM][RAPL_DOMAIN_REG_LIMIT].msr	= MSR_PLATFORM_POWER_LIMIT,
	.regs[RAPL_DOMAIN_PLATFORM][RAPL_DOMAIN_REG_STATUS].msr	= MSR_PLATFORM_ENERGY_STATUS,
	.limits[RAPL_DOMAIN_PACKAGE] = BIT(POWER_LIMIT2),
	.limits[RAPL_DOMAIN_PLATFORM] = BIT(POWER_LIMIT2),
};

static struct rapl_if_priv rapl_msr_priv_amd = {
	.type = RAPL_IF_MSR,
	.reg_unit.msr = MSR_AMD_RAPL_POWER_UNIT,
	.regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_STATUS].msr	= MSR_AMD_PKG_ENERGY_STATUS,
	.regs[RAPL_DOMAIN_PP0][RAPL_DOMAIN_REG_STATUS].msr	= MSR_AMD_CORE_ENERGY_STATUS,
};

/* Handles CPU hotplug on multi-socket systems.
 * If a CPU goes online as the first CPU of the physical package
 * we add the RAPL package to the system. Similarly, when the last
 * CPU of the package is removed, we remove the RAPL package and its
 * associated domains. Cooling devices are handled accordingly at
 * per-domain level.
 */
static int rapl_cpu_online(unsigned int cpu)
{
	struct rapl_package *rp;

	rp = rapl_find_package_domain_cpuslocked(cpu, rapl_msr_priv, true);
	if (!rp) {
		rp = rapl_add_package_cpuslocked(cpu, rapl_msr_priv, true);
		if (IS_ERR(rp))
			return PTR_ERR(rp);
		if (rapl_msr_pmu)
			rapl_package_add_pmu_locked(rp);
	}
	cpumask_set_cpu(cpu, &rp->cpumask);
	return 0;
}

static int rapl_cpu_down_prep(unsigned int cpu)
{
	struct rapl_package *rp;
	int lead_cpu;

	rp = rapl_find_package_domain_cpuslocked(cpu, rapl_msr_priv, true);
	if (!rp)
		return 0;

	cpumask_clear_cpu(cpu, &rp->cpumask);
	lead_cpu = cpumask_first(&rp->cpumask);
	if (lead_cpu >= nr_cpu_ids) {
		if (rapl_msr_pmu)
			rapl_package_remove_pmu_locked(rp);
		rapl_remove_package_cpuslocked(rp);
	} else if (rp->lead_cpu == cpu) {
		rp->lead_cpu = lead_cpu;
	}

	return 0;
}

static int rapl_msr_read_raw(int cpu, struct reg_action *ra, bool pmu_ctx)
{
	/*
	 * When called from PMU context, perform MSR read directly using
	 * rdmsrq() without IPI overhead. Package-scoped MSRs are readable
	 * from any CPU in the package.
	 */
	if (pmu_ctx) {
		rdmsrq(ra->reg.msr, ra->value);
		goto out;
	}

	if (rdmsrq_safe_on_cpu(cpu, ra->reg.msr, &ra->value)) {
		pr_debug("failed to read msr 0x%x on cpu %d\n", ra->reg.msr, cpu);
		return -EIO;
	}

out:
	ra->value &= ra->mask;
	return 0;
}

static void rapl_msr_update_func(void *info)
{
	struct reg_action *ra = info;
	u64 val;

	ra->err = rdmsrq_safe(ra->reg.msr, &val);
	if (ra->err)
		return;

	val &= ~ra->mask;
	val |= ra->value;

	ra->err = wrmsrq_safe(ra->reg.msr, val);
}

static int rapl_msr_write_raw(int cpu, struct reg_action *ra)
{
	int ret;

	ret = smp_call_function_single(cpu, rapl_msr_update_func, ra, 1);
	if (WARN_ON_ONCE(ret))
		return ret;

	return ra->err;
}

static int rapl_check_unit_atom(struct rapl_domain *rd)
{
	struct reg_action ra;
	u32 value;

	ra.reg = rd->regs[RAPL_DOMAIN_REG_UNIT];
	ra.mask = ~0;
	if (rapl_msr_read_raw(rd->rp->lead_cpu, &ra, false)) {
		pr_err("Failed to read power unit REG 0x%llx on %s:%s, exit.\n",
			ra.reg.val, rd->rp->name, rd->name);
		return -ENODEV;
	}

	value = (ra.value & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rd->energy_unit = ENERGY_UNIT_SCALE * (1ULL << value);

	value = (ra.value & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rd->power_unit = (1ULL << value) * MILLIWATT_PER_WATT;

	value = (ra.value & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rd->time_unit = USEC_PER_SEC >> value;

	pr_debug("Atom %s:%s energy=%dpJ, time=%dus, power=%duW\n",
		 rd->rp->name, rd->name, rd->energy_unit, rd->time_unit, rd->power_unit);

	return 0;
}

static void set_floor_freq_atom(struct rapl_domain *rd, bool enable)
{
	static u32 power_ctrl_orig_val;
	const struct rapl_defaults *defaults = rd->rp->priv->defaults;
	u32 mdata;

	if (!defaults->floor_freq_reg_addr) {
		pr_err("Invalid floor frequency config register\n");
		return;
	}

	if (!power_ctrl_orig_val)
		iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_CR_READ,
			      defaults->floor_freq_reg_addr,
			      &power_ctrl_orig_val);
	mdata = power_ctrl_orig_val;
	if (enable) {
		mdata &= ~GENMASK(14, 8);
		mdata |= BIT(8);
	}
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_CR_WRITE,
		       defaults->floor_freq_reg_addr, mdata);
}

static u64 rapl_compute_time_window_atom(struct rapl_domain *rd, u64 value,
					 bool to_raw)
{
	if (to_raw)
		return div64_u64(value, rd->time_unit);

	/*
	 * Atom time unit encoding is straight forward val * time_unit,
	 * where time_unit is default to 1 sec. Never 0.
	 */
	return value ? value * rd->time_unit : rd->time_unit;
}

/* RAPL primitives for MSR I/F */
static struct rapl_primitive_info rpi_msr[NR_RAPL_PRIMITIVES] = {
	/* name, mask, shift, msr index, unit divisor */
	[POWER_LIMIT1]		= PRIMITIVE_INFO_INIT(POWER_LIMIT1, POWER_LIMIT1_MASK, 0,
						      RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT2]		= PRIMITIVE_INFO_INIT(POWER_LIMIT2, POWER_LIMIT2_MASK, 32,
						      RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT4]		= PRIMITIVE_INFO_INIT(POWER_LIMIT4, POWER_LIMIT4_MASK, 0,
						      RAPL_DOMAIN_REG_PL4, POWER_UNIT, 0),
	[ENERGY_COUNTER]	= PRIMITIVE_INFO_INIT(ENERGY_COUNTER, ENERGY_STATUS_MASK, 0,
						      RAPL_DOMAIN_REG_STATUS, ENERGY_UNIT, 0),
	[FW_LOCK]		= PRIMITIVE_INFO_INIT(FW_LOCK, POWER_LOW_LOCK, 31,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[FW_HIGH_LOCK]		= PRIMITIVE_INFO_INIT(FW_LOCK, POWER_HIGH_LOCK, 63,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_ENABLE]		= PRIMITIVE_INFO_INIT(PL1_ENABLE, POWER_LIMIT1_ENABLE, 15,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_CLAMP]		= PRIMITIVE_INFO_INIT(PL1_CLAMP, POWER_LIMIT1_CLAMP, 16,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_ENABLE]		= PRIMITIVE_INFO_INIT(PL2_ENABLE, POWER_LIMIT2_ENABLE, 47,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_CLAMP]		= PRIMITIVE_INFO_INIT(PL2_CLAMP, POWER_LIMIT2_CLAMP, 48,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[TIME_WINDOW1]		= PRIMITIVE_INFO_INIT(TIME_WINDOW1, TIME_WINDOW1_MASK, 17,
						      RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[TIME_WINDOW2]		= PRIMITIVE_INFO_INIT(TIME_WINDOW2, TIME_WINDOW2_MASK, 49,
						      RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[THERMAL_SPEC_POWER]	= PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER,
						      POWER_INFO_THERMAL_SPEC_MASK, 0,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_POWER]		= PRIMITIVE_INFO_INIT(MAX_POWER, POWER_INFO_MAX_MASK, 32,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MIN_POWER]		= PRIMITIVE_INFO_INIT(MIN_POWER, POWER_INFO_MIN_MASK, 16,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_TIME_WINDOW]	= PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW,
						      POWER_INFO_MAX_TIME_WIN_MASK, 48,
						      RAPL_DOMAIN_REG_INFO, TIME_UNIT, 0),
	[THROTTLED_TIME]	= PRIMITIVE_INFO_INIT(THROTTLED_TIME,
						      PERF_STATUS_THROTTLE_TIME_MASK, 0,
						      RAPL_DOMAIN_REG_PERF, TIME_UNIT, 0),
	[PRIORITY_LEVEL]	= PRIMITIVE_INFO_INIT(PRIORITY_LEVEL, PP_POLICY_MASK, 0,
						      RAPL_DOMAIN_REG_POLICY, ARBITRARY_UNIT, 0),
	[PSYS_POWER_LIMIT1]	= PRIMITIVE_INFO_INIT(PSYS_POWER_LIMIT1, PSYS_POWER_LIMIT1_MASK, 0,
						      RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[PSYS_POWER_LIMIT2]	= PRIMITIVE_INFO_INIT(PSYS_POWER_LIMIT2, PSYS_POWER_LIMIT2_MASK,
						      32, RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[PSYS_PL1_ENABLE]	= PRIMITIVE_INFO_INIT(PSYS_PL1_ENABLE, PSYS_POWER_LIMIT1_ENABLE,
						      17, RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT,
						      0),
	[PSYS_PL2_ENABLE]	= PRIMITIVE_INFO_INIT(PSYS_PL2_ENABLE, PSYS_POWER_LIMIT2_ENABLE,
						      49, RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT,
						      0),
	[PSYS_TIME_WINDOW1]	= PRIMITIVE_INFO_INIT(PSYS_TIME_WINDOW1, PSYS_TIME_WINDOW1_MASK,
						      19, RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[PSYS_TIME_WINDOW2]	= PRIMITIVE_INFO_INIT(PSYS_TIME_WINDOW2, PSYS_TIME_WINDOW2_MASK,
						      51, RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
};

static const struct rapl_defaults rapl_defaults_core = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
};

static const struct rapl_defaults rapl_defaults_hsw_server = {
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
	.dram_domain_energy_unit = 15300,
};

static const struct rapl_defaults rapl_defaults_spr_server = {
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
	.psys_domain_energy_unit = NANOJOULE_PER_JOULE,
	.spr_psys_bits = true,
};

static const struct rapl_defaults rapl_defaults_byt = {
	.floor_freq_reg_addr = IOSF_CPU_POWER_BUDGET_CTL_BYT,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = set_floor_freq_atom,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_tng = {
	.floor_freq_reg_addr = IOSF_CPU_POWER_BUDGET_CTL_TNG,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = set_floor_freq_atom,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_ann = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = NULL,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_cht = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = NULL,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_amd = {
	.check_unit = rapl_default_check_unit,
};

static const struct rapl_defaults rapl_defaults_core_pl4 = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
	.msr_pl4_support = 1,
};

static const struct rapl_defaults rapl_defaults_core_pl4_pmu = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
	.msr_pl4_support = 1,
	.msr_pmu_support = 1,
};

static const struct x86_cpu_id rapl_ids[]  = {
	X86_MATCH_VFM(INTEL_SANDYBRIDGE,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE_X,		&rapl_defaults_core),

	X86_MATCH_VFM(INTEL_IVYBRIDGE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_IVYBRIDGE_X,		&rapl_defaults_core),

	X86_MATCH_VFM(INTEL_HASWELL,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_HASWELL_L,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_HASWELL_G,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_HASWELL_X,			&rapl_defaults_hsw_server),

	X86_MATCH_VFM(INTEL_BROADWELL,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_BROADWELL_G,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_BROADWELL_D,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_BROADWELL_X,		&rapl_defaults_hsw_server),

	X86_MATCH_VFM(INTEL_SKYLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_SKYLAKE_L,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_SKYLAKE_X,			&rapl_defaults_hsw_server),
	X86_MATCH_VFM(INTEL_KABYLAKE_L,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_KABYLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_CANNONLAKE_L,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ICELAKE_L,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_ICELAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ICELAKE_NNPI,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ICELAKE_X,			&rapl_defaults_hsw_server),
	X86_MATCH_VFM(INTEL_ICELAKE_D,			&rapl_defaults_hsw_server),
	X86_MATCH_VFM(INTEL_COMETLAKE_L,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_COMETLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_TIGERLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ROCKETLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ALDERLAKE,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_ATOM_GRACEMONT,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_BARTLETTLAKE,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_METEORLAKE,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_METEORLAKE_L,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X,		&rapl_defaults_spr_server),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X,		&rapl_defaults_spr_server),
	X86_MATCH_VFM(INTEL_LUNARLAKE_M,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_PANTHERLAKE_L,		&rapl_defaults_core_pl4_pmu),
	X86_MATCH_VFM(INTEL_WILDCATLAKE_L,		&rapl_defaults_core_pl4_pmu),
	X86_MATCH_VFM(INTEL_NOVALAKE,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_NOVALAKE_L,			&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_ARROWLAKE_H,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_ARROWLAKE,			&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ARROWLAKE_U,		&rapl_defaults_core_pl4),
	X86_MATCH_VFM(INTEL_LAKEFIELD,			&rapl_defaults_core),

	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT,		&rapl_defaults_byt),
	X86_MATCH_VFM(INTEL_ATOM_AIRMONT,		&rapl_defaults_cht),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT_MID,	&rapl_defaults_tng),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT_MID2,	&rapl_defaults_ann),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_PLUS,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_D,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_D,		&rapl_defaults_core),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_L,		&rapl_defaults_core),

	X86_MATCH_VFM(INTEL_XEON_PHI_KNL,		&rapl_defaults_hsw_server),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNM,		&rapl_defaults_hsw_server),

	X86_MATCH_VENDOR_FAM(AMD, 0x17,			&rapl_defaults_amd),
	X86_MATCH_VENDOR_FAM(AMD, 0x19,			&rapl_defaults_amd),
	X86_MATCH_VENDOR_FAM(AMD, 0x1A,			&rapl_defaults_amd),
	X86_MATCH_VENDOR_FAM(HYGON, 0x18,		&rapl_defaults_amd),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, rapl_ids);

static int rapl_msr_probe(struct platform_device *pdev)
{
	int ret;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		rapl_msr_priv = &rapl_msr_priv_intel;
		break;
	case X86_VENDOR_HYGON:
	case X86_VENDOR_AMD:
		rapl_msr_priv = &rapl_msr_priv_amd;
		break;
	default:
		pr_err("intel-rapl does not support CPU vendor %d\n", boot_cpu_data.x86_vendor);
		return -ENODEV;
	}
	rapl_msr_priv->read_raw = rapl_msr_read_raw;
	rapl_msr_priv->write_raw = rapl_msr_write_raw;
	rapl_msr_priv->defaults = (const struct rapl_defaults *)pdev->dev.platform_data;
	rapl_msr_priv->rpi = rpi_msr;

	if (rapl_msr_priv->defaults->msr_pl4_support) {
		rapl_msr_priv->limits[RAPL_DOMAIN_PACKAGE] |= BIT(POWER_LIMIT4);
		rapl_msr_priv->regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_PL4].msr =
			MSR_VR_CURRENT_CONFIG;
		pr_info("PL4 support detected (updated).\n");
	}

	if (rapl_msr_priv->defaults->msr_pmu_support) {
		rapl_msr_pmu = true;
		pr_info("MSR-based RAPL PMU support enabled (updated)\n");
	}

	rapl_msr_priv->control_type = powercap_register_control_type(NULL, "intel-rapl", NULL);
	if (IS_ERR(rapl_msr_priv->control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(rapl_msr_priv->control_type);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "powercap/rapl:online",
				rapl_cpu_online, rapl_cpu_down_prep);
	if (ret < 0)
		goto out;
	rapl_msr_priv->pcap_rapl_online = ret;

	return 0;

out:
	if (ret)
		powercap_unregister_control_type(rapl_msr_priv->control_type);
	return ret;
}

static void rapl_msr_remove(struct platform_device *pdev)
{
	cpuhp_remove_state(rapl_msr_priv->pcap_rapl_online);
	powercap_unregister_control_type(rapl_msr_priv->control_type);
}

static const struct platform_device_id rapl_msr_ids[] = {
	{ .name = "intel_rapl_msr", },
	{}
};
MODULE_DEVICE_TABLE(platform, rapl_msr_ids);

static struct platform_driver intel_rapl_msr_driver = {
	.probe = rapl_msr_probe,
	.remove = rapl_msr_remove,
	.id_table = rapl_msr_ids,
	.driver = {
		.name = "intel_rapl_msr",
	},
};

static struct platform_device *rapl_msr_platdev;

static int intel_rapl_msr_init(void)
{
	const struct rapl_defaults *def;
	const struct x86_cpu_id *id;
	int ret;

	ret = platform_driver_register(&intel_rapl_msr_driver);
	if (ret)
		return ret;

	/* Create the MSR RAPL platform device for supported platforms */
	id = x86_match_cpu(rapl_ids);
	if (!id)
		return 0;

	def = (const struct rapl_defaults *)id->driver_data;

	rapl_msr_platdev = platform_device_register_data(NULL, "intel_rapl_msr", 0, def,
							 sizeof(*def));
	if (IS_ERR(rapl_msr_platdev))
		pr_debug("intel_rapl_msr device register failed, ret:%ld\n",
			 PTR_ERR(rapl_msr_platdev));

	return 0;
}
module_init(intel_rapl_msr_init);

static void intel_rapl_msr_exit(void)
{
	platform_device_unregister(rapl_msr_platdev);
	platform_driver_unregister(&intel_rapl_msr_driver);
}
module_exit(intel_rapl_msr_exit);

MODULE_DESCRIPTION("Driver for Intel RAPL (Running Average Power Limit) control via MSR interface");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("INTEL_RAPL");
