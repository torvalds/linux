
/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef __DSCC_TYPES_H__
#define __DSCC_TYPES_H__

#include <drm/display/drm_dsc.h>

#ifndef NUM_BUF_RANGES
#define NUM_BUF_RANGES 15
#endif

struct dsc_pps_rc_range {
	int range_min_qp;
	int range_max_qp;
	int range_bpg_offset;
};

struct dsc_parameters {
	struct drm_dsc_config pps;

	/* Additional parameters for register programming */
	uint32_t bytes_per_pixel; /* In u3.28 format */
	uint32_t rc_buffer_model_size;
};

int dscc_compute_dsc_parameters(const struct drm_dsc_config *pps, struct dsc_parameters *dsc_params);

#endif

