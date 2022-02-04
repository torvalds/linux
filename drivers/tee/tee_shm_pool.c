// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, 2017, 2022 Linaro Limited
 */
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include "tee_private.h"

static int pool_op_gen_alloc(struct tee_shm_pool *pool, struct tee_shm *shm,
			     size_t size, size_t align)
{
	unsigned long va;
	struct gen_pool *genpool = pool->private_data;
	size_t a = max_t(size_t, align, BIT(genpool->min_alloc_order));
	struct genpool_data_align data = { .align = a };
	size_t s = roundup(size, a);

	va = gen_pool_alloc_algo(genpool, s, gen_pool_first_fit_align, &data);
	if (!va)
		return -ENOMEM;

	memset((void *)va, 0, s);
	shm->kaddr = (void *)va;
	shm->paddr = gen_pool_virt_to_phys(genpool, va);
	shm->size = s;
	/*
	 * This is from a static shared memory pool so no need to register
	 * each chunk, and no need to unregister later either.
	 */
	shm->flags &= ~TEE_SHM_REGISTER;
	return 0;
}

static void pool_op_gen_free(struct tee_shm_pool *pool, struct tee_shm *shm)
{
	gen_pool_free(pool->private_data, (unsigned long)shm->kaddr,
		      shm->size);
	shm->kaddr = NULL;
}

static void pool_op_gen_destroy_pool(struct tee_shm_pool *pool)
{
	gen_pool_destroy(pool->private_data);
	kfree(pool);
}

static const struct tee_shm_pool_ops pool_ops_generic = {
	.alloc = pool_op_gen_alloc,
	.free = pool_op_gen_free,
	.destroy_pool = pool_op_gen_destroy_pool,
};

struct tee_shm_pool *tee_shm_pool_alloc_res_mem(unsigned long vaddr,
						phys_addr_t paddr, size_t size,
						int min_alloc_order)
{
	const size_t page_mask = PAGE_SIZE - 1;
	struct tee_shm_pool *pool;
	int rc;

	/* Start and end must be page aligned */
	if (vaddr & page_mask || paddr & page_mask || size & page_mask)
		return ERR_PTR(-EINVAL);

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->private_data = gen_pool_create(min_alloc_order, -1);
	if (!pool->private_data) {
		rc = -ENOMEM;
		goto err;
	}

	rc = gen_pool_add_virt(pool->private_data, vaddr, paddr, size, -1);
	if (rc) {
		gen_pool_destroy(pool->private_data);
		goto err;
	}

	pool->ops = &pool_ops_generic;

	return pool;
err:
	kfree(pool);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(tee_shm_pool_alloc_res_mem);
