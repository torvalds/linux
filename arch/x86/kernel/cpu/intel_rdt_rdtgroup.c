/*
 * User interface for Resource Alloction in Resource Director Technology(RDT)
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Fenghua Yu <fenghua.yu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/kernfs.h>
#include <linux/slab.h>

#include <uapi/linux/magic.h>

#include <asm/intel_rdt.h>

DEFINE_STATIC_KEY_FALSE(rdt_enable_key);
struct kernfs_root *rdt_root;
struct rdtgroup rdtgroup_default;
LIST_HEAD(rdt_all_groups);

static void l3_qos_cfg_update(void *arg)
{
	bool *enable = arg;

	wrmsrl(IA32_L3_QOS_CFG, *enable ? L3_QOS_CDP_ENABLE : 0ULL);
}

static int set_l3_qos_cfg(struct rdt_resource *r, bool enable)
{
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int cpu;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	list_for_each_entry(d, &r->domains, list) {
		/* Pick one CPU from each domain instance to update MSR */
		cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);
	}
	cpu = get_cpu();
	/* Update QOS_CFG MSR on this cpu if it's in cpu_mask. */
	if (cpumask_test_cpu(cpu, cpu_mask))
		l3_qos_cfg_update(&enable);
	/* Update QOS_CFG MSR on all other cpus in cpu_mask. */
	smp_call_function_many(cpu_mask, l3_qos_cfg_update, &enable, 1);
	put_cpu();

	free_cpumask_var(cpu_mask);

	return 0;
}

static int cdp_enable(void)
{
	struct rdt_resource *r_l3data = &rdt_resources_all[RDT_RESOURCE_L3DATA];
	struct rdt_resource *r_l3code = &rdt_resources_all[RDT_RESOURCE_L3CODE];
	struct rdt_resource *r_l3 = &rdt_resources_all[RDT_RESOURCE_L3];
	int ret;

	if (!r_l3->capable || !r_l3data->capable || !r_l3code->capable)
		return -EINVAL;

	ret = set_l3_qos_cfg(r_l3, true);
	if (!ret) {
		r_l3->enabled = false;
		r_l3data->enabled = true;
		r_l3code->enabled = true;
	}
	return ret;
}

static void cdp_disable(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3];

	r->enabled = r->capable;

	if (rdt_resources_all[RDT_RESOURCE_L3DATA].enabled) {
		rdt_resources_all[RDT_RESOURCE_L3DATA].enabled = false;
		rdt_resources_all[RDT_RESOURCE_L3CODE].enabled = false;
		set_l3_qos_cfg(r, false);
	}
}

static int parse_rdtgroupfs_options(char *data)
{
	char *token, *o = data;
	int ret = 0;

	while ((token = strsep(&o, ",")) != NULL) {
		if (!*token)
			return -EINVAL;

		if (!strcmp(token, "cdp"))
			ret = cdp_enable();
	}

	return ret;
}

static struct dentry *rdt_mount(struct file_system_type *fs_type,
				int flags, const char *unused_dev_name,
				void *data)
{
	struct dentry *dentry;
	int ret;

	mutex_lock(&rdtgroup_mutex);
	/*
	 * resctrl file system can only be mounted once.
	 */
	if (static_branch_unlikely(&rdt_enable_key)) {
		dentry = ERR_PTR(-EBUSY);
		goto out;
	}

	ret = parse_rdtgroupfs_options(data);
	if (ret) {
		dentry = ERR_PTR(ret);
		goto out_cdp;
	}

	dentry = kernfs_mount(fs_type, flags, rdt_root,
			      RDTGROUP_SUPER_MAGIC, NULL);
	if (IS_ERR(dentry))
		goto out_cdp;

	static_branch_enable(&rdt_enable_key);
	goto out;

out_cdp:
	cdp_disable();
out:
	mutex_unlock(&rdtgroup_mutex);

	return dentry;
}

static int reset_all_cbms(struct rdt_resource *r)
{
	struct msr_param msr_param;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int i, cpu;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	msr_param.res = r;
	msr_param.low = 0;
	msr_param.high = r->num_closid;

	/*
	 * Disable resource control for this resource by setting all
	 * CBMs in all domains to the maximum mask value. Pick one CPU
	 * from each domain to update the MSRs below.
	 */
	list_for_each_entry(d, &r->domains, list) {
		cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);

		for (i = 0; i < r->num_closid; i++)
			d->cbm[i] = r->max_cbm;
	}
	cpu = get_cpu();
	/* Update CBM on this cpu if it's in cpu_mask. */
	if (cpumask_test_cpu(cpu, cpu_mask))
		rdt_cbm_update(&msr_param);
	/* Update CBM on all other cpus in cpu_mask. */
	smp_call_function_many(cpu_mask, rdt_cbm_update, &msr_param, 1);
	put_cpu();

	free_cpumask_var(cpu_mask);

	return 0;
}

static void rdt_kill_sb(struct super_block *sb)
{
	struct rdt_resource *r;

	mutex_lock(&rdtgroup_mutex);

	/*Put everything back to default values. */
	for_each_enabled_rdt_resource(r)
		reset_all_cbms(r);
	cdp_disable();
	static_branch_disable(&rdt_enable_key);
	kernfs_kill_sb(sb);
	mutex_unlock(&rdtgroup_mutex);
}

static struct file_system_type rdt_fs_type = {
	.name    = "resctrl",
	.mount   = rdt_mount,
	.kill_sb = rdt_kill_sb,
};

static struct kernfs_syscall_ops rdtgroup_kf_syscall_ops = {
};

static int __init rdtgroup_setup_root(void)
{
	rdt_root = kernfs_create_root(&rdtgroup_kf_syscall_ops,
				      KERNFS_ROOT_CREATE_DEACTIVATED,
				      &rdtgroup_default);
	if (IS_ERR(rdt_root))
		return PTR_ERR(rdt_root);

	mutex_lock(&rdtgroup_mutex);

	rdtgroup_default.closid = 0;
	list_add(&rdtgroup_default.rdtgroup_list, &rdt_all_groups);

	rdtgroup_default.kn = rdt_root->kn;
	kernfs_activate(rdtgroup_default.kn);

	mutex_unlock(&rdtgroup_mutex);

	return 0;
}

/*
 * rdtgroup_init - rdtgroup initialization
 *
 * Setup resctrl file system including set up root, create mount point,
 * register rdtgroup filesystem, and initialize files under root directory.
 *
 * Return: 0 on success or -errno
 */
int __init rdtgroup_init(void)
{
	int ret = 0;

	ret = rdtgroup_setup_root();
	if (ret)
		return ret;

	ret = sysfs_create_mount_point(fs_kobj, "resctrl");
	if (ret)
		goto cleanup_root;

	ret = register_filesystem(&rdt_fs_type);
	if (ret)
		goto cleanup_mountpoint;

	return 0;

cleanup_mountpoint:
	sysfs_remove_mount_point(fs_kobj, "resctrl");
cleanup_root:
	kernfs_destroy_root(rdt_root);

	return ret;
}
