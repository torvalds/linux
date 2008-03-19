/*
 * SPU file system
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef SPUFS_H
#define SPUFS_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cpumask.h>

#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_info.h>

/* The magic number for our file system */
enum {
	SPUFS_MAGIC = 0x23c9b64e,
};

struct spu_context_ops;
struct spu_gang;

/* ctx->sched_flags */
enum {
	SPU_SCHED_NOTIFY_ACTIVE,
	SPU_SCHED_WAS_ACTIVE,	/* was active upon spu_acquire_saved()  */
	SPU_SCHED_SPU_RUN,	/* context is within spu_run */
};

struct spu_context {
	struct spu *spu;		  /* pointer to a physical SPU */
	struct spu_state csa;		  /* SPU context save area. */
	spinlock_t mmio_lock;		  /* protects mmio access */
	struct address_space *local_store; /* local store mapping.  */
	struct address_space *mfc;	   /* 'mfc' area mappings. */
	struct address_space *cntl;	   /* 'control' area mappings. */
	struct address_space *signal1;	   /* 'signal1' area mappings. */
	struct address_space *signal2;	   /* 'signal2' area mappings. */
	struct address_space *mss;	   /* 'mss' area mappings. */
	struct address_space *psmap;	   /* 'psmap' area mappings. */
	struct mutex mapping_lock;
	u64 object_id;		   /* user space pointer for oprofile */

	enum { SPU_STATE_RUNNABLE, SPU_STATE_SAVED } state;
	struct mutex state_mutex;
	struct mutex run_mutex;

	struct mm_struct *owner;

	struct kref kref;
	wait_queue_head_t ibox_wq;
	wait_queue_head_t wbox_wq;
	wait_queue_head_t stop_wq;
	wait_queue_head_t mfc_wq;
	wait_queue_head_t run_wq;
	struct fasync_struct *ibox_fasync;
	struct fasync_struct *wbox_fasync;
	struct fasync_struct *mfc_fasync;
	u32 tagwait;
	struct spu_context_ops *ops;
	struct work_struct reap_work;
	unsigned long flags;
	unsigned long event_return;

	struct list_head gang_list;
	struct spu_gang *gang;
	struct kref *prof_priv_kref;
	void ( * prof_priv_release) (struct kref *kref);

	/* owner thread */
	pid_t tid;

	/* scheduler fields */
	struct list_head rq;
	unsigned int time_slice;
	unsigned long sched_flags;
	cpumask_t cpus_allowed;
	int policy;
	int prio;

	/* statistics */
	struct {
		/* updates protected by ctx->state_mutex */
		enum spu_utilization_state util_state;
		unsigned long long tstamp;	/* time of last state switch */
		unsigned long long times[SPU_UTIL_MAX];
		unsigned long long vol_ctx_switch;
		unsigned long long invol_ctx_switch;
		unsigned long long min_flt;
		unsigned long long maj_flt;
		unsigned long long hash_flt;
		unsigned long long slb_flt;
		unsigned long long slb_flt_base; /* # at last ctx switch */
		unsigned long long class2_intr;
		unsigned long long class2_intr_base; /* # at last ctx switch */
		unsigned long long libassist;
	} stats;

	struct list_head aff_list;
	int aff_head;
	int aff_offset;
};

struct spu_gang {
	struct list_head list;
	struct mutex mutex;
	struct kref kref;
	int contexts;

	struct spu_context *aff_ref_ctx;
	struct list_head aff_list_head;
	struct mutex aff_mutex;
	int aff_flags;
	struct spu *aff_ref_spu;
	atomic_t aff_sched_count;
};

/* Flag bits for spu_gang aff_flags */
#define AFF_OFFSETS_SET		1
#define AFF_MERGED		2

struct mfc_dma_command {
	int32_t pad;	/* reserved */
	uint32_t lsa;	/* local storage address */
	uint64_t ea;	/* effective address */
	uint16_t size;	/* transfer size */
	uint16_t tag;	/* command tag */
	uint16_t class;	/* class ID */
	uint16_t cmd;	/* command opcode */
};


/* SPU context query/set operations. */
struct spu_context_ops {
	int (*mbox_read) (struct spu_context * ctx, u32 * data);
	 u32(*mbox_stat_read) (struct spu_context * ctx);
	unsigned int (*mbox_stat_poll)(struct spu_context *ctx,
					unsigned int events);
	int (*ibox_read) (struct spu_context * ctx, u32 * data);
	int (*wbox_write) (struct spu_context * ctx, u32 data);
	 u32(*signal1_read) (struct spu_context * ctx);
	void (*signal1_write) (struct spu_context * ctx, u32 data);
	 u32(*signal2_read) (struct spu_context * ctx);
	void (*signal2_write) (struct spu_context * ctx, u32 data);
	void (*signal1_type_set) (struct spu_context * ctx, u64 val);
	 u64(*signal1_type_get) (struct spu_context * ctx);
	void (*signal2_type_set) (struct spu_context * ctx, u64 val);
	 u64(*signal2_type_get) (struct spu_context * ctx);
	 u32(*npc_read) (struct spu_context * ctx);
	void (*npc_write) (struct spu_context * ctx, u32 data);
	 u32(*status_read) (struct spu_context * ctx);
	char*(*get_ls) (struct spu_context * ctx);
	void (*privcntl_write) (struct spu_context *ctx, u64 data);
	 u32 (*runcntl_read) (struct spu_context * ctx);
	void (*runcntl_write) (struct spu_context * ctx, u32 data);
	void (*runcntl_stop) (struct spu_context * ctx);
	void (*master_start) (struct spu_context * ctx);
	void (*master_stop) (struct spu_context * ctx);
	int (*set_mfc_query)(struct spu_context * ctx, u32 mask, u32 mode);
	u32 (*read_mfc_tagstatus)(struct spu_context * ctx);
	u32 (*get_mfc_free_elements)(struct spu_context *ctx);
	int (*send_mfc_command)(struct spu_context * ctx,
				struct mfc_dma_command * cmd);
	void (*dma_info_read) (struct spu_context * ctx,
			       struct spu_dma_info * info);
	void (*proxydma_info_read) (struct spu_context * ctx,
				    struct spu_proxydma_info * info);
	void (*restart_dma)(struct spu_context *ctx);
};

extern struct spu_context_ops spu_hw_ops;
extern struct spu_context_ops spu_backing_ops;

struct spufs_inode_info {
	struct spu_context *i_ctx;
	struct spu_gang *i_gang;
	struct inode vfs_inode;
	int i_openers;
};
#define SPUFS_I(inode) \
	container_of(inode, struct spufs_inode_info, vfs_inode)

extern struct tree_descr spufs_dir_contents[];
extern struct tree_descr spufs_dir_nosched_contents[];

/* system call implementation */
extern struct spufs_calls spufs_calls;
long spufs_run_spu(struct spu_context *ctx, u32 *npc, u32 *status);
long spufs_create(struct nameidata *nd, unsigned int flags,
			mode_t mode, struct file *filp);
/* ELF coredump callbacks for writing SPU ELF notes */
extern int spufs_coredump_extra_notes_size(void);
extern int spufs_coredump_extra_notes_write(struct file *file, loff_t *foffset);

extern const struct file_operations spufs_context_fops;

/* gang management */
struct spu_gang *alloc_spu_gang(void);
struct spu_gang *get_spu_gang(struct spu_gang *gang);
int put_spu_gang(struct spu_gang *gang);
void spu_gang_remove_ctx(struct spu_gang *gang, struct spu_context *ctx);
void spu_gang_add_ctx(struct spu_gang *gang, struct spu_context *ctx);

/* fault handling */
int spufs_handle_class1(struct spu_context *ctx);
int spufs_handle_class0(struct spu_context *ctx);

/* affinity */
struct spu *affinity_check(struct spu_context *ctx);

/* context management */
extern atomic_t nr_spu_contexts;
static inline int __must_check spu_acquire(struct spu_context *ctx)
{
	return mutex_lock_interruptible(&ctx->state_mutex);
}

static inline void spu_release(struct spu_context *ctx)
{
	mutex_unlock(&ctx->state_mutex);
}

struct spu_context * alloc_spu_context(struct spu_gang *gang);
void destroy_spu_context(struct kref *kref);
struct spu_context * get_spu_context(struct spu_context *ctx);
int put_spu_context(struct spu_context *ctx);
void spu_unmap_mappings(struct spu_context *ctx);

void spu_forget(struct spu_context *ctx);
int __must_check spu_acquire_saved(struct spu_context *ctx);
void spu_release_saved(struct spu_context *ctx);

int spu_stopped(struct spu_context *ctx, u32 * stat);
void spu_del_from_rq(struct spu_context *ctx);
int spu_activate(struct spu_context *ctx, unsigned long flags);
void spu_deactivate(struct spu_context *ctx);
void spu_yield(struct spu_context *ctx);
void spu_switch_notify(struct spu *spu, struct spu_context *ctx);
void spu_set_timeslice(struct spu_context *ctx);
void spu_update_sched_info(struct spu_context *ctx);
void __spu_update_sched_info(struct spu_context *ctx);
int __init spu_sched_init(void);
void spu_sched_exit(void);

extern char *isolated_loader;

/*
 * spufs_wait
 *	Same as wait_event_interruptible(), except that here
 *	we need to call spu_release(ctx) before sleeping, and
 *	then spu_acquire(ctx) when awoken.
 *
 * 	Returns with state_mutex re-acquired when successfull or
 * 	with -ERESTARTSYS and the state_mutex dropped when interrupted.
 */

#define spufs_wait(wq, condition)					\
({									\
	int __ret = 0;							\
	DEFINE_WAIT(__wait);						\
	for (;;) {							\
		prepare_to_wait(&(wq), &__wait, TASK_INTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		spu_release(ctx);					\
		if (signal_pending(current)) {				\
			__ret = -ERESTARTSYS;				\
			break;						\
		}							\
		schedule();						\
		__ret = spu_acquire(ctx);				\
		if (__ret)						\
			break;						\
	}								\
	finish_wait(&(wq), &__wait);					\
	__ret;								\
})

size_t spu_wbox_write(struct spu_context *ctx, u32 data);
size_t spu_ibox_read(struct spu_context *ctx, u32 *data);

/* irq callback funcs. */
void spufs_ibox_callback(struct spu *spu);
void spufs_wbox_callback(struct spu *spu);
void spufs_stop_callback(struct spu *spu);
void spufs_mfc_callback(struct spu *spu);
void spufs_dma_callback(struct spu *spu, int type);

extern struct spu_coredump_calls spufs_coredump_calls;
struct spufs_coredump_reader {
	char *name;
	ssize_t (*read)(struct spu_context *ctx,
			char __user *buffer, size_t size, loff_t *pos);
	u64 (*get)(struct spu_context *ctx);
	size_t size;
};
extern struct spufs_coredump_reader spufs_coredump_read[];
extern int spufs_coredump_num_notes;

extern int spu_init_csa(struct spu_state *csa);
extern void spu_fini_csa(struct spu_state *csa);
extern int spu_save(struct spu_state *prev, struct spu *spu);
extern int spu_restore(struct spu_state *new, struct spu *spu);
extern int spu_switch(struct spu_state *prev, struct spu_state *new,
		      struct spu *spu);
extern int spu_alloc_lscsa(struct spu_state *csa);
extern void spu_free_lscsa(struct spu_state *csa);

extern void spuctx_switch_state(struct spu_context *ctx,
		enum spu_utilization_state new_state);

#define spu_context_trace(name, ctx, spu) \
	trace_mark(name, "%p %p", ctx, spu);
#define spu_context_nospu_trace(name, ctx) \
	trace_mark(name, "%p", ctx);

#endif
