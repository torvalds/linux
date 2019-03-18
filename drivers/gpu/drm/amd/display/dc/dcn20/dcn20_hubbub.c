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


#include "dcn20_hubbub.h"
#include "reg_helper.h"

#define REG(reg)\
	hubbub1->regs->reg

#define CTX \
	hubbub1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

#define REG(reg)\
	hubbub1->regs->reg

#define CTX \
	hubbub1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

bool hubbub2_dcc_support_swizzle(
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
	case DC_SW_64KB_R_X:
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
		if (bytes_per_element == 2) {
			*segment_order_horz = segment_order__contiguous;
			*segment_order_vert = segment_order__contiguous;
			return true;
		}
		if (bytes_per_element == 4) {
			*segment_order_horz = segment_order__non_contiguous;
			*segment_order_vert = segment_order__contiguous;
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

bool hubbub2_dcc_support_pixel_format(
		enum surface_pixel_format format,
		unsigned int *bytes_per_element)
{
	/* DML: get_bytes_per_element */
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		*bytes_per_element = 2;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		*bytes_per_element = 4;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		*bytes_per_element = 8;
		return true;
	default:
		return false;
	}
}

static void hubbub2_get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
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

static void hubbub2_det_request_size(
		unsigned int height,
		unsigned int width,
		unsigned int bpe,
		bool *req128_horz_wc,
		bool *req128_vert_wc)
{
	unsigned int detile_buf_size = 164 * 1024;  /* 164KB for DCN1.0 */

	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int swath_bytes_horz_wc, swath_bytes_vert_wc;

	hubbub2_get_blk256_size(&blk256_width, &blk256_height, bpe);

	swath_bytes_horz_wc = width * blk256_height * bpe;
	swath_bytes_vert_wc = height * blk256_width * bpe;

	*req128_horz_wc = (2 * swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*req128_vert_wc = (2 * swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */
}

bool hubbub2_get_dcc_compression_cap(struct hubbub *hubbub,
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

	hubbub2_det_request_size(input->surface_size.height,  input->surface_size.width,
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
		break;
	case dcc_control__128_128_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 128;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		break;
	case dcc_control__256_64_64:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 64;
		output->grph.rgb.independent_64b_blks = true;
		break;
	}
	output->capable = true;
	output->const_color_support = true;

	return true;
}

static void hubbub2_setup_vmid_ptb(struct hubbub *hubbub,
		uint64_t ptb,
		uint8_t vmid)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	dcn20_vmid_set_ptb(&hubbub1->vmid[vmid], ptb);
}


void hubbub2_init_dchub(struct hubbub *hubbub,
		struct hubbub_addr_config *config)
{
	int i;
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_vmid_page_table_config phys_config;
	struct dcn_vmid_page_table_config virt_config;

	phys_config.depth = 0; // Depth 1
	phys_config.block_size = 0; // Block size 4KB
	phys_config.page_table_start_addr = config->pa_config.gart_config.page_table_start_addr;
	phys_config.page_table_end_addr = config->pa_config.gart_config.page_table_end_addr;

	REG_SET(DCN_VM_FB_LOCATION_BASE, 0,
			FB_BASE, config->pa_config.system_aperture.fb_base);
	REG_SET(DCN_VM_FB_LOCATION_TOP, 0,
			FB_TOP, config->pa_config.system_aperture.fb_top);
	REG_SET(DCN_VM_FB_OFFSET, 0,
			FB_OFFSET, config->pa_config.system_aperture.fb_offset);
	REG_SET(DCN_VM_AGP_BOT, 0,
			AGP_BOT, config->pa_config.system_aperture.agp_bot);
	REG_SET(DCN_VM_AGP_TOP, 0,
			AGP_TOP, config->pa_config.system_aperture.agp_top);
	REG_SET(DCN_VM_AGP_BASE, 0,
			AGP_BASE, config->pa_config.system_aperture.agp_base);

	// Init VMID 0 based on PA config
	dcn20_vmid_setup(&hubbub1->vmid[0], &phys_config);
	dcn20_vmid_set_ptb(&hubbub1->vmid[0], config->pa_config.gart_config.page_table_base_addr);

	// Init VMID 1-15 based on VA config
	for (i = 1; i < 16; i++) {
		virt_config.page_table_start_addr = config->va_config.page_table_start_addr;
		virt_config.page_table_end_addr = config->va_config.page_table_end_addr;
		virt_config.depth = config->va_config.page_table_depth;
		virt_config.block_size = config->va_config.page_table_block_size;

		dcn20_vmid_setup(&hubbub1->vmid[i], &virt_config);
	}
}

void hubbub2_update_dchub(struct hubbub *hubbub,
		struct dchub_init_data *dh_data)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	if (REG(DCHUBBUB_SDPIF_FB_TOP) == 0) {
		ASSERT(false);
		/*should not come here*/
		return;
	}
	/* TODO: port code from dal2 */
	switch (dh_data->fb_mode) {
	case FRAME_BUFFER_MODE_ZFB_ONLY:
		/*For ZFB case need to put DCHUB FB BASE and TOP upside down to indicate ZFB mode*/
		REG_UPDATE(DCHUBBUB_SDPIF_FB_TOP,
				SDPIF_FB_TOP, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_FB_BASE,
				SDPIF_FB_BASE, 0x0FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_LOCAL_ONLY:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/
		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, 0X03FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, 0);
		break;
	default:
		break;
	}

	dh_data->dchub_initialzied = true;
	dh_data->dchub_info_valid = false;
}

void hubbub2_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	struct dcn_hubbub_wm_set *s;

	memset(wm, 0, sizeof(struct dcn_hubbub_wm));

	s = &wm->sets[0];
	s->wm_set = 0;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A);

	s = &wm->sets[1];
	s->wm_set = 1;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B);

	s = &wm->sets[2];
	s->wm_set = 2;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C);

	s = &wm->sets[3];
	s->wm_set = 3;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D);
}

void hubbub2_get_dchub_ref_freq(struct hubbub *hubbub,
		unsigned int dccg_ref_freq_inKhz,
		unsigned int *dchub_ref_freq_inKhz)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t ref_div = 0;
	uint32_t ref_en = 0;

	REG_GET_2(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, &ref_div,
			DCHUBBUB_GLOBAL_TIMER_ENABLE, &ref_en);

	if (ref_en) {
		if (ref_div == 2)
			*dchub_ref_freq_inKhz = dccg_ref_freq_inKhz / 2;
		else
			*dchub_ref_freq_inKhz = dccg_ref_freq_inKhz;

		// DC hub reference frequency must be around 50Mhz, otherwise there may be
		// overflow/underflow issues when doing HUBBUB programming
		if (*dchub_ref_freq_inKhz < 40000 || *dchub_ref_freq_inKhz > 60000)
			ASSERT_CRITICAL(false);

		return;
	} else {
		*dchub_ref_freq_inKhz = dccg_ref_freq_inKhz;

		// HUBBUB global timer must be enabled.
		ASSERT_CRITICAL(false);
		return;
	}
}

static const struct hubbub_funcs hubbub2_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub = hubbub2_init_dchub,
	.setup_vmid_ptb = hubbub2_setup_vmid_ptb,
	.dcc_support_swizzle = hubbub2_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub2_get_dcc_compression_cap,
	.wm_read_state = hubbub2_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub1_program_watermarks,
};

void hubbub2_construct(struct dcn20_hubbub *hubbub,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask)
{
	hubbub->base.ctx = ctx;

	hubbub->base.funcs = &hubbub2_funcs;

	hubbub->regs = hubbub_regs;
	hubbub->shifts = hubbub_shift;
	hubbub->masks = hubbub_mask;

	hubbub->debug_test_index_pstate = 0xB;
}
