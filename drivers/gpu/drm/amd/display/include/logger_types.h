/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_LOGGER_TYPES_H__
#define __DAL_LOGGER_TYPES_H__

#include "os_types.h"

#define DC_LOG_ERROR(...) drm_err((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_WARNING(...) drm_warn((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DEBUG(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DC(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_INFO(...) drm_info((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_SURFACE(...) pr_debug("[SURFACE]:"__VA_ARGS__)
#define DC_LOG_HW_HOTPLUG(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_HW_LINK_TRAINING(...) pr_debug("[HW_LINK_TRAINING]:"__VA_ARGS__)
#define DC_LOG_HW_RESUME_S3(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_HW_AUDIO(...) pr_debug("[HW_AUDIO]:"__VA_ARGS__)
#define DC_LOG_HW_HPD_IRQ(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_MST(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_SCALER(...) pr_debug("[SCALER]:"__VA_ARGS__)
#define DC_LOG_BIOS(...) pr_debug("[BIOS]:"__VA_ARGS__)
#define DC_LOG_BANDWIDTH_CALCS(...) pr_debug("[BANDWIDTH_CALCS]:"__VA_ARGS__)
#define DC_LOG_BANDWIDTH_VALIDATION(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_SYNC(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_BACKLIGHT(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DETECTION_EDID_PARSER(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DETECTION_DP_CAPS(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DML(...) pr_debug("[DML]:"__VA_ARGS__)
#define DC_LOG_EVENT_MODE_SET(...) drm_dbg_kms((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_EVENT_DETECTION(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_EVENT_LINK_TRAINING(...) \
	drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_EVENT_LINK_LOSS(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_IF_TRACE(...) pr_debug("[IF_TRACE]:"__VA_ARGS__)
#define DC_LOG_PERF_TRACE(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_RETIMER_REDRIVER(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_GAMMA(...) pr_debug("[GAMMA]:"__VA_ARGS__)
#define DC_LOG_ALL_GAMMA(...) pr_debug("[GAMMA]:"__VA_ARGS__)
#define DC_LOG_ALL_TF_CHANNELS(...) pr_debug("[GAMMA]:"__VA_ARGS__)
#define DC_LOG_DSC(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_SMU(...) pr_debug("[SMU_MSG]:"__VA_ARGS__)
#define DC_LOG_DWB(...) drm_dbg((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_DP2(...) drm_dbg_dp((DC_LOGGER)->dev, __VA_ARGS__)
#define DC_LOG_AUTO_DPM_TEST(...) pr_debug("[AutoDPMTest]: "__VA_ARGS__)
#define DC_LOG_IPS(...) pr_debug("[IPS]: "__VA_ARGS__)
#define DC_LOG_MALL(...) pr_debug("[MALL]:"__VA_ARGS__)
#define DC_LOG_REGISTER_READ(...) pr_debug("[REGISTER_READ]: "__VA_ARGS__)
#define DC_LOG_REGISTER_WRITE(...) pr_debug("[REGISTER_WRITE]: "__VA_ARGS__)

struct dc_log_buffer_ctx {
	char *buf;
	size_t pos;
	size_t size;
};

struct dal_logger {
	struct drm_device *dev;
};

#endif /* __DAL_LOGGER_TYPES_H__ */
