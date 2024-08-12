// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2024, Ventana Micro Systems Inc
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 */

#include <linux/acpi.h>
#include <linux/sort.h>

static int irqchip_cmp_func(const void *in0, const void *in1)
{
	struct acpi_probe_entry *elem0 = (struct acpi_probe_entry *)in0;
	struct acpi_probe_entry *elem1 = (struct acpi_probe_entry *)in1;

	return (elem0->type > elem1->type) - (elem0->type < elem1->type);
}

/*
 * On RISC-V, RINTC structures in MADT should be probed before any other
 * interrupt controller structures and IMSIC before APLIC. The interrupt
 * controller subtypes in MADT of ACPI spec for RISC-V are defined in
 * the incremental order like RINTC(24)->IMSIC(25)->APLIC(26)->PLIC(27).
 * Hence, simply sorting the subtypes in incremental order will
 * establish the required order.
 */
void arch_sort_irqchip_probe(struct acpi_probe_entry *ap_head, int nr)
{
	struct acpi_probe_entry *ape = ap_head;

	if (nr == 1 || !ACPI_COMPARE_NAMESEG(ACPI_SIG_MADT, ape->id))
		return;
	sort(ape, nr, sizeof(*ape), irqchip_cmp_func, NULL);
}
