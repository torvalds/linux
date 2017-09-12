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

#ifndef __DAL_LOGGER_H__
#define __DAL_LOGGER_H__

/* Structure for keeping track of offsets, buffer, etc */

#define DAL_LOGGER_BUFFER_MAX_SIZE 2048

/*Connectivity log needs to output EDID, which needs at lease 256x3 bytes,
 * change log line size to 896 to meet the request.
 */
#define LOG_MAX_LINE_SIZE 896

#include "include/logger_types.h"

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

	uint32_t write_wrap_count;
	uint32_t read_wrap_count;

	uint32_t open_count;

	char *log_buffer;	/* Pointer to malloc'ed buffer */
	uint32_t log_buffer_size; /* Size of circular buffer */

	uint32_t mask; /*array of masks for major elements*/

	union logger_flags flags;
	struct dc_context *ctx;
};

#endif /* __DAL_LOGGER_H__ */
