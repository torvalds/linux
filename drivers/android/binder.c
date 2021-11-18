// SPDX-License-Identifier: GPL-2.0-only
/* binder.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2007-2008 Google, Inc.
 */

/*
 * Locking overview
 *
 * There are 3 main spinlocks which must be acquired in the
 * order shown:
 *
 * 1) proc->outer_lock : protects binder_ref
 *    binder_proc_lock() and binder_proc_unlock() are
 *    used to acq/rel.
 * 2) node->lock : protects most fields of binder_node.
 *    binder_node_lock() and binder_node_unlock() are
 *    used to acq/rel
 * 3) proc->inner_lock : protects the thread and node lists
 *    (proc->threads, proc->waiting_threads, proc->nodes)
 *    and all todo lists associated with the binder_proc
 *    (proc->todo, thread->todo, proc->delivered_death and
 *    node->async_todo), as well as thread->transaction_stack
 *    binder_inner_proc_lock() and binder_inner_proc_unlock()
 *    are used to acq/rel
 *
 * Any lock under procA must never be nested under any lock at the same
 * level or below on procB.
 *
 * Functions that require a lock held on entry indicate which lock
 * in the suffix of the function name:
 *
 * foo_olocked() : requires node->outer_lock
 * foo_nlocked() : requires node->lock
 * foo_ilocked() : requires proc->inner_lock
 * foo_oilocked(): requires proc->outer_lock and proc->inner_lock
 * foo_nilocked(): requires node->lock and proc->inner_lock
 * ...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include <linux/rbtree.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/pid_namespace.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/ratelimit.h>
#include <linux/syscalls.h>
#include <linux/task_work.h>
#include <linux/sizes.h>

#include <uapi/linux/android/binder.h>

#include <asm/cacheflush.h>

#include "binder_internal.h"
#include "binder_trace.h"

static HLIST_HEAD(binder_deferred_list);
static DEFINE_MUTEX(binder_deferred_lock);

static HLIST_HEAD(binder_devices);
static HLIST_HEAD(binder_procs);
static DEFINE_MUTEX(binder_procs_lock);

static HLIST_HEAD(binder_dead_nodes);
static DEFINE_SPINLOCK(binder_dead_nodes_lock);

static struct dentry *binder_debugfs_dir_entry_root;
static struct dentry *binder_debugfs_dir_entry_proc;
static atomic_t binder_last_id;

static int proc_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(proc);

#define FORBIDDEN_MMAP_FLAGS                (VM_WRITE)

enum {
	BINDER_DEBUG_USER_ERROR             = 1U << 0,
	BINDER_DEBUG_FAILED_TRANSACTION     = 1U << 1,
	BINDER_DEBUG_DEAD_TRANSACTION       = 1U << 2,
	BINDER_DEBUG_OPEN_CLOSE             = 1U << 3,
	BINDER_DEBUG_DEAD_BINDER            = 1U << 4,
	BINDER_DEBUG_DEATH_NOTIFICATION     = 1U << 5,
	BINDER_DEBUG_READ_WRITE             = 1U << 6,
	BINDER_DEBUG_USER_REFS              = 1U << 7,
	BINDER_DEBUG_THREADS                = 1U << 8,
	BINDER_DEBUG_TRANSACTION            = 1U << 9,
	BINDER_DEBUG_TRANSACTION_COMPLETE   = 1U << 10,
	BINDER_DEBUG_FREE_BUFFER            = 1U << 11,
	BINDER_DEBUG_INTERNAL_REFS          = 1U << 12,
	BINDER_DEBUG_PRIORITY_CAP           = 1U << 13,
	BINDER_DEBUG_SPINLOCKS              = 1U << 14,
};
static uint32_t binder_debug_mask = BINDER_DEBUG_USER_ERROR |
	BINDER_DEBUG_FAILED_TRANSACTION | BINDER_DEBUG_DEAD_TRANSACTION;
module_param_named(debug_mask, binder_debug_mask, uint, 0644);

char *binder_devices_param = CONFIG_ANDROID_BINDER_DEVICES;
module_param_named(devices, binder_devices_param, charp, 0444);

static DECLARE_WAIT_QUEUE_HEAD(binder_user_error_wait);
static int binder_stop_on_user_error;

static int binder_set_stop_on_user_error(const char *val,
					 const struct kernel_param *kp)
{
	int ret;

	ret = param_set_int(val, kp);
	if (binder_stop_on_user_error < 2)
		wake_up(&binder_user_error_wait);
	return ret;
}
module_param_call(stop_on_user_error, binder_set_stop_on_user_error,
	param_get_int, &binder_stop_on_user_error, 0644);

#define binder_debug(mask, x...) \
	do { \
		if (binder_debug_mask & mask) \
			pr_info_ratelimited(x); \
	} while (0)

#define binder_user_error(x...) \
	do { \
		if (binder_debug_mask & BINDER_DEBUG_USER_ERROR) \
			pr_info_ratelimited(x); \
		if (binder_stop_on_user_error) \
			binder_stop_on_user_error = 2; \
	} while (0)

#define to_flat_binder_object(hdr) \
	container_of(hdr, struct flat_binder_object, hdr)

#define to_binder_fd_object(hdr) container_of(hdr, struct binder_fd_object, hdr)

#define to_binder_buffer_object(hdr) \
	container_of(hdr, struct binder_buffer_object, hdr)

#define to_binder_fd_array_object(hdr) \
	container_of(hdr, struct binder_fd_array_object, hdr)

static struct binder_stats binder_stats;

static inline void binder_stats_deleted(enum binder_stat_types type)
{
	atomic_inc(&binder_stats.obj_deleted[type]);
}

static inline void binder_stats_created(enum binder_stat_types type)
{
	atomic_inc(&binder_stats.obj_created[type]);
}

struct binder_transaction_log binder_transaction_log;
struct binder_transaction_log binder_transaction_log_failed;

static struct binder_transaction_log_entry *binder_transaction_log_add(
	struct binder_transaction_log *log)
{
	struct binder_transaction_log_entry *e;
	unsigned int cur = atomic_inc_return(&log->cur);

	if (cur >= ARRAY_SIZE(log->entry))
		log->full = true;
	e = &log->entry[cur % ARRAY_SIZE(log->entry)];
	WRITE_ONCE(e->debug_id_done, 0);
	/*
	 * write-barrier to synchronize access to e->debug_id_done.
	 * We make sure the initialized 0 value is seen before
	 * memset() other fields are zeroed by memset.
	 */
	smp_wmb();
	memset(e, 0, sizeof(*e));
	return e;
}

enum binder_deferred_state {
	BINDER_DEFERRED_FLUSH        = 0x01,
	BINDER_DEFERRED_RELEASE      = 0x02,
};

enum {
	BINDER_LOOPER_STATE_REGISTERED  = 0x01,
	BINDER_LOOPER_STATE_ENTERED     = 0x02,
	BINDER_LOOPER_STATE_EXITED      = 0x04,
	BINDER_LOOPER_STATE_INVALID     = 0x08,
	BINDER_LOOPER_STATE_WAITING     = 0x10,
	BINDER_LOOPER_STATE_POLL        = 0x20,
};

/**
 * binder_proc_lock() - Acquire outer lock for given binder_proc
 * @proc:         struct binder_proc to acquire
 *
 * Acquires proc->outer_lock. Used to protect binder_ref
 * structures associated with the given proc.
 */
#define binder_proc_lock(proc) _binder_proc_lock(proc, __LINE__)
static void
_binder_proc_lock(struct binder_proc *proc, int line)
	__acquires(&proc->outer_lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_lock(&proc->outer_lock);
}

/**
 * binder_proc_unlock() - Release spinlock for given binder_proc
 * @proc:         struct binder_proc to acquire
 *
 * Release lock acquired via binder_proc_lock()
 */
#define binder_proc_unlock(_proc) _binder_proc_unlock(_proc, __LINE__)
static void
_binder_proc_unlock(struct binder_proc *proc, int line)
	__releases(&proc->outer_lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_unlock(&proc->outer_lock);
}

/**
 * binder_inner_proc_lock() - Acquire inner lock for given binder_proc
 * @proc:         struct binder_proc to acquire
 *
 * Acquires proc->inner_lock. Used to protect todo lists
 */
#define binder_inner_proc_lock(proc) _binder_inner_proc_lock(proc, __LINE__)
static void
_binder_inner_proc_lock(struct binder_proc *proc, int line)
	__acquires(&proc->inner_lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_lock(&proc->inner_lock);
}

/**
 * binder_inner_proc_unlock() - Release inner lock for given binder_proc
 * @proc:         struct binder_proc to acquire
 *
 * Release lock acquired via binder_inner_proc_lock()
 */
#define binder_inner_proc_unlock(proc) _binder_inner_proc_unlock(proc, __LINE__)
static void
_binder_inner_proc_unlock(struct binder_proc *proc, int line)
	__releases(&proc->inner_lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_unlock(&proc->inner_lock);
}

/**
 * binder_node_lock() - Acquire spinlock for given binder_node
 * @node:         struct binder_node to acquire
 *
 * Acquires node->lock. Used to protect binder_node fields
 */
#define binder_node_lock(node) _binder_node_lock(node, __LINE__)
static void
_binder_node_lock(struct binder_node *node, int line)
	__acquires(&node->lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_lock(&node->lock);
}

/**
 * binder_node_unlock() - Release spinlock for given binder_proc
 * @node:         struct binder_node to acquire
 *
 * Release lock acquired via binder_node_lock()
 */
#define binder_node_unlock(node) _binder_node_unlock(node, __LINE__)
static void
_binder_node_unlock(struct binder_node *node, int line)
	__releases(&node->lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_unlock(&node->lock);
}

/**
 * binder_node_inner_lock() - Acquire node and inner locks
 * @node:         struct binder_node to acquire
 *
 * Acquires node->lock. If node->proc also acquires
 * proc->inner_lock. Used to protect binder_node fields
 */
#define binder_node_inner_lock(node) _binder_node_inner_lock(node, __LINE__)
static void
_binder_node_inner_lock(struct binder_node *node, int line)
	__acquires(&node->lock) __acquires(&node->proc->inner_lock)
{
	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	spin_lock(&node->lock);
	if (node->proc)
		binder_inner_proc_lock(node->proc);
	else
		/* annotation for sparse */
		__acquire(&node->proc->inner_lock);
}

/**
 * binder_node_unlock() - Release node and inner locks
 * @node:         struct binder_node to acquire
 *
 * Release lock acquired via binder_node_lock()
 */
#define binder_node_inner_unlock(node) _binder_node_inner_unlock(node, __LINE__)
static void
_binder_node_inner_unlock(struct binder_node *node, int line)
	__releases(&node->lock) __releases(&node->proc->inner_lock)
{
	struct binder_proc *proc = node->proc;

	binder_debug(BINDER_DEBUG_SPINLOCKS,
		     "%s: line=%d\n", __func__, line);
	if (proc)
		binder_inner_proc_unlock(proc);
	else
		/* annotation for sparse */
		__release(&node->proc->inner_lock);
	spin_unlock(&node->lock);
}

static bool binder_worklist_empty_ilocked(struct list_head *list)
{
	return list_empty(list);
}

/**
 * binder_worklist_empty() - Check if no items on the work list
 * @proc:       binder_proc associated with list
 * @list:	list to check
 *
 * Return: true if there are no items on list, else false
 */
static bool binder_worklist_empty(struct binder_proc *proc,
				  struct list_head *list)
{
	bool ret;

	binder_inner_proc_lock(proc);
	ret = binder_worklist_empty_ilocked(list);
	binder_inner_proc_unlock(proc);
	return ret;
}

/**
 * binder_enqueue_work_ilocked() - Add an item to the work list
 * @work:         struct binder_work to add to list
 * @target_list:  list to add work to
 *
 * Adds the work to the specified list. Asserts that work
 * is not already on a list.
 *
 * Requires the proc->inner_lock to be held.
 */
static void
binder_enqueue_work_ilocked(struct binder_work *work,
			   struct list_head *target_list)
{
	BUG_ON(target_list == NULL);
	BUG_ON(work->entry.next && !list_empty(&work->entry));
	list_add_tail(&work->entry, target_list);
}

/**
 * binder_enqueue_deferred_thread_work_ilocked() - Add deferred thread work
 * @thread:       thread to queue work to
 * @work:         struct binder_work to add to list
 *
 * Adds the work to the todo list of the thread. Doesn't set the process_todo
 * flag, which means that (if it wasn't already set) the thread will go to
 * sleep without handling this work when it calls read.
 *
 * Requires the proc->inner_lock to be held.
 */
static void
binder_enqueue_deferred_thread_work_ilocked(struct binder_thread *thread,
					    struct binder_work *work)
{
	WARN_ON(!list_empty(&thread->waiting_thread_node));
	binder_enqueue_work_ilocked(work, &thread->todo);
}

/**
 * binder_enqueue_thread_work_ilocked() - Add an item to the thread work list
 * @thread:       thread to queue work to
 * @work:         struct binder_work to add to list
 *
 * Adds the work to the todo list of the thread, and enables processing
 * of the todo queue.
 *
 * Requires the proc->inner_lock to be held.
 */
static void
binder_enqueue_thread_work_ilocked(struct binder_thread *thread,
				   struct binder_work *work)
{
	WARN_ON(!list_empty(&thread->waiting_thread_node));
	binder_enqueue_work_ilocked(work, &thread->todo);
	thread->process_todo = true;
}

/**
 * binder_enqueue_thread_work() - Add an item to the thread work list
 * @thread:       thread to queue work to
 * @work:         struct binder_work to add to list
 *
 * Adds the work to the todo list of the thread, and enables processing
 * of the todo queue.
 */
static void
binder_enqueue_thread_work(struct binder_thread *thread,
			   struct binder_work *work)
{
	binder_inner_proc_lock(thread->proc);
	binder_enqueue_thread_work_ilocked(thread, work);
	binder_inner_proc_unlock(thread->proc);
}

static void
binder_dequeue_work_ilocked(struct binder_work *work)
{
	list_del_init(&work->entry);
}

/**
 * binder_dequeue_work() - Removes an item from the work list
 * @proc:         binder_proc associated with list
 * @work:         struct binder_work to remove from list
 *
 * Removes the specified work item from whatever list it is on.
 * Can safely be called if work is not on any list.
 */
static void
binder_dequeue_work(struct binder_proc *proc, struct binder_work *work)
{
	binder_inner_proc_lock(proc);
	binder_dequeue_work_ilocked(work);
	binder_inner_proc_unlock(proc);
}

static struct binder_work *binder_dequeue_work_head_ilocked(
					struct list_head *list)
{
	struct binder_work *w;

	w = list_first_entry_or_null(list, struct binder_work, entry);
	if (w)
		list_del_init(&w->entry);
	return w;
}

static void
binder_defer_work(struct binder_proc *proc, enum binder_deferred_state defer);
static void binder_free_thread(struct binder_thread *thread);
static void binder_free_proc(struct binder_proc *proc);
static void binder_inc_node_tmpref_ilocked(struct binder_node *node);

static bool binder_has_work_ilocked(struct binder_thread *thread,
				    bool do_proc_work)
{
	return thread->process_todo ||
		thread->looper_need_return ||
		(do_proc_work &&
		 !binder_worklist_empty_ilocked(&thread->proc->todo));
}

static bool binder_has_work(struct binder_thread *thread, bool do_proc_work)
{
	bool has_work;

	binder_inner_proc_lock(thread->proc);
	has_work = binder_has_work_ilocked(thread, do_proc_work);
	binder_inner_proc_unlock(thread->proc);

	return has_work;
}

static bool binder_available_for_proc_work_ilocked(struct binder_thread *thread)
{
	return !thread->transaction_stack &&
		binder_worklist_empty_ilocked(&thread->todo) &&
		(thread->looper & (BINDER_LOOPER_STATE_ENTERED |
				   BINDER_LOOPER_STATE_REGISTERED));
}

static void binder_wakeup_poll_threads_ilocked(struct binder_proc *proc,
					       bool sync)
{
	struct rb_node *n;
	struct binder_thread *thread;

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		if (thread->looper & BINDER_LOOPER_STATE_POLL &&
		    binder_available_for_proc_work_ilocked(thread)) {
			if (sync)
				wake_up_interruptible_sync(&thread->wait);
			else
				wake_up_interruptible(&thread->wait);
		}
	}
}

/**
 * binder_select_thread_ilocked() - selects a thread for doing proc work.
 * @proc:	process to select a thread from
 *
 * Note that calling this function moves the thread off the waiting_threads
 * list, so it can only be woken up by the caller of this function, or a
 * signal. Therefore, callers *should* always wake up the thread this function
 * returns.
 *
 * Return:	If there's a thread currently waiting for process work,
 *		returns that thread. Otherwise returns NULL.
 */
static struct binder_thread *
binder_select_thread_ilocked(struct binder_proc *proc)
{
	struct binder_thread *thread;

	assert_spin_locked(&proc->inner_lock);
	thread = list_first_entry_or_null(&proc->waiting_threads,
					  struct binder_thread,
					  waiting_thread_node);

	if (thread)
		list_del_init(&thread->waiting_thread_node);

	return thread;
}

/**
 * binder_wakeup_thread_ilocked() - wakes up a thread for doing proc work.
 * @proc:	process to wake up a thread in
 * @thread:	specific thread to wake-up (may be NULL)
 * @sync:	whether to do a synchronous wake-up
 *
 * This function wakes up a thread in the @proc process.
 * The caller may provide a specific thread to wake-up in
 * the @thread parameter. If @thread is NULL, this function
 * will wake up threads that have called poll().
 *
 * Note that for this function to work as expected, callers
 * should first call binder_select_thread() to find a thread
 * to handle the work (if they don't have a thread already),
 * and pass the result into the @thread parameter.
 */
static void binder_wakeup_thread_ilocked(struct binder_proc *proc,
					 struct binder_thread *thread,
					 bool sync)
{
	assert_spin_locked(&proc->inner_lock);

	if (thread) {
		if (sync)
			wake_up_interruptible_sync(&thread->wait);
		else
			wake_up_interruptible(&thread->wait);
		return;
	}

	/* Didn't find a thread waiting for proc work; this can happen
	 * in two scenarios:
	 * 1. All threads are busy handling transactions
	 *    In that case, one of those threads should call back into
	 *    the kernel driver soon and pick up this work.
	 * 2. Threads are using the (e)poll interface, in which case
	 *    they may be blocked on the waitqueue without having been
	 *    added to waiting_threads. For this case, we just iterate
	 *    over all threads not handling transaction work, and
	 *    wake them all up. We wake all because we don't know whether
	 *    a thread that called into (e)poll is handling non-binder
	 *    work currently.
	 */
	binder_wakeup_poll_threads_ilocked(proc, sync);
}

static void binder_wakeup_proc_ilocked(struct binder_proc *proc)
{
	struct binder_thread *thread = binder_select_thread_ilocked(proc);

	binder_wakeup_thread_ilocked(proc, thread, /* sync = */false);
}

static void binder_set_nice(long nice)
{
	long min_nice;

	if (can_nice(current, nice)) {
		set_user_nice(current, nice);
		return;
	}
	min_nice = rlimit_to_nice(rlimit(RLIMIT_NICE));
	binder_debug(BINDER_DEBUG_PRIORITY_CAP,
		     "%d: nice value %ld not allowed use %ld instead\n",
		      current->pid, nice, min_nice);
	set_user_nice(current, min_nice);
	if (min_nice <= MAX_NICE)
		return;
	binder_user_error("%d RLIMIT_NICE not set\n", current->pid);
}

static struct binder_node *binder_get_node_ilocked(struct binder_proc *proc,
						   binder_uintptr_t ptr)
{
	struct rb_node *n = proc->nodes.rb_node;
	struct binder_node *node;

	assert_spin_locked(&proc->inner_lock);

	while (n) {
		node = rb_entry(n, struct binder_node, rb_node);

		if (ptr < node->ptr)
			n = n->rb_left;
		else if (ptr > node->ptr)
			n = n->rb_right;
		else {
			/*
			 * take an implicit weak reference
			 * to ensure node stays alive until
			 * call to binder_put_node()
			 */
			binder_inc_node_tmpref_ilocked(node);
			return node;
		}
	}
	return NULL;
}

static struct binder_node *binder_get_node(struct binder_proc *proc,
					   binder_uintptr_t ptr)
{
	struct binder_node *node;

	binder_inner_proc_lock(proc);
	node = binder_get_node_ilocked(proc, ptr);
	binder_inner_proc_unlock(proc);
	return node;
}

static struct binder_node *binder_init_node_ilocked(
						struct binder_proc *proc,
						struct binder_node *new_node,
						struct flat_binder_object *fp)
{
	struct rb_node **p = &proc->nodes.rb_node;
	struct rb_node *parent = NULL;
	struct binder_node *node;
	binder_uintptr_t ptr = fp ? fp->binder : 0;
	binder_uintptr_t cookie = fp ? fp->cookie : 0;
	__u32 flags = fp ? fp->flags : 0;

	assert_spin_locked(&proc->inner_lock);

	while (*p) {

		parent = *p;
		node = rb_entry(parent, struct binder_node, rb_node);

		if (ptr < node->ptr)
			p = &(*p)->rb_left;
		else if (ptr > node->ptr)
			p = &(*p)->rb_right;
		else {
			/*
			 * A matching node is already in
			 * the rb tree. Abandon the init
			 * and return it.
			 */
			binder_inc_node_tmpref_ilocked(node);
			return node;
		}
	}
	node = new_node;
	binder_stats_created(BINDER_STAT_NODE);
	node->tmp_refs++;
	rb_link_node(&node->rb_node, parent, p);
	rb_insert_color(&node->rb_node, &proc->nodes);
	node->debug_id = atomic_inc_return(&binder_last_id);
	node->proc = proc;
	node->ptr = ptr;
	node->cookie = cookie;
	node->work.type = BINDER_WORK_NODE;
	node->min_priority = flags & FLAT_BINDER_FLAG_PRIORITY_MASK;
	node->accept_fds = !!(flags & FLAT_BINDER_FLAG_ACCEPTS_FDS);
	node->txn_security_ctx = !!(flags & FLAT_BINDER_FLAG_TXN_SECURITY_CTX);
	spin_lock_init(&node->lock);
	INIT_LIST_HEAD(&node->work.entry);
	INIT_LIST_HEAD(&node->async_todo);
	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d:%d node %d u%016llx c%016llx created\n",
		     proc->pid, current->pid, node->debug_id,
		     (u64)node->ptr, (u64)node->cookie);

	return node;
}

static struct binder_node *binder_new_node(struct binder_proc *proc,
					   struct flat_binder_object *fp)
{
	struct binder_node *node;
	struct binder_node *new_node = kzalloc(sizeof(*node), GFP_KERNEL);

	if (!new_node)
		return NULL;
	binder_inner_proc_lock(proc);
	node = binder_init_node_ilocked(proc, new_node, fp);
	binder_inner_proc_unlock(proc);
	if (node != new_node)
		/*
		 * The node was already added by another thread
		 */
		kfree(new_node);

	return node;
}

static void binder_free_node(struct binder_node *node)
{
	kfree(node);
	binder_stats_deleted(BINDER_STAT_NODE);
}

static int binder_inc_node_nilocked(struct binder_node *node, int strong,
				    int internal,
				    struct list_head *target_list)
{
	struct binder_proc *proc = node->proc;

	assert_spin_locked(&node->lock);
	if (proc)
		assert_spin_locked(&proc->inner_lock);
	if (strong) {
		if (internal) {
			if (target_list == NULL &&
			    node->internal_strong_refs == 0 &&
			    !(node->proc &&
			      node == node->proc->context->binder_context_mgr_node &&
			      node->has_strong_ref)) {
				pr_err("invalid inc strong node for %d\n",
					node->debug_id);
				return -EINVAL;
			}
			node->internal_strong_refs++;
		} else
			node->local_strong_refs++;
		if (!node->has_strong_ref && target_list) {
			struct binder_thread *thread = container_of(target_list,
						    struct binder_thread, todo);
			binder_dequeue_work_ilocked(&node->work);
			BUG_ON(&thread->todo != target_list);
			binder_enqueue_deferred_thread_work_ilocked(thread,
								   &node->work);
		}
	} else {
		if (!internal)
			node->local_weak_refs++;
		if (!node->has_weak_ref && list_empty(&node->work.entry)) {
			if (target_list == NULL) {
				pr_err("invalid inc weak node for %d\n",
					node->debug_id);
				return -EINVAL;
			}
			/*
			 * See comment above
			 */
			binder_enqueue_work_ilocked(&node->work, target_list);
		}
	}
	return 0;
}

static int binder_inc_node(struct binder_node *node, int strong, int internal,
			   struct list_head *target_list)
{
	int ret;

	binder_node_inner_lock(node);
	ret = binder_inc_node_nilocked(node, strong, internal, target_list);
	binder_node_inner_unlock(node);

	return ret;
}

static bool binder_dec_node_nilocked(struct binder_node *node,
				     int strong, int internal)
{
	struct binder_proc *proc = node->proc;

	assert_spin_locked(&node->lock);
	if (proc)
		assert_spin_locked(&proc->inner_lock);
	if (strong) {
		if (internal)
			node->internal_strong_refs--;
		else
			node->local_strong_refs--;
		if (node->local_strong_refs || node->internal_strong_refs)
			return false;
	} else {
		if (!internal)
			node->local_weak_refs--;
		if (node->local_weak_refs || node->tmp_refs ||
				!hlist_empty(&node->refs))
			return false;
	}

	if (proc && (node->has_strong_ref || node->has_weak_ref)) {
		if (list_empty(&node->work.entry)) {
			binder_enqueue_work_ilocked(&node->work, &proc->todo);
			binder_wakeup_proc_ilocked(proc);
		}
	} else {
		if (hlist_empty(&node->refs) && !node->local_strong_refs &&
		    !node->local_weak_refs && !node->tmp_refs) {
			if (proc) {
				binder_dequeue_work_ilocked(&node->work);
				rb_erase(&node->rb_node, &proc->nodes);
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "refless node %d deleted\n",
					     node->debug_id);
			} else {
				BUG_ON(!list_empty(&node->work.entry));
				spin_lock(&binder_dead_nodes_lock);
				/*
				 * tmp_refs could have changed so
				 * check it again
				 */
				if (node->tmp_refs) {
					spin_unlock(&binder_dead_nodes_lock);
					return false;
				}
				hlist_del(&node->dead_node);
				spin_unlock(&binder_dead_nodes_lock);
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "dead node %d deleted\n",
					     node->debug_id);
			}
			return true;
		}
	}
	return false;
}

static void binder_dec_node(struct binder_node *node, int strong, int internal)
{
	bool free_node;

	binder_node_inner_lock(node);
	free_node = binder_dec_node_nilocked(node, strong, internal);
	binder_node_inner_unlock(node);
	if (free_node)
		binder_free_node(node);
}

static void binder_inc_node_tmpref_ilocked(struct binder_node *node)
{
	/*
	 * No call to binder_inc_node() is needed since we
	 * don't need to inform userspace of any changes to
	 * tmp_refs
	 */
	node->tmp_refs++;
}

/**
 * binder_inc_node_tmpref() - take a temporary reference on node
 * @node:	node to reference
 *
 * Take reference on node to prevent the node from being freed
 * while referenced only by a local variable. The inner lock is
 * needed to serialize with the node work on the queue (which
 * isn't needed after the node is dead). If the node is dead
 * (node->proc is NULL), use binder_dead_nodes_lock to protect
 * node->tmp_refs against dead-node-only cases where the node
 * lock cannot be acquired (eg traversing the dead node list to
 * print nodes)
 */
static void binder_inc_node_tmpref(struct binder_node *node)
{
	binder_node_lock(node);
	if (node->proc)
		binder_inner_proc_lock(node->proc);
	else
		spin_lock(&binder_dead_nodes_lock);
	binder_inc_node_tmpref_ilocked(node);
	if (node->proc)
		binder_inner_proc_unlock(node->proc);
	else
		spin_unlock(&binder_dead_nodes_lock);
	binder_node_unlock(node);
}

/**
 * binder_dec_node_tmpref() - remove a temporary reference on node
 * @node:	node to reference
 *
 * Release temporary reference on node taken via binder_inc_node_tmpref()
 */
static void binder_dec_node_tmpref(struct binder_node *node)
{
	bool free_node;

	binder_node_inner_lock(node);
	if (!node->proc)
		spin_lock(&binder_dead_nodes_lock);
	else
		__acquire(&binder_dead_nodes_lock);
	node->tmp_refs--;
	BUG_ON(node->tmp_refs < 0);
	if (!node->proc)
		spin_unlock(&binder_dead_nodes_lock);
	else
		__release(&binder_dead_nodes_lock);
	/*
	 * Call binder_dec_node() to check if all refcounts are 0
	 * and cleanup is needed. Calling with strong=0 and internal=1
	 * causes no actual reference to be released in binder_dec_node().
	 * If that changes, a change is needed here too.
	 */
	free_node = binder_dec_node_nilocked(node, 0, 1);
	binder_node_inner_unlock(node);
	if (free_node)
		binder_free_node(node);
}

static void binder_put_node(struct binder_node *node)
{
	binder_dec_node_tmpref(node);
}

static struct binder_ref *binder_get_ref_olocked(struct binder_proc *proc,
						 u32 desc, bool need_strong_ref)
{
	struct rb_node *n = proc->refs_by_desc.rb_node;
	struct binder_ref *ref;

	while (n) {
		ref = rb_entry(n, struct binder_ref, rb_node_desc);

		if (desc < ref->data.desc) {
			n = n->rb_left;
		} else if (desc > ref->data.desc) {
			n = n->rb_right;
		} else if (need_strong_ref && !ref->data.strong) {
			binder_user_error("tried to use weak ref as strong ref\n");
			return NULL;
		} else {
			return ref;
		}
	}
	return NULL;
}

/**
 * binder_get_ref_for_node_olocked() - get the ref associated with given node
 * @proc:	binder_proc that owns the ref
 * @node:	binder_node of target
 * @new_ref:	newly allocated binder_ref to be initialized or %NULL
 *
 * Look up the ref for the given node and return it if it exists
 *
 * If it doesn't exist and the caller provides a newly allocated
 * ref, initialize the fields of the newly allocated ref and insert
 * into the given proc rb_trees and node refs list.
 *
 * Return:	the ref for node. It is possible that another thread
 *		allocated/initialized the ref first in which case the
 *		returned ref would be different than the passed-in
 *		new_ref. new_ref must be kfree'd by the caller in
 *		this case.
 */
static struct binder_ref *binder_get_ref_for_node_olocked(
					struct binder_proc *proc,
					struct binder_node *node,
					struct binder_ref *new_ref)
{
	struct binder_context *context = proc->context;
	struct rb_node **p = &proc->refs_by_node.rb_node;
	struct rb_node *parent = NULL;
	struct binder_ref *ref;
	struct rb_node *n;

	while (*p) {
		parent = *p;
		ref = rb_entry(parent, struct binder_ref, rb_node_node);

		if (node < ref->node)
			p = &(*p)->rb_left;
		else if (node > ref->node)
			p = &(*p)->rb_right;
		else
			return ref;
	}
	if (!new_ref)
		return NULL;

	binder_stats_created(BINDER_STAT_REF);
	new_ref->data.debug_id = atomic_inc_return(&binder_last_id);
	new_ref->proc = proc;
	new_ref->node = node;
	rb_link_node(&new_ref->rb_node_node, parent, p);
	rb_insert_color(&new_ref->rb_node_node, &proc->refs_by_node);

	new_ref->data.desc = (node == context->binder_context_mgr_node) ? 0 : 1;
	for (n = rb_first(&proc->refs_by_desc); n != NULL; n = rb_next(n)) {
		ref = rb_entry(n, struct binder_ref, rb_node_desc);
		if (ref->data.desc > new_ref->data.desc)
			break;
		new_ref->data.desc = ref->data.desc + 1;
	}

	p = &proc->refs_by_desc.rb_node;
	while (*p) {
		parent = *p;
		ref = rb_entry(parent, struct binder_ref, rb_node_desc);

		if (new_ref->data.desc < ref->data.desc)
			p = &(*p)->rb_left;
		else if (new_ref->data.desc > ref->data.desc)
			p = &(*p)->rb_right;
		else
			BUG();
	}
	rb_link_node(&new_ref->rb_node_desc, parent, p);
	rb_insert_color(&new_ref->rb_node_desc, &proc->refs_by_desc);

	binder_node_lock(node);
	hlist_add_head(&new_ref->node_entry, &node->refs);

	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d new ref %d desc %d for node %d\n",
		      proc->pid, new_ref->data.debug_id, new_ref->data.desc,
		      node->debug_id);
	binder_node_unlock(node);
	return new_ref;
}

static void binder_cleanup_ref_olocked(struct binder_ref *ref)
{
	bool delete_node = false;

	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d delete ref %d desc %d for node %d\n",
		      ref->proc->pid, ref->data.debug_id, ref->data.desc,
		      ref->node->debug_id);

	rb_erase(&ref->rb_node_desc, &ref->proc->refs_by_desc);
	rb_erase(&ref->rb_node_node, &ref->proc->refs_by_node);

	binder_node_inner_lock(ref->node);
	if (ref->data.strong)
		binder_dec_node_nilocked(ref->node, 1, 1);

	hlist_del(&ref->node_entry);
	delete_node = binder_dec_node_nilocked(ref->node, 0, 1);
	binder_node_inner_unlock(ref->node);
	/*
	 * Clear ref->node unless we want the caller to free the node
	 */
	if (!delete_node) {
		/*
		 * The caller uses ref->node to determine
		 * whether the node needs to be freed. Clear
		 * it since the node is still alive.
		 */
		ref->node = NULL;
	}

	if (ref->death) {
		binder_debug(BINDER_DEBUG_DEAD_BINDER,
			     "%d delete ref %d desc %d has death notification\n",
			      ref->proc->pid, ref->data.debug_id,
			      ref->data.desc);
		binder_dequeue_work(ref->proc, &ref->death->work);
		binder_stats_deleted(BINDER_STAT_DEATH);
	}
	binder_stats_deleted(BINDER_STAT_REF);
}

/**
 * binder_inc_ref_olocked() - increment the ref for given handle
 * @ref:         ref to be incremented
 * @strong:      if true, strong increment, else weak
 * @target_list: list to queue node work on
 *
 * Increment the ref. @ref->proc->outer_lock must be held on entry
 *
 * Return: 0, if successful, else errno
 */
static int binder_inc_ref_olocked(struct binder_ref *ref, int strong,
				  struct list_head *target_list)
{
	int ret;

	if (strong) {
		if (ref->data.strong == 0) {
			ret = binder_inc_node(ref->node, 1, 1, target_list);
			if (ret)
				return ret;
		}
		ref->data.strong++;
	} else {
		if (ref->data.weak == 0) {
			ret = binder_inc_node(ref->node, 0, 1, target_list);
			if (ret)
				return ret;
		}
		ref->data.weak++;
	}
	return 0;
}

/**
 * binder_dec_ref() - dec the ref for given handle
 * @ref:	ref to be decremented
 * @strong:	if true, strong decrement, else weak
 *
 * Decrement the ref.
 *
 * Return: true if ref is cleaned up and ready to be freed
 */
static bool binder_dec_ref_olocked(struct binder_ref *ref, int strong)
{
	if (strong) {
		if (ref->data.strong == 0) {
			binder_user_error("%d invalid dec strong, ref %d desc %d s %d w %d\n",
					  ref->proc->pid, ref->data.debug_id,
					  ref->data.desc, ref->data.strong,
					  ref->data.weak);
			return false;
		}
		ref->data.strong--;
		if (ref->data.strong == 0)
			binder_dec_node(ref->node, strong, 1);
	} else {
		if (ref->data.weak == 0) {
			binder_user_error("%d invalid dec weak, ref %d desc %d s %d w %d\n",
					  ref->proc->pid, ref->data.debug_id,
					  ref->data.desc, ref->data.strong,
					  ref->data.weak);
			return false;
		}
		ref->data.weak--;
	}
	if (ref->data.strong == 0 && ref->data.weak == 0) {
		binder_cleanup_ref_olocked(ref);
		return true;
	}
	return false;
}

/**
 * binder_get_node_from_ref() - get the node from the given proc/desc
 * @proc:	proc containing the ref
 * @desc:	the handle associated with the ref
 * @need_strong_ref: if true, only return node if ref is strong
 * @rdata:	the id/refcount data for the ref
 *
 * Given a proc and ref handle, return the associated binder_node
 *
 * Return: a binder_node or NULL if not found or not strong when strong required
 */
static struct binder_node *binder_get_node_from_ref(
		struct binder_proc *proc,
		u32 desc, bool need_strong_ref,
		struct binder_ref_data *rdata)
{
	struct binder_node *node;
	struct binder_ref *ref;

	binder_proc_lock(proc);
	ref = binder_get_ref_olocked(proc, desc, need_strong_ref);
	if (!ref)
		goto err_no_ref;
	node = ref->node;
	/*
	 * Take an implicit reference on the node to ensure
	 * it stays alive until the call to binder_put_node()
	 */
	binder_inc_node_tmpref(node);
	if (rdata)
		*rdata = ref->data;
	binder_proc_unlock(proc);

	return node;

err_no_ref:
	binder_proc_unlock(proc);
	return NULL;
}

/**
 * binder_free_ref() - free the binder_ref
 * @ref:	ref to free
 *
 * Free the binder_ref. Free the binder_node indicated by ref->node
 * (if non-NULL) and the binder_ref_death indicated by ref->death.
 */
static void binder_free_ref(struct binder_ref *ref)
{
	if (ref->node)
		binder_free_node(ref->node);
	kfree(ref->death);
	kfree(ref);
}

/**
 * binder_update_ref_for_handle() - inc/dec the ref for given handle
 * @proc:	proc containing the ref
 * @desc:	the handle associated with the ref
 * @increment:	true=inc reference, false=dec reference
 * @strong:	true=strong reference, false=weak reference
 * @rdata:	the id/refcount data for the ref
 *
 * Given a proc and ref handle, increment or decrement the ref
 * according to "increment" arg.
 *
 * Return: 0 if successful, else errno
 */
static int binder_update_ref_for_handle(struct binder_proc *proc,
		uint32_t desc, bool increment, bool strong,
		struct binder_ref_data *rdata)
{
	int ret = 0;
	struct binder_ref *ref;
	bool delete_ref = false;

	binder_proc_lock(proc);
	ref = binder_get_ref_olocked(proc, desc, strong);
	if (!ref) {
		ret = -EINVAL;
		goto err_no_ref;
	}
	if (increment)
		ret = binder_inc_ref_olocked(ref, strong, NULL);
	else
		delete_ref = binder_dec_ref_olocked(ref, strong);

	if (rdata)
		*rdata = ref->data;
	binder_proc_unlock(proc);

	if (delete_ref)
		binder_free_ref(ref);
	return ret;

err_no_ref:
	binder_proc_unlock(proc);
	return ret;
}

/**
 * binder_dec_ref_for_handle() - dec the ref for given handle
 * @proc:	proc containing the ref
 * @desc:	the handle associated with the ref
 * @strong:	true=strong reference, false=weak reference
 * @rdata:	the id/refcount data for the ref
 *
 * Just calls binder_update_ref_for_handle() to decrement the ref.
 *
 * Return: 0 if successful, else errno
 */
static int binder_dec_ref_for_handle(struct binder_proc *proc,
		uint32_t desc, bool strong, struct binder_ref_data *rdata)
{
	return binder_update_ref_for_handle(proc, desc, false, strong, rdata);
}


/**
 * binder_inc_ref_for_node() - increment the ref for given proc/node
 * @proc:	 proc containing the ref
 * @node:	 target node
 * @strong:	 true=strong reference, false=weak reference
 * @target_list: worklist to use if node is incremented
 * @rdata:	 the id/refcount data for the ref
 *
 * Given a proc and node, increment the ref. Create the ref if it
 * doesn't already exist
 *
 * Return: 0 if successful, else errno
 */
static int binder_inc_ref_for_node(struct binder_proc *proc,
			struct binder_node *node,
			bool strong,
			struct list_head *target_list,
			struct binder_ref_data *rdata)
{
	struct binder_ref *ref;
	struct binder_ref *new_ref = NULL;
	int ret = 0;

	binder_proc_lock(proc);
	ref = binder_get_ref_for_node_olocked(proc, node, NULL);
	if (!ref) {
		binder_proc_unlock(proc);
		new_ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!new_ref)
			return -ENOMEM;
		binder_proc_lock(proc);
		ref = binder_get_ref_for_node_olocked(proc, node, new_ref);
	}
	ret = binder_inc_ref_olocked(ref, strong, target_list);
	*rdata = ref->data;
	binder_proc_unlock(proc);
	if (new_ref && ref != new_ref)
		/*
		 * Another thread created the ref first so
		 * free the one we allocated
		 */
		kfree(new_ref);
	return ret;
}

static void binder_pop_transaction_ilocked(struct binder_thread *target_thread,
					   struct binder_transaction *t)
{
	BUG_ON(!target_thread);
	assert_spin_locked(&target_thread->proc->inner_lock);
	BUG_ON(target_thread->transaction_stack != t);
	BUG_ON(target_thread->transaction_stack->from != target_thread);
	target_thread->transaction_stack =
		target_thread->transaction_stack->from_parent;
	t->from = NULL;
}

/**
 * binder_thread_dec_tmpref() - decrement thread->tmp_ref
 * @thread:	thread to decrement
 *
 * A thread needs to be kept alive while being used to create or
 * handle a transaction. binder_get_txn_from() is used to safely
 * extract t->from from a binder_transaction and keep the thread
 * indicated by t->from from being freed. When done with that
 * binder_thread, this function is called to decrement the
 * tmp_ref and free if appropriate (thread has been released
 * and no transaction being processed by the driver)
 */
static void binder_thread_dec_tmpref(struct binder_thread *thread)
{
	/*
	 * atomic is used to protect the counter value while
	 * it cannot reach zero or thread->is_dead is false
	 */
	binder_inner_proc_lock(thread->proc);
	atomic_dec(&thread->tmp_ref);
	if (thread->is_dead && !atomic_read(&thread->tmp_ref)) {
		binder_inner_proc_unlock(thread->proc);
		binder_free_thread(thread);
		return;
	}
	binder_inner_proc_unlock(thread->proc);
}

/**
 * binder_proc_dec_tmpref() - decrement proc->tmp_ref
 * @proc:	proc to decrement
 *
 * A binder_proc needs to be kept alive while being used to create or
 * handle a transaction. proc->tmp_ref is incremented when
 * creating a new transaction or the binder_proc is currently in-use
 * by threads that are being released. When done with the binder_proc,
 * this function is called to decrement the counter and free the
 * proc if appropriate (proc has been released, all threads have
 * been released and not currenly in-use to process a transaction).
 */
static void binder_proc_dec_tmpref(struct binder_proc *proc)
{
	binder_inner_proc_lock(proc);
	proc->tmp_ref--;
	if (proc->is_dead && RB_EMPTY_ROOT(&proc->threads) &&
			!proc->tmp_ref) {
		binder_inner_proc_unlock(proc);
		binder_free_proc(proc);
		return;
	}
	binder_inner_proc_unlock(proc);
}

/**
 * binder_get_txn_from() - safely extract the "from" thread in transaction
 * @t:	binder transaction for t->from
 *
 * Atomically return the "from" thread and increment the tmp_ref
 * count for the thread to ensure it stays alive until
 * binder_thread_dec_tmpref() is called.
 *
 * Return: the value of t->from
 */
static struct binder_thread *binder_get_txn_from(
		struct binder_transaction *t)
{
	struct binder_thread *from;

	spin_lock(&t->lock);
	from = t->from;
	if (from)
		atomic_inc(&from->tmp_ref);
	spin_unlock(&t->lock);
	return from;
}

/**
 * binder_get_txn_from_and_acq_inner() - get t->from and acquire inner lock
 * @t:	binder transaction for t->from
 *
 * Same as binder_get_txn_from() except it also acquires the proc->inner_lock
 * to guarantee that the thread cannot be released while operating on it.
 * The caller must call binder_inner_proc_unlock() to release the inner lock
 * as well as call binder_dec_thread_txn() to release the reference.
 *
 * Return: the value of t->from
 */
static struct binder_thread *binder_get_txn_from_and_acq_inner(
		struct binder_transaction *t)
	__acquires(&t->from->proc->inner_lock)
{
	struct binder_thread *from;

	from = binder_get_txn_from(t);
	if (!from) {
		__acquire(&from->proc->inner_lock);
		return NULL;
	}
	binder_inner_proc_lock(from->proc);
	if (t->from) {
		BUG_ON(from != t->from);
		return from;
	}
	binder_inner_proc_unlock(from->proc);
	__acquire(&from->proc->inner_lock);
	binder_thread_dec_tmpref(from);
	return NULL;
}

/**
 * binder_free_txn_fixups() - free unprocessed fd fixups
 * @t:	binder transaction for t->from
 *
 * If the transaction is being torn down prior to being
 * processed by the target process, free all of the
 * fd fixups and fput the file structs. It is safe to
 * call this function after the fixups have been
 * processed -- in that case, the list will be empty.
 */
static void binder_free_txn_fixups(struct binder_transaction *t)
{
	struct binder_txn_fd_fixup *fixup, *tmp;

	list_for_each_entry_safe(fixup, tmp, &t->fd_fixups, fixup_entry) {
		fput(fixup->file);
		list_del(&fixup->fixup_entry);
		kfree(fixup);
	}
}

static void binder_txn_latency_free(struct binder_transaction *t)
{
	int from_proc, from_thread, to_proc, to_thread;

	spin_lock(&t->lock);
	from_proc = t->from ? t->from->proc->pid : 0;
	from_thread = t->from ? t->from->pid : 0;
	to_proc = t->to_proc ? t->to_proc->pid : 0;
	to_thread = t->to_thread ? t->to_thread->pid : 0;
	spin_unlock(&t->lock);

	trace_binder_txn_latency_free(t, from_proc, from_thread, to_proc, to_thread);
}

static void binder_free_transaction(struct binder_transaction *t)
{
	struct binder_proc *target_proc = t->to_proc;

	if (target_proc) {
		binder_inner_proc_lock(target_proc);
		target_proc->outstanding_txns--;
		if (target_proc->outstanding_txns < 0)
			pr_warn("%s: Unexpected outstanding_txns %d\n",
				__func__, target_proc->outstanding_txns);
		if (!target_proc->outstanding_txns && target_proc->is_frozen)
			wake_up_interruptible_all(&target_proc->freeze_wait);
		if (t->buffer)
			t->buffer->transaction = NULL;
		binder_inner_proc_unlock(target_proc);
	}
	if (trace_binder_txn_latency_free_enabled())
		binder_txn_latency_free(t);
	/*
	 * If the transaction has no target_proc, then
	 * t->buffer->transaction has already been cleared.
	 */
	binder_free_txn_fixups(t);
	kfree(t);
	binder_stats_deleted(BINDER_STAT_TRANSACTION);
}

static void binder_send_failed_reply(struct binder_transaction *t,
				     uint32_t error_code)
{
	struct binder_thread *target_thread;
	struct binder_transaction *next;

	BUG_ON(t->flags & TF_ONE_WAY);
	while (1) {
		target_thread = binder_get_txn_from_and_acq_inner(t);
		if (target_thread) {
			binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
				     "send failed reply for transaction %d to %d:%d\n",
				      t->debug_id,
				      target_thread->proc->pid,
				      target_thread->pid);

			binder_pop_transaction_ilocked(target_thread, t);
			if (target_thread->reply_error.cmd == BR_OK) {
				target_thread->reply_error.cmd = error_code;
				binder_enqueue_thread_work_ilocked(
					target_thread,
					&target_thread->reply_error.work);
				wake_up_interruptible(&target_thread->wait);
			} else {
				/*
				 * Cannot get here for normal operation, but
				 * we can if multiple synchronous transactions
				 * are sent without blocking for responses.
				 * Just ignore the 2nd error in this case.
				 */
				pr_warn("Unexpected reply error: %u\n",
					target_thread->reply_error.cmd);
			}
			binder_inner_proc_unlock(target_thread->proc);
			binder_thread_dec_tmpref(target_thread);
			binder_free_transaction(t);
			return;
		}
		__release(&target_thread->proc->inner_lock);
		next = t->from_parent;

		binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
			     "send failed reply for transaction %d, target dead\n",
			     t->debug_id);

		binder_free_transaction(t);
		if (next == NULL) {
			binder_debug(BINDER_DEBUG_DEAD_BINDER,
				     "reply failed, no target thread at root\n");
			return;
		}
		t = next;
		binder_debug(BINDER_DEBUG_DEAD_BINDER,
			     "reply failed, no target thread -- retry %d\n",
			      t->debug_id);
	}
}

/**
 * binder_cleanup_transaction() - cleans up undelivered transaction
 * @t:		transaction that needs to be cleaned up
 * @reason:	reason the transaction wasn't delivered
 * @error_code:	error to return to caller (if synchronous call)
 */
static void binder_cleanup_transaction(struct binder_transaction *t,
				       const char *reason,
				       uint32_t error_code)
{
	if (t->buffer->target_node && !(t->flags & TF_ONE_WAY)) {
		binder_send_failed_reply(t, error_code);
	} else {
		binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
			"undelivered transaction %d, %s\n",
			t->debug_id, reason);
		binder_free_transaction(t);
	}
}

/**
 * binder_get_object() - gets object and checks for valid metadata
 * @proc:	binder_proc owning the buffer
 * @buffer:	binder_buffer that we're parsing.
 * @offset:	offset in the @buffer at which to validate an object.
 * @object:	struct binder_object to read into
 *
 * Return:	If there's a valid metadata object at @offset in @buffer, the
 *		size of that object. Otherwise, it returns zero. The object
 *		is read into the struct binder_object pointed to by @object.
 */
static size_t binder_get_object(struct binder_proc *proc,
				struct binder_buffer *buffer,
				unsigned long offset,
				struct binder_object *object)
{
	size_t read_size;
	struct binder_object_header *hdr;
	size_t object_size = 0;

	read_size = min_t(size_t, sizeof(*object), buffer->data_size - offset);
	if (offset > buffer->data_size || read_size < sizeof(*hdr) ||
	    binder_alloc_copy_from_buffer(&proc->alloc, object, buffer,
					  offset, read_size))
		return 0;

	/* Ok, now see if we read a complete object. */
	hdr = &object->hdr;
	switch (hdr->type) {
	case BINDER_TYPE_BINDER:
	case BINDER_TYPE_WEAK_BINDER:
	case BINDER_TYPE_HANDLE:
	case BINDER_TYPE_WEAK_HANDLE:
		object_size = sizeof(struct flat_binder_object);
		break;
	case BINDER_TYPE_FD:
		object_size = sizeof(struct binder_fd_object);
		break;
	case BINDER_TYPE_PTR:
		object_size = sizeof(struct binder_buffer_object);
		break;
	case BINDER_TYPE_FDA:
		object_size = sizeof(struct binder_fd_array_object);
		break;
	default:
		return 0;
	}
	if (offset <= buffer->data_size - object_size &&
	    buffer->data_size >= object_size)
		return object_size;
	else
		return 0;
}

/**
 * binder_validate_ptr() - validates binder_buffer_object in a binder_buffer.
 * @proc:	binder_proc owning the buffer
 * @b:		binder_buffer containing the object
 * @object:	struct binder_object to read into
 * @index:	index in offset array at which the binder_buffer_object is
 *		located
 * @start_offset: points to the start of the offset array
 * @object_offsetp: offset of @object read from @b
 * @num_valid:	the number of valid offsets in the offset array
 *
 * Return:	If @index is within the valid range of the offset array
 *		described by @start and @num_valid, and if there's a valid
 *		binder_buffer_object at the offset found in index @index
 *		of the offset array, that object is returned. Otherwise,
 *		%NULL is returned.
 *		Note that the offset found in index @index itself is not
 *		verified; this function assumes that @num_valid elements
 *		from @start were previously verified to have valid offsets.
 *		If @object_offsetp is non-NULL, then the offset within
 *		@b is written to it.
 */
static struct binder_buffer_object *binder_validate_ptr(
						struct binder_proc *proc,
						struct binder_buffer *b,
						struct binder_object *object,
						binder_size_t index,
						binder_size_t start_offset,
						binder_size_t *object_offsetp,
						binder_size_t num_valid)
{
	size_t object_size;
	binder_size_t object_offset;
	unsigned long buffer_offset;

	if (index >= num_valid)
		return NULL;

	buffer_offset = start_offset + sizeof(binder_size_t) * index;
	if (binder_alloc_copy_from_buffer(&proc->alloc, &object_offset,
					  b, buffer_offset,
					  sizeof(object_offset)))
		return NULL;
	object_size = binder_get_object(proc, b, object_offset, object);
	if (!object_size || object->hdr.type != BINDER_TYPE_PTR)
		return NULL;
	if (object_offsetp)
		*object_offsetp = object_offset;

	return &object->bbo;
}

/**
 * binder_validate_fixup() - validates pointer/fd fixups happen in order.
 * @proc:		binder_proc owning the buffer
 * @b:			transaction buffer
 * @objects_start_offset: offset to start of objects buffer
 * @buffer_obj_offset:	offset to binder_buffer_object in which to fix up
 * @fixup_offset:	start offset in @buffer to fix up
 * @last_obj_offset:	offset to last binder_buffer_object that we fixed
 * @last_min_offset:	minimum fixup offset in object at @last_obj_offset
 *
 * Return:		%true if a fixup in buffer @buffer at offset @offset is
 *			allowed.
 *
 * For safety reasons, we only allow fixups inside a buffer to happen
 * at increasing offsets; additionally, we only allow fixup on the last
 * buffer object that was verified, or one of its parents.
 *
 * Example of what is allowed:
 *
 * A
 *   B (parent = A, offset = 0)
 *   C (parent = A, offset = 16)
 *     D (parent = C, offset = 0)
 *   E (parent = A, offset = 32) // min_offset is 16 (C.parent_offset)
 *
 * Examples of what is not allowed:
 *
 * Decreasing offsets within the same parent:
 * A
 *   C (parent = A, offset = 16)
 *   B (parent = A, offset = 0) // decreasing offset within A
 *
 * Referring to a parent that wasn't the last object or any of its parents:
 * A
 *   B (parent = A, offset = 0)
 *   C (parent = A, offset = 0)
 *   C (parent = A, offset = 16)
 *     D (parent = B, offset = 0) // B is not A or any of A's parents
 */
static bool binder_validate_fixup(struct binder_proc *proc,
				  struct binder_buffer *b,
				  binder_size_t objects_start_offset,
				  binder_size_t buffer_obj_offset,
				  binder_size_t fixup_offset,
				  binder_size_t last_obj_offset,
				  binder_size_t last_min_offset)
{
	if (!last_obj_offset) {
		/* Nothing to fix up in */
		return false;
	}

	while (last_obj_offset != buffer_obj_offset) {
		unsigned long buffer_offset;
		struct binder_object last_object;
		struct binder_buffer_object *last_bbo;
		size_t object_size = binder_get_object(proc, b, last_obj_offset,
						       &last_object);
		if (object_size != sizeof(*last_bbo))
			return false;

		last_bbo = &last_object.bbo;
		/*
		 * Safe to retrieve the parent of last_obj, since it
		 * was already previously verified by the driver.
		 */
		if ((last_bbo->flags & BINDER_BUFFER_FLAG_HAS_PARENT) == 0)
			return false;
		last_min_offset = last_bbo->parent_offset + sizeof(uintptr_t);
		buffer_offset = objects_start_offset +
			sizeof(binder_size_t) * last_bbo->parent;
		if (binder_alloc_copy_from_buffer(&proc->alloc,
						  &last_obj_offset,
						  b, buffer_offset,
						  sizeof(last_obj_offset)))
			return false;
	}
	return (fixup_offset >= last_min_offset);
}

/**
 * struct binder_task_work_cb - for deferred close
 *
 * @twork:                callback_head for task work
 * @fd:                   fd to close
 *
 * Structure to pass task work to be handled after
 * returning from binder_ioctl() via task_work_add().
 */
struct binder_task_work_cb {
	struct callback_head twork;
	struct file *file;
};

/**
 * binder_do_fd_close() - close list of file descriptors
 * @twork:	callback head for task work
 *
 * It is not safe to call ksys_close() during the binder_ioctl()
 * function if there is a chance that binder's own file descriptor
 * might be closed. This is to meet the requirements for using
 * fdget() (see comments for __fget_light()). Therefore use
 * task_work_add() to schedule the close operation once we have
 * returned from binder_ioctl(). This function is a callback
 * for that mechanism and does the actual ksys_close() on the
 * given file descriptor.
 */
static void binder_do_fd_close(struct callback_head *twork)
{
	struct binder_task_work_cb *twcb = container_of(twork,
			struct binder_task_work_cb, twork);

	fput(twcb->file);
	kfree(twcb);
}

/**
 * binder_deferred_fd_close() - schedule a close for the given file-descriptor
 * @fd:		file-descriptor to close
 *
 * See comments in binder_do_fd_close(). This function is used to schedule
 * a file-descriptor to be closed after returning from binder_ioctl().
 */
static void binder_deferred_fd_close(int fd)
{
	struct binder_task_work_cb *twcb;

	twcb = kzalloc(sizeof(*twcb), GFP_KERNEL);
	if (!twcb)
		return;
	init_task_work(&twcb->twork, binder_do_fd_close);
	close_fd_get_file(fd, &twcb->file);
	if (twcb->file) {
		filp_close(twcb->file, current->files);
		task_work_add(current, &twcb->twork, TWA_RESUME);
	} else {
		kfree(twcb);
	}
}

static void binder_transaction_buffer_release(struct binder_proc *proc,
					      struct binder_thread *thread,
					      struct binder_buffer *buffer,
					      binder_size_t failed_at,
					      bool is_failure)
{
	int debug_id = buffer->debug_id;
	binder_size_t off_start_offset, buffer_offset, off_end_offset;

	binder_debug(BINDER_DEBUG_TRANSACTION,
		     "%d buffer release %d, size %zd-%zd, failed at %llx\n",
		     proc->pid, buffer->debug_id,
		     buffer->data_size, buffer->offsets_size,
		     (unsigned long long)failed_at);

	if (buffer->target_node)
		binder_dec_node(buffer->target_node, 1, 0);

	off_start_offset = ALIGN(buffer->data_size, sizeof(void *));
	off_end_offset = is_failure && failed_at ? failed_at :
				off_start_offset + buffer->offsets_size;
	for (buffer_offset = off_start_offset; buffer_offset < off_end_offset;
	     buffer_offset += sizeof(binder_size_t)) {
		struct binder_object_header *hdr;
		size_t object_size = 0;
		struct binder_object object;
		binder_size_t object_offset;

		if (!binder_alloc_copy_from_buffer(&proc->alloc, &object_offset,
						   buffer, buffer_offset,
						   sizeof(object_offset)))
			object_size = binder_get_object(proc, buffer,
							object_offset, &object);
		if (object_size == 0) {
			pr_err("transaction release %d bad object at offset %lld, size %zd\n",
			       debug_id, (u64)object_offset, buffer->data_size);
			continue;
		}
		hdr = &object.hdr;
		switch (hdr->type) {
		case BINDER_TYPE_BINDER:
		case BINDER_TYPE_WEAK_BINDER: {
			struct flat_binder_object *fp;
			struct binder_node *node;

			fp = to_flat_binder_object(hdr);
			node = binder_get_node(proc, fp->binder);
			if (node == NULL) {
				pr_err("transaction release %d bad node %016llx\n",
				       debug_id, (u64)fp->binder);
				break;
			}
			binder_debug(BINDER_DEBUG_TRANSACTION,
				     "        node %d u%016llx\n",
				     node->debug_id, (u64)node->ptr);
			binder_dec_node(node, hdr->type == BINDER_TYPE_BINDER,
					0);
			binder_put_node(node);
		} break;
		case BINDER_TYPE_HANDLE:
		case BINDER_TYPE_WEAK_HANDLE: {
			struct flat_binder_object *fp;
			struct binder_ref_data rdata;
			int ret;

			fp = to_flat_binder_object(hdr);
			ret = binder_dec_ref_for_handle(proc, fp->handle,
				hdr->type == BINDER_TYPE_HANDLE, &rdata);

			if (ret) {
				pr_err("transaction release %d bad handle %d, ret = %d\n",
				 debug_id, fp->handle, ret);
				break;
			}
			binder_debug(BINDER_DEBUG_TRANSACTION,
				     "        ref %d desc %d\n",
				     rdata.debug_id, rdata.desc);
		} break;

		case BINDER_TYPE_FD: {
			/*
			 * No need to close the file here since user-space
			 * closes it for for successfully delivered
			 * transactions. For transactions that weren't
			 * delivered, the new fd was never allocated so
			 * there is no need to close and the fput on the
			 * file is done when the transaction is torn
			 * down.
			 */
		} break;
		case BINDER_TYPE_PTR:
			/*
			 * Nothing to do here, this will get cleaned up when the
			 * transaction buffer gets freed
			 */
			break;
		case BINDER_TYPE_FDA: {
			struct binder_fd_array_object *fda;
			struct binder_buffer_object *parent;
			struct binder_object ptr_object;
			binder_size_t fda_offset;
			size_t fd_index;
			binder_size_t fd_buf_size;
			binder_size_t num_valid;

			if (is_failure) {
				/*
				 * The fd fixups have not been applied so no
				 * fds need to be closed.
				 */
				continue;
			}

			num_valid = (buffer_offset - off_start_offset) /
						sizeof(binder_size_t);
			fda = to_binder_fd_array_object(hdr);
			parent = binder_validate_ptr(proc, buffer, &ptr_object,
						     fda->parent,
						     off_start_offset,
						     NULL,
						     num_valid);
			if (!parent) {
				pr_err("transaction release %d bad parent offset\n",
				       debug_id);
				continue;
			}
			fd_buf_size = sizeof(u32) * fda->num_fds;
			if (fda->num_fds >= SIZE_MAX / sizeof(u32)) {
				pr_err("transaction release %d invalid number of fds (%lld)\n",
				       debug_id, (u64)fda->num_fds);
				continue;
			}
			if (fd_buf_size > parent->length ||
			    fda->parent_offset > parent->length - fd_buf_size) {
				/* No space for all file descriptors here. */
				pr_err("transaction release %d not enough space for %lld fds in buffer\n",
				       debug_id, (u64)fda->num_fds);
				continue;
			}
			/*
			 * the source data for binder_buffer_object is visible
			 * to user-space and the @buffer element is the user
			 * pointer to the buffer_object containing the fd_array.
			 * Convert the address to an offset relative to
			 * the base of the transaction buffer.
			 */
			fda_offset =
			    (parent->buffer - (uintptr_t)buffer->user_data) +
			    fda->parent_offset;
			for (fd_index = 0; fd_index < fda->num_fds;
			     fd_index++) {
				u32 fd;
				int err;
				binder_size_t offset = fda_offset +
					fd_index * sizeof(fd);

				err = binder_alloc_copy_from_buffer(
						&proc->alloc, &fd, buffer,
						offset, sizeof(fd));
				WARN_ON(err);
				if (!err) {
					binder_deferred_fd_close(fd);
					/*
					 * Need to make sure the thread goes
					 * back to userspace to complete the
					 * deferred close
					 */
					if (thread)
						thread->looper_need_return = true;
				}
			}
		} break;
		default:
			pr_err("transaction release %d bad object type %x\n",
				debug_id, hdr->type);
			break;
		}
	}
}

static int binder_translate_binder(struct flat_binder_object *fp,
				   struct binder_transaction *t,
				   struct binder_thread *thread)
{
	struct binder_node *node;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;
	struct binder_ref_data rdata;
	int ret = 0;

	node = binder_get_node(proc, fp->binder);
	if (!node) {
		node = binder_new_node(proc, fp);
		if (!node)
			return -ENOMEM;
	}
	if (fp->cookie != node->cookie) {
		binder_user_error("%d:%d sending u%016llx node %d, cookie mismatch %016llx != %016llx\n",
				  proc->pid, thread->pid, (u64)fp->binder,
				  node->debug_id, (u64)fp->cookie,
				  (u64)node->cookie);
		ret = -EINVAL;
		goto done;
	}
	if (security_binder_transfer_binder(proc->cred, target_proc->cred)) {
		ret = -EPERM;
		goto done;
	}

	ret = binder_inc_ref_for_node(target_proc, node,
			fp->hdr.type == BINDER_TYPE_BINDER,
			&thread->todo, &rdata);
	if (ret)
		goto done;

	if (fp->hdr.type == BINDER_TYPE_BINDER)
		fp->hdr.type = BINDER_TYPE_HANDLE;
	else
		fp->hdr.type = BINDER_TYPE_WEAK_HANDLE;
	fp->binder = 0;
	fp->handle = rdata.desc;
	fp->cookie = 0;

	trace_binder_transaction_node_to_ref(t, node, &rdata);
	binder_debug(BINDER_DEBUG_TRANSACTION,
		     "        node %d u%016llx -> ref %d desc %d\n",
		     node->debug_id, (u64)node->ptr,
		     rdata.debug_id, rdata.desc);
done:
	binder_put_node(node);
	return ret;
}

static int binder_translate_handle(struct flat_binder_object *fp,
				   struct binder_transaction *t,
				   struct binder_thread *thread)
{
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;
	struct binder_node *node;
	struct binder_ref_data src_rdata;
	int ret = 0;

	node = binder_get_node_from_ref(proc, fp->handle,
			fp->hdr.type == BINDER_TYPE_HANDLE, &src_rdata);
	if (!node) {
		binder_user_error("%d:%d got transaction with invalid handle, %d\n",
				  proc->pid, thread->pid, fp->handle);
		return -EINVAL;
	}
	if (security_binder_transfer_binder(proc->cred, target_proc->cred)) {
		ret = -EPERM;
		goto done;
	}

	binder_node_lock(node);
	if (node->proc == target_proc) {
		if (fp->hdr.type == BINDER_TYPE_HANDLE)
			fp->hdr.type = BINDER_TYPE_BINDER;
		else
			fp->hdr.type = BINDER_TYPE_WEAK_BINDER;
		fp->binder = node->ptr;
		fp->cookie = node->cookie;
		if (node->proc)
			binder_inner_proc_lock(node->proc);
		else
			__acquire(&node->proc->inner_lock);
		binder_inc_node_nilocked(node,
					 fp->hdr.type == BINDER_TYPE_BINDER,
					 0, NULL);
		if (node->proc)
			binder_inner_proc_unlock(node->proc);
		else
			__release(&node->proc->inner_lock);
		trace_binder_transaction_ref_to_node(t, node, &src_rdata);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "        ref %d desc %d -> node %d u%016llx\n",
			     src_rdata.debug_id, src_rdata.desc, node->debug_id,
			     (u64)node->ptr);
		binder_node_unlock(node);
	} else {
		struct binder_ref_data dest_rdata;

		binder_node_unlock(node);
		ret = binder_inc_ref_for_node(target_proc, node,
				fp->hdr.type == BINDER_TYPE_HANDLE,
				NULL, &dest_rdata);
		if (ret)
			goto done;

		fp->binder = 0;
		fp->handle = dest_rdata.desc;
		fp->cookie = 0;
		trace_binder_transaction_ref_to_ref(t, node, &src_rdata,
						    &dest_rdata);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "        ref %d desc %d -> ref %d desc %d (node %d)\n",
			     src_rdata.debug_id, src_rdata.desc,
			     dest_rdata.debug_id, dest_rdata.desc,
			     node->debug_id);
	}
done:
	binder_put_node(node);
	return ret;
}

static int binder_translate_fd(u32 fd, binder_size_t fd_offset,
			       struct binder_transaction *t,
			       struct binder_thread *thread,
			       struct binder_transaction *in_reply_to)
{
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;
	struct binder_txn_fd_fixup *fixup;
	struct file *file;
	int ret = 0;
	bool target_allows_fd;

	if (in_reply_to)
		target_allows_fd = !!(in_reply_to->flags & TF_ACCEPT_FDS);
	else
		target_allows_fd = t->buffer->target_node->accept_fds;
	if (!target_allows_fd) {
		binder_user_error("%d:%d got %s with fd, %d, but target does not allow fds\n",
				  proc->pid, thread->pid,
				  in_reply_to ? "reply" : "transaction",
				  fd);
		ret = -EPERM;
		goto err_fd_not_accepted;
	}

	file = fget(fd);
	if (!file) {
		binder_user_error("%d:%d got transaction with invalid fd, %d\n",
				  proc->pid, thread->pid, fd);
		ret = -EBADF;
		goto err_fget;
	}
	ret = security_binder_transfer_file(proc->cred, target_proc->cred, file);
	if (ret < 0) {
		ret = -EPERM;
		goto err_security;
	}

	/*
	 * Add fixup record for this transaction. The allocation
	 * of the fd in the target needs to be done from a
	 * target thread.
	 */
	fixup = kzalloc(sizeof(*fixup), GFP_KERNEL);
	if (!fixup) {
		ret = -ENOMEM;
		goto err_alloc;
	}
	fixup->file = file;
	fixup->offset = fd_offset;
	trace_binder_transaction_fd_send(t, fd, fixup->offset);
	list_add_tail(&fixup->fixup_entry, &t->fd_fixups);

	return ret;

err_alloc:
err_security:
	fput(file);
err_fget:
err_fd_not_accepted:
	return ret;
}

static int binder_translate_fd_array(struct binder_fd_array_object *fda,
				     struct binder_buffer_object *parent,
				     struct binder_transaction *t,
				     struct binder_thread *thread,
				     struct binder_transaction *in_reply_to)
{
	binder_size_t fdi, fd_buf_size;
	binder_size_t fda_offset;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;

	fd_buf_size = sizeof(u32) * fda->num_fds;
	if (fda->num_fds >= SIZE_MAX / sizeof(u32)) {
		binder_user_error("%d:%d got transaction with invalid number of fds (%lld)\n",
				  proc->pid, thread->pid, (u64)fda->num_fds);
		return -EINVAL;
	}
	if (fd_buf_size > parent->length ||
	    fda->parent_offset > parent->length - fd_buf_size) {
		/* No space for all file descriptors here. */
		binder_user_error("%d:%d not enough space to store %lld fds in buffer\n",
				  proc->pid, thread->pid, (u64)fda->num_fds);
		return -EINVAL;
	}
	/*
	 * the source data for binder_buffer_object is visible
	 * to user-space and the @buffer element is the user
	 * pointer to the buffer_object containing the fd_array.
	 * Convert the address to an offset relative to
	 * the base of the transaction buffer.
	 */
	fda_offset = (parent->buffer - (uintptr_t)t->buffer->user_data) +
		fda->parent_offset;
	if (!IS_ALIGNED((unsigned long)fda_offset, sizeof(u32))) {
		binder_user_error("%d:%d parent offset not aligned correctly.\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}
	for (fdi = 0; fdi < fda->num_fds; fdi++) {
		u32 fd;
		int ret;
		binder_size_t offset = fda_offset + fdi * sizeof(fd);

		ret = binder_alloc_copy_from_buffer(&target_proc->alloc,
						    &fd, t->buffer,
						    offset, sizeof(fd));
		if (!ret)
			ret = binder_translate_fd(fd, offset, t, thread,
						  in_reply_to);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int binder_fixup_parent(struct binder_transaction *t,
			       struct binder_thread *thread,
			       struct binder_buffer_object *bp,
			       binder_size_t off_start_offset,
			       binder_size_t num_valid,
			       binder_size_t last_fixup_obj_off,
			       binder_size_t last_fixup_min_off)
{
	struct binder_buffer_object *parent;
	struct binder_buffer *b = t->buffer;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;
	struct binder_object object;
	binder_size_t buffer_offset;
	binder_size_t parent_offset;

	if (!(bp->flags & BINDER_BUFFER_FLAG_HAS_PARENT))
		return 0;

	parent = binder_validate_ptr(target_proc, b, &object, bp->parent,
				     off_start_offset, &parent_offset,
				     num_valid);
	if (!parent) {
		binder_user_error("%d:%d got transaction with invalid parent offset or type\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}

	if (!binder_validate_fixup(target_proc, b, off_start_offset,
				   parent_offset, bp->parent_offset,
				   last_fixup_obj_off,
				   last_fixup_min_off)) {
		binder_user_error("%d:%d got transaction with out-of-order buffer fixup\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}

	if (parent->length < sizeof(binder_uintptr_t) ||
	    bp->parent_offset > parent->length - sizeof(binder_uintptr_t)) {
		/* No space for a pointer here! */
		binder_user_error("%d:%d got transaction with invalid parent offset\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}
	buffer_offset = bp->parent_offset +
			(uintptr_t)parent->buffer - (uintptr_t)b->user_data;
	if (binder_alloc_copy_to_buffer(&target_proc->alloc, b, buffer_offset,
					&bp->buffer, sizeof(bp->buffer))) {
		binder_user_error("%d:%d got transaction with invalid parent offset\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}

	return 0;
}

/**
 * binder_proc_transaction() - sends a transaction to a process and wakes it up
 * @t:		transaction to send
 * @proc:	process to send the transaction to
 * @thread:	thread in @proc to send the transaction to (may be NULL)
 *
 * This function queues a transaction to the specified process. It will try
 * to find a thread in the target process to handle the transaction and
 * wake it up. If no thread is found, the work is queued to the proc
 * waitqueue.
 *
 * If the @thread parameter is not NULL, the transaction is always queued
 * to the waitlist of that specific thread.
 *
 * Return:	0 if the transaction was successfully queued
 *		BR_DEAD_REPLY if the target process or thread is dead
 *		BR_FROZEN_REPLY if the target process or thread is frozen
 */
static int binder_proc_transaction(struct binder_transaction *t,
				    struct binder_proc *proc,
				    struct binder_thread *thread)
{
	struct binder_node *node = t->buffer->target_node;
	bool oneway = !!(t->flags & TF_ONE_WAY);
	bool pending_async = false;

	BUG_ON(!node);
	binder_node_lock(node);
	if (oneway) {
		BUG_ON(thread);
		if (node->has_async_transaction)
			pending_async = true;
		else
			node->has_async_transaction = true;
	}

	binder_inner_proc_lock(proc);
	if (proc->is_frozen) {
		proc->sync_recv |= !oneway;
		proc->async_recv |= oneway;
	}

	if ((proc->is_frozen && !oneway) || proc->is_dead ||
			(thread && thread->is_dead)) {
		binder_inner_proc_unlock(proc);
		binder_node_unlock(node);
		return proc->is_frozen ? BR_FROZEN_REPLY : BR_DEAD_REPLY;
	}

	if (!thread && !pending_async)
		thread = binder_select_thread_ilocked(proc);

	if (thread)
		binder_enqueue_thread_work_ilocked(thread, &t->work);
	else if (!pending_async)
		binder_enqueue_work_ilocked(&t->work, &proc->todo);
	else
		binder_enqueue_work_ilocked(&t->work, &node->async_todo);

	if (!pending_async)
		binder_wakeup_thread_ilocked(proc, thread, !oneway /* sync */);

	proc->outstanding_txns++;
	binder_inner_proc_unlock(proc);
	binder_node_unlock(node);

	return 0;
}

/**
 * binder_get_node_refs_for_txn() - Get required refs on node for txn
 * @node:         struct binder_node for which to get refs
 * @proc:         returns @node->proc if valid
 * @error:        if no @proc then returns BR_DEAD_REPLY
 *
 * User-space normally keeps the node alive when creating a transaction
 * since it has a reference to the target. The local strong ref keeps it
 * alive if the sending process dies before the target process processes
 * the transaction. If the source process is malicious or has a reference
 * counting bug, relying on the local strong ref can fail.
 *
 * Since user-space can cause the local strong ref to go away, we also take
 * a tmpref on the node to ensure it survives while we are constructing
 * the transaction. We also need a tmpref on the proc while we are
 * constructing the transaction, so we take that here as well.
 *
 * Return: The target_node with refs taken or NULL if no @node->proc is NULL.
 * Also sets @proc if valid. If the @node->proc is NULL indicating that the
 * target proc has died, @error is set to BR_DEAD_REPLY
 */
static struct binder_node *binder_get_node_refs_for_txn(
		struct binder_node *node,
		struct binder_proc **procp,
		uint32_t *error)
{
	struct binder_node *target_node = NULL;

	binder_node_inner_lock(node);
	if (node->proc) {
		target_node = node;
		binder_inc_node_nilocked(node, 1, 0, NULL);
		binder_inc_node_tmpref_ilocked(node);
		node->proc->tmp_ref++;
		*procp = node->proc;
	} else
		*error = BR_DEAD_REPLY;
	binder_node_inner_unlock(node);

	return target_node;
}

static void binder_transaction(struct binder_proc *proc,
			       struct binder_thread *thread,
			       struct binder_transaction_data *tr, int reply,
			       binder_size_t extra_buffers_size)
{
	int ret;
	struct binder_transaction *t;
	struct binder_work *w;
	struct binder_work *tcomplete;
	binder_size_t buffer_offset = 0;
	binder_size_t off_start_offset, off_end_offset;
	binder_size_t off_min;
	binder_size_t sg_buf_offset, sg_buf_end_offset;
	struct binder_proc *target_proc = NULL;
	struct binder_thread *target_thread = NULL;
	struct binder_node *target_node = NULL;
	struct binder_transaction *in_reply_to = NULL;
	struct binder_transaction_log_entry *e;
	uint32_t return_error = 0;
	uint32_t return_error_param = 0;
	uint32_t return_error_line = 0;
	binder_size_t last_fixup_obj_off = 0;
	binder_size_t last_fixup_min_off = 0;
	struct binder_context *context = proc->context;
	int t_debug_id = atomic_inc_return(&binder_last_id);
	char *secctx = NULL;
	u32 secctx_sz = 0;

	e = binder_transaction_log_add(&binder_transaction_log);
	e->debug_id = t_debug_id;
	e->call_type = reply ? 2 : !!(tr->flags & TF_ONE_WAY);
	e->from_proc = proc->pid;
	e->from_thread = thread->pid;
	e->target_handle = tr->target.handle;
	e->data_size = tr->data_size;
	e->offsets_size = tr->offsets_size;
	strscpy(e->context_name, proc->context->name, BINDERFS_MAX_NAME);

	if (reply) {
		binder_inner_proc_lock(proc);
		in_reply_to = thread->transaction_stack;
		if (in_reply_to == NULL) {
			binder_inner_proc_unlock(proc);
			binder_user_error("%d:%d got reply transaction with no transaction stack\n",
					  proc->pid, thread->pid);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			goto err_empty_call_stack;
		}
		if (in_reply_to->to_thread != thread) {
			spin_lock(&in_reply_to->lock);
			binder_user_error("%d:%d got reply transaction with bad transaction stack, transaction %d has target %d:%d\n",
				proc->pid, thread->pid, in_reply_to->debug_id,
				in_reply_to->to_proc ?
				in_reply_to->to_proc->pid : 0,
				in_reply_to->to_thread ?
				in_reply_to->to_thread->pid : 0);
			spin_unlock(&in_reply_to->lock);
			binder_inner_proc_unlock(proc);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			in_reply_to = NULL;
			goto err_bad_call_stack;
		}
		thread->transaction_stack = in_reply_to->to_parent;
		binder_inner_proc_unlock(proc);
		binder_set_nice(in_reply_to->saved_priority);
		target_thread = binder_get_txn_from_and_acq_inner(in_reply_to);
		if (target_thread == NULL) {
			/* annotation for sparse */
			__release(&target_thread->proc->inner_lock);
			return_error = BR_DEAD_REPLY;
			return_error_line = __LINE__;
			goto err_dead_binder;
		}
		if (target_thread->transaction_stack != in_reply_to) {
			binder_user_error("%d:%d got reply transaction with bad target transaction stack %d, expected %d\n",
				proc->pid, thread->pid,
				target_thread->transaction_stack ?
				target_thread->transaction_stack->debug_id : 0,
				in_reply_to->debug_id);
			binder_inner_proc_unlock(target_thread->proc);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			in_reply_to = NULL;
			target_thread = NULL;
			goto err_dead_binder;
		}
		target_proc = target_thread->proc;
		target_proc->tmp_ref++;
		binder_inner_proc_unlock(target_thread->proc);
	} else {
		if (tr->target.handle) {
			struct binder_ref *ref;

			/*
			 * There must already be a strong ref
			 * on this node. If so, do a strong
			 * increment on the node to ensure it
			 * stays alive until the transaction is
			 * done.
			 */
			binder_proc_lock(proc);
			ref = binder_get_ref_olocked(proc, tr->target.handle,
						     true);
			if (ref) {
				target_node = binder_get_node_refs_for_txn(
						ref->node, &target_proc,
						&return_error);
			} else {
				binder_user_error("%d:%d got transaction to invalid handle, %u\n",
						  proc->pid, thread->pid, tr->target.handle);
				return_error = BR_FAILED_REPLY;
			}
			binder_proc_unlock(proc);
		} else {
			mutex_lock(&context->context_mgr_node_lock);
			target_node = context->binder_context_mgr_node;
			if (target_node)
				target_node = binder_get_node_refs_for_txn(
						target_node, &target_proc,
						&return_error);
			else
				return_error = BR_DEAD_REPLY;
			mutex_unlock(&context->context_mgr_node_lock);
			if (target_node && target_proc->pid == proc->pid) {
				binder_user_error("%d:%d got transaction to context manager from process owning it\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_invalid_target_handle;
			}
		}
		if (!target_node) {
			/*
			 * return_error is set above
			 */
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_dead_binder;
		}
		e->to_node = target_node->debug_id;
		if (WARN_ON(proc == target_proc)) {
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_invalid_target_handle;
		}
		if (security_binder_transaction(proc->cred,
						target_proc->cred) < 0) {
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPERM;
			return_error_line = __LINE__;
			goto err_invalid_target_handle;
		}
		binder_inner_proc_lock(proc);

		w = list_first_entry_or_null(&thread->todo,
					     struct binder_work, entry);
		if (!(tr->flags & TF_ONE_WAY) && w &&
		    w->type == BINDER_WORK_TRANSACTION) {
			/*
			 * Do not allow new outgoing transaction from a
			 * thread that has a transaction at the head of
			 * its todo list. Only need to check the head
			 * because binder_select_thread_ilocked picks a
			 * thread from proc->waiting_threads to enqueue
			 * the transaction, and nothing is queued to the
			 * todo list while the thread is on waiting_threads.
			 */
			binder_user_error("%d:%d new transaction not allowed when there is a transaction on thread todo\n",
					  proc->pid, thread->pid);
			binder_inner_proc_unlock(proc);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			goto err_bad_todo_list;
		}

		if (!(tr->flags & TF_ONE_WAY) && thread->transaction_stack) {
			struct binder_transaction *tmp;

			tmp = thread->transaction_stack;
			if (tmp->to_thread != thread) {
				spin_lock(&tmp->lock);
				binder_user_error("%d:%d got new transaction with bad transaction stack, transaction %d has target %d:%d\n",
					proc->pid, thread->pid, tmp->debug_id,
					tmp->to_proc ? tmp->to_proc->pid : 0,
					tmp->to_thread ?
					tmp->to_thread->pid : 0);
				spin_unlock(&tmp->lock);
				binder_inner_proc_unlock(proc);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EPROTO;
				return_error_line = __LINE__;
				goto err_bad_call_stack;
			}
			while (tmp) {
				struct binder_thread *from;

				spin_lock(&tmp->lock);
				from = tmp->from;
				if (from && from->proc == target_proc) {
					atomic_inc(&from->tmp_ref);
					target_thread = from;
					spin_unlock(&tmp->lock);
					break;
				}
				spin_unlock(&tmp->lock);
				tmp = tmp->from_parent;
			}
		}
		binder_inner_proc_unlock(proc);
	}
	if (target_thread)
		e->to_thread = target_thread->pid;
	e->to_proc = target_proc->pid;

	/* TODO: reuse incoming transaction for reply */
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL) {
		return_error = BR_FAILED_REPLY;
		return_error_param = -ENOMEM;
		return_error_line = __LINE__;
		goto err_alloc_t_failed;
	}
	INIT_LIST_HEAD(&t->fd_fixups);
	binder_stats_created(BINDER_STAT_TRANSACTION);
	spin_lock_init(&t->lock);

	tcomplete = kzalloc(sizeof(*tcomplete), GFP_KERNEL);
	if (tcomplete == NULL) {
		return_error = BR_FAILED_REPLY;
		return_error_param = -ENOMEM;
		return_error_line = __LINE__;
		goto err_alloc_tcomplete_failed;
	}
	binder_stats_created(BINDER_STAT_TRANSACTION_COMPLETE);

	t->debug_id = t_debug_id;

	if (reply)
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "%d:%d BC_REPLY %d -> %d:%d, data %016llx-%016llx size %lld-%lld-%lld\n",
			     proc->pid, thread->pid, t->debug_id,
			     target_proc->pid, target_thread->pid,
			     (u64)tr->data.ptr.buffer,
			     (u64)tr->data.ptr.offsets,
			     (u64)tr->data_size, (u64)tr->offsets_size,
			     (u64)extra_buffers_size);
	else
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "%d:%d BC_TRANSACTION %d -> %d - node %d, data %016llx-%016llx size %lld-%lld-%lld\n",
			     proc->pid, thread->pid, t->debug_id,
			     target_proc->pid, target_node->debug_id,
			     (u64)tr->data.ptr.buffer,
			     (u64)tr->data.ptr.offsets,
			     (u64)tr->data_size, (u64)tr->offsets_size,
			     (u64)extra_buffers_size);

	if (!reply && !(tr->flags & TF_ONE_WAY))
		t->from = thread;
	else
		t->from = NULL;
	t->sender_euid = proc->cred->euid;
	t->to_proc = target_proc;
	t->to_thread = target_thread;
	t->code = tr->code;
	t->flags = tr->flags;
	t->priority = task_nice(current);

	if (target_node && target_node->txn_security_ctx) {
		u32 secid;
		size_t added_size;

		security_cred_getsecid(proc->cred, &secid);
		ret = security_secid_to_secctx(secid, &secctx, &secctx_sz);
		if (ret) {
			return_error = BR_FAILED_REPLY;
			return_error_param = ret;
			return_error_line = __LINE__;
			goto err_get_secctx_failed;
		}
		added_size = ALIGN(secctx_sz, sizeof(u64));
		extra_buffers_size += added_size;
		if (extra_buffers_size < added_size) {
			/* integer overflow of extra_buffers_size */
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_bad_extra_size;
		}
	}

	trace_binder_transaction(reply, t, target_node);

	t->buffer = binder_alloc_new_buf(&target_proc->alloc, tr->data_size,
		tr->offsets_size, extra_buffers_size,
		!reply && (t->flags & TF_ONE_WAY), current->tgid);
	if (IS_ERR(t->buffer)) {
		/*
		 * -ESRCH indicates VMA cleared. The target is dying.
		 */
		return_error_param = PTR_ERR(t->buffer);
		return_error = return_error_param == -ESRCH ?
			BR_DEAD_REPLY : BR_FAILED_REPLY;
		return_error_line = __LINE__;
		t->buffer = NULL;
		goto err_binder_alloc_buf_failed;
	}
	if (secctx) {
		int err;
		size_t buf_offset = ALIGN(tr->data_size, sizeof(void *)) +
				    ALIGN(tr->offsets_size, sizeof(void *)) +
				    ALIGN(extra_buffers_size, sizeof(void *)) -
				    ALIGN(secctx_sz, sizeof(u64));

		t->security_ctx = (uintptr_t)t->buffer->user_data + buf_offset;
		err = binder_alloc_copy_to_buffer(&target_proc->alloc,
						  t->buffer, buf_offset,
						  secctx, secctx_sz);
		if (err) {
			t->security_ctx = 0;
			WARN_ON(1);
		}
		security_release_secctx(secctx, secctx_sz);
		secctx = NULL;
	}
	t->buffer->debug_id = t->debug_id;
	t->buffer->transaction = t;
	t->buffer->target_node = target_node;
	t->buffer->clear_on_free = !!(t->flags & TF_CLEAR_BUF);
	trace_binder_transaction_alloc_buf(t->buffer);

	if (binder_alloc_copy_user_to_buffer(
				&target_proc->alloc,
				t->buffer, 0,
				(const void __user *)
					(uintptr_t)tr->data.ptr.buffer,
				tr->data_size)) {
		binder_user_error("%d:%d got transaction with invalid data ptr\n",
				proc->pid, thread->pid);
		return_error = BR_FAILED_REPLY;
		return_error_param = -EFAULT;
		return_error_line = __LINE__;
		goto err_copy_data_failed;
	}
	if (binder_alloc_copy_user_to_buffer(
				&target_proc->alloc,
				t->buffer,
				ALIGN(tr->data_size, sizeof(void *)),
				(const void __user *)
					(uintptr_t)tr->data.ptr.offsets,
				tr->offsets_size)) {
		binder_user_error("%d:%d got transaction with invalid offsets ptr\n",
				proc->pid, thread->pid);
		return_error = BR_FAILED_REPLY;
		return_error_param = -EFAULT;
		return_error_line = __LINE__;
		goto err_copy_data_failed;
	}
	if (!IS_ALIGNED(tr->offsets_size, sizeof(binder_size_t))) {
		binder_user_error("%d:%d got transaction with invalid offsets size, %lld\n",
				proc->pid, thread->pid, (u64)tr->offsets_size);
		return_error = BR_FAILED_REPLY;
		return_error_param = -EINVAL;
		return_error_line = __LINE__;
		goto err_bad_offset;
	}
	if (!IS_ALIGNED(extra_buffers_size, sizeof(u64))) {
		binder_user_error("%d:%d got transaction with unaligned buffers size, %lld\n",
				  proc->pid, thread->pid,
				  (u64)extra_buffers_size);
		return_error = BR_FAILED_REPLY;
		return_error_param = -EINVAL;
		return_error_line = __LINE__;
		goto err_bad_offset;
	}
	off_start_offset = ALIGN(tr->data_size, sizeof(void *));
	buffer_offset = off_start_offset;
	off_end_offset = off_start_offset + tr->offsets_size;
	sg_buf_offset = ALIGN(off_end_offset, sizeof(void *));
	sg_buf_end_offset = sg_buf_offset + extra_buffers_size -
		ALIGN(secctx_sz, sizeof(u64));
	off_min = 0;
	for (buffer_offset = off_start_offset; buffer_offset < off_end_offset;
	     buffer_offset += sizeof(binder_size_t)) {
		struct binder_object_header *hdr;
		size_t object_size;
		struct binder_object object;
		binder_size_t object_offset;

		if (binder_alloc_copy_from_buffer(&target_proc->alloc,
						  &object_offset,
						  t->buffer,
						  buffer_offset,
						  sizeof(object_offset))) {
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_bad_offset;
		}
		object_size = binder_get_object(target_proc, t->buffer,
						object_offset, &object);
		if (object_size == 0 || object_offset < off_min) {
			binder_user_error("%d:%d got transaction with invalid offset (%lld, min %lld max %lld) or object.\n",
					  proc->pid, thread->pid,
					  (u64)object_offset,
					  (u64)off_min,
					  (u64)t->buffer->data_size);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_bad_offset;
		}

		hdr = &object.hdr;
		off_min = object_offset + object_size;
		switch (hdr->type) {
		case BINDER_TYPE_BINDER:
		case BINDER_TYPE_WEAK_BINDER: {
			struct flat_binder_object *fp;

			fp = to_flat_binder_object(hdr);
			ret = binder_translate_binder(fp, t, thread);

			if (ret < 0 ||
			    binder_alloc_copy_to_buffer(&target_proc->alloc,
							t->buffer,
							object_offset,
							fp, sizeof(*fp))) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
		} break;
		case BINDER_TYPE_HANDLE:
		case BINDER_TYPE_WEAK_HANDLE: {
			struct flat_binder_object *fp;

			fp = to_flat_binder_object(hdr);
			ret = binder_translate_handle(fp, t, thread);
			if (ret < 0 ||
			    binder_alloc_copy_to_buffer(&target_proc->alloc,
							t->buffer,
							object_offset,
							fp, sizeof(*fp))) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
		} break;

		case BINDER_TYPE_FD: {
			struct binder_fd_object *fp = to_binder_fd_object(hdr);
			binder_size_t fd_offset = object_offset +
				(uintptr_t)&fp->fd - (uintptr_t)fp;
			int ret = binder_translate_fd(fp->fd, fd_offset, t,
						      thread, in_reply_to);

			fp->pad_binder = 0;
			if (ret < 0 ||
			    binder_alloc_copy_to_buffer(&target_proc->alloc,
							t->buffer,
							object_offset,
							fp, sizeof(*fp))) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
		} break;
		case BINDER_TYPE_FDA: {
			struct binder_object ptr_object;
			binder_size_t parent_offset;
			struct binder_fd_array_object *fda =
				to_binder_fd_array_object(hdr);
			size_t num_valid = (buffer_offset - off_start_offset) /
						sizeof(binder_size_t);
			struct binder_buffer_object *parent =
				binder_validate_ptr(target_proc, t->buffer,
						    &ptr_object, fda->parent,
						    off_start_offset,
						    &parent_offset,
						    num_valid);
			if (!parent) {
				binder_user_error("%d:%d got transaction with invalid parent offset or type\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_bad_parent;
			}
			if (!binder_validate_fixup(target_proc, t->buffer,
						   off_start_offset,
						   parent_offset,
						   fda->parent_offset,
						   last_fixup_obj_off,
						   last_fixup_min_off)) {
				binder_user_error("%d:%d got transaction with out-of-order buffer fixup\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_bad_parent;
			}
			ret = binder_translate_fd_array(fda, parent, t, thread,
							in_reply_to);
			if (ret < 0) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
			last_fixup_obj_off = parent_offset;
			last_fixup_min_off =
				fda->parent_offset + sizeof(u32) * fda->num_fds;
		} break;
		case BINDER_TYPE_PTR: {
			struct binder_buffer_object *bp =
				to_binder_buffer_object(hdr);
			size_t buf_left = sg_buf_end_offset - sg_buf_offset;
			size_t num_valid;

			if (bp->length > buf_left) {
				binder_user_error("%d:%d got transaction with too large buffer\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_bad_offset;
			}
			if (binder_alloc_copy_user_to_buffer(
						&target_proc->alloc,
						t->buffer,
						sg_buf_offset,
						(const void __user *)
							(uintptr_t)bp->buffer,
						bp->length)) {
				binder_user_error("%d:%d got transaction with invalid offsets ptr\n",
						  proc->pid, thread->pid);
				return_error_param = -EFAULT;
				return_error = BR_FAILED_REPLY;
				return_error_line = __LINE__;
				goto err_copy_data_failed;
			}
			/* Fixup buffer pointer to target proc address space */
			bp->buffer = (uintptr_t)
				t->buffer->user_data + sg_buf_offset;
			sg_buf_offset += ALIGN(bp->length, sizeof(u64));

			num_valid = (buffer_offset - off_start_offset) /
					sizeof(binder_size_t);
			ret = binder_fixup_parent(t, thread, bp,
						  off_start_offset,
						  num_valid,
						  last_fixup_obj_off,
						  last_fixup_min_off);
			if (ret < 0 ||
			    binder_alloc_copy_to_buffer(&target_proc->alloc,
							t->buffer,
							object_offset,
							bp, sizeof(*bp))) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
			last_fixup_obj_off = object_offset;
			last_fixup_min_off = 0;
		} break;
		default:
			binder_user_error("%d:%d got transaction with invalid object type, %x\n",
				proc->pid, thread->pid, hdr->type);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_bad_object_type;
		}
	}
	if (t->buffer->oneway_spam_suspect)
		tcomplete->type = BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT;
	else
		tcomplete->type = BINDER_WORK_TRANSACTION_COMPLETE;
	t->work.type = BINDER_WORK_TRANSACTION;

	if (reply) {
		binder_enqueue_thread_work(thread, tcomplete);
		binder_inner_proc_lock(target_proc);
		if (target_thread->is_dead) {
			return_error = BR_DEAD_REPLY;
			binder_inner_proc_unlock(target_proc);
			goto err_dead_proc_or_thread;
		}
		BUG_ON(t->buffer->async_transaction != 0);
		binder_pop_transaction_ilocked(target_thread, in_reply_to);
		binder_enqueue_thread_work_ilocked(target_thread, &t->work);
		target_proc->outstanding_txns++;
		binder_inner_proc_unlock(target_proc);
		wake_up_interruptible_sync(&target_thread->wait);
		binder_free_transaction(in_reply_to);
	} else if (!(t->flags & TF_ONE_WAY)) {
		BUG_ON(t->buffer->async_transaction != 0);
		binder_inner_proc_lock(proc);
		/*
		 * Defer the TRANSACTION_COMPLETE, so we don't return to
		 * userspace immediately; this allows the target process to
		 * immediately start processing this transaction, reducing
		 * latency. We will then return the TRANSACTION_COMPLETE when
		 * the target replies (or there is an error).
		 */
		binder_enqueue_deferred_thread_work_ilocked(thread, tcomplete);
		t->need_reply = 1;
		t->from_parent = thread->transaction_stack;
		thread->transaction_stack = t;
		binder_inner_proc_unlock(proc);
		return_error = binder_proc_transaction(t,
				target_proc, target_thread);
		if (return_error) {
			binder_inner_proc_lock(proc);
			binder_pop_transaction_ilocked(thread, t);
			binder_inner_proc_unlock(proc);
			goto err_dead_proc_or_thread;
		}
	} else {
		BUG_ON(target_node == NULL);
		BUG_ON(t->buffer->async_transaction != 1);
		binder_enqueue_thread_work(thread, tcomplete);
		return_error = binder_proc_transaction(t, target_proc, NULL);
		if (return_error)
			goto err_dead_proc_or_thread;
	}
	if (target_thread)
		binder_thread_dec_tmpref(target_thread);
	binder_proc_dec_tmpref(target_proc);
	if (target_node)
		binder_dec_node_tmpref(target_node);
	/*
	 * write barrier to synchronize with initialization
	 * of log entry
	 */
	smp_wmb();
	WRITE_ONCE(e->debug_id_done, t_debug_id);
	return;

err_dead_proc_or_thread:
	return_error_line = __LINE__;
	binder_dequeue_work(proc, tcomplete);
err_translate_failed:
err_bad_object_type:
err_bad_offset:
err_bad_parent:
err_copy_data_failed:
	binder_free_txn_fixups(t);
	trace_binder_transaction_failed_buffer_release(t->buffer);
	binder_transaction_buffer_release(target_proc, NULL, t->buffer,
					  buffer_offset, true);
	if (target_node)
		binder_dec_node_tmpref(target_node);
	target_node = NULL;
	t->buffer->transaction = NULL;
	binder_alloc_free_buf(&target_proc->alloc, t->buffer);
err_binder_alloc_buf_failed:
err_bad_extra_size:
	if (secctx)
		security_release_secctx(secctx, secctx_sz);
err_get_secctx_failed:
	kfree(tcomplete);
	binder_stats_deleted(BINDER_STAT_TRANSACTION_COMPLETE);
err_alloc_tcomplete_failed:
	if (trace_binder_txn_latency_free_enabled())
		binder_txn_latency_free(t);
	kfree(t);
	binder_stats_deleted(BINDER_STAT_TRANSACTION);
err_alloc_t_failed:
err_bad_todo_list:
err_bad_call_stack:
err_empty_call_stack:
err_dead_binder:
err_invalid_target_handle:
	if (target_thread)
		binder_thread_dec_tmpref(target_thread);
	if (target_proc)
		binder_proc_dec_tmpref(target_proc);
	if (target_node) {
		binder_dec_node(target_node, 1, 0);
		binder_dec_node_tmpref(target_node);
	}

	binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
		     "%d:%d transaction failed %d/%d, size %lld-%lld line %d\n",
		     proc->pid, thread->pid, return_error, return_error_param,
		     (u64)tr->data_size, (u64)tr->offsets_size,
		     return_error_line);

	{
		struct binder_transaction_log_entry *fe;

		e->return_error = return_error;
		e->return_error_param = return_error_param;
		e->return_error_line = return_error_line;
		fe = binder_transaction_log_add(&binder_transaction_log_failed);
		*fe = *e;
		/*
		 * write barrier to synchronize with initialization
		 * of log entry
		 */
		smp_wmb();
		WRITE_ONCE(e->debug_id_done, t_debug_id);
		WRITE_ONCE(fe->debug_id_done, t_debug_id);
	}

	BUG_ON(thread->return_error.cmd != BR_OK);
	if (in_reply_to) {
		thread->return_error.cmd = BR_TRANSACTION_COMPLETE;
		binder_enqueue_thread_work(thread, &thread->return_error.work);
		binder_send_failed_reply(in_reply_to, return_error);
	} else {
		thread->return_error.cmd = return_error;
		binder_enqueue_thread_work(thread, &thread->return_error.work);
	}
}

/**
 * binder_free_buf() - free the specified buffer
 * @proc:	binder proc that owns buffer
 * @buffer:	buffer to be freed
 * @is_failure:	failed to send transaction
 *
 * If buffer for an async transaction, enqueue the next async
 * transaction from the node.
 *
 * Cleanup buffer and free it.
 */
static void
binder_free_buf(struct binder_proc *proc,
		struct binder_thread *thread,
		struct binder_buffer *buffer, bool is_failure)
{
	binder_inner_proc_lock(proc);
	if (buffer->transaction) {
		buffer->transaction->buffer = NULL;
		buffer->transaction = NULL;
	}
	binder_inner_proc_unlock(proc);
	if (buffer->async_transaction && buffer->target_node) {
		struct binder_node *buf_node;
		struct binder_work *w;

		buf_node = buffer->target_node;
		binder_node_inner_lock(buf_node);
		BUG_ON(!buf_node->has_async_transaction);
		BUG_ON(buf_node->proc != proc);
		w = binder_dequeue_work_head_ilocked(
				&buf_node->async_todo);
		if (!w) {
			buf_node->has_async_transaction = false;
		} else {
			binder_enqueue_work_ilocked(
					w, &proc->todo);
			binder_wakeup_proc_ilocked(proc);
		}
		binder_node_inner_unlock(buf_node);
	}
	trace_binder_transaction_buffer_release(buffer);
	binder_transaction_buffer_release(proc, thread, buffer, 0, is_failure);
	binder_alloc_free_buf(&proc->alloc, buffer);
}

static int binder_thread_write(struct binder_proc *proc,
			struct binder_thread *thread,
			binder_uintptr_t binder_buffer, size_t size,
			binder_size_t *consumed)
{
	uint32_t cmd;
	struct binder_context *context = proc->context;
	void __user *buffer = (void __user *)(uintptr_t)binder_buffer;
	void __user *ptr = buffer + *consumed;
	void __user *end = buffer + size;

	while (ptr < end && thread->return_error.cmd == BR_OK) {
		int ret;

		if (get_user(cmd, (uint32_t __user *)ptr))
			return -EFAULT;
		ptr += sizeof(uint32_t);
		trace_binder_command(cmd);
		if (_IOC_NR(cmd) < ARRAY_SIZE(binder_stats.bc)) {
			atomic_inc(&binder_stats.bc[_IOC_NR(cmd)]);
			atomic_inc(&proc->stats.bc[_IOC_NR(cmd)]);
			atomic_inc(&thread->stats.bc[_IOC_NR(cmd)]);
		}
		switch (cmd) {
		case BC_INCREFS:
		case BC_ACQUIRE:
		case BC_RELEASE:
		case BC_DECREFS: {
			uint32_t target;
			const char *debug_string;
			bool strong = cmd == BC_ACQUIRE || cmd == BC_RELEASE;
			bool increment = cmd == BC_INCREFS || cmd == BC_ACQUIRE;
			struct binder_ref_data rdata;

			if (get_user(target, (uint32_t __user *)ptr))
				return -EFAULT;

			ptr += sizeof(uint32_t);
			ret = -1;
			if (increment && !target) {
				struct binder_node *ctx_mgr_node;

				mutex_lock(&context->context_mgr_node_lock);
				ctx_mgr_node = context->binder_context_mgr_node;
				if (ctx_mgr_node) {
					if (ctx_mgr_node->proc == proc) {
						binder_user_error("%d:%d context manager tried to acquire desc 0\n",
								  proc->pid, thread->pid);
						mutex_unlock(&context->context_mgr_node_lock);
						return -EINVAL;
					}
					ret = binder_inc_ref_for_node(
							proc, ctx_mgr_node,
							strong, NULL, &rdata);
				}
				mutex_unlock(&context->context_mgr_node_lock);
			}
			if (ret)
				ret = binder_update_ref_for_handle(
						proc, target, increment, strong,
						&rdata);
			if (!ret && rdata.desc != target) {
				binder_user_error("%d:%d tried to acquire reference to desc %d, got %d instead\n",
					proc->pid, thread->pid,
					target, rdata.desc);
			}
			switch (cmd) {
			case BC_INCREFS:
				debug_string = "IncRefs";
				break;
			case BC_ACQUIRE:
				debug_string = "Acquire";
				break;
			case BC_RELEASE:
				debug_string = "Release";
				break;
			case BC_DECREFS:
			default:
				debug_string = "DecRefs";
				break;
			}
			if (ret) {
				binder_user_error("%d:%d %s %d refcount change on invalid ref %d ret %d\n",
					proc->pid, thread->pid, debug_string,
					strong, target, ret);
				break;
			}
			binder_debug(BINDER_DEBUG_USER_REFS,
				     "%d:%d %s ref %d desc %d s %d w %d\n",
				     proc->pid, thread->pid, debug_string,
				     rdata.debug_id, rdata.desc, rdata.strong,
				     rdata.weak);
			break;
		}
		case BC_INCREFS_DONE:
		case BC_ACQUIRE_DONE: {
			binder_uintptr_t node_ptr;
			binder_uintptr_t cookie;
			struct binder_node *node;
			bool free_node;

			if (get_user(node_ptr, (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			if (get_user(cookie, (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			node = binder_get_node(proc, node_ptr);
			if (node == NULL) {
				binder_user_error("%d:%d %s u%016llx no match\n",
					proc->pid, thread->pid,
					cmd == BC_INCREFS_DONE ?
					"BC_INCREFS_DONE" :
					"BC_ACQUIRE_DONE",
					(u64)node_ptr);
				break;
			}
			if (cookie != node->cookie) {
				binder_user_error("%d:%d %s u%016llx node %d cookie mismatch %016llx != %016llx\n",
					proc->pid, thread->pid,
					cmd == BC_INCREFS_DONE ?
					"BC_INCREFS_DONE" : "BC_ACQUIRE_DONE",
					(u64)node_ptr, node->debug_id,
					(u64)cookie, (u64)node->cookie);
				binder_put_node(node);
				break;
			}
			binder_node_inner_lock(node);
			if (cmd == BC_ACQUIRE_DONE) {
				if (node->pending_strong_ref == 0) {
					binder_user_error("%d:%d BC_ACQUIRE_DONE node %d has no pending acquire request\n",
						proc->pid, thread->pid,
						node->debug_id);
					binder_node_inner_unlock(node);
					binder_put_node(node);
					break;
				}
				node->pending_strong_ref = 0;
			} else {
				if (node->pending_weak_ref == 0) {
					binder_user_error("%d:%d BC_INCREFS_DONE node %d has no pending increfs request\n",
						proc->pid, thread->pid,
						node->debug_id);
					binder_node_inner_unlock(node);
					binder_put_node(node);
					break;
				}
				node->pending_weak_ref = 0;
			}
			free_node = binder_dec_node_nilocked(node,
					cmd == BC_ACQUIRE_DONE, 0);
			WARN_ON(free_node);
			binder_debug(BINDER_DEBUG_USER_REFS,
				     "%d:%d %s node %d ls %d lw %d tr %d\n",
				     proc->pid, thread->pid,
				     cmd == BC_INCREFS_DONE ? "BC_INCREFS_DONE" : "BC_ACQUIRE_DONE",
				     node->debug_id, node->local_strong_refs,
				     node->local_weak_refs, node->tmp_refs);
			binder_node_inner_unlock(node);
			binder_put_node(node);
			break;
		}
		case BC_ATTEMPT_ACQUIRE:
			pr_err("BC_ATTEMPT_ACQUIRE not supported\n");
			return -EINVAL;
		case BC_ACQUIRE_RESULT:
			pr_err("BC_ACQUIRE_RESULT not supported\n");
			return -EINVAL;

		case BC_FREE_BUFFER: {
			binder_uintptr_t data_ptr;
			struct binder_buffer *buffer;

			if (get_user(data_ptr, (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);

			buffer = binder_alloc_prepare_to_free(&proc->alloc,
							      data_ptr);
			if (IS_ERR_OR_NULL(buffer)) {
				if (PTR_ERR(buffer) == -EPERM) {
					binder_user_error(
						"%d:%d BC_FREE_BUFFER u%016llx matched unreturned or currently freeing buffer\n",
						proc->pid, thread->pid,
						(u64)data_ptr);
				} else {
					binder_user_error(
						"%d:%d BC_FREE_BUFFER u%016llx no match\n",
						proc->pid, thread->pid,
						(u64)data_ptr);
				}
				break;
			}
			binder_debug(BINDER_DEBUG_FREE_BUFFER,
				     "%d:%d BC_FREE_BUFFER u%016llx found buffer %d for %s transaction\n",
				     proc->pid, thread->pid, (u64)data_ptr,
				     buffer->debug_id,
				     buffer->transaction ? "active" : "finished");
			binder_free_buf(proc, thread, buffer, false);
			break;
		}

		case BC_TRANSACTION_SG:
		case BC_REPLY_SG: {
			struct binder_transaction_data_sg tr;

			if (copy_from_user(&tr, ptr, sizeof(tr)))
				return -EFAULT;
			ptr += sizeof(tr);
			binder_transaction(proc, thread, &tr.transaction_data,
					   cmd == BC_REPLY_SG, tr.buffers_size);
			break;
		}
		case BC_TRANSACTION:
		case BC_REPLY: {
			struct binder_transaction_data tr;

			if (copy_from_user(&tr, ptr, sizeof(tr)))
				return -EFAULT;
			ptr += sizeof(tr);
			binder_transaction(proc, thread, &tr,
					   cmd == BC_REPLY, 0);
			break;
		}

		case BC_REGISTER_LOOPER:
			binder_debug(BINDER_DEBUG_THREADS,
				     "%d:%d BC_REGISTER_LOOPER\n",
				     proc->pid, thread->pid);
			binder_inner_proc_lock(proc);
			if (thread->looper & BINDER_LOOPER_STATE_ENTERED) {
				thread->looper |= BINDER_LOOPER_STATE_INVALID;
				binder_user_error("%d:%d ERROR: BC_REGISTER_LOOPER called after BC_ENTER_LOOPER\n",
					proc->pid, thread->pid);
			} else if (proc->requested_threads == 0) {
				thread->looper |= BINDER_LOOPER_STATE_INVALID;
				binder_user_error("%d:%d ERROR: BC_REGISTER_LOOPER called without request\n",
					proc->pid, thread->pid);
			} else {
				proc->requested_threads--;
				proc->requested_threads_started++;
			}
			thread->looper |= BINDER_LOOPER_STATE_REGISTERED;
			binder_inner_proc_unlock(proc);
			break;
		case BC_ENTER_LOOPER:
			binder_debug(BINDER_DEBUG_THREADS,
				     "%d:%d BC_ENTER_LOOPER\n",
				     proc->pid, thread->pid);
			if (thread->looper & BINDER_LOOPER_STATE_REGISTERED) {
				thread->looper |= BINDER_LOOPER_STATE_INVALID;
				binder_user_error("%d:%d ERROR: BC_ENTER_LOOPER called after BC_REGISTER_LOOPER\n",
					proc->pid, thread->pid);
			}
			thread->looper |= BINDER_LOOPER_STATE_ENTERED;
			break;
		case BC_EXIT_LOOPER:
			binder_debug(BINDER_DEBUG_THREADS,
				     "%d:%d BC_EXIT_LOOPER\n",
				     proc->pid, thread->pid);
			thread->looper |= BINDER_LOOPER_STATE_EXITED;
			break;

		case BC_REQUEST_DEATH_NOTIFICATION:
		case BC_CLEAR_DEATH_NOTIFICATION: {
			uint32_t target;
			binder_uintptr_t cookie;
			struct binder_ref *ref;
			struct binder_ref_death *death = NULL;

			if (get_user(target, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);
			if (get_user(cookie, (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			if (cmd == BC_REQUEST_DEATH_NOTIFICATION) {
				/*
				 * Allocate memory for death notification
				 * before taking lock
				 */
				death = kzalloc(sizeof(*death), GFP_KERNEL);
				if (death == NULL) {
					WARN_ON(thread->return_error.cmd !=
						BR_OK);
					thread->return_error.cmd = BR_ERROR;
					binder_enqueue_thread_work(
						thread,
						&thread->return_error.work);
					binder_debug(
						BINDER_DEBUG_FAILED_TRANSACTION,
						"%d:%d BC_REQUEST_DEATH_NOTIFICATION failed\n",
						proc->pid, thread->pid);
					break;
				}
			}
			binder_proc_lock(proc);
			ref = binder_get_ref_olocked(proc, target, false);
			if (ref == NULL) {
				binder_user_error("%d:%d %s invalid ref %d\n",
					proc->pid, thread->pid,
					cmd == BC_REQUEST_DEATH_NOTIFICATION ?
					"BC_REQUEST_DEATH_NOTIFICATION" :
					"BC_CLEAR_DEATH_NOTIFICATION",
					target);
				binder_proc_unlock(proc);
				kfree(death);
				break;
			}

			binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION,
				     "%d:%d %s %016llx ref %d desc %d s %d w %d for node %d\n",
				     proc->pid, thread->pid,
				     cmd == BC_REQUEST_DEATH_NOTIFICATION ?
				     "BC_REQUEST_DEATH_NOTIFICATION" :
				     "BC_CLEAR_DEATH_NOTIFICATION",
				     (u64)cookie, ref->data.debug_id,
				     ref->data.desc, ref->data.strong,
				     ref->data.weak, ref->node->debug_id);

			binder_node_lock(ref->node);
			if (cmd == BC_REQUEST_DEATH_NOTIFICATION) {
				if (ref->death) {
					binder_user_error("%d:%d BC_REQUEST_DEATH_NOTIFICATION death notification already set\n",
						proc->pid, thread->pid);
					binder_node_unlock(ref->node);
					binder_proc_unlock(proc);
					kfree(death);
					break;
				}
				binder_stats_created(BINDER_STAT_DEATH);
				INIT_LIST_HEAD(&death->work.entry);
				death->cookie = cookie;
				ref->death = death;
				if (ref->node->proc == NULL) {
					ref->death->work.type = BINDER_WORK_DEAD_BINDER;

					binder_inner_proc_lock(proc);
					binder_enqueue_work_ilocked(
						&ref->death->work, &proc->todo);
					binder_wakeup_proc_ilocked(proc);
					binder_inner_proc_unlock(proc);
				}
			} else {
				if (ref->death == NULL) {
					binder_user_error("%d:%d BC_CLEAR_DEATH_NOTIFICATION death notification not active\n",
						proc->pid, thread->pid);
					binder_node_unlock(ref->node);
					binder_proc_unlock(proc);
					break;
				}
				death = ref->death;
				if (death->cookie != cookie) {
					binder_user_error("%d:%d BC_CLEAR_DEATH_NOTIFICATION death notification cookie mismatch %016llx != %016llx\n",
						proc->pid, thread->pid,
						(u64)death->cookie,
						(u64)cookie);
					binder_node_unlock(ref->node);
					binder_proc_unlock(proc);
					break;
				}
				ref->death = NULL;
				binder_inner_proc_lock(proc);
				if (list_empty(&death->work.entry)) {
					death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
					if (thread->looper &
					    (BINDER_LOOPER_STATE_REGISTERED |
					     BINDER_LOOPER_STATE_ENTERED))
						binder_enqueue_thread_work_ilocked(
								thread,
								&death->work);
					else {
						binder_enqueue_work_ilocked(
								&death->work,
								&proc->todo);
						binder_wakeup_proc_ilocked(
								proc);
					}
				} else {
					BUG_ON(death->work.type != BINDER_WORK_DEAD_BINDER);
					death->work.type = BINDER_WORK_DEAD_BINDER_AND_CLEAR;
				}
				binder_inner_proc_unlock(proc);
			}
			binder_node_unlock(ref->node);
			binder_proc_unlock(proc);
		} break;
		case BC_DEAD_BINDER_DONE: {
			struct binder_work *w;
			binder_uintptr_t cookie;
			struct binder_ref_death *death = NULL;

			if (get_user(cookie, (binder_uintptr_t __user *)ptr))
				return -EFAULT;

			ptr += sizeof(cookie);
			binder_inner_proc_lock(proc);
			list_for_each_entry(w, &proc->delivered_death,
					    entry) {
				struct binder_ref_death *tmp_death =
					container_of(w,
						     struct binder_ref_death,
						     work);

				if (tmp_death->cookie == cookie) {
					death = tmp_death;
					break;
				}
			}
			binder_debug(BINDER_DEBUG_DEAD_BINDER,
				     "%d:%d BC_DEAD_BINDER_DONE %016llx found %pK\n",
				     proc->pid, thread->pid, (u64)cookie,
				     death);
			if (death == NULL) {
				binder_user_error("%d:%d BC_DEAD_BINDER_DONE %016llx not found\n",
					proc->pid, thread->pid, (u64)cookie);
				binder_inner_proc_unlock(proc);
				break;
			}
			binder_dequeue_work_ilocked(&death->work);
			if (death->work.type == BINDER_WORK_DEAD_BINDER_AND_CLEAR) {
				death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
				if (thread->looper &
					(BINDER_LOOPER_STATE_REGISTERED |
					 BINDER_LOOPER_STATE_ENTERED))
					binder_enqueue_thread_work_ilocked(
						thread, &death->work);
				else {
					binder_enqueue_work_ilocked(
							&death->work,
							&proc->todo);
					binder_wakeup_proc_ilocked(proc);
				}
			}
			binder_inner_proc_unlock(proc);
		} break;

		default:
			pr_err("%d:%d unknown command %d\n",
			       proc->pid, thread->pid, cmd);
			return -EINVAL;
		}
		*consumed = ptr - buffer;
	}
	return 0;
}

static void binder_stat_br(struct binder_proc *proc,
			   struct binder_thread *thread, uint32_t cmd)
{
	trace_binder_return(cmd);
	if (_IOC_NR(cmd) < ARRAY_SIZE(binder_stats.br)) {
		atomic_inc(&binder_stats.br[_IOC_NR(cmd)]);
		atomic_inc(&proc->stats.br[_IOC_NR(cmd)]);
		atomic_inc(&thread->stats.br[_IOC_NR(cmd)]);
	}
}

static int binder_put_node_cmd(struct binder_proc *proc,
			       struct binder_thread *thread,
			       void __user **ptrp,
			       binder_uintptr_t node_ptr,
			       binder_uintptr_t node_cookie,
			       int node_debug_id,
			       uint32_t cmd, const char *cmd_name)
{
	void __user *ptr = *ptrp;

	if (put_user(cmd, (uint32_t __user *)ptr))
		return -EFAULT;
	ptr += sizeof(uint32_t);

	if (put_user(node_ptr, (binder_uintptr_t __user *)ptr))
		return -EFAULT;
	ptr += sizeof(binder_uintptr_t);

	if (put_user(node_cookie, (binder_uintptr_t __user *)ptr))
		return -EFAULT;
	ptr += sizeof(binder_uintptr_t);

	binder_stat_br(proc, thread, cmd);
	binder_debug(BINDER_DEBUG_USER_REFS, "%d:%d %s %d u%016llx c%016llx\n",
		     proc->pid, thread->pid, cmd_name, node_debug_id,
		     (u64)node_ptr, (u64)node_cookie);

	*ptrp = ptr;
	return 0;
}

static int binder_wait_for_work(struct binder_thread *thread,
				bool do_proc_work)
{
	DEFINE_WAIT(wait);
	struct binder_proc *proc = thread->proc;
	int ret = 0;

	freezer_do_not_count();
	binder_inner_proc_lock(proc);
	for (;;) {
		prepare_to_wait(&thread->wait, &wait, TASK_INTERRUPTIBLE);
		if (binder_has_work_ilocked(thread, do_proc_work))
			break;
		if (do_proc_work)
			list_add(&thread->waiting_thread_node,
				 &proc->waiting_threads);
		binder_inner_proc_unlock(proc);
		schedule();
		binder_inner_proc_lock(proc);
		list_del_init(&thread->waiting_thread_node);
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}
	finish_wait(&thread->wait, &wait);
	binder_inner_proc_unlock(proc);
	freezer_count();

	return ret;
}

/**
 * binder_apply_fd_fixups() - finish fd translation
 * @proc:         binder_proc associated @t->buffer
 * @t:	binder transaction with list of fd fixups
 *
 * Now that we are in the context of the transaction target
 * process, we can allocate and install fds. Process the
 * list of fds to translate and fixup the buffer with the
 * new fds.
 *
 * If we fail to allocate an fd, then free the resources by
 * fput'ing files that have not been processed and ksys_close'ing
 * any fds that have already been allocated.
 */
static int binder_apply_fd_fixups(struct binder_proc *proc,
				  struct binder_transaction *t)
{
	struct binder_txn_fd_fixup *fixup, *tmp;
	int ret = 0;

	list_for_each_entry(fixup, &t->fd_fixups, fixup_entry) {
		int fd = get_unused_fd_flags(O_CLOEXEC);

		if (fd < 0) {
			binder_debug(BINDER_DEBUG_TRANSACTION,
				     "failed fd fixup txn %d fd %d\n",
				     t->debug_id, fd);
			ret = -ENOMEM;
			break;
		}
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "fd fixup txn %d fd %d\n",
			     t->debug_id, fd);
		trace_binder_transaction_fd_recv(t, fd, fixup->offset);
		fd_install(fd, fixup->file);
		fixup->file = NULL;
		if (binder_alloc_copy_to_buffer(&proc->alloc, t->buffer,
						fixup->offset, &fd,
						sizeof(u32))) {
			ret = -EINVAL;
			break;
		}
	}
	list_for_each_entry_safe(fixup, tmp, &t->fd_fixups, fixup_entry) {
		if (fixup->file) {
			fput(fixup->file);
		} else if (ret) {
			u32 fd;
			int err;

			err = binder_alloc_copy_from_buffer(&proc->alloc, &fd,
							    t->buffer,
							    fixup->offset,
							    sizeof(fd));
			WARN_ON(err);
			if (!err)
				binder_deferred_fd_close(fd);
		}
		list_del(&fixup->fixup_entry);
		kfree(fixup);
	}

	return ret;
}

static int binder_thread_read(struct binder_proc *proc,
			      struct binder_thread *thread,
			      binder_uintptr_t binder_buffer, size_t size,
			      binder_size_t *consumed, int non_block)
{
	void __user *buffer = (void __user *)(uintptr_t)binder_buffer;
	void __user *ptr = buffer + *consumed;
	void __user *end = buffer + size;

	int ret = 0;
	int wait_for_proc_work;

	if (*consumed == 0) {
		if (put_user(BR_NOOP, (uint32_t __user *)ptr))
			return -EFAULT;
		ptr += sizeof(uint32_t);
	}

retry:
	binder_inner_proc_lock(proc);
	wait_for_proc_work = binder_available_for_proc_work_ilocked(thread);
	binder_inner_proc_unlock(proc);

	thread->looper |= BINDER_LOOPER_STATE_WAITING;

	trace_binder_wait_for_work(wait_for_proc_work,
				   !!thread->transaction_stack,
				   !binder_worklist_empty(proc, &thread->todo));
	if (wait_for_proc_work) {
		if (!(thread->looper & (BINDER_LOOPER_STATE_REGISTERED |
					BINDER_LOOPER_STATE_ENTERED))) {
			binder_user_error("%d:%d ERROR: Thread waiting for process work before calling BC_REGISTER_LOOPER or BC_ENTER_LOOPER (state %x)\n",
				proc->pid, thread->pid, thread->looper);
			wait_event_interruptible(binder_user_error_wait,
						 binder_stop_on_user_error < 2);
		}
		binder_set_nice(proc->default_priority);
	}

	if (non_block) {
		if (!binder_has_work(thread, wait_for_proc_work))
			ret = -EAGAIN;
	} else {
		ret = binder_wait_for_work(thread, wait_for_proc_work);
	}

	thread->looper &= ~BINDER_LOOPER_STATE_WAITING;

	if (ret)
		return ret;

	while (1) {
		uint32_t cmd;
		struct binder_transaction_data_secctx tr;
		struct binder_transaction_data *trd = &tr.transaction_data;
		struct binder_work *w = NULL;
		struct list_head *list = NULL;
		struct binder_transaction *t = NULL;
		struct binder_thread *t_from;
		size_t trsize = sizeof(*trd);

		binder_inner_proc_lock(proc);
		if (!binder_worklist_empty_ilocked(&thread->todo))
			list = &thread->todo;
		else if (!binder_worklist_empty_ilocked(&proc->todo) &&
			   wait_for_proc_work)
			list = &proc->todo;
		else {
			binder_inner_proc_unlock(proc);

			/* no data added */
			if (ptr - buffer == 4 && !thread->looper_need_return)
				goto retry;
			break;
		}

		if (end - ptr < sizeof(tr) + 4) {
			binder_inner_proc_unlock(proc);
			break;
		}
		w = binder_dequeue_work_head_ilocked(list);
		if (binder_worklist_empty_ilocked(&thread->todo))
			thread->process_todo = false;

		switch (w->type) {
		case BINDER_WORK_TRANSACTION: {
			binder_inner_proc_unlock(proc);
			t = container_of(w, struct binder_transaction, work);
		} break;
		case BINDER_WORK_RETURN_ERROR: {
			struct binder_error *e = container_of(
					w, struct binder_error, work);

			WARN_ON(e->cmd == BR_OK);
			binder_inner_proc_unlock(proc);
			if (put_user(e->cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			cmd = e->cmd;
			e->cmd = BR_OK;
			ptr += sizeof(uint32_t);

			binder_stat_br(proc, thread, cmd);
		} break;
		case BINDER_WORK_TRANSACTION_COMPLETE:
		case BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT: {
			if (proc->oneway_spam_detection_enabled &&
				   w->type == BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT)
				cmd = BR_ONEWAY_SPAM_SUSPECT;
			else
				cmd = BR_TRANSACTION_COMPLETE;
			binder_inner_proc_unlock(proc);
			kfree(w);
			binder_stats_deleted(BINDER_STAT_TRANSACTION_COMPLETE);
			if (put_user(cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);

			binder_stat_br(proc, thread, cmd);
			binder_debug(BINDER_DEBUG_TRANSACTION_COMPLETE,
				     "%d:%d BR_TRANSACTION_COMPLETE\n",
				     proc->pid, thread->pid);
		} break;
		case BINDER_WORK_NODE: {
			struct binder_node *node = container_of(w, struct binder_node, work);
			int strong, weak;
			binder_uintptr_t node_ptr = node->ptr;
			binder_uintptr_t node_cookie = node->cookie;
			int node_debug_id = node->debug_id;
			int has_weak_ref;
			int has_strong_ref;
			void __user *orig_ptr = ptr;

			BUG_ON(proc != node->proc);
			strong = node->internal_strong_refs ||
					node->local_strong_refs;
			weak = !hlist_empty(&node->refs) ||
					node->local_weak_refs ||
					node->tmp_refs || strong;
			has_strong_ref = node->has_strong_ref;
			has_weak_ref = node->has_weak_ref;

			if (weak && !has_weak_ref) {
				node->has_weak_ref = 1;
				node->pending_weak_ref = 1;
				node->local_weak_refs++;
			}
			if (strong && !has_strong_ref) {
				node->has_strong_ref = 1;
				node->pending_strong_ref = 1;
				node->local_strong_refs++;
			}
			if (!strong && has_strong_ref)
				node->has_strong_ref = 0;
			if (!weak && has_weak_ref)
				node->has_weak_ref = 0;
			if (!weak && !strong) {
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "%d:%d node %d u%016llx c%016llx deleted\n",
					     proc->pid, thread->pid,
					     node_debug_id,
					     (u64)node_ptr,
					     (u64)node_cookie);
				rb_erase(&node->rb_node, &proc->nodes);
				binder_inner_proc_unlock(proc);
				binder_node_lock(node);
				/*
				 * Acquire the node lock before freeing the
				 * node to serialize with other threads that
				 * may have been holding the node lock while
				 * decrementing this node (avoids race where
				 * this thread frees while the other thread
				 * is unlocking the node after the final
				 * decrement)
				 */
				binder_node_unlock(node);
				binder_free_node(node);
			} else
				binder_inner_proc_unlock(proc);

			if (weak && !has_weak_ref)
				ret = binder_put_node_cmd(
						proc, thread, &ptr, node_ptr,
						node_cookie, node_debug_id,
						BR_INCREFS, "BR_INCREFS");
			if (!ret && strong && !has_strong_ref)
				ret = binder_put_node_cmd(
						proc, thread, &ptr, node_ptr,
						node_cookie, node_debug_id,
						BR_ACQUIRE, "BR_ACQUIRE");
			if (!ret && !strong && has_strong_ref)
				ret = binder_put_node_cmd(
						proc, thread, &ptr, node_ptr,
						node_cookie, node_debug_id,
						BR_RELEASE, "BR_RELEASE");
			if (!ret && !weak && has_weak_ref)
				ret = binder_put_node_cmd(
						proc, thread, &ptr, node_ptr,
						node_cookie, node_debug_id,
						BR_DECREFS, "BR_DECREFS");
			if (orig_ptr == ptr)
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "%d:%d node %d u%016llx c%016llx state unchanged\n",
					     proc->pid, thread->pid,
					     node_debug_id,
					     (u64)node_ptr,
					     (u64)node_cookie);
			if (ret)
				return ret;
		} break;
		case BINDER_WORK_DEAD_BINDER:
		case BINDER_WORK_DEAD_BINDER_AND_CLEAR:
		case BINDER_WORK_CLEAR_DEATH_NOTIFICATION: {
			struct binder_ref_death *death;
			uint32_t cmd;
			binder_uintptr_t cookie;

			death = container_of(w, struct binder_ref_death, work);
			if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION)
				cmd = BR_CLEAR_DEATH_NOTIFICATION_DONE;
			else
				cmd = BR_DEAD_BINDER;
			cookie = death->cookie;

			binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION,
				     "%d:%d %s %016llx\n",
				      proc->pid, thread->pid,
				      cmd == BR_DEAD_BINDER ?
				      "BR_DEAD_BINDER" :
				      "BR_CLEAR_DEATH_NOTIFICATION_DONE",
				      (u64)cookie);
			if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION) {
				binder_inner_proc_unlock(proc);
				kfree(death);
				binder_stats_deleted(BINDER_STAT_DEATH);
			} else {
				binder_enqueue_work_ilocked(
						w, &proc->delivered_death);
				binder_inner_proc_unlock(proc);
			}
			if (put_user(cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);
			if (put_user(cookie,
				     (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			binder_stat_br(proc, thread, cmd);
			if (cmd == BR_DEAD_BINDER)
				goto done; /* DEAD_BINDER notifications can cause transactions */
		} break;
		default:
			binder_inner_proc_unlock(proc);
			pr_err("%d:%d: bad work type %d\n",
			       proc->pid, thread->pid, w->type);
			break;
		}

		if (!t)
			continue;

		BUG_ON(t->buffer == NULL);
		if (t->buffer->target_node) {
			struct binder_node *target_node = t->buffer->target_node;

			trd->target.ptr = target_node->ptr;
			trd->cookie =  target_node->cookie;
			t->saved_priority = task_nice(current);
			if (t->priority < target_node->min_priority &&
			    !(t->flags & TF_ONE_WAY))
				binder_set_nice(t->priority);
			else if (!(t->flags & TF_ONE_WAY) ||
				 t->saved_priority > target_node->min_priority)
				binder_set_nice(target_node->min_priority);
			cmd = BR_TRANSACTION;
		} else {
			trd->target.ptr = 0;
			trd->cookie = 0;
			cmd = BR_REPLY;
		}
		trd->code = t->code;
		trd->flags = t->flags;
		trd->sender_euid = from_kuid(current_user_ns(), t->sender_euid);

		t_from = binder_get_txn_from(t);
		if (t_from) {
			struct task_struct *sender = t_from->proc->tsk;

			trd->sender_pid =
				task_tgid_nr_ns(sender,
						task_active_pid_ns(current));
		} else {
			trd->sender_pid = 0;
		}

		ret = binder_apply_fd_fixups(proc, t);
		if (ret) {
			struct binder_buffer *buffer = t->buffer;
			bool oneway = !!(t->flags & TF_ONE_WAY);
			int tid = t->debug_id;

			if (t_from)
				binder_thread_dec_tmpref(t_from);
			buffer->transaction = NULL;
			binder_cleanup_transaction(t, "fd fixups failed",
						   BR_FAILED_REPLY);
			binder_free_buf(proc, thread, buffer, true);
			binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
				     "%d:%d %stransaction %d fd fixups failed %d/%d, line %d\n",
				     proc->pid, thread->pid,
				     oneway ? "async " :
					(cmd == BR_REPLY ? "reply " : ""),
				     tid, BR_FAILED_REPLY, ret, __LINE__);
			if (cmd == BR_REPLY) {
				cmd = BR_FAILED_REPLY;
				if (put_user(cmd, (uint32_t __user *)ptr))
					return -EFAULT;
				ptr += sizeof(uint32_t);
				binder_stat_br(proc, thread, cmd);
				break;
			}
			continue;
		}
		trd->data_size = t->buffer->data_size;
		trd->offsets_size = t->buffer->offsets_size;
		trd->data.ptr.buffer = (uintptr_t)t->buffer->user_data;
		trd->data.ptr.offsets = trd->data.ptr.buffer +
					ALIGN(t->buffer->data_size,
					    sizeof(void *));

		tr.secctx = t->security_ctx;
		if (t->security_ctx) {
			cmd = BR_TRANSACTION_SEC_CTX;
			trsize = sizeof(tr);
		}
		if (put_user(cmd, (uint32_t __user *)ptr)) {
			if (t_from)
				binder_thread_dec_tmpref(t_from);

			binder_cleanup_transaction(t, "put_user failed",
						   BR_FAILED_REPLY);

			return -EFAULT;
		}
		ptr += sizeof(uint32_t);
		if (copy_to_user(ptr, &tr, trsize)) {
			if (t_from)
				binder_thread_dec_tmpref(t_from);

			binder_cleanup_transaction(t, "copy_to_user failed",
						   BR_FAILED_REPLY);

			return -EFAULT;
		}
		ptr += trsize;

		trace_binder_transaction_received(t);
		binder_stat_br(proc, thread, cmd);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "%d:%d %s %d %d:%d, cmd %d size %zd-%zd ptr %016llx-%016llx\n",
			     proc->pid, thread->pid,
			     (cmd == BR_TRANSACTION) ? "BR_TRANSACTION" :
				(cmd == BR_TRANSACTION_SEC_CTX) ?
				     "BR_TRANSACTION_SEC_CTX" : "BR_REPLY",
			     t->debug_id, t_from ? t_from->proc->pid : 0,
			     t_from ? t_from->pid : 0, cmd,
			     t->buffer->data_size, t->buffer->offsets_size,
			     (u64)trd->data.ptr.buffer,
			     (u64)trd->data.ptr.offsets);

		if (t_from)
			binder_thread_dec_tmpref(t_from);
		t->buffer->allow_user_free = 1;
		if (cmd != BR_REPLY && !(t->flags & TF_ONE_WAY)) {
			binder_inner_proc_lock(thread->proc);
			t->to_parent = thread->transaction_stack;
			t->to_thread = thread;
			thread->transaction_stack = t;
			binder_inner_proc_unlock(thread->proc);
		} else {
			binder_free_transaction(t);
		}
		break;
	}

done:

	*consumed = ptr - buffer;
	binder_inner_proc_lock(proc);
	if (proc->requested_threads == 0 &&
	    list_empty(&thread->proc->waiting_threads) &&
	    proc->requested_threads_started < proc->max_threads &&
	    (thread->looper & (BINDER_LOOPER_STATE_REGISTERED |
	     BINDER_LOOPER_STATE_ENTERED)) /* the user-space code fails to */
	     /*spawn a new thread if we leave this out */) {
		proc->requested_threads++;
		binder_inner_proc_unlock(proc);
		binder_debug(BINDER_DEBUG_THREADS,
			     "%d:%d BR_SPAWN_LOOPER\n",
			     proc->pid, thread->pid);
		if (put_user(BR_SPAWN_LOOPER, (uint32_t __user *)buffer))
			return -EFAULT;
		binder_stat_br(proc, thread, BR_SPAWN_LOOPER);
	} else
		binder_inner_proc_unlock(proc);
	return 0;
}

static void binder_release_work(struct binder_proc *proc,
				struct list_head *list)
{
	struct binder_work *w;
	enum binder_work_type wtype;

	while (1) {
		binder_inner_proc_lock(proc);
		w = binder_dequeue_work_head_ilocked(list);
		wtype = w ? w->type : 0;
		binder_inner_proc_unlock(proc);
		if (!w)
			return;

		switch (wtype) {
		case BINDER_WORK_TRANSACTION: {
			struct binder_transaction *t;

			t = container_of(w, struct binder_transaction, work);

			binder_cleanup_transaction(t, "process died.",
						   BR_DEAD_REPLY);
		} break;
		case BINDER_WORK_RETURN_ERROR: {
			struct binder_error *e = container_of(
					w, struct binder_error, work);

			binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
				"undelivered TRANSACTION_ERROR: %u\n",
				e->cmd);
		} break;
		case BINDER_WORK_TRANSACTION_COMPLETE: {
			binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
				"undelivered TRANSACTION_COMPLETE\n");
			kfree(w);
			binder_stats_deleted(BINDER_STAT_TRANSACTION_COMPLETE);
		} break;
		case BINDER_WORK_DEAD_BINDER_AND_CLEAR:
		case BINDER_WORK_CLEAR_DEATH_NOTIFICATION: {
			struct binder_ref_death *death;

			death = container_of(w, struct binder_ref_death, work);
			binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
				"undelivered death notification, %016llx\n",
				(u64)death->cookie);
			kfree(death);
			binder_stats_deleted(BINDER_STAT_DEATH);
		} break;
		case BINDER_WORK_NODE:
			break;
		default:
			pr_err("unexpected work type, %d, not freed\n",
			       wtype);
			break;
		}
	}

}

static struct binder_thread *binder_get_thread_ilocked(
		struct binder_proc *proc, struct binder_thread *new_thread)
{
	struct binder_thread *thread = NULL;
	struct rb_node *parent = NULL;
	struct rb_node **p = &proc->threads.rb_node;

	while (*p) {
		parent = *p;
		thread = rb_entry(parent, struct binder_thread, rb_node);

		if (current->pid < thread->pid)
			p = &(*p)->rb_left;
		else if (current->pid > thread->pid)
			p = &(*p)->rb_right;
		else
			return thread;
	}
	if (!new_thread)
		return NULL;
	thread = new_thread;
	binder_stats_created(BINDER_STAT_THREAD);
	thread->proc = proc;
	thread->pid = current->pid;
	atomic_set(&thread->tmp_ref, 0);
	init_waitqueue_head(&thread->wait);
	INIT_LIST_HEAD(&thread->todo);
	rb_link_node(&thread->rb_node, parent, p);
	rb_insert_color(&thread->rb_node, &proc->threads);
	thread->looper_need_return = true;
	thread->return_error.work.type = BINDER_WORK_RETURN_ERROR;
	thread->return_error.cmd = BR_OK;
	thread->reply_error.work.type = BINDER_WORK_RETURN_ERROR;
	thread->reply_error.cmd = BR_OK;
	INIT_LIST_HEAD(&new_thread->waiting_thread_node);
	return thread;
}

static struct binder_thread *binder_get_thread(struct binder_proc *proc)
{
	struct binder_thread *thread;
	struct binder_thread *new_thread;

	binder_inner_proc_lock(proc);
	thread = binder_get_thread_ilocked(proc, NULL);
	binder_inner_proc_unlock(proc);
	if (!thread) {
		new_thread = kzalloc(sizeof(*thread), GFP_KERNEL);
		if (new_thread == NULL)
			return NULL;
		binder_inner_proc_lock(proc);
		thread = binder_get_thread_ilocked(proc, new_thread);
		binder_inner_proc_unlock(proc);
		if (thread != new_thread)
			kfree(new_thread);
	}
	return thread;
}

static void binder_free_proc(struct binder_proc *proc)
{
	struct binder_device *device;

	BUG_ON(!list_empty(&proc->todo));
	BUG_ON(!list_empty(&proc->delivered_death));
	if (proc->outstanding_txns)
		pr_warn("%s: Unexpected outstanding_txns %d\n",
			__func__, proc->outstanding_txns);
	device = container_of(proc->context, struct binder_device, context);
	if (refcount_dec_and_test(&device->ref)) {
		kfree(proc->context->name);
		kfree(device);
	}
	binder_alloc_deferred_release(&proc->alloc);
	put_task_struct(proc->tsk);
	put_cred(proc->cred);
	binder_stats_deleted(BINDER_STAT_PROC);
	kfree(proc);
}

static void binder_free_thread(struct binder_thread *thread)
{
	BUG_ON(!list_empty(&thread->todo));
	binder_stats_deleted(BINDER_STAT_THREAD);
	binder_proc_dec_tmpref(thread->proc);
	kfree(thread);
}

static int binder_thread_release(struct binder_proc *proc,
				 struct binder_thread *thread)
{
	struct binder_transaction *t;
	struct binder_transaction *send_reply = NULL;
	int active_transactions = 0;
	struct binder_transaction *last_t = NULL;

	binder_inner_proc_lock(thread->proc);
	/*
	 * take a ref on the proc so it survives
	 * after we remove this thread from proc->threads.
	 * The corresponding dec is when we actually
	 * free the thread in binder_free_thread()
	 */
	proc->tmp_ref++;
	/*
	 * take a ref on this thread to ensure it
	 * survives while we are releasing it
	 */
	atomic_inc(&thread->tmp_ref);
	rb_erase(&thread->rb_node, &proc->threads);
	t = thread->transaction_stack;
	if (t) {
		spin_lock(&t->lock);
		if (t->to_thread == thread)
			send_reply = t;
	} else {
		__acquire(&t->lock);
	}
	thread->is_dead = true;

	while (t) {
		last_t = t;
		active_transactions++;
		binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
			     "release %d:%d transaction %d %s, still active\n",
			      proc->pid, thread->pid,
			     t->debug_id,
			     (t->to_thread == thread) ? "in" : "out");

		if (t->to_thread == thread) {
			thread->proc->outstanding_txns--;
			t->to_proc = NULL;
			t->to_thread = NULL;
			if (t->buffer) {
				t->buffer->transaction = NULL;
				t->buffer = NULL;
			}
			t = t->to_parent;
		} else if (t->from == thread) {
			t->from = NULL;
			t = t->from_parent;
		} else
			BUG();
		spin_unlock(&last_t->lock);
		if (t)
			spin_lock(&t->lock);
		else
			__acquire(&t->lock);
	}
	/* annotation for sparse, lock not acquired in last iteration above */
	__release(&t->lock);

	/*
	 * If this thread used poll, make sure we remove the waitqueue
	 * from any epoll data structures holding it with POLLFREE.
	 * waitqueue_active() is safe to use here because we're holding
	 * the inner lock.
	 */
	if ((thread->looper & BINDER_LOOPER_STATE_POLL) &&
	    waitqueue_active(&thread->wait)) {
		wake_up_poll(&thread->wait, EPOLLHUP | POLLFREE);
	}

	binder_inner_proc_unlock(thread->proc);

	/*
	 * This is needed to avoid races between wake_up_poll() above and
	 * and ep_remove_waitqueue() called for other reasons (eg the epoll file
	 * descriptor being closed); ep_remove_waitqueue() holds an RCU read
	 * lock, so we can be sure it's done after calling synchronize_rcu().
	 */
	if (thread->looper & BINDER_LOOPER_STATE_POLL)
		synchronize_rcu();

	if (send_reply)
		binder_send_failed_reply(send_reply, BR_DEAD_REPLY);
	binder_release_work(proc, &thread->todo);
	binder_thread_dec_tmpref(thread);
	return active_transactions;
}

static __poll_t binder_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	struct binder_proc *proc = filp->private_data;
	struct binder_thread *thread = NULL;
	bool wait_for_proc_work;

	thread = binder_get_thread(proc);
	if (!thread)
		return POLLERR;

	binder_inner_proc_lock(thread->proc);
	thread->looper |= BINDER_LOOPER_STATE_POLL;
	wait_for_proc_work = binder_available_for_proc_work_ilocked(thread);

	binder_inner_proc_unlock(thread->proc);

	poll_wait(filp, &thread->wait, wait);

	if (binder_has_work(thread, wait_for_proc_work))
		return EPOLLIN;

	return 0;
}

static int binder_ioctl_write_read(struct file *filp,
				unsigned int cmd, unsigned long arg,
				struct binder_thread *thread)
{
	int ret = 0;
	struct binder_proc *proc = filp->private_data;
	unsigned int size = _IOC_SIZE(cmd);
	void __user *ubuf = (void __user *)arg;
	struct binder_write_read bwr;

	if (size != sizeof(struct binder_write_read)) {
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(&bwr, ubuf, sizeof(bwr))) {
		ret = -EFAULT;
		goto out;
	}
	binder_debug(BINDER_DEBUG_READ_WRITE,
		     "%d:%d write %lld at %016llx, read %lld at %016llx\n",
		     proc->pid, thread->pid,
		     (u64)bwr.write_size, (u64)bwr.write_buffer,
		     (u64)bwr.read_size, (u64)bwr.read_buffer);

	if (bwr.write_size > 0) {
		ret = binder_thread_write(proc, thread,
					  bwr.write_buffer,
					  bwr.write_size,
					  &bwr.write_consumed);
		trace_binder_write_done(ret);
		if (ret < 0) {
			bwr.read_consumed = 0;
			if (copy_to_user(ubuf, &bwr, sizeof(bwr)))
				ret = -EFAULT;
			goto out;
		}
	}
	if (bwr.read_size > 0) {
		ret = binder_thread_read(proc, thread, bwr.read_buffer,
					 bwr.read_size,
					 &bwr.read_consumed,
					 filp->f_flags & O_NONBLOCK);
		trace_binder_read_done(ret);
		binder_inner_proc_lock(proc);
		if (!binder_worklist_empty_ilocked(&proc->todo))
			binder_wakeup_proc_ilocked(proc);
		binder_inner_proc_unlock(proc);
		if (ret < 0) {
			if (copy_to_user(ubuf, &bwr, sizeof(bwr)))
				ret = -EFAULT;
			goto out;
		}
	}
	binder_debug(BINDER_DEBUG_READ_WRITE,
		     "%d:%d wrote %lld of %lld, read return %lld of %lld\n",
		     proc->pid, thread->pid,
		     (u64)bwr.write_consumed, (u64)bwr.write_size,
		     (u64)bwr.read_consumed, (u64)bwr.read_size);
	if (copy_to_user(ubuf, &bwr, sizeof(bwr))) {
		ret = -EFAULT;
		goto out;
	}
out:
	return ret;
}

static int binder_ioctl_set_ctx_mgr(struct file *filp,
				    struct flat_binder_object *fbo)
{
	int ret = 0;
	struct binder_proc *proc = filp->private_data;
	struct binder_context *context = proc->context;
	struct binder_node *new_node;
	kuid_t curr_euid = current_euid();

	mutex_lock(&context->context_mgr_node_lock);
	if (context->binder_context_mgr_node) {
		pr_err("BINDER_SET_CONTEXT_MGR already set\n");
		ret = -EBUSY;
		goto out;
	}
	ret = security_binder_set_context_mgr(proc->cred);
	if (ret < 0)
		goto out;
	if (uid_valid(context->binder_context_mgr_uid)) {
		if (!uid_eq(context->binder_context_mgr_uid, curr_euid)) {
			pr_err("BINDER_SET_CONTEXT_MGR bad uid %d != %d\n",
			       from_kuid(&init_user_ns, curr_euid),
			       from_kuid(&init_user_ns,
					 context->binder_context_mgr_uid));
			ret = -EPERM;
			goto out;
		}
	} else {
		context->binder_context_mgr_uid = curr_euid;
	}
	new_node = binder_new_node(proc, fbo);
	if (!new_node) {
		ret = -ENOMEM;
		goto out;
	}
	binder_node_lock(new_node);
	new_node->local_weak_refs++;
	new_node->local_strong_refs++;
	new_node->has_strong_ref = 1;
	new_node->has_weak_ref = 1;
	context->binder_context_mgr_node = new_node;
	binder_node_unlock(new_node);
	binder_put_node(new_node);
out:
	mutex_unlock(&context->context_mgr_node_lock);
	return ret;
}

static int binder_ioctl_get_node_info_for_ref(struct binder_proc *proc,
		struct binder_node_info_for_ref *info)
{
	struct binder_node *node;
	struct binder_context *context = proc->context;
	__u32 handle = info->handle;

	if (info->strong_count || info->weak_count || info->reserved1 ||
	    info->reserved2 || info->reserved3) {
		binder_user_error("%d BINDER_GET_NODE_INFO_FOR_REF: only handle may be non-zero.",
				  proc->pid);
		return -EINVAL;
	}

	/* This ioctl may only be used by the context manager */
	mutex_lock(&context->context_mgr_node_lock);
	if (!context->binder_context_mgr_node ||
		context->binder_context_mgr_node->proc != proc) {
		mutex_unlock(&context->context_mgr_node_lock);
		return -EPERM;
	}
	mutex_unlock(&context->context_mgr_node_lock);

	node = binder_get_node_from_ref(proc, handle, true, NULL);
	if (!node)
		return -EINVAL;

	info->strong_count = node->local_strong_refs +
		node->internal_strong_refs;
	info->weak_count = node->local_weak_refs;

	binder_put_node(node);

	return 0;
}

static int binder_ioctl_get_node_debug_info(struct binder_proc *proc,
				struct binder_node_debug_info *info)
{
	struct rb_node *n;
	binder_uintptr_t ptr = info->ptr;

	memset(info, 0, sizeof(*info));

	binder_inner_proc_lock(proc);
	for (n = rb_first(&proc->nodes); n != NULL; n = rb_next(n)) {
		struct binder_node *node = rb_entry(n, struct binder_node,
						    rb_node);
		if (node->ptr > ptr) {
			info->ptr = node->ptr;
			info->cookie = node->cookie;
			info->has_strong_ref = node->has_strong_ref;
			info->has_weak_ref = node->has_weak_ref;
			break;
		}
	}
	binder_inner_proc_unlock(proc);

	return 0;
}

static bool binder_txns_pending_ilocked(struct binder_proc *proc)
{
	struct rb_node *n;
	struct binder_thread *thread;

	if (proc->outstanding_txns > 0)
		return true;

	for (n = rb_first(&proc->threads); n; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		if (thread->transaction_stack)
			return true;
	}
	return false;
}

static int binder_ioctl_freeze(struct binder_freeze_info *info,
			       struct binder_proc *target_proc)
{
	int ret = 0;

	if (!info->enable) {
		binder_inner_proc_lock(target_proc);
		target_proc->sync_recv = false;
		target_proc->async_recv = false;
		target_proc->is_frozen = false;
		binder_inner_proc_unlock(target_proc);
		return 0;
	}

	/*
	 * Freezing the target. Prevent new transactions by
	 * setting frozen state. If timeout specified, wait
	 * for transactions to drain.
	 */
	binder_inner_proc_lock(target_proc);
	target_proc->sync_recv = false;
	target_proc->async_recv = false;
	target_proc->is_frozen = true;
	binder_inner_proc_unlock(target_proc);

	if (info->timeout_ms > 0)
		ret = wait_event_interruptible_timeout(
			target_proc->freeze_wait,
			(!target_proc->outstanding_txns),
			msecs_to_jiffies(info->timeout_ms));

	/* Check pending transactions that wait for reply */
	if (ret >= 0) {
		binder_inner_proc_lock(target_proc);
		if (binder_txns_pending_ilocked(target_proc))
			ret = -EAGAIN;
		binder_inner_proc_unlock(target_proc);
	}

	if (ret < 0) {
		binder_inner_proc_lock(target_proc);
		target_proc->is_frozen = false;
		binder_inner_proc_unlock(target_proc);
	}

	return ret;
}

static int binder_ioctl_get_freezer_info(
				struct binder_frozen_status_info *info)
{
	struct binder_proc *target_proc;
	bool found = false;
	__u32 txns_pending;

	info->sync_recv = 0;
	info->async_recv = 0;

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(target_proc, &binder_procs, proc_node) {
		if (target_proc->pid == info->pid) {
			found = true;
			binder_inner_proc_lock(target_proc);
			txns_pending = binder_txns_pending_ilocked(target_proc);
			info->sync_recv |= target_proc->sync_recv |
					(txns_pending << 1);
			info->async_recv |= target_proc->async_recv;
			binder_inner_proc_unlock(target_proc);
		}
	}
	mutex_unlock(&binder_procs_lock);

	if (!found)
		return -EINVAL;

	return 0;
}

static long binder_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct binder_proc *proc = filp->private_data;
	struct binder_thread *thread;
	unsigned int size = _IOC_SIZE(cmd);
	void __user *ubuf = (void __user *)arg;

	/*pr_info("binder_ioctl: %d:%d %x %lx\n",
			proc->pid, current->pid, cmd, arg);*/

	binder_selftest_alloc(&proc->alloc);

	trace_binder_ioctl(cmd, arg);

	ret = wait_event_interruptible(binder_user_error_wait, binder_stop_on_user_error < 2);
	if (ret)
		goto err_unlocked;

	thread = binder_get_thread(proc);
	if (thread == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	switch (cmd) {
	case BINDER_WRITE_READ:
		ret = binder_ioctl_write_read(filp, cmd, arg, thread);
		if (ret)
			goto err;
		break;
	case BINDER_SET_MAX_THREADS: {
		int max_threads;

		if (copy_from_user(&max_threads, ubuf,
				   sizeof(max_threads))) {
			ret = -EINVAL;
			goto err;
		}
		binder_inner_proc_lock(proc);
		proc->max_threads = max_threads;
		binder_inner_proc_unlock(proc);
		break;
	}
	case BINDER_SET_CONTEXT_MGR_EXT: {
		struct flat_binder_object fbo;

		if (copy_from_user(&fbo, ubuf, sizeof(fbo))) {
			ret = -EINVAL;
			goto err;
		}
		ret = binder_ioctl_set_ctx_mgr(filp, &fbo);
		if (ret)
			goto err;
		break;
	}
	case BINDER_SET_CONTEXT_MGR:
		ret = binder_ioctl_set_ctx_mgr(filp, NULL);
		if (ret)
			goto err;
		break;
	case BINDER_THREAD_EXIT:
		binder_debug(BINDER_DEBUG_THREADS, "%d:%d exit\n",
			     proc->pid, thread->pid);
		binder_thread_release(proc, thread);
		thread = NULL;
		break;
	case BINDER_VERSION: {
		struct binder_version __user *ver = ubuf;

		if (size != sizeof(struct binder_version)) {
			ret = -EINVAL;
			goto err;
		}
		if (put_user(BINDER_CURRENT_PROTOCOL_VERSION,
			     &ver->protocol_version)) {
			ret = -EINVAL;
			goto err;
		}
		break;
	}
	case BINDER_GET_NODE_INFO_FOR_REF: {
		struct binder_node_info_for_ref info;

		if (copy_from_user(&info, ubuf, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}

		ret = binder_ioctl_get_node_info_for_ref(proc, &info);
		if (ret < 0)
			goto err;

		if (copy_to_user(ubuf, &info, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}

		break;
	}
	case BINDER_GET_NODE_DEBUG_INFO: {
		struct binder_node_debug_info info;

		if (copy_from_user(&info, ubuf, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}

		ret = binder_ioctl_get_node_debug_info(proc, &info);
		if (ret < 0)
			goto err;

		if (copy_to_user(ubuf, &info, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}
		break;
	}
	case BINDER_FREEZE: {
		struct binder_freeze_info info;
		struct binder_proc **target_procs = NULL, *target_proc;
		int target_procs_count = 0, i = 0;

		ret = 0;

		if (copy_from_user(&info, ubuf, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}

		mutex_lock(&binder_procs_lock);
		hlist_for_each_entry(target_proc, &binder_procs, proc_node) {
			if (target_proc->pid == info.pid)
				target_procs_count++;
		}

		if (target_procs_count == 0) {
			mutex_unlock(&binder_procs_lock);
			ret = -EINVAL;
			goto err;
		}

		target_procs = kcalloc(target_procs_count,
				       sizeof(struct binder_proc *),
				       GFP_KERNEL);

		if (!target_procs) {
			mutex_unlock(&binder_procs_lock);
			ret = -ENOMEM;
			goto err;
		}

		hlist_for_each_entry(target_proc, &binder_procs, proc_node) {
			if (target_proc->pid != info.pid)
				continue;

			binder_inner_proc_lock(target_proc);
			target_proc->tmp_ref++;
			binder_inner_proc_unlock(target_proc);

			target_procs[i++] = target_proc;
		}
		mutex_unlock(&binder_procs_lock);

		for (i = 0; i < target_procs_count; i++) {
			if (ret >= 0)
				ret = binder_ioctl_freeze(&info,
							  target_procs[i]);

			binder_proc_dec_tmpref(target_procs[i]);
		}

		kfree(target_procs);

		if (ret < 0)
			goto err;
		break;
	}
	case BINDER_GET_FROZEN_INFO: {
		struct binder_frozen_status_info info;

		if (copy_from_user(&info, ubuf, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}

		ret = binder_ioctl_get_freezer_info(&info);
		if (ret < 0)
			goto err;

		if (copy_to_user(ubuf, &info, sizeof(info))) {
			ret = -EFAULT;
			goto err;
		}
		break;
	}
	case BINDER_ENABLE_ONEWAY_SPAM_DETECTION: {
		uint32_t enable;

		if (copy_from_user(&enable, ubuf, sizeof(enable))) {
			ret = -EFAULT;
			goto err;
		}
		binder_inner_proc_lock(proc);
		proc->oneway_spam_detection_enabled = (bool)enable;
		binder_inner_proc_unlock(proc);
		break;
	}
	default:
		ret = -EINVAL;
		goto err;
	}
	ret = 0;
err:
	if (thread)
		thread->looper_need_return = false;
	wait_event_interruptible(binder_user_error_wait, binder_stop_on_user_error < 2);
	if (ret && ret != -EINTR)
		pr_info("%d:%d ioctl %x %lx returned %d\n", proc->pid, current->pid, cmd, arg, ret);
err_unlocked:
	trace_binder_ioctl_done(ret);
	return ret;
}

static void binder_vma_open(struct vm_area_struct *vma)
{
	struct binder_proc *proc = vma->vm_private_data;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%d open vm area %lx-%lx (%ld K) vma %lx pagep %lx\n",
		     proc->pid, vma->vm_start, vma->vm_end,
		     (vma->vm_end - vma->vm_start) / SZ_1K, vma->vm_flags,
		     (unsigned long)pgprot_val(vma->vm_page_prot));
}

static void binder_vma_close(struct vm_area_struct *vma)
{
	struct binder_proc *proc = vma->vm_private_data;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%d close vm area %lx-%lx (%ld K) vma %lx pagep %lx\n",
		     proc->pid, vma->vm_start, vma->vm_end,
		     (vma->vm_end - vma->vm_start) / SZ_1K, vma->vm_flags,
		     (unsigned long)pgprot_val(vma->vm_page_prot));
	binder_alloc_vma_close(&proc->alloc);
}

static vm_fault_t binder_vm_fault(struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct binder_vm_ops = {
	.open = binder_vma_open,
	.close = binder_vma_close,
	.fault = binder_vm_fault,
};

static int binder_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct binder_proc *proc = filp->private_data;

	if (proc->tsk != current->group_leader)
		return -EINVAL;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d %lx-%lx (%ld K) vma %lx pagep %lx\n",
		     __func__, proc->pid, vma->vm_start, vma->vm_end,
		     (vma->vm_end - vma->vm_start) / SZ_1K, vma->vm_flags,
		     (unsigned long)pgprot_val(vma->vm_page_prot));

	if (vma->vm_flags & FORBIDDEN_MMAP_FLAGS) {
		pr_err("%s: %d %lx-%lx %s failed %d\n", __func__,
		       proc->pid, vma->vm_start, vma->vm_end, "bad vm_flags", -EPERM);
		return -EPERM;
	}
	vma->vm_flags |= VM_DONTCOPY | VM_MIXEDMAP;
	vma->vm_flags &= ~VM_MAYWRITE;

	vma->vm_ops = &binder_vm_ops;
	vma->vm_private_data = proc;

	return binder_alloc_mmap_handler(&proc->alloc, vma);
}

static int binder_open(struct inode *nodp, struct file *filp)
{
	struct binder_proc *proc, *itr;
	struct binder_device *binder_dev;
	struct binderfs_info *info;
	struct dentry *binder_binderfs_dir_entry_proc = NULL;
	bool existing_pid = false;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE, "%s: %d:%d\n", __func__,
		     current->group_leader->pid, current->pid);

	proc = kzalloc(sizeof(*proc), GFP_KERNEL);
	if (proc == NULL)
		return -ENOMEM;
	spin_lock_init(&proc->inner_lock);
	spin_lock_init(&proc->outer_lock);
	get_task_struct(current->group_leader);
	proc->tsk = current->group_leader;
	proc->cred = get_cred(filp->f_cred);
	INIT_LIST_HEAD(&proc->todo);
	init_waitqueue_head(&proc->freeze_wait);
	proc->default_priority = task_nice(current);
	/* binderfs stashes devices in i_private */
	if (is_binderfs_device(nodp)) {
		binder_dev = nodp->i_private;
		info = nodp->i_sb->s_fs_info;
		binder_binderfs_dir_entry_proc = info->proc_log_dir;
	} else {
		binder_dev = container_of(filp->private_data,
					  struct binder_device, miscdev);
	}
	refcount_inc(&binder_dev->ref);
	proc->context = &binder_dev->context;
	binder_alloc_init(&proc->alloc);

	binder_stats_created(BINDER_STAT_PROC);
	proc->pid = current->group_leader->pid;
	INIT_LIST_HEAD(&proc->delivered_death);
	INIT_LIST_HEAD(&proc->waiting_threads);
	filp->private_data = proc;

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(itr, &binder_procs, proc_node) {
		if (itr->pid == proc->pid) {
			existing_pid = true;
			break;
		}
	}
	hlist_add_head(&proc->proc_node, &binder_procs);
	mutex_unlock(&binder_procs_lock);

	if (binder_debugfs_dir_entry_proc && !existing_pid) {
		char strbuf[11];

		snprintf(strbuf, sizeof(strbuf), "%u", proc->pid);
		/*
		 * proc debug entries are shared between contexts.
		 * Only create for the first PID to avoid debugfs log spamming
		 * The printing code will anyway print all contexts for a given
		 * PID so this is not a problem.
		 */
		proc->debugfs_entry = debugfs_create_file(strbuf, 0444,
			binder_debugfs_dir_entry_proc,
			(void *)(unsigned long)proc->pid,
			&proc_fops);
	}

	if (binder_binderfs_dir_entry_proc && !existing_pid) {
		char strbuf[11];
		struct dentry *binderfs_entry;

		snprintf(strbuf, sizeof(strbuf), "%u", proc->pid);
		/*
		 * Similar to debugfs, the process specific log file is shared
		 * between contexts. Only create for the first PID.
		 * This is ok since same as debugfs, the log file will contain
		 * information on all contexts of a given PID.
		 */
		binderfs_entry = binderfs_create_file(binder_binderfs_dir_entry_proc,
			strbuf, &proc_fops, (void *)(unsigned long)proc->pid);
		if (!IS_ERR(binderfs_entry)) {
			proc->binderfs_entry = binderfs_entry;
		} else {
			int error;

			error = PTR_ERR(binderfs_entry);
			pr_warn("Unable to create file %s in binderfs (error %d)\n",
				strbuf, error);
		}
	}

	return 0;
}

static int binder_flush(struct file *filp, fl_owner_t id)
{
	struct binder_proc *proc = filp->private_data;

	binder_defer_work(proc, BINDER_DEFERRED_FLUSH);

	return 0;
}

static void binder_deferred_flush(struct binder_proc *proc)
{
	struct rb_node *n;
	int wake_count = 0;

	binder_inner_proc_lock(proc);
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		struct binder_thread *thread = rb_entry(n, struct binder_thread, rb_node);

		thread->looper_need_return = true;
		if (thread->looper & BINDER_LOOPER_STATE_WAITING) {
			wake_up_interruptible(&thread->wait);
			wake_count++;
		}
	}
	binder_inner_proc_unlock(proc);

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "binder_flush: %d woke %d threads\n", proc->pid,
		     wake_count);
}

static int binder_release(struct inode *nodp, struct file *filp)
{
	struct binder_proc *proc = filp->private_data;

	debugfs_remove(proc->debugfs_entry);

	if (proc->binderfs_entry) {
		binderfs_remove_file(proc->binderfs_entry);
		proc->binderfs_entry = NULL;
	}

	binder_defer_work(proc, BINDER_DEFERRED_RELEASE);

	return 0;
}

static int binder_node_release(struct binder_node *node, int refs)
{
	struct binder_ref *ref;
	int death = 0;
	struct binder_proc *proc = node->proc;

	binder_release_work(proc, &node->async_todo);

	binder_node_lock(node);
	binder_inner_proc_lock(proc);
	binder_dequeue_work_ilocked(&node->work);
	/*
	 * The caller must have taken a temporary ref on the node,
	 */
	BUG_ON(!node->tmp_refs);
	if (hlist_empty(&node->refs) && node->tmp_refs == 1) {
		binder_inner_proc_unlock(proc);
		binder_node_unlock(node);
		binder_free_node(node);

		return refs;
	}

	node->proc = NULL;
	node->local_strong_refs = 0;
	node->local_weak_refs = 0;
	binder_inner_proc_unlock(proc);

	spin_lock(&binder_dead_nodes_lock);
	hlist_add_head(&node->dead_node, &binder_dead_nodes);
	spin_unlock(&binder_dead_nodes_lock);

	hlist_for_each_entry(ref, &node->refs, node_entry) {
		refs++;
		/*
		 * Need the node lock to synchronize
		 * with new notification requests and the
		 * inner lock to synchronize with queued
		 * death notifications.
		 */
		binder_inner_proc_lock(ref->proc);
		if (!ref->death) {
			binder_inner_proc_unlock(ref->proc);
			continue;
		}

		death++;

		BUG_ON(!list_empty(&ref->death->work.entry));
		ref->death->work.type = BINDER_WORK_DEAD_BINDER;
		binder_enqueue_work_ilocked(&ref->death->work,
					    &ref->proc->todo);
		binder_wakeup_proc_ilocked(ref->proc);
		binder_inner_proc_unlock(ref->proc);
	}

	binder_debug(BINDER_DEBUG_DEAD_BINDER,
		     "node %d now dead, refs %d, death %d\n",
		     node->debug_id, refs, death);
	binder_node_unlock(node);
	binder_put_node(node);

	return refs;
}

static void binder_deferred_release(struct binder_proc *proc)
{
	struct binder_context *context = proc->context;
	struct rb_node *n;
	int threads, nodes, incoming_refs, outgoing_refs, active_transactions;

	mutex_lock(&binder_procs_lock);
	hlist_del(&proc->proc_node);
	mutex_unlock(&binder_procs_lock);

	mutex_lock(&context->context_mgr_node_lock);
	if (context->binder_context_mgr_node &&
	    context->binder_context_mgr_node->proc == proc) {
		binder_debug(BINDER_DEBUG_DEAD_BINDER,
			     "%s: %d context_mgr_node gone\n",
			     __func__, proc->pid);
		context->binder_context_mgr_node = NULL;
	}
	mutex_unlock(&context->context_mgr_node_lock);
	binder_inner_proc_lock(proc);
	/*
	 * Make sure proc stays alive after we
	 * remove all the threads
	 */
	proc->tmp_ref++;

	proc->is_dead = true;
	proc->is_frozen = false;
	proc->sync_recv = false;
	proc->async_recv = false;
	threads = 0;
	active_transactions = 0;
	while ((n = rb_first(&proc->threads))) {
		struct binder_thread *thread;

		thread = rb_entry(n, struct binder_thread, rb_node);
		binder_inner_proc_unlock(proc);
		threads++;
		active_transactions += binder_thread_release(proc, thread);
		binder_inner_proc_lock(proc);
	}

	nodes = 0;
	incoming_refs = 0;
	while ((n = rb_first(&proc->nodes))) {
		struct binder_node *node;

		node = rb_entry(n, struct binder_node, rb_node);
		nodes++;
		/*
		 * take a temporary ref on the node before
		 * calling binder_node_release() which will either
		 * kfree() the node or call binder_put_node()
		 */
		binder_inc_node_tmpref_ilocked(node);
		rb_erase(&node->rb_node, &proc->nodes);
		binder_inner_proc_unlock(proc);
		incoming_refs = binder_node_release(node, incoming_refs);
		binder_inner_proc_lock(proc);
	}
	binder_inner_proc_unlock(proc);

	outgoing_refs = 0;
	binder_proc_lock(proc);
	while ((n = rb_first(&proc->refs_by_desc))) {
		struct binder_ref *ref;

		ref = rb_entry(n, struct binder_ref, rb_node_desc);
		outgoing_refs++;
		binder_cleanup_ref_olocked(ref);
		binder_proc_unlock(proc);
		binder_free_ref(ref);
		binder_proc_lock(proc);
	}
	binder_proc_unlock(proc);

	binder_release_work(proc, &proc->todo);
	binder_release_work(proc, &proc->delivered_death);

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d threads %d, nodes %d (ref %d), refs %d, active transactions %d\n",
		     __func__, proc->pid, threads, nodes, incoming_refs,
		     outgoing_refs, active_transactions);

	binder_proc_dec_tmpref(proc);
}

static void binder_deferred_func(struct work_struct *work)
{
	struct binder_proc *proc;

	int defer;

	do {
		mutex_lock(&binder_deferred_lock);
		if (!hlist_empty(&binder_deferred_list)) {
			proc = hlist_entry(binder_deferred_list.first,
					struct binder_proc, deferred_work_node);
			hlist_del_init(&proc->deferred_work_node);
			defer = proc->deferred_work;
			proc->deferred_work = 0;
		} else {
			proc = NULL;
			defer = 0;
		}
		mutex_unlock(&binder_deferred_lock);

		if (defer & BINDER_DEFERRED_FLUSH)
			binder_deferred_flush(proc);

		if (defer & BINDER_DEFERRED_RELEASE)
			binder_deferred_release(proc); /* frees proc */
	} while (proc);
}
static DECLARE_WORK(binder_deferred_work, binder_deferred_func);

static void
binder_defer_work(struct binder_proc *proc, enum binder_deferred_state defer)
{
	mutex_lock(&binder_deferred_lock);
	proc->deferred_work |= defer;
	if (hlist_unhashed(&proc->deferred_work_node)) {
		hlist_add_head(&proc->deferred_work_node,
				&binder_deferred_list);
		schedule_work(&binder_deferred_work);
	}
	mutex_unlock(&binder_deferred_lock);
}

static void print_binder_transaction_ilocked(struct seq_file *m,
					     struct binder_proc *proc,
					     const char *prefix,
					     struct binder_transaction *t)
{
	struct binder_proc *to_proc;
	struct binder_buffer *buffer = t->buffer;

	spin_lock(&t->lock);
	to_proc = t->to_proc;
	seq_printf(m,
		   "%s %d: %pK from %d:%d to %d:%d code %x flags %x pri %ld r%d",
		   prefix, t->debug_id, t,
		   t->from ? t->from->proc->pid : 0,
		   t->from ? t->from->pid : 0,
		   to_proc ? to_proc->pid : 0,
		   t->to_thread ? t->to_thread->pid : 0,
		   t->code, t->flags, t->priority, t->need_reply);
	spin_unlock(&t->lock);

	if (proc != to_proc) {
		/*
		 * Can only safely deref buffer if we are holding the
		 * correct proc inner lock for this node
		 */
		seq_puts(m, "\n");
		return;
	}

	if (buffer == NULL) {
		seq_puts(m, " buffer free\n");
		return;
	}
	if (buffer->target_node)
		seq_printf(m, " node %d", buffer->target_node->debug_id);
	seq_printf(m, " size %zd:%zd data %pK\n",
		   buffer->data_size, buffer->offsets_size,
		   buffer->user_data);
}

static void print_binder_work_ilocked(struct seq_file *m,
				     struct binder_proc *proc,
				     const char *prefix,
				     const char *transaction_prefix,
				     struct binder_work *w)
{
	struct binder_node *node;
	struct binder_transaction *t;

	switch (w->type) {
	case BINDER_WORK_TRANSACTION:
		t = container_of(w, struct binder_transaction, work);
		print_binder_transaction_ilocked(
				m, proc, transaction_prefix, t);
		break;
	case BINDER_WORK_RETURN_ERROR: {
		struct binder_error *e = container_of(
				w, struct binder_error, work);

		seq_printf(m, "%stransaction error: %u\n",
			   prefix, e->cmd);
	} break;
	case BINDER_WORK_TRANSACTION_COMPLETE:
		seq_printf(m, "%stransaction complete\n", prefix);
		break;
	case BINDER_WORK_NODE:
		node = container_of(w, struct binder_node, work);
		seq_printf(m, "%snode work %d: u%016llx c%016llx\n",
			   prefix, node->debug_id,
			   (u64)node->ptr, (u64)node->cookie);
		break;
	case BINDER_WORK_DEAD_BINDER:
		seq_printf(m, "%shas dead binder\n", prefix);
		break;
	case BINDER_WORK_DEAD_BINDER_AND_CLEAR:
		seq_printf(m, "%shas cleared dead binder\n", prefix);
		break;
	case BINDER_WORK_CLEAR_DEATH_NOTIFICATION:
		seq_printf(m, "%shas cleared death notification\n", prefix);
		break;
	default:
		seq_printf(m, "%sunknown work: type %d\n", prefix, w->type);
		break;
	}
}

static void print_binder_thread_ilocked(struct seq_file *m,
					struct binder_thread *thread,
					int print_always)
{
	struct binder_transaction *t;
	struct binder_work *w;
	size_t start_pos = m->count;
	size_t header_pos;

	seq_printf(m, "  thread %d: l %02x need_return %d tr %d\n",
			thread->pid, thread->looper,
			thread->looper_need_return,
			atomic_read(&thread->tmp_ref));
	header_pos = m->count;
	t = thread->transaction_stack;
	while (t) {
		if (t->from == thread) {
			print_binder_transaction_ilocked(m, thread->proc,
					"    outgoing transaction", t);
			t = t->from_parent;
		} else if (t->to_thread == thread) {
			print_binder_transaction_ilocked(m, thread->proc,
						 "    incoming transaction", t);
			t = t->to_parent;
		} else {
			print_binder_transaction_ilocked(m, thread->proc,
					"    bad transaction", t);
			t = NULL;
		}
	}
	list_for_each_entry(w, &thread->todo, entry) {
		print_binder_work_ilocked(m, thread->proc, "    ",
					  "    pending transaction", w);
	}
	if (!print_always && m->count == header_pos)
		m->count = start_pos;
}

static void print_binder_node_nilocked(struct seq_file *m,
				       struct binder_node *node)
{
	struct binder_ref *ref;
	struct binder_work *w;
	int count;

	count = 0;
	hlist_for_each_entry(ref, &node->refs, node_entry)
		count++;

	seq_printf(m, "  node %d: u%016llx c%016llx hs %d hw %d ls %d lw %d is %d iw %d tr %d",
		   node->debug_id, (u64)node->ptr, (u64)node->cookie,
		   node->has_strong_ref, node->has_weak_ref,
		   node->local_strong_refs, node->local_weak_refs,
		   node->internal_strong_refs, count, node->tmp_refs);
	if (count) {
		seq_puts(m, " proc");
		hlist_for_each_entry(ref, &node->refs, node_entry)
			seq_printf(m, " %d", ref->proc->pid);
	}
	seq_puts(m, "\n");
	if (node->proc) {
		list_for_each_entry(w, &node->async_todo, entry)
			print_binder_work_ilocked(m, node->proc, "    ",
					  "    pending async transaction", w);
	}
}

static void print_binder_ref_olocked(struct seq_file *m,
				     struct binder_ref *ref)
{
	binder_node_lock(ref->node);
	seq_printf(m, "  ref %d: desc %d %snode %d s %d w %d d %pK\n",
		   ref->data.debug_id, ref->data.desc,
		   ref->node->proc ? "" : "dead ",
		   ref->node->debug_id, ref->data.strong,
		   ref->data.weak, ref->death);
	binder_node_unlock(ref->node);
}

static void print_binder_proc(struct seq_file *m,
			      struct binder_proc *proc, int print_all)
{
	struct binder_work *w;
	struct rb_node *n;
	size_t start_pos = m->count;
	size_t header_pos;
	struct binder_node *last_node = NULL;

	seq_printf(m, "proc %d\n", proc->pid);
	seq_printf(m, "context %s\n", proc->context->name);
	header_pos = m->count;

	binder_inner_proc_lock(proc);
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n))
		print_binder_thread_ilocked(m, rb_entry(n, struct binder_thread,
						rb_node), print_all);

	for (n = rb_first(&proc->nodes); n != NULL; n = rb_next(n)) {
		struct binder_node *node = rb_entry(n, struct binder_node,
						    rb_node);
		if (!print_all && !node->has_async_transaction)
			continue;

		/*
		 * take a temporary reference on the node so it
		 * survives and isn't removed from the tree
		 * while we print it.
		 */
		binder_inc_node_tmpref_ilocked(node);
		/* Need to drop inner lock to take node lock */
		binder_inner_proc_unlock(proc);
		if (last_node)
			binder_put_node(last_node);
		binder_node_inner_lock(node);
		print_binder_node_nilocked(m, node);
		binder_node_inner_unlock(node);
		last_node = node;
		binder_inner_proc_lock(proc);
	}
	binder_inner_proc_unlock(proc);
	if (last_node)
		binder_put_node(last_node);

	if (print_all) {
		binder_proc_lock(proc);
		for (n = rb_first(&proc->refs_by_desc);
		     n != NULL;
		     n = rb_next(n))
			print_binder_ref_olocked(m, rb_entry(n,
							    struct binder_ref,
							    rb_node_desc));
		binder_proc_unlock(proc);
	}
	binder_alloc_print_allocated(m, &proc->alloc);
	binder_inner_proc_lock(proc);
	list_for_each_entry(w, &proc->todo, entry)
		print_binder_work_ilocked(m, proc, "  ",
					  "  pending transaction", w);
	list_for_each_entry(w, &proc->delivered_death, entry) {
		seq_puts(m, "  has delivered dead binder\n");
		break;
	}
	binder_inner_proc_unlock(proc);
	if (!print_all && m->count == header_pos)
		m->count = start_pos;
}

static const char * const binder_return_strings[] = {
	"BR_ERROR",
	"BR_OK",
	"BR_TRANSACTION",
	"BR_REPLY",
	"BR_ACQUIRE_RESULT",
	"BR_DEAD_REPLY",
	"BR_TRANSACTION_COMPLETE",
	"BR_INCREFS",
	"BR_ACQUIRE",
	"BR_RELEASE",
	"BR_DECREFS",
	"BR_ATTEMPT_ACQUIRE",
	"BR_NOOP",
	"BR_SPAWN_LOOPER",
	"BR_FINISHED",
	"BR_DEAD_BINDER",
	"BR_CLEAR_DEATH_NOTIFICATION_DONE",
	"BR_FAILED_REPLY",
	"BR_FROZEN_REPLY",
	"BR_ONEWAY_SPAM_SUSPECT",
};

static const char * const binder_command_strings[] = {
	"BC_TRANSACTION",
	"BC_REPLY",
	"BC_ACQUIRE_RESULT",
	"BC_FREE_BUFFER",
	"BC_INCREFS",
	"BC_ACQUIRE",
	"BC_RELEASE",
	"BC_DECREFS",
	"BC_INCREFS_DONE",
	"BC_ACQUIRE_DONE",
	"BC_ATTEMPT_ACQUIRE",
	"BC_REGISTER_LOOPER",
	"BC_ENTER_LOOPER",
	"BC_EXIT_LOOPER",
	"BC_REQUEST_DEATH_NOTIFICATION",
	"BC_CLEAR_DEATH_NOTIFICATION",
	"BC_DEAD_BINDER_DONE",
	"BC_TRANSACTION_SG",
	"BC_REPLY_SG",
};

static const char * const binder_objstat_strings[] = {
	"proc",
	"thread",
	"node",
	"ref",
	"death",
	"transaction",
	"transaction_complete"
};

static void print_binder_stats(struct seq_file *m, const char *prefix,
			       struct binder_stats *stats)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(stats->bc) !=
		     ARRAY_SIZE(binder_command_strings));
	for (i = 0; i < ARRAY_SIZE(stats->bc); i++) {
		int temp = atomic_read(&stats->bc[i]);

		if (temp)
			seq_printf(m, "%s%s: %d\n", prefix,
				   binder_command_strings[i], temp);
	}

	BUILD_BUG_ON(ARRAY_SIZE(stats->br) !=
		     ARRAY_SIZE(binder_return_strings));
	for (i = 0; i < ARRAY_SIZE(stats->br); i++) {
		int temp = atomic_read(&stats->br[i]);

		if (temp)
			seq_printf(m, "%s%s: %d\n", prefix,
				   binder_return_strings[i], temp);
	}

	BUILD_BUG_ON(ARRAY_SIZE(stats->obj_created) !=
		     ARRAY_SIZE(binder_objstat_strings));
	BUILD_BUG_ON(ARRAY_SIZE(stats->obj_created) !=
		     ARRAY_SIZE(stats->obj_deleted));
	for (i = 0; i < ARRAY_SIZE(stats->obj_created); i++) {
		int created = atomic_read(&stats->obj_created[i]);
		int deleted = atomic_read(&stats->obj_deleted[i]);

		if (created || deleted)
			seq_printf(m, "%s%s: active %d total %d\n",
				prefix,
				binder_objstat_strings[i],
				created - deleted,
				created);
	}
}

static void print_binder_proc_stats(struct seq_file *m,
				    struct binder_proc *proc)
{
	struct binder_work *w;
	struct binder_thread *thread;
	struct rb_node *n;
	int count, strong, weak, ready_threads;
	size_t free_async_space =
		binder_alloc_get_free_async_space(&proc->alloc);

	seq_printf(m, "proc %d\n", proc->pid);
	seq_printf(m, "context %s\n", proc->context->name);
	count = 0;
	ready_threads = 0;
	binder_inner_proc_lock(proc);
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n))
		count++;

	list_for_each_entry(thread, &proc->waiting_threads, waiting_thread_node)
		ready_threads++;

	seq_printf(m, "  threads: %d\n", count);
	seq_printf(m, "  requested threads: %d+%d/%d\n"
			"  ready threads %d\n"
			"  free async space %zd\n", proc->requested_threads,
			proc->requested_threads_started, proc->max_threads,
			ready_threads,
			free_async_space);
	count = 0;
	for (n = rb_first(&proc->nodes); n != NULL; n = rb_next(n))
		count++;
	binder_inner_proc_unlock(proc);
	seq_printf(m, "  nodes: %d\n", count);
	count = 0;
	strong = 0;
	weak = 0;
	binder_proc_lock(proc);
	for (n = rb_first(&proc->refs_by_desc); n != NULL; n = rb_next(n)) {
		struct binder_ref *ref = rb_entry(n, struct binder_ref,
						  rb_node_desc);
		count++;
		strong += ref->data.strong;
		weak += ref->data.weak;
	}
	binder_proc_unlock(proc);
	seq_printf(m, "  refs: %d s %d w %d\n", count, strong, weak);

	count = binder_alloc_get_allocated_count(&proc->alloc);
	seq_printf(m, "  buffers: %d\n", count);

	binder_alloc_print_pages(m, &proc->alloc);

	count = 0;
	binder_inner_proc_lock(proc);
	list_for_each_entry(w, &proc->todo, entry) {
		if (w->type == BINDER_WORK_TRANSACTION)
			count++;
	}
	binder_inner_proc_unlock(proc);
	seq_printf(m, "  pending transactions: %d\n", count);

	print_binder_stats(m, "  ", &proc->stats);
}


int binder_state_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;
	struct binder_node *node;
	struct binder_node *last_node = NULL;

	seq_puts(m, "binder state:\n");

	spin_lock(&binder_dead_nodes_lock);
	if (!hlist_empty(&binder_dead_nodes))
		seq_puts(m, "dead nodes:\n");
	hlist_for_each_entry(node, &binder_dead_nodes, dead_node) {
		/*
		 * take a temporary reference on the node so it
		 * survives and isn't removed from the list
		 * while we print it.
		 */
		node->tmp_refs++;
		spin_unlock(&binder_dead_nodes_lock);
		if (last_node)
			binder_put_node(last_node);
		binder_node_lock(node);
		print_binder_node_nilocked(m, node);
		binder_node_unlock(node);
		last_node = node;
		spin_lock(&binder_dead_nodes_lock);
	}
	spin_unlock(&binder_dead_nodes_lock);
	if (last_node)
		binder_put_node(last_node);

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc(m, proc, 1);
	mutex_unlock(&binder_procs_lock);

	return 0;
}

int binder_stats_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;

	seq_puts(m, "binder stats:\n");

	print_binder_stats(m, "", &binder_stats);

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc_stats(m, proc);
	mutex_unlock(&binder_procs_lock);

	return 0;
}

int binder_transactions_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;

	seq_puts(m, "binder transactions:\n");
	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc(m, proc, 0);
	mutex_unlock(&binder_procs_lock);

	return 0;
}

static int proc_show(struct seq_file *m, void *unused)
{
	struct binder_proc *itr;
	int pid = (unsigned long)m->private;

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(itr, &binder_procs, proc_node) {
		if (itr->pid == pid) {
			seq_puts(m, "binder proc state:\n");
			print_binder_proc(m, itr, 1);
		}
	}
	mutex_unlock(&binder_procs_lock);

	return 0;
}

static void print_binder_transaction_log_entry(struct seq_file *m,
					struct binder_transaction_log_entry *e)
{
	int debug_id = READ_ONCE(e->debug_id_done);
	/*
	 * read barrier to guarantee debug_id_done read before
	 * we print the log values
	 */
	smp_rmb();
	seq_printf(m,
		   "%d: %s from %d:%d to %d:%d context %s node %d handle %d size %d:%d ret %d/%d l=%d",
		   e->debug_id, (e->call_type == 2) ? "reply" :
		   ((e->call_type == 1) ? "async" : "call "), e->from_proc,
		   e->from_thread, e->to_proc, e->to_thread, e->context_name,
		   e->to_node, e->target_handle, e->data_size, e->offsets_size,
		   e->return_error, e->return_error_param,
		   e->return_error_line);
	/*
	 * read-barrier to guarantee read of debug_id_done after
	 * done printing the fields of the entry
	 */
	smp_rmb();
	seq_printf(m, debug_id && debug_id == READ_ONCE(e->debug_id_done) ?
			"\n" : " (incomplete)\n");
}

int binder_transaction_log_show(struct seq_file *m, void *unused)
{
	struct binder_transaction_log *log = m->private;
	unsigned int log_cur = atomic_read(&log->cur);
	unsigned int count;
	unsigned int cur;
	int i;

	count = log_cur + 1;
	cur = count < ARRAY_SIZE(log->entry) && !log->full ?
		0 : count % ARRAY_SIZE(log->entry);
	if (count > ARRAY_SIZE(log->entry) || log->full)
		count = ARRAY_SIZE(log->entry);
	for (i = 0; i < count; i++) {
		unsigned int index = cur++ % ARRAY_SIZE(log->entry);

		print_binder_transaction_log_entry(m, &log->entry[index]);
	}
	return 0;
}

const struct file_operations binder_fops = {
	.owner = THIS_MODULE,
	.poll = binder_poll,
	.unlocked_ioctl = binder_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.mmap = binder_mmap,
	.open = binder_open,
	.flush = binder_flush,
	.release = binder_release,
};

static int __init init_binder_device(const char *name)
{
	int ret;
	struct binder_device *binder_device;

	binder_device = kzalloc(sizeof(*binder_device), GFP_KERNEL);
	if (!binder_device)
		return -ENOMEM;

	binder_device->miscdev.fops = &binder_fops;
	binder_device->miscdev.minor = MISC_DYNAMIC_MINOR;
	binder_device->miscdev.name = name;

	refcount_set(&binder_device->ref, 1);
	binder_device->context.binder_context_mgr_uid = INVALID_UID;
	binder_device->context.name = name;
	mutex_init(&binder_device->context.context_mgr_node_lock);

	ret = misc_register(&binder_device->miscdev);
	if (ret < 0) {
		kfree(binder_device);
		return ret;
	}

	hlist_add_head(&binder_device->hlist, &binder_devices);

	return ret;
}

static int __init binder_init(void)
{
	int ret;
	char *device_name, *device_tmp;
	struct binder_device *device;
	struct hlist_node *tmp;
	char *device_names = NULL;

	ret = binder_alloc_shrinker_init();
	if (ret)
		return ret;

	atomic_set(&binder_transaction_log.cur, ~0U);
	atomic_set(&binder_transaction_log_failed.cur, ~0U);

	binder_debugfs_dir_entry_root = debugfs_create_dir("binder", NULL);
	if (binder_debugfs_dir_entry_root)
		binder_debugfs_dir_entry_proc = debugfs_create_dir("proc",
						 binder_debugfs_dir_entry_root);

	if (binder_debugfs_dir_entry_root) {
		debugfs_create_file("state",
				    0444,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_state_fops);
		debugfs_create_file("stats",
				    0444,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_stats_fops);
		debugfs_create_file("transactions",
				    0444,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_transactions_fops);
		debugfs_create_file("transaction_log",
				    0444,
				    binder_debugfs_dir_entry_root,
				    &binder_transaction_log,
				    &binder_transaction_log_fops);
		debugfs_create_file("failed_transaction_log",
				    0444,
				    binder_debugfs_dir_entry_root,
				    &binder_transaction_log_failed,
				    &binder_transaction_log_fops);
	}

	if (!IS_ENABLED(CONFIG_ANDROID_BINDERFS) &&
	    strcmp(binder_devices_param, "") != 0) {
		/*
		* Copy the module_parameter string, because we don't want to
		* tokenize it in-place.
		 */
		device_names = kstrdup(binder_devices_param, GFP_KERNEL);
		if (!device_names) {
			ret = -ENOMEM;
			goto err_alloc_device_names_failed;
		}

		device_tmp = device_names;
		while ((device_name = strsep(&device_tmp, ","))) {
			ret = init_binder_device(device_name);
			if (ret)
				goto err_init_binder_device_failed;
		}
	}

	ret = init_binderfs();
	if (ret)
		goto err_init_binder_device_failed;

	return ret;

err_init_binder_device_failed:
	hlist_for_each_entry_safe(device, tmp, &binder_devices, hlist) {
		misc_deregister(&device->miscdev);
		hlist_del(&device->hlist);
		kfree(device);
	}

	kfree(device_names);

err_alloc_device_names_failed:
	debugfs_remove_recursive(binder_debugfs_dir_entry_root);

	return ret;
}

device_initcall(binder_init);

#define CREATE_TRACE_POINTS
#include "binder_trace.h"

MODULE_LICENSE("GPL v2");
