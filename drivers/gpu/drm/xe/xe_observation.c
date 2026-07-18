// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/errno.h>
#include <linux/perf_event.h>
#include <linux/sysctl.h>

#include <uapi/drm/xe_drm.h>

#include "xe_eu_stall.h"
#include "xe_oa.h"
#include "xe_observation.h"

static u32 xe_observation_paranoid = true;
static struct ctl_table_header *sysctl_header;

/**
 * xe_observation_paranoid_check - Gate access to xe observation streams.
 *
 * When the xe-specific observation_paranoid sysctl is enabled (the
 * default), defer to perf_allow_cpu() so that access is governed by the
 * same policy as system-wide perf CPU events: kernel.perf_event_paranoid
 * plus the security_perf_event_open() LSM hook. When the sysctl has been
 * cleared by a privileged user, observation is open to all callers.
 *
 * Return: 0 if access is permitted, a negative errno otherwise.
 */
int xe_observation_paranoid_check(void)
{
	if (!xe_observation_paranoid)
		return 0;

	return perf_allow_cpu();
}

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

static int xe_eu_stall_ioctl(struct drm_device *dev, struct drm_xe_observation_param *arg,
			     struct drm_file *file)
{
	switch (arg->observation_op) {
	case DRM_XE_OBSERVATION_OP_STREAM_OPEN:
		return xe_eu_stall_stream_open(dev, arg->param, file);
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
	case DRM_XE_OBSERVATION_TYPE_EU_STALL:
		return xe_eu_stall_ioctl(dev, arg, file);
	default:
		return -EINVAL;
	}
}

static const struct ctl_table observation_ctl_table[] = {
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
 * xe_observation_sysctl_register - Register the observation_paranoid sysctl
 *
 * When dev.xe.observation_paranoid is set (the default), access to
 * observation streams follows the system-wide perf_allow_cpu() policy:
 * kernel.perf_event_paranoid plus the security_perf_event_open() LSM
 * hook. A privileged user can clear the sysctl to bypass that gate and
 * allow unprivileged access to observation data.
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
