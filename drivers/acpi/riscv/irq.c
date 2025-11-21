// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2024, Ventana Micro Systems Inc
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 */

#include <linux/acpi.h>
#include <linux/sort.h>
#include <linux/irq.h>

#include "init.h"

#define RISCV_ACPI_INTC_FLAG_PENDING BIT(0)

struct riscv_ext_intc_list {
	acpi_handle		handle;
	u32			gsi_base;
	u32			nr_irqs;
	u32			nr_idcs;
	u32			id;
	u32			type;
	u32			flag;
	struct list_head	list;
};

struct acpi_irq_dep_ctx {
	int		rc;
	unsigned int	index;
	acpi_handle	handle;
};

LIST_HEAD(ext_intc_list);

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

static acpi_status riscv_acpi_update_gsi_handle(u32 gsi_base, acpi_handle handle)
{
	struct riscv_ext_intc_list *ext_intc_element;
	struct list_head *i, *tmp;

	list_for_each_safe(i, tmp, &ext_intc_list) {
		ext_intc_element = list_entry(i, struct riscv_ext_intc_list, list);
		if (gsi_base == ext_intc_element->gsi_base) {
			ext_intc_element->handle = handle;
			return AE_OK;
		}
	}

	return AE_NOT_FOUND;
}

int riscv_acpi_update_gsi_range(u32 gsi_base, u32 nr_irqs)
{
	struct riscv_ext_intc_list *ext_intc_element;

	list_for_each_entry(ext_intc_element, &ext_intc_list, list) {
		if (gsi_base == ext_intc_element->gsi_base &&
		    (ext_intc_element->flag & RISCV_ACPI_INTC_FLAG_PENDING)) {
			ext_intc_element->nr_irqs = nr_irqs;
			ext_intc_element->flag &= ~RISCV_ACPI_INTC_FLAG_PENDING;
			return 0;
		}
	}

	return -ENODEV;
}

int riscv_acpi_get_gsi_info(struct fwnode_handle *fwnode, u32 *gsi_base,
			    u32 *id, u32 *nr_irqs, u32 *nr_idcs)
{
	struct riscv_ext_intc_list *ext_intc_element;
	struct list_head *i;

	list_for_each(i, &ext_intc_list) {
		ext_intc_element = list_entry(i, struct riscv_ext_intc_list, list);
		if (ext_intc_element->handle == ACPI_HANDLE_FWNODE(fwnode)) {
			*gsi_base = ext_intc_element->gsi_base;
			*id = ext_intc_element->id;
			*nr_irqs = ext_intc_element->nr_irqs;
			if (nr_idcs)
				*nr_idcs = ext_intc_element->nr_idcs;

			return 0;
		}
	}

	return -ENODEV;
}

struct fwnode_handle *riscv_acpi_get_gsi_domain_id(u32 gsi)
{
	struct riscv_ext_intc_list *ext_intc_element;
	struct acpi_device *adev;
	struct list_head *i;

	list_for_each(i, &ext_intc_list) {
		ext_intc_element = list_entry(i, struct riscv_ext_intc_list, list);
		if (gsi >= ext_intc_element->gsi_base &&
		    gsi < (ext_intc_element->gsi_base + ext_intc_element->nr_irqs)) {
			adev = acpi_fetch_acpi_dev(ext_intc_element->handle);
			if (!adev)
				return NULL;

			return acpi_fwnode_handle(adev);
		}
	}

	return NULL;
}

static int __init riscv_acpi_register_ext_intc(u32 gsi_base, u32 nr_irqs, u32 nr_idcs,
					       u32 id, u32 type)
{
	struct riscv_ext_intc_list *ext_intc_element, *node, *prev;

	ext_intc_element = kzalloc(sizeof(*ext_intc_element), GFP_KERNEL);
	if (!ext_intc_element)
		return -ENOMEM;

	ext_intc_element->gsi_base = gsi_base;

	/* If nr_irqs is zero, indicate it in flag and set to max range possible */
	if (nr_irqs) {
		ext_intc_element->nr_irqs = nr_irqs;
	} else {
		ext_intc_element->flag |= RISCV_ACPI_INTC_FLAG_PENDING;
		ext_intc_element->nr_irqs = U32_MAX - ext_intc_element->gsi_base;
	}

	ext_intc_element->nr_idcs = nr_idcs;
	ext_intc_element->id = id;
	list_for_each_entry(node, &ext_intc_list, list) {
		if (node->gsi_base < ext_intc_element->gsi_base)
			break;
	}

	/* Adjust the previous node's GSI range if that has pending registration */
	prev = list_prev_entry(node, list);
	if (!list_entry_is_head(prev, &ext_intc_list, list)) {
		if (prev->flag & RISCV_ACPI_INTC_FLAG_PENDING)
			prev->nr_irqs = ext_intc_element->gsi_base - prev->gsi_base;
	}

	list_add_tail(&ext_intc_element->list, &node->list);
	return 0;
}

static acpi_status __init riscv_acpi_create_gsi_map_smsi(acpi_handle handle, u32 level,
							 void *context, void **return_value)
{
	acpi_status status;
	u64 gbase;

	if (!acpi_has_method(handle, "_GSB")) {
		acpi_handle_err(handle, "_GSB method not found\n");
		return AE_ERROR;
	}

	status = acpi_evaluate_integer(handle, "_GSB", NULL, &gbase);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to evaluate _GSB method\n");
		return status;
	}

	riscv_acpi_register_ext_intc(gbase, 0, 0, 0, ACPI_RISCV_IRQCHIP_SMSI);
	status = riscv_acpi_update_gsi_handle((u32)gbase, handle);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to find the GSI mapping entry\n");
		return status;
	}

	return AE_OK;
}

static acpi_status __init riscv_acpi_create_gsi_map(acpi_handle handle, u32 level,
						    void *context, void **return_value)
{
	acpi_status status;
	u64 gbase;

	if (!acpi_has_method(handle, "_GSB")) {
		acpi_handle_err(handle, "_GSB method not found\n");
		return AE_ERROR;
	}

	status = acpi_evaluate_integer(handle, "_GSB", NULL, &gbase);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to evaluate _GSB method\n");
		return status;
	}

	status = riscv_acpi_update_gsi_handle((u32)gbase, handle);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to find the GSI mapping entry\n");
		return status;
	}

	return AE_OK;
}

static int __init riscv_acpi_aplic_parse_madt(union acpi_subtable_headers *header,
					      const unsigned long end)
{
	struct acpi_madt_aplic *aplic = (struct acpi_madt_aplic *)header;

	return riscv_acpi_register_ext_intc(aplic->gsi_base, aplic->num_sources, aplic->num_idcs,
					    aplic->id, ACPI_RISCV_IRQCHIP_APLIC);
}

static int __init riscv_acpi_plic_parse_madt(union acpi_subtable_headers *header,
					     const unsigned long end)
{
	struct acpi_madt_plic *plic = (struct acpi_madt_plic *)header;

	return riscv_acpi_register_ext_intc(plic->gsi_base, plic->num_irqs, 0,
					    plic->id, ACPI_RISCV_IRQCHIP_PLIC);
}

void __init riscv_acpi_init_gsi_mapping(void)
{
	/* There can be either PLIC or APLIC */
	if (acpi_table_parse_madt(ACPI_MADT_TYPE_PLIC, riscv_acpi_plic_parse_madt, 0) > 0) {
		acpi_get_devices("RSCV0001", riscv_acpi_create_gsi_map, NULL, NULL);
		return;
	}

	if (acpi_table_parse_madt(ACPI_MADT_TYPE_APLIC, riscv_acpi_aplic_parse_madt, 0) > 0)
		acpi_get_devices("RSCV0002", riscv_acpi_create_gsi_map, NULL, NULL);

	/* Unlike PLIC/APLIC, SYSMSI doesn't have MADT */
	acpi_get_devices("RSCV0006", riscv_acpi_create_gsi_map_smsi, NULL, NULL);
}

static acpi_handle riscv_acpi_get_gsi_handle(u32 gsi)
{
	struct riscv_ext_intc_list *ext_intc_element;
	struct list_head *i;

	list_for_each(i, &ext_intc_list) {
		ext_intc_element = list_entry(i, struct riscv_ext_intc_list, list);
		if (gsi >= ext_intc_element->gsi_base &&
		    gsi < (ext_intc_element->gsi_base + ext_intc_element->nr_irqs))
			return ext_intc_element->handle;
	}

	return NULL;
}

static acpi_status riscv_acpi_irq_get_parent(struct acpi_resource *ares, void *context)
{
	struct acpi_irq_dep_ctx *ctx = context;
	struct acpi_resource_irq *irq;
	struct acpi_resource_extended_irq *eirq;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		irq = &ares->data.irq;
		if (ctx->index >= irq->interrupt_count) {
			ctx->index -= irq->interrupt_count;
			return AE_OK;
		}
		ctx->handle = riscv_acpi_get_gsi_handle(irq->interrupts[ctx->index]);
		return AE_CTRL_TERMINATE;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		eirq = &ares->data.extended_irq;
		if (eirq->producer_consumer == ACPI_PRODUCER)
			return AE_OK;

		if (ctx->index >= eirq->interrupt_count) {
			ctx->index -= eirq->interrupt_count;
			return AE_OK;
		}

		/* Support GSIs only */
		if (eirq->resource_source.string_length)
			return AE_OK;

		ctx->handle = riscv_acpi_get_gsi_handle(eirq->interrupts[ctx->index]);
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

static int riscv_acpi_irq_get_dep(acpi_handle handle, unsigned int index, acpi_handle *gsi_handle)
{
	struct acpi_irq_dep_ctx ctx = {-EINVAL, index, NULL};

	if (!gsi_handle)
		return 0;

	acpi_walk_resources(handle, METHOD_NAME__CRS, riscv_acpi_irq_get_parent, &ctx);
	*gsi_handle = ctx.handle;
	if (*gsi_handle)
		return 1;

	return 0;
}

static u32 riscv_acpi_add_prt_dep(acpi_handle handle)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_pci_routing_table *entry;
	struct acpi_handle_list dep_devices;
	acpi_handle gsi_handle;
	acpi_handle link_handle;
	acpi_status status;
	u32 count = 0;

	status = acpi_get_irq_routing_table(handle, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to get IRQ routing table\n");
		kfree(buffer.pointer);
		return 0;
	}

	entry = buffer.pointer;
	while (entry && (entry->length > 0)) {
		if (entry->source[0]) {
			acpi_get_handle(handle, entry->source, &link_handle);
			dep_devices.count = 1;
			dep_devices.handles = kcalloc(1, sizeof(*dep_devices.handles), GFP_KERNEL);
			if (!dep_devices.handles) {
				acpi_handle_err(handle, "failed to allocate memory\n");
				continue;
			}

			dep_devices.handles[0] = link_handle;
			count += acpi_scan_add_dep(handle, &dep_devices);
		} else {
			gsi_handle = riscv_acpi_get_gsi_handle(entry->source_index);
			dep_devices.count = 1;
			dep_devices.handles = kcalloc(1, sizeof(*dep_devices.handles), GFP_KERNEL);
			if (!dep_devices.handles) {
				acpi_handle_err(handle, "failed to allocate memory\n");
				continue;
			}

			dep_devices.handles[0] = gsi_handle;
			count += acpi_scan_add_dep(handle, &dep_devices);
		}

		entry = (struct acpi_pci_routing_table *)
			((unsigned long)entry + entry->length);
	}

	kfree(buffer.pointer);
	return count;
}

static u32 riscv_acpi_add_irq_dep(acpi_handle handle)
{
	struct acpi_handle_list dep_devices;
	acpi_handle gsi_handle;
	u32 count = 0;
	int i;

	for (i = 0;
	     riscv_acpi_irq_get_dep(handle, i, &gsi_handle);
	     i++) {
		dep_devices.count = 1;
		dep_devices.handles = kcalloc(1, sizeof(*dep_devices.handles), GFP_KERNEL);
		if (!dep_devices.handles) {
			acpi_handle_err(handle, "failed to allocate memory\n");
			continue;
		}

		dep_devices.handles[0] = gsi_handle;
		count += acpi_scan_add_dep(handle, &dep_devices);
	}

	return count;
}

u32 arch_acpi_add_auto_dep(acpi_handle handle)
{
	if (acpi_has_method(handle, "_PRT"))
		return riscv_acpi_add_prt_dep(handle);

	return riscv_acpi_add_irq_dep(handle);
}
