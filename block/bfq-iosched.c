// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Budget Fair Queueing (BFQ) I/O scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 *                    Arianna Avanzini <avanzini@google.com>
 *
 * Copyright (C) 2017 Paolo Valente <paolo.valente@linaro.org>
 *
 * BFQ is a proportional-share I/O scheduler, with some extra
 * low-latency capabilities. BFQ also supports full hierarchical
 * scheduling through cgroups. Next paragraphs provide an introduction
 * on BFQ inner workings. Details on BFQ benefits, usage and
 * limitations can be found in Documentation/block/bfq-iosched.rst.
 *
 * BFQ is a proportional-share storage-I/O scheduling algorithm based
 * on the slice-by-slice service scheme of CFQ. But BFQ assigns
 * budgets, measured in number of sectors, to processes instead of
 * time slices. The device is not granted to the in-service process
 * for a given time slice, but until it has exhausted its assigned
 * budget. This change from the time to the service domain enables BFQ
 * to distribute the device throughput among processes as desired,
 * without any distortion due to throughput fluctuations, or to device
 * internal queueing. BFQ uses an ad hoc internal scheduler, called
 * B-WF2Q+, to schedule processes according to their budgets. More
 * precisely, BFQ schedules queues associated with processes. Each
 * process/queue is assigned a user-configurable weight, and B-WF2Q+
 * guarantees that each queue receives a fraction of the throughput
 * proportional to its weight. Thanks to the accurate policy of
 * B-WF2Q+, BFQ can afford to assign high budgets to I/O-bound
 * processes issuing sequential requests (to boost the throughput),
 * and yet guarantee a low latency to interactive and soft real-time
 * applications.
 *
 * In particular, to provide these low-latency guarantees, BFQ
 * explicitly privileges the I/O of two classes of time-sensitive
 * applications: interactive and soft real-time. In more detail, BFQ
 * behaves this way if the low_latency parameter is set (default
 * configuration). This feature enables BFQ to provide applications in
 * these classes with a very low latency.
 *
 * To implement this feature, BFQ constantly tries to detect whether
 * the I/O requests in a bfq_queue come from an interactive or a soft
 * real-time application. For brevity, in these cases, the queue is
 * said to be interactive or soft real-time. In both cases, BFQ
 * privileges the service of the queue, over that of non-interactive
 * and non-soft-real-time queues. This privileging is performed,
 * mainly, by raising the weight of the queue. So, for brevity, we
 * call just weight-raising periods the time periods during which a
 * queue is privileged, because deemed interactive or soft real-time.
 *
 * The detection of soft real-time queues/applications is described in
 * detail in the comments on the function
 * bfq_bfqq_softrt_next_start. On the other hand, the detection of an
 * interactive queue works as follows: a queue is deemed interactive
 * if it is constantly non empty only for a limited time interval,
 * after which it does become empty. The queue may be deemed
 * interactive again (for a limited time), if it restarts being
 * constantly non empty, provided that this happens only after the
 * queue has remained empty for a given minimum idle time.
 *
 * By default, BFQ computes automatically the above maximum time
 * interval, i.e., the time interval after which a constantly
 * non-empty queue stops being deemed interactive. Since a queue is
 * weight-raised while it is deemed interactive, this maximum time
 * interval happens to coincide with the (maximum) duration of the
 * weight-raising for interactive queues.
 *
 * Finally, BFQ also features additional heuristics for
 * preserving both a low latency and a high throughput on NCQ-capable,
 * rotational or flash-based devices, and to get the job done quickly
 * for applications consisting in many I/O-bound processes.
 *
 * NOTE: if the main or only goal, with a given device, is to achieve
 * the maximum-possible throughput at all times, then do switch off
 * all low-latency heuristics for that device, by setting low_latency
 * to 0.
 *
 * BFQ is described in [1], where also a reference to the initial,
 * more theoretical paper on BFQ can be found. The interested reader
 * can find in the latter paper full details on the main algorithm, as
 * well as formulas of the guarantees and formal proofs of all the
 * properties.  With respect to the version of BFQ presented in these
 * papers, this implementation adds a few more heuristics, such as the
 * ones that guarantee a low latency to interactive and soft real-time
 * applications, and a hierarchical extension based on H-WF2Q+.
 *
 * B-WF2Q+ is based on WF2Q+, which is described in [2], together with
 * H-WF2Q+, while the augmented tree used here to implement B-WF2Q+
 * with O(log N) complexity derives from the one introduced with EEVDF
 * in [3].
 *
 * [1] P. Valente, A. Avanzini, "Evolution of the BFQ Storage I/O
 *     Scheduler", Proceedings of the First Workshop on Mobile System
 *     Technologies (MST-2015), May 2015.
 *     http://algogroup.unimore.it/people/paolo/disk_sched/mst-2015.pdf
 *
 * [2] Jon C.R. Bennett and H. Zhang, "Hierarchical Packet Fair Queueing
 *     Algorithms", IEEE/ACM Transactions on Networking, 5(5):675-689,
 *     Oct 1997.
 *
 * http://www.cs.cmu.edu/~hzhang/papers/TON-97-Oct.ps.gz
 *
 * [3] I. Stoica and H. Abdel-Wahab, "Earliest Eligible Virtual Deadline
 *     First: A Flexible and Accurate Mechanism for Proportional Share
 *     Resource Allocation", technical report.
 *
 * http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/cgroup.h>
#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include <linux/sbitmap.h>
#include <linux/delay.h>
#include <linux/backing-dev.h>

#include <trace/events/block.h>

#include "elevator.h"
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-sched.h"
#include "bfq-iosched.h"
#include "blk-wbt.h"

#define BFQ_BFQQ_FNS(name)						\
void bfq_mark_bfqq_##name(struct bfq_queue *bfqq)			\
{									\
	__set_bit(BFQQF_##name, &(bfqq)->flags);			\
}									\
void bfq_clear_bfqq_##name(struct bfq_queue *bfqq)			\
{									\
	__clear_bit(BFQQF_##name, &(bfqq)->flags);		\
}									\
int bfq_bfqq_##name(const struct bfq_queue *bfqq)			\
{									\
	return test_bit(BFQQF_##name, &(bfqq)->flags);		\
}

BFQ_BFQQ_FNS(just_created);
BFQ_BFQQ_FNS(busy);
BFQ_BFQQ_FNS(wait_request);
BFQ_BFQQ_FNS(non_blocking_wait_rq);
BFQ_BFQQ_FNS(fifo_expire);
BFQ_BFQQ_FNS(has_short_ttime);
BFQ_BFQQ_FNS(sync);
BFQ_BFQQ_FNS(IO_bound);
BFQ_BFQQ_FNS(in_large_burst);
BFQ_BFQQ_FNS(coop);
BFQ_BFQQ_FNS(split_coop);
BFQ_BFQQ_FNS(softrt_update);
#undef BFQ_BFQQ_FNS						\

/* Expiration time of async (0) and sync (1) requests, in ns. */
static const u64 bfq_fifo_expire[2] = { NSEC_PER_SEC / 4, NSEC_PER_SEC / 8 };

/* Maximum backwards seek (magic number lifted from CFQ), in KiB. */
static const int bfq_back_max = 16 * 1024;

/* Penalty of a backwards seek, in number of sectors. */
static const int bfq_back_penalty = 2;

/* Idling period duration, in ns. */
static u64 bfq_slice_idle = NSEC_PER_SEC / 125;

/* Minimum number of assigned budgets for which stats are safe to compute. */
static const int bfq_stats_min_budgets = 194;

/* Default maximum budget values, in sectors and number of requests. */
static const int bfq_default_max_budget = 16 * 1024;

/*
 * When a sync request is dispatched, the queue that contains that
 * request, and all the ancestor entities of that queue, are charged
 * with the number of sectors of the request. In contrast, if the
 * request is async, then the queue and its ancestor entities are
 * charged with the number of sectors of the request, multiplied by
 * the factor below. This throttles the bandwidth for async I/O,
 * w.r.t. to sync I/O, and it is done to counter the tendency of async
 * writes to steal I/O throughput to reads.
 *
 * The current value of this parameter is the result of a tuning with
 * several hardware and software configurations. We tried to find the
 * lowest value for which writes do not cause noticeable problems to
 * reads. In fact, the lower this parameter, the stabler I/O control,
 * in the following respect.  The lower this parameter is, the less
 * the bandwidth enjoyed by a group decreases
 * - when the group does writes, w.r.t. to when it does reads;
 * - when other groups do reads, w.r.t. to when they do writes.
 */
static const int bfq_async_charge_factor = 3;

/* Default timeout values, in jiffies, approximating CFQ defaults. */
const int bfq_timeout = HZ / 8;

/*
 * Time limit for merging (see comments in bfq_setup_cooperator). Set
 * to the slowest value that, in our tests, proved to be effective in
 * removing false positives, while not causing true positives to miss
 * queue merging.
 *
 * As can be deduced from the low time limit below, queue merging, if
 * successful, happens at the very beginning of the I/O of the involved
 * cooperating processes, as a consequence of the arrival of the very
 * first requests from each cooperator.  After that, there is very
 * little chance to find cooperators.
 */
static const unsigned long bfq_merge_time_limit = HZ/10;

static struct kmem_cache *bfq_pool;

/* Below this threshold (in ns), we consider thinktime immediate. */
#define BFQ_MIN_TT		(2 * NSEC_PER_MSEC)

/* hw_tag detection: parallel requests threshold and min samples needed. */
#define BFQ_HW_QUEUE_THRESHOLD	3
#define BFQ_HW_QUEUE_SAMPLES	32

#define BFQQ_SEEK_THR		(sector_t)(8 * 100)
#define BFQQ_SECT_THR_NONROT	(sector_t)(2 * 32)
#define BFQ_RQ_SEEKY(bfqd, last_pos, rq) \
	(get_sdist(last_pos, rq) >			\
	 BFQQ_SEEK_THR &&				\
	 (!blk_queue_nonrot(bfqd->queue) ||		\
	  blk_rq_sectors(rq) < BFQQ_SECT_THR_NONROT))
#define BFQQ_CLOSE_THR		(sector_t)(8 * 1024)
#define BFQQ_SEEKY(bfqq)	(hweight32(bfqq->seek_history) > 19)
/*
 * Sync random I/O is likely to be confused with soft real-time I/O,
 * because it is characterized by limited throughput and apparently
 * isochronous arrival pattern. To avoid false positives, queues
 * containing only random (seeky) I/O are prevented from being tagged
 * as soft real-time.
 */
#define BFQQ_TOTALLY_SEEKY(bfqq)	(bfqq->seek_history == -1)

/* Min number of samples required to perform peak-rate update */
#define BFQ_RATE_MIN_SAMPLES	32
/* Min observation time interval required to perform a peak-rate update (ns) */
#define BFQ_RATE_MIN_INTERVAL	(300*NSEC_PER_MSEC)
/* Target observation time interval for a peak-rate update (ns) */
#define BFQ_RATE_REF_INTERVAL	NSEC_PER_SEC

/*
 * Shift used for peak-rate fixed precision calculations.
 * With
 * - the current shift: 16 positions
 * - the current type used to store rate: u32
 * - the current unit of measure for rate: [sectors/usec], or, more precisely,
 *   [(sectors/usec) / 2^BFQ_RATE_SHIFT] to take into account the shift,
 * the range of rates that can be stored is
 * [1 / 2^BFQ_RATE_SHIFT, 2^(32 - BFQ_RATE_SHIFT)] sectors/usec =
 * [1 / 2^16, 2^16] sectors/usec = [15e-6, 65536] sectors/usec =
 * [15, 65G] sectors/sec
 * Which, assuming a sector size of 512B, corresponds to a range of
 * [7.5K, 33T] B/sec
 */
#define BFQ_RATE_SHIFT		16

/*
 * When configured for computing the duration of the weight-raising
 * for interactive queues automatically (see the comments at the
 * beginning of this file), BFQ does it using the following formula:
 * duration = (ref_rate / r) * ref_wr_duration,
 * where r is the peak rate of the device, and ref_rate and
 * ref_wr_duration are two reference parameters.  In particular,
 * ref_rate is the peak rate of the reference storage device (see
 * below), and ref_wr_duration is about the maximum time needed, with
 * BFQ and while reading two files in parallel, to load typical large
 * applications on the reference device (see the comments on
 * max_service_from_wr below, for more details on how ref_wr_duration
 * is obtained).  In practice, the slower/faster the device at hand
 * is, the more/less it takes to load applications with respect to the
 * reference device.  Accordingly, the longer/shorter BFQ grants
 * weight raising to interactive applications.
 *
 * BFQ uses two different reference pairs (ref_rate, ref_wr_duration),
 * depending on whether the device is rotational or non-rotational.
 *
 * In the following definitions, ref_rate[0] and ref_wr_duration[0]
 * are the reference values for a rotational device, whereas
 * ref_rate[1] and ref_wr_duration[1] are the reference values for a
 * non-rotational device. The reference rates are not the actual peak
 * rates of the devices used as a reference, but slightly lower
 * values. The reason for using slightly lower values is that the
 * peak-rate estimator tends to yield slightly lower values than the
 * actual peak rate (it can yield the actual peak rate only if there
 * is only one process doing I/O, and the process does sequential
 * I/O).
 *
 * The reference peak rates are measured in sectors/usec, left-shifted
 * by BFQ_RATE_SHIFT.
 */
static int ref_rate[2] = {14000, 33000};
/*
 * To improve readability, a conversion function is used to initialize
 * the following array, which entails that the array can be
 * initialized only in a function.
 */
static int ref_wr_duration[2];

/*
 * BFQ uses the above-detailed, time-based weight-raising mechanism to
 * privilege interactive tasks. This mechanism is vulnerable to the
 * following false positives: I/O-bound applications that will go on
 * doing I/O for much longer than the duration of weight
 * raising. These applications have basically no benefit from being
 * weight-raised at the beginning of their I/O. On the opposite end,
 * while being weight-raised, these applications
 * a) unjustly steal throughput to applications that may actually need
 * low latency;
 * b) make BFQ uselessly perform device idling; device idling results
 * in loss of device throughput with most flash-based storage, and may
 * increase latencies when used purposelessly.
 *
 * BFQ tries to reduce these problems, by adopting the following
 * countermeasure. To introduce this countermeasure, we need first to
 * finish explaining how the duration of weight-raising for
 * interactive tasks is computed.
 *
 * For a bfq_queue deemed as interactive, the duration of weight
 * raising is dynamically adjusted, as a function of the estimated
 * peak rate of the device, so as to be equal to the time needed to
 * execute the 'largest' interactive task we benchmarked so far. By
 * largest task, we mean the task for which each involved process has
 * to do more I/O than for any of the other tasks we benchmarked. This
 * reference interactive task is the start-up of LibreOffice Writer,
 * and in this task each process/bfq_queue needs to have at most ~110K
 * sectors transferred.
 *
 * This last piece of information enables BFQ to reduce the actual
 * duration of weight-raising for at least one class of I/O-bound
 * applications: those doing sequential or quasi-sequential I/O. An
 * example is file copy. In fact, once started, the main I/O-bound
 * processes of these applications usually consume the above 110K
 * sectors in much less time than the processes of an application that
 * is starting, because these I/O-bound processes will greedily devote
 * almost all their CPU cycles only to their target,
 * throughput-friendly I/O operations. This is even more true if BFQ
 * happens to be underestimating the device peak rate, and thus
 * overestimating the duration of weight raising. But, according to
 * our measurements, once transferred 110K sectors, these processes
 * have no right to be weight-raised any longer.
 *
 * Basing on the last consideration, BFQ ends weight-raising for a
 * bfq_queue if the latter happens to have received an amount of
 * service at least equal to the following constant. The constant is
 * set to slightly more than 110K, to have a minimum safety margin.
 *
 * This early ending of weight-raising reduces the amount of time
 * during which interactive false positives cause the two problems
 * described at the beginning of these comments.
 */
static const unsigned long max_service_from_wr = 120000;

/*
 * Maximum time between the creation of two queues, for stable merge
 * to be activated (in ms)
 */
static const unsigned long bfq_activation_stable_merging = 600;
/*
 * Minimum time to be waited before evaluating delayed stable merge (in ms)
 */
static const unsigned long bfq_late_stable_merging = 600;

#define RQ_BIC(rq)		((struct bfq_io_cq *)((rq)->elv.priv[0]))
#define RQ_BFQQ(rq)		((rq)->elv.priv[1])

struct bfq_queue *bic_to_bfqq(struct bfq_io_cq *bic, bool is_sync,
			      unsigned int actuator_idx)
{
	if (is_sync)
		return bic->bfqq[1][actuator_idx];

	return bic->bfqq[0][actuator_idx];
}

static void bfq_put_stable_ref(struct bfq_queue *bfqq);

void bic_set_bfqq(struct bfq_io_cq *bic,
		  struct bfq_queue *bfqq,
		  bool is_sync,
		  unsigned int actuator_idx)
{
	struct bfq_queue *old_bfqq = bic->bfqq[is_sync][actuator_idx];

	/*
	 * If bfqq != NULL, then a non-stable queue merge between
	 * bic->bfqq and bfqq is happening here. This causes troubles
	 * in the following case: bic->bfqq has also been scheduled
	 * for a possible stable merge with bic->stable_merge_bfqq,
	 * and bic->stable_merge_bfqq == bfqq happens to
	 * hold. Troubles occur because bfqq may then undergo a split,
	 * thereby becoming eligible for a stable merge. Yet, if
	 * bic->stable_merge_bfqq points exactly to bfqq, then bfqq
	 * would be stably merged with itself. To avoid this anomaly,
	 * we cancel the stable merge if
	 * bic->stable_merge_bfqq == bfqq.
	 */
	struct bfq_iocq_bfqq_data *bfqq_data = &bic->bfqq_data[actuator_idx];

	/* Clear bic pointer if bfqq is detached from this bic */
	if (old_bfqq && old_bfqq->bic == bic)
		old_bfqq->bic = NULL;

	if (is_sync)
		bic->bfqq[1][actuator_idx] = bfqq;
	else
		bic->bfqq[0][actuator_idx] = bfqq;

	if (bfqq && bfqq_data->stable_merge_bfqq == bfqq) {
		/*
		 * Actually, these same instructions are executed also
		 * in bfq_setup_cooperator, in case of abort or actual
		 * execution of a stable merge. We could avoid
		 * repeating these instructions there too, but if we
		 * did so, we would nest even more complexity in this
		 * function.
		 */
		bfq_put_stable_ref(bfqq_data->stable_merge_bfqq);

		bfqq_data->stable_merge_bfqq = NULL;
	}
}

struct bfq_data *bic_to_bfqd(struct bfq_io_cq *bic)
{
	return bic->icq.q->elevator->elevator_data;
}

/**
 * icq_to_bic - convert iocontext queue structure to bfq_io_cq.
 * @icq: the iocontext queue.
 */
static struct bfq_io_cq *icq_to_bic(struct io_cq *icq)
{
	/* bic->icq is the first member, %NULL will convert to %NULL */
	return container_of(icq, struct bfq_io_cq, icq);
}

/**
 * bfq_bic_lookup - search into @ioc a bic associated to @bfqd.
 * @q: the request queue.
 */
static struct bfq_io_cq *bfq_bic_lookup(struct request_queue *q)
{
	struct bfq_io_cq *icq;
	unsigned long flags;

	if (!current->io_context)
		return NULL;

	spin_lock_irqsave(&q->queue_lock, flags);
	icq = icq_to_bic(ioc_lookup_icq(q));
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return icq;
}

/*
 * Scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing.
 */
void bfq_schedule_dispatch(struct bfq_data *bfqd)
{
	lockdep_assert_held(&bfqd->lock);

	if (bfqd->queued != 0) {
		bfq_log(bfqd, "schedule dispatch");
		blk_mq_run_hw_queues(bfqd->queue, true);
	}
}

#define bfq_class_idle(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_IDLE)

#define bfq_sample_valid(samples)	((samples) > 80)

/*
 * Lifted from AS - choose which of rq1 and rq2 that is best served now.
 * We choose the request that is closer to the head right now.  Distance
 * behind the head is penalized and only allowed to a certain extent.
 */
static struct request *bfq_choose_req(struct bfq_data *bfqd,
				      struct request *rq1,
				      struct request *rq2,
				      sector_t last)
{
	sector_t s1, s2, d1 = 0, d2 = 0;
	unsigned long back_max;
#define BFQ_RQ1_WRAP	0x01 /* request 1 wraps */
#define BFQ_RQ2_WRAP	0x02 /* request 2 wraps */
	unsigned int wrap = 0; /* bit mask: requests behind the disk head? */

	if (!rq1 || rq1 == rq2)
		return rq2;
	if (!rq2)
		return rq1;

	if (rq_is_sync(rq1) && !rq_is_sync(rq2))
		return rq1;
	else if (rq_is_sync(rq2) && !rq_is_sync(rq1))
		return rq2;
	if ((rq1->cmd_flags & REQ_META) && !(rq2->cmd_flags & REQ_META))
		return rq1;
	else if ((rq2->cmd_flags & REQ_META) && !(rq1->cmd_flags & REQ_META))
		return rq2;

	s1 = blk_rq_pos(rq1);
	s2 = blk_rq_pos(rq2);

	/*
	 * By definition, 1KiB is 2 sectors.
	 */
	back_max = bfqd->bfq_back_max * 2;

	/*
	 * Strict one way elevator _except_ in the case where we allow
	 * short backward seeks which are biased as twice the cost of a
	 * similar forward seek.
	 */
	if (s1 >= last)
		d1 = s1 - last;
	else if (s1 + back_max >= last)
		d1 = (last - s1) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ1_WRAP;

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2 + back_max >= last)
		d2 = (last - s2) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ2_WRAP;

	/* Found required data */

	/*
	 * By doing switch() on the bit mask "wrap" we avoid having to
	 * check two variables for all permutations: --> faster!
	 */
	switch (wrap) {
	case 0: /* common case for CFQ: rq1 and rq2 not wrapped */
		if (d1 < d2)
			return rq1;
		else if (d2 < d1)
			return rq2;

		if (s1 >= s2)
			return rq1;
		else
			return rq2;

	case BFQ_RQ2_WRAP:
		return rq1;
	case BFQ_RQ1_WRAP:
		return rq2;
	case BFQ_RQ1_WRAP|BFQ_RQ2_WRAP: /* both rqs wrapped */
	default:
		/*
		 * Since both rqs are wrapped,
		 * start with the one that's further behind head
		 * (--> only *one* back seek required),
		 * since back seek takes more time than forward.
		 */
		if (s1 <= s2)
			return rq1;
		else
			return rq2;
	}
}

#define BFQ_LIMIT_INLINE_DEPTH 16

#ifdef CONFIG_BFQ_GROUP_IOSCHED
static bool bfqq_request_over_limit(struct bfq_data *bfqd,
				    struct bfq_io_cq *bic, blk_opf_t opf,
				    unsigned int act_idx, int limit)
{
	struct bfq_entity *inline_entities[BFQ_LIMIT_INLINE_DEPTH];
	struct bfq_entity **entities = inline_entities;
	int alloc_depth = BFQ_LIMIT_INLINE_DEPTH;
	struct bfq_sched_data *sched_data;
	struct bfq_entity *entity;
	struct bfq_queue *bfqq;
	unsigned long wsum;
	bool ret = false;
	int depth;
	int level;

retry:
	spin_lock_irq(&bfqd->lock);
	bfqq = bic_to_bfqq(bic, op_is_sync(opf), act_idx);
	if (!bfqq)
		goto out;

	entity = &bfqq->entity;
	if (!entity->on_st_or_in_serv)
		goto out;

	/* +1 for bfqq entity, root cgroup not included */
	depth = bfqg_to_blkg(bfqq_group(bfqq))->blkcg->css.cgroup->level + 1;
	if (depth > alloc_depth) {
		spin_unlock_irq(&bfqd->lock);
		if (entities != inline_entities)
			kfree(entities);
		entities = kmalloc_array(depth, sizeof(*entities), GFP_NOIO);
		if (!entities)
			return false;
		alloc_depth = depth;
		goto retry;
	}

	sched_data = entity->sched_data;
	/* Gather our ancestors as we need to traverse them in reverse order */
	level = 0;
	for_each_entity(entity) {
		/*
		 * If at some level entity is not even active, allow request
		 * queueing so that BFQ knows there's work to do and activate
		 * entities.
		 */
		if (!entity->on_st_or_in_serv)
			goto out;
		/* Uh, more parents than cgroup subsystem thinks? */
		if (WARN_ON_ONCE(level >= depth))
			break;
		entities[level++] = entity;
	}
	WARN_ON_ONCE(level != depth);
	for (level--; level >= 0; level--) {
		entity = entities[level];
		if (level > 0) {
			wsum = bfq_entity_service_tree(entity)->wsum;
		} else {
			int i;
			/*
			 * For bfqq itself we take into account service trees
			 * of all higher priority classes and multiply their
			 * weights so that low prio queue from higher class
			 * gets more requests than high prio queue from lower
			 * class.
			 */
			wsum = 0;
			for (i = 0; i <= bfqq->ioprio_class - 1; i++) {
				wsum = wsum * IOPRIO_BE_NR +
					sched_data->service_tree[i].wsum;
			}
		}
		if (!wsum)
			continue;
		limit = DIV_ROUND_CLOSEST(limit * entity->weight, wsum);
		if (entity->allocated >= limit) {
			bfq_log_bfqq(bfqq->bfqd, bfqq,
				"too many requests: allocated %d limit %d level %d",
				entity->allocated, limit, level);
			ret = true;
			break;
		}
	}
out:
	spin_unlock_irq(&bfqd->lock);
	if (entities != inline_entities)
		kfree(entities);
	return ret;
}
#else
static bool bfqq_request_over_limit(struct bfq_data *bfqd,
				    struct bfq_io_cq *bic, blk_opf_t opf,
				    unsigned int act_idx, int limit)
{
	return false;
}
#endif

/*
 * Async I/O can easily starve sync I/O (both sync reads and sync
 * writes), by consuming all tags. Similarly, storms of sync writes,
 * such as those that sync(2) may trigger, can starve sync reads.
 * Limit depths of async I/O and sync writes so as to counter both
 * problems.
 *
 * Also if a bfq queue or its parent cgroup consume more tags than would be
 * appropriate for their weight, we trim the available tag depth to 1. This
 * avoids a situation where one cgroup can starve another cgroup from tags and
 * thus block service differentiation among cgroups. Note that because the
 * queue / cgroup already has many requests allocated and queued, this does not
 * significantly affect service guarantees coming from the BFQ scheduling
 * algorithm.
 */
static void bfq_limit_depth(blk_opf_t opf, struct blk_mq_alloc_data *data)
{
	struct bfq_data *bfqd = data->q->elevator->elevator_data;
	struct bfq_io_cq *bic = bfq_bic_lookup(data->q);
	int depth;
	unsigned limit = data->q->nr_requests;
	unsigned int act_idx;

	/* Sync reads have full depth available */
	if (op_is_sync(opf) && !op_is_write(opf)) {
		depth = 0;
	} else {
		depth = bfqd->word_depths[!!bfqd->wr_busy_queues][op_is_sync(opf)];
		limit = (limit * depth) >> bfqd->full_depth_shift;
	}

	for (act_idx = 0; bic && act_idx < bfqd->num_actuators; act_idx++) {
		/* Fast path to check if bfqq is already allocated. */
		if (!bic_to_bfqq(bic, op_is_sync(opf), act_idx))
			continue;

		/*
		 * Does queue (or any parent entity) exceed number of
		 * requests that should be available to it? Heavily
		 * limit depth so that it cannot consume more
		 * available requests and thus starve other entities.
		 */
		if (bfqq_request_over_limit(bfqd, bic, opf, act_idx, limit)) {
			depth = 1;
			break;
		}
	}
	bfq_log(bfqd, "[%s] wr_busy %d sync %d depth %u",
		__func__, bfqd->wr_busy_queues, op_is_sync(opf), depth);
	if (depth)
		data->shallow_depth = depth;
}

static struct bfq_queue *
bfq_rq_pos_tree_lookup(struct bfq_data *bfqd, struct rb_root *root,
		     sector_t sector, struct rb_node **ret_parent,
		     struct rb_node ***rb_link)
{
	struct rb_node **p, *parent;
	struct bfq_queue *bfqq = NULL;

	parent = NULL;
	p = &root->rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		bfqq = rb_entry(parent, struct bfq_queue, pos_node);

		/*
		 * Sort strictly based on sector. Smallest to the left,
		 * largest to the right.
		 */
		if (sector > blk_rq_pos(bfqq->next_rq))
			n = &(*p)->rb_right;
		else if (sector < blk_rq_pos(bfqq->next_rq))
			n = &(*p)->rb_left;
		else
			break;
		p = n;
		bfqq = NULL;
	}

	*ret_parent = parent;
	if (rb_link)
		*rb_link = p;

	bfq_log(bfqd, "rq_pos_tree_lookup %llu: returning %d",
		(unsigned long long)sector,
		bfqq ? bfqq->pid : 0);

	return bfqq;
}

static bool bfq_too_late_for_merging(struct bfq_queue *bfqq)
{
	return bfqq->service_from_backlogged > 0 &&
		time_is_before_jiffies(bfqq->first_IO_time +
				       bfq_merge_time_limit);
}

/*
 * The following function is not marked as __cold because it is
 * actually cold, but for the same performance goal described in the
 * comments on the likely() at the beginning of
 * bfq_setup_cooperator(). Unexpectedly, to reach an even lower
 * execution time for the case where this function is not invoked, we
 * had to add an unlikely() in each involved if().
 */
void __cold
bfq_pos_tree_add_move(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct rb_node **p, *parent;
	struct bfq_queue *__bfqq;

	if (bfqq->pos_root) {
		rb_erase(&bfqq->pos_node, bfqq->pos_root);
		bfqq->pos_root = NULL;
	}

	/* oom_bfqq does not participate in queue merging */
	if (bfqq == &bfqd->oom_bfqq)
		return;

	/*
	 * bfqq cannot be merged any longer (see comments in
	 * bfq_setup_cooperator): no point in adding bfqq into the
	 * position tree.
	 */
	if (bfq_too_late_for_merging(bfqq))
		return;

	if (bfq_class_idle(bfqq))
		return;
	if (!bfqq->next_rq)
		return;

	bfqq->pos_root = &bfqq_group(bfqq)->rq_pos_tree;
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, bfqq->pos_root,
			blk_rq_pos(bfqq->next_rq), &parent, &p);
	if (!__bfqq) {
		rb_link_node(&bfqq->pos_node, parent, p);
		rb_insert_color(&bfqq->pos_node, bfqq->pos_root);
	} else
		bfqq->pos_root = NULL;
}

/*
 * The following function returns false either if every active queue
 * must receive the same share of the throughput (symmetric scenario),
 * or, as a special case, if bfqq must receive a share of the
 * throughput lower than or equal to the share that every other active
 * queue must receive.  If bfqq does sync I/O, then these are the only
 * two cases where bfqq happens to be guaranteed its share of the
 * throughput even if I/O dispatching is not plugged when bfqq remains
 * temporarily empty (for more details, see the comments in the
 * function bfq_better_to_idle()). For this reason, the return value
 * of this function is used to check whether I/O-dispatch plugging can
 * be avoided.
 *
 * The above first case (symmetric scenario) occurs when:
 * 1) all active queues have the same weight,
 * 2) all active queues belong to the same I/O-priority class,
 * 3) all active groups at the same level in the groups tree have the same
 *    weight,
 * 4) all active groups at the same level in the groups tree have the same
 *    number of children.
 *
 * Unfortunately, keeping the necessary state for evaluating exactly
 * the last two symmetry sub-conditions above would be quite complex
 * and time consuming. Therefore this function evaluates, instead,
 * only the following stronger three sub-conditions, for which it is
 * much easier to maintain the needed state:
 * 1) all active queues have the same weight,
 * 2) all active queues belong to the same I/O-priority class,
 * 3) there is at most one active group.
 * In particular, the last condition is always true if hierarchical
 * support or the cgroups interface are not enabled, thus no state
 * needs to be maintained in this case.
 */
static bool bfq_asymmetric_scenario(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq)
{
	bool smallest_weight = bfqq &&
		bfqq->weight_counter &&
		bfqq->weight_counter ==
		container_of(
			rb_first_cached(&bfqd->queue_weights_tree),
			struct bfq_weight_counter,
			weights_node);

	/*
	 * For queue weights to differ, queue_weights_tree must contain
	 * at least two nodes.
	 */
	bool varied_queue_weights = !smallest_weight &&
		!RB_EMPTY_ROOT(&bfqd->queue_weights_tree.rb_root) &&
		(bfqd->queue_weights_tree.rb_root.rb_node->rb_left ||
		 bfqd->queue_weights_tree.rb_root.rb_node->rb_right);

	bool multiple_classes_busy =
		(bfqd->busy_queues[0] && bfqd->busy_queues[1]) ||
		(bfqd->busy_queues[0] && bfqd->busy_queues[2]) ||
		(bfqd->busy_queues[1] && bfqd->busy_queues[2]);

	return varied_queue_weights || multiple_classes_busy
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	       || bfqd->num_groups_with_pending_reqs > 1
#endif
		;
}

/*
 * If the weight-counter tree passed as input contains no counter for
 * the weight of the input queue, then add that counter; otherwise just
 * increment the existing counter.
 *
 * Note that weight-counter trees contain few nodes in mostly symmetric
 * scenarios. For example, if all queues have the same weight, then the
 * weight-counter tree for the queues may contain at most one node.
 * This holds even if low_latency is on, because weight-raised queues
 * are not inserted in the tree.
 * In most scenarios, the rate at which nodes are created/destroyed
 * should be low too.
 */
void bfq_weights_tree_add(struct bfq_queue *bfqq)
{
	struct rb_root_cached *root = &bfqq->bfqd->queue_weights_tree;
	struct bfq_entity *entity = &bfqq->entity;
	struct rb_node **new = &(root->rb_root.rb_node), *parent = NULL;
	bool leftmost = true;

	/*
	 * Do not insert if the queue is already associated with a
	 * counter, which happens if:
	 *   1) a request arrival has caused the queue to become both
	 *      non-weight-raised, and hence change its weight, and
	 *      backlogged; in this respect, each of the two events
	 *      causes an invocation of this function,
	 *   2) this is the invocation of this function caused by the
	 *      second event. This second invocation is actually useless,
	 *      and we handle this fact by exiting immediately. More
	 *      efficient or clearer solutions might possibly be adopted.
	 */
	if (bfqq->weight_counter)
		return;

	while (*new) {
		struct bfq_weight_counter *__counter = container_of(*new,
						struct bfq_weight_counter,
						weights_node);
		parent = *new;

		if (entity->weight == __counter->weight) {
			bfqq->weight_counter = __counter;
			goto inc_counter;
		}
		if (entity->weight < __counter->weight)
			new = &((*new)->rb_left);
		else {
			new = &((*new)->rb_right);
			leftmost = false;
		}
	}

	bfqq->weight_counter = kzalloc(sizeof(struct bfq_weight_counter),
				       GFP_ATOMIC);

	/*
	 * In the unlucky event of an allocation failure, we just
	 * exit. This will cause the weight of queue to not be
	 * considered in bfq_asymmetric_scenario, which, in its turn,
	 * causes the scenario to be deemed wrongly symmetric in case
	 * bfqq's weight would have been the only weight making the
	 * scenario asymmetric.  On the bright side, no unbalance will
	 * however occur when bfqq becomes inactive again (the
	 * invocation of this function is triggered by an activation
	 * of queue).  In fact, bfq_weights_tree_remove does nothing
	 * if !bfqq->weight_counter.
	 */
	if (unlikely(!bfqq->weight_counter))
		return;

	bfqq->weight_counter->weight = entity->weight;
	rb_link_node(&bfqq->weight_counter->weights_node, parent, new);
	rb_insert_color_cached(&bfqq->weight_counter->weights_node, root,
				leftmost);

inc_counter:
	bfqq->weight_counter->num_active++;
	bfqq->ref++;
}

/*
 * Decrement the weight counter associated with the queue, and, if the
 * counter reaches 0, remove the counter from the tree.
 * See the comments to the function bfq_weights_tree_add() for considerations
 * about overhead.
 */
void bfq_weights_tree_remove(struct bfq_queue *bfqq)
{
	struct rb_root_cached *root;

	if (!bfqq->weight_counter)
		return;

	root = &bfqq->bfqd->queue_weights_tree;
	bfqq->weight_counter->num_active--;
	if (bfqq->weight_counter->num_active > 0)
		goto reset_entity_pointer;

	rb_erase_cached(&bfqq->weight_counter->weights_node, root);
	kfree(bfqq->weight_counter);

reset_entity_pointer:
	bfqq->weight_counter = NULL;
	bfq_put_queue(bfqq);
}

/*
 * Return expired entry, or NULL to just start from scratch in rbtree.
 */
static struct request *bfq_check_fifo(struct bfq_queue *bfqq,
				      struct request *last)
{
	struct request *rq;

	if (bfq_bfqq_fifo_expire(bfqq))
		return NULL;

	bfq_mark_bfqq_fifo_expire(bfqq);

	rq = rq_entry_fifo(bfqq->fifo.next);

	if (rq == last || blk_time_get_ns() < rq->fifo_time)
		return NULL;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "check_fifo: returned %p", rq);
	return rq;
}

static struct request *bfq_find_next_rq(struct bfq_data *bfqd,
					struct bfq_queue *bfqq,
					struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next, *prev = NULL;

	/* Follow expired path, else get first next available. */
	next = bfq_check_fifo(bfqq, last);
	if (next)
		return next;

	if (rbprev)
		prev = rb_entry_rq(rbprev);

	if (rbnext)
		next = rb_entry_rq(rbnext);
	else {
		rbnext = rb_first(&bfqq->sort_list);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return bfq_choose_req(bfqd, next, prev, blk_rq_pos(last));
}

/* see the definition of bfq_async_charge_factor for details */
static unsigned long bfq_serv_to_charge(struct request *rq,
					struct bfq_queue *bfqq)
{
	if (bfq_bfqq_sync(bfqq) || bfqq->wr_coeff > 1 ||
	    bfq_asymmetric_scenario(bfqq->bfqd, bfqq))
		return blk_rq_sectors(rq);

	return blk_rq_sectors(rq) * bfq_async_charge_factor;
}

/**
 * bfq_updated_next_req - update the queue after a new next_rq selection.
 * @bfqd: the device data the queue belongs to.
 * @bfqq: the queue to update.
 *
 * If the first request of a queue changes we make sure that the queue
 * has enough budget to serve at least its first request (if the
 * request has grown).  We do this because if the queue has not enough
 * budget for its first request, it has to go through two dispatch
 * rounds to actually get it dispatched.
 */
static void bfq_updated_next_req(struct bfq_data *bfqd,
				 struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct request *next_rq = bfqq->next_rq;
	unsigned long new_budget;

	if (!next_rq)
		return;

	if (bfqq == bfqd->in_service_queue)
		/*
		 * In order not to break guarantees, budgets cannot be
		 * changed after an entity has been selected.
		 */
		return;

	new_budget = max_t(unsigned long,
			   max_t(unsigned long, bfqq->max_budget,
				 bfq_serv_to_charge(next_rq, bfqq)),
			   entity->service);
	if (entity->budget != new_budget) {
		entity->budget = new_budget;
		bfq_log_bfqq(bfqd, bfqq, "updated next rq: new budget %lu",
					 new_budget);
		bfq_requeue_bfqq(bfqd, bfqq, false);
	}
}

static unsigned int bfq_wr_duration(struct bfq_data *bfqd)
{
	u64 dur;

	dur = bfqd->rate_dur_prod;
	do_div(dur, bfqd->peak_rate);

	/*
	 * Limit duration between 3 and 25 seconds. The upper limit
	 * has been conservatively set after the following worst case:
	 * on a QEMU/KVM virtual machine
	 * - running in a slow PC
	 * - with a virtual disk stacked on a slow low-end 5400rpm HDD
	 * - serving a heavy I/O workload, such as the sequential reading
	 *   of several files
	 * mplayer took 23 seconds to start, if constantly weight-raised.
	 *
	 * As for higher values than that accommodating the above bad
	 * scenario, tests show that higher values would often yield
	 * the opposite of the desired result, i.e., would worsen
	 * responsiveness by allowing non-interactive applications to
	 * preserve weight raising for too long.
	 *
	 * On the other end, lower values than 3 seconds make it
	 * difficult for most interactive tasks to complete their jobs
	 * before weight-raising finishes.
	 */
	return clamp_val(dur, msecs_to_jiffies(3000), msecs_to_jiffies(25000));
}

/* switch back from soft real-time to interactive weight raising */
static void switch_back_to_interactive_wr(struct bfq_queue *bfqq,
					  struct bfq_data *bfqd)
{
	bfqq->wr_coeff = bfqd->bfq_wr_coeff;
	bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
	bfqq->last_wr_start_finish = bfqq->wr_start_at_switch_to_srt;
}

static void
bfq_bfqq_resume_state(struct bfq_queue *bfqq, struct bfq_data *bfqd,
		      struct bfq_io_cq *bic, bool bfq_already_existing)
{
	unsigned int old_wr_coeff = 1;
	bool busy = bfq_already_existing && bfq_bfqq_busy(bfqq);
	unsigned int a_idx = bfqq->actuator_idx;
	struct bfq_iocq_bfqq_data *bfqq_data = &bic->bfqq_data[a_idx];

	if (bfqq_data->saved_has_short_ttime)
		bfq_mark_bfqq_has_short_ttime(bfqq);
	else
		bfq_clear_bfqq_has_short_ttime(bfqq);

	if (bfqq_data->saved_IO_bound)
		bfq_mark_bfqq_IO_bound(bfqq);
	else
		bfq_clear_bfqq_IO_bound(bfqq);

	bfqq->last_serv_time_ns = bfqq_data->saved_last_serv_time_ns;
	bfqq->inject_limit = bfqq_data->saved_inject_limit;
	bfqq->decrease_time_jif = bfqq_data->saved_decrease_time_jif;

	bfqq->entity.new_weight = bfqq_data->saved_weight;
	bfqq->ttime = bfqq_data->saved_ttime;
	bfqq->io_start_time = bfqq_data->saved_io_start_time;
	bfqq->tot_idle_time = bfqq_data->saved_tot_idle_time;
	/*
	 * Restore weight coefficient only if low_latency is on
	 */
	if (bfqd->low_latency) {
		old_wr_coeff = bfqq->wr_coeff;
		bfqq->wr_coeff = bfqq_data->saved_wr_coeff;
	}
	bfqq->service_from_wr = bfqq_data->saved_service_from_wr;
	bfqq->wr_start_at_switch_to_srt =
		bfqq_data->saved_wr_start_at_switch_to_srt;
	bfqq->last_wr_start_finish = bfqq_data->saved_last_wr_start_finish;
	bfqq->wr_cur_max_time = bfqq_data->saved_wr_cur_max_time;

	if (bfqq->wr_coeff > 1 && (bfq_bfqq_in_large_burst(bfqq) ||
	    time_is_before_jiffies(bfqq->last_wr_start_finish +
				   bfqq->wr_cur_max_time))) {
		if (bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time &&
		    !bfq_bfqq_in_large_burst(bfqq) &&
		    time_is_after_eq_jiffies(bfqq->wr_start_at_switch_to_srt +
					     bfq_wr_duration(bfqd))) {
			switch_back_to_interactive_wr(bfqq, bfqd);
		} else {
			bfqq->wr_coeff = 1;
			bfq_log_bfqq(bfqq->bfqd, bfqq,
				     "resume state: switching off wr");
		}
	}

	/* make sure weight will be updated, however we got here */
	bfqq->entity.prio_changed = 1;

	if (likely(!busy))
		return;

	if (old_wr_coeff == 1 && bfqq->wr_coeff > 1)
		bfqd->wr_busy_queues++;
	else if (old_wr_coeff > 1 && bfqq->wr_coeff == 1)
		bfqd->wr_busy_queues--;
}

static int bfqq_process_refs(struct bfq_queue *bfqq)
{
	return bfqq->ref - bfqq->entity.allocated -
		bfqq->entity.on_st_or_in_serv -
		(bfqq->weight_counter != NULL) - bfqq->stable_ref;
}

/* Empty burst list and add just bfqq (see comments on bfq_handle_burst) */
static void bfq_reset_burst_list(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_queue *item;
	struct hlist_node *n;

	hlist_for_each_entry_safe(item, n, &bfqd->burst_list, burst_list_node)
		hlist_del_init(&item->burst_list_node);

	/*
	 * Start the creation of a new burst list only if there is no
	 * active queue. See comments on the conditional invocation of
	 * bfq_handle_burst().
	 */
	if (bfq_tot_busy_queues(bfqd) == 0) {
		hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
		bfqd->burst_size = 1;
	} else
		bfqd->burst_size = 0;

	bfqd->burst_parent_entity = bfqq->entity.parent;
}

/* Add bfqq to the list of queues in current burst (see bfq_handle_burst) */
static void bfq_add_to_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/* Increment burst size to take into account also bfqq */
	bfqd->burst_size++;

	if (bfqd->burst_size == bfqd->bfq_large_burst_thresh) {
		struct bfq_queue *pos, *bfqq_item;
		struct hlist_node *n;

		/*
		 * Enough queues have been activated shortly after each
		 * other to consider this burst as large.
		 */
		bfqd->large_burst = true;

		/*
		 * We can now mark all queues in the burst list as
		 * belonging to a large burst.
		 */
		hlist_for_each_entry(bfqq_item, &bfqd->burst_list,
				     burst_list_node)
			bfq_mark_bfqq_in_large_burst(bfqq_item);
		bfq_mark_bfqq_in_large_burst(bfqq);

		/*
		 * From now on, and until the current burst finishes, any
		 * new queue being activated shortly after the last queue
		 * was inserted in the burst can be immediately marked as
		 * belonging to a large burst. So the burst list is not
		 * needed any more. Remove it.
		 */
		hlist_for_each_entry_safe(pos, n, &bfqd->burst_list,
					  burst_list_node)
			hlist_del_init(&pos->burst_list_node);
	} else /*
		* Burst not yet large: add bfqq to the burst list. Do
		* not increment the ref counter for bfqq, because bfqq
		* is removed from the burst list before freeing bfqq
		* in put_queue.
		*/
		hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
}

/*
 * If many queues belonging to the same group happen to be created
 * shortly after each other, then the processes associated with these
 * queues have typically a common goal. In particular, bursts of queue
 * creations are usually caused by services or applications that spawn
 * many parallel threads/processes. Examples are systemd during boot,
 * or git grep. To help these processes get their job done as soon as
 * possible, it is usually better to not grant either weight-raising
 * or device idling to their queues, unless these queues must be
 * protected from the I/O flowing through other active queues.
 *
 * In this comment we describe, firstly, the reasons why this fact
 * holds, and, secondly, the next function, which implements the main
 * steps needed to properly mark these queues so that they can then be
 * treated in a different way.
 *
 * The above services or applications benefit mostly from a high
 * throughput: the quicker the requests of the activated queues are
 * cumulatively served, the sooner the target job of these queues gets
 * completed. As a consequence, weight-raising any of these queues,
 * which also implies idling the device for it, is almost always
 * counterproductive, unless there are other active queues to isolate
 * these new queues from. If there no other active queues, then
 * weight-raising these new queues just lowers throughput in most
 * cases.
 *
 * On the other hand, a burst of queue creations may be caused also by
 * the start of an application that does not consist of a lot of
 * parallel I/O-bound threads. In fact, with a complex application,
 * several short processes may need to be executed to start-up the
 * application. In this respect, to start an application as quickly as
 * possible, the best thing to do is in any case to privilege the I/O
 * related to the application with respect to all other
 * I/O. Therefore, the best strategy to start as quickly as possible
 * an application that causes a burst of queue creations is to
 * weight-raise all the queues created during the burst. This is the
 * exact opposite of the best strategy for the other type of bursts.
 *
 * In the end, to take the best action for each of the two cases, the
 * two types of bursts need to be distinguished. Fortunately, this
 * seems relatively easy, by looking at the sizes of the bursts. In
 * particular, we found a threshold such that only bursts with a
 * larger size than that threshold are apparently caused by
 * services or commands such as systemd or git grep. For brevity,
 * hereafter we call just 'large' these bursts. BFQ *does not*
 * weight-raise queues whose creation occurs in a large burst. In
 * addition, for each of these queues BFQ performs or does not perform
 * idling depending on which choice boosts the throughput more. The
 * exact choice depends on the device and request pattern at
 * hand.
 *
 * Unfortunately, false positives may occur while an interactive task
 * is starting (e.g., an application is being started). The
 * consequence is that the queues associated with the task do not
 * enjoy weight raising as expected. Fortunately these false positives
 * are very rare. They typically occur if some service happens to
 * start doing I/O exactly when the interactive task starts.
 *
 * Turning back to the next function, it is invoked only if there are
 * no active queues (apart from active queues that would belong to the
 * same, possible burst bfqq would belong to), and it implements all
 * the steps needed to detect the occurrence of a large burst and to
 * properly mark all the queues belonging to it (so that they can then
 * be treated in a different way). This goal is achieved by
 * maintaining a "burst list" that holds, temporarily, the queues that
 * belong to the burst in progress. The list is then used to mark
 * these queues as belonging to a large burst if the burst does become
 * large. The main steps are the following.
 *
 * . when the very first queue is created, the queue is inserted into the
 *   list (as it could be the first queue in a possible burst)
 *
 * . if the current burst has not yet become large, and a queue Q that does
 *   not yet belong to the burst is activated shortly after the last time
 *   at which a new queue entered the burst list, then the function appends
 *   Q to the burst list
 *
 * . if, as a consequence of the previous step, the burst size reaches
 *   the large-burst threshold, then
 *
 *     . all the queues in the burst list are marked as belonging to a
 *       large burst
 *
 *     . the burst list is deleted; in fact, the burst list already served
 *       its purpose (keeping temporarily track of the queues in a burst,
 *       so as to be able to mark them as belonging to a large burst in the
 *       previous sub-step), and now is not needed any more
 *
 *     . the device enters a large-burst mode
 *
 * . if a queue Q that does not belong to the burst is created while
 *   the device is in large-burst mode and shortly after the last time
 *   at which a queue either entered the burst list or was marked as
 *   belonging to the current large burst, then Q is immediately marked
 *   as belonging to a large burst.
 *
 * . if a queue Q that does not belong to the burst is created a while
 *   later, i.e., not shortly after, than the last time at which a queue
 *   either entered the burst list or was marked as belonging to the
 *   current large burst, then the current burst is deemed as finished and:
 *
 *        . the large-burst mode is reset if set
 *
 *        . the burst list is emptied
 *
 *        . Q is inserted in the burst list, as Q may be the first queue
 *          in a possible new burst (then the burst list contains just Q
 *          after this step).
 */
static void bfq_handle_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/*
	 * If bfqq is already in the burst list or is part of a large
	 * burst, or finally has just been split, then there is
	 * nothing else to do.
	 */
	if (!hlist_unhashed(&bfqq->burst_list_node) ||
	    bfq_bfqq_in_large_burst(bfqq) ||
	    time_is_after_eq_jiffies(bfqq->split_time +
				     msecs_to_jiffies(10)))
		return;

	/*
	 * If bfqq's creation happens late enough, or bfqq belongs to
	 * a different group than the burst group, then the current
	 * burst is finished, and related data structures must be
	 * reset.
	 *
	 * In this respect, consider the special case where bfqq is
	 * the very first queue created after BFQ is selected for this
	 * device. In this case, last_ins_in_burst and
	 * burst_parent_entity are not yet significant when we get
	 * here. But it is easy to verify that, whether or not the
	 * following condition is true, bfqq will end up being
	 * inserted into the burst list. In particular the list will
	 * happen to contain only bfqq. And this is exactly what has
	 * to happen, as bfqq may be the first queue of the first
	 * burst.
	 */
	if (time_is_before_jiffies(bfqd->last_ins_in_burst +
	    bfqd->bfq_burst_interval) ||
	    bfqq->entity.parent != bfqd->burst_parent_entity) {
		bfqd->large_burst = false;
		bfq_reset_burst_list(bfqd, bfqq);
		goto end;
	}

	/*
	 * If we get here, then bfqq is being activated shortly after the
	 * last queue. So, if the current burst is also large, we can mark
	 * bfqq as belonging to this large burst immediately.
	 */
	if (bfqd->large_burst) {
		bfq_mark_bfqq_in_large_burst(bfqq);
		goto end;
	}

	/*
	 * If we get here, then a large-burst state has not yet been
	 * reached, but bfqq is being activated shortly after the last
	 * queue. Then we add bfqq to the burst.
	 */
	bfq_add_to_burst(bfqd, bfqq);
end:
	/*
	 * At this point, bfqq either has been added to the current
	 * burst or has caused the current burst to terminate and a
	 * possible new burst to start. In particular, in the second
	 * case, bfqq has become the first queue in the possible new
	 * burst.  In both cases last_ins_in_burst needs to be moved
	 * forward.
	 */
	bfqd->last_ins_in_burst = jiffies;
}

static int bfq_bfqq_budget_left(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	return entity->budget - entity->service;
}

/*
 * If enough samples have been computed, return the current max budget
 * stored in bfqd, which is dynamically updated according to the
 * estimated disk peak rate; otherwise return the default max budget
 */
static int bfq_max_budget(struct bfq_data *bfqd)
{
	if (bfqd->budgets_assigned < bfq_stats_min_budgets)
		return bfq_default_max_budget;
	else
		return bfqd->bfq_max_budget;
}

/*
 * Return min budget, which is a fraction of the current or default
 * max budget (trying with 1/32)
 */
static int bfq_min_budget(struct bfq_data *bfqd)
{
	if (bfqd->budgets_assigned < bfq_stats_min_budgets)
		return bfq_default_max_budget / 32;
	else
		return bfqd->bfq_max_budget / 32;
}

/*
 * The next function, invoked after the input queue bfqq switches from
 * idle to busy, updates the budget of bfqq. The function also tells
 * whether the in-service queue should be expired, by returning
 * true. The purpose of expiring the in-service queue is to give bfqq
 * the chance to possibly preempt the in-service queue, and the reason
 * for preempting the in-service queue is to achieve one of the two
 * goals below.
 *
 * 1. Guarantee to bfqq its reserved bandwidth even if bfqq has
 * expired because it has remained idle. In particular, bfqq may have
 * expired for one of the following two reasons:
 *
 * - BFQQE_NO_MORE_REQUESTS bfqq did not enjoy any device idling
 *   and did not make it to issue a new request before its last
 *   request was served;
 *
 * - BFQQE_TOO_IDLE bfqq did enjoy device idling, but did not issue
 *   a new request before the expiration of the idling-time.
 *
 * Even if bfqq has expired for one of the above reasons, the process
 * associated with the queue may be however issuing requests greedily,
 * and thus be sensitive to the bandwidth it receives (bfqq may have
 * remained idle for other reasons: CPU high load, bfqq not enjoying
 * idling, I/O throttling somewhere in the path from the process to
 * the I/O scheduler, ...). But if, after every expiration for one of
 * the above two reasons, bfqq has to wait for the service of at least
 * one full budget of another queue before being served again, then
 * bfqq is likely to get a much lower bandwidth or resource time than
 * its reserved ones. To address this issue, two countermeasures need
 * to be taken.
 *
 * First, the budget and the timestamps of bfqq need to be updated in
 * a special way on bfqq reactivation: they need to be updated as if
 * bfqq did not remain idle and did not expire. In fact, if they are
 * computed as if bfqq expired and remained idle until reactivation,
 * then the process associated with bfqq is treated as if, instead of
 * being greedy, it stopped issuing requests when bfqq remained idle,
 * and restarts issuing requests only on this reactivation. In other
 * words, the scheduler does not help the process recover the "service
 * hole" between bfqq expiration and reactivation. As a consequence,
 * the process receives a lower bandwidth than its reserved one. In
 * contrast, to recover this hole, the budget must be updated as if
 * bfqq was not expired at all before this reactivation, i.e., it must
 * be set to the value of the remaining budget when bfqq was
 * expired. Along the same line, timestamps need to be assigned the
 * value they had the last time bfqq was selected for service, i.e.,
 * before last expiration. Thus timestamps need to be back-shifted
 * with respect to their normal computation (see [1] for more details
 * on this tricky aspect).
 *
 * Secondly, to allow the process to recover the hole, the in-service
 * queue must be expired too, to give bfqq the chance to preempt it
 * immediately. In fact, if bfqq has to wait for a full budget of the
 * in-service queue to be completed, then it may become impossible to
 * let the process recover the hole, even if the back-shifted
 * timestamps of bfqq are lower than those of the in-service queue. If
 * this happens for most or all of the holes, then the process may not
 * receive its reserved bandwidth. In this respect, it is worth noting
 * that, being the service of outstanding requests unpreemptible, a
 * little fraction of the holes may however be unrecoverable, thereby
 * causing a little loss of bandwidth.
 *
 * The last important point is detecting whether bfqq does need this
 * bandwidth recovery. In this respect, the next function deems the
 * process associated with bfqq greedy, and thus allows it to recover
 * the hole, if: 1) the process is waiting for the arrival of a new
 * request (which implies that bfqq expired for one of the above two
 * reasons), and 2) such a request has arrived soon. The first
 * condition is controlled through the flag non_blocking_wait_rq,
 * while the second through the flag arrived_in_time. If both
 * conditions hold, then the function computes the budget in the
 * above-described special way, and signals that the in-service queue
 * should be expired. Timestamp back-shifting is done later in
 * __bfq_activate_entity.
 *
 * 2. Reduce latency. Even if timestamps are not backshifted to let
 * the process associated with bfqq recover a service hole, bfqq may
 * however happen to have, after being (re)activated, a lower finish
 * timestamp than the in-service queue.	 That is, the next budget of
 * bfqq may have to be completed before the one of the in-service
 * queue. If this is the case, then preempting the in-service queue
 * allows this goal to be achieved, apart from the unpreemptible,
 * outstanding requests mentioned above.
 *
 * Unfortunately, regardless of which of the above two goals one wants
 * to achieve, service trees need first to be updated to know whether
 * the in-service queue must be preempted. To have service trees
 * correctly updated, the in-service queue must be expired and
 * rescheduled, and bfqq must be scheduled too. This is one of the
 * most costly operations (in future versions, the scheduling
 * mechanism may be re-designed in such a way to make it possible to
 * know whether preemption is needed without needing to update service
 * trees). In addition, queue preemptions almost always cause random
 * I/O, which may in turn cause loss of throughput. Finally, there may
 * even be no in-service queue when the next function is invoked (so,
 * no queue to compare timestamps with). Because of these facts, the
 * next function adopts the following simple scheme to avoid costly
 * operations, too frequent preemptions and too many dependencies on
 * the state of the scheduler: it requests the expiration of the
 * in-service queue (unconditionally) only for queues that need to
 * recover a hole. Then it delegates to other parts of the code the
 * responsibility of handling the above case 2.
 */
static bool bfq_bfqq_update_budg_for_activation(struct bfq_data *bfqd,
						struct bfq_queue *bfqq,
						bool arrived_in_time)
{
	struct bfq_entity *entity = &bfqq->entity;

	/*
	 * In the next compound condition, we check also whether there
	 * is some budget left, because otherwise there is no point in
	 * trying to go on serving bfqq with this same budget: bfqq
	 * would be expired immediately after being selected for
	 * service. This would only cause useless overhead.
	 */
	if (bfq_bfqq_non_blocking_wait_rq(bfqq) && arrived_in_time &&
	    bfq_bfqq_budget_left(bfqq) > 0) {
		/*
		 * We do not clear the flag non_blocking_wait_rq here, as
		 * the latter is used in bfq_activate_bfqq to signal
		 * that timestamps need to be back-shifted (and is
		 * cleared right after).
		 */

		/*
		 * In next assignment we rely on that either
		 * entity->service or entity->budget are not updated
		 * on expiration if bfqq is empty (see
		 * __bfq_bfqq_recalc_budget). Thus both quantities
		 * remain unchanged after such an expiration, and the
		 * following statement therefore assigns to
		 * entity->budget the remaining budget on such an
		 * expiration.
		 */
		entity->budget = min_t(unsigned long,
				       bfq_bfqq_budget_left(bfqq),
				       bfqq->max_budget);

		/*
		 * At this point, we have used entity->service to get
		 * the budget left (needed for updating
		 * entity->budget). Thus we finally can, and have to,
		 * reset entity->service. The latter must be reset
		 * because bfqq would otherwise be charged again for
		 * the service it has received during its previous
		 * service slot(s).
		 */
		entity->service = 0;

		return true;
	}

	/*
	 * We can finally complete expiration, by setting service to 0.
	 */
	entity->service = 0;
	entity->budget = max_t(unsigned long, bfqq->max_budget,
			       bfq_serv_to_charge(bfqq->next_rq, bfqq));
	bfq_clear_bfqq_non_blocking_wait_rq(bfqq);
	return false;
}

/*
 * Return the farthest past time instant according to jiffies
 * macros.
 */
static unsigned long bfq_smallest_from_now(void)
{
	return jiffies - MAX_JIFFY_OFFSET;
}

static void bfq_update_bfqq_wr_on_rq_arrival(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq,
					     unsigned int old_wr_coeff,
					     bool wr_or_deserves_wr,
					     bool interactive,
					     bool in_burst,
					     bool soft_rt)
{
	if (old_wr_coeff == 1 && wr_or_deserves_wr) {
		/* start a weight-raising period */
		if (interactive) {
			bfqq->service_from_wr = 0;
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
		} else {
			/*
			 * No interactive weight raising in progress
			 * here: assign minus infinity to
			 * wr_start_at_switch_to_srt, to make sure
			 * that, at the end of the soft-real-time
			 * weight raising periods that is starting
			 * now, no interactive weight-raising period
			 * may be wrongly considered as still in
			 * progress (and thus actually started by
			 * mistake).
			 */
			bfqq->wr_start_at_switch_to_srt =
				bfq_smallest_from_now();
			bfqq->wr_coeff = bfqd->bfq_wr_coeff *
				BFQ_SOFTRT_WEIGHT_FACTOR;
			bfqq->wr_cur_max_time =
				bfqd->bfq_wr_rt_max_time;
		}

		/*
		 * If needed, further reduce budget to make sure it is
		 * close to bfqq's backlog, so as to reduce the
		 * scheduling-error component due to a too large
		 * budget. Do not care about throughput consequences,
		 * but only about latency. Finally, do not assign a
		 * too small budget either, to avoid increasing
		 * latency by causing too frequent expirations.
		 */
		bfqq->entity.budget = min_t(unsigned long,
					    bfqq->entity.budget,
					    2 * bfq_min_budget(bfqd));
	} else if (old_wr_coeff > 1) {
		if (interactive) { /* update wr coeff and duration */
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
		} else if (in_burst)
			bfqq->wr_coeff = 1;
		else if (soft_rt) {
			/*
			 * The application is now or still meeting the
			 * requirements for being deemed soft rt.  We
			 * can then correctly and safely (re)charge
			 * the weight-raising duration for the
			 * application with the weight-raising
			 * duration for soft rt applications.
			 *
			 * In particular, doing this recharge now, i.e.,
			 * before the weight-raising period for the
			 * application finishes, reduces the probability
			 * of the following negative scenario:
			 * 1) the weight of a soft rt application is
			 *    raised at startup (as for any newly
			 *    created application),
			 * 2) since the application is not interactive,
			 *    at a certain time weight-raising is
			 *    stopped for the application,
			 * 3) at that time the application happens to
			 *    still have pending requests, and hence
			 *    is destined to not have a chance to be
			 *    deemed soft rt before these requests are
			 *    completed (see the comments to the
			 *    function bfq_bfqq_softrt_next_start()
			 *    for details on soft rt detection),
			 * 4) these pending requests experience a high
			 *    latency because the application is not
			 *    weight-raised while they are pending.
			 */
			if (bfqq->wr_cur_max_time !=
				bfqd->bfq_wr_rt_max_time) {
				bfqq->wr_start_at_switch_to_srt =
					bfqq->last_wr_start_finish;

				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
				bfqq->wr_coeff = bfqd->bfq_wr_coeff *
					BFQ_SOFTRT_WEIGHT_FACTOR;
			}
			bfqq->last_wr_start_finish = jiffies;
		}
	}
}

static bool bfq_bfqq_idle_for_long_time(struct bfq_data *bfqd,
					struct bfq_queue *bfqq)
{
	return bfqq->dispatched == 0 &&
		time_is_before_jiffies(
			bfqq->budget_timeout +
			bfqd->bfq_wr_min_idle_time);
}


/*
 * Return true if bfqq is in a higher priority class, or has a higher
 * weight than the in-service queue.
 */
static bool bfq_bfqq_higher_class_or_weight(struct bfq_queue *bfqq,
					    struct bfq_queue *in_serv_bfqq)
{
	int bfqq_weight, in_serv_weight;

	if (bfqq->ioprio_class < in_serv_bfqq->ioprio_class)
		return true;

	if (in_serv_bfqq->entity.parent == bfqq->entity.parent) {
		bfqq_weight = bfqq->entity.weight;
		in_serv_weight = in_serv_bfqq->entity.weight;
	} else {
		if (bfqq->entity.parent)
			bfqq_weight = bfqq->entity.parent->weight;
		else
			bfqq_weight = bfqq->entity.weight;
		if (in_serv_bfqq->entity.parent)
			in_serv_weight = in_serv_bfqq->entity.parent->weight;
		else
			in_serv_weight = in_serv_bfqq->entity.weight;
	}

	return bfqq_weight > in_serv_weight;
}

/*
 * Get the index of the actuator that will serve bio.
 */
static unsigned int bfq_actuator_index(struct bfq_data *bfqd, struct bio *bio)
{
	unsigned int i;
	sector_t end;

	/* no search needed if one or zero ranges present */
	if (bfqd->num_actuators == 1)
		return 0;

	/* bio_end_sector(bio) gives the sector after the last one */
	end = bio_end_sector(bio) - 1;

	for (i = 0; i < bfqd->num_actuators; i++) {
		if (end >= bfqd->sector[i] &&
		    end < bfqd->sector[i] + bfqd->nr_sectors[i])
			return i;
	}

	WARN_ONCE(true,
		  "bfq_actuator_index: bio sector out of ranges: end=%llu\n",
		  end);
	return 0;
}

static bool bfq_better_to_idle(struct bfq_queue *bfqq);

static void bfq_bfqq_handle_idle_busy_switch(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq,
					     int old_wr_coeff,
					     struct request *rq,
					     bool *interactive)
{
	bool soft_rt, in_burst,	wr_or_deserves_wr,
		bfqq_wants_to_preempt,
		idle_for_long_time = bfq_bfqq_idle_for_long_time(bfqd, bfqq),
		/*
		 * See the comments on
		 * bfq_bfqq_update_budg_for_activation for
		 * details on the usage of the next variable.
		 */
		arrived_in_time =  blk_time_get_ns() <=
			bfqq->ttime.last_end_request +
			bfqd->bfq_slice_idle * 3;
	unsigned int act_idx = bfq_actuator_index(bfqd, rq->bio);
	bool bfqq_non_merged_or_stably_merged =
		bfqq->bic || RQ_BIC(rq)->bfqq_data[act_idx].stably_merged;

	/*
	 * bfqq deserves to be weight-raised if:
	 * - it is sync,
	 * - it does not belong to a large burst,
	 * - it has been idle for enough time or is soft real-time,
	 * - is linked to a bfq_io_cq (it is not shared in any sense),
	 * - has a default weight (otherwise we assume the user wanted
	 *   to control its weight explicitly)
	 */
	in_burst = bfq_bfqq_in_large_burst(bfqq);
	soft_rt = bfqd->bfq_wr_max_softrt_rate > 0 &&
		!BFQQ_TOTALLY_SEEKY(bfqq) &&
		!in_burst &&
		time_is_before_jiffies(bfqq->soft_rt_next_start) &&
		bfqq->dispatched == 0 &&
		bfqq->entity.new_weight == 40;
	*interactive = !in_burst && idle_for_long_time &&
		bfqq->entity.new_weight == 40;
	/*
	 * Merged bfq_queues are kept out of weight-raising
	 * (low-latency) mechanisms. The reason is that these queues
	 * are usually created for non-interactive and
	 * non-soft-real-time tasks. Yet this is not the case for
	 * stably-merged queues. These queues are merged just because
	 * they are created shortly after each other. So they may
	 * easily serve the I/O of an interactive or soft-real time
	 * application, if the application happens to spawn multiple
	 * processes. So let also stably-merged queued enjoy weight
	 * raising.
	 */
	wr_or_deserves_wr = bfqd->low_latency &&
		(bfqq->wr_coeff > 1 ||
		 (bfq_bfqq_sync(bfqq) && bfqq_non_merged_or_stably_merged &&
		  (*interactive || soft_rt)));

	/*
	 * Using the last flag, update budget and check whether bfqq
	 * may want to preempt the in-service queue.
	 */
	bfqq_wants_to_preempt =
		bfq_bfqq_update_budg_for_activation(bfqd, bfqq,
						    arrived_in_time);

	/*
	 * If bfqq happened to be activated in a burst, but has been
	 * idle for much more than an interactive queue, then we
	 * assume that, in the overall I/O initiated in the burst, the
	 * I/O associated with bfqq is finished. So bfqq does not need
	 * to be treated as a queue belonging to a burst
	 * anymore. Accordingly, we reset bfqq's in_large_burst flag
	 * if set, and remove bfqq from the burst list if it's
	 * there. We do not decrement burst_size, because the fact
	 * that bfqq does not need to belong to the burst list any
	 * more does not invalidate the fact that bfqq was created in
	 * a burst.
	 */
	if (likely(!bfq_bfqq_just_created(bfqq)) &&
	    idle_for_long_time &&
	    time_is_before_jiffies(
		    bfqq->budget_timeout +
		    msecs_to_jiffies(10000))) {
		hlist_del_init(&bfqq->burst_list_node);
		bfq_clear_bfqq_in_large_burst(bfqq);
	}

	bfq_clear_bfqq_just_created(bfqq);

	if (bfqd->low_latency) {
		if (unlikely(time_is_after_jiffies(bfqq->split_time)))
			/* wraparound */
			bfqq->split_time =
				jiffies - bfqd->bfq_wr_min_idle_time - 1;

		if (time_is_before_jiffies(bfqq->split_time +
					   bfqd->bfq_wr_min_idle_time)) {
			bfq_update_bfqq_wr_on_rq_arrival(bfqd, bfqq,
							 old_wr_coeff,
							 wr_or_deserves_wr,
							 *interactive,
							 in_burst,
							 soft_rt);

			if (old_wr_coeff != bfqq->wr_coeff)
				bfqq->entity.prio_changed = 1;
		}
	}

	bfqq->last_idle_bklogged = jiffies;
	bfqq->service_from_backlogged = 0;
	bfq_clear_bfqq_softrt_update(bfqq);

	bfq_add_bfqq_busy(bfqq);

	/*
	 * Expire in-service queue if preemption may be needed for
	 * guarantees or throughput. As for guarantees, we care
	 * explicitly about two cases. The first is that bfqq has to
	 * recover a service hole, as explained in the comments on
	 * bfq_bfqq_update_budg_for_activation(), i.e., that
	 * bfqq_wants_to_preempt is true. However, if bfqq does not
	 * carry time-critical I/O, then bfqq's bandwidth is less
	 * important than that of queues that carry time-critical I/O.
	 * So, as a further constraint, we consider this case only if
	 * bfqq is at least as weight-raised, i.e., at least as time
	 * critical, as the in-service queue.
	 *
	 * The second case is that bfqq is in a higher priority class,
	 * or has a higher weight than the in-service queue. If this
	 * condition does not hold, we don't care because, even if
	 * bfqq does not start to be served immediately, the resulting
	 * delay for bfqq's I/O is however lower or much lower than
	 * the ideal completion time to be guaranteed to bfqq's I/O.
	 *
	 * In both cases, preemption is needed only if, according to
	 * the timestamps of both bfqq and of the in-service queue,
	 * bfqq actually is the next queue to serve. So, to reduce
	 * useless preemptions, the return value of
	 * next_queue_may_preempt() is considered in the next compound
	 * condition too. Yet next_queue_may_preempt() just checks a
	 * simple, necessary condition for bfqq to be the next queue
	 * to serve. In fact, to evaluate a sufficient condition, the
	 * timestamps of the in-service queue would need to be
	 * updated, and this operation is quite costly (see the
	 * comments on bfq_bfqq_update_budg_for_activation()).
	 *
	 * As for throughput, we ask bfq_better_to_idle() whether we
	 * still need to plug I/O dispatching. If bfq_better_to_idle()
	 * says no, then plugging is not needed any longer, either to
	 * boost throughput or to perserve service guarantees. Then
	 * the best option is to stop plugging I/O, as not doing so
	 * would certainly lower throughput. We may end up in this
	 * case if: (1) upon a dispatch attempt, we detected that it
	 * was better to plug I/O dispatch, and to wait for a new
	 * request to arrive for the currently in-service queue, but
	 * (2) this switch of bfqq to busy changes the scenario.
	 */
	if (bfqd->in_service_queue &&
	    ((bfqq_wants_to_preempt &&
	      bfqq->wr_coeff >= bfqd->in_service_queue->wr_coeff) ||
	     bfq_bfqq_higher_class_or_weight(bfqq, bfqd->in_service_queue) ||
	     !bfq_better_to_idle(bfqd->in_service_queue)) &&
	    next_queue_may_preempt(bfqd))
		bfq_bfqq_expire(bfqd, bfqd->in_service_queue,
				false, BFQQE_PREEMPTED);
}

static void bfq_reset_inject_limit(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq)
{
	/* invalidate baseline total service time */
	bfqq->last_serv_time_ns = 0;

	/*
	 * Reset pointer in case we are waiting for
	 * some request completion.
	 */
	bfqd->waited_rq = NULL;

	/*
	 * If bfqq has a short think time, then start by setting the
	 * inject limit to 0 prudentially, because the service time of
	 * an injected I/O request may be higher than the think time
	 * of bfqq, and therefore, if one request was injected when
	 * bfqq remains empty, this injected request might delay the
	 * service of the next I/O request for bfqq significantly. In
	 * case bfqq can actually tolerate some injection, then the
	 * adaptive update will however raise the limit soon. This
	 * lucky circumstance holds exactly because bfqq has a short
	 * think time, and thus, after remaining empty, is likely to
	 * get new I/O enqueued---and then completed---before being
	 * expired. This is the very pattern that gives the
	 * limit-update algorithm the chance to measure the effect of
	 * injection on request service times, and then to update the
	 * limit accordingly.
	 *
	 * However, in the following special case, the inject limit is
	 * left to 1 even if the think time is short: bfqq's I/O is
	 * synchronized with that of some other queue, i.e., bfqq may
	 * receive new I/O only after the I/O of the other queue is
	 * completed. Keeping the inject limit to 1 allows the
	 * blocking I/O to be served while bfqq is in service. And
	 * this is very convenient both for bfqq and for overall
	 * throughput, as explained in detail in the comments in
	 * bfq_update_has_short_ttime().
	 *
	 * On the opposite end, if bfqq has a long think time, then
	 * start directly by 1, because:
	 * a) on the bright side, keeping at most one request in
	 * service in the drive is unlikely to cause any harm to the
	 * latency of bfqq's requests, as the service time of a single
	 * request is likely to be lower than the think time of bfqq;
	 * b) on the downside, after becoming empty, bfqq is likely to
	 * expire before getting its next request. With this request
	 * arrival pattern, it is very hard to sample total service
	 * times and update the inject limit accordingly (see comments
	 * on bfq_update_inject_limit()). So the limit is likely to be
	 * never, or at least seldom, updated.  As a consequence, by
	 * setting the limit to 1, we avoid that no injection ever
	 * occurs with bfqq. On the downside, this proactive step
	 * further reduces chances to actually compute the baseline
	 * total service time. Thus it reduces chances to execute the
	 * limit-update algorithm and possibly raise the limit to more
	 * than 1.
	 */
	if (bfq_bfqq_has_short_ttime(bfqq))
		bfqq->inject_limit = 0;
	else
		bfqq->inject_limit = 1;

	bfqq->decrease_time_jif = jiffies;
}

static void bfq_update_io_intensity(struct bfq_queue *bfqq, u64 now_ns)
{
	u64 tot_io_time = now_ns - bfqq->io_start_time;

	if (RB_EMPTY_ROOT(&bfqq->sort_list) && bfqq->dispatched == 0)
		bfqq->tot_idle_time +=
			now_ns - bfqq->ttime.last_end_request;

	if (unlikely(bfq_bfqq_just_created(bfqq)))
		return;

	/*
	 * Must be busy for at least about 80% of the time to be
	 * considered I/O bound.
	 */
	if (bfqq->tot_idle_time * 5 > tot_io_time)
		bfq_clear_bfqq_IO_bound(bfqq);
	else
		bfq_mark_bfqq_IO_bound(bfqq);

	/*
	 * Keep an observation window of at most 200 ms in the past
	 * from now.
	 */
	if (tot_io_time > 200 * NSEC_PER_MSEC) {
		bfqq->io_start_time = now_ns - (tot_io_time>>1);
		bfqq->tot_idle_time >>= 1;
	}
}

/*
 * Detect whether bfqq's I/O seems synchronized with that of some
 * other queue, i.e., whether bfqq, after remaining empty, happens to
 * receive new I/O only right after some I/O request of the other
 * queue has been completed. We call waker queue the other queue, and
 * we assume, for simplicity, that bfqq may have at most one waker
 * queue.
 *
 * A remarkable throughput boost can be reached by unconditionally
 * injecting the I/O of the waker queue, every time a new
 * bfq_dispatch_request happens to be invoked while I/O is being
 * plugged for bfqq.  In addition to boosting throughput, this
 * unblocks bfqq's I/O, thereby improving bandwidth and latency for
 * bfqq. Note that these same results may be achieved with the general
 * injection mechanism, but less effectively. For details on this
 * aspect, see the comments on the choice of the queue for injection
 * in bfq_select_queue().
 *
 * Turning back to the detection of a waker queue, a queue Q is deemed as a
 * waker queue for bfqq if, for three consecutive times, bfqq happens to become
 * non empty right after a request of Q has been completed within given
 * timeout. In this respect, even if bfqq is empty, we do not check for a waker
 * if it still has some in-flight I/O. In fact, in this case bfqq is actually
 * still being served by the drive, and may receive new I/O on the completion
 * of some of the in-flight requests. In particular, on the first time, Q is
 * tentatively set as a candidate waker queue, while on the third consecutive
 * time that Q is detected, the field waker_bfqq is set to Q, to confirm that Q
 * is a waker queue for bfqq. These detection steps are performed only if bfqq
 * has a long think time, so as to make it more likely that bfqq's I/O is
 * actually being blocked by a synchronization. This last filter, plus the
 * above three-times requirement and time limit for detection, make false
 * positives less likely.
 *
 * NOTE
 *
 * The sooner a waker queue is detected, the sooner throughput can be
 * boosted by injecting I/O from the waker queue. Fortunately,
 * detection is likely to be actually fast, for the following
 * reasons. While blocked by synchronization, bfqq has a long think
 * time. This implies that bfqq's inject limit is at least equal to 1
 * (see the comments in bfq_update_inject_limit()). So, thanks to
 * injection, the waker queue is likely to be served during the very
 * first I/O-plugging time interval for bfqq. This triggers the first
 * step of the detection mechanism. Thanks again to injection, the
 * candidate waker queue is then likely to be confirmed no later than
 * during the next I/O-plugging interval for bfqq.
 *
 * ISSUE
 *
 * On queue merging all waker information is lost.
 */
static void bfq_check_waker(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			    u64 now_ns)
{
	char waker_name[MAX_BFQQ_NAME_LENGTH];

	if (!bfqd->last_completed_rq_bfqq ||
	    bfqd->last_completed_rq_bfqq == bfqq ||
	    bfq_bfqq_has_short_ttime(bfqq) ||
	    now_ns - bfqd->last_completion >= 4 * NSEC_PER_MSEC ||
	    bfqd->last_completed_rq_bfqq == &bfqd->oom_bfqq ||
	    bfqq == &bfqd->oom_bfqq)
		return;

	/*
	 * We reset waker detection logic also if too much time has passed
 	 * since the first detection. If wakeups are rare, pointless idling
	 * doesn't hurt throughput that much. The condition below makes sure
	 * we do not uselessly idle blocking waker in more than 1/64 cases.
	 */
	if (bfqd->last_completed_rq_bfqq !=
	    bfqq->tentative_waker_bfqq ||
	    now_ns > bfqq->waker_detection_started +
					128 * (u64)bfqd->bfq_slice_idle) {
		/*
		 * First synchronization detected with a
		 * candidate waker queue, or with a different
		 * candidate waker queue from the current one.
		 */
		bfqq->tentative_waker_bfqq =
			bfqd->last_completed_rq_bfqq;
		bfqq->num_waker_detections = 1;
		bfqq->waker_detection_started = now_ns;
		bfq_bfqq_name(bfqq->tentative_waker_bfqq, waker_name,
			      MAX_BFQQ_NAME_LENGTH);
		bfq_log_bfqq(bfqd, bfqq, "set tentative waker %s", waker_name);
	} else /* Same tentative waker queue detected again */
		bfqq->num_waker_detections++;

	if (bfqq->num_waker_detections == 3) {
		bfqq->waker_bfqq = bfqd->last_completed_rq_bfqq;
		bfqq->tentative_waker_bfqq = NULL;
		bfq_bfqq_name(bfqq->waker_bfqq, waker_name,
			      MAX_BFQQ_NAME_LENGTH);
		bfq_log_bfqq(bfqd, bfqq, "set waker %s", waker_name);

		/*
		 * If the waker queue disappears, then
		 * bfqq->waker_bfqq must be reset. To
		 * this goal, we maintain in each
		 * waker queue a list, woken_list, of
		 * all the queues that reference the
		 * waker queue through their
		 * waker_bfqq pointer. When the waker
		 * queue exits, the waker_bfqq pointer
		 * of all the queues in the woken_list
		 * is reset.
		 *
		 * In addition, if bfqq is already in
		 * the woken_list of a waker queue,
		 * then, before being inserted into
		 * the woken_list of a new waker
		 * queue, bfqq must be removed from
		 * the woken_list of the old waker
		 * queue.
		 */
		if (!hlist_unhashed(&bfqq->woken_list_node))
			hlist_del_init(&bfqq->woken_list_node);
		hlist_add_head(&bfqq->woken_list_node,
			       &bfqd->last_completed_rq_bfqq->woken_list);
	}
}

static void bfq_add_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;
	unsigned int old_wr_coeff = bfqq->wr_coeff;
	bool interactive = false;
	u64 now_ns = blk_time_get_ns();

	bfq_log_bfqq(bfqd, bfqq, "add_request %d", rq_is_sync(rq));
	bfqq->queued[rq_is_sync(rq)]++;
	/*
	 * Updating of 'bfqd->queued' is protected by 'bfqd->lock', however, it
	 * may be read without holding the lock in bfq_has_work().
	 */
	WRITE_ONCE(bfqd->queued, bfqd->queued + 1);

	if (bfq_bfqq_sync(bfqq) && RQ_BIC(rq)->requests <= 1) {
		bfq_check_waker(bfqd, bfqq, now_ns);

		/*
		 * Periodically reset inject limit, to make sure that
		 * the latter eventually drops in case workload
		 * changes, see step (3) in the comments on
		 * bfq_update_inject_limit().
		 */
		if (time_is_before_eq_jiffies(bfqq->decrease_time_jif +
					     msecs_to_jiffies(1000)))
			bfq_reset_inject_limit(bfqd, bfqq);

		/*
		 * The following conditions must hold to setup a new
		 * sampling of total service time, and then a new
		 * update of the inject limit:
		 * - bfqq is in service, because the total service
		 *   time is evaluated only for the I/O requests of
		 *   the queues in service;
		 * - this is the right occasion to compute or to
		 *   lower the baseline total service time, because
		 *   there are actually no requests in the drive,
		 *   or
		 *   the baseline total service time is available, and
		 *   this is the right occasion to compute the other
		 *   quantity needed to update the inject limit, i.e.,
		 *   the total service time caused by the amount of
		 *   injection allowed by the current value of the
		 *   limit. It is the right occasion because injection
		 *   has actually been performed during the service
		 *   hole, and there are still in-flight requests,
		 *   which are very likely to be exactly the injected
		 *   requests, or part of them;
		 * - the minimum interval for sampling the total
		 *   service time and updating the inject limit has
		 *   elapsed.
		 */
		if (bfqq == bfqd->in_service_queue &&
		    (bfqd->tot_rq_in_driver == 0 ||
		     (bfqq->last_serv_time_ns > 0 &&
		      bfqd->rqs_injected && bfqd->tot_rq_in_driver > 0)) &&
		    time_is_before_eq_jiffies(bfqq->decrease_time_jif +
					      msecs_to_jiffies(10))) {
			bfqd->last_empty_occupied_ns = blk_time_get_ns();
			/*
			 * Start the state machine for measuring the
			 * total service time of rq: setting
			 * wait_dispatch will cause bfqd->waited_rq to
			 * be set when rq will be dispatched.
			 */
			bfqd->wait_dispatch = true;
			/*
			 * If there is no I/O in service in the drive,
			 * then possible injection occurred before the
			 * arrival of rq will not affect the total
			 * service time of rq. So the injection limit
			 * must not be updated as a function of such
			 * total service time, unless new injection
			 * occurs before rq is completed. To have the
			 * injection limit updated only in the latter
			 * case, reset rqs_injected here (rqs_injected
			 * will be set in case injection is performed
			 * on bfqq before rq is completed).
			 */
			if (bfqd->tot_rq_in_driver == 0)
				bfqd->rqs_injected = false;
		}
	}

	if (bfq_bfqq_sync(bfqq))
		bfq_update_io_intensity(bfqq, now_ns);

	elv_rb_add(&bfqq->sort_list, rq);

	/*
	 * Check if this request is a better next-serve candidate.
	 */
	prev = bfqq->next_rq;
	next_rq = bfq_choose_req(bfqd, bfqq->next_rq, rq, bfqd->last_position);
	bfqq->next_rq = next_rq;

	/*
	 * Adjust priority tree position, if next_rq changes.
	 * See comments on bfq_pos_tree_add_move() for the unlikely().
	 */
	if (unlikely(!bfqd->nonrot_with_queueing && prev != bfqq->next_rq))
		bfq_pos_tree_add_move(bfqd, bfqq);

	if (!bfq_bfqq_busy(bfqq)) /* switching to busy ... */
		bfq_bfqq_handle_idle_busy_switch(bfqd, bfqq, old_wr_coeff,
						 rq, &interactive);
	else {
		if (bfqd->low_latency && old_wr_coeff == 1 && !rq_is_sync(rq) &&
		    time_is_before_jiffies(
				bfqq->last_wr_start_finish +
				bfqd->bfq_wr_min_inter_arr_async)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);

			bfqd->wr_busy_queues++;
			bfqq->entity.prio_changed = 1;
		}
		if (prev != bfqq->next_rq)
			bfq_updated_next_req(bfqd, bfqq);
	}

	/*
	 * Assign jiffies to last_wr_start_finish in the following
	 * cases:
	 *
	 * . if bfqq is not going to be weight-raised, because, for
	 *   non weight-raised queues, last_wr_start_finish stores the
	 *   arrival time of the last request; as of now, this piece
	 *   of information is used only for deciding whether to
	 *   weight-raise async queues
	 *
	 * . if bfqq is not weight-raised, because, if bfqq is now
	 *   switching to weight-raised, then last_wr_start_finish
	 *   stores the time when weight-raising starts
	 *
	 * . if bfqq is interactive, because, regardless of whether
	 *   bfqq is currently weight-raised, the weight-raising
	 *   period must start or restart (this case is considered
	 *   separately because it is not detected by the above
	 *   conditions, if bfqq is already weight-raised)
	 *
	 * last_wr_start_finish has to be updated also if bfqq is soft
	 * real-time, because the weight-raising period is constantly
	 * restarted on idle-to-busy transitions for these queues, but
	 * this is already done in bfq_bfqq_handle_idle_busy_switch if
	 * needed.
	 */
	if (bfqd->low_latency &&
		(old_wr_coeff == 1 || bfqq->wr_coeff == 1 || interactive))
		bfqq->last_wr_start_finish = jiffies;
}

static struct request *bfq_find_rq_fmerge(struct bfq_data *bfqd,
					  struct bio *bio,
					  struct request_queue *q)
{
	struct bfq_queue *bfqq = bfqd->bio_bfqq;


	if (bfqq)
		return elv_rb_find(&bfqq->sort_list, bio_end_sector(bio));

	return NULL;
}

static sector_t get_sdist(sector_t last_pos, struct request *rq)
{
	if (last_pos)
		return abs(blk_rq_pos(rq) - last_pos);

	return 0;
}

static void bfq_remove_request(struct request_queue *q,
			       struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	const int sync = rq_is_sync(rq);

	if (bfqq->next_rq == rq) {
		bfqq->next_rq = bfq_find_next_rq(bfqd, bfqq, rq);
		bfq_updated_next_req(bfqd, bfqq);
	}

	if (rq->queuelist.prev != &rq->queuelist)
		list_del_init(&rq->queuelist);
	bfqq->queued[sync]--;
	/*
	 * Updating of 'bfqd->queued' is protected by 'bfqd->lock', however, it
	 * may be read without holding the lock in bfq_has_work().
	 */
	WRITE_ONCE(bfqd->queued, bfqd->queued - 1);
	elv_rb_del(&bfqq->sort_list, rq);

	elv_rqhash_del(q, rq);
	if (q->last_merge == rq)
		q->last_merge = NULL;

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		bfqq->next_rq = NULL;

		if (bfq_bfqq_busy(bfqq) && bfqq != bfqd->in_service_queue) {
			bfq_del_bfqq_busy(bfqq, false);
			/*
			 * bfqq emptied. In normal operation, when
			 * bfqq is empty, bfqq->entity.service and
			 * bfqq->entity.budget must contain,
			 * respectively, the service received and the
			 * budget used last time bfqq emptied. These
			 * facts do not hold in this case, as at least
			 * this last removal occurred while bfqq is
			 * not in service. To avoid inconsistencies,
			 * reset both bfqq->entity.service and
			 * bfqq->entity.budget, if bfqq has still a
			 * process that may issue I/O requests to it.
			 */
			bfqq->entity.budget = bfqq->entity.service = 0;
		}

		/*
		 * Remove queue from request-position tree as it is empty.
		 */
		if (bfqq->pos_root) {
			rb_erase(&bfqq->pos_node, bfqq->pos_root);
			bfqq->pos_root = NULL;
		}
	} else {
		/* see comments on bfq_pos_tree_add_move() for the unlikely() */
		if (unlikely(!bfqd->nonrot_with_queueing))
			bfq_pos_tree_add_move(bfqd, bfqq);
	}

	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending--;

}

static bool bfq_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *free = NULL;
	/*
	 * bfq_bic_lookup grabs the queue_lock: invoke it now and
	 * store its return value for later use, to avoid nesting
	 * queue_lock inside the bfqd->lock. We assume that the bic
	 * returned by bfq_bic_lookup does not go away before
	 * bfqd->lock is taken.
	 */
	struct bfq_io_cq *bic = bfq_bic_lookup(q);
	bool ret;

	spin_lock_irq(&bfqd->lock);

	if (bic) {
		/*
		 * Make sure cgroup info is uptodate for current process before
		 * considering the merge.
		 */
		bfq_bic_update_cgroup(bic, bio);

		bfqd->bio_bfqq = bic_to_bfqq(bic, op_is_sync(bio->bi_opf),
					     bfq_actuator_index(bfqd, bio));
	} else {
		bfqd->bio_bfqq = NULL;
	}
	bfqd->bio_bic = bic;

	ret = blk_mq_sched_try_merge(q, bio, nr_segs, &free);

	spin_unlock_irq(&bfqd->lock);
	if (free)
		blk_mq_free_request(free);

	return ret;
}

static int bfq_request_merge(struct request_queue *q, struct request **req,
			     struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = bfq_find_rq_fmerge(bfqd, bio, q);
	if (__rq && elv_bio_merge_ok(__rq, bio)) {
		*req = __rq;

		if (blk_discard_mergable(__rq))
			return ELEVATOR_DISCARD_MERGE;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void bfq_request_merged(struct request_queue *q, struct request *req,
			       enum elv_merge type)
{
	if (type == ELEVATOR_FRONT_MERGE &&
	    rb_prev(&req->rb_node) &&
	    blk_rq_pos(req) <
	    blk_rq_pos(container_of(rb_prev(&req->rb_node),
				    struct request, rb_node))) {
		struct bfq_queue *bfqq = RQ_BFQQ(req);
		struct bfq_data *bfqd;
		struct request *prev, *next_rq;

		if (!bfqq)
			return;

		bfqd = bfqq->bfqd;

		/* Reposition request in its sort_list */
		elv_rb_del(&bfqq->sort_list, req);
		elv_rb_add(&bfqq->sort_list, req);

		/* Choose next request to be served for bfqq */
		prev = bfqq->next_rq;
		next_rq = bfq_choose_req(bfqd, bfqq->next_rq, req,
					 bfqd->last_position);
		bfqq->next_rq = next_rq;
		/*
		 * If next_rq changes, update both the queue's budget to
		 * fit the new request and the queue's position in its
		 * rq_pos_tree.
		 */
		if (prev != bfqq->next_rq) {
			bfq_updated_next_req(bfqd, bfqq);
			/*
			 * See comments on bfq_pos_tree_add_move() for
			 * the unlikely().
			 */
			if (unlikely(!bfqd->nonrot_with_queueing))
				bfq_pos_tree_add_move(bfqd, bfqq);
		}
	}
}

/*
 * This function is called to notify the scheduler that the requests
 * rq and 'next' have been merged, with 'next' going away.  BFQ
 * exploits this hook to address the following issue: if 'next' has a
 * fifo_time lower that rq, then the fifo_time of rq must be set to
 * the value of 'next', to not forget the greater age of 'next'.
 *
 * NOTE: in this function we assume that rq is in a bfq_queue, basing
 * on that rq is picked from the hash table q->elevator->hash, which,
 * in its turn, is filled only with I/O requests present in
 * bfq_queues, while BFQ is in use for the request queue q. In fact,
 * the function that fills this hash table (elv_rqhash_add) is called
 * only by bfq_insert_request.
 */
static void bfq_requests_merged(struct request_queue *q, struct request *rq,
				struct request *next)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq),
		*next_bfqq = RQ_BFQQ(next);

	if (!bfqq)
		goto remove;

	/*
	 * If next and rq belong to the same bfq_queue and next is older
	 * than rq, then reposition rq in the fifo (by substituting next
	 * with rq). Otherwise, if next and rq belong to different
	 * bfq_queues, never reposition rq: in fact, we would have to
	 * reposition it with respect to next's position in its own fifo,
	 * which would most certainly be too expensive with respect to
	 * the benefits.
	 */
	if (bfqq == next_bfqq &&
	    !list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    next->fifo_time < rq->fifo_time) {
		list_del_init(&rq->queuelist);
		list_replace_init(&next->queuelist, &rq->queuelist);
		rq->fifo_time = next->fifo_time;
	}

	if (bfqq->next_rq == next)
		bfqq->next_rq = rq;

	bfqg_stats_update_io_merged(bfqq_group(bfqq), next->cmd_flags);
remove:
	/* Merged request may be in the IO scheduler. Remove it. */
	if (!RB_EMPTY_NODE(&next->rb_node)) {
		bfq_remove_request(next->q, next);
		if (next_bfqq)
			bfqg_stats_update_io_remove(bfqq_group(next_bfqq),
						    next->cmd_flags);
	}
}

/* Must be called with bfqq != NULL */
static void bfq_bfqq_end_wr(struct bfq_queue *bfqq)
{
	/*
	 * If bfqq has been enjoying interactive weight-raising, then
	 * reset soft_rt_next_start. We do it for the following
	 * reason. bfqq may have been conveying the I/O needed to load
	 * a soft real-time application. Such an application actually
	 * exhibits a soft real-time I/O pattern after it finishes
	 * loading, and finally starts doing its job. But, if bfqq has
	 * been receiving a lot of bandwidth so far (likely to happen
	 * on a fast device), then soft_rt_next_start now contains a
	 * high value that. So, without this reset, bfqq would be
	 * prevented from being possibly considered as soft_rt for a
	 * very long time.
	 */

	if (bfqq->wr_cur_max_time !=
	    bfqq->bfqd->bfq_wr_rt_max_time)
		bfqq->soft_rt_next_start = jiffies;

	if (bfq_bfqq_busy(bfqq))
		bfqq->bfqd->wr_busy_queues--;
	bfqq->wr_coeff = 1;
	bfqq->wr_cur_max_time = 0;
	bfqq->last_wr_start_finish = jiffies;
	/*
	 * Trigger a weight change on the next invocation of
	 * __bfq_entity_update_weight_prio.
	 */
	bfqq->entity.prio_changed = 1;
}

void bfq_end_wr_async_queues(struct bfq_data *bfqd,
			     struct bfq_group *bfqg)
{
	int i, j, k;

	for (k = 0; k < bfqd->num_actuators; k++) {
		for (i = 0; i < 2; i++)
			for (j = 0; j < IOPRIO_NR_LEVELS; j++)
				if (bfqg->async_bfqq[i][j][k])
					bfq_bfqq_end_wr(bfqg->async_bfqq[i][j][k]);
		if (bfqg->async_idle_bfqq[k])
			bfq_bfqq_end_wr(bfqg->async_idle_bfqq[k]);
	}
}

static void bfq_end_wr(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq;
	int i;

	spin_lock_irq(&bfqd->lock);

	for (i = 0; i < bfqd->num_actuators; i++) {
		list_for_each_entry(bfqq, &bfqd->active_list[i], bfqq_list)
			bfq_bfqq_end_wr(bfqq);
	}
	list_for_each_entry(bfqq, &bfqd->idle_list, bfqq_list)
		bfq_bfqq_end_wr(bfqq);
	bfq_end_wr_async(bfqd);

	spin_unlock_irq(&bfqd->lock);
}

static sector_t bfq_io_struct_pos(void *io_struct, bool request)
{
	if (request)
		return blk_rq_pos(io_struct);
	else
		return ((struct bio *)io_struct)->bi_iter.bi_sector;
}

static int bfq_rq_close_to_sector(void *io_struct, bool request,
				  sector_t sector)
{
	return abs(bfq_io_struct_pos(io_struct, request) - sector) <=
	       BFQQ_CLOSE_THR;
}

static struct bfq_queue *bfqq_find_close(struct bfq_data *bfqd,
					 struct bfq_queue *bfqq,
					 sector_t sector)
{
	struct rb_root *root = &bfqq_group(bfqq)->rq_pos_tree;
	struct rb_node *parent, *node;
	struct bfq_queue *__bfqq;

	if (RB_EMPTY_ROOT(root))
		return NULL;

	/*
	 * First, if we find a request starting at the end of the last
	 * request, choose it.
	 */
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, root, sector, &parent, NULL);
	if (__bfqq)
		return __bfqq;

	/*
	 * If the exact sector wasn't found, the parent of the NULL leaf
	 * will contain the closest sector (rq_pos_tree sorted by
	 * next_request position).
	 */
	__bfqq = rb_entry(parent, struct bfq_queue, pos_node);
	if (bfq_rq_close_to_sector(__bfqq->next_rq, true, sector))
		return __bfqq;

	if (blk_rq_pos(__bfqq->next_rq) < sector)
		node = rb_next(&__bfqq->pos_node);
	else
		node = rb_prev(&__bfqq->pos_node);
	if (!node)
		return NULL;

	__bfqq = rb_entry(node, struct bfq_queue, pos_node);
	if (bfq_rq_close_to_sector(__bfqq->next_rq, true, sector))
		return __bfqq;

	return NULL;
}

static struct bfq_queue *bfq_find_close_cooperator(struct bfq_data *bfqd,
						   struct bfq_queue *cur_bfqq,
						   sector_t sector)
{
	struct bfq_queue *bfqq;

	/*
	 * We shall notice if some of the queues are cooperating,
	 * e.g., working closely on the same area of the device. In
	 * that case, we can group them together and: 1) don't waste
	 * time idling, and 2) serve the union of their requests in
	 * the best possible order for throughput.
	 */
	bfqq = bfqq_find_close(bfqd, cur_bfqq, sector);
	if (!bfqq || bfqq == cur_bfqq)
		return NULL;

	return bfqq;
}

static struct bfq_queue *
bfq_setup_merge(struct bfq_queue *bfqq, struct bfq_queue *new_bfqq)
{
	int process_refs, new_process_refs;
	struct bfq_queue *__bfqq;

	/*
	 * If there are no process references on the new_bfqq, then it is
	 * unsafe to follow the ->new_bfqq chain as other bfqq's in the chain
	 * may have dropped their last reference (not just their last process
	 * reference).
	 */
	if (!bfqq_process_refs(new_bfqq))
		return NULL;

	/* Avoid a circular list and skip interim queue merges. */
	while ((__bfqq = new_bfqq->new_bfqq)) {
		if (__bfqq == bfqq)
			return NULL;
		new_bfqq = __bfqq;
	}

	process_refs = bfqq_process_refs(bfqq);
	new_process_refs = bfqq_process_refs(new_bfqq);
	/*
	 * If the process for the bfqq has gone away, there is no
	 * sense in merging the queues.
	 */
	if (process_refs == 0 || new_process_refs == 0)
		return NULL;

	/*
	 * Make sure merged queues belong to the same parent. Parents could
	 * have changed since the time we decided the two queues are suitable
	 * for merging.
	 */
	if (new_bfqq->entity.parent != bfqq->entity.parent)
		return NULL;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "scheduling merge with queue %d",
		new_bfqq->pid);

	/*
	 * Merging is just a redirection: the requests of the process
	 * owning one of the two queues are redirected to the other queue.
	 * The latter queue, in its turn, is set as shared if this is the
	 * first time that the requests of some process are redirected to
	 * it.
	 *
	 * We redirect bfqq to new_bfqq and not the opposite, because
	 * we are in the context of the process owning bfqq, thus we
	 * have the io_cq of this process. So we can immediately
	 * configure this io_cq to redirect the requests of the
	 * process to new_bfqq. In contrast, the io_cq of new_bfqq is
	 * not available any more (new_bfqq->bic == NULL).
	 *
	 * Anyway, even in case new_bfqq coincides with the in-service
	 * queue, redirecting requests the in-service queue is the
	 * best option, as we feed the in-service queue with new
	 * requests close to the last request served and, by doing so,
	 * are likely to increase the throughput.
	 */
	bfqq->new_bfqq = new_bfqq;
	/*
	 * The above assignment schedules the following redirections:
	 * each time some I/O for bfqq arrives, the process that
	 * generated that I/O is disassociated from bfqq and
	 * associated with new_bfqq. Here we increases new_bfqq->ref
	 * in advance, adding the number of processes that are
	 * expected to be associated with new_bfqq as they happen to
	 * issue I/O.
	 */
	new_bfqq->ref += process_refs;
	return new_bfqq;
}

static bool bfq_may_be_close_cooperator(struct bfq_queue *bfqq,
					struct bfq_queue *new_bfqq)
{
	if (bfq_too_late_for_merging(new_bfqq))
		return false;

	if (bfq_class_idle(bfqq) || bfq_class_idle(new_bfqq) ||
	    (bfqq->ioprio_class != new_bfqq->ioprio_class))
		return false;

	/*
	 * If either of the queues has already been detected as seeky,
	 * then merging it with the other queue is unlikely to lead to
	 * sequential I/O.
	 */
	if (BFQQ_SEEKY(bfqq) || BFQQ_SEEKY(new_bfqq))
		return false;

	/*
	 * Interleaved I/O is known to be done by (some) applications
	 * only for reads, so it does not make sense to merge async
	 * queues.
	 */
	if (!bfq_bfqq_sync(bfqq) || !bfq_bfqq_sync(new_bfqq))
		return false;

	return true;
}

static bool idling_boosts_thr_without_issues(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq);

static struct bfq_queue *
bfq_setup_stable_merge(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		       struct bfq_queue *stable_merge_bfqq,
		       struct bfq_iocq_bfqq_data *bfqq_data)
{
	int proc_ref = min(bfqq_process_refs(bfqq),
			   bfqq_process_refs(stable_merge_bfqq));
	struct bfq_queue *new_bfqq = NULL;

	bfqq_data->stable_merge_bfqq = NULL;
	if (idling_boosts_thr_without_issues(bfqd, bfqq) || proc_ref == 0)
		goto out;

	/* next function will take at least one ref */
	new_bfqq = bfq_setup_merge(bfqq, stable_merge_bfqq);

	if (new_bfqq) {
		bfqq_data->stably_merged = true;
		if (new_bfqq->bic) {
			unsigned int new_a_idx = new_bfqq->actuator_idx;
			struct bfq_iocq_bfqq_data *new_bfqq_data =
				&new_bfqq->bic->bfqq_data[new_a_idx];

			new_bfqq_data->stably_merged = true;
		}
	}

out:
	/* deschedule stable merge, because done or aborted here */
	bfq_put_stable_ref(stable_merge_bfqq);

	return new_bfqq;
}

/*
 * Attempt to schedule a merge of bfqq with the currently in-service
 * queue or with a close queue among the scheduled queues.  Return
 * NULL if no merge was scheduled, a pointer to the shared bfq_queue
 * structure otherwise.
 *
 * The OOM queue is not allowed to participate to cooperation: in fact, since
 * the requests temporarily redirected to the OOM queue could be redirected
 * again to dedicated queues at any time, the state needed to correctly
 * handle merging with the OOM queue would be quite complex and expensive
 * to maintain. Besides, in such a critical condition as an out of memory,
 * the benefits of queue merging may be little relevant, or even negligible.
 *
 * WARNING: queue merging may impair fairness among non-weight raised
 * queues, for at least two reasons: 1) the original weight of a
 * merged queue may change during the merged state, 2) even being the
 * weight the same, a merged queue may be bloated with many more
 * requests than the ones produced by its originally-associated
 * process.
 */
static struct bfq_queue *
bfq_setup_cooperator(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		     void *io_struct, bool request, struct bfq_io_cq *bic)
{
	struct bfq_queue *in_service_bfqq, *new_bfqq;
	unsigned int a_idx = bfqq->actuator_idx;
	struct bfq_iocq_bfqq_data *bfqq_data = &bic->bfqq_data[a_idx];

	/* if a merge has already been setup, then proceed with that first */
	new_bfqq = bfqq->new_bfqq;
	if (new_bfqq) {
		while (new_bfqq->new_bfqq)
			new_bfqq = new_bfqq->new_bfqq;
		return new_bfqq;
	}

	/*
	 * Check delayed stable merge for rotational or non-queueing
	 * devs. For this branch to be executed, bfqq must not be
	 * currently merged with some other queue (i.e., bfqq->bic
	 * must be non null). If we considered also merged queues,
	 * then we should also check whether bfqq has already been
	 * merged with bic->stable_merge_bfqq. But this would be
	 * costly and complicated.
	 */
	if (unlikely(!bfqd->nonrot_with_queueing)) {
		/*
		 * Make sure also that bfqq is sync, because
		 * bic->stable_merge_bfqq may point to some queue (for
		 * stable merging) also if bic is associated with a
		 * sync queue, but this bfqq is async
		 */
		if (bfq_bfqq_sync(bfqq) && bfqq_data->stable_merge_bfqq &&
		    !bfq_bfqq_just_created(bfqq) &&
		    time_is_before_jiffies(bfqq->split_time +
					  msecs_to_jiffies(bfq_late_stable_merging)) &&
		    time_is_before_jiffies(bfqq->creation_time +
					   msecs_to_jiffies(bfq_late_stable_merging))) {
			struct bfq_queue *stable_merge_bfqq =
				bfqq_data->stable_merge_bfqq;

			return bfq_setup_stable_merge(bfqd, bfqq,
						      stable_merge_bfqq,
						      bfqq_data);
		}
	}

	/*
	 * Do not perform queue merging if the device is non
	 * rotational and performs internal queueing. In fact, such a
	 * device reaches a high speed through internal parallelism
	 * and pipelining. This means that, to reach a high
	 * throughput, it must have many requests enqueued at the same
	 * time. But, in this configuration, the internal scheduling
	 * algorithm of the device does exactly the job of queue
	 * merging: it reorders requests so as to obtain as much as
	 * possible a sequential I/O pattern. As a consequence, with
	 * the workload generated by processes doing interleaved I/O,
	 * the throughput reached by the device is likely to be the
	 * same, with and without queue merging.
	 *
	 * Disabling merging also provides a remarkable benefit in
	 * terms of throughput. Merging tends to make many workloads
	 * artificially more uneven, because of shared queues
	 * remaining non empty for incomparably more time than
	 * non-merged queues. This may accentuate workload
	 * asymmetries. For example, if one of the queues in a set of
	 * merged queues has a higher weight than a normal queue, then
	 * the shared queue may inherit such a high weight and, by
	 * staying almost always active, may force BFQ to perform I/O
	 * plugging most of the time. This evidently makes it harder
	 * for BFQ to let the device reach a high throughput.
	 *
	 * Finally, the likely() macro below is not used because one
	 * of the two branches is more likely than the other, but to
	 * have the code path after the following if() executed as
	 * fast as possible for the case of a non rotational device
	 * with queueing. We want it because this is the fastest kind
	 * of device. On the opposite end, the likely() may lengthen
	 * the execution time of BFQ for the case of slower devices
	 * (rotational or at least without queueing). But in this case
	 * the execution time of BFQ matters very little, if not at
	 * all.
	 */
	if (likely(bfqd->nonrot_with_queueing))
		return NULL;

	/*
	 * Prevent bfqq from being merged if it has been created too
	 * long ago. The idea is that true cooperating processes, and
	 * thus their associated bfq_queues, are supposed to be
	 * created shortly after each other. This is the case, e.g.,
	 * for KVM/QEMU and dump I/O threads. Basing on this
	 * assumption, the following filtering greatly reduces the
	 * probability that two non-cooperating processes, which just
	 * happen to do close I/O for some short time interval, have
	 * their queues merged by mistake.
	 */
	if (bfq_too_late_for_merging(bfqq))
		return NULL;

	if (!io_struct || unlikely(bfqq == &bfqd->oom_bfqq))
		return NULL;

	/* If there is only one backlogged queue, don't search. */
	if (bfq_tot_busy_queues(bfqd) == 1)
		return NULL;

	in_service_bfqq = bfqd->in_service_queue;

	if (in_service_bfqq && in_service_bfqq != bfqq &&
	    likely(in_service_bfqq != &bfqd->oom_bfqq) &&
	    bfq_rq_close_to_sector(io_struct, request,
				   bfqd->in_serv_last_pos) &&
	    bfqq->entity.parent == in_service_bfqq->entity.parent &&
	    bfq_may_be_close_cooperator(bfqq, in_service_bfqq)) {
		new_bfqq = bfq_setup_merge(bfqq, in_service_bfqq);
		if (new_bfqq)
			return new_bfqq;
	}
	/*
	 * Check whether there is a cooperator among currently scheduled
	 * queues. The only thing we need is that the bio/request is not
	 * NULL, as we need it to establish whether a cooperator exists.
	 */
	new_bfqq = bfq_find_close_cooperator(bfqd, bfqq,
			bfq_io_struct_pos(io_struct, request));

	if (new_bfqq && likely(new_bfqq != &bfqd->oom_bfqq) &&
	    bfq_may_be_close_cooperator(bfqq, new_bfqq))
		return bfq_setup_merge(bfqq, new_bfqq);

	return NULL;
}

static void bfq_bfqq_save_state(struct bfq_queue *bfqq)
{
	struct bfq_io_cq *bic = bfqq->bic;
	unsigned int a_idx = bfqq->actuator_idx;
	struct bfq_iocq_bfqq_data *bfqq_data = &bic->bfqq_data[a_idx];

	/*
	 * If !bfqq->bic, the queue is already shared or its requests
	 * have already been redirected to a shared queue; both idle window
	 * and weight raising state have already been saved. Do nothing.
	 */
	if (!bic)
		return;

	bfqq_data->saved_last_serv_time_ns = bfqq->last_serv_time_ns;
	bfqq_data->saved_inject_limit =	bfqq->inject_limit;
	bfqq_data->saved_decrease_time_jif = bfqq->decrease_time_jif;

	bfqq_data->saved_weight = bfqq->entity.orig_weight;
	bfqq_data->saved_ttime = bfqq->ttime;
	bfqq_data->saved_has_short_ttime =
		bfq_bfqq_has_short_ttime(bfqq);
	bfqq_data->saved_IO_bound = bfq_bfqq_IO_bound(bfqq);
	bfqq_data->saved_io_start_time = bfqq->io_start_time;
	bfqq_data->saved_tot_idle_time = bfqq->tot_idle_time;
	bfqq_data->saved_in_large_burst = bfq_bfqq_in_large_burst(bfqq);
	bfqq_data->was_in_burst_list =
		!hlist_unhashed(&bfqq->burst_list_node);

	if (unlikely(bfq_bfqq_just_created(bfqq) &&
		     !bfq_bfqq_in_large_burst(bfqq) &&
		     bfqq->bfqd->low_latency)) {
		/*
		 * bfqq being merged right after being created: bfqq
		 * would have deserved interactive weight raising, but
		 * did not make it to be set in a weight-raised state,
		 * because of this early merge.	Store directly the
		 * weight-raising state that would have been assigned
		 * to bfqq, so that to avoid that bfqq unjustly fails
		 * to enjoy weight raising if split soon.
		 */
		bfqq_data->saved_wr_coeff = bfqq->bfqd->bfq_wr_coeff;
		bfqq_data->saved_wr_start_at_switch_to_srt =
			bfq_smallest_from_now();
		bfqq_data->saved_wr_cur_max_time =
			bfq_wr_duration(bfqq->bfqd);
		bfqq_data->saved_last_wr_start_finish = jiffies;
	} else {
		bfqq_data->saved_wr_coeff = bfqq->wr_coeff;
		bfqq_data->saved_wr_start_at_switch_to_srt =
			bfqq->wr_start_at_switch_to_srt;
		bfqq_data->saved_service_from_wr =
			bfqq->service_from_wr;
		bfqq_data->saved_last_wr_start_finish =
			bfqq->last_wr_start_finish;
		bfqq_data->saved_wr_cur_max_time = bfqq->wr_cur_max_time;
	}
}


void bfq_reassign_last_bfqq(struct bfq_queue *cur_bfqq,
			    struct bfq_queue *new_bfqq)
{
	if (cur_bfqq->entity.parent &&
	    cur_bfqq->entity.parent->last_bfqq_created == cur_bfqq)
		cur_bfqq->entity.parent->last_bfqq_created = new_bfqq;
	else if (cur_bfqq->bfqd && cur_bfqq->bfqd->last_bfqq_created == cur_bfqq)
		cur_bfqq->bfqd->last_bfqq_created = new_bfqq;
}

void bfq_release_process_ref(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/*
	 * To prevent bfqq's service guarantees from being violated,
	 * bfqq may be left busy, i.e., queued for service, even if
	 * empty (see comments in __bfq_bfqq_expire() for
	 * details). But, if no process will send requests to bfqq any
	 * longer, then there is no point in keeping bfqq queued for
	 * service. In addition, keeping bfqq queued for service, but
	 * with no process ref any longer, may have caused bfqq to be
	 * freed when dequeued from service. But this is assumed to
	 * never happen.
	 */
	if (bfq_bfqq_busy(bfqq) && RB_EMPTY_ROOT(&bfqq->sort_list) &&
	    bfqq != bfqd->in_service_queue)
		bfq_del_bfqq_busy(bfqq, false);

	bfq_reassign_last_bfqq(bfqq, NULL);

	bfq_put_queue(bfqq);
}

static struct bfq_queue *bfq_merge_bfqqs(struct bfq_data *bfqd,
					 struct bfq_io_cq *bic,
					 struct bfq_queue *bfqq)
{
	struct bfq_queue *new_bfqq = bfqq->new_bfqq;

	bfq_log_bfqq(bfqd, bfqq, "merging with queue %lu",
		(unsigned long)new_bfqq->pid);
	/* Save weight raising and idle window of the merged queues */
	bfq_bfqq_save_state(bfqq);
	bfq_bfqq_save_state(new_bfqq);
	if (bfq_bfqq_IO_bound(bfqq))
		bfq_mark_bfqq_IO_bound(new_bfqq);
	bfq_clear_bfqq_IO_bound(bfqq);

	/*
	 * The processes associated with bfqq are cooperators of the
	 * processes associated with new_bfqq. So, if bfqq has a
	 * waker, then assume that all these processes will be happy
	 * to let bfqq's waker freely inject I/O when they have no
	 * I/O.
	 */
	if (bfqq->waker_bfqq && !new_bfqq->waker_bfqq &&
	    bfqq->waker_bfqq != new_bfqq) {
		new_bfqq->waker_bfqq = bfqq->waker_bfqq;
		new_bfqq->tentative_waker_bfqq = NULL;

		/*
		 * If the waker queue disappears, then
		 * new_bfqq->waker_bfqq must be reset. So insert
		 * new_bfqq into the woken_list of the waker. See
		 * bfq_check_waker for details.
		 */
		hlist_add_head(&new_bfqq->woken_list_node,
			       &new_bfqq->waker_bfqq->woken_list);

	}

	/*
	 * If bfqq is weight-raised, then let new_bfqq inherit
	 * weight-raising. To reduce false positives, neglect the case
	 * where bfqq has just been created, but has not yet made it
	 * to be weight-raised (which may happen because EQM may merge
	 * bfqq even before bfq_add_request is executed for the first
	 * time for bfqq). Handling this case would however be very
	 * easy, thanks to the flag just_created.
	 */
	if (new_bfqq->wr_coeff == 1 && bfqq->wr_coeff > 1) {
		new_bfqq->wr_coeff = bfqq->wr_coeff;
		new_bfqq->wr_cur_max_time = bfqq->wr_cur_max_time;
		new_bfqq->last_wr_start_finish = bfqq->last_wr_start_finish;
		new_bfqq->wr_start_at_switch_to_srt =
			bfqq->wr_start_at_switch_to_srt;
		if (bfq_bfqq_busy(new_bfqq))
			bfqd->wr_busy_queues++;
		new_bfqq->entity.prio_changed = 1;
	}

	if (bfqq->wr_coeff > 1) { /* bfqq has given its wr to new_bfqq */
		bfqq->wr_coeff = 1;
		bfqq->entity.prio_changed = 1;
		if (bfq_bfqq_busy(bfqq))
			bfqd->wr_busy_queues--;
	}

	bfq_log_bfqq(bfqd, new_bfqq, "merge_bfqqs: wr_busy %d",
		     bfqd->wr_busy_queues);

	/*
	 * Merge queues (that is, let bic redirect its requests to new_bfqq)
	 */
	bic_set_bfqq(bic, new_bfqq, true, bfqq->actuator_idx);
	bfq_mark_bfqq_coop(new_bfqq);
	/*
	 * new_bfqq now belongs to at least two bics (it is a shared queue):
	 * set new_bfqq->bic to NULL. bfqq either:
	 * - does not belong to any bic any more, and hence bfqq->bic must
	 *   be set to NULL, or
	 * - is a queue whose owning bics have already been redirected to a
	 *   different queue, hence the queue is destined to not belong to
	 *   any bic soon and bfqq->bic is already NULL (therefore the next
	 *   assignment causes no harm).
	 */
	new_bfqq->bic = NULL;
	/*
	 * If the queue is shared, the pid is the pid of one of the associated
	 * processes. Which pid depends on the exact sequence of merge events
	 * the queue underwent. So printing such a pid is useless and confusing
	 * because it reports a random pid between those of the associated
	 * processes.
	 * We mark such a queue with a pid -1, and then print SHARED instead of
	 * a pid in logging messages.
	 */
	new_bfqq->pid = -1;
	bfqq->bic = NULL;

	bfq_reassign_last_bfqq(bfqq, new_bfqq);

	bfq_release_process_ref(bfqd, bfqq);

	return new_bfqq;
}

static bool bfq_allow_bio_merge(struct request_queue *q, struct request *rq,
				struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	bool is_sync = op_is_sync(bio->bi_opf);
	struct bfq_queue *bfqq = bfqd->bio_bfqq, *new_bfqq;

	/*
	 * Disallow merge of a sync bio into an async request.
	 */
	if (is_sync && !rq_is_sync(rq))
		return false;

	/*
	 * Lookup the bfqq that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 */
	if (!bfqq)
		return false;

	/*
	 * We take advantage of this function to perform an early merge
	 * of the queues of possible cooperating processes.
	 */
	new_bfqq = bfq_setup_cooperator(bfqd, bfqq, bio, false, bfqd->bio_bic);
	if (new_bfqq) {
		/*
		 * bic still points to bfqq, then it has not yet been
		 * redirected to some other bfq_queue, and a queue
		 * merge between bfqq and new_bfqq can be safely
		 * fulfilled, i.e., bic can be redirected to new_bfqq
		 * and bfqq can be put.
		 */
		while (bfqq != new_bfqq)
			bfqq = bfq_merge_bfqqs(bfqd, bfqd->bio_bic, bfqq);

		/*
		 * Change also bqfd->bio_bfqq, as
		 * bfqd->bio_bic now points to new_bfqq, and
		 * this function may be invoked again (and then may
		 * use again bqfd->bio_bfqq).
		 */
		bfqd->bio_bfqq = bfqq;
	}

	return bfqq == RQ_BFQQ(rq);
}

/*
 * Set the maximum time for the in-service queue to consume its
 * budget. This prevents seeky processes from lowering the throughput.
 * In practice, a time-slice service scheme is used with seeky
 * processes.
 */
static void bfq_set_budget_timeout(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq)
{
	unsigned int timeout_coeff;

	if (bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time)
		timeout_coeff = 1;
	else
		timeout_coeff = bfqq->entity.weight / bfqq->entity.orig_weight;

	bfqd->last_budget_start = blk_time_get();

	bfqq->budget_timeout = jiffies +
		bfqd->bfq_timeout * timeout_coeff;
}

static void __bfq_set_in_service_queue(struct bfq_data *bfqd,
				       struct bfq_queue *bfqq)
{
	if (bfqq) {
		bfq_clear_bfqq_fifo_expire(bfqq);

		bfqd->budgets_assigned = (bfqd->budgets_assigned * 7 + 256) / 8;

		if (time_is_before_jiffies(bfqq->last_wr_start_finish) &&
		    bfqq->wr_coeff > 1 &&
		    bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time &&
		    time_is_before_jiffies(bfqq->budget_timeout)) {
			/*
			 * For soft real-time queues, move the start
			 * of the weight-raising period forward by the
			 * time the queue has not received any
			 * service. Otherwise, a relatively long
			 * service delay is likely to cause the
			 * weight-raising period of the queue to end,
			 * because of the short duration of the
			 * weight-raising period of a soft real-time
			 * queue.  It is worth noting that this move
			 * is not so dangerous for the other queues,
			 * because soft real-time queues are not
			 * greedy.
			 *
			 * To not add a further variable, we use the
			 * overloaded field budget_timeout to
			 * determine for how long the queue has not
			 * received service, i.e., how much time has
			 * elapsed since the queue expired. However,
			 * this is a little imprecise, because
			 * budget_timeout is set to jiffies if bfqq
			 * not only expires, but also remains with no
			 * request.
			 */
			if (time_after(bfqq->budget_timeout,
				       bfqq->last_wr_start_finish))
				bfqq->last_wr_start_finish +=
					jiffies - bfqq->budget_timeout;
			else
				bfqq->last_wr_start_finish = jiffies;
		}

		bfq_set_budget_timeout(bfqd, bfqq);
		bfq_log_bfqq(bfqd, bfqq,
			     "set_in_service_queue, cur-budget = %d",
			     bfqq->entity.budget);
	}

	bfqd->in_service_queue = bfqq;
	bfqd->in_serv_last_pos = 0;
}

/*
 * Get and set a new queue for service.
 */
static struct bfq_queue *bfq_set_in_service_queue(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfq_get_next_queue(bfqd);

	__bfq_set_in_service_queue(bfqd, bfqq);
	return bfqq;
}

static void bfq_arm_slice_timer(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	u32 sl;

	bfq_mark_bfqq_wait_request(bfqq);

	/*
	 * We don't want to idle for seeks, but we do want to allow
	 * fair distribution of slice time for a process doing back-to-back
	 * seeks. So allow a little bit of time for him to submit a new rq.
	 */
	sl = bfqd->bfq_slice_idle;
	/*
	 * Unless the queue is being weight-raised or the scenario is
	 * asymmetric, grant only minimum idle time if the queue
	 * is seeky. A long idling is preserved for a weight-raised
	 * queue, or, more in general, in an asymmetric scenario,
	 * because a long idling is needed for guaranteeing to a queue
	 * its reserved share of the throughput (in particular, it is
	 * needed if the queue has a higher weight than some other
	 * queue).
	 */
	if (BFQQ_SEEKY(bfqq) && bfqq->wr_coeff == 1 &&
	    !bfq_asymmetric_scenario(bfqd, bfqq))
		sl = min_t(u64, sl, BFQ_MIN_TT);
	else if (bfqq->wr_coeff > 1)
		sl = max_t(u32, sl, 20ULL * NSEC_PER_MSEC);

	bfqd->last_idling_start = blk_time_get();
	bfqd->last_idling_start_jiffies = jiffies;

	hrtimer_start(&bfqd->idle_slice_timer, ns_to_ktime(sl),
		      HRTIMER_MODE_REL);
	bfqg_stats_set_start_idle_time(bfqq_group(bfqq));
}

/*
 * In autotuning mode, max_budget is dynamically recomputed as the
 * amount of sectors transferred in timeout at the estimated peak
 * rate. This enables BFQ to utilize a full timeslice with a full
 * budget, even if the in-service queue is served at peak rate. And
 * this maximises throughput with sequential workloads.
 */
static unsigned long bfq_calc_max_budget(struct bfq_data *bfqd)
{
	return (u64)bfqd->peak_rate * USEC_PER_MSEC *
		jiffies_to_msecs(bfqd->bfq_timeout)>>BFQ_RATE_SHIFT;
}

/*
 * Update parameters related to throughput and responsiveness, as a
 * function of the estimated peak rate. See comments on
 * bfq_calc_max_budget(), and on the ref_wr_duration array.
 */
static void update_thr_responsiveness_params(struct bfq_data *bfqd)
{
	if (bfqd->bfq_user_max_budget == 0) {
		bfqd->bfq_max_budget =
			bfq_calc_max_budget(bfqd);
		bfq_log(bfqd, "new max_budget = %d", bfqd->bfq_max_budget);
	}
}

static void bfq_reset_rate_computation(struct bfq_data *bfqd,
				       struct request *rq)
{
	if (rq != NULL) { /* new rq dispatch now, reset accordingly */
		bfqd->last_dispatch = bfqd->first_dispatch = blk_time_get_ns();
		bfqd->peak_rate_samples = 1;
		bfqd->sequential_samples = 0;
		bfqd->tot_sectors_dispatched = bfqd->last_rq_max_size =
			blk_rq_sectors(rq);
	} else /* no new rq dispatched, just reset the number of samples */
		bfqd->peak_rate_samples = 0; /* full re-init on next disp. */

	bfq_log(bfqd,
		"reset_rate_computation at end, sample %u/%u tot_sects %llu",
		bfqd->peak_rate_samples, bfqd->sequential_samples,
		bfqd->tot_sectors_dispatched);
}

static void bfq_update_rate_reset(struct bfq_data *bfqd, struct request *rq)
{
	u32 rate, weight, divisor;

	/*
	 * For the convergence property to hold (see comments on
	 * bfq_update_peak_rate()) and for the assessment to be
	 * reliable, a minimum number of samples must be present, and
	 * a minimum amount of time must have elapsed. If not so, do
	 * not compute new rate. Just reset parameters, to get ready
	 * for a new evaluation attempt.
	 */
	if (bfqd->peak_rate_samples < BFQ_RATE_MIN_SAMPLES ||
	    bfqd->delta_from_first < BFQ_RATE_MIN_INTERVAL)
		goto reset_computation;

	/*
	 * If a new request completion has occurred after last
	 * dispatch, then, to approximate the rate at which requests
	 * have been served by the device, it is more precise to
	 * extend the observation interval to the last completion.
	 */
	bfqd->delta_from_first =
		max_t(u64, bfqd->delta_from_first,
		      bfqd->last_completion - bfqd->first_dispatch);

	/*
	 * Rate computed in sects/usec, and not sects/nsec, for
	 * precision issues.
	 */
	rate = div64_ul(bfqd->tot_sectors_dispatched<<BFQ_RATE_SHIFT,
			div_u64(bfqd->delta_from_first, NSEC_PER_USEC));

	/*
	 * Peak rate not updated if:
	 * - the percentage of sequential dispatches is below 3/4 of the
	 *   total, and rate is below the current estimated peak rate
	 * - rate is unreasonably high (> 20M sectors/sec)
	 */
	if ((bfqd->sequential_samples < (3 * bfqd->peak_rate_samples)>>2 &&
	     rate <= bfqd->peak_rate) ||
		rate > 20<<BFQ_RATE_SHIFT)
		goto reset_computation;

	/*
	 * We have to update the peak rate, at last! To this purpose,
	 * we use a low-pass filter. We compute the smoothing constant
	 * of the filter as a function of the 'weight' of the new
	 * measured rate.
	 *
	 * As can be seen in next formulas, we define this weight as a
	 * quantity proportional to how sequential the workload is,
	 * and to how long the observation time interval is.
	 *
	 * The weight runs from 0 to 8. The maximum value of the
	 * weight, 8, yields the minimum value for the smoothing
	 * constant. At this minimum value for the smoothing constant,
	 * the measured rate contributes for half of the next value of
	 * the estimated peak rate.
	 *
	 * So, the first step is to compute the weight as a function
	 * of how sequential the workload is. Note that the weight
	 * cannot reach 9, because bfqd->sequential_samples cannot
	 * become equal to bfqd->peak_rate_samples, which, in its
	 * turn, holds true because bfqd->sequential_samples is not
	 * incremented for the first sample.
	 */
	weight = (9 * bfqd->sequential_samples) / bfqd->peak_rate_samples;

	/*
	 * Second step: further refine the weight as a function of the
	 * duration of the observation interval.
	 */
	weight = min_t(u32, 8,
		       div_u64(weight * bfqd->delta_from_first,
			       BFQ_RATE_REF_INTERVAL));

	/*
	 * Divisor ranging from 10, for minimum weight, to 2, for
	 * maximum weight.
	 */
	divisor = 10 - weight;

	/*
	 * Finally, update peak rate:
	 *
	 * peak_rate = peak_rate * (divisor-1) / divisor  +  rate / divisor
	 */
	bfqd->peak_rate *= divisor-1;
	bfqd->peak_rate /= divisor;
	rate /= divisor; /* smoothing constant alpha = 1/divisor */

	bfqd->peak_rate += rate;

	/*
	 * For a very slow device, bfqd->peak_rate can reach 0 (see
	 * the minimum representable values reported in the comments
	 * on BFQ_RATE_SHIFT). Push to 1 if this happens, to avoid
	 * divisions by zero where bfqd->peak_rate is used as a
	 * divisor.
	 */
	bfqd->peak_rate = max_t(u32, 1, bfqd->peak_rate);

	update_thr_responsiveness_params(bfqd);

reset_computation:
	bfq_reset_rate_computation(bfqd, rq);
}

/*
 * Update the read/write peak rate (the main quantity used for
 * auto-tuning, see update_thr_responsiveness_params()).
 *
 * It is not trivial to estimate the peak rate (correctly): because of
 * the presence of sw and hw queues between the scheduler and the
 * device components that finally serve I/O requests, it is hard to
 * say exactly when a given dispatched request is served inside the
 * device, and for how long. As a consequence, it is hard to know
 * precisely at what rate a given set of requests is actually served
 * by the device.
 *
 * On the opposite end, the dispatch time of any request is trivially
 * available, and, from this piece of information, the "dispatch rate"
 * of requests can be immediately computed. So, the idea in the next
 * function is to use what is known, namely request dispatch times
 * (plus, when useful, request completion times), to estimate what is
 * unknown, namely in-device request service rate.
 *
 * The main issue is that, because of the above facts, the rate at
 * which a certain set of requests is dispatched over a certain time
 * interval can vary greatly with respect to the rate at which the
 * same requests are then served. But, since the size of any
 * intermediate queue is limited, and the service scheme is lossless
 * (no request is silently dropped), the following obvious convergence
 * property holds: the number of requests dispatched MUST become
 * closer and closer to the number of requests completed as the
 * observation interval grows. This is the key property used in
 * the next function to estimate the peak service rate as a function
 * of the observed dispatch rate. The function assumes to be invoked
 * on every request dispatch.
 */
static void bfq_update_peak_rate(struct bfq_data *bfqd, struct request *rq)
{
	u64 now_ns = blk_time_get_ns();

	if (bfqd->peak_rate_samples == 0) { /* first dispatch */
		bfq_log(bfqd, "update_peak_rate: goto reset, samples %d",
			bfqd->peak_rate_samples);
		bfq_reset_rate_computation(bfqd, rq);
		goto update_last_values; /* will add one sample */
	}

	/*
	 * Device idle for very long: the observation interval lasting
	 * up to this dispatch cannot be a valid observation interval
	 * for computing a new peak rate (similarly to the late-
	 * completion event in bfq_completed_request()). Go to
	 * update_rate_and_reset to have the following three steps
	 * taken:
	 * - close the observation interval at the last (previous)
	 *   request dispatch or completion
	 * - compute rate, if possible, for that observation interval
	 * - start a new observation interval with this dispatch
	 */
	if (now_ns - bfqd->last_dispatch > 100*NSEC_PER_MSEC &&
	    bfqd->tot_rq_in_driver == 0)
		goto update_rate_and_reset;

	/* Update sampling information */
	bfqd->peak_rate_samples++;

	if ((bfqd->tot_rq_in_driver > 0 ||
		now_ns - bfqd->last_completion < BFQ_MIN_TT)
	    && !BFQ_RQ_SEEKY(bfqd, bfqd->last_position, rq))
		bfqd->sequential_samples++;

	bfqd->tot_sectors_dispatched += blk_rq_sectors(rq);

	/* Reset max observed rq size every 32 dispatches */
	if (likely(bfqd->peak_rate_samples % 32))
		bfqd->last_rq_max_size =
			max_t(u32, blk_rq_sectors(rq), bfqd->last_rq_max_size);
	else
		bfqd->last_rq_max_size = blk_rq_sectors(rq);

	bfqd->delta_from_first = now_ns - bfqd->first_dispatch;

	/* Target observation interval not yet reached, go on sampling */
	if (bfqd->delta_from_first < BFQ_RATE_REF_INTERVAL)
		goto update_last_values;

update_rate_and_reset:
	bfq_update_rate_reset(bfqd, rq);
update_last_values:
	bfqd->last_position = blk_rq_pos(rq) + blk_rq_sectors(rq);
	if (RQ_BFQQ(rq) == bfqd->in_service_queue)
		bfqd->in_serv_last_pos = bfqd->last_position;
	bfqd->last_dispatch = now_ns;
}

/*
 * Remove request from internal lists.
 */
static void bfq_dispatch_remove(struct request_queue *q, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	/*
	 * For consistency, the next instruction should have been
	 * executed after removing the request from the queue and
	 * dispatching it.  We execute instead this instruction before
	 * bfq_remove_request() (and hence introduce a temporary
	 * inconsistency), for efficiency.  In fact, should this
	 * dispatch occur for a non in-service bfqq, this anticipated
	 * increment prevents two counters related to bfqq->dispatched
	 * from risking to be, first, uselessly decremented, and then
	 * incremented again when the (new) value of bfqq->dispatched
	 * happens to be taken into account.
	 */
	bfqq->dispatched++;
	bfq_update_peak_rate(q->elevator->elevator_data, rq);

	bfq_remove_request(q, rq);
}

/*
 * There is a case where idling does not have to be performed for
 * throughput concerns, but to preserve the throughput share of
 * the process associated with bfqq.
 *
 * To introduce this case, we can note that allowing the drive
 * to enqueue more than one request at a time, and hence
 * delegating de facto final scheduling decisions to the
 * drive's internal scheduler, entails loss of control on the
 * actual request service order. In particular, the critical
 * situation is when requests from different processes happen
 * to be present, at the same time, in the internal queue(s)
 * of the drive. In such a situation, the drive, by deciding
 * the service order of the internally-queued requests, does
 * determine also the actual throughput distribution among
 * these processes. But the drive typically has no notion or
 * concern about per-process throughput distribution, and
 * makes its decisions only on a per-request basis. Therefore,
 * the service distribution enforced by the drive's internal
 * scheduler is likely to coincide with the desired throughput
 * distribution only in a completely symmetric, or favorably
 * skewed scenario where:
 * (i-a) each of these processes must get the same throughput as
 *	 the others,
 * (i-b) in case (i-a) does not hold, it holds that the process
 *       associated with bfqq must receive a lower or equal
 *	 throughput than any of the other processes;
 * (ii)  the I/O of each process has the same properties, in
 *       terms of locality (sequential or random), direction
 *       (reads or writes), request sizes, greediness
 *       (from I/O-bound to sporadic), and so on;

 * In fact, in such a scenario, the drive tends to treat the requests
 * of each process in about the same way as the requests of the
 * others, and thus to provide each of these processes with about the
 * same throughput.  This is exactly the desired throughput
 * distribution if (i-a) holds, or, if (i-b) holds instead, this is an
 * even more convenient distribution for (the process associated with)
 * bfqq.
 *
 * In contrast, in any asymmetric or unfavorable scenario, device
 * idling (I/O-dispatch plugging) is certainly needed to guarantee
 * that bfqq receives its assigned fraction of the device throughput
 * (see [1] for details).
 *
 * The problem is that idling may significantly reduce throughput with
 * certain combinations of types of I/O and devices. An important
 * example is sync random I/O on flash storage with command
 * queueing. So, unless bfqq falls in cases where idling also boosts
 * throughput, it is important to check conditions (i-a), i(-b) and
 * (ii) accurately, so as to avoid idling when not strictly needed for
 * service guarantees.
 *
 * Unfortunately, it is extremely difficult to thoroughly check
 * condition (ii). And, in case there are active groups, it becomes
 * very difficult to check conditions (i-a) and (i-b) too.  In fact,
 * if there are active groups, then, for conditions (i-a) or (i-b) to
 * become false 'indirectly', it is enough that an active group
 * contains more active processes or sub-groups than some other active
 * group. More precisely, for conditions (i-a) or (i-b) to become
 * false because of such a group, it is not even necessary that the
 * group is (still) active: it is sufficient that, even if the group
 * has become inactive, some of its descendant processes still have
 * some request already dispatched but still waiting for
 * completion. In fact, requests have still to be guaranteed their
 * share of the throughput even after being dispatched. In this
 * respect, it is easy to show that, if a group frequently becomes
 * inactive while still having in-flight requests, and if, when this
 * happens, the group is not considered in the calculation of whether
 * the scenario is asymmetric, then the group may fail to be
 * guaranteed its fair share of the throughput (basically because
 * idling may not be performed for the descendant processes of the
 * group, but it had to be).  We address this issue with the following
 * bi-modal behavior, implemented in the function
 * bfq_asymmetric_scenario().
 *
 * If there are groups with requests waiting for completion
 * (as commented above, some of these groups may even be
 * already inactive), then the scenario is tagged as
 * asymmetric, conservatively, without checking any of the
 * conditions (i-a), (i-b) or (ii). So the device is idled for bfqq.
 * This behavior matches also the fact that groups are created
 * exactly if controlling I/O is a primary concern (to
 * preserve bandwidth and latency guarantees).
 *
 * On the opposite end, if there are no groups with requests waiting
 * for completion, then only conditions (i-a) and (i-b) are actually
 * controlled, i.e., provided that conditions (i-a) or (i-b) holds,
 * idling is not performed, regardless of whether condition (ii)
 * holds.  In other words, only if conditions (i-a) and (i-b) do not
 * hold, then idling is allowed, and the device tends to be prevented
 * from queueing many requests, possibly of several processes. Since
 * there are no groups with requests waiting for completion, then, to
 * control conditions (i-a) and (i-b) it is enough to check just
 * whether all the queues with requests waiting for completion also
 * have the same weight.
 *
 * Not checking condition (ii) evidently exposes bfqq to the
 * risk of getting less throughput than its fair share.
 * However, for queues with the same weight, a further
 * mechanism, preemption, mitigates or even eliminates this
 * problem. And it does so without consequences on overall
 * throughput. This mechanism and its benefits are explained
 * in the next three paragraphs.
 *
 * Even if a queue, say Q, is expired when it remains idle, Q
 * can still preempt the new in-service queue if the next
 * request of Q arrives soon (see the comments on
 * bfq_bfqq_update_budg_for_activation). If all queues and
 * groups have the same weight, this form of preemption,
 * combined with the hole-recovery heuristic described in the
 * comments on function bfq_bfqq_update_budg_for_activation,
 * are enough to preserve a correct bandwidth distribution in
 * the mid term, even without idling. In fact, even if not
 * idling allows the internal queues of the device to contain
 * many requests, and thus to reorder requests, we can rather
 * safely assume that the internal scheduler still preserves a
 * minimum of mid-term fairness.
 *
 * More precisely, this preemption-based, idleless approach
 * provides fairness in terms of IOPS, and not sectors per
 * second. This can be seen with a simple example. Suppose
 * that there are two queues with the same weight, but that
 * the first queue receives requests of 8 sectors, while the
 * second queue receives requests of 1024 sectors. In
 * addition, suppose that each of the two queues contains at
 * most one request at a time, which implies that each queue
 * always remains idle after it is served. Finally, after
 * remaining idle, each queue receives very quickly a new
 * request. It follows that the two queues are served
 * alternatively, preempting each other if needed. This
 * implies that, although both queues have the same weight,
 * the queue with large requests receives a service that is
 * 1024/8 times as high as the service received by the other
 * queue.
 *
 * The motivation for using preemption instead of idling (for
 * queues with the same weight) is that, by not idling,
 * service guarantees are preserved (completely or at least in
 * part) without minimally sacrificing throughput. And, if
 * there is no active group, then the primary expectation for
 * this device is probably a high throughput.
 *
 * We are now left only with explaining the two sub-conditions in the
 * additional compound condition that is checked below for deciding
 * whether the scenario is asymmetric. To explain the first
 * sub-condition, we need to add that the function
 * bfq_asymmetric_scenario checks the weights of only
 * non-weight-raised queues, for efficiency reasons (see comments on
 * bfq_weights_tree_add()). Then the fact that bfqq is weight-raised
 * is checked explicitly here. More precisely, the compound condition
 * below takes into account also the fact that, even if bfqq is being
 * weight-raised, the scenario is still symmetric if all queues with
 * requests waiting for completion happen to be
 * weight-raised. Actually, we should be even more precise here, and
 * differentiate between interactive weight raising and soft real-time
 * weight raising.
 *
 * The second sub-condition checked in the compound condition is
 * whether there is a fair amount of already in-flight I/O not
 * belonging to bfqq. If so, I/O dispatching is to be plugged, for the
 * following reason. The drive may decide to serve in-flight
 * non-bfqq's I/O requests before bfqq's ones, thereby delaying the
 * arrival of new I/O requests for bfqq (recall that bfqq is sync). If
 * I/O-dispatching is not plugged, then, while bfqq remains empty, a
 * basically uncontrolled amount of I/O from other queues may be
 * dispatched too, possibly causing the service of bfqq's I/O to be
 * delayed even longer in the drive. This problem gets more and more
 * serious as the speed and the queue depth of the drive grow,
 * because, as these two quantities grow, the probability to find no
 * queue busy but many requests in flight grows too. By contrast,
 * plugging I/O dispatching minimizes the delay induced by already
 * in-flight I/O, and enables bfqq to recover the bandwidth it may
 * lose because of this delay.
 *
 * As a side note, it is worth considering that the above
 * device-idling countermeasures may however fail in the following
 * unlucky scenario: if I/O-dispatch plugging is (correctly) disabled
 * in a time period during which all symmetry sub-conditions hold, and
 * therefore the device is allowed to enqueue many requests, but at
 * some later point in time some sub-condition stops to hold, then it
 * may become impossible to make requests be served in the desired
 * order until all the requests already queued in the device have been
 * served. The last sub-condition commented above somewhat mitigates
 * this problem for weight-raised queues.
 *
 * However, as an additional mitigation for this problem, we preserve
 * plugging for a special symmetric case that may suddenly turn into
 * asymmetric: the case where only bfqq is busy. In this case, not
 * expiring bfqq does not cause any harm to any other queues in terms
 * of service guarantees. In contrast, it avoids the following unlucky
 * sequence of events: (1) bfqq is expired, (2) a new queue with a
 * lower weight than bfqq becomes busy (or more queues), (3) the new
 * queue is served until a new request arrives for bfqq, (4) when bfqq
 * is finally served, there are so many requests of the new queue in
 * the drive that the pending requests for bfqq take a lot of time to
 * be served. In particular, event (2) may case even already
 * dispatched requests of bfqq to be delayed, inside the drive. So, to
 * avoid this series of events, the scenario is preventively declared
 * as asymmetric also if bfqq is the only busy queues
 */
static bool idling_needed_for_service_guarantees(struct bfq_data *bfqd,
						 struct bfq_queue *bfqq)
{
	int tot_busy_queues = bfq_tot_busy_queues(bfqd);

	/* No point in idling for bfqq if it won't get requests any longer */
	if (unlikely(!bfqq_process_refs(bfqq)))
		return false;

	return (bfqq->wr_coeff > 1 &&
		(bfqd->wr_busy_queues < tot_busy_queues ||
		 bfqd->tot_rq_in_driver >= bfqq->dispatched + 4)) ||
		bfq_asymmetric_scenario(bfqd, bfqq) ||
		tot_busy_queues == 1;
}

static bool __bfq_bfqq_expire(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			      enum bfqq_expiration reason)
{
	/*
	 * If this bfqq is shared between multiple processes, check
	 * to make sure that those processes are still issuing I/Os
	 * within the mean seek distance. If not, it may be time to
	 * break the queues apart again.
	 */
	if (bfq_bfqq_coop(bfqq) && BFQQ_SEEKY(bfqq))
		bfq_mark_bfqq_split_coop(bfqq);

	/*
	 * Consider queues with a higher finish virtual time than
	 * bfqq. If idling_needed_for_service_guarantees(bfqq) returns
	 * true, then bfqq's bandwidth would be violated if an
	 * uncontrolled amount of I/O from these queues were
	 * dispatched while bfqq is waiting for its new I/O to
	 * arrive. This is exactly what may happen if this is a forced
	 * expiration caused by a preemption attempt, and if bfqq is
	 * not re-scheduled. To prevent this from happening, re-queue
	 * bfqq if it needs I/O-dispatch plugging, even if it is
	 * empty. By doing so, bfqq is granted to be served before the
	 * above queues (provided that bfqq is of course eligible).
	 */
	if (RB_EMPTY_ROOT(&bfqq->sort_list) &&
	    !(reason == BFQQE_PREEMPTED &&
	      idling_needed_for_service_guarantees(bfqd, bfqq))) {
		if (bfqq->dispatched == 0)
			/*
			 * Overloading budget_timeout field to store
			 * the time at which the queue remains with no
			 * backlog and no outstanding request; used by
			 * the weight-raising mechanism.
			 */
			bfqq->budget_timeout = jiffies;

		bfq_del_bfqq_busy(bfqq, true);
	} else {
		bfq_requeue_bfqq(bfqd, bfqq, true);
		/*
		 * Resort priority tree of potential close cooperators.
		 * See comments on bfq_pos_tree_add_move() for the unlikely().
		 */
		if (unlikely(!bfqd->nonrot_with_queueing &&
			     !RB_EMPTY_ROOT(&bfqq->sort_list)))
			bfq_pos_tree_add_move(bfqd, bfqq);
	}

	/*
	 * All in-service entities must have been properly deactivated
	 * or requeued before executing the next function, which
	 * resets all in-service entities as no more in service. This
	 * may cause bfqq to be freed. If this happens, the next
	 * function returns true.
	 */
	return __bfq_bfqd_reset_in_service(bfqd);
}

/**
 * __bfq_bfqq_recalc_budget - try to adapt the budget to the @bfqq behavior.
 * @bfqd: device data.
 * @bfqq: queue to update.
 * @reason: reason for expiration.
 *
 * Handle the feedback on @bfqq budget at queue expiration.
 * See the body for detailed comments.
 */
static void __bfq_bfqq_recalc_budget(struct bfq_data *bfqd,
				     struct bfq_queue *bfqq,
				     enum bfqq_expiration reason)
{
	struct request *next_rq;
	int budget, min_budget;

	min_budget = bfq_min_budget(bfqd);

	if (bfqq->wr_coeff == 1)
		budget = bfqq->max_budget;
	else /*
	      * Use a constant, low budget for weight-raised queues,
	      * to help achieve a low latency. Keep it slightly higher
	      * than the minimum possible budget, to cause a little
	      * bit fewer expirations.
	      */
		budget = 2 * min_budget;

	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last budg %d, budg left %d",
		bfqq->entity.budget, bfq_bfqq_budget_left(bfqq));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last max_budg %d, min budg %d",
		budget, bfq_min_budget(bfqd));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: sync %d, seeky %d",
		bfq_bfqq_sync(bfqq), BFQQ_SEEKY(bfqd->in_service_queue));

	if (bfq_bfqq_sync(bfqq) && bfqq->wr_coeff == 1) {
		switch (reason) {
		/*
		 * Caveat: in all the following cases we trade latency
		 * for throughput.
		 */
		case BFQQE_TOO_IDLE:
			/*
			 * This is the only case where we may reduce
			 * the budget: if there is no request of the
			 * process still waiting for completion, then
			 * we assume (tentatively) that the timer has
			 * expired because the batch of requests of
			 * the process could have been served with a
			 * smaller budget.  Hence, betting that
			 * process will behave in the same way when it
			 * becomes backlogged again, we reduce its
			 * next budget.  As long as we guess right,
			 * this budget cut reduces the latency
			 * experienced by the process.
			 *
			 * However, if there are still outstanding
			 * requests, then the process may have not yet
			 * issued its next request just because it is
			 * still waiting for the completion of some of
			 * the still outstanding ones.  So in this
			 * subcase we do not reduce its budget, on the
			 * contrary we increase it to possibly boost
			 * the throughput, as discussed in the
			 * comments to the BUDGET_TIMEOUT case.
			 */
			if (bfqq->dispatched > 0) /* still outstanding reqs */
				budget = min(budget * 2, bfqd->bfq_max_budget);
			else {
				if (budget > 5 * min_budget)
					budget -= 4 * min_budget;
				else
					budget = min_budget;
			}
			break;
		case BFQQE_BUDGET_TIMEOUT:
			/*
			 * We double the budget here because it gives
			 * the chance to boost the throughput if this
			 * is not a seeky process (and has bumped into
			 * this timeout because of, e.g., ZBR).
			 */
			budget = min(budget * 2, bfqd->bfq_max_budget);
			break;
		case BFQQE_BUDGET_EXHAUSTED:
			/*
			 * The process still has backlog, and did not
			 * let either the budget timeout or the disk
			 * idling timeout expire. Hence it is not
			 * seeky, has a short thinktime and may be
			 * happy with a higher budget too. So
			 * definitely increase the budget of this good
			 * candidate to boost the disk throughput.
			 */
			budget = min(budget * 4, bfqd->bfq_max_budget);
			break;
		case BFQQE_NO_MORE_REQUESTS:
			/*
			 * For queues that expire for this reason, it
			 * is particularly important to keep the
			 * budget close to the actual service they
			 * need. Doing so reduces the timestamp
			 * misalignment problem described in the
			 * comments in the body of
			 * __bfq_activate_entity. In fact, suppose
			 * that a queue systematically expires for
			 * BFQQE_NO_MORE_REQUESTS and presents a
			 * new request in time to enjoy timestamp
			 * back-shifting. The larger the budget of the
			 * queue is with respect to the service the
			 * queue actually requests in each service
			 * slot, the more times the queue can be
			 * reactivated with the same virtual finish
			 * time. It follows that, even if this finish
			 * time is pushed to the system virtual time
			 * to reduce the consequent timestamp
			 * misalignment, the queue unjustly enjoys for
			 * many re-activations a lower finish time
			 * than all newly activated queues.
			 *
			 * The service needed by bfqq is measured
			 * quite precisely by bfqq->entity.service.
			 * Since bfqq does not enjoy device idling,
			 * bfqq->entity.service is equal to the number
			 * of sectors that the process associated with
			 * bfqq requested to read/write before waiting
			 * for request completions, or blocking for
			 * other reasons.
			 */
			budget = max_t(int, bfqq->entity.service, min_budget);
			break;
		default:
			return;
		}
	} else if (!bfq_bfqq_sync(bfqq)) {
		/*
		 * Async queues get always the maximum possible
		 * budget, as for them we do not care about latency
		 * (in addition, their ability to dispatch is limited
		 * by the charging factor).
		 */
		budget = bfqd->bfq_max_budget;
	}

	bfqq->max_budget = budget;

	if (bfqd->budgets_assigned >= bfq_stats_min_budgets &&
	    !bfqd->bfq_user_max_budget)
		bfqq->max_budget = min(bfqq->max_budget, bfqd->bfq_max_budget);

	/*
	 * If there is still backlog, then assign a new budget, making
	 * sure that it is large enough for the next request.  Since
	 * the finish time of bfqq must be kept in sync with the
	 * budget, be sure to call __bfq_bfqq_expire() *after* this
	 * update.
	 *
	 * If there is no backlog, then no need to update the budget;
	 * it will be updated on the arrival of a new request.
	 */
	next_rq = bfqq->next_rq;
	if (next_rq)
		bfqq->entity.budget = max_t(unsigned long, bfqq->max_budget,
					    bfq_serv_to_charge(next_rq, bfqq));

	bfq_log_bfqq(bfqd, bfqq, "head sect: %u, new budget %d",
			next_rq ? blk_rq_sectors(next_rq) : 0,
			bfqq->entity.budget);
}

/*
 * Return true if the process associated with bfqq is "slow". The slow
 * flag is used, in addition to the budget timeout, to reduce the
 * amount of service provided to seeky processes, and thus reduce
 * their chances to lower the throughput. More details in the comments
 * on the function bfq_bfqq_expire().
 *
 * An important observation is in order: as discussed in the comments
 * on the function bfq_update_peak_rate(), with devices with internal
 * queues, it is hard if ever possible to know when and for how long
 * an I/O request is processed by the device (apart from the trivial
 * I/O pattern where a new request is dispatched only after the
 * previous one has been completed). This makes it hard to evaluate
 * the real rate at which the I/O requests of each bfq_queue are
 * served.  In fact, for an I/O scheduler like BFQ, serving a
 * bfq_queue means just dispatching its requests during its service
 * slot (i.e., until the budget of the queue is exhausted, or the
 * queue remains idle, or, finally, a timeout fires). But, during the
 * service slot of a bfq_queue, around 100 ms at most, the device may
 * be even still processing requests of bfq_queues served in previous
 * service slots. On the opposite end, the requests of the in-service
 * bfq_queue may be completed after the service slot of the queue
 * finishes.
 *
 * Anyway, unless more sophisticated solutions are used
 * (where possible), the sum of the sizes of the requests dispatched
 * during the service slot of a bfq_queue is probably the only
 * approximation available for the service received by the bfq_queue
 * during its service slot. And this sum is the quantity used in this
 * function to evaluate the I/O speed of a process.
 */
static bool bfq_bfqq_is_slow(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				 bool compensate, unsigned long *delta_ms)
{
	ktime_t delta_ktime;
	u32 delta_usecs;
	bool slow = BFQQ_SEEKY(bfqq); /* if delta too short, use seekyness */

	if (!bfq_bfqq_sync(bfqq))
		return false;

	if (compensate)
		delta_ktime = bfqd->last_idling_start;
	else
		delta_ktime = blk_time_get();
	delta_ktime = ktime_sub(delta_ktime, bfqd->last_budget_start);
	delta_usecs = ktime_to_us(delta_ktime);

	/* don't use too short time intervals */
	if (delta_usecs < 1000) {
		if (blk_queue_nonrot(bfqd->queue))
			 /*
			  * give same worst-case guarantees as idling
			  * for seeky
			  */
			*delta_ms = BFQ_MIN_TT / NSEC_PER_MSEC;
		else /* charge at least one seek */
			*delta_ms = bfq_slice_idle / NSEC_PER_MSEC;

		return slow;
	}

	*delta_ms = delta_usecs / USEC_PER_MSEC;

	/*
	 * Use only long (> 20ms) intervals to filter out excessive
	 * spikes in service rate estimation.
	 */
	if (delta_usecs > 20000) {
		/*
		 * Caveat for rotational devices: processes doing I/O
		 * in the slower disk zones tend to be slow(er) even
		 * if not seeky. In this respect, the estimated peak
		 * rate is likely to be an average over the disk
		 * surface. Accordingly, to not be too harsh with
		 * unlucky processes, a process is deemed slow only if
		 * its rate has been lower than half of the estimated
		 * peak rate.
		 */
		slow = bfqq->entity.service < bfqd->bfq_max_budget / 2;
	}

	bfq_log_bfqq(bfqd, bfqq, "bfq_bfqq_is_slow: slow %d", slow);

	return slow;
}

/*
 * To be deemed as soft real-time, an application must meet two
 * requirements. First, the application must not require an average
 * bandwidth higher than the approximate bandwidth required to playback or
 * record a compressed high-definition video.
 * The next function is invoked on the completion of the last request of a
 * batch, to compute the next-start time instant, soft_rt_next_start, such
 * that, if the next request of the application does not arrive before
 * soft_rt_next_start, then the above requirement on the bandwidth is met.
 *
 * The second requirement is that the request pattern of the application is
 * isochronous, i.e., that, after issuing a request or a batch of requests,
 * the application stops issuing new requests until all its pending requests
 * have been completed. After that, the application may issue a new batch,
 * and so on.
 * For this reason the next function is invoked to compute
 * soft_rt_next_start only for applications that meet this requirement,
 * whereas soft_rt_next_start is set to infinity for applications that do
 * not.
 *
 * Unfortunately, even a greedy (i.e., I/O-bound) application may
 * happen to meet, occasionally or systematically, both the above
 * bandwidth and isochrony requirements. This may happen at least in
 * the following circumstances. First, if the CPU load is high. The
 * application may stop issuing requests while the CPUs are busy
 * serving other processes, then restart, then stop again for a while,
 * and so on. The other circumstances are related to the storage
 * device: the storage device is highly loaded or reaches a low-enough
 * throughput with the I/O of the application (e.g., because the I/O
 * is random and/or the device is slow). In all these cases, the
 * I/O of the application may be simply slowed down enough to meet
 * the bandwidth and isochrony requirements. To reduce the probability
 * that greedy applications are deemed as soft real-time in these
 * corner cases, a further rule is used in the computation of
 * soft_rt_next_start: the return value of this function is forced to
 * be higher than the maximum between the following two quantities.
 *
 * (a) Current time plus: (1) the maximum time for which the arrival
 *     of a request is waited for when a sync queue becomes idle,
 *     namely bfqd->bfq_slice_idle, and (2) a few extra jiffies. We
 *     postpone for a moment the reason for adding a few extra
 *     jiffies; we get back to it after next item (b).  Lower-bounding
 *     the return value of this function with the current time plus
 *     bfqd->bfq_slice_idle tends to filter out greedy applications,
 *     because the latter issue their next request as soon as possible
 *     after the last one has been completed. In contrast, a soft
 *     real-time application spends some time processing data, after a
 *     batch of its requests has been completed.
 *
 * (b) Current value of bfqq->soft_rt_next_start. As pointed out
 *     above, greedy applications may happen to meet both the
 *     bandwidth and isochrony requirements under heavy CPU or
 *     storage-device load. In more detail, in these scenarios, these
 *     applications happen, only for limited time periods, to do I/O
 *     slowly enough to meet all the requirements described so far,
 *     including the filtering in above item (a). These slow-speed
 *     time intervals are usually interspersed between other time
 *     intervals during which these applications do I/O at a very high
 *     speed. Fortunately, exactly because of the high speed of the
 *     I/O in the high-speed intervals, the values returned by this
 *     function happen to be so high, near the end of any such
 *     high-speed interval, to be likely to fall *after* the end of
 *     the low-speed time interval that follows. These high values are
 *     stored in bfqq->soft_rt_next_start after each invocation of
 *     this function. As a consequence, if the last value of
 *     bfqq->soft_rt_next_start is constantly used to lower-bound the
 *     next value that this function may return, then, from the very
 *     beginning of a low-speed interval, bfqq->soft_rt_next_start is
 *     likely to be constantly kept so high that any I/O request
 *     issued during the low-speed interval is considered as arriving
 *     to soon for the application to be deemed as soft
 *     real-time. Then, in the high-speed interval that follows, the
 *     application will not be deemed as soft real-time, just because
 *     it will do I/O at a high speed. And so on.
 *
 * Getting back to the filtering in item (a), in the following two
 * cases this filtering might be easily passed by a greedy
 * application, if the reference quantity was just
 * bfqd->bfq_slice_idle:
 * 1) HZ is so low that the duration of a jiffy is comparable to or
 *    higher than bfqd->bfq_slice_idle. This happens, e.g., on slow
 *    devices with HZ=100. The time granularity may be so coarse
 *    that the approximation, in jiffies, of bfqd->bfq_slice_idle
 *    is rather lower than the exact value.
 * 2) jiffies, instead of increasing at a constant rate, may stop increasing
 *    for a while, then suddenly 'jump' by several units to recover the lost
 *    increments. This seems to happen, e.g., inside virtual machines.
 * To address this issue, in the filtering in (a) we do not use as a
 * reference time interval just bfqd->bfq_slice_idle, but
 * bfqd->bfq_slice_idle plus a few jiffies. In particular, we add the
 * minimum number of jiffies for which the filter seems to be quite
 * precise also in embedded systems and KVM/QEMU virtual machines.
 */
static unsigned long bfq_bfqq_softrt_next_start(struct bfq_data *bfqd,
						struct bfq_queue *bfqq)
{
	return max3(bfqq->soft_rt_next_start,
		    bfqq->last_idle_bklogged +
		    HZ * bfqq->service_from_backlogged /
		    bfqd->bfq_wr_max_softrt_rate,
		    jiffies + nsecs_to_jiffies(bfqq->bfqd->bfq_slice_idle) + 4);
}

/**
 * bfq_bfqq_expire - expire a queue.
 * @bfqd: device owning the queue.
 * @bfqq: the queue to expire.
 * @compensate: if true, compensate for the time spent idling.
 * @reason: the reason causing the expiration.
 *
 * If the process associated with bfqq does slow I/O (e.g., because it
 * issues random requests), we charge bfqq with the time it has been
 * in service instead of the service it has received (see
 * bfq_bfqq_charge_time for details on how this goal is achieved). As
 * a consequence, bfqq will typically get higher timestamps upon
 * reactivation, and hence it will be rescheduled as if it had
 * received more service than what it has actually received. In the
 * end, bfqq receives less service in proportion to how slowly its
 * associated process consumes its budgets (and hence how seriously it
 * tends to lower the throughput). In addition, this time-charging
 * strategy guarantees time fairness among slow processes. In
 * contrast, if the process associated with bfqq is not slow, we
 * charge bfqq exactly with the service it has received.
 *
 * Charging time to the first type of queues and the exact service to
 * the other has the effect of using the WF2Q+ policy to schedule the
 * former on a timeslice basis, without violating service domain
 * guarantees among the latter.
 */
void bfq_bfqq_expire(struct bfq_data *bfqd,
		     struct bfq_queue *bfqq,
		     bool compensate,
		     enum bfqq_expiration reason)
{
	bool slow;
	unsigned long delta = 0;
	struct bfq_entity *entity = &bfqq->entity;

	/*
	 * Check whether the process is slow (see bfq_bfqq_is_slow).
	 */
	slow = bfq_bfqq_is_slow(bfqd, bfqq, compensate, &delta);

	/*
	 * As above explained, charge slow (typically seeky) and
	 * timed-out queues with the time and not the service
	 * received, to favor sequential workloads.
	 *
	 * Processes doing I/O in the slower disk zones will tend to
	 * be slow(er) even if not seeky. Therefore, since the
	 * estimated peak rate is actually an average over the disk
	 * surface, these processes may timeout just for bad luck. To
	 * avoid punishing them, do not charge time to processes that
	 * succeeded in consuming at least 2/3 of their budget. This
	 * allows BFQ to preserve enough elasticity to still perform
	 * bandwidth, and not time, distribution with little unlucky
	 * or quasi-sequential processes.
	 */
	if (bfqq->wr_coeff == 1 &&
	    (slow ||
	     (reason == BFQQE_BUDGET_TIMEOUT &&
	      bfq_bfqq_budget_left(bfqq) >=  entity->budget / 3)))
		bfq_bfqq_charge_time(bfqd, bfqq, delta);

	if (bfqd->low_latency && bfqq->wr_coeff == 1)
		bfqq->last_wr_start_finish = jiffies;

	if (bfqd->low_latency && bfqd->bfq_wr_max_softrt_rate > 0 &&
	    RB_EMPTY_ROOT(&bfqq->sort_list)) {
		/*
		 * If we get here, and there are no outstanding
		 * requests, then the request pattern is isochronous
		 * (see the comments on the function
		 * bfq_bfqq_softrt_next_start()). Therefore we can
		 * compute soft_rt_next_start.
		 *
		 * If, instead, the queue still has outstanding
		 * requests, then we have to wait for the completion
		 * of all the outstanding requests to discover whether
		 * the request pattern is actually isochronous.
		 */
		if (bfqq->dispatched == 0)
			bfqq->soft_rt_next_start =
				bfq_bfqq_softrt_next_start(bfqd, bfqq);
		else if (bfqq->dispatched > 0) {
			/*
			 * Schedule an update of soft_rt_next_start to when
			 * the task may be discovered to be isochronous.
			 */
			bfq_mark_bfqq_softrt_update(bfqq);
		}
	}

	bfq_log_bfqq(bfqd, bfqq,
		"expire (%d, slow %d, num_disp %d, short_ttime %d)", reason,
		slow, bfqq->dispatched, bfq_bfqq_has_short_ttime(bfqq));

	/*
	 * bfqq expired, so no total service time needs to be computed
	 * any longer: reset state machine for measuring total service
	 * times.
	 */
	bfqd->rqs_injected = bfqd->wait_dispatch = false;
	bfqd->waited_rq = NULL;

	/*
	 * Increase, decrease or leave budget unchanged according to
	 * reason.
	 */
	__bfq_bfqq_recalc_budget(bfqd, bfqq, reason);
	if (__bfq_bfqq_expire(bfqd, bfqq, reason))
		/* bfqq is gone, no more actions on it */
		return;

	/* mark bfqq as waiting a request only if a bic still points to it */
	if (!bfq_bfqq_busy(bfqq) &&
	    reason != BFQQE_BUDGET_TIMEOUT &&
	    reason != BFQQE_BUDGET_EXHAUSTED) {
		bfq_mark_bfqq_non_blocking_wait_rq(bfqq);
		/*
		 * Not setting service to 0, because, if the next rq
		 * arrives in time, the queue will go on receiving
		 * service with this same budget (as if it never expired)
		 */
	} else
		entity->service = 0;

	/*
	 * Reset the received-service counter for every parent entity.
	 * Differently from what happens with bfqq->entity.service,
	 * the resetting of this counter never needs to be postponed
	 * for parent entities. In fact, in case bfqq may have a
	 * chance to go on being served using the last, partially
	 * consumed budget, bfqq->entity.service needs to be kept,
	 * because if bfqq then actually goes on being served using
	 * the same budget, the last value of bfqq->entity.service is
	 * needed to properly decrement bfqq->entity.budget by the
	 * portion already consumed. In contrast, it is not necessary
	 * to keep entity->service for parent entities too, because
	 * the bubble up of the new value of bfqq->entity.budget will
	 * make sure that the budgets of parent entities are correct,
	 * even in case bfqq and thus parent entities go on receiving
	 * service with the same budget.
	 */
	entity = entity->parent;
	for_each_entity(entity)
		entity->service = 0;
}

/*
 * Budget timeout is not implemented through a dedicated timer, but
 * just checked on request arrivals and completions, as well as on
 * idle timer expirations.
 */
static bool bfq_bfqq_budget_timeout(struct bfq_queue *bfqq)
{
	return time_is_before_eq_jiffies(bfqq->budget_timeout);
}

/*
 * If we expire a queue that is actively waiting (i.e., with the
 * device idled) for the arrival of a new request, then we may incur
 * the timestamp misalignment problem described in the body of the
 * function __bfq_activate_entity. Hence we return true only if this
 * condition does not hold, or if the queue is slow enough to deserve
 * only to be kicked off for preserving a high throughput.
 */
static bool bfq_may_expire_for_budg_timeout(struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq,
		"may_budget_timeout: wait_request %d left %d timeout %d",
		bfq_bfqq_wait_request(bfqq),
			bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3,
		bfq_bfqq_budget_timeout(bfqq));

	return (!bfq_bfqq_wait_request(bfqq) ||
		bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3)
		&&
		bfq_bfqq_budget_timeout(bfqq);
}

static bool idling_boosts_thr_without_issues(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq)
{
	bool rot_without_queueing =
		!blk_queue_nonrot(bfqd->queue) && !bfqd->hw_tag,
		bfqq_sequential_and_IO_bound,
		idling_boosts_thr;

	/* No point in idling for bfqq if it won't get requests any longer */
	if (unlikely(!bfqq_process_refs(bfqq)))
		return false;

	bfqq_sequential_and_IO_bound = !BFQQ_SEEKY(bfqq) &&
		bfq_bfqq_IO_bound(bfqq) && bfq_bfqq_has_short_ttime(bfqq);

	/*
	 * The next variable takes into account the cases where idling
	 * boosts the throughput.
	 *
	 * The value of the variable is computed considering, first, that
	 * idling is virtually always beneficial for the throughput if:
	 * (a) the device is not NCQ-capable and rotational, or
	 * (b) regardless of the presence of NCQ, the device is rotational and
	 *     the request pattern for bfqq is I/O-bound and sequential, or
	 * (c) regardless of whether it is rotational, the device is
	 *     not NCQ-capable and the request pattern for bfqq is
	 *     I/O-bound and sequential.
	 *
	 * Secondly, and in contrast to the above item (b), idling an
	 * NCQ-capable flash-based device would not boost the
	 * throughput even with sequential I/O; rather it would lower
	 * the throughput in proportion to how fast the device
	 * is. Accordingly, the next variable is true if any of the
	 * above conditions (a), (b) or (c) is true, and, in
	 * particular, happens to be false if bfqd is an NCQ-capable
	 * flash-based device.
	 */
	idling_boosts_thr = rot_without_queueing ||
		((!blk_queue_nonrot(bfqd->queue) || !bfqd->hw_tag) &&
		 bfqq_sequential_and_IO_bound);

	/*
	 * The return value of this function is equal to that of
	 * idling_boosts_thr, unless a special case holds. In this
	 * special case, described below, idling may cause problems to
	 * weight-raised queues.
	 *
	 * When the request pool is saturated (e.g., in the presence
	 * of write hogs), if the processes associated with
	 * non-weight-raised queues ask for requests at a lower rate,
	 * then processes associated with weight-raised queues have a
	 * higher probability to get a request from the pool
	 * immediately (or at least soon) when they need one. Thus
	 * they have a higher probability to actually get a fraction
	 * of the device throughput proportional to their high
	 * weight. This is especially true with NCQ-capable drives,
	 * which enqueue several requests in advance, and further
	 * reorder internally-queued requests.
	 *
	 * For this reason, we force to false the return value if
	 * there are weight-raised busy queues. In this case, and if
	 * bfqq is not weight-raised, this guarantees that the device
	 * is not idled for bfqq (if, instead, bfqq is weight-raised,
	 * then idling will be guaranteed by another variable, see
	 * below). Combined with the timestamping rules of BFQ (see
	 * [1] for details), this behavior causes bfqq, and hence any
	 * sync non-weight-raised queue, to get a lower number of
	 * requests served, and thus to ask for a lower number of
	 * requests from the request pool, before the busy
	 * weight-raised queues get served again. This often mitigates
	 * starvation problems in the presence of heavy write
	 * workloads and NCQ, thereby guaranteeing a higher
	 * application and system responsiveness in these hostile
	 * scenarios.
	 */
	return idling_boosts_thr &&
		bfqd->wr_busy_queues == 0;
}

/*
 * For a queue that becomes empty, device idling is allowed only if
 * this function returns true for that queue. As a consequence, since
 * device idling plays a critical role for both throughput boosting
 * and service guarantees, the return value of this function plays a
 * critical role as well.
 *
 * In a nutshell, this function returns true only if idling is
 * beneficial for throughput or, even if detrimental for throughput,
 * idling is however necessary to preserve service guarantees (low
 * latency, desired throughput distribution, ...). In particular, on
 * NCQ-capable devices, this function tries to return false, so as to
 * help keep the drives' internal queues full, whenever this helps the
 * device boost the throughput without causing any service-guarantee
 * issue.
 *
 * Most of the issues taken into account to get the return value of
 * this function are not trivial. We discuss these issues in the two
 * functions providing the main pieces of information needed by this
 * function.
 */
static bool bfq_better_to_idle(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;
	bool idling_boosts_thr_with_no_issue, idling_needed_for_service_guar;

	/* No point in idling for bfqq if it won't get requests any longer */
	if (unlikely(!bfqq_process_refs(bfqq)))
		return false;

	if (unlikely(bfqd->strict_guarantees))
		return true;

	/*
	 * Idling is performed only if slice_idle > 0. In addition, we
	 * do not idle if
	 * (a) bfqq is async
	 * (b) bfqq is in the idle io prio class: in this case we do
	 * not idle because we want to minimize the bandwidth that
	 * queues in this class can steal to higher-priority queues
	 */
	if (bfqd->bfq_slice_idle == 0 || !bfq_bfqq_sync(bfqq) ||
	   bfq_class_idle(bfqq))
		return false;

	idling_boosts_thr_with_no_issue =
		idling_boosts_thr_without_issues(bfqd, bfqq);

	idling_needed_for_service_guar =
		idling_needed_for_service_guarantees(bfqd, bfqq);

	/*
	 * We have now the two components we need to compute the
	 * return value of the function, which is true only if idling
	 * either boosts the throughput (without issues), or is
	 * necessary to preserve service guarantees.
	 */
	return idling_boosts_thr_with_no_issue ||
		idling_needed_for_service_guar;
}

/*
 * If the in-service queue is empty but the function bfq_better_to_idle
 * returns true, then:
 * 1) the queue must remain in service and cannot be expired, and
 * 2) the device must be idled to wait for the possible arrival of a new
 *    request for the queue.
 * See the comments on the function bfq_better_to_idle for the reasons
 * why performing device idling is the best choice to boost the throughput
 * and preserve service guarantees when bfq_better_to_idle itself
 * returns true.
 */
static bool bfq_bfqq_must_idle(struct bfq_queue *bfqq)
{
	return RB_EMPTY_ROOT(&bfqq->sort_list) && bfq_better_to_idle(bfqq);
}

/*
 * This function chooses the queue from which to pick the next extra
 * I/O request to inject, if it finds a compatible queue. See the
 * comments on bfq_update_inject_limit() for details on the injection
 * mechanism, and for the definitions of the quantities mentioned
 * below.
 */
static struct bfq_queue *
bfq_choose_bfqq_for_injection(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq, *in_serv_bfqq = bfqd->in_service_queue;
	unsigned int limit = in_serv_bfqq->inject_limit;
	int i;

	/*
	 * If
	 * - bfqq is not weight-raised and therefore does not carry
	 *   time-critical I/O,
	 * or
	 * - regardless of whether bfqq is weight-raised, bfqq has
	 *   however a long think time, during which it can absorb the
	 *   effect of an appropriate number of extra I/O requests
	 *   from other queues (see bfq_update_inject_limit for
	 *   details on the computation of this number);
	 * then injection can be performed without restrictions.
	 */
	bool in_serv_always_inject = in_serv_bfqq->wr_coeff == 1 ||
		!bfq_bfqq_has_short_ttime(in_serv_bfqq);

	/*
	 * If
	 * - the baseline total service time could not be sampled yet,
	 *   so the inject limit happens to be still 0, and
	 * - a lot of time has elapsed since the plugging of I/O
	 *   dispatching started, so drive speed is being wasted
	 *   significantly;
	 * then temporarily raise inject limit to one request.
	 */
	if (limit == 0 && in_serv_bfqq->last_serv_time_ns == 0 &&
	    bfq_bfqq_wait_request(in_serv_bfqq) &&
	    time_is_before_eq_jiffies(bfqd->last_idling_start_jiffies +
				      bfqd->bfq_slice_idle)
		)
		limit = 1;

	if (bfqd->tot_rq_in_driver >= limit)
		return NULL;

	/*
	 * Linear search of the source queue for injection; but, with
	 * a high probability, very few steps are needed to find a
	 * candidate queue, i.e., a queue with enough budget left for
	 * its next request. In fact:
	 * - BFQ dynamically updates the budget of every queue so as
	 *   to accommodate the expected backlog of the queue;
	 * - if a queue gets all its requests dispatched as injected
	 *   service, then the queue is removed from the active list
	 *   (and re-added only if it gets new requests, but then it
	 *   is assigned again enough budget for its new backlog).
	 */
	for (i = 0; i < bfqd->num_actuators; i++) {
		list_for_each_entry(bfqq, &bfqd->active_list[i], bfqq_list)
			if (!RB_EMPTY_ROOT(&bfqq->sort_list) &&
				(in_serv_always_inject || bfqq->wr_coeff > 1) &&
				bfq_serv_to_charge(bfqq->next_rq, bfqq) <=
				bfq_bfqq_budget_left(bfqq)) {
			/*
			 * Allow for only one large in-flight request
			 * on non-rotational devices, for the
			 * following reason. On non-rotationl drives,
			 * large requests take much longer than
			 * smaller requests to be served. In addition,
			 * the drive prefers to serve large requests
			 * w.r.t. to small ones, if it can choose. So,
			 * having more than one large requests queued
			 * in the drive may easily make the next first
			 * request of the in-service queue wait for so
			 * long to break bfqq's service guarantees. On
			 * the bright side, large requests let the
			 * drive reach a very high throughput, even if
			 * there is only one in-flight large request
			 * at a time.
			 */
			if (blk_queue_nonrot(bfqd->queue) &&
			    blk_rq_sectors(bfqq->next_rq) >=
			    BFQQ_SECT_THR_NONROT &&
			    bfqd->tot_rq_in_driver >= 1)
				continue;
			else {
				bfqd->rqs_injected = true;
				return bfqq;
			}
		}
	}

	return NULL;
}

static struct bfq_queue *
bfq_find_active_bfqq_for_actuator(struct bfq_data *bfqd, int idx)
{
	struct bfq_queue *bfqq;

	if (bfqd->in_service_queue &&
	    bfqd->in_service_queue->actuator_idx == idx)
		return bfqd->in_service_queue;

	list_for_each_entry(bfqq, &bfqd->active_list[idx], bfqq_list) {
		if (!RB_EMPTY_ROOT(&bfqq->sort_list) &&
			bfq_serv_to_charge(bfqq->next_rq, bfqq) <=
				bfq_bfqq_budget_left(bfqq)) {
			return bfqq;
		}
	}

	return NULL;
}

/*
 * Perform a linear scan of each actuator, until an actuator is found
 * for which the following three conditions hold: the load of the
 * actuator is below the threshold (see comments on
 * actuator_load_threshold for details) and lower than that of the
 * next actuator (comments on this extra condition below), and there
 * is a queue that contains I/O for that actuator. On success, return
 * that queue.
 *
 * Performing a plain linear scan entails a prioritization among
 * actuators. The extra condition above breaks this prioritization and
 * tends to distribute injection uniformly across actuators.
 */
static struct bfq_queue *
bfq_find_bfqq_for_underused_actuator(struct bfq_data *bfqd)
{
	int i;

	for (i = 0 ; i < bfqd->num_actuators; i++) {
		if (bfqd->rq_in_driver[i] < bfqd->actuator_load_threshold &&
		    (i == bfqd->num_actuators - 1 ||
		     bfqd->rq_in_driver[i] < bfqd->rq_in_driver[i+1])) {
			struct bfq_queue *bfqq =
				bfq_find_active_bfqq_for_actuator(bfqd, i);

			if (bfqq)
				return bfqq;
		}
	}

	return NULL;
}


/*
 * Select a queue for service.  If we have a current queue in service,
 * check whether to continue servicing it, or retrieve and set a new one.
 */
static struct bfq_queue *bfq_select_queue(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq, *inject_bfqq;
	struct request *next_rq;
	enum bfqq_expiration reason = BFQQE_BUDGET_TIMEOUT;

	bfqq = bfqd->in_service_queue;
	if (!bfqq)
		goto new_queue;

	bfq_log_bfqq(bfqd, bfqq, "select_queue: already in-service queue");

	/*
	 * Do not expire bfqq for budget timeout if bfqq may be about
	 * to enjoy device idling. The reason why, in this case, we
	 * prevent bfqq from expiring is the same as in the comments
	 * on the case where bfq_bfqq_must_idle() returns true, in
	 * bfq_completed_request().
	 */
	if (bfq_may_expire_for_budg_timeout(bfqq) &&
	    !bfq_bfqq_must_idle(bfqq))
		goto expire;

check_queue:
	/*
	 *  If some actuator is underutilized, but the in-service
	 *  queue does not contain I/O for that actuator, then try to
	 *  inject I/O for that actuator.
	 */
	inject_bfqq = bfq_find_bfqq_for_underused_actuator(bfqd);
	if (inject_bfqq && inject_bfqq != bfqq)
		return inject_bfqq;

	/*
	 * This loop is rarely executed more than once. Even when it
	 * happens, it is much more convenient to re-execute this loop
	 * than to return NULL and trigger a new dispatch to get a
	 * request served.
	 */
	next_rq = bfqq->next_rq;
	/*
	 * If bfqq has requests queued and it has enough budget left to
	 * serve them, keep the queue, otherwise expire it.
	 */
	if (next_rq) {
		if (bfq_serv_to_charge(next_rq, bfqq) >
			bfq_bfqq_budget_left(bfqq)) {
			/*
			 * Expire the queue for budget exhaustion,
			 * which makes sure that the next budget is
			 * enough to serve the next request, even if
			 * it comes from the fifo expired path.
			 */
			reason = BFQQE_BUDGET_EXHAUSTED;
			goto expire;
		} else {
			/*
			 * The idle timer may be pending because we may
			 * not disable disk idling even when a new request
			 * arrives.
			 */
			if (bfq_bfqq_wait_request(bfqq)) {
				/*
				 * If we get here: 1) at least a new request
				 * has arrived but we have not disabled the
				 * timer because the request was too small,
				 * 2) then the block layer has unplugged
				 * the device, causing the dispatch to be
				 * invoked.
				 *
				 * Since the device is unplugged, now the
				 * requests are probably large enough to
				 * provide a reasonable throughput.
				 * So we disable idling.
				 */
				bfq_clear_bfqq_wait_request(bfqq);
				hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
			}
			goto keep_queue;
		}
	}

	/*
	 * No requests pending. However, if the in-service queue is idling
	 * for a new request, or has requests waiting for a completion and
	 * may idle after their completion, then keep it anyway.
	 *
	 * Yet, inject service from other queues if it boosts
	 * throughput and is possible.
	 */
	if (bfq_bfqq_wait_request(bfqq) ||
	    (bfqq->dispatched != 0 && bfq_better_to_idle(bfqq))) {
		unsigned int act_idx = bfqq->actuator_idx;
		struct bfq_queue *async_bfqq = NULL;
		struct bfq_queue *blocked_bfqq =
			!hlist_empty(&bfqq->woken_list) ?
			container_of(bfqq->woken_list.first,
				     struct bfq_queue,
				     woken_list_node)
			: NULL;

		if (bfqq->bic && bfqq->bic->bfqq[0][act_idx] &&
		    bfq_bfqq_busy(bfqq->bic->bfqq[0][act_idx]) &&
		    bfqq->bic->bfqq[0][act_idx]->next_rq)
			async_bfqq = bfqq->bic->bfqq[0][act_idx];
		/*
		 * The next four mutually-exclusive ifs decide
		 * whether to try injection, and choose the queue to
		 * pick an I/O request from.
		 *
		 * The first if checks whether the process associated
		 * with bfqq has also async I/O pending. If so, it
		 * injects such I/O unconditionally. Injecting async
		 * I/O from the same process can cause no harm to the
		 * process. On the contrary, it can only increase
		 * bandwidth and reduce latency for the process.
		 *
		 * The second if checks whether there happens to be a
		 * non-empty waker queue for bfqq, i.e., a queue whose
		 * I/O needs to be completed for bfqq to receive new
		 * I/O. This happens, e.g., if bfqq is associated with
		 * a process that does some sync. A sync generates
		 * extra blocking I/O, which must be completed before
		 * the process associated with bfqq can go on with its
		 * I/O. If the I/O of the waker queue is not served,
		 * then bfqq remains empty, and no I/O is dispatched,
		 * until the idle timeout fires for bfqq. This is
		 * likely to result in lower bandwidth and higher
		 * latencies for bfqq, and in a severe loss of total
		 * throughput. The best action to take is therefore to
		 * serve the waker queue as soon as possible. So do it
		 * (without relying on the third alternative below for
		 * eventually serving waker_bfqq's I/O; see the last
		 * paragraph for further details). This systematic
		 * injection of I/O from the waker queue does not
		 * cause any delay to bfqq's I/O. On the contrary,
		 * next bfqq's I/O is brought forward dramatically,
		 * for it is not blocked for milliseconds.
		 *
		 * The third if checks whether there is a queue woken
		 * by bfqq, and currently with pending I/O. Such a
		 * woken queue does not steal bandwidth from bfqq,
		 * because it remains soon without I/O if bfqq is not
		 * served. So there is virtually no risk of loss of
		 * bandwidth for bfqq if this woken queue has I/O
		 * dispatched while bfqq is waiting for new I/O.
		 *
		 * The fourth if checks whether bfqq is a queue for
		 * which it is better to avoid injection. It is so if
		 * bfqq delivers more throughput when served without
		 * any further I/O from other queues in the middle, or
		 * if the service times of bfqq's I/O requests both
		 * count more than overall throughput, and may be
		 * easily increased by injection (this happens if bfqq
		 * has a short think time). If none of these
		 * conditions holds, then a candidate queue for
		 * injection is looked for through
		 * bfq_choose_bfqq_for_injection(). Note that the
		 * latter may return NULL (for example if the inject
		 * limit for bfqq is currently 0).
		 *
		 * NOTE: motivation for the second alternative
		 *
		 * Thanks to the way the inject limit is updated in
		 * bfq_update_has_short_ttime(), it is rather likely
		 * that, if I/O is being plugged for bfqq and the
		 * waker queue has pending I/O requests that are
		 * blocking bfqq's I/O, then the fourth alternative
		 * above lets the waker queue get served before the
		 * I/O-plugging timeout fires. So one may deem the
		 * second alternative superfluous. It is not, because
		 * the fourth alternative may be way less effective in
		 * case of a synchronization. For two main
		 * reasons. First, throughput may be low because the
		 * inject limit may be too low to guarantee the same
		 * amount of injected I/O, from the waker queue or
		 * other queues, that the second alternative
		 * guarantees (the second alternative unconditionally
		 * injects a pending I/O request of the waker queue
		 * for each bfq_dispatch_request()). Second, with the
		 * fourth alternative, the duration of the plugging,
		 * i.e., the time before bfqq finally receives new I/O,
		 * may not be minimized, because the waker queue may
		 * happen to be served only after other queues.
		 */
		if (async_bfqq &&
		    icq_to_bic(async_bfqq->next_rq->elv.icq) == bfqq->bic &&
		    bfq_serv_to_charge(async_bfqq->next_rq, async_bfqq) <=
		    bfq_bfqq_budget_left(async_bfqq))
			bfqq = async_bfqq;
		else if (bfqq->waker_bfqq &&
			   bfq_bfqq_busy(bfqq->waker_bfqq) &&
			   bfqq->waker_bfqq->next_rq &&
			   bfq_serv_to_charge(bfqq->waker_bfqq->next_rq,
					      bfqq->waker_bfqq) <=
			   bfq_bfqq_budget_left(bfqq->waker_bfqq)
			)
			bfqq = bfqq->waker_bfqq;
		else if (blocked_bfqq &&
			   bfq_bfqq_busy(blocked_bfqq) &&
			   blocked_bfqq->next_rq &&
			   bfq_serv_to_charge(blocked_bfqq->next_rq,
					      blocked_bfqq) <=
			   bfq_bfqq_budget_left(blocked_bfqq)
			)
			bfqq = blocked_bfqq;
		else if (!idling_boosts_thr_without_issues(bfqd, bfqq) &&
			 (bfqq->wr_coeff == 1 || bfqd->wr_busy_queues > 1 ||
			  !bfq_bfqq_has_short_ttime(bfqq)))
			bfqq = bfq_choose_bfqq_for_injection(bfqd);
		else
			bfqq = NULL;

		goto keep_queue;
	}

	reason = BFQQE_NO_MORE_REQUESTS;
expire:
	bfq_bfqq_expire(bfqd, bfqq, false, reason);
new_queue:
	bfqq = bfq_set_in_service_queue(bfqd);
	if (bfqq) {
		bfq_log_bfqq(bfqd, bfqq, "select_queue: checking new queue");
		goto check_queue;
	}
keep_queue:
	if (bfqq)
		bfq_log_bfqq(bfqd, bfqq, "select_queue: returned this queue");
	else
		bfq_log(bfqd, "select_queue: no queue returned");

	return bfqq;
}

static void bfq_update_wr_data(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	if (bfqq->wr_coeff > 1) { /* queue is being weight-raised */
		bfq_log_bfqq(bfqd, bfqq,
			"raising period dur %u/%u msec, old coeff %u, w %d(%d)",
			jiffies_to_msecs(jiffies - bfqq->last_wr_start_finish),
			jiffies_to_msecs(bfqq->wr_cur_max_time),
			bfqq->wr_coeff,
			bfqq->entity.weight, bfqq->entity.orig_weight);

		if (entity->prio_changed)
			bfq_log_bfqq(bfqd, bfqq, "WARN: pending prio change");

		/*
		 * If the queue was activated in a burst, or too much
		 * time has elapsed from the beginning of this
		 * weight-raising period, then end weight raising.
		 */
		if (bfq_bfqq_in_large_burst(bfqq))
			bfq_bfqq_end_wr(bfqq);
		else if (time_is_before_jiffies(bfqq->last_wr_start_finish +
						bfqq->wr_cur_max_time)) {
			if (bfqq->wr_cur_max_time != bfqd->bfq_wr_rt_max_time ||
			time_is_before_jiffies(bfqq->wr_start_at_switch_to_srt +
					       bfq_wr_duration(bfqd))) {
				/*
				 * Either in interactive weight
				 * raising, or in soft_rt weight
				 * raising with the
				 * interactive-weight-raising period
				 * elapsed (so no switch back to
				 * interactive weight raising).
				 */
				bfq_bfqq_end_wr(bfqq);
			} else { /*
				  * soft_rt finishing while still in
				  * interactive period, switch back to
				  * interactive weight raising
				  */
				switch_back_to_interactive_wr(bfqq, bfqd);
				bfqq->entity.prio_changed = 1;
			}
		}
		if (bfqq->wr_coeff > 1 &&
		    bfqq->wr_cur_max_time != bfqd->bfq_wr_rt_max_time &&
		    bfqq->service_from_wr > max_service_from_wr) {
			/* see comments on max_service_from_wr */
			bfq_bfqq_end_wr(bfqq);
		}
	}
	/*
	 * To improve latency (for this or other queues), immediately
	 * update weight both if it must be raised and if it must be
	 * lowered. Since, entity may be on some active tree here, and
	 * might have a pending change of its ioprio class, invoke
	 * next function with the last parameter unset (see the
	 * comments on the function).
	 */
	if ((entity->weight > entity->orig_weight) != (bfqq->wr_coeff > 1))
		__bfq_entity_update_weight_prio(bfq_entity_service_tree(entity),
						entity, false);
}

/*
 * Dispatch next request from bfqq.
 */
static struct request *bfq_dispatch_rq_from_bfqq(struct bfq_data *bfqd,
						 struct bfq_queue *bfqq)
{
	struct request *rq = bfqq->next_rq;
	unsigned long service_to_charge;

	service_to_charge = bfq_serv_to_charge(rq, bfqq);

	bfq_bfqq_served(bfqq, service_to_charge);

	if (bfqq == bfqd->in_service_queue && bfqd->wait_dispatch) {
		bfqd->wait_dispatch = false;
		bfqd->waited_rq = rq;
	}

	bfq_dispatch_remove(bfqd->queue, rq);

	if (bfqq != bfqd->in_service_queue)
		return rq;

	/*
	 * If weight raising has to terminate for bfqq, then next
	 * function causes an immediate update of bfqq's weight,
	 * without waiting for next activation. As a consequence, on
	 * expiration, bfqq will be timestamped as if has never been
	 * weight-raised during this service slot, even if it has
	 * received part or even most of the service as a
	 * weight-raised queue. This inflates bfqq's timestamps, which
	 * is beneficial, as bfqq is then more willing to leave the
	 * device immediately to possible other weight-raised queues.
	 */
	bfq_update_wr_data(bfqd, bfqq);

	/*
	 * Expire bfqq, pretending that its budget expired, if bfqq
	 * belongs to CLASS_IDLE and other queues are waiting for
	 * service.
	 */
	if (bfq_tot_busy_queues(bfqd) > 1 && bfq_class_idle(bfqq))
		bfq_bfqq_expire(bfqd, bfqq, false, BFQQE_BUDGET_EXHAUSTED);

	return rq;
}

static bool bfq_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;

	/*
	 * Avoiding lock: a race on bfqd->queued should cause at
	 * most a call to dispatch for nothing
	 */
	return !list_empty_careful(&bfqd->dispatch) ||
		READ_ONCE(bfqd->queued);
}

static struct request *__bfq_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;
	struct request *rq = NULL;
	struct bfq_queue *bfqq = NULL;

	if (!list_empty(&bfqd->dispatch)) {
		rq = list_first_entry(&bfqd->dispatch, struct request,
				      queuelist);
		list_del_init(&rq->queuelist);

		bfqq = RQ_BFQQ(rq);

		if (bfqq) {
			/*
			 * Increment counters here, because this
			 * dispatch does not follow the standard
			 * dispatch flow (where counters are
			 * incremented)
			 */
			bfqq->dispatched++;

			goto inc_in_driver_start_rq;
		}

		/*
		 * We exploit the bfq_finish_requeue_request hook to
		 * decrement tot_rq_in_driver, but
		 * bfq_finish_requeue_request will not be invoked on
		 * this request. So, to avoid unbalance, just start
		 * this request, without incrementing tot_rq_in_driver. As
		 * a negative consequence, tot_rq_in_driver is deceptively
		 * lower than it should be while this request is in
		 * service. This may cause bfq_schedule_dispatch to be
		 * invoked uselessly.
		 *
		 * As for implementing an exact solution, the
		 * bfq_finish_requeue_request hook, if defined, is
		 * probably invoked also on this request. So, by
		 * exploiting this hook, we could 1) increment
		 * tot_rq_in_driver here, and 2) decrement it in
		 * bfq_finish_requeue_request. Such a solution would
		 * let the value of the counter be always accurate,
		 * but it would entail using an extra interface
		 * function. This cost seems higher than the benefit,
		 * being the frequency of non-elevator-private
		 * requests very low.
		 */
		goto start_rq;
	}

	bfq_log(bfqd, "dispatch requests: %d busy queues",
		bfq_tot_busy_queues(bfqd));

	if (bfq_tot_busy_queues(bfqd) == 0)
		goto exit;

	/*
	 * Force device to serve one request at a time if
	 * strict_guarantees is true. Forcing this service scheme is
	 * currently the ONLY way to guarantee that the request
	 * service order enforced by the scheduler is respected by a
	 * queueing device. Otherwise the device is free even to make
	 * some unlucky request wait for as long as the device
	 * wishes.
	 *
	 * Of course, serving one request at a time may cause loss of
	 * throughput.
	 */
	if (bfqd->strict_guarantees && bfqd->tot_rq_in_driver > 0)
		goto exit;

	bfqq = bfq_select_queue(bfqd);
	if (!bfqq)
		goto exit;

	rq = bfq_dispatch_rq_from_bfqq(bfqd, bfqq);

	if (rq) {
inc_in_driver_start_rq:
		bfqd->rq_in_driver[bfqq->actuator_idx]++;
		bfqd->tot_rq_in_driver++;
start_rq:
		rq->rq_flags |= RQF_STARTED;
	}
exit:
	return rq;
}

#ifdef CONFIG_BFQ_CGROUP_DEBUG
static void bfq_update_dispatch_stats(struct request_queue *q,
				      struct request *rq,
				      struct bfq_queue *in_serv_queue,
				      bool idle_timer_disabled)
{
	struct bfq_queue *bfqq = rq ? RQ_BFQQ(rq) : NULL;

	if (!idle_timer_disabled && !bfqq)
		return;

	/*
	 * rq and bfqq are guaranteed to exist until this function
	 * ends, for the following reasons. First, rq can be
	 * dispatched to the device, and then can be completed and
	 * freed, only after this function ends. Second, rq cannot be
	 * merged (and thus freed because of a merge) any longer,
	 * because it has already started. Thus rq cannot be freed
	 * before this function ends, and, since rq has a reference to
	 * bfqq, the same guarantee holds for bfqq too.
	 *
	 * In addition, the following queue lock guarantees that
	 * bfqq_group(bfqq) exists as well.
	 */
	spin_lock_irq(&q->queue_lock);
	if (idle_timer_disabled)
		/*
		 * Since the idle timer has been disabled,
		 * in_serv_queue contained some request when
		 * __bfq_dispatch_request was invoked above, which
		 * implies that rq was picked exactly from
		 * in_serv_queue. Thus in_serv_queue == bfqq, and is
		 * therefore guaranteed to exist because of the above
		 * arguments.
		 */
		bfqg_stats_update_idle_time(bfqq_group(in_serv_queue));
	if (bfqq) {
		struct bfq_group *bfqg = bfqq_group(bfqq);

		bfqg_stats_update_avg_queue_size(bfqg);
		bfqg_stats_set_start_empty_time(bfqg);
		bfqg_stats_update_io_remove(bfqg, rq->cmd_flags);
	}
	spin_unlock_irq(&q->queue_lock);
}
#else
static inline void bfq_update_dispatch_stats(struct request_queue *q,
					     struct request *rq,
					     struct bfq_queue *in_serv_queue,
					     bool idle_timer_disabled) {}
#endif /* CONFIG_BFQ_CGROUP_DEBUG */

static struct request *bfq_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;
	struct request *rq;
	struct bfq_queue *in_serv_queue;
	bool waiting_rq, idle_timer_disabled = false;

	spin_lock_irq(&bfqd->lock);

	in_serv_queue = bfqd->in_service_queue;
	waiting_rq = in_serv_queue && bfq_bfqq_wait_request(in_serv_queue);

	rq = __bfq_dispatch_request(hctx);
	if (in_serv_queue == bfqd->in_service_queue) {
		idle_timer_disabled =
			waiting_rq && !bfq_bfqq_wait_request(in_serv_queue);
	}

	spin_unlock_irq(&bfqd->lock);
	bfq_update_dispatch_stats(hctx->queue, rq,
			idle_timer_disabled ? in_serv_queue : NULL,
				idle_timer_disabled);

	return rq;
}

/*
 * Task holds one reference to the queue, dropped when task exits.  Each rq
 * in-flight on this queue also holds a reference, dropped when rq is freed.
 *
 * Scheduler lock must be held here. Recall not to use bfqq after calling
 * this function on it.
 */
void bfq_put_queue(struct bfq_queue *bfqq)
{
	struct bfq_queue *item;
	struct hlist_node *n;
	struct bfq_group *bfqg = bfqq_group(bfqq);

	bfq_log_bfqq(bfqq->bfqd, bfqq, "put_queue: %p %d", bfqq, bfqq->ref);

	bfqq->ref--;
	if (bfqq->ref)
		return;

	if (!hlist_unhashed(&bfqq->burst_list_node)) {
		hlist_del_init(&bfqq->burst_list_node);
		/*
		 * Decrement also burst size after the removal, if the
		 * process associated with bfqq is exiting, and thus
		 * does not contribute to the burst any longer. This
		 * decrement helps filter out false positives of large
		 * bursts, when some short-lived process (often due to
		 * the execution of commands by some service) happens
		 * to start and exit while a complex application is
		 * starting, and thus spawning several processes that
		 * do I/O (and that *must not* be treated as a large
		 * burst, see comments on bfq_handle_burst).
		 *
		 * In particular, the decrement is performed only if:
		 * 1) bfqq is not a merged queue, because, if it is,
		 * then this free of bfqq is not triggered by the exit
		 * of the process bfqq is associated with, but exactly
		 * by the fact that bfqq has just been merged.
		 * 2) burst_size is greater than 0, to handle
		 * unbalanced decrements. Unbalanced decrements may
		 * happen in te following case: bfqq is inserted into
		 * the current burst list--without incrementing
		 * bust_size--because of a split, but the current
		 * burst list is not the burst list bfqq belonged to
		 * (see comments on the case of a split in
		 * bfq_set_request).
		 */
		if (bfqq->bic && bfqq->bfqd->burst_size > 0)
			bfqq->bfqd->burst_size--;
	}

	/*
	 * bfqq does not exist any longer, so it cannot be woken by
	 * any other queue, and cannot wake any other queue. Then bfqq
	 * must be removed from the woken list of its possible waker
	 * queue, and all queues in the woken list of bfqq must stop
	 * having a waker queue. Strictly speaking, these updates
	 * should be performed when bfqq remains with no I/O source
	 * attached to it, which happens before bfqq gets freed. In
	 * particular, this happens when the last process associated
	 * with bfqq exits or gets associated with a different
	 * queue. However, both events lead to bfqq being freed soon,
	 * and dangling references would come out only after bfqq gets
	 * freed. So these updates are done here, as a simple and safe
	 * way to handle all cases.
	 */
	/* remove bfqq from woken list */
	if (!hlist_unhashed(&bfqq->woken_list_node))
		hlist_del_init(&bfqq->woken_list_node);

	/* reset waker for all queues in woken list */
	hlist_for_each_entry_safe(item, n, &bfqq->woken_list,
				  woken_list_node) {
		item->waker_bfqq = NULL;
		hlist_del_init(&item->woken_list_node);
	}

	if (bfqq->bfqd->last_completed_rq_bfqq == bfqq)
		bfqq->bfqd->last_completed_rq_bfqq = NULL;

	WARN_ON_ONCE(!list_empty(&bfqq->fifo));
	WARN_ON_ONCE(!RB_EMPTY_ROOT(&bfqq->sort_list));
	WARN_ON_ONCE(bfqq->dispatched);

	kmem_cache_free(bfq_pool, bfqq);
	bfqg_and_blkg_put(bfqg);
}

static void bfq_put_stable_ref(struct bfq_queue *bfqq)
{
	bfqq->stable_ref--;
	bfq_put_queue(bfqq);
}

void bfq_put_cooperator(struct bfq_queue *bfqq)
{
	struct bfq_queue *__bfqq, *next;

	/*
	 * If this queue was scheduled to merge with another queue, be
	 * sure to drop the reference taken on that queue (and others in
	 * the merge chain). See bfq_setup_merge and bfq_merge_bfqqs.
	 */
	__bfqq = bfqq->new_bfqq;
	while (__bfqq) {
		next = __bfqq->new_bfqq;
		bfq_put_queue(__bfqq);
		__bfqq = next;
	}
}

static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq == bfqd->in_service_queue) {
		__bfq_bfqq_expire(bfqd, bfqq, BFQQE_BUDGET_TIMEOUT);
		bfq_schedule_dispatch(bfqd);
	}

	bfq_log_bfqq(bfqd, bfqq, "exit_bfqq: %p, %d", bfqq, bfqq->ref);

	bfq_put_cooperator(bfqq);

	bfq_release_process_ref(bfqd, bfqq);
}

static void bfq_exit_icq_bfqq(struct bfq_io_cq *bic, bool is_sync,
			      unsigned int actuator_idx)
{
	struct bfq_queue *bfqq = bic_to_bfqq(bic, is_sync, actuator_idx);
	struct bfq_data *bfqd;

	if (bfqq)
		bfqd = bfqq->bfqd; /* NULL if scheduler already exited */

	if (bfqq && bfqd) {
		bic_set_bfqq(bic, NULL, is_sync, actuator_idx);
		bfq_exit_bfqq(bfqd, bfqq);
	}
}

static void _bfq_exit_icq(struct bfq_io_cq *bic, unsigned int num_actuators)
{
	struct bfq_iocq_bfqq_data *bfqq_data = bic->bfqq_data;
	unsigned int act_idx;

	for (act_idx = 0; act_idx < num_actuators; act_idx++) {
		if (bfqq_data[act_idx].stable_merge_bfqq)
			bfq_put_stable_ref(bfqq_data[act_idx].stable_merge_bfqq);

		bfq_exit_icq_bfqq(bic, true, act_idx);
		bfq_exit_icq_bfqq(bic, false, act_idx);
	}
}

static void bfq_exit_icq(struct io_cq *icq)
{
	struct bfq_io_cq *bic = icq_to_bic(icq);
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	unsigned long flags;

	/*
	 * If bfqd and thus bfqd->num_actuators is not available any
	 * longer, then cycle over all possible per-actuator bfqqs in
	 * next loop. We rely on bic being zeroed on creation, and
	 * therefore on its unused per-actuator fields being NULL.
	 *
	 * bfqd is NULL if scheduler already exited, and in that case
	 * this is the last time these queues are accessed.
	 */
	if (bfqd) {
		spin_lock_irqsave(&bfqd->lock, flags);
		_bfq_exit_icq(bic, bfqd->num_actuators);
		spin_unlock_irqrestore(&bfqd->lock, flags);
	} else {
		_bfq_exit_icq(bic, BFQ_MAX_ACTUATORS);
	}
}

/*
 * Update the entity prio values; note that the new values will not
 * be used until the next (re)activation.
 */
static void
bfq_set_next_ioprio_data(struct bfq_queue *bfqq, struct bfq_io_cq *bic)
{
	struct task_struct *tsk = current;
	int ioprio_class;
	struct bfq_data *bfqd = bfqq->bfqd;

	if (!bfqd)
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	switch (ioprio_class) {
	default:
		pr_err("bdi %s: bfq: bad prio class %d\n",
			bdi_dev_name(bfqq->bfqd->queue->disk->bdi),
			ioprio_class);
		fallthrough;
	case IOPRIO_CLASS_NONE:
		/*
		 * No prio set, inherit CPU scheduling settings.
		 */
		bfqq->new_ioprio = task_nice_ioprio(tsk);
		bfqq->new_ioprio_class = task_nice_ioclass(tsk);
		break;
	case IOPRIO_CLASS_RT:
		bfqq->new_ioprio = IOPRIO_PRIO_LEVEL(bic->ioprio);
		bfqq->new_ioprio_class = IOPRIO_CLASS_RT;
		break;
	case IOPRIO_CLASS_BE:
		bfqq->new_ioprio = IOPRIO_PRIO_LEVEL(bic->ioprio);
		bfqq->new_ioprio_class = IOPRIO_CLASS_BE;
		break;
	case IOPRIO_CLASS_IDLE:
		bfqq->new_ioprio_class = IOPRIO_CLASS_IDLE;
		bfqq->new_ioprio = IOPRIO_NR_LEVELS - 1;
		break;
	}

	if (bfqq->new_ioprio >= IOPRIO_NR_LEVELS) {
		pr_crit("bfq_set_next_ioprio_data: new_ioprio %d\n",
			bfqq->new_ioprio);
		bfqq->new_ioprio = IOPRIO_NR_LEVELS - 1;
	}

	bfqq->entity.new_weight = bfq_ioprio_to_weight(bfqq->new_ioprio);
	bfq_log_bfqq(bfqd, bfqq, "new_ioprio %d new_weight %d",
		     bfqq->new_ioprio, bfqq->entity.new_weight);
	bfqq->entity.prio_changed = 1;
}

static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, bool is_sync,
				       struct bfq_io_cq *bic,
				       bool respawn);

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	struct bfq_queue *bfqq;
	int ioprio = bic->icq.ioc->ioprio;

	/*
	 * This condition may trigger on a newly created bic, be sure to
	 * drop the lock before returning.
	 */
	if (unlikely(!bfqd) || likely(bic->ioprio == ioprio))
		return;

	bic->ioprio = ioprio;

	bfqq = bic_to_bfqq(bic, false, bfq_actuator_index(bfqd, bio));
	if (bfqq) {
		struct bfq_queue *old_bfqq = bfqq;

		bfqq = bfq_get_queue(bfqd, bio, false, bic, true);
		bic_set_bfqq(bic, bfqq, false, bfq_actuator_index(bfqd, bio));
		bfq_release_process_ref(bfqd, old_bfqq);
	}

	bfqq = bic_to_bfqq(bic, true, bfq_actuator_index(bfqd, bio));
	if (bfqq)
		bfq_set_next_ioprio_data(bfqq, bic);
}

static void bfq_init_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_io_cq *bic, pid_t pid, int is_sync,
			  unsigned int act_idx)
{
	u64 now_ns = blk_time_get_ns();

	bfqq->actuator_idx = act_idx;
	RB_CLEAR_NODE(&bfqq->entity.rb_node);
	INIT_LIST_HEAD(&bfqq->fifo);
	INIT_HLIST_NODE(&bfqq->burst_list_node);
	INIT_HLIST_NODE(&bfqq->woken_list_node);
	INIT_HLIST_HEAD(&bfqq->woken_list);

	bfqq->ref = 0;
	bfqq->bfqd = bfqd;

	if (bic)
		bfq_set_next_ioprio_data(bfqq, bic);

	if (is_sync) {
		/*
		 * No need to mark as has_short_ttime if in
		 * idle_class, because no device idling is performed
		 * for queues in idle class
		 */
		if (!bfq_class_idle(bfqq))
			/* tentatively mark as has_short_ttime */
			bfq_mark_bfqq_has_short_ttime(bfqq);
		bfq_mark_bfqq_sync(bfqq);
		bfq_mark_bfqq_just_created(bfqq);
	} else
		bfq_clear_bfqq_sync(bfqq);

	/* set end request to minus infinity from now */
	bfqq->ttime.last_end_request = now_ns + 1;

	bfqq->creation_time = jiffies;

	bfqq->io_start_time = now_ns;

	bfq_mark_bfqq_IO_bound(bfqq);

	bfqq->pid = pid;

	/* Tentative initial value to trade off between thr and lat */
	bfqq->max_budget = (2 * bfq_max_budget(bfqd)) / 3;
	bfqq->budget_timeout = bfq_smallest_from_now();

	bfqq->wr_coeff = 1;
	bfqq->last_wr_start_finish = jiffies;
	bfqq->wr_start_at_switch_to_srt = bfq_smallest_from_now();
	bfqq->split_time = bfq_smallest_from_now();

	/*
	 * To not forget the possibly high bandwidth consumed by a
	 * process/queue in the recent past,
	 * bfq_bfqq_softrt_next_start() returns a value at least equal
	 * to the current value of bfqq->soft_rt_next_start (see
	 * comments on bfq_bfqq_softrt_next_start).  Set
	 * soft_rt_next_start to now, to mean that bfqq has consumed
	 * no bandwidth so far.
	 */
	bfqq->soft_rt_next_start = jiffies;

	/* first request is almost certainly seeky */
	bfqq->seek_history = 1;

	bfqq->decrease_time_jif = jiffies;
}

static struct bfq_queue **bfq_async_queue_prio(struct bfq_data *bfqd,
					       struct bfq_group *bfqg,
					       int ioprio_class, int ioprio, int act_idx)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &bfqg->async_bfqq[0][ioprio][act_idx];
	case IOPRIO_CLASS_NONE:
		ioprio = IOPRIO_BE_NORM;
		fallthrough;
	case IOPRIO_CLASS_BE:
		return &bfqg->async_bfqq[1][ioprio][act_idx];
	case IOPRIO_CLASS_IDLE:
		return &bfqg->async_idle_bfqq[act_idx];
	default:
		return NULL;
	}
}

static struct bfq_queue *
bfq_do_early_stable_merge(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_io_cq *bic,
			  struct bfq_queue *last_bfqq_created)
{
	unsigned int a_idx = last_bfqq_created->actuator_idx;
	struct bfq_queue *new_bfqq =
		bfq_setup_merge(bfqq, last_bfqq_created);

	if (!new_bfqq)
		return bfqq;

	if (new_bfqq->bic)
		new_bfqq->bic->bfqq_data[a_idx].stably_merged = true;
	bic->bfqq_data[a_idx].stably_merged = true;

	/*
	 * Reusing merge functions. This implies that
	 * bfqq->bic must be set too, for
	 * bfq_merge_bfqqs to correctly save bfqq's
	 * state before killing it.
	 */
	bfqq->bic = bic;
	return bfq_merge_bfqqs(bfqd, bic, bfqq);
}

/*
 * Many throughput-sensitive workloads are made of several parallel
 * I/O flows, with all flows generated by the same application, or
 * more generically by the same task (e.g., system boot). The most
 * counterproductive action with these workloads is plugging I/O
 * dispatch when one of the bfq_queues associated with these flows
 * remains temporarily empty.
 *
 * To avoid this plugging, BFQ has been using a burst-handling
 * mechanism for years now. This mechanism has proven effective for
 * throughput, and not detrimental for service guarantees. The
 * following function pushes this mechanism a little bit further,
 * basing on the following two facts.
 *
 * First, all the I/O flows of a the same application or task
 * contribute to the execution/completion of that common application
 * or task. So the performance figures that matter are total
 * throughput of the flows and task-wide I/O latency.  In particular,
 * these flows do not need to be protected from each other, in terms
 * of individual bandwidth or latency.
 *
 * Second, the above fact holds regardless of the number of flows.
 *
 * Putting these two facts together, this commits merges stably the
 * bfq_queues associated with these I/O flows, i.e., with the
 * processes that generate these IO/ flows, regardless of how many the
 * involved processes are.
 *
 * To decide whether a set of bfq_queues is actually associated with
 * the I/O flows of a common application or task, and to merge these
 * queues stably, this function operates as follows: given a bfq_queue,
 * say Q2, currently being created, and the last bfq_queue, say Q1,
 * created before Q2, Q2 is merged stably with Q1 if
 * - very little time has elapsed since when Q1 was created
 * - Q2 has the same ioprio as Q1
 * - Q2 belongs to the same group as Q1
 *
 * Merging bfq_queues also reduces scheduling overhead. A fio test
 * with ten random readers on /dev/nullb shows a throughput boost of
 * 40%, with a quadcore. Since BFQ's execution time amounts to ~50% of
 * the total per-request processing time, the above throughput boost
 * implies that BFQ's overhead is reduced by more than 50%.
 *
 * This new mechanism most certainly obsoletes the current
 * burst-handling heuristics. We keep those heuristics for the moment.
 */
static struct bfq_queue *bfq_do_or_sched_stable_merge(struct bfq_data *bfqd,
						      struct bfq_queue *bfqq,
						      struct bfq_io_cq *bic)
{
	struct bfq_queue **source_bfqq = bfqq->entity.parent ?
		&bfqq->entity.parent->last_bfqq_created :
		&bfqd->last_bfqq_created;

	struct bfq_queue *last_bfqq_created = *source_bfqq;

	/*
	 * If last_bfqq_created has not been set yet, then init it. If
	 * it has been set already, but too long ago, then move it
	 * forward to bfqq. Finally, move also if bfqq belongs to a
	 * different group than last_bfqq_created, or if bfqq has a
	 * different ioprio, ioprio_class or actuator_idx. If none of
	 * these conditions holds true, then try an early stable merge
	 * or schedule a delayed stable merge. As for the condition on
	 * actuator_idx, the reason is that, if queues associated with
	 * different actuators are merged, then control is lost on
	 * each actuator. Therefore some actuator may be
	 * underutilized, and throughput may decrease.
	 *
	 * A delayed merge is scheduled (instead of performing an
	 * early merge), in case bfqq might soon prove to be more
	 * throughput-beneficial if not merged. Currently this is
	 * possible only if bfqd is rotational with no queueing. For
	 * such a drive, not merging bfqq is better for throughput if
	 * bfqq happens to contain sequential I/O. So, we wait a
	 * little bit for enough I/O to flow through bfqq. After that,
	 * if such an I/O is sequential, then the merge is
	 * canceled. Otherwise the merge is finally performed.
	 */
	if (!last_bfqq_created ||
	    time_before(last_bfqq_created->creation_time +
			msecs_to_jiffies(bfq_activation_stable_merging),
			bfqq->creation_time) ||
		bfqq->entity.parent != last_bfqq_created->entity.parent ||
		bfqq->ioprio != last_bfqq_created->ioprio ||
		bfqq->ioprio_class != last_bfqq_created->ioprio_class ||
		bfqq->actuator_idx != last_bfqq_created->actuator_idx)
		*source_bfqq = bfqq;
	else if (time_after_eq(last_bfqq_created->creation_time +
				 bfqd->bfq_burst_interval,
				 bfqq->creation_time)) {
		if (likely(bfqd->nonrot_with_queueing))
			/*
			 * With this type of drive, leaving
			 * bfqq alone may provide no
			 * throughput benefits compared with
			 * merging bfqq. So merge bfqq now.
			 */
			bfqq = bfq_do_early_stable_merge(bfqd, bfqq,
							 bic,
							 last_bfqq_created);
		else { /* schedule tentative stable merge */
			/*
			 * get reference on last_bfqq_created,
			 * to prevent it from being freed,
			 * until we decide whether to merge
			 */
			last_bfqq_created->ref++;
			/*
			 * need to keep track of stable refs, to
			 * compute process refs correctly
			 */
			last_bfqq_created->stable_ref++;
			/*
			 * Record the bfqq to merge to.
			 */
			bic->bfqq_data[last_bfqq_created->actuator_idx].stable_merge_bfqq =
				last_bfqq_created;
		}
	}

	return bfqq;
}


static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, bool is_sync,
				       struct bfq_io_cq *bic,
				       bool respawn)
{
	const int ioprio = IOPRIO_PRIO_LEVEL(bic->ioprio);
	const int ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	struct bfq_queue **async_bfqq = NULL;
	struct bfq_queue *bfqq;
	struct bfq_group *bfqg;

	bfqg = bfq_bio_bfqg(bfqd, bio);
	if (!is_sync) {
		async_bfqq = bfq_async_queue_prio(bfqd, bfqg, ioprio_class,
						  ioprio,
						  bfq_actuator_index(bfqd, bio));
		bfqq = *async_bfqq;
		if (bfqq)
			goto out;
	}

	bfqq = kmem_cache_alloc_node(bfq_pool,
				     GFP_NOWAIT | __GFP_ZERO | __GFP_NOWARN,
				     bfqd->queue->node);

	if (bfqq) {
		bfq_init_bfqq(bfqd, bfqq, bic, current->pid,
			      is_sync, bfq_actuator_index(bfqd, bio));
		bfq_init_entity(&bfqq->entity, bfqg);
		bfq_log_bfqq(bfqd, bfqq, "allocated");
	} else {
		bfqq = &bfqd->oom_bfqq;
		bfq_log_bfqq(bfqd, bfqq, "using oom bfqq");
		goto out;
	}

	/*
	 * Pin the queue now that it's allocated, scheduler exit will
	 * prune it.
	 */
	if (async_bfqq) {
		bfqq->ref++; /*
			      * Extra group reference, w.r.t. sync
			      * queue. This extra reference is removed
			      * only if bfqq->bfqg disappears, to
			      * guarantee that this queue is not freed
			      * until its group goes away.
			      */
		bfq_log_bfqq(bfqd, bfqq, "get_queue, bfqq not in async: %p, %d",
			     bfqq, bfqq->ref);
		*async_bfqq = bfqq;
	}

out:
	bfqq->ref++; /* get a process reference to this queue */

	if (bfqq != &bfqd->oom_bfqq && is_sync && !respawn)
		bfqq = bfq_do_or_sched_stable_merge(bfqd, bfqq, bic);
	return bfqq;
}

static void bfq_update_io_thinktime(struct bfq_data *bfqd,
				    struct bfq_queue *bfqq)
{
	struct bfq_ttime *ttime = &bfqq->ttime;
	u64 elapsed;

	/*
	 * We are really interested in how long it takes for the queue to
	 * become busy when there is no outstanding IO for this queue. So
	 * ignore cases when the bfq queue has already IO queued.
	 */
	if (bfqq->dispatched || bfq_bfqq_busy(bfqq))
		return;
	elapsed = blk_time_get_ns() - bfqq->ttime.last_end_request;
	elapsed = min_t(u64, elapsed, 2ULL * bfqd->bfq_slice_idle);

	ttime->ttime_samples = (7*ttime->ttime_samples + 256) / 8;
	ttime->ttime_total = div_u64(7*ttime->ttime_total + 256*elapsed,  8);
	ttime->ttime_mean = div64_ul(ttime->ttime_total + 128,
				     ttime->ttime_samples);
}

static void
bfq_update_io_seektime(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		       struct request *rq)
{
	bfqq->seek_history <<= 1;
	bfqq->seek_history |= BFQ_RQ_SEEKY(bfqd, bfqq->last_request_pos, rq);

	if (bfqq->wr_coeff > 1 &&
	    bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time &&
	    BFQQ_TOTALLY_SEEKY(bfqq)) {
		if (time_is_before_jiffies(bfqq->wr_start_at_switch_to_srt +
					   bfq_wr_duration(bfqd))) {
			/*
			 * In soft_rt weight raising with the
			 * interactive-weight-raising period
			 * elapsed (so no switch back to
			 * interactive weight raising).
			 */
			bfq_bfqq_end_wr(bfqq);
		} else { /*
			  * stopping soft_rt weight raising
			  * while still in interactive period,
			  * switch back to interactive weight
			  * raising
			  */
			switch_back_to_interactive_wr(bfqq, bfqd);
			bfqq->entity.prio_changed = 1;
		}
	}
}

static void bfq_update_has_short_ttime(struct bfq_data *bfqd,
				       struct bfq_queue *bfqq,
				       struct bfq_io_cq *bic)
{
	bool has_short_ttime = true, state_changed;

	/*
	 * No need to update has_short_ttime if bfqq is async or in
	 * idle io prio class, or if bfq_slice_idle is zero, because
	 * no device idling is performed for bfqq in this case.
	 */
	if (!bfq_bfqq_sync(bfqq) || bfq_class_idle(bfqq) ||
	    bfqd->bfq_slice_idle == 0)
		return;

	/* Idle window just restored, statistics are meaningless. */
	if (time_is_after_eq_jiffies(bfqq->split_time +
				     bfqd->bfq_wr_min_idle_time))
		return;

	/* Think time is infinite if no process is linked to
	 * bfqq. Otherwise check average think time to decide whether
	 * to mark as has_short_ttime. To this goal, compare average
	 * think time with half the I/O-plugging timeout.
	 */
	if (atomic_read(&bic->icq.ioc->active_ref) == 0 ||
	    (bfq_sample_valid(bfqq->ttime.ttime_samples) &&
	     bfqq->ttime.ttime_mean > bfqd->bfq_slice_idle>>1))
		has_short_ttime = false;

	state_changed = has_short_ttime != bfq_bfqq_has_short_ttime(bfqq);

	if (has_short_ttime)
		bfq_mark_bfqq_has_short_ttime(bfqq);
	else
		bfq_clear_bfqq_has_short_ttime(bfqq);

	/*
	 * Until the base value for the total service time gets
	 * finally computed for bfqq, the inject limit does depend on
	 * the think-time state (short|long). In particular, the limit
	 * is 0 or 1 if the think time is deemed, respectively, as
	 * short or long (details in the comments in
	 * bfq_update_inject_limit()). Accordingly, the next
	 * instructions reset the inject limit if the think-time state
	 * has changed and the above base value is still to be
	 * computed.
	 *
	 * However, the reset is performed only if more than 100 ms
	 * have elapsed since the last update of the inject limit, or
	 * (inclusive) if the change is from short to long think
	 * time. The reason for this waiting is as follows.
	 *
	 * bfqq may have a long think time because of a
	 * synchronization with some other queue, i.e., because the
	 * I/O of some other queue may need to be completed for bfqq
	 * to receive new I/O. Details in the comments on the choice
	 * of the queue for injection in bfq_select_queue().
	 *
	 * As stressed in those comments, if such a synchronization is
	 * actually in place, then, without injection on bfqq, the
	 * blocking I/O cannot happen to served while bfqq is in
	 * service. As a consequence, if bfqq is granted
	 * I/O-dispatch-plugging, then bfqq remains empty, and no I/O
	 * is dispatched, until the idle timeout fires. This is likely
	 * to result in lower bandwidth and higher latencies for bfqq,
	 * and in a severe loss of total throughput.
	 *
	 * On the opposite end, a non-zero inject limit may allow the
	 * I/O that blocks bfqq to be executed soon, and therefore
	 * bfqq to receive new I/O soon.
	 *
	 * But, if the blocking gets actually eliminated, then the
	 * next think-time sample for bfqq may be very low. This in
	 * turn may cause bfqq's think time to be deemed
	 * short. Without the 100 ms barrier, this new state change
	 * would cause the body of the next if to be executed
	 * immediately. But this would set to 0 the inject
	 * limit. Without injection, the blocking I/O would cause the
	 * think time of bfqq to become long again, and therefore the
	 * inject limit to be raised again, and so on. The only effect
	 * of such a steady oscillation between the two think-time
	 * states would be to prevent effective injection on bfqq.
	 *
	 * In contrast, if the inject limit is not reset during such a
	 * long time interval as 100 ms, then the number of short
	 * think time samples can grow significantly before the reset
	 * is performed. As a consequence, the think time state can
	 * become stable before the reset. Therefore there will be no
	 * state change when the 100 ms elapse, and no reset of the
	 * inject limit. The inject limit remains steadily equal to 1
	 * both during and after the 100 ms. So injection can be
	 * performed at all times, and throughput gets boosted.
	 *
	 * An inject limit equal to 1 is however in conflict, in
	 * general, with the fact that the think time of bfqq is
	 * short, because injection may be likely to delay bfqq's I/O
	 * (as explained in the comments in
	 * bfq_update_inject_limit()). But this does not happen in
	 * this special case, because bfqq's low think time is due to
	 * an effective handling of a synchronization, through
	 * injection. In this special case, bfqq's I/O does not get
	 * delayed by injection; on the contrary, bfqq's I/O is
	 * brought forward, because it is not blocked for
	 * milliseconds.
	 *
	 * In addition, serving the blocking I/O much sooner, and much
	 * more frequently than once per I/O-plugging timeout, makes
	 * it much quicker to detect a waker queue (the concept of
	 * waker queue is defined in the comments in
	 * bfq_add_request()). This makes it possible to start sooner
	 * to boost throughput more effectively, by injecting the I/O
	 * of the waker queue unconditionally on every
	 * bfq_dispatch_request().
	 *
	 * One last, important benefit of not resetting the inject
	 * limit before 100 ms is that, during this time interval, the
	 * base value for the total service time is likely to get
	 * finally computed for bfqq, freeing the inject limit from
	 * its relation with the think time.
	 */
	if (state_changed && bfqq->last_serv_time_ns == 0 &&
	    (time_is_before_eq_jiffies(bfqq->decrease_time_jif +
				      msecs_to_jiffies(100)) ||
	     !has_short_ttime))
		bfq_reset_inject_limit(bfqd, bfqq);
}

/*
 * Called when a new fs request (rq) is added to bfqq.  Check if there's
 * something we should do about it.
 */
static void bfq_rq_enqueued(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			    struct request *rq)
{
	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending++;

	bfqq->last_request_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);

	if (bfqq == bfqd->in_service_queue && bfq_bfqq_wait_request(bfqq)) {
		bool small_req = bfqq->queued[rq_is_sync(rq)] == 1 &&
				 blk_rq_sectors(rq) < 32;
		bool budget_timeout = bfq_bfqq_budget_timeout(bfqq);

		/*
		 * There is just this request queued: if
		 * - the request is small, and
		 * - we are idling to boost throughput, and
		 * - the queue is not to be expired,
		 * then just exit.
		 *
		 * In this way, if the device is being idled to wait
		 * for a new request from the in-service queue, we
		 * avoid unplugging the device and committing the
		 * device to serve just a small request. In contrast
		 * we wait for the block layer to decide when to
		 * unplug the device: hopefully, new requests will be
		 * merged to this one quickly, then the device will be
		 * unplugged and larger requests will be dispatched.
		 */
		if (small_req && idling_boosts_thr_without_issues(bfqd, bfqq) &&
		    !budget_timeout)
			return;

		/*
		 * A large enough request arrived, or idling is being
		 * performed to preserve service guarantees, or
		 * finally the queue is to be expired: in all these
		 * cases disk idling is to be stopped, so clear
		 * wait_request flag and reset timer.
		 */
		bfq_clear_bfqq_wait_request(bfqq);
		hrtimer_try_to_cancel(&bfqd->idle_slice_timer);

		/*
		 * The queue is not empty, because a new request just
		 * arrived. Hence we can safely expire the queue, in
		 * case of budget timeout, without risking that the
		 * timestamps of the queue are not updated correctly.
		 * See [1] for more details.
		 */
		if (budget_timeout)
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_BUDGET_TIMEOUT);
	}
}

static void bfqq_request_allocated(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	for_each_entity(entity)
		entity->allocated++;
}

static void bfqq_request_freed(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	for_each_entity(entity)
		entity->allocated--;
}

/* returns true if it causes the idle timer to be disabled */
static bool __bfq_insert_request(struct bfq_data *bfqd, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq),
		*new_bfqq = bfq_setup_cooperator(bfqd, bfqq, rq, true,
						 RQ_BIC(rq));
	bool waiting, idle_timer_disabled = false;

	if (new_bfqq) {
		struct bfq_queue *old_bfqq = bfqq;
		/*
		 * Release the request's reference to the old bfqq
		 * and make sure one is taken to the shared queue.
		 */
		bfqq_request_allocated(new_bfqq);
		bfqq_request_freed(bfqq);
		new_bfqq->ref++;
		/*
		 * If the bic associated with the process
		 * issuing this request still points to bfqq
		 * (and thus has not been already redirected
		 * to new_bfqq or even some other bfq_queue),
		 * then complete the merge and redirect it to
		 * new_bfqq.
		 */
		if (bic_to_bfqq(RQ_BIC(rq), true,
				bfq_actuator_index(bfqd, rq->bio)) == bfqq) {
			while (bfqq != new_bfqq)
				bfqq = bfq_merge_bfqqs(bfqd, RQ_BIC(rq), bfqq);
		}

		bfq_clear_bfqq_just_created(old_bfqq);
		/*
		 * rq is about to be enqueued into new_bfqq,
		 * release rq reference on bfqq
		 */
		bfq_put_queue(old_bfqq);
		rq->elv.priv[1] = new_bfqq;
	}

	bfq_update_io_thinktime(bfqd, bfqq);
	bfq_update_has_short_ttime(bfqd, bfqq, RQ_BIC(rq));
	bfq_update_io_seektime(bfqd, bfqq, rq);

	waiting = bfqq && bfq_bfqq_wait_request(bfqq);
	bfq_add_request(rq);
	idle_timer_disabled = waiting && !bfq_bfqq_wait_request(bfqq);

	rq->fifo_time = blk_time_get_ns() + bfqd->bfq_fifo_expire[rq_is_sync(rq)];
	list_add_tail(&rq->queuelist, &bfqq->fifo);

	bfq_rq_enqueued(bfqd, bfqq, rq);

	return idle_timer_disabled;
}

#ifdef CONFIG_BFQ_CGROUP_DEBUG
static void bfq_update_insert_stats(struct request_queue *q,
				    struct bfq_queue *bfqq,
				    bool idle_timer_disabled,
				    blk_opf_t cmd_flags)
{
	if (!bfqq)
		return;

	/*
	 * bfqq still exists, because it can disappear only after
	 * either it is merged with another queue, or the process it
	 * is associated with exits. But both actions must be taken by
	 * the same process currently executing this flow of
	 * instructions.
	 *
	 * In addition, the following queue lock guarantees that
	 * bfqq_group(bfqq) exists as well.
	 */
	spin_lock_irq(&q->queue_lock);
	bfqg_stats_update_io_add(bfqq_group(bfqq), bfqq, cmd_flags);
	if (idle_timer_disabled)
		bfqg_stats_update_idle_time(bfqq_group(bfqq));
	spin_unlock_irq(&q->queue_lock);
}
#else
static inline void bfq_update_insert_stats(struct request_queue *q,
					   struct bfq_queue *bfqq,
					   bool idle_timer_disabled,
					   blk_opf_t cmd_flags) {}
#endif /* CONFIG_BFQ_CGROUP_DEBUG */

static struct bfq_queue *bfq_init_rq(struct request *rq);

static void bfq_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			       blk_insert_t flags)
{
	struct request_queue *q = hctx->queue;
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_queue *bfqq;
	bool idle_timer_disabled = false;
	blk_opf_t cmd_flags;
	LIST_HEAD(free);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	if (!cgroup_subsys_on_dfl(io_cgrp_subsys) && rq->bio)
		bfqg_stats_update_legacy_io(q, rq);
#endif
	spin_lock_irq(&bfqd->lock);
	bfqq = bfq_init_rq(rq);
	if (blk_mq_sched_try_insert_merge(q, rq, &free)) {
		spin_unlock_irq(&bfqd->lock);
		blk_mq_free_requests(&free);
		return;
	}

	trace_block_rq_insert(rq);

	if (flags & BLK_MQ_INSERT_AT_HEAD) {
		list_add(&rq->queuelist, &bfqd->dispatch);
	} else if (!bfqq) {
		list_add_tail(&rq->queuelist, &bfqd->dispatch);
	} else {
		idle_timer_disabled = __bfq_insert_request(bfqd, rq);
		/*
		 * Update bfqq, because, if a queue merge has occurred
		 * in __bfq_insert_request, then rq has been
		 * redirected into a new queue.
		 */
		bfqq = RQ_BFQQ(rq);

		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}
	}

	/*
	 * Cache cmd_flags before releasing scheduler lock, because rq
	 * may disappear afterwards (for example, because of a request
	 * merge).
	 */
	cmd_flags = rq->cmd_flags;
	spin_unlock_irq(&bfqd->lock);

	bfq_update_insert_stats(q, bfqq, idle_timer_disabled,
				cmd_flags);
}

static void bfq_insert_requests(struct blk_mq_hw_ctx *hctx,
				struct list_head *list,
				blk_insert_t flags)
{
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		bfq_insert_request(hctx, rq, flags);
	}
}

static void bfq_update_hw_tag(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;

	bfqd->max_rq_in_driver = max_t(int, bfqd->max_rq_in_driver,
				       bfqd->tot_rq_in_driver);

	if (bfqd->hw_tag == 1)
		return;

	/*
	 * This sample is valid if the number of outstanding requests
	 * is large enough to allow a queueing behavior.  Note that the
	 * sum is not exact, as it's not taking into account deactivated
	 * requests.
	 */
	if (bfqd->tot_rq_in_driver + bfqd->queued <= BFQ_HW_QUEUE_THRESHOLD)
		return;

	/*
	 * If active queue hasn't enough requests and can idle, bfq might not
	 * dispatch sufficient requests to hardware. Don't zero hw_tag in this
	 * case
	 */
	if (bfqq && bfq_bfqq_has_short_ttime(bfqq) &&
	    bfqq->dispatched + bfqq->queued[0] + bfqq->queued[1] <
	    BFQ_HW_QUEUE_THRESHOLD &&
	    bfqd->tot_rq_in_driver < BFQ_HW_QUEUE_THRESHOLD)
		return;

	if (bfqd->hw_tag_samples++ < BFQ_HW_QUEUE_SAMPLES)
		return;

	bfqd->hw_tag = bfqd->max_rq_in_driver > BFQ_HW_QUEUE_THRESHOLD;
	bfqd->max_rq_in_driver = 0;
	bfqd->hw_tag_samples = 0;

	bfqd->nonrot_with_queueing =
		blk_queue_nonrot(bfqd->queue) && bfqd->hw_tag;
}

static void bfq_completed_request(struct bfq_queue *bfqq, struct bfq_data *bfqd)
{
	u64 now_ns;
	u32 delta_us;

	bfq_update_hw_tag(bfqd);

	bfqd->rq_in_driver[bfqq->actuator_idx]--;
	bfqd->tot_rq_in_driver--;
	bfqq->dispatched--;

	if (!bfqq->dispatched && !bfq_bfqq_busy(bfqq)) {
		/*
		 * Set budget_timeout (which we overload to store the
		 * time at which the queue remains with no backlog and
		 * no outstanding request; used by the weight-raising
		 * mechanism).
		 */
		bfqq->budget_timeout = jiffies;

		bfq_del_bfqq_in_groups_with_pending_reqs(bfqq);
		bfq_weights_tree_remove(bfqq);
	}

	now_ns = blk_time_get_ns();

	bfqq->ttime.last_end_request = now_ns;

	/*
	 * Using us instead of ns, to get a reasonable precision in
	 * computing rate in next check.
	 */
	delta_us = div_u64(now_ns - bfqd->last_completion, NSEC_PER_USEC);

	/*
	 * If the request took rather long to complete, and, according
	 * to the maximum request size recorded, this completion latency
	 * implies that the request was certainly served at a very low
	 * rate (less than 1M sectors/sec), then the whole observation
	 * interval that lasts up to this time instant cannot be a
	 * valid time interval for computing a new peak rate.  Invoke
	 * bfq_update_rate_reset to have the following three steps
	 * taken:
	 * - close the observation interval at the last (previous)
	 *   request dispatch or completion
	 * - compute rate, if possible, for that observation interval
	 * - reset to zero samples, which will trigger a proper
	 *   re-initialization of the observation interval on next
	 *   dispatch
	 */
	if (delta_us > BFQ_MIN_TT/NSEC_PER_USEC &&
	   (bfqd->last_rq_max_size<<BFQ_RATE_SHIFT)/delta_us <
			1UL<<(BFQ_RATE_SHIFT - 10))
		bfq_update_rate_reset(bfqd, NULL);
	bfqd->last_completion = now_ns;
	/*
	 * Shared queues are likely to receive I/O at a high
	 * rate. This may deceptively let them be considered as wakers
	 * of other queues. But a false waker will unjustly steal
	 * bandwidth to its supposedly woken queue. So considering
	 * also shared queues in the waking mechanism may cause more
	 * control troubles than throughput benefits. Then reset
	 * last_completed_rq_bfqq if bfqq is a shared queue.
	 */
	if (!bfq_bfqq_coop(bfqq))
		bfqd->last_completed_rq_bfqq = bfqq;
	else
		bfqd->last_completed_rq_bfqq = NULL;

	/*
	 * If we are waiting to discover whether the request pattern
	 * of the task associated with the queue is actually
	 * isochronous, and both requisites for this condition to hold
	 * are now satisfied, then compute soft_rt_next_start (see the
	 * comments on the function bfq_bfqq_softrt_next_start()). We
	 * do not compute soft_rt_next_start if bfqq is in interactive
	 * weight raising (see the comments in bfq_bfqq_expire() for
	 * an explanation). We schedule this delayed update when bfqq
	 * expires, if it still has in-flight requests.
	 */
	if (bfq_bfqq_softrt_update(bfqq) && bfqq->dispatched == 0 &&
	    RB_EMPTY_ROOT(&bfqq->sort_list) &&
	    bfqq->wr_coeff != bfqd->bfq_wr_coeff)
		bfqq->soft_rt_next_start =
			bfq_bfqq_softrt_next_start(bfqd, bfqq);

	/*
	 * If this is the in-service queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (bfqd->in_service_queue == bfqq) {
		if (bfq_bfqq_must_idle(bfqq)) {
			if (bfqq->dispatched == 0)
				bfq_arm_slice_timer(bfqd);
			/*
			 * If we get here, we do not expire bfqq, even
			 * if bfqq was in budget timeout or had no
			 * more requests (as controlled in the next
			 * conditional instructions). The reason for
			 * not expiring bfqq is as follows.
			 *
			 * Here bfqq->dispatched > 0 holds, but
			 * bfq_bfqq_must_idle() returned true. This
			 * implies that, even if no request arrives
			 * for bfqq before bfqq->dispatched reaches 0,
			 * bfqq will, however, not be expired on the
			 * completion event that causes bfqq->dispatch
			 * to reach zero. In contrast, on this event,
			 * bfqq will start enjoying device idling
			 * (I/O-dispatch plugging).
			 *
			 * But, if we expired bfqq here, bfqq would
			 * not have the chance to enjoy device idling
			 * when bfqq->dispatched finally reaches
			 * zero. This would expose bfqq to violation
			 * of its reserved service guarantees.
			 */
			return;
		} else if (bfq_may_expire_for_budg_timeout(bfqq))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_BUDGET_TIMEOUT);
		else if (RB_EMPTY_ROOT(&bfqq->sort_list) &&
			 (bfqq->dispatched == 0 ||
			  !bfq_better_to_idle(bfqq)))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_NO_MORE_REQUESTS);
	}

	if (!bfqd->tot_rq_in_driver)
		bfq_schedule_dispatch(bfqd);
}

/*
 * The processes associated with bfqq may happen to generate their
 * cumulative I/O at a lower rate than the rate at which the device
 * could serve the same I/O. This is rather probable, e.g., if only
 * one process is associated with bfqq and the device is an SSD. It
 * results in bfqq becoming often empty while in service. In this
 * respect, if BFQ is allowed to switch to another queue when bfqq
 * remains empty, then the device goes on being fed with I/O requests,
 * and the throughput is not affected. In contrast, if BFQ is not
 * allowed to switch to another queue---because bfqq is sync and
 * I/O-dispatch needs to be plugged while bfqq is temporarily
 * empty---then, during the service of bfqq, there will be frequent
 * "service holes", i.e., time intervals during which bfqq gets empty
 * and the device can only consume the I/O already queued in its
 * hardware queues. During service holes, the device may even get to
 * remaining idle. In the end, during the service of bfqq, the device
 * is driven at a lower speed than the one it can reach with the kind
 * of I/O flowing through bfqq.
 *
 * To counter this loss of throughput, BFQ implements a "request
 * injection mechanism", which tries to fill the above service holes
 * with I/O requests taken from other queues. The hard part in this
 * mechanism is finding the right amount of I/O to inject, so as to
 * both boost throughput and not break bfqq's bandwidth and latency
 * guarantees. In this respect, the mechanism maintains a per-queue
 * inject limit, computed as below. While bfqq is empty, the injection
 * mechanism dispatches extra I/O requests only until the total number
 * of I/O requests in flight---i.e., already dispatched but not yet
 * completed---remains lower than this limit.
 *
 * A first definition comes in handy to introduce the algorithm by
 * which the inject limit is computed.  We define as first request for
 * bfqq, an I/O request for bfqq that arrives while bfqq is in
 * service, and causes bfqq to switch from empty to non-empty. The
 * algorithm updates the limit as a function of the effect of
 * injection on the service times of only the first requests of
 * bfqq. The reason for this restriction is that these are the
 * requests whose service time is affected most, because they are the
 * first to arrive after injection possibly occurred.
 *
 * To evaluate the effect of injection, the algorithm measures the
 * "total service time" of first requests. We define as total service
 * time of an I/O request, the time that elapses since when the
 * request is enqueued into bfqq, to when it is completed. This
 * quantity allows the whole effect of injection to be measured. It is
 * easy to see why. Suppose that some requests of other queues are
 * actually injected while bfqq is empty, and that a new request R
 * then arrives for bfqq. If the device does start to serve all or
 * part of the injected requests during the service hole, then,
 * because of this extra service, it may delay the next invocation of
 * the dispatch hook of BFQ. Then, even after R gets eventually
 * dispatched, the device may delay the actual service of R if it is
 * still busy serving the extra requests, or if it decides to serve,
 * before R, some extra request still present in its queues. As a
 * conclusion, the cumulative extra delay caused by injection can be
 * easily evaluated by just comparing the total service time of first
 * requests with and without injection.
 *
 * The limit-update algorithm works as follows. On the arrival of a
 * first request of bfqq, the algorithm measures the total time of the
 * request only if one of the three cases below holds, and, for each
 * case, it updates the limit as described below:
 *
 * (1) If there is no in-flight request. This gives a baseline for the
 *     total service time of the requests of bfqq. If the baseline has
 *     not been computed yet, then, after computing it, the limit is
 *     set to 1, to start boosting throughput, and to prepare the
 *     ground for the next case. If the baseline has already been
 *     computed, then it is updated, in case it results to be lower
 *     than the previous value.
 *
 * (2) If the limit is higher than 0 and there are in-flight
 *     requests. By comparing the total service time in this case with
 *     the above baseline, it is possible to know at which extent the
 *     current value of the limit is inflating the total service
 *     time. If the inflation is below a certain threshold, then bfqq
 *     is assumed to be suffering from no perceivable loss of its
 *     service guarantees, and the limit is even tentatively
 *     increased. If the inflation is above the threshold, then the
 *     limit is decreased. Due to the lack of any hysteresis, this
 *     logic makes the limit oscillate even in steady workload
 *     conditions. Yet we opted for it, because it is fast in reaching
 *     the best value for the limit, as a function of the current I/O
 *     workload. To reduce oscillations, this step is disabled for a
 *     short time interval after the limit happens to be decreased.
 *
 * (3) Periodically, after resetting the limit, to make sure that the
 *     limit eventually drops in case the workload changes. This is
 *     needed because, after the limit has gone safely up for a
 *     certain workload, it is impossible to guess whether the
 *     baseline total service time may have changed, without measuring
 *     it again without injection. A more effective version of this
 *     step might be to just sample the baseline, by interrupting
 *     injection only once, and then to reset/lower the limit only if
 *     the total service time with the current limit does happen to be
 *     too large.
 *
 * More details on each step are provided in the comments on the
 * pieces of code that implement these steps: the branch handling the
 * transition from empty to non empty in bfq_add_request(), the branch
 * handling injection in bfq_select_queue(), and the function
 * bfq_choose_bfqq_for_injection(). These comments also explain some
 * exceptions, made by the injection mechanism in some special cases.
 */
static void bfq_update_inject_limit(struct bfq_data *bfqd,
				    struct bfq_queue *bfqq)
{
	u64 tot_time_ns = blk_time_get_ns() - bfqd->last_empty_occupied_ns;
	unsigned int old_limit = bfqq->inject_limit;

	if (bfqq->last_serv_time_ns > 0 && bfqd->rqs_injected) {
		u64 threshold = (bfqq->last_serv_time_ns * 3)>>1;

		if (tot_time_ns >= threshold && old_limit > 0) {
			bfqq->inject_limit--;
			bfqq->decrease_time_jif = jiffies;
		} else if (tot_time_ns < threshold &&
			   old_limit <= bfqd->max_rq_in_driver)
			bfqq->inject_limit++;
	}

	/*
	 * Either we still have to compute the base value for the
	 * total service time, and there seem to be the right
	 * conditions to do it, or we can lower the last base value
	 * computed.
	 *
	 * NOTE: (bfqd->tot_rq_in_driver == 1) means that there is no I/O
	 * request in flight, because this function is in the code
	 * path that handles the completion of a request of bfqq, and,
	 * in particular, this function is executed before
	 * bfqd->tot_rq_in_driver is decremented in such a code path.
	 */
	if ((bfqq->last_serv_time_ns == 0 && bfqd->tot_rq_in_driver == 1) ||
	    tot_time_ns < bfqq->last_serv_time_ns) {
		if (bfqq->last_serv_time_ns == 0) {
			/*
			 * Now we certainly have a base value: make sure we
			 * start trying injection.
			 */
			bfqq->inject_limit = max_t(unsigned int, 1, old_limit);
		}
		bfqq->last_serv_time_ns = tot_time_ns;
	} else if (!bfqd->rqs_injected && bfqd->tot_rq_in_driver == 1)
		/*
		 * No I/O injected and no request still in service in
		 * the drive: these are the exact conditions for
		 * computing the base value of the total service time
		 * for bfqq. So let's update this value, because it is
		 * rather variable. For example, it varies if the size
		 * or the spatial locality of the I/O requests in bfqq
		 * change.
		 */
		bfqq->last_serv_time_ns = tot_time_ns;


	/* update complete, not waiting for any request completion any longer */
	bfqd->waited_rq = NULL;
	bfqd->rqs_injected = false;
}

/*
 * Handle either a requeue or a finish for rq. The things to do are
 * the same in both cases: all references to rq are to be dropped. In
 * particular, rq is considered completed from the point of view of
 * the scheduler.
 */
static void bfq_finish_requeue_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd;
	unsigned long flags;

	/*
	 * rq either is not associated with any icq, or is an already
	 * requeued request that has not (yet) been re-inserted into
	 * a bfq_queue.
	 */
	if (!rq->elv.icq || !bfqq)
		return;

	bfqd = bfqq->bfqd;

	if (rq->rq_flags & RQF_STARTED)
		bfqg_stats_update_completion(bfqq_group(bfqq),
					     rq->start_time_ns,
					     rq->io_start_time_ns,
					     rq->cmd_flags);

	spin_lock_irqsave(&bfqd->lock, flags);
	if (likely(rq->rq_flags & RQF_STARTED)) {
		if (rq == bfqd->waited_rq)
			bfq_update_inject_limit(bfqd, bfqq);

		bfq_completed_request(bfqq, bfqd);
	}
	bfqq_request_freed(bfqq);
	bfq_put_queue(bfqq);
	RQ_BIC(rq)->requests--;
	spin_unlock_irqrestore(&bfqd->lock, flags);

	/*
	 * Reset private fields. In case of a requeue, this allows
	 * this function to correctly do nothing if it is spuriously
	 * invoked again on this same request (see the check at the
	 * beginning of the function). Probably, a better general
	 * design would be to prevent blk-mq from invoking the requeue
	 * or finish hooks of an elevator, for a request that is not
	 * referred by that elevator.
	 *
	 * Resetting the following fields would break the
	 * request-insertion logic if rq is re-inserted into a bfq
	 * internal queue, without a re-preparation. Here we assume
	 * that re-insertions of requeued requests, without
	 * re-preparation, can happen only for pass_through or at_head
	 * requests (which are not re-inserted into bfq internal
	 * queues).
	 */
	rq->elv.priv[0] = NULL;
	rq->elv.priv[1] = NULL;
}

static void bfq_finish_request(struct request *rq)
{
	bfq_finish_requeue_request(rq);

	if (rq->elv.icq) {
		put_io_context(rq->elv.icq->ioc);
		rq->elv.icq = NULL;
	}
}

/*
 * Removes the association between the current task and bfqq, assuming
 * that bic points to the bfq iocontext of the task.
 * Returns NULL if a new bfqq should be allocated, or the old bfqq if this
 * was the last process referring to that bfqq.
 */
static struct bfq_queue *
bfq_split_bfqq(struct bfq_io_cq *bic, struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq, "splitting queue");

	if (bfqq_process_refs(bfqq) == 1 && !bfqq->new_bfqq) {
		bfqq->pid = current->pid;
		bfq_clear_bfqq_coop(bfqq);
		bfq_clear_bfqq_split_coop(bfqq);
		return bfqq;
	}

	bic_set_bfqq(bic, NULL, true, bfqq->actuator_idx);

	bfq_put_cooperator(bfqq);

	bfq_release_process_ref(bfqq->bfqd, bfqq);
	return NULL;
}

static struct bfq_queue *
__bfq_get_bfqq_handle_split(struct bfq_data *bfqd, struct bfq_io_cq *bic,
			    struct bio *bio, bool split, bool is_sync,
			    bool *new_queue)
{
	unsigned int act_idx = bfq_actuator_index(bfqd, bio);
	struct bfq_queue *bfqq = bic_to_bfqq(bic, is_sync, act_idx);
	struct bfq_iocq_bfqq_data *bfqq_data = &bic->bfqq_data[act_idx];

	if (likely(bfqq && bfqq != &bfqd->oom_bfqq))
		return bfqq;

	if (new_queue)
		*new_queue = true;

	if (bfqq)
		bfq_put_queue(bfqq);
	bfqq = bfq_get_queue(bfqd, bio, is_sync, bic, split);

	bic_set_bfqq(bic, bfqq, is_sync, act_idx);
	if (split && is_sync) {
		if ((bfqq_data->was_in_burst_list && bfqd->large_burst) ||
		    bfqq_data->saved_in_large_burst)
			bfq_mark_bfqq_in_large_burst(bfqq);
		else {
			bfq_clear_bfqq_in_large_burst(bfqq);
			if (bfqq_data->was_in_burst_list)
				/*
				 * If bfqq was in the current
				 * burst list before being
				 * merged, then we have to add
				 * it back. And we do not need
				 * to increase burst_size, as
				 * we did not decrement
				 * burst_size when we removed
				 * bfqq from the burst list as
				 * a consequence of a merge
				 * (see comments in
				 * bfq_put_queue). In this
				 * respect, it would be rather
				 * costly to know whether the
				 * current burst list is still
				 * the same burst list from
				 * which bfqq was removed on
				 * the merge. To avoid this
				 * cost, if bfqq was in a
				 * burst list, then we add
				 * bfqq to the current burst
				 * list without any further
				 * check. This can cause
				 * inappropriate insertions,
				 * but rarely enough to not
				 * harm the detection of large
				 * bursts significantly.
				 */
				hlist_add_head(&bfqq->burst_list_node,
					       &bfqd->burst_list);
		}
		bfqq->split_time = jiffies;
	}

	return bfqq;
}

/*
 * Only reset private fields. The actual request preparation will be
 * performed by bfq_init_rq, when rq is either inserted or merged. See
 * comments on bfq_init_rq for the reason behind this delayed
 * preparation.
 */
static void bfq_prepare_request(struct request *rq)
{
	rq->elv.icq = ioc_find_get_icq(rq->q);

	/*
	 * Regardless of whether we have an icq attached, we have to
	 * clear the scheduler pointers, as they might point to
	 * previously allocated bic/bfqq structs.
	 */
	rq->elv.priv[0] = rq->elv.priv[1] = NULL;
}

static struct bfq_queue *bfq_waker_bfqq(struct bfq_queue *bfqq)
{
	struct bfq_queue *new_bfqq = bfqq->new_bfqq;
	struct bfq_queue *waker_bfqq = bfqq->waker_bfqq;

	if (!waker_bfqq)
		return NULL;

	while (new_bfqq) {
		if (new_bfqq == waker_bfqq) {
			/*
			 * If waker_bfqq is in the merge chain, and current
			 * is the only process, waker_bfqq can be freed.
			 */
			if (bfqq_process_refs(waker_bfqq) == 1)
				return NULL;

			return waker_bfqq;
		}

		new_bfqq = new_bfqq->new_bfqq;
	}

	/*
	 * If waker_bfqq is not in the merge chain, and it's procress reference
	 * is 0, waker_bfqq can be freed.
	 */
	if (bfqq_process_refs(waker_bfqq) == 0)
		return NULL;

	return waker_bfqq;
}

static struct bfq_queue *bfq_get_bfqq_handle_split(struct bfq_data *bfqd,
						   struct bfq_io_cq *bic,
						   struct bio *bio,
						   unsigned int idx,
						   bool is_sync)
{
	struct bfq_queue *waker_bfqq;
	struct bfq_queue *bfqq;
	bool new_queue = false;

	bfqq = __bfq_get_bfqq_handle_split(bfqd, bic, bio, false, is_sync,
					   &new_queue);
	if (unlikely(new_queue))
		return bfqq;

	/* If the queue was seeky for too long, break it apart. */
	if (!bfq_bfqq_coop(bfqq) || !bfq_bfqq_split_coop(bfqq) ||
	    bic->bfqq_data[idx].stably_merged)
		return bfqq;

	waker_bfqq = bfq_waker_bfqq(bfqq);

	/* Update bic before losing reference to bfqq */
	if (bfq_bfqq_in_large_burst(bfqq))
		bic->bfqq_data[idx].saved_in_large_burst = true;

	bfqq = bfq_split_bfqq(bic, bfqq);
	if (bfqq) {
		bfq_bfqq_resume_state(bfqq, bfqd, bic, true);
		return bfqq;
	}

	bfqq = __bfq_get_bfqq_handle_split(bfqd, bic, bio, true, is_sync, NULL);
	if (unlikely(bfqq == &bfqd->oom_bfqq))
		return bfqq;

	bfq_bfqq_resume_state(bfqq, bfqd, bic, false);
	bfqq->waker_bfqq = waker_bfqq;
	bfqq->tentative_waker_bfqq = NULL;

	/*
	 * If the waker queue disappears, then new_bfqq->waker_bfqq must be
	 * reset. So insert new_bfqq into the
	 * woken_list of the waker. See
	 * bfq_check_waker for details.
	 */
	if (waker_bfqq)
		hlist_add_head(&bfqq->woken_list_node,
			       &bfqq->waker_bfqq->woken_list);

	return bfqq;
}

/*
 * If needed, init rq, allocate bfq data structures associated with
 * rq, and increment reference counters in the destination bfq_queue
 * for rq. Return the destination bfq_queue for rq, or NULL is rq is
 * not associated with any bfq_queue.
 *
 * This function is invoked by the functions that perform rq insertion
 * or merging. One may have expected the above preparation operations
 * to be performed in bfq_prepare_request, and not delayed to when rq
 * is inserted or merged. The rationale behind this delayed
 * preparation is that, after the prepare_request hook is invoked for
 * rq, rq may still be transformed into a request with no icq, i.e., a
 * request not associated with any queue. No bfq hook is invoked to
 * signal this transformation. As a consequence, should these
 * preparation operations be performed when the prepare_request hook
 * is invoked, and should rq be transformed one moment later, bfq
 * would end up in an inconsistent state, because it would have
 * incremented some queue counters for an rq destined to
 * transformation, without any chance to correctly lower these
 * counters back. In contrast, no transformation can still happen for
 * rq after rq has been inserted or merged. So, it is safe to execute
 * these preparation operations when rq is finally inserted or merged.
 */
static struct bfq_queue *bfq_init_rq(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct bio *bio = rq->bio;
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_io_cq *bic;
	const int is_sync = rq_is_sync(rq);
	struct bfq_queue *bfqq;
	unsigned int a_idx = bfq_actuator_index(bfqd, bio);

	if (unlikely(!rq->elv.icq))
		return NULL;

	/*
	 * Assuming that RQ_BFQQ(rq) is set only if everything is set
	 * for this rq. This holds true, because this function is
	 * invoked only for insertion or merging, and, after such
	 * events, a request cannot be manipulated any longer before
	 * being removed from bfq.
	 */
	if (RQ_BFQQ(rq))
		return RQ_BFQQ(rq);

	bic = icq_to_bic(rq->elv.icq);
	bfq_check_ioprio_change(bic, bio);
	bfq_bic_update_cgroup(bic, bio);
	bfqq = bfq_get_bfqq_handle_split(bfqd, bic, bio, a_idx, is_sync);

	bfqq_request_allocated(bfqq);
	bfqq->ref++;
	bic->requests++;
	bfq_log_bfqq(bfqd, bfqq, "get_request %p: bfqq %p, %d",
		     rq, bfqq, bfqq->ref);

	rq->elv.priv[0] = bic;
	rq->elv.priv[1] = bfqq;

	/*
	 * If a bfq_queue has only one process reference, it is owned
	 * by only this bic: we can then set bfqq->bic = bic. in
	 * addition, if the queue has also just been split, we have to
	 * resume its state.
	 */
	if (likely(bfqq != &bfqd->oom_bfqq) && !bfqq->new_bfqq &&
	    bfqq_process_refs(bfqq) == 1)
		bfqq->bic = bic;

	/*
	 * Consider bfqq as possibly belonging to a burst of newly
	 * created queues only if:
	 * 1) A burst is actually happening (bfqd->burst_size > 0)
	 * or
	 * 2) There is no other active queue. In fact, if, in
	 *    contrast, there are active queues not belonging to the
	 *    possible burst bfqq may belong to, then there is no gain
	 *    in considering bfqq as belonging to a burst, and
	 *    therefore in not weight-raising bfqq. See comments on
	 *    bfq_handle_burst().
	 *
	 * This filtering also helps eliminating false positives,
	 * occurring when bfqq does not belong to an actual large
	 * burst, but some background task (e.g., a service) happens
	 * to trigger the creation of new queues very close to when
	 * bfqq and its possible companion queues are created. See
	 * comments on bfq_handle_burst() for further details also on
	 * this issue.
	 */
	if (unlikely(bfq_bfqq_just_created(bfqq) &&
		     (bfqd->burst_size > 0 ||
		      bfq_tot_busy_queues(bfqd) == 0)))
		bfq_handle_burst(bfqd, bfqq);

	return bfqq;
}

static void
bfq_idle_slice_timer_body(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	enum bfqq_expiration reason;
	unsigned long flags;

	spin_lock_irqsave(&bfqd->lock, flags);

	/*
	 * Considering that bfqq may be in race, we should firstly check
	 * whether bfqq is in service before doing something on it. If
	 * the bfqq in race is not in service, it has already been expired
	 * through __bfq_bfqq_expire func and its wait_request flags has
	 * been cleared in __bfq_bfqd_reset_in_service func.
	 */
	if (bfqq != bfqd->in_service_queue) {
		spin_unlock_irqrestore(&bfqd->lock, flags);
		return;
	}

	bfq_clear_bfqq_wait_request(bfqq);

	if (bfq_bfqq_budget_timeout(bfqq))
		/*
		 * Also here the queue can be safely expired
		 * for budget timeout without wasting
		 * guarantees
		 */
		reason = BFQQE_BUDGET_TIMEOUT;
	else if (bfqq->queued[0] == 0 && bfqq->queued[1] == 0)
		/*
		 * The queue may not be empty upon timer expiration,
		 * because we may not disable the timer when the
		 * first request of the in-service queue arrives
		 * during disk idling.
		 */
		reason = BFQQE_TOO_IDLE;
	else
		goto schedule_dispatch;

	bfq_bfqq_expire(bfqd, bfqq, true, reason);

schedule_dispatch:
	bfq_schedule_dispatch(bfqd);
	spin_unlock_irqrestore(&bfqd->lock, flags);
}

/*
 * Handler of the expiration of the timer running if the in-service queue
 * is idling inside its time slice.
 */
static enum hrtimer_restart bfq_idle_slice_timer(struct hrtimer *timer)
{
	struct bfq_data *bfqd = container_of(timer, struct bfq_data,
					     idle_slice_timer);
	struct bfq_queue *bfqq = bfqd->in_service_queue;

	/*
	 * Theoretical race here: the in-service queue can be NULL or
	 * different from the queue that was idling if a new request
	 * arrives for the current queue and there is a full dispatch
	 * cycle that changes the in-service queue.  This can hardly
	 * happen, but in the worst case we just expire a queue too
	 * early.
	 */
	if (bfqq)
		bfq_idle_slice_timer_body(bfqd, bfqq);

	return HRTIMER_NORESTART;
}

static void __bfq_put_async_bfqq(struct bfq_data *bfqd,
				 struct bfq_queue **bfqq_ptr)
{
	struct bfq_queue *bfqq = *bfqq_ptr;

	bfq_log(bfqd, "put_async_bfqq: %p", bfqq);
	if (bfqq) {
		bfq_bfqq_move(bfqd, bfqq, bfqd->root_group);

		bfq_log_bfqq(bfqd, bfqq, "put_async_bfqq: putting %p, %d",
			     bfqq, bfqq->ref);
		bfq_put_queue(bfqq);
		*bfqq_ptr = NULL;
	}
}

/*
 * Release all the bfqg references to its async queues.  If we are
 * deallocating the group these queues may still contain requests, so
 * we reparent them to the root cgroup (i.e., the only one that will
 * exist for sure until all the requests on a device are gone).
 */
void bfq_put_async_queues(struct bfq_data *bfqd, struct bfq_group *bfqg)
{
	int i, j, k;

	for (k = 0; k < bfqd->num_actuators; k++) {
		for (i = 0; i < 2; i++)
			for (j = 0; j < IOPRIO_NR_LEVELS; j++)
				__bfq_put_async_bfqq(bfqd, &bfqg->async_bfqq[i][j][k]);

		__bfq_put_async_bfqq(bfqd, &bfqg->async_idle_bfqq[k]);
	}
}

/*
 * See the comments on bfq_limit_depth for the purpose of
 * the depths set in the function. Return minimum shallow depth we'll use.
 */
static void bfq_update_depths(struct bfq_data *bfqd, struct sbitmap_queue *bt)
{
	unsigned int depth = 1U << bt->sb.shift;

	bfqd->full_depth_shift = bt->sb.shift;
	/*
	 * In-word depths if no bfq_queue is being weight-raised:
	 * leaving 25% of tags only for sync reads.
	 *
	 * In next formulas, right-shift the value
	 * (1U<<bt->sb.shift), instead of computing directly
	 * (1U<<(bt->sb.shift - something)), to be robust against
	 * any possible value of bt->sb.shift, without having to
	 * limit 'something'.
	 */
	/* no more than 50% of tags for async I/O */
	bfqd->word_depths[0][0] = max(depth >> 1, 1U);
	/*
	 * no more than 75% of tags for sync writes (25% extra tags
	 * w.r.t. async I/O, to prevent async I/O from starving sync
	 * writes)
	 */
	bfqd->word_depths[0][1] = max((depth * 3) >> 2, 1U);

	/*
	 * In-word depths in case some bfq_queue is being weight-
	 * raised: leaving ~63% of tags for sync reads. This is the
	 * highest percentage for which, in our tests, application
	 * start-up times didn't suffer from any regression due to tag
	 * shortage.
	 */
	/* no more than ~18% of tags for async I/O */
	bfqd->word_depths[1][0] = max((depth * 3) >> 4, 1U);
	/* no more than ~37% of tags for sync writes (~20% extra tags) */
	bfqd->word_depths[1][1] = max((depth * 6) >> 4, 1U);
}

static void bfq_depth_updated(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;
	struct blk_mq_tags *tags = hctx->sched_tags;

	bfq_update_depths(bfqd, &tags->bitmap_tags);
	sbitmap_queue_min_shallow_depth(&tags->bitmap_tags, 1);
}

static int bfq_init_hctx(struct blk_mq_hw_ctx *hctx, unsigned int index)
{
	bfq_depth_updated(hctx);
	return 0;
}

static void bfq_exit_queue(struct elevator_queue *e)
{
	struct bfq_data *bfqd = e->elevator_data;
	struct bfq_queue *bfqq, *n;
	unsigned int actuator;

	hrtimer_cancel(&bfqd->idle_slice_timer);

	spin_lock_irq(&bfqd->lock);
	list_for_each_entry_safe(bfqq, n, &bfqd->idle_list, bfqq_list)
		bfq_deactivate_bfqq(bfqd, bfqq, false, false);
	spin_unlock_irq(&bfqd->lock);

	for (actuator = 0; actuator < bfqd->num_actuators; actuator++)
		WARN_ON_ONCE(bfqd->rq_in_driver[actuator]);
	WARN_ON_ONCE(bfqd->tot_rq_in_driver);

	hrtimer_cancel(&bfqd->idle_slice_timer);

	/* release oom-queue reference to root group */
	bfqg_and_blkg_put(bfqd->root_group);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_deactivate_policy(bfqd->queue->disk, &blkcg_policy_bfq);
#else
	spin_lock_irq(&bfqd->lock);
	bfq_put_async_queues(bfqd, bfqd->root_group);
	kfree(bfqd->root_group);
	spin_unlock_irq(&bfqd->lock);
#endif

	blk_stat_disable_accounting(bfqd->queue);
	clear_bit(ELEVATOR_FLAG_DISABLE_WBT, &e->flags);
	wbt_enable_default(bfqd->queue->disk);

	kfree(bfqd);
}

static void bfq_init_root_group(struct bfq_group *root_group,
				struct bfq_data *bfqd)
{
	int i;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	root_group->entity.parent = NULL;
	root_group->my_entity = NULL;
	root_group->bfqd = bfqd;
#endif
	root_group->rq_pos_tree = RB_ROOT;
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		root_group->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;
	root_group->sched_data.bfq_class_idle_last_service = jiffies;
}

static int bfq_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct bfq_data *bfqd;
	struct elevator_queue *eq;
	unsigned int i;
	struct blk_independent_access_ranges *ia_ranges = q->disk->ia_ranges;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	bfqd = kzalloc_node(sizeof(*bfqd), GFP_KERNEL, q->node);
	if (!bfqd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = bfqd;

	spin_lock_irq(&q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(&q->queue_lock);

	/*
	 * Our fallback bfqq if bfq_find_alloc_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.
	 * Set zero as actuator index: we will pretend that
	 * all I/O requests are for the same actuator.
	 */
	bfq_init_bfqq(bfqd, &bfqd->oom_bfqq, NULL, 1, 0, 0);
	bfqd->oom_bfqq.ref++;
	bfqd->oom_bfqq.new_ioprio = BFQ_DEFAULT_QUEUE_IOPRIO;
	bfqd->oom_bfqq.new_ioprio_class = IOPRIO_CLASS_BE;
	bfqd->oom_bfqq.entity.new_weight =
		bfq_ioprio_to_weight(bfqd->oom_bfqq.new_ioprio);

	/* oom_bfqq does not participate to bursts */
	bfq_clear_bfqq_just_created(&bfqd->oom_bfqq);

	/*
	 * Trigger weight initialization, according to ioprio, at the
	 * oom_bfqq's first activation. The oom_bfqq's ioprio and ioprio
	 * class won't be changed any more.
	 */
	bfqd->oom_bfqq.entity.prio_changed = 1;

	bfqd->queue = q;

	bfqd->num_actuators = 1;
	/*
	 * If the disk supports multiple actuators, copy independent
	 * access ranges from the request queue structure.
	 */
	spin_lock_irq(&q->queue_lock);
	if (ia_ranges) {
		/*
		 * Check if the disk ia_ranges size exceeds the current bfq
		 * actuator limit.
		 */
		if (ia_ranges->nr_ia_ranges > BFQ_MAX_ACTUATORS) {
			pr_crit("nr_ia_ranges higher than act limit: iars=%d, max=%d.\n",
				ia_ranges->nr_ia_ranges, BFQ_MAX_ACTUATORS);
			pr_crit("Falling back to single actuator mode.\n");
		} else {
			bfqd->num_actuators = ia_ranges->nr_ia_ranges;

			for (i = 0; i < bfqd->num_actuators; i++) {
				bfqd->sector[i] = ia_ranges->ia_range[i].sector;
				bfqd->nr_sectors[i] =
					ia_ranges->ia_range[i].nr_sectors;
			}
		}
	}

	/* Otherwise use single-actuator dev info */
	if (bfqd->num_actuators == 1) {
		bfqd->sector[0] = 0;
		bfqd->nr_sectors[0] = get_capacity(q->disk);
	}
	spin_unlock_irq(&q->queue_lock);

	INIT_LIST_HEAD(&bfqd->dispatch);

	hrtimer_init(&bfqd->idle_slice_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	bfqd->idle_slice_timer.function = bfq_idle_slice_timer;

	bfqd->queue_weights_tree = RB_ROOT_CACHED;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqd->num_groups_with_pending_reqs = 0;
#endif

	INIT_LIST_HEAD(&bfqd->active_list[0]);
	INIT_LIST_HEAD(&bfqd->active_list[1]);
	INIT_LIST_HEAD(&bfqd->idle_list);
	INIT_HLIST_HEAD(&bfqd->burst_list);

	bfqd->hw_tag = -1;
	bfqd->nonrot_with_queueing = blk_queue_nonrot(bfqd->queue);

	bfqd->bfq_max_budget = bfq_default_max_budget;

	bfqd->bfq_fifo_expire[0] = bfq_fifo_expire[0];
	bfqd->bfq_fifo_expire[1] = bfq_fifo_expire[1];
	bfqd->bfq_back_max = bfq_back_max;
	bfqd->bfq_back_penalty = bfq_back_penalty;
	bfqd->bfq_slice_idle = bfq_slice_idle;
	bfqd->bfq_timeout = bfq_timeout;

	bfqd->bfq_large_burst_thresh = 8;
	bfqd->bfq_burst_interval = msecs_to_jiffies(180);

	bfqd->low_latency = true;

	/*
	 * Trade-off between responsiveness and fairness.
	 */
	bfqd->bfq_wr_coeff = 30;
	bfqd->bfq_wr_rt_max_time = msecs_to_jiffies(300);
	bfqd->bfq_wr_min_idle_time = msecs_to_jiffies(2000);
	bfqd->bfq_wr_min_inter_arr_async = msecs_to_jiffies(500);
	bfqd->bfq_wr_max_softrt_rate = 7000; /*
					      * Approximate rate required
					      * to playback or record a
					      * high-definition compressed
					      * video.
					      */
	bfqd->wr_busy_queues = 0;

	/*
	 * Begin by assuming, optimistically, that the device peak
	 * rate is equal to 2/3 of the highest reference rate.
	 */
	bfqd->rate_dur_prod = ref_rate[blk_queue_nonrot(bfqd->queue)] *
		ref_wr_duration[blk_queue_nonrot(bfqd->queue)];
	bfqd->peak_rate = ref_rate[blk_queue_nonrot(bfqd->queue)] * 2 / 3;

	/* see comments on the definition of next field inside bfq_data */
	bfqd->actuator_load_threshold = 4;

	spin_lock_init(&bfqd->lock);

	/*
	 * The invocation of the next bfq_create_group_hierarchy
	 * function is the head of a chain of function calls
	 * (bfq_create_group_hierarchy->blkcg_activate_policy->
	 * blk_mq_freeze_queue) that may lead to the invocation of the
	 * has_work hook function. For this reason,
	 * bfq_create_group_hierarchy is invoked only after all
	 * scheduler data has been initialized, apart from the fields
	 * that can be initialized only after invoking
	 * bfq_create_group_hierarchy. This, in particular, enables
	 * has_work to correctly return false. Of course, to avoid
	 * other inconsistencies, the blk-mq stack must then refrain
	 * from invoking further scheduler hooks before this init
	 * function is finished.
	 */
	bfqd->root_group = bfq_create_group_hierarchy(bfqd, q->node);
	if (!bfqd->root_group)
		goto out_free;
	bfq_init_root_group(bfqd->root_group, bfqd);
	bfq_init_entity(&bfqd->oom_bfqq.entity, bfqd->root_group);

	/* We dispatch from request queue wide instead of hw queue */
	blk_queue_flag_set(QUEUE_FLAG_SQ_SCHED, q);

	set_bit(ELEVATOR_FLAG_DISABLE_WBT, &eq->flags);
	wbt_disable_default(q->disk);
	blk_stat_enable_accounting(q);

	return 0;

out_free:
	kfree(bfqd);
	kobject_put(&eq->kobj);
	return -ENOMEM;
}

static void bfq_slab_kill(void)
{
	kmem_cache_destroy(bfq_pool);
}

static int __init bfq_slab_setup(void)
{
	bfq_pool = KMEM_CACHE(bfq_queue, 0);
	if (!bfq_pool)
		return -ENOMEM;
	return 0;
}

static ssize_t bfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%u\n", var);
}

static int bfq_var_store(unsigned long *var, const char *page)
{
	unsigned long new_val;
	int ret = kstrtoul(page, 10, &new_val);

	if (ret)
		return ret;
	*var = new_val;
	return 0;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	u64 __data = __VAR;						\
	if (__CONV == 1)						\
		__data = jiffies_to_msecs(__data);			\
	else if (__CONV == 2)						\
		__data = div_u64(__data, NSEC_PER_MSEC);		\
	return bfq_var_show(__data, (page));				\
}
SHOW_FUNCTION(bfq_fifo_expire_sync_show, bfqd->bfq_fifo_expire[1], 2);
SHOW_FUNCTION(bfq_fifo_expire_async_show, bfqd->bfq_fifo_expire[0], 2);
SHOW_FUNCTION(bfq_back_seek_max_show, bfqd->bfq_back_max, 0);
SHOW_FUNCTION(bfq_back_seek_penalty_show, bfqd->bfq_back_penalty, 0);
SHOW_FUNCTION(bfq_slice_idle_show, bfqd->bfq_slice_idle, 2);
SHOW_FUNCTION(bfq_max_budget_show, bfqd->bfq_user_max_budget, 0);
SHOW_FUNCTION(bfq_timeout_sync_show, bfqd->bfq_timeout, 1);
SHOW_FUNCTION(bfq_strict_guarantees_show, bfqd->strict_guarantees, 0);
SHOW_FUNCTION(bfq_low_latency_show, bfqd->low_latency, 0);
#undef SHOW_FUNCTION

#define USEC_SHOW_FUNCTION(__FUNC, __VAR)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	u64 __data = __VAR;						\
	__data = div_u64(__data, NSEC_PER_USEC);			\
	return bfq_var_show(__data, (page));				\
}
USEC_SHOW_FUNCTION(bfq_slice_idle_us_show, bfqd->bfq_slice_idle);
#undef USEC_SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t								\
__FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned long __data, __min = (MIN), __max = (MAX);		\
	int ret;							\
									\
	ret = bfq_var_store(&__data, (page));				\
	if (ret)							\
		return ret;						\
	if (__data < __min)						\
		__data = __min;						\
	else if (__data > __max)					\
		__data = __max;						\
	if (__CONV == 1)						\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else if (__CONV == 2)						\
		*(__PTR) = (u64)__data * NSEC_PER_MSEC;			\
	else								\
		*(__PTR) = __data;					\
	return count;							\
}
STORE_FUNCTION(bfq_fifo_expire_sync_store, &bfqd->bfq_fifo_expire[1], 1,
		INT_MAX, 2);
STORE_FUNCTION(bfq_fifo_expire_async_store, &bfqd->bfq_fifo_expire[0], 1,
		INT_MAX, 2);
STORE_FUNCTION(bfq_back_seek_max_store, &bfqd->bfq_back_max, 0, INT_MAX, 0);
STORE_FUNCTION(bfq_back_seek_penalty_store, &bfqd->bfq_back_penalty, 1,
		INT_MAX, 0);
STORE_FUNCTION(bfq_slice_idle_store, &bfqd->bfq_slice_idle, 0, INT_MAX, 2);
#undef STORE_FUNCTION

#define USEC_STORE_FUNCTION(__FUNC, __PTR, MIN, MAX)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned long __data, __min = (MIN), __max = (MAX);		\
	int ret;							\
									\
	ret = bfq_var_store(&__data, (page));				\
	if (ret)							\
		return ret;						\
	if (__data < __min)						\
		__data = __min;						\
	else if (__data > __max)					\
		__data = __max;						\
	*(__PTR) = (u64)__data * NSEC_PER_USEC;				\
	return count;							\
}
USEC_STORE_FUNCTION(bfq_slice_idle_us_store, &bfqd->bfq_slice_idle, 0,
		    UINT_MAX);
#undef USEC_STORE_FUNCTION

static ssize_t bfq_max_budget_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret;

	ret = bfq_var_store(&__data, (page));
	if (ret)
		return ret;

	if (__data == 0)
		bfqd->bfq_max_budget = bfq_calc_max_budget(bfqd);
	else {
		if (__data > INT_MAX)
			__data = INT_MAX;
		bfqd->bfq_max_budget = __data;
	}

	bfqd->bfq_user_max_budget = __data;

	return count;
}

/*
 * Leaving this name to preserve name compatibility with cfq
 * parameters, but this timeout is used for both sync and async.
 */
static ssize_t bfq_timeout_sync_store(struct elevator_queue *e,
				      const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret;

	ret = bfq_var_store(&__data, (page));
	if (ret)
		return ret;

	if (__data < 1)
		__data = 1;
	else if (__data > INT_MAX)
		__data = INT_MAX;

	bfqd->bfq_timeout = msecs_to_jiffies(__data);
	if (bfqd->bfq_user_max_budget == 0)
		bfqd->bfq_max_budget = bfq_calc_max_budget(bfqd);

	return count;
}

static ssize_t bfq_strict_guarantees_store(struct elevator_queue *e,
				     const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret;

	ret = bfq_var_store(&__data, (page));
	if (ret)
		return ret;

	if (__data > 1)
		__data = 1;
	if (!bfqd->strict_guarantees && __data == 1
	    && bfqd->bfq_slice_idle < 8 * NSEC_PER_MSEC)
		bfqd->bfq_slice_idle = 8 * NSEC_PER_MSEC;

	bfqd->strict_guarantees = __data;

	return count;
}

static ssize_t bfq_low_latency_store(struct elevator_queue *e,
				     const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long __data;
	int ret;

	ret = bfq_var_store(&__data, (page));
	if (ret)
		return ret;

	if (__data > 1)
		__data = 1;
	if (__data == 0 && bfqd->low_latency != 0)
		bfq_end_wr(bfqd);
	bfqd->low_latency = __data;

	return count;
}

#define BFQ_ATTR(name) \
	__ATTR(name, 0644, bfq_##name##_show, bfq_##name##_store)

static const struct elv_fs_entry bfq_attrs[] = {
	BFQ_ATTR(fifo_expire_sync),
	BFQ_ATTR(fifo_expire_async),
	BFQ_ATTR(back_seek_max),
	BFQ_ATTR(back_seek_penalty),
	BFQ_ATTR(slice_idle),
	BFQ_ATTR(slice_idle_us),
	BFQ_ATTR(max_budget),
	BFQ_ATTR(timeout_sync),
	BFQ_ATTR(strict_guarantees),
	BFQ_ATTR(low_latency),
	__ATTR_NULL
};

static struct elevator_type iosched_bfq_mq = {
	.ops = {
		.limit_depth		= bfq_limit_depth,
		.prepare_request	= bfq_prepare_request,
		.requeue_request        = bfq_finish_requeue_request,
		.finish_request		= bfq_finish_request,
		.exit_icq		= bfq_exit_icq,
		.insert_requests	= bfq_insert_requests,
		.dispatch_request	= bfq_dispatch_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.allow_merge		= bfq_allow_bio_merge,
		.bio_merge		= bfq_bio_merge,
		.request_merge		= bfq_request_merge,
		.requests_merged	= bfq_requests_merged,
		.request_merged		= bfq_request_merged,
		.has_work		= bfq_has_work,
		.depth_updated		= bfq_depth_updated,
		.init_hctx		= bfq_init_hctx,
		.init_sched		= bfq_init_queue,
		.exit_sched		= bfq_exit_queue,
	},

	.icq_size =		sizeof(struct bfq_io_cq),
	.icq_align =		__alignof__(struct bfq_io_cq),
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};
MODULE_ALIAS("bfq-iosched");

static int __init bfq_init(void)
{
	int ret;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	ret = blkcg_policy_register(&blkcg_policy_bfq);
	if (ret)
		return ret;
#endif

	ret = -ENOMEM;
	if (bfq_slab_setup())
		goto err_pol_unreg;

	/*
	 * Times to load large popular applications for the typical
	 * systems installed on the reference devices (see the
	 * comments before the definition of the next
	 * array). Actually, we use slightly lower values, as the
	 * estimated peak rate tends to be smaller than the actual
	 * peak rate.  The reason for this last fact is that estimates
	 * are computed over much shorter time intervals than the long
	 * intervals typically used for benchmarking. Why? First, to
	 * adapt more quickly to variations. Second, because an I/O
	 * scheduler cannot rely on a peak-rate-evaluation workload to
	 * be run for a long time.
	 */
	ref_wr_duration[0] = msecs_to_jiffies(7000); /* actually 8 sec */
	ref_wr_duration[1] = msecs_to_jiffies(2500); /* actually 3 sec */

	ret = elv_register(&iosched_bfq_mq);
	if (ret)
		goto slab_kill;

	return 0;

slab_kill:
	bfq_slab_kill();
err_pol_unreg:
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	return ret;
}

static void __exit bfq_exit(void)
{
	elv_unregister(&iosched_bfq_mq);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	bfq_slab_kill();
}

module_init(bfq_init);
module_exit(bfq_exit);

MODULE_AUTHOR("Paolo Valente");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MQ Budget Fair Queueing I/O Scheduler");
