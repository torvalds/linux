/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_FW_LOG_H__
#define __IVPU_FW_LOG_H__

#include <linux/types.h>

#include <drm/drm_print.h>

#include "ivpu_drv.h"

#define IVPU_FW_LOG_DEFAULT 0
#define IVPU_FW_LOG_DEBUG   1
#define IVPU_FW_LOG_INFO    2
#define IVPU_FW_LOG_WARN    3
#define IVPU_FW_LOG_ERROR   4
#define IVPU_FW_LOG_FATAL   5

extern unsigned int ivpu_log_level;

#define IVPU_FW_VERBOSE_BUFFER_SMALL_SIZE	SZ_1M
#define IVPU_FW_VERBOSE_BUFFER_LARGE_SIZE	SZ_8M
#define IVPU_FW_CRITICAL_BUFFER_SIZE		SZ_512K

void ivpu_fw_log_print(struct ivpu_device *vdev, bool only_new_msgs, struct drm_printer *p);
void ivpu_fw_log_clear(struct ivpu_device *vdev);

static inline void ivpu_fw_log_dump(struct ivpu_device *vdev)
{
	struct drm_printer p = drm_info_printer(vdev->drm.dev);

	ivpu_fw_log_print(vdev, false, &p);
}

#endif /* __IVPU_FW_LOG_H__ */
