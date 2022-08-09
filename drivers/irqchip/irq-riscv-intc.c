// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017-2018 SiFive
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#define pr_fmt(fmt) "riscv-intc: " fmt
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

	switch (cause) {
#ifdef CONFIG_SMP
	case RV_IRQ_SOFT:
		/*
		 * We only use software interrupts to pass IPIs, so if a
		 * non-SMP system gets one, then we don't know what to do.
		 */
		handle_IPI(regs);
		break;
#endif
	default:
		generic_handle_domain_irq(intc_domain, cause);
		break;
	}
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

static int riscv_intc_cpu_starting(unsigned int cpu)
{
	csr_set(CSR_IE, BIT(RV_IRQ_SOFT));
	return 0;
}

static int riscv_intc_cpu_dying(unsigned int cpu)
{
	csr_clear(CSR_IE, BIT(RV_IRQ_SOFT));
	return 0;
}

static struct irq_chip riscv_intc_chip = {
	.name = "RISC-V INTC",
	.irq_mask = riscv_intc_irq_mask,
	.irq_unmask = riscv_intc_irq_unmask,
};

static int riscv_intc_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, &riscv_intc_chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static const struct irq_domain_ops riscv_intc_domain_ops = {
	.map	= riscv_intc_domain_map,
	.xlate	= irq_domain_xlate_onecell,
};

static int __init riscv_intc_init(struct device_node *node,
				  struct device_node *parent)
{
	int rc, hartid;

	hartid = riscv_of_parent_hartid(node);
	if (hartid < 0) {
		pr_warn("unable to find hart id for %pOF\n", node);
		return 0;
	}

	/*
	 * The DT will have one INTC DT node under each CPU (or HART)
	 * DT node so riscv_intc_init() function will be called once
	 * for each INTC DT node. We only need to do INTC initialization
	 * for the INTC DT node belonging to boot CPU (or boot HART).
	 */
	if (riscv_hartid_to_cpuid(hartid) != smp_processor_id())
		return 0;

	intc_domain = irq_domain_add_linear(node, BITS_PER_LONG,
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

	cpuhp_setup_state(CPUHP_AP_IRQ_RISCV_STARTING,
			  "irqchip/riscv/intc:starting",
			  riscv_intc_cpu_starting,
			  riscv_intc_cpu_dying);

	pr_info("%d local interrupts mapped\n", BITS_PER_LONG);

	return 0;
}

IRQCHIP_DECLARE(riscv, "riscv,cpu-intc", riscv_intc_init);
