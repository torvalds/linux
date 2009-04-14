#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

/* Amount of time in which a process may batch requests */
#define BLK_BATCH_TIME	(HZ/50UL)

/* Number of requests a "batching" process may submit */
#define BLK_BATCH_REQ	32

extern struct kmem_cache *blk_requestq_cachep;
extern struct kobj_type blk_queue_ktype;

void init_request_from_bio(struct request *req, struct bio *bio);
void blk_rq_bio_prep(struct request_queue *q, struct request *rq,
			struct bio *bio);
void __blk_queue_free_tags(struct request_queue *q);

void blk_unplug_work(struct work_struct *work);
void blk_unplug_timeout(unsigned long data);
void blk_rq_timed_out_timer(unsigned long data);
void blk_delete_timer(struct request *);
void blk_add_timer(struct request *);
void __generic_unplug_device(struct request_queue *);

/*
 * Internal atomic flags for request handling
 */
enum rq_atomic_flags {
	REQ_ATOM_COMPLETE = 0,
};

/*
 * EH timer and IO completion will both attempt to 'grab' the request, make
 * sure that only one of them suceeds
 */
static inline int blk_mark_rq_complete(struct request *rq)
{
	return test_and_set_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags);
}

static inline void blk_clear_rq_complete(struct request *rq)
{
	clear_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags);
}

#ifdef CONFIG_FAIL_IO_TIMEOUT
int blk_should_fake_timeout(struct request_queue *);
ssize_t part_timeout_show(struct device *, struct device_attribute *, char *);
ssize_t part_timeout_store(struct device *, struct device_attribute *,
				const char *, size_t);
#else
static inline int blk_should_fake_timeout(struct request_queue *q)
{
	return 0;
}
#endif

struct io_context *current_io_context(gfp_t gfp_flags, int node);

int ll_back_merge_fn(struct request_queue *q, struct request *req,
		     struct bio *bio);
int ll_front_merge_fn(struct request_queue *q, struct request *req, 
		      struct bio *bio);
int attempt_back_merge(struct request_queue *q, struct request *rq);
int attempt_front_merge(struct request_queue *q, struct request *rq);
void blk_recalc_rq_segments(struct request *rq);
void blk_recalc_rq_sectors(struct request *rq, int nsect);

void blk_queue_congestion_threshold(struct request_queue *q);

int blk_dev_init(void);

void elv_quisce_start(struct request_queue *q);
void elv_quisce_end(struct request_queue *q);


/*
 * Return the threshold (number of used requests) at which the queue is
 * considered to be congested.  It include a little hysteresis to keep the
 * context switch rate down.
 */
static inline int queue_congestion_on_threshold(struct request_queue *q)
{
	return q->nr_congestion_on;
}

/*
 * The threshold at which a queue is considered to be uncongested
 */
static inline int queue_congestion_off_threshold(struct request_queue *q)
{
	return q->nr_congestion_off;
}

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define rq_for_each_integrity_segment(bvl, _rq, _iter)		\
	__rq_for_each_bio(_iter.bio, _rq)			\
		bip_for_each_vec(bvl, _iter.bio->bi_integrity, _iter.i)

#endif /* BLK_DEV_INTEGRITY */

static inline int blk_cpu_to_group(int cpu)
{
#ifdef CONFIG_SCHED_MC
	const struct cpumask *mask = cpu_coregroup_mask(cpu);
	return cpumask_first(mask);
#elif defined(CONFIG_SCHED_SMT)
	return cpumask_first(topology_thread_cpumask(cpu));
#else
	return cpu;
#endif
}

static inline int blk_do_io_stat(struct request *rq)
{
	struct gendisk *disk = rq->rq_disk;

	if (!disk || !disk->queue)
		return 0;

	return blk_queue_io_stat(disk->queue) && (rq->cmd_flags & REQ_ELVPRIV);
}

#endif
