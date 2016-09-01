#ifndef BLK_STAT_H
#define BLK_STAT_H

/*
 * ~0.13s window as a power-of-2 (2^27 nsecs)
 */
#define BLK_STAT_NSEC	134217728ULL
#define BLK_STAT_MASK	~(BLK_STAT_NSEC - 1)

void blk_stat_add(struct blk_rq_stat *, struct request *);
void blk_hctx_stat_get(struct blk_mq_hw_ctx *, struct blk_rq_stat *);
void blk_queue_stat_get(struct request_queue *, struct blk_rq_stat *);
void blk_stat_clear(struct request_queue *q);
void blk_stat_init(struct blk_rq_stat *);
void blk_stat_sum(struct blk_rq_stat *, struct blk_rq_stat *);
bool blk_stat_is_current(struct blk_rq_stat *);

#endif
