// SPDX-License-Identifier: GPL-2.0
/*
 * Historical Service Time
 *
 *  Keeps a time-weighted exponential moving average of the historical
 *  service time. Estimates future service time based on the historical
 *  service time and the number of outstanding requests.
 *
 *  Marks paths stale if they have not finished within hst *
 *  num_paths. If a path is stale and unused, we will send a single
 *  request to probe in case the path has improved. This situation
 *  generally arises if the path is so much worse than others that it
 *  will never have the best estimated service time, or if the entire
 *  multipath device is unused. If a path is stale and in use, limit the
 *  number of requests it can receive with the assumption that the path
 *  has become degraded.
 *
 *  To avoid repeatedly calculating exponents for time weighting, times
 *  are split into HST_WEIGHT_COUNT buckets each (1 >> HST_BUCKET_SHIFT)
 *  ns, and the weighting is pre-calculated.
 *
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched/clock.h>


#define DM_MSG_PREFIX	"multipath historical-service-time"
#define HST_MIN_IO 1
#define HST_VERSION "0.1.1"

#define HST_FIXED_SHIFT 10  /* 10 bits of decimal precision */
#define HST_FIXED_MAX (ULLONG_MAX >> HST_FIXED_SHIFT)
#define HST_FIXED_1 (1 << HST_FIXED_SHIFT)
#define HST_FIXED_95 972
#define HST_MAX_INFLIGHT HST_FIXED_1
#define HST_BUCKET_SHIFT 24 /* Buckets are ~ 16ms */
#define HST_WEIGHT_COUNT 64ULL

struct selector {
	struct list_head valid_paths;
	struct list_head failed_paths;
	int valid_count;
	spinlock_t lock;

	unsigned int weights[HST_WEIGHT_COUNT];
	unsigned int threshold_multiplier;
};

struct path_info {
	struct list_head list;
	struct dm_path *path;
	unsigned int repeat_count;

	spinlock_t lock;

	u64 historical_service_time; /* Fixed point */

	u64 stale_after;
	u64 last_finish;

	u64 outstanding;
};

/**
 * fixed_power - compute: x^n, in O(log n) time
 *
 * @x:         base of the power
 * @frac_bits: fractional bits of @x
 * @n:         power to raise @x to.
 *
 * By exploiting the relation between the definition of the natural power
 * function: x^n := x*x*...*x (x multiplied by itself for n times), and
 * the binary encoding of numbers used by computers: n := \Sum n_i * 2^i,
 * (where: n_i \elem {0, 1}, the binary vector representing n),
 * we find: x^n := x^(\Sum n_i * 2^i) := \Prod x^(n_i * 2^i), which is
 * of course trivially computable in O(log_2 n), the length of our binary
 * vector.
 *
 * (see: kernel/sched/loadavg.c)
 */
static u64 fixed_power(u64 x, unsigned int frac_bits, unsigned int n)
{
	unsigned long result = 1UL << frac_bits;

	if (n) {
		for (;;) {
			if (n & 1) {
				result *= x;
				result += 1UL << (frac_bits - 1);
				result >>= frac_bits;
			}
			n >>= 1;
			if (!n)
				break;
			x *= x;
			x += 1UL << (frac_bits - 1);
			x >>= frac_bits;
		}
	}

	return result;
}

/*
 * Calculate the next value of an exponential moving average
 * a_1 = a_0 * e + a * (1 - e)
 *
 * @last: [0, ULLONG_MAX >> HST_FIXED_SHIFT]
 * @next: [0, ULLONG_MAX >> HST_FIXED_SHIFT]
 * @weight: [0, HST_FIXED_1]
 *
 * Note:
 *   To account for multiple periods in the same calculation,
 *   a_n = a_0 * e^n + a * (1 - e^n),
 *   so call fixed_ema(last, next, pow(weight, N))
 */
static u64 fixed_ema(u64 last, u64 next, u64 weight)
{
	last *= weight;
	last += next * (HST_FIXED_1 - weight);
	last += 1ULL << (HST_FIXED_SHIFT - 1);
	return last >> HST_FIXED_SHIFT;
}

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->failed_paths);
		spin_lock_init(&s->lock);
		s->valid_count = 0;
	}

	return s;
}

/*
 * Get the weight for a given time span.
 */
static u64 hst_weight(struct path_selector *ps, u64 delta)
{
	struct selector *s = ps->context;
	int bucket = clamp(delta >> HST_BUCKET_SHIFT, 0ULL,
			   HST_WEIGHT_COUNT - 1);

	return s->weights[bucket];
}

/*
 * Set up the weights array.
 *
 * weights[len-1] = 0
 * weights[n] = base ^ (n + 1)
 */
static void hst_set_weights(struct path_selector *ps, unsigned int base)
{
	struct selector *s = ps->context;
	int i;

	if (base >= HST_FIXED_1)
		return;

	for (i = 0; i < HST_WEIGHT_COUNT - 1; i++)
		s->weights[i] = fixed_power(base, HST_FIXED_SHIFT, i + 1);
	s->weights[HST_WEIGHT_COUNT - 1] = 0;
}

static int hst_create(struct path_selector *ps, unsigned int argc, char **argv)
{
	struct selector *s;
	unsigned int base_weight = HST_FIXED_95;
	unsigned int threshold_multiplier = 0;
	char dummy;

	/*
	 * Arguments: [<base_weight> [<threshold_multiplier>]]
	 *   <base_weight>: Base weight for ema [0, 1024) 10-bit fixed point. A
	 *                  value of 0 will completely ignore any history.
	 *                  If not given, default (HST_FIXED_95) is used.
	 *   <threshold_multiplier>: Minimum threshold multiplier for paths to
	 *                  be considered different. That is, a path is
	 *                  considered different iff (p1 > N * p2) where p1
	 *                  is the path with higher service time. A threshold
	 *                  of 1 or 0 has no effect. Defaults to 0.
	 */
	if (argc > 2)
		return -EINVAL;

	if (argc && (sscanf(argv[0], "%u%c", &base_weight, &dummy) != 1 ||
	     base_weight >= HST_FIXED_1)) {
		return -EINVAL;
	}

	if (argc > 1 && (sscanf(argv[1], "%u%c",
				&threshold_multiplier, &dummy) != 1)) {
		return -EINVAL;
	}

	s = alloc_selector();
	if (!s)
		return -ENOMEM;

	ps->context = s;

	hst_set_weights(ps, base_weight);
	s->threshold_multiplier = threshold_multiplier;
	return 0;
}

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

static void hst_destroy(struct path_selector *ps)
{
	struct selector *s = ps->context;

	free_paths(&s->valid_paths);
	free_paths(&s->failed_paths);
	kfree(s);
	ps->context = NULL;
}

static int hst_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	unsigned int sz = 0;
	struct path_info *pi;

	if (!path) {
		struct selector *s = ps->context;

		DMEMIT("2 %u %u ", s->weights[0], s->threshold_multiplier);
	} else {
		pi = path->pscontext;

		switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("%llu %llu %llu ", pi->historical_service_time,
			       pi->outstanding, pi->stale_after);
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("0 ");
			break;
		case STATUSTYPE_IMA:
			*result = '\0';
			break;
		}
	}

	return sz;
}

static int hst_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = ps->context;
	struct path_info *pi;
	unsigned int repeat_count = HST_MIN_IO;
	char dummy;
	unsigned long flags;

	/*
	 * Arguments: [<repeat_count>]
	 *   <repeat_count>: The number of I/Os before switching path.
	 *                   If not given, default (HST_MIN_IO) is used.
	 */
	if (argc > 1) {
		*error = "historical-service-time ps: incorrect number of arguments";
		return -EINVAL;
	}

	if (argc && (sscanf(argv[0], "%u%c", &repeat_count, &dummy) != 1)) {
		*error = "historical-service-time ps: invalid repeat count";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "historical-service-time ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;

	pi->historical_service_time = HST_FIXED_1;

	spin_lock_init(&pi->lock);
	pi->outstanding = 0;

	pi->stale_after = 0;
	pi->last_finish = 0;

	path->pscontext = pi;

	spin_lock_irqsave(&s->lock, flags);
	list_add_tail(&pi->list, &s->valid_paths);
	s->valid_count++;
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

static void hst_fail_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	list_move(&pi->list, &s->failed_paths);
	s->valid_count--;
	spin_unlock_irqrestore(&s->lock, flags);
}

static int hst_reinstate_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	list_move_tail(&pi->list, &s->valid_paths);
	s->valid_count++;
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

static void hst_fill_compare(struct path_info *pi, u64 *hst,
			     u64 *out, u64 *stale)
{
	unsigned long flags;

	spin_lock_irqsave(&pi->lock, flags);
	*hst = pi->historical_service_time;
	*out = pi->outstanding;
	*stale = pi->stale_after;
	spin_unlock_irqrestore(&pi->lock, flags);
}

/*
 * Compare the estimated service time of 2 paths, pi1 and pi2,
 * for the incoming I/O.
 *
 * Returns:
 * < 0 : pi1 is better
 * 0   : no difference between pi1 and pi2
 * > 0 : pi2 is better
 *
 */
static long long hst_compare(struct path_info *pi1, struct path_info *pi2,
			     u64 time_now, struct path_selector *ps)
{
	struct selector *s = ps->context;
	u64 hst1, hst2;
	long long out1, out2, stale1, stale2;
	int pi2_better, over_threshold;

	hst_fill_compare(pi1, &hst1, &out1, &stale1);
	hst_fill_compare(pi2, &hst2, &out2, &stale2);

	/* Check here if estimated latency for two paths are too similar.
	 * If this is the case, we skip extra calculation and just compare
	 * outstanding requests. In this case, any unloaded paths will
	 * be preferred.
	 */
	if (hst1 > hst2)
		over_threshold = hst1 > (s->threshold_multiplier * hst2);
	else
		over_threshold = hst2 > (s->threshold_multiplier * hst1);

	if (!over_threshold)
		return out1 - out2;

	/*
	 * If an unloaded path is stale, choose it. If both paths are unloaded,
	 * choose path that is the most stale.
	 * (If one path is loaded, choose the other)
	 */
	if ((!out1 && stale1 < time_now) || (!out2 && stale2 < time_now) ||
	    (!out1 && !out2))
		return (!out2 * stale1) - (!out1 * stale2);

	/* Compare estimated service time. If outstanding is the same, we
	 * don't need to multiply
	 */
	if (out1 == out2) {
		pi2_better = hst1 > hst2;
	} else {
		/* Potential overflow with out >= 1024 */
		if (unlikely(out1 >= HST_MAX_INFLIGHT ||
			     out2 >= HST_MAX_INFLIGHT)) {
			/* If over 1023 in-flights, we may overflow if hst
			 * is at max. (With this shift we still overflow at
			 * 1048576 in-flights, which is high enough).
			 */
			hst1 >>= HST_FIXED_SHIFT;
			hst2 >>= HST_FIXED_SHIFT;
		}
		pi2_better = (1 + out1) * hst1 > (1 + out2) * hst2;
	}

	/* In the case that the 'winner' is stale, limit to equal usage. */
	if (pi2_better) {
		if (stale2 < time_now)
			return out1 - out2;
		return 1;
	}
	if (stale1 < time_now)
		return out1 - out2;
	return -1;
}

static struct dm_path *hst_select_path(struct path_selector *ps,
				       size_t nr_bytes)
{
	struct selector *s = ps->context;
	struct path_info *pi = NULL, *best = NULL;
	u64 time_now = sched_clock();
	struct dm_path *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (list_empty(&s->valid_paths))
		goto out;

	list_for_each_entry(pi, &s->valid_paths, list) {
		if (!best || (hst_compare(pi, best, time_now, ps) < 0))
			best = pi;
	}

	if (!best)
		goto out;

	/* Move last used path to end (least preferred in case of ties) */
	list_move_tail(&best->list, &s->valid_paths);

	ret = best->path;

out:
	spin_unlock_irqrestore(&s->lock, flags);
	return ret;
}

static int hst_start_io(struct path_selector *ps, struct dm_path *path,
			size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;
	unsigned long flags;

	spin_lock_irqsave(&pi->lock, flags);
	pi->outstanding++;
	spin_unlock_irqrestore(&pi->lock, flags);

	return 0;
}

static u64 path_service_time(struct path_info *pi, u64 start_time)
{
	u64 sched_now = ktime_get_ns();

	/* if a previous disk request has finished after this IO was
	 * sent to the hardware, pretend the submission happened
	 * serially.
	 */
	if (time_after64(pi->last_finish, start_time))
		start_time = pi->last_finish;

	pi->last_finish = sched_now;
	if (time_before64(sched_now, start_time))
		return 0;

	return sched_now - start_time;
}

static int hst_end_io(struct path_selector *ps, struct dm_path *path,
		      size_t nr_bytes, u64 start_time)
{
	struct path_info *pi = path->pscontext;
	struct selector *s = ps->context;
	unsigned long flags;
	u64 st;

	spin_lock_irqsave(&pi->lock, flags);

	st = path_service_time(pi, start_time);
	pi->outstanding--;
	pi->historical_service_time =
		fixed_ema(pi->historical_service_time,
			  min(st * HST_FIXED_1, HST_FIXED_MAX),
			  hst_weight(ps, st));

	/*
	 * On request end, mark path as fresh. If a path hasn't
	 * finished any requests within the fresh period, the estimated
	 * service time is considered too optimistic and we limit the
	 * maximum requests on that path.
	 */
	pi->stale_after = pi->last_finish +
		(s->valid_count * (pi->historical_service_time >> HST_FIXED_SHIFT));

	spin_unlock_irqrestore(&pi->lock, flags);

	return 0;
}

static struct path_selector_type hst_ps = {
	.name		= "historical-service-time",
	.module		= THIS_MODULE,
	.table_args	= 1,
	.info_args	= 3,
	.create		= hst_create,
	.destroy	= hst_destroy,
	.status		= hst_status,
	.add_path	= hst_add_path,
	.fail_path	= hst_fail_path,
	.reinstate_path	= hst_reinstate_path,
	.select_path	= hst_select_path,
	.start_io	= hst_start_io,
	.end_io		= hst_end_io,
};

static int __init dm_hst_init(void)
{
	int r = dm_register_path_selector(&hst_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " HST_VERSION " loaded");

	return r;
}

static void __exit dm_hst_exit(void)
{
	int r = dm_unregister_path_selector(&hst_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_hst_init);
module_exit(dm_hst_exit);

MODULE_DESCRIPTION(DM_NAME " measured service time oriented path selector");
MODULE_AUTHOR("Khazhismel Kumykov <khazhy@google.com>");
MODULE_LICENSE("GPL");
