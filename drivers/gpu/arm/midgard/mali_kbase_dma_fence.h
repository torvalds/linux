/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
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



#ifndef _KBASE_DMA_FENCE_H_
#define _KBASE_DMA_FENCE_H_

#ifdef CONFIG_MALI_DMA_FENCE

#include <linux/list.h>
#include <linux/reservation.h>
#include <mali_kbase_fence.h>


/* Forward declaration from mali_kbase_defs.h */
struct kbase_jd_atom;
struct kbase_context;

/**
 * struct kbase_dma_fence_resv_info - Structure with list of reservation objects
 * @resv_objs:             Array of reservation objects to attach the
 *                         new fence to.
 * @dma_fence_resv_count:  Number of reservation objects in the array.
 * @dma_fence_excl_bitmap: Specifies which resv_obj are exclusive.
 *
 * This is used by some functions to pass around a collection of data about
 * reservation objects.
 */
struct kbase_dma_fence_resv_info {
	struct reservation_object **resv_objs;
	unsigned int dma_fence_resv_count;
	unsigned long *dma_fence_excl_bitmap;
};

/**
 * kbase_dma_fence_add_reservation() - Adds a resv to the array of resv_objs
 * @resv:      Reservation object to add to the array.
 * @info:      Pointer to struct with current reservation info
 * @exclusive: Boolean indicating if exclusive access is needed
 *
 * The function adds a new reservation_object to an existing array of
 * reservation_objects. At the same time keeps track of which objects require
 * exclusive access in dma_fence_excl_bitmap.
 */
void kbase_dma_fence_add_reservation(struct reservation_object *resv,
				     struct kbase_dma_fence_resv_info *info,
				     bool exclusive);

/**
 * kbase_dma_fence_wait() - Creates a new fence and attaches it to the resv_objs
 * @katom: Katom with the external dependency.
 * @info:  Pointer to struct with current reservation info
 *
 * Return: An error code or 0 if succeeds
 */
int kbase_dma_fence_wait(struct kbase_jd_atom *katom,
			 struct kbase_dma_fence_resv_info *info);

/**
 * kbase_dma_fence_cancel_ctx() - Cancel all dma-fences blocked atoms on kctx
 * @kctx: Pointer to kbase context
 *
 * This function will cancel and clean up all katoms on @kctx that is waiting
 * on dma-buf fences.
 *
 * Locking: jctx.lock needs to be held when calling this function.
 */
void kbase_dma_fence_cancel_all_atoms(struct kbase_context *kctx);

/**
 * kbase_dma_fence_cancel_callbacks() - Cancel only callbacks on katom
 * @katom: Pointer to katom whose callbacks are to be canceled
 *
 * This function cancels all dma-buf fence callbacks on @katom, but does not
 * cancel the katom itself.
 *
 * The caller is responsible for ensuring that jd_done_nolock is called on
 * @katom.
 *
 * Locking: jctx.lock must be held when calling this function.
 */
void kbase_dma_fence_cancel_callbacks(struct kbase_jd_atom *katom);

/**
 * kbase_dma_fence_signal() - Signal katom's fence and clean up after wait
 * @katom: Pointer to katom to signal and clean up
 *
 * This function will signal the @katom's fence, if it has one, and clean up
 * the callback data from the katom's wait on earlier fences.
 *
 * Locking: jctx.lock must be held while calling this function.
 */
void kbase_dma_fence_signal(struct kbase_jd_atom *katom);

/**
 * kbase_dma_fence_term() - Terminate Mali dma-fence context
 * @kctx: kbase context to terminate
 */
void kbase_dma_fence_term(struct kbase_context *kctx);

/**
 * kbase_dma_fence_init() - Initialize Mali dma-fence context
 * @kctx: kbase context to initialize
 */
int kbase_dma_fence_init(struct kbase_context *kctx);


#else /* CONFIG_MALI_DMA_FENCE */
/* Dummy functions for when dma-buf fence isn't enabled. */

static inline int kbase_dma_fence_init(struct kbase_context *kctx)
{
	return 0;
}

static inline void kbase_dma_fence_term(struct kbase_context *kctx) {}
#endif /* CONFIG_MALI_DMA_FENCE */
#endif
