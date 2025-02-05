/* SPDX-License-Identifier: GPL-2.0
 *
 * Legacy blkg rwstat helpers enabled by CONFIG_BLK_CGROUP_RWSTAT.
 * Do not use in new code.
 */
#include "blk-cgroup-rwstat.h"

int blkg_rwstat_init(struct blkg_rwstat *rwstat, gfp_t gfp)
{
	int i, ret;

	ret = percpu_counter_init_many(rwstat->cpu_cnt, 0, gfp, BLKG_RWSTAT_NR);
	if (ret)
		return ret;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		atomic64_set(&rwstat->aux_cnt[i], 0);
	return 0;
}
EXPORT_SYMBOL_GPL(blkg_rwstat_init);

void blkg_rwstat_exit(struct blkg_rwstat *rwstat)
{
	percpu_counter_destroy_many(rwstat->cpu_cnt, BLKG_RWSTAT_NR);
}
EXPORT_SYMBOL_GPL(blkg_rwstat_exit);

/**
 * __blkg_prfill_rwstat - prfill helper for a blkg_rwstat
 * @sf: seq_file to print to
 * @pd: policy private data of interest
 * @rwstat: rwstat to print
 *
 * Print @rwstat to @sf for the device assocaited with @pd.
 */
u64 __blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
			 const struct blkg_rwstat_sample *rwstat)
{
	static const char *rwstr[] = {
		[BLKG_RWSTAT_READ]	= "Read",
		[BLKG_RWSTAT_WRITE]	= "Write",
		[BLKG_RWSTAT_SYNC]	= "Sync",
		[BLKG_RWSTAT_ASYNC]	= "Async",
		[BLKG_RWSTAT_DISCARD]	= "Discard",
	};
	const char *dname = blkg_dev_name(pd->blkg);
	u64 v;
	int i;

	if (!dname)
		return 0;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		seq_printf(sf, "%s %s %llu\n", dname, rwstr[i],
			   rwstat->cnt[i]);

	v = rwstat->cnt[BLKG_RWSTAT_READ] +
		rwstat->cnt[BLKG_RWSTAT_WRITE] +
		rwstat->cnt[BLKG_RWSTAT_DISCARD];
	seq_printf(sf, "%s Total %llu\n", dname, v);
	return v;
}
EXPORT_SYMBOL_GPL(__blkg_prfill_rwstat);

/**
 * blkg_prfill_rwstat - prfill callback for blkg_rwstat
 * @sf: seq_file to print to
 * @pd: policy private data of interest
 * @off: offset to the blkg_rwstat in @pd
 *
 * prfill callback for printing a blkg_rwstat.
 */
u64 blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
		       int off)
{
	struct blkg_rwstat_sample rwstat = { };

	blkg_rwstat_read((void *)pd + off, &rwstat);
	return __blkg_prfill_rwstat(sf, pd, &rwstat);
}
EXPORT_SYMBOL_GPL(blkg_prfill_rwstat);

/**
 * blkg_rwstat_recursive_sum - collect hierarchical blkg_rwstat
 * @blkg: blkg of interest
 * @pol: blkcg_policy which contains the blkg_rwstat
 * @off: offset to the blkg_rwstat in blkg_policy_data or @blkg
 * @sum: blkg_rwstat_sample structure containing the results
 *
 * Collect the blkg_rwstat specified by @blkg, @pol and @off and all its
 * online descendants and their aux counts.  The caller must be holding the
 * queue lock for online tests.
 *
 * If @pol is NULL, blkg_rwstat is at @off bytes into @blkg; otherwise, it
 * is at @off bytes into @blkg's blkg_policy_data of the policy.
 */
void blkg_rwstat_recursive_sum(struct blkcg_gq *blkg, struct blkcg_policy *pol,
		int off, struct blkg_rwstat_sample *sum)
{
	struct blkcg_gq *pos_blkg;
	struct cgroup_subsys_state *pos_css;
	unsigned int i;

	lockdep_assert_held(&blkg->q->queue_lock);

	memset(sum, 0, sizeof(*sum));
	rcu_read_lock();
	blkg_for_each_descendant_pre(pos_blkg, pos_css, blkg) {
		struct blkg_rwstat *rwstat;

		if (!pos_blkg->online)
			continue;

		if (pol)
			rwstat = (void *)blkg_to_pd(pos_blkg, pol) + off;
		else
			rwstat = (void *)pos_blkg + off;

		for (i = 0; i < BLKG_RWSTAT_NR; i++)
			sum->cnt[i] += blkg_rwstat_read_counter(rwstat, i);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(blkg_rwstat_recursive_sum);
