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

static const struct rapl_mmio_regs rapl_mmio_default = {
	.reg_unit = 0x5938,
	.regs[RAPL_DOMAIN_PACKAGE] = { 0x59a0, 0x593c, 0x58f0, 0, 0x5930},
	.regs[RAPL_DOMAIN_DRAM] = { 0x58e0, 0x58e8, 0x58ec, 0, 0},
	.limits[RAPL_DOMAIN_PACKAGE] = 2,
	.limits[RAPL_DOMAIN_DRAM] = 2,
};

static int rapl_mmio_cpu_online(unsigned int cpu)
{
	struct rapl_package *rp;

	/* mmio rapl supports package 0 only for now */
	if (topology_physical_package_id(cpu))
		return 0;

	rp = rapl_find_package_domain(cpu, &rapl_mmio_priv);
	if (!rp) {
		rp = rapl_add_package(cpu, &rapl_mmio_priv);
		if (IS_ERR(rp))
			return PTR_ERR(rp);
	}
	cpumask_set_cpu(cpu, &rp->cpumask);
	return 0;
}

static int rapl_mmio_cpu_down_prep(unsigned int cpu)
{
	struct rapl_package *rp;
	int lead_cpu;

	rp = rapl_find_package_domain(cpu, &rapl_mmio_priv);
	if (!rp)
		return 0;

	cpumask_clear_cpu(cpu, &rp->cpumask);
	lead_cpu = cpumask_first(&rp->cpumask);
	if (lead_cpu >= nr_cpu_ids)
		rapl_remove_package(rp);
	else if (rp->lead_cpu == cpu)
		rp->lead_cpu = lead_cpu;
	return 0;
}

static int rapl_mmio_read_raw(int cpu, struct reg_action *ra)
{
	if (!ra->reg)
		return -EINVAL;

	ra->value = readq((void __iomem *)ra->reg);
	ra->value &= ra->mask;
	return 0;
}

static int rapl_mmio_write_raw(int cpu, struct reg_action *ra)
{
	u64 val;

	if (!ra->reg)
		return -EINVAL;

	val = readq((void __iomem *)ra->reg);
	val &= ~ra->mask;
	val |= ra->value;
	writeq(val, (void __iomem *)ra->reg);
	return 0;
}

int proc_thermal_rapl_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	const struct rapl_mmio_regs *rapl_regs = &rapl_mmio_default;
	enum rapl_domain_reg_id reg;
	enum rapl_domain_type domain;
	int ret;

	if (!rapl_regs)
		return 0;

	for (domain = RAPL_DOMAIN_PACKAGE; domain < RAPL_DOMAIN_MAX; domain++) {
		for (reg = RAPL_DOMAIN_REG_LIMIT; reg < RAPL_DOMAIN_REG_MAX; reg++)
			if (rapl_regs->regs[domain][reg])
				rapl_mmio_priv.regs[domain][reg] =
						(u64)proc_priv->mmio_base +
						rapl_regs->regs[domain][reg];
		rapl_mmio_priv.limits[domain] = rapl_regs->limits[domain];
	}
	rapl_mmio_priv.reg_unit = (u64)proc_priv->mmio_base + rapl_regs->reg_unit;

	rapl_mmio_priv.read_raw = rapl_mmio_read_raw;
	rapl_mmio_priv.write_raw = rapl_mmio_write_raw;

	rapl_mmio_priv.control_type = powercap_register_control_type(NULL, "intel-rapl-mmio", NULL);
	if (IS_ERR(rapl_mmio_priv.control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(rapl_mmio_priv.control_type);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "powercap/rapl:online",
				rapl_mmio_cpu_online, rapl_mmio_cpu_down_prep);
	if (ret < 0) {
		powercap_unregister_control_type(rapl_mmio_priv.control_type);
		rapl_mmio_priv.control_type = NULL;
		return ret;
	}
	rapl_mmio_priv.pcap_rapl_online = ret;

	return 0;
}
EXPORT_SYMBOL_GPL(proc_thermal_rapl_add);

void proc_thermal_rapl_remove(void)
{
	if (IS_ERR_OR_NULL(rapl_mmio_priv.control_type))
		return;

	cpuhp_remove_state(rapl_mmio_priv.pcap_rapl_online);
	powercap_unregister_control_type(rapl_mmio_priv.control_type);
}
EXPORT_SYMBOL_GPL(proc_thermal_rapl_remove);

MODULE_LICENSE("GPL v2");
