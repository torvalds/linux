/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "reg_helper.h"
#include "dcn30_hubbub.h"


#define CTX \
	hubbub1->base.ctx
#define DC_LOGGER \
	hubbub1->base.ctx->logger
#define REG(reg)\
	hubbub1->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

#ifdef NUM_VMID
#undef NUM_VMID
#endif
#define NUM_VMID 16


static uint32_t convert_and_clamp(
	uint32_t wm_ns,
	uint32_t refclk_mhz,
	uint32_t clamp_value)
{
	uint32_t ret_val = 0;
	ret_val = wm_ns * refclk_mhz;
	ret_val /= 1000;

	if (ret_val > clamp_value)
		ret_val = clamp_value;

	return ret_val;
}

int hubbub3_init_dchub_sys_ctx(struct hubbub *hubbub,
		struct dcn_hubbub_phys_addr_config *pa_config)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_vmid_page_table_config phys_config;

	REG_SET(DCN_VM_FB_LOCATION_BASE, 0,
			FB_BASE, pa_config->system_aperture.fb_base >> 24);
	REG_SET(DCN_VM_FB_LOCATION_TOP, 0,
			FB_TOP, pa_config->system_aperture.fb_top >> 24);
	REG_SET(DCN_VM_FB_OFFSET, 0,
			FB_OFFSET, pa_config->system_aperture.fb_offset >> 24);
	REG_SET(DCN_VM_AGP_BOT, 0,
			AGP_BOT, pa_config->system_aperture.agp_bot >> 24);
	REG_SET(DCN_VM_AGP_TOP, 0,
			AGP_TOP, pa_config->system_aperture.agp_top >> 24);
	REG_SET(DCN_VM_AGP_BASE, 0,
			AGP_BASE, pa_config->system_aperture.agp_base >> 24);

	if (pa_config->gart_config.page_table_start_addr != pa_config->gart_config.page_table_end_addr) {
		phys_config.page_table_start_addr = pa_config->gart_config.page_table_start_addr >> 12;
		phys_config.page_table_end_addr = pa_config->gart_config.page_table_end_addr >> 12;
		phys_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr;
		phys_config.depth = 0;
		phys_config.block_size = 0;
		// Init VMID 0 based on PA config
		dcn20_vmid_setup(&hubbub1->vmid[0], &phys_config);
	}

	return NUM_VMID;
}

bool hubbub3_program_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	if (hubbub21_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub21_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub21_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	/*
	 * The DCHub arbiter has a mechanism to dynamically rate limit the DCHub request stream to the fabric.
	 * If the memory controller is fully utilized and the DCHub requestors are
	 * well ahead of their amortized schedule, then it is safe to prevent the next winner
	 * from being committed and sent to the fabric.
	 * The utilization of the memory controller is approximated by ensuring that
	 * the number of outstanding requests is greater than a threshold specified
	 * by the ARB_MIN_REQ_OUTSTANDING. To determine that the DCHub requestors are well ahead of the amortized schedule,
	 * the slack of the next winner is compared with the ARB_SAT_LEVEL in DLG RefClk cycles.
	 *
	 * TODO: Revisit request limit after figure out right number. request limit for Renoir isn't decided yet, set maximum value (0x1FF)
	 * to turn off it for now.
	 */
	REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0x1FF);

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	return wm_pending;
}

bool hubbub3_dcc_support_swizzle(
		enum swizzle_mode_values swizzle,
		unsigned int bytes_per_element,
		enum segment_order *segment_order_horz,
		enum segment_order *segment_order_vert)
{
	bool standard_swizzle = false;
	bool display_swizzle = false;
	bool render_swizzle = false;

	switch (swizzle) {
	case DC_SW_4KB_S:
	case DC_SW_64KB_S:
	case DC_SW_VAR_S:
	case DC_SW_4KB_S_X:
	case DC_SW_64KB_S_X:
	case DC_SW_VAR_S_X:
		standard_swizzle = true;
		break;
	case DC_SW_4KB_R:
	case DC_SW_64KB_R:
	case DC_SW_VAR_R:
	case DC_SW_4KB_R_X:
	case DC_SW_64KB_R_X:
	case DC_SW_VAR_R_X:
		render_swizzle = true;
		break;
	case DC_SW_4KB_D:
	case DC_SW_64KB_D:
	case DC_SW_VAR_D:
	case DC_SW_4KB_D_X:
	case DC_SW_64KB_D_X:
	case DC_SW_VAR_D_X:
		display_swizzle = true;
		break;
	default:
		break;
	}

	if (standard_swizzle) {
		if (bytes_per_element == 1) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__na;
			return true;
		}
		if (bytes_per_element == 2) {
			*segment_order_horz = segment_order__non_contiguous;
			*segment_order_vert = segment_order__contiguous;
			return true;
		}
		if (bytes_per_element == 4) {
			*segment_order_horz = segment_order__non_contiguous;
			*segment_order_vert = segment_order__contiguous;
			return true;
		}
		if (bytes_per_element == 8) {
			*segment_order_horz = segment_order__na;
			*segment_order_vert = segment_order__contiguous;
			return true;
		}
	}
	if (render_swizzle) {
		if (bytes_per_element == 1) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__na;
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
	if (display_swizzle && bytes_per_element == 8) {
		*segment_order_horz = segment_order__contiguous;
		*segment_order_vert = segment_order__non_contiguous;
		return true;
	}

	return false;
}

static void hubbub3_get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
		unsigned int bytes_per_element)
{
	/* copied from DML.  might want to refactor DML to leverage from DML */
	/* DML : get_blk256_size */
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

static void hubbub3_det_request_size(
		unsigned int detile_buf_size,
		unsigned int height,
		unsigned int width,
		unsigned int bpe,
		bool *req128_horz_wc,
		bool *req128_vert_wc)
{
	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int swath_bytes_horz_wc, swath_bytes_vert_wc;

	hubbub3_get_blk256_size(&blk256_width, &blk256_height, bpe);

	swath_bytes_horz_wc = width * blk256_height * bpe;
	swath_bytes_vert_wc = height * blk256_width * bpe;

	*req128_horz_wc = (2 * swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*req128_vert_wc = (2 * swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */
}

bool hubbub3_get_dcc_compression_cap(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	struct dc *dc = hubbub->ctx->dc;
	/* implement section 1.6.2.1 of DCN1_Programming_Guide.docx */
	enum dcc_control dcc_control;
	unsigned int bpe;
	enum segment_order segment_order_horz, segment_order_vert;
	bool req128_horz_wc, req128_vert_wc;

	memset(output, 0, sizeof(*output));

	if (dc->debug.disable_dcc == DCC_DISABLE)
		return false;

	if (!hubbub->funcs->dcc_support_pixel_format(input->format,
			&bpe))
		return false;

	if (!hubbub->funcs->dcc_support_swizzle(input->swizzle_mode, bpe,
			&segment_order_horz, &segment_order_vert))
		return false;

	hubbub3_det_request_size(TO_DCN20_HUBBUB(hubbub)->detile_buf_size,
			input->surface_size.height,  input->surface_size.width,
			bpe, &req128_horz_wc, &req128_vert_wc);

	if (!req128_horz_wc && !req128_vert_wc) {
		dcc_control = dcc_control__256_256_xxx;
	} else if (input->scan == SCAN_DIRECTION_HORIZONTAL) {
		if (!req128_horz_wc)
			dcc_control = dcc_control__256_256_xxx;
		else if (segment_order_horz == segment_order__contiguous)
			dcc_control = dcc_control__128_128_xxx;
		else
			dcc_control = dcc_control__256_64_64;
	} else if (input->scan == SCAN_DIRECTION_VERTICAL) {
		if (!req128_vert_wc)
			dcc_control = dcc_control__256_256_xxx;
		else if (segment_order_vert == segment_order__contiguous)
			dcc_control = dcc_control__128_128_xxx;
		else
			dcc_control = dcc_control__256_64_64;
	} else {
		if ((req128_horz_wc &&
			segment_order_horz == segment_order__non_contiguous) ||
			(req128_vert_wc &&
			segment_order_vert == segment_order__non_contiguous))
			/* access_dir not known, must use most constraining */
			dcc_control = dcc_control__256_64_64;
		else
			/* reg128 is true for either horz and vert
			 * but segment_order is contiguous
			 */
			dcc_control = dcc_control__128_128_xxx;
	}

	/* Exception for 64KB_R_X */
	if ((bpe == 2) && (input->swizzle_mode == DC_SW_64KB_R_X))
		dcc_control = dcc_control__128_128_xxx;

	if (dc->debug.disable_dcc == DCC_HALF_REQ_DISALBE &&
		dcc_control != dcc_control__256_256_xxx)
		return false;

	switch (dcc_control) {
	case dcc_control__256_256_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 256;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_256_256_unconstrained = 1;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	case dcc_control__128_128_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 128;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_128_128_uncontrained = 1;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	case dcc_control__256_64_64:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 64;
		output->grph.rgb.independent_64b_blks = true;
		output->grph.rgb.dcc_controls.dcc_256_64_64 = 1;
		break;
	case dcc_control__256_128_128:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	}
	output->capable = true;
	output->const_color_support = true;

	return true;
}

void hubbub3_force_wm_propagate_to_pipes(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t refclk_mhz = hubbub->ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	uint32_t prog_wm_value = convert_and_clamp(hubbub1->watermarks.a.urgent_ns,
			refclk_mhz, 0x1fffff);

	REG_SET_2(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value,
			DCHUBBUB_ARB_VM_ROW_URGENCY_WATERMARK_A, prog_wm_value);
}

void hubbub3_force_pstate_change_control(struct hubbub *hubbub,
		bool force, bool allow)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_VALUE, allow,
			DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_ENABLE, force);
}

/* Copy values from WM set A to all other sets */
void hubbub3_init_watermarks(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t reg;

	reg = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, reg);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, reg);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, reg);
}

static const struct hubbub_funcs hubbub30_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub3_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub3_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub3_get_dcc_compression_cap,
	.wm_read_state = hubbub21_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub3_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.force_wm_propagate_to_pipes = hubbub3_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub3_init_watermarks,
	.hubbub_read_state = hubbub2_read_state,
};

void hubbub3_construct(struct dcn20_hubbub *hubbub3,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask)
{
	hubbub3->base.ctx = ctx;
	hubbub3->base.funcs = &hubbub30_funcs;
	hubbub3->regs = hubbub_regs;
	hubbub3->shifts = hubbub_shift;
	hubbub3->masks = hubbub_mask;

	hubbub3->debug_test_index_pstate = 0xB;
	hubbub3->detile_buf_size = 184 * 1024; /* 184KB for DCN3 */
}

