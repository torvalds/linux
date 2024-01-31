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

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

/* Local defines */
#define MSR_PLATFORM_POWER_LIMIT	0x0000065C
#define MSR_VR_CURRENT_CONFIG		0x00000601

/* private data for RAPL MSR Interface */
static struct rapl_if_priv *rapl_msr_priv;

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
	if (lead_cpu >= nr_cpu_ids)
		rapl_remove_package_cpuslocked(rp);
	else if (rp->lead_cpu == cpu)
		rp->lead_cpu = lead_cpu;
	return 0;
}

static int rapl_msr_read_raw(int cpu, struct reg_action *ra)
{
	if (rdmsrl_safe_on_cpu(cpu, ra->reg.msr, &ra->value)) {
		pr_debug("failed to read msr 0x%x on cpu %d\n", ra->reg.msr, cpu);
		return -EIO;
	}
	ra->value &= ra->mask;
	return 0;
}

static void rapl_msr_update_func(void *info)
{
	struct reg_action *ra = info;
	u64 val;

	ra->err = rdmsrl_safe(ra->reg.msr, &val);
	if (ra->err)
		return;

	val &= ~ra->mask;
	val |= ra->value;

	ra->err = wrmsrl_safe(ra->reg.msr, val);
}

static int rapl_msr_write_raw(int cpu, struct reg_action *ra)
{
	int ret;

	ret = smp_call_function_single(cpu, rapl_msr_update_func, ra, 1);
	if (WARN_ON_ONCE(ret))
		return ret;

	return ra->err;
}

/* List of verified CPUs. */
static const struct x86_cpu_id pl4_support_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GRACEMONT, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(METEORLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(METEORLAKE_L, NULL),
	{}
};

static int rapl_msr_probe(struct platform_device *pdev)
{
	const struct x86_cpu_id *id = x86_match_cpu(pl4_support_ids);
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

	if (id) {
		rapl_msr_priv->limits[RAPL_DOMAIN_PACKAGE] |= BIT(POWER_LIMIT4);
		rapl_msr_priv->regs[RAPL_DOMAIN_PACKAGE][RAPL_DOMAIN_REG_PL4].msr =
			MSR_VR_CURRENT_CONFIG;
		pr_info("PL4 support detected.\n");
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

static int rapl_msr_remove(struct platform_device *pdev)
{
	cpuhp_remove_state(rapl_msr_priv->pcap_rapl_online);
	powercap_unregister_control_type(rapl_msr_priv->control_type);
	return 0;
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

module_platform_driver(intel_rapl_msr_driver);

MODULE_DESCRIPTION("Driver for Intel RAPL (Running Average Power Limit) control via MSR interface");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL v2");
