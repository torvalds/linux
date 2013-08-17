/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * All rights reserved.
 */
#ifndef FIMC_IS_CONFIG_H_
#define FIMC_IS_CONFIG_H_

#include "fimc-is.h"
#define FIMC_IS_CONFIG_TIMEOUT		3000 /* ms */

#define DEFAULT_PREVIEW_STILL_WIDTH	640
#define DEFAULT_PREVIEW_STILL_HEIGHT	480
#define DEFAULT_CAPTURE_STILL_WIDTH	640
#define DEFAULT_CAPTURE_STILL_HEIGHT	480
#define DEFAULT_PREVIEW_VIDEO_WIDTH	640
#define DEFAULT_PREVIEW_VIDEO_HEIGHT	480
#define DEFAULT_CAPTURE_VIDEO_WIDTH	640
#define DEFAULT_CAPTURE_VIDEO_HEIGHT	480

#define DEFAULT_PREVIEW_STILL_FRAMERATE	30
#define DEFAULT_CAPTURE_STILL_FRAMERATE	15
#define DEFAULT_PREVIEW_VIDEO_FRAMERATE	30
#define DEFAULT_CAPTURE_VIDEO_FRAMERATE	30

int fimc_is_hw_get_sensor_format(struct fimc_is *is);
void fimc_is_hw_set_init(struct fimc_is *dev);
int fimc_is_hw_initialize(struct fimc_is *is);

int fimc_is_hw_get_sensor_max_framerate(struct fimc_is *is);
void fimc_is_set_init_value(struct fimc_is *is);

int _is_hw_update_param(struct fimc_is *is);
void __is_get_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf);
void __is_set_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf);
void __is_set_sensor(struct fimc_is *is, int fps);
void __is_set_isp_aa_ae(struct fimc_is *is);
void __is_set_isp_flash(struct fimc_is *is, u32 cmd, u32 redeye);
void __is_set_isp_awb(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_effect(struct fimc_is *is, u32 cmd);
void __is_set_isp_iso(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_adjust(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_metering(struct fimc_is *is, u32 id, u32 val);
void __is_set_isp_afc(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_drc_control(struct fimc_is *is, u32 val);
void __is_set_fd_control(struct fimc_is *is, u32 val);
void __is_set_fd_config_maxface(struct fimc_is *is, u32 val);
void __is_set_fd_config_rollangle(struct fimc_is *is, u32 val);
void __is_set_fd_config_yawangle(struct fimc_is *is, u32 val);
void __is_set_fd_config_smilemode(struct fimc_is *is, u32 val);
void __is_set_fd_config_blinkmode(struct fimc_is *is, u32 val);
void __is_set_fd_config_eyedetect(struct fimc_is *is, u32 val);
void __is_set_fd_config_mouthdetect(struct fimc_is *is, u32 val);
void __is_set_fd_config_orientation(struct fimc_is *is, u32 val);
void __is_set_fd_config_orientation_val(struct fimc_is *is, u32 val);
void __is_set_isp_aa_af_mode(struct fimc_is *is, int cmd);
void __is_set_isp_aa_af_start_stop(struct fimc_is *is, int cmd);
#endif  /* FIMC_IS_CONFIG_H_ */
