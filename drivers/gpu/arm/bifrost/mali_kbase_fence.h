/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2023 ARM Limited. All rights reserved.
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
 * mali_kbase_fence.[hc] has fence code used only by
 * - CONFIG_SYNC_FILE      - explicit fences
 */

#if IS_ENABLED(CONFIG_SYNC_FILE)

#include <linux/list.h>
#include "mali_kbase_fence_defs.h"
#include "mali_kbase.h"
#include "mali_kbase_refcount_defs.h"

#if MALI_USE_CSF
/* Maximum number of characters in DMA fence timeline name. */
#define MAX_TIMELINE_NAME (32)

/**
 * struct kbase_kcpu_dma_fence_meta - Metadata structure for dma fence objects containing
 *                                    information about KCPU queue. One instance per KCPU
 *                                    queue.
 *
 * @refcount:       Atomic value to keep track of number of references to an instance.
 *                  An instance can outlive the KCPU queue itself.
 * @kbdev:          Pointer to Kbase device.
 * @kctx_id:        Kbase context ID.
 * @timeline_name:  String of timeline name for associated fence object.
 */
struct kbase_kcpu_dma_fence_meta {
	kbase_refcount_t refcount;
	struct kbase_device *kbdev;
	int kctx_id;
	char timeline_name[MAX_TIMELINE_NAME];
};

/**
 * struct kbase_kcpu_dma_fence - Structure which extends a dma fence object to include a
 *                               reference to metadata containing more informaiton about it.
 *
 * @base:      Fence object itself.
 * @metadata:  Pointer to metadata structure.
 */
struct kbase_kcpu_dma_fence {
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	struct fence base;
#else
	struct dma_fence base;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0) */
	struct kbase_kcpu_dma_fence_meta *metadata;
};
#endif

#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
extern const struct fence_ops kbase_fence_ops;
#else
extern const struct dma_fence_ops kbase_fence_ops;
#endif

/**
 * kbase_fence_out_new() - Creates a new output fence and puts it on the atom
 * @katom: Atom to create an output fence for
 *
 * Return: A new fence object on success, NULL on failure.
 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
struct fence *kbase_fence_out_new(struct kbase_jd_atom *katom);
#else
struct dma_fence *kbase_fence_out_new(struct kbase_jd_atom *katom);
#endif

#if IS_ENABLED(CONFIG_SYNC_FILE)
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

#if IS_ENABLED(CONFIG_SYNC_FILE)
/**
 * kbase_fence_in_remove() - Removes the input fence from atom
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

#if IS_ENABLED(CONFIG_SYNC_FILE)
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
 * kbase_fence_get() - Retrieve fence for a KCPUQ fence command.
 * @fence_info: KCPUQ fence command
 *
 * A ref will be taken for the fence, so use @kbase_fence_put() to release it
 *
 * Return: The fence, or NULL if there is no fence for KCPUQ fence command
 */
#define kbase_fence_get(fence_info) dma_fence_get((fence_info)->fence)

#if MALI_USE_CSF
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
static inline struct kbase_kcpu_dma_fence *kbase_kcpu_dma_fence_get(struct fence *fence)
#else
static inline struct kbase_kcpu_dma_fence *kbase_kcpu_dma_fence_get(struct dma_fence *fence)
#endif
{
	if (fence->ops == &kbase_fence_ops)
		return (struct kbase_kcpu_dma_fence *)fence;

	return NULL;
}

static inline void kbase_kcpu_dma_fence_meta_put(struct kbase_kcpu_dma_fence_meta *metadata)
{
	if (kbase_refcount_dec_and_test(&metadata->refcount)) {
		atomic_dec(&metadata->kbdev->live_fence_metadata);
		kfree(metadata);
	}
}

#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
static inline void kbase_kcpu_dma_fence_put(struct fence *fence)
#else
static inline void kbase_kcpu_dma_fence_put(struct dma_fence *fence)
#endif
{
	struct kbase_kcpu_dma_fence *kcpu_fence = kbase_kcpu_dma_fence_get(fence);

	if (kcpu_fence)
		kbase_kcpu_dma_fence_meta_put(kcpu_fence->metadata);
}
#endif /* MALI_USE_CSF */

/**
 * kbase_fence_put() - Releases a reference to a fence
 * @fence: Fence to release reference for.
 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
static inline void kbase_fence_put(struct fence *fence)
#else
static inline void kbase_fence_put(struct dma_fence *fence)
#endif
{
	dma_fence_put(fence);
}

#endif /* IS_ENABLED(CONFIG_SYNC_FILE) */

#endif /* _KBASE_FENCE_H_ */
