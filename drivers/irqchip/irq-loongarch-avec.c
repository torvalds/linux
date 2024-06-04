// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Loongson Technologies, Inc.
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

#define VECTORS_PER_REG		64
#define ILR_INVALID_MASK	0x80000000UL
#define ILR_VECTOR_MASK		0xffUL
#define AVEC_MSG_OFFSET		0x100000

static phys_addr_t msi_base_v2;
static DEFINE_PER_CPU(struct irq_desc * [NR_VECTORS], irq_map);

struct pending_list {
	struct list_head	head;
};

static DEFINE_PER_CPU(struct pending_list, pending_list);

struct loongarch_avec_chip {
	struct fwnode_handle	*fwnode;
	struct irq_domain	*domain;
	struct irq_matrix	*vector_matrix;
	raw_spinlock_t		lock;
};

static struct loongarch_avec_chip loongarch_avec;

struct loongarch_avec_data {
	struct list_head	entry;
	unsigned int		cpu;
	unsigned int		vec;
	unsigned int		prev_cpu;
	unsigned int		prev_vec;
	unsigned int		moving		: 1,
				managed		: 1;
};

static struct cpumask intersect_mask;

static int assign_irq_vector(struct irq_data *irqd, const struct cpumask *dest,
			     unsigned int *cpu)
{
	return irq_matrix_alloc(loongarch_avec.vector_matrix, dest, false, cpu);
}

static inline void loongarch_avec_ack_irq(struct irq_data *d)
{
}

static inline void loongarch_avec_unmask_irq(struct irq_data *d)
{
}

static inline void loongarch_avec_mask_irq(struct irq_data *d)
{
}

static void loongarch_avec_sync(struct loongarch_avec_data *adata)
{
	struct pending_list *plist;

	if (cpu_online(adata->prev_cpu)) {
		plist = per_cpu_ptr(&pending_list, adata->prev_cpu);
		list_add_tail(&adata->entry, &plist->head);
		adata->moving = true;
		loongson_send_ipi_single(adata->prev_cpu, SMP_CLEAR_VECT);
	}
	adata->prev_cpu = adata->cpu;
	adata->prev_vec = adata->vec;
}

static int loongarch_avec_set_affinity(struct irq_data *data, const struct cpumask *dest,
				       bool force)
{
	struct loongarch_avec_data *adata;
	unsigned int cpu, vector;
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&loongarch_avec.lock, flags);
	adata = irq_data_get_irq_chip_data(data);

	if (adata->vec && cpu_online(adata->cpu) && cpumask_test_cpu(adata->cpu, dest)) {
		raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
		return 0;
	}
	if (adata->moving)
		return -EBUSY;

	cpumask_and(&intersect_mask, dest, cpu_online_mask);

	ret = assign_irq_vector(data, &intersect_mask, &cpu);
	if (ret < 0) {
		raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
		return ret;
	}
	vector = ret;
	adata->cpu = cpu;
	adata->vec = vector;
	per_cpu_ptr(irq_map, adata->cpu)[adata->vec] = irq_data_to_desc(data);
	loongarch_avec_sync(adata);

	raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
	irq_data_update_effective_affinity(data, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static void loongarch_avec_compose_msg(struct irq_data *d,
		struct msi_msg *msg)
{
	struct loongarch_avec_data *avec_data;

	avec_data = irq_data_get_irq_chip_data(d);

	msg->address_hi = 0x0;
	msg->address_lo = msi_base_v2 | ((avec_data->vec & 0xff) << 4) |
			  ((cpu_logical_map(avec_data->cpu & 0xffff)) << 12);
	msg->data = 0x0;

}

static struct irq_chip loongarch_avec_controller = {
	.name			= "CORE_AVEC",
	.irq_ack		= loongarch_avec_ack_irq,
	.irq_mask		= loongarch_avec_mask_irq,
	.irq_unmask		= loongarch_avec_unmask_irq,
	.irq_set_affinity	= loongarch_avec_set_affinity,
	.irq_compose_msi_msg	= loongarch_avec_compose_msg,
};

void complete_irq_moving(void)
{
	struct pending_list *plist = this_cpu_ptr(&pending_list);
	struct loongarch_avec_data *adata, *tmp;
	int cpu, vector, bias;
	u64 irr;

	raw_spin_lock(&loongarch_avec.lock);

	list_for_each_entry_safe(adata, tmp, &plist->head, entry) {
		cpu = adata->prev_cpu;
		vector = adata->prev_vec;
		bias = vector / VECTORS_PER_REG;
		switch (bias) {
		case 0:
			irr = csr_read64(LOONGARCH_CSR_IRR0);
		case 1:
			irr = csr_read64(LOONGARCH_CSR_IRR1);
		case 2:
			irr = csr_read64(LOONGARCH_CSR_IRR2);
		case 3:
			irr = csr_read64(LOONGARCH_CSR_IRR3);
		}

		if (irr & (1UL << (vector % VECTORS_PER_REG))) {
			loongson_send_ipi_single(cpu, SMP_CLEAR_VECT);
			continue;
		}
		list_del(&adata->entry);
		irq_matrix_free(loongarch_avec.vector_matrix, cpu, vector, adata->managed);
		this_cpu_write(irq_map[vector], NULL);
		adata->moving = 0;
	}
	raw_spin_unlock(&loongarch_avec.lock);
}

static void loongarch_avec_dispatch(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long vector;
	struct irq_desc *d;

	chained_irq_enter(chip, desc);
	vector = csr_read64(LOONGARCH_CSR_ILR);
	if (vector & ILR_INVALID_MASK)
		return;

	vector &= ILR_VECTOR_MASK;

	d = this_cpu_read(irq_map[vector]);
	if (d) {
		generic_handle_irq_desc(d);
	} else {
		pr_warn("IRQ ERROR:Unexpected irq  occur on cpu %d[vector %ld]\n",
			smp_processor_id(), vector);
	}

	chained_irq_exit(chip, desc);
}

static int loongarch_avec_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	struct loongarch_avec_data *adata;
	struct irq_data *irqd;
	unsigned int cpu, vector, i, ret;
	unsigned long flags;

	raw_spin_lock_irqsave(&loongarch_avec.lock, flags);
	for (i = 0; i < nr_irqs; i++) {
		irqd = irq_domain_get_irq_data(domain, virq + i);
		adata = kzalloc(sizeof(*adata), GFP_KERNEL);
		if (!adata) {
			raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
			return -ENOMEM;
		}
		ret = assign_irq_vector(irqd, cpu_online_mask, &cpu);
		if (ret < 0) {
			raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
			return ret;
		}
		vector = ret;
		adata->prev_cpu = adata->cpu = cpu;
		adata->prev_vec = adata->vec = vector;
		adata->managed = irqd_affinity_is_managed(irqd);
		irq_domain_set_info(domain, virq + i, virq + i, &loongarch_avec_controller,
				adata, handle_edge_irq, NULL, NULL);
		adata->moving = 0;
		irqd_set_single_target(irqd);
		irqd_set_affinity_on_activate(irqd);

		per_cpu_ptr(irq_map, adata->cpu)[adata->vec] = irq_data_to_desc(irqd);
	}
	raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);

	return 0;
}

static void clear_free_vector(struct irq_data *irqd)
{
	struct loongarch_avec_data *adata = irq_data_get_irq_chip_data(irqd);
	bool managed = irqd_affinity_is_managed(irqd);

	per_cpu(irq_map, adata->cpu)[adata->vec] = NULL;
	irq_matrix_free(loongarch_avec.vector_matrix, adata->cpu, adata->vec, managed);
	adata->cpu = 0;
	adata->vec = 0;
	if (!adata->moving)
		return;

	per_cpu(irq_map, adata->prev_cpu)[adata->prev_vec] = 0;
	irq_matrix_free(loongarch_avec.vector_matrix, adata->prev_cpu,
			adata->prev_vec, adata->managed);
	adata->prev_vec = 0;
	adata->prev_cpu = 0;
	adata->moving = 0;
	list_del_init(&adata->entry);
}

static void loongarch_avec_free(struct irq_domain *domain, unsigned int virq,
		unsigned int nr_irqs)
{
	struct irq_data *d;
	unsigned long flags;
	unsigned int i;

	raw_spin_lock_irqsave(&loongarch_avec.lock, flags);
	for (i = 0; i < nr_irqs; i++) {
		d = irq_domain_get_irq_data(domain, virq + i);
		if (d) {
			clear_free_vector(d);
			irq_domain_reset_irq_data(d);

		}
	}

	raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
}

static const struct irq_domain_ops loongarch_avec_domain_ops = {
	.alloc		= loongarch_avec_alloc,
	.free		= loongarch_avec_free,
};

static int __init irq_matrix_init(void)
{
	int i;

	loongarch_avec.vector_matrix = irq_alloc_matrix(NR_VECTORS, 0, NR_VECTORS - 1);
	if (!loongarch_avec.vector_matrix)
		return -ENOMEM;
	for (i = 0; i < NR_LEGACY_VECTORS; i++)
		irq_matrix_assign_system(loongarch_avec.vector_matrix, i, false);

	irq_matrix_online(loongarch_avec.vector_matrix);

	return 0;
}

static int __init loongarch_avec_init(struct irq_domain *parent)
{
	struct pending_list *plist = per_cpu_ptr(&pending_list, 0);
	int ret = 0, parent_irq;
	unsigned long tmp;

	raw_spin_lock_init(&loongarch_avec.lock);

	loongarch_avec.fwnode = irq_domain_alloc_named_fwnode("CORE_AVEC");
	if (!loongarch_avec.fwnode) {
		pr_err("Unable to allocate domain handle\n");
		ret = -ENOMEM;
		goto out;
	}

	loongarch_avec.domain = irq_domain_create_tree(loongarch_avec.fwnode,
			&loongarch_avec_domain_ops, NULL);
	if (!loongarch_avec.domain) {
		pr_err("core-vec: cannot create IRQ domain\n");
		ret = -ENOMEM;
		goto out_free_handle;
	}

	parent_irq = irq_create_mapping(parent, INT_AVEC);
	if (!parent_irq) {
		pr_err("Failed to mapping hwirq\n");
		ret = -EINVAL;
		goto out_remove_domain;
	}
	irq_set_chained_handler_and_data(parent_irq, loongarch_avec_dispatch, NULL);

	ret = irq_matrix_init();
	if (ret) {
		pr_err("Failed to init irq matrix\n");
		goto out_free_matrix;
	}

	INIT_LIST_HEAD(&plist->head);
	tmp = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	tmp |= IOCSR_MISC_FUNC_AVEC_EN;
	iocsr_write64(tmp, LOONGARCH_IOCSR_MISC_FUNC);

	return ret;

out_free_matrix:
	kfree(loongarch_avec.vector_matrix);
out_remove_domain:
	irq_domain_remove(loongarch_avec.domain);
out_free_handle:
	irq_domain_free_fwnode(loongarch_avec.fwnode);
out:
	return ret;
}

void loongarch_avec_offline_cpu(unsigned int cpu)
{
	struct pending_list *plist = per_cpu_ptr(&pending_list, cpu);
	unsigned long flags;

	raw_spin_lock_irqsave(&loongarch_avec.lock, flags);
	if (list_empty(&plist->head))
		irq_matrix_offline(loongarch_avec.vector_matrix);
	else
		pr_warn("cpu %d advanced extioi is busy\n", cpu);
	raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
}

void loongarch_avec_online_cpu(unsigned int cpu)
{
	struct pending_list *plist = per_cpu_ptr(&pending_list, cpu);
	unsigned long flags;

	raw_spin_lock_irqsave(&loongarch_avec.lock, flags);

	irq_matrix_online(loongarch_avec.vector_matrix);

	INIT_LIST_HEAD(&plist->head);

	raw_spin_unlock_irqrestore(&loongarch_avec.lock, flags);
}

static int __init pch_msi_parse_madt(union acpi_subtable_headers *header,
				     const unsigned long end)
{
	struct acpi_madt_msi_pic *pchmsi_entry = (struct acpi_madt_msi_pic *)header;

	msi_base_v2 = pchmsi_entry->msg_address - AVEC_MSG_OFFSET;
	return pch_msi_acpi_init_v2(loongarch_avec.domain, pchmsi_entry);
}

static inline int __init acpi_cascade_irqdomain_init(void)
{
	return acpi_table_parse_madt(ACPI_MADT_TYPE_MSI_PIC, pch_msi_parse_madt, 1);
}

int __init loongarch_avec_acpi_init(struct irq_domain *parent)
{
	int ret = 0;

	ret = loongarch_avec_init(parent);
	if (ret) {
		pr_err("Failed to init irq domain\n");
		return ret;
	}

	ret = acpi_cascade_irqdomain_init();
	if (ret) {
		pr_err("Failed to cascade IRQ domain\n");
		return ret;
	}

	return ret;
}
