/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_COREDUMP_H__
#define __IVPU_COREDUMP_H__

#include <drm/drm_print.h>

#include "ivpu_drv.h"
#include "ivpu_fw_log.h"

#ifdef CONFIG_DEV_COREDUMP
void ivpu_dev_coredump(struct ivpu_device *vdev);
#else
static inline void ivpu_dev_coredump(struct ivpu_device *vdev)
{
	struct drm_printer p = drm_info_printer(vdev->drm.dev);

	ivpu_fw_log_print(vdev, false, &p);
}
#endif

#endif /* __IVPU_COREDUMP_H__ */
