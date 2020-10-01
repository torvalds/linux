// SPDX-License-Identifier: GPL-2.0
/*
 * Asynchronous refcounty things
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/sched/debug.h>

#include "closure.h"

static inline void closure_put_after_sub(struct closure *cl, int flags)
{
	int r = flags & CLOSURE_REMAINING_MASK;

	BUG_ON(flags & CLOSURE_GUARD_MASK);
	BUG_ON(!r && (flags & ~CLOSURE_DESTRUCTOR));

	if (!r) {
		if (cl->fn && !(flags & CLOSURE_DESTRUCTOR)) {
			atomic_set(&cl->remaining,
				   CLOSURE_REMAINING_INITIALIZER);
			closure_queue(cl);
		} else {
			struct closure *parent = cl->parent;
			closure_fn *destructor = cl->fn;

			closure_debug_destroy(cl);

			if (destructor)
				destructor(cl);

			if (parent)
				closure_put(parent);
		}
	}
}

/* For clearing flags with the same atomic op as a put */
void closure_sub(struct closure *cl, int v)
{
	closure_put_after_sub(cl, atomic_sub_return(v, &cl->remaining));
}

/*
 * closure_put - decrement a closure's refcount
 */
void closure_put(struct closure *cl)
{
	closure_put_after_sub(cl, atomic_dec_return(&cl->remaining));
}

/*
 * closure_wake_up - wake up all closures on a wait list, without memory barrier
 */
void __closure_wake_up(struct closure_waitlist *wait_list)
{
	struct llist_node *list;
	struct closure *cl, *t;
	struct llist_node *reverse = NULL;

	list = llist_del_all(&wait_list->list);

	/* We first reverse the list to preserve FIFO ordering and fairness */
	reverse = llist_reverse_order(list);

	/* Then do the wakeups */
	llist_for_each_entry_safe(cl, t, reverse, list) {
		closure_set_waiting(cl, 0);
		closure_sub(cl, CLOSURE_WAITING + 1);
	}
}

/**
 * closure_wait - add a closure to a waitlist
 * @waitlist: will own a ref on @cl, which will be released when
 * closure_wake_up() is called on @waitlist.
 * @cl: closure pointer.
 *
 */
bool closure_wait(struct closure_waitlist *waitlist, struct closure *cl)
{
	if (atomic_read(&cl->remaining) & CLOSURE_WAITING)
		return false;

	closure_set_waiting(cl, _RET_IP_);
	atomic_add(CLOSURE_WAITING + 1, &cl->remaining);
	llist_add(&cl->list, &waitlist->list);

	return true;
}

struct closure_syncer {
	struct task_struct	*task;
	int			done;
};

static void closure_sync_fn(struct closure *cl)
{
	struct closure_syncer *s = cl->s;
	struct task_struct *p;

	rcu_read_lock();
	p = READ_ONCE(s->task);
	s->done = 1;
	wake_up_process(p);
	rcu_read_unlock();
}

void __sched __closure_sync(struct closure *cl)
{
	struct closure_syncer s = { .task = current };

	cl->s = &s;
	continue_at(cl, closure_sync_fn, NULL);

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (s.done)
			break;
		schedule();
	}

	__set_current_state(TASK_RUNNING);
}

#ifdef CONFIG_BCACHE_CLOSURES_DEBUG

static LIST_HEAD(closure_list);
static DEFINE_SPINLOCK(closure_list_lock);

void closure_debug_create(struct closure *cl)
{
	unsigned long flags;

	BUG_ON(cl->magic == CLOSURE_MAGIC_ALIVE);
	cl->magic = CLOSURE_MAGIC_ALIVE;

	spin_lock_irqsave(&closure_list_lock, flags);
	list_add(&cl->all, &closure_list);
	spin_unlock_irqrestore(&closure_list_lock, flags);
}

void closure_debug_destroy(struct closure *cl)
{
	unsigned long flags;

	BUG_ON(cl->magic != CLOSURE_MAGIC_ALIVE);
	cl->magic = CLOSURE_MAGIC_DEAD;

	spin_lock_irqsave(&closure_list_lock, flags);
	list_del(&cl->all);
	spin_unlock_irqrestore(&closure_list_lock, flags);
}

static struct dentry *closure_debug;

static int debug_show(struct seq_file *f, void *data)
{
	struct closure *cl;

	spin_lock_irq(&closure_list_lock);

	list_for_each_entry(cl, &closure_list, all) {
		int r = atomic_read(&cl->remaining);

		seq_printf(f, "%p: %pS -> %pS p %p r %i ",
			   cl, (void *) cl->ip, cl->fn, cl->parent,
			   r & CLOSURE_REMAINING_MASK);

		seq_printf(f, "%s%s\n",
			   test_bit(WORK_STRUCT_PENDING_BIT,
				    work_data_bits(&cl->work)) ? "Q" : "",
			   r & CLOSURE_RUNNING	? "R" : "");

		if (r & CLOSURE_WAITING)
			seq_printf(f, " W %pS\n",
				   (void *) cl->waiting_on);

		seq_printf(f, "\n");
	}

	spin_unlock_irq(&closure_list_lock);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(debug);

void  __init closure_debug_init(void)
{
	if (!IS_ERR_OR_NULL(bcache_debug))
		/*
		 * it is unnecessary to check return value of
		 * debugfs_create_file(), we should not care
		 * about this.
		 */
		closure_debug = debugfs_create_file(
			"closures", 0400, bcache_debug, NULL, &debug_fops);
}
#endif

MODULE_AUTHOR("Kent Overstreet <koverstreet@google.com>");
MODULE_LICENSE("GPL");
