// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/loongarch.h>
#include <asm/setup.h>

static struct irq_domain *irq_domain;
struct fwnode_handle *cpuintc_handle;

static void mask_loongarch_irq(struct irq_data *d)
{
	clear_csr_ecfg(ECFGF(d->hwirq));
}

static void unmask_loongarch_irq(struct irq_data *d)
{
	set_csr_ecfg(ECFGF(d->hwirq));
}

static struct irq_chip cpu_irq_controller = {
	.name		= "CPUINTC",
	.irq_mask	= mask_loongarch_irq,
	.irq_unmask	= unmask_loongarch_irq,
};

static void handle_cpu_irq(struct pt_regs *regs)
{
	int hwirq;
	unsigned int estat = read_csr_estat() & CSR_ESTAT_IS;

	while ((hwirq = ffs(estat))) {
		estat &= ~BIT(hwirq - 1);
		generic_handle_domain_irq(irq_domain, hwirq - 1);
	}
}

static int loongarch_cpu_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_noprobe(irq);
	irq_set_chip_and_handler(irq, &cpu_irq_controller, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops loongarch_cpu_intc_irq_domain_ops = {
	.map = loongarch_cpu_intc_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init
liointc_parse_madt(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	struct acpi_madt_lio_pic *liointc_entry = (struct acpi_madt_lio_pic *)header;

	return liointc_acpi_init(irq_domain, liointc_entry);
}

static int __init
eiointc_parse_madt(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	struct acpi_madt_eio_pic *eiointc_entry = (struct acpi_madt_eio_pic *)header;

	return eiointc_acpi_init(irq_domain, eiointc_entry);
}

static int __init acpi_cascade_irqdomain_init(void)
{
	acpi_table_parse_madt(ACPI_MADT_TYPE_LIO_PIC,
			      liointc_parse_madt, 0);
	acpi_table_parse_madt(ACPI_MADT_TYPE_EIO_PIC,
			      eiointc_parse_madt, 0);
	return 0;
}

static int __init cpuintc_acpi_init(union acpi_subtable_headers *header,
				   const unsigned long end)
{
	if (irq_domain)
		return 0;

	/* Mask interrupts. */
	clear_csr_ecfg(ECFG0_IM);
	clear_csr_estat(ESTATF_IP);

	cpuintc_handle = irq_domain_alloc_fwnode(NULL);
	irq_domain = irq_domain_create_linear(cpuintc_handle, EXCCODE_INT_NUM,
					&loongarch_cpu_intc_irq_domain_ops, NULL);

	if (!irq_domain)
		panic("Failed to add irqdomain for LoongArch CPU");

	set_handle_irq(&handle_cpu_irq);
	acpi_cascade_irqdomain_init();

	return 0;
}

IRQCHIP_ACPI_DECLARE(cpuintc_v1, ACPI_MADT_TYPE_CORE_PIC,
		NULL, ACPI_MADT_CORE_PIC_VERSION_V1, cpuintc_acpi_init);
