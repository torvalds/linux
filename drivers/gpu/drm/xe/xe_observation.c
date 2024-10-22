// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/errno.h>
#include <linux/sysctl.h>

#include <drm/xe_drm.h>

#include "xe_oa.h"
#include "xe_observation.h"

u32 xe_observation_paranoid = true;
static struct ctl_table_header *sysctl_header;

static int xe_oa_ioctl(struct drm_device *dev, struct drm_xe_observation_param *arg,
		       struct drm_file *file)
{
	switch (arg->observation_op) {
	case DRM_XE_OBSERVATION_OP_STREAM_OPEN:
		return xe_oa_stream_open_ioctl(dev, arg->param, file);
	case DRM_XE_OBSERVATION_OP_ADD_CONFIG:
		return xe_oa_add_config_ioctl(dev, arg->param, file);
	case DRM_XE_OBSERVATION_OP_REMOVE_CONFIG:
		return xe_oa_remove_config_ioctl(dev, arg->param, file);
	default:
		return -EINVAL;
	}
}

/**
 * xe_observation_ioctl - The top level observation layer ioctl
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_observation_param
 * @file: @drm_file
 *
 * The function is called for different observation streams types and
 * allows execution of different operations supported by those stream
 * types.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_observation_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_xe_observation_param *arg = data;

	if (arg->extensions)
		return -EINVAL;

	switch (arg->observation_type) {
	case DRM_XE_OBSERVATION_TYPE_OA:
		return xe_oa_ioctl(dev, arg, file);
	default:
		return -EINVAL;
	}
}

static struct ctl_table observation_ctl_table[] = {
	{
	 .procname = "observation_paranoid",
	 .data = &xe_observation_paranoid,
	 .maxlen = sizeof(xe_observation_paranoid),
	 .mode = 0644,
	 .proc_handler = proc_dointvec_minmax,
	 .extra1 = SYSCTL_ZERO,
	 .extra2 = SYSCTL_ONE,
	 },
};

/**
 * xe_observation_sysctl_register - Register xe_observation_paranoid sysctl
 *
 * Normally only superuser/root can access observation stream
 * data. However, superuser can set xe_observation_paranoid sysctl to 0 to
 * allow non-privileged users to also access observation data.
 *
 * Return: always returns 0
 */
int xe_observation_sysctl_register(void)
{
	sysctl_header = register_sysctl("dev/xe", observation_ctl_table);
	return 0;
}

/**
 * xe_observation_sysctl_unregister - Unregister xe_observation_paranoid sysctl
 */
void xe_observation_sysctl_unregister(void)
{
	unregister_sysctl_table(sysctl_header);
}
