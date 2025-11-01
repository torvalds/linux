// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Linaro Limited
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/genalloc.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_core.h>
#include <linux/types.h>
#include "optee_private.h"

struct optee_protmem_dyn_pool {
	struct tee_protmem_pool pool;
	struct gen_pool *gen_pool;
	struct optee *optee;
	size_t page_count;
	u32 *mem_attrs;
	u_int mem_attr_count;
	refcount_t refcount;
	u32 use_case;
	struct tee_shm *protmem;
	/* Protects when initializing and tearing down this struct */
	struct mutex mutex;
};

static struct optee_protmem_dyn_pool *
to_protmem_dyn_pool(struct tee_protmem_pool *pool)
{
	return container_of(pool, struct optee_protmem_dyn_pool, pool);
}

static int init_dyn_protmem(struct optee_protmem_dyn_pool *rp)
{
	int rc;

	rp->protmem = tee_shm_alloc_dma_mem(rp->optee->ctx, rp->page_count);
	if (IS_ERR(rp->protmem)) {
		rc = PTR_ERR(rp->protmem);
		goto err_null_protmem;
	}

	/*
	 * TODO unmap the memory range since the physical memory will
	 * become inaccesible after the lend_protmem() call.
	 *
	 * If the platform supports a hypervisor at EL2, it will unmap the
	 * intermediate physical memory for us and stop cache pre-fetch of
	 * the memory.
	 */
	rc = rp->optee->ops->lend_protmem(rp->optee, rp->protmem,
					  rp->mem_attrs,
					  rp->mem_attr_count, rp->use_case);
	if (rc)
		goto err_put_shm;
	rp->protmem->flags |= TEE_SHM_DYNAMIC;

	rp->gen_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!rp->gen_pool) {
		rc = -ENOMEM;
		goto err_reclaim;
	}

	rc = gen_pool_add(rp->gen_pool, rp->protmem->paddr,
			  rp->protmem->size, -1);
	if (rc)
		goto err_free_pool;

	refcount_set(&rp->refcount, 1);
	return 0;

err_free_pool:
	gen_pool_destroy(rp->gen_pool);
	rp->gen_pool = NULL;
err_reclaim:
	rp->optee->ops->reclaim_protmem(rp->optee, rp->protmem);
err_put_shm:
	tee_shm_put(rp->protmem);
err_null_protmem:
	rp->protmem = NULL;
	return rc;
}

static int get_dyn_protmem(struct optee_protmem_dyn_pool *rp)
{
	int rc = 0;

	if (!refcount_inc_not_zero(&rp->refcount)) {
		mutex_lock(&rp->mutex);
		if (rp->gen_pool) {
			/*
			 * Another thread has already initialized the pool
			 * before us, or the pool was just about to be torn
			 * down. Either way we only need to increase the
			 * refcount and we're done.
			 */
			refcount_inc(&rp->refcount);
		} else {
			rc = init_dyn_protmem(rp);
		}
		mutex_unlock(&rp->mutex);
	}

	return rc;
}

static void release_dyn_protmem(struct optee_protmem_dyn_pool *rp)
{
	gen_pool_destroy(rp->gen_pool);
	rp->gen_pool = NULL;

	rp->optee->ops->reclaim_protmem(rp->optee, rp->protmem);
	rp->protmem->flags &= ~TEE_SHM_DYNAMIC;

	WARN(refcount_read(&rp->protmem->refcount) != 1, "Unexpected refcount");
	tee_shm_put(rp->protmem);
	rp->protmem = NULL;
}

static void put_dyn_protmem(struct optee_protmem_dyn_pool *rp)
{
	if (refcount_dec_and_test(&rp->refcount)) {
		mutex_lock(&rp->mutex);
		if (rp->gen_pool)
			release_dyn_protmem(rp);
		mutex_unlock(&rp->mutex);
	}
}

static int protmem_pool_op_dyn_alloc(struct tee_protmem_pool *pool,
				     struct sg_table *sgt, size_t size,
				     size_t *offs)
{
	struct optee_protmem_dyn_pool *rp = to_protmem_dyn_pool(pool);
	size_t sz = ALIGN(size, PAGE_SIZE);
	phys_addr_t pa;
	int rc;

	rc = get_dyn_protmem(rp);
	if (rc)
		return rc;

	pa = gen_pool_alloc(rp->gen_pool, sz);
	if (!pa) {
		rc = -ENOMEM;
		goto err_put;
	}

	rc = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (rc)
		goto err_free;

	sg_set_page(sgt->sgl, phys_to_page(pa), size, 0);
	*offs = pa - rp->protmem->paddr;

	return 0;
err_free:
	gen_pool_free(rp->gen_pool, pa, size);
err_put:
	put_dyn_protmem(rp);

	return rc;
}

static void protmem_pool_op_dyn_free(struct tee_protmem_pool *pool,
				     struct sg_table *sgt)
{
	struct optee_protmem_dyn_pool *rp = to_protmem_dyn_pool(pool);
	struct scatterlist *sg;
	int i;

	for_each_sgtable_sg(sgt, sg, i)
		gen_pool_free(rp->gen_pool, sg_phys(sg), sg->length);
	sg_free_table(sgt);
	put_dyn_protmem(rp);
}

static int protmem_pool_op_dyn_update_shm(struct tee_protmem_pool *pool,
					  struct sg_table *sgt, size_t offs,
					  struct tee_shm *shm,
					  struct tee_shm **parent_shm)
{
	struct optee_protmem_dyn_pool *rp = to_protmem_dyn_pool(pool);

	*parent_shm = rp->protmem;

	return 0;
}

static void pool_op_dyn_destroy_pool(struct tee_protmem_pool *pool)
{
	struct optee_protmem_dyn_pool *rp = to_protmem_dyn_pool(pool);

	mutex_destroy(&rp->mutex);
	kfree(rp);
}

static struct tee_protmem_pool_ops protmem_pool_ops_dyn = {
	.alloc = protmem_pool_op_dyn_alloc,
	.free = protmem_pool_op_dyn_free,
	.update_shm = protmem_pool_op_dyn_update_shm,
	.destroy_pool = pool_op_dyn_destroy_pool,
};

static int get_protmem_config(struct optee *optee, u32 use_case,
			      size_t *min_size, u_int *pa_width,
			      u32 *mem_attrs, u_int *ma_count)
{
	struct tee_param params[2] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT,
			.u.value.a = use_case,
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT,
		},
	};
	struct optee_shm_arg_entry *entry;
	struct tee_shm *shm_param = NULL;
	struct optee_msg_arg *msg_arg;
	struct tee_shm *shm;
	u_int offs;
	int rc;

	if (mem_attrs && *ma_count) {
		params[1].u.memref.size = *ma_count * sizeof(*mem_attrs);
		shm_param = tee_shm_alloc_priv_buf(optee->ctx,
						   params[1].u.memref.size);
		if (IS_ERR(shm_param))
			return PTR_ERR(shm_param);
		params[1].u.memref.shm = shm_param;
	}

	msg_arg = optee_get_msg_arg(optee->ctx, ARRAY_SIZE(params), &entry,
				    &shm, &offs);
	if (IS_ERR(msg_arg)) {
		rc = PTR_ERR(msg_arg);
		goto out_free_shm;
	}
	msg_arg->cmd = OPTEE_MSG_CMD_GET_PROTMEM_CONFIG;

	rc = optee->ops->to_msg_param(optee, msg_arg->params,
				      ARRAY_SIZE(params), params);
	if (rc)
		goto out_free_msg;

	rc = optee->ops->do_call_with_arg(optee->ctx, shm, offs, false);
	if (rc)
		goto out_free_msg;
	if (msg_arg->ret && msg_arg->ret != TEEC_ERROR_SHORT_BUFFER) {
		rc = -EINVAL;
		goto out_free_msg;
	}

	rc = optee->ops->from_msg_param(optee, params, ARRAY_SIZE(params),
					msg_arg->params);
	if (rc)
		goto out_free_msg;

	if (!msg_arg->ret && mem_attrs &&
	    *ma_count < params[1].u.memref.size / sizeof(*mem_attrs)) {
		rc = -EINVAL;
		goto out_free_msg;
	}

	*min_size = params[0].u.value.a;
	*pa_width = params[0].u.value.c;
	*ma_count = params[1].u.memref.size / sizeof(*mem_attrs);

	if (msg_arg->ret == TEEC_ERROR_SHORT_BUFFER) {
		rc = -ENOSPC;
		goto out_free_msg;
	}

	if (mem_attrs)
		memcpy(mem_attrs, tee_shm_get_va(shm_param, 0),
		       params[1].u.memref.size);

out_free_msg:
	optee_free_msg_arg(optee->ctx, entry, offs);
out_free_shm:
	if (shm_param)
		tee_shm_free(shm_param);
	return rc;
}

struct tee_protmem_pool *optee_protmem_alloc_dyn_pool(struct optee *optee,
						      enum tee_dma_heap_id id)
{
	struct optee_protmem_dyn_pool *rp;
	size_t min_size;
	u_int pa_width;
	int rc;

	rp = kzalloc(sizeof(*rp), GFP_KERNEL);
	if (!rp)
		return ERR_PTR(-ENOMEM);
	rp->use_case = id;

	rc = get_protmem_config(optee, id, &min_size, &pa_width, NULL,
				&rp->mem_attr_count);
	if (rc) {
		if (rc != -ENOSPC)
			goto err;
		rp->mem_attrs = kcalloc(rp->mem_attr_count,
					sizeof(*rp->mem_attrs), GFP_KERNEL);
		if (!rp->mem_attrs) {
			rc = -ENOMEM;
			goto err;
		}
		rc = get_protmem_config(optee, id, &min_size, &pa_width,
					rp->mem_attrs, &rp->mem_attr_count);
		if (rc)
			goto err_kfree_eps;
	}

	rc = optee_set_dma_mask(optee, pa_width);
	if (rc)
		goto err_kfree_eps;

	rp->pool.ops = &protmem_pool_ops_dyn;
	rp->optee = optee;
	rp->page_count = min_size / PAGE_SIZE;
	mutex_init(&rp->mutex);

	return &rp->pool;

err_kfree_eps:
	kfree(rp->mem_attrs);
err:
	kfree(rp);
	return ERR_PTR(rc);
}
