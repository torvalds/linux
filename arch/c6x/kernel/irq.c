/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *
 *  This borrows heavily from powerpc version, which is:
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/megamod-pic.h>

unsigned long irq_err_count;

static DEFINE_RAW_SPINLOCK(core_irq_lock);

static void mask_core_irq(struct irq_data *data)
{
	unsigned int prio = data->irq;

	BUG_ON(prio < 4 || prio >= NR_PRIORITY_IRQS);

	raw_spin_lock(&core_irq_lock);
	and_creg(IER, ~(1 << prio));
	raw_spin_unlock(&core_irq_lock);
}

static void unmask_core_irq(struct irq_data *data)
{
	unsigned int prio = data->irq;

	raw_spin_lock(&core_irq_lock);
	or_creg(IER, 1 << prio);
	raw_spin_unlock(&core_irq_lock);
}

static struct irq_chip core_chip = {
	.name		= "core",
	.irq_mask	= mask_core_irq,
	.irq_unmask	= unmask_core_irq,
};

asmlinkage void c6x_do_IRQ(unsigned int prio, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	BUG_ON(prio < 4 || prio >= NR_PRIORITY_IRQS);

	generic_handle_irq(prio);

	irq_exit();

	set_irq_regs(old_regs);
}

static struct irq_host *core_host;

static int core_host_map(struct irq_host *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	if (hw < 4 || hw >= NR_PRIORITY_IRQS)
		return -EINVAL;

	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &core_chip, handle_level_irq);
	return 0;
}

static struct irq_host_ops core_host_ops = {
	.map = core_host_map,
};

void __init init_IRQ(void)
{
	struct device_node *np;

	/* Mask all priority IRQs */
	and_creg(IER, ~0xfff0);

	np = of_find_compatible_node(NULL, NULL, "ti,c64x+core-pic");
	if (np != NULL) {
		/* create the core host */
		core_host = irq_alloc_host(np, IRQ_HOST_MAP_PRIORITY, 0,
					   &core_host_ops, 0);
		if (core_host)
			irq_set_default_host(core_host);
		of_node_put(np);
	}

	printk(KERN_INFO "Core interrupt controller initialized\n");

	/* now we're ready for other SoC controllers */
	megamod_pic_init();

	/* Clear all general IRQ flags */
	set_creg(ICR, 0xfff0);
}

void ack_bad_irq(int irq)
{
	printk(KERN_ERR "IRQ: spurious interrupt %d\n", irq);
	irq_err_count++;
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	seq_printf(p, "%*s: %10lu\n", prec, "Err", irq_err_count);
	return 0;
}

/*
 * IRQ controller and virtual interrupts
 */

/* The main irq map itself is an array of NR_IRQ entries containing the
 * associate host and irq number. An entry with a host of NULL is free.
 * An entry can be allocated if it's free, the allocator always then sets
 * hwirq first to the host's invalid irq number and then fills ops.
 */
struct irq_map_entry {
	irq_hw_number_t	hwirq;
	struct irq_host	*host;
};

static LIST_HEAD(irq_hosts);
static DEFINE_RAW_SPINLOCK(irq_big_lock);
static DEFINE_MUTEX(revmap_trees_mutex);
static struct irq_map_entry irq_map[NR_IRQS];
static unsigned int irq_virq_count = NR_IRQS;
static struct irq_host *irq_default_host;

irq_hw_number_t irqd_to_hwirq(struct irq_data *d)
{
	return irq_map[d->irq].hwirq;
}
EXPORT_SYMBOL_GPL(irqd_to_hwirq);

irq_hw_number_t virq_to_hw(unsigned int virq)
{
	return irq_map[virq].hwirq;
}
EXPORT_SYMBOL_GPL(virq_to_hw);

bool virq_is_host(unsigned int virq, struct irq_host *host)
{
	return irq_map[virq].host == host;
}
EXPORT_SYMBOL_GPL(virq_is_host);

static int default_irq_host_match(struct irq_host *h, struct device_node *np)
{
	return h->of_node != NULL && h->of_node == np;
}

struct irq_host *irq_alloc_host(struct device_node *of_node,
				unsigned int revmap_type,
				unsigned int revmap_arg,
				struct irq_host_ops *ops,
				irq_hw_number_t inval_irq)
{
	struct irq_host *host;
	unsigned int size = sizeof(struct irq_host);
	unsigned int i;
	unsigned int *rmap;
	unsigned long flags;

	/* Allocate structure and revmap table if using linear mapping */
	if (revmap_type == IRQ_HOST_MAP_LINEAR)
		size += revmap_arg * sizeof(unsigned int);
	host = kzalloc(size, GFP_KERNEL);
	if (host == NULL)
		return NULL;

	/* Fill structure */
	host->revmap_type = revmap_type;
	host->inval_irq = inval_irq;
	host->ops = ops;
	host->of_node = of_node_get(of_node);

	if (host->ops->match == NULL)
		host->ops->match = default_irq_host_match;

	raw_spin_lock_irqsave(&irq_big_lock, flags);

	/* Check for the priority controller. */
	if (revmap_type == IRQ_HOST_MAP_PRIORITY) {
		if (irq_map[0].host != NULL) {
			raw_spin_unlock_irqrestore(&irq_big_lock, flags);
			of_node_put(host->of_node);
			kfree(host);
			return NULL;
		}
		irq_map[0].host = host;
	}

	list_add(&host->link, &irq_hosts);
	raw_spin_unlock_irqrestore(&irq_big_lock, flags);

	/* Additional setups per revmap type */
	switch (revmap_type) {
	case IRQ_HOST_MAP_PRIORITY:
		/* 0 is always the invalid number for priority */
		host->inval_irq = 0;
		/* setup us as the host for all priority interrupts */
		for (i = 1; i < NR_PRIORITY_IRQS; i++) {
			irq_map[i].hwirq = i;
			smp_wmb();
			irq_map[i].host = host;
			smp_wmb();

			ops->map(host, i, i);
		}
		break;
	case IRQ_HOST_MAP_LINEAR:
		rmap = (unsigned int *)(host + 1);
		for (i = 0; i < revmap_arg; i++)
			rmap[i] = NO_IRQ;
		host->revmap_data.linear.size = revmap_arg;
		smp_wmb();
		host->revmap_data.linear.revmap = rmap;
		break;
	case IRQ_HOST_MAP_TREE:
		INIT_RADIX_TREE(&host->revmap_data.tree, GFP_KERNEL);
		break;
	default:
		break;
	}

	pr_debug("irq: Allocated host of type %d @0x%p\n", revmap_type, host);

	return host;
}

struct irq_host *irq_find_host(struct device_node *node)
{
	struct irq_host *h, *found = NULL;
	unsigned long flags;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	raw_spin_lock_irqsave(&irq_big_lock, flags);
	list_for_each_entry(h, &irq_hosts, link)
		if (h->ops->match(h, node)) {
			found = h;
			break;
		}
	raw_spin_unlock_irqrestore(&irq_big_lock, flags);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_host);

void irq_set_default_host(struct irq_host *host)
{
	pr_debug("irq: Default host set to @0x%p\n", host);

	irq_default_host = host;
}

void irq_set_virq_count(unsigned int count)
{
	pr_debug("irq: Trying to set virq count to %d\n", count);

	BUG_ON(count < NR_PRIORITY_IRQS);
	if (count < NR_IRQS)
		irq_virq_count = count;
}

static int irq_setup_virq(struct irq_host *host, unsigned int virq,
			    irq_hw_number_t hwirq)
{
	int res;

	res = irq_alloc_desc_at(virq, 0);
	if (res != virq) {
		pr_debug("irq: -> allocating desc failed\n");
		goto error;
	}

	/* map it */
	smp_wmb();
	irq_map[virq].hwirq = hwirq;
	smp_mb();

	if (host->ops->map(host, virq, hwirq)) {
		pr_debug("irq: -> mapping failed, freeing\n");
		goto errdesc;
	}

	irq_clear_status_flags(virq, IRQ_NOREQUEST);

	return 0;

errdesc:
	irq_free_descs(virq, 1);
error:
	irq_free_virt(virq, 1);
	return -1;
}

unsigned int irq_create_direct_mapping(struct irq_host *host)
{
	unsigned int virq;

	if (host == NULL)
		host = irq_default_host;

	BUG_ON(host == NULL);
	WARN_ON(host->revmap_type != IRQ_HOST_MAP_NOMAP);

	virq = irq_alloc_virt(host, 1, 0);
	if (virq == NO_IRQ) {
		pr_debug("irq: create_direct virq allocation failed\n");
		return NO_IRQ;
	}

	pr_debug("irq: create_direct obtained virq %d\n", virq);

	if (irq_setup_virq(host, virq, virq))
		return NO_IRQ;

	return virq;
}

unsigned int irq_create_mapping(struct irq_host *host,
				irq_hw_number_t hwirq)
{
	unsigned int virq, hint;

	pr_debug("irq: irq_create_mapping(0x%p, 0x%lx)\n", host, hwirq);

	/* Look for default host if nececssary */
	if (host == NULL)
		host = irq_default_host;
	if (host == NULL) {
		printk(KERN_WARNING "irq_create_mapping called for"
		       " NULL host, hwirq=%lx\n", hwirq);
		WARN_ON(1);
		return NO_IRQ;
	}
	pr_debug("irq: -> using host @%p\n", host);

	/* Check if mapping already exists */
	virq = irq_find_mapping(host, hwirq);
	if (virq != NO_IRQ) {
		pr_debug("irq: -> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Allocate a virtual interrupt number */
	hint = hwirq % irq_virq_count;
	virq = irq_alloc_virt(host, 1, hint);
	if (virq == NO_IRQ) {
		pr_debug("irq: -> virq allocation failed\n");
		return NO_IRQ;
	}

	if (irq_setup_virq(host, virq, hwirq))
		return NO_IRQ;

	pr_debug("irq: irq %lu on host %s mapped to virtual irq %u\n",
		hwirq, host->of_node ? host->of_node->full_name : "null", virq);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_host *host;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	unsigned int virq;

	if (controller == NULL)
		host = irq_default_host;
	else
		host = irq_find_host(controller);
	if (host == NULL) {
		printk(KERN_WARNING "irq: no irq host found for %s !\n",
		       controller->full_name);
		return NO_IRQ;
	}

	/* If host has no translation, then we assume interrupt line */
	if (host->ops->xlate == NULL)
		hwirq = intspec[0];
	else {
		if (host->ops->xlate(host, controller, intspec, intsize,
				     &hwirq, &type))
			return NO_IRQ;
	}

	/* Create mapping */
	virq = irq_create_mapping(host, hwirq);
	if (virq == NO_IRQ)
		return virq;

	/* Set type if specified and different than the current one */
	if (type != IRQ_TYPE_NONE &&
	    type != (irqd_get_trigger_type(irq_get_irq_data(virq))))
		irq_set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

void irq_dispose_mapping(unsigned int virq)
{
	struct irq_host *host;
	irq_hw_number_t hwirq;

	if (virq == NO_IRQ)
		return;

	/* Never unmap priority interrupts */
	if (virq < NR_PRIORITY_IRQS)
		return;

	host = irq_map[virq].host;
	if (WARN_ON(host == NULL))
		return;

	irq_set_status_flags(virq, IRQ_NOREQUEST);

	/* remove chip and handler */
	irq_set_chip_and_handler(virq, NULL, NULL);

	/* Make sure it's completed */
	synchronize_irq(virq);

	/* Tell the PIC about it */
	if (host->ops->unmap)
		host->ops->unmap(host, virq);
	smp_mb();

	/* Clear reverse map */
	hwirq = irq_map[virq].hwirq;
	switch (host->revmap_type) {
	case IRQ_HOST_MAP_LINEAR:
		if (hwirq < host->revmap_data.linear.size)
			host->revmap_data.linear.revmap[hwirq] = NO_IRQ;
		break;
	case IRQ_HOST_MAP_TREE:
		mutex_lock(&revmap_trees_mutex);
		radix_tree_delete(&host->revmap_data.tree, hwirq);
		mutex_unlock(&revmap_trees_mutex);
		break;
	}

	/* Destroy map */
	smp_mb();
	irq_map[virq].hwirq = host->inval_irq;

	irq_free_descs(virq, 1);
	/* Free it */
	irq_free_virt(virq, 1);
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

unsigned int irq_find_mapping(struct irq_host *host,
			      irq_hw_number_t hwirq)
{
	unsigned int i;
	unsigned int hint = hwirq % irq_virq_count;

	/* Look for default host if nececssary */
	if (host == NULL)
		host = irq_default_host;
	if (host == NULL)
		return NO_IRQ;

	/* Slow path does a linear search of the map */
	i = hint;
	do  {
		if (irq_map[i].host == host &&
		    irq_map[i].hwirq == hwirq)
			return i;
		i++;
		if (i >= irq_virq_count)
			i = 4;
	} while (i != hint);
	return NO_IRQ;
}
EXPORT_SYMBOL_GPL(irq_find_mapping);

unsigned int irq_radix_revmap_lookup(struct irq_host *host,
				     irq_hw_number_t hwirq)
{
	struct irq_map_entry *ptr;
	unsigned int virq;

	if (WARN_ON_ONCE(host->revmap_type != IRQ_HOST_MAP_TREE))
		return irq_find_mapping(host, hwirq);

	/*
	 * The ptr returned references the static global irq_map.
	 * but freeing an irq can delete nodes along the path to
	 * do the lookup via call_rcu.
	 */
	rcu_read_lock();
	ptr = radix_tree_lookup(&host->revmap_data.tree, hwirq);
	rcu_read_unlock();

	/*
	 * If found in radix tree, then fine.
	 * Else fallback to linear lookup - this should not happen in practice
	 * as it means that we failed to insert the node in the radix tree.
	 */
	if (ptr)
		virq = ptr - irq_map;
	else
		virq = irq_find_mapping(host, hwirq);

	return virq;
}

void irq_radix_revmap_insert(struct irq_host *host, unsigned int virq,
			     irq_hw_number_t hwirq)
{
	if (WARN_ON(host->revmap_type != IRQ_HOST_MAP_TREE))
		return;

	if (virq != NO_IRQ) {
		mutex_lock(&revmap_trees_mutex);
		radix_tree_insert(&host->revmap_data.tree, hwirq,
				  &irq_map[virq]);
		mutex_unlock(&revmap_trees_mutex);
	}
}

unsigned int irq_linear_revmap(struct irq_host *host,
			       irq_hw_number_t hwirq)
{
	unsigned int *revmap;

	if (WARN_ON_ONCE(host->revmap_type != IRQ_HOST_MAP_LINEAR))
		return irq_find_mapping(host, hwirq);

	/* Check revmap bounds */
	if (unlikely(hwirq >= host->revmap_data.linear.size))
		return irq_find_mapping(host, hwirq);

	/* Check if revmap was allocated */
	revmap = host->revmap_data.linear.revmap;
	if (unlikely(revmap == NULL))
		return irq_find_mapping(host, hwirq);

	/* Fill up revmap with slow path if no mapping found */
	if (unlikely(revmap[hwirq] == NO_IRQ))
		revmap[hwirq] = irq_find_mapping(host, hwirq);

	return revmap[hwirq];
}

unsigned int irq_alloc_virt(struct irq_host *host,
			    unsigned int count,
			    unsigned int hint)
{
	unsigned long flags;
	unsigned int i, j, found = NO_IRQ;

	if (count == 0 || count > (irq_virq_count - NR_PRIORITY_IRQS))
		return NO_IRQ;

	raw_spin_lock_irqsave(&irq_big_lock, flags);

	/* Use hint for 1 interrupt if any */
	if (count == 1 && hint >= NR_PRIORITY_IRQS &&
	    hint < irq_virq_count && irq_map[hint].host == NULL) {
		found = hint;
		goto hint_found;
	}

	/* Look for count consecutive numbers in the allocatable
	 * (non-legacy) space
	 */
	for (i = NR_PRIORITY_IRQS, j = 0; i < irq_virq_count; i++) {
		if (irq_map[i].host != NULL)
			j = 0;
		else
			j++;

		if (j == count) {
			found = i - count + 1;
			break;
		}
	}
	if (found == NO_IRQ) {
		raw_spin_unlock_irqrestore(&irq_big_lock, flags);
		return NO_IRQ;
	}
 hint_found:
	for (i = found; i < (found + count); i++) {
		irq_map[i].hwirq = host->inval_irq;
		smp_wmb();
		irq_map[i].host = host;
	}
	raw_spin_unlock_irqrestore(&irq_big_lock, flags);
	return found;
}

void irq_free_virt(unsigned int virq, unsigned int count)
{
	unsigned long flags;
	unsigned int i;

	WARN_ON(virq < NR_PRIORITY_IRQS);
	WARN_ON(count == 0 || (virq + count) > irq_virq_count);

	if (virq < NR_PRIORITY_IRQS) {
		if (virq + count < NR_PRIORITY_IRQS)
			return;
		count  -= NR_PRIORITY_IRQS - virq;
		virq = NR_PRIORITY_IRQS;
	}

	if (count > irq_virq_count || virq > irq_virq_count - count) {
		if (virq > irq_virq_count)
			return;
		count = irq_virq_count - virq;
	}

	raw_spin_lock_irqsave(&irq_big_lock, flags);
	for (i = virq; i < (virq + count); i++) {
		struct irq_host *host;

		host = irq_map[i].host;
		irq_map[i].hwirq = host->inval_irq;
		smp_wmb();
		irq_map[i].host = NULL;
	}
	raw_spin_unlock_irqrestore(&irq_big_lock, flags);
}

#ifdef CONFIG_VIRQ_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	struct irq_desc *desc;
	const char *p;
	static const char none[] = "none";
	void *data;
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s  %-18s  %s\n", "virq", "hwirq",
		      "chip name", "chip data", "host name");

	for (i = 1; i < nr_irqs; i++) {
		desc = irq_to_desc(i);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		if (desc->action && desc->action->handler) {
			struct irq_chip *chip;

			seq_printf(m, "%5d  ", i);
			seq_printf(m, "0x%05lx  ", irq_map[i].hwirq);

			chip = irq_desc_get_chip(desc);
			if (chip && chip->name)
				p = chip->name;
			else
				p = none;
			seq_printf(m, "%-15s  ", p);

			data = irq_desc_get_chip_data(desc);
			seq_printf(m, "0x%16p  ", data);

			if (irq_map[i].host && irq_map[i].host->of_node)
				p = irq_map[i].host->of_node->full_name;
			else
				p = none;
			seq_printf(m, "%s\n", p);
		}

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	return 0;
}

static int virq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, virq_debug_show, inode->i_private);
}

static const struct file_operations virq_debug_fops = {
	.open = virq_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init irq_debugfs_init(void)
{
	if (debugfs_create_file("virq_mapping", S_IRUGO, powerpc_debugfs_root,
				 NULL, &virq_debug_fops) == NULL)
		return -ENOMEM;

	return 0;
}
device_initcall(irq_debugfs_init);
#endif /* CONFIG_VIRQ_DEBUG */
