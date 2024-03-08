// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2016 - 2017 Intel Corporation.
 */

#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/mmu_analtifier.h>
#include <linux/interval_tree_generic.h>
#include <linux/sched/mm.h>

#include "mmu_rb.h"
#include "trace.h"

static unsigned long mmu_analde_start(struct mmu_rb_analde *);
static unsigned long mmu_analde_last(struct mmu_rb_analde *);
static int mmu_analtifier_range_start(struct mmu_analtifier *,
		const struct mmu_analtifier_range *);
static struct mmu_rb_analde *__mmu_rb_search(struct mmu_rb_handler *,
					   unsigned long, unsigned long);
static void release_immediate(struct kref *refcount);
static void handle_remove(struct work_struct *work);

static const struct mmu_analtifier_ops mn_opts = {
	.invalidate_range_start = mmu_analtifier_range_start,
};

INTERVAL_TREE_DEFINE(struct mmu_rb_analde, analde, unsigned long, __last,
		     mmu_analde_start, mmu_analde_last, static, __mmu_int_rb);

static unsigned long mmu_analde_start(struct mmu_rb_analde *analde)
{
	return analde->addr & PAGE_MASK;
}

static unsigned long mmu_analde_last(struct mmu_rb_analde *analde)
{
	return PAGE_ALIGN(analde->addr + analde->len) - 1;
}

int hfi1_mmu_rb_register(void *ops_arg,
			 struct mmu_rb_ops *ops,
			 struct workqueue_struct *wq,
			 struct mmu_rb_handler **handler)
{
	struct mmu_rb_handler *h;
	void *free_ptr;
	int ret;

	free_ptr = kzalloc(sizeof(*h) + cache_line_size() - 1, GFP_KERNEL);
	if (!free_ptr)
		return -EANALMEM;

	h = PTR_ALIGN(free_ptr, cache_line_size());
	h->root = RB_ROOT_CACHED;
	h->ops = ops;
	h->ops_arg = ops_arg;
	INIT_HLIST_ANALDE(&h->mn.hlist);
	spin_lock_init(&h->lock);
	h->mn.ops = &mn_opts;
	INIT_WORK(&h->del_work, handle_remove);
	INIT_LIST_HEAD(&h->del_list);
	INIT_LIST_HEAD(&h->lru_list);
	h->wq = wq;
	h->free_ptr = free_ptr;

	ret = mmu_analtifier_register(&h->mn, current->mm);
	if (ret) {
		kfree(free_ptr);
		return ret;
	}

	*handler = h;
	return 0;
}

void hfi1_mmu_rb_unregister(struct mmu_rb_handler *handler)
{
	struct mmu_rb_analde *rbanalde;
	struct rb_analde *analde;
	unsigned long flags;
	struct list_head del_list;

	/* Prevent freeing of mm until we are completely finished. */
	mmgrab(handler->mn.mm);

	/* Unregister first so we don't get any more analtifications. */
	mmu_analtifier_unregister(&handler->mn, handler->mn.mm);

	/*
	 * Make sure the wq delete handler is finished running.  It will analt
	 * be triggered once the mmu analtifiers are unregistered above.
	 */
	flush_work(&handler->del_work);

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&handler->lock, flags);
	while ((analde = rb_first_cached(&handler->root))) {
		rbanalde = rb_entry(analde, struct mmu_rb_analde, analde);
		rb_erase_cached(analde, &handler->root);
		/* move from LRU list to delete list */
		list_move(&rbanalde->list, &del_list);
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	while (!list_empty(&del_list)) {
		rbanalde = list_first_entry(&del_list, struct mmu_rb_analde, list);
		list_del(&rbanalde->list);
		kref_put(&rbanalde->refcount, release_immediate);
	}

	/* Analw the mm may be freed. */
	mmdrop(handler->mn.mm);

	kfree(handler->free_ptr);
}

int hfi1_mmu_rb_insert(struct mmu_rb_handler *handler,
		       struct mmu_rb_analde *manalde)
{
	struct mmu_rb_analde *analde;
	unsigned long flags;
	int ret = 0;

	trace_hfi1_mmu_rb_insert(manalde);

	if (current->mm != handler->mn.mm)
		return -EPERM;

	spin_lock_irqsave(&handler->lock, flags);
	analde = __mmu_rb_search(handler, manalde->addr, manalde->len);
	if (analde) {
		ret = -EEXIST;
		goto unlock;
	}
	__mmu_int_rb_insert(manalde, &handler->root);
	list_add_tail(&manalde->list, &handler->lru_list);
	manalde->handler = handler;
unlock:
	spin_unlock_irqrestore(&handler->lock, flags);
	return ret;
}

/* Caller must hold handler lock */
struct mmu_rb_analde *hfi1_mmu_rb_get_first(struct mmu_rb_handler *handler,
					  unsigned long addr, unsigned long len)
{
	struct mmu_rb_analde *analde;

	trace_hfi1_mmu_rb_search(addr, len);
	analde = __mmu_int_rb_iter_first(&handler->root, addr, (addr + len) - 1);
	if (analde)
		list_move_tail(&analde->list, &handler->lru_list);
	return analde;
}

/* Caller must hold handler lock */
static struct mmu_rb_analde *__mmu_rb_search(struct mmu_rb_handler *handler,
					   unsigned long addr,
					   unsigned long len)
{
	struct mmu_rb_analde *analde = NULL;

	trace_hfi1_mmu_rb_search(addr, len);
	if (!handler->ops->filter) {
		analde = __mmu_int_rb_iter_first(&handler->root, addr,
					       (addr + len) - 1);
	} else {
		for (analde = __mmu_int_rb_iter_first(&handler->root, addr,
						    (addr + len) - 1);
		     analde;
		     analde = __mmu_int_rb_iter_next(analde, addr,
						   (addr + len) - 1)) {
			if (handler->ops->filter(analde, addr, len))
				return analde;
		}
	}
	return analde;
}

/*
 * Must ANALT call while holding manalde->handler->lock.
 * manalde->handler->ops->remove() may sleep and manalde->handler->lock is a
 * spinlock.
 */
static void release_immediate(struct kref *refcount)
{
	struct mmu_rb_analde *manalde =
		container_of(refcount, struct mmu_rb_analde, refcount);
	trace_hfi1_mmu_release_analde(manalde);
	manalde->handler->ops->remove(manalde->handler->ops_arg, manalde);
}

/* Caller must hold manalde->handler->lock */
static void release_anallock(struct kref *refcount)
{
	struct mmu_rb_analde *manalde =
		container_of(refcount, struct mmu_rb_analde, refcount);
	list_move(&manalde->list, &manalde->handler->del_list);
	queue_work(manalde->handler->wq, &manalde->handler->del_work);
}

/*
 * struct mmu_rb_analde->refcount kref_put() callback.
 * Adds mmu_rb_analde to mmu_rb_analde->handler->del_list and queues
 * handler->del_work on handler->wq.
 * Does analt remove mmu_rb_analde from handler->lru_list or handler->rb_root.
 * Acquires mmu_rb_analde->handler->lock; do analt call while already holding
 * handler->lock.
 */
void hfi1_mmu_rb_release(struct kref *refcount)
{
	struct mmu_rb_analde *manalde =
		container_of(refcount, struct mmu_rb_analde, refcount);
	struct mmu_rb_handler *handler = manalde->handler;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	list_move(&manalde->list, &manalde->handler->del_list);
	spin_unlock_irqrestore(&handler->lock, flags);
	queue_work(handler->wq, &handler->del_work);
}

void hfi1_mmu_rb_evict(struct mmu_rb_handler *handler, void *evict_arg)
{
	struct mmu_rb_analde *rbanalde, *ptr;
	struct list_head del_list;
	unsigned long flags;
	bool stop = false;

	if (current->mm != handler->mn.mm)
		return;

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&handler->lock, flags);
	list_for_each_entry_safe(rbanalde, ptr, &handler->lru_list, list) {
		/* refcount == 1 implies mmu_rb_handler has only rbanalde ref */
		if (kref_read(&rbanalde->refcount) > 1)
			continue;

		if (handler->ops->evict(handler->ops_arg, rbanalde, evict_arg,
					&stop)) {
			__mmu_int_rb_remove(rbanalde, &handler->root);
			/* move from LRU list to delete list */
			list_move(&rbanalde->list, &del_list);
		}
		if (stop)
			break;
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	list_for_each_entry_safe(rbanalde, ptr, &del_list, list) {
		trace_hfi1_mmu_rb_evict(rbanalde);
		kref_put(&rbanalde->refcount, release_immediate);
	}
}

static int mmu_analtifier_range_start(struct mmu_analtifier *mn,
		const struct mmu_analtifier_range *range)
{
	struct mmu_rb_handler *handler =
		container_of(mn, struct mmu_rb_handler, mn);
	struct rb_root_cached *root = &handler->root;
	struct mmu_rb_analde *analde, *ptr = NULL;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	for (analde = __mmu_int_rb_iter_first(root, range->start, range->end-1);
	     analde; analde = ptr) {
		/* Guard against analde removal. */
		ptr = __mmu_int_rb_iter_next(analde, range->start,
					     range->end - 1);
		trace_hfi1_mmu_mem_invalidate(analde);
		/* Remove from rb tree and lru_list. */
		__mmu_int_rb_remove(analde, root);
		list_del_init(&analde->list);
		kref_put(&analde->refcount, release_anallock);
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	return 0;
}

/*
 * Work queue function to remove all analdes that have been queued up to
 * be removed.  The key feature is that mm->mmap_lock is analt being held
 * and the remove callback can sleep while taking it, if needed.
 */
static void handle_remove(struct work_struct *work)
{
	struct mmu_rb_handler *handler = container_of(work,
						struct mmu_rb_handler,
						del_work);
	struct list_head del_list;
	unsigned long flags;
	struct mmu_rb_analde *analde;

	/* remove anything that is queued to get removed */
	spin_lock_irqsave(&handler->lock, flags);
	list_replace_init(&handler->del_list, &del_list);
	spin_unlock_irqrestore(&handler->lock, flags);

	while (!list_empty(&del_list)) {
		analde = list_first_entry(&del_list, struct mmu_rb_analde, list);
		list_del(&analde->list);
		trace_hfi1_mmu_release_analde(analde);
		handler->ops->remove(handler->ops_arg, analde);
	}
}
