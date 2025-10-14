/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ELEVATOR_H
#define _ELEVATOR_H

#include <linux/percpu.h>
#include <linux/hashtable.h>
#include "blk-mq.h"

struct io_cq;
struct elevator_type;
struct blk_mq_debugfs_attr;

/*
 * Return values from elevator merger
 */
enum elv_merge {
	ELEVATOR_NO_MERGE	= 0,
	ELEVATOR_FRONT_MERGE	= 1,
	ELEVATOR_BACK_MERGE	= 2,
	ELEVATOR_DISCARD_MERGE	= 3,
};

struct blk_mq_alloc_data;
struct blk_mq_hw_ctx;

struct elevator_tags {
	/* num. of hardware queues for which tags are allocated */
	unsigned int nr_hw_queues;
	/* depth used while allocating tags */
	unsigned int nr_requests;
	/* shared tag is stored at index 0 */
	struct blk_mq_tags *tags[];
};

struct elevator_mq_ops {
	int (*init_sched)(struct request_queue *, struct elevator_queue *);
	void (*exit_sched)(struct elevator_queue *);
	int (*init_hctx)(struct blk_mq_hw_ctx *, unsigned int);
	void (*exit_hctx)(struct blk_mq_hw_ctx *, unsigned int);
	void (*depth_updated)(struct request_queue *);

	bool (*allow_merge)(struct request_queue *, struct request *, struct bio *);
	bool (*bio_merge)(struct request_queue *, struct bio *, unsigned int);
	int (*request_merge)(struct request_queue *q, struct request **, struct bio *);
	void (*request_merged)(struct request_queue *, struct request *, enum elv_merge);
	void (*requests_merged)(struct request_queue *, struct request *, struct request *);
	void (*limit_depth)(blk_opf_t, struct blk_mq_alloc_data *);
	void (*prepare_request)(struct request *);
	void (*finish_request)(struct request *);
	void (*insert_requests)(struct blk_mq_hw_ctx *hctx, struct list_head *list,
			blk_insert_t flags);
	struct request *(*dispatch_request)(struct blk_mq_hw_ctx *);
	bool (*has_work)(struct blk_mq_hw_ctx *);
	void (*completed_request)(struct request *, u64);
	void (*requeue_request)(struct request *);
	struct request *(*former_request)(struct request_queue *, struct request *);
	struct request *(*next_request)(struct request_queue *, struct request *);
	void (*init_icq)(struct io_cq *);
	void (*exit_icq)(struct io_cq *);
};

#define ELV_NAME_MAX	(16)

struct elv_fs_entry {
	struct attribute attr;
	ssize_t (*show)(struct elevator_queue *, char *);
	ssize_t (*store)(struct elevator_queue *, const char *, size_t);
};

/*
 * identifies an elevator type, such as AS or deadline
 */
struct elevator_type
{
	/* managed by elevator core */
	struct kmem_cache *icq_cache;

	/* fields provided by elevator implementation */
	struct elevator_mq_ops ops;

	size_t icq_size;	/* see iocontext.h */
	size_t icq_align;	/* ditto */
	const struct elv_fs_entry *elevator_attrs;
	const char *elevator_name;
	const char *elevator_alias;
	struct module *elevator_owner;
#ifdef CONFIG_BLK_DEBUG_FS
	const struct blk_mq_debugfs_attr *queue_debugfs_attrs;
	const struct blk_mq_debugfs_attr *hctx_debugfs_attrs;
#endif

	/* managed by elevator core */
	char icq_cache_name[ELV_NAME_MAX + 6];	/* elvname + "_io_cq" */
	struct list_head list;
};

static inline bool elevator_tryget(struct elevator_type *e)
{
	return try_module_get(e->elevator_owner);
}

static inline void __elevator_get(struct elevator_type *e)
{
	__module_get(e->elevator_owner);
}

static inline void elevator_put(struct elevator_type *e)
{
	module_put(e->elevator_owner);
}

#define ELV_HASH_BITS 6

void elv_rqhash_del(struct request_queue *q, struct request *rq);
void elv_rqhash_add(struct request_queue *q, struct request *rq);
void elv_rqhash_reposition(struct request_queue *q, struct request *rq);
struct request *elv_rqhash_find(struct request_queue *q, sector_t offset);

/*
 * each queue has an elevator_queue associated with it
 */
struct elevator_queue
{
	struct elevator_type *type;
	struct elevator_tags *et;
	void *elevator_data;
	struct kobject kobj;
	struct mutex sysfs_lock;
	unsigned long flags;
	DECLARE_HASHTABLE(hash, ELV_HASH_BITS);
};

#define ELEVATOR_FLAG_REGISTERED	0
#define ELEVATOR_FLAG_DYING		1
#define ELEVATOR_FLAG_ENABLE_WBT_ON_EXIT	2

/*
 * block elevator interface
 */
extern enum elv_merge elv_merge(struct request_queue *, struct request **,
		struct bio *);
extern void elv_merge_requests(struct request_queue *, struct request *,
			       struct request *);
extern void elv_merged_request(struct request_queue *, struct request *,
		enum elv_merge);
extern bool elv_attempt_insert_merge(struct request_queue *, struct request *,
				     struct list_head *);
extern struct request *elv_former_request(struct request_queue *, struct request *);
extern struct request *elv_latter_request(struct request_queue *, struct request *);
void elevator_init_mq(struct request_queue *q);

/*
 * io scheduler registration
 */
extern int elv_register(struct elevator_type *);
extern void elv_unregister(struct elevator_type *);

/*
 * io scheduler sysfs switching
 */
ssize_t elv_iosched_show(struct gendisk *disk, char *page);
ssize_t elv_iosched_store(struct gendisk *disk, const char *page, size_t count);

extern bool elv_bio_merge_ok(struct request *, struct bio *);
struct elevator_queue *elevator_alloc(struct request_queue *,
		struct elevator_type *, struct elevator_tags *);

/*
 * Helper functions.
 */
extern struct request *elv_rb_former_request(struct request_queue *, struct request *);
extern struct request *elv_rb_latter_request(struct request_queue *, struct request *);

/*
 * rb support functions.
 */
extern void elv_rb_add(struct rb_root *, struct request *);
extern void elv_rb_del(struct rb_root *, struct request *);
extern struct request *elv_rb_find(struct rb_root *, sector_t);

/*
 * Insertion selection
 */
#define ELEVATOR_INSERT_FRONT	1
#define ELEVATOR_INSERT_BACK	2
#define ELEVATOR_INSERT_SORT	3
#define ELEVATOR_INSERT_REQUEUE	4
#define ELEVATOR_INSERT_FLUSH	5
#define ELEVATOR_INSERT_SORT_MERGE	6

#define rb_entry_rq(node)	rb_entry((node), struct request, rb_node)

#define rq_entry_fifo(ptr)	list_entry((ptr), struct request, queuelist)
#define rq_fifo_clear(rq)	list_del_init(&(rq)->queuelist)

void blk_mq_sched_reg_debugfs(struct request_queue *q);
void blk_mq_sched_unreg_debugfs(struct request_queue *q);

#endif /* _ELEVATOR_H */
