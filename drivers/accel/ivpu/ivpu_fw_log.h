/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_FW_LOG_H__
#define __IVPU_FW_LOG_H__

#include <linux/types.h>

#include "ivpu_drv.h"

#define IVPU_FW_LOG_DEFAULT 0
#define IVPU_FW_LOG_DEBUG   1
#define IVPU_FW_LOG_INFO    2
#define IVPU_FW_LOG_WARN    3
#define IVPU_FW_LOG_ERROR   4
#define IVPU_FW_LOG_FATAL   5

#define IVPU_FW_VERBOSE_BUFFER_SMALL_SIZE	SZ_1M
#define IVPU_FW_VERBOSE_BUFFER_LARGE_SIZE	SZ_8M
#define IVPU_FW_CRITICAL_BUFFER_SIZE		SZ_512K

extern unsigned int ivpu_fw_log_level;

void ivpu_fw_log_print(struct ivpu_device *vdev, bool only_new_msgs, struct drm_printer *p);
void ivpu_fw_log_mark_read(struct ivpu_device *vdev);
void ivpu_fw_log_reset(struct ivpu_device *vdev);


#endif /* __IVPU_FW_LOG_H__ */
