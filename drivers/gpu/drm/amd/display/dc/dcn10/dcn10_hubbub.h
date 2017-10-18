/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __DC_HUBBUB_DCN10_H__
#define __DC_HUBBUB_DCN10_H__

#include "core_types.h"

struct dc;

struct dcn_hubbub_wm_set {
	uint32_t wm_set;
	uint32_t data_urgent;
	uint32_t pte_meta_urgent;
	uint32_t sr_enter;
	uint32_t sr_exit;
	uint32_t dram_clk_chanage;
};

struct dcn_hubbub_wm {
	struct dcn_hubbub_wm_set sets[4];
};

void dcn10_update_dchub(
	struct dce_hwseq *hws,
	struct dchub_init_data *dh_data);

void dcn10_log_hw_state(
		struct dc *dc);

void verify_allow_pstate_change_high(
	struct dce_hwseq *hws);

void program_watermarks(
		struct dce_hwseq *hws,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz);

void toggle_watermark_change_req(
		struct dce_hwseq *hws);

void dcn10_hubbub_wm_read_state(struct dce_hwseq *hws,
		struct dcn_hubbub_wm *wm);

#endif
