#ifndef RQ_QOS_H
#define RQ_QOS_H

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/atomic.h>
#include <linux/wait.h>

enum rq_qos_id {
	RQ_QOS_WBT,
	RQ_QOS_CGROUP,
};

struct rq_wait {
	wait_queue_head_t wait;
	atomic_t inflight;
};

struct rq_qos {
	struct rq_qos_ops *ops;
	struct request_queue *q;
	enum rq_qos_id id;
	struct rq_qos *next;
};

struct rq_qos_ops {
	void (*throttle)(struct rq_qos *, struct bio *, spinlock_t *);
	void (*track)(struct rq_qos *, struct request *, struct bio *);
	void (*issue)(struct rq_qos *, struct request *);
	void (*requeue)(struct rq_qos *, struct request *);
	void (*done)(struct rq_qos *, struct request *);
	void (*done_bio)(struct rq_qos *, struct bio *);
	void (*cleanup)(struct rq_qos *, struct bio *);
	void (*exit)(struct rq_qos *);
};

struct rq_depth {
	unsigned int max_depth;

	int scale_step;
	bool scaled_max;

	unsigned int queue_depth;
	unsigned int default_depth;
};

static inline struct rq_qos *rq_qos_id(struct request_queue *q,
				       enum rq_qos_id id)
{
	struct rq_qos *rqos;
	for (rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->id == id)
			break;
	}
	return rqos;
}

static inline struct rq_qos *wbt_rq_qos(struct request_queue *q)
{
	return rq_qos_id(q, RQ_QOS_WBT);
}

static inline struct rq_qos *blkcg_rq_qos(struct request_queue *q)
{
	return rq_qos_id(q, RQ_QOS_CGROUP);
}

static inline void rq_wait_init(struct rq_wait *rq_wait)
{
	atomic_set(&rq_wait->inflight, 0);
	init_waitqueue_head(&rq_wait->wait);
}

static inline void rq_qos_add(struct request_queue *q, struct rq_qos *rqos)
{
	rqos->next = q->rq_qos;
	q->rq_qos = rqos;
}

static inline void rq_qos_del(struct request_queue *q, struct rq_qos *rqos)
{
	struct rq_qos *cur, *prev = NULL;
	for (cur = q->rq_qos; cur; cur = cur->next) {
		if (cur == rqos) {
			if (prev)
				prev->next = rqos->next;
			else
				q->rq_qos = cur;
			break;
		}
		prev = cur;
	}
}

bool rq_wait_inc_below(struct rq_wait *rq_wait, unsigned int limit);
void rq_depth_scale_up(struct rq_depth *rqd);
void rq_depth_scale_down(struct rq_depth *rqd, bool hard_throttle);
bool rq_depth_calc_max_depth(struct rq_depth *rqd);

void rq_qos_cleanup(struct request_queue *, struct bio *);
void rq_qos_done(struct request_queue *, struct request *);
void rq_qos_issue(struct request_queue *, struct request *);
void rq_qos_requeue(struct request_queue *, struct request *);
void rq_qos_done_bio(struct request_queue *q, struct bio *bio);
void rq_qos_throttle(struct request_queue *, struct bio *, spinlock_t *);
void rq_qos_track(struct request_queue *q, struct request *, struct bio *);
void rq_qos_exit(struct request_queue *);
#endif
