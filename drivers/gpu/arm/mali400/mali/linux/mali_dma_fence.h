/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_dma_fence.h
 *
 * Mali interface for Linux dma buf fence objects.
 */

#ifndef _MALI_DMA_FENCE_H_
#define _MALI_DMA_FENCE_H_

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/dma-fence.h>
#else
#include <linux/fence.h>
#endif
#include <linux/reservation.h>
#endif

struct mali_dma_fence_context;

/* The mali dma fence context callback function */
typedef void (*mali_dma_fence_context_callback_func_t)(void *pp_job_ptr);

struct mali_dma_fence_waiter {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	struct dma_fence *fence;
	struct dma_fence_cb base;
#else
	struct fence_cb base;
	struct fence *fence;
#endif
	struct mali_dma_fence_context *parent;
};

struct mali_dma_fence_context {
	struct work_struct work_handle;
	struct mali_dma_fence_waiter **mali_dma_fence_waiters;
	u32 num_dma_fence_waiter;
	atomic_t count;
	void *pp_job_ptr; /* the mali pp job pointer */;
	mali_dma_fence_context_callback_func_t cb_func;
};

/* Create a dma fence
 * @param context The execution context this fence is run on
 * @param seqno A linearly increasing sequence number for this context
 * @return the new dma fence if success, or NULL on failure.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
struct dma_fence *mali_dma_fence_new(u32  context, u32 seqno);
#else
struct fence *mali_dma_fence_new(u32  context, u32 seqno);
#endif
/* Signal and put dma fence
 * @param fence The dma fence to signal and put
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
void mali_dma_fence_signal_and_put(struct dma_fence **fence);
#else
void mali_dma_fence_signal_and_put(struct fence **fence);
#endif
/**
 * Initialize a mali dma fence context for pp job.
 * @param dma_fence_context The mali dma fence context to initialize.
 * @param cb_func The dma fence context callback function to call when all dma fence release.
 * @param pp_job_ptr The pp_job to call function with.
 */
void mali_dma_fence_context_init(struct mali_dma_fence_context *dma_fence_context,
				 mali_dma_fence_context_callback_func_t  cb_func,
				 void *pp_job_ptr);

/**
 * Add new mali dma fence waiter into mali dma fence context
 * @param dma_fence_context The mali dma fence context
 * @param dma_reservation_object the reservation object to create new mali dma fence waiters
 * @return _MALI_OSK_ERR_OK if success, or not.
 */
_mali_osk_errcode_t mali_dma_fence_context_add_waiters(struct mali_dma_fence_context *dma_fence_context,
		struct reservation_object *dma_reservation_object);

/**
 * Release the dma fence context
 * @param dma_fence_text The mali dma fence context.
 */
void mali_dma_fence_context_term(struct mali_dma_fence_context *dma_fence_context);

/**
 * Decrease the dma fence context atomic count
 * @param dma_fence_text The mali dma fence context.
 */
void mali_dma_fence_context_dec_count(struct mali_dma_fence_context *dma_fence_context);

/**
 * Get all reservation object
 * @param dma_reservation_object The reservation object to add into the reservation object list
 * @param dma_reservation_object_list The reservation object list to store all reservation object
 * @param num_dma_reservation_object The number of all reservation object
 */
void mali_dma_fence_add_reservation_object_list(struct reservation_object *dma_reservation_object,
		struct reservation_object **dma_reservation_object_list,
		u32 *num_dma_reservation_object);

/**
 * Wait/wound mutex lock to lock all reservation object.
 */
int mali_dma_fence_lock_reservation_object_list(struct reservation_object **dma_reservation_object_list,
		u32  num_dma_reservation_object, struct ww_acquire_ctx *ww_actx);

/**
 * Wait/wound mutex lock to unlock all reservation object.
 */
void mali_dma_fence_unlock_reservation_object_list(struct reservation_object **dma_reservation_object_list,
		u32 num_dma_reservation_object, struct ww_acquire_ctx *ww_actx);
#endif
