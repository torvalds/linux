/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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


#include "dcn30/dcn30_hubbub.h"
#include "dcn401_hubbub.h"
#include "dm_services.h"
#include "reg_helper.h"


#define CTX \
	hubbub2->base.ctx
#define DC_LOGGER \
	hubbub2->base.ctx->logger
#define REG(reg)\
	hubbub2->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub2->shifts->field_name, hubbub2->masks->field_name

static void dcn401_init_crb(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_GET(DCHUBBUB_DET0_CTRL, DET0_SIZE_CURRENT,
		&hubbub2->det0_size);

	REG_GET(DCHUBBUB_DET1_CTRL, DET1_SIZE_CURRENT,
		&hubbub2->det1_size);

	REG_GET(DCHUBBUB_DET2_CTRL, DET2_SIZE_CURRENT,
		&hubbub2->det2_size);

	REG_GET(DCHUBBUB_DET3_CTRL, DET3_SIZE_CURRENT,
		&hubbub2->det3_size);

	REG_GET(DCHUBBUB_COMPBUF_CTRL, COMPBUF_SIZE_CURRENT,
		&hubbub2->compbuf_size_segments);

	REG_SET(COMPBUF_RESERVED_SPACE, 0,
			COMPBUF_RESERVED_SPACE_64B, hubbub2->pixel_chunk_size / 32); // 256 64Bytes
}

bool hubbub401_program_urgent_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* Repeat for water mark set A and B */
	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.urgent > hubbub2->watermarks.dcn4x.a.urgent) {
		hubbub2->watermarks.dcn4x.a.urgent = watermarks->dcn4x.a.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, watermarks->dcn4x.a.urgent);
		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.a.urgent, watermarks->dcn4x.a.urgent);
	} else if (watermarks->dcn4x.a.urgent < hubbub2->watermarks.dcn4x.a.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.a.frac_urg_bw_flip
			> hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip = watermarks->dcn4x.a.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, watermarks->dcn4x.a.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.a.frac_urg_bw_flip
			< hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.frac_urg_bw_nom
			> hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom = watermarks->dcn4x.a.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, watermarks->dcn4x.a.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.a.frac_urg_bw_nom
			< hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.frac_urg_bw_mall
			> hubbub2->watermarks.dcn4x.a.frac_urg_bw_mall) {
		hubbub2->watermarks.dcn4x.a.frac_urg_bw_mall = watermarks->dcn4x.a.frac_urg_bw_mall;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_MALL_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_MALL_A, watermarks->dcn4x.a.frac_urg_bw_mall);
	} else if (watermarks->dcn4x.a.frac_urg_bw_mall < hubbub2->watermarks.dcn4x.a.frac_urg_bw_mall)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem = watermarks->dcn4x.a.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, watermarks->dcn4x.a.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.a.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem > hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem) {
		hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem = watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A, watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem);
	} else if (watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem < hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem)
		wm_pending = true;


	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.urgent > hubbub2->watermarks.dcn4x.b.urgent) {
		hubbub2->watermarks.dcn4x.b.urgent = watermarks->dcn4x.b.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, watermarks->dcn4x.b.urgent);
		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.b.urgent, watermarks->dcn4x.b.urgent);
	} else if (watermarks->dcn4x.b.urgent < hubbub2->watermarks.dcn4x.b.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.b.frac_urg_bw_flip
			> hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip = watermarks->dcn4x.b.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, watermarks->dcn4x.b.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.b.frac_urg_bw_flip
			< hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.frac_urg_bw_nom
			> hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom = watermarks->dcn4x.b.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, watermarks->dcn4x.b.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.b.frac_urg_bw_nom
			< hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.frac_urg_bw_mall
			> hubbub2->watermarks.dcn4x.b.frac_urg_bw_mall) {
		hubbub2->watermarks.dcn4x.b.frac_urg_bw_mall = watermarks->dcn4x.b.frac_urg_bw_mall;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_MALL_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_MALL_B, watermarks->dcn4x.b.frac_urg_bw_mall);
	} else if (watermarks->dcn4x.b.frac_urg_bw_mall < hubbub2->watermarks.dcn4x.b.frac_urg_bw_mall)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem = watermarks->dcn4x.b.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, watermarks->dcn4x.b.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.b.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem > hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem) {
		hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem = watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B, watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem);
	} else if (watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem < hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem)
		wm_pending = true;

	return wm_pending;
}

bool hubbub401_program_stutter_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.sr_enter
			> hubbub2->watermarks.dcn4x.a.sr_enter) {
		hubbub2->watermarks.dcn4x.a.sr_enter =
				watermarks->dcn4x.a.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, watermarks->dcn4x.a.sr_enter);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.a.sr_enter, watermarks->dcn4x.a.sr_enter);
		// On dGPU Z states are N/A, so program all other 3 Stutter Enter wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A, watermarks->dcn4x.a.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A, watermarks->dcn4x.a.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A, watermarks->dcn4x.a.sr_enter);

	} else if (watermarks->dcn4x.a.sr_enter
			< hubbub2->watermarks.dcn4x.a.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.sr_exit
			> hubbub2->watermarks.dcn4x.a.sr_exit) {
		hubbub2->watermarks.dcn4x.a.sr_exit =
				watermarks->dcn4x.a.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, watermarks->dcn4x.a.sr_exit);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.a.sr_exit, watermarks->dcn4x.a.sr_exit);
		// On dGPU Z states are N/A, so program all other 3 Stutter Exit wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A, watermarks->dcn4x.a.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A, watermarks->dcn4x.a.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A, watermarks->dcn4x.a.sr_exit);

	} else if (watermarks->dcn4x.a.sr_exit
			< hubbub2->watermarks.dcn4x.a.sr_exit)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.sr_enter
			> hubbub2->watermarks.dcn4x.b.sr_enter) {
		hubbub2->watermarks.dcn4x.b.sr_enter =
				watermarks->dcn4x.b.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, watermarks->dcn4x.b.sr_enter);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.b.sr_enter, watermarks->dcn4x.b.sr_enter);
		// On dGPU Z states are N/A, so program all other 3 Stutter Enter wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B, watermarks->dcn4x.b.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B, watermarks->dcn4x.b.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B, watermarks->dcn4x.b.sr_enter);

	} else if (watermarks->dcn4x.b.sr_enter
			< hubbub2->watermarks.dcn4x.b.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.sr_exit
			> hubbub2->watermarks.dcn4x.b.sr_exit) {
		hubbub2->watermarks.dcn4x.b.sr_exit =
				watermarks->dcn4x.b.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, watermarks->dcn4x.b.sr_exit);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.b.sr_exit, watermarks->dcn4x.b.sr_exit);
		// On dGPU Z states are N/A, so program all other 3 Stutter Exit wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B, watermarks->dcn4x.b.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B, watermarks->dcn4x.b.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B, watermarks->dcn4x.b.sr_exit);

	} else if (watermarks->dcn4x.b.sr_exit
			< hubbub2->watermarks.dcn4x.b.sr_exit)
		wm_pending = true;

	return wm_pending;
}


bool hubbub401_program_pstate_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS */
	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.uclk_pstate
			> hubbub2->watermarks.dcn4x.a.uclk_pstate) {
		hubbub2->watermarks.dcn4x.a.uclk_pstate =
				watermarks->dcn4x.a.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, watermarks->dcn4x.a.uclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.uclk_pstate, watermarks->dcn4x.a.uclk_pstate);
	} else if (watermarks->dcn4x.a.uclk_pstate
			< hubbub2->watermarks.dcn4x.a.uclk_pstate)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.uclk_pstate
			> hubbub2->watermarks.dcn4x.b.uclk_pstate) {
		hubbub2->watermarks.dcn4x.b.uclk_pstate =
				watermarks->dcn4x.b.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, watermarks->dcn4x.b.uclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.uclk_pstate, watermarks->dcn4x.b.uclk_pstate);
	} else if (watermarks->dcn4x.b.uclk_pstate
			< hubbub2->watermarks.dcn4x.b.uclk_pstate)
		wm_pending = true;

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS1 (DUMMY_PSTATE/TEMP_READ/PPT) */
	if (safe_to_lower || watermarks->dcn4x.a.temp_read_or_ppt
			> hubbub2->watermarks.dcn4x.a.temp_read_or_ppt) {
		hubbub2->watermarks.dcn4x.a.temp_read_or_ppt =
				watermarks->dcn4x.a.temp_read_or_ppt;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A, watermarks->dcn4x.a.temp_read_or_ppt);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK1_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.temp_read_or_ppt, watermarks->dcn4x.a.temp_read_or_ppt);
	} else if (watermarks->dcn4x.a.temp_read_or_ppt
			< hubbub2->watermarks.dcn4x.a.temp_read_or_ppt)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.temp_read_or_ppt
			> hubbub2->watermarks.dcn4x.b.temp_read_or_ppt) {
		hubbub2->watermarks.dcn4x.b.temp_read_or_ppt =
				watermarks->dcn4x.b.temp_read_or_ppt;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, watermarks->dcn4x.b.temp_read_or_ppt);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.temp_read_or_ppt, watermarks->dcn4x.b.temp_read_or_ppt);
	} else if (watermarks->dcn4x.b.temp_read_or_ppt
			< hubbub2->watermarks.dcn4x.b.temp_read_or_ppt)
		wm_pending = true;

	/* Section for FCLK_PSTATE_CHANGE_WATERMARKS */
	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.fclk_pstate
			> hubbub2->watermarks.dcn4x.a.fclk_pstate) {
		hubbub2->watermarks.dcn4x.a.fclk_pstate =
				watermarks->dcn4x.a.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, watermarks->dcn4x.a.fclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.fclk_pstate, watermarks->dcn4x.a.fclk_pstate);
	} else if (watermarks->dcn4x.a.fclk_pstate
			< hubbub2->watermarks.dcn4x.a.fclk_pstate)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.fclk_pstate
			> hubbub2->watermarks.dcn4x.b.fclk_pstate) {
		hubbub2->watermarks.dcn4x.b.fclk_pstate =
				watermarks->dcn4x.b.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, watermarks->dcn4x.b.fclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.fclk_pstate, watermarks->dcn4x.b.fclk_pstate);
	} else if (watermarks->dcn4x.b.fclk_pstate
			< hubbub2->watermarks.dcn4x.b.fclk_pstate)
		wm_pending = true;

	/* Section for FCLK_CHANGE_WATERMARKS1 (DUMMY_PSTATE/TEMP_READ/PPT) */
	if (safe_to_lower || watermarks->dcn4x.a.temp_read_or_ppt
			> hubbub2->watermarks.dcn4x.a.temp_read_or_ppt) {
		hubbub2->watermarks.dcn4x.a.temp_read_or_ppt =
				watermarks->dcn4x.a.temp_read_or_ppt;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A, watermarks->dcn4x.a.temp_read_or_ppt);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK1_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.temp_read_or_ppt, watermarks->dcn4x.a.temp_read_or_ppt);
	} else if (watermarks->dcn4x.a.temp_read_or_ppt
			< hubbub2->watermarks.dcn4x.a.temp_read_or_ppt)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.temp_read_or_ppt
			> hubbub2->watermarks.dcn4x.b.temp_read_or_ppt) {
		hubbub2->watermarks.dcn4x.b.temp_read_or_ppt =
				watermarks->dcn4x.b.temp_read_or_ppt;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, watermarks->dcn4x.b.temp_read_or_ppt);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.temp_read_or_ppt, watermarks->dcn4x.b.temp_read_or_ppt);
	} else if (watermarks->dcn4x.b.temp_read_or_ppt
			< hubbub2->watermarks.dcn4x.b.temp_read_or_ppt)
		wm_pending = true;

	return wm_pending;
}


bool hubbub401_program_usr_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.usr
			> hubbub2->watermarks.dcn4x.a.usr) {
		hubbub2->watermarks.dcn4x.a.usr = watermarks->dcn4x.a.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, watermarks->dcn4x.a.usr);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.usr, watermarks->dcn4x.a.usr);
	} else if (watermarks->dcn4x.a.usr
			< hubbub2->watermarks.dcn4x.a.usr)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.usr
			> hubbub2->watermarks.dcn4x.b.usr) {
		hubbub2->watermarks.dcn4x.b.usr = watermarks->dcn4x.b.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, watermarks->dcn4x.b.usr);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.usr, watermarks->dcn4x.b.usr);
	} else if (watermarks->dcn4x.b.usr
			< hubbub2->watermarks.dcn4x.b.usr)
		wm_pending = true;

	return wm_pending;
}


static bool hubbub401_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;

	if (hubbub401_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub401_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub401_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub401_program_usr_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	/*
	 * The DCHub arbiter has a mechanism to dynamically rate limit the DCHub request stream to the fabric.
	 * If the memory controller is fully utilized and the DCHub requestors are
	 * well ahead of their amortized schedule, then it is safe to prevent the next winner
	 * from being committed and sent to the fabric.
	 * The utilization of the memory controller is approximated by ensuring that
	 * the number of outstanding requests is greater than a threshold specified
	 * by the ARB_MIN_REQ_OUTSTANDING. To determine that the DCHub requestors are well ahead of the amortized
	 * schedule, the slack of the next winner is compared with the ARB_SAT_LEVEL in DLG RefClk cycles.
	 *
	 * TODO: Revisit request limit after figure out right number. request limit for RM isn't decided yet,
	 *  set maximum value (0x1FF) to turn off it for now.
	 */
	/*REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
		REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0x1FF);
	*/

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	hubbub32_force_usr_retraining_allow(hubbub, hubbub->ctx->dc->debug.force_usr_allow);

	return wm_pending;
}

/* Copy values from WM set A to all other sets */
static void hubbub401_init_watermarks(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t reg;

	reg = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_MALL_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_MALL_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	reg = REG_READ(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	reg = REG_READ(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, reg);
}

static void hubbub401_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_hubbub_wm_set *s;

	memset(wm, 0, sizeof(struct dcn_hubbub_wm));

	s = &wm->sets[0];
	s->wm_set = 0;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A,
			 DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A,
			 DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, &s->fclk_pstate_change);

	s = &wm->sets[1];
	s->wm_set = 1;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B,
			DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B,
			DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, &s->fclk_pstate_change);
}

bool hubbub401_dcc_support_swizzle(
		enum swizzle_mode_addr3_values swizzle,
		unsigned int plane_pitch,
		unsigned int bytes_per_element,
		enum segment_order *segment_order_horz,
		enum segment_order *segment_order_vert)
{
	bool swizzle_supported = false;

	switch (swizzle) {
	case DC_ADDR3_SW_LINEAR:
		if ((plane_pitch * bytes_per_element) % 256 == 0)
			swizzle_supported = true;
		break;
	case DC_ADDR3_SW_64KB_2D:
	case DC_ADDR3_SW_256KB_2D:
		swizzle_supported = true;
		break;
	default:
		swizzle_supported = false;
		break;
	}

	if (swizzle_supported) {
		if (bytes_per_element == 1) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__non_contiguous;
			return true;
		}
		if (bytes_per_element == 2) {
			*segment_order_horz = segment_order__non_contiguous;
			*segment_order_vert = segment_order__contiguous;
			return true;
		}
		if (bytes_per_element == 4) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__non_contiguous;
			return true;
		}
		if (bytes_per_element == 8) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__non_contiguous;
			return true;
		}
	}

	return false;
}

bool hubbub401_dcc_support_pixel_format(
		enum surface_pixel_format format,
		unsigned int *plane0_bpe,
		unsigned int *plane1_bpe)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		*plane0_bpe = 2;
		*plane1_bpe = 0;
		return true;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		*plane0_bpe = 1;
		*plane1_bpe = 2;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
		*plane0_bpe = 4;
		*plane1_bpe = 0;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		*plane0_bpe = 4;
		*plane1_bpe = 1;
		return true;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		*plane0_bpe = 2;
		*plane1_bpe = 4;
		return true;
	case SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
	case SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
	case SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
		*plane0_bpe = 4;
		*plane1_bpe = 0;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		*plane0_bpe = 8;
		*plane1_bpe = 0;
		return true;
	default:
		return false;
	}
}

void hubbub401_get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
		unsigned int bytes_per_element)
{
	if (bytes_per_element == 1) {
		*blk256_width = 16;
		*blk256_height = 16;
	} else if (bytes_per_element == 2) {
		*blk256_width = 16;
		*blk256_height = 8;
	} else if (bytes_per_element == 4) {
		*blk256_width = 8;
		*blk256_height = 8;
	} else if (bytes_per_element == 8) {
		*blk256_width = 8;
		*blk256_height = 4;
	}
}

void hubbub401_det_request_size(
		unsigned int detile_buf_size,
		enum surface_pixel_format format,
		unsigned int p0_height,
		unsigned int p0_width,
		unsigned int p0_bpe,
		unsigned int p1_height,
		unsigned int p1_width,
		unsigned int p1_bpe,
		bool *p0_req128_horz_wc,
		bool *p0_req128_vert_wc,
		bool *p1_req128_horz_wc,
		bool *p1_req128_vert_wc)
{
	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int p0_swath_bytes_horz_wc, p0_swath_bytes_vert_wc;
	unsigned int p1_swath_bytes_horz_wc, p1_swath_bytes_vert_wc;

	//For plane0
	hubbub401_get_blk256_size(&blk256_width, &blk256_height, p0_bpe);

	p0_swath_bytes_horz_wc = p0_width * blk256_height * p0_bpe;
	p0_swath_bytes_vert_wc = p0_height * blk256_width * p0_bpe;

	*p0_req128_horz_wc = (2 * p0_swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*p0_req128_vert_wc = (2 * p0_swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	/*For dual planes needs to be considered together */
	if (p1_bpe) {
		hubbub401_get_blk256_size(&blk256_width, &blk256_height, p1_bpe);

		p1_swath_bytes_horz_wc = p1_width * blk256_height * p1_bpe;
		p1_swath_bytes_vert_wc = p1_height * blk256_width * p1_bpe;

		switch (format) {
		default:
			/* No any adjustment needed*/
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
			/* Packing at the ratio of 3:2 is supported before the detile buffer
			 * for YUV420 video with 10bpc (P010). Need to adjust for that.
			 */
			p0_swath_bytes_horz_wc = (((p0_swath_bytes_horz_wc * 2) / 3 + 255) / 256) * 256;
			p0_swath_bytes_vert_wc = (((p0_swath_bytes_vert_wc * 2) / 3 + 255) / 256) * 256;
			p1_swath_bytes_horz_wc = (((p1_swath_bytes_horz_wc * 2) / 3 + 255) / 256) * 256;
			p1_swath_bytes_vert_wc = (((p1_swath_bytes_vert_wc * 2) / 3 + 255) / 256) * 256;
			break;
		}

		*p0_req128_horz_wc = *p1_req128_horz_wc = (2 * p0_swath_bytes_horz_wc +
			2 * p1_swath_bytes_horz_wc <= detile_buf_size) ?
				false : /* full 256B request */
				true; /* half 128B request */

		*p0_req128_vert_wc = *p1_req128_vert_wc = (2 * p0_swath_bytes_vert_wc +
			2 * p1_swath_bytes_vert_wc <= detile_buf_size) ?
				false : /* full 256B request */
				true; /* half 128B request */

		/* If 128B requests are true, meaning 2 full swaths of data cannot fit
		 * in de-tile buffer, check if one plane can use 256B request while
		 * the other plane is using 128B requests
		 */
		if (*p0_req128_horz_wc) {
			// If ratio around 1:1 between p0 and p1 try to recalulate if p0 can use 256B
			if (p0_swath_bytes_horz_wc <= p1_swath_bytes_horz_wc + p1_swath_bytes_horz_wc / 2) {

				*p0_req128_horz_wc = (2 * p0_swath_bytes_horz_wc + p1_swath_bytes_horz_wc <= detile_buf_size) ?
					false : /* full 256B request */
					true; /* half 128b request */

			} else {
				/* ratio about 2:1 between p0 and p1, try to recalulate if p1 can use 256B */
				*p1_req128_horz_wc = (p0_swath_bytes_horz_wc + 2 * p1_swath_bytes_horz_wc <= detile_buf_size) ?
					false : /* full 256B request */
					true; /* half 128b request */
			}
		}

		if (*p0_req128_vert_wc) {
			// If ratio around 1:1 between p0 and p1 try to recalulate if p0 can use 256B
			if (p0_swath_bytes_vert_wc <= p1_swath_bytes_vert_wc + p1_swath_bytes_vert_wc / 2) {

				*p0_req128_vert_wc = (2 * p0_swath_bytes_vert_wc + p1_swath_bytes_vert_wc <= detile_buf_size) ?
					false : /* full 256B request */
					true; /* half 128b request */

			} else {
				/* ratio about 2:1 between p0 and p1, try to recalulate if p1 can use 256B */
				*p1_req128_vert_wc = (p0_swath_bytes_vert_wc + 2 * p1_swath_bytes_vert_wc <= detile_buf_size) ?
					false : /* full 256B request */
					true; /* half 128b request */
			}
		}
	}
}
bool hubbub401_get_dcc_compression_cap(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	struct dc *dc = hubbub->ctx->dc;
	const unsigned int max_dcc_plane_width = dc->caps.dcc_plane_width_limit;
	/* DCN4_Programming_Guide_DCHUB.docx, Section 5.11.2.2 */
	enum dcc_control dcc_control;
	unsigned int plane0_bpe, plane1_bpe;
	enum segment_order segment_order_horz, segment_order_vert;
	enum segment_order p1_segment_order_horz, p1_segment_order_vert;
	bool req128_horz_wc, req128_vert_wc;
	unsigned int plane0_width = 0, plane0_height = 0, plane1_width = 0, plane1_height = 0;
	bool p1_req128_horz_wc, p1_req128_vert_wc, is_dual_plane;

	memset(output, 0, sizeof(*output));

	if (dc->debug.disable_dcc == DCC_DISABLE)
		return false;

	/* Conservatively disable DCC for cases where ODM4:1 may be required. */
	if (max_dcc_plane_width != 0 &&
			(input->surface_size.width > max_dcc_plane_width || input->plane1_size.width > max_dcc_plane_width))
		return false;

	switch (input->format) {
	default:
		is_dual_plane = false;

		plane1_width = 0;
		plane1_height = 0;

		if (input->surface_size.width > 6144 + 16)
			plane0_width = 6160;
		else
			plane0_width = input->surface_size.width;

		if (input->surface_size.height > 6144 + 16)
			plane0_height = 6160;
		else
			plane0_height = input->surface_size.height;

		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		is_dual_plane = true;

		if (input->surface_size.width > 7680 + 16)
			plane0_width = 7696;
		else
			plane0_width = input->surface_size.width;

		if (input->surface_size.height > 4320 + 16)
			plane0_height = 4336;
		else
			plane0_height = input->surface_size.height;

		if (input->plane1_size.width > 7680 + 16)
			plane1_width = 7696 / 2;
		else
			plane1_width = input->plane1_size.width;

		if (input->plane1_size.height > 4320 + 16)
			plane1_height = 4336 / 2;
		else
			plane1_height = input->plane1_size.height;

		break;

	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		is_dual_plane = true;

		if (input->surface_size.width > 5120 + 16)
			plane0_width = 5136;
		else
			plane0_width = input->surface_size.width;

		if (input->surface_size.height > 5120 + 16)
			plane0_height = 5136;
		else
			plane0_height = input->surface_size.height;

		if (input->plane1_size.width > 5120 + 16)
			plane1_width = 5136;
		else
			plane1_width = input->plane1_size.width;

		if (input->plane1_size.height > 5120 + 16)
			plane1_height = 5136;
		else
			plane1_height = input->plane1_size.height;

		break;
	}

	if (!hubbub->funcs->dcc_support_pixel_format_plane0_plane1(input->format,
			&plane0_bpe, &plane1_bpe))
		return false;

	/* Find plane0 DCC Controls */
	if (!is_dual_plane) {

		if (!hubbub->funcs->dcc_support_swizzle_addr3(input->swizzle_mode_addr3,
				input->plane0_pitch, plane0_bpe,
				&segment_order_horz, &segment_order_vert))
			return false;

		hubbub401_det_request_size(TO_DCN20_HUBBUB(hubbub)->detile_buf_size, input->format,
				plane0_height, plane0_width, plane0_bpe,
				plane1_height, plane1_width, plane1_bpe,
				&req128_horz_wc, &req128_vert_wc, &p1_req128_horz_wc, &p1_req128_vert_wc);

		if (!req128_horz_wc && !req128_vert_wc) {
			dcc_control = dcc_control__256_256;
		} else if (input->scan == SCAN_DIRECTION_HORIZONTAL) {
			if (!req128_horz_wc)
				dcc_control = dcc_control__256_256;
			else if (segment_order_horz == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else if (input->scan == SCAN_DIRECTION_VERTICAL) {
			if (!req128_vert_wc)
				dcc_control = dcc_control__256_256;
			else if (segment_order_vert == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else {
			if ((req128_horz_wc &&
				segment_order_horz == segment_order__non_contiguous) ||
				(req128_vert_wc &&
				segment_order_vert == segment_order__non_contiguous))
				/* access_dir not known, must use most constraining */
				dcc_control = dcc_control__256_64;
			else
				/* req128 is true for either horz and vert
				 * but segment_order is contiguous
				 */
				dcc_control = dcc_control__256_128;
		}

		if (dc->debug.disable_dcc == DCC_HALF_REQ_DISALBE &&
			dcc_control != dcc_control__256_256)
			return false;

		switch (dcc_control) {
		case dcc_control__256_256:
			output->grph.rgb.dcc_controls.dcc_256_256 = 1;
			output->grph.rgb.dcc_controls.dcc_256_128 = 1;
			output->grph.rgb.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_128:
			output->grph.rgb.dcc_controls.dcc_256_128 = 1;
			output->grph.rgb.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_64:
			output->grph.rgb.dcc_controls.dcc_256_64 = 1;
			break;
		default:
			/* Shouldn't get here */
			ASSERT(0);
			break;
		}
	} else {
		/* For dual plane cases, need to examine both planes together */
		if (!hubbub->funcs->dcc_support_swizzle_addr3(input->swizzle_mode_addr3,
				input->plane0_pitch, plane0_bpe,
				&segment_order_horz, &segment_order_vert))
			return false;

		if (!hubbub->funcs->dcc_support_swizzle_addr3(input->swizzle_mode_addr3,
			input->plane1_pitch, plane1_bpe,
			&p1_segment_order_horz, &p1_segment_order_vert))
			return false;

		hubbub401_det_request_size(TO_DCN20_HUBBUB(hubbub)->detile_buf_size, input->format,
				plane0_height, plane0_width, plane0_bpe,
				plane1_height, plane1_width, plane1_bpe,
				&req128_horz_wc, &req128_vert_wc, &p1_req128_horz_wc, &p1_req128_vert_wc);

		/* Determine Plane 0 DCC Controls */
		if (!req128_horz_wc && !req128_vert_wc) {
			dcc_control = dcc_control__256_256;
		} else if (input->scan == SCAN_DIRECTION_HORIZONTAL) {
			if (!req128_horz_wc)
				dcc_control = dcc_control__256_256;
			else if (segment_order_horz == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else if (input->scan == SCAN_DIRECTION_VERTICAL) {
			if (!req128_vert_wc)
				dcc_control = dcc_control__256_256;
			else if (segment_order_vert == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else {
			if ((req128_horz_wc &&
				segment_order_horz == segment_order__non_contiguous) ||
				(req128_vert_wc &&
				segment_order_vert == segment_order__non_contiguous))
				/* access_dir not known, must use most constraining */
				dcc_control = dcc_control__256_64;
			else
				/* req128 is true for either horz and vert
				 * but segment_order is contiguous
				 */
				dcc_control = dcc_control__256_128;
		}

		switch (dcc_control) {
		case dcc_control__256_256:
			output->video.luma.dcc_controls.dcc_256_256 = 1;
			output->video.luma.dcc_controls.dcc_256_128 = 1;
			output->video.luma.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_128:
			output->video.luma.dcc_controls.dcc_256_128 = 1;
			output->video.luma.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_64:
			output->video.luma.dcc_controls.dcc_256_64 = 1;
			break;
		default:
			ASSERT(0);
			break;
		}

		/* Determine Plane 1 DCC Controls */
		if (!p1_req128_horz_wc && !p1_req128_vert_wc) {
			dcc_control = dcc_control__256_256;
		} else if (input->scan == SCAN_DIRECTION_HORIZONTAL) {
			if (!p1_req128_horz_wc)
				dcc_control = dcc_control__256_256;
			else if (p1_segment_order_horz == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else if (input->scan == SCAN_DIRECTION_VERTICAL) {
			if (!p1_req128_vert_wc)
				dcc_control = dcc_control__256_256;
			else if (p1_segment_order_vert == segment_order__contiguous)
				dcc_control = dcc_control__256_128;
			else
				dcc_control = dcc_control__256_64;
		} else {
			if ((p1_req128_horz_wc &&
				p1_segment_order_horz == segment_order__non_contiguous) ||
				(p1_req128_vert_wc &&
				p1_segment_order_vert == segment_order__non_contiguous))
				/* access_dir not known, must use most constraining */
				dcc_control = dcc_control__256_64;
			else
				/* req128 is true for either horz and vert
				 * but segment_order is contiguous
				 */
				dcc_control = dcc_control__256_128;
		}

		switch (dcc_control) {
		case dcc_control__256_256:
			output->video.chroma.dcc_controls.dcc_256_256 = 1;
			output->video.chroma.dcc_controls.dcc_256_128 = 1;
			output->video.chroma.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_128:
			output->video.chroma.dcc_controls.dcc_256_128 = 1;
			output->video.chroma.dcc_controls.dcc_256_64 = 1;
			break;
		case dcc_control__256_64:
			output->video.chroma.dcc_controls.dcc_256_64 = 1;
			break;
		default:
			ASSERT(0);
			break;
		}
	}

	output->capable = true;
	return true;
}

static void dcn401_program_det_segments(struct hubbub *hubbub, int hubp_inst, unsigned det_buffer_size_seg)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	switch (hubp_inst) {
	case 0:
		REG_UPDATE(DCHUBBUB_DET0_CTRL,
					DET0_SIZE, det_buffer_size_seg);
		hubbub2->det0_size = det_buffer_size_seg;
		break;
	case 1:
		REG_UPDATE(DCHUBBUB_DET1_CTRL,
					DET1_SIZE, det_buffer_size_seg);
		hubbub2->det1_size = det_buffer_size_seg;
		break;
	case 2:
		REG_UPDATE(DCHUBBUB_DET2_CTRL,
					DET2_SIZE, det_buffer_size_seg);
		hubbub2->det2_size = det_buffer_size_seg;
		break;
	case 3:
		REG_UPDATE(DCHUBBUB_DET3_CTRL,
					DET3_SIZE, det_buffer_size_seg);
		hubbub2->det3_size = det_buffer_size_seg;
		break;
	default:
		break;
	}
	if (hubbub2->det0_size + hubbub2->det1_size + hubbub2->det2_size
			+ hubbub2->det3_size + hubbub2->compbuf_size_segments > hubbub2->crb_size_segs) {
		/* This may happen during seamless transition from ODM 2:1 to ODM4:1 */
		DC_LOG_WARNING("CRB Config Warning: DET size (%d,%d,%d,%d) + Compbuf size (%d) >  CRB segments (%d)\n",
						hubbub2->det0_size, hubbub2->det1_size, hubbub2->det2_size, hubbub2->det3_size,
						hubbub2->compbuf_size_segments, hubbub2->crb_size_segs);
	}
}

static void dcn401_program_compbuf_segments(struct hubbub *hubbub, unsigned compbuf_size_seg, bool safe_to_increase)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	unsigned int cur_compbuf_size_seg = 0;

	if (safe_to_increase || compbuf_size_seg <= hubbub2->compbuf_size_segments) {
		if (compbuf_size_seg > hubbub2->compbuf_size_segments) {
			REG_WAIT(DCHUBBUB_DET0_CTRL, DET0_SIZE_CURRENT, hubbub2->det0_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET1_CTRL, DET1_SIZE_CURRENT, hubbub2->det1_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET2_CTRL, DET2_SIZE_CURRENT, hubbub2->det2_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET3_CTRL, DET3_SIZE_CURRENT, hubbub2->det3_size, 1, 100);
		}
		/* Should never be hit, if it is we have an erroneous hw config*/
		ASSERT(hubbub2->det0_size + hubbub2->det1_size + hubbub2->det2_size
				+ hubbub2->det3_size + compbuf_size_seg <= hubbub2->crb_size_segs);
		REG_UPDATE(DCHUBBUB_COMPBUF_CTRL, COMPBUF_SIZE, compbuf_size_seg);
		hubbub2->compbuf_size_segments = compbuf_size_seg;

		ASSERT(REG_GET(DCHUBBUB_COMPBUF_CTRL, CONFIG_ERROR, &cur_compbuf_size_seg) && !cur_compbuf_size_seg);
	}
}

static void dcn401_wait_for_det_update(struct hubbub *hubbub, int hubp_inst)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	switch (hubp_inst) {
	case 0:
		REG_WAIT(DCHUBBUB_DET0_CTRL, DET0_SIZE_CURRENT, hubbub2->det0_size, 1, 100000); /* 1 vupdate at 10hz */
		break;
	case 1:
		REG_WAIT(DCHUBBUB_DET1_CTRL, DET1_SIZE_CURRENT, hubbub2->det1_size, 1, 100000);
		break;
	case 2:
		REG_WAIT(DCHUBBUB_DET2_CTRL, DET2_SIZE_CURRENT, hubbub2->det2_size, 1, 100000);
		break;
	case 3:
		REG_WAIT(DCHUBBUB_DET3_CTRL, DET3_SIZE_CURRENT, hubbub2->det3_size, 1, 100000);
		break;
	default:
		break;
	}
}

static void dcn401_program_timeout_thresholds(struct hubbub *hubbub, struct dml2_display_arb_regs *arb_regs)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* request backpressure and outstanding return threshold (unused)*/
	//REG_UPDATE(DCHUBBUB_TIMEOUT_DETECTION_CTRL1, DCHUBBUB_TIMEOUT_REQ_STALL_THRESHOLD, arb_regs->req_stall_threshold);

	/* P-State stall threshold */
	REG_UPDATE(DCHUBBUB_TIMEOUT_DETECTION_CTRL2, DCHUBBUB_TIMEOUT_PSTATE_STALL_THRESHOLD, arb_regs->pstate_stall_threshold);
}

static const struct hubbub_funcs hubbub4_01_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub3_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle_addr3 = hubbub401_dcc_support_swizzle,
	.dcc_support_pixel_format_plane0_plane1 = hubbub401_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub401_get_dcc_compression_cap,
	.wm_read_state = hubbub401_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub401_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.verify_allow_pstate_change_high = NULL,
	.force_wm_propagate_to_pipes = hubbub32_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub401_init_watermarks,
	.init_crb = dcn401_init_crb,
	.hubbub_read_state = hubbub2_read_state,
	.force_usr_retraining_allow = hubbub32_force_usr_retraining_allow,
	.set_request_limit = hubbub32_set_request_limit,
	.program_det_segments = dcn401_program_det_segments,
	.program_compbuf_segments = dcn401_program_compbuf_segments,
	.wait_for_det_update = dcn401_wait_for_det_update,
	.program_timeout_thresholds = dcn401_program_timeout_thresholds,
};

void hubbub401_construct(struct dcn20_hubbub *hubbub2,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{
	hubbub2->base.ctx = ctx;
	hubbub2->base.funcs = &hubbub4_01_funcs;
	hubbub2->regs = hubbub_regs;
	hubbub2->shifts = hubbub_shift;
	hubbub2->masks = hubbub_mask;

	hubbub2->detile_buf_size = det_size_kb * 1024;
	hubbub2->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub2->crb_size_segs = config_return_buffer_size_kb / DCN4_01_CRB_SEGMENT_SIZE_KB;
}
