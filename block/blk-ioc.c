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

static void cfq_dtor(struct io_context *ioc)
{
	if (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic;

		cic = hlist_entry(ioc->cic_list.first, struct cfq_io_context,
								cic_list);
		cic->dtor(ioc);
	}
}

/**
 * put_io_context - put a reference of io_context
 * @ioc: io_context to put
 *
 * Decrement reference count of @ioc and release it if the count reaches
 * zero.
 */
void put_io_context(struct io_context *ioc)
{
	if (ioc == NULL)
		return;

	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);

	if (!atomic_long_dec_and_test(&ioc->refcount))
		return;

	rcu_read_lock();
	cfq_dtor(ioc);
	rcu_read_unlock();

	kmem_cache_free(iocontext_cachep, ioc);
}
EXPORT_SYMBOL(put_io_context);

static void cfq_exit(struct io_context *ioc)
{
	rcu_read_lock();

	if (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic;

		cic = hlist_entry(ioc->cic_list.first, struct cfq_io_context,
								cic_list);
		cic->exit(ioc);
	}
	rcu_read_unlock();
}

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

	if (atomic_dec_and_test(&ioc->nr_tasks))
		cfq_exit(ioc);

	put_io_context(ioc);
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
