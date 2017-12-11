/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _GXIO_DMA_QUEUE_H_
#define _GXIO_DMA_QUEUE_H_

/*
 * DMA queue management APIs shared between TRIO and mPIPE.
 */

#include <gxio/common.h>

/* The credit counter lives in the high 32 bits. */
#define DMA_QUEUE_CREDIT_SHIFT 32

/*
 * State object that tracks a DMA queue's head and tail indices, as
 * well as the number of commands posted and completed.  The
 * structure is accessed via a thread-safe, lock-free algorithm.
 */
typedef struct {
	/*
	 * Address of a MPIPE_EDMA_POST_REGION_VAL_t,
	 * TRIO_PUSH_DMA_REGION_VAL_t, or TRIO_PULL_DMA_REGION_VAL_t
	 * register.  These register have identical encodings and provide
	 * information about how many commands have been processed.
	 */
	void *post_region_addr;

	/*
	 * A lazily-updated count of how many edescs the hardware has
	 * completed.
	 */
	uint64_t hw_complete_count __attribute__ ((aligned(64)));

	/*
	 * High 32 bits are a count of available egress command credits,
	 * low 24 bits are the next egress "slot".
	 */
	int64_t credits_and_next_index;

} __gxio_dma_queue_t;

/* Initialize a dma queue. */
extern void __gxio_dma_queue_init(__gxio_dma_queue_t *dma_queue,
				  void *post_region_addr,
				  unsigned int num_entries);

/*
 * Update the "credits_and_next_index" and "hw_complete_count" fields
 * based on pending hardware completions.  Note that some other thread
 * may have already done this and, importantly, may still be in the
 * process of updating "credits_and_next_index".
 */
extern void __gxio_dma_queue_update_credits(__gxio_dma_queue_t *dma_queue);

/* Wait for credits to become available. */
extern int64_t __gxio_dma_queue_wait_for_credits(__gxio_dma_queue_t *dma_queue,
						 int64_t modifier);

/* Reserve slots in the queue, optionally waiting for slots to become
 * available, and optionally returning a "completion_slot" suitable for
 * direct comparison to "hw_complete_count".
 */
static inline int64_t __gxio_dma_queue_reserve(__gxio_dma_queue_t *dma_queue,
					       unsigned int num, bool wait,
					       bool completion)
{
	uint64_t slot;

	/*
	 * Try to reserve 'num' egress command slots.  We do this by
	 * constructing a constant that subtracts N credits and adds N to
	 * the index, and using fetchaddgez to only apply it if the credits
	 * count doesn't go negative.
	 */
	int64_t modifier = (((int64_t)(-num)) << DMA_QUEUE_CREDIT_SHIFT) | num;
	int64_t old =
		__insn_fetchaddgez(&dma_queue->credits_and_next_index,
				   modifier);

	if (unlikely(old + modifier < 0)) {
		/*
		 * We're out of credits.  Try once to get more by checking for
		 * completed egress commands.  If that fails, wait or fail.
		 */
		__gxio_dma_queue_update_credits(dma_queue);
		old = __insn_fetchaddgez(&dma_queue->credits_and_next_index,
					 modifier);
		if (old + modifier < 0) {
			if (wait)
				old = __gxio_dma_queue_wait_for_credits
					(dma_queue, modifier);
			else
				return GXIO_ERR_DMA_CREDITS;
		}
	}

	/* The bottom 24 bits of old encode the "slot". */
	slot = (old & 0xffffff);

	if (completion) {
		/*
		 * A "completion_slot" is a "slot" which can be compared to
		 * "hw_complete_count" at any time in the future.  To convert
		 * "slot" into a "completion_slot", we access "hw_complete_count"
		 * once (knowing that we have reserved a slot, and thus, it will
		 * be "basically" accurate), and combine its high 40 bits with
		 * the 24 bit "slot", and handle "wrapping" by adding "1 << 24"
		 * if the result is LESS than "hw_complete_count".
		 */
		uint64_t complete;
		complete = READ_ONCE(dma_queue->hw_complete_count);
		slot |= (complete & 0xffffffffff000000);
		if (slot < complete)
			slot += 0x1000000;
	}

	/*
	 * If any of our slots mod 256 were equivalent to 0, go ahead and
	 * collect some egress credits, and update "hw_complete_count", and
	 * make sure the index doesn't overflow into the credits.
	 */
	if (unlikely(((old + num) & 0xff) < num)) {
		__gxio_dma_queue_update_credits(dma_queue);

		/* Make sure the index doesn't overflow into the credits. */
#ifdef __BIG_ENDIAN__
		*(((uint8_t *)&dma_queue->credits_and_next_index) + 4) = 0;
#else
		*(((uint8_t *)&dma_queue->credits_and_next_index) + 3) = 0;
#endif
	}

	return slot;
}

/* Non-inlinable "__gxio_dma_queue_reserve(..., true)". */
extern int64_t __gxio_dma_queue_reserve_aux(__gxio_dma_queue_t *dma_queue,
					    unsigned int num, int wait);

/* Check whether a particular "completion slot" has completed.
 *
 * Note that this function requires a "completion slot", and thus
 * cannot be used with the result of any "reserve_fast" function.
 */
extern int __gxio_dma_queue_is_complete(__gxio_dma_queue_t *dma_queue,
					int64_t completion_slot, int update);

#endif /* !_GXIO_DMA_QUEUE_H_ */
