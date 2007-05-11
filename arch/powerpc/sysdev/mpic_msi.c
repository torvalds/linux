/*
 * Copyright 2006-2007, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/bitmap.h>
#include <linux/msi.h>
#include <asm/mpic.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>


static void __mpic_msi_reserve_hwirq(struct mpic *mpic, irq_hw_number_t hwirq)
{
	pr_debug("mpic: reserving hwirq 0x%lx\n", hwirq);
	bitmap_allocate_region(mpic->hwirq_bitmap, hwirq, 0);
}

void mpic_msi_reserve_hwirq(struct mpic *mpic, irq_hw_number_t hwirq)
{
	unsigned long flags;

	/* The mpic calls this even when there is no allocator setup */
	if (!mpic->hwirq_bitmap)
		return;

	spin_lock_irqsave(&mpic->bitmap_lock, flags);
	__mpic_msi_reserve_hwirq(mpic, hwirq);
	spin_unlock_irqrestore(&mpic->bitmap_lock, flags);
}

irq_hw_number_t mpic_msi_alloc_hwirqs(struct mpic *mpic, int num)
{
	unsigned long flags;
	int offset, order = get_count_order(num);

	spin_lock_irqsave(&mpic->bitmap_lock, flags);
	/*
	 * This is fast, but stricter than we need. We might want to add
	 * a fallback routine which does a linear search with no alignment.
	 */
	offset = bitmap_find_free_region(mpic->hwirq_bitmap, mpic->irq_count,
					 order);
	spin_unlock_irqrestore(&mpic->bitmap_lock, flags);

	pr_debug("mpic: allocated 0x%x (2^%d) at offset 0x%x\n",
		 num, order, offset);

	return offset;
}

void mpic_msi_free_hwirqs(struct mpic *mpic, int offset, int num)
{
	unsigned long flags;
	int order = get_count_order(num);

	pr_debug("mpic: freeing 0x%x (2^%d) at offset 0x%x\n",
		 num, order, offset);

	spin_lock_irqsave(&mpic->bitmap_lock, flags);
	bitmap_release_region(mpic->hwirq_bitmap, offset, order);
	spin_unlock_irqrestore(&mpic->bitmap_lock, flags);
}

#ifdef CONFIG_MPIC_U3_HT_IRQS
static int mpic_msi_reserve_u3_hwirqs(struct mpic *mpic)
{
	irq_hw_number_t hwirq;
	struct irq_host_ops *ops = mpic->irqhost->ops;
	struct device_node *np;
	int flags, index, i;
	struct of_irq oirq;

	pr_debug("mpic: found U3, guessing msi allocator setup\n");

	/* Reserve source numbers we know are reserved in the HW */
	for (i = 0;   i < 8;   i++)
		__mpic_msi_reserve_hwirq(mpic, i);

	for (i = 42;  i < 46;  i++)
		__mpic_msi_reserve_hwirq(mpic, i);

	for (i = 100; i < 105; i++)
		__mpic_msi_reserve_hwirq(mpic, i);

	np = NULL;
	while ((np = of_find_all_nodes(np))) {
		pr_debug("mpic: mapping hwirqs for %s\n", np->full_name);

		index = 0;
		while (of_irq_map_one(np, index++, &oirq) == 0) {
			ops->xlate(mpic->irqhost, NULL, oirq.specifier,
						oirq.size, &hwirq, &flags);
			__mpic_msi_reserve_hwirq(mpic, hwirq);
		}
	}

	return 0;
}
#else
static int mpic_msi_reserve_u3_hwirqs(struct mpic *mpic)
{
	return -1;
}
#endif

static int mpic_msi_reserve_dt_hwirqs(struct mpic *mpic)
{
	int i, len;
	const u32 *p;

	p = of_get_property(mpic->of_node, "msi-available-ranges", &len);
	if (!p) {
		pr_debug("mpic: no msi-available-ranges property found on %s\n",
			  mpic->of_node->full_name);
		return -ENODEV;
	}

	if (len % 8 != 0) {
		printk(KERN_WARNING "mpic: Malformed msi-available-ranges "
		       "property on %s\n", mpic->of_node->full_name);
		return -EINVAL;
	}

	bitmap_allocate_region(mpic->hwirq_bitmap, 0,
			       get_count_order(mpic->irq_count));

	/* Format is: (<u32 start> <u32 count>)+ */
	len /= sizeof(u32);
	for (i = 0; i < len / 2; i++, p += 2)
		mpic_msi_free_hwirqs(mpic, *p, *(p + 1));

	return 0;
}

int mpic_msi_init_allocator(struct mpic *mpic)
{
	int rc, size;

	BUG_ON(mpic->hwirq_bitmap);
	spin_lock_init(&mpic->bitmap_lock);

	size = BITS_TO_LONGS(mpic->irq_count) * sizeof(long);
	pr_debug("mpic: allocator bitmap size is 0x%x bytes\n", size);

	if (mem_init_done)
		mpic->hwirq_bitmap = kmalloc(size, GFP_KERNEL);
	else
		mpic->hwirq_bitmap = alloc_bootmem(size);

	if (!mpic->hwirq_bitmap) {
		pr_debug("mpic: ENOMEM allocating allocator bitmap!\n");
		return -ENOMEM;
	}

	memset(mpic->hwirq_bitmap, 0, size);

	rc = mpic_msi_reserve_dt_hwirqs(mpic);
	if (rc) {
		if (mpic->flags & MPIC_U3_HT_IRQS)
			rc = mpic_msi_reserve_u3_hwirqs(mpic);

		if (rc)
			goto out_free;
	}

	return 0;

 out_free:
	if (mem_init_done)
		kfree(mpic->hwirq_bitmap);

	mpic->hwirq_bitmap = NULL;
	return rc;
}
