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
 * parameters for several different classes of devices are provided and the
 * parameters can be configured from userspace via
 * /sys/fs/cgroup/io.cost.model.
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
 * upto 1 (WEIGHT_ONE).
 *
 * A given cgroup's vtime runs slower in inverse proportion to its hweight.
 * For example, with 12.5% weight, A0's time runs 8 times slower (100/12.5)
 * against the device vtime - an IO which takes 10ms on the underlying
 * device is considered to take 80ms on A0.
 *
 * This constitutes the basis of IO capacity distribution.  Each cgroup's
 * vtime is running at a rate determined by its hweight.  A cgroup tracks
 * the vtime consumed by past IOs and can issue a new IO if doing so
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
 * are executed, solely depending on rq wait may not result in satisfactory
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
 * https://github.com/osandov/drgn.  The output looks like the following.
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
#include <asm/local.h>
#include <asm/local64.h>
#include "blk-rq-qos.h"
#include "blk-stat.h"
#include "blk-wbt.h"
#include "blk-cgroup.h"

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
	 * iocg->vtime is targeted at 50% behind the device vtime, which
	 * serves as its IO credit buffer.  Surplus weight adjustment is
	 * immediately canceled if the vtime margin runs below 10%.
	 */
	MARGIN_MIN_PCT		= 10,
	MARGIN_LOW_PCT		= 20,
	MARGIN_TARGET_PCT	= 50,

	INUSE_ADJ_STEP_PCT	= 25,

	/* Have some play in timer operations */
	TIMER_SLACK_PCT		= 1,

	/* 1/64k is granular enough and can easily be handled w/ u32 */
	WEIGHT_ONE		= 1 << 16,
};

enum {
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
	VTIME_PER_NSEC		= VTIME_PER_SEC / NSEC_PER_SEC,

	/* bound vrate adjustments within two orders of magnitude */
	VRATE_MIN_PPM		= 10000,	/* 1% */
	VRATE_MAX_PPM		= 100000000,	/* 10000% */

	VRATE_MIN		= VTIME_PER_USEC * VRATE_MIN_PPM / MILLION,
	VRATE_CLAMP_ADJ_PCT	= 4,

	/* switch iff the conditions are met for longer than this */
	AUTOP_CYCLE_NSEC	= 10LLU * NSEC_PER_SEC,
};

enum {
	/* if IOs end up waiting for requests, issue less */
	RQ_WAIT_BUSY_PCT	= 5,

	/* unbusy hysterisis */
	UNBUSY_THR_PCT		= 75,

	/*
	 * The effect of delay is indirect and non-linear and a huge amount of
	 * future debt can accumulate abruptly while unthrottled. Linearly scale
	 * up delay as debt is going up and then let it decay exponentially.
	 * This gives us quick ramp ups while delay is accumulating and long
	 * tails which can help reducing the frequency of debt explosions on
	 * unthrottle. The parameters are experimentally determined.
	 *
	 * The delay mechanism provides adequate protection and behavior in many
	 * cases. However, this is far from ideal and falls shorts on both
	 * fronts. The debtors are often throttled too harshly costing a
	 * significant level of fairness and possibly total work while the
	 * protection against their impacts on the system can be choppy and
	 * unreliable.
	 *
	 * The shortcoming primarily stems from the fact that, unlike for page
	 * cache, the kernel doesn't have well-defined back-pressure propagation
	 * mechanism and policies for anonymous memory. Fully addressing this
	 * issue will likely require substantial improvements in the area.
	 */
	MIN_DELAY_THR_PCT	= 500,
	MAX_DELAY_THR_PCT	= 25000,
	MIN_DELAY		= 250,
	MAX_DELAY		= 250 * USEC_PER_MSEC,

	/* halve debts if avg usage over 100ms is under 50% */
	DFGV_USAGE_PCT		= 50,
	DFGV_PERIOD		= 100 * USEC_PER_MSEC,

	/* don't let cmds which take a very long time pin lagging for too long */
	MAX_LAGGING_PERIODS	= 10,

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

struct ioc_params {
	u32				qos[NR_QOS_PARAMS];
	u64				i_lcoefs[NR_I_LCOEFS];
	u64				lcoefs[NR_LCOEFS];
	u32				too_fast_vrate_pct;
	u32				too_slow_vrate_pct;
};

struct ioc_margins {
	s64				min;
	s64				low;
	s64				target;
};

struct ioc_missed {
	local_t				nr_met;
	local_t				nr_missed;
	u32				last_met;
	u32				last_missed;
};

struct ioc_pcpu_stat {
	struct ioc_missed		missed[2];

	local64_t			rq_wait_ns;
	u64				last_rq_wait_ns;
};

/* per device */
struct ioc {
	struct rq_qos			rqos;

	bool				enabled;

	struct ioc_params		params;
	struct ioc_margins		margins;
	u32				period_us;
	u32				timer_slack_ns;
	u64				vrate_min;
	u64				vrate_max;

	spinlock_t			lock;
	struct timer_list		timer;
	struct list_head		active_iocgs;	/* active cgroups */
	struct ioc_pcpu_stat __percpu	*pcpu_stat;

	enum ioc_running		running;
	atomic64_t			vtime_rate;
	u64				vtime_base_rate;
	s64				vtime_err;

	seqcount_spinlock_t		period_seqcount;
	u64				period_at;	/* wallclock starttime */
	u64				period_at_vtime; /* vtime starttime */

	atomic64_t			cur_period;	/* inc'd each period */
	int				busy_level;	/* saturation history */

	bool				weights_updated;
	atomic_t			hweight_gen;	/* for lazy hweights */

	/* debt forgivness */
	u64				dfgv_period_at;
	u64				dfgv_period_rem;
	u64				dfgv_usage_us_sum;

	u64				autop_too_fast_at;
	u64				autop_too_slow_at;
	int				autop_idx;
	bool				user_qos_params:1;
	bool				user_cost_model:1;
};

struct iocg_pcpu_stat {
	local64_t			abs_vusage;
};

struct iocg_stat {
	u64				usage_us;
	u64				wait_us;
	u64				indebt_us;
	u64				indelay_us;
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
	 *
	 * `inuse` may be adjusted dynamically during period. `saved_*` are used
	 * to determine and track adjustments.
	 */
	u32				cfg_weight;
	u32				weight;
	u32				active;
	u32				inuse;

	u32				last_inuse;
	s64				saved_margin;

	sector_t			cursor;		/* to detect randio */

	/*
	 * `vtime` is this iocg's vtime cursor which progresses as IOs are
	 * issued.  If lagging behind device vtime, the delta represents
	 * the currently available IO budget.  If running ahead, the
	 * overage.
	 *
	 * `vtime_done` is the same but progressed on completion rather
	 * than issue.  The delta behind `vtime` represents the cost of
	 * currently in-flight IOs.
	 */
	atomic64_t			vtime;
	atomic64_t			done_vtime;
	u64				abs_vdebt;

	/* current delay in effect and when it started */
	u64				delay;
	u64				delay_at;

	/*
	 * The period this iocg was last active in.  Used for deactivation
	 * and invalidating `vtime`.
	 */
	atomic64_t			active_period;
	struct list_head		active_list;

	/* see __propagate_weights() and current_hweight() for details */
	u64				child_active_sum;
	u64				child_inuse_sum;
	u64				child_adjusted_sum;
	int				hweight_gen;
	u32				hweight_active;
	u32				hweight_inuse;
	u32				hweight_donating;
	u32				hweight_after_donation;

	struct list_head		walk_list;
	struct list_head		surplus_list;

	struct wait_queue_head		waitq;
	struct hrtimer			waitq_timer;

	/* timestamp at the latest activation */
	u64				activated_at;

	/* statistics */
	struct iocg_pcpu_stat __percpu	*pcpu_stat;
	struct iocg_stat		stat;
	struct iocg_stat		last_stat;
	u64				last_stat_abs_vusage;
	u64				usage_delta_us;
	u64				wait_since;
	u64				indebt_since;
	u64				indelay_since;

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
	u64				now;
	u64				vnow;
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
static const u32 vrate_adj_pct[] =
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

static const char __maybe_unused *ioc_name(struct ioc *ioc)
{
	struct gendisk *disk = ioc->rqos.disk;

	if (!disk)
		return "<unknown>";
	return disk->disk_name;
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
	return DIV64_U64_ROUND_UP(abs_cost * WEIGHT_ONE, hw_inuse);
}

/*
 * The inverse of abs_cost_to_cost().  Must round up.
 */
static u64 cost_to_abs_cost(u64 cost, u32 hw_inuse)
{
	return DIV64_U64_ROUND_UP(cost * hw_inuse, WEIGHT_ONE);
}

static void iocg_commit_bio(struct ioc_gq *iocg, struct bio *bio,
			    u64 abs_cost, u64 cost)
{
	struct iocg_pcpu_stat *gcs;

	bio->bi_iocost_cost = cost;
	atomic64_add(cost, &iocg->vtime);

	gcs = get_cpu_ptr(iocg->pcpu_stat);
	local64_add(abs_cost, &gcs->abs_vusage);
	put_cpu_ptr(gcs);
}

static void iocg_lock(struct ioc_gq *iocg, bool lock_ioc, unsigned long *flags)
{
	if (lock_ioc) {
		spin_lock_irqsave(&iocg->ioc->lock, *flags);
		spin_lock(&iocg->waitq.lock);
	} else {
		spin_lock_irqsave(&iocg->waitq.lock, *flags);
	}
}

static void iocg_unlock(struct ioc_gq *iocg, bool unlock_ioc, unsigned long *flags)
{
	if (unlock_ioc) {
		spin_unlock(&iocg->waitq.lock);
		spin_unlock_irqrestore(&iocg->ioc->lock, *flags);
	} else {
		spin_unlock_irqrestore(&iocg->waitq.lock, *flags);
	}
}

#define CREATE_TRACE_POINTS
#include <trace/events/iocost.h>

static void ioc_refresh_margins(struct ioc *ioc)
{
	struct ioc_margins *margins = &ioc->margins;
	u32 period_us = ioc->period_us;
	u64 vrate = ioc->vtime_base_rate;

	margins->min = (period_us * MARGIN_MIN_PCT / 100) * vrate;
	margins->low = (period_us * MARGIN_LOW_PCT / 100) * vrate;
	margins->target = (period_us * MARGIN_TARGET_PCT / 100) * vrate;
}

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
	ioc->timer_slack_ns = div64_u64(
		(u64)period_us * NSEC_PER_USEC * TIMER_SLACK_PCT,
		100);
	ioc_refresh_margins(ioc);
}

/*
 *  ioc->rqos.disk isn't initialized when this function is called from
 *  the init path.
 */
static int ioc_autop_idx(struct ioc *ioc, struct gendisk *disk)
{
	int idx = ioc->autop_idx;
	const struct ioc_params *p = &autop[idx];
	u32 vrate_pct;
	u64 now_ns;

	/* rotational? */
	if (!blk_queue_nonrot(disk->queue))
		return AUTOP_HDD;

	/* handle SATA SSDs w/ broken NCQ */
	if (blk_queue_depth(disk->queue) == 1)
		return AUTOP_SSD_QD1;

	/* use one of the normal ssd sets */
	if (idx < AUTOP_SSD_DFL)
		return AUTOP_SSD_DFL;

	/* if user is overriding anything, maintain what was there */
	if (ioc->user_qos_params || ioc->user_cost_model)
		return idx;

	/* step up/down based on the vrate */
	vrate_pct = div64_u64(ioc->vtime_base_rate * 100, VTIME_PER_USEC);
	now_ns = blk_time_get_ns();

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

	if (bps) {
		u64 bps_pages = DIV_ROUND_UP_ULL(bps, IOC_PAGE_SIZE);

		if (bps_pages)
			*page = DIV64_U64_ROUND_UP(VTIME_PER_SEC, bps_pages);
		else
			*page = 1;
	}

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

/*
 * struct gendisk is required as an argument because ioc->rqos.disk
 * is not properly initialized when called from the init path.
 */
static bool ioc_refresh_params_disk(struct ioc *ioc, bool force,
				    struct gendisk *disk)
{
	const struct ioc_params *p;
	int idx;

	lockdep_assert_held(&ioc->lock);

	idx = ioc_autop_idx(ioc, disk);
	p = &autop[idx];

	if (idx == ioc->autop_idx && !force)
		return false;

	if (idx != ioc->autop_idx) {
		atomic64_set(&ioc->vtime_rate, VTIME_PER_USEC);
		ioc->vtime_base_rate = VTIME_PER_USEC;
	}

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
	ioc->vrate_max = DIV64_U64_ROUND_UP((u64)ioc->params.qos[QOS_MAX] *
					    VTIME_PER_USEC, MILLION);

	return true;
}

static bool ioc_refresh_params(struct ioc *ioc, bool force)
{
	return ioc_refresh_params_disk(ioc, force, ioc->rqos.disk);
}

/*
 * When an iocg accumulates too much vtime or gets deactivated, we throw away
 * some vtime, which lowers the overall device utilization. As the exact amount
 * which is being thrown away is known, we can compensate by accelerating the
 * vrate accordingly so that the extra vtime generated in the current period
 * matches what got lost.
 */
static void ioc_refresh_vrate(struct ioc *ioc, struct ioc_now *now)
{
	s64 pleft = ioc->period_at + ioc->period_us - now->now;
	s64 vperiod = ioc->period_us * ioc->vtime_base_rate;
	s64 vcomp, vcomp_min, vcomp_max;

	lockdep_assert_held(&ioc->lock);

	/* we need some time left in this period */
	if (pleft <= 0)
		goto done;

	/*
	 * Calculate how much vrate should be adjusted to offset the error.
	 * Limit the amount of adjustment and deduct the adjusted amount from
	 * the error.
	 */
	vcomp = -div64_s64(ioc->vtime_err, pleft);
	vcomp_min = -(ioc->vtime_base_rate >> 1);
	vcomp_max = ioc->vtime_base_rate;
	vcomp = clamp(vcomp, vcomp_min, vcomp_max);

	ioc->vtime_err += vcomp * pleft;

	atomic64_set(&ioc->vtime_rate, ioc->vtime_base_rate + vcomp);
done:
	/* bound how much error can accumulate */
	ioc->vtime_err = clamp(ioc->vtime_err, -vperiod, vperiod);
}

static void ioc_adjust_base_vrate(struct ioc *ioc, u32 rq_wait_pct,
				  int nr_lagging, int nr_shortages,
				  int prev_busy_level, u32 *missed_ppm)
{
	u64 vrate = ioc->vtime_base_rate;
	u64 vrate_min = ioc->vrate_min, vrate_max = ioc->vrate_max;

	if (!ioc->busy_level || (ioc->busy_level < 0 && nr_lagging)) {
		if (ioc->busy_level != prev_busy_level || nr_lagging)
			trace_iocost_ioc_vrate_adj(ioc, vrate,
						   missed_ppm, rq_wait_pct,
						   nr_lagging, nr_shortages);

		return;
	}

	/*
	 * If vrate is out of bounds, apply clamp gradually as the
	 * bounds can change abruptly.  Otherwise, apply busy_level
	 * based adjustment.
	 */
	if (vrate < vrate_min) {
		vrate = div64_u64(vrate * (100 + VRATE_CLAMP_ADJ_PCT), 100);
		vrate = min(vrate, vrate_min);
	} else if (vrate > vrate_max) {
		vrate = div64_u64(vrate * (100 - VRATE_CLAMP_ADJ_PCT), 100);
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

	trace_iocost_ioc_vrate_adj(ioc, vrate, missed_ppm, rq_wait_pct,
				   nr_lagging, nr_shortages);

	ioc->vtime_base_rate = vrate;
	ioc_refresh_margins(ioc);
}

/* take a snapshot of the current [v]time and vrate */
static void ioc_now(struct ioc *ioc, struct ioc_now *now)
{
	unsigned seq;
	u64 vrate;

	now->now_ns = blk_time_get_ns();
	now->now = ktime_to_us(now->now_ns);
	vrate = atomic64_read(&ioc->vtime_rate);

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
			(now->now - ioc->period_at) * vrate;
	} while (read_seqcount_retry(&ioc->period_seqcount, seq));
}

static void ioc_start_period(struct ioc *ioc, struct ioc_now *now)
{
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
 * weight sums and propagate upwards accordingly. If @save, the current margin
 * is saved to be used as reference for later inuse in-period adjustments.
 */
static void __propagate_weights(struct ioc_gq *iocg, u32 active, u32 inuse,
				bool save, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	int lvl;

	lockdep_assert_held(&ioc->lock);

	/*
	 * For an active leaf node, its inuse shouldn't be zero or exceed
	 * @active. An active internal node's inuse is solely determined by the
	 * inuse to active ratio of its children regardless of @inuse.
	 */
	if (list_empty(&iocg->active_list) && iocg->child_active_sum) {
		inuse = DIV64_U64_ROUND_UP(active * iocg->child_inuse_sum,
					   iocg->child_active_sum);
	} else {
		inuse = clamp_t(u32, inuse, 1, active);
	}

	iocg->last_inuse = iocg->inuse;
	if (save)
		iocg->saved_margin = now->vnow - atomic64_read(&iocg->vtime);

	if (active == iocg->active && inuse == iocg->inuse)
		return;

	for (lvl = iocg->level - 1; lvl >= 0; lvl--) {
		struct ioc_gq *parent = iocg->ancestors[lvl];
		struct ioc_gq *child = iocg->ancestors[lvl + 1];
		u32 parent_active = 0, parent_inuse = 0;

		/* update the level sums */
		parent->child_active_sum += (s32)(active - child->active);
		parent->child_inuse_sum += (s32)(inuse - child->inuse);
		/* apply the updates */
		child->active = active;
		child->inuse = inuse;

		/*
		 * The delta between inuse and active sums indicates that
		 * much of weight is being given away.  Parent's inuse
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

static void commit_weights(struct ioc *ioc)
{
	lockdep_assert_held(&ioc->lock);

	if (ioc->weights_updated) {
		/* paired with rmb in current_hweight(), see there */
		smp_wmb();
		atomic_inc(&ioc->hweight_gen);
		ioc->weights_updated = false;
	}
}

static void propagate_weights(struct ioc_gq *iocg, u32 active, u32 inuse,
			      bool save, struct ioc_now *now)
{
	__propagate_weights(iocg, active, inuse, save, now);
	commit_weights(iocg->ioc);
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
	 * Paired with wmb in commit_weights(). If we saw the updated
	 * hweight_gen, all the weight updates from __propagate_weights() are
	 * visible too.
	 *
	 * We can race with weight updates during calculation and get it
	 * wrong.  However, hweight_gen would have changed and a future
	 * reader will recalculate and we're guaranteed to discard the
	 * wrong result soon.
	 */
	smp_rmb();

	hwa = hwi = WEIGHT_ONE;
	for (lvl = 0; lvl <= iocg->level - 1; lvl++) {
		struct ioc_gq *parent = iocg->ancestors[lvl];
		struct ioc_gq *child = iocg->ancestors[lvl + 1];
		u64 active_sum = READ_ONCE(parent->child_active_sum);
		u64 inuse_sum = READ_ONCE(parent->child_inuse_sum);
		u32 active = READ_ONCE(child->active);
		u32 inuse = READ_ONCE(child->inuse);

		/* we can race with deactivations and either may read as zero */
		if (!active_sum || !inuse_sum)
			continue;

		active_sum = max_t(u64, active, active_sum);
		hwa = div64_u64((u64)hwa * active, active_sum);

		inuse_sum = max_t(u64, inuse, inuse_sum);
		hwi = div64_u64((u64)hwi * inuse, inuse_sum);
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

/*
 * Calculate the hweight_inuse @iocg would get with max @inuse assuming all the
 * other weights stay unchanged.
 */
static u32 current_hweight_max(struct ioc_gq *iocg)
{
	u32 hwm = WEIGHT_ONE;
	u32 inuse = iocg->active;
	u64 child_inuse_sum;
	int lvl;

	lockdep_assert_held(&iocg->ioc->lock);

	for (lvl = iocg->level - 1; lvl >= 0; lvl--) {
		struct ioc_gq *parent = iocg->ancestors[lvl];
		struct ioc_gq *child = iocg->ancestors[lvl + 1];

		child_inuse_sum = parent->child_inuse_sum + inuse - child->inuse;
		hwm = div64_u64((u64)hwm * inuse, child_inuse_sum);
		inuse = DIV64_U64_ROUND_UP(parent->active * child_inuse_sum,
					   parent->child_active_sum);
	}

	return max_t(u32, hwm, 1);
}

static void weight_updated(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	struct blkcg_gq *blkg = iocg_to_blkg(iocg);
	struct ioc_cgrp *iocc = blkcg_to_iocc(blkg->blkcg);
	u32 weight;

	lockdep_assert_held(&ioc->lock);

	weight = iocg->cfg_weight ?: iocc->dfl_weight;
	if (weight != iocg->weight && iocg->active)
		propagate_weights(iocg, weight, iocg->inuse, true, now);
	iocg->weight = weight;
}

static bool iocg_activate(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	u64 __maybe_unused last_period, cur_period;
	u64 vtime, vtarget;
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
	 * Always start with the target budget. On deactivation, we throw away
	 * anything above it.
	 */
	vtarget = now->vnow - ioc->margins.target;
	vtime = atomic64_read(&iocg->vtime);

	atomic64_add(vtarget - vtime, &iocg->vtime);
	atomic64_add(vtarget - vtime, &iocg->done_vtime);
	vtime = vtarget;

	/*
	 * Activate, propagate weight and start period timer if not
	 * running.  Reset hweight_gen to avoid accidental match from
	 * wrapping.
	 */
	iocg->hweight_gen = atomic_read(&ioc->hweight_gen) - 1;
	list_add(&iocg->active_list, &ioc->active_iocgs);

	propagate_weights(iocg, iocg->weight,
			  iocg->last_inuse ?: iocg->weight, true, now);

	TRACE_IOCG_PATH(iocg_activate, iocg, now,
			last_period, cur_period, vtime);

	iocg->activated_at = now->now;

	if (ioc->running == IOC_IDLE) {
		ioc->running = IOC_RUNNING;
		ioc->dfgv_period_at = now->now;
		ioc->dfgv_period_rem = 0;
		ioc_start_period(ioc, now);
	}

succeed_unlock:
	spin_unlock_irq(&ioc->lock);
	return true;

fail_unlock:
	spin_unlock_irq(&ioc->lock);
	return false;
}

static bool iocg_kick_delay(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	struct blkcg_gq *blkg = iocg_to_blkg(iocg);
	u64 tdelta, delay, new_delay, shift;
	s64 vover, vover_pct;
	u32 hwa;

	lockdep_assert_held(&iocg->waitq.lock);

	/*
	 * If the delay is set by another CPU, we may be in the past. No need to
	 * change anything if so. This avoids decay calculation underflow.
	 */
	if (time_before64(now->now, iocg->delay_at))
		return false;

	/* calculate the current delay in effect - 1/2 every second */
	tdelta = now->now - iocg->delay_at;
	shift = div64_u64(tdelta, USEC_PER_SEC);
	if (iocg->delay && shift < BITS_PER_LONG)
		delay = iocg->delay >> shift;
	else
		delay = 0;

	/* calculate the new delay from the debt amount */
	current_hweight(iocg, &hwa, NULL);
	vover = atomic64_read(&iocg->vtime) +
		abs_cost_to_cost(iocg->abs_vdebt, hwa) - now->vnow;
	vover_pct = div64_s64(100 * vover,
			      ioc->period_us * ioc->vtime_base_rate);

	if (vover_pct <= MIN_DELAY_THR_PCT)
		new_delay = 0;
	else if (vover_pct >= MAX_DELAY_THR_PCT)
		new_delay = MAX_DELAY;
	else
		new_delay = MIN_DELAY +
			div_u64((MAX_DELAY - MIN_DELAY) *
				(vover_pct - MIN_DELAY_THR_PCT),
				MAX_DELAY_THR_PCT - MIN_DELAY_THR_PCT);

	/* pick the higher one and apply */
	if (new_delay > delay) {
		iocg->delay = new_delay;
		iocg->delay_at = now->now;
		delay = new_delay;
	}

	if (delay >= MIN_DELAY) {
		if (!iocg->indelay_since)
			iocg->indelay_since = now->now;
		blkcg_set_delay(blkg, delay * NSEC_PER_USEC);
		return true;
	} else {
		if (iocg->indelay_since) {
			iocg->stat.indelay_us += now->now - iocg->indelay_since;
			iocg->indelay_since = 0;
		}
		iocg->delay = 0;
		blkcg_clear_delay(blkg);
		return false;
	}
}

static void iocg_incur_debt(struct ioc_gq *iocg, u64 abs_cost,
			    struct ioc_now *now)
{
	struct iocg_pcpu_stat *gcs;

	lockdep_assert_held(&iocg->ioc->lock);
	lockdep_assert_held(&iocg->waitq.lock);
	WARN_ON_ONCE(list_empty(&iocg->active_list));

	/*
	 * Once in debt, debt handling owns inuse. @iocg stays at the minimum
	 * inuse donating all of it share to others until its debt is paid off.
	 */
	if (!iocg->abs_vdebt && abs_cost) {
		iocg->indebt_since = now->now;
		propagate_weights(iocg, iocg->active, 0, false, now);
	}

	iocg->abs_vdebt += abs_cost;

	gcs = get_cpu_ptr(iocg->pcpu_stat);
	local64_add(abs_cost, &gcs->abs_vusage);
	put_cpu_ptr(gcs);
}

static void iocg_pay_debt(struct ioc_gq *iocg, u64 abs_vpay,
			  struct ioc_now *now)
{
	lockdep_assert_held(&iocg->ioc->lock);
	lockdep_assert_held(&iocg->waitq.lock);

	/*
	 * make sure that nobody messed with @iocg. Check iocg->pd.online
	 * to avoid warn when removing blkcg or disk.
	 */
	WARN_ON_ONCE(list_empty(&iocg->active_list) && iocg->pd.online);
	WARN_ON_ONCE(iocg->inuse > 1);

	iocg->abs_vdebt -= min(abs_vpay, iocg->abs_vdebt);

	/* if debt is paid in full, restore inuse */
	if (!iocg->abs_vdebt) {
		iocg->stat.indebt_us += now->now - iocg->indebt_since;
		iocg->indebt_since = 0;

		propagate_weights(iocg, iocg->active, iocg->last_inuse,
				  false, now);
	}
}

static int iocg_wake_fn(struct wait_queue_entry *wq_entry, unsigned mode,
			int flags, void *key)
{
	struct iocg_wait *wait = container_of(wq_entry, struct iocg_wait, wait);
	struct iocg_wake_ctx *ctx = key;
	u64 cost = abs_cost_to_cost(wait->abs_cost, ctx->hw_inuse);

	ctx->vbudget -= cost;

	if (ctx->vbudget < 0)
		return -1;

	iocg_commit_bio(ctx->iocg, wait->bio, wait->abs_cost, cost);
	wait->committed = true;

	/*
	 * autoremove_wake_function() removes the wait entry only when it
	 * actually changed the task state. We want the wait always removed.
	 * Remove explicitly and use default_wake_function(). Note that the
	 * order of operations is important as finish_wait() tests whether
	 * @wq_entry is removed without grabbing the lock.
	 */
	default_wake_function(wq_entry, mode, flags, key);
	list_del_init_careful(&wq_entry->entry);
	return 0;
}

/*
 * Calculate the accumulated budget, pay debt if @pay_debt and wake up waiters
 * accordingly. When @pay_debt is %true, the caller must be holding ioc->lock in
 * addition to iocg->waitq.lock.
 */
static void iocg_kick_waitq(struct ioc_gq *iocg, bool pay_debt,
			    struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	struct iocg_wake_ctx ctx = { .iocg = iocg };
	u64 vshortage, expires, oexpires;
	s64 vbudget;
	u32 hwa;

	lockdep_assert_held(&iocg->waitq.lock);

	current_hweight(iocg, &hwa, NULL);
	vbudget = now->vnow - atomic64_read(&iocg->vtime);

	/* pay off debt */
	if (pay_debt && iocg->abs_vdebt && vbudget > 0) {
		u64 abs_vbudget = cost_to_abs_cost(vbudget, hwa);
		u64 abs_vpay = min_t(u64, abs_vbudget, iocg->abs_vdebt);
		u64 vpay = abs_cost_to_cost(abs_vpay, hwa);

		lockdep_assert_held(&ioc->lock);

		atomic64_add(vpay, &iocg->vtime);
		atomic64_add(vpay, &iocg->done_vtime);
		iocg_pay_debt(iocg, abs_vpay, now);
		vbudget -= vpay;
	}

	if (iocg->abs_vdebt || iocg->delay)
		iocg_kick_delay(iocg, now);

	/*
	 * Debt can still be outstanding if we haven't paid all yet or the
	 * caller raced and called without @pay_debt. Shouldn't wake up waiters
	 * under debt. Make sure @vbudget reflects the outstanding amount and is
	 * not positive.
	 */
	if (iocg->abs_vdebt) {
		s64 vdebt = abs_cost_to_cost(iocg->abs_vdebt, hwa);
		vbudget = min_t(s64, 0, vbudget - vdebt);
	}

	/*
	 * Wake up the ones which are due and see how much vtime we'll need for
	 * the next one. As paying off debt restores hw_inuse, it must be read
	 * after the above debt payment.
	 */
	ctx.vbudget = vbudget;
	current_hweight(iocg, NULL, &ctx.hw_inuse);

	__wake_up_locked_key(&iocg->waitq, TASK_NORMAL, &ctx);

	if (!waitqueue_active(&iocg->waitq)) {
		if (iocg->wait_since) {
			iocg->stat.wait_us += now->now - iocg->wait_since;
			iocg->wait_since = 0;
		}
		return;
	}

	if (!iocg->wait_since)
		iocg->wait_since = now->now;

	if (WARN_ON_ONCE(ctx.vbudget >= 0))
		return;

	/* determine next wakeup, add a timer margin to guarantee chunking */
	vshortage = -ctx.vbudget;
	expires = now->now_ns +
		DIV64_U64_ROUND_UP(vshortage, ioc->vtime_base_rate) *
		NSEC_PER_USEC;
	expires += ioc->timer_slack_ns;

	/* if already active and close enough, don't bother */
	oexpires = ktime_to_ns(hrtimer_get_softexpires(&iocg->waitq_timer));
	if (hrtimer_is_queued(&iocg->waitq_timer) &&
	    abs(oexpires - expires) <= ioc->timer_slack_ns)
		return;

	hrtimer_start_range_ns(&iocg->waitq_timer, ns_to_ktime(expires),
			       ioc->timer_slack_ns, HRTIMER_MODE_ABS);
}

static enum hrtimer_restart iocg_waitq_timer_fn(struct hrtimer *timer)
{
	struct ioc_gq *iocg = container_of(timer, struct ioc_gq, waitq_timer);
	bool pay_debt = READ_ONCE(iocg->abs_vdebt);
	struct ioc_now now;
	unsigned long flags;

	ioc_now(iocg->ioc, &now);

	iocg_lock(iocg, pay_debt, &flags);
	iocg_kick_waitq(iocg, pay_debt, &now);
	iocg_unlock(iocg, pay_debt, &flags);

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
			u32 this_met = local_read(&stat->missed[rw].nr_met);
			u32 this_missed = local_read(&stat->missed[rw].nr_missed);

			nr_met[rw] += this_met - stat->missed[rw].last_met;
			nr_missed[rw] += this_missed - stat->missed[rw].last_missed;
			stat->missed[rw].last_met = this_met;
			stat->missed[rw].last_missed = this_missed;
		}

		this_rq_wait_ns = local64_read(&stat->rq_wait_ns);
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
	if (atomic64_read(&iocg->done_vtime) != atomic64_read(&iocg->vtime))
		return false;

	return true;
}

/*
 * Call this function on the target leaf @iocg's to build pre-order traversal
 * list of all the ancestors in @inner_walk. The inner nodes are linked through
 * ->walk_list and the caller is responsible for dissolving the list after use.
 */
static void iocg_build_inner_walk(struct ioc_gq *iocg,
				  struct list_head *inner_walk)
{
	int lvl;

	WARN_ON_ONCE(!list_empty(&iocg->walk_list));

	/* find the first ancestor which hasn't been visited yet */
	for (lvl = iocg->level - 1; lvl >= 0; lvl--) {
		if (!list_empty(&iocg->ancestors[lvl]->walk_list))
			break;
	}

	/* walk down and visit the inner nodes to get pre-order traversal */
	while (++lvl <= iocg->level - 1) {
		struct ioc_gq *inner = iocg->ancestors[lvl];

		/* record traversal order */
		list_add_tail(&inner->walk_list, inner_walk);
	}
}

/* propagate the deltas to the parent */
static void iocg_flush_stat_upward(struct ioc_gq *iocg)
{
	if (iocg->level > 0) {
		struct iocg_stat *parent_stat =
			&iocg->ancestors[iocg->level - 1]->stat;

		parent_stat->usage_us +=
			iocg->stat.usage_us - iocg->last_stat.usage_us;
		parent_stat->wait_us +=
			iocg->stat.wait_us - iocg->last_stat.wait_us;
		parent_stat->indebt_us +=
			iocg->stat.indebt_us - iocg->last_stat.indebt_us;
		parent_stat->indelay_us +=
			iocg->stat.indelay_us - iocg->last_stat.indelay_us;
	}

	iocg->last_stat = iocg->stat;
}

/* collect per-cpu counters and propagate the deltas to the parent */
static void iocg_flush_stat_leaf(struct ioc_gq *iocg, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	u64 abs_vusage = 0;
	u64 vusage_delta;
	int cpu;

	lockdep_assert_held(&iocg->ioc->lock);

	/* collect per-cpu counters */
	for_each_possible_cpu(cpu) {
		abs_vusage += local64_read(
				per_cpu_ptr(&iocg->pcpu_stat->abs_vusage, cpu));
	}
	vusage_delta = abs_vusage - iocg->last_stat_abs_vusage;
	iocg->last_stat_abs_vusage = abs_vusage;

	iocg->usage_delta_us = div64_u64(vusage_delta, ioc->vtime_base_rate);
	iocg->stat.usage_us += iocg->usage_delta_us;

	iocg_flush_stat_upward(iocg);
}

/* get stat counters ready for reading on all active iocgs */
static void iocg_flush_stat(struct list_head *target_iocgs, struct ioc_now *now)
{
	LIST_HEAD(inner_walk);
	struct ioc_gq *iocg, *tiocg;

	/* flush leaves and build inner node walk list */
	list_for_each_entry(iocg, target_iocgs, active_list) {
		iocg_flush_stat_leaf(iocg, now);
		iocg_build_inner_walk(iocg, &inner_walk);
	}

	/* keep flushing upwards by walking the inner list backwards */
	list_for_each_entry_safe_reverse(iocg, tiocg, &inner_walk, walk_list) {
		iocg_flush_stat_upward(iocg);
		list_del_init(&iocg->walk_list);
	}
}

/*
 * Determine what @iocg's hweight_inuse should be after donating unused
 * capacity. @hwm is the upper bound and used to signal no donation. This
 * function also throws away @iocg's excess budget.
 */
static u32 hweight_after_donation(struct ioc_gq *iocg, u32 old_hwi, u32 hwm,
				  u32 usage, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	u64 vtime = atomic64_read(&iocg->vtime);
	s64 excess, delta, target, new_hwi;

	/* debt handling owns inuse for debtors */
	if (iocg->abs_vdebt)
		return 1;

	/* see whether minimum margin requirement is met */
	if (waitqueue_active(&iocg->waitq) ||
	    time_after64(vtime, now->vnow - ioc->margins.min))
		return hwm;

	/* throw away excess above target */
	excess = now->vnow - vtime - ioc->margins.target;
	if (excess > 0) {
		atomic64_add(excess, &iocg->vtime);
		atomic64_add(excess, &iocg->done_vtime);
		vtime += excess;
		ioc->vtime_err -= div64_u64(excess * old_hwi, WEIGHT_ONE);
	}

	/*
	 * Let's say the distance between iocg's and device's vtimes as a
	 * fraction of period duration is delta. Assuming that the iocg will
	 * consume the usage determined above, we want to determine new_hwi so
	 * that delta equals MARGIN_TARGET at the end of the next period.
	 *
	 * We need to execute usage worth of IOs while spending the sum of the
	 * new budget (1 - MARGIN_TARGET) and the leftover from the last period
	 * (delta):
	 *
	 *   usage = (1 - MARGIN_TARGET + delta) * new_hwi
	 *
	 * Therefore, the new_hwi is:
	 *
	 *   new_hwi = usage / (1 - MARGIN_TARGET + delta)
	 */
	delta = div64_s64(WEIGHT_ONE * (now->vnow - vtime),
			  now->vnow - ioc->period_at_vtime);
	target = WEIGHT_ONE * MARGIN_TARGET_PCT / 100;
	new_hwi = div64_s64(WEIGHT_ONE * usage, WEIGHT_ONE - target + delta);

	return clamp_t(s64, new_hwi, 1, hwm);
}

/*
 * For work-conservation, an iocg which isn't using all of its share should
 * donate the leftover to other iocgs. There are two ways to achieve this - 1.
 * bumping up vrate accordingly 2. lowering the donating iocg's inuse weight.
 *
 * #1 is mathematically simpler but has the drawback of requiring synchronous
 * global hweight_inuse updates when idle iocg's get activated or inuse weights
 * change due to donation snapbacks as it has the possibility of grossly
 * overshooting what's allowed by the model and vrate.
 *
 * #2 is inherently safe with local operations. The donating iocg can easily
 * snap back to higher weights when needed without worrying about impacts on
 * other nodes as the impacts will be inherently correct. This also makes idle
 * iocg activations safe. The only effect activations have is decreasing
 * hweight_inuse of others, the right solution to which is for those iocgs to
 * snap back to higher weights.
 *
 * So, we go with #2. The challenge is calculating how each donating iocg's
 * inuse should be adjusted to achieve the target donation amounts. This is done
 * using Andy's method described in the following pdf.
 *
 *   https://drive.google.com/file/d/1PsJwxPFtjUnwOY1QJ5AeICCcsL7BM3bo
 *
 * Given the weights and target after-donation hweight_inuse values, Andy's
 * method determines how the proportional distribution should look like at each
 * sibling level to maintain the relative relationship between all non-donating
 * pairs. To roughly summarize, it divides the tree into donating and
 * non-donating parts, calculates global donation rate which is used to
 * determine the target hweight_inuse for each node, and then derives per-level
 * proportions.
 *
 * The following pdf shows that global distribution calculated this way can be
 * achieved by scaling inuse weights of donating leaves and propagating the
 * adjustments upwards proportionally.
 *
 *   https://drive.google.com/file/d/1vONz1-fzVO7oY5DXXsLjSxEtYYQbOvsE
 *
 * Combining the above two, we can determine how each leaf iocg's inuse should
 * be adjusted to achieve the target donation.
 *
 *   https://drive.google.com/file/d/1WcrltBOSPN0qXVdBgnKm4mdp9FhuEFQN
 *
 * The inline comments use symbols from the last pdf.
 *
 *   b is the sum of the absolute budgets in the subtree. 1 for the root node.
 *   f is the sum of the absolute budgets of non-donating nodes in the subtree.
 *   t is the sum of the absolute budgets of donating nodes in the subtree.
 *   w is the weight of the node. w = w_f + w_t
 *   w_f is the non-donating portion of w. w_f = w * f / b
 *   w_b is the donating portion of w. w_t = w * t / b
 *   s is the sum of all sibling weights. s = Sum(w) for siblings
 *   s_f and s_t are the non-donating and donating portions of s.
 *
 * Subscript p denotes the parent's counterpart and ' the adjusted value - e.g.
 * w_pt is the donating portion of the parent's weight and w'_pt the same value
 * after adjustments. Subscript r denotes the root node's values.
 */
static void transfer_surpluses(struct list_head *surpluses, struct ioc_now *now)
{
	LIST_HEAD(over_hwa);
	LIST_HEAD(inner_walk);
	struct ioc_gq *iocg, *tiocg, *root_iocg;
	u32 after_sum, over_sum, over_target, gamma;

	/*
	 * It's pretty unlikely but possible for the total sum of
	 * hweight_after_donation's to be higher than WEIGHT_ONE, which will
	 * confuse the following calculations. If such condition is detected,
	 * scale down everyone over its full share equally to keep the sum below
	 * WEIGHT_ONE.
	 */
	after_sum = 0;
	over_sum = 0;
	list_for_each_entry(iocg, surpluses, surplus_list) {
		u32 hwa;

		current_hweight(iocg, &hwa, NULL);
		after_sum += iocg->hweight_after_donation;

		if (iocg->hweight_after_donation > hwa) {
			over_sum += iocg->hweight_after_donation;
			list_add(&iocg->walk_list, &over_hwa);
		}
	}

	if (after_sum >= WEIGHT_ONE) {
		/*
		 * The delta should be deducted from the over_sum, calculate
		 * target over_sum value.
		 */
		u32 over_delta = after_sum - (WEIGHT_ONE - 1);
		WARN_ON_ONCE(over_sum <= over_delta);
		over_target = over_sum - over_delta;
	} else {
		over_target = 0;
	}

	list_for_each_entry_safe(iocg, tiocg, &over_hwa, walk_list) {
		if (over_target)
			iocg->hweight_after_donation =
				div_u64((u64)iocg->hweight_after_donation *
					over_target, over_sum);
		list_del_init(&iocg->walk_list);
	}

	/*
	 * Build pre-order inner node walk list and prepare for donation
	 * adjustment calculations.
	 */
	list_for_each_entry(iocg, surpluses, surplus_list) {
		iocg_build_inner_walk(iocg, &inner_walk);
	}

	root_iocg = list_first_entry(&inner_walk, struct ioc_gq, walk_list);
	WARN_ON_ONCE(root_iocg->level > 0);

	list_for_each_entry(iocg, &inner_walk, walk_list) {
		iocg->child_adjusted_sum = 0;
		iocg->hweight_donating = 0;
		iocg->hweight_after_donation = 0;
	}

	/*
	 * Propagate the donating budget (b_t) and after donation budget (b'_t)
	 * up the hierarchy.
	 */
	list_for_each_entry(iocg, surpluses, surplus_list) {
		struct ioc_gq *parent = iocg->ancestors[iocg->level - 1];

		parent->hweight_donating += iocg->hweight_donating;
		parent->hweight_after_donation += iocg->hweight_after_donation;
	}

	list_for_each_entry_reverse(iocg, &inner_walk, walk_list) {
		if (iocg->level > 0) {
			struct ioc_gq *parent = iocg->ancestors[iocg->level - 1];

			parent->hweight_donating += iocg->hweight_donating;
			parent->hweight_after_donation += iocg->hweight_after_donation;
		}
	}

	/*
	 * Calculate inner hwa's (b) and make sure the donation values are
	 * within the accepted ranges as we're doing low res calculations with
	 * roundups.
	 */
	list_for_each_entry(iocg, &inner_walk, walk_list) {
		if (iocg->level) {
			struct ioc_gq *parent = iocg->ancestors[iocg->level - 1];

			iocg->hweight_active = DIV64_U64_ROUND_UP(
				(u64)parent->hweight_active * iocg->active,
				parent->child_active_sum);

		}

		iocg->hweight_donating = min(iocg->hweight_donating,
					     iocg->hweight_active);
		iocg->hweight_after_donation = min(iocg->hweight_after_donation,
						   iocg->hweight_donating - 1);
		if (WARN_ON_ONCE(iocg->hweight_active <= 1 ||
				 iocg->hweight_donating <= 1 ||
				 iocg->hweight_after_donation == 0)) {
			pr_warn("iocg: invalid donation weights in ");
			pr_cont_cgroup_path(iocg_to_blkg(iocg)->blkcg->css.cgroup);
			pr_cont(": active=%u donating=%u after=%u\n",
				iocg->hweight_active, iocg->hweight_donating,
				iocg->hweight_after_donation);
		}
	}

	/*
	 * Calculate the global donation rate (gamma) - the rate to adjust
	 * non-donating budgets by.
	 *
	 * No need to use 64bit multiplication here as the first operand is
	 * guaranteed to be smaller than WEIGHT_ONE (1<<16).
	 *
	 * We know that there are beneficiary nodes and the sum of the donating
	 * hweights can't be whole; however, due to the round-ups during hweight
	 * calculations, root_iocg->hweight_donating might still end up equal to
	 * or greater than whole. Limit the range when calculating the divider.
	 *
	 * gamma = (1 - t_r') / (1 - t_r)
	 */
	gamma = DIV_ROUND_UP(
		(WEIGHT_ONE - root_iocg->hweight_after_donation) * WEIGHT_ONE,
		WEIGHT_ONE - min_t(u32, root_iocg->hweight_donating, WEIGHT_ONE - 1));

	/*
	 * Calculate adjusted hwi, child_adjusted_sum and inuse for the inner
	 * nodes.
	 */
	list_for_each_entry(iocg, &inner_walk, walk_list) {
		struct ioc_gq *parent;
		u32 inuse, wpt, wptp;
		u64 st, sf;

		if (iocg->level == 0) {
			/* adjusted weight sum for 1st level: s' = s * b_pf / b'_pf */
			iocg->child_adjusted_sum = DIV64_U64_ROUND_UP(
				iocg->child_active_sum * (WEIGHT_ONE - iocg->hweight_donating),
				WEIGHT_ONE - iocg->hweight_after_donation);
			continue;
		}

		parent = iocg->ancestors[iocg->level - 1];

		/* b' = gamma * b_f + b_t' */
		iocg->hweight_inuse = DIV64_U64_ROUND_UP(
			(u64)gamma * (iocg->hweight_active - iocg->hweight_donating),
			WEIGHT_ONE) + iocg->hweight_after_donation;

		/* w' = s' * b' / b'_p */
		inuse = DIV64_U64_ROUND_UP(
			(u64)parent->child_adjusted_sum * iocg->hweight_inuse,
			parent->hweight_inuse);

		/* adjusted weight sum for children: s' = s_f + s_t * w'_pt / w_pt */
		st = DIV64_U64_ROUND_UP(
			iocg->child_active_sum * iocg->hweight_donating,
			iocg->hweight_active);
		sf = iocg->child_active_sum - st;
		wpt = DIV64_U64_ROUND_UP(
			(u64)iocg->active * iocg->hweight_donating,
			iocg->hweight_active);
		wptp = DIV64_U64_ROUND_UP(
			(u64)inuse * iocg->hweight_after_donation,
			iocg->hweight_inuse);

		iocg->child_adjusted_sum = sf + DIV64_U64_ROUND_UP(st * wptp, wpt);
	}

	/*
	 * All inner nodes now have ->hweight_inuse and ->child_adjusted_sum and
	 * we can finally determine leaf adjustments.
	 */
	list_for_each_entry(iocg, surpluses, surplus_list) {
		struct ioc_gq *parent = iocg->ancestors[iocg->level - 1];
		u32 inuse;

		/*
		 * In-debt iocgs participated in the donation calculation with
		 * the minimum target hweight_inuse. Configuring inuse
		 * accordingly would work fine but debt handling expects
		 * @iocg->inuse stay at the minimum and we don't wanna
		 * interfere.
		 */
		if (iocg->abs_vdebt) {
			WARN_ON_ONCE(iocg->inuse > 1);
			continue;
		}

		/* w' = s' * b' / b'_p, note that b' == b'_t for donating leaves */
		inuse = DIV64_U64_ROUND_UP(
			parent->child_adjusted_sum * iocg->hweight_after_donation,
			parent->hweight_inuse);

		TRACE_IOCG_PATH(inuse_transfer, iocg, now,
				iocg->inuse, inuse,
				iocg->hweight_inuse,
				iocg->hweight_after_donation);

		__propagate_weights(iocg, iocg->active, inuse, true, now);
	}

	/* walk list should be dissolved after use */
	list_for_each_entry_safe(iocg, tiocg, &inner_walk, walk_list)
		list_del_init(&iocg->walk_list);
}

/*
 * A low weight iocg can amass a large amount of debt, for example, when
 * anonymous memory gets reclaimed aggressively. If the system has a lot of
 * memory paired with a slow IO device, the debt can span multiple seconds or
 * more. If there are no other subsequent IO issuers, the in-debt iocg may end
 * up blocked paying its debt while the IO device is idle.
 *
 * The following protects against such cases. If the device has been
 * sufficiently idle for a while, the debts are halved and delays are
 * recalculated.
 */
static void ioc_forgive_debts(struct ioc *ioc, u64 usage_us_sum, int nr_debtors,
			      struct ioc_now *now)
{
	struct ioc_gq *iocg;
	u64 dur, usage_pct, nr_cycles, nr_cycles_shift;

	/* if no debtor, reset the cycle */
	if (!nr_debtors) {
		ioc->dfgv_period_at = now->now;
		ioc->dfgv_period_rem = 0;
		ioc->dfgv_usage_us_sum = 0;
		return;
	}

	/*
	 * Debtors can pass through a lot of writes choking the device and we
	 * don't want to be forgiving debts while the device is struggling from
	 * write bursts. If we're missing latency targets, consider the device
	 * fully utilized.
	 */
	if (ioc->busy_level > 0)
		usage_us_sum = max_t(u64, usage_us_sum, ioc->period_us);

	ioc->dfgv_usage_us_sum += usage_us_sum;
	if (time_before64(now->now, ioc->dfgv_period_at + DFGV_PERIOD))
		return;

	/*
	 * At least DFGV_PERIOD has passed since the last period. Calculate the
	 * average usage and reset the period counters.
	 */
	dur = now->now - ioc->dfgv_period_at;
	usage_pct = div64_u64(100 * ioc->dfgv_usage_us_sum, dur);

	ioc->dfgv_period_at = now->now;
	ioc->dfgv_usage_us_sum = 0;

	/* if was too busy, reset everything */
	if (usage_pct > DFGV_USAGE_PCT) {
		ioc->dfgv_period_rem = 0;
		return;
	}

	/*
	 * Usage is lower than threshold. Let's forgive some debts. Debt
	 * forgiveness runs off of the usual ioc timer but its period usually
	 * doesn't match ioc's. Compensate the difference by performing the
	 * reduction as many times as would fit in the duration since the last
	 * run and carrying over the left-over duration in @ioc->dfgv_period_rem
	 * - if ioc period is 75% of DFGV_PERIOD, one out of three consecutive
	 * reductions is doubled.
	 */
	nr_cycles = dur + ioc->dfgv_period_rem;
	ioc->dfgv_period_rem = do_div(nr_cycles, DFGV_PERIOD);

	list_for_each_entry(iocg, &ioc->active_iocgs, active_list) {
		u64 __maybe_unused old_debt, __maybe_unused old_delay;

		if (!iocg->abs_vdebt && !iocg->delay)
			continue;

		spin_lock(&iocg->waitq.lock);

		old_debt = iocg->abs_vdebt;
		old_delay = iocg->delay;

		nr_cycles_shift = min_t(u64, nr_cycles, BITS_PER_LONG - 1);
		if (iocg->abs_vdebt)
			iocg->abs_vdebt = iocg->abs_vdebt >> nr_cycles_shift ?: 1;

		if (iocg->delay)
			iocg->delay = iocg->delay >> nr_cycles_shift ?: 1;

		iocg_kick_waitq(iocg, true, now);

		TRACE_IOCG_PATH(iocg_forgive_debt, iocg, now, usage_pct,
				old_debt, iocg->abs_vdebt,
				old_delay, iocg->delay);

		spin_unlock(&iocg->waitq.lock);
	}
}

/*
 * Check the active iocgs' state to avoid oversleeping and deactive
 * idle iocgs.
 *
 * Since waiters determine the sleep durations based on the vrate
 * they saw at the time of sleep, if vrate has increased, some
 * waiters could be sleeping for too long. Wake up tardy waiters
 * which should have woken up in the last period and expire idle
 * iocgs.
 */
static int ioc_check_iocgs(struct ioc *ioc, struct ioc_now *now)
{
	int nr_debtors = 0;
	struct ioc_gq *iocg, *tiocg;

	list_for_each_entry_safe(iocg, tiocg, &ioc->active_iocgs, active_list) {
		if (!waitqueue_active(&iocg->waitq) && !iocg->abs_vdebt &&
		    !iocg->delay && !iocg_is_idle(iocg))
			continue;

		spin_lock(&iocg->waitq.lock);

		/* flush wait and indebt stat deltas */
		if (iocg->wait_since) {
			iocg->stat.wait_us += now->now - iocg->wait_since;
			iocg->wait_since = now->now;
		}
		if (iocg->indebt_since) {
			iocg->stat.indebt_us +=
				now->now - iocg->indebt_since;
			iocg->indebt_since = now->now;
		}
		if (iocg->indelay_since) {
			iocg->stat.indelay_us +=
				now->now - iocg->indelay_since;
			iocg->indelay_since = now->now;
		}

		if (waitqueue_active(&iocg->waitq) || iocg->abs_vdebt ||
		    iocg->delay) {
			/* might be oversleeping vtime / hweight changes, kick */
			iocg_kick_waitq(iocg, true, now);
			if (iocg->abs_vdebt || iocg->delay)
				nr_debtors++;
		} else if (iocg_is_idle(iocg)) {
			/* no waiter and idle, deactivate */
			u64 vtime = atomic64_read(&iocg->vtime);
			s64 excess;

			/*
			 * @iocg has been inactive for a full duration and will
			 * have a high budget. Account anything above target as
			 * error and throw away. On reactivation, it'll start
			 * with the target budget.
			 */
			excess = now->vnow - vtime - ioc->margins.target;
			if (excess > 0) {
				u32 old_hwi;

				current_hweight(iocg, NULL, &old_hwi);
				ioc->vtime_err -= div64_u64(excess * old_hwi,
							    WEIGHT_ONE);
			}

			TRACE_IOCG_PATH(iocg_idle, iocg, now,
					atomic64_read(&iocg->active_period),
					atomic64_read(&ioc->cur_period), vtime);
			__propagate_weights(iocg, 0, 0, false, now);
			list_del_init(&iocg->active_list);
		}

		spin_unlock(&iocg->waitq.lock);
	}

	commit_weights(ioc);
	return nr_debtors;
}

static void ioc_timer_fn(struct timer_list *timer)
{
	struct ioc *ioc = container_of(timer, struct ioc, timer);
	struct ioc_gq *iocg, *tiocg;
	struct ioc_now now;
	LIST_HEAD(surpluses);
	int nr_debtors, nr_shortages = 0, nr_lagging = 0;
	u64 usage_us_sum = 0;
	u32 ppm_rthr;
	u32 ppm_wthr;
	u32 missed_ppm[2], rq_wait_pct;
	u64 period_vtime;
	int prev_busy_level;

	/* how were the latencies during the period? */
	ioc_lat_stat(ioc, missed_ppm, &rq_wait_pct);

	/* take care of active iocgs */
	spin_lock_irq(&ioc->lock);

	ppm_rthr = MILLION - ioc->params.qos[QOS_RPPM];
	ppm_wthr = MILLION - ioc->params.qos[QOS_WPPM];
	ioc_now(ioc, &now);

	period_vtime = now.vnow - ioc->period_at_vtime;
	if (WARN_ON_ONCE(!period_vtime)) {
		spin_unlock_irq(&ioc->lock);
		return;
	}

	nr_debtors = ioc_check_iocgs(ioc, &now);

	/*
	 * Wait and indebt stat are flushed above and the donation calculation
	 * below needs updated usage stat. Let's bring stat up-to-date.
	 */
	iocg_flush_stat(&ioc->active_iocgs, &now);

	/* calc usage and see whether some weights need to be moved around */
	list_for_each_entry(iocg, &ioc->active_iocgs, active_list) {
		u64 vdone, vtime, usage_us;
		u32 hw_active, hw_inuse;

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

		/*
		 * Determine absolute usage factoring in in-flight IOs to avoid
		 * high-latency completions appearing as idle.
		 */
		usage_us = iocg->usage_delta_us;
		usage_us_sum += usage_us;

		/* see whether there's surplus vtime */
		WARN_ON_ONCE(!list_empty(&iocg->surplus_list));
		if (hw_inuse < hw_active ||
		    (!waitqueue_active(&iocg->waitq) &&
		     time_before64(vtime, now.vnow - ioc->margins.low))) {
			u32 hwa, old_hwi, hwm, new_hwi, usage;
			u64 usage_dur;

			if (vdone != vtime) {
				u64 inflight_us = DIV64_U64_ROUND_UP(
					cost_to_abs_cost(vtime - vdone, hw_inuse),
					ioc->vtime_base_rate);

				usage_us = max(usage_us, inflight_us);
			}

			/* convert to hweight based usage ratio */
			if (time_after64(iocg->activated_at, ioc->period_at))
				usage_dur = max_t(u64, now.now - iocg->activated_at, 1);
			else
				usage_dur = max_t(u64, now.now - ioc->period_at, 1);

			usage = clamp_t(u32,
				DIV64_U64_ROUND_UP(usage_us * WEIGHT_ONE,
						   usage_dur),
				1, WEIGHT_ONE);

			/*
			 * Already donating or accumulated enough to start.
			 * Determine the donation amount.
			 */
			current_hweight(iocg, &hwa, &old_hwi);
			hwm = current_hweight_max(iocg);
			new_hwi = hweight_after_donation(iocg, old_hwi, hwm,
							 usage, &now);
			/*
			 * Donation calculation assumes hweight_after_donation
			 * to be positive, a condition that a donor w/ hwa < 2
			 * can't meet. Don't bother with donation if hwa is
			 * below 2. It's not gonna make a meaningful difference
			 * anyway.
			 */
			if (new_hwi < hwm && hwa >= 2) {
				iocg->hweight_donating = hwa;
				iocg->hweight_after_donation = new_hwi;
				list_add(&iocg->surplus_list, &surpluses);
			} else if (!iocg->abs_vdebt) {
				/*
				 * @iocg doesn't have enough to donate. Reset
				 * its inuse to active.
				 *
				 * Don't reset debtors as their inuse's are
				 * owned by debt handling. This shouldn't affect
				 * donation calculuation in any meaningful way
				 * as @iocg doesn't have a meaningful amount of
				 * share anyway.
				 */
				TRACE_IOCG_PATH(inuse_shortage, iocg, &now,
						iocg->inuse, iocg->active,
						iocg->hweight_inuse, new_hwi);

				__propagate_weights(iocg, iocg->active,
						    iocg->active, true, &now);
				nr_shortages++;
			}
		} else {
			/* genuinely short on vtime */
			nr_shortages++;
		}
	}

	if (!list_empty(&surpluses) && nr_shortages)
		transfer_surpluses(&surpluses, &now);

	commit_weights(ioc);

	/* surplus list should be dissolved after use */
	list_for_each_entry_safe(iocg, tiocg, &surpluses, surplus_list)
		list_del_init(&iocg->surplus_list);

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
		/* clearly missing QoS targets, slow down vrate */
		ioc->busy_level = max(ioc->busy_level, 0);
		ioc->busy_level++;
	} else if (rq_wait_pct <= RQ_WAIT_BUSY_PCT * UNBUSY_THR_PCT / 100 &&
		   missed_ppm[READ] <= ppm_rthr * UNBUSY_THR_PCT / 100 &&
		   missed_ppm[WRITE] <= ppm_wthr * UNBUSY_THR_PCT / 100) {
		/* QoS targets are being met with >25% margin */
		if (nr_shortages) {
			/*
			 * We're throttling while the device has spare
			 * capacity.  If vrate was being slowed down, stop.
			 */
			ioc->busy_level = min(ioc->busy_level, 0);

			/*
			 * If there are IOs spanning multiple periods, wait
			 * them out before pushing the device harder.
			 */
			if (!nr_lagging)
				ioc->busy_level--;
		} else {
			/*
			 * Nobody is being throttled and the users aren't
			 * issuing enough IOs to saturate the device.  We
			 * simply don't know how close the device is to
			 * saturation.  Coast.
			 */
			ioc->busy_level = 0;
		}
	} else {
		/* inside the hysterisis margin, we're good */
		ioc->busy_level = 0;
	}

	ioc->busy_level = clamp(ioc->busy_level, -1000, 1000);

	ioc_adjust_base_vrate(ioc, rq_wait_pct, nr_lagging, nr_shortages,
			      prev_busy_level, missed_ppm);

	ioc_refresh_params(ioc, false);

	ioc_forgive_debts(ioc, usage_us_sum, nr_debtors, &now);

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
			ioc->vtime_err = 0;
			ioc->running = IOC_IDLE;
		}

		ioc_refresh_vrate(ioc, &now);
	}

	spin_unlock_irq(&ioc->lock);
}

static u64 adjust_inuse_and_calc_cost(struct ioc_gq *iocg, u64 vtime,
				      u64 abs_cost, struct ioc_now *now)
{
	struct ioc *ioc = iocg->ioc;
	struct ioc_margins *margins = &ioc->margins;
	u32 __maybe_unused old_inuse = iocg->inuse, __maybe_unused old_hwi;
	u32 hwi, adj_step;
	s64 margin;
	u64 cost, new_inuse;
	unsigned long flags;

	current_hweight(iocg, NULL, &hwi);
	old_hwi = hwi;
	cost = abs_cost_to_cost(abs_cost, hwi);
	margin = now->vnow - vtime - cost;

	/* debt handling owns inuse for debtors */
	if (iocg->abs_vdebt)
		return cost;

	/*
	 * We only increase inuse during period and do so if the margin has
	 * deteriorated since the previous adjustment.
	 */
	if (margin >= iocg->saved_margin || margin >= margins->low ||
	    iocg->inuse == iocg->active)
		return cost;

	spin_lock_irqsave(&ioc->lock, flags);

	/* we own inuse only when @iocg is in the normal active state */
	if (iocg->abs_vdebt || list_empty(&iocg->active_list)) {
		spin_unlock_irqrestore(&ioc->lock, flags);
		return cost;
	}

	/*
	 * Bump up inuse till @abs_cost fits in the existing budget.
	 * adj_step must be determined after acquiring ioc->lock - we might
	 * have raced and lost to another thread for activation and could
	 * be reading 0 iocg->active before ioc->lock which will lead to
	 * infinite loop.
	 */
	new_inuse = iocg->inuse;
	adj_step = DIV_ROUND_UP(iocg->active * INUSE_ADJ_STEP_PCT, 100);
	do {
		new_inuse = new_inuse + adj_step;
		propagate_weights(iocg, iocg->active, new_inuse, true, now);
		current_hweight(iocg, NULL, &hwi);
		cost = abs_cost_to_cost(abs_cost, hwi);
	} while (time_after64(vtime + cost, now->vnow) &&
		 iocg->inuse != iocg->active);

	spin_unlock_irqrestore(&ioc->lock, flags);

	TRACE_IOCG_PATH(inuse_adjust, iocg, now,
			old_inuse, iocg->inuse, old_hwi, hwi);

	return cost;
}

static void calc_vtime_cost_builtin(struct bio *bio, struct ioc_gq *iocg,
				    bool is_merge, u64 *costp)
{
	struct ioc *ioc = iocg->ioc;
	u64 coef_seqio, coef_randio, coef_page;
	u64 pages = max_t(u64, bio_sectors(bio) >> IOC_SECT_TO_PAGE_SHIFT, 1);
	u64 seek_pages = 0;
	u64 cost = 0;

	/* Can't calculate cost for empty bio */
	if (!bio->bi_iter.bi_size)
		goto out;

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

static void calc_size_vtime_cost_builtin(struct request *rq, struct ioc *ioc,
					 u64 *costp)
{
	unsigned int pages = blk_rq_stats_sectors(rq) >> IOC_SECT_TO_PAGE_SHIFT;

	switch (req_op(rq)) {
	case REQ_OP_READ:
		*costp = pages * ioc->params.lcoefs[LCOEF_RPAGE];
		break;
	case REQ_OP_WRITE:
		*costp = pages * ioc->params.lcoefs[LCOEF_WPAGE];
		break;
	default:
		*costp = 0;
	}
}

static u64 calc_size_vtime_cost(struct request *rq, struct ioc *ioc)
{
	u64 cost;

	calc_size_vtime_cost_builtin(rq, ioc, &cost);
	return cost;
}

static void ioc_rqos_throttle(struct rq_qos *rqos, struct bio *bio)
{
	struct blkcg_gq *blkg = bio->bi_blkg;
	struct ioc *ioc = rqos_to_ioc(rqos);
	struct ioc_gq *iocg = blkg_to_iocg(blkg);
	struct ioc_now now;
	struct iocg_wait wait;
	u64 abs_cost, cost, vtime;
	bool use_debt, ioc_locked;
	unsigned long flags;

	/* bypass IOs if disabled, still initializing, or for root cgroup */
	if (!ioc->enabled || !iocg || !iocg->level)
		return;

	/* calculate the absolute vtime cost */
	abs_cost = calc_vtime_cost(bio, iocg, false);
	if (!abs_cost)
		return;

	if (!iocg_activate(iocg, &now))
		return;

	iocg->cursor = bio_end_sector(bio);
	vtime = atomic64_read(&iocg->vtime);
	cost = adjust_inuse_and_calc_cost(iocg, vtime, abs_cost, &now);

	/*
	 * If no one's waiting and within budget, issue right away.  The
	 * tests are racy but the races aren't systemic - we only miss once
	 * in a while which is fine.
	 */
	if (!waitqueue_active(&iocg->waitq) && !iocg->abs_vdebt &&
	    time_before_eq64(vtime + cost, now.vnow)) {
		iocg_commit_bio(iocg, bio, abs_cost, cost);
		return;
	}

	/*
	 * We're over budget. This can be handled in two ways. IOs which may
	 * cause priority inversions are punted to @ioc->aux_iocg and charged as
	 * debt. Otherwise, the issuer is blocked on @iocg->waitq. Debt handling
	 * requires @ioc->lock, waitq handling @iocg->waitq.lock. Determine
	 * whether debt handling is needed and acquire locks accordingly.
	 */
	use_debt = bio_issue_as_root_blkg(bio) || fatal_signal_pending(current);
	ioc_locked = use_debt || READ_ONCE(iocg->abs_vdebt);
retry_lock:
	iocg_lock(iocg, ioc_locked, &flags);

	/*
	 * @iocg must stay activated for debt and waitq handling. Deactivation
	 * is synchronized against both ioc->lock and waitq.lock and we won't
	 * get deactivated as long as we're waiting or has debt, so we're good
	 * if we're activated here. In the unlikely cases that we aren't, just
	 * issue the IO.
	 */
	if (unlikely(list_empty(&iocg->active_list))) {
		iocg_unlock(iocg, ioc_locked, &flags);
		iocg_commit_bio(iocg, bio, abs_cost, cost);
		return;
	}

	/*
	 * We're over budget. If @bio has to be issued regardless, remember
	 * the abs_cost instead of advancing vtime. iocg_kick_waitq() will pay
	 * off the debt before waking more IOs.
	 *
	 * This way, the debt is continuously paid off each period with the
	 * actual budget available to the cgroup. If we just wound vtime, we
	 * would incorrectly use the current hw_inuse for the entire amount
	 * which, for example, can lead to the cgroup staying blocked for a
	 * long time even with substantially raised hw_inuse.
	 *
	 * An iocg with vdebt should stay online so that the timer can keep
	 * deducting its vdebt and [de]activate use_delay mechanism
	 * accordingly. We don't want to race against the timer trying to
	 * clear them and leave @iocg inactive w/ dangling use_delay heavily
	 * penalizing the cgroup and its descendants.
	 */
	if (use_debt) {
		iocg_incur_debt(iocg, abs_cost, &now);
		if (iocg_kick_delay(iocg, &now))
			blkcg_schedule_throttle(rqos->disk,
					(bio->bi_opf & REQ_SWAP) == REQ_SWAP);
		iocg_unlock(iocg, ioc_locked, &flags);
		return;
	}

	/* guarantee that iocgs w/ waiters have maximum inuse */
	if (!iocg->abs_vdebt && iocg->inuse != iocg->active) {
		if (!ioc_locked) {
			iocg_unlock(iocg, false, &flags);
			ioc_locked = true;
			goto retry_lock;
		}
		propagate_weights(iocg, iocg->active, iocg->active, true,
				  &now);
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
	init_waitqueue_func_entry(&wait.wait, iocg_wake_fn);
	wait.wait.private = current;
	wait.bio = bio;
	wait.abs_cost = abs_cost;
	wait.committed = false;	/* will be set true by waker */

	__add_wait_queue_entry_tail(&iocg->waitq, &wait.wait);
	iocg_kick_waitq(iocg, ioc_locked, &now);

	iocg_unlock(iocg, ioc_locked, &flags);

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
	struct ioc *ioc = rqos_to_ioc(rqos);
	sector_t bio_end = bio_end_sector(bio);
	struct ioc_now now;
	u64 vtime, abs_cost, cost;
	unsigned long flags;

	/* bypass if disabled, still initializing, or for root cgroup */
	if (!ioc->enabled || !iocg || !iocg->level)
		return;

	abs_cost = calc_vtime_cost(bio, iocg, true);
	if (!abs_cost)
		return;

	ioc_now(ioc, &now);

	vtime = atomic64_read(&iocg->vtime);
	cost = adjust_inuse_and_calc_cost(iocg, vtime, abs_cost, &now);

	/* update cursor if backmerging into the request at the cursor */
	if (blk_rq_pos(rq) < bio_end &&
	    blk_rq_pos(rq) + blk_rq_sectors(rq) == iocg->cursor)
		iocg->cursor = bio_end;

	/*
	 * Charge if there's enough vtime budget and the existing request has
	 * cost assigned.
	 */
	if (rq->bio && rq->bio->bi_iocost_cost &&
	    time_before_eq64(atomic64_read(&iocg->vtime) + cost, now.vnow)) {
		iocg_commit_bio(iocg, bio, abs_cost, cost);
		return;
	}

	/*
	 * Otherwise, account it as debt if @iocg is online, which it should
	 * be for the vast majority of cases. See debt handling in
	 * ioc_rqos_throttle() for details.
	 */
	spin_lock_irqsave(&ioc->lock, flags);
	spin_lock(&iocg->waitq.lock);

	if (likely(!list_empty(&iocg->active_list))) {
		iocg_incur_debt(iocg, abs_cost, &now);
		if (iocg_kick_delay(iocg, &now))
			blkcg_schedule_throttle(rqos->disk,
					(bio->bi_opf & REQ_SWAP) == REQ_SWAP);
	} else {
		iocg_commit_bio(iocg, bio, abs_cost, cost);
	}

	spin_unlock(&iocg->waitq.lock);
	spin_unlock_irqrestore(&ioc->lock, flags);
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
	struct ioc_pcpu_stat *ccs;
	u64 on_q_ns, rq_wait_ns, size_nsec;
	int pidx, rw;

	if (!ioc->enabled || !rq->alloc_time_ns || !rq->start_time_ns)
		return;

	switch (req_op(rq)) {
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

	on_q_ns = blk_time_get_ns() - rq->alloc_time_ns;
	rq_wait_ns = rq->start_time_ns - rq->alloc_time_ns;
	size_nsec = div64_u64(calc_size_vtime_cost(rq, ioc), VTIME_PER_NSEC);

	ccs = get_cpu_ptr(ioc->pcpu_stat);

	if (on_q_ns <= size_nsec ||
	    on_q_ns - size_nsec <= ioc->params.qos[pidx] * NSEC_PER_USEC)
		local_inc(&ccs->missed[rw].nr_met);
	else
		local_inc(&ccs->missed[rw].nr_missed);

	local64_add(rq_wait_ns, &ccs->rq_wait_ns);

	put_cpu_ptr(ccs);
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

	blkcg_deactivate_policy(rqos->disk, &blkcg_policy_iocost);

	spin_lock_irq(&ioc->lock);
	ioc->running = IOC_STOP;
	spin_unlock_irq(&ioc->lock);

	timer_shutdown_sync(&ioc->timer);
	free_percpu(ioc->pcpu_stat);
	kfree(ioc);
}

static const struct rq_qos_ops ioc_rqos_ops = {
	.throttle = ioc_rqos_throttle,
	.merge = ioc_rqos_merge,
	.done_bio = ioc_rqos_done_bio,
	.done = ioc_rqos_done,
	.queue_depth_changed = ioc_rqos_queue_depth_changed,
	.exit = ioc_rqos_exit,
};

static int blk_iocost_init(struct gendisk *disk)
{
	struct ioc *ioc;
	int i, cpu, ret;

	ioc = kzalloc(sizeof(*ioc), GFP_KERNEL);
	if (!ioc)
		return -ENOMEM;

	ioc->pcpu_stat = alloc_percpu(struct ioc_pcpu_stat);
	if (!ioc->pcpu_stat) {
		kfree(ioc);
		return -ENOMEM;
	}

	for_each_possible_cpu(cpu) {
		struct ioc_pcpu_stat *ccs = per_cpu_ptr(ioc->pcpu_stat, cpu);

		for (i = 0; i < ARRAY_SIZE(ccs->missed); i++) {
			local_set(&ccs->missed[i].nr_met, 0);
			local_set(&ccs->missed[i].nr_missed, 0);
		}
		local64_set(&ccs->rq_wait_ns, 0);
	}

	spin_lock_init(&ioc->lock);
	timer_setup(&ioc->timer, ioc_timer_fn, 0);
	INIT_LIST_HEAD(&ioc->active_iocgs);

	ioc->running = IOC_IDLE;
	ioc->vtime_base_rate = VTIME_PER_USEC;
	atomic64_set(&ioc->vtime_rate, VTIME_PER_USEC);
	seqcount_spinlock_init(&ioc->period_seqcount, &ioc->lock);
	ioc->period_at = ktime_to_us(blk_time_get());
	atomic64_set(&ioc->cur_period, 0);
	atomic_set(&ioc->hweight_gen, 0);

	spin_lock_irq(&ioc->lock);
	ioc->autop_idx = AUTOP_INVALID;
	ioc_refresh_params_disk(ioc, true, disk);
	spin_unlock_irq(&ioc->lock);

	/*
	 * rqos must be added before activation to allow ioc_pd_init() to
	 * lookup the ioc from q. This means that the rqos methods may get
	 * called before policy activation completion, can't assume that the
	 * target bio has an iocg associated and need to test for NULL iocg.
	 */
	ret = rq_qos_add(&ioc->rqos, disk, RQ_QOS_COST, &ioc_rqos_ops);
	if (ret)
		goto err_free_ioc;

	ret = blkcg_activate_policy(disk, &blkcg_policy_iocost);
	if (ret)
		goto err_del_qos;
	return 0;

err_del_qos:
	rq_qos_del(&ioc->rqos);
err_free_ioc:
	free_percpu(ioc->pcpu_stat);
	kfree(ioc);
	return ret;
}

static struct blkcg_policy_data *ioc_cpd_alloc(gfp_t gfp)
{
	struct ioc_cgrp *iocc;

	iocc = kzalloc(sizeof(struct ioc_cgrp), gfp);
	if (!iocc)
		return NULL;

	iocc->dfl_weight = CGROUP_WEIGHT_DFL * WEIGHT_ONE;
	return &iocc->cpd;
}

static void ioc_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(container_of(cpd, struct ioc_cgrp, cpd));
}

static struct blkg_policy_data *ioc_pd_alloc(struct gendisk *disk,
		struct blkcg *blkcg, gfp_t gfp)
{
	int levels = blkcg->css.cgroup->level + 1;
	struct ioc_gq *iocg;

	iocg = kzalloc_node(struct_size(iocg, ancestors, levels), gfp,
			    disk->node_id);
	if (!iocg)
		return NULL;

	iocg->pcpu_stat = alloc_percpu_gfp(struct iocg_pcpu_stat, gfp);
	if (!iocg->pcpu_stat) {
		kfree(iocg);
		return NULL;
	}

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
	atomic64_set(&iocg->active_period, atomic64_read(&ioc->cur_period));
	INIT_LIST_HEAD(&iocg->active_list);
	INIT_LIST_HEAD(&iocg->walk_list);
	INIT_LIST_HEAD(&iocg->surplus_list);
	iocg->hweight_active = WEIGHT_ONE;
	iocg->hweight_inuse = WEIGHT_ONE;

	init_waitqueue_head(&iocg->waitq);
	hrtimer_init(&iocg->waitq_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	iocg->waitq_timer.function = iocg_waitq_timer_fn;

	iocg->level = blkg->blkcg->css.cgroup->level;

	for (tblkg = blkg; tblkg; tblkg = tblkg->parent) {
		struct ioc_gq *tiocg = blkg_to_iocg(tblkg);
		iocg->ancestors[tiocg->level] = tiocg;
	}

	spin_lock_irqsave(&ioc->lock, flags);
	weight_updated(iocg, &now);
	spin_unlock_irqrestore(&ioc->lock, flags);
}

static void ioc_pd_free(struct blkg_policy_data *pd)
{
	struct ioc_gq *iocg = pd_to_iocg(pd);
	struct ioc *ioc = iocg->ioc;
	unsigned long flags;

	if (ioc) {
		spin_lock_irqsave(&ioc->lock, flags);

		if (!list_empty(&iocg->active_list)) {
			struct ioc_now now;

			ioc_now(ioc, &now);
			propagate_weights(iocg, 0, 0, false, &now);
			list_del_init(&iocg->active_list);
		}

		WARN_ON_ONCE(!list_empty(&iocg->walk_list));
		WARN_ON_ONCE(!list_empty(&iocg->surplus_list));

		spin_unlock_irqrestore(&ioc->lock, flags);

		hrtimer_cancel(&iocg->waitq_timer);
	}
	free_percpu(iocg->pcpu_stat);
	kfree(iocg);
}

static void ioc_pd_stat(struct blkg_policy_data *pd, struct seq_file *s)
{
	struct ioc_gq *iocg = pd_to_iocg(pd);
	struct ioc *ioc = iocg->ioc;

	if (!ioc->enabled)
		return;

	if (iocg->level == 0) {
		unsigned vp10k = DIV64_U64_ROUND_CLOSEST(
			ioc->vtime_base_rate * 10000,
			VTIME_PER_USEC);
		seq_printf(s, " cost.vrate=%u.%02u", vp10k / 100, vp10k % 100);
	}

	seq_printf(s, " cost.usage=%llu", iocg->last_stat.usage_us);

	if (blkcg_debug_stats)
		seq_printf(s, " cost.wait=%llu cost.indebt=%llu cost.indelay=%llu",
			iocg->last_stat.wait_us,
			iocg->last_stat.indebt_us,
			iocg->last_stat.indelay_us);
}

static u64 ioc_weight_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			     int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioc_gq *iocg = pd_to_iocg(pd);

	if (dname && iocg->cfg_weight)
		seq_printf(sf, "%s %u\n", dname, iocg->cfg_weight / WEIGHT_ONE);
	return 0;
}


static int ioc_weight_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct ioc_cgrp *iocc = blkcg_to_iocc(blkcg);

	seq_printf(sf, "default %u\n", iocc->dfl_weight / WEIGHT_ONE);
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
	struct ioc_now now;
	struct ioc_gq *iocg;
	u32 v;
	int ret;

	if (!strchr(buf, ':')) {
		struct blkcg_gq *blkg;

		if (!sscanf(buf, "default %u", &v) && !sscanf(buf, "%u", &v))
			return -EINVAL;

		if (v < CGROUP_WEIGHT_MIN || v > CGROUP_WEIGHT_MAX)
			return -EINVAL;

		spin_lock_irq(&blkcg->lock);
		iocc->dfl_weight = v * WEIGHT_ONE;
		hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
			struct ioc_gq *iocg = blkg_to_iocg(blkg);

			if (iocg) {
				spin_lock(&iocg->ioc->lock);
				ioc_now(iocg->ioc, &now);
				weight_updated(iocg, &now);
				spin_unlock(&iocg->ioc->lock);
			}
		}
		spin_unlock_irq(&blkcg->lock);

		return nbytes;
	}

	blkg_conf_init(&ctx, buf);

	ret = blkg_conf_prep(blkcg, &blkcg_policy_iocost, &ctx);
	if (ret)
		goto err;

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
	iocg->cfg_weight = v * WEIGHT_ONE;
	ioc_now(iocg->ioc, &now);
	weight_updated(iocg, &now);
	spin_unlock(&iocg->ioc->lock);

	blkg_conf_exit(&ctx);
	return nbytes;

einval:
	ret = -EINVAL;
err:
	blkg_conf_exit(&ctx);
	return ret;
}

static u64 ioc_qos_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			  int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioc *ioc = pd_to_iocg(pd)->ioc;

	if (!dname)
		return 0;

	spin_lock(&ioc->lock);
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
	spin_unlock(&ioc->lock);
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
	struct blkg_conf_ctx ctx;
	struct gendisk *disk;
	struct ioc *ioc;
	u32 qos[NR_QOS_PARAMS];
	bool enable, user;
	char *body, *p;
	int ret;

	blkg_conf_init(&ctx, input);

	ret = blkg_conf_open_bdev(&ctx);
	if (ret)
		goto err;

	body = ctx.body;
	disk = ctx.bdev->bd_disk;
	if (!queue_is_mq(disk->queue)) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	ioc = q_to_ioc(disk->queue);
	if (!ioc) {
		ret = blk_iocost_init(disk);
		if (ret)
			goto err;
		ioc = q_to_ioc(disk->queue);
	}

	blk_mq_freeze_queue(disk->queue);
	blk_mq_quiesce_queue(disk->queue);

	spin_lock_irq(&ioc->lock);
	memcpy(qos, ioc->params.qos, sizeof(qos));
	enable = ioc->enabled;
	user = ioc->user_qos_params;

	while ((p = strsep(&body, " \t\n"))) {
		substring_t args[MAX_OPT_ARGS];
		char buf[32];
		int tok;
		s64 v;

		if (!*p)
			continue;

		switch (match_token(p, qos_ctrl_tokens, args)) {
		case QOS_ENABLE:
			if (match_u64(&args[0], &v))
				goto einval;
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

	if (enable && !ioc->enabled) {
		blk_stat_enable_accounting(disk->queue);
		blk_queue_flag_set(QUEUE_FLAG_RQ_ALLOC_TIME, disk->queue);
		ioc->enabled = true;
	} else if (!enable && ioc->enabled) {
		blk_stat_disable_accounting(disk->queue);
		blk_queue_flag_clear(QUEUE_FLAG_RQ_ALLOC_TIME, disk->queue);
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

	if (enable)
		wbt_disable_default(disk);
	else
		wbt_enable_default(disk);

	blk_mq_unquiesce_queue(disk->queue);
	blk_mq_unfreeze_queue(disk->queue);

	blkg_conf_exit(&ctx);
	return nbytes;
einval:
	spin_unlock_irq(&ioc->lock);

	blk_mq_unquiesce_queue(disk->queue);
	blk_mq_unfreeze_queue(disk->queue);

	ret = -EINVAL;
err:
	blkg_conf_exit(&ctx);
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

	spin_lock(&ioc->lock);
	seq_printf(sf, "%s ctrl=%s model=linear "
		   "rbps=%llu rseqiops=%llu rrandiops=%llu "
		   "wbps=%llu wseqiops=%llu wrandiops=%llu\n",
		   dname, ioc->user_cost_model ? "user" : "auto",
		   u[I_LCOEF_RBPS], u[I_LCOEF_RSEQIOPS], u[I_LCOEF_RRANDIOPS],
		   u[I_LCOEF_WBPS], u[I_LCOEF_WSEQIOPS], u[I_LCOEF_WRANDIOPS]);
	spin_unlock(&ioc->lock);
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
	struct blkg_conf_ctx ctx;
	struct request_queue *q;
	struct ioc *ioc;
	u64 u[NR_I_LCOEFS];
	bool user;
	char *body, *p;
	int ret;

	blkg_conf_init(&ctx, input);

	ret = blkg_conf_open_bdev(&ctx);
	if (ret)
		goto err;

	body = ctx.body;
	q = bdev_get_queue(ctx.bdev);
	if (!queue_is_mq(q)) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	ioc = q_to_ioc(q);
	if (!ioc) {
		ret = blk_iocost_init(ctx.bdev->bd_disk);
		if (ret)
			goto err;
		ioc = q_to_ioc(q);
	}

	blk_mq_freeze_queue(q);
	blk_mq_quiesce_queue(q);

	spin_lock_irq(&ioc->lock);
	memcpy(u, ioc->params.i_lcoefs, sizeof(u));
	user = ioc->user_cost_model;

	while ((p = strsep(&body, " \t\n"))) {
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

	if (user) {
		memcpy(ioc->params.i_lcoefs, u, sizeof(u));
		ioc->user_cost_model = true;
	} else {
		ioc->user_cost_model = false;
	}
	ioc_refresh_params(ioc, true);
	spin_unlock_irq(&ioc->lock);

	blk_mq_unquiesce_queue(q);
	blk_mq_unfreeze_queue(q);

	blkg_conf_exit(&ctx);
	return nbytes;

einval:
	spin_unlock_irq(&ioc->lock);

	blk_mq_unquiesce_queue(q);
	blk_mq_unfreeze_queue(q);

	ret = -EINVAL;
err:
	blkg_conf_exit(&ctx);
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
	.pd_stat_fn	= ioc_pd_stat,
};

static int __init ioc_init(void)
{
	return blkcg_policy_register(&blkcg_policy_iocost);
}

static void __exit ioc_exit(void)
{
	blkcg_policy_unregister(&blkcg_policy_iocost);
}

module_init(ioc_init);
module_exit(ioc_exit);
