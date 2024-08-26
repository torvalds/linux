// SPDX-License-Identifier: GPL-2.0-only
/*
 * Multiplex several IPIs over a single HW IPI.
 *
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv: " fmt
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <asm/sbi.h>

DEFINE_STATIC_KEY_FALSE(riscv_sbi_for_rfence);
EXPORT_SYMBOL_GPL(riscv_sbi_for_rfence);

static int sbi_ipi_virq;

static void sbi_ipi_handle(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	csr_clear(CSR_IP, IE_SIE);
	ipi_mux_process();

	chained_irq_exit(chip, desc);
}

static int sbi_ipi_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(sbi_ipi_virq, irq_get_trigger_type(sbi_ipi_virq));
	return 0;
}

void __init sbi_ipi_init(void)
{
	int virq;
	struct irq_domain *domain;

	if (riscv_ipi_have_virq_range())
		return;

	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(),
					  DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("unable to find INTC IRQ domain\n");
		return;
	}

	sbi_ipi_virq = irq_create_mapping(domain, RV_IRQ_SOFT);
	if (!sbi_ipi_virq) {
		pr_err("unable to create INTC IRQ mapping\n");
		return;
	}

	virq = ipi_mux_create(BITS_PER_BYTE, sbi_send_ipi);
	if (virq <= 0) {
		pr_err("unable to create muxed IPIs\n");
		irq_dispose_mapping(sbi_ipi_virq);
		return;
	}

	irq_set_chained_handler(sbi_ipi_virq, sbi_ipi_handle);

	/*
	 * Don't disable IPI when CPU goes offline because
	 * the masking/unmasking of virtual IPIs is done
	 * via generic IPI-Mux
	 */
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			  "irqchip/sbi-ipi:starting",
			  sbi_ipi_starting_cpu, NULL);

	riscv_ipi_set_virq_range(virq, BITS_PER_BYTE);
	pr_info("providing IPIs using SBI IPI extension\n");

	/*
	 * Use the SBI remote fence extension to avoid
	 * the extra context switch needed to handle IPIs.
	 */
	static_branch_enable(&riscv_sbi_for_rfence);
}
