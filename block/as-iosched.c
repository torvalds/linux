/*
 *  Anticipatory & deadline i/o scheduler.
 *
 *  Copyright (C) 2002 Jens Axboe <axboe@kernel.dk>
 *                     Nick Piggin <nickpiggin@yahoo.com.au>
 *
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/interrupt.h>

/*
 * See Documentation/block/as-iosched.txt
 */

/*
 * max time before a read is submitted.
 */
#define default_read_expire (HZ / 8)

/*
 * ditto for writes, these limits are not hard, even
 * if the disk is capable of satisfying them.
 */
#define default_write_expire (HZ / 4)

/*
 * read_batch_expire describes how long we will allow a stream of reads to
 * persist before looking to see whether it is time to switch over to writes.
 */
#define default_read_batch_expire (HZ / 2)

/*
 * write_batch_expire describes how long we want a stream of writes to run for.
 * This is not a hard limit, but a target we set for the auto-tuning thingy.
 * See, the problem is: we can send a lot of writes to disk cache / TCQ in
 * a short amount of time...
 */
#define default_write_batch_expire (HZ / 8)

/*
 * max time we may wait to anticipate a read (default around 6ms)
 */
#define default_antic_expire ((HZ / 150) ? HZ / 150 : 1)

/*
 * Keep track of up to 20ms thinktimes. We can go as big as we like here,
 * however huge values tend to interfere and not decay fast enough. A program
 * might be in a non-io phase of operation. Waiting on user input for example,
 * or doing a lengthy computation. A small penalty can be justified there, and
 * will still catch out those processes that constantly have large thinktimes.
 */
#define MAX_THINKTIME (HZ/50UL)

/* Bits in as_io_context.state */
enum as_io_states {
	AS_TASK_RUNNING=0,	/* Process has not exited */
	AS_TASK_IOSTARTED,	/* Process has started some IO */
	AS_TASK_IORUNNING,	/* Process has completed some IO */
};

enum anticipation_status {
	ANTIC_OFF=0,		/* Not anticipating (normal operation)	*/
	ANTIC_WAIT_REQ,		/* The last read has not yet completed  */
	ANTIC_WAIT_NEXT,	/* Currently anticipating a request vs
				   last read (which has completed) */
	ANTIC_FINISHED,		/* Anticipating but have found a candidate
				 * or timed out */
};

struct as_data {
	/*
	 * run time data
	 */

	struct request_queue *q;	/* the "owner" queue */

	/*
	 * requests (as_rq s) are present on both sort_list and fifo_list
	 */
	struct rb_root sort_list[2];
	struct list_head fifo_list[2];

	struct request *next_rq[2];	/* next in sort order */
	sector_t last_sector[2];	/* last SYNC & ASYNC sectors */

	unsigned long exit_prob;	/* probability a task will exit while
					   being waited on */
	unsigned long exit_no_coop;	/* probablility an exited task will
					   not be part of a later cooperating
					   request */
	unsigned long new_ttime_total; 	/* mean thinktime on new proc */
	unsigned long new_ttime_mean;
	u64 new_seek_total;		/* mean seek on new proc */
	sector_t new_seek_mean;

	unsigned long current_batch_expires;
	unsigned long last_check_fifo[2];
	int changed_batch;		/* 1: waiting for old batch to end */
	int new_batch;			/* 1: waiting on first read complete */
	int batch_data_dir;		/* current batch SYNC / ASYNC */
	int write_batch_count;		/* max # of reqs in a write batch */
	int current_write_count;	/* how many requests left this batch */
	int write_batch_idled;		/* has the write batch gone idle? */

	enum anticipation_status antic_status;
	unsigned long antic_start;	/* jiffies: when it started */
	struct timer_list antic_timer;	/* anticipatory scheduling timer */
	struct work_struct antic_work;	/* Deferred unplugging */
	struct io_context *io_context;	/* Identify the expected process */
	int ioc_finished; /* IO associated with io_context is finished */
	int nr_dispatched;

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	unsigned long fifo_expire[2];
	unsigned long batch_expire[2];
	unsigned long antic_expire;
};

/*
 * per-request data.
 */
enum arq_state {
	AS_RQ_NEW=0,		/* New - not referenced and not on any lists */
	AS_RQ_QUEUED,		/* In the request queue. It belongs to the
				   scheduler */
	AS_RQ_DISPATCHED,	/* On the dispatch list. It belongs to the
				   driver now */
	AS_RQ_PRESCHED,		/* Debug poisoning for requests being used */
	AS_RQ_REMOVED,
	AS_RQ_MERGED,
	AS_RQ_POSTSCHED,	/* when they shouldn't be */
};

#define RQ_IOC(rq)	((struct io_context *) (rq)->elevator_private)
#define RQ_STATE(rq)	((enum arq_state)(rq)->elevator_private2)
#define RQ_SET_STATE(rq, state)	((rq)->elevator_private2 = (void *) state)

static DEFINE_PER_CPU(unsigned long, ioc_count);
static struct completion *ioc_gone;
static DEFINE_SPINLOCK(ioc_gone_lock);

static void as_move_to_dispatch(struct as_data *ad, struct request *rq);
static void as_antic_stop(struct as_data *ad);

/*
 * IO Context helper functions
 */

/* Called to deallocate the as_io_context */
static void free_as_io_context(struct as_io_context *aic)
{
	kfree(aic);
	elv_ioc_count_dec(ioc_count);
	if (ioc_gone) {
		/*
		 * AS scheduler is exiting, grab exit lock and check
		 * the pending io context count. If it hits zero,
		 * complete ioc_gone and set it back to NULL.
		 */
		spin_lock(&ioc_gone_lock);
		if (ioc_gone && !elv_ioc_count_read(ioc_count)) {
			complete(ioc_gone);
			ioc_gone = NULL;
		}
		spin_unlock(&ioc_gone_lock);
	}
}

static void as_trim(struct io_context *ioc)
{
	spin_lock_irq(&ioc->lock);
	if (ioc->aic)
		free_as_io_context(ioc->aic);
	ioc->aic = NULL;
	spin_unlock_irq(&ioc->lock);
}

/* Called when the task exits */
static void exit_as_io_context(struct as_io_context *aic)
{
	WARN_ON(!test_bit(AS_TASK_RUNNING, &aic->state));
	clear_bit(AS_TASK_RUNNING, &aic->state);
}

static struct as_io_context *alloc_as_io_context(void)
{
	struct as_io_context *ret;

	ret = kmalloc(sizeof(*ret), GFP_ATOMIC);
	if (ret) {
		ret->dtor = free_as_io_context;
		ret->exit = exit_as_io_context;
		ret->state = 1 << AS_TASK_RUNNING;
		atomic_set(&ret->nr_queued, 0);
		atomic_set(&ret->nr_dispatched, 0);
		spin_lock_init(&ret->lock);
		ret->ttime_total = 0;
		ret->ttime_samples = 0;
		ret->ttime_mean = 0;
		ret->seek_total = 0;
		ret->seek_samples = 0;
		ret->seek_mean = 0;
		elv_ioc_count_inc(ioc_count);
	}

	return ret;
}

/*
 * If the current task has no AS IO context then create one and initialise it.
 * Then take a ref on the task's io context and return it.
 */
static struct io_context *as_get_io_context(int node)
{
	struct io_context *ioc = get_io_context(GFP_ATOMIC, node);
	if (ioc && !ioc->aic) {
		ioc->aic = alloc_as_io_context();
		if (!ioc->aic) {
			put_io_context(ioc);
			ioc = NULL;
		}
	}
	return ioc;
}

static void as_put_io_context(struct request *rq)
{
	struct as_io_context *aic;

	if (unlikely(!RQ_IOC(rq)))
		return;

	aic = RQ_IOC(rq)->aic;

	if (rq_is_sync(rq) && aic) {
		unsigned long flags;

		spin_lock_irqsave(&aic->lock, flags);
		set_bit(AS_TASK_IORUNNING, &aic->state);
		aic->last_end_request = jiffies;
		spin_unlock_irqrestore(&aic->lock, flags);
	}

	put_io_context(RQ_IOC(rq));
}

/*
 * rb tree support functions
 */
#define RQ_RB_ROOT(ad, rq)	(&(ad)->sort_list[rq_is_sync((rq))])

static void as_add_rq_rb(struct as_data *ad, struct request *rq)
{
	struct request *alias;

	while ((unlikely(alias = elv_rb_add(RQ_RB_ROOT(ad, rq), rq)))) {
		as_move_to_dispatch(ad, alias);
		as_antic_stop(ad);
	}
}

static inline void as_del_rq_rb(struct as_data *ad, struct request *rq)
{
	elv_rb_del(RQ_RB_ROOT(ad, rq), rq);
}

/*
 * IO Scheduler proper
 */

#define MAXBACK (1024 * 1024)	/*
				 * Maximum distance the disk will go backward
				 * for a request.
				 */

#define BACK_PENALTY	2

/*
 * as_choose_req selects the preferred one of two requests of the same data_dir
 * ignoring time - eg. timeouts, which is the job of as_dispatch_request
 */
static struct request *
as_choose_req(struct as_data *ad, struct request *rq1, struct request *rq2)
{
	int data_dir;
	sector_t last, s1, s2, d1, d2;
	int r1_wrap=0, r2_wrap=0;	/* requests are behind the disk head */
	const sector_t maxback = MAXBACK;

	if (rq1 == NULL || rq1 == rq2)
		return rq2;
	if (rq2 == NULL)
		return rq1;

	data_dir = rq_is_sync(rq1);

	last = ad->last_sector[data_dir];
	s1 = blk_rq_pos(rq1);
	s2 = blk_rq_pos(rq2);

	BUG_ON(data_dir != rq_is_sync(rq2));

	/*
	 * Strict one way elevator _except_ in the case where we allow
	 * short backward seeks which are biased as twice the cost of a
	 * similar forward seek.
	 */
	if (s1 >= last)
		d1 = s1 - last;
	else if (s1+maxback >= last)
		d1 = (last - s1)*BACK_PENALTY;
	else {
		r1_wrap = 1;
		d1 = 0; /* shut up, gcc */
	}

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2+maxback >= last)
		d2 = (last - s2)*BACK_PENALTY;
	else {
		r2_wrap = 1;
		d2 = 0;
	}

	/* Found required data */
	if (!r1_wrap && r2_wrap)
		return rq1;
	else if (!r2_wrap && r1_wrap)
		return rq2;
	else if (r1_wrap && r2_wrap) {
		/* both behind the head */
		if (s1 <= s2)
			return rq1;
		else
			return rq2;
	}

	/* Both requests in front of the head */
	if (d1 < d2)
		return rq1;
	else if (d2 < d1)
		return rq2;
	else {
		if (s1 >= s2)
			return rq1;
		else
			return rq2;
	}
}

/*
 * as_find_next_rq finds the next request after @prev in elevator order.
 * this with as_choose_req form the basis for how the scheduler chooses
 * what request to process next. Anticipation works on top of this.
 */
static struct request *
as_find_next_rq(struct as_data *ad, struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next = NULL, *prev = NULL;

	BUG_ON(RB_EMPTY_NODE(&last->rb_node));

	if (rbprev)
		prev = rb_entry_rq(rbprev);

	if (rbnext)
		next = rb_entry_rq(rbnext);
	else {
		const int data_dir = rq_is_sync(last);

		rbnext = rb_first(&ad->sort_list[data_dir]);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return as_choose_req(ad, next, prev);
}

/*
 * anticipatory scheduling functions follow
 */

/*
 * as_antic_expired tells us when we have anticipated too long.
 * The funny "absolute difference" math on the elapsed time is to handle
 * jiffy wraps, and disks which have been idle for 0x80000000 jiffies.
 */
static int as_antic_expired(struct as_data *ad)
{
	long delta_jif;

	delta_jif = jiffies - ad->antic_start;
	if (unlikely(delta_jif < 0))
		delta_jif = -delta_jif;
	if (delta_jif < ad->antic_expire)
		return 0;

	return 1;
}

/*
 * as_antic_waitnext starts anticipating that a nice request will soon be
 * submitted. See also as_antic_waitreq
 */
static void as_antic_waitnext(struct as_data *ad)
{
	unsigned long timeout;

	BUG_ON(ad->antic_status != ANTIC_OFF
			&& ad->antic_status != ANTIC_WAIT_REQ);

	timeout = ad->antic_start + ad->antic_expire;

	mod_timer(&ad->antic_timer, timeout);

	ad->antic_status = ANTIC_WAIT_NEXT;
}

/*
 * as_antic_waitreq starts anticipating. We don't start timing the anticipation
 * until the request that we're anticipating on has finished. This means we
 * are timing from when the candidate process wakes up hopefully.
 */
static void as_antic_waitreq(struct as_data *ad)
{
	BUG_ON(ad->antic_status == ANTIC_FINISHED);
	if (ad->antic_status == ANTIC_OFF) {
		if (!ad->io_context || ad->ioc_finished)
			as_antic_waitnext(ad);
		else
			ad->antic_status = ANTIC_WAIT_REQ;
	}
}

/*
 * This is called directly by the functions in this file to stop anticipation.
 * We kill the timer and schedule a call to the request_fn asap.
 */
static void as_antic_stop(struct as_data *ad)
{
	int status = ad->antic_status;

	if (status == ANTIC_WAIT_REQ || status == ANTIC_WAIT_NEXT) {
		if (status == ANTIC_WAIT_NEXT)
			del_timer(&ad->antic_timer);
		ad->antic_status = ANTIC_FINISHED;
		/* see as_work_handler */
		kblockd_schedule_work(ad->q, &ad->antic_work);
	}
}

/*
 * as_antic_timeout is the timer function set by as_antic_waitnext.
 */
static void as_antic_timeout(unsigned long data)
{
	struct request_queue *q = (struct request_queue *)data;
	struct as_data *ad = q->elevator->elevator_data;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (ad->antic_status == ANTIC_WAIT_REQ
			|| ad->antic_status == ANTIC_WAIT_NEXT) {
		struct as_io_context *aic;
		spin_lock(&ad->io_context->lock);
		aic = ad->io_context->aic;

		ad->antic_status = ANTIC_FINISHED;
		kblockd_schedule_work(q, &ad->antic_work);

		if (aic->ttime_samples == 0) {
			/* process anticipated on has exited or timed out*/
			ad->exit_prob = (7*ad->exit_prob + 256)/8;
		}
		if (!test_bit(AS_TASK_RUNNING, &aic->state)) {
			/* process not "saved" by a cooperating request */
			ad->exit_no_coop = (7*ad->exit_no_coop + 256)/8;
		}
		spin_unlock(&ad->io_context->lock);
	}
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void as_update_thinktime(struct as_data *ad, struct as_io_context *aic,
				unsigned long ttime)
{
	/* fixed point: 1.0 == 1<<8 */
	if (aic->ttime_samples == 0) {
		ad->new_ttime_total = (7*ad->new_ttime_total + 256*ttime) / 8;
		ad->new_ttime_mean = ad->new_ttime_total / 256;

		ad->exit_prob = (7*ad->exit_prob)/8;
	}
	aic->ttime_samples = (7*aic->ttime_samples + 256) / 8;
	aic->ttime_total = (7*aic->ttime_total + 256*ttime) / 8;
	aic->ttime_mean = (aic->ttime_total + 128) / aic->ttime_samples;
}

static void as_update_seekdist(struct as_data *ad, struct as_io_context *aic,
				sector_t sdist)
{
	u64 total;

	if (aic->seek_samples == 0) {
		ad->new_seek_total = (7*ad->new_seek_total + 256*(u64)sdist)/8;
		ad->new_seek_mean = ad->new_seek_total / 256;
	}

	/*
	 * Don't allow the seek distance to get too large from the
	 * odd fragment, pagein, etc
	 */
	if (aic->seek_samples <= 60) /* second&third seek */
		sdist = min(sdist, (aic->seek_mean * 4) + 2*1024*1024);
	else
		sdist = min(sdist, (aic->seek_mean * 4)	+ 2*1024*64);

	aic->seek_samples = (7*aic->seek_samples + 256) / 8;
	aic->seek_total = (7*aic->seek_total + (u64)256*sdist) / 8;
	total = aic->seek_total + (aic->seek_samples/2);
	do_div(total, aic->seek_samples);
	aic->seek_mean = (sector_t)total;
}

/*
 * as_update_iohist keeps a decaying histogram of IO thinktimes, and
 * updates @aic->ttime_mean based on that. It is called when a new
 * request is queued.
 */
static void as_update_iohist(struct as_data *ad, struct as_io_context *aic,
				struct request *rq)
{
	int data_dir = rq_is_sync(rq);
	unsigned long thinktime = 0;
	sector_t seek_dist;

	if (aic == NULL)
		return;

	if (data_dir == BLK_RW_SYNC) {
		unsigned long in_flight = atomic_read(&aic->nr_queued)
					+ atomic_read(&aic->nr_dispatched);
		spin_lock(&aic->lock);
		if (test_bit(AS_TASK_IORUNNING, &aic->state) ||
			test_bit(AS_TASK_IOSTARTED, &aic->state)) {
			/* Calculate read -> read thinktime */
			if (test_bit(AS_TASK_IORUNNING, &aic->state)
							&& in_flight == 0) {
				thinktime = jiffies - aic->last_end_request;
				thinktime = min(thinktime, MAX_THINKTIME-1);
			}
			as_update_thinktime(ad, aic, thinktime);

			/* Calculate read -> read seek distance */
			if (aic->last_request_pos < blk_rq_pos(rq))
				seek_dist = blk_rq_pos(rq) -
					    aic->last_request_pos;
			else
				seek_dist = aic->last_request_pos -
					    blk_rq_pos(rq);
			as_update_seekdist(ad, aic, seek_dist);
		}
		aic->last_request_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);
		set_bit(AS_TASK_IOSTARTED, &aic->state);
		spin_unlock(&aic->lock);
	}
}

/*
 * as_close_req decides if one request is considered "close" to the
 * previous one issued.
 */
static int as_close_req(struct as_data *ad, struct as_io_context *aic,
			struct request *rq)
{
	unsigned long delay;	/* jiffies */
	sector_t last = ad->last_sector[ad->batch_data_dir];
	sector_t next = blk_rq_pos(rq);
	sector_t delta; /* acceptable close offset (in sectors) */
	sector_t s;

	if (ad->antic_status == ANTIC_OFF || !ad->ioc_finished)
		delay = 0;
	else
		delay = jiffies - ad->antic_start;

	if (delay == 0)
		delta = 8192;
	else if (delay <= (20 * HZ / 1000) && delay <= ad->antic_expire)
		delta = 8192 << delay;
	else
		return 1;

	if ((last <= next + (delta>>1)) && (next <= last + delta))
		return 1;

	if (last < next)
		s = next - last;
	else
		s = last - next;

	if (aic->seek_samples == 0) {
		/*
		 * Process has just started IO. Use past statistics to
		 * gauge success possibility
		 */
		if (ad->new_seek_mean > s) {
			/* this request is better than what we're expecting */
			return 1;
		}

	} else {
		if (aic->seek_mean > s) {
			/* this request is better than what we're expecting */
			return 1;
		}
	}

	return 0;
}

/*
 * as_can_break_anticipation returns true if we have been anticipating this
 * request.
 *
 * It also returns true if the process against which we are anticipating
 * submits a write - that's presumably an fsync, O_SYNC write, etc. We want to
 * dispatch it ASAP, because we know that application will not be submitting
 * any new reads.
 *
 * If the task which has submitted the request has exited, break anticipation.
 *
 * If this task has queued some other IO, do not enter enticipation.
 */
static int as_can_break_anticipation(struct as_data *ad, struct request *rq)
{
	struct io_context *ioc;
	struct as_io_context *aic;

	ioc = ad->io_context;
	BUG_ON(!ioc);
	spin_lock(&ioc->lock);

	if (rq && ioc == RQ_IOC(rq)) {
		/* request from same process */
		spin_unlock(&ioc->lock);
		return 1;
	}

	if (ad->ioc_finished && as_antic_expired(ad)) {
		/*
		 * In this situation status should really be FINISHED,
		 * however the timer hasn't had the chance to run yet.
		 */
		spin_unlock(&ioc->lock);
		return 1;
	}

	aic = ioc->aic;
	if (!aic) {
		spin_unlock(&ioc->lock);
		return 0;
	}

	if (atomic_read(&aic->nr_queued) > 0) {
		/* process has more requests queued */
		spin_unlock(&ioc->lock);
		return 1;
	}

	if (atomic_read(&aic->nr_dispatched) > 0) {
		/* process has more requests dispatched */
		spin_unlock(&ioc->lock);
		return 1;
	}

	if (rq && rq_is_sync(rq) && as_close_req(ad, aic, rq)) {
		/*
		 * Found a close request that is not one of ours.
		 *
		 * This makes close requests from another process update
		 * our IO history. Is generally useful when there are
		 * two or more cooperating processes working in the same
		 * area.
		 */
		if (!test_bit(AS_TASK_RUNNING, &aic->state)) {
			if (aic->ttime_samples == 0)
				ad->exit_prob = (7*ad->exit_prob + 256)/8;

			ad->exit_no_coop = (7*ad->exit_no_coop)/8;
		}

		as_update_iohist(ad, aic, rq);
		spin_unlock(&ioc->lock);
		return 1;
	}

	if (!test_bit(AS_TASK_RUNNING, &aic->state)) {
		/* process anticipated on has exited */
		if (aic->ttime_samples == 0)
			ad->exit_prob = (7*ad->exit_prob + 256)/8;

		if (ad->exit_no_coop > 128) {
			spin_unlock(&ioc->lock);
			return 1;
		}
	}

	if (aic->ttime_samples == 0) {
		if (ad->new_ttime_mean > ad->antic_expire) {
			spin_unlock(&ioc->lock);
			return 1;
		}
		if (ad->exit_prob * ad->exit_no_coop > 128*256) {
			spin_unlock(&ioc->lock);
			return 1;
		}
	} else if (aic->ttime_mean > ad->antic_expire) {
		/* the process thinks too much between requests */
		spin_unlock(&ioc->lock);
		return 1;
	}
	spin_unlock(&ioc->lock);
	return 0;
}

/*
 * as_can_anticipate indicates whether we should either run rq
 * or keep anticipating a better request.
 */
static int as_can_anticipate(struct as_data *ad, struct request *rq)
{
#if 0 /* disable for now, we need to check tag level as well */
	/*
	 * SSD device without seek penalty, disable idling
	 */
	if (blk_queue_nonrot(ad->q)) axman
		return 0;
#endif

	if (!ad->io_context)
		/*
		 * Last request submitted was a write
		 */
		return 0;

	if (ad->antic_status == ANTIC_FINISHED)
		/*
		 * Don't restart if we have just finished. Run the next request
		 */
		return 0;

	if (as_can_break_anticipation(ad, rq))
		/*
		 * This request is a good candidate. Don't keep anticipating,
		 * run it.
		 */
		return 0;

	/*
	 * OK from here, we haven't finished, and don't have a decent request!
	 * Status is either ANTIC_OFF so start waiting,
	 * ANTIC_WAIT_REQ so continue waiting for request to finish
	 * or ANTIC_WAIT_NEXT so continue waiting for an acceptable request.
	 */

	return 1;
}

/*
 * as_update_rq must be called whenever a request (rq) is added to
 * the sort_list. This function keeps caches up to date, and checks if the
 * request might be one we are "anticipating"
 */
static void as_update_rq(struct as_data *ad, struct request *rq)
{
	const int data_dir = rq_is_sync(rq);

	/* keep the next_rq cache up to date */
	ad->next_rq[data_dir] = as_choose_req(ad, rq, ad->next_rq[data_dir]);

	/*
	 * have we been anticipating this request?
	 * or does it come from the same process as the one we are anticipating
	 * for?
	 */
	if (ad->antic_status == ANTIC_WAIT_REQ
			|| ad->antic_status == ANTIC_WAIT_NEXT) {
		if (as_can_break_anticipation(ad, rq))
			as_antic_stop(ad);
	}
}

/*
 * Gathers timings and resizes the write batch automatically
 */
static void update_write_batch(struct as_data *ad)
{
	unsigned long batch = ad->batch_expire[BLK_RW_ASYNC];
	long write_time;

	write_time = (jiffies - ad->current_batch_expires) + batch;
	if (write_time < 0)
		write_time = 0;

	if (write_time > batch && !ad->write_batch_idled) {
		if (write_time > batch * 3)
			ad->write_batch_count /= 2;
		else
			ad->write_batch_count--;
	} else if (write_time < batch && ad->current_write_count == 0) {
		if (batch > write_time * 3)
			ad->write_batch_count *= 2;
		else
			ad->write_batch_count++;
	}

	if (ad->write_batch_count < 1)
		ad->write_batch_count = 1;
}

/*
 * as_completed_request is to be called when a request has completed and
 * returned something to the requesting process, be it an error or data.
 */
static void as_completed_request(struct request_queue *q, struct request *rq)
{
	struct as_data *ad = q->elevator->elevator_data;

	WARN_ON(!list_empty(&rq->queuelist));

	if (RQ_STATE(rq) != AS_RQ_REMOVED) {
		WARN(1, "rq->state %d\n", RQ_STATE(rq));
		goto out;
	}

	if (ad->changed_batch && ad->nr_dispatched == 1) {
		ad->current_batch_expires = jiffies +
					ad->batch_expire[ad->batch_data_dir];
		kblockd_schedule_work(q, &ad->antic_work);
		ad->changed_batch = 0;

		if (ad->batch_data_dir == BLK_RW_SYNC)
			ad->new_batch = 1;
	}
	WARN_ON(ad->nr_dispatched == 0);
	ad->nr_dispatched--;

	/*
	 * Start counting the batch from when a request of that direction is
	 * actually serviced. This should help devices with big TCQ windows
	 * and writeback caches
	 */
	if (ad->new_batch && ad->batch_data_dir == rq_is_sync(rq)) {
		update_write_batch(ad);
		ad->current_batch_expires = jiffies +
				ad->batch_expire[BLK_RW_SYNC];
		ad->new_batch = 0;
	}

	if (ad->io_context == RQ_IOC(rq) && ad->io_context) {
		ad->antic_start = jiffies;
		ad->ioc_finished = 1;
		if (ad->antic_status == ANTIC_WAIT_REQ) {
			/*
			 * We were waiting on this request, now anticipate
			 * the next one
			 */
			as_antic_waitnext(ad);
		}
	}

	as_put_io_context(rq);
out:
	RQ_SET_STATE(rq, AS_RQ_POSTSCHED);
}

/*
 * as_remove_queued_request removes a request from the pre dispatch queue
 * without updating refcounts. It is expected the caller will drop the
 * reference unless it replaces the request at somepart of the elevator
 * (ie. the dispatch queue)
 */
static void as_remove_queued_request(struct request_queue *q,
				     struct request *rq)
{
	const int data_dir = rq_is_sync(rq);
	struct as_data *ad = q->elevator->elevator_data;
	struct io_context *ioc;

	WARN_ON(RQ_STATE(rq) != AS_RQ_QUEUED);

	ioc = RQ_IOC(rq);
	if (ioc && ioc->aic) {
		BUG_ON(!atomic_read(&ioc->aic->nr_queued));
		atomic_dec(&ioc->aic->nr_queued);
	}

	/*
	 * Update the "next_rq" cache if we are about to remove its
	 * entry
	 */
	if (ad->next_rq[data_dir] == rq)
		ad->next_rq[data_dir] = as_find_next_rq(ad, rq);

	rq_fifo_clear(rq);
	as_del_rq_rb(ad, rq);
}

/*
 * as_fifo_expired returns 0 if there are no expired requests on the fifo,
 * 1 otherwise.  It is ratelimited so that we only perform the check once per
 * `fifo_expire' interval.  Otherwise a large number of expired requests
 * would create a hopeless seekstorm.
 *
 * See as_antic_expired comment.
 */
static int as_fifo_expired(struct as_data *ad, int adir)
{
	struct request *rq;
	long delta_jif;

	delta_jif = jiffies - ad->last_check_fifo[adir];
	if (unlikely(delta_jif < 0))
		delta_jif = -delta_jif;
	if (delta_jif < ad->fifo_expire[adir])
		return 0;

	ad->last_check_fifo[adir] = jiffies;

	if (list_empty(&ad->fifo_list[adir]))
		return 0;

	rq = rq_entry_fifo(ad->fifo_list[adir].next);

	return time_after(jiffies, rq_fifo_time(rq));
}

/*
 * as_batch_expired returns true if the current batch has expired. A batch
 * is a set of reads or a set of writes.
 */
static inline int as_batch_expired(struct as_data *ad)
{
	if (ad->changed_batch || ad->new_batch)
		return 0;

	if (ad->batch_data_dir == BLK_RW_SYNC)
		/* TODO! add a check so a complete fifo gets written? */
		return time_after(jiffies, ad->current_batch_expires);

	return time_after(jiffies, ad->current_batch_expires)
		|| ad->current_write_count == 0;
}

/*
 * move an entry to dispatch queue
 */
static void as_move_to_dispatch(struct as_data *ad, struct request *rq)
{
	const int data_dir = rq_is_sync(rq);

	BUG_ON(RB_EMPTY_NODE(&rq->rb_node));

	as_antic_stop(ad);
	ad->antic_status = ANTIC_OFF;

	/*
	 * This has to be set in order to be correctly updated by
	 * as_find_next_rq
	 */
	ad->last_sector[data_dir] = blk_rq_pos(rq) + blk_rq_sectors(rq);

	if (data_dir == BLK_RW_SYNC) {
		struct io_context *ioc = RQ_IOC(rq);
		/* In case we have to anticipate after this */
		copy_io_context(&ad->io_context, &ioc);
	} else {
		if (ad->io_context) {
			put_io_context(ad->io_context);
			ad->io_context = NULL;
		}

		if (ad->current_write_count != 0)
			ad->current_write_count--;
	}
	ad->ioc_finished = 0;

	ad->next_rq[data_dir] = as_find_next_rq(ad, rq);

	/*
	 * take it off the sort and fifo list, add to dispatch queue
	 */
	as_remove_queued_request(ad->q, rq);
	WARN_ON(RQ_STATE(rq) != AS_RQ_QUEUED);

	elv_dispatch_sort(ad->q, rq);

	RQ_SET_STATE(rq, AS_RQ_DISPATCHED);
	if (RQ_IOC(rq) && RQ_IOC(rq)->aic)
		atomic_inc(&RQ_IOC(rq)->aic->nr_dispatched);
	ad->nr_dispatched++;
}

/*
 * as_dispatch_request selects the best request according to
 * read/write expire, batch expire, etc, and moves it to the dispatch
 * queue. Returns 1 if a request was found, 0 otherwise.
 */
static int as_dispatch_request(struct request_queue *q, int force)
{
	struct as_data *ad = q->elevator->elevator_data;
	const int reads = !list_empty(&ad->fifo_list[BLK_RW_SYNC]);
	const int writes = !list_empty(&ad->fifo_list[BLK_RW_ASYNC]);
	struct request *rq;

	if (unlikely(force)) {
		/*
		 * Forced dispatch, accounting is useless.  Reset
		 * accounting states and dump fifo_lists.  Note that
		 * batch_data_dir is reset to BLK_RW_SYNC to avoid
		 * screwing write batch accounting as write batch
		 * accounting occurs on W->R transition.
		 */
		int dispatched = 0;

		ad->batch_data_dir = BLK_RW_SYNC;
		ad->changed_batch = 0;
		ad->new_batch = 0;

		while (ad->next_rq[BLK_RW_SYNC]) {
			as_move_to_dispatch(ad, ad->next_rq[BLK_RW_SYNC]);
			dispatched++;
		}
		ad->last_check_fifo[BLK_RW_SYNC] = jiffies;

		while (ad->next_rq[BLK_RW_ASYNC]) {
			as_move_to_dispatch(ad, ad->next_rq[BLK_RW_ASYNC]);
			dispatched++;
		}
		ad->last_check_fifo[BLK_RW_ASYNC] = jiffies;

		return dispatched;
	}

	/* Signal that the write batch was uncontended, so we can't time it */
	if (ad->batch_data_dir == BLK_RW_ASYNC && !reads) {
		if (ad->current_write_count == 0 || !writes)
			ad->write_batch_idled = 1;
	}

	if (!(reads || writes)
		|| ad->antic_status == ANTIC_WAIT_REQ
		|| ad->antic_status == ANTIC_WAIT_NEXT
		|| ad->changed_batch)
		return 0;

	if (!(reads && writes && as_batch_expired(ad))) {
		/*
		 * batch is still running or no reads or no writes
		 */
		rq = ad->next_rq[ad->batch_data_dir];

		if (ad->batch_data_dir == BLK_RW_SYNC && ad->antic_expire) {
			if (as_fifo_expired(ad, BLK_RW_SYNC))
				goto fifo_expired;

			if (as_can_anticipate(ad, rq)) {
				as_antic_waitreq(ad);
				return 0;
			}
		}

		if (rq) {
			/* we have a "next request" */
			if (reads && !writes)
				ad->current_batch_expires =
					jiffies + ad->batch_expire[BLK_RW_SYNC];
			goto dispatch_request;
		}
	}

	/*
	 * at this point we are not running a batch. select the appropriate
	 * data direction (read / write)
	 */

	if (reads) {
		BUG_ON(RB_EMPTY_ROOT(&ad->sort_list[BLK_RW_SYNC]));

		if (writes && ad->batch_data_dir == BLK_RW_SYNC)
			/*
			 * Last batch was a read, switch to writes
			 */
			goto dispatch_writes;

		if (ad->batch_data_dir == BLK_RW_ASYNC) {
			WARN_ON(ad->new_batch);
			ad->changed_batch = 1;
		}
		ad->batch_data_dir = BLK_RW_SYNC;
		rq = rq_entry_fifo(ad->fifo_list[BLK_RW_SYNC].next);
		ad->last_check_fifo[ad->batch_data_dir] = jiffies;
		goto dispatch_request;
	}

	/*
	 * the last batch was a read
	 */

	if (writes) {
dispatch_writes:
		BUG_ON(RB_EMPTY_ROOT(&ad->sort_list[BLK_RW_ASYNC]));

		if (ad->batch_data_dir == BLK_RW_SYNC) {
			ad->changed_batch = 1;

			/*
			 * new_batch might be 1 when the queue runs out of
			 * reads. A subsequent submission of a write might
			 * cause a change of batch before the read is finished.
			 */
			ad->new_batch = 0;
		}
		ad->batch_data_dir = BLK_RW_ASYNC;
		ad->current_write_count = ad->write_batch_count;
		ad->write_batch_idled = 0;
		rq = rq_entry_fifo(ad->fifo_list[BLK_RW_ASYNC].next);
		ad->last_check_fifo[BLK_RW_ASYNC] = jiffies;
		goto dispatch_request;
	}

	BUG();
	return 0;

dispatch_request:
	/*
	 * If a request has expired, service it.
	 */

	if (as_fifo_expired(ad, ad->batch_data_dir)) {
fifo_expired:
		rq = rq_entry_fifo(ad->fifo_list[ad->batch_data_dir].next);
	}

	if (ad->changed_batch) {
		WARN_ON(ad->new_batch);

		if (ad->nr_dispatched)
			return 0;

		if (ad->batch_data_dir == BLK_RW_ASYNC)
			ad->current_batch_expires = jiffies +
					ad->batch_expire[BLK_RW_ASYNC];
		else
			ad->new_batch = 1;

		ad->changed_batch = 0;
	}

	/*
	 * rq is the selected appropriate request.
	 */
	as_move_to_dispatch(ad, rq);

	return 1;
}

/*
 * add rq to rbtree and fifo
 */
static void as_add_request(struct request_queue *q, struct request *rq)
{
	struct as_data *ad = q->elevator->elevator_data;
	int data_dir;

	RQ_SET_STATE(rq, AS_RQ_NEW);

	data_dir = rq_is_sync(rq);

	rq->elevator_private = as_get_io_context(q->node);

	if (RQ_IOC(rq)) {
		as_update_iohist(ad, RQ_IOC(rq)->aic, rq);
		atomic_inc(&RQ_IOC(rq)->aic->nr_queued);
	}

	as_add_rq_rb(ad, rq);

	/*
	 * set expire time and add to fifo list
	 */
	rq_set_fifo_time(rq, jiffies + ad->fifo_expire[data_dir]);
	list_add_tail(&rq->queuelist, &ad->fifo_list[data_dir]);

	as_update_rq(ad, rq); /* keep state machine up to date */
	RQ_SET_STATE(rq, AS_RQ_QUEUED);
}

static void as_activate_request(struct request_queue *q, struct request *rq)
{
	WARN_ON(RQ_STATE(rq) != AS_RQ_DISPATCHED);
	RQ_SET_STATE(rq, AS_RQ_REMOVED);
	if (RQ_IOC(rq) && RQ_IOC(rq)->aic)
		atomic_dec(&RQ_IOC(rq)->aic->nr_dispatched);
}

static void as_deactivate_request(struct request_queue *q, struct request *rq)
{
	WARN_ON(RQ_STATE(rq) != AS_RQ_REMOVED);
	RQ_SET_STATE(rq, AS_RQ_DISPATCHED);
	if (RQ_IOC(rq) && RQ_IOC(rq)->aic)
		atomic_inc(&RQ_IOC(rq)->aic->nr_dispatched);
}

/*
 * as_queue_empty tells us if there are requests left in the device. It may
 * not be the case that a driver can get the next request even if the queue
 * is not empty - it is used in the block layer to check for plugging and
 * merging opportunities
 */
static int as_queue_empty(struct request_queue *q)
{
	struct as_data *ad = q->elevator->elevator_data;

	return list_empty(&ad->fifo_list[BLK_RW_ASYNC])
		&& list_empty(&ad->fifo_list[BLK_RW_SYNC]);
}

static int
as_merge(struct request_queue *q, struct request **req, struct bio *bio)
{
	struct as_data *ad = q->elevator->elevator_data;
	sector_t rb_key = bio->bi_sector + bio_sectors(bio);
	struct request *__rq;

	/*
	 * check for front merge
	 */
	__rq = elv_rb_find(&ad->sort_list[bio_data_dir(bio)], rb_key);
	if (__rq && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void as_merged_request(struct request_queue *q, struct request *req,
			      int type)
{
	struct as_data *ad = q->elevator->elevator_data;

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		as_del_rq_rb(ad, req);
		as_add_rq_rb(ad, req);
		/*
		 * Note! At this stage of this and the next function, our next
		 * request may not be optimal - eg the request may have "grown"
		 * behind the disk head. We currently don't bother adjusting.
		 */
	}
}

static void as_merged_requests(struct request_queue *q, struct request *req,
			 	struct request *next)
{
	/*
	 * if next expires before rq, assign its expire time to arq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before(rq_fifo_time(next), rq_fifo_time(req))) {
			list_move(&req->queuelist, &next->queuelist);
			rq_set_fifo_time(req, rq_fifo_time(next));
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	as_remove_queued_request(q, next);
	as_put_io_context(next);

	RQ_SET_STATE(next, AS_RQ_MERGED);
}

/*
 * This is executed in a "deferred" process context, by kblockd. It calls the
 * driver's request_fn so the driver can submit that request.
 *
 * IMPORTANT! This guy will reenter the elevator, so set up all queue global
 * state before calling, and don't rely on any state over calls.
 *
 * FIXME! dispatch queue is not a queue at all!
 */
static void as_work_handler(struct work_struct *work)
{
	struct as_data *ad = container_of(work, struct as_data, antic_work);

	blk_run_queue(ad->q);
}

static int as_may_queue(struct request_queue *q, int rw)
{
	int ret = ELV_MQUEUE_MAY;
	struct as_data *ad = q->elevator->elevator_data;
	struct io_context *ioc;
	if (ad->antic_status == ANTIC_WAIT_REQ ||
			ad->antic_status == ANTIC_WAIT_NEXT) {
		ioc = as_get_io_context(q->node);
		if (ad->io_context == ioc)
			ret = ELV_MQUEUE_MUST;
		put_io_context(ioc);
	}

	return ret;
}

static void as_exit_queue(struct elevator_queue *e)
{
	struct as_data *ad = e->elevator_data;

	del_timer_sync(&ad->antic_timer);
	cancel_work_sync(&ad->antic_work);

	BUG_ON(!list_empty(&ad->fifo_list[BLK_RW_SYNC]));
	BUG_ON(!list_empty(&ad->fifo_list[BLK_RW_ASYNC]));

	put_io_context(ad->io_context);
	kfree(ad);
}

/*
 * initialize elevator private data (as_data).
 */
static void *as_init_queue(struct request_queue *q)
{
	struct as_data *ad;

	ad = kmalloc_node(sizeof(*ad), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!ad)
		return NULL;

	ad->q = q; /* Identify what queue the data belongs to */

	/* anticipatory scheduling helpers */
	ad->antic_timer.function = as_antic_timeout;
	ad->antic_timer.data = (unsigned long)q;
	init_timer(&ad->antic_timer);
	INIT_WORK(&ad->antic_work, as_work_handler);

	INIT_LIST_HEAD(&ad->fifo_list[BLK_RW_SYNC]);
	INIT_LIST_HEAD(&ad->fifo_list[BLK_RW_ASYNC]);
	ad->sort_list[BLK_RW_SYNC] = RB_ROOT;
	ad->sort_list[BLK_RW_ASYNC] = RB_ROOT;
	ad->fifo_expire[BLK_RW_SYNC] = default_read_expire;
	ad->fifo_expire[BLK_RW_ASYNC] = default_write_expire;
	ad->antic_expire = default_antic_expire;
	ad->batch_expire[BLK_RW_SYNC] = default_read_batch_expire;
	ad->batch_expire[BLK_RW_ASYNC] = default_write_batch_expire;

	ad->current_batch_expires = jiffies + ad->batch_expire[BLK_RW_SYNC];
	ad->write_batch_count = ad->batch_expire[BLK_RW_ASYNC] / 10;
	if (ad->write_batch_count < 2)
		ad->write_batch_count = 2;

	return ad;
}

/*
 * sysfs parts below
 */

static ssize_t
as_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
as_var_store(unsigned long *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul(p, &p, 10);
	return count;
}

static ssize_t est_time_show(struct elevator_queue *e, char *page)
{
	struct as_data *ad = e->elevator_data;
	int pos = 0;

	pos += sprintf(page+pos, "%lu %% exit probability\n",
				100*ad->exit_prob/256);
	pos += sprintf(page+pos, "%lu %% probability of exiting without a "
				"cooperating process submitting IO\n",
				100*ad->exit_no_coop/256);
	pos += sprintf(page+pos, "%lu ms new thinktime\n", ad->new_ttime_mean);
	pos += sprintf(page+pos, "%llu sectors new seek distance\n",
				(unsigned long long)ad->new_seek_mean);

	return pos;
}

#define SHOW_FUNCTION(__FUNC, __VAR)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)	\
{								\
	struct as_data *ad = e->elevator_data;			\
	return as_var_show(jiffies_to_msecs((__VAR)), (page));	\
}
SHOW_FUNCTION(as_read_expire_show, ad->fifo_expire[BLK_RW_SYNC]);
SHOW_FUNCTION(as_write_expire_show, ad->fifo_expire[BLK_RW_ASYNC]);
SHOW_FUNCTION(as_antic_expire_show, ad->antic_expire);
SHOW_FUNCTION(as_read_batch_expire_show, ad->batch_expire[BLK_RW_SYNC]);
SHOW_FUNCTION(as_write_batch_expire_show, ad->batch_expire[BLK_RW_ASYNC]);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX)				\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct as_data *ad = e->elevator_data;				\
	int ret = as_var_store(__PTR, (page), count);			\
	if (*(__PTR) < (MIN))						\
		*(__PTR) = (MIN);					\
	else if (*(__PTR) > (MAX))					\
		*(__PTR) = (MAX);					\
	*(__PTR) = msecs_to_jiffies(*(__PTR));				\
	return ret;							\
}
STORE_FUNCTION(as_read_expire_store, &ad->fifo_expire[BLK_RW_SYNC], 0, INT_MAX);
STORE_FUNCTION(as_write_expire_store,
			&ad->fifo_expire[BLK_RW_ASYNC], 0, INT_MAX);
STORE_FUNCTION(as_antic_expire_store, &ad->antic_expire, 0, INT_MAX);
STORE_FUNCTION(as_read_batch_expire_store,
			&ad->batch_expire[BLK_RW_SYNC], 0, INT_MAX);
STORE_FUNCTION(as_write_batch_expire_store,
			&ad->batch_expire[BLK_RW_ASYNC], 0, INT_MAX);
#undef STORE_FUNCTION

#define AS_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, as_##name##_show, as_##name##_store)

static struct elv_fs_entry as_attrs[] = {
	__ATTR_RO(est_time),
	AS_ATTR(read_expire),
	AS_ATTR(write_expire),
	AS_ATTR(antic_expire),
	AS_ATTR(read_batch_expire),
	AS_ATTR(write_batch_expire),
	__ATTR_NULL
};

static struct elevator_type iosched_as = {
	.ops = {
		.elevator_merge_fn = 		as_merge,
		.elevator_merged_fn =		as_merged_request,
		.elevator_merge_req_fn =	as_merged_requests,
		.elevator_dispatch_fn =		as_dispatch_request,
		.elevator_add_req_fn =		as_add_request,
		.elevator_activate_req_fn =	as_activate_request,
		.elevator_deactivate_req_fn = 	as_deactivate_request,
		.elevator_queue_empty_fn =	as_queue_empty,
		.elevator_completed_req_fn =	as_completed_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_may_queue_fn =	as_may_queue,
		.elevator_init_fn =		as_init_queue,
		.elevator_exit_fn =		as_exit_queue,
		.trim =				as_trim,
	},

	.elevator_attrs = as_attrs,
	.elevator_name = "anticipatory",
	.elevator_owner = THIS_MODULE,
};

static int __init as_init(void)
{
	elv_register(&iosched_as);

	return 0;
}

static void __exit as_exit(void)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);
	elv_unregister(&iosched_as);
	ioc_gone = &all_gone;
	/* ioc_gone's update must be visible before reading ioc_count */
	smp_wmb();
	if (elv_ioc_count_read(ioc_count))
		wait_for_completion(&all_gone);
	synchronize_rcu();
}

module_init(as_init);
module_exit(as_exit);

MODULE_AUTHOR("Nick Piggin");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("anticipatory IO scheduler");
