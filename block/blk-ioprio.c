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

#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "blk-cgroup.h"
#include "blk-ioprio.h"
#include "blk-rq-qos.h"

/**
 * enum prio_policy - I/O priority class policy.
 * @POLICY_NO_CHANGE: (default) do not modify the I/O priority class.
 * @POLICY_PROMOTE_TO_RT: modify no-IOPRIO_CLASS_RT to IOPRIO_CLASS_RT.
 * @POLICY_RESTRICT_TO_BE: modify IOPRIO_CLASS_NONE and IOPRIO_CLASS_RT into
 *		IOPRIO_CLASS_BE.
 * @POLICY_ALL_TO_IDLE: change the I/O priority class into IOPRIO_CLASS_IDLE.
 * @POLICY_NONE_TO_RT: an alias for POLICY_PROMOTE_TO_RT.
 *
 * See also <linux/ioprio.h>.
 */
enum prio_policy {
	POLICY_NO_CHANGE	= 0,
	POLICY_PROMOTE_TO_RT	= 1,
	POLICY_RESTRICT_TO_BE	= 2,
	POLICY_ALL_TO_IDLE	= 3,
	POLICY_NONE_TO_RT	= 4,
};

static const char *policy_name[] = {
	[POLICY_NO_CHANGE]	= "no-change",
	[POLICY_PROMOTE_TO_RT]	= "promote-to-rt",
	[POLICY_RESTRICT_TO_BE]	= "restrict-to-be",
	[POLICY_ALL_TO_IDLE]	= "idle",
	[POLICY_NONE_TO_RT]	= "none-to-rt",
};

static struct blkcg_policy ioprio_policy;

/**
 * struct ioprio_blkcg - Per cgroup data.
 * @cpd: blkcg_policy_data structure.
 * @prio_policy: One of the IOPRIO_CLASS_* values. See also <linux/ioprio.h>.
 */
struct ioprio_blkcg {
	struct blkcg_policy_data cpd;
	enum prio_policy	 prio_policy;
};

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
	return nbytes;
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

static struct cftype ioprio_files[] = {
	{
		.name		= "prio.class",
		.seq_show	= ioprio_show_prio_policy,
		.write		= ioprio_set_prio_policy,
	},
	{ } /* sentinel */
};

static struct blkcg_policy ioprio_policy = {
	.dfl_cftypes	= ioprio_files,
	.legacy_cftypes = ioprio_files,

	.cpd_alloc_fn	= ioprio_alloc_cpd,
	.cpd_free_fn	= ioprio_free_cpd,
};

void blkcg_set_ioprio(struct bio *bio)
{
	struct ioprio_blkcg *blkcg = blkcg_to_ioprio_blkcg(bio->bi_blkg->blkcg);
	u16 prio;

	if (!blkcg || blkcg->prio_policy == POLICY_NO_CHANGE)
		return;

	if (blkcg->prio_policy == POLICY_PROMOTE_TO_RT ||
	    blkcg->prio_policy == POLICY_NONE_TO_RT) {
		/*
		 * For RT threads, the default priority level is 4 because
		 * task_nice is 0. By promoting non-RT io-priority to RT-class
		 * and default level 4, those requests that are already
		 * RT-class but need a higher io-priority can use ioprio_set()
		 * to achieve this.
		 */
		if (IOPRIO_PRIO_CLASS(bio->bi_ioprio) != IOPRIO_CLASS_RT)
			bio->bi_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 4);
		return;
	}

	/*
	 * Except for IOPRIO_CLASS_NONE, higher I/O priority numbers
	 * correspond to a lower priority. Hence, the max_t() below selects
	 * the lower priority of bi_ioprio and the cgroup I/O priority class.
	 * If the bio I/O priority equals IOPRIO_CLASS_NONE, the cgroup I/O
	 * priority is assigned to the bio.
	 */
	prio = max_t(u16, bio->bi_ioprio,
			IOPRIO_PRIO_VALUE(blkcg->prio_policy, 0));
	if (prio > bio->bi_ioprio)
		bio->bi_ioprio = prio;
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
