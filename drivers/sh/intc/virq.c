/*
 * Support for virtual IRQ subgroups.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "intc: " fmt

#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/spinlock.h>
#include "internals.h"

static struct intc_map_entry intc_irq_xlate[NR_IRQS];

struct intc_virq_list {
	unsigned int irq;
	struct intc_virq_list *next;
};

#define for_each_virq(entry, head) \
	for (entry = head; entry; entry = entry->next)

/*
 * Tags for the radix tree
 */
#define INTC_TAG_VIRQ_NEEDS_ALLOC	0

void intc_irq_xlate_set(unsigned int irq, intc_enum id, struct intc_desc_int *d)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&intc_big_lock, flags);
	intc_irq_xlate[irq].enum_id = id;
	intc_irq_xlate[irq].desc = d;
	raw_spin_unlock_irqrestore(&intc_big_lock, flags);
}

struct intc_map_entry *intc_irq_xlate_get(unsigned int irq)
{
	return intc_irq_xlate + irq;
}

int intc_irq_lookup(const char *chipname, intc_enum enum_id)
{
	struct intc_map_entry *ptr;
	struct intc_desc_int *d;
	int irq = -1;

	list_for_each_entry(d, &intc_list, list) {
		int tagged;

		if (strcmp(d->chip.name, chipname) != 0)
			continue;

		/*
		 * Catch early lookups for subgroup VIRQs that have not
		 * yet been allocated an IRQ. This already includes a
		 * fast-path out if the tree is untagged, so there is no
		 * need to explicitly test the root tree.
		 */
		tagged = radix_tree_tag_get(&d->tree, enum_id,
					    INTC_TAG_VIRQ_NEEDS_ALLOC);
		if (unlikely(tagged))
			break;

		ptr = radix_tree_lookup(&d->tree, enum_id);
		if (ptr) {
			irq = ptr - intc_irq_xlate;
			break;
		}
	}

	return irq;
}
EXPORT_SYMBOL_GPL(intc_irq_lookup);

static int add_virq_to_pirq(unsigned int irq, unsigned int virq)
{
	struct intc_virq_list **last, *entry;
	struct irq_data *data = irq_get_irq_data(irq);

	/* scan for duplicates */
	last = (struct intc_virq_list **)&data->handler_data;
	for_each_virq(entry, data->handler_data) {
		if (entry->irq == virq)
			return 0;
		last = &entry->next;
	}

	entry = kzalloc(sizeof(struct intc_virq_list), GFP_ATOMIC);
	if (!entry) {
		pr_err("can't allocate VIRQ mapping for %d\n", virq);
		return -ENOMEM;
	}

	entry->irq = virq;

	*last = entry;

	return 0;
}

static void intc_virq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	struct intc_virq_list *entry, *vlist = irq_data_get_irq_data(data);
	struct intc_desc_int *d = get_intc_desc(irq);

	chip->irq_mask_ack(data);

	for_each_virq(entry, vlist) {
		unsigned long addr, handle;

		handle = (unsigned long)get_irq_data(entry->irq);
		addr = INTC_REG(d, _INTC_ADDR_E(handle), 0);

		if (intc_reg_fns[_INTC_FN(handle)](addr, handle, 0))
			generic_handle_irq(entry->irq);
	}

	chip->irq_unmask(data);
}

static unsigned long __init intc_subgroup_data(struct intc_subgroup *subgroup,
					       struct intc_desc_int *d,
					       unsigned int index)
{
	unsigned int fn = REG_FN_TEST_BASE + (subgroup->reg_width >> 3) - 1;

	return _INTC_MK(fn, MODE_ENABLE_REG, intc_get_reg(d, subgroup->reg),
			0, 1, (subgroup->reg_width - 1) - index);
}

static void __init intc_subgroup_init_one(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  struct intc_subgroup *subgroup)
{
	struct intc_map_entry *mapped;
	unsigned int pirq;
	unsigned long flags;
	int i;

	mapped = radix_tree_lookup(&d->tree, subgroup->parent_id);
	if (!mapped) {
		WARN_ON(1);
		return;
	}

	pirq = mapped - intc_irq_xlate;

	raw_spin_lock_irqsave(&d->lock, flags);

	for (i = 0; i < ARRAY_SIZE(subgroup->enum_ids); i++) {
		struct intc_subgroup_entry *entry;
		int err;

		if (!subgroup->enum_ids[i])
			continue;

		entry = kmalloc(sizeof(*entry), GFP_NOWAIT);
		if (!entry)
			break;

		entry->pirq = pirq;
		entry->enum_id = subgroup->enum_ids[i];
		entry->handle = intc_subgroup_data(subgroup, d, i);

		err = radix_tree_insert(&d->tree, entry->enum_id, entry);
		if (unlikely(err < 0))
			break;

		radix_tree_tag_set(&d->tree, entry->enum_id,
				   INTC_TAG_VIRQ_NEEDS_ALLOC);
	}

	raw_spin_unlock_irqrestore(&d->lock, flags);
}

void __init intc_subgroup_init(struct intc_desc *desc, struct intc_desc_int *d)
{
	int i;

	if (!desc->hw.subgroups)
		return;

	for (i = 0; i < desc->hw.nr_subgroups; i++)
		intc_subgroup_init_one(desc, d, desc->hw.subgroups + i);
}

static void __init intc_subgroup_map(struct intc_desc_int *d)
{
	struct intc_subgroup_entry *entries[32];
	unsigned long flags;
	unsigned int nr_found;
	int i;

	raw_spin_lock_irqsave(&d->lock, flags);

restart:
	nr_found = radix_tree_gang_lookup_tag_slot(&d->tree,
			(void ***)entries, 0, ARRAY_SIZE(entries),
			INTC_TAG_VIRQ_NEEDS_ALLOC);

	for (i = 0; i < nr_found; i++) {
		struct intc_subgroup_entry *entry;
		int irq;

		entry = radix_tree_deref_slot((void **)entries[i]);
		if (unlikely(!entry))
			continue;
		if (radix_tree_deref_retry(entry))
			goto restart;

		irq = create_irq();
		if (unlikely(irq < 0)) {
			pr_err("no more free IRQs, bailing..\n");
			break;
		}

		pr_info("Setting up a chained VIRQ from %d -> %d\n",
			irq, entry->pirq);

		intc_irq_xlate_set(irq, entry->enum_id, d);

		set_irq_chip_and_handler_name(irq, get_irq_chip(entry->pirq),
					      handle_simple_irq, "virq");
		set_irq_chip_data(irq, get_irq_chip_data(entry->pirq));

		set_irq_data(irq, (void *)entry->handle);

		set_irq_chained_handler(entry->pirq, intc_virq_handler);
		add_virq_to_pirq(entry->pirq, irq);

		radix_tree_tag_clear(&d->tree, entry->enum_id,
				     INTC_TAG_VIRQ_NEEDS_ALLOC);
		radix_tree_replace_slot((void **)entries[i],
					&intc_irq_xlate[irq]);
	}

	raw_spin_unlock_irqrestore(&d->lock, flags);
}

void __init intc_finalize(void)
{
	struct intc_desc_int *d;

	list_for_each_entry(d, &intc_list, list)
		if (radix_tree_tagged(&d->tree, INTC_TAG_VIRQ_NEEDS_ALLOC))
			intc_subgroup_map(d);
}
