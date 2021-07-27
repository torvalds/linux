// SPDX-License-Identifier: GPL-2.0

#include <linux/blk-cgroup.h>
#include <linux/ioprio.h>

#include "mq-deadline-cgroup.h"

static struct blkcg_policy dd_blkcg_policy;

static struct blkcg_policy_data *dd_cpd_alloc(gfp_t gfp)
{
	struct dd_blkcg *pd;

	pd = kzalloc(sizeof(*pd), gfp);
	if (!pd)
		return NULL;
	pd->stats = alloc_percpu_gfp(typeof(*pd->stats),
				     GFP_KERNEL | __GFP_ZERO);
	if (!pd->stats) {
		kfree(pd);
		return NULL;
	}
	return &pd->cpd;
}

static void dd_cpd_free(struct blkcg_policy_data *cpd)
{
	struct dd_blkcg *dd_blkcg = container_of(cpd, typeof(*dd_blkcg), cpd);

	free_percpu(dd_blkcg->stats);
	kfree(dd_blkcg);
}

static struct dd_blkcg *dd_blkcg_from_pd(struct blkg_policy_data *pd)
{
	return container_of(blkcg_to_cpd(pd->blkg->blkcg, &dd_blkcg_policy),
			    struct dd_blkcg, cpd);
}

/*
 * Convert an association between a block cgroup and a request queue into a
 * pointer to the mq-deadline information associated with a (blkcg, queue) pair.
 */
struct dd_blkcg *dd_blkcg_from_bio(struct bio *bio)
{
	struct blkg_policy_data *pd;

	pd = blkg_to_pd(bio->bi_blkg, &dd_blkcg_policy);
	if (!pd)
		return NULL;

	return dd_blkcg_from_pd(pd);
}

static size_t dd_pd_stat(struct blkg_policy_data *pd, char *buf, size_t size)
{
	static const char *const prio_class_name[] = {
		[IOPRIO_CLASS_NONE]	= "NONE",
		[IOPRIO_CLASS_RT]	= "RT",
		[IOPRIO_CLASS_BE]	= "BE",
		[IOPRIO_CLASS_IDLE]	= "IDLE",
	};
	struct dd_blkcg *blkcg = dd_blkcg_from_pd(pd);
	int res = 0;
	u8 prio;

	for (prio = 0; prio < ARRAY_SIZE(blkcg->stats->stats); prio++)
		res += scnprintf(buf + res, size - res,
			" [%s] dispatched=%u inserted=%u merged=%u",
			prio_class_name[prio],
			ddcg_sum(blkcg, dispatched, prio) +
			ddcg_sum(blkcg, merged, prio) -
			ddcg_sum(blkcg, completed, prio),
			ddcg_sum(blkcg, inserted, prio) -
			ddcg_sum(blkcg, completed, prio),
			ddcg_sum(blkcg, merged, prio));

	return res;
}

static struct blkg_policy_data *dd_pd_alloc(gfp_t gfp, struct request_queue *q,
					    struct blkcg *blkcg)
{
	struct dd_blkg *pd;

	pd = kzalloc(sizeof(*pd), gfp);
	if (!pd)
		return NULL;
	return &pd->pd;
}

static void dd_pd_free(struct blkg_policy_data *pd)
{
	struct dd_blkg *dd_blkg = container_of(pd, typeof(*dd_blkg), pd);

	kfree(dd_blkg);
}

static struct blkcg_policy dd_blkcg_policy = {
	.cpd_alloc_fn		= dd_cpd_alloc,
	.cpd_free_fn		= dd_cpd_free,

	.pd_alloc_fn		= dd_pd_alloc,
	.pd_free_fn		= dd_pd_free,
	.pd_stat_fn		= dd_pd_stat,
};

int dd_activate_policy(struct request_queue *q)
{
	return blkcg_activate_policy(q, &dd_blkcg_policy);
}

void dd_deactivate_policy(struct request_queue *q)
{
	blkcg_deactivate_policy(q, &dd_blkcg_policy);
}

int __init dd_blkcg_init(void)
{
	return blkcg_policy_register(&dd_blkcg_policy);
}

void __exit dd_blkcg_exit(void)
{
	blkcg_policy_unregister(&dd_blkcg_policy);
}
