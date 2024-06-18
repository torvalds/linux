// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/errno.h>
#include <linux/sysctl.h>

#include <drm/xe_drm.h>

#include "xe_perf.h"

u32 xe_perf_stream_paranoid = true;
static struct ctl_table_header *sysctl_header;

/**
 * xe_perf_ioctl - The top level perf layer ioctl
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_perf_param
 * @file: @drm_file
 *
 * The function is called for different perf streams types and allows execution
 * of different operations supported by those perf stream types.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_perf_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_xe_perf_param *arg = data;

	if (arg->extensions)
		return -EINVAL;

	switch (arg->perf_type) {
	default:
		return -EINVAL;
	}
}

static struct ctl_table perf_ctl_table[] = {
	{
	 .procname = "perf_stream_paranoid",
	 .data = &xe_perf_stream_paranoid,
	 .maxlen = sizeof(xe_perf_stream_paranoid),
	 .mode = 0644,
	 .proc_handler = proc_dointvec_minmax,
	 .extra1 = SYSCTL_ZERO,
	 .extra2 = SYSCTL_ONE,
	 },
	{}
};

/**
 * xe_perf_sysctl_register - Register "perf_stream_paranoid" sysctl
 *
 * Normally only superuser/root can access perf counter data. However,
 * superuser can set perf_stream_paranoid sysctl to 0 to allow non-privileged
 * users to also access perf data.
 *
 * Return: always returns 0
 */
int xe_perf_sysctl_register(void)
{
	sysctl_header = register_sysctl("dev/xe", perf_ctl_table);
	return 0;
}

/**
 * xe_perf_sysctl_unregister - Unregister "perf_stream_paranoid" sysctl
 */
void xe_perf_sysctl_unregister(void)
{
	unregister_sysctl_table(sysctl_header);
}
