// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/string_choices.h>
#include <asm/sbi.h>
#include <asm/vendorid_list.h>

static int sswi_ipi_virq __ro_after_init;
static DEFINE_PER_CPU(void __iomem *, sswi_cpu_regs);

static void aclint_sswi_ipi_send(unsigned int cpu)
{
	writel(0x1, per_cpu(sswi_cpu_regs, cpu));
}

static void aclint_sswi_ipi_clear(void)
{
	writel_relaxed(0x0, this_cpu_read(sswi_cpu_regs));
}

static void aclint_sswi_ipi_handle(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	csr_clear(CSR_IP, IE_SIE);
	aclint_sswi_ipi_clear();

	ipi_mux_process();

	chained_irq_exit(chip, desc);
}

static int aclint_sswi_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(sswi_ipi_virq, irq_get_trigger_type(sswi_ipi_virq));

	return 0;
}

static int aclint_sswi_dying_cpu(unsigned int cpu)
{
	aclint_sswi_ipi_clear();

	disable_percpu_irq(sswi_ipi_virq);

	return 0;
}

static int __init aclint_sswi_parse_irq(struct fwnode_handle *fwnode, void __iomem *reg)
{
	u32 contexts = of_irq_count(to_of_node(fwnode));

	if (!(contexts)) {
		pr_err("%pfwP: no ACLINT SSWI context available\n", fwnode);
		return -EINVAL;
	}

	for (u32 i = 0; i < contexts; i++) {
		struct of_phandle_args parent;
		unsigned long hartid;
		u32 hart_index;
		int rc, cpu;

		rc = of_irq_parse_one(to_of_node(fwnode), i, &parent);
		if (rc)
			return rc;

		rc = riscv_of_parent_hartid(parent.np, &hartid);
		if (rc)
			return rc;

		if (parent.args[0] != RV_IRQ_SOFT)
			return -ENOTSUPP;

		cpu = riscv_hartid_to_cpuid(hartid);

		rc = riscv_get_hart_index(fwnode, i, &hart_index);
		if (rc) {
			pr_warn("%pfwP: hart index [%d] not found\n", fwnode, i);
			return -EINVAL;
		}
		per_cpu(sswi_cpu_regs, cpu) = reg + hart_index * 4;
	}

	pr_info("%pfwP: register %u CPU%s\n", fwnode, contexts, str_plural(contexts));

	return 0;
}

static int __init aclint_sswi_probe(struct fwnode_handle *fwnode)
{
	struct irq_domain *domain;
	void __iomem *reg;
	int virq, rc;

	if (!is_of_node(fwnode))
		return -EINVAL;

	reg = of_iomap(to_of_node(fwnode), 0);
	if (!reg)
		return -ENOMEM;

	/* Parse SSWI setting */
	rc = aclint_sswi_parse_irq(fwnode, reg);
	if (rc < 0)
		return rc;

	/* If mulitple SSWI devices are present, do not register irq again */
	if (sswi_ipi_virq)
		return 0;

	/* Find riscv intc domain and create IPI irq mapping */
	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(), DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("%pfwP: Failed to find INTC domain\n", fwnode);
		return -ENOENT;
	}

	sswi_ipi_virq = irq_create_mapping(domain, RV_IRQ_SOFT);
	if (!sswi_ipi_virq) {
		pr_err("unable to create ACLINT SSWI IRQ mapping\n");
		return -ENOMEM;
	}

	/* Register SSWI irq and handler */
	virq = ipi_mux_create(BITS_PER_BYTE, aclint_sswi_ipi_send);
	if (virq <= 0) {
		pr_err("unable to create muxed IPIs\n");
		irq_dispose_mapping(sswi_ipi_virq);
		return virq < 0 ? virq : -ENOMEM;
	}

	irq_set_chained_handler(sswi_ipi_virq, aclint_sswi_ipi_handle);

	cpuhp_setup_state(CPUHP_AP_IRQ_ACLINT_SSWI_STARTING,
			  "irqchip/aclint-sswi:starting",
			  aclint_sswi_starting_cpu,
			  aclint_sswi_dying_cpu);

	riscv_ipi_set_virq_range(virq, BITS_PER_BYTE);

	return 0;
}

/* generic/MIPS variant */
static int __init generic_aclint_sswi_probe(struct fwnode_handle *fwnode)
{
	int rc;

	rc = aclint_sswi_probe(fwnode);
	if (rc)
		return rc;

	/* Announce that SSWI is providing IPIs */
	pr_info("providing IPIs using ACLINT SSWI\n");

	return 0;
}

static int __init generic_aclint_sswi_early_probe(struct device_node *node,
						  struct device_node *parent)
{
	return generic_aclint_sswi_probe(&node->fwnode);
}
IRQCHIP_DECLARE(generic_aclint_sswi, "mips,p8700-aclint-sswi", generic_aclint_sswi_early_probe);

/* THEAD variant */
#define THEAD_C9XX_CSR_SXSTATUS			0x5c0
#define THEAD_C9XX_SXSTATUS_CLINTEE		BIT(17)

static int __init thead_aclint_sswi_probe(struct fwnode_handle *fwnode)
{
	int rc;

	/* If it is T-HEAD CPU, check whether SSWI is enabled */
	if (riscv_cached_mvendorid(0) == THEAD_VENDOR_ID &&
	    !(csr_read(THEAD_C9XX_CSR_SXSTATUS) & THEAD_C9XX_SXSTATUS_CLINTEE))
		return -ENOTSUPP;

	rc = aclint_sswi_probe(fwnode);
	if (rc)
		return rc;

	/* Announce that SSWI is providing IPIs */
	pr_info("providing IPIs using THEAD ACLINT SSWI\n");

	return 0;
}

static int __init thead_aclint_sswi_early_probe(struct device_node *node,
						struct device_node *parent)
{
	return thead_aclint_sswi_probe(&node->fwnode);
}
IRQCHIP_DECLARE(thead_aclint_sswi, "thead,c900-aclint-sswi", thead_aclint_sswi_early_probe);
