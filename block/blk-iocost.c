/* SPDX-License-Identifier: GPL-2.0
 *
 * IO cost model based controller.
 *
 * Copyright (C) 2019 Tejun Heo <tj@kernel.org>
 * Copyright (C) 2019 Andy Newell <newella@fb.com>
 * Copyright (C) 2019 Facebook
 *
 * One challenge of controlling IO resources is the lack of trivially
 * observable cost metric.  This is distinguished from CPU and memory where
 * wallclock time and the number of bytes can serve as accurate enough
 * approximations.
 *
 * Bandwidth and iops are the most commonly used metrics for IO devices but
 * depending on the type and specifics of the device, different IO patterns
 * easily lead to multiple orders of magnitude variations rendering them
 * useless for the purpose of IO capacity distribution.  While on-device
 * time, with a lot of clutches, could serve as a useful approximation for
 * non-queued rotational devices, this is no longer viable with modern
 * devices, even the rotational ones.
 *
 * While there is no cost metric we can trivially observe, it isn't a
 * complete mystery.  For example, on a rotational device, seek cost
 * dominates while a contiguous transfer contributes a smaller amount
 * proportional to the size.  If we can characterize at least the relative
 * costs of these different types of IOs, it should be possible to
 * implement a reasonable work-conserving proportional IO resource
 * distribution.
 *
 * 1. IO Cost Model
 *
 * IO cost model estimates the cost of an IO given its basic parameters and
 * history (e.g. the end sector of the last IO).  The cost is measured in
 * device time.  If a given IO is estimated to cost 10ms, the device should
 * be able to process ~100 of those IOs in a second.
 *
 * Currently, there's only one builtin cost model - linear.  Each IO is
 * classified as sequential or random and given a base cost accordingly.
 * On top of that, a size cost proportional to the length of the IO is
 * added.  While simple, this model captures the operational
 * characteristics of a wide varienty of devices well enough.  Default
 * paramters for several different classes of devices are provided and the
 * parameters can be configured from userspace via
 * /sys/fs/cgroup/io.cost.model.
 *
 * If needed, tools/cgroup/iocost_coef_gen.py can be used to generate
 * device-specific coefficients.
 *
 * If needed, tools/cgroup/iocost_coef_gen.py can be used to generate
 * device-specific coefficients.
 *
 * 2. Control Strategy
 *
 * The device virtual time (vtime) is used as the primary control metric.
 * The control strategy is composed of the following three parts.
 *
 * 2-1. Vtime Distribution
 *
 * When a cgroup becomes active in terms of IOs, its hierarchical share is
 * calculated.  Please consider the following hierarchy where the numbers
 * inside parentheses denote the configured weights.
 *
 *           root
 *         /       \
 *      A (w:100)  B (w:300)
 *      /       \
 *  A0 (w:100)  A1 (w:100)
 *
 * If B is idle and only A0 and A1 are actively issuing IOs, as the two are
 * of equal weight, each gets 50% share.  If then B starts issuing IOs, B
 * gets 300/(100+300) or 75% share, and A0 and A1 equally splits the rest,
 * 12.5% each.  The distribution mechanism only cares about these flattened
 * shares.  They're called hweights (hierarchical weights) and always add
 * upto 1 (HWEIGHT_WHOLE).
 *
 * A given cgroup's vtime runs slower in inverse proportion to its hweight.
 * For example, with 12.5% weight, A0's time runs 8 times slower (100/12.5)
 * against the device vtime - an IO which takes 10ms on the underlying
 * device is considered to take 80ms on A0.
 *
 * This constitutes the basis of IO capacity distribution.  Each cgroup's
 * vtime is running at a rate determined by its hweight.  A cgroup tracks
 * the vtime consumed by past IOs and can issue a new IO iff doing so
 * wouldn't outrun the current device vtime.  Otherwise, the IO is
 * suspended until the vtime has progressed enough to cover it.
 *
 * 2-2. Vrate Adjustment
 *
 * It's unrealistic to expect the cost model to be perfect.  There are too
 * many devices and even on the same device the overall performance
 * fluctuates depending on numerous factors such as IO mixture and device
 * internal garbage collection.  The controller needs to adapt dynamically.
 *
 * This is achieved by adjusting the overall IO rate according to how busy
 * the device is.  If the device becomes overloaded, we're sending down too
 * many IOs and should generally slow down.  If there are waiting issuers
 * but the device isn't saturated, we're issuing too few and should
 * generally speed up.
 *
 * To slow down, we lower the vrate - the rate at which the device vtime
 * passes compared to the wall clock.  For example, if the vtime is running
 * at the vrate of 75%, all cgroups added up would only be able to issue
 * 750ms worth of IOs per second, and vice-versa for speeding up.
 *
 * Device business is determined using two criteria - rq wait and
 * completion latencies.
 *
 * When a device gets saturated, the on-device and then the request queues
 * fill up and a bio which is ready to be issued has to wait for a request
 * to become available.  When this delay becomes noticeable, it's a clear
 * indication that the device is saturated and we lower the vrate.  This
 * saturation signal is fairly conservative as it only triggers when both
 * hardware and software queues are filled up, and is used as the default
 * busy signal.
 *
 * As devices can have deep queues and be unfair in how the queued commands
 * are executed, soley depending on rq wait may not result in satisfactory
 * control quality.  For a better control quality, completion latency QoS
 * parameters can be configured so that the device is considered saturated
 * if N'th percentile completion latency rises above the set point.
 *
 * The completion latency requirements are a function of both the
 * underlying device characteristics and the desired IO latency quality of
 * service.  There is an inherent trade-off - the tighter the latency QoS,
 * the higher the bandwidth lossage.  Latency QoS is disabled by default
 * and can be set through /sys/fs/cgroup/io.cost.qos.
 *
 * 2-3. Work Conservation
 *
 * Imagine two cgroups A and B with equal weights.  A is issuing a small IO
 * periodically while B is sending out enough parallel IOs to saturate the
 * device on its own.  Let's say A's usage amounts to 100ms worth of IO
 * cost per second, i.e., 10% of the device capacity.  The naive
 * distribution of half and half would lead to 60% utilization of the
 * device, a significant reduction in the total amount of work done
 * compared to free-for-all competition.  This is too high a cost to pay
 * for IO control.
 *
 * To conserve the total amount of work done, we keep track of how much
 * each active cgroup is actually using and yield part of its weight if
 * there are other cgroups which can make use of it.  In the above case,
 * A's weight will be lowered so that it hovers above the actual usage and
 * B would be able to use the rest.
 *
 * As we don't want to penalize a cgroup for donating its weight, the
 * surplus weight adjustment factors in a margin and has an immediate
 * snapback mechanism in case the cgroup needs more IO vtime for itself.
 *
 * Note that adjusting down surplus weights has the same effects as
 * accelerating vtime for other cgroups and work conservation can also be
 * implemented by adjusting vrate dynamically.  However, squaring who can
 * donate and should take back how much requires hweight propagations
 * anyway making it easier to implement and understand as a separate
 * mechanism.
 *
 * 3. Monitoring
 *
 * Instead of debugfs or other clumsy monitoring mechanisms, this
 * controller uses a drgn based monitoring script -
 * tools/cgroup/iocost_monitor.py.  For details on drgn, please see
 * https://github.com/osandov/drgn.  The ouput looks like the following.
 *
 *  sdb RUN   per=300ms cur_per=234.218:v203.695 busy= +1 vrate= 62.12%
 *                 active      weight      hweight% inflt% dbt  delay usages%
 *  test/a              *    50/   50  33.33/ 33.33  27.65   2  0*041 033:033:033
 *  test/b              *   100/  100  66.67/ 66.67  17.56   0  0*000 066:079:077
 *
 * - per	: Timer period
 * - cur_per	: Internal wall and device vtime clock
 * - vrate	: Device virtual time rate against wall clock
 * - weight	: Surplus-adjusted and configured weights
 * - hweight	: Surplus-adjusted and configured hierarchical weights
 * - inflt	: The percentage of in-flight IO cost at the end of last period
 * - del_ms	: Deferred issuer delay induction level and duration
 * - usages	: Usage history
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time64.h>
#include <linux/parser.h>
#include <linux/sched/signal.h>
#include <linux/blk-cgroup.h>
#include "blk-rq-qos.h"
#include "blk-stat.h"
#include "blk-wbt.h"

#ifdef CONFIG_TRACEPOINTS

/* copied from TRACE_CGROUP_PATH, see cgroup-internal.h */
#define TRACE_IOCG_PATH_LEN 1024
static DEFINE_SPINLOCK(trace_iocg_path_lock);
static char trace_iocg_path[TRACE_IOCG_PATH_LEN];

#define TRACE_IOCG_PATH(type, iocg, ...)					\
	do {									\
		unsigned long flags;						\
		if (trace_iocost_##type##_enabled()) {				\
			spin_lock_irqsave(&trace_iocg_path_lock, flags);	\
			cgroup_path(iocg_to_blkg(iocg)->blkcg->css.cgroup,	\
				    trace_iocg_path, TRACE_IOCG_PATH_LEN);	\
			trace_iocost_##type(iocg, trace_iocg_path,		\
					      ##__VA_ARGS__);			\
			spin_unlock_irqrestore(&trace_iocg_path_lock, flags);	\
		}								\
	} while (0)

#else	/* CONFIG_TRACE_POINTS */
#define TRACE_IOCG_PATH(type, iocg, ...)	do { } while (0)
#endif	/* CONFIG_TRACE_POINTS */

enum {
	MILLION			= 1000000,

	/* timer period is calculated from latency requirements, bound it */
	MIN_PERIOD		= USEC_PER_MSEC,
	MAX_PERIOD		= USEC_PER_SEC,

	/*
	 * A cgroup's vtime can run 50% behind the device vtime, which
	 * serves as its IO credit buffer.  Surplus weight adjustment is
	 * immediately canceled if the vtime margin runs below 10%.
	 */
	MARGIN_PCT		= 50,
	INUSE_MARGIN_PCT	= 10,

	/* Have some play in waitq timer operations */
	WAITQ_TIMER_MARGIN_PCT	= 5,

	/*
	 * vtime can wrap well within a reasonable uptime when vrate is
	 * consistently raised.  Don't trust recorded cgroup vtime if the
	 * period counter indicates that it's older than 5mins.
	 */
	VTIME_VALID_DUR		= 300 * USEC_PER_SEC,

	/*
	 * Remember the past three non-zero usages and use the max for
	 * surplus calculation.  Three slots guarantee that we remember one
	 * full period usage from the last active stretch even after
	 * partial deactivation and re-activation periods.  Don't start
	 * giving away weight before collecting two data points to prevent
	 * hweight adjustments based on one partial activation period.
	 */
	NR_USAGE_SLOTS		= 3,
	MIN_VALID_USAGES	= 2,

	/* 1/64k is granular enough and can easily be handled w/ u32 */
	HWEIGHT_WHOLE		= 1 << 16,

	/*
	 * As vtime is used to calculate the cost of each IO, it needs to
	 * be fairly high precision.  For example, it should be able to
	 * represent the cost of a single page worth of discard with
	 * suffificient accuracy.  At the same time, it should be able to
	 * represent reasonably long enough durations to be useful and
	 * convenient during operation.
	 *
	 * 1s worth of vtime is 2^37.  This gives us both sub-nanosecond
	 * granularity and days of wrap-around time even at extreme vrates.
	 */
	VTIME_PER_SEC_SHIFT	= 37,
	VTIME_PER_SEC		= 1LLU << VTIME_PER_SEC_SHIFT,
	VTIME_PER_USEC		= VTIME_PER_SEC / USEC_PER_SEC,

	/* bound vrate adjustments within two orders of magnitude */
	VRATE_MIN_PPM		= 10000,	/* 1% */
	VRATE_MAX_PPM		= 100000000,	/* 10000% */

	VRATE_MIN		= VTIME_PER_USEC * VRATE_MIN_PPM / MILLION,
	VRATE_CLAMP_ADJ_PCT	= 4,

	/* if IOs end up waiting for requests, issue less */
	RQ_WAIT_BUSY_PCT	= 5,

	/* unbusy hysterisis */
	UNBUSY_THR_PCT		= 75,

	/* don't let cmds which take a very long time pin lagging for too long */
	MAX_LAGGING_PERIODS	= 10,

	/*
	 * If usage% * 1.25 + 2% is lower than hweight% by more than 3%,
	 * donate the surplus.
	 */
	SURPLUS_SCALE_PCT	= 125,			/* * 125% */
	SURPLUS_SCALE_ABS	= HWEIGHT_WHOLE / 50,	/* + 2% */
	SURPLUS_MIN_ADJ_DELTA	= HWEIGHT_WHOLE / 33,	/* 3% */

	/* switch iff the conditions are met for longer than this */
	AUTOP_CYCLE_NSEC	= 10LLU * NSEC_PER_SEC,

	/*
	 * Count IO size in 4k pages.  The 12bit shift helps keeping
	 * size-proportional components of cost calculation in closer
	 * numbers of digits to per-IO cost components.
	 */
	IOC_PAGE_SHIFT		= 12,
	IOC_PAGE_SIZE		= 1 << IOC_PAGE_SHIFT,
	IOC_SECT_TO_PAGE_SHIFT	= IOC_PAGE_SHIFT - SECTOR_SHIFT,

	/* if apart further than 16M, consider randio for linear model */
	LCOEF_RANDIO_PAGES	= 4096,
};

enum ioc_running {
	IOC_IDLE,
	IOC_RUNNING,
	IOC_STOP,
};

/* io.cost.qos controls including per-dev enable of the whole controller */
enum {
	QOS_ENABLE,
	QOS_CTRL,
	NR_QOS_CTRL_PARAMS,
};

/* io.cost.qos params */
enum {
	QOS_RPPM,
	QOS_RLAT,
	QOS_WPPM,
	QOS_WLAT,
	QOS_MIN,
	QOS_MAX,
	NR_QOS_PARAMS,
};

/* io.cost.model controls */
enum {
	COST_CTRL,
	COST_MODEL,
	NR_COST_CTRL_PARAMS,
};

/* builtin linear cost model coefficients */
enum {
	I_LCOEF_RBPS,
	I_LCOEF_RSEQIOPS,
	I_LCOEF_RRANDIOPS,
	I_LCOEF_WBPS,
	I_LCOEF_WSEQIOPS,
	I_LCOEF_WRANDIOPS,
	NR_I_LCOEFS,
};

enum {
	LCOEF_RPAGE,
	LCOEF_RSEQIO,
	LCOEF_RRANDIO,
	LCOEF_WPAGE,
	LCOEF_WSEQIO,
	LCOEF_WRANDIO,
	NR_LCOEFS,
};

enum {
	AUTOP_INVALID,
	AUTOP_HDD,
	AUTOP_SSD_QD1,
	AUTOP_SSD_DFL,
	AUTOP_SSD_FAST,
};

struct ioc_gq;

struct ioc_params {
	u32				qos[NR_QOS_PARAMS];
	u64				i_lcoefs[NR_I_LCOEFS];
	u64				lcoefs[NR_LCOEFS];
	u32				too_fast_vrate_pct;
	u32				too_slow_vrate_pct;
};

struct ioc_missed {
	u32				nr_met;
	u32				nr_missed;
	u32				last_met;
	u32				last_missed;
};

struct ioc_pcpu_stat {
	struct ioc_missed		missed[2];

	u64				rq_wait_ns;
	u64				last_rq_wait_ns;
};

/* per device */
struct ioc {
	struct rq_qos			rqos;

	bool				enabled;

	struct ioc_params		params;
	u32				period_us;
	u32				margin_us;
	u64				vrate_min;
	u64				vrate_max;

	spinlock_t			lock;
	struct timer_list		timer;
	struct list_head		active_iocgs;	/* active cgroups */
	struct ioc_pcpu_stat __percpu	*pcpu_stat;

	enum ioc_running		running;
	atomic64_t			vtime_rate;

	seqcount_t			period_seqcount;
	u32				period_at;	/* wallclock starttime */
	u64				period_at_vtime; /* vtime starttime */

	atomic64_t			cur_period;	/* inc'd each period */
	int				busy_level;	/* saturation history */

	u64				inuse_margin_vtime;
	bool				weights_updated;
	atomic_t			hweight_gen;	/* for lazy hweights */

	u64				autop_too_fast_at;
	u64				autop_too_slow_at;
	int				autop_idx;
	bool				user_qos_params:1;
	bool				user_cost_model:1;
};

/* per device-cgroup pair */
struct ioc_gq {
	struct blkg_policy_data		pd;
	struct ioc			*ioc;

	/*
	 * A iocg can get its weight from two sources - an explicit
	 * per-device-cgroup configuration or the default weight of the
	 * cgroup.  `cfg_weight` is the explicit per-device-cgroup
	 * configuration.  `weight` is the effective considering both
	 * sources.
	 *
	 * When an idle cgroup becomes active its `active` goes from 0 to
	 * `weight`.  `inuse` is the surplus adjusted active weight.
	 * `active` and `inuse` are used to calculate `hweight_active` and
	 * `hweight_inuse`.
	 *
	 * `last_inuse` remembers `inuse` while an iocg is idle to persist
	 * surplus adjustments.
	 */
	u32				cfg_weight;
	u32				weight;
	u32				active;
	u32				inuse;
	u32				last_inuse;

	sector_t			cursor;		/* to detect randio */

	/*
	 * `vtime` is this iocg's vtime cursor which progresses as IOs are
	 * issued.  If lagging behind device vtime, the delta represents
	 * the currently available IO budget.  If runnning ahead, the
	 * overage.
	 *
	 * `vtime_done` is the same but progressed on completion rather
	 * than issue.  The delta behind `vtime` represents the cost of
	 * currently in-flight IOs.
	 *
	 * `last_vtime` is used to remember `vtime` at the end of the last
	 * period to calculate utilization.
	 */
	atomic64_t			vtime;
	atomic64_t			done_vtime;
	atomic64_t			abs_vdebt;
	u64				last_vtime;

	/*
	 * The period this iocg was last active in.  Used for deactivation
	 * and invalidating `vtime`.
	 */
	atomic64_t			active_period;
	struct list_head		active_list;

	/* see __propagate_active_weight() and current_hweight() for details */
	u64				child_active_sum;
	u64				child_inuse_sum;
	int				hweight_gen;
	u32				hweight_active;
	u32				hweight_inuse;
	bool				has_surplus;

	struct wait_queue_head		waitq;
	struct hrtimer			waitq_timer;
	struct hrtimer			delay_timer;

	/* usage is recorded as fractions of HWEIGHT_WHOLE */
	int				usage_idx;
	u32				usages[NR_USAGE_SLOTS];

	/* this iocg's depth in the hierarchy and ancestors including self */
	int				level;
	struct ioc_gq			*ancestors[];
};

/* per cgroup */
struct ioc_cgrp {
	struct blkcg_policy_data	cpd;
	unsigned int			dfl_weight;
};

struct ioc_now {
	u64				now_ns;
	u32				now;
	u64				vnow;
	u64				vrate;
};

struct iocg_wait {
	struct wait_queue_entry		wait;
	struct bio			*bio;
	u64				abs_cost;
	bool				committed;
};

struct iocg_wake_ctx {
	struct ioc_gq			*iocg;
	u32				hw_inuse;
	s64				vbudget;
};

static const struct ioc_params autop[] = {
	[AUTOP_HDD] = {
		.qos				= {
			[QOS_RLAT]		=        250000, /* 250ms */
			[QOS_WLAT]		=        250000,
			[QOS_MIN]		= VRATE_MIN_PPM,
			[QOS_MAX]		= VRATE_MAX_PPM,
		},
		.i_lcoefs			= {
			[I_LCOEF_RBPS]		=     174019176,
			[I_LCOEF_RSEQIOPS]	=         41708,
			[I_LCOEF_RRANDIOPS]	=           370,
			[I_LCOEF_WBPS]		=     178075866,
			[I_LCOEF_WSEQIOPS]	=         42705,
			[I_LCOEF_WRANDIOPS]	=           378,
		},
	},
	[AUTOP_SSD_QD1] = {
		.qos				= {
			[QOS_RLAT]		=         25000, /* 25ms */
			[QOS_WLAT]		=         25000,
			[QOS_MIN]		= VRATE_MIN_PPM,
			[QOS_MAX]		= VRATE_MAX_PPM,
		},
		.i_lcoefs			= {
			[I_LCOEF_RBPS]		=     245855193,
			[I_LCOEF_RSEQIOPS]	=         61575,
			[I_LCOEF_RRANDIOPS]	=          6946,
			[I_LCOEF_WBPS]		=     141365009,
			[I_LCOEF_WSEQIOPS]	=         33716,
			[I_LCOEF_WRANDIOPS]	=         26796,
		},
	},
	[AUTOP_SSD_DFL] = {
		.qos				= {
			[QOS_RLAT]		=         25000, /* 25ms */
			[QOS_WLAT]		=         25000,
			[QOS_MIN]		= VRATE_MIN_PPM,
			[QOS_MAX]		= VRATE_MAX_PPM,
		},
		.i_lcoefs			= {
			[I_LCOEF_RBPS]		=     488636629,
			[I_LCOEF_RSEQIOPS]	=          8932,
			[I_LCOEF_RRANDIOPS]	=          8518,
			[I_LCOEF_WBPS]		=     427891549,
			[I_LCOEF_WSEQIOPS]	=         28755,
			[I_LCOEF_WRANDIOPS]	=         21940,
		},
		.too_fast_vrate_pct		=           500,
	},
	[AUTOP_SSD_FAST] = {
		.qos				= {
			[QOS_RLAT]		=          5000, /* 5ms */
			[QOS_WLAT]		=          5000,
			[QOS_MIN]		= VRATE_MIN_PPM,
			[QOS_MAX]		= VRATE_MAX_PPM,
		},
		.i_lcoefs			= {
			[I_LCOEF_RBPS]		=    3102524156LLU,
			[I_LCOEF_RSEQIOPS]	=        724816,
			[I_LCOEF_RRANDIOPS]	=        778122,
			[I_LCOEF_WBPS]		=    1742780862LLU,
			[I_LCOEF_WSEQIOPS]	=        425702,
			[I_LCOEF_WRANDIOPS]	=	 443193,
		},
		.too_slow_vrate_pct		=            10,
	},
};

/*
 * vrate adjust percentages indexed by ioc->busy_level.  We adjust up on
 * vtime credit shortage and down on device saturation.
 */
static u32 vrate_adj_pct[] =
	{ 0, 0, 0, 0,
	  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	  4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 16 };

static struct blkcg_policy blkcg_policy_iocost;

/* accessors and helpers */
static struct ioc *rqos_to_ioc(struct rq_qos *rqos)
{
	return container_of(rqos, struct ioc, rqos);
}

static struct ioc *q_to_ioc(struct request_queue *q)
{
	return rqos_to_ioc(rq_qos_id(q, RQ_QOS_COST));
}

static const char *q_name(struct request_queue *q)
{
	if (test_bit(QUEUE_FLAG_REGISTERED, &q->queue_flags))
		return kobject_name(q->kobj.parent);
	else
		return "<unknown>";
}

static const char __maybe_unused *ioc_name(struct ioc *ioc)
{
	return q_name(ioc->rqos.q);
}

static struct ioc_gq *pd_to_iocg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct ioc_gq, pd) : NULL;
}

static struct ioc_gq *blkg_to_iocg(struct blkcg_gq *blkg)
{
	return pd_to_iocg(blkg_to_pd(blkg, &blkcg_policy_iocost));
}

static struct blkcg_gq *iocg_to_blkg(struct ioc_gq *iocg)
{
	return pd_to_blkg(&iocg->pd);
}

static struct ioc_cgrp *blkcg_to_iocc(struct blkcg *blkcg)
{
	return container_of(blkcg_to_cpd(blkcg, &blkcg_policy_iocost),
			    struct ioc_cgrp, cpd);
}

/*
 * Scale @abs_cost to the inverse of @hw_inuse.  The lower the hierarchical
 * weight, the more expensive each IO.  Must round up.
 */
static u64 abs_cost_to_cost(u64 abs_cost, u32 hw_inuse)
{
	return DIV64_U64_ROUND_UP(abs_cost * HWEIGHT_WHOLE, hw_inuse);
}

/*
 * The inverse of abs_cost_to_cost().  Must round up.
 */
static u64 cost_to_abs_cost(u64 cost, u32 hw_inuse)
{
	return DIV64_U64_ROUND_UP(cost * hw_inuse, HWEIGHT_WHOLE);
}

static void iocg_commit_bio(struct ioc_gq *iocg, struct bio *bio, u64 cost)
{
	bio->bi_iocost_cost = cost;
	atomic64_add(cost, &iocg->vtime);
}

#define CREATE_TRACE_POINTS
#include <trace/events/iocost.h>

/* latency Qos params changed, update period_us and all the dependent params */
static void ioc_refresh_period_us(struct ioc *ioc)
{
	u32 ppm, lat, multi, period_us;

	lockdep_assert_held(&ioc->lock);

	/* pick the higher latency target */
	if (ioc->params.qos[QOS_RLAT] >= ioc->params.qos[QOS_WLAT]) {
		ppm = ioc->params.qos[QOS_RPPM];
		lat = ioc->params.qos[QOS_RLAT];
	} else {
		ppm = ioc->params.qos[QOS_WPPM];
		lat = ioc->params.qos[QOS_WLAT];
	}

	/*
	 * We want the period to be long enough to contain a healthy number
	 * of IOs while short enough for granular control.  Define it as a
	 * multiple of the latency target.  Ideally, the multiplier should
	 * be scaled according to the percentile so that it would nominally
	 * contain a certain number of requests.  Let's be simpler and
	 * scale it linearly so that it's 2x >= pct(90) and 10x at pct(50).
	 */
	if (ppm)
		multi = max_t(u32, (MILLION - ppm) / 50000, 2);
	else
		multi = 2;
	period_us = multi * lat;
	period_us = clamp_t(u32, period_us, MIN_PERIOD, MAX_PERIOD);

	/* calculate dependent params */
	ioc->period_us = period_us;
	ioc->margin_us = period_us * MARGIN_PCT / 100;
	ioc->inuse_margin_vtime = DIV64_U64_ROUND_UP(
			period_us * VTIME_PER_USEC * INUSE_MARGIN_PCT, 100);
}

static int ioc_autop_idx(struct ioc *ioc)
{
	int idx = ioc->autop_idx;
	const struct ioc_params *p = &autop[idx];
	u32 vrate_pct;
	u64 now_ns;

	/* rotational? */
	if (!blk_queue_nonrot(ioc->rqos.q))
		return AUTOP_HDD;

	/* handle SATA SSDs w/ broken NCQ */
	if (blk_queue_depth(ioc->rqos.q) == 1)
		return AUTOP_SSD_QD1;

	/* use one of the normal ssd sets */
	if (idx < AUTOP_SSD_DFL)
		return AUTOP_SSD_DFL;

	/* if user is overriding anything, maintain what was there */
	if (ioc->user_qos_params || ioc->user_cost_model)
		return idx;

	/* step up/down based on the vrate */
	vrate_pct = div64_u64(atomic64_read(&ioc->vtime_rate) * 100,
			      VTIME_PER_USEC);
	now_ns = ktime_get_ns();

	if (p->too_fast_vrate_pct && p->too_fast_vrate_pct <= vrate_pct) {
		if (!ioc->autop_too_fast_at)
			ioc->autop_too_fast_at = now_ns;
		if (now_ns - ioc->autop_too_fast_at >= AUTOP_CYCLE_NSEC)
			return idx + 1;
	} else {
		ioc->autop_too_fast_at = 0;
	}

	if (p->too_slow_vrate_pct && p->too_slow_vrate_pct >= vrate_pct) {
		if (!ioc->autop_too_slow_at)
			ioc->autop_too_slow_at = now_ns;
		if (now_ns - ioc->autop_too_slow_at >= AUTOP_CYCLE_NSEC)
			return idx - 1;
	} else {
		ioc->autop_too_slow_at = 0;
	}

	return idx;
}

/*
 * Take the followings as input
 *
 *  @bps	maximum sequential throughput
 *  @seqiops	maximum sequential 4k iops
 *  @randiops	maximum random 4k iops
 *
 * and calculate the linear model cost coefficients.
 *
 *  *@page	per-page cost		1s / (@bps / 4096)
 *  *@seqio	base cost of a seq IO	max((1s / @seqiops) - *@page, 0)
 *  @randiops	base cost of a rand IO	max((1s / @randiops) - *@page, 0)
 */
static void calc_lcoefs(u64 bps, u64 seqiops, u64 randiops,
			u64 *page, u64 *seqio, u64 *randio)
{
	u64 v;

	*page = *seqio = *randio = 0;

	if (bps)
		*page = DIV64_U64_ROUND_UP(VTIME_PER_SEC,
					   DIV_ROUND_UP_ULL(bps, IOC_PAGE_SIZE));

	if (seqiops) {
		v = DIV64_U64_ROUND_UP(VTIME_PER_SEC, seqiops);
		if (v > *page)
			*seqio = v - *page;
	}

	if (randiops) {
		v = DIV64_U64_ROUND_UP(VTIME_PER_SEC, randiops);
		if (v > *page)
			*randio = v - *page;
	}
}

static void ioc_refresh_lcoefs(struct ioc *ioc)
{
	u64 *u = ioc->params.i_lcoefs;
	u64 *c = ioc->params.lcoefs;

	calc_lcoefs(u[I_LCOEF_RBPS], u[I_LCOEF_RSEQIOPS], u[I_LCOEF_RRANDIOPS],
		    &c[LCOEF_RPAGE], &c[LCOEF_RSEQIO], &c[LCOEF_RRANDIO]);
	calc_lcoefs(u[I_LCOEF_WBPS], u[I_LCOEF_WSEQIOPS], u[I_LCOEF_WRANDIOPS],
		    &c[LCOEF_WPAGE], &c[LCOEF_WSEQIO], &c[LCOEF_WRANDIO]);
}

static bool ioc_refresh_params(struct ioc *ioc, bool force)
{
	const struct ioc_params *p;
	int idx;

	lockdep_assert_held(&ioc->lock);

	idx = ioc_autop_idx(ioc);
	p = &autop[idx];

	if (idx == ioc->autop_idx && !force)
		return false;

	if (idx != ioc->autop_idx)
		atomic64_set(&ioc->vtime_rate, VTIME_PER_USEC);

	ioc->autop_idx = idx;
	ioc->autop_too_fast_at = 0;
	ioc->autop_too_slow_at = 0;

	if (!ioc->user_qos_params)
		memcpy(ioc->params.qos, p->qos, sizeof(p->qos));
	if (!ioc->user_cost_model)
		memcpy(ioc->params.i_lcoefs, p->i_lcoefs, sizeof(p->i_lcoefs));

	ioc_refresh_period_us(ioc);
	ioc_refresh_lcoefs(ioc);

	ioc->vrate_min = DIV64_U64_ROUND_UP((u64)ioc->params.qos[QOS_MIN] *
					    VTIME_PER_USEC, MILLION);
	ioc->vrate_max = div64_u64((u64)ioc->params.qos[QOS_MAX] *
				   VTIME_PER_USEC, MILLION);

	return true;
}

/* take a snapshot of the current [v]time and vrate */
static void ioc_now(struct ioc *ioc, struct ioc_now *now)
{
	unsigned seq;

	now->now_ns = ktime_get();
	now->now = ktime_to_us(now->now_ns);
	now->vrate = atomic64_read(&ioc->vtime_rate);

	/*
	 * The current vtime is
	 *
	 *   vtime at period start + (wallclock time since the start) * vrate
	 *
	 * As a consistent snapshot of `period_at_vtime` and `period_at` is
	 * needed, they're seqcount protected.
	 */
	do {
		seq = read_seqcount_begin(&ioc->period_seqcount);
		now->vnow = ioc->period_at_vtime +
			(now->now - ioc->period_at) * now->vrate;
	} while (read_seqcount_retry(&ioc->period_seqcount, seq));
}

static void ioc_start_period(struct ioc *ioc, struct ioc_now *now)
{
	lockdep_assert_held(&ioc->lock);
	WARN_ON_ONCE(ioc->running != IOC_RUNNING);

	write_seqcount_begin(&ioc->period_seqcount);
	ioc->period_at = now->now;
	ioc->period_at_vtime = now->vnow;
	write_seqcount_end(&ioc->period_seqcount);

	ioc->timer.expires = jiffies + usecs_to_jiffies(ioc->period_us);
	add_timer(&ioc->timer);
}

/*
 * Update @iocg's `active` and `inuse` to @active and @inuse, update level
 * weight sums and propagate upwards accordingly.
 */
static void __propagate_active_weight(struct ioc_gq *iocg, u32 active, u32 inuse)
{
	struct ioc *ioc = iocg->ioc;
	int lvl;

	lockdep_assert_held(&ioc->lock);

	inuse = min(active, inuse);

	for (lvl = iocg->level - 1; lvl >= 0; lvl--) {
		struct ioc_gq *parent = iocg->ancestors[lvl];
		struct ioc_gq *child = iocg->ancestors[lvl + 1];
		u32 parent_active = 0, parent_inuse = 0;

		/* update the level sums */
		parent->child_active_sum += (s32)(active - child->active);
		parent->child_inuse_sum += (s32)(inuse - child->inuse);
		/* apply the udpates */
		child->active = active;
		child->inuse = inuse;

		/*
		 * The delta between inuse and active sums indicates that
		 * that much of weight is being given away.  Parent's inuse
		 * and active should reflect the ratio.
		 */
		if (parent->child_active_sum) {
			parent_active = parent->weight;
			parent_inuse = DIV64_U64_ROUND_UP(
				parent_active * parent->child_inuse_sum,
				parent->child_active_sum);
		}

		/* do we need to keep walking up? */
		if (parent_active == parent->active &&
		    parent_inuse == parent->inuse)
			break;

		active = parent_active;
		inuse = parent_inuse;
	}

	ioc->weights_updated = true;
}

static void commit_active_weights(struct ioc *ioc)
{
	lockdep_assert_held(&ioc->lock);

	if (ioc->weights_updated) {
		/* paired with rmb in current_hweight(), see there */
		smp_wmb();
		atomic_inc(&ioc->hweight_gen);
		ioc->weights_updated = false;
	}
}

static void propagate_active_weight(struct ioc_gq *iocg, u32 active, u32 inuse)
{
	__propagate_active_weight(iocg, active, inuse);
	commit_active_weights(iocg->ioc);
}

static void current_hweight(struct ioc_gq *iocg, u32 *hw_activep, u32 *hw_inusep)
{
	struct ioc *ioc = iocg->ioc;
	int lvl;
	u32 hwa, hwi;
	int ioc_gen;

	/* hot path - if uptodate, use cached */
	ioc_gen = atomic_read(&ioc->hweight_gen);
	if (ioc_gen == iocg->hweight_gen)
		goto out;

	/*
	 * Paired with wmb in commit_active_weights().  If we saw the
	 * updated hweight_gen, all the weight updates from
	 * __propagate_active_weight() are visible too.
	 *
	 * We can race with weight updates during calculation and get it
	 * wrong.  However, hweight_gen would have changed and a future
	 * reader will recalculate and we're guaranteed to discard the
	 * wrong result soon.
	 */
	smp_rmb();

	hwa = hwi = HWEIGHT_WHOLE;
	for (lvl = 0; lvl <= iocg->level - 1; lvl++) {
		struct ioc_gq *parent = iocg->ancestors[lvl];
		struct ioc_gq *child = iocg->ancestors[lvl + 1];
		u32 active_sum = READ_ONCE(parent->child_active_sum);
		u32 inuse_sum = READ_ONCE(parent->child_inuse_sum);
		u32 active = READ_ONCE(child->active);
		u32 inuse = READ_ONCE(child->inuse);

		/* we can race with deactivations and either may read as zero */
		if (!active_sum || !inuse_sum)
			continue;

		active_sum = max(active, active_sum);
		hwa = hwa * active / active_sum;	/* max 16bits * 10000 */

		inuse_sum = max(inuse, inuse_sum);
		hwi = hwi * inuse / inuse_sum;		/* max 16bits * 10000 */
	}

	iocg->hweight_active = max_t(u32, hwa, 1);
	iocg->hweight_inuse = max_t(u32, hwi, 1);
	iocg->hweight_gen = ioc_gen;
out:
	if (hw_activep)
		*hw_activep = iocg->hweight_active;
	if (hw_inusep)
		*hw_inusep = iocg->hweight_inuse;
}

static void weight_updated(struct ioc_gq *iocg)
{
	struct ioc *ioc = iocg->ioc;
	struct blkcg_gq *blkg = iocg_to_blkg(iocg);
	struct ioc_cgrp *iocc = blkcg_to_iocc(blkg->blkcg);
	u32 weight;

	lockdep_assert_held(&ioc->lock);

	weight = iocg->cfg_weight ?: iocc->dfl_weight;
	if (weight != iocg->weight && iocg->active)
		propagate_active_weight(iocg, weight,
			DIV64_U64_ROUND_UP(iocg->inuse * weight, iocg->weight));
	iocg->weight = weight;
}

static bool iocg_activate(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	u64 last_period, cur_period, max_period_delta;
	u64 vtime, vmargin, vmin;
	int i;

	/*
	 * If seem to be already active, just update the stamp to tell the
	 * timer that we're still active.  We don't mind occassional races.
	 */
	if (!list_empty(&iocg->active_list)) {
		ioc_now(ioc, now);
		cur_period = atomic64_read(&ioc->cur_period);
		if (atomic64_read(&iocg->active_period) != cur_period)
			atomic64_set(&iocg->active_period, cur_period);
		return true;
	}

	/* racy check on internal node IOs, treat as root level IOs */
	if (iocg->child_active_sum)
		return false;

	spin_lock_irq(&ioc->lock);

	ioc_now(ioc, now);

	/* update period */
	cur_period = atomic64_read(&ioc->cur_period);
	last_period = atomic64_read(&iocg->active_period);
	atomic64_set(&iocg->active_period, cur_period);

	/* already activated or breaking leaf-only constraint? */
	if (!list_empty(&iocg->active_list))
		goto succeed_unlock;
	for (i = iocg->level - 1; i > 0; i--)
		if (!list_empty(&iocg->ancestors[i]->active_list))
			goto fail_unlock;

	if (iocg->child_active_sum)
		goto fail_unlock;

	/*
	 * vtime may wrap when vrate is raised substantially due to
	 * underestimated IO costs.  Look at the period and ignore its
	 * vtime if the iocg has been idle for too long.  Also, cap the
	 * budget it can start with to the margin.
	 */
	max_period_delta = DIV64_U64_ROUND_UP(VTIME_VALID_DUR, ioc->period_us);
	vtime = atomic64_read(&iocg->vtime);
	vmargin = ioc->margin_us * now->vrate;
	vmin = now->vnow - vmargin;

	if (last_period + max_period_delta < cur_period ||
	    time_before64(vtime, vmin)) {
		atomic64_add(vmin - vtime, &iocg->vtime);
		atomic64_add(vmin - vtime, &iocg->done_vtime);
		vtime = vmin;
	}

	/*
	 * Activate, propagate weight and start period timer if not
	 * running.  Reset hweight_gen to avoid accidental match from
	 * wrapping.
	 */
	iocg->hweight_gen = atomic_read(&ioc->hweight_gen) - 1;
	list_add(&iocg->active_list, &ioc->active_iocgs);
	propagate_active_weight(iocg, iocg->weight,
				iocg->last_inuse ?: iocg->weight);

	TRACE_IOCG_PATH(iocg_activate, iocg, now,
			last_period, cur_period, vtime);

	iocg->last_vtime = vtime;

	if (ioc->running == IOC_IDLE) {
		ioc->running = IOC_RUNNING;
		ioc_start_period(ioc, now);
	}

succeed_unlock:
	spin_unlock_irq(&ioc->lock);
	return true;

fail_unlock:
	spin_unlock_irq(&ioc->lock);
	return false;
}

static int iocg_wake_fn(struct wait_queue_entry *wq_entry, unsigned mode,
			int flags, void *key)
{
	struct iocg_wait *wait = container_of(wq_entry, struct iocg_wait, wait);
	struct iocg_wake_ctx *ctx = (struct iocg_wake_ctx *)key;
	u64 cost = abs_cost_to_cost(wait->abs_cost, ctx->hw_inuse);

	ctx->vbudget -= cost;

	if (ctx->vbudget < 0)
		return -1;

	iocg_commit_bio(ctx->iocg, wait->bio, cost);

	/*
	 * autoremove_wake_function() removes the wait entry only when it
	 * actually changed the task state.  We want the wait always
	 * removed.  Remove explicitly and use default_wake_function().
	 */
	list_del_init(&wq_entry->entry);
	wait->committed = true;

	default_wake_function(wq_entry, mode, flags, key);
	return 0;
}

static void iocg_kick_waitq(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	struct iocg_wake_ctx ctx = { .iocg = iocg };
	u64 margin_ns = (u64)(ioc->period_us *
			      WAITQ_TIMER_MARGIN_PCT / 100) * NSEC_PER_USEC;
	u64 abs_vdebt, vdebt, vshortage, expires, oexpires;
	s64 vbudget;
	u32 hw_inuse;

	lockdep_assert_held(&iocg->waitq.lock);

	current_hweight(iocg, NULL, &hw_inuse);
	vbudget = now->vnow - atomic64_read(&iocg->vtime);

	/* pay off debt */
	abs_vdebt = atomic64_read(&iocg->abs_vdebt);
	vdebt = abs_cost_to_cost(abs_vdebt, hw_inuse);
	if (vdebt && vbudget > 0) {
		u64 delta = min_t(u64, vbudget, vdebt);
		u64 abs_delta = min(cost_to_abs_cost(delta, hw_inuse),
				    abs_vdebt);

		atomic64_add(delta, &iocg->vtime);
		atomic64_add(delta, &iocg->done_vtime);
		atomic64_sub(abs_delta, &iocg->abs_vdebt);
		if (WARN_ON_ONCE(atomic64_read(&iocg->abs_vdebt) < 0))
			atomic64_set(&iocg->abs_vdebt, 0);
	}

	/*
	 * Wake up the ones which are due and see how much vtime we'll need
	 * for the next one.
	 */
	ctx.hw_inuse = hw_inuse;
	ctx.vbudget = vbudget - vdebt;
	__wake_up_locked_key(&iocg->waitq, TASK_NORMAL, &ctx);
	if (!waitqueue_active(&iocg->waitq))
		return;
	if (WARN_ON_ONCE(ctx.vbudget >= 0))
		return;

	/* determine next wakeup, add a quarter margin to guarantee chunking */
	vshortage = -ctx.vbudget;
	expires = now->now_ns +
		DIV64_U64_ROUND_UP(vshortage, now->vrate) * NSEC_PER_USEC;
	expires += margin_ns / 4;

	/* if already active and close enough, don't bother */
	oexpires = ktime_to_ns(hrtimer_get_softexpires(&iocg->waitq_timer));
	if (hrtimer_is_queued(&iocg->waitq_timer) &&
	    abs(oexpires - expires) <= margin_ns / 4)
		return;

	hrtimer_start_range_ns(&iocg->waitq_timer, ns_to_ktime(expires),
			       margin_ns / 4, HRTIMER_MODE_ABS);
}

static enum hrtimer_restart iocg_waitq_timer_fn(struct hrtimer *timer)
{
	struct ioc_gq *iocg = container_of(timer, struct ioc_gq, waitq_timer);
	struct ioc_now now;
	unsigned long flags;

	ioc_now(iocg->ioc, &now);

	spin_lock_irqsave(&iocg->waitq.lock, flags);
	iocg_kick_waitq(iocg, &now);
	spin_unlock_irqrestore(&iocg->waitq.lock, flags);

	return HRTIMER_NORESTART;
}

static void iocg_kick_delay(struct ioc_gq *iocg, struct ioc_now *now, u64 cost)
{
	struct ioc *ioc = iocg->ioc;
	struct blkcg_gq *blkg = iocg_to_blkg(iocg);
	u64 vtime = atomic64_read(&iocg->vtime);
	u64 vmargin = ioc->margin_us * now->vrate;
	u64 margin_ns = ioc->margin_us * NSEC_PER_USEC;
	u64 expires, oexpires;
	u32 hw_inuse;

	/* debt-adjust vtime */
	current_hweight(iocg, NULL, &hw_inuse);
	vtime += abs_cost_to_cost(atomic64_read(&iocg->abs_vdebt), hw_inuse);

	/* clear or maintain depending on the overage */
	if (time_before_eq64(vtime, now->vnow)) {
		blkcg_clear_delay(blkg);
		return;
	}
	if (!atomic_read(&blkg->use_delay) &&
	    time_before_eq64(vtime, now->vnow + vmargin))
		return;

	/* use delay */
	if (cost) {
		u64 cost_ns = DIV64_U64_ROUND_UP(cost * NSEC_PER_USEC,
						 now->vrate);
		blkcg_add_delay(blkg, now->now_ns, cost_ns);
	}
	blkcg_use_delay(blkg);

	expires = now->now_ns + DIV64_U64_ROUND_UP(vtime - now->vnow,
						   now->vrate) * NSEC_PER_USEC;

	/* if already active and close enough, don't bother */
	oexpires = ktime_to_ns(hrtimer_get_softexpires(&iocg->delay_timer));
	if (hrtimer_is_queued(&iocg->delay_timer) &&
	    abs(oexpires - expires) <= margin_ns / 4)
		return;

	hrtimer_start_range_ns(&iocg->delay_timer, ns_to_ktime(expires),
			       margin_ns / 4, HRTIMER_MODE_ABS);
}

static enum hrtimer_restart iocg_delay_timer_fn(struct hrtimer *timer)
{
	struct ioc_gq *iocg = container_of(timer, struct ioc_gq, delay_timer);
	struct ioc_now now;

	ioc_now(iocg->ioc, &now);
	iocg_kick_delay(iocg, &now, 0);

	return HRTIMER_NORESTART;
}

static void ioc_lat_stat(struct ioc *ioc, u32 *missed_ppm_ar, u32 *rq_wait_pct_p)
{
	u32 nr_met[2] = { };
	u32 nr_missed[2] = { };
	u64 rq_wait_ns = 0;
	int cpu, rw;

	for_each_online_cpu(cpu) {
		struct ioc_pcpu_stat *stat = per_cpu_ptr(ioc->pcpu_stat, cpu);
		u64 this_rq_wait_ns;

		for (rw = READ; rw <= WRITE; rw++) {
			u32 this_met = READ_ONCE(stat->missed[rw].nr_met);
			u32 this_missed = READ_ONCE(stat->missed[rw].nr_missed);

			nr_met[rw] += this_met - stat->missed[rw].last_met;
			nr_missed[rw] += this_missed - stat->missed[rw].last_missed;
			stat->missed[rw].last_met = this_met;
			stat->missed[rw].last_missed = this_missed;
		}

		this_rq_wait_ns = READ_ONCE(stat->rq_wait_ns);
		rq_wait_ns += this_rq_wait_ns - stat->last_rq_wait_ns;
		stat->last_rq_wait_ns = this_rq_wait_ns;
	}

	for (rw = READ; rw <= WRITE; rw++) {
		if (nr_met[rw] + nr_missed[rw])
			missed_ppm_ar[rw] =
				DIV64_U64_ROUND_UP((u64)nr_missed[rw] * MILLION,
						   nr_met[rw] + nr_missed[rw]);
		else
			missed_ppm_ar[rw] = 0;
	}

	*rq_wait_pct_p = div64_u64(rq_wait_ns * 100,
				   ioc->period_us * NSEC_PER_USEC);
}

/* was iocg idle this period? */
static bool iocg_is_idle(struct ioc_gq *iocg)
{
	struct ioc *ioc = iocg->ioc;

	/* did something get issued this period? */
	if (atomic64_read(&iocg->active_period) ==
	    atomic64_read(&ioc->cur_period))
		return false;

	/* is something in flight? */
	if (atomic64_read(&iocg->done_vtime) < atomic64_read(&iocg->vtime))
		return false;

	return true;
}

/* returns usage with margin added if surplus is large enough */
static u32 surplus_adjusted_hweight_inuse(u32 usage, u32 hw_inuse)
{
	/* add margin */
	usage = DIV_ROUND_UP(usage * SURPLUS_SCALE_PCT, 100);
	usage += SURPLUS_SCALE_ABS;

	/* don't bother if the surplus is too small */
	if (usage + SURPLUS_MIN_ADJ_DELTA > hw_inuse)
		return 0;

	return usage;
}

static void ioc_timer_fn(struct timer_list *timer)
{
	struct ioc *ioc = container_of(timer, struct ioc, timer);
	struct ioc_gq *iocg, *tiocg;
	struct ioc_now now;
	int nr_surpluses = 0, nr_shortages = 0, nr_lagging = 0;
	u32 ppm_rthr = MILLION - ioc->params.qos[QOS_RPPM];
	u32 ppm_wthr = MILLION - ioc->params.qos[QOS_WPPM];
	u32 missed_ppm[2], rq_wait_pct;
	u64 period_vtime;
	int prev_busy_level, i;

	/* how were the latencies during the period? */
	ioc_lat_stat(ioc, missed_ppm, &rq_wait_pct);

	/* take care of active iocgs */
	spin_lock_irq(&ioc->lock);

	ioc_now(ioc, &now);

	period_vtime = now.vnow - ioc->period_at_vtime;
	if (WARN_ON_ONCE(!period_vtime)) {
		spin_unlock_irq(&ioc->lock);
		return;
	}

	/*
	 * Waiters determine the sleep durations based on the vrate they
	 * saw at the time of sleep.  If vrate has increased, some waiters
	 * could be sleeping for too long.  Wake up tardy waiters which
	 * should have woken up in the last period and expire idle iocgs.
	 */
	list_for_each_entry_safe(iocg, tiocg, &ioc->active_iocgs, active_list) {
		if (!waitqueue_active(&iocg->waitq) &&
		    !atomic64_read(&iocg->abs_vdebt) && !iocg_is_idle(iocg))
			continue;

		spin_lock(&iocg->waitq.lock);

		if (waitqueue_active(&iocg->waitq) ||
		    atomic64_read(&iocg->abs_vdebt)) {
			/* might be oversleeping vtime / hweight changes, kick */
			iocg_kick_waitq(iocg, &now);
			iocg_kick_delay(iocg, &now, 0);
		} else if (iocg_is_idle(iocg)) {
			/* no waiter and idle, deactivate */
			iocg->last_inuse = iocg->inuse;
			__propagate_active_weight(iocg, 0, 0);
			list_del_init(&iocg->active_list);
		}

		spin_unlock(&iocg->waitq.lock);
	}
	commit_active_weights(ioc);

	/* calc usages and see whether some weights need to be moved around */
	list_for_each_entry(iocg, &ioc->active_iocgs, active_list) {
		u64 vdone, vtime, vusage, vmargin, vmin;
		u32 hw_active, hw_inuse, usage;

		/*
		 * Collect unused and wind vtime closer to vnow to prevent
		 * iocgs from accumulating a large amount of budget.
		 */
		vdone = atomic64_read(&iocg->done_vtime);
		vtime = atomic64_read(&iocg->vtime);
		current_hweight(iocg, &hw_active, &hw_inuse);

		/*
		 * Latency QoS detection doesn't account for IOs which are
		 * in-flight for longer than a period.  Detect them by
		 * comparing vdone against period start.  If lagging behind
		 * IOs from past periods, don't increase vrate.
		 */
		if ((ppm_rthr != MILLION || ppm_wthr != MILLION) &&
		    !atomic_read(&iocg_to_blkg(iocg)->use_delay) &&
		    time_after64(vtime, vdone) &&
		    time_after64(vtime, now.vnow -
				 MAX_LAGGING_PERIODS * period_vtime) &&
		    time_before64(vdone, now.vnow - period_vtime))
			nr_lagging++;

		if (waitqueue_active(&iocg->waitq))
			vusage = now.vnow - iocg->last_vtime;
		else if (time_before64(iocg->last_vtime, vtime))
			vusage = vtime - iocg->last_vtime;
		else
			vusage = 0;

		iocg->last_vtime += vusage;
		/*
		 * Factor in in-flight vtime into vusage to avoid
		 * high-latency completions appearing as idle.  This should
		 * be done after the above ->last_time adjustment.
		 */
		vusage = max(vusage, vtime - vdone);

		/* calculate hweight based usage ratio and record */
		if (vusage) {
			usage = DIV64_U64_ROUND_UP(vusage * hw_inuse,
						   period_vtime);
			iocg->usage_idx = (iocg->usage_idx + 1) % NR_USAGE_SLOTS;
			iocg->usages[iocg->usage_idx] = usage;
		} else {
			usage = 0;
		}

		/* see whether there's surplus vtime */
		vmargin = ioc->margin_us * now.vrate;
		vmin = now.vnow - vmargin;

		iocg->has_surplus = false;

		if (!waitqueue_active(&iocg->waitq) &&
		    time_before64(vtime, vmin)) {
			u64 delta = vmin - vtime;

			/* throw away surplus vtime */
			atomic64_add(delta, &iocg->vtime);
			atomic64_add(delta, &iocg->done_vtime);
			iocg->last_vtime += delta;
			/* if usage is sufficiently low, maybe it can donate */
			if (surplus_adjusted_hweight_inuse(usage, hw_inuse)) {
				iocg->has_surplus = true;
				nr_surpluses++;
			}
		} else if (hw_inuse < hw_active) {
			u32 new_hwi, new_inuse;

			/* was donating but might need to take back some */
			if (waitqueue_active(&iocg->waitq)) {
				new_hwi = hw_active;
			} else {
				new_hwi = max(hw_inuse,
					      usage * SURPLUS_SCALE_PCT / 100 +
					      SURPLUS_SCALE_ABS);
			}

			new_inuse = div64_u64((u64)iocg->inuse * new_hwi,
					      hw_inuse);
			new_inuse = clamp_t(u32, new_inuse, 1, iocg->active);

			if (new_inuse > iocg->inuse) {
				TRACE_IOCG_PATH(inuse_takeback, iocg, &now,
						iocg->inuse, new_inuse,
						hw_inuse, new_hwi);
				__propagate_active_weight(iocg, iocg->weight,
							  new_inuse);
			}
		} else {
			/* genuninely out of vtime */
			nr_shortages++;
		}
	}

	if (!nr_shortages || !nr_surpluses)
		goto skip_surplus_transfers;

	/* there are both shortages and surpluses, transfer surpluses */
	list_for_each_entry(iocg, &ioc->active_iocgs, active_list) {
		u32 usage, hw_active, hw_inuse, new_hwi, new_inuse;
		int nr_valid = 0;

		if (!iocg->has_surplus)
			continue;

		/* base the decision on max historical usage */
		for (i = 0, usage = 0; i < NR_USAGE_SLOTS; i++) {
			if (iocg->usages[i]) {
				usage = max(usage, iocg->usages[i]);
				nr_valid++;
			}
		}
		if (nr_valid < MIN_VALID_USAGES)
			continue;

		current_hweight(iocg, &hw_active, &hw_inuse);
		new_hwi = surplus_adjusted_hweight_inuse(usage, hw_inuse);
		if (!new_hwi)
			continue;

		new_inuse = DIV64_U64_ROUND_UP((u64)iocg->inuse * new_hwi,
					       hw_inuse);
		if (new_inuse < iocg->inuse) {
			TRACE_IOCG_PATH(inuse_giveaway, iocg, &now,
					iocg->inuse, new_inuse,
					hw_inuse, new_hwi);
			__propagate_active_weight(iocg, iocg->weight, new_inuse);
		}
	}
skip_surplus_transfers:
	commit_active_weights(ioc);

	/*
	 * If q is getting clogged or we're missing too much, we're issuing
	 * too much IO and should lower vtime rate.  If we're not missing
	 * and experiencing shortages but not surpluses, we're too stingy
	 * and should increase vtime rate.
	 */
	prev_busy_level = ioc->busy_level;
	if (rq_wait_pct > RQ_WAIT_BUSY_PCT ||
	    missed_ppm[READ] > ppm_rthr ||
	    missed_ppm[WRITE] > ppm_wthr) {
		ioc->busy_level = max(ioc->busy_level, 0);
		ioc->busy_level++;
	} else if (rq_wait_pct <= RQ_WAIT_BUSY_PCT * UNBUSY_THR_PCT / 100 &&
		   missed_ppm[READ] <= ppm_rthr * UNBUSY_THR_PCT / 100 &&
		   missed_ppm[WRITE] <= ppm_wthr * UNBUSY_THR_PCT / 100) {
		/* take action iff there is contention */
		if (nr_shortages && !nr_lagging) {
			ioc->busy_level = min(ioc->busy_level, 0);
			/* redistribute surpluses first */
			if (!nr_surpluses)
				ioc->busy_level--;
		}
	} else {
		ioc->busy_level = 0;
	}

	ioc->busy_level = clamp(ioc->busy_level, -1000, 1000);

	if (ioc->busy_level > 0 || (ioc->busy_level < 0 && !nr_lagging)) {
		u64 vrate = atomic64_read(&ioc->vtime_rate);
		u64 vrate_min = ioc->vrate_min, vrate_max = ioc->vrate_max;

		/* rq_wait signal is always reliable, ignore user vrate_min */
		if (rq_wait_pct > RQ_WAIT_BUSY_PCT)
			vrate_min = VRATE_MIN;

		/*
		 * If vrate is out of bounds, apply clamp gradually as the
		 * bounds can change abruptly.  Otherwise, apply busy_level
		 * based adjustment.
		 */
		if (vrate < vrate_min) {
			vrate = div64_u64(vrate * (100 + VRATE_CLAMP_ADJ_PCT),
					  100);
			vrate = min(vrate, vrate_min);
		} else if (vrate > vrate_max) {
			vrate = div64_u64(vrate * (100 - VRATE_CLAMP_ADJ_PCT),
					  100);
			vrate = max(vrate, vrate_max);
		} else {
			int idx = min_t(int, abs(ioc->busy_level),
					ARRAY_SIZE(vrate_adj_pct) - 1);
			u32 adj_pct = vrate_adj_pct[idx];

			if (ioc->busy_level > 0)
				adj_pct = 100 - adj_pct;
			else
				adj_pct = 100 + adj_pct;

			vrate = clamp(DIV64_U64_ROUND_UP(vrate * adj_pct, 100),
				      vrate_min, vrate_max);
		}

		trace_iocost_ioc_vrate_adj(ioc, vrate, &missed_ppm, rq_wait_pct,
					   nr_lagging, nr_shortages,
					   nr_surpluses);

		atomic64_set(&ioc->vtime_rate, vrate);
		ioc->inuse_margin_vtime = DIV64_U64_ROUND_UP(
			ioc->period_us * vrate * INUSE_MARGIN_PCT, 100);
	} else if (ioc->busy_level != prev_busy_level || nr_lagging) {
		trace_iocost_ioc_vrate_adj(ioc, atomic64_read(&ioc->vtime_rate),
					   &missed_ppm, rq_wait_pct, nr_lagging,
					   nr_shortages, nr_surpluses);
	}

	ioc_refresh_params(ioc, false);

	/*
	 * This period is done.  Move onto the next one.  If nothing's
	 * going on with the device, stop the timer.
	 */
	atomic64_inc(&ioc->cur_period);

	if (ioc->running != IOC_STOP) {
		if (!list_empty(&ioc->active_iocgs)) {
			ioc_start_period(ioc, &now);
		} else {
			ioc->busy_level = 0;
			ioc->running = IOC_IDLE;
		}
	}

	spin_unlock_irq(&ioc->lock);
}

static void calc_vtime_cost_builtin(struct bio *bio, struct ioc_gq *iocg,
				    bool is_merge, u64 *costp)
{
	struct ioc *ioc = iocg->ioc;
	u64 coef_seqio, coef_randio, coef_page;
	u64 pages = max_t(u64, bio_sectors(bio) >> IOC_SECT_TO_PAGE_SHIFT, 1);
	u64 seek_pages = 0;
	u64 cost = 0;

	switch (bio_op(bio)) {
	case REQ_OP_READ:
		coef_seqio	= ioc->params.lcoefs[LCOEF_RSEQIO];
		coef_randio	= ioc->params.lcoefs[LCOEF_RRANDIO];
		coef_page	= ioc->params.lcoefs[LCOEF_RPAGE];
		break;
	case REQ_OP_WRITE:
		coef_seqio	= ioc->params.lcoefs[LCOEF_WSEQIO];
		coef_randio	= ioc->params.lcoefs[LCOEF_WRANDIO];
		coef_page	= ioc->params.lcoefs[LCOEF_WPAGE];
		break;
	default:
		goto out;
	}

	if (iocg->cursor) {
		seek_pages = abs(bio->bi_iter.bi_sector - iocg->cursor);
		seek_pages >>= IOC_SECT_TO_PAGE_SHIFT;
	}

	if (!is_merge) {
		if (seek_pages > LCOEF_RANDIO_PAGES) {
			cost += coef_randio;
		} else {
			cost += coef_seqio;
		}
	}
	cost += pages * coef_page;
out:
	*costp = cost;
}

static u64 calc_vtime_cost(struct bio *bio, struct ioc_gq *iocg, bool is_merge)
{
	u64 cost;

	calc_vtime_cost_builtin(bio, iocg, is_merge, &cost);
	return cost;
}

static void ioc_rqos_throttle(struct rq_qos *rqos, struct bio *bio)
{
	struct blkcg_gq *blkg = bio->bi_blkg;
	struct ioc *ioc = rqos_to_ioc(rqos);
	struct ioc_gq *iocg = blkg_to_iocg(blkg);
	struct ioc_now now;
	struct iocg_wait wait;
	u32 hw_active, hw_inuse;
	u64 abs_cost, cost, vtime;

	/* bypass IOs if disabled or for root cgroup */
	if (!ioc->enabled || !iocg->level)
		return;

	/* always activate so that even 0 cost IOs get protected to some level */
	if (!iocg_activate(iocg, &now))
		return;

	/* calculate the absolute vtime cost */
	abs_cost = calc_vtime_cost(bio, iocg, false);
	if (!abs_cost)
		return;

	iocg->cursor = bio_end_sector(bio);

	vtime = atomic64_read(&iocg->vtime);
	current_hweight(iocg, &hw_active, &hw_inuse);

	if (hw_inuse < hw_active &&
	    time_after_eq64(vtime + ioc->inuse_margin_vtime, now.vnow)) {
		TRACE_IOCG_PATH(inuse_reset, iocg, &now,
				iocg->inuse, iocg->weight, hw_inuse, hw_active);
		spin_lock_irq(&ioc->lock);
		propagate_active_weight(iocg, iocg->weight, iocg->weight);
		spin_unlock_irq(&ioc->lock);
		current_hweight(iocg, &hw_active, &hw_inuse);
	}

	cost = abs_cost_to_cost(abs_cost, hw_inuse);

	/*
	 * If no one's waiting and within budget, issue right away.  The
	 * tests are racy but the races aren't systemic - we only miss once
	 * in a while which is fine.
	 */
	if (!waitqueue_active(&iocg->waitq) &&
	    !atomic64_read(&iocg->abs_vdebt) &&
	    time_before_eq64(vtime + cost, now.vnow)) {
		iocg_commit_bio(iocg, bio, cost);
		return;
	}

	/*
	 * We're over budget.  If @bio has to be issued regardless,
	 * remember the abs_cost instead of advancing vtime.
	 * iocg_kick_waitq() will pay off the debt before waking more IOs.
	 * This way, the debt is continuously paid off each period with the
	 * actual budget available to the cgroup.  If we just wound vtime,
	 * we would incorrectly use the current hw_inuse for the entire
	 * amount which, for example, can lead to the cgroup staying
	 * blocked for a long time even with substantially raised hw_inuse.
	 */
	if (bio_issue_as_root_blkg(bio) || fatal_signal_pending(current)) {
		atomic64_add(abs_cost, &iocg->abs_vdebt);
		iocg_kick_delay(iocg, &now, cost);
		return;
	}

	/*
	 * Append self to the waitq and schedule the wakeup timer if we're
	 * the first waiter.  The timer duration is calculated based on the
	 * current vrate.  vtime and hweight changes can make it too short
	 * or too long.  Each wait entry records the absolute cost it's
	 * waiting for to allow re-evaluation using a custom wait entry.
	 *
	 * If too short, the timer simply reschedules itself.  If too long,
	 * the period timer will notice and trigger wakeups.
	 *
	 * All waiters are on iocg->waitq and the wait states are
	 * synchronized using waitq.lock.
	 */
	spin_lock_irq(&iocg->waitq.lock);

	/*
	 * We activated above but w/o any synchronization.  Deactivation is
	 * synchronized with waitq.lock and we won't get deactivated as
	 * long as we're waiting, so we're good if we're activated here.
	 * In the unlikely case that we are deactivated, just issue the IO.
	 */
	if (unlikely(list_empty(&iocg->active_list))) {
		spin_unlock_irq(&iocg->waitq.lock);
		iocg_commit_bio(iocg, bio, cost);
		return;
	}

	init_waitqueue_func_entry(&wait.wait, iocg_wake_fn);
	wait.wait.private = current;
	wait.bio = bio;
	wait.abs_cost = abs_cost;
	wait.committed = false;	/* will be set true by waker */

	__add_wait_queue_entry_tail(&iocg->waitq, &wait.wait);
	iocg_kick_waitq(iocg, &now);

	spin_unlock_irq(&iocg->waitq.lock);

	while (true) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (wait.committed)
			break;
		io_schedule();
	}

	/* waker already committed us, proceed */
	finish_wait(&iocg->waitq, &wait.wait);
}

static void ioc_rqos_merge(struct rq_qos *rqos, struct request *rq,
			   struct bio *bio)
{
	struct ioc_gq *iocg = blkg_to_iocg(bio->bi_blkg);
	struct ioc *ioc = iocg->ioc;
	sector_t bio_end = bio_end_sector(bio);
	struct ioc_now now;
	u32 hw_inuse;
	u64 abs_cost, cost;

	/* bypass if disabled or for root cgroup */
	if (!ioc->enabled || !iocg->level)
		return;

	abs_cost = calc_vtime_cost(bio, iocg, true);
	if (!abs_cost)
		return;

	ioc_now(ioc, &now);
	current_hweight(iocg, NULL, &hw_inuse);
	cost = abs_cost_to_cost(abs_cost, hw_inuse);

	/* update cursor if backmerging into the request at the cursor */
	if (blk_rq_pos(rq) < bio_end &&
	    blk_rq_pos(rq) + blk_rq_sectors(rq) == iocg->cursor)
		iocg->cursor = bio_end;

	/*
	 * Charge if there's enough vtime budget and the existing request
	 * has cost assigned.  Otherwise, account it as debt.  See debt
	 * handling in ioc_rqos_throttle() for details.
	 */
	if (rq->bio && rq->bio->bi_iocost_cost &&
	    time_before_eq64(atomic64_read(&iocg->vtime) + cost, now.vnow))
		iocg_commit_bio(iocg, bio, cost);
	else
		atomic64_add(abs_cost, &iocg->abs_vdebt);
}

static void ioc_rqos_done_bio(struct rq_qos *rqos, struct bio *bio)
{
	struct ioc_gq *iocg = blkg_to_iocg(bio->bi_blkg);

	if (iocg && bio->bi_iocost_cost)
		atomic64_add(bio->bi_iocost_cost, &iocg->done_vtime);
}

static void ioc_rqos_done(struct rq_qos *rqos, struct request *rq)
{
	struct ioc *ioc = rqos_to_ioc(rqos);
	u64 on_q_ns, rq_wait_ns;
	int pidx, rw;

	if (!ioc->enabled || !rq->alloc_time_ns || !rq->start_time_ns)
		return;

	switch (req_op(rq) & REQ_OP_MASK) {
	case REQ_OP_READ:
		pidx = QOS_RLAT;
		rw = READ;
		break;
	case REQ_OP_WRITE:
		pidx = QOS_WLAT;
		rw = WRITE;
		break;
	default:
		return;
	}

	on_q_ns = ktime_get_ns() - rq->alloc_time_ns;
	rq_wait_ns = rq->start_time_ns - rq->alloc_time_ns;

	if (on_q_ns <= ioc->params.qos[pidx] * NSEC_PER_USEC)
		this_cpu_inc(ioc->pcpu_stat->missed[rw].nr_met);
	else
		this_cpu_inc(ioc->pcpu_stat->missed[rw].nr_missed);

	this_cpu_add(ioc->pcpu_stat->rq_wait_ns, rq_wait_ns);
}

static void ioc_rqos_queue_depth_changed(struct rq_qos *rqos)
{
	struct ioc *ioc = rqos_to_ioc(rqos);

	spin_lock_irq(&ioc->lock);
	ioc_refresh_params(ioc, false);
	spin_unlock_irq(&ioc->lock);
}

static void ioc_rqos_exit(struct rq_qos *rqos)
{
	struct ioc *ioc = rqos_to_ioc(rqos);

	blkcg_deactivate_policy(rqos->q, &blkcg_policy_iocost);

	spin_lock_irq(&ioc->lock);
	ioc->running = IOC_STOP;
	spin_unlock_irq(&ioc->lock);

	del_timer_sync(&ioc->timer);
	free_percpu(ioc->pcpu_stat);
	kfree(ioc);
}

static struct rq_qos_ops ioc_rqos_ops = {
	.throttle = ioc_rqos_throttle,
	.merge = ioc_rqos_merge,
	.done_bio = ioc_rqos_done_bio,
	.done = ioc_rqos_done,
	.queue_depth_changed = ioc_rqos_queue_depth_changed,
	.exit = ioc_rqos_exit,
};

static int blk_iocost_init(struct request_queue *q)
{
	struct ioc *ioc;
	struct rq_qos *rqos;
	int ret;

	ioc = kzalloc(sizeof(*ioc), GFP_KERNEL);
	if (!ioc)
		return -ENOMEM;

	ioc->pcpu_stat = alloc_percpu(struct ioc_pcpu_stat);
	if (!ioc->pcpu_stat) {
		kfree(ioc);
		return -ENOMEM;
	}

	rqos = &ioc->rqos;
	rqos->id = RQ_QOS_COST;
	rqos->ops = &ioc_rqos_ops;
	rqos->q = q;

	spin_lock_init(&ioc->lock);
	timer_setup(&ioc->timer, ioc_timer_fn, 0);
	INIT_LIST_HEAD(&ioc->active_iocgs);

	ioc->running = IOC_IDLE;
	atomic64_set(&ioc->vtime_rate, VTIME_PER_USEC);
	seqcount_init(&ioc->period_seqcount);
	ioc->period_at = ktime_to_us(ktime_get());
	atomic64_set(&ioc->cur_period, 0);
	atomic_set(&ioc->hweight_gen, 0);

	spin_lock_irq(&ioc->lock);
	ioc->autop_idx = AUTOP_INVALID;
	ioc_refresh_params(ioc, true);
	spin_unlock_irq(&ioc->lock);

	rq_qos_add(q, rqos);
	ret = blkcg_activate_policy(q, &blkcg_policy_iocost);
	if (ret) {
		rq_qos_del(q, rqos);
		free_percpu(ioc->pcpu_stat);
		kfree(ioc);
		return ret;
	}
	return 0;
}

static struct blkcg_policy_data *ioc_cpd_alloc(gfp_t gfp)
{
	struct ioc_cgrp *iocc;

	iocc = kzalloc(sizeof(struct ioc_cgrp), gfp);
	if (!iocc)
		return NULL;

	iocc->dfl_weight = CGROUP_WEIGHT_DFL;
	return &iocc->cpd;
}

static void ioc_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(container_of(cpd, struct ioc_cgrp, cpd));
}

static struct blkg_policy_data *ioc_pd_alloc(gfp_t gfp, struct request_queue *q,
					     struct blkcg *blkcg)
{
	int levels = blkcg->css.cgroup->level + 1;
	struct ioc_gq *iocg;

	iocg = kzalloc_node(sizeof(*iocg) + levels * sizeof(iocg->ancestors[0]),
			    gfp, q->node);
	if (!iocg)
		return NULL;

	return &iocg->pd;
}

static void ioc_pd_init(struct blkg_policy_data *pd)
{
	struct ioc_gq *iocg = pd_to_iocg(pd);
	struct blkcg_gq *blkg = pd_to_blkg(&iocg->pd);
	struct ioc *ioc = q_to_ioc(blkg->q);
	struct ioc_now now;
	struct blkcg_gq *tblkg;
	unsigned long flags;

	ioc_now(ioc, &now);

	iocg->ioc = ioc;
	atomic64_set(&iocg->vtime, now.vnow);
	atomic64_set(&iocg->done_vtime, now.vnow);
	atomic64_set(&iocg->abs_vdebt, 0);
	atomic64_set(&iocg->active_period, atomic64_read(&ioc->cur_period));
	INIT_LIST_HEAD(&iocg->active_list);
	iocg->hweight_active = HWEIGHT_WHOLE;
	iocg->hweight_inuse = HWEIGHT_WHOLE;

	init_waitqueue_head(&iocg->waitq);
	hrtimer_init(&iocg->waitq_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	iocg->waitq_timer.function = iocg_waitq_timer_fn;
	hrtimer_init(&iocg->delay_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	iocg->delay_timer.function = iocg_delay_timer_fn;

	iocg->level = blkg->blkcg->css.cgroup->level;

	for (tblkg = blkg; tblkg; tblkg = tblkg->parent) {
		struct ioc_gq *tiocg = blkg_to_iocg(tblkg);
		iocg->ancestors[tiocg->level] = tiocg;
	}

	spin_lock_irqsave(&ioc->lock, flags);
	weight_updated(iocg);
	spin_unlock_irqrestore(&ioc->lock, flags);
}

static void ioc_pd_free(struct blkg_policy_data *pd)
{
	struct ioc_gq *iocg = pd_to_iocg(pd);
	struct ioc *ioc = iocg->ioc;

	if (ioc) {
		spin_lock(&ioc->lock);
		if (!list_empty(&iocg->active_list)) {
			propagate_active_weight(iocg, 0, 0);
			list_del_init(&iocg->active_list);
		}
		spin_unlock(&ioc->lock);

		hrtimer_cancel(&iocg->waitq_timer);
		hrtimer_cancel(&iocg->delay_timer);
	}
	kfree(iocg);
}

static u64 ioc_weight_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			     int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioc_gq *iocg = pd_to_iocg(pd);

	if (dname && iocg->cfg_weight)
		seq_printf(sf, "%s %u\n", dname, iocg->cfg_weight);
	return 0;
}


static int ioc_weight_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct ioc_cgrp *iocc = blkcg_to_iocc(blkcg);

	seq_printf(sf, "default %u\n", iocc->dfl_weight);
	blkcg_print_blkgs(sf, blkcg, ioc_weight_prfill,
			  &blkcg_policy_iocost, seq_cft(sf)->private, false);
	return 0;
}

static ssize_t ioc_weight_write(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off)
{
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	struct ioc_cgrp *iocc = blkcg_to_iocc(blkcg);
	struct blkg_conf_ctx ctx;
	struct ioc_gq *iocg;
	u32 v;
	int ret;

	if (!strchr(buf, ':')) {
		struct blkcg_gq *blkg;

		if (!sscanf(buf, "default %u", &v) && !sscanf(buf, "%u", &v))
			return -EINVAL;

		if (v < CGROUP_WEIGHT_MIN || v > CGROUP_WEIGHT_MAX)
			return -EINVAL;

		spin_lock(&blkcg->lock);
		iocc->dfl_weight = v;
		hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
			struct ioc_gq *iocg = blkg_to_iocg(blkg);

			if (iocg) {
				spin_lock_irq(&iocg->ioc->lock);
				weight_updated(iocg);
				spin_unlock_irq(&iocg->ioc->lock);
			}
		}
		spin_unlock(&blkcg->lock);

		return nbytes;
	}

	ret = blkg_conf_prep(blkcg, &blkcg_policy_iocost, buf, &ctx);
	if (ret)
		return ret;

	iocg = blkg_to_iocg(ctx.blkg);

	if (!strncmp(ctx.body, "default", 7)) {
		v = 0;
	} else {
		if (!sscanf(ctx.body, "%u", &v))
			goto einval;
		if (v < CGROUP_WEIGHT_MIN || v > CGROUP_WEIGHT_MAX)
			goto einval;
	}

	spin_lock(&iocg->ioc->lock);
	iocg->cfg_weight = v;
	weight_updated(iocg);
	spin_unlock(&iocg->ioc->lock);

	blkg_conf_finish(&ctx);
	return nbytes;

einval:
	blkg_conf_finish(&ctx);
	return -EINVAL;
}

static u64 ioc_qos_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			  int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioc *ioc = pd_to_iocg(pd)->ioc;

	if (!dname)
		return 0;

	seq_printf(sf, "%s enable=%d ctrl=%s rpct=%u.%02u rlat=%u wpct=%u.%02u wlat=%u min=%u.%02u max=%u.%02u\n",
		   dname, ioc->enabled, ioc->user_qos_params ? "user" : "auto",
		   ioc->params.qos[QOS_RPPM] / 10000,
		   ioc->params.qos[QOS_RPPM] % 10000 / 100,
		   ioc->params.qos[QOS_RLAT],
		   ioc->params.qos[QOS_WPPM] / 10000,
		   ioc->params.qos[QOS_WPPM] % 10000 / 100,
		   ioc->params.qos[QOS_WLAT],
		   ioc->params.qos[QOS_MIN] / 10000,
		   ioc->params.qos[QOS_MIN] % 10000 / 100,
		   ioc->params.qos[QOS_MAX] / 10000,
		   ioc->params.qos[QOS_MAX] % 10000 / 100);
	return 0;
}

static int ioc_qos_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));

	blkcg_print_blkgs(sf, blkcg, ioc_qos_prfill,
			  &blkcg_policy_iocost, seq_cft(sf)->private, false);
	return 0;
}

static const match_table_t qos_ctrl_tokens = {
	{ QOS_ENABLE,		"enable=%u"	},
	{ QOS_CTRL,		"ctrl=%s"	},
	{ NR_QOS_CTRL_PARAMS,	NULL		},
};

static const match_table_t qos_tokens = {
	{ QOS_RPPM,		"rpct=%s"	},
	{ QOS_RLAT,		"rlat=%u"	},
	{ QOS_WPPM,		"wpct=%s"	},
	{ QOS_WLAT,		"wlat=%u"	},
	{ QOS_MIN,		"min=%s"	},
	{ QOS_MAX,		"max=%s"	},
	{ NR_QOS_PARAMS,	NULL		},
};

static ssize_t ioc_qos_write(struct kernfs_open_file *of, char *input,
			     size_t nbytes, loff_t off)
{
	struct gendisk *disk;
	struct ioc *ioc;
	u32 qos[NR_QOS_PARAMS];
	bool enable, user;
	char *p;
	int ret;

	disk = blkcg_conf_get_disk(&input);
	if (IS_ERR(disk))
		return PTR_ERR(disk);

	ioc = q_to_ioc(disk->queue);
	if (!ioc) {
		ret = blk_iocost_init(disk->queue);
		if (ret)
			goto err;
		ioc = q_to_ioc(disk->queue);
	}

	spin_lock_irq(&ioc->lock);
	memcpy(qos, ioc->params.qos, sizeof(qos));
	enable = ioc->enabled;
	user = ioc->user_qos_params;
	spin_unlock_irq(&ioc->lock);

	while ((p = strsep(&input, " \t\n"))) {
		substring_t args[MAX_OPT_ARGS];
		char buf[32];
		int tok;
		s64 v;

		if (!*p)
			continue;

		switch (match_token(p, qos_ctrl_tokens, args)) {
		case QOS_ENABLE:
			match_u64(&args[0], &v);
			enable = v;
			continue;
		case QOS_CTRL:
			match_strlcpy(buf, &args[0], sizeof(buf));
			if (!strcmp(buf, "auto"))
				user = false;
			else if (!strcmp(buf, "user"))
				user = true;
			else
				goto einval;
			continue;
		}

		tok = match_token(p, qos_tokens, args);
		switch (tok) {
		case QOS_RPPM:
		case QOS_WPPM:
			if (match_strlcpy(buf, &args[0], sizeof(buf)) >=
			    sizeof(buf))
				goto einval;
			if (cgroup_parse_float(buf, 2, &v))
				goto einval;
			if (v < 0 || v > 10000)
				goto einval;
			qos[tok] = v * 100;
			break;
		case QOS_RLAT:
		case QOS_WLAT:
			if (match_u64(&args[0], &v))
				goto einval;
			qos[tok] = v;
			break;
		case QOS_MIN:
		case QOS_MAX:
			if (match_strlcpy(buf, &args[0], sizeof(buf)) >=
			    sizeof(buf))
				goto einval;
			if (cgroup_parse_float(buf, 2, &v))
				goto einval;
			if (v < 0)
				goto einval;
			qos[tok] = clamp_t(s64, v * 100,
					   VRATE_MIN_PPM, VRATE_MAX_PPM);
			break;
		default:
			goto einval;
		}
		user = true;
	}

	if (qos[QOS_MIN] > qos[QOS_MAX])
		goto einval;

	spin_lock_irq(&ioc->lock);

	if (enable) {
		blk_queue_flag_set(QUEUE_FLAG_RQ_ALLOC_TIME, ioc->rqos.q);
		ioc->enabled = true;
	} else {
		blk_queue_flag_clear(QUEUE_FLAG_RQ_ALLOC_TIME, ioc->rqos.q);
		ioc->enabled = false;
	}

	if (user) {
		memcpy(ioc->params.qos, qos, sizeof(qos));
		ioc->user_qos_params = true;
	} else {
		ioc->user_qos_params = false;
	}

	ioc_refresh_params(ioc, true);
	spin_unlock_irq(&ioc->lock);

	put_disk_and_module(disk);
	return nbytes;
einval:
	ret = -EINVAL;
err:
	put_disk_and_module(disk);
	return ret;
}

static u64 ioc_cost_model_prfill(struct seq_file *sf,
				 struct blkg_policy_data *pd, int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioc *ioc = pd_to_iocg(pd)->ioc;
	u64 *u = ioc->params.i_lcoefs;

	if (!dname)
		return 0;

	seq_printf(sf, "%s ctrl=%s model=linear "
		   "rbps=%llu rseqiops=%llu rrandiops=%llu "
		   "wbps=%llu wseqiops=%llu wrandiops=%llu\n",
		   dname, ioc->user_cost_model ? "user" : "auto",
		   u[I_LCOEF_RBPS], u[I_LCOEF_RSEQIOPS], u[I_LCOEF_RRANDIOPS],
		   u[I_LCOEF_WBPS], u[I_LCOEF_WSEQIOPS], u[I_LCOEF_WRANDIOPS]);
	return 0;
}

static int ioc_cost_model_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));

	blkcg_print_blkgs(sf, blkcg, ioc_cost_model_prfill,
			  &blkcg_policy_iocost, seq_cft(sf)->private, false);
	return 0;
}

static const match_table_t cost_ctrl_tokens = {
	{ COST_CTRL,		"ctrl=%s"	},
	{ COST_MODEL,		"model=%s"	},
	{ NR_COST_CTRL_PARAMS,	NULL		},
};

static const match_table_t i_lcoef_tokens = {
	{ I_LCOEF_RBPS,		"rbps=%u"	},
	{ I_LCOEF_RSEQIOPS,	"rseqiops=%u"	},
	{ I_LCOEF_RRANDIOPS,	"rrandiops=%u"	},
	{ I_LCOEF_WBPS,		"wbps=%u"	},
	{ I_LCOEF_WSEQIOPS,	"wseqiops=%u"	},
	{ I_LCOEF_WRANDIOPS,	"wrandiops=%u"	},
	{ NR_I_LCOEFS,		NULL		},
};

static ssize_t ioc_cost_model_write(struct kernfs_open_file *of, char *input,
				    size_t nbytes, loff_t off)
{
	struct gendisk *disk;
	struct ioc *ioc;
	u64 u[NR_I_LCOEFS];
	bool user;
	char *p;
	int ret;

	disk = blkcg_conf_get_disk(&input);
	if (IS_ERR(disk))
		return PTR_ERR(disk);

	ioc = q_to_ioc(disk->queue);
	if (!ioc) {
		ret = blk_iocost_init(disk->queue);
		if (ret)
			goto err;
		ioc = q_to_ioc(disk->queue);
	}

	spin_lock_irq(&ioc->lock);
	memcpy(u, ioc->params.i_lcoefs, sizeof(u));
	user = ioc->user_cost_model;
	spin_unlock_irq(&ioc->lock);

	while ((p = strsep(&input, " \t\n"))) {
		substring_t args[MAX_OPT_ARGS];
		char buf[32];
		int tok;
		u64 v;

		if (!*p)
			continue;

		switch (match_token(p, cost_ctrl_tokens, args)) {
		case COST_CTRL:
			match_strlcpy(buf, &args[0], sizeof(buf));
			if (!strcmp(buf, "auto"))
				user = false;
			else if (!strcmp(buf, "user"))
				user = true;
			else
				goto einval;
			continue;
		case COST_MODEL:
			match_strlcpy(buf, &args[0], sizeof(buf));
			if (strcmp(buf, "linear"))
				goto einval;
			continue;
		}

		tok = match_token(p, i_lcoef_tokens, args);
		if (tok == NR_I_LCOEFS)
			goto einval;
		if (match_u64(&args[0], &v))
			goto einval;
		u[tok] = v;
		user = true;
	}

	spin_lock_irq(&ioc->lock);
	if (user) {
		memcpy(ioc->params.i_lcoefs, u, sizeof(u));
		ioc->user_cost_model = true;
	} else {
		ioc->user_cost_model = false;
	}
	ioc_refresh_params(ioc, true);
	spin_unlock_irq(&ioc->lock);

	put_disk_and_module(disk);
	return nbytes;

einval:
	ret = -EINVAL;
err:
	put_disk_and_module(disk);
	return ret;
}

static struct cftype ioc_files[] = {
	{
		.name = "weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = ioc_weight_show,
		.write = ioc_weight_write,
	},
	{
		.name = "cost.qos",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = ioc_qos_show,
		.write = ioc_qos_write,
	},
	{
		.name = "cost.model",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = ioc_cost_model_show,
		.write = ioc_cost_model_write,
	},
	{}
};

static struct blkcg_policy blkcg_policy_iocost = {
	.dfl_cftypes	= ioc_files,
	.cpd_alloc_fn	= ioc_cpd_alloc,
	.cpd_free_fn	= ioc_cpd_free,
	.pd_alloc_fn	= ioc_pd_alloc,
	.pd_init_fn	= ioc_pd_init,
	.pd_free_fn	= ioc_pd_free,
};

static int __init ioc_init(void)
{
	return blkcg_policy_register(&blkcg_policy_iocost);
}

static void __exit ioc_exit(void)
{
	return blkcg_policy_unregister(&blkcg_policy_iocost);
}

module_init(ioc_init);
module_exit(ioc_exit);
