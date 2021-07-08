/*
 *
 * (C) COPYRIGHT 2011-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <mali_kbase_fence_defs.h>
#include <mali_kbase_fence.h>
#include <mali_kbase.h>

/* Spin lock protecting all Mali fences as fence->lock. */
static DEFINE_SPINLOCK(kbase_fence_lock);

static const char *
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
kbase_fence_get_driver_name(struct fence *fence)
#else
kbase_fence_get_driver_name(struct dma_fence *fence)
#endif
{
	return kbase_drv_name;
}

static const char *
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
kbase_fence_get_timeline_name(struct fence *fence)
#else
kbase_fence_get_timeline_name(struct dma_fence *fence)
#endif
{
	return kbase_timeline_name;
}

static bool
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
kbase_fence_enable_signaling(struct fence *fence)
#else
kbase_fence_enable_signaling(struct dma_fence *fence)
#endif
{
	return true;
}

static void
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
kbase_fence_fence_value_str(struct fence *fence, char *str, int size)
#else
kbase_fence_fence_value_str(struct dma_fence *fence, char *str, int size)
#endif
{
#if (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
	snprintf(str, size, "%u", fence->seqno);
#else
	snprintf(str, size, "%llu", fence->seqno);
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
const struct fence_ops kbase_fence_ops = {
	.wait = fence_default_wait,
#else
const struct dma_fence_ops kbase_fence_ops = {
	.wait = dma_fence_default_wait,
#endif
	.get_driver_name = kbase_fence_get_driver_name,
	.get_timeline_name = kbase_fence_get_timeline_name,
	.enable_signaling = kbase_fence_enable_signaling,
	.fence_value_str = kbase_fence_fence_value_str
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
struct fence *
kbase_fence_out_new(struct kbase_jd_atom *katom)
#else
struct dma_fence *
kbase_fence_out_new(struct kbase_jd_atom *katom)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
	struct fence *fence;
#else
	struct dma_fence *fence;
#endif

	WARN_ON(katom->dma_fence.fence);

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	dma_fence_init(fence,
		       &kbase_fence_ops,
		       &kbase_fence_lock,
		       katom->dma_fence.context,
		       atomic_inc_return(&katom->dma_fence.seqno));

	katom->dma_fence.fence = fence;

	return fence;
}

bool
kbase_fence_free_callbacks(struct kbase_jd_atom *katom)
{
	struct kbase_fence_cb *cb, *tmp;
	bool res = false;

	lockdep_assert_held(&katom->kctx->jctx.lock);

	/* Clean up and free callbacks. */
	list_for_each_entry_safe(cb, tmp, &katom->dma_fence.callbacks, node) {
		bool ret;

		/* Cancel callbacks that hasn't been called yet. */
		ret = dma_fence_remove_callback(cb->fence, &cb->fence_cb);
		if (ret) {
			int ret;

			/* Fence had not signaled, clean up after
			 * canceling.
			 */
			ret = atomic_dec_return(&katom->dma_fence.dep_count);

			if (unlikely(ret == 0))
				res = true;
		}

		/*
		 * Release the reference taken in
		 * kbase_fence_add_callback().
		 */
		dma_fence_put(cb->fence);
		list_del(&cb->node);
		kfree(cb);
	}

	return res;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
int
kbase_fence_add_callback(struct kbase_jd_atom *katom,
			 struct fence *fence,
			 fence_func_t callback)
#else
int
kbase_fence_add_callback(struct kbase_jd_atom *katom,
			 struct dma_fence *fence,
			 dma_fence_func_t callback)
#endif
{
	int err = 0;
	struct kbase_fence_cb *kbase_fence_cb;

	if (!fence)
		return -EINVAL;

	kbase_fence_cb = kmalloc(sizeof(*kbase_fence_cb), GFP_KERNEL);
	if (!kbase_fence_cb)
		return -ENOMEM;

	kbase_fence_cb->fence = fence;
	kbase_fence_cb->katom = katom;
	INIT_LIST_HEAD(&kbase_fence_cb->node);

	err = dma_fence_add_callback(fence, &kbase_fence_cb->fence_cb,
				     callback);
	if (err == -ENOENT) {
		/* Fence signaled, clear the error and return */
		err = 0;
		kfree(kbase_fence_cb);
	} else if (err) {
		kfree(kbase_fence_cb);
	} else {
		/*
		 * Get reference to fence that will be kept until callback gets
		 * cleaned up in kbase_fence_free_callbacks().
		 */
		dma_fence_get(fence);
		atomic_inc(&katom->dma_fence.dep_count);
		/* Add callback to katom's list of callbacks */
		list_add(&kbase_fence_cb->node, &katom->dma_fence.callbacks);
	}

	return err;
}
