// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_pm.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_control.h"
#include "xe_sriov_pf_debugfs.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_provision.h"
#include "xe_sriov_pf_service.h"
#include "xe_sriov_printk.h"
#include "xe_tile_sriov_pf_debugfs.h"

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov		# d_inode->i_private = (xe_device*)
 *      │   ├── pf		# d_inode->i_private = (xe_device*)
 *      │   ├── vf1		# d_inode->i_private = VFID(1)
 *      :   :
 *      │   ├── vfN		# d_inode->i_private = VFID(N)
 */

static void *extract_priv(struct dentry *d)
{
	return d->d_inode->i_private;
}

static struct xe_device *extract_xe(struct dentry *d)
{
	return extract_priv(d->d_parent);
}

static unsigned int extract_vfid(struct dentry *d)
{
	void *p = extract_priv(d);

	return p == extract_xe(d) ? PFID : (uintptr_t)p;
}

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov
 *      │   ├── restore_auto_provisioning
 *      │   :
 *      │   ├── pf/
 *      │   ├── vf1
 *      │   │   ├── ...
 */

static ssize_t from_file_write_to_xe_call(struct file *file, const char __user *userbuf,
					  size_t count, loff_t *ppos,
					  int (*call)(struct xe_device *))
{
	struct dentry *dent = file_dentry(file);
	struct xe_device *xe = extract_xe(dent);
	bool yes;
	int ret;

	if (*ppos)
		return -EINVAL;
	ret = kstrtobool_from_user(userbuf, count, &yes);
	if (ret < 0)
		return ret;
	if (yes) {
		xe_pm_runtime_get(xe);
		ret = call(xe);
		xe_pm_runtime_put(xe);
	}
	if (ret < 0)
		return ret;
	return count;
}

#define DEFINE_SRIOV_ATTRIBUTE(OP)						\
static int OP##_show(struct seq_file *s, void *unused)				\
{										\
	return 0;								\
}										\
static ssize_t OP##_write(struct file *file, const char __user *userbuf,	\
			  size_t count, loff_t *ppos)				\
{										\
	return from_file_write_to_xe_call(file, userbuf, count, ppos,		\
					  xe_sriov_pf_##OP);			\
}										\
DEFINE_SHOW_STORE_ATTRIBUTE(OP)

static inline int xe_sriov_pf_restore_auto_provisioning(struct xe_device *xe)
{
	return xe_sriov_pf_provision_set_mode(xe, XE_SRIOV_PROVISIONING_MODE_AUTO);
}

DEFINE_SRIOV_ATTRIBUTE(restore_auto_provisioning);

static void pf_populate_root(struct xe_device *xe, struct dentry *dent)
{
	debugfs_create_file("restore_auto_provisioning", 0200, dent, xe,
			    &restore_auto_provisioning_fops);
}

static int simple_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct dentry *parent = node->dent->d_parent;
	struct xe_device *xe = parent->d_inode->i_private;
	void (*print)(struct xe_device *, struct drm_printer *) = node->info_ent->data;

	print(xe, &p);
	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{ .name = "vfs", .show = simple_show, .data = xe_sriov_pf_print_vfs_summary },
	{ .name = "versions", .show = simple_show, .data = xe_sriov_pf_service_print_versions },
};

static void pf_populate_pf(struct xe_device *xe, struct dentry *pfdent)
{
	struct drm_minor *minor = xe->drm.primary;

	drm_debugfs_create_files(debugfs_list, ARRAY_SIZE(debugfs_list), pfdent, minor);
}

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov
 *      │   ├── vf1
 *      │   │   ├── pause
 *      │   │   ├── reset
 *      │   │   ├── resume
 *      │   │   ├── stop
 *      │   │   :
 *      │   ├── vf2
 *      │   │   ├── ...
 */

static ssize_t from_file_write_to_vf_call(struct file *file, const char __user *userbuf,
					  size_t count, loff_t *ppos,
					  int (*call)(struct xe_device *, unsigned int))
{
	struct dentry *dent = file_dentry(file)->d_parent;
	struct xe_device *xe = extract_xe(dent);
	unsigned int vfid = extract_vfid(dent);
	bool yes;
	int ret;

	if (*ppos)
		return -EINVAL;
	ret = kstrtobool_from_user(userbuf, count, &yes);
	if (ret < 0)
		return ret;
	if (yes) {
		xe_pm_runtime_get(xe);
		ret = call(xe, vfid);
		xe_pm_runtime_put(xe);
	}
	if (ret < 0)
		return ret;
	return count;
}

#define DEFINE_VF_CONTROL_ATTRIBUTE(OP)						\
static int OP##_show(struct seq_file *s, void *unused)				\
{										\
	return 0;								\
}										\
static ssize_t OP##_write(struct file *file, const char __user *userbuf,	\
			  size_t count, loff_t *ppos)				\
{										\
	return from_file_write_to_vf_call(file, userbuf, count, ppos,		\
					  xe_sriov_pf_control_##OP);		\
}										\
DEFINE_SHOW_STORE_ATTRIBUTE(OP)

DEFINE_VF_CONTROL_ATTRIBUTE(pause_vf);
DEFINE_VF_CONTROL_ATTRIBUTE(resume_vf);
DEFINE_VF_CONTROL_ATTRIBUTE(stop_vf);
DEFINE_VF_CONTROL_ATTRIBUTE(reset_vf);

static void pf_populate_vf(struct xe_device *xe, struct dentry *vfdent)
{
	debugfs_create_file("pause", 0200, vfdent, xe, &pause_vf_fops);
	debugfs_create_file("resume", 0200, vfdent, xe, &resume_vf_fops);
	debugfs_create_file("stop", 0200, vfdent, xe, &stop_vf_fops);
	debugfs_create_file("reset", 0200, vfdent, xe, &reset_vf_fops);
}

static void pf_populate_with_tiles(struct xe_device *xe, struct dentry *dent, unsigned int vfid)
{
	struct xe_tile *tile;
	unsigned int id;

	for_each_tile(tile, xe, id)
		xe_tile_sriov_pf_debugfs_populate(tile, dent, vfid);
}

/**
 * xe_sriov_pf_debugfs_register - Register PF debugfs attributes.
 * @xe: the &xe_device
 * @root: the root &dentry
 *
 * Create separate directory that will contain all SR-IOV related files,
 * organized per each SR-IOV function (PF, VF1, VF2, ..., VFn).
 */
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root)
{
	int totalvfs = xe_sriov_pf_get_totalvfs(xe);
	struct dentry *pfdent;
	struct dentry *vfdent;
	struct dentry *dent;
	char vfname[16]; /* should be more than enough for "vf%u\0" and VFID(UINT_MAX) */
	unsigned int n;

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── ...
	 */
	dent = debugfs_create_dir("sriov", root);
	if (IS_ERR(dent))
		return;
	dent->d_inode->i_private = xe;

	pf_populate_root(xe, dent);

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── pf		# d_inode->i_private = (xe_device*)
	 *      │   │   ├── ...
	 */
	pfdent = debugfs_create_dir("pf", dent);
	if (IS_ERR(pfdent))
		return;
	pfdent->d_inode->i_private = xe;

	pf_populate_pf(xe, pfdent);
	pf_populate_with_tiles(xe, pfdent, PFID);

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── vf1		# d_inode->i_private = VFID(1)
	 *      │   ├── vf2		# d_inode->i_private = VFID(2)
	 *      │   ├── ...
	 */
	for (n = 1; n <= totalvfs; n++) {
		snprintf(vfname, sizeof(vfname), "vf%u", VFID(n));
		vfdent = debugfs_create_dir(vfname, dent);
		if (IS_ERR(vfdent))
			return;
		vfdent->d_inode->i_private = (void *)(uintptr_t)VFID(n);

		pf_populate_vf(xe, vfdent);
		pf_populate_with_tiles(xe, vfdent, VFID(n));
	}
}
