/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */
#ifndef _TTM_FENCE_DRIVER_H_
#define _TTM_FENCE_DRIVER_H_

#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include "psb_ttm_fence_api.h"
#include "ttm/ttm_memory.h"

/** @file ttm_fence_driver.h
 *
 * Definitions needed for a driver implementing the
 * ttm_fence subsystem.
 */

/**
 * struct ttm_fence_class_manager:
 *
 * @wrap_diff: Sequence difference to catch 32-bit wrapping.
 * if (seqa - seqb) > @wrap_diff, then seqa < seqb.
 * @flush_diff: Sequence difference to trigger fence flush.
 * if (cur_seq - seqa) > @flush_diff, then consider fence object with
 * seqa as old an needing a flush.
 * @sequence_mask: Mask of valid bits in a fence sequence.
 * @lock: Lock protecting this struct as well as fence objects
 * associated with this struct.
 * @ring: Circular sequence-ordered list of fence objects.
 * @pending_flush: Fence types currently needing a flush.
 * @waiting_types: Fence types that are currently waited for.
 * @fence_queue: Queue of waiters on fences belonging to this fence class.
 * @highest_waiting_sequence: Sequence number of the fence with highest
 * sequence number and that is waited for.
 * @latest_queued_sequence: Sequence number of the fence latest queued
 * on the ring.
 */

struct ttm_fence_class_manager {

	/*
	 * Unprotected constant members.
	 */

	uint32_t wrap_diff;
	uint32_t flush_diff;
	uint32_t sequence_mask;

	/*
	 * The rwlock protects this structure as well as
	 * the data in all fence objects belonging to this
	 * class. This should be OK as most fence objects are
	 * only read from once they're created.
	 */

	rwlock_t lock;
	struct list_head ring;
	uint32_t pending_flush;
	uint32_t waiting_types;
	wait_queue_head_t fence_queue;
	uint32_t highest_waiting_sequence;
	uint32_t latest_queued_sequence;
};

/**
 * struct ttm_fence_device
 *
 * @fence_class:  Array of fence class managers.
 * @num_classes:  Array dimension of @fence_class.
 * @count:        Current number of fence objects for statistics.
 * @driver:       Driver struct.
 *
 * Provided in the driver interface so that the driver can derive
 * from this struct for its driver_private, and accordingly
 * access the driver_private from the fence driver callbacks.
 *
 * All members except "count" are initialized at creation and
 * never touched after that. No protection needed.
 *
 * This struct is private to the fence implementation and to the fence
 * driver callbacks, and may otherwise be used by drivers only to
 * obtain the derived device_private object using container_of().
 */

struct ttm_fence_device {
	struct ttm_mem_global *mem_glob;
	struct ttm_fence_class_manager *fence_class;
	uint32_t num_classes;
	atomic_t count;
	const struct ttm_fence_driver *driver;
};

/**
 * struct ttm_fence_class_init
 *
 * @wrap_diff:    Fence sequence number wrap indicator. If
 * (sequence1 - sequence2) > @wrap_diff, then sequence1 is
 * considered to be older than sequence2.
 * @flush_diff:   Fence sequence number flush indicator.
 * If a non-completely-signaled fence has a fence sequence number
 * sequence1 and (sequence1 - current_emit_sequence) > @flush_diff,
 * the fence is considered too old and it will be flushed upon the
 * next call of ttm_fence_flush_old(), to make sure no fences with
 * stale sequence numbers remains unsignaled. @flush_diff should
 * be sufficiently less than @wrap_diff.
 * @sequence_mask: Mask with valid bits of the fence sequence
 * number set to 1.
 *
 * This struct is used as input to ttm_fence_device_init.
 */

struct ttm_fence_class_init {
	uint32_t wrap_diff;
	uint32_t flush_diff;
	uint32_t sequence_mask;
};

/**
 * struct ttm_fence_driver
 *
 * @has_irq: Called by a potential waiter. Should return 1 if a
 * fence object with indicated parameters is expected to signal
 * automatically, and 0 if the fence implementation needs to
 * repeatedly call @poll to make it signal.
 * @emit:    Make sure a fence with the given parameters is
 * present in the indicated command stream. Return its sequence number
 * in "breadcrumb".
 * @poll:    Check and report sequences of the given "fence_class"
 *           that have signaled "types"
 * @flush:   Make sure that the types indicated by the bitfield
 *           ttm_fence_class_manager::pending_flush will eventually
 *           signal. These bits have been put together using the
 *           result from the needed_flush function described below.
 * @needed_flush: Given the fence_class and fence_types indicated by
 *           "fence", and the last received fence sequence of this
 *           fence class, indicate what types need a fence flush to
 *           signal. Return as a bitfield.
 * @wait:    Set to non-NULL if the driver wants to override the fence
 *           wait implementation. Return 0 on success, -EBUSY on failure,
 *           and -ERESTART if interruptible and a signal is pending.
 * @signaled:  Driver callback that is called whenever a
 *           ttm_fence_object::signaled_types has changed status.
 *           This function is called from atomic context,
 *           with the ttm_fence_class_manager::lock held in write mode.
 * @lockup:  Driver callback that is called whenever a wait has exceeded
 *           the lifetime of a fence object.
 *           If there is a GPU lockup,
 *           this function should, if possible, reset the GPU,
 *           call the ttm_fence_handler with an error status, and
 *           return. If no lockup was detected, simply extend the
 *           fence timeout_jiffies and return. The driver might
 *           want to protect the lockup check with a mutex and cache a
 *           non-locked-up status for a while to avoid an excessive
 *           amount of lockup checks from every waiting thread.
 */

struct ttm_fence_driver {
	bool (*has_irq) (struct ttm_fence_device *fdev,
			uint32_t fence_class, uint32_t flags);
	int (*emit) (struct ttm_fence_device *fdev,
		     uint32_t fence_class,
		     uint32_t flags,
		     uint32_t *breadcrumb, unsigned long *timeout_jiffies);
	void (*flush) (struct ttm_fence_device *fdev, uint32_t fence_class);
	void (*poll) (struct ttm_fence_device *fdev,
		      uint32_t fence_class, uint32_t types);
	 uint32_t(*needed_flush)
	 (struct ttm_fence_object *fence);
	int (*wait) (struct ttm_fence_object *fence, bool lazy,
		     bool interruptible, uint32_t mask);
	void (*signaled) (struct ttm_fence_object *fence);
	void (*lockup) (struct ttm_fence_object *fence, uint32_t fence_types);
};

/**
 * function ttm_fence_device_init
 *
 * @num_classes:      Number of fence classes for this fence implementation.
 * @mem_global:       Pointer to the global memory accounting info.
 * @fdev:             Pointer to an uninitialised struct ttm_fence_device.
 * @init:             Array of initialization info for each fence class.
 * @replicate_init:   Use the first @init initialization info for all classes.
 * @driver:           Driver callbacks.
 *
 * Initialize a struct ttm_fence_driver structure. Returns -ENOMEM if
 * out-of-memory. Otherwise returns 0.
 */
extern int
ttm_fence_device_init(int num_classes,
		      struct ttm_mem_global *mem_glob,
		      struct ttm_fence_device *fdev,
		      const struct ttm_fence_class_init *init,
		      bool replicate_init,
		      const struct ttm_fence_driver *driver);

/**
 * function ttm_fence_device_release
 *
 * @fdev:             Pointer to the fence device.
 *
 * Release all resources held by a fence device. Note that before
 * this function is called, the caller must have made sure all fence
 * objects belonging to this fence device are completely signaled.
 */

extern void ttm_fence_device_release(struct ttm_fence_device *fdev);

/**
 * ttm_fence_handler - the fence handler.
 *
 * @fdev:        Pointer to the fence device.
 * @fence_class: Fence class that signals.
 * @sequence:    Signaled sequence.
 * @type:        Types that signal.
 * @error:       Error from the engine.
 *
 * This function signals all fences with a sequence previous to the
 * @sequence argument, and belonging to @fence_class. The signaled fence
 * types are provided in @type. If error is non-zero, the error member
 * of the fence with sequence = @sequence is set to @error. This value
 * may be reported back to user-space, indicating, for example an illegal
 * 3D command or illegal mpeg data.
 *
 * This function is typically called from the driver::poll method when the
 * command sequence preceding the fence marker has executed. It should be
 * called with the ttm_fence_class_manager::lock held in write mode and
 * may be called from interrupt context.
 */

extern void
ttm_fence_handler(struct ttm_fence_device *fdev,
		  uint32_t fence_class,
		  uint32_t sequence, uint32_t type, uint32_t error);

/**
 * ttm_fence_driver_from_dev
 *
 * @fdev:        The ttm fence device.
 *
 * Returns a pointer to the fence driver struct.
 */

static inline const struct ttm_fence_driver *ttm_fence_driver_from_dev(
						struct ttm_fence_device *fdev)
{
	return fdev->driver;
}

/**
 * ttm_fence_driver
 *
 * @fence:        Pointer to a ttm fence object.
 *
 * Returns a pointer to the fence driver struct.
 */

static inline const struct ttm_fence_driver *ttm_fence_driver(struct
							      ttm_fence_object
							      *fence)
{
	return ttm_fence_driver_from_dev(fence->fdev);
}

/**
 * ttm_fence_fc
 *
 * @fence:        Pointer to a ttm fence object.
 *
 * Returns a pointer to the struct ttm_fence_class_manager for the
 * fence class of @fence.
 */

static inline struct ttm_fence_class_manager *ttm_fence_fc(struct
							   ttm_fence_object
							   *fence)
{
	return &fence->fdev->fence_class[fence->fence_class];
}

#endif
