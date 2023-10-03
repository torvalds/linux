// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017-2018 SiFive
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#define pr_fmt(fmt) "riscv-intc: " fmt
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/smp.h>

static struct irq_domain *intc_domain;

static asmlinkage void riscv_intc_irq(struct pt_regs *regs)
{
	unsigned long cause = regs->cause & ~CAUSE_IRQ_FLAG;

	if (unlikely(cause >= BITS_PER_LONG))
		panic("unexpected interrupt cause");

	generic_handle_domain_irq(intc_domain, cause);
}

/*
 * On RISC-V systems local interrupts are masked or unmasked by writing
 * the SIE (Supervisor Interrupt Enable) CSR.  As CSRs can only be written
 * on the local hart, these functions can only be called on the hart that
 * corresponds to the IRQ chip.
 */

static void riscv_intc_irq_mask(struct irq_data *d)
{
	csr_clear(CSR_IE, BIT(d->hwirq));
}

static void riscv_intc_irq_unmask(struct irq_data *d)
{
	csr_set(CSR_IE, BIT(d->hwirq));
}

static void riscv_intc_irq_eoi(struct irq_data *d)
{
	/*
	 * The RISC-V INTC driver uses handle_percpu_devid_irq() flow
	 * for the per-HART local interrupts and child irqchip drivers
	 * (such as PLIC, SBI IPI, CLINT, APLIC, IMSIC, etc) implement
	 * chained handlers for the per-HART local interrupts.
	 *
	 * In the absence of irq_eoi(), the chained_irq_enter() and
	 * chained_irq_exit() functions (used by child irqchip drivers)
	 * will do unnecessary mask/unmask of per-HART local interrupts
	 * at the time of handling interrupts. To avoid this, we provide
	 * an empty irq_eoi() callback for RISC-V INTC irqchip.
	 */
}

static struct irq_chip riscv_intc_chip = {
	.name = "RISC-V INTC",
	.irq_mask = riscv_intc_irq_mask,
	.irq_unmask = riscv_intc_irq_unmask,
	.irq_eoi = riscv_intc_irq_eoi,
};

static int riscv_intc_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, &riscv_intc_chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static int riscv_intc_domain_alloc(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs,
				   void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_onecell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = riscv_intc_domain_map(domain, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops riscv_intc_domain_ops = {
	.map	= riscv_intc_domain_map,
	.xlate	= irq_domain_xlate_onecell,
	.alloc	= riscv_intc_domain_alloc
};

static struct fwnode_handle *riscv_intc_hwnode(void)
{
	return intc_domain->fwnode;
}

static int __init riscv_intc_init_common(struct fwnode_handle *fn)
{
	int rc;

	intc_domain = irq_domain_create_linear(fn, BITS_PER_LONG,
					       &riscv_intc_domain_ops, NULL);
	if (!intc_domain) {
		pr_err("unable to add IRQ domain\n");
		return -ENXIO;
	}

	rc = set_handle_irq(&riscv_intc_irq);
	if (rc) {
		pr_err("failed to set irq handler\n");
		return rc;
	}

	riscv_set_intc_hwnode_fn(riscv_intc_hwnode);

	pr_info("%d local interrupts mapped\n", BITS_PER_LONG);

	return 0;
}

static int __init riscv_intc_init(struct device_node *node,
				  struct device_node *parent)
{
	int rc;
	unsigned long hartid;

	rc = riscv_of_parent_hartid(node, &hartid);
	if (rc < 0) {
		pr_warn("unable to find hart id for %pOF\n", node);
		return 0;
	}

	/*
	 * The DT will have one INTC DT node under each CPU (or HART)
	 * DT node so riscv_intc_init() function will be called once
	 * for each INTC DT node. We only need to do INTC initialization
	 * for the INTC DT node belonging to boot CPU (or boot HART).
	 */
	if (riscv_hartid_to_cpuid(hartid) != smp_processor_id()) {
		/*
		 * The INTC nodes of each CPU are suppliers for downstream
		 * interrupt controllers (such as PLIC, IMSIC and APLIC
		 * direct-mode) so we should mark an INTC node as initialized
		 * if we are not creating IRQ domain for it.
		 */
		fwnode_dev_initialized(of_fwnode_handle(node), true);
		return 0;
	}

	return riscv_intc_init_common(of_node_to_fwnode(node));
}

IRQCHIP_DECLARE(riscv, "riscv,cpu-intc", riscv_intc_init);

#ifdef CONFIG_ACPI

static int __init riscv_intc_acpi_init(union acpi_subtable_headers *header,
				       const unsigned long end)
{
	struct fwnode_handle *fn;
	struct acpi_madt_rintc *rintc;

	rintc = (struct acpi_madt_rintc *)header;

	/*
	 * The ACPI MADT will have one INTC for each CPU (or HART)
	 * so riscv_intc_acpi_init() function will be called once
	 * for each INTC. We only do INTC initialization
	 * for the INTC belonging to the boot CPU (or boot HART).
	 */
	if (riscv_hartid_to_cpuid(rintc->hart_id) != smp_processor_id())
		return 0;

	fn = irq_domain_alloc_named_fwnode("RISCV-INTC");
	if (!fn) {
		pr_err("unable to allocate INTC FW node\n");
		return -ENOMEM;
	}

	return riscv_intc_init_common(fn);
}

IRQCHIP_ACPI_DECLARE(riscv_intc, ACPI_MADT_TYPE_RINTC, NULL,
		     ACPI_MADT_RINTC_VERSION_V1, riscv_intc_acpi_init);
#endif
