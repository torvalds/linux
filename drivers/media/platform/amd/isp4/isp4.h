/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_H_
#define _ISP4_H_

#include <drm/amd/isp.h>
#include "isp4_subdev.h"

struct isp4_device {
	struct v4l2_device v4l2_dev;
	struct isp4_subdev isp_subdev;
	struct media_device mdev;
};

void isp4_intr_enable(struct isp4_subdev *isp_subdev, u32 index, bool enable);

#endif /* _ISP4_H_ */
