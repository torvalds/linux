/* binder.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/cacheflush.h>
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
#include <linux/uaccess.h>
#include <linux/pid_namespace.h>
#include <linux/security.h>

#ifdef CONFIG_ANDROID_BINDER_IPC_32BIT
#define BINDER_IPC_32BIT 1
#endif

#include <uapi/linux/android/binder.h>
#include "binder_alloc.h"
#include "binder_trace.h"

static DEFINE_MUTEX(binder_main_lock);

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

#define BINDER_DEBUG_ENTRY(name) \
static int binder_##name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, binder_##name##_show, inode->i_private); \
} \
\
static const struct file_operations binder_##name##_fops = { \
	.owner = THIS_MODULE, \
	.open = binder_##name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

static int binder_proc_show(struct seq_file *m, void *unused);
BINDER_DEBUG_ENTRY(proc);

/* This is only defined in include/asm-arm/sizes.h */
#ifndef SZ_1K
#define SZ_1K                               0x400
#endif

#ifndef SZ_4M
#define SZ_4M                               0x400000
#endif

#define FORBIDDEN_MMAP_FLAGS                (VM_WRITE)

#define BINDER_SMALL_BUF_SIZE (PAGE_SIZE * 64)

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
};
static uint32_t binder_debug_mask = BINDER_DEBUG_USER_ERROR |
	BINDER_DEBUG_FAILED_TRANSACTION | BINDER_DEBUG_DEAD_TRANSACTION;
module_param_named(debug_mask, binder_debug_mask, uint, S_IWUSR | S_IRUGO);

static char *binder_devices_param = CONFIG_ANDROID_BINDER_DEVICES;
module_param_named(devices, binder_devices_param, charp, 0444);

static DECLARE_WAIT_QUEUE_HEAD(binder_user_error_wait);
static int binder_stop_on_user_error;

static int binder_set_stop_on_user_error(const char *val,
					 struct kernel_param *kp)
{
	int ret;

	ret = param_set_int(val, kp);
	if (binder_stop_on_user_error < 2)
		wake_up(&binder_user_error_wait);
	return ret;
}
module_param_call(stop_on_user_error, binder_set_stop_on_user_error,
	param_get_int, &binder_stop_on_user_error, S_IWUSR | S_IRUGO);

#define binder_debug(mask, x...) \
	do { \
		if (binder_debug_mask & mask) \
			pr_info(x); \
	} while (0)

#define binder_user_error(x...) \
	do { \
		if (binder_debug_mask & BINDER_DEBUG_USER_ERROR) \
			pr_info(x); \
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

enum binder_stat_types {
	BINDER_STAT_PROC,
	BINDER_STAT_THREAD,
	BINDER_STAT_NODE,
	BINDER_STAT_REF,
	BINDER_STAT_DEATH,
	BINDER_STAT_TRANSACTION,
	BINDER_STAT_TRANSACTION_COMPLETE,
	BINDER_STAT_COUNT
};

struct binder_stats {
	atomic_t br[_IOC_NR(BR_FAILED_REPLY) + 1];
	atomic_t bc[_IOC_NR(BC_REPLY_SG) + 1];
	atomic_t obj_created[BINDER_STAT_COUNT];
	atomic_t obj_deleted[BINDER_STAT_COUNT];
};

static struct binder_stats binder_stats;

static inline void binder_stats_deleted(enum binder_stat_types type)
{
	atomic_inc(&binder_stats.obj_deleted[type]);
}

static inline void binder_stats_created(enum binder_stat_types type)
{
	atomic_inc(&binder_stats.obj_created[type]);
}

struct binder_transaction_log_entry {
	int debug_id;
	int debug_id_done;
	int call_type;
	int from_proc;
	int from_thread;
	int target_handle;
	int to_proc;
	int to_thread;
	int to_node;
	int data_size;
	int offsets_size;
	int return_error_line;
	uint32_t return_error;
	uint32_t return_error_param;
	const char *context_name;
};
struct binder_transaction_log {
	atomic_t cur;
	bool full;
	struct binder_transaction_log_entry entry[32];
};
static struct binder_transaction_log binder_transaction_log;
static struct binder_transaction_log binder_transaction_log_failed;

static struct binder_transaction_log_entry *binder_transaction_log_add(
	struct binder_transaction_log *log)
{
	struct binder_transaction_log_entry *e;
	unsigned int cur = atomic_inc_return(&log->cur);

	if (cur >= ARRAY_SIZE(log->entry))
		log->full = 1;
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

struct binder_context {
	struct binder_node *binder_context_mgr_node;
	struct mutex context_mgr_node_lock;

	kuid_t binder_context_mgr_uid;
	const char *name;
};

struct binder_device {
	struct hlist_node hlist;
	struct miscdevice miscdev;
	struct binder_context context;
};

struct binder_work {
	struct list_head entry;
	enum {
		BINDER_WORK_TRANSACTION = 1,
		BINDER_WORK_TRANSACTION_COMPLETE,
		BINDER_WORK_RETURN_ERROR,
		BINDER_WORK_NODE,
		BINDER_WORK_DEAD_BINDER,
		BINDER_WORK_DEAD_BINDER_AND_CLEAR,
		BINDER_WORK_CLEAR_DEATH_NOTIFICATION,
	} type;
};

struct binder_error {
	struct binder_work work;
	uint32_t cmd;
};

struct binder_node {
	int debug_id;
	struct binder_work work;
	union {
		struct rb_node rb_node;
		struct hlist_node dead_node;
	};
	struct binder_proc *proc;
	struct hlist_head refs;
	int internal_strong_refs;
	int local_weak_refs;
	int local_strong_refs;
	binder_uintptr_t ptr;
	binder_uintptr_t cookie;
	unsigned has_strong_ref:1;
	unsigned pending_strong_ref:1;
	unsigned has_weak_ref:1;
	unsigned pending_weak_ref:1;
	unsigned has_async_transaction:1;
	unsigned accept_fds:1;
	unsigned min_priority:8;
	struct list_head async_todo;
};

struct binder_ref_death {
	struct binder_work work;
	binder_uintptr_t cookie;
};

struct binder_ref {
	/* Lookups needed: */
	/*   node + proc => ref (transaction) */
	/*   desc + proc => ref (transaction, inc/dec ref) */
	/*   node => refs + procs (proc exit) */
	int debug_id;
	struct rb_node rb_node_desc;
	struct rb_node rb_node_node;
	struct hlist_node node_entry;
	struct binder_proc *proc;
	struct binder_node *node;
	uint32_t desc;
	int strong;
	int weak;
	struct binder_ref_death *death;
};

enum binder_deferred_state {
	BINDER_DEFERRED_PUT_FILES    = 0x01,
	BINDER_DEFERRED_FLUSH        = 0x02,
	BINDER_DEFERRED_RELEASE      = 0x04,
};

struct binder_proc {
	struct hlist_node proc_node;
	struct rb_root threads;
	struct rb_root nodes;
	struct rb_root refs_by_desc;
	struct rb_root refs_by_node;
	int pid;
	struct task_struct *tsk;
	struct files_struct *files;
	struct hlist_node deferred_work_node;
	int deferred_work;

	struct list_head todo;
	wait_queue_head_t wait;
	struct binder_stats stats;
	struct list_head delivered_death;
	int max_threads;
	int requested_threads;
	int requested_threads_started;
	int ready_threads;
	long default_priority;
	struct dentry *debugfs_entry;
	struct binder_alloc alloc;
	struct binder_context *context;
};

enum {
	BINDER_LOOPER_STATE_REGISTERED  = 0x01,
	BINDER_LOOPER_STATE_ENTERED     = 0x02,
	BINDER_LOOPER_STATE_EXITED      = 0x04,
	BINDER_LOOPER_STATE_INVALID     = 0x08,
	BINDER_LOOPER_STATE_WAITING     = 0x10,
};

struct binder_thread {
	struct binder_proc *proc;
	struct rb_node rb_node;
	int pid;
	int looper;              /* only modified by this thread */
	bool looper_need_return; /* can be written by other thread */
	struct binder_transaction *transaction_stack;
	struct list_head todo;
	struct binder_error return_error;
	struct binder_error reply_error;
	wait_queue_head_t wait;
	struct binder_stats stats;
};

struct binder_transaction {
	int debug_id;
	struct binder_work work;
	struct binder_thread *from;
	struct binder_transaction *from_parent;
	struct binder_proc *to_proc;
	struct binder_thread *to_thread;
	struct binder_transaction *to_parent;
	unsigned need_reply:1;
	/* unsigned is_dead:1; */	/* not used at the moment */

	struct binder_buffer *buffer;
	unsigned int	code;
	unsigned int	flags;
	long	priority;
	long	saved_priority;
	kuid_t	sender_euid;
};

static void
binder_defer_work(struct binder_proc *proc, enum binder_deferred_state defer);

static int task_get_unused_fd_flags(struct binder_proc *proc, int flags)
{
	struct files_struct *files = proc->files;
	unsigned long rlim_cur;
	unsigned long irqs;

	if (files == NULL)
		return -ESRCH;

	if (!lock_task_sighand(proc->tsk, &irqs))
		return -EMFILE;

	rlim_cur = task_rlimit(proc->tsk, RLIMIT_NOFILE);
	unlock_task_sighand(proc->tsk, &irqs);

	return __alloc_fd(files, 0, rlim_cur, flags);
}

/*
 * copied from fd_install
 */
static void task_fd_install(
	struct binder_proc *proc, unsigned int fd, struct file *file)
{
	if (proc->files)
		__fd_install(proc->files, fd, file);
}

/*
 * copied from sys_close
 */
static long task_close_fd(struct binder_proc *proc, unsigned int fd)
{
	int retval;

	if (proc->files == NULL)
		return -ESRCH;

	retval = __close_fd(proc->files, fd);
	/* can't restart close syscall because file table entry was cleared */
	if (unlikely(retval == -ERESTARTSYS ||
		     retval == -ERESTARTNOINTR ||
		     retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK))
		retval = -EINTR;

	return retval;
}

static inline void binder_lock(const char *tag)
{
	trace_binder_lock(tag);
	mutex_lock(&binder_main_lock);
	trace_binder_locked(tag);
}

static inline void binder_unlock(const char *tag)
{
	trace_binder_unlock(tag);
	mutex_unlock(&binder_main_lock);
}

static void binder_set_nice(long nice)
{
	long min_nice;

	if (can_nice(current, nice)) {
		set_user_nice(current, nice);
		return;
	}
	min_nice = rlimit_to_nice(current->signal->rlim[RLIMIT_NICE].rlim_cur);
	binder_debug(BINDER_DEBUG_PRIORITY_CAP,
		     "%d: nice value %ld not allowed use %ld instead\n",
		      current->pid, nice, min_nice);
	set_user_nice(current, min_nice);
	if (min_nice <= MAX_NICE)
		return;
	binder_user_error("%d RLIMIT_NICE not set\n", current->pid);
}

static struct binder_node *binder_get_node(struct binder_proc *proc,
					   binder_uintptr_t ptr)
{
	struct rb_node *n = proc->nodes.rb_node;
	struct binder_node *node;

	while (n) {
		node = rb_entry(n, struct binder_node, rb_node);

		if (ptr < node->ptr)
			n = n->rb_left;
		else if (ptr > node->ptr)
			n = n->rb_right;
		else
			return node;
	}
	return NULL;
}

static struct binder_node *binder_new_node(struct binder_proc *proc,
					   binder_uintptr_t ptr,
					   binder_uintptr_t cookie)
{
	struct rb_node **p = &proc->nodes.rb_node;
	struct rb_node *parent = NULL;
	struct binder_node *node;

	while (*p) {
		parent = *p;
		node = rb_entry(parent, struct binder_node, rb_node);

		if (ptr < node->ptr)
			p = &(*p)->rb_left;
		else if (ptr > node->ptr)
			p = &(*p)->rb_right;
		else
			return NULL;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (node == NULL)
		return NULL;
	binder_stats_created(BINDER_STAT_NODE);
	rb_link_node(&node->rb_node, parent, p);
	rb_insert_color(&node->rb_node, &proc->nodes);
	node->debug_id = atomic_inc_return(&binder_last_id);
	node->proc = proc;
	node->ptr = ptr;
	node->cookie = cookie;
	node->work.type = BINDER_WORK_NODE;
	INIT_LIST_HEAD(&node->work.entry);
	INIT_LIST_HEAD(&node->async_todo);
	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d:%d node %d u%016llx c%016llx created\n",
		     proc->pid, current->pid, node->debug_id,
		     (u64)node->ptr, (u64)node->cookie);
	return node;
}

static int binder_inc_node(struct binder_node *node, int strong, int internal,
			   struct list_head *target_list)
{
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
			list_del_init(&node->work.entry);
			list_add_tail(&node->work.entry, target_list);
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
			list_add_tail(&node->work.entry, target_list);
		}
	}
	return 0;
}

static int binder_dec_node(struct binder_node *node, int strong, int internal)
{
	if (strong) {
		if (internal)
			node->internal_strong_refs--;
		else
			node->local_strong_refs--;
		if (node->local_strong_refs || node->internal_strong_refs)
			return 0;
	} else {
		if (!internal)
			node->local_weak_refs--;
		if (node->local_weak_refs || !hlist_empty(&node->refs))
			return 0;
	}
	if (node->proc && (node->has_strong_ref || node->has_weak_ref)) {
		if (list_empty(&node->work.entry)) {
			list_add_tail(&node->work.entry, &node->proc->todo);
			wake_up_interruptible(&node->proc->wait);
		}
	} else {
		if (hlist_empty(&node->refs) && !node->local_strong_refs &&
		    !node->local_weak_refs) {
			list_del_init(&node->work.entry);
			if (node->proc) {
				rb_erase(&node->rb_node, &node->proc->nodes);
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "refless node %d deleted\n",
					     node->debug_id);
			} else {
				spin_lock(&binder_dead_nodes_lock);
				hlist_del(&node->dead_node);
				spin_unlock(&binder_dead_nodes_lock);
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "dead node %d deleted\n",
					     node->debug_id);
			}
			kfree(node);
			binder_stats_deleted(BINDER_STAT_NODE);
		}
	}

	return 0;
}


static struct binder_ref *binder_get_ref(struct binder_proc *proc,
					 u32 desc, bool need_strong_ref)
{
	struct rb_node *n = proc->refs_by_desc.rb_node;
	struct binder_ref *ref;

	while (n) {
		ref = rb_entry(n, struct binder_ref, rb_node_desc);

		if (desc < ref->desc) {
			n = n->rb_left;
		} else if (desc > ref->desc) {
			n = n->rb_right;
		} else if (need_strong_ref && !ref->strong) {
			binder_user_error("tried to use weak ref as strong ref\n");
			return NULL;
		} else {
			return ref;
		}
	}
	return NULL;
}

static struct binder_ref *binder_get_ref_for_node(struct binder_proc *proc,
						  struct binder_node *node)
{
	struct rb_node *n;
	struct rb_node **p = &proc->refs_by_node.rb_node;
	struct rb_node *parent = NULL;
	struct binder_ref *ref, *new_ref;
	struct binder_context *context = proc->context;

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
	new_ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (new_ref == NULL)
		return NULL;
	binder_stats_created(BINDER_STAT_REF);
	new_ref->debug_id = atomic_inc_return(&binder_last_id);
	new_ref->proc = proc;
	new_ref->node = node;
	rb_link_node(&new_ref->rb_node_node, parent, p);
	rb_insert_color(&new_ref->rb_node_node, &proc->refs_by_node);

	new_ref->desc = (node == context->binder_context_mgr_node) ? 0 : 1;
	for (n = rb_first(&proc->refs_by_desc); n != NULL; n = rb_next(n)) {
		ref = rb_entry(n, struct binder_ref, rb_node_desc);
		if (ref->desc > new_ref->desc)
			break;
		new_ref->desc = ref->desc + 1;
	}

	p = &proc->refs_by_desc.rb_node;
	while (*p) {
		parent = *p;
		ref = rb_entry(parent, struct binder_ref, rb_node_desc);

		if (new_ref->desc < ref->desc)
			p = &(*p)->rb_left;
		else if (new_ref->desc > ref->desc)
			p = &(*p)->rb_right;
		else
			BUG();
	}
	rb_link_node(&new_ref->rb_node_desc, parent, p);
	rb_insert_color(&new_ref->rb_node_desc, &proc->refs_by_desc);
	hlist_add_head(&new_ref->node_entry, &node->refs);

	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d new ref %d desc %d for node %d\n",
		      proc->pid, new_ref->debug_id, new_ref->desc,
		      node->debug_id);
	return new_ref;
}

static void binder_delete_ref(struct binder_ref *ref)
{
	binder_debug(BINDER_DEBUG_INTERNAL_REFS,
		     "%d delete ref %d desc %d for node %d\n",
		      ref->proc->pid, ref->debug_id, ref->desc,
		      ref->node->debug_id);

	rb_erase(&ref->rb_node_desc, &ref->proc->refs_by_desc);
	rb_erase(&ref->rb_node_node, &ref->proc->refs_by_node);
	if (ref->strong)
		binder_dec_node(ref->node, 1, 1);
	hlist_del(&ref->node_entry);
	binder_dec_node(ref->node, 0, 1);
	if (ref->death) {
		binder_debug(BINDER_DEBUG_DEAD_BINDER,
			     "%d delete ref %d desc %d has death notification\n",
			      ref->proc->pid, ref->debug_id, ref->desc);
		list_del(&ref->death->work.entry);
		kfree(ref->death);
		binder_stats_deleted(BINDER_STAT_DEATH);
	}
	kfree(ref);
	binder_stats_deleted(BINDER_STAT_REF);
}

static int binder_inc_ref(struct binder_ref *ref, int strong,
			  struct list_head *target_list)
{
	int ret;

	if (strong) {
		if (ref->strong == 0) {
			ret = binder_inc_node(ref->node, 1, 1, target_list);
			if (ret)
				return ret;
		}
		ref->strong++;
	} else {
		if (ref->weak == 0) {
			ret = binder_inc_node(ref->node, 0, 1, target_list);
			if (ret)
				return ret;
		}
		ref->weak++;
	}
	return 0;
}


static int binder_dec_ref(struct binder_ref *ref, int strong)
{
	if (strong) {
		if (ref->strong == 0) {
			binder_user_error("%d invalid dec strong, ref %d desc %d s %d w %d\n",
					  ref->proc->pid, ref->debug_id,
					  ref->desc, ref->strong, ref->weak);
			return -EINVAL;
		}
		ref->strong--;
		if (ref->strong == 0) {
			int ret;

			ret = binder_dec_node(ref->node, strong, 1);
			if (ret)
				return ret;
		}
	} else {
		if (ref->weak == 0) {
			binder_user_error("%d invalid dec weak, ref %d desc %d s %d w %d\n",
					  ref->proc->pid, ref->debug_id,
					  ref->desc, ref->strong, ref->weak);
			return -EINVAL;
		}
		ref->weak--;
	}
	if (ref->strong == 0 && ref->weak == 0)
		binder_delete_ref(ref);
	return 0;
}

static void binder_pop_transaction(struct binder_thread *target_thread,
				   struct binder_transaction *t)
{
	BUG_ON(!target_thread);
	BUG_ON(target_thread->transaction_stack != t);
	BUG_ON(target_thread->transaction_stack->from != target_thread);
	target_thread->transaction_stack =
		target_thread->transaction_stack->from_parent;
	t->from = NULL;
}

static void binder_free_transaction(struct binder_transaction *t)
{
	if (t->buffer)
		t->buffer->transaction = NULL;
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
		target_thread = t->from;
		if (target_thread) {
			binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
				     "send failed reply for transaction %d to %d:%d\n",
				      t->debug_id,
				      target_thread->proc->pid,
				      target_thread->pid);

			binder_pop_transaction(target_thread, t);
			if (target_thread->reply_error.cmd == BR_OK) {
				target_thread->reply_error.cmd = error_code;
				list_add_tail(
					&target_thread->reply_error.work.entry,
					&target_thread->todo);
				wake_up_interruptible(&target_thread->wait);
			} else {
				WARN(1, "Unexpected reply error: %u\n",
						target_thread->reply_error.cmd);
			}
			binder_free_transaction(t);
			return;
		}
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
 * binder_validate_object() - checks for a valid metadata object in a buffer.
 * @buffer:	binder_buffer that we're parsing.
 * @offset:	offset in the buffer at which to validate an object.
 *
 * Return:	If there's a valid metadata object at @offset in @buffer, the
 *		size of that object. Otherwise, it returns zero.
 */
static size_t binder_validate_object(struct binder_buffer *buffer, u64 offset)
{
	/* Check if we can read a header first */
	struct binder_object_header *hdr;
	size_t object_size = 0;

	if (offset > buffer->data_size - sizeof(*hdr) ||
	    buffer->data_size < sizeof(*hdr) ||
	    !IS_ALIGNED(offset, sizeof(u32)))
		return 0;

	/* Ok, now see if we can read a complete object. */
	hdr = (struct binder_object_header *)(buffer->data + offset);
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
 * @b:		binder_buffer containing the object
 * @index:	index in offset array at which the binder_buffer_object is
 *		located
 * @start:	points to the start of the offset array
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
 */
static struct binder_buffer_object *binder_validate_ptr(struct binder_buffer *b,
							binder_size_t index,
							binder_size_t *start,
							binder_size_t num_valid)
{
	struct binder_buffer_object *buffer_obj;
	binder_size_t *offp;

	if (index >= num_valid)
		return NULL;

	offp = start + index;
	buffer_obj = (struct binder_buffer_object *)(b->data + *offp);
	if (buffer_obj->hdr.type != BINDER_TYPE_PTR)
		return NULL;

	return buffer_obj;
}

/**
 * binder_validate_fixup() - validates pointer/fd fixups happen in order.
 * @b:			transaction buffer
 * @objects_start	start of objects buffer
 * @buffer:		binder_buffer_object in which to fix up
 * @offset:		start offset in @buffer to fix up
 * @last_obj:		last binder_buffer_object that we fixed up in
 * @last_min_offset:	minimum fixup offset in @last_obj
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
static bool binder_validate_fixup(struct binder_buffer *b,
				  binder_size_t *objects_start,
				  struct binder_buffer_object *buffer,
				  binder_size_t fixup_offset,
				  struct binder_buffer_object *last_obj,
				  binder_size_t last_min_offset)
{
	if (!last_obj) {
		/* Nothing to fix up in */
		return false;
	}

	while (last_obj != buffer) {
		/*
		 * Safe to retrieve the parent of last_obj, since it
		 * was already previously verified by the driver.
		 */
		if ((last_obj->flags & BINDER_BUFFER_FLAG_HAS_PARENT) == 0)
			return false;
		last_min_offset = last_obj->parent_offset + sizeof(uintptr_t);
		last_obj = (struct binder_buffer_object *)
			(b->data + *(objects_start + last_obj->parent));
	}
	return (fixup_offset >= last_min_offset);
}

static void binder_transaction_buffer_release(struct binder_proc *proc,
					      struct binder_buffer *buffer,
					      binder_size_t *failed_at)
{
	binder_size_t *offp, *off_start, *off_end;
	int debug_id = buffer->debug_id;

	binder_debug(BINDER_DEBUG_TRANSACTION,
		     "%d buffer release %d, size %zd-%zd, failed at %p\n",
		     proc->pid, buffer->debug_id,
		     buffer->data_size, buffer->offsets_size, failed_at);

	if (buffer->target_node)
		binder_dec_node(buffer->target_node, 1, 0);

	off_start = (binder_size_t *)(buffer->data +
				      ALIGN(buffer->data_size, sizeof(void *)));
	if (failed_at)
		off_end = failed_at;
	else
		off_end = (void *)off_start + buffer->offsets_size;
	for (offp = off_start; offp < off_end; offp++) {
		struct binder_object_header *hdr;
		size_t object_size = binder_validate_object(buffer, *offp);

		if (object_size == 0) {
			pr_err("transaction release %d bad object at offset %lld, size %zd\n",
			       debug_id, (u64)*offp, buffer->data_size);
			continue;
		}
		hdr = (struct binder_object_header *)(buffer->data + *offp);
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
		} break;
		case BINDER_TYPE_HANDLE:
		case BINDER_TYPE_WEAK_HANDLE: {
			struct flat_binder_object *fp;
			struct binder_ref *ref;

			fp = to_flat_binder_object(hdr);
			ref = binder_get_ref(proc, fp->handle,
					     hdr->type == BINDER_TYPE_HANDLE);
			if (ref == NULL) {
				pr_err("transaction release %d bad handle %d\n",
				 debug_id, fp->handle);
				break;
			}
			binder_debug(BINDER_DEBUG_TRANSACTION,
				     "        ref %d desc %d (node %d)\n",
				     ref->debug_id, ref->desc, ref->node->debug_id);
			binder_dec_ref(ref, hdr->type == BINDER_TYPE_HANDLE);
		} break;

		case BINDER_TYPE_FD: {
			struct binder_fd_object *fp = to_binder_fd_object(hdr);

			binder_debug(BINDER_DEBUG_TRANSACTION,
				     "        fd %d\n", fp->fd);
			if (failed_at)
				task_close_fd(proc, fp->fd);
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
			uintptr_t parent_buffer;
			u32 *fd_array;
			size_t fd_index;
			binder_size_t fd_buf_size;

			fda = to_binder_fd_array_object(hdr);
			parent = binder_validate_ptr(buffer, fda->parent,
						     off_start,
						     offp - off_start);
			if (!parent) {
				pr_err("transaction release %d bad parent offset",
				       debug_id);
				continue;
			}
			/*
			 * Since the parent was already fixed up, convert it
			 * back to kernel address space to access it
			 */
			parent_buffer = parent->buffer -
				binder_alloc_get_user_buffer_offset(
						&proc->alloc);

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
			fd_array = (u32 *)(parent_buffer + fda->parent_offset);
			for (fd_index = 0; fd_index < fda->num_fds; fd_index++)
				task_close_fd(proc, fd_array[fd_index]);
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
	struct binder_ref *ref;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;

	node = binder_get_node(proc, fp->binder);
	if (!node) {
		node = binder_new_node(proc, fp->binder, fp->cookie);
		if (!node)
			return -ENOMEM;

		node->min_priority = fp->flags & FLAT_BINDER_FLAG_PRIORITY_MASK;
		node->accept_fds = !!(fp->flags & FLAT_BINDER_FLAG_ACCEPTS_FDS);
	}
	if (fp->cookie != node->cookie) {
		binder_user_error("%d:%d sending u%016llx node %d, cookie mismatch %016llx != %016llx\n",
				  proc->pid, thread->pid, (u64)fp->binder,
				  node->debug_id, (u64)fp->cookie,
				  (u64)node->cookie);
		return -EINVAL;
	}
	if (security_binder_transfer_binder(proc->tsk, target_proc->tsk))
		return -EPERM;

	ref = binder_get_ref_for_node(target_proc, node);
	if (!ref)
		return -ENOMEM;

	if (fp->hdr.type == BINDER_TYPE_BINDER)
		fp->hdr.type = BINDER_TYPE_HANDLE;
	else
		fp->hdr.type = BINDER_TYPE_WEAK_HANDLE;
	fp->binder = 0;
	fp->handle = ref->desc;
	fp->cookie = 0;
	binder_inc_ref(ref, fp->hdr.type == BINDER_TYPE_HANDLE, &thread->todo);

	trace_binder_transaction_node_to_ref(t, node, ref);
	binder_debug(BINDER_DEBUG_TRANSACTION,
		     "        node %d u%016llx -> ref %d desc %d\n",
		     node->debug_id, (u64)node->ptr,
		     ref->debug_id, ref->desc);

	return 0;
}

static int binder_translate_handle(struct flat_binder_object *fp,
				   struct binder_transaction *t,
				   struct binder_thread *thread)
{
	struct binder_ref *ref;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;

	ref = binder_get_ref(proc, fp->handle,
			     fp->hdr.type == BINDER_TYPE_HANDLE);
	if (!ref) {
		binder_user_error("%d:%d got transaction with invalid handle, %d\n",
				  proc->pid, thread->pid, fp->handle);
		return -EINVAL;
	}
	if (security_binder_transfer_binder(proc->tsk, target_proc->tsk))
		return -EPERM;

	if (ref->node->proc == target_proc) {
		if (fp->hdr.type == BINDER_TYPE_HANDLE)
			fp->hdr.type = BINDER_TYPE_BINDER;
		else
			fp->hdr.type = BINDER_TYPE_WEAK_BINDER;
		fp->binder = ref->node->ptr;
		fp->cookie = ref->node->cookie;
		binder_inc_node(ref->node, fp->hdr.type == BINDER_TYPE_BINDER,
				0, NULL);
		trace_binder_transaction_ref_to_node(t, ref);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "        ref %d desc %d -> node %d u%016llx\n",
			     ref->debug_id, ref->desc, ref->node->debug_id,
			     (u64)ref->node->ptr);
	} else {
		struct binder_ref *new_ref;

		new_ref = binder_get_ref_for_node(target_proc, ref->node);
		if (!new_ref)
			return -ENOMEM;

		fp->binder = 0;
		fp->handle = new_ref->desc;
		fp->cookie = 0;
		binder_inc_ref(new_ref, fp->hdr.type == BINDER_TYPE_HANDLE,
			       NULL);
		trace_binder_transaction_ref_to_ref(t, ref, new_ref);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "        ref %d desc %d -> ref %d desc %d (node %d)\n",
			     ref->debug_id, ref->desc, new_ref->debug_id,
			     new_ref->desc, ref->node->debug_id);
	}
	return 0;
}

static int binder_translate_fd(int fd,
			       struct binder_transaction *t,
			       struct binder_thread *thread,
			       struct binder_transaction *in_reply_to)
{
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;
	int target_fd;
	struct file *file;
	int ret;
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
	ret = security_binder_transfer_file(proc->tsk, target_proc->tsk, file);
	if (ret < 0) {
		ret = -EPERM;
		goto err_security;
	}

	target_fd = task_get_unused_fd_flags(target_proc, O_CLOEXEC);
	if (target_fd < 0) {
		ret = -ENOMEM;
		goto err_get_unused_fd;
	}
	task_fd_install(target_proc, target_fd, file);
	trace_binder_transaction_fd(t, fd, target_fd);
	binder_debug(BINDER_DEBUG_TRANSACTION, "        fd %d -> %d\n",
		     fd, target_fd);

	return target_fd;

err_get_unused_fd:
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
	binder_size_t fdi, fd_buf_size, num_installed_fds;
	int target_fd;
	uintptr_t parent_buffer;
	u32 *fd_array;
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
	 * Since the parent was already fixed up, convert it
	 * back to the kernel address space to access it
	 */
	parent_buffer = parent->buffer -
		binder_alloc_get_user_buffer_offset(&target_proc->alloc);
	fd_array = (u32 *)(parent_buffer + fda->parent_offset);
	if (!IS_ALIGNED((unsigned long)fd_array, sizeof(u32))) {
		binder_user_error("%d:%d parent offset not aligned correctly.\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}
	for (fdi = 0; fdi < fda->num_fds; fdi++) {
		target_fd = binder_translate_fd(fd_array[fdi], t, thread,
						in_reply_to);
		if (target_fd < 0)
			goto err_translate_fd_failed;
		fd_array[fdi] = target_fd;
	}
	return 0;

err_translate_fd_failed:
	/*
	 * Failed to allocate fd or security error, free fds
	 * installed so far.
	 */
	num_installed_fds = fdi;
	for (fdi = 0; fdi < num_installed_fds; fdi++)
		task_close_fd(target_proc, fd_array[fdi]);
	return target_fd;
}

static int binder_fixup_parent(struct binder_transaction *t,
			       struct binder_thread *thread,
			       struct binder_buffer_object *bp,
			       binder_size_t *off_start,
			       binder_size_t num_valid,
			       struct binder_buffer_object *last_fixup_obj,
			       binder_size_t last_fixup_min_off)
{
	struct binder_buffer_object *parent;
	u8 *parent_buffer;
	struct binder_buffer *b = t->buffer;
	struct binder_proc *proc = thread->proc;
	struct binder_proc *target_proc = t->to_proc;

	if (!(bp->flags & BINDER_BUFFER_FLAG_HAS_PARENT))
		return 0;

	parent = binder_validate_ptr(b, bp->parent, off_start, num_valid);
	if (!parent) {
		binder_user_error("%d:%d got transaction with invalid parent offset or type\n",
				  proc->pid, thread->pid);
		return -EINVAL;
	}

	if (!binder_validate_fixup(b, off_start,
				   parent, bp->parent_offset,
				   last_fixup_obj,
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
	parent_buffer = (u8 *)(parent->buffer -
			binder_alloc_get_user_buffer_offset(
				&target_proc->alloc));
	*(binder_uintptr_t *)(parent_buffer + bp->parent_offset) = bp->buffer;

	return 0;
}

static void binder_transaction(struct binder_proc *proc,
			       struct binder_thread *thread,
			       struct binder_transaction_data *tr, int reply,
			       binder_size_t extra_buffers_size)
{
	int ret;
	struct binder_transaction *t;
	struct binder_work *tcomplete;
	binder_size_t *offp, *off_end, *off_start;
	binder_size_t off_min;
	u8 *sg_bufp, *sg_buf_end;
	struct binder_proc *target_proc;
	struct binder_thread *target_thread = NULL;
	struct binder_node *target_node = NULL;
	struct list_head *target_list;
	wait_queue_head_t *target_wait;
	struct binder_transaction *in_reply_to = NULL;
	struct binder_transaction_log_entry *e;
	uint32_t return_error = 0;
	uint32_t return_error_param = 0;
	uint32_t return_error_line = 0;
	struct binder_buffer_object *last_fixup_obj = NULL;
	binder_size_t last_fixup_min_off = 0;
	struct binder_context *context = proc->context;
	int t_debug_id = atomic_inc_return(&binder_last_id);

	e = binder_transaction_log_add(&binder_transaction_log);
	e->debug_id = t_debug_id;
	e->call_type = reply ? 2 : !!(tr->flags & TF_ONE_WAY);
	e->from_proc = proc->pid;
	e->from_thread = thread->pid;
	e->target_handle = tr->target.handle;
	e->data_size = tr->data_size;
	e->offsets_size = tr->offsets_size;
	e->context_name = proc->context->name;

	if (reply) {
		in_reply_to = thread->transaction_stack;
		if (in_reply_to == NULL) {
			binder_user_error("%d:%d got reply transaction with no transaction stack\n",
					  proc->pid, thread->pid);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			goto err_empty_call_stack;
		}
		binder_set_nice(in_reply_to->saved_priority);
		if (in_reply_to->to_thread != thread) {
			binder_user_error("%d:%d got reply transaction with bad transaction stack, transaction %d has target %d:%d\n",
				proc->pid, thread->pid, in_reply_to->debug_id,
				in_reply_to->to_proc ?
				in_reply_to->to_proc->pid : 0,
				in_reply_to->to_thread ?
				in_reply_to->to_thread->pid : 0);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			in_reply_to = NULL;
			goto err_bad_call_stack;
		}
		thread->transaction_stack = in_reply_to->to_parent;
		target_thread = in_reply_to->from;
		if (target_thread == NULL) {
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
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPROTO;
			return_error_line = __LINE__;
			in_reply_to = NULL;
			target_thread = NULL;
			goto err_dead_binder;
		}
		target_proc = target_thread->proc;
	} else {
		if (tr->target.handle) {
			struct binder_ref *ref;

			ref = binder_get_ref(proc, tr->target.handle, true);
			if (ref == NULL) {
				binder_user_error("%d:%d got transaction to invalid handle\n",
					proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_invalid_target_handle;
			}
			target_node = ref->node;
		} else {
			mutex_lock(&context->context_mgr_node_lock);
			target_node = context->binder_context_mgr_node;
			if (target_node == NULL) {
				return_error = BR_DEAD_REPLY;
				mutex_unlock(&context->context_mgr_node_lock);
				return_error_line = __LINE__;
				goto err_no_context_mgr_node;
			}
			mutex_unlock(&context->context_mgr_node_lock);
		}
		e->to_node = target_node->debug_id;
		target_proc = target_node->proc;
		if (target_proc == NULL) {
			return_error = BR_DEAD_REPLY;
			return_error_line = __LINE__;
			goto err_dead_binder;
		}
		if (security_binder_transaction(proc->tsk,
						target_proc->tsk) < 0) {
			return_error = BR_FAILED_REPLY;
			return_error_param = -EPERM;
			return_error_line = __LINE__;
			goto err_invalid_target_handle;
		}
		if (!(tr->flags & TF_ONE_WAY) && thread->transaction_stack) {
			struct binder_transaction *tmp;

			tmp = thread->transaction_stack;
			if (tmp->to_thread != thread) {
				binder_user_error("%d:%d got new transaction with bad transaction stack, transaction %d has target %d:%d\n",
					proc->pid, thread->pid, tmp->debug_id,
					tmp->to_proc ? tmp->to_proc->pid : 0,
					tmp->to_thread ?
					tmp->to_thread->pid : 0);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EPROTO;
				return_error_line = __LINE__;
				goto err_bad_call_stack;
			}
			while (tmp) {
				if (tmp->from && tmp->from->proc == target_proc)
					target_thread = tmp->from;
				tmp = tmp->from_parent;
			}
		}
	}
	if (target_thread) {
		e->to_thread = target_thread->pid;
		target_list = &target_thread->todo;
		target_wait = &target_thread->wait;
	} else {
		target_list = &target_proc->todo;
		target_wait = &target_proc->wait;
	}
	e->to_proc = target_proc->pid;

	/* TODO: reuse incoming transaction for reply */
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL) {
		return_error = BR_FAILED_REPLY;
		return_error_param = -ENOMEM;
		return_error_line = __LINE__;
		goto err_alloc_t_failed;
	}
	binder_stats_created(BINDER_STAT_TRANSACTION);

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
	t->sender_euid = task_euid(proc->tsk);
	t->to_proc = target_proc;
	t->to_thread = target_thread;
	t->code = tr->code;
	t->flags = tr->flags;
	t->priority = task_nice(current);

	trace_binder_transaction(reply, t, target_node);

	t->buffer = binder_alloc_new_buf(&target_proc->alloc, tr->data_size,
		tr->offsets_size, extra_buffers_size,
		!reply && (t->flags & TF_ONE_WAY));
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
	t->buffer->allow_user_free = 0;
	t->buffer->debug_id = t->debug_id;
	t->buffer->transaction = t;
	t->buffer->target_node = target_node;
	trace_binder_transaction_alloc_buf(t->buffer);
	if (target_node)
		binder_inc_node(target_node, 1, 0, NULL);

	off_start = (binder_size_t *)(t->buffer->data +
				      ALIGN(tr->data_size, sizeof(void *)));
	offp = off_start;

	if (copy_from_user(t->buffer->data, (const void __user *)(uintptr_t)
			   tr->data.ptr.buffer, tr->data_size)) {
		binder_user_error("%d:%d got transaction with invalid data ptr\n",
				proc->pid, thread->pid);
		return_error = BR_FAILED_REPLY;
		return_error_param = -EFAULT;
		return_error_line = __LINE__;
		goto err_copy_data_failed;
	}
	if (copy_from_user(offp, (const void __user *)(uintptr_t)
			   tr->data.ptr.offsets, tr->offsets_size)) {
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
	off_end = (void *)off_start + tr->offsets_size;
	sg_bufp = (u8 *)(PTR_ALIGN(off_end, sizeof(void *)));
	sg_buf_end = sg_bufp + extra_buffers_size;
	off_min = 0;
	for (; offp < off_end; offp++) {
		struct binder_object_header *hdr;
		size_t object_size = binder_validate_object(t->buffer, *offp);

		if (object_size == 0 || *offp < off_min) {
			binder_user_error("%d:%d got transaction with invalid offset (%lld, min %lld max %lld) or object.\n",
					  proc->pid, thread->pid, (u64)*offp,
					  (u64)off_min,
					  (u64)t->buffer->data_size);
			return_error = BR_FAILED_REPLY;
			return_error_param = -EINVAL;
			return_error_line = __LINE__;
			goto err_bad_offset;
		}

		hdr = (struct binder_object_header *)(t->buffer->data + *offp);
		off_min = *offp + object_size;
		switch (hdr->type) {
		case BINDER_TYPE_BINDER:
		case BINDER_TYPE_WEAK_BINDER: {
			struct flat_binder_object *fp;

			fp = to_flat_binder_object(hdr);
			ret = binder_translate_binder(fp, t, thread);
			if (ret < 0) {
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
			if (ret < 0) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
		} break;

		case BINDER_TYPE_FD: {
			struct binder_fd_object *fp = to_binder_fd_object(hdr);
			int target_fd = binder_translate_fd(fp->fd, t, thread,
							    in_reply_to);

			if (target_fd < 0) {
				return_error = BR_FAILED_REPLY;
				return_error_param = target_fd;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
			fp->pad_binder = 0;
			fp->fd = target_fd;
		} break;
		case BINDER_TYPE_FDA: {
			struct binder_fd_array_object *fda =
				to_binder_fd_array_object(hdr);
			struct binder_buffer_object *parent =
				binder_validate_ptr(t->buffer, fda->parent,
						    off_start,
						    offp - off_start);
			if (!parent) {
				binder_user_error("%d:%d got transaction with invalid parent offset or type\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_bad_parent;
			}
			if (!binder_validate_fixup(t->buffer, off_start,
						   parent, fda->parent_offset,
						   last_fixup_obj,
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
			last_fixup_obj = parent;
			last_fixup_min_off =
				fda->parent_offset + sizeof(u32) * fda->num_fds;
		} break;
		case BINDER_TYPE_PTR: {
			struct binder_buffer_object *bp =
				to_binder_buffer_object(hdr);
			size_t buf_left = sg_buf_end - sg_bufp;

			if (bp->length > buf_left) {
				binder_user_error("%d:%d got transaction with too large buffer\n",
						  proc->pid, thread->pid);
				return_error = BR_FAILED_REPLY;
				return_error_param = -EINVAL;
				return_error_line = __LINE__;
				goto err_bad_offset;
			}
			if (copy_from_user(sg_bufp,
					   (const void __user *)(uintptr_t)
					   bp->buffer, bp->length)) {
				binder_user_error("%d:%d got transaction with invalid offsets ptr\n",
						  proc->pid, thread->pid);
				return_error_param = -EFAULT;
				return_error = BR_FAILED_REPLY;
				return_error_line = __LINE__;
				goto err_copy_data_failed;
			}
			/* Fixup buffer pointer to target proc address space */
			bp->buffer = (uintptr_t)sg_bufp +
				binder_alloc_get_user_buffer_offset(
						&target_proc->alloc);
			sg_bufp += ALIGN(bp->length, sizeof(u64));

			ret = binder_fixup_parent(t, thread, bp, off_start,
						  offp - off_start,
						  last_fixup_obj,
						  last_fixup_min_off);
			if (ret < 0) {
				return_error = BR_FAILED_REPLY;
				return_error_param = ret;
				return_error_line = __LINE__;
				goto err_translate_failed;
			}
			last_fixup_obj = bp;
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
	tcomplete->type = BINDER_WORK_TRANSACTION_COMPLETE;
	list_add_tail(&tcomplete->entry, &thread->todo);

	if (reply) {
		BUG_ON(t->buffer->async_transaction != 0);
		binder_pop_transaction(target_thread, in_reply_to);
		binder_free_transaction(in_reply_to);
	} else if (!(t->flags & TF_ONE_WAY)) {
		BUG_ON(t->buffer->async_transaction != 0);
		t->need_reply = 1;
		t->from_parent = thread->transaction_stack;
		thread->transaction_stack = t;
	} else {
		BUG_ON(target_node == NULL);
		BUG_ON(t->buffer->async_transaction != 1);
		if (target_node->has_async_transaction) {
			target_list = &target_node->async_todo;
			target_wait = NULL;
		} else
			target_node->has_async_transaction = 1;
	}
	t->work.type = BINDER_WORK_TRANSACTION;
	list_add_tail(&t->work.entry, target_list);
	if (target_wait) {
		if (reply || !(tr->flags & TF_ONE_WAY))
			wake_up_interruptible_sync(target_wait);
		else
			wake_up_interruptible(target_wait);
	}
	/*
	 * write barrier to synchronize with initialization
	 * of log entry
	 */
	smp_wmb();
	WRITE_ONCE(e->debug_id_done, t_debug_id);
	return;

err_translate_failed:
err_bad_object_type:
err_bad_offset:
err_bad_parent:
err_copy_data_failed:
	trace_binder_transaction_failed_buffer_release(t->buffer);
	binder_transaction_buffer_release(target_proc, t->buffer, offp);
	t->buffer->transaction = NULL;
	binder_alloc_free_buf(&target_proc->alloc, t->buffer);
err_binder_alloc_buf_failed:
	kfree(tcomplete);
	binder_stats_deleted(BINDER_STAT_TRANSACTION_COMPLETE);
err_alloc_tcomplete_failed:
	kfree(t);
	binder_stats_deleted(BINDER_STAT_TRANSACTION);
err_alloc_t_failed:
err_bad_call_stack:
err_empty_call_stack:
err_dead_binder:
err_invalid_target_handle:
err_no_context_mgr_node:
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
		list_add_tail(&thread->return_error.work.entry,
			      &thread->todo);
		binder_send_failed_reply(in_reply_to, return_error);
	} else {
		thread->return_error.cmd = return_error;
		list_add_tail(&thread->return_error.work.entry,
			      &thread->todo);
	}
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
			struct binder_ref *ref = NULL;
			const char *debug_string;

			if (get_user(target, (uint32_t __user *)ptr))
				return -EFAULT;

			ptr += sizeof(uint32_t);
			if (target == 0 &&
			    (cmd == BC_INCREFS || cmd == BC_ACQUIRE)) {
				struct binder_node *ctx_mgr_node;

				mutex_lock(&context->context_mgr_node_lock);
				ctx_mgr_node = context->binder_context_mgr_node;
				if (ctx_mgr_node) {
					ref = binder_get_ref_for_node(proc,
							ctx_mgr_node);
					if (ref && ref->desc != target) {
						binder_user_error("%d:%d tried to acquire reference to desc 0, got %d instead\n",
							proc->pid, thread->pid,
							ref->desc);
					}
				}
				mutex_unlock(&context->context_mgr_node_lock);
			}
			if (ref == NULL)
				ref = binder_get_ref(proc, target,
						     cmd == BC_ACQUIRE ||
						     cmd == BC_RELEASE);
			if (ref == NULL) {
				binder_user_error("%d:%d refcount change on invalid ref %d\n",
					proc->pid, thread->pid, target);
				break;
			}
			switch (cmd) {
			case BC_INCREFS:
				debug_string = "IncRefs";
				binder_inc_ref(ref, 0, NULL);
				break;
			case BC_ACQUIRE:
				debug_string = "Acquire";
				binder_inc_ref(ref, 1, NULL);
				break;
			case BC_RELEASE:
				debug_string = "Release";
				binder_dec_ref(ref, 1);
				break;
			case BC_DECREFS:
			default:
				debug_string = "DecRefs";
				binder_dec_ref(ref, 0);
				break;
			}
			binder_debug(BINDER_DEBUG_USER_REFS,
				     "%d:%d %s ref %d desc %d s %d w %d for node %d\n",
				     proc->pid, thread->pid, debug_string, ref->debug_id,
				     ref->desc, ref->strong, ref->weak, ref->node->debug_id);
			break;
		}
		case BC_INCREFS_DONE:
		case BC_ACQUIRE_DONE: {
			binder_uintptr_t node_ptr;
			binder_uintptr_t cookie;
			struct binder_node *node;

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
				break;
			}
			if (cmd == BC_ACQUIRE_DONE) {
				if (node->pending_strong_ref == 0) {
					binder_user_error("%d:%d BC_ACQUIRE_DONE node %d has no pending acquire request\n",
						proc->pid, thread->pid,
						node->debug_id);
					break;
				}
				node->pending_strong_ref = 0;
			} else {
				if (node->pending_weak_ref == 0) {
					binder_user_error("%d:%d BC_INCREFS_DONE node %d has no pending increfs request\n",
						proc->pid, thread->pid,
						node->debug_id);
					break;
				}
				node->pending_weak_ref = 0;
			}
			binder_dec_node(node, cmd == BC_ACQUIRE_DONE, 0);
			binder_debug(BINDER_DEBUG_USER_REFS,
				     "%d:%d %s node %d ls %d lw %d\n",
				     proc->pid, thread->pid,
				     cmd == BC_INCREFS_DONE ? "BC_INCREFS_DONE" : "BC_ACQUIRE_DONE",
				     node->debug_id, node->local_strong_refs, node->local_weak_refs);
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
			if (buffer == NULL) {
				binder_user_error("%d:%d BC_FREE_BUFFER u%016llx no match\n",
					proc->pid, thread->pid, (u64)data_ptr);
				break;
			}
			if (!buffer->allow_user_free) {
				binder_user_error("%d:%d BC_FREE_BUFFER u%016llx matched unreturned buffer\n",
					proc->pid, thread->pid, (u64)data_ptr);
				break;
			}
			binder_debug(BINDER_DEBUG_FREE_BUFFER,
				     "%d:%d BC_FREE_BUFFER u%016llx found buffer %d for %s transaction\n",
				     proc->pid, thread->pid, (u64)data_ptr,
				     buffer->debug_id,
				     buffer->transaction ? "active" : "finished");

			if (buffer->transaction) {
				buffer->transaction->buffer = NULL;
				buffer->transaction = NULL;
			}
			if (buffer->async_transaction && buffer->target_node) {
				BUG_ON(!buffer->target_node->has_async_transaction);
				if (list_empty(&buffer->target_node->async_todo))
					buffer->target_node->has_async_transaction = 0;
				else
					list_move_tail(buffer->target_node->async_todo.next, &thread->todo);
			}
			trace_binder_transaction_buffer_release(buffer);
			binder_transaction_buffer_release(proc, buffer, NULL);
			binder_alloc_free_buf(&proc->alloc, buffer);
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
			struct binder_ref_death *death;

			if (get_user(target, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);
			if (get_user(cookie, (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			ref = binder_get_ref(proc, target, false);
			if (ref == NULL) {
				binder_user_error("%d:%d %s invalid ref %d\n",
					proc->pid, thread->pid,
					cmd == BC_REQUEST_DEATH_NOTIFICATION ?
					"BC_REQUEST_DEATH_NOTIFICATION" :
					"BC_CLEAR_DEATH_NOTIFICATION",
					target);
				break;
			}

			binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION,
				     "%d:%d %s %016llx ref %d desc %d s %d w %d for node %d\n",
				     proc->pid, thread->pid,
				     cmd == BC_REQUEST_DEATH_NOTIFICATION ?
				     "BC_REQUEST_DEATH_NOTIFICATION" :
				     "BC_CLEAR_DEATH_NOTIFICATION",
				     (u64)cookie, ref->debug_id, ref->desc,
				     ref->strong, ref->weak, ref->node->debug_id);

			if (cmd == BC_REQUEST_DEATH_NOTIFICATION) {
				if (ref->death) {
					binder_user_error("%d:%d BC_REQUEST_DEATH_NOTIFICATION death notification already set\n",
						proc->pid, thread->pid);
					break;
				}
				death = kzalloc(sizeof(*death), GFP_KERNEL);
				if (death == NULL) {
					WARN_ON(thread->return_error.cmd !=
						BR_OK);
					thread->return_error.cmd = BR_ERROR;
					list_add_tail(
					    &thread->return_error.work.entry,
					    &thread->todo);
					binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
						     "%d:%d BC_REQUEST_DEATH_NOTIFICATION failed\n",
						     proc->pid, thread->pid);
					break;
				}
				binder_stats_created(BINDER_STAT_DEATH);
				INIT_LIST_HEAD(&death->work.entry);
				death->cookie = cookie;
				ref->death = death;
				if (ref->node->proc == NULL) {
					ref->death->work.type = BINDER_WORK_DEAD_BINDER;
					if (thread->looper & (BINDER_LOOPER_STATE_REGISTERED | BINDER_LOOPER_STATE_ENTERED)) {
						list_add_tail(&ref->death->work.entry, &thread->todo);
					} else {
						list_add_tail(&ref->death->work.entry, &proc->todo);
						wake_up_interruptible(&proc->wait);
					}
				}
			} else {
				if (ref->death == NULL) {
					binder_user_error("%d:%d BC_CLEAR_DEATH_NOTIFICATION death notification not active\n",
						proc->pid, thread->pid);
					break;
				}
				death = ref->death;
				if (death->cookie != cookie) {
					binder_user_error("%d:%d BC_CLEAR_DEATH_NOTIFICATION death notification cookie mismatch %016llx != %016llx\n",
						proc->pid, thread->pid,
						(u64)death->cookie,
						(u64)cookie);
					break;
				}
				ref->death = NULL;
				if (list_empty(&death->work.entry)) {
					death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
					if (thread->looper & (BINDER_LOOPER_STATE_REGISTERED | BINDER_LOOPER_STATE_ENTERED)) {
						list_add_tail(&death->work.entry, &thread->todo);
					} else {
						list_add_tail(&death->work.entry, &proc->todo);
						wake_up_interruptible(&proc->wait);
					}
				} else {
					BUG_ON(death->work.type != BINDER_WORK_DEAD_BINDER);
					death->work.type = BINDER_WORK_DEAD_BINDER_AND_CLEAR;
				}
			}
		} break;
		case BC_DEAD_BINDER_DONE: {
			struct binder_work *w;
			binder_uintptr_t cookie;
			struct binder_ref_death *death = NULL;

			if (get_user(cookie, (binder_uintptr_t __user *)ptr))
				return -EFAULT;

			ptr += sizeof(cookie);
			list_for_each_entry(w, &proc->delivered_death, entry) {
				struct binder_ref_death *tmp_death = container_of(w, struct binder_ref_death, work);

				if (tmp_death->cookie == cookie) {
					death = tmp_death;
					break;
				}
			}
			binder_debug(BINDER_DEBUG_DEAD_BINDER,
				     "%d:%d BC_DEAD_BINDER_DONE %016llx found %p\n",
				     proc->pid, thread->pid, (u64)cookie,
				     death);
			if (death == NULL) {
				binder_user_error("%d:%d BC_DEAD_BINDER_DONE %016llx not found\n",
					proc->pid, thread->pid, (u64)cookie);
				break;
			}

			list_del_init(&death->work.entry);
			if (death->work.type == BINDER_WORK_DEAD_BINDER_AND_CLEAR) {
				death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
				if (thread->looper & (BINDER_LOOPER_STATE_REGISTERED | BINDER_LOOPER_STATE_ENTERED)) {
					list_add_tail(&death->work.entry, &thread->todo);
				} else {
					list_add_tail(&death->work.entry, &proc->todo);
					wake_up_interruptible(&proc->wait);
				}
			}
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

static int binder_has_proc_work(struct binder_proc *proc,
				struct binder_thread *thread)
{
	return !list_empty(&proc->todo) || thread->looper_need_return;
}

static int binder_has_thread_work(struct binder_thread *thread)
{
	return !list_empty(&thread->todo) || thread->looper_need_return;
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
	wait_for_proc_work = thread->transaction_stack == NULL &&
				list_empty(&thread->todo);

	thread->looper |= BINDER_LOOPER_STATE_WAITING;
	if (wait_for_proc_work)
		proc->ready_threads++;

	binder_unlock(__func__);

	trace_binder_wait_for_work(wait_for_proc_work,
				   !!thread->transaction_stack,
				   !list_empty(&thread->todo));
	if (wait_for_proc_work) {
		if (!(thread->looper & (BINDER_LOOPER_STATE_REGISTERED |
					BINDER_LOOPER_STATE_ENTERED))) {
			binder_user_error("%d:%d ERROR: Thread waiting for process work before calling BC_REGISTER_LOOPER or BC_ENTER_LOOPER (state %x)\n",
				proc->pid, thread->pid, thread->looper);
			wait_event_interruptible(binder_user_error_wait,
						 binder_stop_on_user_error < 2);
		}
		binder_set_nice(proc->default_priority);
		if (non_block) {
			if (!binder_has_proc_work(proc, thread))
				ret = -EAGAIN;
		} else
			ret = wait_event_freezable_exclusive(proc->wait, binder_has_proc_work(proc, thread));
	} else {
		if (non_block) {
			if (!binder_has_thread_work(thread))
				ret = -EAGAIN;
		} else
			ret = wait_event_freezable(thread->wait, binder_has_thread_work(thread));
	}

	binder_lock(__func__);

	if (wait_for_proc_work)
		proc->ready_threads--;
	thread->looper &= ~BINDER_LOOPER_STATE_WAITING;

	if (ret)
		return ret;

	while (1) {
		uint32_t cmd;
		struct binder_transaction_data tr;
		struct binder_work *w;
		struct binder_transaction *t = NULL;

		if (!list_empty(&thread->todo)) {
			w = list_first_entry(&thread->todo, struct binder_work,
					     entry);
		} else if (!list_empty(&proc->todo) && wait_for_proc_work) {
			w = list_first_entry(&proc->todo, struct binder_work,
					     entry);
		} else {
			/* no data added */
			if (ptr - buffer == 4 && !thread->looper_need_return)
				goto retry;
			break;
		}

		if (end - ptr < sizeof(tr) + 4)
			break;

		switch (w->type) {
		case BINDER_WORK_TRANSACTION: {
			t = container_of(w, struct binder_transaction, work);
		} break;
		case BINDER_WORK_RETURN_ERROR: {
			struct binder_error *e = container_of(
					w, struct binder_error, work);

			WARN_ON(e->cmd == BR_OK);
			if (put_user(e->cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			e->cmd = BR_OK;
			ptr += sizeof(uint32_t);

			binder_stat_br(proc, thread, cmd);
			list_del(&w->entry);
		} break;
		case BINDER_WORK_TRANSACTION_COMPLETE: {
			cmd = BR_TRANSACTION_COMPLETE;
			if (put_user(cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);

			binder_stat_br(proc, thread, cmd);
			binder_debug(BINDER_DEBUG_TRANSACTION_COMPLETE,
				     "%d:%d BR_TRANSACTION_COMPLETE\n",
				     proc->pid, thread->pid);

			list_del(&w->entry);
			kfree(w);
			binder_stats_deleted(BINDER_STAT_TRANSACTION_COMPLETE);
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
					node->local_weak_refs || strong;
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
			list_del(&w->entry);

			if (!weak && !strong) {
				binder_debug(BINDER_DEBUG_INTERNAL_REFS,
					     "%d:%d node %d u%016llx c%016llx deleted\n",
					     proc->pid, thread->pid,
					     node_debug_id,
					     (u64)node_ptr,
					     (u64)node_cookie);
				rb_erase(&node->rb_node, &proc->nodes);
				kfree(node);
				binder_stats_deleted(BINDER_STAT_NODE);
			}
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

			death = container_of(w, struct binder_ref_death, work);
			if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION)
				cmd = BR_CLEAR_DEATH_NOTIFICATION_DONE;
			else
				cmd = BR_DEAD_BINDER;
			if (put_user(cmd, (uint32_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(uint32_t);
			if (put_user(death->cookie,
				     (binder_uintptr_t __user *)ptr))
				return -EFAULT;
			ptr += sizeof(binder_uintptr_t);
			binder_stat_br(proc, thread, cmd);
			binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION,
				     "%d:%d %s %016llx\n",
				      proc->pid, thread->pid,
				      cmd == BR_DEAD_BINDER ?
				      "BR_DEAD_BINDER" :
				      "BR_CLEAR_DEATH_NOTIFICATION_DONE",
				      (u64)death->cookie);

			if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION) {
				list_del(&w->entry);
				kfree(death);
				binder_stats_deleted(BINDER_STAT_DEATH);
			} else
				list_move(&w->entry, &proc->delivered_death);
			if (cmd == BR_DEAD_BINDER)
				goto done; /* DEAD_BINDER notifications can cause transactions */
		} break;
		}

		if (!t)
			continue;

		BUG_ON(t->buffer == NULL);
		if (t->buffer->target_node) {
			struct binder_node *target_node = t->buffer->target_node;

			tr.target.ptr = target_node->ptr;
			tr.cookie =  target_node->cookie;
			t->saved_priority = task_nice(current);
			if (t->priority < target_node->min_priority &&
			    !(t->flags & TF_ONE_WAY))
				binder_set_nice(t->priority);
			else if (!(t->flags & TF_ONE_WAY) ||
				 t->saved_priority > target_node->min_priority)
				binder_set_nice(target_node->min_priority);
			cmd = BR_TRANSACTION;
		} else {
			tr.target.ptr = 0;
			tr.cookie = 0;
			cmd = BR_REPLY;
		}
		tr.code = t->code;
		tr.flags = t->flags;
		tr.sender_euid = from_kuid(current_user_ns(), t->sender_euid);

		if (t->from) {
			struct task_struct *sender = t->from->proc->tsk;

			tr.sender_pid = task_tgid_nr_ns(sender,
							task_active_pid_ns(current));
		} else {
			tr.sender_pid = 0;
		}

		tr.data_size = t->buffer->data_size;
		tr.offsets_size = t->buffer->offsets_size;
		tr.data.ptr.buffer = (binder_uintptr_t)
			((uintptr_t)t->buffer->data +
			binder_alloc_get_user_buffer_offset(&proc->alloc));
		tr.data.ptr.offsets = tr.data.ptr.buffer +
					ALIGN(t->buffer->data_size,
					    sizeof(void *));

		if (put_user(cmd, (uint32_t __user *)ptr))
			return -EFAULT;
		ptr += sizeof(uint32_t);
		if (copy_to_user(ptr, &tr, sizeof(tr)))
			return -EFAULT;
		ptr += sizeof(tr);

		trace_binder_transaction_received(t);
		binder_stat_br(proc, thread, cmd);
		binder_debug(BINDER_DEBUG_TRANSACTION,
			     "%d:%d %s %d %d:%d, cmd %d size %zd-%zd ptr %016llx-%016llx\n",
			     proc->pid, thread->pid,
			     (cmd == BR_TRANSACTION) ? "BR_TRANSACTION" :
			     "BR_REPLY",
			     t->debug_id, t->from ? t->from->proc->pid : 0,
			     t->from ? t->from->pid : 0, cmd,
			     t->buffer->data_size, t->buffer->offsets_size,
			     (u64)tr.data.ptr.buffer, (u64)tr.data.ptr.offsets);

		list_del(&t->work.entry);
		t->buffer->allow_user_free = 1;
		if (cmd == BR_TRANSACTION && !(t->flags & TF_ONE_WAY)) {
			t->to_parent = thread->transaction_stack;
			t->to_thread = thread;
			thread->transaction_stack = t;
		} else {
			binder_free_transaction(t);
		}
		break;
	}

done:

	*consumed = ptr - buffer;
	if (proc->requested_threads + proc->ready_threads == 0 &&
	    proc->requested_threads_started < proc->max_threads &&
	    (thread->looper & (BINDER_LOOPER_STATE_REGISTERED |
	     BINDER_LOOPER_STATE_ENTERED)) /* the user-space code fails to */
	     /*spawn a new thread if we leave this out */) {
		proc->requested_threads++;
		binder_debug(BINDER_DEBUG_THREADS,
			     "%d:%d BR_SPAWN_LOOPER\n",
			     proc->pid, thread->pid);
		if (put_user(BR_SPAWN_LOOPER, (uint32_t __user *)buffer))
			return -EFAULT;
		binder_stat_br(proc, thread, BR_SPAWN_LOOPER);
	}
	return 0;
}

static void binder_release_work(struct list_head *list)
{
	struct binder_work *w;

	while (!list_empty(list)) {
		w = list_first_entry(list, struct binder_work, entry);
		list_del_init(&w->entry);
		switch (w->type) {
		case BINDER_WORK_TRANSACTION: {
			struct binder_transaction *t;

			t = container_of(w, struct binder_transaction, work);
			if (t->buffer->target_node &&
			    !(t->flags & TF_ONE_WAY)) {
				binder_send_failed_reply(t, BR_DEAD_REPLY);
			} else {
				binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
					"undelivered transaction %d\n",
					t->debug_id);
				binder_free_transaction(t);
			}
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
		default:
			pr_err("unexpected work type, %d, not freed\n",
			       w->type);
			break;
		}
	}

}

static struct binder_thread *binder_get_thread(struct binder_proc *proc)
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
			break;
	}
	if (*p == NULL) {
		thread = kzalloc(sizeof(*thread), GFP_KERNEL);
		if (thread == NULL)
			return NULL;
		binder_stats_created(BINDER_STAT_THREAD);
		thread->proc = proc;
		thread->pid = current->pid;
		init_waitqueue_head(&thread->wait);
		INIT_LIST_HEAD(&thread->todo);
		rb_link_node(&thread->rb_node, parent, p);
		rb_insert_color(&thread->rb_node, &proc->threads);
		thread->looper_need_return = true;
		thread->return_error.work.type = BINDER_WORK_RETURN_ERROR;
		thread->return_error.cmd = BR_OK;
		thread->reply_error.work.type = BINDER_WORK_RETURN_ERROR;
		thread->reply_error.cmd = BR_OK;
	}
	return thread;
}

static int binder_free_thread(struct binder_proc *proc,
			      struct binder_thread *thread)
{
	struct binder_transaction *t;
	struct binder_transaction *send_reply = NULL;
	int active_transactions = 0;

	rb_erase(&thread->rb_node, &proc->threads);
	t = thread->transaction_stack;
	if (t && t->to_thread == thread)
		send_reply = t;
	while (t) {
		active_transactions++;
		binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
			     "release %d:%d transaction %d %s, still active\n",
			      proc->pid, thread->pid,
			     t->debug_id,
			     (t->to_thread == thread) ? "in" : "out");

		if (t->to_thread == thread) {
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
	}
	if (send_reply)
		binder_send_failed_reply(send_reply, BR_DEAD_REPLY);
	binder_release_work(&thread->todo);
	kfree(thread);
	binder_stats_deleted(BINDER_STAT_THREAD);
	return active_transactions;
}

static unsigned int binder_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	struct binder_proc *proc = filp->private_data;
	struct binder_thread *thread = NULL;
	int wait_for_proc_work;

	binder_lock(__func__);

	thread = binder_get_thread(proc);

	wait_for_proc_work = thread->transaction_stack == NULL &&
		list_empty(&thread->todo);

	binder_unlock(__func__);

	if (wait_for_proc_work) {
		if (binder_has_proc_work(proc, thread))
			return POLLIN;
		poll_wait(filp, &proc->wait, wait);
		if (binder_has_proc_work(proc, thread))
			return POLLIN;
	} else {
		if (binder_has_thread_work(thread))
			return POLLIN;
		poll_wait(filp, &thread->wait, wait);
		if (binder_has_thread_work(thread))
			return POLLIN;
	}
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
		if (!list_empty(&proc->todo))
			wake_up_interruptible(&proc->wait);
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

static int binder_ioctl_set_ctx_mgr(struct file *filp)
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
	ret = security_binder_set_context_mgr(proc->tsk);
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
	new_node = binder_new_node(proc, 0, 0);
	if (!new_node) {
		ret = -ENOMEM;
		goto out;
	}
	new_node->local_weak_refs++;
	new_node->local_strong_refs++;
	new_node->has_strong_ref = 1;
	new_node->has_weak_ref = 1;
	context->binder_context_mgr_node = new_node;
out:
	mutex_unlock(&context->context_mgr_node_lock);
	return ret;
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

	trace_binder_ioctl(cmd, arg);

	ret = wait_event_interruptible(binder_user_error_wait, binder_stop_on_user_error < 2);
	if (ret)
		goto err_unlocked;

	binder_lock(__func__);
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
	case BINDER_SET_MAX_THREADS:
		if (copy_from_user(&proc->max_threads, ubuf, sizeof(proc->max_threads))) {
			ret = -EINVAL;
			goto err;
		}
		break;
	case BINDER_SET_CONTEXT_MGR:
		ret = binder_ioctl_set_ctx_mgr(filp);
		if (ret)
			goto err;
		break;
	case BINDER_THREAD_EXIT:
		binder_debug(BINDER_DEBUG_THREADS, "%d:%d exit\n",
			     proc->pid, thread->pid);
		binder_free_thread(proc, thread);
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
	default:
		ret = -EINVAL;
		goto err;
	}
	ret = 0;
err:
	if (thread)
		thread->looper_need_return = false;
	binder_unlock(__func__);
	wait_event_interruptible(binder_user_error_wait, binder_stop_on_user_error < 2);
	if (ret && ret != -ERESTARTSYS)
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
	binder_defer_work(proc, BINDER_DEFERRED_PUT_FILES);
}

static int binder_vm_fault(struct vm_fault *vmf)
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
	int ret;
	struct binder_proc *proc = filp->private_data;
	const char *failure_string;

	if (proc->tsk != current->group_leader)
		return -EINVAL;

	if ((vma->vm_end - vma->vm_start) > SZ_4M)
		vma->vm_end = vma->vm_start + SZ_4M;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d %lx-%lx (%ld K) vma %lx pagep %lx\n",
		     __func__, proc->pid, vma->vm_start, vma->vm_end,
		     (vma->vm_end - vma->vm_start) / SZ_1K, vma->vm_flags,
		     (unsigned long)pgprot_val(vma->vm_page_prot));

	if (vma->vm_flags & FORBIDDEN_MMAP_FLAGS) {
		ret = -EPERM;
		failure_string = "bad vm_flags";
		goto err_bad_arg;
	}
	vma->vm_flags = (vma->vm_flags | VM_DONTCOPY) & ~VM_MAYWRITE;
	vma->vm_ops = &binder_vm_ops;
	vma->vm_private_data = proc;

	ret = binder_alloc_mmap_handler(&proc->alloc, vma);
	if (ret)
		return ret;
	proc->files = get_files_struct(current);
	return 0;

err_bad_arg:
	pr_err("binder_mmap: %d %lx-%lx %s failed %d\n",
	       proc->pid, vma->vm_start, vma->vm_end, failure_string, ret);
	return ret;
}

static int binder_open(struct inode *nodp, struct file *filp)
{
	struct binder_proc *proc;
	struct binder_device *binder_dev;

	binder_debug(BINDER_DEBUG_OPEN_CLOSE, "binder_open: %d:%d\n",
		     current->group_leader->pid, current->pid);

	proc = kzalloc(sizeof(*proc), GFP_KERNEL);
	if (proc == NULL)
		return -ENOMEM;
	get_task_struct(current->group_leader);
	proc->tsk = current->group_leader;
	INIT_LIST_HEAD(&proc->todo);
	init_waitqueue_head(&proc->wait);
	proc->default_priority = task_nice(current);
	binder_dev = container_of(filp->private_data, struct binder_device,
				  miscdev);
	proc->context = &binder_dev->context;
	binder_alloc_init(&proc->alloc);

	binder_lock(__func__);

	binder_stats_created(BINDER_STAT_PROC);
	proc->pid = current->group_leader->pid;
	INIT_LIST_HEAD(&proc->delivered_death);
	filp->private_data = proc;

	binder_unlock(__func__);

	mutex_lock(&binder_procs_lock);
	hlist_add_head(&proc->proc_node, &binder_procs);
	mutex_unlock(&binder_procs_lock);

	if (binder_debugfs_dir_entry_proc) {
		char strbuf[11];

		snprintf(strbuf, sizeof(strbuf), "%u", proc->pid);
		/*
		 * proc debug entries are shared between contexts, so
		 * this will fail if the process tries to open the driver
		 * again with a different context. The priting code will
		 * anyway print all contexts that a given PID has, so this
		 * is not a problem.
		 */
		proc->debugfs_entry = debugfs_create_file(strbuf, S_IRUGO,
			binder_debugfs_dir_entry_proc,
			(void *)(unsigned long)proc->pid,
			&binder_proc_fops);
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

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		struct binder_thread *thread = rb_entry(n, struct binder_thread, rb_node);

		thread->looper_need_return = true;
		if (thread->looper & BINDER_LOOPER_STATE_WAITING) {
			wake_up_interruptible(&thread->wait);
			wake_count++;
		}
	}
	wake_up_interruptible_all(&proc->wait);

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "binder_flush: %d woke %d threads\n", proc->pid,
		     wake_count);
}

static int binder_release(struct inode *nodp, struct file *filp)
{
	struct binder_proc *proc = filp->private_data;

	debugfs_remove(proc->debugfs_entry);
	binder_defer_work(proc, BINDER_DEFERRED_RELEASE);

	return 0;
}

static int binder_node_release(struct binder_node *node, int refs)
{
	struct binder_ref *ref;
	int death = 0;

	list_del_init(&node->work.entry);
	binder_release_work(&node->async_todo);

	if (hlist_empty(&node->refs)) {
		kfree(node);
		binder_stats_deleted(BINDER_STAT_NODE);

		return refs;
	}

	node->proc = NULL;
	node->local_strong_refs = 0;
	node->local_weak_refs = 0;

	spin_lock(&binder_dead_nodes_lock);
	hlist_add_head(&node->dead_node, &binder_dead_nodes);
	spin_unlock(&binder_dead_nodes_lock);

	hlist_for_each_entry(ref, &node->refs, node_entry) {
		refs++;

		if (!ref->death)
			continue;

		death++;

		if (list_empty(&ref->death->work.entry)) {
			ref->death->work.type = BINDER_WORK_DEAD_BINDER;
			list_add_tail(&ref->death->work.entry,
				      &ref->proc->todo);
			wake_up_interruptible(&ref->proc->wait);
		} else
			BUG();
	}

	binder_debug(BINDER_DEBUG_DEAD_BINDER,
		     "node %d now dead, refs %d, death %d\n",
		     node->debug_id, refs, death);

	return refs;
}

static void binder_deferred_release(struct binder_proc *proc)
{
	struct binder_context *context = proc->context;
	struct rb_node *n;
	int threads, nodes, incoming_refs, outgoing_refs, active_transactions;

	BUG_ON(proc->files);

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

	threads = 0;
	active_transactions = 0;
	while ((n = rb_first(&proc->threads))) {
		struct binder_thread *thread;

		thread = rb_entry(n, struct binder_thread, rb_node);
		threads++;
		active_transactions += binder_free_thread(proc, thread);
	}

	nodes = 0;
	incoming_refs = 0;
	while ((n = rb_first(&proc->nodes))) {
		struct binder_node *node;

		node = rb_entry(n, struct binder_node, rb_node);
		nodes++;
		rb_erase(&node->rb_node, &proc->nodes);
		incoming_refs = binder_node_release(node, incoming_refs);
	}

	outgoing_refs = 0;
	while ((n = rb_first(&proc->refs_by_desc))) {
		struct binder_ref *ref;

		ref = rb_entry(n, struct binder_ref, rb_node_desc);
		outgoing_refs++;
		binder_delete_ref(ref);
	}

	binder_release_work(&proc->todo);
	binder_release_work(&proc->delivered_death);

	binder_alloc_deferred_release(&proc->alloc);
	binder_stats_deleted(BINDER_STAT_PROC);

	put_task_struct(proc->tsk);

	binder_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d threads %d, nodes %d (ref %d), refs %d, active transactions %d\n",
		     __func__, proc->pid, threads, nodes, incoming_refs,
		     outgoing_refs, active_transactions);

	kfree(proc);
}

static void binder_deferred_func(struct work_struct *work)
{
	struct binder_proc *proc;
	struct files_struct *files;

	int defer;

	do {
		binder_lock(__func__);
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

		files = NULL;
		if (defer & BINDER_DEFERRED_PUT_FILES) {
			files = proc->files;
			if (files)
				proc->files = NULL;
		}

		if (defer & BINDER_DEFERRED_FLUSH)
			binder_deferred_flush(proc);

		if (defer & BINDER_DEFERRED_RELEASE)
			binder_deferred_release(proc); /* frees proc */

		binder_unlock(__func__);
		if (files)
			put_files_struct(files);
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

static void print_binder_transaction(struct seq_file *m, const char *prefix,
				     struct binder_transaction *t)
{
	seq_printf(m,
		   "%s %d: %p from %d:%d to %d:%d code %x flags %x pri %ld r%d",
		   prefix, t->debug_id, t,
		   t->from ? t->from->proc->pid : 0,
		   t->from ? t->from->pid : 0,
		   t->to_proc ? t->to_proc->pid : 0,
		   t->to_thread ? t->to_thread->pid : 0,
		   t->code, t->flags, t->priority, t->need_reply);
	if (t->buffer == NULL) {
		seq_puts(m, " buffer free\n");
		return;
	}
	if (t->buffer->target_node)
		seq_printf(m, " node %d",
			   t->buffer->target_node->debug_id);
	seq_printf(m, " size %zd:%zd data %p\n",
		   t->buffer->data_size, t->buffer->offsets_size,
		   t->buffer->data);
}

static void print_binder_work(struct seq_file *m, const char *prefix,
			      const char *transaction_prefix,
			      struct binder_work *w)
{
	struct binder_node *node;
	struct binder_transaction *t;

	switch (w->type) {
	case BINDER_WORK_TRANSACTION:
		t = container_of(w, struct binder_transaction, work);
		print_binder_transaction(m, transaction_prefix, t);
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

static void print_binder_thread(struct seq_file *m,
				struct binder_thread *thread,
				int print_always)
{
	struct binder_transaction *t;
	struct binder_work *w;
	size_t start_pos = m->count;
	size_t header_pos;

	seq_printf(m, "  thread %d: l %02x need_return %d\n",
			thread->pid, thread->looper,
			thread->looper_need_return);
	header_pos = m->count;
	t = thread->transaction_stack;
	while (t) {
		if (t->from == thread) {
			print_binder_transaction(m,
						 "    outgoing transaction", t);
			t = t->from_parent;
		} else if (t->to_thread == thread) {
			print_binder_transaction(m,
						 "    incoming transaction", t);
			t = t->to_parent;
		} else {
			print_binder_transaction(m, "    bad transaction", t);
			t = NULL;
		}
	}
	list_for_each_entry(w, &thread->todo, entry) {
		print_binder_work(m, "    ", "    pending transaction", w);
	}
	if (!print_always && m->count == header_pos)
		m->count = start_pos;
}

static void print_binder_node(struct seq_file *m, struct binder_node *node)
{
	struct binder_ref *ref;
	struct binder_work *w;
	int count;

	count = 0;
	hlist_for_each_entry(ref, &node->refs, node_entry)
		count++;

	seq_printf(m, "  node %d: u%016llx c%016llx hs %d hw %d ls %d lw %d is %d iw %d",
		   node->debug_id, (u64)node->ptr, (u64)node->cookie,
		   node->has_strong_ref, node->has_weak_ref,
		   node->local_strong_refs, node->local_weak_refs,
		   node->internal_strong_refs, count);
	if (count) {
		seq_puts(m, " proc");
		hlist_for_each_entry(ref, &node->refs, node_entry)
			seq_printf(m, " %d", ref->proc->pid);
	}
	seq_puts(m, "\n");
	list_for_each_entry(w, &node->async_todo, entry)
		print_binder_work(m, "    ",
				  "    pending async transaction", w);
}

static void print_binder_ref(struct seq_file *m, struct binder_ref *ref)
{
	seq_printf(m, "  ref %d: desc %d %snode %d s %d w %d d %p\n",
		   ref->debug_id, ref->desc, ref->node->proc ? "" : "dead ",
		   ref->node->debug_id, ref->strong, ref->weak, ref->death);
}

static void print_binder_proc(struct seq_file *m,
			      struct binder_proc *proc, int print_all)
{
	struct binder_work *w;
	struct rb_node *n;
	size_t start_pos = m->count;
	size_t header_pos;

	seq_printf(m, "proc %d\n", proc->pid);
	seq_printf(m, "context %s\n", proc->context->name);
	header_pos = m->count;

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n))
		print_binder_thread(m, rb_entry(n, struct binder_thread,
						rb_node), print_all);
	for (n = rb_first(&proc->nodes); n != NULL; n = rb_next(n)) {
		struct binder_node *node = rb_entry(n, struct binder_node,
						    rb_node);
		if (print_all || node->has_async_transaction)
			print_binder_node(m, node);
	}
	if (print_all) {
		for (n = rb_first(&proc->refs_by_desc);
		     n != NULL;
		     n = rb_next(n))
			print_binder_ref(m, rb_entry(n, struct binder_ref,
						     rb_node_desc));
	}
	binder_alloc_print_allocated(m, &proc->alloc);
	list_for_each_entry(w, &proc->todo, entry)
		print_binder_work(m, "  ", "  pending transaction", w);
	list_for_each_entry(w, &proc->delivered_death, entry) {
		seq_puts(m, "  has delivered dead binder\n");
		break;
	}
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
	"BR_FAILED_REPLY"
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
	struct rb_node *n;
	int count, strong, weak;

	seq_printf(m, "proc %d\n", proc->pid);
	seq_printf(m, "context %s\n", proc->context->name);
	count = 0;
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n))
		count++;
	seq_printf(m, "  threads: %d\n", count);
	seq_printf(m, "  requested threads: %d+%d/%d\n"
			"  ready threads %d\n"
			"  free async space %zd\n", proc->requested_threads,
			proc->requested_threads_started, proc->max_threads,
			proc->ready_threads,
			binder_alloc_get_free_async_space(&proc->alloc));
	count = 0;
	for (n = rb_first(&proc->nodes); n != NULL; n = rb_next(n))
		count++;
	seq_printf(m, "  nodes: %d\n", count);
	count = 0;
	strong = 0;
	weak = 0;
	for (n = rb_first(&proc->refs_by_desc); n != NULL; n = rb_next(n)) {
		struct binder_ref *ref = rb_entry(n, struct binder_ref,
						  rb_node_desc);
		count++;
		strong += ref->strong;
		weak += ref->weak;
	}
	seq_printf(m, "  refs: %d s %d w %d\n", count, strong, weak);

	count = binder_alloc_get_allocated_count(&proc->alloc);
	seq_printf(m, "  buffers: %d\n", count);

	count = 0;
	list_for_each_entry(w, &proc->todo, entry) {
		switch (w->type) {
		case BINDER_WORK_TRANSACTION:
			count++;
			break;
		default:
			break;
		}
	}
	seq_printf(m, "  pending transactions: %d\n", count);

	print_binder_stats(m, "  ", &proc->stats);
}


static int binder_state_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;
	struct binder_node *node;

	binder_lock(__func__);

	seq_puts(m, "binder state:\n");

	spin_lock(&binder_dead_nodes_lock);
	if (!hlist_empty(&binder_dead_nodes))
		seq_puts(m, "dead nodes:\n");
	hlist_for_each_entry(node, &binder_dead_nodes, dead_node)
		print_binder_node(m, node);
	spin_unlock(&binder_dead_nodes_lock);

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc(m, proc, 1);
	mutex_unlock(&binder_procs_lock);
	binder_unlock(__func__);
	return 0;
}

static int binder_stats_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;

	binder_lock(__func__);

	seq_puts(m, "binder stats:\n");

	print_binder_stats(m, "", &binder_stats);

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc_stats(m, proc);
	mutex_unlock(&binder_procs_lock);
	binder_unlock(__func__);
	return 0;
}

static int binder_transactions_show(struct seq_file *m, void *unused)
{
	struct binder_proc *proc;

	binder_lock(__func__);

	seq_puts(m, "binder transactions:\n");
	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(proc, &binder_procs, proc_node)
		print_binder_proc(m, proc, 0);
	mutex_unlock(&binder_procs_lock);
	binder_unlock(__func__);
	return 0;
}

static int binder_proc_show(struct seq_file *m, void *unused)
{
	struct binder_proc *itr;
	int pid = (unsigned long)m->private;

	binder_lock(__func__);

	mutex_lock(&binder_procs_lock);
	hlist_for_each_entry(itr, &binder_procs, proc_node) {
		if (itr->pid == pid) {
			seq_puts(m, "binder proc state:\n");
			print_binder_proc(m, itr, 1);
		}
	}
	mutex_unlock(&binder_procs_lock);

	binder_unlock(__func__);
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

static int binder_transaction_log_show(struct seq_file *m, void *unused)
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

static const struct file_operations binder_fops = {
	.owner = THIS_MODULE,
	.poll = binder_poll,
	.unlocked_ioctl = binder_ioctl,
	.compat_ioctl = binder_ioctl,
	.mmap = binder_mmap,
	.open = binder_open,
	.flush = binder_flush,
	.release = binder_release,
};

BINDER_DEBUG_ENTRY(state);
BINDER_DEBUG_ENTRY(stats);
BINDER_DEBUG_ENTRY(transactions);
BINDER_DEBUG_ENTRY(transaction_log);

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
	char *device_name, *device_names;
	struct binder_device *device;
	struct hlist_node *tmp;

	atomic_set(&binder_transaction_log.cur, ~0U);
	atomic_set(&binder_transaction_log_failed.cur, ~0U);

	binder_debugfs_dir_entry_root = debugfs_create_dir("binder", NULL);
	if (binder_debugfs_dir_entry_root)
		binder_debugfs_dir_entry_proc = debugfs_create_dir("proc",
						 binder_debugfs_dir_entry_root);

	if (binder_debugfs_dir_entry_root) {
		debugfs_create_file("state",
				    S_IRUGO,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_state_fops);
		debugfs_create_file("stats",
				    S_IRUGO,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_stats_fops);
		debugfs_create_file("transactions",
				    S_IRUGO,
				    binder_debugfs_dir_entry_root,
				    NULL,
				    &binder_transactions_fops);
		debugfs_create_file("transaction_log",
				    S_IRUGO,
				    binder_debugfs_dir_entry_root,
				    &binder_transaction_log,
				    &binder_transaction_log_fops);
		debugfs_create_file("failed_transaction_log",
				    S_IRUGO,
				    binder_debugfs_dir_entry_root,
				    &binder_transaction_log_failed,
				    &binder_transaction_log_fops);
	}

	/*
	 * Copy the module_parameter string, because we don't want to
	 * tokenize it in-place.
	 */
	device_names = kzalloc(strlen(binder_devices_param) + 1, GFP_KERNEL);
	if (!device_names) {
		ret = -ENOMEM;
		goto err_alloc_device_names_failed;
	}
	strcpy(device_names, binder_devices_param);

	while ((device_name = strsep(&device_names, ","))) {
		ret = init_binder_device(device_name);
		if (ret)
			goto err_init_binder_device_failed;
	}

	return ret;

err_init_binder_device_failed:
	hlist_for_each_entry_safe(device, tmp, &binder_devices, hlist) {
		misc_deregister(&device->miscdev);
		hlist_del(&device->hlist);
		kfree(device);
	}
err_alloc_device_names_failed:
	debugfs_remove_recursive(binder_debugfs_dir_entry_root);

	return ret;
}

device_initcall(binder_init);

#define CREATE_TRACE_POINTS
#include "binder_trace.h"

MODULE_LICENSE("GPL v2");
