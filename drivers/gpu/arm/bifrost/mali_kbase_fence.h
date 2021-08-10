/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2018, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KBASE_FENCE_H_
#define _KBASE_FENCE_H_

/*
 * mali_kbase_fence.[hc] has common fence code used by both
 * - CONFIG_MALI_BIFROST_DMA_FENCE - implicit DMA fences
 * - CONFIG_SYNC_FILE      - explicit fences beginning with 4.9 kernel
 */

#if defined(CONFIG_MALI_BIFROST_DMA_FENCE) || defined(CONFIG_SYNC_FILE)

#include <linux/list.h>
#include "mali_kbase_fence_defs.h"
#include "mali_kbase.h"

#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
extern const struct fence_ops kbase_fence_ops;
#else
extern const struct dma_fence_ops kbase_fence_ops;
#endif

/**
* struct kbase_fence_cb - Mali dma-fence callback data struct
* @fence_cb: Callback function
* @katom:    Pointer to katom that is waiting on this callback
* @fence:    Pointer to the fence object on which this callback is waiting
* @node:     List head for linking this callback to the katom
*/
struct kbase_fence_cb {
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	struct fence_cb fence_cb;
	struct fence *fence;
#else
	struct dma_fence_cb fence_cb;
	struct dma_fence *fence;
#endif
	struct kbase_jd_atom *katom;
	struct list_head node;
};

/**
 * kbase_fence_out_new() - Creates a new output fence and puts it on the atom
 * @katom: Atom to create an output fence for
 *
 * return: A new fence object on success, NULL on failure.
 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
struct fence *kbase_fence_out_new(struct kbase_jd_atom *katom);
#else
struct dma_fence *kbase_fence_out_new(struct kbase_jd_atom *katom);
#endif

#if defined(CONFIG_SYNC_FILE)
/**
 * kbase_fence_fence_in_set() - Assign input fence to atom
 * @katom: Atom to assign input fence to
 * @fence: Input fence to assign to atom
 *
 * This function will take ownership of one fence reference!
 */
#define kbase_fence_fence_in_set(katom, fence) \
	do { \
		WARN_ON((katom)->dma_fence.fence_in); \
		(katom)->dma_fence.fence_in = fence; \
	} while (0)
#endif


#if !MALI_USE_CSF
/**
 * kbase_fence_out_remove() - Removes the output fence from atom
 * @katom: Atom to remove output fence for
 *
 * This will also release the reference to this fence which the atom keeps
 */
static inline void kbase_fence_out_remove(struct kbase_jd_atom *katom)
{
	if (katom->dma_fence.fence) {
		dma_fence_put(katom->dma_fence.fence);
		katom->dma_fence.fence = NULL;
	}
}

#if defined(CONFIG_SYNC_FILE)
/**
 * kbase_fence_out_remove() - Removes the input fence from atom
 * @katom: Atom to remove input fence for
 *
 * This will also release the reference to this fence which the atom keeps
 */
static inline void kbase_fence_in_remove(struct kbase_jd_atom *katom)
{
	if (katom->dma_fence.fence_in) {
		dma_fence_put(katom->dma_fence.fence_in);
		katom->dma_fence.fence_in = NULL;
	}
}
#endif

/**
 * kbase_fence_out_is_ours() - Check if atom has a valid fence created by us
 * @katom: Atom to check output fence for
 *
 * Return: true if fence exists and is valid, otherwise false
 */
static inline bool kbase_fence_out_is_ours(struct kbase_jd_atom *katom)
{
	return katom->dma_fence.fence &&
				katom->dma_fence.fence->ops == &kbase_fence_ops;
}

/**
 * kbase_fence_out_signal() - Signal output fence of atom
 * @katom: Atom to signal output fence for
 * @status: Status to signal with (0 for success, < 0 for error)
 *
 * Return: 0 on success, < 0 on error
 */
static inline int kbase_fence_out_signal(struct kbase_jd_atom *katom,
					 int status)
{
	if (status) {
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE && \
	  KERNEL_VERSION(4, 9, 68) <= LINUX_VERSION_CODE)
		fence_set_error(katom->dma_fence.fence, status);
#elif (KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE)
		dma_fence_set_error(katom->dma_fence.fence, status);
#else
		katom->dma_fence.fence->status = status;
#endif
	}
	return dma_fence_signal(katom->dma_fence.fence);
}

/**
 * kbase_fence_add_callback() - Add callback on @fence to block @katom
 * @katom: Pointer to katom that will be blocked by @fence
 * @fence: Pointer to fence on which to set up the callback
 * @callback: Pointer to function to be called when fence is signaled
 *
 * Caller needs to hold a reference to @fence when calling this function, and
 * the caller is responsible for releasing that reference.  An additional
 * reference to @fence will be taken when the callback was successfully set up
 * and @fence needs to be kept valid until the callback has been called and
 * cleanup have been done.
 *
 * Return: 0 on success: fence was either already signaled, or callback was
 * set up. Negative error code is returned on error.
 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
int kbase_fence_add_callback(struct kbase_jd_atom *katom,
			     struct fence *fence,
			     fence_func_t callback);
#else
int kbase_fence_add_callback(struct kbase_jd_atom *katom,
			     struct dma_fence *fence,
			     dma_fence_func_t callback);
#endif

/**
 * kbase_fence_dep_count_set() - Set dep_count value on atom to specified value
 * @katom: Atom to set dep_count for
 * @val: value to set dep_count to
 *
 * The dep_count is available to the users of this module so that they can
 * synchronize completion of the wait with cancellation and adding of more
 * callbacks. For instance, a user could do the following:
 *
 * dep_count set to 1
 * callback #1 added, dep_count is increased to 2
 *                             callback #1 happens, dep_count decremented to 1
 *                             since dep_count > 0, no completion is done
 * callback #2 is added, dep_count is increased to 2
 * dep_count decremented to 1
 *                             callback #2 happens, dep_count decremented to 0
 *                             since dep_count now is zero, completion executes
 *
 * The dep_count can also be used to make sure that the completion only
 * executes once. This is typically done by setting dep_count to -1 for the
 * thread that takes on this responsibility.
 */
static inline void
kbase_fence_dep_count_set(struct kbase_jd_atom *katom, int val)
{
	atomic_set(&katom->dma_fence.dep_count, val);
}

/**
 * kbase_fence_dep_count_dec_and_test() - Decrements dep_count
 * @katom: Atom to decrement dep_count for
 *
 * See @kbase_fence_dep_count_set for general description about dep_count
 *
 * Return: true if value was decremented to zero, otherwise false
 */
static inline bool
kbase_fence_dep_count_dec_and_test(struct kbase_jd_atom *katom)
{
	return atomic_dec_and_test(&katom->dma_fence.dep_count);
}

/**
 * kbase_fence_dep_count_read() - Returns the current dep_count value
 * @katom: Pointer to katom
 *
 * See @kbase_fence_dep_count_set for general description about dep_count
 *
 * Return: The current dep_count value
 */
static inline int kbase_fence_dep_count_read(struct kbase_jd_atom *katom)
{
	return atomic_read(&katom->dma_fence.dep_count);
}

/**
 * kbase_fence_free_callbacks() - Free dma-fence callbacks on a katom
 * @katom: Pointer to katom
 *
 * This function will free all fence callbacks on the katom's list of
 * callbacks. Callbacks that have not yet been called, because their fence
 * hasn't yet signaled, will first be removed from the fence.
 *
 * Locking: katom->dma_fence.callbacks list assumes jctx.lock is held.
 *
 * Return: true if dep_count reached 0, otherwise false.
 */
bool kbase_fence_free_callbacks(struct kbase_jd_atom *katom);

#if defined(CONFIG_SYNC_FILE)
/**
 * kbase_fence_in_get() - Retrieve input fence for atom.
 * @katom: Atom to get input fence from
 *
 * A ref will be taken for the fence, so use @kbase_fence_put() to release it
 *
 * Return: The fence, or NULL if there is no input fence for atom
 */
#define kbase_fence_in_get(katom) dma_fence_get((katom)->dma_fence.fence_in)
#endif

/**
 * kbase_fence_out_get() - Retrieve output fence for atom.
 * @katom: Atom to get output fence from
 *
 * A ref will be taken for the fence, so use @kbase_fence_put() to release it
 *
 * Return: The fence, or NULL if there is no output fence for atom
 */
#define kbase_fence_out_get(katom) dma_fence_get((katom)->dma_fence.fence)

#endif /* !MALI_USE_CSF */

/**
 * kbase_fence_put() - Releases a reference to a fence
 * @fence: Fence to release reference for.
 */
#define kbase_fence_put(fence) dma_fence_put(fence)


#endif /* CONFIG_MALI_BIFROST_DMA_FENCE || defined(CONFIG_SYNC_FILE */

#endif /* _KBASE_FENCE_H_ */
