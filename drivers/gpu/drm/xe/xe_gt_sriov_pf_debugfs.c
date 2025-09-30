// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/debugfs.h>

#include <drm/drm_print.h>
#include <drm/drm_debugfs.h>

#include "xe_bo.h"
#include "xe_debugfs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_debugfs.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_debugfs.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_gt_sriov_pf_monitor.h"
#include "xe_gt_sriov_pf_policy.h"
#include "xe_gt_sriov_pf_service.h"
#include "xe_pm.h"
#include "xe_sriov_pf.h"

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0		# d_inode->i_private = gt
 *      │   ├── pf	# d_inode->i_private = gt
 *      │   ├── vf1	# d_inode->i_private = VFID(1)
 *      :   :
 *      │   ├── vfN	# d_inode->i_private = VFID(N)
 */

static void *extract_priv(struct dentry *d)
{
	return d->d_inode->i_private;
}

static struct xe_gt *extract_gt(struct dentry *d)
{
	return extract_priv(d->d_parent);
}

static unsigned int extract_vfid(struct dentry *d)
{
	return extract_priv(d) == extract_gt(d) ? PFID : (uintptr_t)extract_priv(d);
}

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── contexts_provisioned
 *      │   │   ├── doorbells_provisioned
 *      │   │   ├── runtime_registers
 *      │   │   ├── negotiated_versions
 *      │   │   ├── adverse_events
 *      ├── gt1
 *      │   ├── pf
 *      │   │   ├── ...
 */

static const struct drm_info_list pf_info[] = {
	{
		"contexts_provisioned",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_config_print_ctxs,
	},
	{
		"doorbells_provisioned",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_config_print_dbs,
	},
	{
		"runtime_registers",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_service_print_runtime,
	},
	{
		"adverse_events",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_monitor_print_events,
	},
};

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── ggtt_available
 *      │   │   ├── ggtt_provisioned
 */

static const struct drm_info_list pf_ggtt_info[] = {
	{
		"ggtt_available",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_config_print_available_ggtt,
	},
	{
		"ggtt_provisioned",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_config_print_ggtt,
	},
};

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── lmem_provisioned
 */

static const struct drm_info_list pf_lmem_info[] = {
	{
		"lmem_provisioned",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_pf_config_print_lmem,
	},
};

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── reset_engine
 *      │   │   ├── sample_period
 *      │   │   ├── sched_if_idle
 */

#define DEFINE_SRIOV_GT_POLICY_DEBUGFS_ATTRIBUTE(POLICY, TYPE, FORMAT)		\
										\
static int POLICY##_set(void *data, u64 val)					\
{										\
	struct xe_gt *gt = extract_gt(data);					\
	struct xe_device *xe = gt_to_xe(gt);					\
	int err;								\
										\
	if (val > (TYPE)~0ull)							\
		return -EOVERFLOW;						\
										\
	xe_pm_runtime_get(xe);							\
	err = xe_gt_sriov_pf_policy_set_##POLICY(gt, val);			\
	xe_pm_runtime_put(xe);							\
										\
	return err;								\
}										\
										\
static int POLICY##_get(void *data, u64 *val)					\
{										\
	struct xe_gt *gt = extract_gt(data);					\
										\
	*val = xe_gt_sriov_pf_policy_get_##POLICY(gt);				\
	return 0;								\
}										\
										\
DEFINE_DEBUGFS_ATTRIBUTE(POLICY##_fops, POLICY##_get, POLICY##_set, FORMAT)

DEFINE_SRIOV_GT_POLICY_DEBUGFS_ATTRIBUTE(reset_engine, bool, "%llu\n");
DEFINE_SRIOV_GT_POLICY_DEBUGFS_ATTRIBUTE(sched_if_idle, bool, "%llu\n");
DEFINE_SRIOV_GT_POLICY_DEBUGFS_ATTRIBUTE(sample_period, u32, "%llu\n");

static void pf_add_policy_attrs(struct xe_gt *gt, struct dentry *parent)
{
	xe_gt_assert(gt, gt == extract_gt(parent));
	xe_gt_assert(gt, PFID == extract_vfid(parent));

	debugfs_create_file_unsafe("reset_engine", 0644, parent, parent, &reset_engine_fops);
	debugfs_create_file_unsafe("sched_if_idle", 0644, parent, parent, &sched_if_idle_fops);
	debugfs_create_file_unsafe("sample_period_ms", 0644, parent, parent, &sample_period_fops);
}

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── ggtt_spare
 *      │   │   ├── lmem_spare
 *      │   │   ├── doorbells_spare
 *      │   │   ├── contexts_spare
 *      │   │   ├── exec_quantum_ms
 *      │   │   ├── preempt_timeout_us
 *      │   │   ├── sched_priority
 *      │   ├── vf1
 *      │   │   ├── ggtt_quota
 *      │   │   ├── lmem_quota
 *      │   │   ├── doorbells_quota
 *      │   │   ├── contexts_quota
 *      │   │   ├── exec_quantum_ms
 *      │   │   ├── preempt_timeout_us
 *      │   │   ├── sched_priority
 */

#define DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(CONFIG, TYPE, FORMAT)		\
										\
static int CONFIG##_set(void *data, u64 val)					\
{										\
	struct xe_gt *gt = extract_gt(data);					\
	unsigned int vfid = extract_vfid(data);					\
	struct xe_device *xe = gt_to_xe(gt);					\
	int err;								\
										\
	if (val > (TYPE)~0ull)							\
		return -EOVERFLOW;						\
										\
	xe_pm_runtime_get(xe);							\
	err = xe_sriov_pf_wait_ready(xe) ?:					\
	      xe_gt_sriov_pf_config_set_##CONFIG(gt, vfid, val);		\
	xe_pm_runtime_put(xe);							\
										\
	return err;								\
}										\
										\
static int CONFIG##_get(void *data, u64 *val)					\
{										\
	struct xe_gt *gt = extract_gt(data);					\
	unsigned int vfid = extract_vfid(data);					\
										\
	*val = xe_gt_sriov_pf_config_get_##CONFIG(gt, vfid);			\
	return 0;								\
}										\
										\
DEFINE_DEBUGFS_ATTRIBUTE(CONFIG##_fops, CONFIG##_get, CONFIG##_set, FORMAT)

DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(ggtt, u64, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(lmem, u64, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(ctxs, u32, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(dbs, u32, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(exec_quantum, u32, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(preempt_timeout, u32, "%llu\n");
DEFINE_SRIOV_GT_CONFIG_DEBUGFS_ATTRIBUTE(sched_priority, u32, "%llu\n");

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── pf
 *      │   │   ├── threshold_cat_error_count
 *      │   │   ├── threshold_doorbell_time_us
 *      │   │   ├── threshold_engine_reset_count
 *      │   │   ├── threshold_guc_time_us
 *      │   │   ├── threshold_irq_time_us
 *      │   │   ├── threshold_page_fault_count
 *      │   ├── vf1
 *      │   │   ├── threshold_cat_error_count
 *      │   │   ├── threshold_doorbell_time_us
 *      │   │   ├── threshold_engine_reset_count
 *      │   │   ├── threshold_guc_time_us
 *      │   │   ├── threshold_irq_time_us
 *      │   │   ├── threshold_page_fault_count
 */

static int set_threshold(void *data, u64 val, enum xe_guc_klv_threshold_index index)
{
	struct xe_gt *gt = extract_gt(data);
	unsigned int vfid = extract_vfid(data);
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	if (val > (u32)~0ull)
		return -EOVERFLOW;

	xe_pm_runtime_get(xe);
	err = xe_gt_sriov_pf_config_set_threshold(gt, vfid, index, val);
	xe_pm_runtime_put(xe);

	return err;
}

static int get_threshold(void *data, u64 *val, enum xe_guc_klv_threshold_index index)
{
	struct xe_gt *gt = extract_gt(data);
	unsigned int vfid = extract_vfid(data);

	*val = xe_gt_sriov_pf_config_get_threshold(gt, vfid, index);
	return 0;
}

#define DEFINE_SRIOV_GT_THRESHOLD_DEBUGFS_ATTRIBUTE(THRESHOLD, INDEX)		\
										\
static int THRESHOLD##_set(void *data, u64 val)					\
{										\
	return set_threshold(data, val, INDEX);					\
}										\
										\
static int THRESHOLD##_get(void *data, u64 *val)				\
{										\
	return get_threshold(data, val, INDEX);					\
}										\
										\
DEFINE_DEBUGFS_ATTRIBUTE(THRESHOLD##_fops, THRESHOLD##_get, THRESHOLD##_set, "%llu\n")

/* generate all threshold attributes */
#define define_threshold_attribute(TAG, NAME, ...) \
	DEFINE_SRIOV_GT_THRESHOLD_DEBUGFS_ATTRIBUTE(NAME, MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG));
MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_threshold_attribute)
#undef define_threshold_attribute

static void pf_add_config_attrs(struct xe_gt *gt, struct dentry *parent, unsigned int vfid)
{
	xe_gt_assert(gt, gt == extract_gt(parent));
	xe_gt_assert(gt, vfid == extract_vfid(parent));

	if (xe_gt_is_main_type(gt)) {
		debugfs_create_file_unsafe(vfid ? "ggtt_quota" : "ggtt_spare",
					   0644, parent, parent, &ggtt_fops);
		if (xe_device_has_lmtt(gt_to_xe(gt)))
			debugfs_create_file_unsafe(vfid ? "lmem_quota" : "lmem_spare",
						   0644, parent, parent, &lmem_fops);
	}
	debugfs_create_file_unsafe(vfid ? "doorbells_quota" : "doorbells_spare",
				   0644, parent, parent, &dbs_fops);
	debugfs_create_file_unsafe(vfid ? "contexts_quota" : "contexts_spare",
				   0644, parent, parent, &ctxs_fops);
	debugfs_create_file_unsafe("exec_quantum_ms", 0644, parent, parent,
				   &exec_quantum_fops);
	debugfs_create_file_unsafe("preempt_timeout_us", 0644, parent, parent,
				   &preempt_timeout_fops);
	debugfs_create_file_unsafe("sched_priority", 0644, parent, parent,
				   &sched_priority_fops);

	/* register all threshold attributes */
#define register_threshold_attribute(TAG, NAME, ...) \
	debugfs_create_file_unsafe("threshold_" #NAME, 0644, parent, parent, \
				   &NAME##_fops);
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(register_threshold_attribute)
#undef register_threshold_attribute
}

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── vf1
 *      │   │   ├── control { stop, pause, resume }
 */

static const struct {
	const char *cmd;
	int (*fn)(struct xe_gt *gt, unsigned int vfid);
} control_cmds[] = {
	{ "stop", xe_gt_sriov_pf_control_stop_vf },
	{ "pause", xe_gt_sriov_pf_control_pause_vf },
	{ "resume", xe_gt_sriov_pf_control_resume_vf },
#ifdef CONFIG_DRM_XE_DEBUG_SRIOV
	{ "restore!", xe_gt_sriov_pf_migration_restore_guc_state },
#endif
};

static ssize_t control_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct dentry *dent = file_dentry(file);
	struct dentry *parent = dent->d_parent;
	struct xe_gt *gt = extract_gt(parent);
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int vfid = extract_vfid(parent);
	int ret = -EINVAL;
	char cmd[32];
	size_t n;

	xe_gt_assert(gt, vfid);
	xe_gt_sriov_pf_assert_vfid(gt, vfid);

	if (*pos)
		return -ESPIPE;

	if (count > sizeof(cmd) - 1)
		return -EINVAL;

	ret = simple_write_to_buffer(cmd, sizeof(cmd) - 1, pos, buf, count);
	if (ret < 0)
		return ret;
	cmd[ret] = '\0';

	for (n = 0; n < ARRAY_SIZE(control_cmds); n++) {
		xe_gt_assert(gt, sizeof(cmd) > strlen(control_cmds[n].cmd));

		if (sysfs_streq(cmd, control_cmds[n].cmd)) {
			xe_pm_runtime_get(xe);
			ret = control_cmds[n].fn ? (*control_cmds[n].fn)(gt, vfid) : 0;
			xe_pm_runtime_put(xe);
			break;
		}
	}

	return (ret < 0) ? ret : count;
}

static ssize_t control_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char help[128];
	size_t n;

	help[0] = '\0';
	for (n = 0; n < ARRAY_SIZE(control_cmds); n++) {
		strlcat(help, control_cmds[n].cmd, sizeof(help));
		strlcat(help, "\n", sizeof(help));
	}

	return simple_read_from_buffer(buf, count, ppos, help, strlen(help));
}

static const struct file_operations control_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.write		= control_write,
	.read		= control_read,
	.llseek		= default_llseek,
};

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── vf1
 *      │   │   ├── guc_state
 */
static ssize_t guc_state_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	struct dentry *dent = file_dentry(file);
	struct dentry *parent = dent->d_parent;
	struct xe_gt *gt = extract_gt(parent);
	unsigned int vfid = extract_vfid(parent);

	return xe_gt_sriov_pf_migration_read_guc_state(gt, vfid, buf, count, pos);
}

static ssize_t guc_state_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct dentry *dent = file_dentry(file);
	struct dentry *parent = dent->d_parent;
	struct xe_gt *gt = extract_gt(parent);
	unsigned int vfid = extract_vfid(parent);

	if (*pos)
		return -EINVAL;

	return xe_gt_sriov_pf_migration_write_guc_state(gt, vfid, buf, count);
}

static const struct file_operations guc_state_ops = {
	.owner		= THIS_MODULE,
	.read		= guc_state_read,
	.write		= guc_state_write,
	.llseek		= default_llseek,
};

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── vf1
 *      │   │   ├── config_blob
 */
static ssize_t config_blob_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	struct dentry *dent = file_dentry(file);
	struct dentry *parent = dent->d_parent;
	struct xe_gt *gt = extract_gt(parent);
	unsigned int vfid = extract_vfid(parent);
	ssize_t ret;
	void *tmp;

	ret = xe_gt_sriov_pf_config_save(gt, vfid, NULL, 0);
	if (!ret)
		return -ENODATA;
	if (ret < 0)
		return ret;

	tmp = kzalloc(ret, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = xe_gt_sriov_pf_config_save(gt, vfid, tmp, ret);
	if (ret > 0)
		ret = simple_read_from_buffer(buf, count, pos, tmp, ret);

	kfree(tmp);
	return ret;
}

static ssize_t config_blob_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *pos)
{
	struct dentry *dent = file_dentry(file);
	struct dentry *parent = dent->d_parent;
	struct xe_gt *gt = extract_gt(parent);
	unsigned int vfid = extract_vfid(parent);
	ssize_t ret;
	void *tmp;

	if (*pos)
		return -EINVAL;

	if (!count)
		return -ENODATA;

	if (count > SZ_4K)
		return -EINVAL;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
	} else {
		ret = xe_gt_sriov_pf_config_restore(gt, vfid, tmp, count);
		if (!ret)
			ret = count;
	}
	kfree(tmp);
	return ret;
}

static const struct file_operations config_blob_ops = {
	.owner		= THIS_MODULE,
	.read		= config_blob_read,
	.write		= config_blob_write,
	.llseek		= default_llseek,
};

/**
 * xe_gt_sriov_pf_debugfs_register - Register SR-IOV PF specific entries in GT debugfs.
 * @gt: the &xe_gt to register
 * @root: the &dentry that represents the GT directory
 *
 * Register SR-IOV PF entries that are GT related and must be shown under GT debugfs.
 */
void xe_gt_sriov_pf_debugfs_register(struct xe_gt *gt, struct dentry *root)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_minor *minor = xe->drm.primary;
	int n, totalvfs = xe_sriov_pf_get_totalvfs(xe);
	struct dentry *pfdentry;
	struct dentry *vfdentry;
	char buf[14]; /* should be enough up to "vf%u\0" for 2^32 - 1 */

	xe_gt_assert(gt, IS_SRIOV_PF(xe));
	xe_gt_assert(gt, root->d_inode->i_private == gt);

	/*
	 *      /sys/kernel/debug/dri/0/
	 *      ├── gt0
	 *      │   ├── pf
	 */
	pfdentry = debugfs_create_dir("pf", root);
	if (IS_ERR(pfdentry))
		return;
	pfdentry->d_inode->i_private = gt;

	drm_debugfs_create_files(pf_info, ARRAY_SIZE(pf_info), pfdentry, minor);
	if (xe_gt_is_main_type(gt)) {
		drm_debugfs_create_files(pf_ggtt_info,
					 ARRAY_SIZE(pf_ggtt_info),
					 pfdentry, minor);
		if (xe_device_has_lmtt(gt_to_xe(gt)))
			drm_debugfs_create_files(pf_lmem_info,
						 ARRAY_SIZE(pf_lmem_info),
						 pfdentry, minor);
	}

	pf_add_policy_attrs(gt, pfdentry);
	pf_add_config_attrs(gt, pfdentry, PFID);

	for (n = 1; n <= totalvfs; n++) {
		/*
		 *      /sys/kernel/debug/dri/0/
		 *      ├── gt0
		 *      │   ├── vf1
		 *      │   ├── vf2
		 */
		snprintf(buf, sizeof(buf), "vf%u", n);
		vfdentry = debugfs_create_dir(buf, root);
		if (IS_ERR(vfdentry))
			break;
		vfdentry->d_inode->i_private = (void *)(uintptr_t)n;

		pf_add_config_attrs(gt, vfdentry, VFID(n));
		debugfs_create_file("control", 0600, vfdentry, NULL, &control_ops);

		/* for testing/debugging purposes only! */
		if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
			debugfs_create_file("guc_state",
					    IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV) ? 0600 : 0400,
					    vfdentry, NULL, &guc_state_ops);
			debugfs_create_file("config_blob",
					    IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV) ? 0600 : 0400,
					    vfdentry, NULL, &config_blob_ops);
		}
	}
}
