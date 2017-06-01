/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_MPCC_H__
#define __DC_MPCC_H__

#include "dc_hw_types.h"

struct mpcc_cfg {
	int top_dpp_id;
	int bot_mpcc_id;
	int opp_id;
	bool per_pixel_alpha;
	bool top_of_tree;
};

struct mpcc {
	const struct mpcc_funcs *funcs;
	struct dc_context *ctx;
	int inst;
};

struct mpcc_funcs {
	void (*set)(struct mpcc *mpcc, struct mpcc_cfg *cfg);
	void (*wait_for_idle)(struct mpcc *mpcc);
	void (*set_bg_color)( struct mpcc *mpcc, struct tg_color *bg_color);
};

#endif
