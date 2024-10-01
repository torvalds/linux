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

#ifndef DAL_DC_DCE_DCE112_CLK_MGR_H_
#define DAL_DC_DCE_DCE112_CLK_MGR_H_


void dce112_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr);

/* functions shared with other clk mgr */
int dce112_set_clock(struct clk_mgr *clk_mgr_base, int requested_clk_khz);
int dce112_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_clk_khz);
int dce112_set_dprefclk(struct clk_mgr_internal *clk_mgr);

#endif /* DAL_DC_DCE_DCE112_CLK_MGR_H_ */
