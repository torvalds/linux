// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests relating directly to heap memory, including
 * page allocation and slab allocations.
 */
#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>

static struct kmem_cache *double_free_cache;
static struct kmem_cache *a_cache;
static struct kmem_cache *b_cache;

/*
 * Using volatile here means the compiler cannot ever make assumptions
 * about this value. This means compile-time length checks involving
 * this variable cannot be performed; only run-time checks.
 */
static volatile int __offset = 1;

/*
 * If there aren't guard pages, it's likely that a consecutive allocation will
 * let us overflow into the second allocation without overwriting something real.
 *
 * This should always be caught because there is an unconditional unmapped
 * page after vmap allocations.
 */
static void lkdtm_VMALLOC_LINEAR_OVERFLOW(void)
{
	char *one, *two;

	one = vzalloc(PAGE_SIZE);
	two = vzalloc(PAGE_SIZE);

	pr_info("Attempting vmalloc linear overflow ...\n");
	memset(one, 0xAA, PAGE_SIZE + __offset);

	vfree(two);
	vfree(one);
}

/*
 * This tries to stay within the next largest power-of-2 kmalloc cache
 * to avoid actually overwriting anything important if it's not detected
 * correctly.
 *
 * This should get caught by either memory tagging, KASan, or by using
 * CONFIG_SLUB_DEBUG=y and slub_debug=ZF (or CONFIG_SLUB_DEBUG_ON=y).
 */
static void lkdtm_SLAB_LINEAR_OVERFLOW(void)
{
	size_t len = 1020;
	u32 *data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return;

	pr_info("Attempting slab linear overflow ...\n");
	OPTIMIZER_HIDE_VAR(data);
	data[1024 / sizeof(u32)] = 0x12345678;
	kfree(data);
}

static void lkdtm_WRITE_AFTER_FREE(void)
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

static void lkdtm_READ_AFTER_FREE(void)
{
	int *base, *val, saw;
	size_t len = 1024;
	/*
	 * The slub allocator will use the either the first word or
	 * the middle of the allocation to store the free pointer,
	 * depending on configurations. Store in the second word to
	 * avoid running into the freelist.
	 */
	size_t offset = sizeof(*base);

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
	} else {
		pr_err("FAIL: Memory was not poisoned!\n");
		pr_expected_config_param(CONFIG_INIT_ON_FREE_DEFAULT_ON, "init_on_free");
	}

	kfree(val);
}

static void lkdtm_WRITE_BUDDY_AFTER_FREE(void)
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

static void lkdtm_READ_BUDDY_AFTER_FREE(void)
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
	} else {
		pr_err("FAIL: Buddy page was not poisoned!\n");
		pr_expected_config_param(CONFIG_INIT_ON_FREE_DEFAULT_ON, "init_on_free");
	}

	kfree(val);
}

static void lkdtm_SLAB_INIT_ON_ALLOC(void)
{
	u8 *first;
	u8 *val;

	first = kmalloc(512, GFP_KERNEL);
	if (!first) {
		pr_info("Unable to allocate 512 bytes the first time.\n");
		return;
	}

	memset(first, 0xAB, 512);
	kfree(first);

	val = kmalloc(512, GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate 512 bytes the second time.\n");
		return;
	}
	if (val != first) {
		pr_warn("Reallocation missed clobbered memory.\n");
	}

	if (memchr(val, 0xAB, 512) == NULL) {
		pr_info("Memory appears initialized (%x, no earlier values)\n", *val);
	} else {
		pr_err("FAIL: Slab was not initialized\n");
		pr_expected_config_param(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, "init_on_alloc");
	}
	kfree(val);
}

static void lkdtm_BUDDY_INIT_ON_ALLOC(void)
{
	u8 *first;
	u8 *val;

	first = (u8 *)__get_free_page(GFP_KERNEL);
	if (!first) {
		pr_info("Unable to allocate first free page\n");
		return;
	}

	memset(first, 0xAB, PAGE_SIZE);
	free_page((unsigned long)first);

	val = (u8 *)__get_free_page(GFP_KERNEL);
	if (!val) {
		pr_info("Unable to allocate second free page\n");
		return;
	}

	if (val != first) {
		pr_warn("Reallocation missed clobbered memory.\n");
	}

	if (memchr(val, 0xAB, PAGE_SIZE) == NULL) {
		pr_info("Memory appears initialized (%x, no earlier values)\n", *val);
	} else {
		pr_err("FAIL: Slab was not initialized\n");
		pr_expected_config_param(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, "init_on_alloc");
	}
	free_page((unsigned long)val);
}

static void lkdtm_SLAB_FREE_DOUBLE(void)
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

static void lkdtm_SLAB_FREE_CROSS(void)
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

static void lkdtm_SLAB_FREE_PAGE(void)
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

static struct crashtype crashtypes[] = {
	CRASHTYPE(SLAB_LINEAR_OVERFLOW),
	CRASHTYPE(VMALLOC_LINEAR_OVERFLOW),
	CRASHTYPE(WRITE_AFTER_FREE),
	CRASHTYPE(READ_AFTER_FREE),
	CRASHTYPE(WRITE_BUDDY_AFTER_FREE),
	CRASHTYPE(READ_BUDDY_AFTER_FREE),
	CRASHTYPE(SLAB_INIT_ON_ALLOC),
	CRASHTYPE(BUDDY_INIT_ON_ALLOC),
	CRASHTYPE(SLAB_FREE_DOUBLE),
	CRASHTYPE(SLAB_FREE_CROSS),
	CRASHTYPE(SLAB_FREE_PAGE),
};

struct crashtype_category heap_crashtypes = {
	.crashtypes = crashtypes,
	.len	    = ARRAY_SIZE(crashtypes),
};
