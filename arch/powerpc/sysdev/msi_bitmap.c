/*
 * Copyright 2006-2008, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <asm/msi_bitmap.h>

int msi_bitmap_alloc_hwirqs(struct msi_bitmap *bmp, int num)
{
	unsigned long flags;
	int offset, order = get_count_order(num);

	spin_lock_irqsave(&bmp->lock, flags);
	/*
	 * This is fast, but stricter than we need. We might want to add
	 * a fallback routine which does a linear search with no alignment.
	 */
	offset = bitmap_find_free_region(bmp->bitmap, bmp->irq_count, order);
	spin_unlock_irqrestore(&bmp->lock, flags);

	pr_debug("msi_bitmap: allocated 0x%x (2^%d) at offset 0x%x\n",
		 num, order, offset);

	return offset;
}

void msi_bitmap_free_hwirqs(struct msi_bitmap *bmp, unsigned int offset,
			    unsigned int num)
{
	unsigned long flags;
	int order = get_count_order(num);

	pr_debug("msi_bitmap: freeing 0x%x (2^%d) at offset 0x%x\n",
		 num, order, offset);

	spin_lock_irqsave(&bmp->lock, flags);
	bitmap_release_region(bmp->bitmap, offset, order);
	spin_unlock_irqrestore(&bmp->lock, flags);
}

void msi_bitmap_reserve_hwirq(struct msi_bitmap *bmp, unsigned int hwirq)
{
	unsigned long flags;

	pr_debug("msi_bitmap: reserving hwirq 0x%x\n", hwirq);

	spin_lock_irqsave(&bmp->lock, flags);
	bitmap_allocate_region(bmp->bitmap, hwirq, 0);
	spin_unlock_irqrestore(&bmp->lock, flags);
}

/**
 * msi_bitmap_reserve_dt_hwirqs - Reserve irqs specified in the device tree.
 * @bmp: pointer to the MSI bitmap.
 *
 * Looks in the device tree to see if there is a property specifying which
 * irqs can be used for MSI. If found those irqs reserved in the device tree
 * are reserved in the bitmap.
 *
 * Returns 0 for success, < 0 if there was an error, and > 0 if no property
 * was found in the device tree.
 **/
int msi_bitmap_reserve_dt_hwirqs(struct msi_bitmap *bmp)
{
	int i, j, len;
	const u32 *p;

	if (!bmp->of_node)
		return 1;

	p = of_get_property(bmp->of_node, "msi-available-ranges", &len);
	if (!p) {
		pr_debug("msi_bitmap: no msi-available-ranges property " \
			 "found on %s\n", bmp->of_node->full_name);
		return 1;
	}

	if (len % (2 * sizeof(u32)) != 0) {
		printk(KERN_WARNING "msi_bitmap: Malformed msi-available-ranges"
		       " property on %s\n", bmp->of_node->full_name);
		return -EINVAL;
	}

	bitmap_allocate_region(bmp->bitmap, 0, get_count_order(bmp->irq_count));

	spin_lock(&bmp->lock);

	/* Format is: (<u32 start> <u32 count>)+ */
	len /= 2 * sizeof(u32);
	for (i = 0; i < len; i++, p += 2) {
		for (j = 0; j < *(p + 1); j++)
			bitmap_release_region(bmp->bitmap, *p + j, 0);
	}

	spin_unlock(&bmp->lock);

	return 0;
}

int msi_bitmap_alloc(struct msi_bitmap *bmp, unsigned int irq_count,
		     struct device_node *of_node)
{
	int size;

	if (!irq_count)
		return -EINVAL;

	size = BITS_TO_LONGS(irq_count) * sizeof(long);
	pr_debug("msi_bitmap: allocator bitmap size is 0x%x bytes\n", size);

	bmp->bitmap = zalloc_maybe_bootmem(size, GFP_KERNEL);
	if (!bmp->bitmap) {
		pr_debug("msi_bitmap: ENOMEM allocating allocator bitmap!\n");
		return -ENOMEM;
	}

	/* We zalloc'ed the bitmap, so all irqs are free by default */
	spin_lock_init(&bmp->lock);
	bmp->of_node = of_node_get(of_node);
	bmp->irq_count = irq_count;

	return 0;
}

void msi_bitmap_free(struct msi_bitmap *bmp)
{
	/* we can't free the bitmap we don't know if it's bootmem etc. */
	of_node_put(bmp->of_node);
	bmp->bitmap = NULL;
}

#ifdef CONFIG_MSI_BITMAP_SELFTEST

#define check(x)	\
	if (!(x)) printk("msi_bitmap: test failed at line %d\n", __LINE__);

void __init test_basics(void)
{
	struct msi_bitmap bmp;
	int i, size = 512;

	/* Can't allocate a bitmap of 0 irqs */
	check(msi_bitmap_alloc(&bmp, 0, NULL) != 0);

	/* of_node may be NULL */
	check(0 == msi_bitmap_alloc(&bmp, size, NULL));

	/* Should all be free by default */
	check(0 == bitmap_find_free_region(bmp.bitmap, size,
					   get_count_order(size)));
	bitmap_release_region(bmp.bitmap, 0, get_count_order(size));

	/* With no node, there's no msi-available-ranges, so expect > 0 */
	check(msi_bitmap_reserve_dt_hwirqs(&bmp) > 0);

	/* Should all still be free */
	check(0 == bitmap_find_free_region(bmp.bitmap, size,
					   get_count_order(size)));
	bitmap_release_region(bmp.bitmap, 0, get_count_order(size));

	/* Check we can fill it up and then no more */
	for (i = 0; i < size; i++)
		check(msi_bitmap_alloc_hwirqs(&bmp, 1) >= 0);

	check(msi_bitmap_alloc_hwirqs(&bmp, 1) < 0);

	/* Should all be allocated */
	check(bitmap_find_free_region(bmp.bitmap, size, 0) < 0);

	/* And if we free one we can then allocate another */
	msi_bitmap_free_hwirqs(&bmp, size / 2, 1);
	check(msi_bitmap_alloc_hwirqs(&bmp, 1) == size / 2);

	msi_bitmap_free(&bmp);

	/* Clients may check bitmap == NULL for "not-allocated" */
	check(bmp.bitmap == NULL);

	kfree(bmp.bitmap);
}

void __init test_of_node(void)
{
	u32 prop_data[] = { 10, 10, 25, 3, 40, 1, 100, 100, 200, 20 };
	const char *expected_str = "0-9,20-24,28-39,41-99,220-255";
	char *prop_name = "msi-available-ranges";
	char *node_name = "/fakenode";
	struct device_node of_node;
	struct property prop;
	struct msi_bitmap bmp;
	int size = 256;
	DECLARE_BITMAP(expected, size);

	/* There should really be a struct device_node allocator */
	memset(&of_node, 0, sizeof(of_node));
	kref_init(&of_node.kref);
	of_node.full_name = node_name;

	check(0 == msi_bitmap_alloc(&bmp, size, &of_node));

	/* No msi-available-ranges, so expect > 0 */
	check(msi_bitmap_reserve_dt_hwirqs(&bmp) > 0);

	/* Should all still be free */
	check(0 == bitmap_find_free_region(bmp.bitmap, size,
					   get_count_order(size)));
	bitmap_release_region(bmp.bitmap, 0, get_count_order(size));

	/* Now create a fake msi-available-ranges property */

	/* There should really .. oh whatever */
	memset(&prop, 0, sizeof(prop));
	prop.name = prop_name;
	prop.value = &prop_data;
	prop.length = sizeof(prop_data);

	of_node.properties = &prop;

	/* msi-available-ranges, so expect == 0 */
	check(msi_bitmap_reserve_dt_hwirqs(&bmp) == 0);

	/* Check we got the expected result */
	check(0 == bitmap_parselist(expected_str, expected, size));
	check(bitmap_equal(expected, bmp.bitmap, size));

	msi_bitmap_free(&bmp);
	kfree(bmp.bitmap);
}

int __init msi_bitmap_selftest(void)
{
	printk(KERN_DEBUG "Running MSI bitmap self-tests ...\n");

	test_basics();
	test_of_node();

	return 0;
}
late_initcall(msi_bitmap_selftest);
#endif /* CONFIG_MSI_BITMAP_SELFTEST */
