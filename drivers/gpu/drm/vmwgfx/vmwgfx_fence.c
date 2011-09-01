/**************************************************************************
 *
 * Copyright © 2011 VMware, Inc., Palo Alto, CA., USA
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

#include "drmP.h"
#include "vmwgfx_drv.h"

#define VMW_FENCE_WRAP (1 << 31)

struct vmw_fence_manager {
	int num_fence_objects;
	struct vmw_private *dev_priv;
	spinlock_t lock;
	u32 next_seqno;
	struct list_head fence_list;
	struct work_struct work;
	u32 user_fence_size;
	u32 fence_size;
	bool fifo_down;
	struct list_head cleanup_list;
};

struct vmw_user_fence {
	struct ttm_base_object base;
	struct vmw_fence_obj fence;
};

/**
 * vmw_fence_destroy_locked
 *
 */

static void vmw_fence_obj_destroy_locked(struct kref *kref)
{
	struct vmw_fence_obj *fence =
		container_of(kref, struct vmw_fence_obj, kref);

	struct vmw_fence_manager *fman = fence->fman;
	unsigned int num_fences;

	list_del_init(&fence->head);
	num_fences = --fman->num_fence_objects;
	spin_unlock_irq(&fman->lock);
	if (fence->destroy)
		fence->destroy(fence);
	else
		kfree(fence);

	spin_lock_irq(&fman->lock);
}


/**
 * Execute signal actions on fences recently signaled.
 * This is done from a workqueue so we don't have to execute
 * signal actions from atomic context.
 */

static void vmw_fence_work_func(struct work_struct *work)
{
	struct vmw_fence_manager *fman =
		container_of(work, struct vmw_fence_manager, work);
	struct list_head list;
	struct vmw_fence_action *action, *next_action;

	do {
		INIT_LIST_HEAD(&list);
		spin_lock_irq(&fman->lock);
		list_splice_init(&fman->cleanup_list, &list);
		spin_unlock_irq(&fman->lock);

		if (list_empty(&list))
			return;

		/*
		 * At this point, only we should be able to manipulate the
		 * list heads of the actions we have on the private list.
		 */

		list_for_each_entry_safe(action, next_action, &list, head) {
			list_del_init(&action->head);
			action->cleanup(action);
		}
	} while (1);
}

struct vmw_fence_manager *vmw_fence_manager_init(struct vmw_private *dev_priv)
{
	struct vmw_fence_manager *fman = kzalloc(sizeof(*fman), GFP_KERNEL);

	if (unlikely(fman == NULL))
		return NULL;

	fman->dev_priv = dev_priv;
	spin_lock_init(&fman->lock);
	INIT_LIST_HEAD(&fman->fence_list);
	INIT_LIST_HEAD(&fman->cleanup_list);
	INIT_WORK(&fman->work, &vmw_fence_work_func);
	fman->fifo_down = true;
	fman->user_fence_size = ttm_round_pot(sizeof(struct vmw_user_fence));
	fman->fence_size = ttm_round_pot(sizeof(struct vmw_fence_obj));

	return fman;
}

void vmw_fence_manager_takedown(struct vmw_fence_manager *fman)
{
	unsigned long irq_flags;
	bool lists_empty;

	(void) cancel_work_sync(&fman->work);

	spin_lock_irqsave(&fman->lock, irq_flags);
	lists_empty = list_empty(&fman->fence_list) &&
		list_empty(&fman->cleanup_list);
	spin_unlock_irqrestore(&fman->lock, irq_flags);

	BUG_ON(!lists_empty);
	kfree(fman);
}

static int vmw_fence_obj_init(struct vmw_fence_manager *fman,
			      struct vmw_fence_obj *fence,
			      u32 seqno,
			      uint32_t mask,
			      void (*destroy) (struct vmw_fence_obj *fence))
{
	unsigned long irq_flags;
	unsigned int num_fences;
	int ret = 0;

	fence->seqno = seqno;
	INIT_LIST_HEAD(&fence->seq_passed_actions);
	fence->fman = fman;
	fence->signaled = 0;
	fence->signal_mask = mask;
	kref_init(&fence->kref);
	fence->destroy = destroy;
	init_waitqueue_head(&fence->queue);

	spin_lock_irqsave(&fman->lock, irq_flags);
	if (unlikely(fman->fifo_down)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	list_add_tail(&fence->head, &fman->fence_list);
	num_fences = ++fman->num_fence_objects;

out_unlock:
	spin_unlock_irqrestore(&fman->lock, irq_flags);
	return ret;

}

struct vmw_fence_obj *vmw_fence_obj_reference(struct vmw_fence_obj *fence)
{
	kref_get(&fence->kref);
	return fence;
}

/**
 * vmw_fence_obj_unreference
 *
 * Note that this function may not be entered with disabled irqs since
 * it may re-enable them in the destroy function.
 *
 */
void vmw_fence_obj_unreference(struct vmw_fence_obj **fence_p)
{
	struct vmw_fence_obj *fence = *fence_p;
	struct vmw_fence_manager *fman = fence->fman;

	*fence_p = NULL;
	spin_lock_irq(&fman->lock);
	BUG_ON(atomic_read(&fence->kref.refcount) == 0);
	kref_put(&fence->kref, vmw_fence_obj_destroy_locked);
	spin_unlock_irq(&fman->lock);
}

void vmw_fences_perform_actions(struct vmw_fence_manager *fman,
				struct list_head *list)
{
	struct vmw_fence_action *action, *next_action;

	list_for_each_entry_safe(action, next_action, list, head) {
		list_del_init(&action->head);
		if (action->seq_passed != NULL)
			action->seq_passed(action);

		/*
		 * Add the cleanup action to the cleanup list so that
		 * it will be performed by a worker task.
		 */

		if (action->cleanup != NULL)
			list_add_tail(&action->head, &fman->cleanup_list);
	}
}

void vmw_fences_update(struct vmw_fence_manager *fman, u32 seqno)
{
	unsigned long flags;
	struct vmw_fence_obj *fence, *next_fence;
	struct list_head action_list;

	spin_lock_irqsave(&fman->lock, flags);
	list_for_each_entry_safe(fence, next_fence, &fman->fence_list, head) {
		if (seqno - fence->seqno < VMW_FENCE_WRAP) {
			list_del_init(&fence->head);
			fence->signaled |= DRM_VMW_FENCE_FLAG_EXEC;
			INIT_LIST_HEAD(&action_list);
			list_splice_init(&fence->seq_passed_actions,
					 &action_list);
			vmw_fences_perform_actions(fman, &action_list);
			wake_up_all(&fence->queue);
		}

	}
	if (!list_empty(&fman->cleanup_list))
		(void) schedule_work(&fman->work);
	spin_unlock_irqrestore(&fman->lock, flags);
}


bool vmw_fence_obj_signaled(struct vmw_fence_obj *fence,
			    uint32_t flags)
{
	struct vmw_fence_manager *fman = fence->fman;
	unsigned long irq_flags;
	uint32_t signaled;

	spin_lock_irqsave(&fman->lock, irq_flags);
	signaled = fence->signaled;
	spin_unlock_irqrestore(&fman->lock, irq_flags);

	flags &= fence->signal_mask;
	if ((signaled & flags) == flags)
		return 1;

	if ((signaled & DRM_VMW_FENCE_FLAG_EXEC) == 0) {
		struct vmw_private *dev_priv = fman->dev_priv;
		__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
		u32 seqno;

		seqno = ioread32(fifo_mem + SVGA_FIFO_FENCE);
		vmw_fences_update(fman, seqno);
	}

	spin_lock_irqsave(&fman->lock, irq_flags);
	signaled = fence->signaled;
	spin_unlock_irqrestore(&fman->lock, irq_flags);

	return ((signaled & flags) == flags);
}

int vmw_fence_obj_wait(struct vmw_fence_obj *fence,
		       uint32_t flags, bool lazy,
		       bool interruptible, unsigned long timeout)
{
	struct vmw_private *dev_priv = fence->fman->dev_priv;
	long ret;

	if (likely(vmw_fence_obj_signaled(fence, flags)))
		return 0;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	vmw_seqno_waiter_add(dev_priv);

	if (interruptible)
		ret = wait_event_interruptible_timeout
			(fence->queue,
			 vmw_fence_obj_signaled(fence, flags),
			 timeout);
	else
		ret = wait_event_timeout
			(fence->queue,
			 vmw_fence_obj_signaled(fence, flags),
			 timeout);

	vmw_seqno_waiter_remove(dev_priv);

	if (unlikely(ret == 0))
		ret = -EBUSY;
	else if (likely(ret > 0))
		ret = 0;

	return ret;
}

void vmw_fence_obj_flush(struct vmw_fence_obj *fence)
{
	struct vmw_private *dev_priv = fence->fman->dev_priv;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
}

static void vmw_fence_destroy(struct vmw_fence_obj *fence)
{
	struct vmw_fence_manager *fman = fence->fman;

	kfree(fence);
	/*
	 * Free kernel space accounting.
	 */
	ttm_mem_global_free(vmw_mem_glob(fman->dev_priv),
			    fman->fence_size);
}

int vmw_fence_create(struct vmw_fence_manager *fman,
		     uint32_t seqno,
		     uint32_t mask,
		     struct vmw_fence_obj **p_fence)
{
	struct ttm_mem_global *mem_glob = vmw_mem_glob(fman->dev_priv);
	struct vmw_fence_obj *fence;
	int ret;

	ret = ttm_mem_global_alloc(mem_glob, fman->fence_size,
				   false, false);
	if (unlikely(ret != 0))
		return ret;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (unlikely(fence == NULL)) {
		ret = -ENOMEM;
		goto out_no_object;
	}

	ret = vmw_fence_obj_init(fman, fence, seqno, mask,
				 vmw_fence_destroy);
	if (unlikely(ret != 0))
		goto out_err_init;

	*p_fence = fence;
	return 0;

out_err_init:
	kfree(fence);
out_no_object:
	ttm_mem_global_free(mem_glob, fman->fence_size);
	return ret;
}


static void vmw_user_fence_destroy(struct vmw_fence_obj *fence)
{
	struct vmw_user_fence *ufence =
		container_of(fence, struct vmw_user_fence, fence);
	struct vmw_fence_manager *fman = fence->fman;

	kfree(ufence);
	/*
	 * Free kernel space accounting.
	 */
	ttm_mem_global_free(vmw_mem_glob(fman->dev_priv),
			    fman->user_fence_size);
}

static void vmw_user_fence_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_fence *ufence =
		container_of(base, struct vmw_user_fence, base);
	struct vmw_fence_obj *fence = &ufence->fence;

	*p_base = NULL;
	vmw_fence_obj_unreference(&fence);
}

int vmw_user_fence_create(struct drm_file *file_priv,
			  struct vmw_fence_manager *fman,
			  uint32_t seqno,
			  uint32_t mask,
			  struct vmw_fence_obj **p_fence,
			  uint32_t *p_handle)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_user_fence *ufence;
	struct vmw_fence_obj *tmp;
	struct ttm_mem_global *mem_glob = vmw_mem_glob(fman->dev_priv);
	int ret;

	/*
	 * Kernel memory space accounting, since this object may
	 * be created by a user-space request.
	 */

	ret = ttm_mem_global_alloc(mem_glob, fman->user_fence_size,
				   false, false);
	if (unlikely(ret != 0))
		return ret;

	ufence = kzalloc(sizeof(*ufence), GFP_KERNEL);
	if (unlikely(ufence == NULL)) {
		ret = -ENOMEM;
		goto out_no_object;
	}

	ret = vmw_fence_obj_init(fman, &ufence->fence, seqno,
				 mask, vmw_user_fence_destroy);
	if (unlikely(ret != 0)) {
		kfree(ufence);
		goto out_no_object;
	}

	/*
	 * The base object holds a reference which is freed in
	 * vmw_user_fence_base_release.
	 */
	tmp = vmw_fence_obj_reference(&ufence->fence);
	ret = ttm_base_object_init(tfile, &ufence->base, false,
				   VMW_RES_FENCE,
				   &vmw_user_fence_base_release, NULL);


	if (unlikely(ret != 0)) {
		/*
		 * Free the base object's reference
		 */
		vmw_fence_obj_unreference(&tmp);
		goto out_err;
	}

	*p_fence = &ufence->fence;
	*p_handle = ufence->base.hash.key;

	return 0;
out_err:
	tmp = &ufence->fence;
	vmw_fence_obj_unreference(&tmp);
out_no_object:
	ttm_mem_global_free(mem_glob, fman->user_fence_size);
	return ret;
}


/**
 * vmw_fence_fifo_down - signal all unsignaled fence objects.
 */

void vmw_fence_fifo_down(struct vmw_fence_manager *fman)
{
	unsigned long irq_flags;
	struct list_head action_list;
	int ret;

	/*
	 * The list may be altered while we traverse it, so always
	 * restart when we've released the fman->lock.
	 */

	spin_lock_irqsave(&fman->lock, irq_flags);
	fman->fifo_down = true;
	while (!list_empty(&fman->fence_list)) {
		struct vmw_fence_obj *fence =
			list_entry(fman->fence_list.prev, struct vmw_fence_obj,
				   head);
		kref_get(&fence->kref);
		spin_unlock_irq(&fman->lock);

		ret = vmw_fence_obj_wait(fence, fence->signal_mask,
					 false, false,
					 VMW_FENCE_WAIT_TIMEOUT);

		if (unlikely(ret != 0)) {
			list_del_init(&fence->head);
			fence->signaled |= DRM_VMW_FENCE_FLAG_EXEC;
			INIT_LIST_HEAD(&action_list);
			list_splice_init(&fence->seq_passed_actions,
					 &action_list);
			vmw_fences_perform_actions(fman, &action_list);
			wake_up_all(&fence->queue);
		}

		spin_lock_irq(&fman->lock);

		BUG_ON(!list_empty(&fence->head));
		kref_put(&fence->kref, vmw_fence_obj_destroy_locked);
	}
	spin_unlock_irqrestore(&fman->lock, irq_flags);
}

void vmw_fence_fifo_up(struct vmw_fence_manager *fman)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&fman->lock, irq_flags);
	fman->fifo_down = false;
	spin_unlock_irqrestore(&fman->lock, irq_flags);
}


int vmw_fence_obj_wait_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct drm_vmw_fence_wait_arg *arg =
	    (struct drm_vmw_fence_wait_arg *)data;
	unsigned long timeout;
	struct ttm_base_object *base;
	struct vmw_fence_obj *fence;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret;
	uint64_t wait_timeout = ((uint64_t)arg->timeout_us * HZ);

	/*
	 * 64-bit division not present on 32-bit systems, so do an
	 * approximation. (Divide by 1000000).
	 */

	wait_timeout = (wait_timeout >> 20) + (wait_timeout >> 24) -
	  (wait_timeout >> 26);

	if (!arg->cookie_valid) {
		arg->cookie_valid = 1;
		arg->kernel_cookie = jiffies + wait_timeout;
	}

	base = ttm_base_object_lookup(tfile, arg->handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR "Wait invalid fence object handle "
		       "0x%08lx.\n",
		       (unsigned long)arg->handle);
		return -EINVAL;
	}

	fence = &(container_of(base, struct vmw_user_fence, base)->fence);

	timeout = jiffies;
	if (time_after_eq(timeout, (unsigned long)arg->kernel_cookie)) {
		ret = ((vmw_fence_obj_signaled(fence, arg->flags)) ?
		       0 : -EBUSY);
		goto out;
	}

	timeout = (unsigned long)arg->kernel_cookie - timeout;

	ret = vmw_fence_obj_wait(fence, arg->flags, arg->lazy, true, timeout);

out:
	ttm_base_object_unref(&base);

	/*
	 * Optionally unref the fence object.
	 */

	if (ret == 0 && (arg->wait_options & DRM_VMW_WAIT_OPTION_UNREF))
		return ttm_ref_object_base_unref(tfile, arg->handle,
						 TTM_REF_USAGE);
	return ret;
}

int vmw_fence_obj_signaled_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_vmw_fence_signaled_arg *arg =
		(struct drm_vmw_fence_signaled_arg *) data;
	struct ttm_base_object *base;
	struct vmw_fence_obj *fence;
	struct vmw_fence_manager *fman;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_private *dev_priv = vmw_priv(dev);

	base = ttm_base_object_lookup(tfile, arg->handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR "Fence signaled invalid fence object handle "
		       "0x%08lx.\n",
		       (unsigned long)arg->handle);
		return -EINVAL;
	}

	fence = &(container_of(base, struct vmw_user_fence, base)->fence);
	fman = fence->fman;

	arg->signaled = vmw_fence_obj_signaled(fence, arg->flags);
	spin_lock_irq(&fman->lock);

	arg->signaled_flags = fence->signaled;
	arg->passed_seqno = dev_priv->last_read_seqno;
	spin_unlock_irq(&fman->lock);

	ttm_base_object_unref(&base);

	return 0;
}


int vmw_fence_obj_unref_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_vmw_fence_arg *arg =
		(struct drm_vmw_fence_arg *) data;

	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 arg->handle,
					 TTM_REF_USAGE);
}
