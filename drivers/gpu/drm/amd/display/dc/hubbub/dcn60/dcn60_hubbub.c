// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn60_hubbub.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed31_32.h"

#define CTX \
	hubbub2->base.ctx
#define DC_LOGGER \
	hubbub2->base.ctx->logger
#define REG(reg)\
	hubbub2->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub2->shifts->field_name, hubbub2->masks->field_name

static void dcn60_init_crb(struct hubbub *hubbub)
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

static void dcn60_program_det_segments(struct hubbub *hubbub, int hubp_inst, unsigned int det_buffer_size_seg)
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

static void dcn60_program_compbuf_segments(struct hubbub *hubbub, unsigned int compbuf_size_seg, bool safe_to_increase)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

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
	}
}

static void dcn60_wait_for_det_update(struct hubbub *hubbub, int hubp_inst)
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

static bool hubbub60_program_urgent_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	(void)refclk_mhz;
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

	if (safe_to_lower ||
		watermarks->dcn4x.a.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem = watermarks->dcn4x.a.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, watermarks->dcn4x.a.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.a.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower ||
		watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem > hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem) {
		hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem = watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A, watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem);
	} else if (watermarks->dcn4x.a.refcyc_per_meta_trip_to_mem <
								hubbub2->watermarks.dcn4x.a.refcyc_per_meta_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.buffer_fullness > hubbub2->watermarks.dcn4x.a.buffer_fullness) {
		hubbub2->watermarks.dcn4x.a.buffer_fullness = watermarks->dcn4x.a.buffer_fullness;
		REG_SET(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_A, 0,
			DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_A, watermarks->dcn4x.a.buffer_fullness);
	} else if (watermarks->dcn4x.a.buffer_fullness < hubbub2->watermarks.dcn4x.a.buffer_fullness)
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

	if (safe_to_lower ||
		watermarks->dcn4x.b.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem = watermarks->dcn4x.b.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, watermarks->dcn4x.b.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.b.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower ||
		watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem > hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem) {
		hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem = watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B, watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem);
	} else if (watermarks->dcn4x.b.refcyc_per_meta_trip_to_mem <
								hubbub2->watermarks.dcn4x.b.refcyc_per_meta_trip_to_mem)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.buffer_fullness > hubbub2->watermarks.dcn4x.b.buffer_fullness) {
		hubbub2->watermarks.dcn4x.b.buffer_fullness = watermarks->dcn4x.b.buffer_fullness;
		REG_SET(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_B, 0,
			DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_B, watermarks->dcn4x.b.buffer_fullness);
	} else if (watermarks->dcn4x.b.buffer_fullness < hubbub2->watermarks.dcn4x.b.buffer_fullness)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub60_program_pstate_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	(void)refclk_mhz;
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS, used for UCLK change (UCLK/FCLK PState) */
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

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS1 (Reserved set, can be used for FCLK Pstate only) */
	if (safe_to_lower || watermarks->dcn4x.a.fclk_pstate
			> hubbub2->watermarks.dcn4x.a.fclk_pstate) {
		hubbub2->watermarks.dcn4x.a.fclk_pstate =
				watermarks->dcn4x.a.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A, watermarks->dcn4x.a.fclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK1_A calculated =%d\n"
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
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, watermarks->dcn4x.b.fclk_pstate);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.fclk_pstate, watermarks->dcn4x.b.fclk_pstate);
	} else if (watermarks->dcn4x.b.fclk_pstate
			< hubbub2->watermarks.dcn4x.b.fclk_pstate)
		wm_pending = true;

	/* Section for FCLK_PSTATE_CHANGE_WATERMARKS, instance 0 used for G7 PPT */
	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.ppt
			> hubbub2->watermarks.dcn4x.a.ppt) {
		hubbub2->watermarks.dcn4x.a.ppt =
				watermarks->dcn4x.a.ppt;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, watermarks->dcn4x.a.ppt);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.ppt, watermarks->dcn4x.a.ppt);
	} else if (watermarks->dcn4x.a.ppt
			< hubbub2->watermarks.dcn4x.a.ppt)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.ppt
			> hubbub2->watermarks.dcn4x.b.ppt) {
		hubbub2->watermarks.dcn4x.b.ppt =
				watermarks->dcn4x.b.ppt;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, watermarks->dcn4x.b.ppt);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.ppt, watermarks->dcn4x.b.ppt);
	} else if (watermarks->dcn4x.b.ppt
			< hubbub2->watermarks.dcn4x.b.ppt)
		wm_pending = true;

	/* Section for FCLK_CHANGE_WATERMARKS1, instance 1 used for G7 Temp Read */
	if (safe_to_lower || watermarks->dcn4x.a.temp_read
			> hubbub2->watermarks.dcn4x.a.temp_read) {
		hubbub2->watermarks.dcn4x.a.temp_read =
				watermarks->dcn4x.a.temp_read;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A, watermarks->dcn4x.a.temp_read);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK1_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.a.temp_read, watermarks->dcn4x.a.temp_read);
	} else if (watermarks->dcn4x.a.temp_read
			< hubbub2->watermarks.dcn4x.a.temp_read)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.temp_read
			> hubbub2->watermarks.dcn4x.b.temp_read) {
		hubbub2->watermarks.dcn4x.b.temp_read =
				watermarks->dcn4x.b.temp_read;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, watermarks->dcn4x.b.temp_read);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->dcn4x.b.temp_read, watermarks->dcn4x.b.temp_read);
	} else if (watermarks->dcn4x.b.temp_read
			< hubbub2->watermarks.dcn4x.b.temp_read)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub60_program_stutter_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	(void)refclk_mhz;
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
		// On dGPU Z states are N/A, so program unused Stutter Enter wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A, watermarks->dcn4x.a.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A, watermarks->dcn4x.a.sr_enter);

	}
	/* possible 2nd stutter watermark. PMFW to choose which one to use */
	if (safe_to_lower || watermarks->dcn4x.a.sr_enter_low_power
			> hubbub2->watermarks.dcn4x.a.sr_enter_low_power) {
		hubbub2->watermarks.dcn4x.a.sr_enter_low_power =
				watermarks->dcn4x.a.sr_enter_low_power;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A, watermarks->dcn4x.a.sr_enter_low_power);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK1_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.a.sr_enter_low_power, watermarks->dcn4x.a.sr_enter_low_power);
	}

	if (watermarks->dcn4x.a.sr_enter
			< hubbub2->watermarks.dcn4x.a.sr_enter ||
			watermarks->dcn4x.a.sr_enter_low_power
			< hubbub2->watermarks.dcn4x.a.sr_enter_low_power)
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
		// On dGPU Z states are N/A, so program unused Stutter Exit wm A with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A, watermarks->dcn4x.a.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A, watermarks->dcn4x.a.sr_exit);
	}
	/* possible 2nd stutter exit watermark. PMFW to choose which one to use */
	if (safe_to_lower || watermarks->dcn4x.a.sr_exit_low_power
			> hubbub2->watermarks.dcn4x.a.sr_exit_low_power) {
		hubbub2->watermarks.dcn4x.a.sr_exit_low_power =
				watermarks->dcn4x.a.sr_exit_low_power;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A, watermarks->dcn4x.a.sr_exit_low_power);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK1_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.a.sr_exit_low_power, watermarks->dcn4x.a.sr_exit_low_power);
	}

	if (watermarks->dcn4x.a.sr_exit
			< hubbub2->watermarks.dcn4x.a.sr_exit ||
			watermarks->dcn4x.a.sr_exit_low_power
			< hubbub2->watermarks.dcn4x.a.sr_exit_low_power)
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
		// On dGPU Z states are N/A, so program unused Stutter Enter wm B with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B, watermarks->dcn4x.b.sr_enter);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B, watermarks->dcn4x.b.sr_enter);
	}
	/* possible 2nd stutter enter watermark. PMFW to choose which one to use */
	if (safe_to_lower || watermarks->dcn4x.b.sr_enter_low_power
			> hubbub2->watermarks.dcn4x.b.sr_enter_low_power) {
		hubbub2->watermarks.dcn4x.b.sr_enter_low_power =
				watermarks->dcn4x.b.sr_enter_low_power;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B, watermarks->dcn4x.b.sr_enter_low_power);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.b.sr_enter_low_power, watermarks->dcn4x.b.sr_enter_low_power);
	}

	if (watermarks->dcn4x.b.sr_enter
			< hubbub2->watermarks.dcn4x.b.sr_enter ||
			watermarks->dcn4x.b.sr_enter_low_power
			< hubbub2->watermarks.dcn4x.b.sr_enter_low_power)
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
		// On dGPU Z states are N/A, so program unused Stutter Exit wm B with the same value
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B, watermarks->dcn4x.b.sr_exit);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B, watermarks->dcn4x.b.sr_exit);
	}
	/* possible 2nd stutter exit watermark. PMFW to choose which one to use */
	if (safe_to_lower || watermarks->dcn4x.b.sr_exit_low_power
			> hubbub2->watermarks.dcn4x.b.sr_exit_low_power) {
		hubbub2->watermarks.dcn4x.b.sr_exit_low_power =
				watermarks->dcn4x.b.sr_exit_low_power;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B, watermarks->dcn4x.b.sr_exit_low_power);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK1_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->dcn4x.b.sr_exit_low_power, watermarks->dcn4x.b.sr_exit_low_power);
	}

	if (watermarks->dcn4x.b.sr_exit
			< hubbub2->watermarks.dcn4x.b.sr_exit ||
			watermarks->dcn4x.b.sr_exit_low_power
			< hubbub2->watermarks.dcn4x.b.sr_exit_low_power)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub60_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;

	if (hubbub60_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub60_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub60_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	return wm_pending;
}

/* Copy values from WM set A to all other sets */
static void hubbub60_init_watermarks(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t reg;

	reg = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, reg);

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

	reg = REG_READ(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	reg = REG_READ(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	reg = REG_READ(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B, reg);

	reg = REG_READ(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_B, reg);
}

static void hubbub60_force_wm_propagate_to_pipes(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
		DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, hubbub2->watermarks.dcn4x.a.urgent);

}

static void hubbub60_wm_read_state(struct hubbub *hubbub,
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

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A,
		DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A, &s->fclk_pstate_change);

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

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B,
		DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B, &s->fclk_pstate_change);
}

/**
 * @brief Resets the performance monitor for the DCN 6.0 hubbub.
 *
 * This function resets the performance monitoring counters and related
 * state for the specified hubbub instance. It is typically called to
 * clear performance statistics before starting a new measurement period.
 *
 * @param hubbub Pointer to the hubbub structure to reset performance monitoring
 * for.
 */
static void hubbub60_perfmon_reset(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_WRITE(DC_PERFMON5_PERFMON_CNTL, 0);
	REG_WRITE(DC_PERFMON5_PERFMON_CNTL2, 0);
	REG_WRITE(DC_PERFMON5_PERFCOUNTER_STATE, 0);
	REG_WRITE(DC_PERFMON5_PERFMON_CVALUE_INT_MISC, 0xFF00);
	REG_WRITE(DC_PERFMON5_PERFMON_CVALUE_LOW, 0);
	REG_WRITE(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0);
	REG_WRITE(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0);
}

/**
 * Starts measuring memory latencies (maximum, minimum, and average) in
 * nanoseconds using performance counters.
 *
 * This function configures and enables performance monitoring counters 4, 5,
 * 6, and 7 to track memory latency within the hubbub hardware block. The
 * function sets up:
 *
 * Counter 4: Counts the number of memory latency samples
 * (PERFCOUNTER_INC_MODE = 0x3 for positive edge counting)
 * Counter 5: Accumulates total latency cycles for average calculation
 * (PERFCOUNTER_INC_MODE = 0x2 for LSB level counting)
 * Counter 6: Tracks maximum latency values (PERFCOUNTER_COUNTED_VALUE_TYPE =
 * 0x1, event 74 for frame_window_refclk)
 * Counter 7: Tracks minimum latency values (PERFCOUNTER_COUNTED_VALUE_TYPE =
 * 0x2, event 74 for frame_window_refclk)
 *
 * Configuration details:
 * - Uses Data Fabric latency source (LATENCY_SOURCE_SEL = 0x2)
 * - Monitors all request types (UTM_FILTER_SEL = 0)
 * - Event 79 for memory latency monitoring (counters 4, 5)
 * - Event 74 for frame_window_refclk timing (counters 6, 7)
 * - Counters 6 and 7 use independent state mode with restart enabled
 * - All counters run on refclk cycles for consistent timing measurement
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 * instance.
 */
static void hubbub60_perfmon_start_measuring_memory_latencies(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* configure measurement control */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x2,
			UTM_FILTER_SEL, 0);

	/* program counter 4 to count until duration  */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x4,
			PERFCOUNTER_EVENT_SEL, 79,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x3,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x0,
			PERFCOUNTER_RESTART_EN, 0x0,
			PERFCOUNTER_ACTIVE, 0x1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x4,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* program counter 5 to measure accumulated latency */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x5,
			PERFCOUNTER_EVENT_SEL, 79,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x2,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x0,
			PERFCOUNTER_RESTART_EN, 0x0,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x5,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* program counter 6 to measure max latency */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x6,
			PERFCOUNTER_EVENT_SEL, 74,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x2,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x0,
			PERFCOUNTER_RESTART_EN, 0x1,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x6,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x1,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* program counter 7 to measure min latency */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x7,
			PERFCOUNTER_EVENT_SEL, 74,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x2,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x0,
			PERFCOUNTER_RESTART_EN, 0x1,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x7,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x2,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* Program perfcounter states */
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_STATE, 0,
			PERFCOUNTER_STATE_SEL6, 0x1,
			PERFCOUNTER_CNT6_STATE, 0x3,
			PERFCOUNTER_STATE_SEL7, 0x1,
			PERFCOUNTER_CNT7_STATE, 0x3);

	REG_SET_2(DC_PERFMON5_PERFMON_CNTL2, 0,
			PERFMON_RUN_ENABLE_START_SEL, 0x0,
			PERFMON_RUN_ENABLE_STOP_SEL, 0);

	/* start the counters */
	REG_SET_2(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 0x1,
			PERFMON_RPT_COUNT, 0xFFFFF);
}

/**
 * @brief Reads the current performance monitor results and calculates
 * memory latencies in nanoseconds.
 *
 * This function reads the values from the DCN 6.0 hubbub performance monitor
 * counters, then calculates the minimum, maximum, and average observed
 * memory latencies in nanoseconds.
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 * instance.
 * @param refclk_mhz Reference clock frequency in MHz, used for time
 * conversion.
 * @param min_latency_ns Optional pointer to store the minimum latency in
 * nanoseconds.
 * @param max_latency_ns Optional pointer to store the maximum latency in
 * nanoseconds.
 * @param avg_latency_ns Optional pointer to store the average latency in
 * nanoseconds.
 *
 * @return The number of memory latency samples measured as a 32-bit
 * unsigned integer.
 */
static uint32_t hubbub60_perfmon_get_memory_latencies_ns(
		struct hubbub *hubbub, uint32_t refclk_mhz,
		uint32_t *min_latency_ns, uint32_t *max_latency_ns,
		uint32_t *avg_latency_ns)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count4 = 0, count5 = 0, count6 = 0, count7 = 0;
	struct fixed31_32 temp;

	ASSERT(refclk_mhz != 0);
	if (refclk_mhz == 0)
		return 0;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x4);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count4);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x5);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count5);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x6);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count6);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x7);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count7);

	if (avg_latency_ns && count4) {
		temp = dc_fixpt_from_fraction(count5, count4);
		temp = dc_fixpt_div_int(temp, refclk_mhz);
		*avg_latency_ns = dc_fixpt_ceil(dc_fixpt_mul_int(temp, 1000));
	}

	if (max_latency_ns) {
		temp = dc_fixpt_from_fraction(count6, refclk_mhz);
		*max_latency_ns = dc_fixpt_ceil(dc_fixpt_mul_int(temp, 1000));
	}

	if (min_latency_ns) {
		temp = dc_fixpt_from_fraction(count7, refclk_mhz);
		*min_latency_ns = dc_fixpt_ceil(dc_fixpt_mul_int(temp, 1000));
	}

	return count4;
}

/**
 * Starts measuring urgent assertion and deassertion counts using
 * performance counters.
 *
 * This function configures and enables performance monitoring counters 0,
 * 1, and 4 to track urgent assertion and deassertion events within the
 * hubbub hardware block. The function sets up:
 *
 * Counter 0: Counts urgent assertion events (PERFCOUNTER_INC_MODE = 0x3 for
 * positive edge counting)
 * Counter 1: Counts urgent deassertion events (PERFCOUNTER_INC_MODE = 0x4 for
 * negative edge counting)
 * Counter 4: Gets the current timestamp in reference clock cycles
 *
 * Configuration details:
 * - Uses UTM urgent latency source (LATENCY_SOURCE_SEL = 0x4)
 * - Monitors all request types (UTM_FILTER_SEL = 0)
 * - Event 65 for urgent assertion/deassertion monitoring (counters 0, 1)
 * - Event 19 for current timestamp (counter 4)
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 * instance.
 */
static void hubbub60_perfmon_start_measuring_urgent_assertion_count(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* configure measurement control */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x4,
			UTM_FILTER_SEL, 0);

	/* program counter 0 to urgent assertions */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x0,
			PERFCOUNTER_EVENT_SEL, 65,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x3,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x1,
			PERFCOUNTER_RESTART_EN, 0x0,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x0,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* program counter 1 to urgent assertions */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x1,
			PERFCOUNTER_EVENT_SEL, 65,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x4,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x1,
			PERFCOUNTER_RESTART_EN, 0x0,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x1,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* program counter 4 to get current timestamp in refclk cycles */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x4,
			PERFCOUNTER_EVENT_SEL, 19,
			PERFCOUNTER_CVALUE_SEL, 0x0,
			PERFCOUNTER_INC_MODE, 0x2,
			PERFCOUNTER_HW_CNTL_SEL, 0x0,
			PERFCOUNTER_RUNEN_MODE, 0x0,
			PERFCOUNTER_RESTART_EN, 0x0,
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x4,
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0,
			PERFCOUNTER_HW_STOP1_SEL, 0x0,
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	REG_WRITE(DC_PERFMON5_PERFCOUNTER_STATE, 0);

	/* start the counters */
	REG_SET_2(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 0x1,
			PERFMON_RPT_COUNT, 0xFFFFF);
}

/**
 * @brief Reads the urgent assertion and deassertion counts from the
 * performance monitor.
 *
 * This function reads the values from the DCN 6.0 hubbub performance monitor
 * counters for urgent assertion and deassertion events. It also retrieves
 * a timestamp in microseconds based on the reference clock frequency.
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 * instance.
 * @param refclk_mhz Reference clock frequency in MHz, used for time
 * conversion.
 * @param assertion_count Optional pointer to store the urgent assertion count.
 * @param deassertion_count Optional pointer to store the urgent deassertion
 * count.
 * @param timestamp_us Optional pointer to store the timestamp in microseconds.
 *
 * @return A boolean indicating whether the assertion or deassertion counts
 * have been updated since the last read.
 */
static bool hubbub60_perfmon_get_urgent_assertion_count(
		struct hubbub *hubbub, uint32_t refclk_mhz,
		uint32_t *assertion_count, uint32_t *deassertion_count,
		uint32_t *timestamp_us)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count0 = 0, count1 = 0, count4 = 0;
	bool updated = false;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x0);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count0);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x1);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count1);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x4);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count4);

	if (refclk_mhz != 0 && timestamp_us)
		*timestamp_us = count4 / refclk_mhz;

	if (assertion_count && (*assertion_count != count0)) {
		*assertion_count = count0;
		updated = true;
	}

	if (deassertion_count && (*deassertion_count != count1)) {
		*deassertion_count = count1;
		updated = true;
	}

	return updated;
}

/**
 * @brief Configures the performance monitor to measure the urgent ramp
 * latency.
 *
 * This function configures the performance monitoring counters to measure
 * the urgent ramp latency. It uses hardware counters 0, 1, 2, 4, 5, 6, 7
 * for this purpose:
 *
 * Counter 4:
 *   - Purpose: Begins counting on the first urgent data return, marking
 *     the start of the first rolling window (t_win).
 *   - Behavior: Signals a stop event at the end of every t_win period.
 *
 * Counter 5:
 *   - Purpose: Starts counting at the beginning of t_win.
 *   - Behavior: Signals a stop event at 1/3 of t_win.
 *
 * Counter 6:
 *   - Purpose: Starts counting at the beginning of t_win.
 *   - Behavior: Signals a stop event at 2/3 of t_win.
 *
 * After the first t_win period, counters 4, 5, and 6 will generate stop
 * events every 1/3 t_win. These stop events trigger counters 0, 1, and 2
 * respectively:
 *
 * Counter 0, 1, 2:
 *   - Purpose: Track the total accumulated data received during each 1/3
 *     t_win interval.
 *   - Behavior: Each counter starts on its respective stop event and stops
 *     after 1/3 t_win.
 *   - Threshold: If the total data size exceeds a precalculated threshold,
 *     it indicates bandwidth higher than the target for that 1/3 t_win
 *     period.
 *   - When the counter reaches the threshold cvalue, it signals a stop
 *     interrupt for counter 7.
 *
 * Counter 7:
 *   - Purpose: Measures the total time in reference clock (refclk) cycles.
 *   - Behavior: Starts from the first data return and stops when any of
 *     counters 0, 1, or 2 signal a stop interrupt.
 *   - Usage: The total time counted in refclk cycles is converted into
 *     urgent ramp latency.
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 *        instance.
 * @param params Pointer to the urgent latency measurement parameters.
 */
static void hubbub60_perfmon_start_measuring_urgent_ramp_latency(
		struct hubbub *hubbub,
		const struct hubbub_urgent_latency_params *params)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* configure measurement control */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x4, // df_urgent
			UTM_FILTER_SEL, 0x1);    // urgent requests

	/* program counter 0 as the first bw counter */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x0, // select counter 0
			PERFCOUNTER_EVENT_SEL, 257, // ROB output valid event
			PERFCOUNTER_CVALUE_SEL, 0x7, // use cvalue bits 47-36
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // individual mode
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x1, // restart the counter while it is active
			PERFCOUNTER_INT_EN, 0x1, // signal when the measurement is complete
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x0, // select counter 0
			PERFCOUNTER_CNTOFF_SEL, 0x4, // start when counter 4 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_SET_2(DC_PERFMON5_PERFCOUNTER_STATE, 0,
			PERFCOUNTER_STATE_SEL0, 0x1, // independent state mode
			PERFCOUNTER_CNT0_STATE, 0x3); // hw mode

	/* program counter 1 as the second bw counter */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x1, // select counter 1
			PERFCOUNTER_EVENT_SEL, 257, // ROB output valid event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x7, // use cvalue bits 47-36
			PERFCOUNTER_RESTART_EN, 0x1, // restart the counter while it is active
			PERFCOUNTER_INT_EN, 0x1, // signal when the measurement is complete
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x1, // select counter 1
			PERFCOUNTER_CNTOFF_SEL, 0x5, // start when counter 5 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL1, 0x1, // independent state mode
			PERFCOUNTER_CNT1_STATE, 0x3); // hw mode

	/* program counter 2 as the third bw counter */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x2, // select counter 2
			PERFCOUNTER_EVENT_SEL, 257, // ROB output valid event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x7, // use cvalue bits 47-36
			PERFCOUNTER_RESTART_EN, 0x1, // restart the counter while it is active
			PERFCOUNTER_INT_EN, 0x1, // signal when the measurement is complete
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x2, // select counter 2
			PERFCOUNTER_CNTOFF_SEL, 0x6, // start when counter 6 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL2, 0x1, // independent state mode
			PERFCOUNTER_CNT2_STATE, 0x3); // hw mode

	/* program counter 4 as the first time window */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x4, // select counter 4
			PERFCOUNTER_EVENT_SEL, 19, // Always 1 event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x4, // use cvalue bits 11-0
			PERFCOUNTER_RESTART_EN, 0x0, // do not restart the counter while active.
			PERFCOUNTER_INT_EN, 0x0, // used for timing only, do not enable interrupt
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x4, // select counter 4
			PERFCOUNTER_CNTOFF_SEL, 0x8, // start on custom signal: latency start | counter interrupt
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL4, 0x1, // independent state mode
			PERFCOUNTER_CNT4_STATE, 0x3); // hw mode

	/* program counter 5 as the second time window */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x5, // select counter 5
			PERFCOUNTER_EVENT_SEL, 19, // Always 1 event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x5, // use cvalue bits 23-12
			PERFCOUNTER_RESTART_EN, 0x1, // restart the counter while it's active
			PERFCOUNTER_OFF_MASK, 0x1, // Don't allow this counter to trigger counter 4 restart
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x5, // select counter 5
			PERFCOUNTER_CNTOFF_SEL, 0x4, // start when counter 4 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL5, 0x1, // independent state mode
			PERFCOUNTER_CNT5_STATE, 0x3); // hw mode

	/* program counter 6 as the third time window */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x6, // select counter 6
			PERFCOUNTER_EVENT_SEL, 19, // Always 1 event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x6, // use cvalue bits 35-24
			PERFCOUNTER_RESTART_EN, 0x1, // restart the counter while it's active
			PERFCOUNTER_OFF_MASK, 0x1, // Don't allow this counter to trigger counter 4 restart
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x6, // select counter 6
			PERFCOUNTER_CNTOFF_SEL, 0x4, // start when counter 4 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // stop when count reaches cvalue
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL6, 0x1, // independent state mode
			PERFCOUNTER_CNT6_STATE, 0x3); // hw mode

	/* program counter 7 to count urgent ramp latency from urg asserted until bw reaches threshold */
	REG_SET_10(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x7, // select counter 7
			PERFCOUNTER_EVENT_SEL, 19, // Always 1 event
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // hw indepedennt mode: start event on perfmon off and stop counting on cvalue reached.
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_CVALUE_SEL, 0x0, // default/not used
			PERFCOUNTER_RESTART_EN, 0x0, // do not restart counter
			PERFCOUNTER_INT_EN, 0x0, // Final measurement; doesn't need to interrupt
			PERFCOUNTER_OFF_MASK, 0x0, // default
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x7, // select counter 7
			PERFCOUNTER_CNTOFF_SEL, 0x8, // start on custom signal: latency start | counter interrupt
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored for hardware independent mode
			PERFCOUNTER_HW_STOP2_SEL, 0x1); // stop on external event
	REG_UPDATE_2(DC_PERFMON5_PERFCOUNTER_STATE,
			PERFCOUNTER_STATE_SEL7, 0x1, // independent state mode
			PERFCOUNTER_CNT7_STATE, 0x3); // hw mode

	REG_SET_3(DC_PERFMON5_PERFMON_CNTL2, 0,
			PERFMON_RUN_ENABLE_START_SEL, 0x0, // start on first urgent request from lat mon
			PERFMON_RUN_ENABLE_STOP_SEL, 11, // stop on counter interrupt
			PERFMON_CNTOFF_INT_TYPE, 0x0);

	// Program Perfmon cvalue registers (example values, should be
	// calculated as per doc) refclk_hz = refclk_mhz * 1,000,000 t_win_s =
	// t_win_ns / 1,000,000,000 CVALUE[11:0] = refclk_hz * t_win_s =
	// (refclk_mhz * t_win_ns) / 1000
	struct fixed31_32 slice_size =
			dc_fixpt_from_fraction((uint64_t)params->refclk_mhz * params->t_win_ns, 3000);
	struct fixed31_32 threshold_bytes = dc_fixpt_div_int(
			dc_fixpt_from_int(params->bandwidth_mbps * params->t_win_ns),
			1000);

	uint32_t cvalue_0 = dc_fixpt_floor(dc_fixpt_mul_int(slice_size, 3));
	uint32_t cvalue_1 = dc_fixpt_floor(slice_size);
	uint32_t cvalue_2 = dc_fixpt_floor(dc_fixpt_mul_int(slice_size, 2));
	uint32_t cvalue_3 = dc_fixpt_floor(dc_fixpt_mul(
			dc_fixpt_div_int(threshold_bytes, 64),
			dc_fixpt_from_fraction(params->bw_factor_x1000, 1000)));

	// Pack into 48 bits: [47:36][35:24][23:12][11:0]
	uint64_t cvalue = ((uint64_t)cvalue_3 << 36) |
			((uint64_t)cvalue_2 << 24) |
			((uint64_t)cvalue_1 << 12) |
			((uint64_t)cvalue_0);

	REG_SET(DC_PERFMON5_PERFMON_CVALUE_INT_MISC, 0, PERFMON_CVALUE_HI, (uint32_t) (cvalue >> 32));
	REG_SET(DC_PERFMON5_PERFMON_CVALUE_LOW, 0, PERFMON_CVALUE_LOW, (uint32_t) (cvalue & 0xFFFFFFFF));

	/* start the counters */
	REG_SET_3(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 0x3,
			PERFMON_RPT_COUNT, 1,
			PERFMON_CNTOFF_INT_EN, 0x1);
}

/**
 * @brief Reads the current performance monitor result and calculates
 * the urgent ramp latency in nanoseconds.
 *
 * This function reads the values from the DCN 6.0 hubbub performance
 * monitor counters, then calculates the urgent ramp latency in
 * nanoseconds.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 * @param refclk_mhz Reference clock frequency in MHz, used for time
 * conversion.
 *
 * @return The urgent ramp latency in nanoseconds as a 32-bit unsigned
 * integer.
 */
static uint32_t hubbub60_perfmon_get_urgent_ramp_latency_ns(
		struct hubbub *hubbub, uint32_t refclk_mhz)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count7 = 0, latency_ns = 0;
	struct fixed31_32 temp;

	if (refclk_mhz == 0)
		return 0;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x7);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count7);

	temp = dc_fixpt_from_fraction(count7, refclk_mhz);
	temp = dc_fixpt_mul_int(temp, 1000);
	latency_ns = dc_fixpt_ceil(temp);

	return latency_ns;
}

/**
 * hubbub60_perfmon_arm_measuring_out_of_order_bandwidth - Configure the out-of-order BW counter.
 * @hubbub: pointer to the hubbub hardware instance
 *
 * Configures the performance monitoring counters to measure out-of-order
 * (peak prefetch) bandwidth using hardware counters 0, 1, and 4:
 *
 * Counter 0: count-off counter — counts response-valid events and gates the
 *            measurement window once it reaches its target value.
 * Counter 1: data counter — tracks total data received during the measurement
 *            period; generates an interrupt when measurement completes.
 * Counter 4: duration timer — measures elapsed time in refclk cycles.
 *
 * UTM_FILTER_SEL is set to 0 so that isolation comes from OTG-vblank gating
 * rather than the silicon-broken HW filter.  The first 200 prefetch requests
 * are skipped (ramp-up), and the subsequent 200 are the measurement window.
 *
 * This function configures counters only; call
 * hubbub60_perfmon_start_measuring_out_of_order_bandwidth() to enable them.
 */
static void hubbub60_perfmon_arm_measuring_out_of_order_bandwidth(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* configure measurement control */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	/*
	 * UTM_FILTER_SEL = 0: time-domain gating (OTG vblank) replaces the
	 * silicon-broken HW filter that can no longer isolate prefetch traffic.
	 */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x2,
			UTM_FILTER_SEL, 0x0);

	/* Program counter 0 as the count off counter */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x0, // select counter 0
			PERFCOUNTER_EVENT_SEL, 259, // response vld
			PERFCOUNTER_CVALUE_SEL, 0x1, // use cvalue bits 15-0
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x0, // simutaneous mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stop after counting is done
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x0, // select counter 0
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x1, // the stop trigger is that perfcounter meet the target CVALUE
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // ignored

	/* Program counter 1 to count total data received */
	REG_SET_9(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x1, // select counter 1
			PERFCOUNTER_EVENT_SEL, 259, // response vld
			PERFCOUNTER_CVALUE_SEL, 0x2, // use cvalue bits 31-16
			PERFCOUNTER_INC_MODE, 0x2, // count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // independent mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stop after counting is done
			PERFCOUNTER_INT_EN, 1, // signal when the measurement is complete
			PERFCOUNTER_ACTIVE, 0x1);
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x1,
			PERFCOUNTER_CNTOFF_SEL, 0, // start when count0 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // the stop trigger is that perfcounter meet the target CVALUE

	/* Program counter 4 to count the measuring time */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x4, // select counter 4
			PERFCOUNTER_EVENT_SEL, 19, // always 1 event
			PERFCOUNTER_CVALUE_SEL, 0x0, // ignored
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x1, // independent mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stop after counting is done
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_5(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x4, // select counter 4
			PERFCOUNTER_CNTOFF_SEL, 0, // start when count0 stops
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // ignored
			PERFCOUNTER_HW_STOP2_SEL, 0x1); // the stop trigger is from the external 64 pairs of start/stop events

	/* Program perfcounter states */
	REG_SET_6(DC_PERFMON5_PERFCOUNTER_STATE, 0,
			PERFCOUNTER_STATE_SEL0, 0x1, // independent state mode
			PERFCOUNTER_CNT0_STATE, 0x3, // hw mode
			PERFCOUNTER_STATE_SEL1, 0x1, // independent state mode
			PERFCOUNTER_CNT1_STATE, 0x3, // hw mode
			PERFCOUNTER_STATE_SEL4, 0x1, // independent state mode
			PERFCOUNTER_CNT4_STATE, 0x3); // hw mode

	/*
	 * The cvalue is derived based on experimental results at the lowest
	 * clock state. Out-of-order bandwidth requires ~200 prefetch requests
	 * to ramp up to full speed; the subsequent 200 requests form the
	 * measurement window.
	 */
	REG_SET(DC_PERFMON5_PERFMON_CVALUE_LOW, 0, PERFMON_CVALUE_LOW, 200 | 200 << 16);

	REG_SET_2(DC_PERFMON5_PERFMON_CNTL2, 0,
			PERFMON_RUN_ENABLE_START_SEL, 0x0,
			PERFMON_RUN_ENABLE_STOP_SEL, 11); // perfmon counter off event
}

/**
 * hubbub60_perfmon_start_measuring_out_of_order_bandwidth - Enable the out-of-order BW counter.
 * @hubbub: pointer to the hubbub hardware instance
 *
 * Enables the performance monitor counters previously configured by
 * hubbub60_perfmon_arm_measuring_out_of_order_bandwidth().  The counter
 * self-stops once the count-off counter reaches its target; there is no
 * explicit stop step for the peak-BW path.
 */
static void hubbub60_perfmon_start_measuring_out_of_order_bandwidth(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_SET_2(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 0x3,
			PERFMON_RPT_COUNT, 1);
}

/**
 * hubbub60_perfmon_get_out_of_order_bandwidth_mbps - Read out-of-order BW counter result.
 * @hubbub: pointer to the hubbub hardware instance
 * @refclk_mhz: reference clock frequency in MHz, used for duration conversion
 * @duration_ns: output parameter; receives the measured duration in nanoseconds
 *
 * Reads hardware counters 1 (data) and 4 (duration) and converts the raw
 * refclk-cycle count into bandwidth in Mbps.
 *
 * Return: out-of-order bandwidth in Mbps
 */
static uint32_t hubbub60_perfmon_get_out_of_order_bandwidth_mbps(
		struct hubbub *hubbub, uint32_t refclk_mhz, uint32_t *duration_ns)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count0 = 0, count1 = 0, count4 = 0,
			out_of_order_bandwidth_mbps = 0, measuring_duration_ns = 0;
	struct fixed31_32 temp;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x0);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count0);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x1);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count1);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x4);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count4);

	if (refclk_mhz == 0)
		return 0;

	temp = dc_fixpt_from_fraction(count4, refclk_mhz);
	temp = dc_fixpt_mul_int(temp, 1000);
	measuring_duration_ns = dc_fixpt_ceil(temp);

	if (duration_ns)
		*duration_ns = measuring_duration_ns;
	if (measuring_duration_ns == 0)
		return 0;

	temp = dc_fixpt_from_fraction(count1, measuring_duration_ns);
	temp = dc_fixpt_mul_int(temp, 64 * 1000);
	out_of_order_bandwidth_mbps = dc_fixpt_floor(temp);

	return out_of_order_bandwidth_mbps;
}

/**
 * @brief Configures the performance monitor to measure in-order
 * bandwidth in megabits per second (Mbps).
 *
 * This function sets up performance monitoring counters to measure
 * in-order bandwidth. It uses hardware counters 0 and 4 for this
 * purpose:
 *
 * Counter 0:
 *   - Purpose: Tracks the total data received during the measurement
 *     period.
 *   - Behavior: Starts counting when the measurement starts and stops
 *     when the measurement is stopped manually.
 *
 * Counter 4:
 *   - Purpose: Measures the total time taken during the measurement
 *     period in reference clock (refclk) cycles.
 *   - Behavior: Starts counting when the measurement starts and stops
 *     when the measurement is stopped manually.
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 *        instance.
 */
static void hubbub60_perfmon_start_measuring_in_order_bandwidth(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* Program Latency Monitor Registers */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x8, // ROB latency
			UTM_FILTER_SEL, 0x0);

	/* Program counter 0 to count total data received */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x0, // select counter 0
			PERFCOUNTER_EVENT_SEL, 257, // in order data
			PERFCOUNTER_CVALUE_SEL, 0x0, // ignored
			PERFCOUNTER_INC_MODE, 0x2, // count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x0, // simutaneous mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stop after counting is done
			PERFCOUNTER_ACTIVE, 0x1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x0, // select counter 0
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // stop when other counters are done
			PERFCOUNTER_HW_STOP2_SEL, 0x0);

	/* Program counter 4 to count the measuring time */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x4, // select counter 4
			PERFCOUNTER_EVENT_SEL, 19, // always 1 event
			PERFCOUNTER_CVALUE_SEL, 0x0, // all cvalue range
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x0, // simutaneous mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stops after counting is done
			PERFCOUNTER_ACTIVE, 0x1); // enable the counter
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x4, // select counter 4
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // stop when count reaches cvalue
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // ignored

	/* Program perfcounter states */
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_STATE, 0,
			PERFCOUNTER_STATE_SEL0, 0x0, // global state mode
			PERFCOUNTER_CNT0_STATE, 0x0, // ignored
			PERFCOUNTER_STATE_SEL4, 0x0, // global state mode
			PERFCOUNTER_CNT4_STATE, 0x0); // ignored

	/* start the counters */
	REG_SET_2(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 1,
			PERFMON_RPT_COUNT, 0xFFFFF);
}

/**
 * @brief Reads the current performance monitor result and calculates
 * the in-order bandwidth in Mbps.
 *
 * This function reads the values from the DCN 6.0 hubbub performance
 * monitor counters, then calculates the in-order bandwidth in Mbps.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 * @param refclk_mhz Reference clock frequency in MHz, used for time
 * conversion.
 * @param min_duration_ns Minimum duration in nanoseconds required for
 * a valid measurement.
 * @param duration_ns Measured duration in nanoseconds.
 *
 * @return The in-order bandwidth in Mbps as a 32-bit unsigned integer.
 */
static uint32_t hubbub60_perfmon_get_in_order_bandwidth_mbps(
		struct hubbub *hubbub, uint32_t refclk_mhz,
		uint32_t min_duration_ns, uint32_t *duration_ns)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count0 = 0, count4 = 0, in_order_bandwidth_mbps = 0,
			measuring_duration_ns = 0;
	struct fixed31_32 temp;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x4);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count4);

	if (refclk_mhz == 0)
		return 0;

	measuring_duration_ns = count4 * 1000 / refclk_mhz;
	*duration_ns = measuring_duration_ns;
	if (min_duration_ns > measuring_duration_ns)
		return 0;

	/* stop the counters */
	REG_SET(DC_PERFMON5_PERFMON_CNTL, 0, PERFMON_STATE, 2);

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x0);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count0);
	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x4);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count4);

	ASSERT(count4);
	temp = dc_fixpt_from_fraction(count4, refclk_mhz);
	temp = dc_fixpt_mul_int(temp, 1000);
	measuring_duration_ns = dc_fixpt_ceil(temp);
	if (duration_ns)
		*duration_ns = measuring_duration_ns;

	temp = dc_fixpt_from_fraction(count0, measuring_duration_ns);
	temp = dc_fixpt_mul_int(temp, 64 * 1000);
	in_order_bandwidth_mbps = dc_fixpt_floor(temp);

	return in_order_bandwidth_mbps;
}

/**
 * @brief Configures the performance monitor to measure prefetch data size.
 *
 * This function sets up performance monitoring counter 0 to measure
 * the total prefetch data size in bytes. It configures the necessary
 * registers to count the number of valid ROB output events, which
 * correspond to prefetch data.
 *
 * @param hubbub Pointer to the hubbub structure representing the hardware
 *        instance.
 */
static void hubbub60_perfmon_start_measuring_prefetch_data_size(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* configure measurement control */
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL, 0,
			DCHUBBUB_LATENCY_CNT_EN, 0x1,
			DCHUBBUB_DF_REQ_CMD_LATENCY_SEL, 0x1);
	REG_SET_2(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2, 0,
			LATENCY_SOURCE_SEL, 0x2,
			UTM_FILTER_SEL, 0x2); // prefetch only

	/* program counter 0 to count prefetch data size */
	REG_SET_8(DC_PERFMON5_PERFCOUNTER_CNTL, 0,
			PERFCOUNTER_CNTL_SEL, 0x0, // select counter 0
			PERFCOUNTER_EVENT_SEL, 259, // ROB output valid event
			PERFCOUNTER_CVALUE_SEL, 0x0, // ignored
			PERFCOUNTER_INC_MODE, 0x2, // Count LSB level
			PERFCOUNTER_HW_CNTL_SEL, 0x0, // simultaneous mode
			PERFCOUNTER_RUNEN_MODE, 0x0, // counter runs as long as run_enable is high
			PERFCOUNTER_RESTART_EN, 0x0, // stop after counting is done
			PERFCOUNTER_ACTIVE, 1);
	REG_SET_4(DC_PERFMON5_PERFCOUNTER_CNTL2, 0,
			PERFCOUNTER_CNTL2_SEL, 0x0, // select counter 0
			PERFCOUNTER_COUNTED_VALUE_TYPE, 0x0, // count the accumulated value
			PERFCOUNTER_HW_STOP1_SEL, 0x0, // stop when counting is done
			PERFCOUNTER_HW_STOP2_SEL, 0x0); // ignored

	REG_SET_2(DC_PERFMON5_PERFCOUNTER_STATE, 0,
			PERFCOUNTER_STATE_SEL0, 0x1, // simultaneous state mode
			PERFCOUNTER_CNT0_STATE, 0x3); // hw mode

	REG_SET_2(DC_PERFMON5_PERFMON_CNTL2, 0,
			PERFMON_RUN_ENABLE_START_SEL, 0x0,
			PERFMON_RUN_ENABLE_STOP_SEL, 0);

	/* start the counters */
	REG_SET_2(DC_PERFMON5_PERFMON_CNTL, 0,
			PERFMON_STATE, 0x3,
			PERFMON_RPT_COUNT, 0x1);
}

/**
 * @brief Reads the current performance monitor result and calculates
 * the prefetch data size in bytes.
 *
 * This function reads the value from the DCN 6.0 hubbub performance
 * monitor counter 0, then calculates the prefetch data size in bytes.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 *
 * @return The prefetch data size in bytes as a 32-bit unsigned integer.
 */
static uint32_t hubbub60_perfmon_get_prefetch_data_size(
		struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t count0 = 0;
	uint32_t prefetch_data_size_bytes = 0;

	REG_SET(DC_PERFMON5_PERFMON_HI, 0, PERFMON_READ_SEL, 0x0);
	REG_GET(DC_PERFMON5_PERFMON_LOW, PERFMON_LOW, &count0);

	prefetch_data_size_bytes = count0 * 64;
	return prefetch_data_size_bytes;
}

/**
 * @brief Forces the display to use the nominal QoS profile.
 *
 * This function configures the hubbub to force the display to use
 * the nominal QoS profile by updating the relevant registers.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 */
static void hubbub60_force_display_nominal_profile(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_URGENT, 0);
	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_ENABLE, 1);
}

/**
 * @brief Forces the display to use the urgent QoS profile.
 *
 * This function configures the hubbub to force the display to use
 * the urgent QoS profile by updating the relevant registers.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 */
static void hubbub60_force_display_urgent_profile(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_URGENT, 1);
	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_ENABLE, 1);
}

/**
 * @brief Resets the display QoS profile to default.
 *
 * This function resets the display QoS profile by disabling any
 * forced QoS settings in the hubbub.
 *
 * @param hubbub Pointer to the hubbub structure representing the
 * hardware instance.
 */
static void hubbub60_reset_display_qos_profile(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_ENABLE, 0);
	REG_UPDATE(DCHUBBUB_ARB_QOS_FORCE, DCHUBBUB_ARB_UTM_FORCE_URGENT, 0);
}

static const struct hubbub_funcs hubbub60_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub3_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle_addr3 = hubbub401_dcc_support_swizzle,
	.dcc_support_pixel_format_plane0_plane1 =
		hubbub401_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub401_get_dcc_compression_cap,
	.wm_read_state = hubbub60_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub60_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.verify_allow_pstate_change_high = NULL,
	.force_wm_propagate_to_pipes = hubbub60_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub60_init_watermarks,
	.init_crb = dcn60_init_crb,
	.hubbub_read_state = hubbub2_read_state,
	.force_usr_retraining_allow = NULL,
	.set_request_limit = hubbub32_set_request_limit,
	.program_det_segments = dcn60_program_det_segments,
	.program_compbuf_segments = dcn60_program_compbuf_segments,
	.wait_for_det_update = dcn60_wait_for_det_update,
	.program_arbiter = dcn401_program_arbiter,
	.hubbub_read_reg_state = hubbub3_read_reg_state,
	.perfmon = {
		.reset = hubbub60_perfmon_reset,
		.start_measuring_memory_latencies =
			hubbub60_perfmon_start_measuring_memory_latencies,
		.get_memory_latencies_ns =
			hubbub60_perfmon_get_memory_latencies_ns,
		.start_measuring_urgent_assertion_count =
			hubbub60_perfmon_start_measuring_urgent_assertion_count,
		.get_urgent_assertion_count =
			hubbub60_perfmon_get_urgent_assertion_count,
		.start_measuring_urgent_ramp_latency =
			hubbub60_perfmon_start_measuring_urgent_ramp_latency,
		.get_urgent_ramp_latency_ns =
			hubbub60_perfmon_get_urgent_ramp_latency_ns,
		.arm_measuring_out_of_order_bandwidth =
			hubbub60_perfmon_arm_measuring_out_of_order_bandwidth,
		.start_measuring_out_of_order_bandwidth =
			hubbub60_perfmon_start_measuring_out_of_order_bandwidth,
		.get_out_of_order_bandwidth_mbps =
			hubbub60_perfmon_get_out_of_order_bandwidth_mbps,
		.start_measuring_in_order_bandwidth =
			hubbub60_perfmon_start_measuring_in_order_bandwidth,
		.get_in_order_bandwidth_mbps =
			hubbub60_perfmon_get_in_order_bandwidth_mbps,
		.start_measuring_prefetch_data_size =
			hubbub60_perfmon_start_measuring_prefetch_data_size,
		.get_prefetch_data_size =
			hubbub60_perfmon_get_prefetch_data_size,
	},
	.qos = {
		.force_display_nominal_profile =
			hubbub60_force_display_nominal_profile,
		.force_display_urgent_profile =
			hubbub60_force_display_urgent_profile,
		.reset_display_qos_profile =
			hubbub60_reset_display_qos_profile,
	},
};

void hubbub60_construct(struct dcn20_hubbub *hubbub2,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{
	hubbub2->base.ctx = ctx;
	hubbub2->base.funcs = &hubbub60_funcs;
	hubbub2->regs = hubbub_regs;
	hubbub2->shifts = hubbub_shift;
	hubbub2->masks = hubbub_mask;

	hubbub2->detile_buf_size = det_size_kb * 1024;
	hubbub2->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub2->crb_size_segs = config_return_buffer_size_kb / DCN6_0_CRB_SEGMENT_SIZE_KB;
}
