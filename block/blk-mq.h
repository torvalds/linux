#ifndef INT_BLK_MQ_H
#define INT_BLK_MQ_H

struct blk_mq_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	rq_list;
	}  ____cacheline_aligned_in_smp;

	unsigned int		cpu;
	unsigned int		index_hw;
	unsigned int		ipi_redirect;

	/* incremented at dispatch time */
	unsigned long		rq_dispatched[2];
	unsigned long		rq_merged;

	/* incremented at completion time */
	unsigned long		____cacheline_aligned_in_smp rq_completed[2];

	struct request_queue	*queue;
	struct kobject		kobj;
};

void __blk_mq_complete_request(struct request *rq);
void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async);
void blk_mq_init_flush(struct request_queue *q);
void blk_mq_drain_queue(struct request_queue *q);
void blk_mq_free_queue(struct request_queue *q);
void blk_mq_rq_init(struct blk_mq_hw_ctx *hctx, struct request *rq);

/*
 * CPU hotplug helpers
 */
struct blk_mq_cpu_notifier;
void blk_mq_init_cpu_notifier(struct blk_mq_cpu_notifier *notifier,
			      void (*fn)(void *, unsigned long, unsigned int),
			      void *data);
void blk_mq_register_cpu_notifier(struct blk_mq_cpu_notifier *notifier);
void blk_mq_unregister_cpu_notifier(struct blk_mq_cpu_notifier *notifier);
void blk_mq_cpu_init(void);
void blk_mq_enable_hotplug(void);
void blk_mq_disable_hotplug(void);

/*
 * CPU -> queue mappings
 */
struct blk_mq_reg;
extern unsigned int *blk_mq_make_queue_map(struct blk_mq_reg *reg);
extern int blk_mq_update_queue_map(unsigned int *map, unsigned int nr_queues);

void blk_mq_add_timer(struct request *rq);

#endif
