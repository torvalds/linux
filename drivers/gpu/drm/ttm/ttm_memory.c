/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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

#include "ttm/ttm_memory.h"
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/module.h>

#define TTM_PFX "[TTM] "
#define TTM_MEMORY_ALLOC_RETRIES 4

/**
 * At this point we only support a single shrink callback.
 * Extend this if needed, perhaps using a linked list of callbacks.
 * Note that this function is reentrant:
 * many threads may try to swap out at any given time.
 */

static void ttm_shrink(struct ttm_mem_global *glob, bool from_workqueue,
		       uint64_t extra)
{
	int ret;
	struct ttm_mem_shrink *shrink;
	uint64_t target;
	uint64_t total_target;

	spin_lock(&glob->lock);
	if (glob->shrink == NULL)
		goto out;

	if (from_workqueue) {
		target = glob->swap_limit;
		total_target = glob->total_memory_swap_limit;
	} else if (capable(CAP_SYS_ADMIN)) {
		total_target = glob->emer_total_memory;
		target = glob->emer_memory;
	} else {
		total_target = glob->max_total_memory;
		target = glob->max_memory;
	}

	total_target = (extra >= total_target) ? 0 : total_target - extra;
	target = (extra >= target) ? 0 : target - extra;

	while (glob->used_memory > target ||
	       glob->used_total_memory > total_target) {
		shrink = glob->shrink;
		spin_unlock(&glob->lock);
		ret = shrink->do_shrink(shrink);
		spin_lock(&glob->lock);
		if (unlikely(ret != 0))
			goto out;
	}
out:
	spin_unlock(&glob->lock);
}

static void ttm_shrink_work(struct work_struct *work)
{
	struct ttm_mem_global *glob =
	    container_of(work, struct ttm_mem_global, work);

	ttm_shrink(glob, true, 0ULL);
}

int ttm_mem_global_init(struct ttm_mem_global *glob)
{
	struct sysinfo si;
	uint64_t mem;

	spin_lock_init(&glob->lock);
	glob->swap_queue = create_singlethread_workqueue("ttm_swap");
	INIT_WORK(&glob->work, ttm_shrink_work);
	init_waitqueue_head(&glob->queue);

	si_meminfo(&si);

	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	glob->max_memory = mem >> 1;
	glob->emer_memory = (mem >> 1) + (mem >> 2);
	glob->swap_limit = glob->max_memory - (mem >> 3);
	glob->used_memory = 0;
	glob->used_total_memory = 0;
	glob->shrink = NULL;

	mem = si.totalram;
	mem *= si.mem_unit;

	glob->max_total_memory = mem >> 1;
	glob->emer_total_memory = (mem >> 1) + (mem >> 2);

	glob->total_memory_swap_limit = glob->max_total_memory - (mem >> 3);

	printk(KERN_INFO TTM_PFX "TTM available graphics memory: %llu MiB\n",
	       glob->max_total_memory >> 20);
	printk(KERN_INFO TTM_PFX "TTM available object memory: %llu MiB\n",
	       glob->max_memory >> 20);

	return 0;
}
EXPORT_SYMBOL(ttm_mem_global_init);

void ttm_mem_global_release(struct ttm_mem_global *glob)
{
	printk(KERN_INFO TTM_PFX "Used total memory is %llu bytes.\n",
	       (unsigned long long)glob->used_total_memory);
	flush_workqueue(glob->swap_queue);
	destroy_workqueue(glob->swap_queue);
	glob->swap_queue = NULL;
}
EXPORT_SYMBOL(ttm_mem_global_release);

static inline void ttm_check_swapping(struct ttm_mem_global *glob)
{
	bool needs_swapping;

	spin_lock(&glob->lock);
	needs_swapping = (glob->used_memory > glob->swap_limit ||
			  glob->used_total_memory >
			  glob->total_memory_swap_limit);
	spin_unlock(&glob->lock);

	if (unlikely(needs_swapping))
		(void)queue_work(glob->swap_queue, &glob->work);

}

void ttm_mem_global_free(struct ttm_mem_global *glob,
			 uint64_t amount, bool himem)
{
	spin_lock(&glob->lock);
	glob->used_total_memory -= amount;
	if (!himem)
		glob->used_memory -= amount;
	wake_up_all(&glob->queue);
	spin_unlock(&glob->lock);
}

static int ttm_mem_global_reserve(struct ttm_mem_global *glob,
				  uint64_t amount, bool himem, bool reserve)
{
	uint64_t limit;
	uint64_t lomem_limit;
	int ret = -ENOMEM;

	spin_lock(&glob->lock);

	if (capable(CAP_SYS_ADMIN)) {
		limit = glob->emer_total_memory;
		lomem_limit = glob->emer_memory;
	} else {
		limit = glob->max_total_memory;
		lomem_limit = glob->max_memory;
	}

	if (unlikely(glob->used_total_memory + amount > limit))
		goto out_unlock;
	if (unlikely(!himem && glob->used_memory + amount > lomem_limit))
		goto out_unlock;

	if (reserve) {
		glob->used_total_memory += amount;
		if (!himem)
			glob->used_memory += amount;
	}
	ret = 0;
out_unlock:
	spin_unlock(&glob->lock);
	ttm_check_swapping(glob);

	return ret;
}

int ttm_mem_global_alloc(struct ttm_mem_global *glob, uint64_t memory,
			 bool no_wait, bool interruptible, bool himem)
{
	int count = TTM_MEMORY_ALLOC_RETRIES;

	while (unlikely(ttm_mem_global_reserve(glob, memory, himem, true)
			!= 0)) {
		if (no_wait)
			return -ENOMEM;
		if (unlikely(count-- == 0))
			return -ENOMEM;
		ttm_shrink(glob, false, memory + (memory >> 2) + 16);
	}

	return 0;
}

size_t ttm_round_pot(size_t size)
{
	if ((size & (size - 1)) == 0)
		return size;
	else if (size > PAGE_SIZE)
		return PAGE_ALIGN(size);
	else {
		size_t tmp_size = 4;

		while (tmp_size < size)
			tmp_size <<= 1;

		return tmp_size;
	}
	return 0;
}
