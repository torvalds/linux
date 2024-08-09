// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_hw_engine_group.h"

static void
hw_engine_group_free(struct drm_device *drm, void *arg)
{
	struct xe_hw_engine_group *group = arg;

	kfree(group);
}

static struct xe_hw_engine_group *
hw_engine_group_alloc(struct xe_device *xe)
{
	struct xe_hw_engine_group *group;
	int err;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	init_rwsem(&group->mode_sem);
	INIT_LIST_HEAD(&group->exec_queue_list);

	err = drmm_add_action_or_reset(&xe->drm, hw_engine_group_free, group);
	if (err)
		return ERR_PTR(err);

	return group;
}

/**
 * xe_hw_engine_setup_groups() - Setup the hw engine groups for the gt
 * @gt: The gt for which groups are setup
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_hw_engine_setup_groups(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_hw_engine_group *group_rcs_ccs, *group_bcs, *group_vcs_vecs;
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	group_rcs_ccs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_rcs_ccs)) {
		err = PTR_ERR(group_rcs_ccs);
		goto err_group_rcs_ccs;
	}

	group_bcs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_bcs)) {
		err = PTR_ERR(group_bcs);
		goto err_group_bcs;
	}

	group_vcs_vecs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_vcs_vecs)) {
		err = PTR_ERR(group_vcs_vecs);
		goto err_group_vcs_vecs;
	}

	for_each_hw_engine(hwe, gt, id) {
		switch (hwe->class) {
		case XE_ENGINE_CLASS_COPY:
			hwe->hw_engine_group = group_bcs;
			break;
		case XE_ENGINE_CLASS_RENDER:
		case XE_ENGINE_CLASS_COMPUTE:
			hwe->hw_engine_group = group_rcs_ccs;
			break;
		case XE_ENGINE_CLASS_VIDEO_DECODE:
		case XE_ENGINE_CLASS_VIDEO_ENHANCE:
			hwe->hw_engine_group = group_vcs_vecs;
			break;
		case XE_ENGINE_CLASS_OTHER:
			break;
		default:
			drm_warn(&xe->drm, "NOT POSSIBLE");
		}
	}

	return 0;

err_group_vcs_vecs:
	kfree(group_vcs_vecs);
err_group_bcs:
	kfree(group_bcs);
err_group_rcs_ccs:
	kfree(group_rcs_ccs);

	return err;
}
