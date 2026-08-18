/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef _DRIVERS_IRQCHIP_IRQ_LOONGSON_H
#define _DRIVERS_IRQCHIP_IRQ_LOONGSON_H

#define AVEC_MSG_OFFSET		0x100000

struct avecintc_data {
	struct list_head        entry;
	unsigned int            cpu;
	unsigned int            vec;
	unsigned int            prev_cpu;
	unsigned int            prev_vec;
	unsigned int            moving;
};

int find_pch_pic(u32 gsi);

int liointc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_lio_pic *acpi_liointc);
int eiointc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_eio_pic *acpi_eiointc);
int avecintc_acpi_init(struct irq_domain *parent);

int redirect_acpi_init(struct irq_domain *parent);

int htvec_acpi_init(struct irq_domain *parent,
					struct acpi_madt_ht_pic *acpi_htvec);
int pch_lpc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_lpc_pic *acpi_pchlpc);
int pch_pic_acpi_init(struct irq_domain *parent,
					struct acpi_madt_bio_pic *acpi_pchpic);
int pch_msi_acpi_init(struct irq_domain *parent,
					struct acpi_madt_msi_pic *acpi_pchmsi);
int pch_msi_acpi_init_avec(struct irq_domain *parent);

void avecintc_sync(struct avecintc_data *adata);

#endif /* _DRIVERS_IRQCHIP_IRQ_LOONGSON_H */
