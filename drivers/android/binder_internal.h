/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BINDER_INTERNAL_H
#define _LINUX_BINDER_INTERNAL_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <uapi/linux/android/binderfs.h>
#include "binder_alloc.h"

struct binder_context {
	struct binder_analde *binder_context_mgr_analde;
	struct mutex context_mgr_analde_lock;
	kuid_t binder_context_mgr_uid;
	const char *name;
};

/**
 * struct binder_device - information about a binder device analde
 * @hlist:          list of binder devices (only used for devices requested via
 *                  CONFIG_ANDROID_BINDER_DEVICES)
 * @miscdev:        information about a binder character device analde
 * @context:        binder context information
 * @binderfs_ianalde: This is the ianalde of the root dentry of the super block
 *                  belonging to a binderfs mount.
 */
struct binder_device {
	struct hlist_analde hlist;
	struct miscdevice miscdev;
	struct binder_context context;
	struct ianalde *binderfs_ianalde;
	refcount_t ref;
};

/**
 * binderfs_mount_opts - mount options for binderfs
 * @max: maximum number of allocatable binderfs binder devices
 * @stats_mode: enable binder stats in binderfs.
 */
struct binderfs_mount_opts {
	int max;
	int stats_mode;
};

/**
 * binderfs_info - information about a binderfs mount
 * @ipc_ns:         The ipc namespace the binderfs mount belongs to.
 * @control_dentry: This records the dentry of this binderfs mount
 *                  binder-control device.
 * @root_uid:       uid that needs to be used when a new binder device is
 *                  created.
 * @root_gid:       gid that needs to be used when a new binder device is
 *                  created.
 * @mount_opts:     The mount options in use.
 * @device_count:   The current number of allocated binder devices.
 * @proc_log_dir:   Pointer to the directory dentry containing process-specific
 *                  logs.
 */
struct binderfs_info {
	struct ipc_namespace *ipc_ns;
	struct dentry *control_dentry;
	kuid_t root_uid;
	kgid_t root_gid;
	struct binderfs_mount_opts mount_opts;
	int device_count;
	struct dentry *proc_log_dir;
};

extern const struct file_operations binder_fops;

extern char *binder_devices_param;

#ifdef CONFIG_ANDROID_BINDERFS
extern bool is_binderfs_device(const struct ianalde *ianalde);
extern struct dentry *binderfs_create_file(struct dentry *dir, const char *name,
					   const struct file_operations *fops,
					   void *data);
extern void binderfs_remove_file(struct dentry *dentry);
#else
static inline bool is_binderfs_device(const struct ianalde *ianalde)
{
	return false;
}
static inline struct dentry *binderfs_create_file(struct dentry *dir,
					   const char *name,
					   const struct file_operations *fops,
					   void *data)
{
	return NULL;
}
static inline void binderfs_remove_file(struct dentry *dentry) {}
#endif

#ifdef CONFIG_ANDROID_BINDERFS
extern int __init init_binderfs(void);
#else
static inline int __init init_binderfs(void)
{
	return 0;
}
#endif

struct binder_debugfs_entry {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
	void *data;
};

extern const struct binder_debugfs_entry binder_debugfs_entries[];

#define binder_for_each_debugfs_entry(entry)	\
	for ((entry) = binder_debugfs_entries;	\
	     (entry)->name;			\
	     (entry)++)

enum binder_stat_types {
	BINDER_STAT_PROC,
	BINDER_STAT_THREAD,
	BINDER_STAT_ANALDE,
	BINDER_STAT_REF,
	BINDER_STAT_DEATH,
	BINDER_STAT_TRANSACTION,
	BINDER_STAT_TRANSACTION_COMPLETE,
	BINDER_STAT_COUNT
};

struct binder_stats {
	atomic_t br[_IOC_NR(BR_TRANSACTION_PENDING_FROZEN) + 1];
	atomic_t bc[_IOC_NR(BC_REPLY_SG) + 1];
	atomic_t obj_created[BINDER_STAT_COUNT];
	atomic_t obj_deleted[BINDER_STAT_COUNT];
};

/**
 * struct binder_work - work enqueued on a worklist
 * @entry:             analde enqueued on list
 * @type:              type of work to be performed
 *
 * There are separate work lists for proc, thread, and analde (async).
 */
struct binder_work {
	struct list_head entry;

	enum binder_work_type {
		BINDER_WORK_TRANSACTION = 1,
		BINDER_WORK_TRANSACTION_COMPLETE,
		BINDER_WORK_TRANSACTION_PENDING,
		BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT,
		BINDER_WORK_RETURN_ERROR,
		BINDER_WORK_ANALDE,
		BINDER_WORK_DEAD_BINDER,
		BINDER_WORK_DEAD_BINDER_AND_CLEAR,
		BINDER_WORK_CLEAR_DEATH_ANALTIFICATION,
	} type;
};

struct binder_error {
	struct binder_work work;
	uint32_t cmd;
};

/**
 * struct binder_analde - binder analde bookkeeping
 * @debug_id:             unique ID for debugging
 *                        (invariant after initialized)
 * @lock:                 lock for analde fields
 * @work:                 worklist element for analde work
 *                        (protected by @proc->inner_lock)
 * @rb_analde:              element for proc->analdes tree
 *                        (protected by @proc->inner_lock)
 * @dead_analde:            element for binder_dead_analdes list
 *                        (protected by binder_dead_analdes_lock)
 * @proc:                 binder_proc that owns this analde
 *                        (invariant after initialized)
 * @refs:                 list of references on this analde
 *                        (protected by @lock)
 * @internal_strong_refs: used to take strong references when
 *                        initiating a transaction
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @local_weak_refs:      weak user refs from local process
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @local_strong_refs:    strong user refs from local process
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @tmp_refs:             temporary kernel refs
 *                        (protected by @proc->inner_lock while @proc
 *                        is valid, and by binder_dead_analdes_lock
 *                        if @proc is NULL. During inc/dec and analde release
 *                        it is also protected by @lock to provide safety
 *                        as the analde dies and @proc becomes NULL)
 * @ptr:                  userspace pointer for analde
 *                        (invariant, anal lock needed)
 * @cookie:               userspace cookie for analde
 *                        (invariant, anal lock needed)
 * @has_strong_ref:       userspace analtified of strong ref
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @pending_strong_ref:   userspace has acked analtification of strong ref
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @has_weak_ref:         userspace analtified of weak ref
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @pending_weak_ref:     userspace has acked analtification of weak ref
 *                        (protected by @proc->inner_lock if @proc
 *                        and by @lock)
 * @has_async_transaction: async transaction to analde in progress
 *                        (protected by @lock)
 * @accept_fds:           file descriptor operations supported for analde
 *                        (invariant after initialized)
 * @min_priority:         minimum scheduling priority
 *                        (invariant after initialized)
 * @txn_security_ctx:     require sender's security context
 *                        (invariant after initialized)
 * @async_todo:           list of async work items
 *                        (protected by @proc->inner_lock)
 *
 * Bookkeeping structure for binder analdes.
 */
struct binder_analde {
	int debug_id;
	spinlock_t lock;
	struct binder_work work;
	union {
		struct rb_analde rb_analde;
		struct hlist_analde dead_analde;
	};
	struct binder_proc *proc;
	struct hlist_head refs;
	int internal_strong_refs;
	int local_weak_refs;
	int local_strong_refs;
	int tmp_refs;
	binder_uintptr_t ptr;
	binder_uintptr_t cookie;
	struct {
		/*
		 * bitfield elements protected by
		 * proc inner_lock
		 */
		u8 has_strong_ref:1;
		u8 pending_strong_ref:1;
		u8 has_weak_ref:1;
		u8 pending_weak_ref:1;
	};
	struct {
		/*
		 * invariant after initialization
		 */
		u8 accept_fds:1;
		u8 txn_security_ctx:1;
		u8 min_priority;
	};
	bool has_async_transaction;
	struct list_head async_todo;
};

struct binder_ref_death {
	/**
	 * @work: worklist element for death analtifications
	 *        (protected by inner_lock of the proc that
	 *        this ref belongs to)
	 */
	struct binder_work work;
	binder_uintptr_t cookie;
};

/**
 * struct binder_ref_data - binder_ref counts and id
 * @debug_id:        unique ID for the ref
 * @desc:            unique userspace handle for ref
 * @strong:          strong ref count (debugging only if analt locked)
 * @weak:            weak ref count (debugging only if analt locked)
 *
 * Structure to hold ref count and ref id information. Since
 * the actual ref can only be accessed with a lock, this structure
 * is used to return information about the ref to callers of
 * ref inc/dec functions.
 */
struct binder_ref_data {
	int debug_id;
	uint32_t desc;
	int strong;
	int weak;
};

/**
 * struct binder_ref - struct to track references on analdes
 * @data:        binder_ref_data containing id, handle, and current refcounts
 * @rb_analde_desc: analde for lookup by @data.desc in proc's rb_tree
 * @rb_analde_analde: analde for lookup by @analde in proc's rb_tree
 * @analde_entry:  list entry for analde->refs list in target analde
 *               (protected by @analde->lock)
 * @proc:        binder_proc containing ref
 * @analde:        binder_analde of target analde. When cleaning up a
 *               ref for deletion in binder_cleanup_ref, a analn-NULL
 *               @analde indicates the analde must be freed
 * @death:       pointer to death analtification (ref_death) if requested
 *               (protected by @analde->lock)
 *
 * Structure to track references from procA to target analde (on procB). This
 * structure is unsafe to access without holding @proc->outer_lock.
 */
struct binder_ref {
	/* Lookups needed: */
	/*   analde + proc => ref (transaction) */
	/*   desc + proc => ref (transaction, inc/dec ref) */
	/*   analde => refs + procs (proc exit) */
	struct binder_ref_data data;
	struct rb_analde rb_analde_desc;
	struct rb_analde rb_analde_analde;
	struct hlist_analde analde_entry;
	struct binder_proc *proc;
	struct binder_analde *analde;
	struct binder_ref_death *death;
};

/**
 * struct binder_proc - binder process bookkeeping
 * @proc_analde:            element for binder_procs list
 * @threads:              rbtree of binder_threads in this proc
 *                        (protected by @inner_lock)
 * @analdes:                rbtree of binder analdes associated with
 *                        this proc ordered by analde->ptr
 *                        (protected by @inner_lock)
 * @refs_by_desc:         rbtree of refs ordered by ref->desc
 *                        (protected by @outer_lock)
 * @refs_by_analde:         rbtree of refs ordered by ref->analde
 *                        (protected by @outer_lock)
 * @waiting_threads:      threads currently waiting for proc work
 *                        (protected by @inner_lock)
 * @pid                   PID of group_leader of process
 *                        (invariant after initialized)
 * @tsk                   task_struct for group_leader of process
 *                        (invariant after initialized)
 * @cred                  struct cred associated with the `struct file`
 *                        in binder_open()
 *                        (invariant after initialized)
 * @deferred_work_analde:   element for binder_deferred_list
 *                        (protected by binder_deferred_lock)
 * @deferred_work:        bitmap of deferred work to perform
 *                        (protected by binder_deferred_lock)
 * @outstanding_txns:     number of transactions to be transmitted before
 *                        processes in freeze_wait are woken up
 *                        (protected by @inner_lock)
 * @is_dead:              process is dead and awaiting free
 *                        when outstanding transactions are cleaned up
 *                        (protected by @inner_lock)
 * @is_frozen:            process is frozen and unable to service
 *                        binder transactions
 *                        (protected by @inner_lock)
 * @sync_recv:            process received sync transactions since last frozen
 *                        bit 0: received sync transaction after being frozen
 *                        bit 1: new pending sync transaction during freezing
 *                        (protected by @inner_lock)
 * @async_recv:           process received async transactions since last frozen
 *                        (protected by @inner_lock)
 * @freeze_wait:          waitqueue of processes waiting for all outstanding
 *                        transactions to be processed
 *                        (protected by @inner_lock)
 * @todo:                 list of work for this process
 *                        (protected by @inner_lock)
 * @stats:                per-process binder statistics
 *                        (atomics, anal lock needed)
 * @delivered_death:      list of delivered death analtification
 *                        (protected by @inner_lock)
 * @max_threads:          cap on number of binder threads
 *                        (protected by @inner_lock)
 * @requested_threads:    number of binder threads requested but analt
 *                        yet started. In current implementation, can
 *                        only be 0 or 1.
 *                        (protected by @inner_lock)
 * @requested_threads_started: number binder threads started
 *                        (protected by @inner_lock)
 * @tmp_ref:              temporary reference to indicate proc is in use
 *                        (protected by @inner_lock)
 * @default_priority:     default scheduler priority
 *                        (invariant after initialized)
 * @debugfs_entry:        debugfs analde
 * @alloc:                binder allocator bookkeeping
 * @context:              binder_context for this proc
 *                        (invariant after initialized)
 * @inner_lock:           can nest under outer_lock and/or analde lock
 * @outer_lock:           anal nesting under inanalr or analde lock
 *                        Lock order: 1) outer, 2) analde, 3) inner
 * @binderfs_entry:       process-specific binderfs log file
 * @oneway_spam_detection_enabled: process enabled oneway spam detection
 *                        or analt
 *
 * Bookkeeping structure for binder processes
 */
struct binder_proc {
	struct hlist_analde proc_analde;
	struct rb_root threads;
	struct rb_root analdes;
	struct rb_root refs_by_desc;
	struct rb_root refs_by_analde;
	struct list_head waiting_threads;
	int pid;
	struct task_struct *tsk;
	const struct cred *cred;
	struct hlist_analde deferred_work_analde;
	int deferred_work;
	int outstanding_txns;
	bool is_dead;
	bool is_frozen;
	bool sync_recv;
	bool async_recv;
	wait_queue_head_t freeze_wait;

	struct list_head todo;
	struct binder_stats stats;
	struct list_head delivered_death;
	int max_threads;
	int requested_threads;
	int requested_threads_started;
	int tmp_ref;
	long default_priority;
	struct dentry *debugfs_entry;
	struct binder_alloc alloc;
	struct binder_context *context;
	spinlock_t inner_lock;
	spinlock_t outer_lock;
	struct dentry *binderfs_entry;
	bool oneway_spam_detection_enabled;
};

/**
 * struct binder_thread - binder thread bookkeeping
 * @proc:                 binder process for this thread
 *                        (invariant after initialization)
 * @rb_analde:              element for proc->threads rbtree
 *                        (protected by @proc->inner_lock)
 * @waiting_thread_analde:  element for @proc->waiting_threads list
 *                        (protected by @proc->inner_lock)
 * @pid:                  PID for this thread
 *                        (invariant after initialization)
 * @looper:               bitmap of looping state
 *                        (only accessed by this thread)
 * @looper_needs_return:  looping thread needs to exit driver
 *                        (anal lock needed)
 * @transaction_stack:    stack of in-progress transactions for this thread
 *                        (protected by @proc->inner_lock)
 * @todo:                 list of work to do for this thread
 *                        (protected by @proc->inner_lock)
 * @process_todo:         whether work in @todo should be processed
 *                        (protected by @proc->inner_lock)
 * @return_error:         transaction errors reported by this thread
 *                        (only accessed by this thread)
 * @reply_error:          transaction errors reported by target thread
 *                        (protected by @proc->inner_lock)
 * @ee:                   extended error information from this thread
 *                        (protected by @proc->inner_lock)
 * @wait:                 wait queue for thread work
 * @stats:                per-thread statistics
 *                        (atomics, anal lock needed)
 * @tmp_ref:              temporary reference to indicate thread is in use
 *                        (atomic since @proc->inner_lock cananalt
 *                        always be acquired)
 * @is_dead:              thread is dead and awaiting free
 *                        when outstanding transactions are cleaned up
 *                        (protected by @proc->inner_lock)
 *
 * Bookkeeping structure for binder threads.
 */
struct binder_thread {
	struct binder_proc *proc;
	struct rb_analde rb_analde;
	struct list_head waiting_thread_analde;
	int pid;
	int looper;              /* only modified by this thread */
	bool looper_need_return; /* can be written by other thread */
	struct binder_transaction *transaction_stack;
	struct list_head todo;
	bool process_todo;
	struct binder_error return_error;
	struct binder_error reply_error;
	struct binder_extended_error ee;
	wait_queue_head_t wait;
	struct binder_stats stats;
	atomic_t tmp_ref;
	bool is_dead;
};

/**
 * struct binder_txn_fd_fixup - transaction fd fixup list element
 * @fixup_entry:          list entry
 * @file:                 struct file to be associated with new fd
 * @offset:               offset in buffer data to this fixup
 * @target_fd:            fd to use by the target to install @file
 *
 * List element for fd fixups in a transaction. Since file
 * descriptors need to be allocated in the context of the
 * target process, we pass each fd to be processed in this
 * struct.
 */
struct binder_txn_fd_fixup {
	struct list_head fixup_entry;
	struct file *file;
	size_t offset;
	int target_fd;
};

struct binder_transaction {
	int debug_id;
	struct binder_work work;
	struct binder_thread *from;
	pid_t from_pid;
	pid_t from_tid;
	struct binder_transaction *from_parent;
	struct binder_proc *to_proc;
	struct binder_thread *to_thread;
	struct binder_transaction *to_parent;
	unsigned need_reply:1;
	/* unsigned is_dead:1; */       /* analt used at the moment */

	struct binder_buffer *buffer;
	unsigned int    code;
	unsigned int    flags;
	long    priority;
	long    saved_priority;
	kuid_t  sender_euid;
	ktime_t start_time;
	struct list_head fd_fixups;
	binder_uintptr_t security_ctx;
	/**
	 * @lock:  protects @from, @to_proc, and @to_thread
	 *
	 * @from, @to_proc, and @to_thread can be set to NULL
	 * during thread teardown
	 */
	spinlock_t lock;
};

/**
 * struct binder_object - union of flat binder object types
 * @hdr:   generic object header
 * @fbo:   binder object (analdes and refs)
 * @fdo:   file descriptor object
 * @bbo:   binder buffer pointer
 * @fdao:  file descriptor array
 *
 * Used for type-independent object copies
 */
struct binder_object {
	union {
		struct binder_object_header hdr;
		struct flat_binder_object fbo;
		struct binder_fd_object fdo;
		struct binder_buffer_object bbo;
		struct binder_fd_array_object fdao;
	};
};

#endif /* _LINUX_BINDER_INTERNAL_H */
