/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_ISP_VIDEO__
#define FIMC_ISP_VIDEO__

#include <media/videobuf2-core.h>
#include "fimc-isp.h"

#ifdef CONFIG_VIDEO_EXYNOS4_ISP_DMA_CAPTURE
int fimc_isp_video_device_register(struct fimc_isp *isp,
				struct v4l2_device *v4l2_dev,
				enum v4l2_buf_type type);

void fimc_isp_video_device_unregister(struct fimc_isp *isp,
				enum v4l2_buf_type type);

void fimc_isp_video_irq_handler(struct fimc_is *is);
#else
static inline void fimc_isp_video_irq_handler(struct fimc_is *is)
{
}

static inline int fimc_isp_video_device_register(struct fimc_isp *isp,
						struct v4l2_device *v4l2_dev,
						enum v4l2_buf_type type)
{
	return 0;
}

void fimc_isp_video_device_unregister(struct fimc_isp *isp,
				enum v4l2_buf_type type)
{
}
#endif /* !CONFIG_VIDEO_EXYNOS4_ISP_DMA_CAPTURE */

#endif /* FIMC_ISP_VIDEO__ */
