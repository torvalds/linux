// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device RFIM control
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "processor_thermal_device.h"

static struct rapl_if_priv rapl_mmio_priv;

/* bitmasks for RAPL MSRs, used by primitive access functions */
#define MMIO_ENERGY_STATUS_MASK			GENMASK(31, 0)

#define MMIO_POWER_LIMIT1_MASK			GENMASK(14, 0)
#define MMIO_POWER_LIMIT1_ENABLE		BIT(15)
#define MMIO_POWER_LIMIT1_CLAMP			BIT(16)

#define MMIO_POWER_LIMIT2_MASK			GENMASK_ULL(46, 32)
#define MMIO_POWER_LIMIT2_ENABLE		BIT_ULL(47)
#define MMIO_POWER_LIMIT2_CLAMP			BIT_ULL(48)

#define MMIO_POWER_LOW_LOCK			BIT(31)
#define MMIO_POWER_HIGH_LOCK			BIT_ULL(63)

#define MMIO_POWER_LIMIT4_MASK			GENMASK(12, 0)

#define MMIO_TIME_WINDOW1_MASK			GENMASK_ULL(23, 17)
#define MMIO_TIME_WINDOW2_MASK			GENMASK_ULL(55, 49)

#define MMIO_POWER_INFO_MAX_MASK		GENMASK_ULL(46, 32)
#define MMIO_POWER_INFO_MIN_MASK		GENMASK_ULL(30, 16)
#define MMIO_POWER_INFO_MAX_TIME_WIN_MASK	GENMASK_ULL(53, 48)
#define MMIO_POWER_INFO_THERMAL_SPEC_MASK	GENMASK(14, 0)

#define MMIO_PERF_STATUS_THROTTLE_TIME_MASK	GENMASK(31, 0)
#define MMIO_PP_POLICY_MASK			GENMASK(4, 0)

/* RAPL primitives for MMIO I/F */
static struct rapl_primitive_info rpi_mmio[NR_RAPL_PRIMITIVES] = {
	/* name, mask, shift, msr index, unit divisor */
	[POWER_LIMIT1]		= PRIMITIVE_INFO_INIT(POWER_LIMIT1, MMIO_POWER_LIMIT1_MASK, 0,
						      RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT2]		= PRIMITIVE_INFO_INIT(POWER_LIMIT2, MMIO_POWER_LIMIT2_MASK, 32,
						      RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT4]		= PRIMITIVE_INFO_INIT(POWER_LIMIT4, MMIO_POWER_LIMIT4_MASK, 0,
						      RAPL_DOMAIN_REG_PL4, POWER_UNIT, 0),
	[ENERGY_COUNTER]	= PRIMITIVE_INFO_INIT(ENERGY_COUNTER, MMIO_ENERGY_STATUS_MASK, 0,
						      RAPL_DOMAIN_REG_STATUS, ENERGY_UNIT, 0),
	[FW_LOCK]		= PRIMITIVE_INFO_INIT(FW_LOCK, MMIO_POWER_LOW_LOCK, 31,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[FW_HIGH_LOCK]		= PRIMITIVE_INFO_INIT(FW_LOCK, MMIO_POWER_HIGH_LOCK, 63,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_ENABLE]		= PRIMITIVE_INFO_INIT(PL1_ENABLE, MMIO_POWER_LIMIT1_ENABLE, 15,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_CLAMP]		= PRIMITIVE_INFO_INIT(PL1_CLAMP, MMIO_POWER_LIMIT1_CLAMP, 16,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_ENABLE]		= PRIMITIVE_INFO_INIT(PL2_ENABLE, MMIO_POWER_LIMIT2_ENABLE, 47,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_CLAMP]		= PRIMITIVE_INFO_INIT(PL2_CLAMP, MMIO_POWER_LIMIT2_CLAMP, 48,
						      RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[TIME_WINDOW1]		= PRIMITIVE_INFO_INIT(TIME_WINDOW1, MMIO_TIME_WINDOW1_MASK, 17,
						      RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[TIME_WINDOW2]		= PRIMITIVE_INFO_INIT(TIME_WINDOW2, MMIO_TIME_WINDOW2_MASK, 49,
						      RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[THERMAL_SPEC_POWER]	= PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER,
						      MMIO_POWER_INFO_THERMAL_SPEC_MASK, 0,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_POWER]		= PRIMITIVE_INFO_INIT(MAX_POWER, MMIO_POWER_INFO_MAX_MASK, 32,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MIN_POWER]		= PRIMITIVE_INFO_INIT(MIN_POWER, MMIO_POWER_INFO_MIN_MASK, 16,
						      RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_TIME_WINDOW]	= PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW,
						      MMIO_POWER_INFO_MAX_TIME_WIN_MASK, 48,
						      RAPL_DOMAIN_REG_INFO, TIME_UNIT, 0),
	[THROTTLED_TIME]	= PRIMITIVE_INFO_INIT(THROTTLED_TIME,
						      MMIO_PERF_STATUS_THROTTLE_TIME_MASK, 0,
						      RAPL_DOMAIN_REG_PERF, TIME_UNIT, 0),
	[PRIORITY_LEVEL]	= PRIMITIVE_INFO_INIT(PRIORITY_LEVEL, MMIO_PP_POLICY_MASK, 0,
						      RAPL_DOMAIN_REG_POLICY, ARBITRARY_UNIT, 0),
};

static const struct rapl_mmio_regs rapl_mmio_default = {
	.reg_unit = 0x5938,
	.regs[RAPL_DOMAIN_PACKAGE] = { 0x59a0, 0x593c, 0x58f0, 0, 0x5930, 0x59b0},
	.regs[RAPL_DOMAIN_DRAM] = { 0x58e0, 0x58e8, 0x58ec, 0, 0},
	.limits[RAPL_DOMAIN_PACKAGE] = BIT(POWER_LIMIT2) | BIT(POWER_LIMIT4),
	.limits[RAPL_DOMAIN_DRAM] = BIT(POWER_LIMIT2),
};

static const struct rapl_defaults rapl_defaults_mmio = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_default_check_unit,
	.set_floor_freq = rapl_default_set_floor_freq,
	.compute_time_window = rapl_default_compute_time_window,
};

static int rapl_mmio_read_raw(int cpu, struct reg_action *ra, bool atomic)
{
	if (!ra->reg.mmio)
		return -EINVAL;

	ra->value = readq(ra->reg.mmio);
	ra->value &= ra->mask;
	return 0;
}

static int rapl_mmio_write_raw(int cpu, struct reg_action *ra)
{
	u64 val;

	if (!ra->reg.mmio)
		return -EINVAL;

	val = readq(ra->reg.mmio);
	val &= ~ra->mask;
	val |= ra->value;
	writeq(val, ra->reg.mmio);
	return 0;
}

int proc_thermal_rapl_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	const struct rapl_mmio_regs *rapl_regs = &rapl_mmio_default;
	struct rapl_package *rp;
	enum rapl_domain_reg_id reg;
	enum rapl_domain_type domain;
	int ret;

	if (!rapl_regs)
		return 0;

	for (domain = RAPL_DOMAIN_PACKAGE; domain < RAPL_DOMAIN_MAX; domain++) {
		for (reg = RAPL_DOMAIN_REG_LIMIT; reg < RAPL_DOMAIN_REG_MAX; reg++)
			if (rapl_regs->regs[domain][reg])
				rapl_mmio_priv.regs[domain][reg].mmio =
						proc_priv->mmio_base +
						rapl_regs->regs[domain][reg];
		rapl_mmio_priv.limits[domain] = rapl_regs->limits[domain];
	}
	rapl_mmio_priv.type = RAPL_IF_MMIO;
	rapl_mmio_priv.reg_unit.mmio = proc_priv->mmio_base + rapl_regs->reg_unit;

	rapl_mmio_priv.read_raw = rapl_mmio_read_raw;
	rapl_mmio_priv.write_raw = rapl_mmio_write_raw;
	rapl_mmio_priv.defaults = &rapl_defaults_mmio;
	rapl_mmio_priv.rpi = rpi_mmio;

	rapl_mmio_priv.control_type = powercap_register_control_type(NULL, "intel-rapl-mmio", NULL);
	if (IS_ERR(rapl_mmio_priv.control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(rapl_mmio_priv.control_type);
	}

	/* Register a RAPL package device for package 0 which is always online */
	rp = rapl_find_package_domain(0, &rapl_mmio_priv, false);
	if (rp) {
		ret = -EEXIST;
		goto err;
	}

	rp = rapl_add_package(0, &rapl_mmio_priv, false);
	if (IS_ERR(rp)) {
		ret = PTR_ERR(rp);
		goto err;
	}

	return 0;

err:
	powercap_unregister_control_type(rapl_mmio_priv.control_type);
	rapl_mmio_priv.control_type = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(proc_thermal_rapl_add);

void proc_thermal_rapl_remove(void)
{
	struct rapl_package *rp;

	if (IS_ERR_OR_NULL(rapl_mmio_priv.control_type))
		return;

	rp = rapl_find_package_domain(0, &rapl_mmio_priv, false);
	if (rp)
		rapl_remove_package(rp);
	powercap_unregister_control_type(rapl_mmio_priv.control_type);
}
EXPORT_SYMBOL_GPL(proc_thermal_rapl_remove);

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("INTEL_RAPL");
MODULE_DESCRIPTION("RAPL interface using MMIO");
