// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to io context handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/sched/task.h>

#include "blk.h"
#include "blk-mq-sched.h"

/*
 * For io context allocations
 */
static struct kmem_cache *iocontext_cachep;

#ifdef CONFIG_BLK_ICQ
/**
 * get_io_context - increment reference count to io_context
 * @ioc: io_context to get
 *
 * Increment reference count to @ioc.
 */
static void get_io_context(struct io_context *ioc)
{
	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);
	atomic_long_inc(&ioc->refcount);
}

/*
 * Exit an icq. Called with ioc locked for blk-mq, and with both ioc
 * and queue locked for legacy.
 */
static void ioc_exit_icq(struct io_cq *icq)
{
	struct elevator_type *et = icq->q->elevator->type;

	if (icq->flags & ICQ_EXITED)
		return;

	if (et->ops.exit_icq)
		et->ops.exit_icq(icq);

	icq->flags |= ICQ_EXITED;
}

static void ioc_exit_icqs(struct io_context *ioc)
{
	struct io_cq *icq;

	spin_lock_irq(&ioc->lock);
	hlist_for_each_entry(icq, &ioc->icq_list, ioc_node)
		ioc_exit_icq(icq);
	spin_unlock_irq(&ioc->lock);
}

/*
 * Release an icq. Called with ioc locked for blk-mq, and with both ioc
 * and queue locked for legacy.
 */
static void ioc_destroy_icq(struct io_cq *icq)
{
	struct io_context *ioc = icq->ioc;
	struct request_queue *q = icq->q;
	struct elevator_type *et = q->elevator->type;

	lockdep_assert_held(&ioc->lock);
	lockdep_assert_held(&q->queue_lock);

	if (icq->flags & ICQ_DESTROYED)
		return;

	radix_tree_delete(&ioc->icq_tree, icq->q->id);
	hlist_del_init(&icq->ioc_node);
	list_del_init(&icq->q_node);

	/*
	 * Both setting lookup hint to and clearing it from @icq are done
	 * under queue_lock.  If it's not pointing to @icq now, it never
	 * will.  Hint assignment itself can race safely.
	 */
	if (rcu_access_pointer(ioc->icq_hint) == icq)
		rcu_assign_pointer(ioc->icq_hint, NULL);

	ioc_exit_icq(icq);

	/*
	 * @icq->q might have gone away by the time RCU callback runs
	 * making it impossible to determine icq_cache.  Record it in @icq.
	 */
	icq->__rcu_icq_cache = et->icq_cache;
	icq->flags |= ICQ_DESTROYED;
	kfree_rcu(icq, __rcu_head);
}

/*
 * Slow path for ioc release in put_io_context().  Performs double-lock
 * dancing to unlink all icq's and then frees ioc.
 */
static void ioc_release_fn(struct work_struct *work)
{
	struct io_context *ioc = container_of(work, struct io_context,
					      release_work);
	spin_lock_irq(&ioc->lock);

	while (!hlist_empty(&ioc->icq_list)) {
		struct io_cq *icq = hlist_entry(ioc->icq_list.first,
						struct io_cq, ioc_node);
		struct request_queue *q = icq->q;

		if (spin_trylock(&q->queue_lock)) {
			ioc_destroy_icq(icq);
			spin_unlock(&q->queue_lock);
		} else {
			/* Make sure q and icq cannot be freed. */
			rcu_read_lock();

			/* Re-acquire the locks in the correct order. */
			spin_unlock(&ioc->lock);
			spin_lock(&q->queue_lock);
			spin_lock(&ioc->lock);

			ioc_destroy_icq(icq);

			spin_unlock(&q->queue_lock);
			rcu_read_unlock();
		}
	}

	spin_unlock_irq(&ioc->lock);

	kmem_cache_free(iocontext_cachep, ioc);
}

/*
 * Releasing icqs requires reverse order double locking and we may already be
 * holding a queue_lock.  Do it asynchronously from a workqueue.
 */
static bool ioc_delay_free(struct io_context *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->lock, flags);
	if (!hlist_empty(&ioc->icq_list)) {
		queue_work(system_power_efficient_wq, &ioc->release_work);
		spin_unlock_irqrestore(&ioc->lock, flags);
		return true;
	}
	spin_unlock_irqrestore(&ioc->lock, flags);
	return false;
}

/**
 * ioc_clear_queue - break any ioc association with the specified queue
 * @q: request_queue being cleared
 *
 * Walk @q->icq_list and exit all io_cq's.
 */
void ioc_clear_queue(struct request_queue *q)
{
	spin_lock_irq(&q->queue_lock);
	while (!list_empty(&q->icq_list)) {
		struct io_cq *icq =
			list_first_entry(&q->icq_list, struct io_cq, q_node);

		/*
		 * Other context won't hold ioc lock to wait for queue_lock, see
		 * details in ioc_release_fn().
		 */
		spin_lock(&icq->ioc->lock);
		ioc_destroy_icq(icq);
		spin_unlock(&icq->ioc->lock);
	}
	spin_unlock_irq(&q->queue_lock);
}
#else /* CONFIG_BLK_ICQ */
static inline void ioc_exit_icqs(struct io_context *ioc)
{
}
static inline bool ioc_delay_free(struct io_context *ioc)
{
	return false;
}
#endif /* CONFIG_BLK_ICQ */

/**
 * put_io_context - put a reference of io_context
 * @ioc: io_context to put
 *
 * Decrement reference count of @ioc and release it if the count reaches
 * zero.
 */
void put_io_context(struct io_context *ioc)
{
	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);
	if (atomic_long_dec_and_test(&ioc->refcount) && !ioc_delay_free(ioc))
		kmem_cache_free(iocontext_cachep, ioc);
}
EXPORT_SYMBOL_GPL(put_io_context);

/* Called by the exiting task */
void exit_io_context(struct task_struct *task)
{
	struct io_context *ioc;

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	task_unlock(task);

	if (atomic_dec_and_test(&ioc->active_ref)) {
		ioc_exit_icqs(ioc);
		put_io_context(ioc);
	}
}

static struct io_context *alloc_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ioc;

	ioc = kmem_cache_alloc_node(iocontext_cachep, gfp_flags | __GFP_ZERO,
				    node);
	if (unlikely(!ioc))
		return NULL;

	atomic_long_set(&ioc->refcount, 1);
	atomic_set(&ioc->active_ref, 1);
#ifdef CONFIG_BLK_ICQ
	spin_lock_init(&ioc->lock);
	INIT_RADIX_TREE(&ioc->icq_tree, GFP_ATOMIC);
	INIT_HLIST_HEAD(&ioc->icq_list);
	INIT_WORK(&ioc->release_work, ioc_release_fn);
#endif
	ioc->ioprio = IOPRIO_DEFAULT;

	return ioc;
}

int set_task_ioprio(struct task_struct *task, int ioprio)
{
	int err;
	const struct cred *cred = current_cred(), *tcred;

	rcu_read_lock();
	tcred = __task_cred(task);
	if (!uid_eq(tcred->uid, cred->euid) &&
	    !uid_eq(tcred->uid, cred->uid) && !capable(CAP_SYS_NICE)) {
		rcu_read_unlock();
		return -EPERM;
	}
	rcu_read_unlock();

	err = security_task_setioprio(task, ioprio);
	if (err)
		return err;

	task_lock(task);
	if (unlikely(!task->io_context)) {
		struct io_context *ioc;

		task_unlock(task);

		ioc = alloc_io_context(GFP_ATOMIC, NUMA_NO_NODE);
		if (!ioc)
			return -ENOMEM;

		task_lock(task);
		if (task->flags & PF_EXITING) {
			kmem_cache_free(iocontext_cachep, ioc);
			goto out;
		}
		if (task->io_context)
			kmem_cache_free(iocontext_cachep, ioc);
		else
			task->io_context = ioc;
	}
	task->io_context->ioprio = ioprio;
out:
	task_unlock(task);
	return 0;
}
EXPORT_SYMBOL_GPL(set_task_ioprio);

int __copy_io(u64 clone_flags, struct task_struct *tsk)
{
	struct io_context *ioc = current->io_context;

	/*
	 * Share io context with parent, if CLONE_IO is set
	 */
	if (clone_flags & CLONE_IO) {
		atomic_inc(&ioc->active_ref);
		tsk->io_context = ioc;
	} else if (ioprio_valid(ioc->ioprio)) {
		tsk->io_context = alloc_io_context(GFP_KERNEL, NUMA_NO_NODE);
		if (!tsk->io_context)
			return -ENOMEM;
		tsk->io_context->ioprio = ioc->ioprio;
	}

	return 0;
}

#ifdef CONFIG_BLK_ICQ
/**
 * ioc_lookup_icq - lookup io_cq from ioc in io issue path
 * @q: the associated request_queue
 *
 * Look up io_cq associated with @ioc - @q pair from @ioc.  Must be called
 * from io issue path, either return NULL if current issue io to @q for the
 * first time, or return a valid icq.
 */
struct io_cq *ioc_lookup_icq(struct request_queue *q)
{
	struct io_context *ioc = current->io_context;
	struct io_cq *icq;

	/*
	 * icq's are indexed from @ioc using radix tree and hint pointer,
	 * both of which are protected with RCU, io issue path ensures that
	 * both request_queue and current task are valid, the found icq
	 * is guaranteed to be valid until the io is done.
	 */
	rcu_read_lock();
	icq = rcu_dereference(ioc->icq_hint);
	if (icq && icq->q == q)
		goto out;

	icq = radix_tree_lookup(&ioc->icq_tree, q->id);
	if (icq && icq->q == q)
		rcu_assign_pointer(ioc->icq_hint, icq);	/* allowed to race */
	else
		icq = NULL;
out:
	rcu_read_unlock();
	return icq;
}
EXPORT_SYMBOL(ioc_lookup_icq);

/**
 * ioc_create_icq - create and link io_cq
 * @q: request_queue of interest
 *
 * Make sure io_cq linking @ioc and @q exists.  If icq doesn't exist, they
 * will be created using @gfp_mask.
 *
 * The caller is responsible for ensuring @ioc won't go away and @q is
 * alive and will stay alive until this function returns.
 */
static struct io_cq *ioc_create_icq(struct request_queue *q)
{
	struct io_context *ioc = current->io_context;
	struct elevator_type *et = q->elevator->type;
	struct io_cq *icq;

	/* allocate stuff */
	icq = kmem_cache_alloc_node(et->icq_cache, GFP_ATOMIC | __GFP_ZERO,
				    q->node);
	if (!icq)
		return NULL;

	if (radix_tree_maybe_preload(GFP_ATOMIC) < 0) {
		kmem_cache_free(et->icq_cache, icq);
		return NULL;
	}

	icq->ioc = ioc;
	icq->q = q;
	INIT_LIST_HEAD(&icq->q_node);
	INIT_HLIST_NODE(&icq->ioc_node);

	/* lock both q and ioc and try to link @icq */
	spin_lock_irq(&q->queue_lock);
	spin_lock(&ioc->lock);

	if (likely(!radix_tree_insert(&ioc->icq_tree, q->id, icq))) {
		hlist_add_head(&icq->ioc_node, &ioc->icq_list);
		list_add(&icq->q_node, &q->icq_list);
		if (et->ops.init_icq)
			et->ops.init_icq(icq);
	} else {
		kmem_cache_free(et->icq_cache, icq);
		icq = ioc_lookup_icq(q);
		if (!icq)
			printk(KERN_ERR "cfq: icq link failed!\n");
	}

	spin_unlock(&ioc->lock);
	spin_unlock_irq(&q->queue_lock);
	radix_tree_preload_end();
	return icq;
}

struct io_cq *ioc_find_get_icq(struct request_queue *q)
{
	struct io_context *ioc = current->io_context;
	struct io_cq *icq = NULL;

	if (unlikely(!ioc)) {
		ioc = alloc_io_context(GFP_ATOMIC, q->node);
		if (!ioc)
			return NULL;

		task_lock(current);
		if (current->io_context) {
			kmem_cache_free(iocontext_cachep, ioc);
			ioc = current->io_context;
		} else {
			current->io_context = ioc;
		}

		get_io_context(ioc);
		task_unlock(current);
	} else {
		get_io_context(ioc);
		icq = ioc_lookup_icq(q);
	}

	if (!icq) {
		icq = ioc_create_icq(q);
		if (!icq) {
			put_io_context(ioc);
			return NULL;
		}
	}
	return icq;
}
EXPORT_SYMBOL_GPL(ioc_find_get_icq);
#endif /* CONFIG_BLK_ICQ */

static int __init blk_ioc_init(void)
{
	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_ioc_init);
