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
#include "clk_mgr.h"

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

#ifdef NUM_VMID
#undef NUM_VMID
#endif
#define NUM_VMID 16

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
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		*bytes_per_element = 4;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
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

	hubbub2_det_request_size(TO_DCN20_HUBBUB(hubbub)->detile_buf_size,
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
	default:
		ASSERT(false);
		break;
	}
	output->capable = true;
	output->const_color_support = true;

	return true;
}

static enum dcn_hubbub_page_table_depth page_table_depth_to_hw(unsigned int page_table_depth)
{
	enum dcn_hubbub_page_table_depth depth = 0;

	switch (page_table_depth) {
	case 1:
		depth = DCN_PAGE_TABLE_DEPTH_1_LEVEL;
		break;
	case 2:
		depth = DCN_PAGE_TABLE_DEPTH_2_LEVEL;
		break;
	case 3:
		depth = DCN_PAGE_TABLE_DEPTH_3_LEVEL;
		break;
	case 4:
		depth = DCN_PAGE_TABLE_DEPTH_4_LEVEL;
		break;
	default:
		ASSERT(false);
		break;
	}

	return depth;
}

static enum dcn_hubbub_page_table_block_size page_table_block_size_to_hw(unsigned int page_table_block_size)
{
	enum dcn_hubbub_page_table_block_size block_size = 0;

	switch (page_table_block_size) {
	case 4096:
		block_size = DCN_PAGE_TABLE_BLOCK_SIZE_4KB;
		break;
	case 65536:
		block_size = DCN_PAGE_TABLE_BLOCK_SIZE_64KB;
		break;
	case 32768:
		block_size = DCN_PAGE_TABLE_BLOCK_SIZE_32KB;
		break;
	default:
		ASSERT(false);
		block_size = page_table_block_size;
		break;
	}

	return block_size;
}

void hubbub2_init_vm_ctx(struct hubbub *hubbub,
		struct dcn_hubbub_virt_addr_config *va_config,
		int vmid)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_vmid_page_table_config virt_config;

	virt_config.page_table_start_addr = va_config->page_table_start_addr >> 12;
	virt_config.page_table_end_addr = va_config->page_table_end_addr >> 12;
	virt_config.depth = page_table_depth_to_hw(va_config->page_table_depth);
	virt_config.block_size = page_table_block_size_to_hw(va_config->page_table_block_size);
	virt_config.page_table_base_addr = va_config->page_table_base_addr;

	dcn20_vmid_setup(&hubbub1->vmid[vmid], &virt_config);
}

int hubbub2_init_dchub_sys_ctx(struct hubbub *hubbub,
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

	REG_SET(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_MSB, 0,
			DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_MSB, (pa_config->page_table_default_page_addr >> 44) & 0xF);
	REG_SET(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_LSB, 0,
			DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_LSB, (pa_config->page_table_default_page_addr >> 12) & 0xFFFFFFFF);

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

void hubbub2_update_dchub(struct hubbub *hubbub,
		struct dchub_init_data *dh_data)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	if (REG(DCN_VM_FB_LOCATION_TOP) == 0)
		return;

	switch (dh_data->fb_mode) {
	case FRAME_BUFFER_MODE_ZFB_ONLY:
		/*For ZFB case need to put DCHUB FB BASE and TOP upside down to indicate ZFB mode*/
		REG_UPDATE(DCN_VM_FB_LOCATION_TOP,
				FB_TOP, 0);

		REG_UPDATE(DCN_VM_FB_LOCATION_BASE,
				FB_BASE, 0xFFFFFF);

		/*This field defines the 24 MSBs, bits [47:24] of the 48 bit AGP Base*/
		REG_UPDATE(DCN_VM_AGP_BASE,
				AGP_BASE, dh_data->zfb_phys_addr_base >> 24);

		/*This field defines the bottom range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_BOT,
				AGP_BOT, dh_data->zfb_mc_base_addr >> 24);

		/*This field defines the top range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_TOP,
				AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 24);
		break;
	case FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/

		/*This field defines the 24 MSBs, bits [47:24] of the 48 bit AGP Base*/
		REG_UPDATE(DCN_VM_AGP_BASE,
				AGP_BASE, dh_data->zfb_phys_addr_base >> 24);

		/*This field defines the bottom range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_BOT,
				AGP_BOT, dh_data->zfb_mc_base_addr >> 24);

		/*This field defines the top range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_TOP,
				AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 24);
		break;
	case FRAME_BUFFER_MODE_LOCAL_ONLY:
		/*Should not touch FB LOCATION (should be done by VBIOS)*/

		/*This field defines the 24 MSBs, bits [47:24] of the 48 bit AGP Base*/
		REG_UPDATE(DCN_VM_AGP_BASE,
				AGP_BASE, 0);

		/*This field defines the bottom range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_BOT,
				AGP_BOT, 0xFFFFFF);

		/*This field defines the top range of the AGP aperture and represents the 24*/
		/*MSBs, bits [47:24] of the 48 address bits*/
		REG_UPDATE(DCN_VM_AGP_TOP,
				AGP_TOP, 0);
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
	s->dram_clk_change = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A);

	s = &wm->sets[1];
	s->wm_set = 1;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B);
	}
	s->dram_clk_change = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B);

	s = &wm->sets[2];
	s->wm_set = 2;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C);
	}
	s->dram_clk_change = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C);

	s = &wm->sets[3];
	s->wm_set = 3;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D);
	if (REG(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D))
		s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D);
	}
	s->dram_clk_change = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D);
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

static bool hubbub2_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;
	/*
	 * Need to clamp to max of the register values (i.e. no wrap)
	 * for dcn1, all wm registers are 21-bit wide
	 */
	if (hubbub1_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub1_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	/*
	 * There's a special case when going from p-state support to p-state unsupported
	 * here we are going to LOWER watermarks to go to dummy p-state only, but this has
	 * to be done prepare_bandwidth, not optimize
	 */
	if (hubbub1->base.ctx->dc->clk_mgr->clks.prev_p_state_change_support == true &&
		hubbub1->base.ctx->dc->clk_mgr->clks.p_state_change_support == false)
		safe_to_lower = true;

	hubbub1_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower);

	REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND, DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 180);

	hubbub->funcs->allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);
	return wm_pending;
}

void hubbub2_read_state(struct hubbub *hubbub, struct dcn_hubbub_state *hubbub_state)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);

	if (REG(DCN_VM_FAULT_ADDR_MSB))
		hubbub_state->vm_fault_addr_msb = REG_READ(DCN_VM_FAULT_ADDR_MSB);

	if (REG(DCN_VM_FAULT_ADDR_LSB))
		hubbub_state->vm_fault_addr_msb = REG_READ(DCN_VM_FAULT_ADDR_LSB);

	if (REG(DCN_VM_FAULT_CNTL))
		REG_GET(DCN_VM_FAULT_CNTL, DCN_VM_ERROR_STATUS_MODE, &hubbub_state->vm_error_mode);

	if (REG(DCN_VM_FAULT_STATUS)) {
		 REG_GET(DCN_VM_FAULT_STATUS, DCN_VM_ERROR_STATUS, &hubbub_state->vm_error_status);
		 REG_GET(DCN_VM_FAULT_STATUS, DCN_VM_ERROR_VMID, &hubbub_state->vm_error_vmid);
		 REG_GET(DCN_VM_FAULT_STATUS, DCN_VM_ERROR_PIPE, &hubbub_state->vm_error_pipe);
	}

	if (REG(DCHUBBUB_TEST_DEBUG_INDEX) && REG(DCHUBBUB_TEST_DEBUG_DATA)) {
		REG_WRITE(DCHUBBUB_TEST_DEBUG_INDEX, 0x6);
		hubbub_state->test_debug_data = REG_READ(DCHUBBUB_TEST_DEBUG_DATA);
	}

	if (REG(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL))
		hubbub_state->watermark_change_cntl = REG_READ(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL);

	if (REG(DCHUBBUB_ARB_DRAM_STATE_CNTL))
		hubbub_state->dram_state_cntl = REG_READ(DCHUBBUB_ARB_DRAM_STATE_CNTL);
}

static const struct hubbub_funcs hubbub2_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub2_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub2_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub2_get_dcc_compression_cap,
	.wm_read_state = hubbub2_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub2_program_watermarks,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.hubbub_read_state = hubbub2_read_state,
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
	hubbub->detile_buf_size = 164 * 1024; /* 164KB for DCN2.0 */
}
