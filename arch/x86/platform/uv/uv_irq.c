/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV IRQ functions
 *
 * Copyright (C) 2008 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/export.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <asm/irqdomain.h>
#include <asm/apic.h>
#include <asm/uv/uv_irq.h>
#include <asm/uv/uv_hub.h>

/* MMR offset and pnode of hub sourcing interrupts for a given irq */
struct uv_irq_2_mmr_pnode {
	unsigned long		offset;
	int			pnode;
};

static void uv_program_mmr(struct irq_cfg *cfg, struct uv_irq_2_mmr_pnode *info)
{
	unsigned long mmr_value;
	struct uv_IO_APIC_route_entry *entry;

	BUILD_BUG_ON(sizeof(struct uv_IO_APIC_route_entry) !=
		     sizeof(unsigned long));

	mmr_value = 0;
	entry = (struct uv_IO_APIC_route_entry *)&mmr_value;
	entry->vector		= cfg->vector;
	entry->delivery_mode	= apic->delivery_mode;
	entry->dest_mode	= apic->dest_mode_logical;
	entry->polarity		= 0;
	entry->trigger		= 0;
	entry->mask		= 0;
	entry->dest		= cfg->dest_apicid;

	uv_write_global_mmr64(info->pnode, info->offset, mmr_value);
}

static void uv_noop(struct irq_data *data) { }

static int
uv_set_irq_affinity(struct irq_data *data, const struct cpumask *mask,
		    bool force)
{
	struct irq_data *parent = data->parent_data;
	struct irq_cfg *cfg = irqd_cfg(data);
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret >= 0) {
		uv_program_mmr(cfg, data->chip_data);
		send_cleanup_vector(cfg);
	}

	return ret;
}

static struct irq_chip uv_irq_chip = {
	.name			= "UV-CORE",
	.irq_mask		= uv_noop,
	.irq_unmask		= uv_noop,
	.irq_eoi		= apic_ack_irq,
	.irq_set_affinity	= uv_set_irq_affinity,
};

static int uv_domain_alloc(struct irq_domain *domain, unsigned int virq,
			   unsigned int nr_irqs, void *arg)
{
	struct uv_irq_2_mmr_pnode *chip_data;
	struct irq_alloc_info *info = arg;
	struct irq_data *irq_data = irq_domain_get_irq_data(domain, virq);
	int ret;

	if (nr_irqs > 1 || !info || info->type != X86_IRQ_ALLOC_TYPE_UV)
		return -EINVAL;

	chip_data = kmalloc_node(sizeof(*chip_data), GFP_KERNEL,
				 irq_data_get_node(irq_data));
	if (!chip_data)
		return -ENOMEM;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret >= 0) {
		if (info->uv.limit == UV_AFFINITY_CPU)
			irq_set_status_flags(virq, IRQ_NO_BALANCING);
		else
			irq_set_status_flags(virq, IRQ_MOVE_PCNTXT);

		chip_data->pnode = uv_blade_to_pnode(info->uv.blade);
		chip_data->offset = info->uv.offset;
		irq_domain_set_info(domain, virq, virq, &uv_irq_chip, chip_data,
				    handle_percpu_irq, NULL, info->uv.name);
	} else {
		kfree(chip_data);
	}

	return ret;
}

static void uv_domain_free(struct irq_domain *domain, unsigned int virq,
			   unsigned int nr_irqs)
{
	struct irq_data *irq_data = irq_domain_get_irq_data(domain, virq);

	BUG_ON(nr_irqs != 1);
	kfree(irq_data->chip_data);
	irq_clear_status_flags(virq, IRQ_MOVE_PCNTXT);
	irq_clear_status_flags(virq, IRQ_NO_BALANCING);
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

/*
 * Re-target the irq to the specified CPU and enable the specified MMR located
 * on the specified blade to allow the sending of MSIs to the specified CPU.
 */
static int uv_domain_activate(struct irq_domain *domain,
			      struct irq_data *irq_data, bool reserve)
{
	uv_program_mmr(irqd_cfg(irq_data), irq_data->chip_data);
	return 0;
}

/*
 * Disable the specified MMR located on the specified blade so that MSIs are
 * longer allowed to be sent.
 */
static void uv_domain_deactivate(struct irq_domain *domain,
				 struct irq_data *irq_data)
{
	unsigned long mmr_value;
	struct uv_IO_APIC_route_entry *entry;

	mmr_value = 0;
	entry = (struct uv_IO_APIC_route_entry *)&mmr_value;
	entry->mask = 1;
	uv_program_mmr(irqd_cfg(irq_data), irq_data->chip_data);
}

static const struct irq_domain_ops uv_domain_ops = {
	.alloc		= uv_domain_alloc,
	.free		= uv_domain_free,
	.activate	= uv_domain_activate,
	.deactivate	= uv_domain_deactivate,
};

static struct irq_domain *uv_get_irq_domain(void)
{
	static struct irq_domain *uv_domain;
	static DEFINE_MUTEX(uv_lock);
	struct fwnode_handle *fn;

	mutex_lock(&uv_lock);
	if (uv_domain)
		goto out;

	fn = irq_domain_alloc_named_fwnode("UV-CORE");
	if (!fn)
		goto out;

	uv_domain = irq_domain_create_tree(fn, &uv_domain_ops, NULL);
	if (uv_domain)
		uv_domain->parent = x86_vector_domain;
	else
		irq_domain_free_fwnode(fn);
out:
	mutex_unlock(&uv_lock);

	return uv_domain;
}

/*
 * Set up a mapping of an available irq and vector, and enable the specified
 * MMR that defines the MSI that is to be sent to the specified CPU when an
 * interrupt is raised.
 */
int uv_setup_irq(char *irq_name, int cpu, int mmr_blade,
		 unsigned long mmr_offset, int limit)
{
	struct irq_alloc_info info;
	struct irq_domain *domain = uv_get_irq_domain();

	if (!domain)
		return -ENOMEM;

	init_irq_alloc_info(&info, cpumask_of(cpu));
	info.type = X86_IRQ_ALLOC_TYPE_UV;
	info.uv.limit = limit;
	info.uv.blade = mmr_blade;
	info.uv.offset = mmr_offset;
	info.uv.name = irq_name;

	return irq_domain_alloc_irqs(domain, 1,
				     uv_blade_to_memory_nid(mmr_blade), &info);
}
EXPORT_SYMBOL_GPL(uv_setup_irq);

/*
 * Tear down a mapping of an irq and vector, and disable the specified MMR that
 * defined the MSI that was to be sent to the specified CPU when an interrupt
 * was raised.
 *
 * Set mmr_blade and mmr_offset to what was passed in on uv_setup_irq().
 */
void uv_teardown_irq(unsigned int irq)
{
	irq_domain_free_irqs(irq, 1);
}
EXPORT_SYMBOL_GPL(uv_teardown_irq);
