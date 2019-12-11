/* SPDX-License-Identifier: GPL-2.0
 *
 * Legacy blkg rwstat helpers enabled by CONFIG_BLK_CGROUP_RWSTAT.
 * Do not use in new code.
 */
#ifndef _BLK_CGROUP_RWSTAT_H
#define _BLK_CGROUP_RWSTAT_H

#include <linux/blk-cgroup.h>

enum blkg_rwstat_type {
	BLKG_RWSTAT_READ,
	BLKG_RWSTAT_WRITE,
	BLKG_RWSTAT_SYNC,
	BLKG_RWSTAT_ASYNC,
	BLKG_RWSTAT_DISCARD,

	BLKG_RWSTAT_NR,
	BLKG_RWSTAT_TOTAL = BLKG_RWSTAT_NR,
};

/*
 * blkg_[rw]stat->aux_cnt is excluded for local stats but included for
 * recursive.  Used to carry stats of dead children.
 */
struct blkg_rwstat {
	struct percpu_counter		cpu_cnt[BLKG_RWSTAT_NR];
	atomic64_t			aux_cnt[BLKG_RWSTAT_NR];
};

struct blkg_rwstat_sample {
	u64				cnt[BLKG_RWSTAT_NR];
};

static inline u64 blkg_rwstat_read_counter(struct blkg_rwstat *rwstat,
		unsigned int idx)
{
	return atomic64_read(&rwstat->aux_cnt[idx]) +
		percpu_counter_sum_positive(&rwstat->cpu_cnt[idx]);
}

int blkg_rwstat_init(struct blkg_rwstat *rwstat, gfp_t gfp);
void blkg_rwstat_exit(struct blkg_rwstat *rwstat);
u64 __blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
			 const struct blkg_rwstat_sample *rwstat);
u64 blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
		       int off);
void blkg_rwstat_recursive_sum(struct blkcg_gq *blkg, struct blkcg_policy *pol,
		int off, struct blkg_rwstat_sample *sum);


/**
 * blkg_rwstat_add - add a value to a blkg_rwstat
 * @rwstat: target blkg_rwstat
 * @op: REQ_OP and flags
 * @val: value to add
 *
 * Add @val to @rwstat.  The counters are chosen according to @rw.  The
 * caller is responsible for synchronizing calls to this function.
 */
static inline void blkg_rwstat_add(struct blkg_rwstat *rwstat,
				   unsigned int op, uint64_t val)
{
	struct percpu_counter *cnt;

	if (op_is_discard(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_DISCARD];
	else if (op_is_write(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_WRITE];
	else
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_READ];

	percpu_counter_add_batch(cnt, val, BLKG_STAT_CPU_BATCH);

	if (op_is_sync(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_SYNC];
	else
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_ASYNC];

	percpu_counter_add_batch(cnt, val, BLKG_STAT_CPU_BATCH);
}

/**
 * blkg_rwstat_read - read the current values of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Read the current snapshot of @rwstat and return it in the aux counts.
 */
static inline void blkg_rwstat_read(struct blkg_rwstat *rwstat,
		struct blkg_rwstat_sample *result)
{
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		result->cnt[i] =
			percpu_counter_sum_positive(&rwstat->cpu_cnt[i]);
}

/**
 * blkg_rwstat_total - read the total count of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Return the total count of @rwstat regardless of the IO direction.  This
 * function can be called without synchronization and takes care of u64
 * atomicity.
 */
static inline uint64_t blkg_rwstat_total(struct blkg_rwstat *rwstat)
{
	struct blkg_rwstat_sample tmp = { };

	blkg_rwstat_read(rwstat, &tmp);
	return tmp.cnt[BLKG_RWSTAT_READ] + tmp.cnt[BLKG_RWSTAT_WRITE];
}

/**
 * blkg_rwstat_reset - reset a blkg_rwstat
 * @rwstat: blkg_rwstat to reset
 */
static inline void blkg_rwstat_reset(struct blkg_rwstat *rwstat)
{
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++) {
		percpu_counter_set(&rwstat->cpu_cnt[i], 0);
		atomic64_set(&rwstat->aux_cnt[i], 0);
	}
}

/**
 * blkg_rwstat_add_aux - add a blkg_rwstat into another's aux count
 * @to: the destination blkg_rwstat
 * @from: the source
 *
 * Add @from's count including the aux one to @to's aux count.
 */
static inline void blkg_rwstat_add_aux(struct blkg_rwstat *to,
				       struct blkg_rwstat *from)
{
	u64 sum[BLKG_RWSTAT_NR];
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		sum[i] = percpu_counter_sum_positive(&from->cpu_cnt[i]);

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		atomic64_add(sum[i] + atomic64_read(&from->aux_cnt[i]),
			     &to->aux_cnt[i]);
}
#endif	/* _BLK_CGROUP_RWSTAT_H */
