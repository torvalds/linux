// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Microsoft Corporation.
 *
 * The /sys/kernel/debug/mshv directory contents.
 * Contains various statistics data, provided by the hypervisor.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/debugfs.h>
#include <linux/stringify.h>
#include <asm/mshyperv.h>
#include <linux/slab.h>

#include "mshv.h"
#include "mshv_root.h"

/* Ensure this file is not used elsewhere by accident */
#define MSHV_DEBUGFS_C
#include "mshv_debugfs_counters.c"

#define U32_BUF_SZ 11
#define U64_BUF_SZ 21
/* Only support SELF and PARENT areas */
#define NUM_STATS_AREAS 2
static_assert(HV_STATS_AREA_SELF == 0 && HV_STATS_AREA_PARENT == 1,
	      "SELF and PARENT areas must be usable as indices into an array of size NUM_STATS_AREAS");
/* HV_HYPERVISOR_COUNTER */
#define HV_HYPERVISOR_COUNTER_LOGICAL_PROCESSORS 1

static struct dentry *mshv_debugfs;
static struct dentry *mshv_debugfs_partition;
static struct dentry *mshv_debugfs_lp;
static struct dentry **parent_vp_stats;
static struct dentry *parent_partition_stats;

static u64 mshv_lps_count;
static struct hv_stats_page **mshv_lps_stats;

static int lp_stats_show(struct seq_file *m, void *v)
{
	const struct hv_stats_page *stats = m->private;
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(hv_lp_counters); idx++) {
		char *name = hv_lp_counters[idx];

		if (!name)
			continue;
		seq_printf(m, "%-32s: %llu\n", name, stats->data[idx]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(lp_stats);

static void mshv_lp_stats_unmap(u32 lp_index)
{
	union hv_stats_object_identity identity = {
		.lp.lp_index = lp_index,
		.lp.stats_area_type = HV_STATS_AREA_SELF,
	};
	int err;

	err = hv_unmap_stats_page(HV_STATS_OBJECT_LOGICAL_PROCESSOR,
				  mshv_lps_stats[lp_index], &identity);
	if (err)
		pr_err("%s: failed to unmap logical processor %u stats, err: %d\n",
		       __func__, lp_index, err);

	mshv_lps_stats[lp_index] = NULL;
}

static struct hv_stats_page * __init mshv_lp_stats_map(u32 lp_index)
{
	union hv_stats_object_identity identity = {
		.lp.lp_index = lp_index,
		.lp.stats_area_type = HV_STATS_AREA_SELF,
	};
	struct hv_stats_page *stats;
	int err;

	err = hv_map_stats_page(HV_STATS_OBJECT_LOGICAL_PROCESSOR, &identity,
				&stats);
	if (err) {
		pr_err("%s: failed to map logical processor %u stats, err: %d\n",
		       __func__, lp_index, err);
		return ERR_PTR(err);
	}
	mshv_lps_stats[lp_index] = stats;

	return stats;
}

static struct hv_stats_page * __init lp_debugfs_stats_create(u32 lp_index,
							     struct dentry *parent)
{
	struct dentry *dentry;
	struct hv_stats_page *stats;

	stats = mshv_lp_stats_map(lp_index);
	if (IS_ERR(stats))
		return stats;

	dentry = debugfs_create_file("stats", 0400, parent,
				     stats, &lp_stats_fops);
	if (IS_ERR(dentry)) {
		mshv_lp_stats_unmap(lp_index);
		return ERR_CAST(dentry);
	}
	return stats;
}

static int __init lp_debugfs_create(u32 lp_index, struct dentry *parent)
{
	struct dentry *idx;
	char lp_idx_str[U32_BUF_SZ];
	struct hv_stats_page *stats;
	int err;

	sprintf(lp_idx_str, "%u", lp_index);

	idx = debugfs_create_dir(lp_idx_str, parent);
	if (IS_ERR(idx))
		return PTR_ERR(idx);

	stats = lp_debugfs_stats_create(lp_index, idx);
	if (IS_ERR(stats)) {
		err = PTR_ERR(stats);
		goto remove_debugfs_lp_idx;
	}

	return 0;

remove_debugfs_lp_idx:
	debugfs_remove_recursive(idx);
	return err;
}

static void mshv_debugfs_lp_remove(void)
{
	int lp_index;

	debugfs_remove_recursive(mshv_debugfs_lp);

	for (lp_index = 0; lp_index < mshv_lps_count; lp_index++)
		mshv_lp_stats_unmap(lp_index);

	kfree(mshv_lps_stats);
	mshv_lps_stats = NULL;
}

static int __init mshv_debugfs_lp_create(struct dentry *parent)
{
	struct dentry *lp_dir;
	int err, lp_index;

	mshv_lps_stats = kzalloc_objs(*mshv_lps_stats, mshv_lps_count,
				      GFP_KERNEL_ACCOUNT);

	if (!mshv_lps_stats)
		return -ENOMEM;

	lp_dir = debugfs_create_dir("lp", parent);
	if (IS_ERR(lp_dir)) {
		err = PTR_ERR(lp_dir);
		goto free_lp_stats;
	}

	for (lp_index = 0; lp_index < mshv_lps_count; lp_index++) {
		err = lp_debugfs_create(lp_index, lp_dir);
		if (err)
			goto remove_debugfs_lps;
	}

	mshv_debugfs_lp = lp_dir;

	return 0;

remove_debugfs_lps:
	for (lp_index -= 1; lp_index >= 0; lp_index--)
		mshv_lp_stats_unmap(lp_index);
	debugfs_remove_recursive(lp_dir);
free_lp_stats:
	kfree(mshv_lps_stats);
	mshv_lps_stats = NULL;

	return err;
}

static int vp_stats_show(struct seq_file *m, void *v)
{
	const struct hv_stats_page **pstats = m->private;
	u64 parent_val, self_val;
	int idx;

	/*
	 * For VP and partition stats, there may be two stats areas mapped,
	 * SELF and PARENT. These refer to the privilege level of the data in
	 * each page. Some fields may be 0 in SELF and nonzero in PARENT, or
	 * vice versa.
	 *
	 * Hence, prioritize printing from the PARENT page (more privileged
	 * data), but use the value from the SELF page if the PARENT value is
	 * 0.
	 */

	for (idx = 0; idx < ARRAY_SIZE(hv_vp_counters); idx++) {
		char *name = hv_vp_counters[idx];

		if (!name)
			continue;

		parent_val = pstats[HV_STATS_AREA_PARENT]->data[idx];
		self_val = pstats[HV_STATS_AREA_SELF]->data[idx];
		seq_printf(m, "%-43s: %llu\n", name,
			   parent_val ? parent_val : self_val);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vp_stats);

static void vp_debugfs_remove(struct dentry *vp_stats)
{
	debugfs_remove_recursive(vp_stats->d_parent);
}

static int vp_debugfs_create(u64 partition_id, u32 vp_index,
			     struct hv_stats_page **pstats,
			     struct dentry **vp_stats_ptr,
			     struct dentry *parent)
{
	struct dentry *vp_idx_dir, *d;
	char vp_idx_str[U32_BUF_SZ];
	int err;

	sprintf(vp_idx_str, "%u", vp_index);

	vp_idx_dir = debugfs_create_dir(vp_idx_str, parent);
	if (IS_ERR(vp_idx_dir))
		return PTR_ERR(vp_idx_dir);

	d = debugfs_create_file("stats", 0400, vp_idx_dir,
				pstats, &vp_stats_fops);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto remove_debugfs_vp_idx;
	}

	*vp_stats_ptr = d;

	return 0;

remove_debugfs_vp_idx:
	debugfs_remove_recursive(vp_idx_dir);
	return err;
}

static int partition_stats_show(struct seq_file *m, void *v)
{
	const struct hv_stats_page **pstats = m->private;
	u64 parent_val, self_val;
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(hv_partition_counters); idx++) {
		char *name = hv_partition_counters[idx];

		if (!name)
			continue;

		parent_val = pstats[HV_STATS_AREA_PARENT]->data[idx];
		self_val = pstats[HV_STATS_AREA_SELF]->data[idx];
		seq_printf(m, "%-37s: %llu\n", name,
			   parent_val ? parent_val : self_val);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(partition_stats);

static void mshv_partition_stats_unmap(u64 partition_id,
				       struct hv_stats_page *stats_page,
				       enum hv_stats_area_type stats_area_type)
{
	union hv_stats_object_identity identity = {
		.partition.partition_id = partition_id,
		.partition.stats_area_type = stats_area_type,
	};
	int err;

	err = hv_unmap_stats_page(HV_STATS_OBJECT_PARTITION, stats_page,
				  &identity);
	if (err)
		pr_err("%s: failed to unmap partition %lld %s stats, err: %d\n",
		       __func__, partition_id,
		       (stats_area_type == HV_STATS_AREA_SELF) ? "self" : "parent",
		       err);
}

static struct hv_stats_page *mshv_partition_stats_map(u64 partition_id,
						      enum hv_stats_area_type stats_area_type)
{
	union hv_stats_object_identity identity = {
		.partition.partition_id = partition_id,
		.partition.stats_area_type = stats_area_type,
	};
	struct hv_stats_page *stats;
	int err;

	err = hv_map_stats_page(HV_STATS_OBJECT_PARTITION, &identity, &stats);
	if (err) {
		pr_err("%s: failed to map partition %lld %s stats, err: %d\n",
		       __func__, partition_id,
		       (stats_area_type == HV_STATS_AREA_SELF) ? "self" : "parent",
		       err);
		return ERR_PTR(err);
	}
	return stats;
}

static int mshv_debugfs_partition_stats_create(u64 partition_id,
					       struct dentry **partition_stats_ptr,
					       struct dentry *parent)
{
	struct dentry *dentry;
	struct hv_stats_page **pstats;
	int err;

	pstats = kzalloc_objs(struct hv_stats_page *, NUM_STATS_AREAS,
			      GFP_KERNEL_ACCOUNT);
	if (!pstats)
		return -ENOMEM;

	pstats[HV_STATS_AREA_SELF] = mshv_partition_stats_map(partition_id,
							      HV_STATS_AREA_SELF);
	if (IS_ERR(pstats[HV_STATS_AREA_SELF])) {
		err = PTR_ERR(pstats[HV_STATS_AREA_SELF]);
		goto cleanup;
	}

	/*
	 * L1VH partition cannot access its partition stats in parent area.
	 */
	if (is_l1vh_parent(partition_id)) {
		pstats[HV_STATS_AREA_PARENT] = pstats[HV_STATS_AREA_SELF];
	} else {
		pstats[HV_STATS_AREA_PARENT] = mshv_partition_stats_map(partition_id,
									HV_STATS_AREA_PARENT);
		if (IS_ERR(pstats[HV_STATS_AREA_PARENT])) {
			err = PTR_ERR(pstats[HV_STATS_AREA_PARENT]);
			goto unmap_self;
		}
		if (!pstats[HV_STATS_AREA_PARENT])
			pstats[HV_STATS_AREA_PARENT] = pstats[HV_STATS_AREA_SELF];
	}

	dentry = debugfs_create_file("stats", 0400, parent,
				     pstats, &partition_stats_fops);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto unmap_partition_stats;
	}

	*partition_stats_ptr = dentry;
	return 0;

unmap_partition_stats:
	if (pstats[HV_STATS_AREA_PARENT] != pstats[HV_STATS_AREA_SELF])
		mshv_partition_stats_unmap(partition_id, pstats[HV_STATS_AREA_PARENT],
					   HV_STATS_AREA_PARENT);
unmap_self:
	mshv_partition_stats_unmap(partition_id, pstats[HV_STATS_AREA_SELF],
				   HV_STATS_AREA_SELF);
cleanup:
	kfree(pstats);
	return err;
}

static void partition_debugfs_remove(u64 partition_id, struct dentry *dentry)
{
	struct hv_stats_page **pstats = NULL;

	pstats = dentry->d_inode->i_private;

	debugfs_remove_recursive(dentry->d_parent);

	if (pstats[HV_STATS_AREA_PARENT] != pstats[HV_STATS_AREA_SELF]) {
		mshv_partition_stats_unmap(partition_id,
					   pstats[HV_STATS_AREA_PARENT],
					   HV_STATS_AREA_PARENT);
	}

	mshv_partition_stats_unmap(partition_id,
				   pstats[HV_STATS_AREA_SELF],
				   HV_STATS_AREA_SELF);

	kfree(pstats);
}

static int partition_debugfs_create(u64 partition_id,
				    struct dentry **vp_dir_ptr,
				    struct dentry **partition_stats_ptr,
				    struct dentry *parent)
{
	char part_id_str[U64_BUF_SZ];
	struct dentry *part_id_dir, *vp_dir;
	int err;

	if (is_l1vh_parent(partition_id))
		sprintf(part_id_str, "self");
	else
		sprintf(part_id_str, "%llu", partition_id);

	part_id_dir = debugfs_create_dir(part_id_str, parent);
	if (IS_ERR(part_id_dir))
		return PTR_ERR(part_id_dir);

	vp_dir = debugfs_create_dir("vp", part_id_dir);
	if (IS_ERR(vp_dir)) {
		err = PTR_ERR(vp_dir);
		goto remove_debugfs_partition_id;
	}

	err = mshv_debugfs_partition_stats_create(partition_id,
						  partition_stats_ptr,
						  part_id_dir);
	if (err)
		goto remove_debugfs_partition_id;

	*vp_dir_ptr = vp_dir;

	return 0;

remove_debugfs_partition_id:
	debugfs_remove_recursive(part_id_dir);
	return err;
}

static void parent_vp_debugfs_remove(u32 vp_index,
				     struct dentry *vp_stats_ptr)
{
	struct hv_stats_page **pstats;

	pstats = vp_stats_ptr->d_inode->i_private;
	vp_debugfs_remove(vp_stats_ptr);
	mshv_vp_stats_unmap(hv_current_partition_id, vp_index, pstats);
	kfree(pstats);
}

static void mshv_debugfs_parent_partition_remove(void)
{
	int idx;

	for_each_online_cpu(idx)
		parent_vp_debugfs_remove(hv_vp_index[idx],
					 parent_vp_stats[idx]);

	partition_debugfs_remove(hv_current_partition_id,
				 parent_partition_stats);
	kfree(parent_vp_stats);
	parent_vp_stats = NULL;
	parent_partition_stats = NULL;
}

static int __init parent_vp_debugfs_create(u32 vp_index,
					   struct dentry **vp_stats_ptr,
					   struct dentry *parent)
{
	struct hv_stats_page **pstats;
	int err;

	pstats = kzalloc_objs(struct hv_stats_page *, NUM_STATS_AREAS,
			      GFP_KERNEL_ACCOUNT);
	if (!pstats)
		return -ENOMEM;

	err = mshv_vp_stats_map(hv_current_partition_id, vp_index, pstats);
	if (err)
		goto cleanup;

	err = vp_debugfs_create(hv_current_partition_id, vp_index, pstats,
				vp_stats_ptr, parent);
	if (err)
		goto unmap_vp_stats;

	return 0;

unmap_vp_stats:
	mshv_vp_stats_unmap(hv_current_partition_id, vp_index, pstats);
cleanup:
	kfree(pstats);
	return err;
}

static int __init mshv_debugfs_parent_partition_create(void)
{
	struct dentry *vp_dir;
	int err, idx, i;

	mshv_debugfs_partition = debugfs_create_dir("partition",
						    mshv_debugfs);
	if (IS_ERR(mshv_debugfs_partition))
		return PTR_ERR(mshv_debugfs_partition);

	err = partition_debugfs_create(hv_current_partition_id,
				       &vp_dir,
				       &parent_partition_stats,
				       mshv_debugfs_partition);
	if (err)
		goto remove_debugfs_partition;

	parent_vp_stats = kzalloc_objs(*parent_vp_stats, nr_cpu_ids);
	if (!parent_vp_stats) {
		err = -ENOMEM;
		goto remove_debugfs_partition;
	}

	for_each_online_cpu(idx) {
		err = parent_vp_debugfs_create(hv_vp_index[idx],
					       &parent_vp_stats[idx],
					       vp_dir);
		if (err)
			goto remove_debugfs_partition_vp;
	}

	return 0;

remove_debugfs_partition_vp:
	for_each_online_cpu(i) {
		if (i >= idx)
			break;
		parent_vp_debugfs_remove(i, parent_vp_stats[i]);
	}
	partition_debugfs_remove(hv_current_partition_id,
				 parent_partition_stats);

	kfree(parent_vp_stats);
	parent_vp_stats = NULL;
	parent_partition_stats = NULL;

remove_debugfs_partition:
	debugfs_remove_recursive(mshv_debugfs_partition);
	mshv_debugfs_partition = NULL;
	return err;
}

static int hv_stats_show(struct seq_file *m, void *v)
{
	const struct hv_stats_page *stats = m->private;
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(hv_hypervisor_counters); idx++) {
		char *name = hv_hypervisor_counters[idx];

		if (!name)
			continue;
		seq_printf(m, "%-27s: %llu\n", name, stats->data[idx]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hv_stats);

static void mshv_hv_stats_unmap(void)
{
	union hv_stats_object_identity identity = {
		.hv.stats_area_type = HV_STATS_AREA_SELF,
	};
	int err;

	err = hv_unmap_stats_page(HV_STATS_OBJECT_HYPERVISOR, NULL, &identity);
	if (err)
		pr_err("%s: failed to unmap hypervisor stats: %d\n",
		       __func__, err);
}

static void * __init mshv_hv_stats_map(void)
{
	union hv_stats_object_identity identity = {
		.hv.stats_area_type = HV_STATS_AREA_SELF,
	};
	struct hv_stats_page *stats;
	int err;

	err = hv_map_stats_page(HV_STATS_OBJECT_HYPERVISOR, &identity, &stats);
	if (err) {
		pr_err("%s: failed to map hypervisor stats: %d\n",
		       __func__, err);
		return ERR_PTR(err);
	}
	return stats;
}

static int __init mshv_debugfs_hv_stats_create(struct dentry *parent)
{
	struct dentry *dentry;
	u64 *stats;
	int err;

	stats = mshv_hv_stats_map();
	if (IS_ERR(stats))
		return PTR_ERR(stats);

	dentry = debugfs_create_file("stats", 0400, parent,
				     stats, &hv_stats_fops);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		pr_err("%s: failed to create hypervisor stats dentry: %d\n",
		       __func__, err);
		goto unmap_hv_stats;
	}

	mshv_lps_count = stats[HV_HYPERVISOR_COUNTER_LOGICAL_PROCESSORS];

	return 0;

unmap_hv_stats:
	mshv_hv_stats_unmap();
	return err;
}

int mshv_debugfs_vp_create(struct mshv_vp *vp)
{
	struct mshv_partition *p = vp->vp_partition;

	if (!mshv_debugfs)
		return 0;

	return vp_debugfs_create(p->pt_id, vp->vp_index,
				 vp->vp_stats_pages,
				 &vp->vp_stats_dentry,
				 p->pt_vp_dentry);
}

void mshv_debugfs_vp_remove(struct mshv_vp *vp)
{
	if (!mshv_debugfs)
		return;

	vp_debugfs_remove(vp->vp_stats_dentry);
}

int mshv_debugfs_partition_create(struct mshv_partition *partition)
{
	int err;

	if (!mshv_debugfs)
		return 0;

	err = partition_debugfs_create(partition->pt_id,
				       &partition->pt_vp_dentry,
				       &partition->pt_stats_dentry,
				       mshv_debugfs_partition);
	if (err)
		return err;

	return 0;
}

void mshv_debugfs_partition_remove(struct mshv_partition *partition)
{
	if (!mshv_debugfs)
		return;

	partition_debugfs_remove(partition->pt_id,
				 partition->pt_stats_dentry);
}

int __init mshv_debugfs_init(void)
{
	int err;

	mshv_debugfs = debugfs_create_dir("mshv", NULL);
	if (IS_ERR(mshv_debugfs)) {
		pr_err("%s: failed to create debugfs directory\n", __func__);
		return PTR_ERR(mshv_debugfs);
	}

	if (hv_root_partition()) {
		err = mshv_debugfs_hv_stats_create(mshv_debugfs);
		if (err)
			goto remove_mshv_dir;

		err = mshv_debugfs_lp_create(mshv_debugfs);
		if (err)
			goto unmap_hv_stats;
	}

	err = mshv_debugfs_parent_partition_create();
	if (err)
		goto unmap_lp_stats;

	return 0;

unmap_lp_stats:
	if (hv_root_partition()) {
		mshv_debugfs_lp_remove();
		mshv_debugfs_lp = NULL;
	}
unmap_hv_stats:
	if (hv_root_partition())
		mshv_hv_stats_unmap();
remove_mshv_dir:
	debugfs_remove_recursive(mshv_debugfs);
	mshv_debugfs = NULL;
	return err;
}

void mshv_debugfs_exit(void)
{
	mshv_debugfs_parent_partition_remove();

	if (hv_root_partition()) {
		mshv_debugfs_lp_remove();
		mshv_debugfs_lp = NULL;
		mshv_hv_stats_unmap();
	}

	debugfs_remove_recursive(mshv_debugfs);
	mshv_debugfs = NULL;
	mshv_debugfs_partition = NULL;
}
