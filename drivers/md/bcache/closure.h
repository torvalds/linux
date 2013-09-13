#ifndef _LINUX_CLOSURE_H
#define _LINUX_CLOSURE_H

#include <linux/llist.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

/*
 * Closure is perhaps the most overused and abused term in computer science, but
 * since I've been unable to come up with anything better you're stuck with it
 * again.
 *
 * What are closures?
 *
 * They embed a refcount. The basic idea is they count "things that are in
 * progress" - in flight bios, some other thread that's doing something else -
 * anything you might want to wait on.
 *
 * The refcount may be manipulated with closure_get() and closure_put().
 * closure_put() is where many of the interesting things happen, when it causes
 * the refcount to go to 0.
 *
 * Closures can be used to wait on things both synchronously and asynchronously,
 * and synchronous and asynchronous use can be mixed without restriction. To
 * wait synchronously, use closure_sync() - you will sleep until your closure's
 * refcount hits 1.
 *
 * To wait asynchronously, use
 *   continue_at(cl, next_function, workqueue);
 *
 * passing it, as you might expect, the function to run when nothing is pending
 * and the workqueue to run that function out of.
 *
 * continue_at() also, critically, is a macro that returns the calling function.
 * There's good reason for this.
 *
 * To use safely closures asynchronously, they must always have a refcount while
 * they are running owned by the thread that is running them. Otherwise, suppose
 * you submit some bios and wish to have a function run when they all complete:
 *
 * foo_endio(struct bio *bio, int error)
 * {
 *	closure_put(cl);
 * }
 *
 * closure_init(cl);
 *
 * do_stuff();
 * closure_get(cl);
 * bio1->bi_endio = foo_endio;
 * bio_submit(bio1);
 *
 * do_more_stuff();
 * closure_get(cl);
 * bio2->bi_endio = foo_endio;
 * bio_submit(bio2);
 *
 * continue_at(cl, complete_some_read, system_wq);
 *
 * If closure's refcount started at 0, complete_some_read() could run before the
 * second bio was submitted - which is almost always not what you want! More
 * importantly, it wouldn't be possible to say whether the original thread or
 * complete_some_read()'s thread owned the closure - and whatever state it was
 * associated with!
 *
 * So, closure_init() initializes a closure's refcount to 1 - and when a
 * closure_fn is run, the refcount will be reset to 1 first.
 *
 * Then, the rule is - if you got the refcount with closure_get(), release it
 * with closure_put() (i.e, in a bio->bi_endio function). If you have a refcount
 * on a closure because you called closure_init() or you were run out of a
 * closure - _always_ use continue_at(). Doing so consistently will help
 * eliminate an entire class of particularly pernicious races.
 *
 * For a closure to wait on an arbitrary event, we need to introduce waitlists:
 *
 * struct closure_waitlist list;
 * closure_wait_event(list, cl, condition);
 * closure_wake_up(wait_list);
 *
 * These work analagously to wait_event() and wake_up() - except that instead of
 * operating on the current thread (for wait_event()) and lists of threads, they
 * operate on an explicit closure and lists of closures.
 *
 * Because it's a closure we can now wait either synchronously or
 * asynchronously. closure_wait_event() returns the current value of the
 * condition, and if it returned false continue_at() or closure_sync() can be
 * used to wait for it to become true.
 *
 * It's useful for waiting on things when you can't sleep in the context in
 * which you must check the condition (perhaps a spinlock held, or you might be
 * beneath generic_make_request() - in which case you can't sleep on IO).
 *
 * closure_wait_event() will wait either synchronously or asynchronously,
 * depending on whether the closure is in blocking mode or not. You can pick a
 * mode explicitly with closure_wait_event_sync() and
 * closure_wait_event_async(), which do just what you might expect.
 *
 * Lastly, you might have a wait list dedicated to a specific event, and have no
 * need for specifying the condition - you just want to wait until someone runs
 * closure_wake_up() on the appropriate wait list. In that case, just use
 * closure_wait(). It will return either true or false, depending on whether the
 * closure was already on a wait list or not - a closure can only be on one wait
 * list at a time.
 *
 * Parents:
 *
 * closure_init() takes two arguments - it takes the closure to initialize, and
 * a (possibly null) parent.
 *
 * If parent is non null, the new closure will have a refcount for its lifetime;
 * a closure is considered to be "finished" when its refcount hits 0 and the
 * function to run is null. Hence
 *
 * continue_at(cl, NULL, NULL);
 *
 * returns up the (spaghetti) stack of closures, precisely like normal return
 * returns up the C stack. continue_at() with non null fn is better thought of
 * as doing a tail call.
 *
 * All this implies that a closure should typically be embedded in a particular
 * struct (which its refcount will normally control the lifetime of), and that
 * struct can very much be thought of as a stack frame.
 *
 * Locking:
 *
 * Closures are based on work items but they can be thought of as more like
 * threads - in that like threads and unlike work items they have a well
 * defined lifetime; they are created (with closure_init()) and eventually
 * complete after a continue_at(cl, NULL, NULL).
 *
 * Suppose you've got some larger structure with a closure embedded in it that's
 * used for periodically doing garbage collection. You only want one garbage
 * collection happening at a time, so the natural thing to do is protect it with
 * a lock. However, it's difficult to use a lock protecting a closure correctly
 * because the unlock should come after the last continue_to() (additionally, if
 * you're using the closure asynchronously a mutex won't work since a mutex has
 * to be unlocked by the same process that locked it).
 *
 * So to make it less error prone and more efficient, we also have the ability
 * to use closures as locks:
 *
 * closure_init_unlocked();
 * closure_trylock();
 *
 * That's all we need for trylock() - the last closure_put() implicitly unlocks
 * it for you.  But for closure_lock(), we also need a wait list:
 *
 * struct closure_with_waitlist frobnicator_cl;
 *
 * closure_init_unlocked(&frobnicator_cl);
 * closure_lock(&frobnicator_cl);
 *
 * A closure_with_waitlist embeds a closure and a wait list - much like struct
 * delayed_work embeds a work item and a timer_list. The important thing is, use
 * it exactly like you would a regular closure and closure_put() will magically
 * handle everything for you.
 *
 * We've got closures that embed timers, too. They're called, appropriately
 * enough:
 * struct closure_with_timer;
 *
 * This gives you access to closure_delay(). It takes a refcount for a specified
 * number of jiffies - you could then call closure_sync() (for a slightly
 * convoluted version of msleep()) or continue_at() - which gives you the same
 * effect as using a delayed work item, except you can reuse the work_struct
 * already embedded in struct closure.
 *
 * Lastly, there's struct closure_with_waitlist_and_timer. It does what you
 * probably expect, if you happen to need the features of both. (You don't
 * really want to know how all this is implemented, but if I've done my job
 * right you shouldn't have to care).
 */

struct closure;
typedef void (closure_fn) (struct closure *);

struct closure_waitlist {
	struct llist_head	list;
};

enum closure_type {
	TYPE_closure				= 0,
	TYPE_closure_with_waitlist		= 1,
	TYPE_closure_with_timer			= 2,
	TYPE_closure_with_waitlist_and_timer	= 3,
	MAX_CLOSURE_TYPE			= 3,
};

enum closure_state {
	/*
	 * CLOSURE_BLOCKING: Causes closure_wait_event() to block, instead of
	 * waiting asynchronously
	 *
	 * CLOSURE_WAITING: Set iff the closure is on a waitlist. Must be set by
	 * the thread that owns the closure, and cleared by the thread that's
	 * waking up the closure.
	 *
	 * CLOSURE_SLEEPING: Must be set before a thread uses a closure to sleep
	 * - indicates that cl->task is valid and closure_put() may wake it up.
	 * Only set or cleared by the thread that owns the closure.
	 *
	 * CLOSURE_TIMER: Analagous to CLOSURE_WAITING, indicates that a closure
	 * has an outstanding timer. Must be set by the thread that owns the
	 * closure, and cleared by the timer function when the timer goes off.
	 *
	 * The rest are for debugging and don't affect behaviour:
	 *
	 * CLOSURE_RUNNING: Set when a closure is running (i.e. by
	 * closure_init() and when closure_put() runs then next function), and
	 * must be cleared before remaining hits 0. Primarily to help guard
	 * against incorrect usage and accidentally transferring references.
	 * continue_at() and closure_return() clear it for you, if you're doing
	 * something unusual you can use closure_set_dead() which also helps
	 * annotate where references are being transferred.
	 *
	 * CLOSURE_STACK: Sanity check - remaining should never hit 0 on a
	 * closure with this flag set
	 */

	CLOSURE_BITS_START	= (1 << 19),
	CLOSURE_DESTRUCTOR	= (1 << 19),
	CLOSURE_BLOCKING	= (1 << 21),
	CLOSURE_WAITING		= (1 << 23),
	CLOSURE_SLEEPING	= (1 << 25),
	CLOSURE_TIMER		= (1 << 27),
	CLOSURE_RUNNING		= (1 << 29),
	CLOSURE_STACK		= (1 << 31),
};

#define CLOSURE_GUARD_MASK					\
	((CLOSURE_DESTRUCTOR|CLOSURE_BLOCKING|CLOSURE_WAITING|	\
	  CLOSURE_SLEEPING|CLOSURE_TIMER|CLOSURE_RUNNING|CLOSURE_STACK) << 1)

#define CLOSURE_REMAINING_MASK		(CLOSURE_BITS_START - 1)
#define CLOSURE_REMAINING_INITIALIZER	(1|CLOSURE_RUNNING)

struct closure {
	union {
		struct {
			struct workqueue_struct *wq;
			struct task_struct	*task;
			struct llist_node	list;
			closure_fn		*fn;
		};
		struct work_struct	work;
	};

	struct closure		*parent;

	atomic_t		remaining;

	enum closure_type	type;

#ifdef CONFIG_BCACHE_CLOSURES_DEBUG
#define CLOSURE_MAGIC_DEAD	0xc054dead
#define CLOSURE_MAGIC_ALIVE	0xc054a11e

	unsigned		magic;
	struct list_head	all;
	unsigned long		ip;
	unsigned long		waiting_on;
#endif
};

struct closure_with_waitlist {
	struct closure		cl;
	struct closure_waitlist	wait;
};

struct closure_with_timer {
	struct closure		cl;
	struct timer_list	timer;
};

struct closure_with_waitlist_and_timer {
	struct closure		cl;
	struct closure_waitlist	wait;
	struct timer_list	timer;
};

extern unsigned invalid_closure_type(void);

#define __CLOSURE_TYPE(cl, _t)						\
	  __builtin_types_compatible_p(typeof(cl), struct _t)		\
		? TYPE_ ## _t :						\

#define __closure_type(cl)						\
(									\
	__CLOSURE_TYPE(cl, closure)					\
	__CLOSURE_TYPE(cl, closure_with_waitlist)			\
	__CLOSURE_TYPE(cl, closure_with_timer)				\
	__CLOSURE_TYPE(cl, closure_with_waitlist_and_timer)		\
	invalid_closure_type()						\
)

void closure_sub(struct closure *cl, int v);
void closure_put(struct closure *cl);
void closure_queue(struct closure *cl);
void __closure_wake_up(struct closure_waitlist *list);
bool closure_wait(struct closure_waitlist *list, struct closure *cl);
void closure_sync(struct closure *cl);

bool closure_trylock(struct closure *cl, struct closure *parent);
void __closure_lock(struct closure *cl, struct closure *parent,
		    struct closure_waitlist *wait_list);

void do_closure_timer_init(struct closure *cl);
bool __closure_delay(struct closure *cl, unsigned long delay,
		     struct timer_list *timer);
void __closure_flush(struct closure *cl, struct timer_list *timer);
void __closure_flush_sync(struct closure *cl, struct timer_list *timer);

#ifdef CONFIG_BCACHE_CLOSURES_DEBUG

void closure_debug_init(void);
void closure_debug_create(struct closure *cl);
void closure_debug_destroy(struct closure *cl);

#else

static inline void closure_debug_init(void) {}
static inline void closure_debug_create(struct closure *cl) {}
static inline void closure_debug_destroy(struct closure *cl) {}

#endif

static inline void closure_set_ip(struct closure *cl)
{
#ifdef CONFIG_BCACHE_CLOSURES_DEBUG
	cl->ip = _THIS_IP_;
#endif
}

static inline void closure_set_ret_ip(struct closure *cl)
{
#ifdef CONFIG_BCACHE_CLOSURES_DEBUG
	cl->ip = _RET_IP_;
#endif
}

static inline void closure_get(struct closure *cl)
{
#ifdef CONFIG_BCACHE_CLOSURES_DEBUG
	BUG_ON((atomic_inc_return(&cl->remaining) &
		CLOSURE_REMAINING_MASK) <= 1);
#else
	atomic_inc(&cl->remaining);
#endif
}

static inline void closure_set_stopped(struct closure *cl)
{
	atomic_sub(CLOSURE_RUNNING, &cl->remaining);
}

static inline bool closure_is_stopped(struct closure *cl)
{
	return !(atomic_read(&cl->remaining) & CLOSURE_RUNNING);
}

static inline bool closure_is_unlocked(struct closure *cl)
{
	return atomic_read(&cl->remaining) == -1;
}

static inline void do_closure_init(struct closure *cl, struct closure *parent,
				   bool running)
{
	switch (cl->type) {
	case TYPE_closure_with_timer:
	case TYPE_closure_with_waitlist_and_timer:
		do_closure_timer_init(cl);
	default:
		break;
	}

	cl->parent = parent;
	if (parent)
		closure_get(parent);

	if (running) {
		closure_debug_create(cl);
		atomic_set(&cl->remaining, CLOSURE_REMAINING_INITIALIZER);
	} else
		atomic_set(&cl->remaining, -1);

	closure_set_ip(cl);
}

/*
 * Hack to get at the embedded closure if there is one, by doing an unsafe cast:
 * the result of __closure_type() is thrown away, it's used merely for type
 * checking.
 */
#define __to_internal_closure(cl)				\
({								\
	BUILD_BUG_ON(__closure_type(*cl) > MAX_CLOSURE_TYPE);	\
	(struct closure *) cl;					\
})

#define closure_init_type(cl, parent, running)			\
do {								\
	struct closure *_cl = __to_internal_closure(cl);	\
	_cl->type = __closure_type(*(cl));			\
	do_closure_init(_cl, parent, running);			\
} while (0)

/**
 * __closure_init() - Initialize a closure, skipping the memset()
 *
 * May be used instead of closure_init() when memory has already been zeroed.
 */
#define __closure_init(cl, parent)				\
	closure_init_type(cl, parent, true)

/**
 * closure_init() - Initialize a closure, setting the refcount to 1
 * @cl:		closure to initialize
 * @parent:	parent of the new closure. cl will take a refcount on it for its
 *		lifetime; may be NULL.
 */
#define closure_init(cl, parent)				\
do {								\
	memset((cl), 0, sizeof(*(cl)));				\
	__closure_init(cl, parent);				\
} while (0)

static inline void closure_init_stack(struct closure *cl)
{
	memset(cl, 0, sizeof(struct closure));
	atomic_set(&cl->remaining, CLOSURE_REMAINING_INITIALIZER|
		   CLOSURE_BLOCKING|CLOSURE_STACK);
}

/**
 * closure_init_unlocked() - Initialize a closure but leave it unlocked.
 * @cl:		closure to initialize
 *
 * For when the closure will be used as a lock. The closure may not be used
 * until after a closure_lock() or closure_trylock().
 */
#define closure_init_unlocked(cl)				\
do {								\
	memset((cl), 0, sizeof(*(cl)));				\
	closure_init_type(cl, NULL, false);			\
} while (0)

/**
 * closure_lock() - lock and initialize a closure.
 * @cl:		the closure to lock
 * @parent:	the new parent for this closure
 *
 * The closure must be of one of the types that has a waitlist (otherwise we
 * wouldn't be able to sleep on contention).
 *
 * @parent has exactly the same meaning as in closure_init(); if non null, the
 * closure will take a reference on @parent which will be released when it is
 * unlocked.
 */
#define closure_lock(cl, parent)				\
	__closure_lock(__to_internal_closure(cl), parent, &(cl)->wait)

/**
 * closure_delay() - delay some number of jiffies
 * @cl:		the closure that will sleep
 * @delay:	the delay in jiffies
 *
 * Takes a refcount on @cl which will be released after @delay jiffies; this may
 * be used to have a function run after a delay with continue_at(), or
 * closure_sync() may be used for a convoluted version of msleep().
 */
#define closure_delay(cl, delay)			\
	__closure_delay(__to_internal_closure(cl), delay, &(cl)->timer)

#define closure_flush(cl)				\
	__closure_flush(__to_internal_closure(cl), &(cl)->timer)

#define closure_flush_sync(cl)				\
	__closure_flush_sync(__to_internal_closure(cl), &(cl)->timer)

static inline void __closure_end_sleep(struct closure *cl)
{
	__set_current_state(TASK_RUNNING);

	if (atomic_read(&cl->remaining) & CLOSURE_SLEEPING)
		atomic_sub(CLOSURE_SLEEPING, &cl->remaining);
}

static inline void __closure_start_sleep(struct closure *cl)
{
	closure_set_ip(cl);
	cl->task = current;
	set_current_state(TASK_UNINTERRUPTIBLE);

	if (!(atomic_read(&cl->remaining) & CLOSURE_SLEEPING))
		atomic_add(CLOSURE_SLEEPING, &cl->remaining);
}

/**
 * closure_blocking() - returns true if the closure is in blocking mode.
 *
 * If a closure is in blocking mode, closure_wait_event() will sleep until the
 * condition is true instead of waiting asynchronously.
 */
static inline bool closure_blocking(struct closure *cl)
{
	return atomic_read(&cl->remaining) & CLOSURE_BLOCKING;
}

/**
 * set_closure_blocking() - put a closure in blocking mode.
 *
 * If a closure is in blocking mode, closure_wait_event() will sleep until the
 * condition is true instead of waiting asynchronously.
 *
 * Not thread safe - can only be called by the thread running the closure.
 */
static inline void set_closure_blocking(struct closure *cl)
{
	if (!closure_blocking(cl))
		atomic_add(CLOSURE_BLOCKING, &cl->remaining);
}

/*
 * Not thread safe - can only be called by the thread running the closure.
 */
static inline void clear_closure_blocking(struct closure *cl)
{
	if (closure_blocking(cl))
		atomic_sub(CLOSURE_BLOCKING, &cl->remaining);
}

/**
 * closure_wake_up() - wake up all closures on a wait list.
 */
static inline void closure_wake_up(struct closure_waitlist *list)
{
	smp_mb();
	__closure_wake_up(list);
}

/*
 * Wait on an event, synchronously or asynchronously - analogous to wait_event()
 * but for closures.
 *
 * The loop is oddly structured so as to avoid a race; we must check the
 * condition again after we've added ourself to the waitlist. We know if we were
 * already on the waitlist because closure_wait() returns false; thus, we only
 * schedule or break if closure_wait() returns false. If it returns true, we
 * just loop again - rechecking the condition.
 *
 * The __closure_wake_up() is necessary because we may race with the event
 * becoming true; i.e. we see event false -> wait -> recheck condition, but the
 * thread that made the event true may have called closure_wake_up() before we
 * added ourself to the wait list.
 *
 * We have to call closure_sync() at the end instead of just
 * __closure_end_sleep() because a different thread might've called
 * closure_wake_up() before us and gotten preempted before they dropped the
 * refcount on our closure. If this was a stack allocated closure, that would be
 * bad.
 */
#define __closure_wait_event(list, cl, condition, _block)		\
({									\
	bool block = _block;						\
	typeof(condition) ret;						\
									\
	while (1) {							\
		ret = (condition);					\
		if (ret) {						\
			__closure_wake_up(list);			\
			if (block)					\
				closure_sync(cl);			\
									\
			break;						\
		}							\
									\
		if (block)						\
			__closure_start_sleep(cl);			\
									\
		if (!closure_wait(list, cl)) {				\
			if (!block)					\
				break;					\
									\
			schedule();					\
		}							\
	}								\
									\
	ret;								\
})

/**
 * closure_wait_event() - wait on a condition, synchronously or asynchronously.
 * @list:	the wait list to wait on
 * @cl:		the closure that is doing the waiting
 * @condition:	a C expression for the event to wait for
 *
 * If the closure is in blocking mode, sleeps until the @condition evaluates to
 * true - exactly like wait_event().
 *
 * If the closure is not in blocking mode, waits asynchronously; if the
 * condition is currently false the @cl is put onto @list and returns. @list
 * owns a refcount on @cl; closure_sync() or continue_at() may be used later to
 * wait for another thread to wake up @list, which drops the refcount on @cl.
 *
 * Returns the value of @condition; @cl will be on @list iff @condition was
 * false.
 *
 * closure_wake_up(@list) must be called after changing any variable that could
 * cause @condition to become true.
 */
#define closure_wait_event(list, cl, condition)				\
	__closure_wait_event(list, cl, condition, closure_blocking(cl))

#define closure_wait_event_async(list, cl, condition)			\
	__closure_wait_event(list, cl, condition, false)

#define closure_wait_event_sync(list, cl, condition)			\
	__closure_wait_event(list, cl, condition, true)

static inline void set_closure_fn(struct closure *cl, closure_fn *fn,
				  struct workqueue_struct *wq)
{
	BUG_ON(object_is_on_stack(cl));
	closure_set_ip(cl);
	cl->fn = fn;
	cl->wq = wq;
	/* between atomic_dec() in closure_put() */
	smp_mb__before_atomic_dec();
}

#define continue_at(_cl, _fn, _wq)					\
do {									\
	set_closure_fn(_cl, _fn, _wq);					\
	closure_sub(_cl, CLOSURE_RUNNING + 1);				\
	return;								\
} while (0)

#define closure_return(_cl)	continue_at((_cl), NULL, NULL)

#define continue_at_nobarrier(_cl, _fn, _wq)				\
do {									\
	set_closure_fn(_cl, _fn, _wq);					\
	closure_queue(cl);						\
	return;								\
} while (0)

#define closure_return_with_destructor(_cl, _destructor)		\
do {									\
	set_closure_fn(_cl, _destructor, NULL);				\
	closure_sub(_cl, CLOSURE_RUNNING - CLOSURE_DESTRUCTOR + 1);	\
	return;								\
} while (0)

static inline void closure_call(struct closure *cl, closure_fn fn,
				struct workqueue_struct *wq,
				struct closure *parent)
{
	closure_init(cl, parent);
	continue_at_nobarrier(cl, fn, wq);
}

static inline void closure_trylock_call(struct closure *cl, closure_fn fn,
					struct workqueue_struct *wq,
					struct closure *parent)
{
	if (closure_trylock(cl, parent))
		continue_at_nobarrier(cl, fn, wq);
}

#endif /* _LINUX_CLOSURE_H */
