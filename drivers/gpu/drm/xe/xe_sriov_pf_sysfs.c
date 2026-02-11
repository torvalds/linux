// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_pci_sriov.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_control.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_provision.h"
#include "xe_sriov_pf_sysfs.h"
#include "xe_sriov_printk.h"

static int emit_choice(char *buf, int choice, const char * const *array, size_t size)
{
	int pos = 0;
	int n;

	for (n = 0; n < size; n++) {
		pos += sysfs_emit_at(buf, pos, "%s%s%s%s",
				    n ? " " : "",
				    n == choice ? "[" : "",
				    array[n],
				    n == choice ? "]" : "");
	}
	pos += sysfs_emit_at(buf, pos, "\n");

	return pos;
}

/*
 * /sys/bus/pci/drivers/xe/BDF/
 * :
 * ├── sriov_admin/
 *     ├── ...
 *     ├── .bulk_profile
 *     │   ├── exec_quantum_ms
 *     │   ├── preempt_timeout_us
 *     │   └── sched_priority
 *     ├── pf/
 *     │   ├── ...
 *     │   ├── device -> ../../../BDF
 *     │   └── profile
 *     │       ├── exec_quantum_ms
 *     │       ├── preempt_timeout_us
 *     │       └── sched_priority
 *     ├── vf1/
 *     │   ├── ...
 *     │   ├── device -> ../../../BDF.1
 *     │   ├── stop
 *     │   └── profile
 *     │       ├── exec_quantum_ms
 *     │       ├── preempt_timeout_us
 *     │       └── sched_priority
 *     ├── vf2/
 *     :
 *     └── vfN/
 */

struct xe_sriov_kobj {
	struct kobject base;
	struct xe_device *xe;
	unsigned int vfid;
};
#define to_xe_sriov_kobj(p) container_of_const((p), struct xe_sriov_kobj, base)

struct xe_sriov_dev_attr {
	struct attribute attr;
	ssize_t (*show)(struct xe_device *xe, char *buf);
	ssize_t (*store)(struct xe_device *xe, const char *buf, size_t count);
};
#define to_xe_sriov_dev_attr(p) container_of_const((p), struct xe_sriov_dev_attr, attr)

#define XE_SRIOV_DEV_ATTR(NAME) \
struct xe_sriov_dev_attr xe_sriov_dev_attr_##NAME = \
	__ATTR(NAME, 0644, xe_sriov_dev_attr_##NAME##_show, xe_sriov_dev_attr_##NAME##_store)

#define XE_SRIOV_DEV_ATTR_RO(NAME) \
struct xe_sriov_dev_attr xe_sriov_dev_attr_##NAME = \
	__ATTR(NAME, 0444, xe_sriov_dev_attr_##NAME##_show, NULL)

#define XE_SRIOV_DEV_ATTR_WO(NAME) \
struct xe_sriov_dev_attr xe_sriov_dev_attr_##NAME = \
	__ATTR(NAME, 0200, NULL, xe_sriov_dev_attr_##NAME##_store)

struct xe_sriov_vf_attr {
	struct attribute attr;
	ssize_t (*show)(struct xe_device *xe, unsigned int vfid, char *buf);
	ssize_t (*store)(struct xe_device *xe, unsigned int vfid, const char *buf, size_t count);
};
#define to_xe_sriov_vf_attr(p) container_of_const((p), struct xe_sriov_vf_attr, attr)

#define XE_SRIOV_VF_ATTR(NAME) \
struct xe_sriov_vf_attr xe_sriov_vf_attr_##NAME = \
	__ATTR(NAME, 0644, xe_sriov_vf_attr_##NAME##_show, xe_sriov_vf_attr_##NAME##_store)

#define XE_SRIOV_VF_ATTR_RO(NAME) \
struct xe_sriov_vf_attr xe_sriov_vf_attr_##NAME = \
	__ATTR(NAME, 0444, xe_sriov_vf_attr_##NAME##_show, NULL)

#define XE_SRIOV_VF_ATTR_WO(NAME) \
struct xe_sriov_vf_attr xe_sriov_vf_attr_##NAME = \
	__ATTR(NAME, 0200, NULL, xe_sriov_vf_attr_##NAME##_store)

/* device level attributes go here */

#define DEFINE_SIMPLE_BULK_PROVISIONING_SRIOV_DEV_ATTR_WO(NAME, ITEM, TYPE)		\
											\
static ssize_t xe_sriov_dev_attr_##NAME##_store(struct xe_device *xe,			\
						const char *buf, size_t count)		\
{											\
	TYPE value;									\
	int err;									\
											\
	err = kstrto##TYPE(buf, 0, &value);						\
	if (err)									\
		return err;								\
											\
	err = xe_sriov_pf_provision_bulk_apply_##ITEM(xe, value);			\
	return err ?: count;								\
}											\
											\
static XE_SRIOV_DEV_ATTR_WO(NAME)

DEFINE_SIMPLE_BULK_PROVISIONING_SRIOV_DEV_ATTR_WO(exec_quantum_ms, eq, u32);
DEFINE_SIMPLE_BULK_PROVISIONING_SRIOV_DEV_ATTR_WO(preempt_timeout_us, pt, u32);

static const char * const sched_priority_names[] = {
	[GUC_SCHED_PRIORITY_LOW] = "low",
	[GUC_SCHED_PRIORITY_NORMAL] = "normal",
	[GUC_SCHED_PRIORITY_HIGH] = "high",
};

static bool sched_priority_change_allowed(unsigned int vfid)
{
	/* As of today GuC FW allows to selectively change only the PF priority. */
	return vfid == PFID;
}

static bool sched_priority_high_allowed(unsigned int vfid)
{
	/* As of today GuC FW allows to select 'high' priority only for the PF. */
	return vfid == PFID;
}

static bool sched_priority_bulk_high_allowed(struct xe_device *xe)
{
	/* all VFs are equal - it's sufficient to check VF1 only */
	return sched_priority_high_allowed(VFID(1));
}

static ssize_t xe_sriov_dev_attr_sched_priority_store(struct xe_device *xe,
						      const char *buf, size_t count)
{
	size_t num_priorities = ARRAY_SIZE(sched_priority_names);
	int match;
	int err;

	if (!sched_priority_bulk_high_allowed(xe))
		num_priorities--;

	match = __sysfs_match_string(sched_priority_names, num_priorities, buf);
	if (match < 0)
		return -EINVAL;

	err = xe_sriov_pf_provision_bulk_apply_priority(xe, match);
	return err ?: count;
}

static XE_SRIOV_DEV_ATTR_WO(sched_priority);

static struct attribute *bulk_profile_dev_attrs[] = {
	&xe_sriov_dev_attr_exec_quantum_ms.attr,
	&xe_sriov_dev_attr_preempt_timeout_us.attr,
	&xe_sriov_dev_attr_sched_priority.attr,
	NULL
};

static const struct attribute_group bulk_profile_dev_attr_group = {
	.name = ".bulk_profile",
	.attrs = bulk_profile_dev_attrs,
};

static const struct attribute_group *xe_sriov_dev_attr_groups[] = {
	&bulk_profile_dev_attr_group,
	NULL
};

/* and VF-level attributes go here */

#define DEFINE_SIMPLE_PROVISIONING_SRIOV_VF_ATTR(NAME, ITEM, TYPE, FORMAT)		\
static ssize_t xe_sriov_vf_attr_##NAME##_show(struct xe_device *xe, unsigned int vfid,	\
					      char *buf)				\
{											\
	TYPE value = 0;									\
	int err;									\
											\
	err = xe_sriov_pf_provision_query_vf_##ITEM(xe, vfid, &value);			\
	if (err)									\
		return err;								\
											\
	return sysfs_emit(buf, FORMAT, value);						\
}											\
											\
static ssize_t xe_sriov_vf_attr_##NAME##_store(struct xe_device *xe, unsigned int vfid,	\
					       const char *buf, size_t count)		\
{											\
	TYPE value;									\
	int err;									\
											\
	err = kstrto##TYPE(buf, 0, &value);						\
	if (err)									\
		return err;								\
											\
	err = xe_sriov_pf_provision_apply_vf_##ITEM(xe, vfid, value);			\
	return err ?: count;								\
}											\
											\
static XE_SRIOV_VF_ATTR(NAME)

DEFINE_SIMPLE_PROVISIONING_SRIOV_VF_ATTR(exec_quantum_ms, eq, u32, "%u\n");
DEFINE_SIMPLE_PROVISIONING_SRIOV_VF_ATTR(preempt_timeout_us, pt, u32, "%u\n");

static ssize_t xe_sriov_vf_attr_sched_priority_show(struct xe_device *xe, unsigned int vfid,
						    char *buf)
{
	size_t num_priorities = ARRAY_SIZE(sched_priority_names);
	u32 priority;
	int err;

	err = xe_sriov_pf_provision_query_vf_priority(xe, vfid, &priority);
	if (err)
		return err;

	if (!sched_priority_high_allowed(vfid))
		num_priorities--;

	xe_assert(xe, priority < num_priorities);
	return emit_choice(buf, priority, sched_priority_names, num_priorities);
}

static ssize_t xe_sriov_vf_attr_sched_priority_store(struct xe_device *xe, unsigned int vfid,
						     const char *buf, size_t count)
{
	size_t num_priorities = ARRAY_SIZE(sched_priority_names);
	int match;
	int err;

	if (!sched_priority_change_allowed(vfid))
		return -EOPNOTSUPP;

	if (!sched_priority_high_allowed(vfid))
		num_priorities--;

	match = __sysfs_match_string(sched_priority_names, num_priorities, buf);
	if (match < 0)
		return -EINVAL;

	err = xe_sriov_pf_provision_apply_vf_priority(xe, vfid, match);
	return err ?: count;
}

static XE_SRIOV_VF_ATTR(sched_priority);

static struct attribute *profile_vf_attrs[] = {
	&xe_sriov_vf_attr_exec_quantum_ms.attr,
	&xe_sriov_vf_attr_preempt_timeout_us.attr,
	&xe_sriov_vf_attr_sched_priority.attr,
	NULL
};

static umode_t profile_vf_attr_is_visible(struct kobject *kobj,
					  struct attribute *attr, int index)
{
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);

	if (attr == &xe_sriov_vf_attr_sched_priority.attr &&
	    !sched_priority_change_allowed(vkobj->vfid))
		return attr->mode & 0444;

	return attr->mode;
}

static const struct attribute_group profile_vf_attr_group = {
	.name = "profile",
	.attrs = profile_vf_attrs,
	.is_visible = profile_vf_attr_is_visible,
};

#define DEFINE_SIMPLE_CONTROL_SRIOV_VF_ATTR(NAME)					\
											\
static ssize_t xe_sriov_vf_attr_##NAME##_store(struct xe_device *xe, unsigned int vfid,	\
					       const char *buf, size_t count)		\
{											\
	bool yes;									\
	int err;									\
											\
	if (!vfid)									\
		return -EPERM;								\
											\
	err = kstrtobool(buf, &yes);							\
	if (err)									\
		return err;								\
	if (!yes)									\
		return count;								\
											\
	err = xe_sriov_pf_control_##NAME##_vf(xe, vfid);				\
	return err ?: count;								\
}											\
											\
static XE_SRIOV_VF_ATTR_WO(NAME)

DEFINE_SIMPLE_CONTROL_SRIOV_VF_ATTR(stop);

static struct attribute *control_vf_attrs[] = {
	&xe_sriov_vf_attr_stop.attr,
	NULL
};

static umode_t control_vf_attr_is_visible(struct kobject *kobj,
					  struct attribute *attr, int index)
{
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);

	if (vkobj->vfid == PFID)
		return 0;

	return attr->mode;
}

static const struct attribute_group control_vf_attr_group = {
	.attrs = control_vf_attrs,
	.is_visible = control_vf_attr_is_visible,
};

static const struct attribute_group *xe_sriov_vf_attr_groups[] = {
	&profile_vf_attr_group,
	&control_vf_attr_group,
	NULL
};

/* no user serviceable parts below */

static struct kobject *create_xe_sriov_kobj(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_kobj *vkobj;

	xe_sriov_pf_assert_vfid(xe, vfid);

	vkobj = kzalloc(sizeof(*vkobj), GFP_KERNEL);
	if (!vkobj)
		return NULL;

	vkobj->xe = xe;
	vkobj->vfid = vfid;
	return &vkobj->base;
}

static void release_xe_sriov_kobj(struct kobject *kobj)
{
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);

	kfree(vkobj);
}

static ssize_t xe_sriov_dev_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct xe_sriov_dev_attr *vattr  = to_xe_sriov_dev_attr(attr);
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);
	struct xe_device *xe = vkobj->xe;

	if (!vattr->show)
		return -EPERM;

	return vattr->show(xe, buf);
}

static ssize_t xe_sriov_dev_attr_store(struct kobject *kobj, struct attribute *attr,
				       const char *buf, size_t count)
{
	struct xe_sriov_dev_attr *vattr = to_xe_sriov_dev_attr(attr);
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);
	struct xe_device *xe = vkobj->xe;

	if (!vattr->store)
		return -EPERM;

	guard(xe_pm_runtime)(xe);
	return xe_sriov_pf_wait_ready(xe) ?: vattr->store(xe, buf, count);
}

static ssize_t xe_sriov_vf_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct xe_sriov_vf_attr *vattr = to_xe_sriov_vf_attr(attr);
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);
	struct xe_device *xe = vkobj->xe;
	unsigned int vfid = vkobj->vfid;

	xe_sriov_pf_assert_vfid(xe, vfid);

	if (!vattr->show)
		return -EPERM;

	return vattr->show(xe, vfid, buf);
}

static ssize_t xe_sriov_vf_attr_store(struct kobject *kobj, struct attribute *attr,
				      const char *buf, size_t count)
{
	struct xe_sriov_vf_attr *vattr = to_xe_sriov_vf_attr(attr);
	struct xe_sriov_kobj *vkobj = to_xe_sriov_kobj(kobj);
	struct xe_device *xe = vkobj->xe;
	unsigned int vfid = vkobj->vfid;

	xe_sriov_pf_assert_vfid(xe, vfid);

	if (!vattr->store)
		return -EPERM;

	guard(xe_pm_runtime)(xe);
	return xe_sriov_pf_wait_ready(xe) ?: vattr->store(xe, vfid, buf, count);
}

static const struct sysfs_ops xe_sriov_dev_sysfs_ops = {
	.show = xe_sriov_dev_attr_show,
	.store = xe_sriov_dev_attr_store,
};

static const struct sysfs_ops xe_sriov_vf_sysfs_ops = {
	.show = xe_sriov_vf_attr_show,
	.store = xe_sriov_vf_attr_store,
};

static const struct kobj_type xe_sriov_dev_ktype = {
	.release = release_xe_sriov_kobj,
	.sysfs_ops = &xe_sriov_dev_sysfs_ops,
	.default_groups = xe_sriov_dev_attr_groups,
};

static const struct kobj_type xe_sriov_vf_ktype = {
	.release = release_xe_sriov_kobj,
	.sysfs_ops = &xe_sriov_vf_sysfs_ops,
	.default_groups = xe_sriov_vf_attr_groups,
};

static int pf_sysfs_error(struct xe_device *xe, int err, const char *what)
{
	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		xe_sriov_dbg(xe, "Failed to setup sysfs %s (%pe)\n", what, ERR_PTR(err));
	return err;
}

static void pf_sysfs_note(struct xe_device *xe, int err, const char *what)
{
	xe_sriov_dbg(xe, "Failed to setup sysfs %s (%pe)\n", what, ERR_PTR(err));
}

static void action_put_kobject(void *arg)
{
	struct kobject *kobj = arg;

	kobject_put(kobj);
}

static int pf_setup_root(struct xe_device *xe)
{
	struct kobject *parent = &xe->drm.dev->kobj;
	struct kobject *root;
	int err;

	root = create_xe_sriov_kobj(xe, PFID);
	if (!root)
		return pf_sysfs_error(xe, -ENOMEM, "root obj");

	err = devm_add_action_or_reset(xe->drm.dev, action_put_kobject, root);
	if (err)
		return pf_sysfs_error(xe, err, "root action");

	err = kobject_init_and_add(root, &xe_sriov_dev_ktype, parent, "sriov_admin");
	if (err)
		return pf_sysfs_error(xe, err, "root init");

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, !xe->sriov.pf.sysfs.root);
	xe->sriov.pf.sysfs.root = root;
	return 0;
}

static int pf_setup_tree(struct xe_device *xe)
{
	unsigned int totalvfs = xe_sriov_pf_get_totalvfs(xe);
	struct kobject *root, *kobj;
	unsigned int n;
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));
	root = xe->sriov.pf.sysfs.root;

	for (n = 0; n <= totalvfs; n++) {
		kobj = create_xe_sriov_kobj(xe, VFID(n));
		if (!kobj)
			return pf_sysfs_error(xe, -ENOMEM, "tree obj");

		err = devm_add_action_or_reset(xe->drm.dev, action_put_kobject, root);
		if (err)
			return pf_sysfs_error(xe, err, "tree action");

		if (n)
			err = kobject_init_and_add(kobj, &xe_sriov_vf_ktype,
						   root, "vf%u", n);
		else
			err = kobject_init_and_add(kobj, &xe_sriov_vf_ktype,
						   root, "pf");
		if (err)
			return pf_sysfs_error(xe, err, "tree init");

		xe_assert(xe, !xe->sriov.pf.vfs[n].kobj);
		xe->sriov.pf.vfs[n].kobj = kobj;
	}

	return 0;
}

static void action_rm_device_link(void *arg)
{
	struct kobject *kobj = arg;

	sysfs_remove_link(kobj, "device");
}

static int pf_link_pf_device(struct xe_device *xe)
{
	struct kobject *kobj = xe->sriov.pf.vfs[PFID].kobj;
	int err;

	err = sysfs_create_link(kobj, &xe->drm.dev->kobj, "device");
	if (err)
		return pf_sysfs_error(xe, err, "PF device link");

	err = devm_add_action_or_reset(xe->drm.dev, action_rm_device_link, kobj);
	if (err)
		return pf_sysfs_error(xe, err, "PF unlink action");

	return 0;
}

/**
 * xe_sriov_pf_sysfs_init() - Setup PF's SR-IOV sysfs tree.
 * @xe: the PF &xe_device to setup sysfs
 *
 * This function will create additional nodes that will represent PF and VFs
 * devices, each populated with SR-IOV Xe specific attributes.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_sysfs_init(struct xe_device *xe)
{
	int err;

	err = pf_setup_root(xe);
	if (err)
		return err;

	err = pf_setup_tree(xe);
	if (err)
		return err;

	err = pf_link_pf_device(xe);
	if (err)
		return err;

	return 0;
}

/**
 * xe_sriov_pf_sysfs_link_vfs() - Add VF's links in SR-IOV sysfs tree.
 * @xe: the &xe_device where to update sysfs
 * @num_vfs: number of enabled VFs to link
 *
 * This function is specific for the PF driver.
 *
 * This function will add symbolic links between VFs represented in the SR-IOV
 * sysfs tree maintained by the PF and enabled VF PCI devices.
 *
 * The @xe_sriov_pf_sysfs_unlink_vfs() shall be used to remove those links.
 */
void xe_sriov_pf_sysfs_link_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	unsigned int totalvfs = xe_sriov_pf_get_totalvfs(xe);
	struct pci_dev *pf_pdev = to_pci_dev(xe->drm.dev);
	struct pci_dev *vf_pdev = NULL;
	unsigned int n;
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, num_vfs <= totalvfs);

	for (n = 1; n <= num_vfs; n++) {
		vf_pdev = xe_pci_sriov_get_vf_pdev(pf_pdev, VFID(n));
		if (!vf_pdev)
			return pf_sysfs_note(xe, -ENOENT, "VF link");

		err = sysfs_create_link(xe->sriov.pf.vfs[VFID(n)].kobj,
					&vf_pdev->dev.kobj, "device");

		/* must balance xe_pci_sriov_get_vf_pdev() */
		pci_dev_put(vf_pdev);

		if (err)
			return pf_sysfs_note(xe, err, "VF link");
	}
}

/**
 * xe_sriov_pf_sysfs_unlink_vfs() - Remove VF's links from SR-IOV sysfs tree.
 * @xe: the &xe_device where to update sysfs
 * @num_vfs: number of VFs to unlink
 *
 * This function shall be called only on the PF.
 * This function will remove "device" links added by @xe_sriov_sysfs_link_vfs().
 */
void xe_sriov_pf_sysfs_unlink_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	unsigned int n;

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, num_vfs <= xe_sriov_pf_get_totalvfs(xe));

	for (n = 1; n <= num_vfs; n++)
		sysfs_remove_link(xe->sriov.pf.vfs[VFID(n)].kobj, "device");
}
