// SPDX-License-Identifier: GPL-2.0-only
/*
 * fence-chain: chain fences together in a timeline
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 */

#include <linux/dma-fence-chain.h>

static bool dma_fence_chain_enable_signaling(struct dma_fence *fence);

/**
 * dma_fence_chain_get_prev - use RCU to get a reference to the previous fence
 * @chain: chain node to get the previous node from
 *
 * Use dma_fence_get_rcu_safe to get a reference to the previous fence of the
 * chain node.
 */
static struct dma_fence *dma_fence_chain_get_prev(struct dma_fence_chain *chain)
{
	struct dma_fence *prev;

	rcu_read_lock();
	prev = dma_fence_get_rcu_safe(&chain->prev);
	rcu_read_unlock();
	return prev;
}

/**
 * dma_fence_chain_walk - chain walking function
 * @fence: current chain node
 *
 * Walk the chain to the next node. Returns the next fence or NULL if we are at
 * the end of the chain. Garbage collects chain nodes which are already
 * signaled.
 */
struct dma_fence *dma_fence_chain_walk(struct dma_fence *fence)
{
	struct dma_fence_chain *chain, *prev_chain;
	struct dma_fence *prev, *replacement, *tmp;

	chain = to_dma_fence_chain(fence);
	if (!chain) {
		dma_fence_put(fence);
		return NULL;
	}

	while ((prev = dma_fence_chain_get_prev(chain))) {

		prev_chain = to_dma_fence_chain(prev);
		if (prev_chain) {
			if (!dma_fence_is_signaled(prev_chain->fence))
				break;

			replacement = dma_fence_chain_get_prev(prev_chain);
		} else {
			if (!dma_fence_is_signaled(prev))
				break;

			replacement = NULL;
		}

		tmp = cmpxchg((struct dma_fence __force **)&chain->prev,
			      prev, replacement);
		if (tmp == prev)
			dma_fence_put(tmp);
		else
			dma_fence_put(replacement);
		dma_fence_put(prev);
	}

	dma_fence_put(fence);
	return prev;
}
EXPORT_SYMBOL(dma_fence_chain_walk);

/**
 * dma_fence_chain_find_seqno - find fence chain node by seqno
 * @pfence: pointer to the chain node where to start
 * @seqno: the sequence number to search for
 *
 * Advance the fence pointer to the chain node which will signal this sequence
 * number. If no sequence number is provided then this is a no-op.
 *
 * Returns EINVAL if the fence is not a chain node or the sequence number has
 * not yet advanced far enough.
 */
int dma_fence_chain_find_seqno(struct dma_fence **pfence, uint64_t seqno)
{
	struct dma_fence_chain *chain;

	if (!seqno)
		return 0;

	chain = to_dma_fence_chain(*pfence);
	if (!chain || chain->base.seqno < seqno)
		return -EINVAL;

	dma_fence_chain_for_each(*pfence, &chain->base) {
		if ((*pfence)->context != chain->base.context ||
		    to_dma_fence_chain(*pfence)->prev_seqno < seqno)
			break;
	}
	dma_fence_put(&chain->base);

	return 0;
}
EXPORT_SYMBOL(dma_fence_chain_find_seqno);

static const char *dma_fence_chain_get_driver_name(struct dma_fence *fence)
{
        return "dma_fence_chain";
}

static const char *dma_fence_chain_get_timeline_name(struct dma_fence *fence)
{
        return "unbound";
}

static void dma_fence_chain_irq_work(struct irq_work *work)
{
	struct dma_fence_chain *chain;

	chain = container_of(work, typeof(*chain), work);

	/* Try to rearm the callback */
	if (!dma_fence_chain_enable_signaling(&chain->base))
		/* Ok, we are done. No more unsignaled fences left */
		dma_fence_signal(&chain->base);
	dma_fence_put(&chain->base);
}

static void dma_fence_chain_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct dma_fence_chain *chain;

	chain = container_of(cb, typeof(*chain), cb);
	init_irq_work(&chain->work, dma_fence_chain_irq_work);
	irq_work_queue(&chain->work);
	dma_fence_put(f);
}

static bool dma_fence_chain_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_chain *head = to_dma_fence_chain(fence);

	dma_fence_get(&head->base);
	dma_fence_chain_for_each(fence, &head->base) {
		struct dma_fence *f = dma_fence_chain_contained(fence);

		dma_fence_get(f);
		if (!dma_fence_add_callback(f, &head->cb, dma_fence_chain_cb)) {
			dma_fence_put(fence);
			return true;
		}
		dma_fence_put(f);
	}
	dma_fence_put(&head->base);
	return false;
}

static bool dma_fence_chain_signaled(struct dma_fence *fence)
{
	dma_fence_chain_for_each(fence, fence) {
		struct dma_fence *f = dma_fence_chain_contained(fence);

		if (!dma_fence_is_signaled(f)) {
			dma_fence_put(fence);
			return false;
		}
	}

	return true;
}

static void dma_fence_chain_release(struct dma_fence *fence)
{
	struct dma_fence_chain *chain = to_dma_fence_chain(fence);
	struct dma_fence *prev;

	/* Manually unlink the chain as much as possible to avoid recursion
	 * and potential stack overflow.
	 */
	while ((prev = rcu_dereference_protected(chain->prev, true))) {
		struct dma_fence_chain *prev_chain;

		if (kref_read(&prev->refcount) > 1)
		       break;

		prev_chain = to_dma_fence_chain(prev);
		if (!prev_chain)
			break;

		/* No need for atomic operations since we hold the last
		 * reference to prev_chain.
		 */
		chain->prev = prev_chain->prev;
		RCU_INIT_POINTER(prev_chain->prev, NULL);
		dma_fence_put(prev);
	}
	dma_fence_put(prev);

	dma_fence_put(chain->fence);
	dma_fence_free(fence);
}

const struct dma_fence_ops dma_fence_chain_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = dma_fence_chain_get_driver_name,
	.get_timeline_name = dma_fence_chain_get_timeline_name,
	.enable_signaling = dma_fence_chain_enable_signaling,
	.signaled = dma_fence_chain_signaled,
	.release = dma_fence_chain_release,
};
EXPORT_SYMBOL(dma_fence_chain_ops);

/**
 * dma_fence_chain_init - initialize a fence chain
 * @chain: the chain node to initialize
 * @prev: the previous fence
 * @fence: the current fence
 * @seqno: the sequence number to use for the fence chain
 *
 * Initialize a new chain node and either start a new chain or add the node to
 * the existing chain of the previous fence.
 */
void dma_fence_chain_init(struct dma_fence_chain *chain,
			  struct dma_fence *prev,
			  struct dma_fence *fence,
			  uint64_t seqno)
{
	struct dma_fence_chain *prev_chain = to_dma_fence_chain(prev);
	uint64_t context;

	spin_lock_init(&chain->lock);
	rcu_assign_pointer(chain->prev, prev);
	chain->fence = fence;
	chain->prev_seqno = 0;

	/* Try to reuse the context of the previous chain node. */
	if (prev_chain && __dma_fence_is_later(seqno, prev->seqno, prev->ops)) {
		context = prev->context;
		chain->prev_seqno = prev->seqno;
	} else {
		context = dma_fence_context_alloc(1);
		/* Make sure that we always have a valid sequence number. */
		if (prev_chain)
			seqno = max(prev->seqno, seqno);
	}

	dma_fence_init(&chain->base, &dma_fence_chain_ops,
		       &chain->lock, context, seqno);

	/*
	 * Chaining dma_fence_chain container together is only allowed through
	 * the prev fence and not through the contained fence.
	 *
	 * The correct way of handling this is to flatten out the fence
	 * structure into a dma_fence_array by the caller instead.
	 */
	WARN_ON(dma_fence_is_chain(fence));
}
EXPORT_SYMBOL(dma_fence_chain_init);
