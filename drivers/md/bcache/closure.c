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

void closure_queue(struct closure *cl)
{
	struct workqueue_struct *wq = cl->wq;
	if (wq) {
		INIT_WORK(&cl->work, cl->work.func);
		BUG_ON(!queue_work(wq, &cl->work));
	} else
		cl->fn(cl);
}
EXPORT_SYMBOL_GPL(closure_queue);

#define CL_FIELD(type, field)					\
	case TYPE_ ## type:					\
	return &container_of(cl, struct type, cl)->field

static struct closure_waitlist *closure_waitlist(struct closure *cl)
{
	switch (cl->type) {
		CL_FIELD(closure_with_waitlist, wait);
		CL_FIELD(closure_with_waitlist_and_timer, wait);
	default:
		return NULL;
	}
}

static struct timer_list *closure_timer(struct closure *cl)
{
	switch (cl->type) {
		CL_FIELD(closure_with_timer, timer);
		CL_FIELD(closure_with_waitlist_and_timer, timer);
	default:
		return NULL;
	}
}

static inline void closure_put_after_sub(struct closure *cl, int flags)
{
	int r = flags & CLOSURE_REMAINING_MASK;

	BUG_ON(flags & CLOSURE_GUARD_MASK);
	BUG_ON(!r && (flags & ~(CLOSURE_DESTRUCTOR|CLOSURE_BLOCKING)));

	/* Must deliver precisely one wakeup */
	if (r == 1 && (flags & CLOSURE_SLEEPING))
		wake_up_process(cl->task);

	if (!r) {
		if (cl->fn && !(flags & CLOSURE_DESTRUCTOR)) {
			/* CLOSURE_BLOCKING might be set - clear it */
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
EXPORT_SYMBOL_GPL(closure_sub);

void closure_put(struct closure *cl)
{
	closure_put_after_sub(cl, atomic_dec_return(&cl->remaining));
}
EXPORT_SYMBOL_GPL(closure_put);

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
EXPORT_SYMBOL_GPL(__closure_wake_up);

bool closure_wait(struct closure_waitlist *list, struct closure *cl)
{
	if (atomic_read(&cl->remaining) & CLOSURE_WAITING)
		return false;

	set_waiting(cl, _RET_IP_);
	atomic_add(CLOSURE_WAITING + 1, &cl->remaining);
	llist_add(&cl->list, &list->list);

	return true;
}
EXPORT_SYMBOL_GPL(closure_wait);

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
EXPORT_SYMBOL_GPL(closure_sync);

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

	closure_set_ret_ip(cl);

	smp_mb();
	cl->parent = parent;
	if (parent)
		closure_get(parent);

	closure_debug_create(cl);
	return true;
}
EXPORT_SYMBOL_GPL(closure_trylock);

void __closure_lock(struct closure *cl, struct closure *parent,
		    struct closure_waitlist *wait_list)
{
	struct closure wait;
	closure_init_stack(&wait);

	while (1) {
		if (closure_trylock(cl, parent))
			return;

		closure_wait_event_sync(wait_list, &wait,
					atomic_read(&cl->remaining) == -1);
	}
}
EXPORT_SYMBOL_GPL(__closure_lock);

static void closure_delay_timer_fn(unsigned long data)
{
	struct closure *cl = (struct closure *) data;
	closure_sub(cl, CLOSURE_TIMER + 1);
}

void do_closure_timer_init(struct closure *cl)
{
	struct timer_list *timer = closure_timer(cl);

	init_timer(timer);
	timer->data	= (unsigned long) cl;
	timer->function = closure_delay_timer_fn;
}
EXPORT_SYMBOL_GPL(do_closure_timer_init);

bool __closure_delay(struct closure *cl, unsigned long delay,
		     struct timer_list *timer)
{
	if (atomic_read(&cl->remaining) & CLOSURE_TIMER)
		return false;

	BUG_ON(timer_pending(timer));

	timer->expires	= jiffies + delay;

	atomic_add(CLOSURE_TIMER + 1, &cl->remaining);
	add_timer(timer);
	return true;
}
EXPORT_SYMBOL_GPL(__closure_delay);

void __closure_flush(struct closure *cl, struct timer_list *timer)
{
	if (del_timer(timer))
		closure_sub(cl, CLOSURE_TIMER + 1);
}
EXPORT_SYMBOL_GPL(__closure_flush);

void __closure_flush_sync(struct closure *cl, struct timer_list *timer)
{
	if (del_timer_sync(timer))
		closure_sub(cl, CLOSURE_TIMER + 1);
}
EXPORT_SYMBOL_GPL(__closure_flush_sync);

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
EXPORT_SYMBOL_GPL(closure_debug_create);

void closure_debug_destroy(struct closure *cl)
{
	unsigned long flags;

	BUG_ON(cl->magic != CLOSURE_MAGIC_ALIVE);
	cl->magic = CLOSURE_MAGIC_DEAD;

	spin_lock_irqsave(&closure_list_lock, flags);
	list_del(&cl->all);
	spin_unlock_irqrestore(&closure_list_lock, flags);
}
EXPORT_SYMBOL_GPL(closure_debug_destroy);

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

		seq_printf(f, "%s%s%s%s%s%s\n",
			   test_bit(WORK_STRUCT_PENDING,
				    work_data_bits(&cl->work)) ? "Q" : "",
			   r & CLOSURE_RUNNING	? "R" : "",
			   r & CLOSURE_BLOCKING	? "B" : "",
			   r & CLOSURE_STACK	? "S" : "",
			   r & CLOSURE_SLEEPING	? "Sl" : "",
			   r & CLOSURE_TIMER	? "T" : "");

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
