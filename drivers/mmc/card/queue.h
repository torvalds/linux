#ifndef MMC_QUEUE_H
#define MMC_QUEUE_H

struct request;
struct task_struct;

struct mmc_queue {
	struct mmc_card		*card;
	struct task_struct	*thread;
	struct semaphore	thread_sem;
	unsigned int		flags;
	struct request		*req;
	int			(*issue_fn)(struct mmc_queue *, struct request *);
	void			*data;
	struct request_queue	*queue;
	struct scatterlist	*sg;
};

extern int mmc_init_queue(struct mmc_queue *, struct mmc_card *, spinlock_t *);
extern void mmc_cleanup_queue(struct mmc_queue *);
extern void mmc_queue_suspend(struct mmc_queue *);
extern void mmc_queue_resume(struct mmc_queue *);

#endif
