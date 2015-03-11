/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
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

static const struct tee_shm_pool_mgr_ops pool_ops_generic = {
	.alloc = pool_op_gen_alloc,
	.free = pool_op_gen_free,
};

static void pool_res_mem_destroy(struct tee_shm_pool *pool)
{
	gen_pool_destroy(pool->private_mgr.private_data);
	gen_pool_destroy(pool->dma_buf_mgr.private_data);
}

static int pool_res_mem_mgr_init(struct tee_shm_pool_mgr *mgr,
			struct tee_shm_pool_mem_info *info, int min_alloc_order)
{
	size_t page_mask = PAGE_SIZE - 1;
	struct gen_pool *genpool = NULL;
	int rc;

	/*
	 * Start and end must be page aligned
	 */
	if ((info->vaddr & page_mask) || (info->paddr & page_mask) ||
	    (info->size & page_mask))
		return -EINVAL;

	genpool = gen_pool_create(min_alloc_order, -1);
	if (!genpool)
		return -ENOMEM;

	gen_pool_set_algo(genpool, gen_pool_best_fit, NULL);
	rc = gen_pool_add_virt(genpool, info->vaddr, info->paddr, info->size,
			       -1);
	if (rc) {
		gen_pool_destroy(genpool);
		return rc;
	}

	mgr->private_data = genpool;
	mgr->ops = &pool_ops_generic;
	return 0;
}

struct tee_shm_pool *tee_shm_pool_alloc_res_mem(struct device *dev,
			struct tee_shm_pool_mem_info *priv_info,
			struct tee_shm_pool_mem_info *dmabuf_info)
{
	struct tee_shm_pool *pool = NULL;
	int ret;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		ret = -ENOMEM;
		goto err;
	}

	/*
	 * Create the pool for driver private shared memory
	 */
	ret = pool_res_mem_mgr_init(&pool->private_mgr, priv_info,
				    3 /* 8 byte aligned */);
	if (ret)
		goto err;

	/*
	 * Create the pool for dma_buf shared memory
	 */
	ret = pool_res_mem_mgr_init(&pool->dma_buf_mgr, dmabuf_info,
				    PAGE_SHIFT);
	if (ret)
		goto err;

	pool->destroy = pool_res_mem_destroy;
	return pool;
err:
	if (ret == -ENOMEM)
		dev_err(dev, "can't allocate memory for res_mem shared memory pool\n");
	if (pool && pool->private_mgr.private_data)
		gen_pool_destroy(pool->private_mgr.private_data);
	kfree(pool);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(tee_shm_pool_alloc_res_mem);

void tee_shm_pool_free(struct tee_shm_pool *pool)
{
	pool->destroy(pool);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(tee_shm_pool_free);
