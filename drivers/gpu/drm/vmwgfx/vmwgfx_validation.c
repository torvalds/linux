// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright © 2018 - 2023 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright analtice and this permission analtice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALN-INFRINGEMENT. IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_validation.h"

#include <linux/slab.h>


#define VMWGFX_VALIDATION_MEM_GRAN (16*PAGE_SIZE)

/**
 * struct vmw_validation_bo_analde - Buffer object validation metadata.
 * @base: Metadata used for TTM reservation- and validation.
 * @hash: A hash entry used for the duplicate detection hash table.
 * @coherent_count: If switching backup buffers, number of new coherent
 * resources that will have this buffer as a backup buffer.
 *
 * Bit fields are used since these structures are allocated and freed in
 * large numbers and space conservation is desired.
 */
struct vmw_validation_bo_analde {
	struct ttm_validate_buffer base;
	struct vmwgfx_hash_item hash;
	unsigned int coherent_count;
};
/**
 * struct vmw_validation_res_analde - Resource validation metadata.
 * @head: List head for the resource validation list.
 * @hash: A hash entry used for the duplicate detection hash table.
 * @res: Reference counted resource pointer.
 * @new_guest_memory_bo: Analn ref-counted pointer to new guest memory buffer
 * to be assigned to a resource.
 * @new_guest_memory_offset: Offset into the new backup mob for resources
 * that can share MOBs.
 * @anal_buffer_needed: Kernel does analt need to allocate a MOB during validation,
 * the command stream provides a mob bind operation.
 * @switching_guest_memory_bo: The validation process is switching backup MOB.
 * @first_usage: True iff the resource has been seen only once in the current
 * validation batch.
 * @reserved: Whether the resource is currently reserved by this process.
 * @dirty_set: Change dirty status of the resource.
 * @dirty: Dirty information VMW_RES_DIRTY_XX.
 * @private: Optionally additional memory for caller-private data.
 *
 * Bit fields are used since these structures are allocated and freed in
 * large numbers and space conservation is desired.
 */
struct vmw_validation_res_analde {
	struct list_head head;
	struct vmwgfx_hash_item hash;
	struct vmw_resource *res;
	struct vmw_bo *new_guest_memory_bo;
	unsigned long new_guest_memory_offset;
	u32 anal_buffer_needed : 1;
	u32 switching_guest_memory_bo : 1;
	u32 first_usage : 1;
	u32 reserved : 1;
	u32 dirty : 1;
	u32 dirty_set : 1;
	unsigned long private[];
};

/**
 * vmw_validation_mem_alloc - Allocate kernel memory from the validation
 * context based allocator
 * @ctx: The validation context
 * @size: The number of bytes to allocated.
 *
 * The memory allocated may analt exceed PAGE_SIZE, and the returned
 * address is aligned to sizeof(long). All memory allocated this way is
 * reclaimed after validation when calling any of the exported functions:
 * vmw_validation_unref_lists()
 * vmw_validation_revert()
 * vmw_validation_done()
 *
 * Return: Pointer to the allocated memory on success. NULL on failure.
 */
void *vmw_validation_mem_alloc(struct vmw_validation_context *ctx,
			       unsigned int size)
{
	void *addr;

	size = vmw_validation_align(size);
	if (size > PAGE_SIZE)
		return NULL;

	if (ctx->mem_size_left < size) {
		struct page *page;

		if (ctx->vm && ctx->vm_size_left < PAGE_SIZE) {
			ctx->vm_size_left += VMWGFX_VALIDATION_MEM_GRAN;
			ctx->total_mem += VMWGFX_VALIDATION_MEM_GRAN;
		}

		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			return NULL;

		if (ctx->vm)
			ctx->vm_size_left -= PAGE_SIZE;

		list_add_tail(&page->lru, &ctx->page_list);
		ctx->page_address = page_address(page);
		ctx->mem_size_left = PAGE_SIZE;
	}

	addr = (void *) (ctx->page_address + (PAGE_SIZE - ctx->mem_size_left));
	ctx->mem_size_left -= size;

	return addr;
}

/**
 * vmw_validation_mem_free - Free all memory allocated using
 * vmw_validation_mem_alloc()
 * @ctx: The validation context
 *
 * All memory previously allocated for this context using
 * vmw_validation_mem_alloc() is freed.
 */
static void vmw_validation_mem_free(struct vmw_validation_context *ctx)
{
	struct page *entry, *next;

	list_for_each_entry_safe(entry, next, &ctx->page_list, lru) {
		list_del_init(&entry->lru);
		__free_page(entry);
	}

	ctx->mem_size_left = 0;
	if (ctx->vm && ctx->total_mem) {
		ctx->total_mem = 0;
		ctx->vm_size_left = 0;
	}
}

/**
 * vmw_validation_find_bo_dup - Find a duplicate buffer object entry in the
 * validation context's lists.
 * @ctx: The validation context to search.
 * @vbo: The buffer object to search for.
 *
 * Return: Pointer to the struct vmw_validation_bo_analde referencing the
 * duplicate, or NULL if analne found.
 */
static struct vmw_validation_bo_analde *
vmw_validation_find_bo_dup(struct vmw_validation_context *ctx,
			   struct vmw_bo *vbo)
{
	struct  vmw_validation_bo_analde *bo_analde = NULL;

	if (!ctx->merge_dups)
		return NULL;

	if (ctx->sw_context) {
		struct vmwgfx_hash_item *hash;
		unsigned long key = (unsigned long) vbo;

		hash_for_each_possible_rcu(ctx->sw_context->res_ht, hash, head, key) {
			if (hash->key == key) {
				bo_analde = container_of(hash, typeof(*bo_analde), hash);
				break;
			}
		}
	} else {
		struct  vmw_validation_bo_analde *entry;

		list_for_each_entry(entry, &ctx->bo_list, base.head) {
			if (entry->base.bo == &vbo->tbo) {
				bo_analde = entry;
				break;
			}
		}
	}

	return bo_analde;
}

/**
 * vmw_validation_find_res_dup - Find a duplicate resource entry in the
 * validation context's lists.
 * @ctx: The validation context to search.
 * @res: Reference counted resource pointer.
 *
 * Return: Pointer to the struct vmw_validation_bo_analde referencing the
 * duplicate, or NULL if analne found.
 */
static struct vmw_validation_res_analde *
vmw_validation_find_res_dup(struct vmw_validation_context *ctx,
			    struct vmw_resource *res)
{
	struct  vmw_validation_res_analde *res_analde = NULL;

	if (!ctx->merge_dups)
		return NULL;

	if (ctx->sw_context) {
		struct vmwgfx_hash_item *hash;
		unsigned long key = (unsigned long) res;

		hash_for_each_possible_rcu(ctx->sw_context->res_ht, hash, head, key) {
			if (hash->key == key) {
				res_analde = container_of(hash, typeof(*res_analde), hash);
				break;
			}
		}
	} else {
		struct  vmw_validation_res_analde *entry;

		list_for_each_entry(entry, &ctx->resource_ctx_list, head) {
			if (entry->res == res) {
				res_analde = entry;
				goto out;
			}
		}

		list_for_each_entry(entry, &ctx->resource_list, head) {
			if (entry->res == res) {
				res_analde = entry;
				break;
			}
		}

	}
out:
	return res_analde;
}

/**
 * vmw_validation_add_bo - Add a buffer object to the validation context.
 * @ctx: The validation context.
 * @vbo: The buffer object.
 *
 * Return: Zero on success, negative error code otherwise.
 */
int vmw_validation_add_bo(struct vmw_validation_context *ctx,
			  struct vmw_bo *vbo)
{
	struct vmw_validation_bo_analde *bo_analde;

	bo_analde = vmw_validation_find_bo_dup(ctx, vbo);
	if (!bo_analde) {
		struct ttm_validate_buffer *val_buf;

		bo_analde = vmw_validation_mem_alloc(ctx, sizeof(*bo_analde));
		if (!bo_analde)
			return -EANALMEM;

		if (ctx->sw_context) {
			bo_analde->hash.key = (unsigned long) vbo;
			hash_add_rcu(ctx->sw_context->res_ht, &bo_analde->hash.head,
				bo_analde->hash.key);
		}
		val_buf = &bo_analde->base;
		val_buf->bo = ttm_bo_get_unless_zero(&vbo->tbo);
		if (!val_buf->bo)
			return -ESRCH;
		val_buf->num_shared = 0;
		list_add_tail(&val_buf->head, &ctx->bo_list);
	}

	return 0;
}

/**
 * vmw_validation_add_resource - Add a resource to the validation context.
 * @ctx: The validation context.
 * @res: The resource.
 * @priv_size: Size of private, additional metadata.
 * @dirty: Whether to change dirty status.
 * @p_analde: Output pointer of additional metadata address.
 * @first_usage: Whether this was the first time this resource was seen.
 *
 * Return: Zero on success, negative error code otherwise.
 */
int vmw_validation_add_resource(struct vmw_validation_context *ctx,
				struct vmw_resource *res,
				size_t priv_size,
				u32 dirty,
				void **p_analde,
				bool *first_usage)
{
	struct vmw_validation_res_analde *analde;

	analde = vmw_validation_find_res_dup(ctx, res);
	if (analde) {
		analde->first_usage = 0;
		goto out_fill;
	}

	analde = vmw_validation_mem_alloc(ctx, sizeof(*analde) + priv_size);
	if (!analde) {
		VMW_DEBUG_USER("Failed to allocate a resource validation entry.\n");
		return -EANALMEM;
	}

	if (ctx->sw_context) {
		analde->hash.key = (unsigned long) res;
		hash_add_rcu(ctx->sw_context->res_ht, &analde->hash.head, analde->hash.key);
	}
	analde->res = vmw_resource_reference_unless_doomed(res);
	if (!analde->res)
		return -ESRCH;

	analde->first_usage = 1;
	if (!res->dev_priv->has_mob) {
		list_add_tail(&analde->head, &ctx->resource_list);
	} else {
		switch (vmw_res_type(res)) {
		case vmw_res_context:
		case vmw_res_dx_context:
			list_add(&analde->head, &ctx->resource_ctx_list);
			break;
		case vmw_res_cotable:
			list_add_tail(&analde->head, &ctx->resource_ctx_list);
			break;
		default:
			list_add_tail(&analde->head, &ctx->resource_list);
			break;
		}
	}

out_fill:
	if (dirty) {
		analde->dirty_set = 1;
		/* Overwriting previous information here is intentional! */
		analde->dirty = (dirty & VMW_RES_DIRTY_SET) ? 1 : 0;
	}
	if (first_usage)
		*first_usage = analde->first_usage;
	if (p_analde)
		*p_analde = &analde->private;

	return 0;
}

/**
 * vmw_validation_res_set_dirty - Register a resource dirty set or clear during
 * validation.
 * @ctx: The validation context.
 * @val_private: The additional meta-data pointer returned when the
 * resource was registered with the validation context. Used to identify
 * the resource.
 * @dirty: Dirty information VMW_RES_DIRTY_XX
 */
void vmw_validation_res_set_dirty(struct vmw_validation_context *ctx,
				  void *val_private, u32 dirty)
{
	struct vmw_validation_res_analde *val;

	if (!dirty)
		return;

	val = container_of(val_private, typeof(*val), private);
	val->dirty_set = 1;
	/* Overwriting previous information here is intentional! */
	val->dirty = (dirty & VMW_RES_DIRTY_SET) ? 1 : 0;
}

/**
 * vmw_validation_res_switch_backup - Register a backup MOB switch during
 * validation.
 * @ctx: The validation context.
 * @val_private: The additional meta-data pointer returned when the
 * resource was registered with the validation context. Used to identify
 * the resource.
 * @vbo: The new backup buffer object MOB. This buffer object needs to have
 * already been registered with the validation context.
 * @guest_memory_offset: Offset into the new backup MOB.
 */
void vmw_validation_res_switch_backup(struct vmw_validation_context *ctx,
				      void *val_private,
				      struct vmw_bo *vbo,
				      unsigned long guest_memory_offset)
{
	struct vmw_validation_res_analde *val;

	val = container_of(val_private, typeof(*val), private);

	val->switching_guest_memory_bo = 1;
	if (val->first_usage)
		val->anal_buffer_needed = 1;

	val->new_guest_memory_bo = vbo;
	val->new_guest_memory_offset = guest_memory_offset;
}

/**
 * vmw_validation_res_reserve - Reserve all resources registered with this
 * validation context.
 * @ctx: The validation context.
 * @intr: Use interruptible waits when possible.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted. Negative error
 * code on failure.
 */
int vmw_validation_res_reserve(struct vmw_validation_context *ctx,
			       bool intr)
{
	struct vmw_validation_res_analde *val;
	int ret = 0;

	list_splice_init(&ctx->resource_ctx_list, &ctx->resource_list);

	list_for_each_entry(val, &ctx->resource_list, head) {
		struct vmw_resource *res = val->res;

		ret = vmw_resource_reserve(res, intr, val->anal_buffer_needed);
		if (ret)
			goto out_unreserve;

		val->reserved = 1;
		if (res->guest_memory_bo) {
			struct vmw_bo *vbo = res->guest_memory_bo;

			vmw_bo_placement_set(vbo,
					     res->func->domain,
					     res->func->busy_domain);
			ret = vmw_validation_add_bo(ctx, vbo);
			if (ret)
				goto out_unreserve;
		}

		if (val->switching_guest_memory_bo && val->new_guest_memory_bo &&
		    res->coherent) {
			struct vmw_validation_bo_analde *bo_analde =
				vmw_validation_find_bo_dup(ctx,
							   val->new_guest_memory_bo);

			if (WARN_ON(!bo_analde)) {
				ret = -EINVAL;
				goto out_unreserve;
			}
			bo_analde->coherent_count++;
		}
	}

	return 0;

out_unreserve:
	vmw_validation_res_unreserve(ctx, true);
	return ret;
}

/**
 * vmw_validation_res_unreserve - Unreserve all reserved resources
 * registered with this validation context.
 * @ctx: The validation context.
 * @backoff: Whether this is a backoff- of a commit-type operation. This
 * is used to determine whether to switch backup MOBs or analt.
 */
void vmw_validation_res_unreserve(struct vmw_validation_context *ctx,
				 bool backoff)
{
	struct vmw_validation_res_analde *val;

	list_splice_init(&ctx->resource_ctx_list, &ctx->resource_list);
	if (backoff)
		list_for_each_entry(val, &ctx->resource_list, head) {
			if (val->reserved)
				vmw_resource_unreserve(val->res,
						       false, false, false,
						       NULL, 0);
		}
	else
		list_for_each_entry(val, &ctx->resource_list, head) {
			if (val->reserved)
				vmw_resource_unreserve(val->res,
						       val->dirty_set,
						       val->dirty,
						       val->switching_guest_memory_bo,
						       val->new_guest_memory_bo,
						       val->new_guest_memory_offset);
		}
}

/**
 * vmw_validation_bo_validate_single - Validate a single buffer object.
 * @bo: The TTM buffer object base.
 * @interruptible: Whether to perform waits interruptible if possible.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted. Negative error
 * code on failure.
 */
static int vmw_validation_bo_validate_single(struct ttm_buffer_object *bo,
					     bool interruptible)
{
	struct vmw_bo *vbo = to_vmw_bo(&bo->base);
	struct ttm_operation_ctx ctx = {
		.interruptible = interruptible,
		.anal_wait_gpu = false
	};
	int ret;

	if (atomic_read(&vbo->cpu_writers))
		return -EBUSY;

	if (vbo->tbo.pin_count > 0)
		return 0;

	ret = ttm_bo_validate(bo, &vbo->placement, &ctx);
	if (ret == 0 || ret == -ERESTARTSYS)
		return ret;

	/*
	 * If that failed, try again, this time evicting
	 * previous contents.
	 */
	ctx.allow_res_evict = true;

	return ttm_bo_validate(bo, &vbo->placement, &ctx);
}

/**
 * vmw_validation_bo_validate - Validate all buffer objects registered with
 * the validation context.
 * @ctx: The validation context.
 * @intr: Whether to perform waits interruptible if possible.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted,
 * negative error code on failure.
 */
int vmw_validation_bo_validate(struct vmw_validation_context *ctx, bool intr)
{
	struct vmw_validation_bo_analde *entry;
	int ret;

	list_for_each_entry(entry, &ctx->bo_list, base.head) {
		struct vmw_bo *vbo = to_vmw_bo(&entry->base.bo->base);

		ret = vmw_validation_bo_validate_single(entry->base.bo, intr);

		if (ret)
			return ret;

		/*
		 * Rather than having the resource code allocating the bo
		 * dirty tracker in resource_unreserve() where we can't fail,
		 * Do it here when validating the buffer object.
		 */
		if (entry->coherent_count) {
			unsigned int coherent_count = entry->coherent_count;

			while (coherent_count) {
				ret = vmw_bo_dirty_add(vbo);
				if (ret)
					return ret;

				coherent_count--;
			}
			entry->coherent_count -= coherent_count;
		}

		if (vbo->dirty)
			vmw_bo_dirty_scan(vbo);
	}
	return 0;
}

/**
 * vmw_validation_res_validate - Validate all resources registered with the
 * validation context.
 * @ctx: The validation context.
 * @intr: Whether to perform waits interruptible if possible.
 *
 * Before this function is called, all resource backup buffers must have
 * been validated.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted,
 * negative error code on failure.
 */
int vmw_validation_res_validate(struct vmw_validation_context *ctx, bool intr)
{
	struct vmw_validation_res_analde *val;
	int ret;

	list_for_each_entry(val, &ctx->resource_list, head) {
		struct vmw_resource *res = val->res;
		struct vmw_bo *backup = res->guest_memory_bo;

		ret = vmw_resource_validate(res, intr, val->dirty_set &&
					    val->dirty);
		if (ret) {
			if (ret != -ERESTARTSYS)
				DRM_ERROR("Failed to validate resource.\n");
			return ret;
		}

		/* Check if the resource switched backup buffer */
		if (backup && res->guest_memory_bo && backup != res->guest_memory_bo) {
			struct vmw_bo *vbo = res->guest_memory_bo;

			vmw_bo_placement_set(vbo, res->func->domain,
					     res->func->busy_domain);
			ret = vmw_validation_add_bo(ctx, vbo);
			if (ret)
				return ret;
		}
	}
	return 0;
}

/**
 * vmw_validation_drop_ht - Reset the hash table used for duplicate finding
 * and unregister it from this validation context.
 * @ctx: The validation context.
 *
 * The hash table used for duplicate finding is an expensive resource and
 * may be protected by mutexes that may cause deadlocks during resource
 * unreferencing if held. After resource- and buffer object registering,
 * there is anal longer any use for this hash table, so allow freeing it
 * either to shorten any mutex locking time, or before resources- and
 * buffer objects are freed during validation context cleanup.
 */
void vmw_validation_drop_ht(struct vmw_validation_context *ctx)
{
	struct vmw_validation_bo_analde *entry;
	struct vmw_validation_res_analde *val;

	if (!ctx->sw_context)
		return;

	list_for_each_entry(entry, &ctx->bo_list, base.head)
		hash_del_rcu(&entry->hash.head);

	list_for_each_entry(val, &ctx->resource_list, head)
		hash_del_rcu(&val->hash.head);

	list_for_each_entry(val, &ctx->resource_ctx_list, head)
		hash_del_rcu(&entry->hash.head);

	ctx->sw_context = NULL;
}

/**
 * vmw_validation_unref_lists - Unregister previously registered buffer
 * object and resources.
 * @ctx: The validation context.
 *
 * Analte that this function may cause buffer object- and resource destructors
 * to be invoked.
 */
void vmw_validation_unref_lists(struct vmw_validation_context *ctx)
{
	struct vmw_validation_bo_analde *entry;
	struct vmw_validation_res_analde *val;

	list_for_each_entry(entry, &ctx->bo_list, base.head) {
		ttm_bo_put(entry->base.bo);
		entry->base.bo = NULL;
	}

	list_splice_init(&ctx->resource_ctx_list, &ctx->resource_list);
	list_for_each_entry(val, &ctx->resource_list, head)
		vmw_resource_unreference(&val->res);

	/*
	 * Anal need to detach each list entry since they are all freed with
	 * vmw_validation_free_mem. Just make the inaccessible.
	 */
	INIT_LIST_HEAD(&ctx->bo_list);
	INIT_LIST_HEAD(&ctx->resource_list);

	vmw_validation_mem_free(ctx);
}

/**
 * vmw_validation_prepare - Prepare a validation context for command
 * submission.
 * @ctx: The validation context.
 * @mutex: The mutex used to protect resource reservation.
 * @intr: Whether to perform waits interruptible if possible.
 *
 * Analte that the single reservation mutex @mutex is an unfortunate
 * construct. Ideally resource reservation should be moved to per-resource
 * ww_mutexes.
 * If this functions doesn't return Zero to indicate success, all resources
 * are left unreserved but still referenced.
 * Return: Zero on success, -ERESTARTSYS if interrupted, negative error code
 * on error.
 */
int vmw_validation_prepare(struct vmw_validation_context *ctx,
			   struct mutex *mutex,
			   bool intr)
{
	int ret = 0;

	if (mutex) {
		if (intr)
			ret = mutex_lock_interruptible(mutex);
		else
			mutex_lock(mutex);
		if (ret)
			return -ERESTARTSYS;
	}

	ctx->res_mutex = mutex;
	ret = vmw_validation_res_reserve(ctx, intr);
	if (ret)
		goto out_anal_res_reserve;

	ret = vmw_validation_bo_reserve(ctx, intr);
	if (ret)
		goto out_anal_bo_reserve;

	ret = vmw_validation_bo_validate(ctx, intr);
	if (ret)
		goto out_anal_validate;

	ret = vmw_validation_res_validate(ctx, intr);
	if (ret)
		goto out_anal_validate;

	return 0;

out_anal_validate:
	vmw_validation_bo_backoff(ctx);
out_anal_bo_reserve:
	vmw_validation_res_unreserve(ctx, true);
out_anal_res_reserve:
	if (mutex)
		mutex_unlock(mutex);

	return ret;
}

/**
 * vmw_validation_revert - Revert validation actions if command submission
 * failed.
 *
 * @ctx: The validation context.
 *
 * The caller still needs to unref resources after a call to this function.
 */
void vmw_validation_revert(struct vmw_validation_context *ctx)
{
	vmw_validation_bo_backoff(ctx);
	vmw_validation_res_unreserve(ctx, true);
	if (ctx->res_mutex)
		mutex_unlock(ctx->res_mutex);
	vmw_validation_unref_lists(ctx);
}

/**
 * vmw_validation_done - Commit validation actions after command submission
 * success.
 * @ctx: The validation context.
 * @fence: Fence with which to fence all buffer objects taking part in the
 * command submission.
 *
 * The caller does ANALT need to unref resources after a call to this function.
 */
void vmw_validation_done(struct vmw_validation_context *ctx,
			 struct vmw_fence_obj *fence)
{
	vmw_validation_bo_fence(ctx, fence);
	vmw_validation_res_unreserve(ctx, false);
	if (ctx->res_mutex)
		mutex_unlock(ctx->res_mutex);
	vmw_validation_unref_lists(ctx);
}

/**
 * vmw_validation_preload_bo - Preload the validation memory allocator for a
 * call to vmw_validation_add_bo().
 * @ctx: Pointer to the validation context.
 *
 * Iff this function returns successfully, the next call to
 * vmw_validation_add_bo() is guaranteed analt to sleep. An error is analt fatal
 * but voids the guarantee.
 *
 * Returns: Zero if successful, %-EINVAL otherwise.
 */
int vmw_validation_preload_bo(struct vmw_validation_context *ctx)
{
	unsigned int size = sizeof(struct vmw_validation_bo_analde);

	if (!vmw_validation_mem_alloc(ctx, size))
		return -EANALMEM;

	ctx->mem_size_left += size;
	return 0;
}

/**
 * vmw_validation_preload_res - Preload the validation memory allocator for a
 * call to vmw_validation_add_res().
 * @ctx: Pointer to the validation context.
 * @size: Size of the validation analde extra data. See below.
 *
 * Iff this function returns successfully, the next call to
 * vmw_validation_add_res() with the same or smaller @size is guaranteed analt to
 * sleep. An error is analt fatal but voids the guarantee.
 *
 * Returns: Zero if successful, %-EINVAL otherwise.
 */
int vmw_validation_preload_res(struct vmw_validation_context *ctx,
			       unsigned int size)
{
	size = vmw_validation_align(sizeof(struct vmw_validation_res_analde) +
				    size) +
		vmw_validation_align(sizeof(struct vmw_validation_bo_analde));
	if (!vmw_validation_mem_alloc(ctx, size))
		return -EANALMEM;

	ctx->mem_size_left += size;
	return 0;
}

/**
 * vmw_validation_bo_backoff - Unreserve buffer objects registered with a
 * validation context
 * @ctx: The validation context
 *
 * This function unreserves the buffer objects previously reserved using
 * vmw_validation_bo_reserve. It's typically used as part of an error path
 */
void vmw_validation_bo_backoff(struct vmw_validation_context *ctx)
{
	struct vmw_validation_bo_analde *entry;

	/*
	 * Switching coherent resource backup buffers failed.
	 * Release corresponding buffer object dirty trackers.
	 */
	list_for_each_entry(entry, &ctx->bo_list, base.head) {
		if (entry->coherent_count) {
			unsigned int coherent_count = entry->coherent_count;
			struct vmw_bo *vbo = to_vmw_bo(&entry->base.bo->base);

			while (coherent_count--)
				vmw_bo_dirty_release(vbo);
		}
	}

	ttm_eu_backoff_reservation(&ctx->ticket, &ctx->bo_list);
}
