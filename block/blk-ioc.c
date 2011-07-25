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

static void cfq_dtor(struct io_context *ioc)
{
	if (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic;

		cic = hlist_entry(ioc->cic_list.first, struct cfq_io_context,
								cic_list);
		cic->dtor(ioc);
	}
}

/*
 * IO Context helper functions. put_io_context() returns 1 if there are no
 * more users of this io context, 0 otherwise.
 */
int put_io_context(struct io_context *ioc)
{
	if (ioc == NULL)
		return 1;

	BUG_ON(atomic_long_read(&ioc->refcount) == 0);

	if (atomic_long_dec_and_test(&ioc->refcount)) {
		rcu_read_lock();
		cfq_dtor(ioc);
		rcu_read_unlock();

		kmem_cache_free(iocontext_cachep, ioc);
		return 1;
	}
	return 0;
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

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	task_unlock(task);

	if (atomic_dec_and_test(&ioc->nr_tasks))
		cfq_exit(ioc);

	put_io_context(ioc);
}

struct io_context *alloc_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ioc;

	ioc = kmem_cache_alloc_node(iocontext_cachep, gfp_flags, node);
	if (ioc) {
		atomic_long_set(&ioc->refcount, 1);
		atomic_set(&ioc->nr_tasks, 1);
		spin_lock_init(&ioc->lock);
		ioc->ioprio_changed = 0;
		ioc->ioprio = 0;
		ioc->last_waited = 0; /* doesn't matter... */
		ioc->nr_batch_requests = 0; /* because this is 0 */
		INIT_RADIX_TREE(&ioc->radix_root, GFP_ATOMIC | __GFP_HIGH);
		INIT_HLIST_HEAD(&ioc->cic_list);
		ioc->ioc_data = NULL;
#if defined(CONFIG_BLK_CGROUP) || defined(CONFIG_BLK_CGROUP_MODULE)
		ioc->cgroup_changed = 0;
#endif
	}

	return ioc;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * Otherwise, return its existing IO context.
 *
 * This returned IO context doesn't have a specifically elevated refcount,
 * but since the current task itself holds a reference, the context can be
 * used in general code, so long as it stays within `current` context.
 */
struct io_context *current_io_context(gfp_t gfp_flags, int node)
{
	struct task_struct *tsk = current;
	struct io_context *ret;

	ret = tsk->io_context;
	if (likely(ret))
		return ret;

	ret = alloc_io_context(gfp_flags, node);
	if (ret) {
		/* make sure set_task_ioprio() sees the settings above */
		smp_wmb();
		tsk->io_context = ret;
	}

	return ret;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * If it does have a context, take a ref on it.
 *
 * This is always called in the context of the task which submitted the I/O.
 */
struct io_context *get_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ioc = NULL;

	/*
	 * Check for unlikely race with exiting task. ioc ref count is
	 * zero when ioc is being detached.
	 */
	do {
		ioc = current_io_context(gfp_flags, node);
		if (unlikely(!ioc))
			break;
	} while (!atomic_long_inc_not_zero(&ioc->refcount));

	return ioc;
}
EXPORT_SYMBOL(get_io_context);

static int __init blk_ioc_init(void)
{
	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_ioc_init);
