/*
 * Copyright (C) 2012-2014 Canonical Ltd (Maarten Lankhorst)
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
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
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include <linux/dma-resv.h>
#include <linux/export.h>
#include <linux/sched/mm.h>

/**
 * DOC: Reservation Object Overview
 *
 * The reservation object provides a mechanism to manage shared and
 * exclusive fences associated with a buffer.  A reservation object
 * can have attached one exclusive fence (normally associated with
 * write operations) or N shared fences (read operations).  The RCU
 * mechanism is used to protect read access to fences from locked
 * write-side updates.
 */

DEFINE_WD_CLASS(reservation_ww_class);
EXPORT_SYMBOL(reservation_ww_class);

struct lock_class_key reservation_seqcount_class;
EXPORT_SYMBOL(reservation_seqcount_class);

const char reservation_seqcount_string[] = "reservation_seqcount";
EXPORT_SYMBOL(reservation_seqcount_string);

/**
 * dma_resv_list_alloc - allocate fence list
 * @shared_max: number of fences we need space for
 *
 * Allocate a new dma_resv_list and make sure to correctly initialize
 * shared_max.
 */
static struct dma_resv_list *dma_resv_list_alloc(unsigned int shared_max)
{
	struct dma_resv_list *list;

	list = kmalloc(offsetof(typeof(*list), shared[shared_max]), GFP_KERNEL);
	if (!list)
		return NULL;

	list->shared_max = (ksize(list) - offsetof(typeof(*list), shared)) /
		sizeof(*list->shared);

	return list;
}

/**
 * dma_resv_list_free - free fence list
 * @list: list to free
 *
 * Free a dma_resv_list and make sure to drop all references.
 */
static void dma_resv_list_free(struct dma_resv_list *list)
{
	unsigned int i;

	if (!list)
		return;

	for (i = 0; i < list->shared_count; ++i)
		dma_fence_put(rcu_dereference_protected(list->shared[i], true));

	kfree_rcu(list, rcu);
}

#if IS_ENABLED(CONFIG_LOCKDEP)
static int __init dma_resv_lockdep(void)
{
	struct mm_struct *mm = mm_alloc();
	struct ww_acquire_ctx ctx;
	struct dma_resv obj;
	int ret;

	if (!mm)
		return -ENOMEM;

	dma_resv_init(&obj);

	down_read(&mm->mmap_sem);
	ww_acquire_init(&ctx, &reservation_ww_class);
	ret = dma_resv_lock(&obj, &ctx);
	if (ret == -EDEADLK)
		dma_resv_lock_slow(&obj, &ctx);
	fs_reclaim_acquire(GFP_KERNEL);
	fs_reclaim_release(GFP_KERNEL);
	ww_mutex_unlock(&obj.lock);
	ww_acquire_fini(&ctx);
	up_read(&mm->mmap_sem);
	
	mmput(mm);

	return 0;
}
subsys_initcall(dma_resv_lockdep);
#endif

/**
 * dma_resv_init - initialize a reservation object
 * @obj: the reservation object
 */
void dma_resv_init(struct dma_resv *obj)
{
	ww_mutex_init(&obj->lock, &reservation_ww_class);

	__seqcount_init(&obj->seq, reservation_seqcount_string,
			&reservation_seqcount_class);
	RCU_INIT_POINTER(obj->fence, NULL);
	RCU_INIT_POINTER(obj->fence_excl, NULL);
}
EXPORT_SYMBOL(dma_resv_init);

/**
 * dma_resv_fini - destroys a reservation object
 * @obj: the reservation object
 */
void dma_resv_fini(struct dma_resv *obj)
{
	struct dma_resv_list *fobj;
	struct dma_fence *excl;

	/*
	 * This object should be dead and all references must have
	 * been released to it, so no need to be protected with rcu.
	 */
	excl = rcu_dereference_protected(obj->fence_excl, 1);
	if (excl)
		dma_fence_put(excl);

	fobj = rcu_dereference_protected(obj->fence, 1);
	dma_resv_list_free(fobj);
	ww_mutex_destroy(&obj->lock);
}
EXPORT_SYMBOL(dma_resv_fini);

/**
 * dma_resv_reserve_shared - Reserve space to add shared fences to
 * a dma_resv.
 * @obj: reservation object
 * @num_fences: number of fences we want to add
 *
 * Should be called before dma_resv_add_shared_fence().  Must
 * be called with obj->lock held.
 *
 * RETURNS
 * Zero for success, or -errno
 */
int dma_resv_reserve_shared(struct dma_resv *obj, unsigned int num_fences)
{
	struct dma_resv_list *old, *new;
	unsigned int i, j, k, max;

	dma_resv_assert_held(obj);

	old = dma_resv_get_list(obj);

	if (old && old->shared_max) {
		if ((old->shared_count + num_fences) <= old->shared_max)
			return 0;
		else
			max = max(old->shared_count + num_fences,
				  old->shared_max * 2);
	} else {
		max = 4;
	}

	new = dma_resv_list_alloc(max);
	if (!new)
		return -ENOMEM;

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	for (i = 0, j = 0, k = max; i < (old ? old->shared_count : 0); ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(old->shared[i],
						  dma_resv_held(obj));
		if (dma_fence_is_signaled(fence))
			RCU_INIT_POINTER(new->shared[--k], fence);
		else
			RCU_INIT_POINTER(new->shared[j++], fence);
	}
	new->shared_count = j;

	/*
	 * We are not changing the effective set of fences here so can
	 * merely update the pointer to the new array; both existing
	 * readers and new readers will see exactly the same set of
	 * active (unsignaled) shared fences. Individual fences and the
	 * old array are protected by RCU and so will not vanish under
	 * the gaze of the rcu_read_lock() readers.
	 */
	rcu_assign_pointer(obj->fence, new);

	if (!old)
		return 0;

	/* Drop the references to the signaled fences */
	for (i = k; i < max; ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(new->shared[i],
						  dma_resv_held(obj));
		dma_fence_put(fence);
	}
	kfree_rcu(old, rcu);

	return 0;
}
EXPORT_SYMBOL(dma_resv_reserve_shared);

/**
 * dma_resv_add_shared_fence - Add a fence to a shared slot
 * @obj: the reservation object
 * @fence: the shared fence to add
 *
 * Add a fence to a shared slot, obj->lock must be held, and
 * dma_resv_reserve_shared() has been called.
 */
void dma_resv_add_shared_fence(struct dma_resv *obj, struct dma_fence *fence)
{
	struct dma_resv_list *fobj;
	struct dma_fence *old;
	unsigned int i, count;

	dma_fence_get(fence);

	dma_resv_assert_held(obj);

	fobj = dma_resv_get_list(obj);
	count = fobj->shared_count;

	preempt_disable();
	write_seqcount_begin(&obj->seq);

	for (i = 0; i < count; ++i) {

		old = rcu_dereference_protected(fobj->shared[i],
						dma_resv_held(obj));
		if (old->context == fence->context ||
		    dma_fence_is_signaled(old))
			goto replace;
	}

	BUG_ON(fobj->shared_count >= fobj->shared_max);
	old = NULL;
	count++;

replace:
	RCU_INIT_POINTER(fobj->shared[i], fence);
	/* pointer update must be visible before we extend the shared_count */
	smp_store_mb(fobj->shared_count, count);

	write_seqcount_end(&obj->seq);
	preempt_enable();
	dma_fence_put(old);
}
EXPORT_SYMBOL(dma_resv_add_shared_fence);

/**
 * dma_resv_add_excl_fence - Add an exclusive fence.
 * @obj: the reservation object
 * @fence: the shared fence to add
 *
 * Add a fence to the exclusive slot.  The obj->lock must be held.
 */
void dma_resv_add_excl_fence(struct dma_resv *obj, struct dma_fence *fence)
{
	struct dma_fence *old_fence = dma_resv_get_excl(obj);
	struct dma_resv_list *old;
	u32 i = 0;

	dma_resv_assert_held(obj);

	old = dma_resv_get_list(obj);
	if (old)
		i = old->shared_count;

	if (fence)
		dma_fence_get(fence);

	preempt_disable();
	write_seqcount_begin(&obj->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(obj->fence_excl, fence);
	if (old)
		old->shared_count = 0;
	write_seqcount_end(&obj->seq);
	preempt_enable();

	/* inplace update, no shared fences */
	while (i--)
		dma_fence_put(rcu_dereference_protected(old->shared[i],
						dma_resv_held(obj)));

	dma_fence_put(old_fence);
}
EXPORT_SYMBOL(dma_resv_add_excl_fence);

/**
* dma_resv_copy_fences - Copy all fences from src to dst.
* @dst: the destination reservation object
* @src: the source reservation object
*
* Copy all fences from src to dst. dst-lock must be held.
*/
int dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src)
{
	struct dma_resv_list *src_list, *dst_list;
	struct dma_fence *old, *new;
	unsigned i;

	dma_resv_assert_held(dst);

	rcu_read_lock();
	src_list = rcu_dereference(src->fence);

retry:
	if (src_list) {
		unsigned shared_count = src_list->shared_count;

		rcu_read_unlock();

		dst_list = dma_resv_list_alloc(shared_count);
		if (!dst_list)
			return -ENOMEM;

		rcu_read_lock();
		src_list = rcu_dereference(src->fence);
		if (!src_list || src_list->shared_count > shared_count) {
			kfree(dst_list);
			goto retry;
		}

		dst_list->shared_count = 0;
		for (i = 0; i < src_list->shared_count; ++i) {
			struct dma_fence *fence;

			fence = rcu_dereference(src_list->shared[i]);
			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &fence->flags))
				continue;

			if (!dma_fence_get_rcu(fence)) {
				dma_resv_list_free(dst_list);
				src_list = rcu_dereference(src->fence);
				goto retry;
			}

			if (dma_fence_is_signaled(fence)) {
				dma_fence_put(fence);
				continue;
			}

			rcu_assign_pointer(dst_list->shared[dst_list->shared_count++], fence);
		}
	} else {
		dst_list = NULL;
	}

	new = dma_fence_get_rcu_safe(&src->fence_excl);
	rcu_read_unlock();

	src_list = dma_resv_get_list(dst);
	old = dma_resv_get_excl(dst);

	preempt_disable();
	write_seqcount_begin(&dst->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(dst->fence_excl, new);
	RCU_INIT_POINTER(dst->fence, dst_list);
	write_seqcount_end(&dst->seq);
	preempt_enable();

	dma_resv_list_free(src_list);
	dma_fence_put(old);

	return 0;
}
EXPORT_SYMBOL(dma_resv_copy_fences);

/**
 * dma_resv_get_fences_rcu - Get an object's shared and exclusive
 * fences without update side lock held
 * @obj: the reservation object
 * @pfence_excl: the returned exclusive fence (or NULL)
 * @pshared_count: the number of shared fences returned
 * @pshared: the array of shared fence ptrs returned (array is krealloc'd to
 * the required size, and must be freed by caller)
 *
 * Retrieve all fences from the reservation object. If the pointer for the
 * exclusive fence is not specified the fence is put into the array of the
 * shared fences as well. Returns either zero or -ENOMEM.
 */
int dma_resv_get_fences_rcu(struct dma_resv *obj,
			    struct dma_fence **pfence_excl,
			    unsigned *pshared_count,
			    struct dma_fence ***pshared)
{
	struct dma_fence **shared = NULL;
	struct dma_fence *fence_excl;
	unsigned int shared_count;
	int ret = 1;

	do {
		struct dma_resv_list *fobj;
		unsigned int i, seq;
		size_t sz = 0;

		shared_count = i = 0;

		rcu_read_lock();
		seq = read_seqcount_begin(&obj->seq);

		fence_excl = rcu_dereference(obj->fence_excl);
		if (fence_excl && !dma_fence_get_rcu(fence_excl))
			goto unlock;

		fobj = rcu_dereference(obj->fence);
		if (fobj)
			sz += sizeof(*shared) * fobj->shared_max;

		if (!pfence_excl && fence_excl)
			sz += sizeof(*shared);

		if (sz) {
			struct dma_fence **nshared;

			nshared = krealloc(shared, sz,
					   GFP_NOWAIT | __GFP_NOWARN);
			if (!nshared) {
				rcu_read_unlock();

				dma_fence_put(fence_excl);
				fence_excl = NULL;

				nshared = krealloc(shared, sz, GFP_KERNEL);
				if (nshared) {
					shared = nshared;
					continue;
				}

				ret = -ENOMEM;
				break;
			}
			shared = nshared;
			shared_count = fobj ? fobj->shared_count : 0;
			for (i = 0; i < shared_count; ++i) {
				shared[i] = rcu_dereference(fobj->shared[i]);
				if (!dma_fence_get_rcu(shared[i]))
					break;
			}
		}

		if (i != shared_count || read_seqcount_retry(&obj->seq, seq)) {
			while (i--)
				dma_fence_put(shared[i]);
			dma_fence_put(fence_excl);
			goto unlock;
		}

		ret = 0;
unlock:
		rcu_read_unlock();
	} while (ret);

	if (pfence_excl)
		*pfence_excl = fence_excl;
	else if (fence_excl)
		shared[shared_count++] = fence_excl;

	if (!shared_count) {
		kfree(shared);
		shared = NULL;
	}

	*pshared_count = shared_count;
	*pshared = shared;
	return ret;
}
EXPORT_SYMBOL_GPL(dma_resv_get_fences_rcu);

/**
 * dma_resv_wait_timeout_rcu - Wait on reservation's objects
 * shared and/or exclusive fences.
 * @obj: the reservation object
 * @wait_all: if true, wait on all fences, else wait on just exclusive fence
 * @intr: if true, do interruptible wait
 * @timeout: timeout value in jiffies or zero to return immediately
 *
 * RETURNS
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or
 * greater than zer on success.
 */
long dma_resv_wait_timeout_rcu(struct dma_resv *obj,
			       bool wait_all, bool intr,
			       unsigned long timeout)
{
	struct dma_fence *fence;
	unsigned seq, shared_count;
	long ret = timeout ? timeout : 1;
	int i;

retry:
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();
	i = -1;

	fence = rcu_dereference(obj->fence_excl);
	if (fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		if (!dma_fence_get_rcu(fence))
			goto unlock_retry;

		if (dma_fence_is_signaled(fence)) {
			dma_fence_put(fence);
			fence = NULL;
		}

	} else {
		fence = NULL;
	}

	if (wait_all) {
		struct dma_resv_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		for (i = 0; !fence && i < shared_count; ++i) {
			struct dma_fence *lfence = rcu_dereference(fobj->shared[i]);

			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &lfence->flags))
				continue;

			if (!dma_fence_get_rcu(lfence))
				goto unlock_retry;

			if (dma_fence_is_signaled(lfence)) {
				dma_fence_put(lfence);
				continue;
			}

			fence = lfence;
			break;
		}
	}

	rcu_read_unlock();
	if (fence) {
		if (read_seqcount_retry(&obj->seq, seq)) {
			dma_fence_put(fence);
			goto retry;
		}

		ret = dma_fence_wait_timeout(fence, intr, ret);
		dma_fence_put(fence);
		if (ret > 0 && wait_all && (i + 1 < shared_count))
			goto retry;
	}
	return ret;

unlock_retry:
	rcu_read_unlock();
	goto retry;
}
EXPORT_SYMBOL_GPL(dma_resv_wait_timeout_rcu);


static inline int dma_resv_test_signaled_single(struct dma_fence *passed_fence)
{
	struct dma_fence *fence, *lfence = passed_fence;
	int ret = 1;

	if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &lfence->flags)) {
		fence = dma_fence_get_rcu(lfence);
		if (!fence)
			return -1;

		ret = !!dma_fence_is_signaled(fence);
		dma_fence_put(fence);
	}
	return ret;
}

/**
 * dma_resv_test_signaled_rcu - Test if a reservation object's
 * fences have been signaled.
 * @obj: the reservation object
 * @test_all: if true, test all fences, otherwise only test the exclusive
 * fence
 *
 * RETURNS
 * true if all fences signaled, else false
 */
bool dma_resv_test_signaled_rcu(struct dma_resv *obj, bool test_all)
{
	unsigned seq, shared_count;
	int ret;

	rcu_read_lock();
retry:
	ret = true;
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);

	if (test_all) {
		unsigned i;

		struct dma_resv_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		for (i = 0; i < shared_count; ++i) {
			struct dma_fence *fence = rcu_dereference(fobj->shared[i]);

			ret = dma_resv_test_signaled_single(fence);
			if (ret < 0)
				goto retry;
			else if (!ret)
				break;
		}

		if (read_seqcount_retry(&obj->seq, seq))
			goto retry;
	}

	if (!shared_count) {
		struct dma_fence *fence_excl = rcu_dereference(obj->fence_excl);

		if (fence_excl) {
			ret = dma_resv_test_signaled_single(fence_excl);
			if (ret < 0)
				goto retry;

			if (read_seqcount_retry(&obj->seq, seq))
				goto retry;
		}
	}

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(dma_resv_test_signaled_rcu);
