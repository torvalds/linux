/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef _CORE_STATUS_H_
#define _CORE_STATUS_H_

enum dc_status {
	DC_OK = 1,

	DC_NO_CONTROLLER_RESOURCE = 2,
	DC_NO_STREAM_ENG_RESOURCE = 3,
	DC_NO_CLOCK_SOURCE_RESOURCE = 4,
	DC_FAIL_CONTROLLER_VALIDATE = 5,
	DC_FAIL_ENC_VALIDATE = 6,
	DC_FAIL_ATTACH_SURFACES = 7,
	DC_FAIL_SURFACE_VALIDATE = 8,
	DC_NO_DP_LINK_BANDWIDTH = 9,
	DC_EXCEED_DONGLE_MAX_CLK = 10,
	DC_SURFACE_PIXEL_FORMAT_UNSUPPORTED = 11,
	DC_FAIL_BANDWIDTH_VALIDATE = 12, /* BW and Watermark validation */
	DC_FAIL_SCALING = 13,
	DC_FAIL_CLK_CONSTRAINT = 14,

	DC_ERROR_UNEXPECTED = -1
};

#endif /* _CORE_STATUS_H_ */
