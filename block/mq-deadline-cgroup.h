/* SPDX-License-Identifier: GPL-2.0 */

#if !defined(_MQ_DEADLINE_CGROUP_H_)
#define _MQ_DEADLINE_CGROUP_H_

#include <linux/blk-cgroup.h>

struct request_queue;

/**
 * struct io_stats_per_prio - I/O statistics per I/O priority class.
 * @inserted: Number of inserted requests.
 * @merged: Number of merged requests.
 * @dispatched: Number of dispatched requests.
 * @completed: Number of I/O completions.
 */
struct io_stats_per_prio {
	local_t inserted;
	local_t merged;
	local_t dispatched;
	local_t completed;
};

/* I/O statistics per I/O cgroup per I/O priority class (IOPRIO_CLASS_*). */
struct blkcg_io_stats {
	struct io_stats_per_prio stats[4];
};

/**
 * struct dd_blkcg - Per cgroup data.
 * @cpd: blkcg_policy_data structure.
 * @stats: I/O statistics.
 */
struct dd_blkcg {
	struct blkcg_policy_data cpd;	/* must be the first member */
	struct blkcg_io_stats __percpu *stats;
};

/*
 * Count one event of type 'event_type' and with I/O priority class
 * 'prio_class'.
 */
#define ddcg_count(ddcg, event_type, prio_class) do {			\
if (ddcg) {								\
	struct blkcg_io_stats *io_stats = get_cpu_ptr((ddcg)->stats);	\
									\
	BUILD_BUG_ON(!__same_type((ddcg), struct dd_blkcg *));		\
	BUILD_BUG_ON(!__same_type((prio_class), u8));			\
	local_inc(&io_stats->stats[(prio_class)].event_type);		\
	put_cpu_ptr(io_stats);						\
}									\
} while (0)

/*
 * Returns the total number of ddcg_count(ddcg, event_type, prio_class) calls
 * across all CPUs. No locking or barriers since it is fine if the returned
 * sum is slightly outdated.
 */
#define ddcg_sum(ddcg, event_type, prio) ({				\
	unsigned int cpu;						\
	u32 sum = 0;							\
									\
	BUILD_BUG_ON(!__same_type((ddcg), struct dd_blkcg *));		\
	BUILD_BUG_ON(!__same_type((prio), u8));				\
	for_each_present_cpu(cpu)					\
		sum += local_read(&per_cpu_ptr((ddcg)->stats, cpu)->	\
				  stats[(prio)].event_type);		\
	sum;								\
})

#ifdef CONFIG_BLK_CGROUP

/**
 * struct dd_blkg - Per (cgroup, request queue) data.
 * @pd: blkg_policy_data structure.
 */
struct dd_blkg {
	struct blkg_policy_data pd;	/* must be the first member */
};

struct dd_blkcg *dd_blkcg_from_bio(struct bio *bio);
int dd_activate_policy(struct request_queue *q);
void dd_deactivate_policy(struct request_queue *q);
int __init dd_blkcg_init(void);
void __exit dd_blkcg_exit(void);

#else /* CONFIG_BLK_CGROUP */

static inline struct dd_blkcg *dd_blkcg_from_bio(struct bio *bio)
{
	return NULL;
}

static inline int dd_activate_policy(struct request_queue *q)
{
	return 0;
}

static inline void dd_deactivate_policy(struct request_queue *q)
{
}

static inline int dd_blkcg_init(void)
{
	return 0;
}

static inline void dd_blkcg_exit(void)
{
}

#endif /* CONFIG_BLK_CGROUP */

#endif /* _MQ_DEADLINE_CGROUP_H_ */
