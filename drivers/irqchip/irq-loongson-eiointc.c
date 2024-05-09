// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson Extend I/O Interrupt Controller support
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) "eiointc: " fmt

#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/kernel.h>
#include <linux/syscore_ops.h>

#define EIOINTC_REG_NODEMAP	0x14a0
#define EIOINTC_REG_IPMAP	0x14c0
#define EIOINTC_REG_ENABLE	0x1600
#define EIOINTC_REG_BOUNCE	0x1680
#define EIOINTC_REG_ISR		0x1800
#define EIOINTC_REG_ROUTE	0x1c00

#define VEC_REG_COUNT		4
#define VEC_COUNT_PER_REG	64
#define VEC_COUNT		(VEC_REG_COUNT * VEC_COUNT_PER_REG)
#define VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define VEC_REG_BIT(irq_id)     ((irq_id) % VEC_COUNT_PER_REG)
#define EIOINTC_ALL_ENABLE	0xffffffff

#define MAX_EIO_NODES		(NR_CPUS / CORES_PER_EIO_NODE)

static int nr_pics;

struct eiointc_priv {
	u32			node;
	u32			vec_count;
	nodemask_t		node_map;
	cpumask_t		cpuspan_map;
	struct fwnode_handle	*domain_handle;
	struct irq_domain	*eiointc_domain;
};

static struct eiointc_priv *eiointc_priv[MAX_IO_PICS];

static void eiointc_enable(void)
{
	uint64_t misc;

	misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_write64(misc, LOONGARCH_IOCSR_MISC_FUNC);
}

static int cpu_to_eio_node(int cpu)
{
	return cpu_logical_map(cpu) / CORES_PER_EIO_NODE;
}

static void eiointc_set_irq_route(int pos, unsigned int cpu, unsigned int mnode, nodemask_t *node_map)
{
	int i, node, cpu_node, route_node;
	unsigned char coremap;
	uint32_t pos_off, data, data_byte, data_mask;

	pos_off = pos & ~3;
	data_byte = pos & 3;
	data_mask = ~BIT_MASK(data_byte) & 0xf;

	/* Calculate node and coremap of target irq */
	cpu_node = cpu_logical_map(cpu) / CORES_PER_EIO_NODE;
	coremap = BIT(cpu_logical_map(cpu) % CORES_PER_EIO_NODE);

	for_each_online_cpu(i) {
		node = cpu_to_eio_node(i);
		if (!node_isset(node, *node_map))
			continue;

		/* EIO node 0 is in charge of inter-node interrupt dispatch */
		route_node = (node == mnode) ? cpu_node : node;
		data = ((coremap | (route_node << 4)) << (data_byte * 8));
		csr_any_send(EIOINTC_REG_ROUTE + pos_off, data, data_mask, node * CORES_PER_EIO_NODE);
	}
}

static DEFINE_RAW_SPINLOCK(affinity_lock);

static int eiointc_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity, bool force)
{
	unsigned int cpu;
	unsigned long flags;
	uint32_t vector, regaddr;
	struct cpumask intersect_affinity;
	struct eiointc_priv *priv = d->domain->host_data;

	raw_spin_lock_irqsave(&affinity_lock, flags);

	cpumask_and(&intersect_affinity, affinity, cpu_online_mask);
	cpumask_and(&intersect_affinity, &intersect_affinity, &priv->cpuspan_map);

	if (cpumask_empty(&intersect_affinity)) {
		raw_spin_unlock_irqrestore(&affinity_lock, flags);
		return -EINVAL;
	}
	cpu = cpumask_first(&intersect_affinity);

	vector = d->hwirq;
	regaddr = EIOINTC_REG_ENABLE + ((vector >> 5) << 2);

	/* Mask target vector */
	csr_any_send(regaddr, EIOINTC_ALL_ENABLE & (~BIT(vector & 0x1F)),
			0x0, priv->node * CORES_PER_EIO_NODE);

	/* Set route for target vector */
	eiointc_set_irq_route(vector, cpu, priv->node, &priv->node_map);

	/* Unmask target vector */
	csr_any_send(regaddr, EIOINTC_ALL_ENABLE,
			0x0, priv->node * CORES_PER_EIO_NODE);

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	raw_spin_unlock_irqrestore(&affinity_lock, flags);

	return IRQ_SET_MASK_OK;
}

static int eiointc_index(int node)
{
	int i;

	for (i = 0; i < nr_pics; i++) {
		if (node_isset(node, eiointc_priv[i]->node_map))
			return i;
	}

	return -1;
}

static int eiointc_router_init(unsigned int cpu)
{
	int i, bit;
	uint32_t data;
	uint32_t node = cpu_to_eio_node(cpu);
	int index = eiointc_index(node);

	if (index < 0) {
		pr_err("Error: invalid nodemap!\n");
		return -1;
	}

	if ((cpu_logical_map(cpu) % CORES_PER_EIO_NODE) == 0) {
		eiointc_enable();

		for (i = 0; i < eiointc_priv[0]->vec_count / 32; i++) {
			data = (((1 << (i * 2 + 1)) << 16) | (1 << (i * 2)));
			iocsr_write32(data, EIOINTC_REG_NODEMAP + i * 4);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 32 / 4; i++) {
			bit = BIT(1 + index); /* Route to IP[1 + index] */
			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_write32(data, EIOINTC_REG_IPMAP + i * 4);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 4; i++) {
			/* Route to Node-0 Core-0 */
			if (index == 0)
				bit = BIT(cpu_logical_map(0));
			else
				bit = (eiointc_priv[index]->node << 4) | 1;

			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_write32(data, EIOINTC_REG_ROUTE + i * 4);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 32; i++) {
			data = 0xffffffff;
			iocsr_write32(data, EIOINTC_REG_ENABLE + i * 4);
			iocsr_write32(data, EIOINTC_REG_BOUNCE + i * 4);
		}
	}

	return 0;
}

static void eiointc_irq_dispatch(struct irq_desc *desc)
{
	int i;
	u64 pending;
	bool handled = false;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct eiointc_priv *priv = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	for (i = 0; i < eiointc_priv[0]->vec_count / VEC_COUNT_PER_REG; i++) {
		pending = iocsr_read64(EIOINTC_REG_ISR + (i << 3));
		iocsr_write64(pending, EIOINTC_REG_ISR + (i << 3));
		while (pending) {
			int bit = __ffs(pending);
			int irq = bit + VEC_COUNT_PER_REG * i;

			generic_handle_domain_irq(priv->eiointc_domain, irq);
			pending &= ~BIT(bit);
			handled = true;
		}
	}

	if (!handled)
		spurious_interrupt();

	chained_irq_exit(chip, desc);
}

static void eiointc_ack_irq(struct irq_data *d)
{
}

static void eiointc_mask_irq(struct irq_data *d)
{
}

static void eiointc_unmask_irq(struct irq_data *d)
{
}

static struct irq_chip eiointc_irq_chip = {
	.name			= "EIOINTC",
	.irq_ack		= eiointc_ack_irq,
	.irq_mask		= eiointc_mask_irq,
	.irq_unmask		= eiointc_unmask_irq,
	.irq_set_affinity	= eiointc_set_irq_affinity,
};

static int eiointc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int ret;
	unsigned int i, type;
	unsigned long hwirq = 0;
	struct eiointc *priv = domain->host_data;

	ret = irq_domain_translate_onecell(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i, &eiointc_irq_chip,
					priv, handle_edge_irq, NULL, NULL);
	}

	return 0;
}

static void eiointc_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops eiointc_domain_ops = {
	.translate	= irq_domain_translate_onecell,
	.alloc		= eiointc_domain_alloc,
	.free		= eiointc_domain_free,
};

static void acpi_set_vec_parent(int node, struct irq_domain *parent, struct acpi_vector_group *vec_group)
{
	int i;

	for (i = 0; i < MAX_IO_PICS; i++) {
		if (node == vec_group[i].node) {
			vec_group[i].parent = parent;
			return;
		}
	}
}

static struct irq_domain *acpi_get_vec_parent(int node, struct acpi_vector_group *vec_group)
{
	int i;

	for (i = 0; i < MAX_IO_PICS; i++) {
		if (node == vec_group[i].node)
			return vec_group[i].parent;
	}
	return NULL;
}

static int eiointc_suspend(void)
{
	return 0;
}

static void eiointc_resume(void)
{
	int i, j;
	struct irq_desc *desc;
	struct irq_data *irq_data;

	eiointc_router_init(0);

	for (i = 0; i < nr_pics; i++) {
		for (j = 0; j < eiointc_priv[0]->vec_count; j++) {
			desc = irq_resolve_mapping(eiointc_priv[i]->eiointc_domain, j);
			if (desc && desc->handle_irq && desc->handle_irq != handle_bad_irq) {
				raw_spin_lock(&desc->lock);
				irq_data = irq_domain_get_irq_data(eiointc_priv[i]->eiointc_domain, irq_desc_get_irq(desc));
				eiointc_set_irq_affinity(irq_data, irq_data->common->affinity, 0);
				raw_spin_unlock(&desc->lock);
			}
		}
	}
}

static struct syscore_ops eiointc_syscore_ops = {
	.suspend = eiointc_suspend,
	.resume = eiointc_resume,
};

static int __init pch_pic_parse_madt(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct acpi_madt_bio_pic *pchpic_entry = (struct acpi_madt_bio_pic *)header;
	unsigned int node = (pchpic_entry->address >> 44) & 0xf;
	struct irq_domain *parent = acpi_get_vec_parent(node, pch_group);

	if (parent)
		return pch_pic_acpi_init(parent, pchpic_entry);

	return 0;
}

static int __init pch_msi_parse_madt(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct irq_domain *parent;
	struct acpi_madt_msi_pic *pchmsi_entry = (struct acpi_madt_msi_pic *)header;
	int node;

	if (cpu_has_flatmode)
		node = cpu_to_node(eiointc_priv[nr_pics - 1]->node * CORES_PER_EIO_NODE);
	else
		node = eiointc_priv[nr_pics - 1]->node;

	parent = acpi_get_vec_parent(node, msi_group);

	if (parent)
		return pch_msi_acpi_init(parent, pchmsi_entry);

	return 0;
}

static int __init acpi_cascade_irqdomain_init(void)
{
	int r;

	r = acpi_table_parse_madt(ACPI_MADT_TYPE_BIO_PIC, pch_pic_parse_madt, 0);
	if (r < 0)
		return r;

	r = acpi_table_parse_madt(ACPI_MADT_TYPE_MSI_PIC, pch_msi_parse_madt, 1);
	if (r < 0)
		return r;

	return 0;
}

static int __init eiointc_init(struct eiointc_priv *priv, int parent_irq,
			       u64 node_map)
{
	int i;

	node_map = node_map ? node_map : -1ULL;
	for_each_possible_cpu(i) {
		if (node_map & (1ULL << (cpu_to_eio_node(i)))) {
			node_set(cpu_to_eio_node(i), priv->node_map);
			cpumask_or(&priv->cpuspan_map, &priv->cpuspan_map,
				   cpumask_of(i));
		}
	}

	priv->eiointc_domain = irq_domain_create_linear(priv->domain_handle,
							priv->vec_count,
							&eiointc_domain_ops,
							priv);
	if (!priv->eiointc_domain) {
		pr_err("loongson-extioi: cannot add IRQ domain\n");
		return -ENOMEM;
	}

	eiointc_priv[nr_pics++] = priv;
	eiointc_router_init(0);
	irq_set_chained_handler_and_data(parent_irq, eiointc_irq_dispatch, priv);

	if (nr_pics == 1) {
		register_syscore_ops(&eiointc_syscore_ops);
		cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_LOONGARCH_STARTING,
					  "irqchip/loongarch/intc:starting",
					  eiointc_router_init, NULL);
	}

	return 0;
}

int __init eiointc_acpi_init(struct irq_domain *parent,
				     struct acpi_madt_eio_pic *acpi_eiointc)
{
	int parent_irq, ret;
	struct eiointc_priv *priv;
	int node;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->domain_handle = irq_domain_alloc_named_id_fwnode("EIOPIC",
							       acpi_eiointc->node);
	if (!priv->domain_handle) {
		pr_err("Unable to allocate domain handle\n");
		goto out_free_priv;
	}

	priv->vec_count = VEC_COUNT;
	priv->node = acpi_eiointc->node;

	parent_irq = irq_create_mapping(parent, acpi_eiointc->cascade);

	ret = eiointc_init(priv, parent_irq, acpi_eiointc->node_map);
	if (ret < 0)
		goto out_free_handle;

	if (cpu_has_flatmode)
		node = cpu_to_node(acpi_eiointc->node * CORES_PER_EIO_NODE);
	else
		node = acpi_eiointc->node;
	acpi_set_vec_parent(node, priv->eiointc_domain, pch_group);
	acpi_set_vec_parent(node, priv->eiointc_domain, msi_group);

	ret = acpi_cascade_irqdomain_init();
	if (ret < 0)
		goto out_free_handle;

	return ret;

out_free_handle:
	irq_domain_free_fwnode(priv->domain_handle);
	priv->domain_handle = NULL;
out_free_priv:
	kfree(priv);

	return -ENOMEM;
}

static int __init eiointc_of_init(struct device_node *of_node,
				  struct device_node *parent)
{
	int parent_irq, ret;
	struct eiointc_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	parent_irq = irq_of_parse_and_map(of_node, 0);
	if (parent_irq <= 0) {
		ret = -ENODEV;
		goto out_free_priv;
	}

	ret = irq_set_handler_data(parent_irq, priv);
	if (ret < 0)
		goto out_free_priv;

	/*
	 * In particular, the number of devices supported by the LS2K0500
	 * extended I/O interrupt vector is 128.
	 */
	if (of_device_is_compatible(of_node, "loongson,ls2k0500-eiointc"))
		priv->vec_count = 128;
	else
		priv->vec_count = VEC_COUNT;

	priv->node = 0;
	priv->domain_handle = of_node_to_fwnode(of_node);

	ret = eiointc_init(priv, parent_irq, 0);
	if (ret < 0)
		goto out_free_priv;

	return 0;

out_free_priv:
	kfree(priv);
	return ret;
}

IRQCHIP_DECLARE(loongson_ls2k0500_eiointc, "loongson,ls2k0500-eiointc", eiointc_of_init);
IRQCHIP_DECLARE(loongson_ls2k2000_eiointc, "loongson,ls2k2000-eiointc", eiointc_of_init);
