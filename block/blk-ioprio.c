// SPDX-License-Identifier: GPL-2.0
/*
 * Block rq-qos policy for assigning an I/O priority class to requests.
 *
 * Using an rq-qos policy for assigning I/O priority class has two advantages
 * over using the ioprio_set() system call:
 *
 * - This policy is cgroup based so it has all the advantages of cgroups.
 * - While ioprio_set() does not affect page cache writeback I/O, this rq-qos
 *   controller affects page cache writeback I/O for filesystems that support
 *   assiociating a cgroup with writeback I/O. See also
 *   Documentation/admin-guide/cgroup-v2.rst.
 */

#include <linux/blk-cgroup.h>
#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "blk-ioprio.h"
#include "blk-rq-qos.h"

/**
 * enum prio_policy - I/O priority class policy.
 * @POLICY_NO_CHANGE: (default) do not modify the I/O priority class.
 * @POLICY_NONE_TO_RT: modify IOPRIO_CLASS_NONE into IOPRIO_CLASS_RT.
 * @POLICY_RESTRICT_TO_BE: modify IOPRIO_CLASS_NONE and IOPRIO_CLASS_RT into
 *		IOPRIO_CLASS_BE.
 * @POLICY_ALL_TO_IDLE: change the I/O priority class into IOPRIO_CLASS_IDLE.
 *
 * See also <linux/ioprio.h>.
 */
enum prio_policy {
	POLICY_NO_CHANGE	= 0,
	POLICY_NONE_TO_RT	= 1,
	POLICY_RESTRICT_TO_BE	= 2,
	POLICY_ALL_TO_IDLE	= 3,
};

static const char *policy_name[] = {
	[POLICY_NO_CHANGE]	= "no-change",
	[POLICY_NONE_TO_RT]	= "none-to-rt",
	[POLICY_RESTRICT_TO_BE]	= "restrict-to-be",
	[POLICY_ALL_TO_IDLE]	= "idle",
};

static struct blkcg_policy ioprio_policy;

/**
 * struct ioprio_blkg - Per (cgroup, request queue) data.
 * @pd: blkg_policy_data structure.
 */
struct ioprio_blkg {
	struct blkg_policy_data pd;
};

/**
 * struct ioprio_blkcg - Per cgroup data.
 * @cpd: blkcg_policy_data structure.
 * @prio_policy: One of the IOPRIO_CLASS_* values. See also <linux/ioprio.h>.
 */
struct ioprio_blkcg {
	struct blkcg_policy_data cpd;
	enum prio_policy	 prio_policy;
	bool			 prio_set;
};

static inline struct ioprio_blkg *pd_to_ioprio(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct ioprio_blkg, pd) : NULL;
}

static struct ioprio_blkcg *blkcg_to_ioprio_blkcg(struct blkcg *blkcg)
{
	return container_of(blkcg_to_cpd(blkcg, &ioprio_policy),
			    struct ioprio_blkcg, cpd);
}

static struct ioprio_blkcg *
ioprio_blkcg_from_css(struct cgroup_subsys_state *css)
{
	return blkcg_to_ioprio_blkcg(css_to_blkcg(css));
}

static struct ioprio_blkcg *ioprio_blkcg_from_bio(struct bio *bio)
{
	struct blkg_policy_data *pd = blkg_to_pd(bio->bi_blkg, &ioprio_policy);

	if (!pd)
		return NULL;

	return blkcg_to_ioprio_blkcg(pd->blkg->blkcg);
}

static int ioprio_show_prio_policy(struct seq_file *sf, void *v)
{
	struct ioprio_blkcg *blkcg = ioprio_blkcg_from_css(seq_css(sf));

	seq_printf(sf, "%s\n", policy_name[blkcg->prio_policy]);
	return 0;
}

static ssize_t ioprio_set_prio_policy(struct kernfs_open_file *of, char *buf,
				      size_t nbytes, loff_t off)
{
	struct ioprio_blkcg *blkcg = ioprio_blkcg_from_css(of_css(of));
	int ret;

	if (off != 0)
		return -EIO;
	/* kernfs_fop_write_iter() terminates 'buf' with '\0'. */
	ret = sysfs_match_string(policy_name, buf);
	if (ret < 0)
		return ret;
	blkcg->prio_policy = ret;
	blkcg->prio_set = true;
	return nbytes;
}

static struct blkg_policy_data *
ioprio_alloc_pd(gfp_t gfp, struct request_queue *q, struct blkcg *blkcg)
{
	struct ioprio_blkg *ioprio_blkg;

	ioprio_blkg = kzalloc(sizeof(*ioprio_blkg), gfp);
	if (!ioprio_blkg)
		return NULL;

	return &ioprio_blkg->pd;
}

static void ioprio_free_pd(struct blkg_policy_data *pd)
{
	struct ioprio_blkg *ioprio_blkg = pd_to_ioprio(pd);

	kfree(ioprio_blkg);
}

static struct blkcg_policy_data *ioprio_alloc_cpd(gfp_t gfp)
{
	struct ioprio_blkcg *blkcg;

	blkcg = kzalloc(sizeof(*blkcg), gfp);
	if (!blkcg)
		return NULL;
	blkcg->prio_policy = POLICY_NO_CHANGE;
	return &blkcg->cpd;
}

static void ioprio_free_cpd(struct blkcg_policy_data *cpd)
{
	struct ioprio_blkcg *blkcg = container_of(cpd, typeof(*blkcg), cpd);

	kfree(blkcg);
}

#define IOPRIO_ATTRS						\
	{							\
		.name		= "prio.class",			\
		.seq_show	= ioprio_show_prio_policy,	\
		.write		= ioprio_set_prio_policy,	\
	},							\
	{ } /* sentinel */

/* cgroup v2 attributes */
static struct cftype ioprio_files[] = {
	IOPRIO_ATTRS
};

/* cgroup v1 attributes */
static struct cftype ioprio_legacy_files[] = {
	IOPRIO_ATTRS
};

static struct blkcg_policy ioprio_policy = {
	.dfl_cftypes	= ioprio_files,
	.legacy_cftypes = ioprio_legacy_files,

	.cpd_alloc_fn	= ioprio_alloc_cpd,
	.cpd_free_fn	= ioprio_free_cpd,

	.pd_alloc_fn	= ioprio_alloc_pd,
	.pd_free_fn	= ioprio_free_pd,
};

struct blk_ioprio {
	struct rq_qos rqos;
};

static void blkcg_ioprio_track(struct rq_qos *rqos, struct request *rq,
			       struct bio *bio)
{
	struct ioprio_blkcg *blkcg = ioprio_blkcg_from_bio(bio);
	u16 prio;

	if (!blkcg->prio_set)
		return;

	/*
	 * Except for IOPRIO_CLASS_NONE, higher I/O priority numbers
	 * correspond to a lower priority. Hence, the max_t() below selects
	 * the lower priority of bi_ioprio and the cgroup I/O priority class.
	 * If the cgroup policy has been set to POLICY_NO_CHANGE == 0, the
	 * bio I/O priority is not modified. If the bio I/O priority equals
	 * IOPRIO_CLASS_NONE, the cgroup I/O priority is assigned to the bio.
	 */
	prio = max_t(u16, bio->bi_ioprio,
			IOPRIO_PRIO_VALUE(blkcg->prio_policy, 0));
	if (prio > bio->bi_ioprio)
		bio->bi_ioprio = prio;
}

static void blkcg_ioprio_exit(struct rq_qos *rqos)
{
	struct blk_ioprio *blkioprio_blkg =
		container_of(rqos, typeof(*blkioprio_blkg), rqos);

	blkcg_deactivate_policy(rqos->q, &ioprio_policy);
	kfree(blkioprio_blkg);
}

static struct rq_qos_ops blkcg_ioprio_ops = {
	.track	= blkcg_ioprio_track,
	.exit	= blkcg_ioprio_exit,
};

int blk_ioprio_init(struct request_queue *q)
{
	struct blk_ioprio *blkioprio_blkg;
	struct rq_qos *rqos;
	int ret;

	blkioprio_blkg = kzalloc(sizeof(*blkioprio_blkg), GFP_KERNEL);
	if (!blkioprio_blkg)
		return -ENOMEM;

	ret = blkcg_activate_policy(q, &ioprio_policy);
	if (ret) {
		kfree(blkioprio_blkg);
		return ret;
	}

	rqos = &blkioprio_blkg->rqos;
	rqos->id = RQ_QOS_IOPRIO;
	rqos->ops = &blkcg_ioprio_ops;
	rqos->q = q;

	/*
	 * Registering the rq-qos policy after activating the blk-cgroup
	 * policy guarantees that ioprio_blkcg_from_bio(bio) != NULL in the
	 * rq-qos callbacks.
	 */
	rq_qos_add(q, rqos);

	return 0;
}

static int __init ioprio_init(void)
{
	return blkcg_policy_register(&ioprio_policy);
}

static void __exit ioprio_exit(void)
{
	blkcg_policy_unregister(&ioprio_policy);
}

module_init(ioprio_init);
module_exit(ioprio_exit);
