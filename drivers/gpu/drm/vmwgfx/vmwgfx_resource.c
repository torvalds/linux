// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_binding.h"
#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"

#define VMW_RES_EVICT_ERR_COUNT 10

/**
 * vmw_resource_mob_attach - Mark a resource as attached to its backing mob
 * @res: The resource
 */
void vmw_resource_mob_attach(struct vmw_resource *res)
{
	struct vmw_bo *gbo = res->guest_memory_bo;
	struct rb_node **new = &gbo->res_tree.rb_node, *parent = NULL;

	dma_resv_assert_held(gbo->tbo.base.resv);
	res->used_prio = (res->res_dirty) ? res->func->dirty_prio :
		res->func->prio;

	while (*new) {
		struct vmw_resource *this =
			container_of(*new, struct vmw_resource, mob_node);

		parent = *new;
		new = (res->guest_memory_offset < this->guest_memory_offset) ?
			&((*new)->rb_left) : &((*new)->rb_right);
	}

	rb_link_node(&res->mob_node, parent, new);
	rb_insert_color(&res->mob_node, &gbo->res_tree);
	vmw_bo_del_detached_resource(gbo, res);

	vmw_bo_prio_add(gbo, res->used_prio);
}

/**
 * vmw_resource_mob_detach - Mark a resource as detached from its backing mob
 * @res: The resource
 */
void vmw_resource_mob_detach(struct vmw_resource *res)
{
	struct vmw_bo *gbo = res->guest_memory_bo;

	dma_resv_assert_held(gbo->tbo.base.resv);
	if (vmw_resource_mob_attached(res)) {
		rb_erase(&res->mob_node, &gbo->res_tree);
		RB_CLEAR_NODE(&res->mob_node);
		vmw_bo_prio_del(gbo, res->used_prio);
	}
}

struct vmw_resource *vmw_resource_reference(struct vmw_resource *res)
{
	kref_get(&res->kref);
	return res;
}

struct vmw_resource *
vmw_resource_reference_unless_doomed(struct vmw_resource *res)
{
	return kref_get_unless_zero(&res->kref) ? res : NULL;
}

/**
 * vmw_resource_release_id - release a resource id to the id manager.
 *
 * @res: Pointer to the resource.
 *
 * Release the resource id to the resource id manager and set it to -1
 */
void vmw_resource_release_id(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	spin_lock(&dev_priv->resource_lock);
	if (res->id != -1)
		idr_remove(idr, res->id);
	res->id = -1;
	spin_unlock(&dev_priv->resource_lock);
}

static void vmw_resource_release(struct kref *kref)
{
	struct vmw_resource *res =
	    container_of(kref, struct vmw_resource, kref);
	struct vmw_private *dev_priv = res->dev_priv;
	int id;
	int ret;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	spin_lock(&dev_priv->resource_lock);
	list_del_init(&res->lru_head);
	spin_unlock(&dev_priv->resource_lock);
	if (res->guest_memory_bo) {
		struct ttm_buffer_object *bo = &res->guest_memory_bo->tbo;

		ret = ttm_bo_reserve(bo, false, false, NULL);
		BUG_ON(ret);
		if (vmw_resource_mob_attached(res) &&
		    res->func->unbind != NULL) {
			struct ttm_validate_buffer val_buf;

			val_buf.bo = bo;
			val_buf.num_shared = 0;
			res->func->unbind(res, false, &val_buf);
		}
		res->guest_memory_size = false;
		vmw_resource_mob_detach(res);
		if (res->dirty)
			res->func->dirty_free(res);
		if (res->coherent)
			vmw_bo_dirty_release(res->guest_memory_bo);
		ttm_bo_unreserve(bo);
		vmw_user_bo_unref(&res->guest_memory_bo);
	}

	if (likely(res->hw_destroy != NULL)) {
		mutex_lock(&dev_priv->binding_mutex);
		vmw_binding_res_list_kill(&res->binding_head);
		mutex_unlock(&dev_priv->binding_mutex);
		res->hw_destroy(res);
	}

	id = res->id;
	if (res->res_free != NULL)
		res->res_free(res);
	else
		kfree(res);

	spin_lock(&dev_priv->resource_lock);
	if (id != -1)
		idr_remove(idr, id);
	spin_unlock(&dev_priv->resource_lock);
}

void vmw_resource_unreference(struct vmw_resource **p_res)
{
	struct vmw_resource *res = *p_res;

	*p_res = NULL;
	kref_put(&res->kref, vmw_resource_release);
}


/**
 * vmw_resource_alloc_id - release a resource id to the id manager.
 *
 * @res: Pointer to the resource.
 *
 * Allocate the lowest free resource from the resource manager, and set
 * @res->id to that id. Returns 0 on success and -ENOMEM on failure.
 */
int vmw_resource_alloc_id(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	BUG_ON(res->id != -1);

	idr_preload(GFP_KERNEL);
	spin_lock(&dev_priv->resource_lock);

	ret = idr_alloc(idr, res, 1, 0, GFP_NOWAIT);
	if (ret >= 0)
		res->id = ret;

	spin_unlock(&dev_priv->resource_lock);
	idr_preload_end();
	return ret < 0 ? ret : 0;
}

/**
 * vmw_resource_init - initialize a struct vmw_resource
 *
 * @dev_priv:       Pointer to a device private struct.
 * @res:            The struct vmw_resource to initialize.
 * @delay_id:       Boolean whether to defer device id allocation until
 *                  the first validation.
 * @res_free:       Resource destructor.
 * @func:           Resource function table.
 */
int vmw_resource_init(struct vmw_private *dev_priv, struct vmw_resource *res,
		      bool delay_id,
		      void (*res_free) (struct vmw_resource *res),
		      const struct vmw_res_func *func)
{
	kref_init(&res->kref);
	res->hw_destroy = NULL;
	res->res_free = res_free;
	res->dev_priv = dev_priv;
	res->func = func;
	RB_CLEAR_NODE(&res->mob_node);
	INIT_LIST_HEAD(&res->lru_head);
	INIT_LIST_HEAD(&res->binding_head);
	res->id = -1;
	res->guest_memory_bo = NULL;
	res->guest_memory_offset = 0;
	res->guest_memory_dirty = false;
	res->res_dirty = false;
	res->coherent = false;
	res->used_prio = 3;
	res->dirty = NULL;
	if (delay_id)
		return 0;
	else
		return vmw_resource_alloc_id(res);
}


/**
 * vmw_user_resource_lookup_handle - lookup a struct resource from a
 * TTM user-space handle and perform basic type checks
 *
 * @dev_priv:     Pointer to a device private struct
 * @tfile:        Pointer to a struct ttm_object_file identifying the caller
 * @handle:       The TTM user-space handle
 * @converter:    Pointer to an object describing the resource type
 * @p_res:        On successful return the location pointed to will contain
 *                a pointer to a refcounted struct vmw_resource.
 *
 * If the handle can't be found or is associated with an incorrect resource
 * type, -EINVAL will be returned.
 */
int vmw_user_resource_lookup_handle(struct vmw_private *dev_priv,
				    struct ttm_object_file *tfile,
				    uint32_t handle,
				    const struct vmw_user_resource_conv
				    *converter,
				    struct vmw_resource **p_res)
{
	struct ttm_base_object *base;
	struct vmw_resource *res;
	int ret = -EINVAL;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(!base))
		return -EINVAL;

	if (unlikely(ttm_base_object_type(base) != converter->object_type))
		goto out_bad_resource;

	res = converter->base_obj_to_res(base);
	kref_get(&res->kref);

	*p_res = res;
	ret = 0;

out_bad_resource:
	ttm_base_object_unref(&base);

	return ret;
}

/*
 * Helper function that looks either a surface or bo.
 *
 * The pointer this pointed at by out_surf and out_buf needs to be null.
 */
int vmw_user_object_lookup(struct vmw_private *dev_priv,
			   struct drm_file *filp,
			   u32 handle,
			   struct vmw_user_object *uo)
{
	struct ttm_object_file *tfile = vmw_fpriv(filp)->tfile;
	struct vmw_resource *res;
	int ret;

	WARN_ON(uo->surface || uo->buffer);

	ret = vmw_user_resource_lookup_handle(dev_priv, tfile, handle,
					      user_surface_converter,
					      &res);
	if (!ret) {
		uo->surface = vmw_res_to_srf(res);
		return 0;
	}

	uo->surface = NULL;
	ret = vmw_user_bo_lookup(filp, handle, &uo->buffer);
	if (!ret && !uo->buffer->is_dumb) {
		uo->surface = vmw_lookup_surface_for_buffer(dev_priv,
							    uo->buffer,
							    handle);
		if (uo->surface)
			vmw_user_bo_unref(&uo->buffer);
	}

	return ret;
}

/**
 * vmw_resource_buf_alloc - Allocate a guest memory buffer for a resource.
 *
 * @res:            The resource for which to allocate a gbo buffer.
 * @interruptible:  Whether any sleeps during allocation should be
 *                  performed while interruptible.
 */
static int vmw_resource_buf_alloc(struct vmw_resource *res,
				  bool interruptible)
{
	unsigned long size = PFN_ALIGN(res->guest_memory_size);
	struct vmw_bo *gbo;
	struct vmw_bo_params bo_params = {
		.domain = res->func->domain,
		.busy_domain = res->func->busy_domain,
		.bo_type = ttm_bo_type_device,
		.size = res->guest_memory_size,
		.pin = false
	};
	int ret;

	if (likely(res->guest_memory_bo)) {
		BUG_ON(res->guest_memory_bo->tbo.base.size < size);
		return 0;
	}

	ret = vmw_gem_object_create(res->dev_priv, &bo_params, &gbo);
	if (unlikely(ret != 0))
		goto out_no_bo;

	res->guest_memory_bo = gbo;

out_no_bo:
	return ret;
}

/**
 * vmw_resource_do_validate - Make a resource up-to-date and visible
 *                            to the device.
 *
 * @res:            The resource to make visible to the device.
 * @val_buf:        Information about a buffer possibly
 *                  containing backup data if a bind operation is needed.
 * @dirtying:       Transfer dirty regions.
 *
 * On hardware resource shortage, this function returns -EBUSY and
 * should be retried once resources have been freed up.
 */
static int vmw_resource_do_validate(struct vmw_resource *res,
				    struct ttm_validate_buffer *val_buf,
				    bool dirtying)
{
	int ret = 0;
	const struct vmw_res_func *func = res->func;

	if (unlikely(res->id == -1)) {
		ret = func->create(res);
		if (unlikely(ret != 0))
			return ret;
	}

	if (func->bind &&
	    ((func->needs_guest_memory && !vmw_resource_mob_attached(res) &&
	      val_buf->bo) ||
	     (!func->needs_guest_memory && val_buf->bo))) {
		ret = func->bind(res, val_buf);
		if (unlikely(ret != 0))
			goto out_bind_failed;
		if (func->needs_guest_memory)
			vmw_resource_mob_attach(res);
	}

	/*
	 * Handle the case where the backup mob is marked coherent but
	 * the resource isn't.
	 */
	if (func->dirty_alloc && vmw_resource_mob_attached(res) &&
	    !res->coherent) {
		if (res->guest_memory_bo->dirty && !res->dirty) {
			ret = func->dirty_alloc(res);
			if (ret)
				return ret;
		} else if (!res->guest_memory_bo->dirty && res->dirty) {
			func->dirty_free(res);
		}
	}

	/*
	 * Transfer the dirty regions to the resource and update
	 * the resource.
	 */
	if (res->dirty) {
		if (dirtying && !res->res_dirty) {
			pgoff_t start = res->guest_memory_offset >> PAGE_SHIFT;
			pgoff_t end = __KERNEL_DIV_ROUND_UP
				(res->guest_memory_offset + res->guest_memory_size,
				 PAGE_SIZE);

			vmw_bo_dirty_unmap(res->guest_memory_bo, start, end);
		}

		vmw_bo_dirty_transfer_to_res(res);
		return func->dirty_sync(res);
	}

	return 0;

out_bind_failed:
	func->destroy(res);

	return ret;
}

/**
 * vmw_resource_unreserve - Unreserve a resource previously reserved for
 * command submission.
 *
 * @res:               Pointer to the struct vmw_resource to unreserve.
 * @dirty_set:         Change dirty status of the resource.
 * @dirty:             When changing dirty status indicates the new status.
 * @switch_guest_memory: Guest memory buffer has been switched.
 * @new_guest_memory_bo: Pointer to new guest memory buffer if command submission
 *                     switched. May be NULL.
 * @new_guest_memory_offset: New gbo offset if @switch_guest_memory is true.
 *
 * Currently unreserving a resource means putting it back on the device's
 * resource lru list, so that it can be evicted if necessary.
 */
void vmw_resource_unreserve(struct vmw_resource *res,
			    bool dirty_set,
			    bool dirty,
			    bool switch_guest_memory,
			    struct vmw_bo *new_guest_memory_bo,
			    unsigned long new_guest_memory_offset)
{
	struct vmw_private *dev_priv = res->dev_priv;

	if (!list_empty(&res->lru_head))
		return;

	if (switch_guest_memory && new_guest_memory_bo != res->guest_memory_bo) {
		if (res->guest_memory_bo) {
			vmw_resource_mob_detach(res);
			if (res->coherent)
				vmw_bo_dirty_release(res->guest_memory_bo);
			vmw_user_bo_unref(&res->guest_memory_bo);
		}

		if (new_guest_memory_bo) {
			res->guest_memory_bo = vmw_user_bo_ref(new_guest_memory_bo);

			/*
			 * The validation code should already have added a
			 * dirty tracker here.
			 */
			WARN_ON(res->coherent && !new_guest_memory_bo->dirty);

			vmw_resource_mob_attach(res);
		} else {
			res->guest_memory_bo = NULL;
		}
	} else if (switch_guest_memory && res->coherent) {
		vmw_bo_dirty_release(res->guest_memory_bo);
	}

	if (switch_guest_memory)
		res->guest_memory_offset = new_guest_memory_offset;

	if (dirty_set)
		res->res_dirty = dirty;

	if (!res->func->may_evict || res->id == -1 || res->pin_count)
		return;

	spin_lock(&dev_priv->resource_lock);
	list_add_tail(&res->lru_head,
		      &res->dev_priv->res_lru[res->func->res_type]);
	spin_unlock(&dev_priv->resource_lock);
}

/**
 * vmw_resource_check_buffer - Check whether a backup buffer is needed
 *                             for a resource and in that case, allocate
 *                             one, reserve and validate it.
 *
 * @ticket:         The ww acquire context to use, or NULL if trylocking.
 * @res:            The resource for which to allocate a backup buffer.
 * @interruptible:  Whether any sleeps during allocation should be
 *                  performed while interruptible.
 * @val_buf:        On successful return contains data about the
 *                  reserved and validated backup buffer.
 */
static int
vmw_resource_check_buffer(struct ww_acquire_ctx *ticket,
			  struct vmw_resource *res,
			  bool interruptible,
			  struct ttm_validate_buffer *val_buf)
{
	struct ttm_operation_ctx ctx = { true, false };
	struct list_head val_list;
	bool guest_memory_dirty = false;
	int ret;

	if (unlikely(!res->guest_memory_bo)) {
		ret = vmw_resource_buf_alloc(res, interruptible);
		if (unlikely(ret != 0))
			return ret;
	}

	INIT_LIST_HEAD(&val_list);
	ttm_bo_get(&res->guest_memory_bo->tbo);
	val_buf->bo = &res->guest_memory_bo->tbo;
	val_buf->num_shared = 0;
	list_add_tail(&val_buf->head, &val_list);
	ret = ttm_eu_reserve_buffers(ticket, &val_list, interruptible, NULL);
	if (unlikely(ret != 0))
		goto out_no_reserve;

	if (res->func->needs_guest_memory && !vmw_resource_mob_attached(res))
		return 0;

	guest_memory_dirty = res->guest_memory_dirty;
	vmw_bo_placement_set(res->guest_memory_bo, res->func->domain,
			     res->func->busy_domain);
	ret = ttm_bo_validate(&res->guest_memory_bo->tbo,
			      &res->guest_memory_bo->placement,
			      &ctx);

	if (unlikely(ret != 0))
		goto out_no_validate;

	return 0;

out_no_validate:
	ttm_eu_backoff_reservation(ticket, &val_list);
out_no_reserve:
	ttm_bo_put(val_buf->bo);
	val_buf->bo = NULL;
	if (guest_memory_dirty)
		vmw_user_bo_unref(&res->guest_memory_bo);

	return ret;
}

/*
 * vmw_resource_reserve - Reserve a resource for command submission
 *
 * @res:            The resource to reserve.
 *
 * This function takes the resource off the LRU list and make sure
 * a guest memory buffer is present for guest-backed resources.
 * However, the buffer may not be bound to the resource at this
 * point.
 *
 */
int vmw_resource_reserve(struct vmw_resource *res, bool interruptible,
			 bool no_guest_memory)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;

	spin_lock(&dev_priv->resource_lock);
	list_del_init(&res->lru_head);
	spin_unlock(&dev_priv->resource_lock);

	if (res->func->needs_guest_memory && !res->guest_memory_bo &&
	    !no_guest_memory) {
		ret = vmw_resource_buf_alloc(res, interruptible);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed to allocate a guest memory buffer "
				  "of size %lu. bytes\n",
				  (unsigned long) res->guest_memory_size);
			return ret;
		}
	}

	return 0;
}

/**
 * vmw_resource_backoff_reservation - Unreserve and unreference a
 *                                    guest memory buffer
 *.
 * @ticket:         The ww acquire ctx used for reservation.
 * @val_buf:        Guest memory buffer information.
 */
static void
vmw_resource_backoff_reservation(struct ww_acquire_ctx *ticket,
				 struct ttm_validate_buffer *val_buf)
{
	struct list_head val_list;

	if (likely(val_buf->bo == NULL))
		return;

	INIT_LIST_HEAD(&val_list);
	list_add_tail(&val_buf->head, &val_list);
	ttm_eu_backoff_reservation(ticket, &val_list);
	ttm_bo_put(val_buf->bo);
	val_buf->bo = NULL;
}

/**
 * vmw_resource_do_evict - Evict a resource, and transfer its data
 *                         to a backup buffer.
 *
 * @ticket:         The ww acquire ticket to use, or NULL if trylocking.
 * @res:            The resource to evict.
 * @interruptible:  Whether to wait interruptible.
 */
static int vmw_resource_do_evict(struct ww_acquire_ctx *ticket,
				 struct vmw_resource *res, bool interruptible)
{
	struct ttm_validate_buffer val_buf;
	const struct vmw_res_func *func = res->func;
	int ret;

	BUG_ON(!func->may_evict);

	val_buf.bo = NULL;
	val_buf.num_shared = 0;
	ret = vmw_resource_check_buffer(ticket, res, interruptible, &val_buf);
	if (unlikely(ret != 0))
		return ret;

	if (unlikely(func->unbind != NULL &&
		     (!func->needs_guest_memory || vmw_resource_mob_attached(res)))) {
		ret = func->unbind(res, res->res_dirty, &val_buf);
		if (unlikely(ret != 0))
			goto out_no_unbind;
		vmw_resource_mob_detach(res);
	}
	ret = func->destroy(res);
	res->guest_memory_dirty = true;
	res->res_dirty = false;
out_no_unbind:
	vmw_resource_backoff_reservation(ticket, &val_buf);

	return ret;
}


/**
 * vmw_resource_validate - Make a resource up-to-date and visible
 *                         to the device.
 * @res: The resource to make visible to the device.
 * @intr: Perform waits interruptible if possible.
 * @dirtying: Pending GPU operation will dirty the resource
 *
 * On successful return, any backup DMA buffer pointed to by @res->backup will
 * be reserved and validated.
 * On hardware resource shortage, this function will repeatedly evict
 * resources of the same type until the validation succeeds.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted, negative error code
 * on failure.
 */
int vmw_resource_validate(struct vmw_resource *res, bool intr,
			  bool dirtying)
{
	int ret;
	struct vmw_resource *evict_res;
	struct vmw_private *dev_priv = res->dev_priv;
	struct list_head *lru_list = &dev_priv->res_lru[res->func->res_type];
	struct ttm_validate_buffer val_buf;
	unsigned err_count = 0;

	if (!res->func->create)
		return 0;

	val_buf.bo = NULL;
	val_buf.num_shared = 0;
	if (res->guest_memory_bo)
		val_buf.bo = &res->guest_memory_bo->tbo;
	do {
		ret = vmw_resource_do_validate(res, &val_buf, dirtying);
		if (likely(ret != -EBUSY))
			break;

		spin_lock(&dev_priv->resource_lock);
		if (list_empty(lru_list) || !res->func->may_evict) {
			DRM_ERROR("Out of device device resources "
				  "for %s.\n", res->func->type_name);
			ret = -EBUSY;
			spin_unlock(&dev_priv->resource_lock);
			break;
		}

		evict_res = vmw_resource_reference
			(list_first_entry(lru_list, struct vmw_resource,
					  lru_head));
		list_del_init(&evict_res->lru_head);

		spin_unlock(&dev_priv->resource_lock);

		/* Trylock backup buffers with a NULL ticket. */
		ret = vmw_resource_do_evict(NULL, evict_res, intr);
		if (unlikely(ret != 0)) {
			spin_lock(&dev_priv->resource_lock);
			list_add_tail(&evict_res->lru_head, lru_list);
			spin_unlock(&dev_priv->resource_lock);
			if (ret == -ERESTARTSYS ||
			    ++err_count > VMW_RES_EVICT_ERR_COUNT) {
				vmw_resource_unreference(&evict_res);
				goto out_no_validate;
			}
		}

		vmw_resource_unreference(&evict_res);
	} while (1);

	if (unlikely(ret != 0))
		goto out_no_validate;
	else if (!res->func->needs_guest_memory && res->guest_memory_bo) {
		WARN_ON_ONCE(vmw_resource_mob_attached(res));
		vmw_user_bo_unref(&res->guest_memory_bo);
	}

	return 0;

out_no_validate:
	return ret;
}


/**
 * vmw_resource_unbind_list
 *
 * @vbo: Pointer to the current backing MOB.
 *
 * Evicts the Guest Backed hardware resource if the backup
 * buffer is being moved out of MOB memory.
 * Note that this function will not race with the resource
 * validation code, since resource validation and eviction
 * both require the backup buffer to be reserved.
 */
void vmw_resource_unbind_list(struct vmw_bo *vbo)
{
	struct ttm_validate_buffer val_buf = {
		.bo = &vbo->tbo,
		.num_shared = 0
	};

	dma_resv_assert_held(vbo->tbo.base.resv);
	while (!RB_EMPTY_ROOT(&vbo->res_tree)) {
		struct rb_node *node = vbo->res_tree.rb_node;
		struct vmw_resource *res =
			container_of(node, struct vmw_resource, mob_node);

		if (!WARN_ON_ONCE(!res->func->unbind))
			(void) res->func->unbind(res, res->res_dirty, &val_buf);

		res->guest_memory_size = true;
		res->res_dirty = false;
		vmw_resource_mob_detach(res);
	}

	(void) ttm_bo_wait(&vbo->tbo, false, false);
}


/**
 * vmw_query_readback_all - Read back cached query states
 *
 * @dx_query_mob: Buffer containing the DX query MOB
 *
 * Read back cached states from the device if they exist.  This function
 * assumes binding_mutex is held.
 */
int vmw_query_readback_all(struct vmw_bo *dx_query_mob)
{
	struct vmw_resource *dx_query_ctx;
	struct vmw_private *dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXReadbackAllQuery body;
	} *cmd;


	/* No query bound, so do nothing */
	if (!dx_query_mob || !dx_query_mob->dx_query_ctx)
		return 0;

	dx_query_ctx = dx_query_mob->dx_query_ctx;
	dev_priv     = dx_query_ctx->dev_priv;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), dx_query_ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id   = SVGA_3D_CMD_DX_READBACK_ALL_QUERY;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid    = dx_query_ctx->id;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	/* Triggers a rebind the next time affected context is bound */
	dx_query_mob->dx_query_ctx = NULL;

	return 0;
}



/**
 * vmw_query_move_notify - Read back cached query states
 *
 * @bo: The TTM buffer object about to move.
 * @old_mem: The memory region @bo is moving from.
 * @new_mem: The memory region @bo is moving to.
 *
 * Called before the query MOB is swapped out to read back cached query
 * states from the device.
 */
void vmw_query_move_notify(struct ttm_buffer_object *bo,
			   struct ttm_resource *old_mem,
			   struct ttm_resource *new_mem)
{
	struct vmw_bo *dx_query_mob;
	struct ttm_device *bdev = bo->bdev;
	struct vmw_private *dev_priv = vmw_priv_from_ttm(bdev);

	mutex_lock(&dev_priv->binding_mutex);

	/* If BO is being moved from MOB to system memory */
	if (old_mem &&
	    new_mem->mem_type == TTM_PL_SYSTEM &&
	    old_mem->mem_type == VMW_PL_MOB) {
		struct vmw_fence_obj *fence;

		dx_query_mob = to_vmw_bo(&bo->base);
		if (!dx_query_mob || !dx_query_mob->dx_query_ctx) {
			mutex_unlock(&dev_priv->binding_mutex);
			return;
		}

		(void) vmw_query_readback_all(dx_query_mob);
		mutex_unlock(&dev_priv->binding_mutex);

		/* Create a fence and attach the BO to it */
		(void) vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
		vmw_bo_fence_single(bo, fence);

		if (fence != NULL)
			vmw_fence_obj_unreference(&fence);

		(void) ttm_bo_wait(bo, false, false);
	} else
		mutex_unlock(&dev_priv->binding_mutex);
}

/**
 * vmw_resource_needs_backup - Return whether a resource needs a backup buffer.
 *
 * @res:            The resource being queried.
 */
bool vmw_resource_needs_backup(const struct vmw_resource *res)
{
	return res->func->needs_guest_memory;
}

/**
 * vmw_resource_evict_type - Evict all resources of a specific type
 *
 * @dev_priv:       Pointer to a device private struct
 * @type:           The resource type to evict
 *
 * To avoid thrashing starvation or as part of the hibernation sequence,
 * try to evict all evictable resources of a specific type.
 */
static void vmw_resource_evict_type(struct vmw_private *dev_priv,
				    enum vmw_res_type type)
{
	struct list_head *lru_list = &dev_priv->res_lru[type];
	struct vmw_resource *evict_res;
	unsigned err_count = 0;
	int ret;
	struct ww_acquire_ctx ticket;

	do {
		spin_lock(&dev_priv->resource_lock);

		if (list_empty(lru_list))
			goto out_unlock;

		evict_res = vmw_resource_reference(
			list_first_entry(lru_list, struct vmw_resource,
					 lru_head));
		list_del_init(&evict_res->lru_head);
		spin_unlock(&dev_priv->resource_lock);

		/* Wait lock backup buffers with a ticket. */
		ret = vmw_resource_do_evict(&ticket, evict_res, false);
		if (unlikely(ret != 0)) {
			spin_lock(&dev_priv->resource_lock);
			list_add_tail(&evict_res->lru_head, lru_list);
			spin_unlock(&dev_priv->resource_lock);
			if (++err_count > VMW_RES_EVICT_ERR_COUNT) {
				vmw_resource_unreference(&evict_res);
				return;
			}
		}

		vmw_resource_unreference(&evict_res);
	} while (1);

out_unlock:
	spin_unlock(&dev_priv->resource_lock);
}

/**
 * vmw_resource_evict_all - Evict all evictable resources
 *
 * @dev_priv:       Pointer to a device private struct
 *
 * To avoid thrashing starvation or as part of the hibernation sequence,
 * evict all evictable resources. In particular this means that all
 * guest-backed resources that are registered with the device are
 * evicted and the OTable becomes clean.
 */
void vmw_resource_evict_all(struct vmw_private *dev_priv)
{
	enum vmw_res_type type;

	mutex_lock(&dev_priv->cmdbuf_mutex);

	for (type = 0; type < vmw_res_max; ++type)
		vmw_resource_evict_type(dev_priv, type);

	mutex_unlock(&dev_priv->cmdbuf_mutex);
}

/*
 * vmw_resource_pin - Add a pin reference on a resource
 *
 * @res: The resource to add a pin reference on
 *
 * This function adds a pin reference, and if needed validates the resource.
 * Having a pin reference means that the resource can never be evicted, and
 * its id will never change as long as there is a pin reference.
 * This function returns 0 on success and a negative error code on failure.
 */
int vmw_resource_pin(struct vmw_resource *res, bool interruptible)
{
	struct ttm_operation_ctx ctx = { interruptible, false };
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;

	mutex_lock(&dev_priv->cmdbuf_mutex);
	ret = vmw_resource_reserve(res, interruptible, false);
	if (ret)
		goto out_no_reserve;

	if (res->pin_count == 0) {
		struct vmw_bo *vbo = NULL;

		if (res->guest_memory_bo) {
			vbo = res->guest_memory_bo;

			ret = ttm_bo_reserve(&vbo->tbo, interruptible, false, NULL);
			if (ret)
				goto out_no_validate;
			if (!vbo->tbo.pin_count) {
				vmw_bo_placement_set(vbo,
						     res->func->domain,
						     res->func->busy_domain);
				ret = ttm_bo_validate
					(&vbo->tbo,
					 &vbo->placement,
					 &ctx);
				if (ret) {
					ttm_bo_unreserve(&vbo->tbo);
					goto out_no_validate;
				}
			}

			/* Do we really need to pin the MOB as well? */
			vmw_bo_pin_reserved(vbo, true);
		}
		ret = vmw_resource_validate(res, interruptible, true);
		if (vbo)
			ttm_bo_unreserve(&vbo->tbo);
		if (ret)
			goto out_no_validate;
	}
	res->pin_count++;

out_no_validate:
	vmw_resource_unreserve(res, false, false, false, NULL, 0UL);
out_no_reserve:
	mutex_unlock(&dev_priv->cmdbuf_mutex);

	return ret;
}

/**
 * vmw_resource_unpin - Remove a pin reference from a resource
 *
 * @res: The resource to remove a pin reference from
 *
 * Having a pin reference means that the resource can never be evicted, and
 * its id will never change as long as there is a pin reference.
 */
void vmw_resource_unpin(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;

	mutex_lock(&dev_priv->cmdbuf_mutex);

	ret = vmw_resource_reserve(res, false, true);
	WARN_ON(ret);

	WARN_ON(res->pin_count == 0);
	if (--res->pin_count == 0 && res->guest_memory_bo) {
		struct vmw_bo *vbo = res->guest_memory_bo;

		(void) ttm_bo_reserve(&vbo->tbo, false, false, NULL);
		vmw_bo_pin_reserved(vbo, false);
		ttm_bo_unreserve(&vbo->tbo);
	}

	vmw_resource_unreserve(res, false, false, false, NULL, 0UL);

	mutex_unlock(&dev_priv->cmdbuf_mutex);
}

/**
 * vmw_res_type - Return the resource type
 *
 * @res: Pointer to the resource
 */
enum vmw_res_type vmw_res_type(const struct vmw_resource *res)
{
	return res->func->res_type;
}

/**
 * vmw_resource_dirty_update - Update a resource's dirty tracker with a
 * sequential range of touched backing store memory.
 * @res: The resource.
 * @start: The first page touched.
 * @end: The last page touched + 1.
 */
void vmw_resource_dirty_update(struct vmw_resource *res, pgoff_t start,
			       pgoff_t end)
{
	if (res->dirty)
		res->func->dirty_range_add(res, start << PAGE_SHIFT,
					   end << PAGE_SHIFT);
}

int vmw_resource_clean(struct vmw_resource *res)
{
	int ret = 0;

	if (res->res_dirty) {
		if (!res->func->clean)
			return -EINVAL;

		ret = res->func->clean(res);
		if (ret)
			return ret;
		res->res_dirty = false;
	}
	return ret;
}

/**
 * vmw_resources_clean - Clean resources intersecting a mob range
 * @vbo: The mob buffer object
 * @start: The mob page offset starting the range
 * @end: The mob page offset ending the range
 * @num_prefault: Returns how many pages including the first have been
 * cleaned and are ok to prefault
 */
int vmw_resources_clean(struct vmw_bo *vbo, pgoff_t start,
			pgoff_t end, pgoff_t *num_prefault)
{
	struct rb_node *cur = vbo->res_tree.rb_node;
	struct vmw_resource *found = NULL;
	unsigned long res_start = start << PAGE_SHIFT;
	unsigned long res_end = end << PAGE_SHIFT;
	unsigned long last_cleaned = 0;
	int ret;

	/*
	 * Find the resource with lowest backup_offset that intersects the
	 * range.
	 */
	while (cur) {
		struct vmw_resource *cur_res =
			container_of(cur, struct vmw_resource, mob_node);

		if (cur_res->guest_memory_offset >= res_end) {
			cur = cur->rb_left;
		} else if (cur_res->guest_memory_offset + cur_res->guest_memory_size <=
			   res_start) {
			cur = cur->rb_right;
		} else {
			found = cur_res;
			cur = cur->rb_left;
			/* Continue to look for resources with lower offsets */
		}
	}

	/*
	 * In order of increasing guest_memory_offset, clean dirty resources
	 * intersecting the range.
	 */
	while (found) {
		ret = vmw_resource_clean(found);
		if (ret)
			return ret;
		last_cleaned = found->guest_memory_offset + found->guest_memory_size;
		cur = rb_next(&found->mob_node);
		if (!cur)
			break;

		found = container_of(cur, struct vmw_resource, mob_node);
		if (found->guest_memory_offset >= res_end)
			break;
	}

	/*
	 * Set number of pages allowed prefaulting and fence the buffer object
	 */
	*num_prefault = 1;
	if (last_cleaned > res_start) {
		struct ttm_buffer_object *bo = &vbo->tbo;

		*num_prefault = __KERNEL_DIV_ROUND_UP(last_cleaned - res_start,
						      PAGE_SIZE);
		vmw_bo_fence_single(bo, NULL);
	}

	return 0;
}
