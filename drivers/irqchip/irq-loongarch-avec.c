// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Loongson Technologies, Inc.
 */

#include <linux/cpuhotplug.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/radix-tree.h>
#include <linux/spinlock.h>

#include <asm/loongarch.h>
#include <asm/setup.h>

#include "irq-msi-lib.h"
#include "irq-loongson.h"

#define VECTORS_PER_REG		64
#define IRR_VECTOR_MASK		0xffUL
#define IRR_INVALID_MASK	0x80000000UL
#define AVEC_MSG_OFFSET		0x100000

#ifdef CONFIG_SMP
struct pending_list {
	struct list_head	head;
};

static struct cpumask intersect_mask;
static DEFINE_PER_CPU(struct pending_list, pending_list);
#endif

static DEFINE_PER_CPU(struct irq_desc * [NR_VECTORS], irq_map);

struct avecintc_chip {
	raw_spinlock_t		lock;
	struct fwnode_handle	*fwnode;
	struct irq_domain	*domain;
	struct irq_matrix	*vector_matrix;
	phys_addr_t		msi_base_addr;
};

static struct avecintc_chip loongarch_avec;

struct avecintc_data {
	struct list_head	entry;
	unsigned int		cpu;
	unsigned int		vec;
	unsigned int		prev_cpu;
	unsigned int		prev_vec;
	unsigned int		moving;
};

static inline void avecintc_ack_irq(struct irq_data *d)
{
}

static inline void avecintc_mask_irq(struct irq_data *d)
{
}

static inline void avecintc_unmask_irq(struct irq_data *d)
{
}

#ifdef CONFIG_SMP
static inline void pending_list_init(int cpu)
{
	struct pending_list *plist = per_cpu_ptr(&pending_list, cpu);

	INIT_LIST_HEAD(&plist->head);
}

static void avecintc_sync(struct avecintc_data *adata)
{
	struct pending_list *plist;

	if (cpu_online(adata->prev_cpu)) {
		plist = per_cpu_ptr(&pending_list, adata->prev_cpu);
		list_add_tail(&adata->entry, &plist->head);
		adata->moving = 1;
		mp_ops.send_ipi_single(adata->prev_cpu, ACTION_CLEAR_VECTOR);
	}
}

static int avecintc_set_affinity(struct irq_data *data, const struct cpumask *dest, bool force)
{
	int cpu, ret, vector;
	struct avecintc_data *adata;

	scoped_guard(raw_spinlock, &loongarch_avec.lock) {
		adata = irq_data_get_irq_chip_data(data);

		if (adata->moving)
			return -EBUSY;

		if (cpu_online(adata->cpu) && cpumask_test_cpu(adata->cpu, dest))
			return 0;

		cpumask_and(&intersect_mask, dest, cpu_online_mask);

		ret = irq_matrix_alloc(loongarch_avec.vector_matrix, &intersect_mask, false, &cpu);
		if (ret < 0)
			return ret;

		vector = ret;
		adata->cpu = cpu;
		adata->vec = vector;
		per_cpu_ptr(irq_map, adata->cpu)[adata->vec] = irq_data_to_desc(data);
		avecintc_sync(adata);
	}

	irq_data_update_effective_affinity(data, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static int avecintc_cpu_online(unsigned int cpu)
{
	if (!loongarch_avec.vector_matrix)
		return 0;

	guard(raw_spinlock)(&loongarch_avec.lock);

	irq_matrix_online(loongarch_avec.vector_matrix);

	pending_list_init(cpu);

	return 0;
}

static int avecintc_cpu_offline(unsigned int cpu)
{
	struct pending_list *plist = per_cpu_ptr(&pending_list, cpu);

	if (!loongarch_avec.vector_matrix)
		return 0;

	guard(raw_spinlock)(&loongarch_avec.lock);

	if (!list_empty(&plist->head))
		pr_warn("CPU#%d vector is busy\n", cpu);

	irq_matrix_offline(loongarch_avec.vector_matrix);

	return 0;
}

void complete_irq_moving(void)
{
	struct pending_list *plist = this_cpu_ptr(&pending_list);
	struct avecintc_data *adata, *tdata;
	int cpu, vector, bias;
	uint64_t isr;

	guard(raw_spinlock)(&loongarch_avec.lock);

	list_for_each_entry_safe(adata, tdata, &plist->head, entry) {
		cpu = adata->prev_cpu;
		vector = adata->prev_vec;
		bias = vector / VECTORS_PER_REG;
		switch (bias) {
		case 0:
			isr = csr_read64(LOONGARCH_CSR_ISR0);
			break;
		case 1:
			isr = csr_read64(LOONGARCH_CSR_ISR1);
			break;
		case 2:
			isr = csr_read64(LOONGARCH_CSR_ISR2);
			break;
		case 3:
			isr = csr_read64(LOONGARCH_CSR_ISR3);
			break;
		}

		if (isr & (1UL << (vector % VECTORS_PER_REG))) {
			mp_ops.send_ipi_single(cpu, ACTION_CLEAR_VECTOR);
			continue;
		}
		list_del(&adata->entry);
		irq_matrix_free(loongarch_avec.vector_matrix, cpu, vector, false);
		this_cpu_write(irq_map[vector], NULL);
		adata->moving = 0;
		adata->prev_cpu = adata->cpu;
		adata->prev_vec = adata->vec;
	}
}
#endif

static void avecintc_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct avecintc_data *adata = irq_data_get_irq_chip_data(d);

	msg->address_hi = 0x0;
	msg->address_lo = (loongarch_avec.msi_base_addr | (adata->vec & 0xff) << 4)
			  | ((cpu_logical_map(adata->cpu & 0xffff)) << 12);
	msg->data = 0x0;
}

static struct irq_chip avec_irq_controller = {
	.name			= "AVECINTC",
	.irq_ack		= avecintc_ack_irq,
	.irq_mask		= avecintc_mask_irq,
	.irq_unmask		= avecintc_unmask_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity	= avecintc_set_affinity,
#endif
	.irq_compose_msi_msg	= avecintc_compose_msi_msg,
};

static void avecintc_irq_dispatch(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_desc *d;

	chained_irq_enter(chip, desc);

	while (true) {
		unsigned long vector = csr_read64(LOONGARCH_CSR_IRR);
		if (vector & IRR_INVALID_MASK)
			break;

		vector &= IRR_VECTOR_MASK;

		d = this_cpu_read(irq_map[vector]);
		if (d) {
			generic_handle_irq_desc(d);
		} else {
			spurious_interrupt();
			pr_warn("Unexpected IRQ occurs on CPU#%d [vector %ld]\n", smp_processor_id(), vector);
		}
	}

	chained_irq_exit(chip, desc);
}

static int avecintc_alloc_vector(struct irq_data *irqd, struct avecintc_data *adata)
{
	int cpu, ret;

	guard(raw_spinlock_irqsave)(&loongarch_avec.lock);

	ret = irq_matrix_alloc(loongarch_avec.vector_matrix, cpu_online_mask, false, &cpu);
	if (ret < 0)
		return ret;

	adata->prev_cpu = adata->cpu = cpu;
	adata->prev_vec = adata->vec = ret;
	per_cpu_ptr(irq_map, adata->cpu)[adata->vec] = irq_data_to_desc(irqd);

	return 0;
}

static int avecintc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	for (unsigned int i = 0; i < nr_irqs; i++) {
		struct irq_data *irqd = irq_domain_get_irq_data(domain, virq + i);
		struct avecintc_data *adata = kzalloc(sizeof(*adata), GFP_KERNEL);
		int ret;

		if (!adata)
			return -ENOMEM;

		ret = avecintc_alloc_vector(irqd, adata);
		if (ret < 0) {
			kfree(adata);
			return ret;
		}

		irq_domain_set_info(domain, virq + i, virq + i, &avec_irq_controller,
				    adata, handle_edge_irq, NULL, NULL);
		irqd_set_single_target(irqd);
		irqd_set_affinity_on_activate(irqd);
	}

	return 0;
}

static void avecintc_free_vector(struct irq_data *irqd, struct avecintc_data *adata)
{
	guard(raw_spinlock_irqsave)(&loongarch_avec.lock);

	per_cpu(irq_map, adata->cpu)[adata->vec] = NULL;
	irq_matrix_free(loongarch_avec.vector_matrix, adata->cpu, adata->vec, false);

#ifdef CONFIG_SMP
	if (!adata->moving)
		return;

	per_cpu(irq_map, adata->prev_cpu)[adata->prev_vec] = NULL;
	irq_matrix_free(loongarch_avec.vector_matrix, adata->prev_cpu, adata->prev_vec, false);
	list_del_init(&adata->entry);
#endif
}

static void avecintc_domain_free(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs)
{
	for (unsigned int i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		if (d) {
			struct avecintc_data *adata = irq_data_get_irq_chip_data(d);

			avecintc_free_vector(d, adata);
			irq_domain_reset_irq_data(d);
			kfree(adata);
		}
	}
}

static const struct irq_domain_ops avecintc_domain_ops = {
	.alloc		= avecintc_domain_alloc,
	.free		= avecintc_domain_free,
	.select		= msi_lib_irq_domain_select,
};

static int __init irq_matrix_init(void)
{
	loongarch_avec.vector_matrix = irq_alloc_matrix(NR_VECTORS, 0, NR_VECTORS);
	if (!loongarch_avec.vector_matrix)
		return -ENOMEM;

	for (int i = 0; i < NR_LEGACY_VECTORS; i++)
		irq_matrix_assign_system(loongarch_avec.vector_matrix, i, false);

	irq_matrix_online(loongarch_avec.vector_matrix);

	return 0;
}

static int __init avecintc_init(struct irq_domain *parent)
{
	int ret, parent_irq;
	unsigned long value;

	raw_spin_lock_init(&loongarch_avec.lock);

	loongarch_avec.fwnode = irq_domain_alloc_named_fwnode("AVECINTC");
	if (!loongarch_avec.fwnode) {
		pr_err("Unable to allocate domain handle\n");
		ret = -ENOMEM;
		goto out;
	}

	loongarch_avec.domain = irq_domain_create_tree(loongarch_avec.fwnode,
						       &avecintc_domain_ops, NULL);
	if (!loongarch_avec.domain) {
		pr_err("Unable to create IRQ domain\n");
		ret = -ENOMEM;
		goto out_free_handle;
	}

	parent_irq = irq_create_mapping(parent, INT_AVEC);
	if (!parent_irq) {
		pr_err("Failed to mapping hwirq\n");
		ret = -EINVAL;
		goto out_remove_domain;
	}

	ret = irq_matrix_init();
	if (ret < 0) {
		pr_err("Failed to init irq matrix\n");
		goto out_remove_domain;
	}
	irq_set_chained_handler_and_data(parent_irq, avecintc_irq_dispatch, NULL);

#ifdef CONFIG_SMP
	pending_list_init(0);
	cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_AVECINTC_STARTING,
				  "irqchip/loongarch/avecintc:starting",
				  avecintc_cpu_online, avecintc_cpu_offline);
#endif
	value = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	value |= IOCSR_MISC_FUNC_AVEC_EN;
	iocsr_write64(value, LOONGARCH_IOCSR_MISC_FUNC);

	return ret;

out_remove_domain:
	irq_domain_remove(loongarch_avec.domain);
out_free_handle:
	irq_domain_free_fwnode(loongarch_avec.fwnode);
out:
	return ret;
}

static int __init pch_msi_parse_madt(union acpi_subtable_headers *header,
				     const unsigned long end)
{
	struct acpi_madt_msi_pic *pchmsi_entry = (struct acpi_madt_msi_pic *)header;

	loongarch_avec.msi_base_addr = pchmsi_entry->msg_address - AVEC_MSG_OFFSET;

	return pch_msi_acpi_init_avec(loongarch_avec.domain);
}

static inline int __init acpi_cascade_irqdomain_init(void)
{
	return acpi_table_parse_madt(ACPI_MADT_TYPE_MSI_PIC, pch_msi_parse_madt, 1);
}

int __init avecintc_acpi_init(struct irq_domain *parent)
{
	int ret = avecintc_init(parent);
	if (ret < 0) {
		pr_err("Failed to init IRQ domain\n");
		return ret;
	}

	ret = acpi_cascade_irqdomain_init();
	if (ret < 0) {
		pr_err("Failed to init cascade IRQ domain\n");
		return ret;
	}

	return ret;
}
