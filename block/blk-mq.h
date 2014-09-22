#ifndef INT_BLK_MQ_H
#define INT_BLK_MQ_H

struct blk_mq_tag_set;

struct blk_mq_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	rq_list;
	}  ____cacheline_aligned_in_smp;

	unsigned int		cpu;
	unsigned int		index_hw;

	unsigned int		last_tag ____cacheline_aligned_in_smp;

	/* incremented at dispatch time */
	unsigned long		rq_dispatched[2];
	unsigned long		rq_merged;

	/* incremented at completion time */
	unsigned long		____cacheline_aligned_in_smp rq_completed[2];

	struct request_queue	*queue;
	struct kobject		kobj;
} ____cacheline_aligned_in_smp;

void __blk_mq_complete_request(struct request *rq);
void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async);
void blk_mq_init_flush(struct request_queue *q);
void blk_mq_freeze_queue(struct request_queue *q);
void blk_mq_free_queue(struct request_queue *q);
void blk_mq_clone_flush_request(struct request *flush_rq,
		struct request *orig_rq);
int blk_mq_update_nr_requests(struct request_queue *q, unsigned int nr);

/*
 * CPU hotplug helpers
 */
struct blk_mq_cpu_notifier;
void blk_mq_init_cpu_notifier(struct blk_mq_cpu_notifier *notifier,
			      int (*fn)(void *, unsigned long, unsigned int),
			      void *data);
void blk_mq_register_cpu_notifier(struct blk_mq_cpu_notifier *notifier);
void blk_mq_unregister_cpu_notifier(struct blk_mq_cpu_notifier *notifier);
void blk_mq_cpu_init(void);
void blk_mq_enable_hotplug(void);
void blk_mq_disable_hotplug(void);

/*
 * CPU -> queue mappings
 */
extern unsigned int *blk_mq_make_queue_map(struct blk_mq_tag_set *set);
extern int blk_mq_update_queue_map(unsigned int *map, unsigned int nr_queues);
extern int blk_mq_hw_queue_to_node(unsigned int *map, unsigned int);

/*
 * sysfs helpers
 */
extern int blk_mq_sysfs_register(struct request_queue *q);
extern void blk_mq_sysfs_unregister(struct request_queue *q);

extern void blk_mq_rq_timed_out(struct request *req, bool reserved);

/*
 * Basic implementation of sparser bitmap, allowing the user to spread
 * the bits over more cachelines.
 */
struct blk_align_bitmap {
	unsigned long word;
	unsigned long depth;
} ____cacheline_aligned_in_smp;

static inline struct blk_mq_ctx *__blk_mq_get_ctx(struct request_queue *q,
					   unsigned int cpu)
{
	return per_cpu_ptr(q->queue_ctx, cpu);
}

/*
 * This assumes per-cpu software queueing queues. They could be per-node
 * as well, for instance. For now this is hardcoded as-is. Note that we don't
 * care about preemption, since we know the ctx's are persistent. This does
 * mean that we can't rely on ctx always matching the currently running CPU.
 */
static inline struct blk_mq_ctx *blk_mq_get_ctx(struct request_queue *q)
{
	return __blk_mq_get_ctx(q, get_cpu());
}

static inline void blk_mq_put_ctx(struct blk_mq_ctx *ctx)
{
	put_cpu();
}

struct blk_mq_alloc_data {
	/* input parameter */
	struct request_queue *q;
	gfp_t gfp;
	bool reserved;

	/* input & output parameter */
	struct blk_mq_ctx *ctx;
	struct blk_mq_hw_ctx *hctx;
};

static inline void blk_mq_set_alloc_data(struct blk_mq_alloc_data *data,
		struct request_queue *q, gfp_t gfp, bool reserved,
		struct blk_mq_ctx *ctx,
		struct blk_mq_hw_ctx *hctx)
{
	data->q = q;
	data->gfp = gfp;
	data->reserved = reserved;
	data->ctx = ctx;
	data->hctx = hctx;
}

#endif
