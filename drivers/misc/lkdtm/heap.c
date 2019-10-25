// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests relating directly to heap memory, including
 * page allocation and slab allocations.
 */
#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/sched.h>

static struct kmem_cache *double_free_cache;
static struct kmem_cache *a_cache;
static struct kmem_cache *b_cache;

/*
 * This tries to stay within the next largest power-of-2 kmalloc cache
 * to avoid actually overwriting anything important if it's not detected
 * correctly.
 */
void lkdtm_OVERWRITE_ALLOCATION(void)
{
	size_t len = 1020;
	u32 *data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return;

	data[1024 / sizeof(u32)] = 0x12345678;
	kfree(data);
}

void lkdtm_WRITE_AFTER_FREE(void)
{
	int *base, *again;
	size_t len = 1024;
	/*
	 * The slub allocator uses the first word to store the free
	 * pointer in some configurations. Use the middle of the
	 * allocation to avoid running into the freelist
	 */
	size_t offset = (len / sizeof(*base)) / 2;

	base = kmalloc(len, GFP_KERNEL);
	if (!base)
		return;
	pr_info("Allocated memory %p-%p\n", base, &base[offset * 2]);
	pr_info("Attempting bad write to freed memory at %p\n",
		&base[offset]);
	kfree(base);
	base[offset] = 0x0abcdef0;
	/* Attempt to notice the overwrite. */
	again = kmalloc(len, GFP_KERNEL);
	kfree(again);
	if (again != base)
		pr_info("Hmm, didn't get the same memory range.\n");
}

void lkdtm_READ_AFTER_FREE(void)
{
	int *base, *val, saw;
	size_t len = 1024;
	/*
	 * The slub allocator uses the first word to store the free
	 * pointer in some configurations. Use the middle of the
	 * allocation to avoid running into the freelist
	 */
	size_t offset = (len / sizeof(*base)) / 2;

	base = kmalloc(len, GFP_KERNEL);
	if (!base) {
		pr_info("Unable to allocate base memory.\n");
		return;
	}

	val = kmalloc(len, GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate val memory.\n");
		kfree(base);
		return;
	}

	*val = 0x12345678;
	base[offset] = *val;
	pr_info("Value in memory before free: %x\n", base[offset]);

	kfree(base);

	pr_info("Attempting bad read from freed memory\n");
	saw = base[offset];
	if (saw != *val) {
		/* Good! Poisoning happened, so declare a win. */
		pr_info("Memory correctly poisoned (%x)\n", saw);
		BUG();
	}
	pr_info("Memory was not poisoned\n");

	kfree(val);
}

void lkdtm_WRITE_BUDDY_AFTER_FREE(void)
{
	unsigned long p = __get_free_page(GFP_KERNEL);
	if (!p) {
		pr_info("Unable to allocate free page\n");
		return;
	}

	pr_info("Writing to the buddy page before free\n");
	memset((void *)p, 0x3, PAGE_SIZE);
	free_page(p);
	schedule();
	pr_info("Attempting bad write to the buddy page after free\n");
	memset((void *)p, 0x78, PAGE_SIZE);
	/* Attempt to notice the overwrite. */
	p = __get_free_page(GFP_KERNEL);
	free_page(p);
	schedule();
}

void lkdtm_READ_BUDDY_AFTER_FREE(void)
{
	unsigned long p = __get_free_page(GFP_KERNEL);
	int saw, *val;
	int *base;

	if (!p) {
		pr_info("Unable to allocate free page\n");
		return;
	}

	val = kmalloc(1024, GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate val memory.\n");
		free_page(p);
		return;
	}

	base = (int *)p;

	*val = 0x12345678;
	base[0] = *val;
	pr_info("Value in memory before free: %x\n", base[0]);
	free_page(p);
	pr_info("Attempting to read from freed memory\n");
	saw = base[0];
	if (saw != *val) {
		/* Good! Poisoning happened, so declare a win. */
		pr_info("Memory correctly poisoned (%x)\n", saw);
		BUG();
	}
	pr_info("Buddy page was not poisoned\n");

	kfree(val);
}

void lkdtm_SLAB_FREE_DOUBLE(void)
{
	int *val;

	val = kmem_cache_alloc(double_free_cache, GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate double_free_cache memory.\n");
		return;
	}

	/* Just make sure we got real memory. */
	*val = 0x12345678;
	pr_info("Attempting double slab free ...\n");
	kmem_cache_free(double_free_cache, val);
	kmem_cache_free(double_free_cache, val);
}

void lkdtm_SLAB_FREE_CROSS(void)
{
	int *val;

	val = kmem_cache_alloc(a_cache, GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate a_cache memory.\n");
		return;
	}

	/* Just make sure we got real memory. */
	*val = 0x12345679;
	pr_info("Attempting cross-cache slab free ...\n");
	kmem_cache_free(b_cache, val);
}

void lkdtm_SLAB_FREE_PAGE(void)
{
	unsigned long p = __get_free_page(GFP_KERNEL);

	pr_info("Attempting non-Slab slab free ...\n");
	kmem_cache_free(NULL, (void *)p);
	free_page(p);
}

/*
 * We have constructors to keep the caches distinctly separated without
 * needing to boot with "slab_nomerge".
 */
static void ctor_double_free(void *region)
{ }
static void ctor_a(void *region)
{ }
static void ctor_b(void *region)
{ }

void __init lkdtm_heap_init(void)
{
	double_free_cache = kmem_cache_create("lkdtm-heap-double_free",
					      64, 0, 0, ctor_double_free);
	a_cache = kmem_cache_create("lkdtm-heap-a", 64, 0, 0, ctor_a);
	b_cache = kmem_cache_create("lkdtm-heap-b", 64, 0, 0, ctor_b);
}

void __exit lkdtm_heap_exit(void)
{
	kmem_cache_destroy(double_free_cache);
	kmem_cache_destroy(a_cache);
	kmem_cache_destroy(b_cache);
}
