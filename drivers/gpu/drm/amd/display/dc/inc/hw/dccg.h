/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DAL_DCCG_H__
#define __DAL_DCCG_H__

#include "dc_types.h"

struct dccg {
	struct dc_context *ctx;
	const struct dccg_funcs *funcs;

	int ref_dppclk;
};

struct dccg_funcs {
	void (*update_dpp_dto)(struct dccg *dccg,
			int dpp_inst,
			int req_dppclk,
			bool reduce_divider_only);
	void (*get_dccg_ref_freq)(struct dccg *dccg,
			unsigned int xtalin_freq_inKhz,
			unsigned int *dccg_ref_freq_inKhz);
	void (*dccg_init)(struct dccg *dccg);
};

#endif //__DAL_DCCG_H__
