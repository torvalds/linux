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

#define MAX_NAME_LEN 32

#define DC_LOG_ERROR(...) DRM_ERROR(__VA_ARGS__)
#define DC_LOG_WARNING(...) DRM_WARN(__VA_ARGS__)
#define DC_LOG_DEBUG(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_DC(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_DTN(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_SURFACE(...) pr_debug("[SURFACE]:"__VA_ARGS__)
#define DC_LOG_HW_HOTPLUG(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_HW_LINK_TRAINING(...) pr_debug("[HW_LINK_TRAINING]:"__VA_ARGS__)
#define DC_LOG_HW_SET_MODE(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_HW_RESUME_S3(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_HW_AUDIO(...) pr_debug("[HW_AUDIO]:"__VA_ARGS__)
#define DC_LOG_HW_HPD_IRQ(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_MST(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_SCALER(...) pr_debug("[SCALER]:"__VA_ARGS__)
#define DC_LOG_BIOS(...) pr_debug("[BIOS]:"__VA_ARGS__)
#define DC_LOG_BANDWIDTH_CALCS(...) pr_debug("[BANDWIDTH_CALCS]:"__VA_ARGS__)
#define DC_LOG_BANDWIDTH_VALIDATION(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_I2C_AUX(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_SYNC(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_BACKLIGHT(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_FEATURE_OVERRIDE(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_DETECTION_EDID_PARSER(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_DETECTION_DP_CAPS(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_RESOURCE(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_DML(...) pr_debug("[DML]:"__VA_ARGS__)
#define DC_LOG_EVENT_MODE_SET(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_EVENT_DETECTION(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_EVENT_LINK_TRAINING(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_EVENT_LINK_LOSS(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_EVENT_UNDERFLOW(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_IF_TRACE(...) pr_debug("[IF_TRACE]:"__VA_ARGS__)
#define DC_LOG_PERF_TRACE(...) DRM_DEBUG_KMS(__VA_ARGS__)
#define DC_LOG_RETIMER_REDRIVER(...) DRM_DEBUG_KMS(__VA_ARGS__)

struct dal_logger;

struct dc_log_buffer_ctx {
	char *buf;
	size_t pos;
	size_t size;
};

enum dc_log_type {
	LOG_ERROR = 0,
	LOG_WARNING,
	LOG_DEBUG,
	LOG_DC,
	LOG_DTN,
	LOG_SURFACE,
	LOG_HW_HOTPLUG,
	LOG_HW_LINK_TRAINING,
	LOG_HW_SET_MODE,
	LOG_HW_RESUME_S3,
	LOG_HW_AUDIO,
	LOG_HW_HPD_IRQ,
	LOG_MST,
	LOG_SCALER,
	LOG_BIOS,
	LOG_BANDWIDTH_CALCS,
	LOG_BANDWIDTH_VALIDATION,
	LOG_I2C_AUX,
	LOG_SYNC,
	LOG_BACKLIGHT,
	LOG_FEATURE_OVERRIDE,
	LOG_DETECTION_EDID_PARSER,
	LOG_DETECTION_DP_CAPS,
	LOG_RESOURCE,
	LOG_DML,
	LOG_EVENT_MODE_SET,
	LOG_EVENT_DETECTION,
	LOG_EVENT_LINK_TRAINING,
	LOG_EVENT_LINK_LOSS,
	LOG_EVENT_UNDERFLOW,
	LOG_IF_TRACE,
	LOG_PERF_TRACE,
	LOG_DISPLAYSTATS,
	LOG_HDMI_RETIMER_REDRIVER,
	LOG_SECTION_TOTAL_COUNT
};

#define DC_MIN_LOG_MASK ((1 << LOG_ERROR) | \
		(1 << LOG_DETECTION_EDID_PARSER))

#define DC_DEFAULT_LOG_MASK ((1 << LOG_ERROR) | \
		(1 << LOG_WARNING) | \
		(1 << LOG_EVENT_MODE_SET) | \
		(1 << LOG_EVENT_DETECTION) | \
		(1 << LOG_EVENT_LINK_TRAINING) | \
		(1 << LOG_EVENT_LINK_LOSS) | \
		(1 << LOG_EVENT_UNDERFLOW) | \
		(1 << LOG_RESOURCE) | \
		(1 << LOG_FEATURE_OVERRIDE) | \
		(1 << LOG_DETECTION_EDID_PARSER) | \
		(1 << LOG_DC) | \
		(1 << LOG_HW_HOTPLUG) | \
		(1 << LOG_HW_SET_MODE) | \
		(1 << LOG_HW_RESUME_S3) | \
		(1 << LOG_HW_HPD_IRQ) | \
		(1 << LOG_SYNC) | \
		(1 << LOG_BANDWIDTH_VALIDATION) | \
		(1 << LOG_MST) | \
		(1 << LOG_DETECTION_DP_CAPS) | \
		(1 << LOG_BACKLIGHT)) | \
		(1 << LOG_I2C_AUX) | \
		(1 << LOG_IF_TRACE) | \
		(1 << LOG_DTN) /* | \
		(1 << LOG_DEBUG) | \
		(1 << LOG_BIOS) | \
		(1 << LOG_SURFACE) | \
		(1 << LOG_SCALER) | \
		(1 << LOG_DML) | \
		(1 << LOG_HW_LINK_TRAINING) | \
		(1 << LOG_HW_AUDIO)| \
		(1 << LOG_BANDWIDTH_CALCS)*/

#endif /* __DAL_LOGGER_TYPES_H__ */
