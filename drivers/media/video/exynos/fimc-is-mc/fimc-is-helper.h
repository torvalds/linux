/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Jiyoung Shin<idon.shin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_HELPER_H
#define FIMC_IS_HELPER_H

#include "fimc-is-core.h"
/*
Default setting values
*/

#define DEFAULT_PREVIEW_STILL_WIDTH	1280	/* sensor margin : 16 */
#define DEFAULT_PREVIEW_STILL_HEIGHT  720		/* sensor margin : 12 */
#define DEFAULT_CAPTURE_STILL_WIDTH	640
#define DEFAULT_CAPTURE_STILL_HEIGHT	480
#define DEFAULT_PREVIEW_VIDEO_WIDTH	640
#define DEFAULT_PREVIEW_VIDEO_HEIGHT	480
#define DEFAULT_CAPTURE_VIDEO_WIDTH	640
#define DEFAULT_CAPTURE_VIDEO_HEIGHT	480

#define DEFAULT_PREVIEW_STILL_FRAMERATE	15
#define DEFAULT_CAPTURE_STILL_FRAMERATE	15
#define DEFAULT_PREVIEW_VIDEO_FRAMERATE	30
#define DEFAULT_CAPTURE_VIDEO_FRAMERATE	30

int fimc_is_fw_clear_irq2(struct fimc_is_dev *dev);
int fimc_is_fw_clear_irq1(struct fimc_is_dev *dev, unsigned int intr_pos);
void fimc_is_hw_set_sensor_num(struct fimc_is_dev *dev);
void fimc_is_hw_set_load_setfile(struct fimc_is_dev *dev);
int fimc_is_hw_get_sensor_num(struct fimc_is_dev *dev);
int fimc_is_hw_set_param(struct fimc_is_dev *dev);
int fimc_is_hw_get_param(struct fimc_is_dev *dev, u16 offset);
void fimc_is_hw_set_intgr0_gd0(struct fimc_is_dev *dev);
int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is_dev *dev);
int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is_dev *dev);
void fimc_is_hw_a5_power(struct fimc_is_dev *dev, int on);
void fimc_is_hw_open_sensor(struct fimc_is_dev *dev,
					u32 id, u32 sensor_index);
void fimc_is_hw_set_stream(struct fimc_is_dev *dev, int on);
void fimc_is_hw_set_init(struct fimc_is_dev *dev);
void fimc_is_hw_change_mode(struct fimc_is_dev *dev, int val);
void fimc_is_hw_set_lite(struct fimc_is_dev *dev, u32 width, u32 height);
void fimc_is_hw_diable_wdt(struct fimc_is_dev *dev);
void fimc_is_hw_subip_poweroff(struct fimc_is_dev *dev);
int fimc_is_fw_clear_insr1(struct fimc_is_dev *dev);


#endif

