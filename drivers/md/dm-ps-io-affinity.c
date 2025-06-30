// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oracle Corporation
 *
 * Module Author: Mike Christie
 */
#include "dm-path-selector.h"

#include <linux/device-mapper.h>
#include <linux/module.h>

#define DM_MSG_PREFIX "multipath io-affinity"

struct path_info {
	struct dm_path *path;
	cpumask_var_t cpumask;
	refcount_t refcount;
	bool failed;
};

struct selector {
	struct path_info **path_map;
	cpumask_var_t path_mask;
	atomic_t map_misses;
};

static void ioa_free_path(struct selector *s, unsigned int cpu)
{
	struct path_info *pi = s->path_map[cpu];

	if (!pi)
		return;

	if (refcount_dec_and_test(&pi->refcount)) {
		cpumask_clear_cpu(cpu, s->path_mask);
		free_cpumask_var(pi->cpumask);
		kfree(pi);

		s->path_map[cpu] = NULL;
	}
}

static int ioa_add_path(struct path_selector *ps, struct dm_path *path,
			int argc, char **argv, char **error)
{
	struct selector *s = ps->context;
	struct path_info *pi = NULL;
	unsigned int cpu;
	int ret;

	if (argc != 1) {
		*error = "io-affinity ps: invalid number of arguments";
		return -EINVAL;
	}

	pi = kzalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "io-affinity ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	path->pscontext = pi;
	refcount_set(&pi->refcount, 1);

	if (!zalloc_cpumask_var(&pi->cpumask, GFP_KERNEL)) {
		*error = "io-affinity ps: Error allocating cpumask context";
		ret = -ENOMEM;
		goto free_pi;
	}

	ret = cpumask_parse(argv[0], pi->cpumask);
	if (ret) {
		*error = "io-affinity ps: invalid cpumask";
		ret = -EINVAL;
		goto free_mask;
	}

	for_each_cpu(cpu, pi->cpumask) {
		if (cpu >= nr_cpu_ids) {
			DMWARN_LIMIT("Ignoring mapping for CPU %u. Max CPU is %u",
				     cpu, nr_cpu_ids);
			break;
		}

		if (s->path_map[cpu]) {
			DMWARN("CPU mapping for %u exists. Ignoring.", cpu);
			continue;
		}

		cpumask_set_cpu(cpu, s->path_mask);
		s->path_map[cpu] = pi;
		refcount_inc(&pi->refcount);
	}

	if (refcount_dec_and_test(&pi->refcount)) {
		*error = "io-affinity ps: No new/valid CPU mapping found";
		ret = -EINVAL;
		goto free_mask;
	}

	return 0;

free_mask:
	free_cpumask_var(pi->cpumask);
free_pi:
	kfree(pi);
	return ret;
}

static int ioa_create(struct path_selector *ps, unsigned int argc, char **argv)
{
	struct selector *s;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->path_map = kcalloc(nr_cpu_ids, sizeof(struct path_info *),
			      GFP_KERNEL);
	if (!s->path_map)
		goto free_selector;

	if (!zalloc_cpumask_var(&s->path_mask, GFP_KERNEL))
		goto free_map;

	atomic_set(&s->map_misses, 0);
	ps->context = s;
	return 0;

free_map:
	kfree(s->path_map);
free_selector:
	kfree(s);
	return -ENOMEM;
}

static void ioa_destroy(struct path_selector *ps)
{
	struct selector *s = ps->context;
	unsigned int cpu;

	for_each_cpu(cpu, s->path_mask)
		ioa_free_path(s, cpu);

	free_cpumask_var(s->path_mask);
	kfree(s->path_map);
	kfree(s);

	ps->context = NULL;
}

static int ioa_status(struct path_selector *ps, struct dm_path *path,
		      status_type_t type, char *result, unsigned int maxlen)
{
	struct selector *s = ps->context;
	struct path_info *pi;
	int sz = 0;

	if (!path) {
		DMEMIT("0 ");
		return sz;
	}

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", atomic_read(&s->map_misses));
		break;
	case STATUSTYPE_TABLE:
		pi = path->pscontext;
		DMEMIT("%*pb ", cpumask_pr_args(pi->cpumask));
		break;
	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}

	return sz;
}

static void ioa_fail_path(struct path_selector *ps, struct dm_path *p)
{
	struct path_info *pi = p->pscontext;

	pi->failed = true;
}

static int ioa_reinstate_path(struct path_selector *ps, struct dm_path *p)
{
	struct path_info *pi = p->pscontext;

	pi->failed = false;
	return 0;
}

static struct dm_path *ioa_select_path(struct path_selector *ps,
				       size_t nr_bytes)
{
	unsigned int cpu, node;
	struct selector *s = ps->context;
	const struct cpumask *cpumask;
	struct path_info *pi;
	int i;

	cpu = get_cpu();

	pi = s->path_map[cpu];
	if (pi && !pi->failed)
		goto done;

	/*
	 * Perf is not optimal, but we at least try the local node then just
	 * try not to fail.
	 */
	if (!pi)
		atomic_inc(&s->map_misses);

	node = cpu_to_node(cpu);
	cpumask = cpumask_of_node(node);
	for_each_cpu(i, cpumask) {
		pi = s->path_map[i];
		if (pi && !pi->failed)
			goto done;
	}

	for_each_cpu(i, s->path_mask) {
		pi = s->path_map[i];
		if (pi && !pi->failed)
			goto done;
	}
	pi = NULL;

done:
	put_cpu();
	return pi ? pi->path : NULL;
}

static struct path_selector_type ioa_ps = {
	.name		= "io-affinity",
	.module		= THIS_MODULE,
	.table_args	= 1,
	.info_args	= 1,
	.create		= ioa_create,
	.destroy	= ioa_destroy,
	.status		= ioa_status,
	.add_path	= ioa_add_path,
	.fail_path	= ioa_fail_path,
	.reinstate_path	= ioa_reinstate_path,
	.select_path	= ioa_select_path,
};

static int __init dm_ioa_init(void)
{
	int ret = dm_register_path_selector(&ioa_ps);

	if (ret < 0)
		DMERR("register failed %d", ret);
	return ret;
}

static void __exit dm_ioa_exit(void)
{
	dm_unregister_path_selector(&ioa_ps);
}

module_init(dm_ioa_init);
module_exit(dm_ioa_exit);

MODULE_DESCRIPTION(DM_NAME " multipath path selector that selects paths based on the CPU IO is being executed on");
MODULE_AUTHOR("Mike Christie <michael.christie@oracle.com>");
MODULE_LICENSE("GPL");
