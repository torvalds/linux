/*
 * Copyright (C) 2010 Imagination Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/genalloc.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <asm/tcm.h>

struct tcm_pool {
	struct list_head list;
	unsigned int tag;
	unsigned long start;
	unsigned long end;
	struct gen_pool *pool;
};

static LIST_HEAD(pool_list);

static struct tcm_pool *find_pool(unsigned int tag)
{
	struct list_head *lh;
	struct tcm_pool *pool;

	list_for_each(lh, &pool_list) {
		pool = list_entry(lh, struct tcm_pool, list);
		if (pool->tag == tag)
			return pool;
	}

	return NULL;
}

/**
 * tcm_alloc - allocate memory from a TCM pool
 * @tag: tag of the pool to allocate memory from
 * @len: number of bytes to be allocated
 *
 * Allocate the requested number of bytes from the pool matching
 * the specified tag. Returns the address of the allocated memory
 * or zero on failure.
 */
unsigned long tcm_alloc(unsigned int tag, size_t len)
{
	unsigned long vaddr;
	struct tcm_pool *pool;

	pool = find_pool(tag);
	if (!pool)
		return 0;

	vaddr = gen_pool_alloc(pool->pool, len);
	if (!vaddr)
		return 0;

	return vaddr;
}

/**
 * tcm_free - free a block of memory to a TCM pool
 * @tag: tag of the pool to free memory to
 * @addr: address of the memory to be freed
 * @len: number of bytes to be freed
 *
 * Free the requested number of bytes at a specific address to the
 * pool matching the specified tag.
 */
void tcm_free(unsigned int tag, unsigned long addr, size_t len)
{
	struct tcm_pool *pool;

	pool = find_pool(tag);
	if (!pool)
		return;
	gen_pool_free(pool->pool, addr, len);
}

/**
 * tcm_lookup_tag - find the tag matching an address
 * @p: memory address to lookup the tag for
 *
 * Find the tag of the tcm memory region that contains the
 * specified address. Returns %TCM_INVALID_TAG if no such
 * memory region could be found.
 */
unsigned int tcm_lookup_tag(unsigned long p)
{
	struct list_head *lh;
	struct tcm_pool *pool;
	unsigned long addr = (unsigned long) p;

	list_for_each(lh, &pool_list) {
		pool = list_entry(lh, struct tcm_pool, list);
		if (addr >= pool->start && addr < pool->end)
			return pool->tag;
	}

	return TCM_INVALID_TAG;
}

/**
 * tcm_add_region - add a memory region to TCM pool list
 * @reg: descriptor of region to be added
 *
 * Add a region of memory to the TCM pool list. Returns 0 on success.
 */
int __init tcm_add_region(struct tcm_region *reg)
{
	struct tcm_pool *pool;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		pr_err("Failed to alloc memory for TCM pool!\n");
		return -ENOMEM;
	}

	pool->tag = reg->tag;
	pool->start = reg->res.start;
	pool->end = reg->res.end;

	/*
	 * 2^3 = 8 bytes granularity to allow for 64bit access alignment.
	 * -1 = NUMA node specifier.
	 */
	pool->pool = gen_pool_create(3, -1);

	if (!pool->pool) {
		pr_err("Failed to create TCM pool!\n");
		kfree(pool);
		return -ENOMEM;
	}

	if (gen_pool_add(pool->pool, reg->res.start,
			 reg->res.end - reg->res.start + 1, -1)) {
		pr_err("Failed to add memory to TCM pool!\n");
		return -ENOMEM;
	}
	pr_info("Added %s TCM pool (%08x bytes @ %08x)\n",
		reg->res.name, reg->res.end - reg->res.start + 1,
		reg->res.start);

	list_add_tail(&pool->list, &pool_list);

	return 0;
}
