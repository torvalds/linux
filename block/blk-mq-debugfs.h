#ifndef INT_BLK_MQ_DEBUGFS_H
#define INT_BLK_MQ_DEBUGFS_H

#ifdef CONFIG_BLK_DEBUG_FS
int blk_mq_debugfs_register(struct request_queue *q);
void blk_mq_debugfs_unregister(struct request_queue *q);
int blk_mq_debugfs_register_hctx(struct request_queue *q,
				 struct blk_mq_hw_ctx *hctx);
void blk_mq_debugfs_unregister_hctx(struct blk_mq_hw_ctx *hctx);
int blk_mq_debugfs_register_hctxs(struct request_queue *q);
void blk_mq_debugfs_unregister_hctxs(struct request_queue *q);
#else
static inline int blk_mq_debugfs_register(struct request_queue *q)
{
	return 0;
}

static inline void blk_mq_debugfs_unregister(struct request_queue *q)
{
}

static inline int blk_mq_debugfs_register_hctx(struct request_queue *q,
					       struct blk_mq_hw_ctx *hctx)
{
	return 0;
}

static inline void blk_mq_debugfs_unregister_hctx(struct blk_mq_hw_ctx *hctx)
{
}

static inline int blk_mq_debugfs_register_hctxs(struct request_queue *q)
{
	return 0;
}

static inline void blk_mq_debugfs_unregister_hctxs(struct request_queue *q)
{
}
#endif

#endif
