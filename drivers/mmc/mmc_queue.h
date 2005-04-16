#ifndef MMC_QUEUE_H
#define MMC_QUEUE_H

struct request;
struct task_struct;

struct mmc_queue {
	struct mmc_card		*card;
	struct completion	thread_complete;
	wait_queue_head_t	thread_wq;
	struct semaphore	thread_sem;
	unsigned int		flags;
	struct request		*req;
	int			(*prep_fn)(struct mmc_queue *, struct request *);
	int			(*issue_fn)(struct mmc_queue *, struct request *);
	void			*data;
	struct request_queue	*queue;
	struct scatterlist	*sg;
};

struct mmc_io_request {
	struct request		*rq;
	int			num;
	struct mmc_command	selcmd;		/* mmc_queue private */
	struct mmc_command	cmd[4];		/* max 4 commands */
};

extern int mmc_init_queue(struct mmc_queue *, struct mmc_card *, spinlock_t *);
extern void mmc_cleanup_queue(struct mmc_queue *);
extern void mmc_queue_suspend(struct mmc_queue *);
extern void mmc_queue_resume(struct mmc_queue *);

#endif
