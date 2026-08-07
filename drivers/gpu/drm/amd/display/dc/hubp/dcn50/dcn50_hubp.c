// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dce_calcs.h"
#include "reg_helper.h"
#include "basics/conversion.h"
#include "dcn50_hubp.h"
#include "dcn42/dcn42_hubp.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

bool hubp50_program_surface_flip_and_addr(
	struct hubp *hubp,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	//program flip type
	REG_UPDATE(DCSURF_FLIP_CONTROL,
			SURFACE_FLIP_TYPE, flip_immediate);

	// Program VMID reg
	if (flip_immediate == 0)
		REG_UPDATE(VMID_SETTINGS_0,
			VMID, address->vmid);

	if (address->type == PLN_ADDR_TYPE_GRPH_STEREO) {
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0);
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
		if (address->grph.addr.quad_part == 0)
			break;

		REG_UPDATE(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface);

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

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface);

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
		if (address->grph_stereo.left_addr.quad_part == 0
			|| address->grph_stereo.right_addr.quad_part == 0)
			break;

		REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_SURFACE_TMZ_C, address->tmz_surface);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH_C, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.right_alpha_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_C, 0,
				SECONDARY_SURFACE_ADDRESS_C,
				address->grph_stereo.right_alpha_addr.low_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.right_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS, 0,
				SECONDARY_SURFACE_ADDRESS,
				address->grph_stereo.right_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.left_alpha_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
				PRIMARY_SURFACE_ADDRESS_C,
				address->grph_stereo.left_alpha_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.left_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph_stereo.left_addr.low_part);
		break;
	case PLN_ADDR_TYPE_RGBEA:
		if (address->rgbea.addr.quad_part == 0
				|| address->rgbea.alpha_addr.quad_part == 0)
			break;

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH_C,
				address->rgbea.alpha_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
				PRIMARY_SURFACE_ADDRESS_C,
				address->rgbea.alpha_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->rgbea.addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->rgbea.addr.low_part);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	hubp->request_address = *address;

	return true;
}

static enum swizzle_mode_values hubp50_addr3_to_swizzle_mode_mapping(enum swizzle_mode_addr3_values addr3_swizzle_mode)
{
	enum swizzle_mode_values swizzle_mode = DC_SW_UNKNOWN;

	switch (addr3_swizzle_mode) {
	case DC_ADDR3_SW_LINEAR:
		swizzle_mode = DC_SW_LINEAR;
		break;
	case DC_ADDR3_SW_256B_2D:
		swizzle_mode = DC_SW_256_R;
		break;
	case DC_ADDR3_SW_4KB_2D:
		swizzle_mode = DC_SW_4KB_R;
		break;
	case DC_ADDR3_SW_64KB_2D:
	case DC_ADDR3_SW_64KB_2D_Z:
		swizzle_mode = DC_SW_64KB_R;
		break;
	case DC_ADDR3_SW_256KB_2D:
	case DC_ADDR3_SW_256KB_2D_Z:
		swizzle_mode = DC_SW_VAR_R;
		break;
	case DC_ADDR3_SW_4KB_3D:
	case DC_ADDR3_SW_64KB_3D:
	case DC_ADDR3_SW_256KB_3D:
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	return swizzle_mode;
}

static void hubp50_program_tiling(
	struct dcn20_hubp *hubp2,
	const struct dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	(void)pixel_format;
	/* Tiling address-generation compatibility level programmed into the
	 * DCSURF_TILING_CONFIG.COMPAT_LEVEL register field:
	 *   0 - gfx8 bank_height == 1
	 *   1 - gfx8 bank_height == 2
	 *   2 - gfx9/10/11
	 *   5 - gfx addr3
	 */
	unsigned int compat_level = 0;
	enum swizzle_mode_values swizzle_mode;

	switch (info->gfxversion) {
	case DcGfxVersion7:
	case DcGfxVersion8:

		REG_UPDATE_8(DCSURF_LEGACY_ADDR_CONFIG,
			LEGACY_NUM_BANKS, info->gfx8.num_banks,
			BANK_WIDTH, info->gfx8.bank_width,
			BANK_HEIGHT, info->gfx8.bank_height,
			MACRO_TILE_ASPECT, info->gfx8.tile_aspect,
			TILE_SPLIT, info->gfx8.tile_split,
			MICRO_TILE_MODE_NEW, info->gfx8.tile_mode,
			PIPE_CONFIG, info->gfx8.pipe_config,
			ARRAY_MODE, info->gfx8.array_mode);
		if (info->gfx8.bank_height == 1) {
			compat_level = 0;
		} else if (info->gfx8.bank_height == 2) {
			compat_level = 1;
		}
		break;

	case DcGfxVersion9:
	case DcGfxVersion10:
	case DcGfxVersion11:
		REG_UPDATE_4(DCSURF_ADDR_CONFIG,
			NUM_PIPES, log_2(info->gfx9.num_pipes),
			PIPE_INTERLEAVE, info->gfx9.pipe_interleave,
			MAX_COMPRESSED_FRAGS, log_2(info->gfx9.max_compressed_frags),
			NUM_PKRS, log_2(info->gfx9.num_pkrs));
		REG_UPDATE(DCSURF_TILING_CONFIG, SW_MODE, info->gfx9.swizzle);
		compat_level = 2;

		break;
	case DcGfxAddr3:
		swizzle_mode = hubp50_addr3_to_swizzle_mode_mapping(info->gfx_addr3.swizzle);
		REG_UPDATE(DCSURF_TILING_CONFIG, SW_MODE, swizzle_mode);
		compat_level = 5;
		break;
	}

	REG_UPDATE(DCSURF_TILING_CONFIG, COMPAT_LEVEL, compat_level);
}
static void hubp50_program_pixel_format(
	struct hubp *hubp,
	enum surface_pixel_format format)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	uint32_t red_bar = 3;
	uint32_t blue_bar = 2;

	/* swap for ABGR format */
	if (format == SURFACE_PIXEL_FORMAT_GRPH_ABGR8888
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F) {
		red_bar = 2;
		blue_bar = 3;
	}

	REG_UPDATE_2(HUBPRET_CONTROL,
			CROSSBAR_SRC_CB_B, blue_bar,
			CROSSBAR_SRC_CR_R, red_bar);

	/* Mapping is same as ipp programming (cnvc) */

	switch (format)	{
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 1);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 3);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 8);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 10);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616: /* we use crossbar already */
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 26); /* ARGB16161616_UNORM */
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:/*we use crossbar already*/
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 24);
		break;

	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 65);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 64);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 67);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 66);
		break;
	/* Planar 422 formats*/
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P208:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 64);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P208:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 65);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P210:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 66);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P210:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 67);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P212:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 68);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P212:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 69);
		break;
	/* Packed 422 formats*/
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 72);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 73);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 74);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 75);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 76);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 77);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 78);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 79);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 80);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 81);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 82);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 83);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 12);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 112);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 113);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 114);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 118);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 119);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 116,
				ALPHA_PLANE_EN, 0);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 116,
				ALPHA_PLANE_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void hubp50_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	struct dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level)
{
	(void)compat_level;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	hubp401_dcc_control(hubp, dcc);
	hubp50_program_tiling(hubp2, tiling_info, format);
	hubp401_program_size(hubp, format, plane_size, dcc);
	hubp2_program_rotation(hubp, rotation, horizontal_mirror);
	hubp50_program_pixel_format(hubp, format);
}

void hubp50_read_state(struct hubp *hubp)
{
	hubp401_read_state(hubp);
}

static struct hubp_funcs dcn50_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp50_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp50_program_surface_config,
	.hubp_is_flip_pending = hubp2_is_flip_pending,
	.hubp_setup2 = hubp401_setup,
	.hubp_setup_interdependent2 = hubp401_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp3_set_vm_system_aperture_settings,
	.set_blank = hubp2_set_blank,
	.set_blank_regs = hubp2_set_blank_regs,
	.hubp_reset = hubp_reset,
	.mem_program_viewport = hubp401_set_viewport,
	.set_cursor_attributes	= hubp32_cursor_set_attributes,
	.set_cursor_position	= hubp401_cursor_set_position,
	.hubp_clk_cntl = hubp2_clk_cntl,
	.hubp_vtg_sel = hubp2_vtg_sel,
	.dmdata_set_attributes = hubp3_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp50_read_state,
	.hubp_clear_underflow = hubp2_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp401_init,
	.set_unbounded_requesting = hubp401_set_unbounded_requesting,
	.hubp_soft_reset = hubp31_soft_reset,
	.hubp_set_flip_int = hubp401_set_flip_int,
	.hubp_in_blank = hubp401_in_blank,
	.phantom_hubp_post_enable = hubp32_phantom_hubp_post_enable,
	.hubp_update_mall_sel = hubp401_update_mall_sel,
	.hubp_prepare_subvp_buffering = hubp32_prepare_subvp_buffering,
	.hubp_program_mcache_id_and_split_coordinate = hubp401_program_mcache_id_and_split_coordinate,
	.hubp_program_3dlut_fl_addr = hubp401_program_3dlut_fl_addr,
	.hubp_program_3dlut_fl_config = hubp42_program_3dlut_fl_config,
	.hubp_program_3dlut_fl_dlg_param = hubp401_program_3dlut_fl_dlg_param,
	.hubp_enable_3dlut_fl = hubp401_enable_3dlut_fl,
	.hubp_program_3dlut_fl_crossbar = hubp42_program_3dlut_fl_crossbar,
	.hubp_get_3dlut_fl_done = hubp401_get_3dlut_fl_done,
	.hubp_clear_tiling = hubp401_clear_tiling,
	.hubp_read_reg_state = hubp3_read_reg_state
};

bool hubp50_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn50_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
