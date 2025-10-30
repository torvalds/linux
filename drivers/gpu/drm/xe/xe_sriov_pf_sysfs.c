// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_sysfs.h"
#include "xe_sriov_printk.h"

/*
 * /sys/bus/pci/drivers/xe/BDF/
 * :
 * ├── sriov_admin/
 *     ├── ...
 *     ├── pf/
 *     │   ├── ...
 *     │   └── ...
 *     ├── vf1/
 *     │   ├── ...
 *     │   └── ...
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

static const struct attribute_group *xe_sriov_dev_attr_groups[] = {
	NULL
};

/* and VF-level attributes go here */

static const struct attribute_group *xe_sriov_vf_attr_groups[] = {
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

	return vattr->store(xe, buf, count);
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

	return vattr->store(xe, vfid, buf, count);
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

	return 0;
}
