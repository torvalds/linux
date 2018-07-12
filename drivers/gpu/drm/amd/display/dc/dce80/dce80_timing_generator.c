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

/* include DCE8 register header files */
#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dc_types.h"

#include "include/grph_object_id.h"
#include "include/logger_interface.h"
#include "../dce110/dce110_timing_generator.h"
#include "dce80_timing_generator.h"

#include "timing_generator.h"

enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,	/* used as index in array */
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,

	BLACK_COLOR_FORMAT_COUNT
};

static const struct dce110_timing_generator_offsets reg_offsets[] = {
{
	.crtc = (mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.crtc = (mmCRTC1_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.crtc = (mmCRTC2_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.crtc = (mmCRTC3_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP3_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.crtc = (mmCRTC4_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP4_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.crtc = (mmCRTC5_DCFE_MEM_LIGHT_SLEEP_CNTL - mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp = (mmDCP5_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
}
};

#define NUMBER_OF_FRAME_TO_WAIT_ON_TRIGGERED_RESET 10

#define MAX_H_TOTAL (CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1)
#define MAX_V_TOTAL (CRTC_V_TOTAL__CRTC_V_TOTAL_MASKhw + 1)

#define CRTC_REG(reg) (reg + tg110->offsets.crtc)
#define DCP_REG(reg) (reg + tg110->offsets.dcp)
#define DMIF_REG(reg) (reg + tg110->offsets.dmif)

static void program_pix_dur(struct timing_generator *tg, uint32_t pix_clk_khz)
{
	uint64_t pix_dur;
	uint32_t addr = mmDMIF_PG0_DPG_PIPE_ARBITRATION_CONTROL1
					+ DCE110TG_FROM_TG(tg)->offsets.dmif;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	if (pix_clk_khz == 0)
		return;

	pix_dur = 1000000000 / pix_clk_khz;

	set_reg_field_value(
		value,
		pix_dur,
		DPG_PIPE_ARBITRATION_CONTROL1,
		PIXEL_DURATION);

	dm_write_reg(tg->ctx, addr, value);
}

static void program_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	bool use_vbios)
{
	if (!use_vbios)
		program_pix_dur(tg, timing->pix_clk_khz);

	dce110_tg_program_timing(tg, timing, use_vbios);
}

static void dce80_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_START_LINE_CONTROL);
	uint32_t value = dm_read_reg(tg->ctx, addr);

	if (enable) {
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	} else {
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	}

	if ((timing->v_sync_width + timing->v_front_porch) <= 3) {
		set_reg_field_value(
			value,
			3,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	} else {
		set_reg_field_value(
			value,
			4,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	}

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_PROGRESSIVE_START_LINE_EARLY);

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_INTERLACE_START_LINE_EARLY);

	dm_write_reg(tg->ctx, addr, value);
}

static const struct timing_generator_funcs dce80_tg_funcs = {
		.validate_timing = dce110_tg_validate_timing,
		.program_timing = program_timing,
		.enable_crtc = dce110_timing_generator_enable_crtc,
		.disable_crtc = dce110_timing_generator_disable_crtc,
		.is_counter_moving = dce110_timing_generator_is_counter_moving,
		.get_position = dce110_timing_generator_get_position,
		.get_frame_count = dce110_timing_generator_get_vblank_counter,
		.get_scanoutpos = dce110_timing_generator_get_crtc_scanoutpos,
		.set_early_control = dce110_timing_generator_set_early_control,
		.wait_for_state = dce110_tg_wait_for_state,
		.set_blank = dce110_tg_set_blank,
		.is_blanked = dce110_tg_is_blanked,
		.set_colors = dce110_tg_set_colors,
		.set_overscan_blank_color =
				dce110_timing_generator_set_overscan_color_black,
		.set_blank_color = dce110_timing_generator_program_blank_color,
		.disable_vga = dce110_timing_generator_disable_vga,
		.did_triggered_reset_occur =
				dce110_timing_generator_did_triggered_reset_occur,
		.setup_global_swap_lock =
				dce110_timing_generator_setup_global_swap_lock,
		.enable_reset_trigger = dce110_timing_generator_enable_reset_trigger,
		.disable_reset_trigger = dce110_timing_generator_disable_reset_trigger,
		.tear_down_global_swap_lock =
				dce110_timing_generator_tear_down_global_swap_lock,
		.set_drr = dce110_timing_generator_set_drr,
		.set_static_screen_control =
			dce110_timing_generator_set_static_screen_control,
		.set_test_pattern = dce110_timing_generator_set_test_pattern,
		.arm_vert_intr = dce110_arm_vert_intr,

		/* DCE8.0 overrides */
		.enable_advanced_request =
				dce80_timing_generator_enable_advanced_request,
		.configure_crc = dce110_configure_crc,
		.get_crc = dce110_get_crc,
};

void dce80_timing_generator_construct(
	struct dce110_timing_generator *tg110,
	struct dc_context *ctx,
	uint32_t instance,
	const struct dce110_timing_generator_offsets *offsets)
{
	tg110->controller_id = CONTROLLER_ID_D0 + instance;
	tg110->base.inst = instance;
	tg110->offsets = *offsets;
	tg110->derived_offsets = reg_offsets[instance];

	tg110->base.funcs = &dce80_tg_funcs;

	tg110->base.ctx = ctx;
	tg110->base.bp = ctx->dc_bios;

	tg110->max_h_total = CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg110->max_v_total = CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	tg110->min_h_blank = 56;
	tg110->min_h_front_porch = 4;
	tg110->min_h_back_porch = 4;
}

