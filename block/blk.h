#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

extern struct kmem_cache *blk_requestq_cachep;
extern struct kobj_type blk_queue_ktype;

void __blk_queue_free_tags(struct request_queue *q);

void blk_queue_congestion_threshold(struct request_queue *q);

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

#endif
