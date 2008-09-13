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
	cpumask_t mask = cpu_coregroup_map(cpu);
	return first_cpu(mask);
#elif defined(CONFIG_SCHED_SMT)
	return first_cpu(per_cpu(cpu_sibling_map, cpu));
#else
	return cpu;
#endif
}

#endif
