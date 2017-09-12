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

void dal_display_clock_base_set_dp_ref_clock_source(
	struct display_clock *disp_clk,
	enum clock_source_id clk_src)
{/*must be implemented in derived*/

}

void dal_display_clock_base_set_clock_state(struct display_clock *disp_clk,
	struct display_clock_state clk_state)
{
	/*Implemented only in DCE81*/
}
struct display_clock_state dal_display_clock_base_get_clock_state(
	struct display_clock *disp_clk)
{
	/*Implemented only in DCE81*/
	struct display_clock_state state = {0};
	return state;
}
uint32_t dal_display_clock_base_get_dfs_bypass_threshold(
	struct display_clock *disp_clk)
{
	/*Implemented only in DCE81*/
	return 0;
}

bool dal_display_clock_construct_base(
	struct display_clock *base,
	struct dc_context *ctx)
{
	base->ctx = ctx;
	base->id = CLOCK_SOURCE_ID_DCPLL;
	base->min_display_clk_threshold_khz = 0;

/* Initially set current min clocks state to invalid since we
 * cannot make any assumption about PPLIB's initial state. This will be updated
 * by HWSS via SetMinClocksState() on first mode set prior to programming
 * state dependent clocks.*/
	base->cur_min_clks_state = CLOCKS_STATE_INVALID;

	return true;
}

void dal_display_clock_destroy(struct display_clock **disp_clk)
{
	if (!disp_clk || !*disp_clk) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*disp_clk)->funcs->destroy(disp_clk);

	*disp_clk = NULL;
}

bool dal_display_clock_validate(
	struct display_clock *disp_clk,
	struct min_clock_params *params)
{
	return disp_clk->funcs->validate(disp_clk, params);
}

uint32_t dal_display_clock_calculate_min_clock(
	struct display_clock *disp_clk,
	uint32_t path_num,
	struct min_clock_params *params)
{
	return disp_clk->funcs->calculate_min_clock(disp_clk, path_num, params);
}

uint32_t dal_display_clock_get_validation_clock(struct display_clock *disp_clk)
{
	return disp_clk->funcs->get_validation_clock(disp_clk);
}

void dal_display_clock_set_clock(
	struct display_clock *disp_clk,
	uint32_t requested_clock_khz)
{
	disp_clk->funcs->set_clock(disp_clk, requested_clock_khz);
}

uint32_t dal_display_clock_get_clock(struct display_clock *disp_clk)
{
	return disp_clk->funcs->get_clock(disp_clk);
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

uint32_t dal_display_clock_get_dp_ref_clk_frequency(
	struct display_clock *disp_clk)
{
	return disp_clk->funcs->get_dp_ref_clk_frequency(disp_clk);
}

/*the second parameter of "switchreferenceclock" is
 * a dummy argument for all pre dce 6.0 versions*/

void dal_display_clock_switch_reference_clock(
	struct display_clock *disp_clk,
	bool use_external_ref_clk,
	uint32_t requested_clk_khz)
{
	/* TODO: requires Asic Control*/
	/*
	struct ac_pixel_clk_params params;
	struct asic_control *ac =
		dal_adapter_service_get_asic_control(disp_clk->as);
	dc_service_memset(&params, 0, sizeof(struct ac_pixel_clk_params));

	params.tgt_pixel_clk_khz = requested_clk_khz;
	params.flags.SET_EXTERNAL_REF_DIV_SRC = use_external_ref_clk;
	params.pll_id = disp_clk->id;
	dal_asic_control_program_display_engine_pll(ac, &params);
	*/
}

void dal_display_clock_set_dp_ref_clock_source(
	struct display_clock *disp_clk,
	enum clock_source_id clk_src)
{
	disp_clk->funcs->set_dp_ref_clock_source(disp_clk, clk_src);
}

void dal_display_clock_store_max_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state max_clocks_state)
{
	disp_clk->funcs->store_max_clocks_state(disp_clk, max_clocks_state);
}

void dal_display_clock_set_clock_state(
	struct display_clock *disp_clk,
	struct display_clock_state clk_state)
{
	disp_clk->funcs->set_clock_state(disp_clk, clk_state);
}

struct display_clock_state dal_display_clock_get_clock_state(
	struct display_clock *disp_clk)
{
	return disp_clk->funcs->get_clock_state(disp_clk);
}

uint32_t dal_display_clock_get_dfs_bypass_threshold(
	struct display_clock *disp_clk)
{
	return disp_clk->funcs->get_dfs_bypass_threshold(disp_clk);
}

void dal_display_clock_invalid_clock_state(
	struct display_clock *disp_clk)
{
	disp_clk->cur_min_clks_state = CLOCKS_STATE_INVALID;
}

