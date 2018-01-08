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

struct dal_logger;

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

union logger_flags {
	struct {
		uint32_t ENABLE_CONSOLE:1; /* Print to console */
		uint32_t ENABLE_BUFFER:1; /* Print to buffer */
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};

struct log_entry {
	struct dal_logger *logger;
	enum dc_log_type type;

	char *buf;
	uint32_t buf_offset;
	uint32_t max_buf_bytes;
};

/**
* Structure for enumerating log types
*/
struct dc_log_type_info {
	enum dc_log_type type;
	char name[MAX_NAME_LEN];
};

/* Structure for keeping track of offsets, buffer, etc */

#define DAL_LOGGER_BUFFER_MAX_SIZE 2048

/*Connectivity log needs to output EDID, which needs at lease 256x3 bytes,
 * change log line size to 896 to meet the request.
 */
#define LOG_MAX_LINE_SIZE 896

struct dal_logger {

	/* How far into the circular buffer has been read by dsat
	 * Read offset should never cross write offset. Write \0's to
	 * read data just to be sure?
	 */
	uint32_t buffer_read_offset;

	/* How far into the circular buffer we have written
	 * Write offset should never cross read offset
	 */
	uint32_t buffer_write_offset;

	uint32_t open_count;

	char *log_buffer;	/* Pointer to malloc'ed buffer */
	uint32_t log_buffer_size; /* Size of circular buffer */

	uint32_t mask; /*array of masks for major elements*/

	union logger_flags flags;
	struct dc_context *ctx;
};

#endif /* __DAL_LOGGER_TYPES_H__ */
