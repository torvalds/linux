/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of enc software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and enc permission notice shall be included in
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

#ifndef __DAL_ASIC_CAPABILITY_INTERFACE_H__
#define __DAL_ASIC_CAPABILITY_INTERFACE_H__

/* Include */
#include "include/asic_capability_types.h"

/* Forward declaration */
struct hw_asic_id;

/* ASIC capability */
struct asic_capability {
	struct dc_context *ctx;
	struct asic_caps caps;
	struct asic_stereo_3d_caps stereo_3d_caps;
	struct asic_bugs bugs;
	uint32_t data[ASIC_DATA_MAX_NUMBER];
};

/**
 * Interfaces
 */

/* Create and initialize ASIC capability */
struct asic_capability *dal_asic_capability_create(struct hw_asic_id *init,
		struct dc_context *ctx);

/* Destroy ASIC capability and free memory space */
void dal_asic_capability_destroy(struct asic_capability **cap);

#endif /* __DAL_ASIC_CAPABILITY_INTERFACE_H__ */
