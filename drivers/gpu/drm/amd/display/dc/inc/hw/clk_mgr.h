/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#ifndef __DAL_CLK_MGR_H__
#define __DAL_CLK_MGR_H__

#include "dc.h"

/* Public interfaces */

struct clk_states {
	uint32_t dprefclk_khz;
};

struct clk_mgr_funcs {
	/*
	 * This function should set new clocks based on the input "safe_to_lower".
	 * If safe_to_lower == false, then only clocks which are to be increased
	 * should changed.
	 * If safe_to_lower == true, then only clocks which are to be decreased
	 * should be changed.
	 */
	void (*update_clocks)(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower);

	int (*get_dp_ref_clk_frequency)(struct clk_mgr *clk_mgr);

	void (*init_clocks)(struct clk_mgr *clk_mgr);

	void (*enable_pme_wa) (struct clk_mgr *clk_mgr);
};

void dce121_clock_patch_xgmi_ss_info(struct clk_mgr *clk_mgr_base);

struct clk_mgr {
	struct dc_context *ctx;
	struct clk_mgr_funcs *funcs;
	struct dc_clocks clks;
	int dprefclk_khz; // Used by program pixel clock in clock source funcs, need to figureout where this goes
};

/* forward declarations */
struct dccg;

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg);

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr);

#endif /* __DAL_CLK_MGR_H__ */
