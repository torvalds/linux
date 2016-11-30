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

#include "dm_services.h"
#include "display_clock.h"

void dal_display_clock_destroy(struct display_clock **disp_clk)
{
	if (!disp_clk || !*disp_clk) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*disp_clk)->funcs->destroy(disp_clk);

	*disp_clk = NULL;
}

bool dal_display_clock_get_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state *clocks_state)
{
	if (!disp_clk->funcs->get_min_clocks_state)
		return false;

	*clocks_state = disp_clk->funcs->get_min_clocks_state(disp_clk);
	return true;
}

bool dal_display_clock_get_required_clocks_state(
	struct display_clock *disp_clk,
	struct state_dependent_clocks *req_clocks,
	enum clocks_state *clocks_state)
{
	if (!disp_clk->funcs->get_required_clocks_state)
		return false;

	*clocks_state = disp_clk->funcs->get_required_clocks_state(
			disp_clk, req_clocks);
	return true;
}

bool dal_display_clock_set_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state clocks_state)
{
	if (!disp_clk->funcs->set_min_clocks_state)
		return false;

	disp_clk->funcs->set_min_clocks_state(disp_clk, clocks_state);
	return true;
}

