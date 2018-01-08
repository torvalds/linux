/**************************************************************************
 *
 * Copyright © 2011-2014 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/drmP.h>
#include "vmwgfx_drv.h"

#define VMW_FENCE_WRAP (1 << 31)

struct vmw_fence_manager {
	int num_fence_objects;
	struct vmw_private *dev_priv;
	spinlock_t lock;
	struct list_head fence_list;
	struct work_struct work;
	u32 user_fence_size;
	u32 fence_size;
	u32 event_fence_action_size;
	bool fifo_down;
	struct list_head cleanup_list;
	uint32_t pending_actions[VMW_ACTION_MAX];
	struct mutex goal_irq_mutex;
	bool goal_irq_on; /* Protected by @goal_irq_mutex */
	bool seqno_valid; /* Protected by @lock, and may not be set to true
			     without the @goal_irq_mutex held. */
	u64 ctx;
};

struct vmw_user_fence {
	struct ttm_base_object base;
	struct vmw_fence_obj fence;
};

/**
 * struct vmw_event_fence_action - fence action that delivers a drm event.
 *
 * @e: A struct drm_pending_event that controls the event delivery.
 * @action: A struct vmw_fence_action to hook up to a fence.
 * @fence: A referenced pointer to the fence to keep it alive while @action
 * hangs on it.
 * @dev: Pointer to a struct drm_device so we can access the event stuff.
 * @kref: Both @e and @action has destructors, so we need to refcount.
 * @size: Size accounted for this object.
 * @tv_sec: If non-null, the variable pointed to will be assigned
 * current time tv_sec val when the fence signals.
 * @tv_usec: Must be set if @tv_sec is set, and the variable pointed to will
 * be assigned the current time tv_usec val when the fence signals.
 */
struct vmw_event_fence_action {
	struct vmw_fence_action action;

	struct drm_pending_event *event;
	struct vmw_fence_obj *fence;
	struct drm_device *dev;

	uint32_t *tv_sec;
	uint32_t *tv_usec;
};

static struct vmw_fence_manager *
fman_from_fence(struct vmw_fence_obj *fence)
{
	return container_of(fence->base.lock, struct vmw_fence_manager, lock);
}

/**
 * Note on fencing subsystem usage of irqs:
 * Typically the vmw_fences_update function is called
 *
 * a) When a new fence seqno has been submitted by the fifo code.
 * b) On-demand when we have waiters. Sleeping waiters will switch on the
 * ANY_FENCE irq and call vmw_fences_update function each time an ANY_FENCE
 * irq is received. When the last fence waiter is gone, that IRQ is masked
 * away.
 *
 * In situations where there are no waiters and we don't submit any new fences,
 * fence objects may not be signaled. This is perfectly OK, since there are
 * no consumers of the signaled data, but that is NOT ok when there are fence
 * actions attached to a fence. The fencing subsystem then makes use of the
 * FENCE_GOAL irq and sets the fence goal seqno to that of the next fence
 * which has an action attached, and each time vmw_fences_update is called,
 * the subsystem makes sure the fence goal seqno is updated.
 *
 * The fence goal seqno irq is on as long as there are unsignaled fence
 * objects with actions attached to them.
 */

static void vmw_fence_obj_destroy(struct dma_fence *f)
{
	struct vmw_fence_obj *fence =
		container_of(f, struct vmw_fence_obj, base);

	struct vmw_fence_manager *fman = fman_from_fence(fence);

	spin_lock(&fman->lock);
	list_del_init(&fence->head);
	--fman->num_fence_objects;
	spin_unlock(&fman->lock);
	fence->destroy(fence);
}

static const char *vmw_fence_get_driver_name(struct dma_fence *f)
{
	return "vmwgfx";
}

static const char *vmw_fence_get_timeline_name(struct dma_fence *f)
{
	return "svga";
}

static bool vmw_fence_enable_signaling(struct dma_fence *f)
{
	struct vmw_fence_obj *fence =
		container_of(f, struct vmw_fence_obj, base);

	struct vmw_fence_manager *fman = fman_from_fence(fence);
	struct vmw_private *dev_priv = fman->dev_priv;

	u32 *fifo_mem = dev_priv->mmio_virt;
	u32 seqno = vmw_mmio_read(fifo_mem + SVGA_FIFO_FENCE);
	if (seqno - fence->base.seqno < VMW_FENCE_WRAP)
		return false;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);

	return true;
}

struct vmwgfx_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void
vmwgfx_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct vmwgfx_wait_cb *wait =
		container_of(cb, struct vmwgfx_wait_cb, base);

	wake_up_process(wait->task);
}

static void __vmw_fences_update(struct vmw_fence_manager *fman);

static long vmw_fence_wait(struct dma_fence *f, bool intr, signed long timeout)
{
	struct vmw_fence_obj *fence =
		container_of(f, struct vmw_fence_obj, base);

	struct vmw_fence_manager *fman = fman_from_fence(fence);
	struct vmw_private *dev_priv = fman->dev_priv;
	struct vmwgfx_wait_cb cb;
	long ret = timeout;
	unsigned long irq_flags;

	if (likely(vmw_fence_obj_signaled(fence)))
		return timeout;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	vmw_seqno_waiter_add(dev_priv);

	spin_lock_irqsave(f->lock, irq_flags);

	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	cb.base.func = vmwgfx_wait_cb;
	cb.task = current;
	list_add(&cb.base.node, &f->cb_list);

	while (ret > 0) {
		__vmw_fences_update(fman);
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags))
			break;

		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(f->lock, irq_flags);

		ret = schedule_timeout(ret);

		spin_lock_irqsave(f->lock, irq_flags);
		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
	__set_current_state(TASK_RUNNING);

out:
	spin_unlock_irqrestore(f->lock, irq_flags);

	vmw_seqno_waiter_remove(dev_priv);

	return ret;
}

static const struct dma_fence_ops vmw_fence_ops = {
	.get_driver_name = vmw_fence_get_driver_name,
	.get_timeline_name = vmw_fence_get_timeline_name,
	.enable_signaling = vmw_fence_enable_signaling,
	.wait = vmw_fence_wait,
	.release = vmw_fence_obj_destroy,
};


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
	bool seqno_valid;

	do {
		INIT_LIST_HEAD(&list);
		mutex_lock(&fman->goal_irq_mutex);

		spin_lock(&fman->lock);
		list_splice_init(&fman->cleanup_list, &list);
		seqno_valid = fman->seqno_valid;
		spin_unlock(&fman->lock);

		if (!seqno_valid && fman->goal_irq_on) {
			fman->goal_irq_on = false;
			vmw_goal_waiter_remove(fman->dev_priv);
		}
		mutex_unlock(&fman->goal_irq_mutex);

		if (list_empty(&list))
			return;

		/*
		 * At this point, only we should be able to manipulate the
		 * list heads of the actions we have on the private list.
		 * hence fman::lock not held.
		 */

		list_for_each_entry_safe(action, next_action, &list, head) {
			list_del_init(&action->head);
			if (action->cleanup)
				action->cleanup(action);
		}
	} while (1);
}

struct vmw_fence_manager *vmw_fence_manager_init(struct vmw_private *dev_priv)
{
	struct vmw_fence_manager *fman = kzalloc(sizeof(*fman), GFP_KERNEL);

	if (unlikely(!fman))
		return NULL;

	fman->dev_priv = dev_priv;
	spin_lock_init(&fman->lock);
	INIT_LIST_HEAD(&fman->fence_list);
	INIT_LIST_HEAD(&fman->cleanup_list);
	INIT_WORK(&fman->work, &vmw_fence_work_func);
	fman->fifo_down = true;
	fman->user_fence_size = ttm_round_pot(sizeof(struct vmw_user_fence));
	fman->fence_size = ttm_round_pot(sizeof(struct vmw_fence_obj));
	fman->event_fence_action_size =
		ttm_round_pot(sizeof(struct vmw_event_fence_action));
	mutex_init(&fman->goal_irq_mutex);
	fman->ctx = dma_fence_context_alloc(1);

	return fman;
}

void vmw_fence_manager_takedown(struct vmw_fence_manager *fman)
{
	bool lists_empty;

	(void) cancel_work_sync(&fman->work);

	spin_lock(&fman->lock);
	lists_empty = list_empty(&fman->fence_list) &&
		list_empty(&fman->cleanup_list);
	spin_unlock(&fman->lock);

	BUG_ON(!lists_empty);
	kfree(fman);
}

static int vmw_fence_obj_init(struct vmw_fence_manager *fman,
			      struct vmw_fence_obj *fence, u32 seqno,
			      void (*destroy) (struct vmw_fence_obj *fence))
{
	int ret = 0;

	dma_fence_init(&fence->base, &vmw_fence_ops, &fman->lock,
		       fman->ctx, seqno);
	INIT_LIST_HEAD(&fence->seq_passed_actions);
	fence->destroy = destroy;

	spin_lock(&fman->lock);
	if (unlikely(fman->fifo_down)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	list_add_tail(&fence->head, &fman->fence_list);
	++fman->num_fence_objects;

out_unlock:
	spin_unlock(&fman->lock);
	return ret;

}

static void vmw_fences_perform_actions(struct vmw_fence_manager *fman,
				struct list_head *list)
{
	struct vmw_fence_action *action, *next_action;

	list_for_each_entry_safe(action, next_action, list, head) {
		list_del_init(&action->head);
		fman->pending_actions[action->type]--;
		if (action->seq_passed != NULL)
			action->seq_passed(action);

		/*
		 * Add the cleanup action to the cleanup list so that
		 * it will be performed by a worker task.
		 */

		list_add_tail(&action->head, &fman->cleanup_list);
	}
}

/**
 * vmw_fence_goal_new_locked - Figure out a new device fence goal
 * seqno if needed.
 *
 * @fman: Pointer to a fence manager.
 * @passed_seqno: The seqno the device currently signals as passed.
 *
 * This function should be called with the fence manager lock held.
 * It is typically called when we have a new passed_seqno, and
 * we might need to update the fence goal. It checks to see whether
 * the current fence goal has already passed, and, in that case,
 * scans through all unsignaled fences to get the next fence object with an
 * action attached, and sets the seqno of that fence as a new fence goal.
 *
 * returns true if the device goal seqno was updated. False otherwise.
 */
static bool vmw_fence_goal_new_locked(struct vmw_fence_manager *fman,
				      u32 passed_seqno)
{
	u32 goal_seqno;
	u32 *fifo_mem;
	struct vmw_fence_obj *fence;

	if (likely(!fman->seqno_valid))
		return false;

	fifo_mem = fman->dev_priv->mmio_virt;
	goal_seqno = vmw_mmio_read(fifo_mem + SVGA_FIFO_FENCE_GOAL);
	if (likely(passed_seqno - goal_seqno >= VMW_FENCE_WRAP))
		return false;

	fman->seqno_valid = false;
	list_for_each_entry(fence, &fman->fence_list, head) {
		if (!list_empty(&fence->seq_passed_actions)) {
			fman->seqno_valid = true;
			vmw_mmio_write(fence->base.seqno,
				       fifo_mem + SVGA_FIFO_FENCE_GOAL);
			break;
		}
	}

	return true;
}


/**
 * vmw_fence_goal_check_locked - Replace the device fence goal seqno if
 * needed.
 *
 * @fence: Pointer to a struct vmw_fence_obj the seqno of which should be
 * considered as a device fence goal.
 *
 * This function should be called with the fence manager lock held.
 * It is typically called when an action has been attached to a fence to
 * check whether the seqno of that fence should be used for a fence
 * goal interrupt. This is typically needed if the current fence goal is
 * invalid, or has a higher seqno than that of the current fence object.
 *
 * returns true if the device goal seqno was updated. False otherwise.
 */
static bool vmw_fence_goal_check_locked(struct vmw_fence_obj *fence)
{
	struct vmw_fence_manager *fman = fman_from_fence(fence);
	u32 goal_seqno;
	u32 *fifo_mem;

	if (dma_fence_is_signaled_locked(&fence->base))
		return false;

	fifo_mem = fman->dev_priv->mmio_virt;
	goal_seqno = vmw_mmio_read(fifo_mem + SVGA_FIFO_FENCE_GOAL);
	if (likely(fman->seqno_valid &&
		   goal_seqno - fence->base.seqno < VMW_FENCE_WRAP))
		return false;

	vmw_mmio_write(fence->base.seqno, fifo_mem + SVGA_FIFO_FENCE_GOAL);
	fman->seqno_valid = true;

	return true;
}

static void __vmw_fences_update(struct vmw_fence_manager *fman)
{
	struct vmw_fence_obj *fence, *next_fence;
	struct list_head action_list;
	bool needs_rerun;
	uint32_t seqno, new_seqno;
	u32 *fifo_mem = fman->dev_priv->mmio_virt;

	seqno = vmw_mmio_read(fifo_mem + SVGA_FIFO_FENCE);
rerun:
	list_for_each_entry_safe(fence, next_fence, &fman->fence_list, head) {
		if (seqno - fence->base.seqno < VMW_FENCE_WRAP) {
			list_del_init(&fence->head);
			dma_fence_signal_locked(&fence->base);
			INIT_LIST_HEAD(&action_list);
			list_splice_init(&fence->seq_passed_actions,
					 &action_list);
			vmw_fences_perform_actions(fman, &action_list);
		} else
			break;
	}

	/*
	 * Rerun if the fence goal seqno was updated, and the
	 * hardware might have raced with that update, so that
	 * we missed a fence_goal irq.
	 */

	needs_rerun = vmw_fence_goal_new_locked(fman, seqno);
	if (unlikely(needs_rerun)) {
		new_seqno = vmw_mmio_read(fifo_mem + SVGA_FIFO_FENCE);
		if (new_seqno != seqno) {
			seqno = new_seqno;
			goto rerun;
		}
	}

	if (!list_empty(&fman->cleanup_list))
		(void) schedule_work(&fman->work);
}

void vmw_fences_update(struct vmw_fence_manager *fman)
{
	spin_lock(&fman->lock);
	__vmw_fences_update(fman);
	spin_unlock(&fman->lock);
}

bool vmw_fence_obj_signaled(struct vmw_fence_obj *fence)
{
	struct vmw_fence_manager *fman = fman_from_fence(fence);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->base.flags))
		return 1;

	vmw_fences_update(fman);

	return dma_fence_is_signaled(&fence->base);
}

int vmw_fence_obj_wait(struct vmw_fence_obj *fence, bool lazy,
		       bool interruptible, unsigned long timeout)
{
	long ret = dma_fence_wait_timeout(&fence->base, interruptible, timeout);

	if (likely(ret > 0))
		return 0;
	else if (ret == 0)
		return -EBUSY;
	else
		return ret;
}

void vmw_fence_obj_flush(struct vmw_fence_obj *fence)
{
	struct vmw_private *dev_priv = fman_from_fence(fence)->dev_priv;

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
}

static void vmw_fence_destroy(struct vmw_fence_obj *fence)
{
	dma_fence_free(&fence->base);
}

int vmw_fence_create(struct vmw_fence_manager *fman,
		     uint32_t seqno,
		     struct vmw_fence_obj **p_fence)
{
	struct vmw_fence_obj *fence;
 	int ret;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (unlikely(!fence))
		return -ENOMEM;

	ret = vmw_fence_obj_init(fman, fence, seqno,
				 vmw_fence_destroy);
	if (unlikely(ret != 0))
		goto out_err_init;

	*p_fence = fence;
	return 0;

out_err_init:
	kfree(fence);
	return ret;
}


static void vmw_user_fence_destroy(struct vmw_fence_obj *fence)
{
	struct vmw_user_fence *ufence =
		container_of(fence, struct vmw_user_fence, fence);
	struct vmw_fence_manager *fman = fman_from_fence(fence);

	ttm_base_object_kfree(ufence, base);
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
	if (unlikely(!ufence)) {
		ret = -ENOMEM;
		goto out_no_object;
	}

	ret = vmw_fence_obj_init(fman, &ufence->fence, seqno,
				 vmw_user_fence_destroy);
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
 * vmw_wait_dma_fence - Wait for a dma fence
 *
 * @fman: pointer to a fence manager
 * @fence: DMA fence to wait on
 *
 * This function handles the case when the fence is actually a fence
 * array.  If that's the case, it'll wait on each of the child fence
 */
int vmw_wait_dma_fence(struct vmw_fence_manager *fman,
		       struct dma_fence *fence)
{
	struct dma_fence_array *fence_array;
	int ret = 0;
	int i;


	if (dma_fence_is_signaled(fence))
		return 0;

	if (!dma_fence_is_array(fence))
		return dma_fence_wait(fence, true);

	/* From i915: Note that if the fence-array was created in
	 * signal-on-any mode, we should *not* decompose it into its individual
	 * fences. However, we don't currently store which mode the fence-array
	 * is operating in. Fortunately, the only user of signal-on-any is
	 * private to amdgpu and we should not see any incoming fence-array
	 * from sync-file being in signal-on-any mode.
	 */

	fence_array = to_dma_fence_array(fence);
	for (i = 0; i < fence_array->num_fences; i++) {
		struct dma_fence *child = fence_array->fences[i];

		ret = dma_fence_wait(child, true);

		if (ret < 0)
			return ret;
	}

	return 0;
}


/**
 * vmw_fence_fifo_down - signal all unsignaled fence objects.
 */

void vmw_fence_fifo_down(struct vmw_fence_manager *fman)
{
	struct list_head action_list;
	int ret;

	/*
	 * The list may be altered while we traverse it, so always
	 * restart when we've released the fman->lock.
	 */

	spin_lock(&fman->lock);
	fman->fifo_down = true;
	while (!list_empty(&fman->fence_list)) {
		struct vmw_fence_obj *fence =
			list_entry(fman->fence_list.prev, struct vmw_fence_obj,
				   head);
		dma_fence_get(&fence->base);
		spin_unlock(&fman->lock);

		ret = vmw_fence_obj_wait(fence, false, false,
					 VMW_FENCE_WAIT_TIMEOUT);

		if (unlikely(ret != 0)) {
			list_del_init(&fence->head);
			dma_fence_signal(&fence->base);
			INIT_LIST_HEAD(&action_list);
			list_splice_init(&fence->seq_passed_actions,
					 &action_list);
			vmw_fences_perform_actions(fman, &action_list);
		}

		BUG_ON(!list_empty(&fence->head));
		dma_fence_put(&fence->base);
		spin_lock(&fman->lock);
	}
	spin_unlock(&fman->lock);
}

void vmw_fence_fifo_up(struct vmw_fence_manager *fman)
{
	spin_lock(&fman->lock);
	fman->fifo_down = false;
	spin_unlock(&fman->lock);
}


/**
 * vmw_fence_obj_lookup - Look up a user-space fence object
 *
 * @tfile: A struct ttm_object_file identifying the caller.
 * @handle: A handle identifying the fence object.
 * @return: A struct vmw_user_fence base ttm object on success or
 * an error pointer on failure.
 *
 * The fence object is looked up and type-checked. The caller needs
 * to have opened the fence object first, but since that happens on
 * creation and fence objects aren't shareable, that's not an
 * issue currently.
 */
static struct ttm_base_object *
vmw_fence_obj_lookup(struct ttm_object_file *tfile, u32 handle)
{
	struct ttm_base_object *base = ttm_base_object_lookup(tfile, handle);

	if (!base) {
		pr_err("Invalid fence object handle 0x%08lx.\n",
		       (unsigned long)handle);
		return ERR_PTR(-EINVAL);
	}

	if (base->refcount_release != vmw_user_fence_base_release) {
		pr_err("Invalid fence object handle 0x%08lx.\n",
		       (unsigned long)handle);
		ttm_base_object_unref(&base);
		return ERR_PTR(-EINVAL);
	}

	return base;
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

	base = vmw_fence_obj_lookup(tfile, arg->handle);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fence = &(container_of(base, struct vmw_user_fence, base)->fence);

	timeout = jiffies;
	if (time_after_eq(timeout, (unsigned long)arg->kernel_cookie)) {
		ret = ((vmw_fence_obj_signaled(fence)) ?
		       0 : -EBUSY);
		goto out;
	}

	timeout = (unsigned long)arg->kernel_cookie - timeout;

	ret = vmw_fence_obj_wait(fence, arg->lazy, true, timeout);

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

	base = vmw_fence_obj_lookup(tfile, arg->handle);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fence = &(container_of(base, struct vmw_user_fence, base)->fence);
	fman = fman_from_fence(fence);

	arg->signaled = vmw_fence_obj_signaled(fence);

	arg->signaled_flags = arg->flags;
	spin_lock(&fman->lock);
	arg->passed_seqno = dev_priv->last_read_seqno;
	spin_unlock(&fman->lock);

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

/**
 * vmw_event_fence_action_seq_passed
 *
 * @action: The struct vmw_fence_action embedded in a struct
 * vmw_event_fence_action.
 *
 * This function is called when the seqno of the fence where @action is
 * attached has passed. It queues the event on the submitter's event list.
 * This function is always called from atomic context.
 */
static void vmw_event_fence_action_seq_passed(struct vmw_fence_action *action)
{
	struct vmw_event_fence_action *eaction =
		container_of(action, struct vmw_event_fence_action, action);
	struct drm_device *dev = eaction->dev;
	struct drm_pending_event *event = eaction->event;
	struct drm_file *file_priv;


	if (unlikely(event == NULL))
		return;

	file_priv = event->file_priv;
	spin_lock_irq(&dev->event_lock);

	if (likely(eaction->tv_sec != NULL)) {
		struct timeval tv;

		do_gettimeofday(&tv);
		*eaction->tv_sec = tv.tv_sec;
		*eaction->tv_usec = tv.tv_usec;
	}

	drm_send_event_locked(dev, eaction->event);
	eaction->event = NULL;
	spin_unlock_irq(&dev->event_lock);
}

/**
 * vmw_event_fence_action_cleanup
 *
 * @action: The struct vmw_fence_action embedded in a struct
 * vmw_event_fence_action.
 *
 * This function is the struct vmw_fence_action destructor. It's typically
 * called from a workqueue.
 */
static void vmw_event_fence_action_cleanup(struct vmw_fence_action *action)
{
	struct vmw_event_fence_action *eaction =
		container_of(action, struct vmw_event_fence_action, action);

	vmw_fence_obj_unreference(&eaction->fence);
	kfree(eaction);
}


/**
 * vmw_fence_obj_add_action - Add an action to a fence object.
 *
 * @fence - The fence object.
 * @action - The action to add.
 *
 * Note that the action callbacks may be executed before this function
 * returns.
 */
static void vmw_fence_obj_add_action(struct vmw_fence_obj *fence,
			      struct vmw_fence_action *action)
{
	struct vmw_fence_manager *fman = fman_from_fence(fence);
	bool run_update = false;

	mutex_lock(&fman->goal_irq_mutex);
	spin_lock(&fman->lock);

	fman->pending_actions[action->type]++;
	if (dma_fence_is_signaled_locked(&fence->base)) {
		struct list_head action_list;

		INIT_LIST_HEAD(&action_list);
		list_add_tail(&action->head, &action_list);
		vmw_fences_perform_actions(fman, &action_list);
	} else {
		list_add_tail(&action->head, &fence->seq_passed_actions);

		/*
		 * This function may set fman::seqno_valid, so it must
		 * be run with the goal_irq_mutex held.
		 */
		run_update = vmw_fence_goal_check_locked(fence);
	}

	spin_unlock(&fman->lock);

	if (run_update) {
		if (!fman->goal_irq_on) {
			fman->goal_irq_on = true;
			vmw_goal_waiter_add(fman->dev_priv);
		}
		vmw_fences_update(fman);
	}
	mutex_unlock(&fman->goal_irq_mutex);

}

/**
 * vmw_event_fence_action_create - Post an event for sending when a fence
 * object seqno has passed.
 *
 * @file_priv: The file connection on which the event should be posted.
 * @fence: The fence object on which to post the event.
 * @event: Event to be posted. This event should've been alloced
 * using k[mz]alloc, and should've been completely initialized.
 * @interruptible: Interruptible waits if possible.
 *
 * As a side effect, the object pointed to by @event may have been
 * freed when this function returns. If this function returns with
 * an error code, the caller needs to free that object.
 */

int vmw_event_fence_action_queue(struct drm_file *file_priv,
				 struct vmw_fence_obj *fence,
				 struct drm_pending_event *event,
				 uint32_t *tv_sec,
				 uint32_t *tv_usec,
				 bool interruptible)
{
	struct vmw_event_fence_action *eaction;
	struct vmw_fence_manager *fman = fman_from_fence(fence);

	eaction = kzalloc(sizeof(*eaction), GFP_KERNEL);
	if (unlikely(!eaction))
		return -ENOMEM;

	eaction->event = event;

	eaction->action.seq_passed = vmw_event_fence_action_seq_passed;
	eaction->action.cleanup = vmw_event_fence_action_cleanup;
	eaction->action.type = VMW_ACTION_EVENT;

	eaction->fence = vmw_fence_obj_reference(fence);
	eaction->dev = fman->dev_priv->dev;
	eaction->tv_sec = tv_sec;
	eaction->tv_usec = tv_usec;

	vmw_fence_obj_add_action(fence, &eaction->action);

	return 0;
}

struct vmw_event_fence_pending {
	struct drm_pending_event base;
	struct drm_vmw_event_fence event;
};

static int vmw_event_fence_action_create(struct drm_file *file_priv,
				  struct vmw_fence_obj *fence,
				  uint32_t flags,
				  uint64_t user_data,
				  bool interruptible)
{
	struct vmw_event_fence_pending *event;
	struct vmw_fence_manager *fman = fman_from_fence(fence);
	struct drm_device *dev = fman->dev_priv->dev;
	int ret;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (unlikely(!event)) {
		DRM_ERROR("Failed to allocate an event.\n");
		ret = -ENOMEM;
		goto out_no_space;
	}

	event->event.base.type = DRM_VMW_EVENT_FENCE_SIGNALED;
	event->event.base.length = sizeof(*event);
	event->event.user_data = user_data;

	ret = drm_event_reserve_init(dev, file_priv, &event->base, &event->event.base);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate event space for this file.\n");
		kfree(event);
		goto out_no_space;
	}

	if (flags & DRM_VMW_FE_FLAG_REQ_TIME)
		ret = vmw_event_fence_action_queue(file_priv, fence,
						   &event->base,
						   &event->event.tv_sec,
						   &event->event.tv_usec,
						   interruptible);
	else
		ret = vmw_event_fence_action_queue(file_priv, fence,
						   &event->base,
						   NULL,
						   NULL,
						   interruptible);
	if (ret != 0)
		goto out_no_queue;

	return 0;

out_no_queue:
	drm_event_cancel_free(dev, &event->base);
out_no_space:
	return ret;
}

int vmw_fence_event_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_fence_event_arg *arg =
		(struct drm_vmw_fence_event_arg *) data;
	struct vmw_fence_obj *fence = NULL;
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);
	struct ttm_object_file *tfile = vmw_fp->tfile;
	struct drm_vmw_fence_rep __user *user_fence_rep =
		(struct drm_vmw_fence_rep __user *)(unsigned long)
		arg->fence_rep;
	uint32_t handle;
	int ret;

	/*
	 * Look up an existing fence object,
	 * and if user-space wants a new reference,
	 * add one.
	 */
	if (arg->handle) {
		struct ttm_base_object *base =
			vmw_fence_obj_lookup(tfile, arg->handle);

		if (IS_ERR(base))
			return PTR_ERR(base);

		fence = &(container_of(base, struct vmw_user_fence,
				       base)->fence);
		(void) vmw_fence_obj_reference(fence);

		if (user_fence_rep != NULL) {
			ret = ttm_ref_object_add(vmw_fp->tfile, base,
						 TTM_REF_USAGE, NULL, false);
			if (unlikely(ret != 0)) {
				DRM_ERROR("Failed to reference a fence "
					  "object.\n");
				goto out_no_ref_obj;
			}
			handle = base->hash.key;
		}
		ttm_base_object_unref(&base);
	}

	/*
	 * Create a new fence object.
	 */
	if (!fence) {
		ret = vmw_execbuf_fence_commands(file_priv, dev_priv,
						 &fence,
						 (user_fence_rep) ?
						 &handle : NULL);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Fence event failed to create fence.\n");
			return ret;
		}
	}

	BUG_ON(fence == NULL);

	ret = vmw_event_fence_action_create(file_priv, fence,
					    arg->flags,
					    arg->user_data,
					    true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Failed to attach event to fence.\n");
		goto out_no_create;
	}

	vmw_execbuf_copy_fence_user(dev_priv, vmw_fp, 0, user_fence_rep, fence,
				    handle, -1, NULL);
	vmw_fence_obj_unreference(&fence);
	return 0;
out_no_create:
	if (user_fence_rep != NULL)
		ttm_ref_object_base_unref(tfile, handle, TTM_REF_USAGE);
out_no_ref_obj:
	vmw_fence_obj_unreference(&fence);
	return ret;
}
