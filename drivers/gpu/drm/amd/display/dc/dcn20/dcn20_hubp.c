/*
 * Copyright 2012-17 Advanced Micro Devices, Inc.
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

#include "dcn20_hubp.h"

#include "dm_services.h"
#include "dce_calcs.h"
#include "reg_helper.h"
#include "basics/conversion.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

void hubp2_update_dchub(
	struct hubp *hubp,
	struct dchub_init_data *dh_data)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
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

void hubp2_set_vm_system_aperture_settings(struct hubp *hubp,
		struct vm_system_aperture_param *apt)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	PHYSICAL_ADDRESS_LOC mc_vm_apt_default;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_low;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_high;

	// The format of default addr is 48:12 of the 48 bit addr
	mc_vm_apt_default.quad_part = apt->sys_default.quad_part >> 12;

	// The format of high/low are 48:18 of the 48 bit addr
	mc_vm_apt_low.quad_part = apt->sys_low.quad_part >> 18;
	mc_vm_apt_high.quad_part = apt->sys_high.quad_part >> 18;

	REG_UPDATE_2(DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
		DCN_VM_SYSTEM_APERTURE_DEFAULT_SYSTEM, 1, /* 1 = system physical memory */
		DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, mc_vm_apt_default.high_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, 0,
			DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, mc_vm_apt_default.low_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_LOW_ADDR, 0,
			MC_VM_SYSTEM_APERTURE_LOW_ADDR, mc_vm_apt_low.quad_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR, 0,
			MC_VM_SYSTEM_APERTURE_HIGH_ADDR, mc_vm_apt_high.quad_part);

	REG_SET_2(DCN_VM_MX_L1_TLB_CNTL, 0,
			ENABLE_L1_TLB, 1,
			SYSTEM_ACCESS_MODE, 0x3);
}

void hubp2_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	hubp1_program_deadline(hubp, dlg_attr, ttu_attr);

	REG_SET(FLIP_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_FLIP_L, dlg_attr->refcyc_per_pte_group_flip_l);
}

void hubp2_vready_at_or_After_vsync(struct hubp *hubp,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	uint32_t value = 0;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	/* disable_dlg_test_mode Set 9th bit to 1 to disable "dv" mode */
	REG_WRITE(HUBPREQ_DEBUG_DB, 1 << 8);
	/*
	if (VSTARTUP_START - (VREADY_OFFSET+VUPDATE_WIDTH+VUPDATE_OFFSET)/htotal)
	<= OTG_V_BLANK_END
		Set HUBP_VREADY_AT_OR_AFTER_VSYNC = 1
	else
		Set HUBP_VREADY_AT_OR_AFTER_VSYNC = 0
	*/
	if ((pipe_dest->vstartup_start - (pipe_dest->vready_offset+pipe_dest->vupdate_width
		+ pipe_dest->vupdate_offset) / pipe_dest->htotal) <= pipe_dest->vblank_end) {
		value = 1;
	} else
		value = 0;
	REG_UPDATE(DCHUBP_CNTL, HUBP_VREADY_AT_OR_AFTER_VSYNC, value);
}

static void hubp2_setup(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */

	hubp2_vready_at_or_After_vsync(hubp, pipe_dest);
	hubp1_program_requestor(hubp, rq_regs);
	hubp2_program_deadline(hubp, dlg_attr, ttu_attr);

}

void hubp2_setup_interdependent(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_SET_2(PREFETCH_SETTINGS, 0,
			DST_Y_PREFETCH, dlg_attr->dst_y_prefetch,
			VRATIO_PREFETCH, dlg_attr->vratio_prefetch);

	REG_SET(PREFETCH_SETTINGS_C, 0,
			VRATIO_PREFETCH_C, dlg_attr->vratio_prefetch_c);

	REG_SET_2(VBLANK_PARAMETERS_0, 0,
		DST_Y_PER_VM_VBLANK, dlg_attr->dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, dlg_attr->dst_y_per_row_vblank);

	REG_SET_2(FLIP_PARAMETERS_0, 0,
		DST_Y_PER_VM_FLIP, dlg_attr->dst_y_per_vm_flip,
		DST_Y_PER_ROW_FLIP, dlg_attr->dst_y_per_row_flip);

	REG_SET(VBLANK_PARAMETERS_3, 0,
		REFCYC_PER_META_CHUNK_VBLANK_L, dlg_attr->refcyc_per_meta_chunk_vblank_l);

	REG_SET(VBLANK_PARAMETERS_4, 0,
		REFCYC_PER_META_CHUNK_VBLANK_C, dlg_attr->refcyc_per_meta_chunk_vblank_c);

	REG_SET(FLIP_PARAMETERS_2, 0,
		REFCYC_PER_META_CHUNK_FLIP_L, dlg_attr->refcyc_per_meta_chunk_flip_l);

	REG_SET_2(PER_LINE_DELIVERY_PRE, 0,
		REFCYC_PER_LINE_DELIVERY_PRE_L, dlg_attr->refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, dlg_attr->refcyc_per_line_delivery_pre_c);

	REG_SET(DCN_SURF0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_l);
	REG_SET(DCN_SURF1_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_c);
	REG_SET(DCN_CUR0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE, ttu_attr->refcyc_per_req_delivery_pre_cur0);
	REG_SET(DCN_CUR1_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE, ttu_attr->refcyc_per_req_delivery_pre_cur1);

	REG_SET_2(DCN_GLOBAL_TTU_CNTL, 0,
		MIN_TTU_VBLANK, ttu_attr->min_ttu_vblank,
		QoS_LEVEL_FLIP, ttu_attr->qos_level_flip);
}

/* DCN2 (GFX10), the following GFX fields are deprecated. They can be set but they will not be used:
 *	NUM_BANKS
 *	NUM_SE
 *	NUM_RB_PER_SE
 *	RB_ALIGNED
 * Other things can be defaulted, since they never change:
 *	PIPE_ALIGNED = 0
 *	META_LINEAR = 0
 * In GFX10, only these apply:
 *	PIPE_INTERLEAVE
 *	NUM_PIPES
 *	MAX_COMPRESSED_FRAGS
 *	SW_MODE
 */
static void hubp2_program_tiling(
	struct dcn20_hubp *hubp2,
	const union dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	REG_UPDATE_3(DCSURF_ADDR_CONFIG,
			NUM_PIPES, log_2(info->gfx9.num_pipes),
			PIPE_INTERLEAVE, info->gfx9.pipe_interleave,
			MAX_COMPRESSED_FRAGS, log_2(info->gfx9.max_compressed_frags));

	REG_UPDATE_4(DCSURF_TILING_CONFIG,
			SW_MODE, info->gfx9.swizzle,
			META_LINEAR, 0,
			RB_ALIGNED, 0,
			PIPE_ALIGNED, 0);
}

void hubp2_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	hubp1_dcc_control(hubp, dcc->enable, dcc->grph.independent_64b_blks);
	hubp2_program_tiling(hubp2, tiling_info, format);
	hubp1_program_size(hubp, format, plane_size, dcc);
	hubp1_program_rotation(hubp, rotation, horizontal_mirror);
	hubp1_program_pixel_format(hubp, format);
}

enum cursor_lines_per_chunk hubp2_get_lines_per_chunk(
	unsigned int cursor_width,
	enum dc_cursor_color_format cursor_mode)
{
	enum cursor_lines_per_chunk line_per_chunk = CURSOR_LINE_PER_CHUNK_16;

	if (cursor_mode == CURSOR_MODE_MONO)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cursor_mode == CURSOR_MODE_COLOR_1BIT_AND ||
		 cursor_mode == CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA ||
		 cursor_mode == CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA) {
		if (cursor_width >= 1   && cursor_width <= 32)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
		else if (cursor_width >= 33  && cursor_width <= 64)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_8;
		else if (cursor_width >= 65  && cursor_width <= 128)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_4;
		else if (cursor_width >= 129 && cursor_width <= 256)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_2;
	} else if (cursor_mode == CURSOR_MODE_COLOR_64BIT_FP_PRE_MULTIPLIED ||
		   cursor_mode == CURSOR_MODE_COLOR_64BIT_FP_UN_PRE_MULTIPLIED) {
		if (cursor_width >= 1   && cursor_width <= 16)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
		else if (cursor_width >= 17  && cursor_width <= 32)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_8;
		else if (cursor_width >= 33  && cursor_width <= 64)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_4;
		else if (cursor_width >= 65 && cursor_width <= 128)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_2;
		else if (cursor_width >= 129 && cursor_width <= 256)
			line_per_chunk = CURSOR_LINE_PER_CHUNK_1;
	}

	return line_per_chunk;
}

void hubp2_cursor_set_attributes(
		struct hubp *hubp,
		const struct dc_cursor_attributes *attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	enum cursor_pitch hw_pitch = hubp1_get_cursor_pitch(attr->pitch);
	enum cursor_lines_per_chunk lpc = hubp2_get_lines_per_chunk(
			attr->width, attr->color_format);

	hubp->curs_attr = *attr;

	REG_UPDATE(CURSOR_SURFACE_ADDRESS_HIGH,
			CURSOR_SURFACE_ADDRESS_HIGH, attr->address.high_part);
	REG_UPDATE(CURSOR_SURFACE_ADDRESS,
			CURSOR_SURFACE_ADDRESS, attr->address.low_part);

	REG_UPDATE_2(CURSOR_SIZE,
			CURSOR_WIDTH, attr->width,
			CURSOR_HEIGHT, attr->height);

	REG_UPDATE_4(CURSOR_CONTROL,
			CURSOR_MODE, attr->color_format,
			CURSOR_2X_MAGNIFY, attr->attribute_flags.bits.ENABLE_MAGNIFICATION,
			CURSOR_PITCH, hw_pitch,
			CURSOR_LINES_PER_CHUNK, lpc);

	REG_SET_2(CURSOR_SETTINGS, 0,
			/* no shift of the cursor HDL schedule */
			CURSOR0_DST_Y_OFFSET, 0,
			 /* used to shift the cursor chunk request deadline */
			CURSOR0_CHUNK_HDL_ADJUST, 3);
}

void hubp2_dmdata_set_attributes(
		struct hubp *hubp,
		const struct dc_dmdata_attributes *attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	if (attr->dmdata_mode == DMDATA_HW_MODE) {
		/* set to HW mode */
		REG_UPDATE(DMDATA_CNTL,
				DMDATA_MODE, 1);

		/* for DMDATA flip, need to use SURFACE_UPDATE_LOCK */
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_UPDATE_LOCK, 1);

		/* toggle DMDATA_UPDATED and set repeat and size */
		REG_UPDATE(DMDATA_CNTL,
				DMDATA_UPDATED, 0);
		REG_UPDATE_3(DMDATA_CNTL,
				DMDATA_UPDATED, 1,
				DMDATA_REPEAT, attr->dmdata_repeat,
				DMDATA_SIZE, attr->dmdata_size);

		/* set DMDATA address */
		REG_WRITE(DMDATA_ADDRESS_LOW, attr->address.low_part);
		REG_UPDATE(DMDATA_ADDRESS_HIGH,
				DMDATA_ADDRESS_HIGH, attr->address.high_part);

		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_UPDATE_LOCK, 0);

	} else {
		/* set to SW mode before loading data */
		REG_SET(DMDATA_CNTL, 0,
				DMDATA_MODE, 0);
		/* toggle DMDATA_SW_UPDATED to start loading sequence */
		REG_UPDATE(DMDATA_SW_CNTL,
				DMDATA_SW_UPDATED, 0);
		REG_UPDATE_3(DMDATA_SW_CNTL,
				DMDATA_SW_UPDATED, 1,
				DMDATA_SW_REPEAT, attr->dmdata_repeat,
				DMDATA_SW_SIZE, attr->dmdata_size);
		/* load data into hubp dmdata buffer */
		hubp2_dmdata_load(hubp, attr->dmdata_size, attr->dmdata_sw_data);
	}

	/* Note that DL_DELTA must be programmed if we want to use TTU mode */
	REG_SET_3(DMDATA_QOS_CNTL, 0,
			DMDATA_QOS_MODE, attr->dmdata_qos_mode,
			DMDATA_QOS_LEVEL, attr->dmdata_qos_level,
			DMDATA_DL_DELTA, attr->dmdata_dl_delta);
}

void hubp2_dmdata_load(
		struct hubp *hubp,
		uint32_t dmdata_sw_size,
		const uint32_t *dmdata_sw_data)
{
	int i;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	/* load dmdata into HUBP buffer in SW mode */
	for (i = 0; i < dmdata_sw_size / 4; i++)
		REG_WRITE(DMDATA_SW_DATA, dmdata_sw_data[i]);
}

bool hubp2_dmdata_status_done(struct hubp *hubp)
{
	uint32_t status;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_GET(DMDATA_STATUS, DMDATA_DONE, &status);
	return (status == 1);
}

bool hubp2_program_surface_flip_and_addr(
	struct hubp *hubp,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	//program flip type
	REG_UPDATE(DCSURF_FLIP_CONTROL,
			SURFACE_FLIP_TYPE, flip_immediate);

	// Program VMID reg
	REG_UPDATE(VMID_SETTINGS_0,
			VMID, address->vmid);

	if (address->type == PLN_ADDR_TYPE_GRPH_STEREO) {
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0x1);
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_IN_STEREOSYNC, 0x1);

	} else {
		// turn off stereo if not in stereo
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0x0);
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_IN_STEREOSYNC, 0x0);
	}



	/* HW automatically latch rest of address register on write to
	 * DCSURF_PRIMARY_SURFACE_ADDRESS if SURFACE_UPDATE_LOCK is not used
	 *
	 * program high first and then the low addr, order matters!
	 */
	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		/* DCN1.0 does not support const color
		 * TODO: program DCHUBBUB_RET_PATH_DCC_CFGx_0/1
		 * base on address->grph.dcc_const_color
		 * x = 0, 2, 4, 6 for pipe 0, 1, 2, 3 for rgb and luma
		 * x = 1, 3, 5, 7 for pipe 0, 1, 2, 3 for chroma
		 */

		if (address->grph.addr.quad_part == 0)
			break;

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface);

		if (address->grph.meta_addr.quad_part != 0) {
			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->grph.meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->grph.meta_addr.low_part);
		}

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph.addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph.addr.low_part);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		if (address->video_progressive.luma_addr.quad_part == 0
				|| address->video_progressive.chroma_addr.quad_part == 0)
			break;

		REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ_C, address->tmz_surface);

		if (address->video_progressive.luma_meta_addr.quad_part != 0) {
			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH_C,
					address->video_progressive.chroma_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, 0,
					PRIMARY_META_SURFACE_ADDRESS_C,
					address->video_progressive.chroma_meta_addr.low_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->video_progressive.luma_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->video_progressive.luma_meta_addr.low_part);
		}

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH_C,
				address->video_progressive.chroma_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
				PRIMARY_SURFACE_ADDRESS_C,
				address->video_progressive.chroma_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->video_progressive.luma_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->video_progressive.luma_addr.low_part);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0)
			break;
		if (address->grph_stereo.right_addr.quad_part == 0)
			break;

		REG_UPDATE_8(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_META_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_META_SURFACE_TMZ_C, address->tmz_surface);

		if (address->grph_stereo.right_meta_addr.quad_part != 0) {

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH, 0,
					SECONDARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.right_meta_addr.high_part);

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS, 0,
					SECONDARY_META_SURFACE_ADDRESS,
					address->grph_stereo.right_meta_addr.low_part);
		}
		if (address->grph_stereo.left_meta_addr.quad_part != 0) {

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.left_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->grph_stereo.left_meta_addr.low_part);
		}

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.right_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS, 0,
				SECONDARY_SURFACE_ADDRESS,
				address->grph_stereo.right_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.left_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph_stereo.left_addr.low_part);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	hubp->request_address = *address;

	return true;
}

void hubp2_enable_triplebuffer(
	struct hubp *hubp,
	bool enable)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	uint32_t triple_buffer_en = 0;
	bool tri_buffer_en;

	REG_GET(DCSURF_FLIP_CONTROL2, SURFACE_TRIPLE_BUFFER_ENABLE, &triple_buffer_en);
	tri_buffer_en = (triple_buffer_en == 1);
	if (tri_buffer_en != enable) {
		REG_UPDATE(DCSURF_FLIP_CONTROL2,
			SURFACE_TRIPLE_BUFFER_ENABLE, enable ? DC_TRIPLEBUFFER_ENABLE : DC_TRIPLEBUFFER_DISABLE);
	}
}

bool hubp2_is_triplebuffer_enabled(
	struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	uint32_t triple_buffer_en = 0;

	REG_GET(DCSURF_FLIP_CONTROL2, SURFACE_TRIPLE_BUFFER_ENABLE, &triple_buffer_en);

	return (bool)triple_buffer_en;
}

void hubp2_set_flip_control_surface_gsl(struct hubp *hubp, bool enable)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(DCSURF_FLIP_CONTROL2, SURFACE_GSL_ENABLE, enable ? 1 : 0);
}

static struct hubp_funcs dcn20_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp2_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp2_program_surface_config,
	.hubp_is_flip_pending = hubp1_is_flip_pending,
	.hubp_setup = hubp2_setup,
	.hubp_setup_interdependent = hubp2_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp2_set_vm_system_aperture_settings,
	.set_blank = hubp1_set_blank,
	.dcc_control = hubp1_dcc_control,
	.hubp_update_dchub = hubp2_update_dchub,
	.mem_program_viewport = min_set_viewport,
	.set_cursor_attributes	= hubp2_cursor_set_attributes,
	.set_cursor_position	= hubp1_cursor_set_position,
	.hubp_clk_cntl = hubp1_clk_cntl,
	.hubp_vtg_sel = hubp1_vtg_sel,
	.dmdata_set_attributes = hubp2_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp1_read_state,
	.hubp_clear_underflow = hubp1_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp1_init,
};


bool hubp2_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn20_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
