/*
 * Functions related to io context handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/slab.h>

#include "blk.h"

/*
 * For io context allocations
 */
static struct kmem_cache *iocontext_cachep;

/**
 * get_io_context - increment reference count to io_context
 * @ioc: io_context to get
 *
 * Increment reference count to @ioc.
 */
void get_io_context(struct io_context *ioc)
{
	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);
	atomic_long_inc(&ioc->refcount);
}
EXPORT_SYMBOL(get_io_context);

/*
 * Releasing ioc may nest into another put_io_context() leading to nested
 * fast path release.  As the ioc's can't be the same, this is okay but
 * makes lockdep whine.  Keep track of nesting and use it as subclass.
 */
#ifdef CONFIG_LOCKDEP
#define ioc_release_depth(q)		((q) ? (q)->ioc_release_depth : 0)
#define ioc_release_depth_inc(q)	(q)->ioc_release_depth++
#define ioc_release_depth_dec(q)	(q)->ioc_release_depth--
#else
#define ioc_release_depth(q)		0
#define ioc_release_depth_inc(q)	do { } while (0)
#define ioc_release_depth_dec(q)	do { } while (0)
#endif

/*
 * Slow path for ioc release in put_io_context().  Performs double-lock
 * dancing to unlink all cic's and then frees ioc.
 */
static void ioc_release_fn(struct work_struct *work)
{
	struct io_context *ioc = container_of(work, struct io_context,
					      release_work);
	struct request_queue *last_q = NULL;

	spin_lock_irq(&ioc->lock);

	while (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic = hlist_entry(ioc->cic_list.first,
							 struct cfq_io_context,
							 cic_list);
		struct request_queue *this_q = cic->q;

		if (this_q != last_q) {
			/*
			 * Need to switch to @this_q.  Once we release
			 * @ioc->lock, it can go away along with @cic.
			 * Hold on to it.
			 */
			__blk_get_queue(this_q);

			/*
			 * blk_put_queue() might sleep thanks to kobject
			 * idiocy.  Always release both locks, put and
			 * restart.
			 */
			if (last_q) {
				spin_unlock(last_q->queue_lock);
				spin_unlock_irq(&ioc->lock);
				blk_put_queue(last_q);
			} else {
				spin_unlock_irq(&ioc->lock);
			}

			last_q = this_q;
			spin_lock_irq(this_q->queue_lock);
			spin_lock(&ioc->lock);
			continue;
		}
		ioc_release_depth_inc(this_q);
		cic->exit(cic);
		cic->release(cic);
		ioc_release_depth_dec(this_q);
	}

	if (last_q) {
		spin_unlock(last_q->queue_lock);
		spin_unlock_irq(&ioc->lock);
		blk_put_queue(last_q);
	} else {
		spin_unlock_irq(&ioc->lock);
	}

	kmem_cache_free(iocontext_cachep, ioc);
}

/**
 * put_io_context - put a reference of io_context
 * @ioc: io_context to put
 * @locked_q: request_queue the caller is holding queue_lock of (hint)
 *
 * Decrement reference count of @ioc and release it if the count reaches
 * zero.  If the caller is holding queue_lock of a queue, it can indicate
 * that with @locked_q.  This is an optimization hint and the caller is
 * allowed to pass in %NULL even when it's holding a queue_lock.
 */
void put_io_context(struct io_context *ioc, struct request_queue *locked_q)
{
	struct request_queue *last_q = locked_q;
	unsigned long flags;

	if (ioc == NULL)
		return;

	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);
	if (locked_q)
		lockdep_assert_held(locked_q->queue_lock);

	if (!atomic_long_dec_and_test(&ioc->refcount))
		return;

	/*
	 * Destroy @ioc.  This is a bit messy because cic's are chained
	 * from both ioc and queue, and ioc->lock nests inside queue_lock.
	 * The inner ioc->lock should be held to walk our cic_list and then
	 * for each cic the outer matching queue_lock should be grabbed.
	 * ie. We need to do reverse-order double lock dancing.
	 *
	 * Another twist is that we are often called with one of the
	 * matching queue_locks held as indicated by @locked_q, which
	 * prevents performing double-lock dance for other queues.
	 *
	 * So, we do it in two stages.  The fast path uses the queue_lock
	 * the caller is holding and, if other queues need to be accessed,
	 * uses trylock to avoid introducing locking dependency.  This can
	 * handle most cases, especially if @ioc was performing IO on only
	 * single device.
	 *
	 * If trylock doesn't cut it, we defer to @ioc->release_work which
	 * can do all the double-locking dancing.
	 */
	spin_lock_irqsave_nested(&ioc->lock, flags,
				 ioc_release_depth(locked_q));

	while (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic = hlist_entry(ioc->cic_list.first,
							 struct cfq_io_context,
							 cic_list);
		struct request_queue *this_q = cic->q;

		if (this_q != last_q) {
			if (last_q && last_q != locked_q)
				spin_unlock(last_q->queue_lock);
			last_q = NULL;

			if (!spin_trylock(this_q->queue_lock))
				break;
			last_q = this_q;
			continue;
		}
		ioc_release_depth_inc(this_q);
		cic->exit(cic);
		cic->release(cic);
		ioc_release_depth_dec(this_q);
	}

	if (last_q && last_q != locked_q)
		spin_unlock(last_q->queue_lock);

	spin_unlock_irqrestore(&ioc->lock, flags);

	/* if no cic's left, we're done; otherwise, kick release_work */
	if (hlist_empty(&ioc->cic_list))
		kmem_cache_free(iocontext_cachep, ioc);
	else
		schedule_work(&ioc->release_work);
}
EXPORT_SYMBOL(put_io_context);

/* Called by the exiting task */
void exit_io_context(struct task_struct *task)
{
	struct io_context *ioc;

	/* PF_EXITING prevents new io_context from being attached to @task */
	WARN_ON_ONCE(!(current->flags & PF_EXITING));

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	task_unlock(task);

	atomic_dec(&ioc->nr_tasks);
	put_io_context(ioc, NULL);
}

static struct io_context *create_task_io_context(struct task_struct *task,
						 gfp_t gfp_flags, int node,
						 bool take_ref)
{
	struct io_context *ioc;

	ioc = kmem_cache_alloc_node(iocontext_cachep, gfp_flags | __GFP_ZERO,
				    node);
	if (unlikely(!ioc))
		return NULL;

	/* initialize */
	atomic_long_set(&ioc->refcount, 1);
	atomic_set(&ioc->nr_tasks, 1);
	spin_lock_init(&ioc->lock);
	INIT_RADIX_TREE(&ioc->radix_root, GFP_ATOMIC | __GFP_HIGH);
	INIT_HLIST_HEAD(&ioc->cic_list);
	INIT_WORK(&ioc->release_work, ioc_release_fn);

	/* try to install, somebody might already have beaten us to it */
	task_lock(task);

	if (!task->io_context && !(task->flags & PF_EXITING)) {
		task->io_context = ioc;
	} else {
		kmem_cache_free(iocontext_cachep, ioc);
		ioc = task->io_context;
	}

	if (ioc && take_ref)
		get_io_context(ioc);

	task_unlock(task);
	return ioc;
}

/**
 * current_io_context - get io_context of %current
 * @gfp_flags: allocation flags, used if allocation is necessary
 * @node: allocation node, used if allocation is necessary
 *
 * Return io_context of %current.  If it doesn't exist, it is created with
 * @gfp_flags and @node.  The returned io_context does NOT have its
 * reference count incremented.  Because io_context is exited only on task
 * exit, %current can be sure that the returned io_context is valid and
 * alive as long as it is executing.
 */
struct io_context *current_io_context(gfp_t gfp_flags, int node)
{
	might_sleep_if(gfp_flags & __GFP_WAIT);

	if (current->io_context)
		return current->io_context;

	return create_task_io_context(current, gfp_flags, node, false);
}
EXPORT_SYMBOL(current_io_context);

/**
 * get_task_io_context - get io_context of a task
 * @task: task of interest
 * @gfp_flags: allocation flags, used if allocation is necessary
 * @node: allocation node, used if allocation is necessary
 *
 * Return io_context of @task.  If it doesn't exist, it is created with
 * @gfp_flags and @node.  The returned io_context has its reference count
 * incremented.
 *
 * This function always goes through task_lock() and it's better to use
 * current_io_context() + get_io_context() for %current.
 */
struct io_context *get_task_io_context(struct task_struct *task,
				       gfp_t gfp_flags, int node)
{
	struct io_context *ioc;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	task_lock(task);
	ioc = task->io_context;
	if (likely(ioc)) {
		get_io_context(ioc);
		task_unlock(task);
		return ioc;
	}
	task_unlock(task);

	return create_task_io_context(task, gfp_flags, node, true);
}
EXPORT_SYMBOL(get_task_io_context);

void ioc_set_changed(struct io_context *ioc, int which)
{
	struct cfq_io_context *cic;
	struct hlist_node *n;

	hlist_for_each_entry(cic, n, &ioc->cic_list, cic_list)
		set_bit(which, &cic->changed);
}

/**
 * ioc_ioprio_changed - notify ioprio change
 * @ioc: io_context of interest
 * @ioprio: new ioprio
 *
 * @ioc's ioprio has changed to @ioprio.  Set %CIC_IOPRIO_CHANGED for all
 * cic's.  iosched is responsible for checking the bit and applying it on
 * request issue path.
 */
void ioc_ioprio_changed(struct io_context *ioc, int ioprio)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->lock, flags);
	ioc->ioprio = ioprio;
	ioc_set_changed(ioc, CIC_IOPRIO_CHANGED);
	spin_unlock_irqrestore(&ioc->lock, flags);
}

/**
 * ioc_cgroup_changed - notify cgroup change
 * @ioc: io_context of interest
 *
 * @ioc's cgroup has changed.  Set %CIC_CGROUP_CHANGED for all cic's.
 * iosched is responsible for checking the bit and applying it on request
 * issue path.
 */
void ioc_cgroup_changed(struct io_context *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->lock, flags);
	ioc_set_changed(ioc, CIC_CGROUP_CHANGED);
	spin_unlock_irqrestore(&ioc->lock, flags);
}

static int __init blk_ioc_init(void)
{
	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_ioc_init);
