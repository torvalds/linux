/*
 * Asynchronous refcounty things
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#include "closure.h"

#define CL_FIELD(type, field)					\
	case TYPE_ ## type:					\
	return &container_of(cl, struct type, cl)->field

static struct closure_waitlist *closure_waitlist(struct closure *cl)
{
	switch (cl->type) {
		CL_FIELD(closure_with_waitlist, wait);
	default:
		return NULL;
	}
}

static inline void closure_put_after_sub(struct closure *cl, int flags)
{
	int r = flags & CLOSURE_REMAINING_MASK;

	BUG_ON(flags & CLOSURE_GUARD_MASK);
	BUG_ON(!r && (flags & ~CLOSURE_DESTRUCTOR));

	/* Must deliver precisely one wakeup */
	if (r == 1 && (flags & CLOSURE_SLEEPING))
		wake_up_process(cl->task);

	if (!r) {
		if (cl->fn && !(flags & CLOSURE_DESTRUCTOR)) {
			atomic_set(&cl->remaining,
				   CLOSURE_REMAINING_INITIALIZER);
			closure_queue(cl);
		} else {
			struct closure *parent = cl->parent;
			struct closure_waitlist *wait = closure_waitlist(cl);
			closure_fn *destructor = cl->fn;

			closure_debug_destroy(cl);

			smp_mb();
			atomic_set(&cl->remaining, -1);

			if (wait)
				closure_wake_up(wait);

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
EXPORT_SYMBOL(closure_sub);

void closure_put(struct closure *cl)
{
	closure_put_after_sub(cl, atomic_dec_return(&cl->remaining));
}
EXPORT_SYMBOL(closure_put);

static void set_waiting(struct closure *cl, unsigned long f)
{
#ifdef CONFIG_BCACHE_CLOSURES_DEBUG
	cl->waiting_on = f;
#endif
}

void __closure_wake_up(struct closure_waitlist *wait_list)
{
	struct llist_node *list;
	struct closure *cl;
	struct llist_node *reverse = NULL;

	list = llist_del_all(&wait_list->list);

	/* We first reverse the list to preserve FIFO ordering and fairness */

	while (list) {
		struct llist_node *t = list;
		list = llist_next(list);

		t->next = reverse;
		reverse = t;
	}

	/* Then do the wakeups */

	while (reverse) {
		cl = container_of(reverse, struct closure, list);
		reverse = llist_next(reverse);

		set_waiting(cl, 0);
		closure_sub(cl, CLOSURE_WAITING + 1);
	}
}
EXPORT_SYMBOL(__closure_wake_up);

bool closure_wait(struct closure_waitlist *list, struct closure *cl)
{
	if (atomic_read(&cl->remaining) & CLOSURE_WAITING)
		return false;

	set_waiting(cl, _RET_IP_);
	atomic_add(CLOSURE_WAITING + 1, &cl->remaining);
	llist_add(&cl->list, &list->list);

	return true;
}
EXPORT_SYMBOL(closure_wait);

/**
 * closure_sync() - sleep until a closure a closure has nothing left to wait on
 *
 * Sleeps until the refcount hits 1 - the thread that's running the closure owns
 * the last refcount.
 */
void closure_sync(struct closure *cl)
{
	while (1) {
		__closure_start_sleep(cl);
		closure_set_ret_ip(cl);

		if ((atomic_read(&cl->remaining) &
		     CLOSURE_REMAINING_MASK) == 1)
			break;

		schedule();
	}

	__closure_end_sleep(cl);
}
EXPORT_SYMBOL(closure_sync);

/**
 * closure_trylock() - try to acquire the closure, without waiting
 * @cl:		closure to lock
 *
 * Returns true if the closure was succesfully locked.
 */
bool closure_trylock(struct closure *cl, struct closure *parent)
{
	if (atomic_cmpxchg(&cl->remaining, -1,
			   CLOSURE_REMAINING_INITIALIZER) != -1)
		return false;

	smp_mb();

	cl->parent = parent;
	if (parent)
		closure_get(parent);

	closure_set_ret_ip(cl);
	closure_debug_create(cl);
	return true;
}
EXPORT_SYMBOL(closure_trylock);

void __closure_lock(struct closure *cl, struct closure *parent,
		    struct closure_waitlist *wait_list)
{
	struct closure wait;
	closure_init_stack(&wait);

	while (1) {
		if (closure_trylock(cl, parent))
			return;

		closure_wait_event(wait_list, &wait,
				   atomic_read(&cl->remaining) == -1);
	}
}
EXPORT_SYMBOL(__closure_lock);

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
EXPORT_SYMBOL(closure_debug_create);

void closure_debug_destroy(struct closure *cl)
{
	unsigned long flags;

	BUG_ON(cl->magic != CLOSURE_MAGIC_ALIVE);
	cl->magic = CLOSURE_MAGIC_DEAD;

	spin_lock_irqsave(&closure_list_lock, flags);
	list_del(&cl->all);
	spin_unlock_irqrestore(&closure_list_lock, flags);
}
EXPORT_SYMBOL(closure_debug_destroy);

static struct dentry *debug;

#define work_data_bits(work) ((unsigned long *)(&(work)->data))

static int debug_seq_show(struct seq_file *f, void *data)
{
	struct closure *cl;
	spin_lock_irq(&closure_list_lock);

	list_for_each_entry(cl, &closure_list, all) {
		int r = atomic_read(&cl->remaining);

		seq_printf(f, "%p: %pF -> %pf p %p r %i ",
			   cl, (void *) cl->ip, cl->fn, cl->parent,
			   r & CLOSURE_REMAINING_MASK);

		seq_printf(f, "%s%s%s%s\n",
			   test_bit(WORK_STRUCT_PENDING,
				    work_data_bits(&cl->work)) ? "Q" : "",
			   r & CLOSURE_RUNNING	? "R" : "",
			   r & CLOSURE_STACK	? "S" : "",
			   r & CLOSURE_SLEEPING	? "Sl" : "");

		if (r & CLOSURE_WAITING)
			seq_printf(f, " W %pF\n",
				   (void *) cl->waiting_on);

		seq_printf(f, "\n");
	}

	spin_unlock_irq(&closure_list_lock);
	return 0;
}

static int debug_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_seq_show, NULL);
}

static const struct file_operations debug_ops = {
	.owner		= THIS_MODULE,
	.open		= debug_seq_open,
	.read		= seq_read,
	.release	= single_release
};

void __init closure_debug_init(void)
{
	debug = debugfs_create_file("closures", 0400, NULL, NULL, &debug_ops);
}

#endif

MODULE_AUTHOR("Kent Overstreet <koverstreet@google.com>");
MODULE_LICENSE("GPL");
