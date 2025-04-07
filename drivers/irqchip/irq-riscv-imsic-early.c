// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-imsic: " fmt
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/smp.h>

#include "irq-riscv-imsic-state.h"

static int imsic_parent_irq;

#ifdef CONFIG_SMP
static void imsic_ipi_send(unsigned int cpu)
{
	struct imsic_local_config *local = per_cpu_ptr(imsic->global.local, cpu);

	writel(IMSIC_IPI_ID, local->msi_va);
}

static void imsic_ipi_starting_cpu(void)
{
	/* Enable IPIs for current CPU. */
	__imsic_id_set_enable(IMSIC_IPI_ID);
}

static void imsic_ipi_dying_cpu(void)
{
	/* Disable IPIs for current CPU. */
	__imsic_id_clear_enable(IMSIC_IPI_ID);
}

static int __init imsic_ipi_domain_init(void)
{
	int virq;

	/* Create IMSIC IPI multiplexing */
	virq = ipi_mux_create(IMSIC_NR_IPI, imsic_ipi_send);
	if (virq <= 0)
		return virq < 0 ? virq : -ENOMEM;

	/* Set vIRQ range */
	riscv_ipi_set_virq_range(virq, IMSIC_NR_IPI);

	/* Announce that IMSIC is providing IPIs */
	pr_info("%pfwP: providing IPIs using interrupt %d\n", imsic->fwnode, IMSIC_IPI_ID);

	return 0;
}
#else
static void imsic_ipi_starting_cpu(void) { }
static void imsic_ipi_dying_cpu(void) { }
static int __init imsic_ipi_domain_init(void) { return 0; }
#endif

/*
 * To handle an interrupt, we read the TOPEI CSR and write zero in one
 * instruction. If TOPEI CSR is non-zero then we translate TOPEI.ID to
 * Linux interrupt number and let Linux IRQ subsystem handle it.
 */
static void imsic_handle_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cpu = smp_processor_id();
	struct imsic_vector *vec;
	unsigned long local_id;

	/*
	 * Process pending local synchronization instead of waiting
	 * for per-CPU local timer to expire.
	 */
	imsic_local_sync_all(false);

	chained_irq_enter(chip, desc);

	while ((local_id = csr_swap(CSR_TOPEI, 0))) {
		local_id >>= TOPEI_ID_SHIFT;

		if (local_id == IMSIC_IPI_ID) {
			if (IS_ENABLED(CONFIG_SMP))
				ipi_mux_process();
			continue;
		}

		if (unlikely(!imsic->base_domain))
			continue;

		vec = imsic_vector_from_local_id(cpu, local_id);
		if (!vec) {
			pr_warn_ratelimited("vector not found for local ID 0x%lx\n", local_id);
			continue;
		}

		generic_handle_irq(vec->irq);
	}

	chained_irq_exit(chip, desc);
}

static int imsic_starting_cpu(unsigned int cpu)
{
	/* Mark per-CPU IMSIC state as online */
	imsic_state_online();

	/* Enable per-CPU parent interrupt */
	enable_percpu_irq(imsic_parent_irq, irq_get_trigger_type(imsic_parent_irq));

	/* Setup IPIs */
	imsic_ipi_starting_cpu();

	/*
	 * Interrupts identities might have been enabled/disabled while
	 * this CPU was not running so sync-up local enable/disable state.
	 */
	imsic_local_sync_all(true);

	/* Enable local interrupt delivery */
	imsic_local_delivery(true);

	return 0;
}

static int imsic_dying_cpu(unsigned int cpu)
{
	/* Cleanup IPIs */
	imsic_ipi_dying_cpu();

	/* Mark per-CPU IMSIC state as offline */
	imsic_state_offline();

	return 0;
}

static int __init imsic_early_probe(struct fwnode_handle *fwnode)
{
	struct irq_domain *domain;
	int rc;

	/* Find parent domain and register chained handler */
	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(), DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("%pfwP: Failed to find INTC domain\n", fwnode);
		return -ENOENT;
	}
	imsic_parent_irq = irq_create_mapping(domain, RV_IRQ_EXT);
	if (!imsic_parent_irq) {
		pr_err("%pfwP: Failed to create INTC mapping\n", fwnode);
		return -ENOENT;
	}

	/* Initialize IPI domain */
	rc = imsic_ipi_domain_init();
	if (rc) {
		pr_err("%pfwP: Failed to initialize IPI domain\n", fwnode);
		return rc;
	}

	/* Setup chained handler to the parent domain interrupt */
	irq_set_chained_handler(imsic_parent_irq, imsic_handle_irq);

	/*
	 * Setup cpuhp state (must be done after setting imsic_parent_irq)
	 *
	 * Don't disable per-CPU IMSIC file when CPU goes offline
	 * because this affects IPI and the masking/unmasking of
	 * virtual IPIs is done via generic IPI-Mux
	 */
	cpuhp_setup_state(CPUHP_AP_IRQ_RISCV_IMSIC_STARTING, "irqchip/riscv/imsic:starting",
			  imsic_starting_cpu, imsic_dying_cpu);

	return 0;
}

static int __init imsic_early_dt_init(struct device_node *node, struct device_node *parent)
{
	struct fwnode_handle *fwnode = &node->fwnode;
	int rc;

	/* Setup IMSIC state */
	rc = imsic_setup_state(fwnode, NULL);
	if (rc) {
		pr_err("%pfwP: failed to setup state (error %d)\n", fwnode, rc);
		return rc;
	}

	/* Do early setup of IPIs */
	rc = imsic_early_probe(fwnode);
	if (rc)
		return rc;

	/* Ensure that OF platform device gets probed */
	of_node_clear_flag(node, OF_POPULATED);
	return 0;
}

IRQCHIP_DECLARE(riscv_imsic, "riscv,imsics", imsic_early_dt_init);

#ifdef CONFIG_ACPI

static struct fwnode_handle *imsic_acpi_fwnode;

struct fwnode_handle *imsic_acpi_get_fwnode(struct device *dev)
{
	return imsic_acpi_fwnode;
}

static int __init imsic_early_acpi_init(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct acpi_madt_imsic *imsic = (struct acpi_madt_imsic *)header;
	int rc;

	imsic_acpi_fwnode = irq_domain_alloc_named_fwnode("imsic");
	if (!imsic_acpi_fwnode) {
		pr_err("unable to allocate IMSIC FW node\n");
		return -ENOMEM;
	}

	/* Setup IMSIC state */
	rc = imsic_setup_state(imsic_acpi_fwnode, imsic);
	if (rc) {
		pr_err("%pfwP: failed to setup state (error %d)\n", imsic_acpi_fwnode, rc);
		return rc;
	}

	/* Do early setup of IMSIC state and IPIs */
	rc = imsic_early_probe(imsic_acpi_fwnode);
	if (rc) {
		irq_domain_free_fwnode(imsic_acpi_fwnode);
		imsic_acpi_fwnode = NULL;
		return rc;
	}

	rc = imsic_platform_acpi_probe(imsic_acpi_fwnode);

#ifdef CONFIG_PCI
	if (!rc)
		pci_msi_register_fwnode_provider(&imsic_acpi_get_fwnode);
#endif

	if (rc)
		pr_err("%pfwP: failed to register IMSIC for MSI functionality (error %d)\n",
		       imsic_acpi_fwnode, rc);

	/*
	 * Even if imsic_platform_acpi_probe() fails, the IPI part of IMSIC can
	 * continue to work. So, no need to return failure. This is similar to
	 * DT where IPI works but MSI probe fails for some reason.
	 */
	return 0;
}

IRQCHIP_ACPI_DECLARE(riscv_imsic, ACPI_MADT_TYPE_IMSIC, NULL,
		     1, imsic_early_acpi_init);
#endif
