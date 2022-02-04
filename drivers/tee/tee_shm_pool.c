// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Linaro Limited
 */
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include "tee_private.h"

static int pool_op_gen_alloc(struct tee_shm_pool_mgr *poolm,
			     struct tee_shm *shm, size_t size)
{
	unsigned long va;
	struct gen_pool *genpool = poolm->private_data;
	size_t s = roundup(size, 1 << genpool->min_alloc_order);

	va = gen_pool_alloc(genpool, s);
	if (!va)
		return -ENOMEM;

	memset((void *)va, 0, s);
	shm->kaddr = (void *)va;
	shm->paddr = gen_pool_virt_to_phys(genpool, va);
	shm->size = s;
	return 0;
}

static void pool_op_gen_free(struct tee_shm_pool_mgr *poolm,
			     struct tee_shm *shm)
{
	gen_pool_free(poolm->private_data, (unsigned long)shm->kaddr,
		      shm->size);
	shm->kaddr = NULL;
}

static void pool_op_gen_destroy_poolmgr(struct tee_shm_pool_mgr *poolm)
{
	gen_pool_destroy(poolm->private_data);
	kfree(poolm);
}

static const struct tee_shm_pool_mgr_ops pool_ops_generic = {
	.alloc = pool_op_gen_alloc,
	.free = pool_op_gen_free,
	.destroy_poolmgr = pool_op_gen_destroy_poolmgr,
};

struct tee_shm_pool_mgr *tee_shm_pool_mgr_alloc_res_mem(unsigned long vaddr,
							phys_addr_t paddr,
							size_t size,
							int min_alloc_order)
{
	const size_t page_mask = PAGE_SIZE - 1;
	struct tee_shm_pool_mgr *mgr;
	int rc;

	/* Start and end must be page aligned */
	if (vaddr & page_mask || paddr & page_mask || size & page_mask)
		return ERR_PTR(-EINVAL);

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	mgr->private_data = gen_pool_create(min_alloc_order, -1);
	if (!mgr->private_data) {
		rc = -ENOMEM;
		goto err;
	}

	gen_pool_set_algo(mgr->private_data, gen_pool_best_fit, NULL);
	rc = gen_pool_add_virt(mgr->private_data, vaddr, paddr, size, -1);
	if (rc) {
		gen_pool_destroy(mgr->private_data);
		goto err;
	}

	mgr->ops = &pool_ops_generic;

	return mgr;
err:
	kfree(mgr);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(tee_shm_pool_mgr_alloc_res_mem);

static bool check_mgr_ops(struct tee_shm_pool_mgr *mgr)
{
	return mgr && mgr->ops && mgr->ops->alloc && mgr->ops->free &&
		mgr->ops->destroy_poolmgr;
}

struct tee_shm_pool *tee_shm_pool_alloc(struct tee_shm_pool_mgr *priv_mgr,
					struct tee_shm_pool_mgr *dmabuf_mgr)
{
	struct tee_shm_pool *pool;

	if (!check_mgr_ops(priv_mgr) || !check_mgr_ops(dmabuf_mgr))
		return ERR_PTR(-EINVAL);

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->private_mgr = priv_mgr;
	pool->dma_buf_mgr = dmabuf_mgr;

	return pool;
}
EXPORT_SYMBOL_GPL(tee_shm_pool_alloc);

/**
 * tee_shm_pool_free() - Free a shared memory pool
 * @pool:	The shared memory pool to free
 *
 * There must be no remaining shared memory allocated from this pool when
 * this function is called.
 */
void tee_shm_pool_free(struct tee_shm_pool *pool)
{
	if (pool->private_mgr)
		tee_shm_pool_mgr_destroy(pool->private_mgr);
	if (pool->dma_buf_mgr)
		tee_shm_pool_mgr_destroy(pool->dma_buf_mgr);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(tee_shm_pool_free);
