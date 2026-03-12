// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "dcn30/dcn30_cm_common.h"
#include "reg_helper.h"
#include "abm.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dc_dmub_srv.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dcn401/dcn401_hwseq.h"
#include "dcn42_hwseq.h"
#include "clk_mgr.h"
#include "dsc.h"
#include "dcn20/dcn20_optc.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "dcn42/dcn42_resource.h"
#include "link_service.h"
#include "../dcn10/dcn10_hwseq.h"
#include "../dcn20/dcn20_hwseq.h"
#include "dc_state_priv.h"
#include "dc_stream_priv.h"
#include "dcn35/dcn35_hwseq.h"
#include "dcn42/dcn42_hwseq.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "dio/dcn10/dcn10_dio.h"

#define DC_LOGGER \
	ctx->logger

#define CTX \
	hws->ctx

#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name


static void print_pg_status(struct dc *dc, const char *debug_func, const char *debug_log)
{
	if (dc->debug.enable_pg_cntl_debug_logs && dc->res_pool->pg_cntl) {
		if (dc->res_pool->pg_cntl->funcs->print_pg_status)
			dc->res_pool->pg_cntl->funcs->print_pg_status(dc->res_pool->pg_cntl, debug_func, debug_log);
	}
}
void dcn42_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	int i;
	int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;
	int current_dchub_ref_freq = 0;

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->init_clocks) {
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

		// mark dcmode limits present if any clock has distinct AC and DC values from SMU
		dc->caps.dcmode_power_limits_present = dc->clk_mgr->funcs->is_dc_mode_present &&
				dc->clk_mgr->funcs->is_dc_mode_present(dc->clk_mgr);
	}

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	// Disable DMUB Initialization until IPS state programming is finalized
	//if (!dcb->funcs->is_accelerated_mode(dcb)) {
	//	hws->funcs.bios_golden_init(dc);
	//}

	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->hubbub) {
			(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
					dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
					&res_pool->ref_clocks.dccg_ref_clock_inKhz);

			current_dchub_ref_freq = res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;

			(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
					res_pool->ref_clocks.dccg_ref_clock_inKhz,
					&res_pool->ref_clocks.dchub_ref_clock_inKhz);
		} else {
			// Not all ASICs have DCCG sw component
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
		}
	} else
		ASSERT_CRITICAL(false);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		if (link->ep_type != DISPLAY_ENDPOINT_PHY)
			continue;

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* enable_power_gating_plane before dsc_pg_control because
	 * FORCEON = 1 with hw default value on bootup, resume from s3
	 */
	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {
		/* Disable boot optimizations means power down everything including PHY, DIG,
		 * and OTG (i.e. the boot is not optimized because we do a full power down).
		 */
		if (dc->hwss.enable_accelerated_mode && dc->debug.disable_boot_optimizations)
			dc->hwss.enable_accelerated_mode(dc, dc->current_state);
		else
			hws->funcs.init_pipes(dc, dc->current_state);

		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);

	}

	/* In headless boot cases, DIG may be turned
	 * on which causes HW/SW discrepancies.
	 * To avoid this, power down hardware on boot
	 * if DIG is turned on and seamless boot not enabled
	 */
	if (!dc->config.seamless_boot_edp_requested) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		struct dc_link *edp_link;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num) {
			for (i = 0; i < edp_num; i++) {
				edp_link = edp_links[i];
				if (edp_link->link_enc->funcs->is_dig_enabled &&
						edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
						dc->hwss.edp_backlight_control &&
						hws->funcs.power_down &&
						dc->hwss.edp_power_control) {
					dc->hwss.edp_backlight_control(edp_link, false);
					hws->funcs.power_down(dc);
					dc->hwss.edp_power_control(edp_link, false);
				}
			}
		} else {
			for (i = 0; i < dc->link_count; i++) {
				struct dc_link *link = dc->links[i];

				if (link->link_enc->funcs->is_dig_enabled &&
						link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
						hws->funcs.power_down) {
					hws->funcs.power_down(dc);
					break;
				}

			}
		}
	}

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl) {
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
			user_level = link->panel_cntl->stored_backlight_registers.USER_LEVEL;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	if (dc->res_pool->dio && dc->res_pool->dio->funcs->mem_pwr_ctrl)
		dc->res_pool->dio->funcs->mem_pwr_ctrl(dc->res_pool->dio, false);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	dcn401_setup_hpo_hw_control(hws, true);

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub, dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);

	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver > 0;
		dc->caps.dmub_caps.fams_ver = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
		dc->debug.fams2_config.bits.enable &=
				dc->caps.dmub_caps.fams_ver == dc->debug.fams_version.ver; // sw & fw fams versions must match for support
		if ((!dc->debug.fams2_config.bits.enable && dc->res_pool->funcs->update_bw_bounding_box)
			|| res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000 != current_dchub_ref_freq) {
			/* update bounding box if FAMS2 disabled, or if dchub clk has changed */
			if (dc->clk_mgr)
				dc->res_pool->funcs->update_bw_bounding_box(dc,
									    dc->clk_mgr->bw_params);
		}
	}
	if (dc->res_pool->pg_cntl) {
		if (dc->res_pool->pg_cntl->funcs->init_pg_status)
			dc->res_pool->pg_cntl->funcs->init_pg_status(dc->res_pool->pg_cntl);
	}
	print_pg_status(dc, __func__, ": after init_pg_status");
}

void dcn42_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct mpcc_blnd_cfg blnd_cfg = {0};
	bool per_pixel_alpha = pipe_ctx->plane_state->per_pixel_alpha;
	int mpcc_id;
	struct mpcc *new_mpcc;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params = &(pipe_ctx->stream_res.opp->mpc_tree_params);


	blnd_cfg.overlap_only = false;
	blnd_cfg.global_gain = 0xfff;

	if (per_pixel_alpha) {
		blnd_cfg.pre_multiplied_alpha = pipe_ctx->plane_state->pre_multiplied_alpha;
		if (pipe_ctx->plane_state->global_alpha) {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN;
			blnd_cfg.global_gain = pipe_ctx->plane_state->global_alpha_value;
		} else {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA;
		}
	} else {
		blnd_cfg.pre_multiplied_alpha = false;
		blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
	}

	if (pipe_ctx->plane_state->global_alpha)
		blnd_cfg.global_alpha = pipe_ctx->plane_state->global_alpha_value;
	else
		blnd_cfg.global_alpha = 0xfff;

	blnd_cfg.background_color_bpc = 4;
	blnd_cfg.bottom_gain_mode = 0;
	blnd_cfg.top_gain = 0x1f000;
	blnd_cfg.bottom_inside_gain = 0x1f000;
	blnd_cfg.bottom_outside_gain = 0x1f000;

	if (pipe_ctx->plane_state->format
			== SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA)
		blnd_cfg.pre_multiplied_alpha = false;

	/*
	 * TODO: remove hack
	 * Note: currently there is a bug in init_hw such that
	 * on resume from hibernate, BIOS sets up MPCC0, and
	 * we do mpcc_remove but the mpcc cannot go to idle
	 * after remove. This cause us to pick mpcc1 here,
	 * which causes a pstate hang for yet unknown reason.
	 */
	mpcc_id = hubp->inst;

	/* If there is no full update, don't need to touch MPC tree*/
	if (!pipe_ctx->plane_state->update_flags.bits.full_update &&
		!pipe_ctx->update_flags.bits.mpcc) {
		mpc->funcs->update_blending(mpc, &blnd_cfg, mpcc_id);
		dc->hwss.update_visual_confirm_color(dc, pipe_ctx, mpcc_id);
		return;
	}

	/* check if this MPCC is already being used */
	new_mpcc = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, mpcc_id);
	/* remove MPCC if being used */
	if (new_mpcc != NULL)
		mpc->funcs->remove_mpcc(mpc, mpc_tree_params, new_mpcc);
	else
		if (dc->debug.sanity_checks)
			mpc->funcs->assert_mpcc_idle_before_connect(
					dc->res_pool->mpc, mpcc_id);

	/* Call MPC to insert new plane */
	new_mpcc = mpc->funcs->insert_plane(dc->res_pool->mpc,
			mpc_tree_params,
			&blnd_cfg,
			NULL,
			NULL,
			hubp->inst,
			mpcc_id);
	dc->hwss.update_visual_confirm_color(dc, pipe_ctx, mpcc_id);

	ASSERT(new_mpcc != NULL);
	hubp->opp_id = pipe_ctx->stream_res.opp->inst;
	hubp->mpcc_id = mpcc_id;
}

void dcn42_program_cm_hist(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	const struct dc_plane_state *plane_state)
{
	struct dpp *dpp = pipe_ctx->plane_res.dpp;

	if (dpp && dpp->funcs->dpp_cm_hist_control)
		dpp->funcs->dpp_cm_hist_control(dpp,
			plane_state->cm_hist_control, plane_state->color_space);
}

static void dc_get_lut_xbar(
	enum dc_cm2_gpu_mem_pixel_component_order order,
	enum hubp_3dlut_fl_crossbar_bit_slice *cr_r,
	enum hubp_3dlut_fl_crossbar_bit_slice *y_g,
	enum hubp_3dlut_fl_crossbar_bit_slice *cb_b)
{
	switch (order) {
	case DC_CM2_GPU_MEM_PIXEL_COMPONENT_ORDER_RGBA:
		*cr_r = hubp_3dlut_fl_crossbar_bit_slice_32_47;
		*y_g = hubp_3dlut_fl_crossbar_bit_slice_16_31;
		*cb_b =  hubp_3dlut_fl_crossbar_bit_slice_0_15;
		break;
	case DC_CM2_GPU_MEM_PIXEL_COMPONENT_ORDER_BGRA:
		*cr_r = hubp_3dlut_fl_crossbar_bit_slice_0_15;
		*y_g = hubp_3dlut_fl_crossbar_bit_slice_16_31;
		*cb_b = hubp_3dlut_fl_crossbar_bit_slice_32_47;
		break;
	}
}

static void dc_get_lut_mode(
	enum dc_cm2_gpu_mem_layout layout,
	enum hubp_3dlut_fl_mode *mode,
	enum hubp_3dlut_fl_addressing_mode *addr_mode)
{
	switch (layout) {
	case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_RGB:
		*mode = hubp_3dlut_fl_mode_native_1;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_BGR:
		*mode = hubp_3dlut_fl_mode_native_2;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	case DC_CM2_GPU_MEM_LAYOUT_1D_PACKED_LINEAR:
		*mode = hubp_3dlut_fl_mode_transform;
		*addr_mode = hubp_3dlut_fl_addressing_mode_simple_linear;
		break;
	default:
		*mode = hubp_3dlut_fl_mode_disable;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	}
}

static void dc_get_lut_format(
	enum dc_cm2_gpu_mem_format dc_format,
	enum hubp_3dlut_fl_format *format)
{
	switch (dc_format) {
	case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12MSB:
		*format = hubp_3dlut_fl_format_unorm_12msb_bitslice;
		break;
	case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12LSB:
		*format = hubp_3dlut_fl_format_unorm_12lsb_bitslice;
		break;
	case DC_CM2_GPU_MEM_FORMAT_16161616_FLOAT_FP1_5_10:
		*format = hubp_3dlut_fl_format_float_fp1_5_10;
		break;
	}
}

static bool dc_is_rmcm_3dlut_supported(struct hubp *hubp, struct mpc *mpc)
{
	if (mpc->funcs->rmcm.power_on_shaper_3dlut &&
		mpc->funcs->rmcm.fl_3dlut_configure &&
		hubp->funcs->hubp_program_3dlut_fl_config)
		return true;

	return false;
}

static bool is_rmcm_3dlut_fl_supported(struct dc *dc, enum dc_cm2_gpu_mem_size size)
{
	if (!dc->caps.color.mpc.rmcm_3d_lut_caps.dma_3d_lut)
		return false;
	if (size == DC_CM2_GPU_MEM_SIZE_171717)
		return (dc->caps.color.mpc.rmcm_3d_lut_caps.lut_dim_caps.dim_17);
	else if (size == DC_CM2_GPU_MEM_SIZE_333333)
		return (dc->caps.color.mpc.rmcm_3d_lut_caps.lut_dim_caps.dim_33);
	return false;
}

static void dcn42_set_mcm_location_post_blend(struct dc *dc, struct pipe_ctx *pipe_ctx, bool bPostBlend)
{
	struct mpc *mpc = dc->res_pool->mpc;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;

	if (!pipe_ctx->plane_state)
		return;

	mpc->funcs->set_movable_cm_location(mpc, MPCC_MOVABLE_CM_LOCATION_BEFORE, mpcc_id);
	pipe_ctx->plane_state->mcm_location = (bPostBlend) ?
											MPCC_MOVABLE_CM_LOCATION_AFTER :
											MPCC_MOVABLE_CM_LOCATION_BEFORE;
}

static void dcn42_get_mcm_lut_xable_from_pipe_ctx(struct dc *dc, struct pipe_ctx *pipe_ctx,
		enum MCM_LUT_XABLE *shaper_xable,
		enum MCM_LUT_XABLE *lut3d_xable,
		enum MCM_LUT_XABLE *lut1d_xable)
{
	enum dc_cm2_shaper_3dlut_setting shaper_3dlut_setting = DC_CM2_SHAPER_3DLUT_SETTING_BYPASS_ALL;
	bool lut1d_enable = false;
	struct mpc *mpc = dc->res_pool->mpc;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;

	if (!pipe_ctx->plane_state)
		return;
	shaper_3dlut_setting = pipe_ctx->plane_state->mcm_shaper_3dlut_setting;
	lut1d_enable = pipe_ctx->plane_state->mcm_lut1d_enable;
	mpc->funcs->set_movable_cm_location(mpc, MPCC_MOVABLE_CM_LOCATION_BEFORE, mpcc_id);
	pipe_ctx->plane_state->mcm_location = MPCC_MOVABLE_CM_LOCATION_BEFORE;

	*lut1d_xable = lut1d_enable ? MCM_LUT_ENABLE : MCM_LUT_DISABLE;

	switch (shaper_3dlut_setting) {
	case DC_CM2_SHAPER_3DLUT_SETTING_BYPASS_ALL:
		*lut3d_xable = *shaper_xable = MCM_LUT_DISABLE;
		break;
	case DC_CM2_SHAPER_3DLUT_SETTING_ENABLE_SHAPER:
		*lut3d_xable = MCM_LUT_DISABLE;
		*shaper_xable = MCM_LUT_ENABLE;
		break;
	case DC_CM2_SHAPER_3DLUT_SETTING_ENABLE_SHAPER_3DLUT:
		*lut3d_xable = *shaper_xable = MCM_LUT_ENABLE;
		break;
	}
}

static void fl_get_lut_mode(
	enum dc_cm2_gpu_mem_layout layout,
	enum dc_cm2_gpu_mem_size   size,
	enum hubp_3dlut_fl_mode *mode,
	enum hubp_3dlut_fl_addressing_mode *addr_mode,
	enum hubp_3dlut_fl_width *width)
{
	*width = hubp_3dlut_fl_width_17;

	if (size == DC_CM2_GPU_MEM_SIZE_333333)
		*width = hubp_3dlut_fl_width_33;

	switch (layout) {
	case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_RGB:
		*mode = hubp_3dlut_fl_mode_native_1;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_BGR:
		*mode = hubp_3dlut_fl_mode_native_2;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	case DC_CM2_GPU_MEM_LAYOUT_1D_PACKED_LINEAR:
		*mode = hubp_3dlut_fl_mode_transform;
		*addr_mode = hubp_3dlut_fl_addressing_mode_simple_linear;
		break;
	default:
		*mode = hubp_3dlut_fl_mode_disable;
		*addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
		break;
	}
}

bool dcn42_program_rmcm_luts(
	struct hubp *hubp,
	struct pipe_ctx *pipe_ctx,
	enum dc_cm2_transfer_func_source lut3d_src,
	struct dc_cm2_func_luts *mcm_luts,
	struct mpc *mpc,
	bool lut_bank_a,
	int mpcc_id)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	union mcm_lut_params m_lut_params = {0};
	enum MCM_LUT_XABLE shaper_xable, lut3d_xable = MCM_LUT_DISABLE, lut1d_xable;
		enum hubp_3dlut_fl_mode mode;
	enum hubp_3dlut_fl_addressing_mode addr_mode;
	enum hubp_3dlut_fl_format format = hubp_3dlut_fl_format_unorm_12msb_bitslice;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_y_g = hubp_3dlut_fl_crossbar_bit_slice_16_31;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cb_b = hubp_3dlut_fl_crossbar_bit_slice_0_15;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cr_r = hubp_3dlut_fl_crossbar_bit_slice_32_47;
	enum hubp_3dlut_fl_width width = hubp_3dlut_fl_width_17;


	struct dc *dc = hubp->ctx->dc;
	struct hubp_fl_3dlut_config fl_config;
	struct mpc_fl_3dlut_config mpc_fl_config;

	struct dc_stream_state *stream = pipe_ctx->stream;
	bool bypass_rmcm_shaper = false;
	// true->false when it can be allocated at DI time
	struct dc_rmcm_3dlut *rmcm_3dlut = dc_stream_get_3dlut_for_stream(dc, stream, false);

	//check to see current pipe is part of a stream with allocated rmcm 3dlut
	if (!rmcm_3dlut)
		return false;

	rmcm_3dlut->protection_bits = mcm_luts->lut3d_data.rmcm_tmz;

	dcn42_get_mcm_lut_xable_from_pipe_ctx(dc, pipe_ctx, &shaper_xable, &lut3d_xable, &lut1d_xable);

	/* Shaper */
	if (mcm_luts->shaper) {
		memset(&m_lut_params, 0, sizeof(m_lut_params));

		if (mcm_luts->shaper->type == TF_TYPE_HWPWL) {
			m_lut_params.pwl = &mcm_luts->shaper->pwl;
		} else if (mcm_luts->shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
			ASSERT(false);
			cm_helper_translate_curve_to_hw_format(
					dc->ctx,
					mcm_luts->shaper,
					&dpp_base->shaper_params, true);
			m_lut_params.pwl = &dpp_base->shaper_params;
		}
		if (m_lut_params.pwl) {
			if (mpc->funcs->rmcm.populate_lut)
				mpc->funcs->rmcm.populate_lut(mpc, m_lut_params, lut_bank_a, mpcc_id);
			if (mpc->funcs->rmcm.program_lut_mode)
				mpc->funcs->rmcm.program_lut_mode(mpc, !bypass_rmcm_shaper, lut_bank_a, mpcc_id);
		} else {
			//RMCM 3dlut won't work without its shaper
			return false;
		}
	}

	/* 3DLUT */
	switch (lut3d_src) {
	case DC_CM2_TRANSFER_FUNC_SOURCE_SYSMEM:
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		// Don't know what to do in this case.
		//case DC_CM2_TRANSFER_FUNC_SOURCE_SYSMEM:
		break;
	case DC_CM2_TRANSFER_FUNC_SOURCE_VIDMEM:
		fl_get_lut_mode(mcm_luts->lut3d_data.gpu_mem_params.layout,
				mcm_luts->lut3d_data.gpu_mem_params.size,
				&mode,
				&addr_mode,
				&width);

		if (!dc_is_rmcm_3dlut_supported(hubp, mpc) ||
			!mpc->funcs->rmcm.is_config_supported(
				(width == hubp_3dlut_fl_width_17 ||
				 width == hubp_3dlut_fl_width_transformed) ? 17 : 33))
			return false;

		// setting native or transformed mode,
		dc_get_lut_mode(mcm_luts->lut3d_data.gpu_mem_params.layout, &mode, &addr_mode);

		//seems to be only for the MCM
		dc_get_lut_format(mcm_luts->lut3d_data.gpu_mem_params.format_params.format, &format);

		dc_get_lut_xbar(
			mcm_luts->lut3d_data.gpu_mem_params.component_order,
			&crossbar_bit_slice_cr_r,
			&crossbar_bit_slice_y_g,
			&crossbar_bit_slice_cb_b);

		fl_config.mode					= mode;
		fl_config.enabled				= lut3d_xable != MCM_LUT_DISABLE;
		fl_config.address				= mcm_luts->lut3d_data.gpu_mem_params.addr;
		fl_config.format				= format;
		fl_config.crossbar_bit_slice_y_g  = crossbar_bit_slice_y_g;
		fl_config.crossbar_bit_slice_cb_b = crossbar_bit_slice_cb_b;
		fl_config.crossbar_bit_slice_cr_r = crossbar_bit_slice_cr_r;
		fl_config.width				    = width;
		fl_config.protection_bits		= rmcm_3dlut->protection_bits;
		fl_config.addr_mode			    = addr_mode;
		fl_config.layout                = mcm_luts->lut3d_data.gpu_mem_params.layout;
		fl_config.bias	= mcm_luts->lut3d_data.gpu_mem_params.format_params.float_params.bias;
		fl_config.scale	= mcm_luts->lut3d_data.gpu_mem_params.format_params.float_params.scale;

		mpc_fl_config.enabled			= fl_config.enabled;
		mpc_fl_config.width	            = width;
		mpc_fl_config.select_lut_bank_a = lut_bank_a;
		mpc_fl_config.bit_depth		    = mcm_luts->lut3d_data.gpu_mem_params.bit_depth;
		mpc_fl_config.hubp_index		= hubp->inst;
		mpc_fl_config.bias	= mcm_luts->lut3d_data.gpu_mem_params.format_params.float_params.bias;
		mpc_fl_config.scale	= mcm_luts->lut3d_data.gpu_mem_params.format_params.float_params.scale;

		//1. power down the block
		mpc->funcs->rmcm.power_on_shaper_3dlut(mpc, mpcc_id, false);

		//2. program RMCM - 3dlut reg programming
		mpc->funcs->rmcm.fl_3dlut_configure(mpc, &mpc_fl_config, mpcc_id);

		hubp->funcs->hubp_program_3dlut_fl_config(hubp, &fl_config);

		//3. power on the block
		mpc->funcs->rmcm.power_on_shaper_3dlut(mpc, mpcc_id, true);

		break;
	default:
		return false;
	}

	return true;
}

void dcn42_populate_mcm_luts(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_cm2_func_luts mcm_luts,
		bool lut_bank_a)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	int mpcc_id = hubp->inst;
	struct mpc *mpc = dc->res_pool->mpc;
	union mcm_lut_params m_lut_params;
	enum dc_cm2_transfer_func_source lut3d_src = mcm_luts.lut3d_data.lut3d_src;
	enum hubp_3dlut_fl_format format = 0;
	enum hubp_3dlut_fl_mode mode;
	enum hubp_3dlut_fl_width width = 0;
	enum hubp_3dlut_fl_addressing_mode addr_mode;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_y_g = 0;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cb_b = 0;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cr_r = 0;
	enum MCM_LUT_XABLE shaper_xable = MCM_LUT_DISABLE;
	enum MCM_LUT_XABLE lut3d_xable = MCM_LUT_DISABLE;
	enum MCM_LUT_XABLE lut1d_xable = MCM_LUT_DISABLE;
	bool rval;

	dcn42_get_mcm_lut_xable_from_pipe_ctx(dc, pipe_ctx, &shaper_xable, &lut3d_xable, &lut1d_xable);

	//MCM - setting its location (Before/After) blender
	//set to post blend (true)
	dcn42_set_mcm_location_post_blend(
		dc,
		pipe_ctx,
		mcm_luts.lut3d_data.mpc_mcm_post_blend);

	//RMCM - 3dLUT+Shaper
	if (mcm_luts.lut3d_data.rmcm_3dlut_enable &&
		is_rmcm_3dlut_fl_supported(dc, mcm_luts.lut3d_data.gpu_mem_params.size)) {
		dcn42_program_rmcm_luts(
			hubp,
			pipe_ctx,
			lut3d_src,
			&mcm_luts,
			mpc,
			lut_bank_a,
			mpcc_id);
	}

	/* 1D LUT */
	if (mcm_luts.lut1d_func) {
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (mcm_luts.lut1d_func->type == TF_TYPE_HWPWL)
			m_lut_params.pwl = &mcm_luts.lut1d_func->pwl;
		else if (mcm_luts.lut1d_func->type == TF_TYPE_DISTRIBUTED_POINTS) {
			rval = cm3_helper_translate_curve_to_hw_format(mpc->ctx,
					mcm_luts.lut1d_func,
					&dpp_base->regamma_params, false);
			m_lut_params.pwl = rval ? &dpp_base->regamma_params : NULL;
		}
		if (m_lut_params.pwl) {
			if (mpc->funcs->populate_lut)
				mpc->funcs->populate_lut(mpc, MCM_LUT_1DLUT, m_lut_params, lut_bank_a, mpcc_id);
		}
		if (mpc->funcs->program_lut_mode)
			mpc->funcs->program_lut_mode(mpc, MCM_LUT_1DLUT, lut1d_xable && m_lut_params.pwl, lut_bank_a, mpcc_id);
	}

	/* Shaper */
	if (mcm_luts.shaper && mcm_luts.lut3d_data.mpc_3dlut_enable) {
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (mcm_luts.shaper->type == TF_TYPE_HWPWL)
			m_lut_params.pwl = &mcm_luts.shaper->pwl;
		else if (mcm_luts.shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
			ASSERT(false);
			rval = cm3_helper_translate_curve_to_hw_format(mpc->ctx,
					mcm_luts.shaper,
					&dpp_base->regamma_params, true);
			m_lut_params.pwl = rval ? &dpp_base->regamma_params : NULL;
		}
		if (m_lut_params.pwl) {
			if (mpc->funcs->mcm.populate_lut)
				mpc->funcs->mcm.populate_lut(mpc, m_lut_params, lut_bank_a, mpcc_id);
			if (mpc->funcs->program_lut_mode)
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_SHAPER, MCM_LUT_ENABLE, lut_bank_a, mpcc_id);
		}
	}

	/* 3DLUT */
	switch (lut3d_src) {
	case DC_CM2_TRANSFER_FUNC_SOURCE_SYSMEM:
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (hubp->funcs->hubp_enable_3dlut_fl)
			hubp->funcs->hubp_enable_3dlut_fl(hubp, false);

		if (mcm_luts.lut3d_data.lut3d_func && mcm_luts.lut3d_data.lut3d_func->state.bits.initialized) {
			m_lut_params.lut3d = &mcm_luts.lut3d_data.lut3d_func->lut_3d;
			if (mpc->funcs->populate_lut)
				mpc->funcs->populate_lut(mpc, MCM_LUT_3DLUT, m_lut_params, lut_bank_a, mpcc_id);
			if (mpc->funcs->program_lut_mode)
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, lut3d_xable, lut_bank_a,
						mpcc_id);
		}
		break;
	case DC_CM2_TRANSFER_FUNC_SOURCE_VIDMEM:
		switch (mcm_luts.lut3d_data.gpu_mem_params.size) {
		case DC_CM2_GPU_MEM_SIZE_333333:
			width = hubp_3dlut_fl_width_33;
			break;
		case DC_CM2_GPU_MEM_SIZE_171717:
			width = hubp_3dlut_fl_width_17;
			break;
		case DC_CM2_GPU_MEM_SIZE_TRANSFORMED:
			width = hubp_3dlut_fl_width_transformed;
			break;
		default:
			//TODO: Handle default case
			break;
		}

		//check for support
		if (mpc->funcs->mcm.is_config_supported &&
			!mpc->funcs->mcm.is_config_supported(width))
			break;

		if (mpc->funcs->program_lut_read_write_control)
			mpc->funcs->program_lut_read_write_control(mpc, MCM_LUT_3DLUT, lut_bank_a, mpcc_id);
		if (mpc->funcs->program_lut_mode)
			mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, lut3d_xable, lut_bank_a, mpcc_id);

		if (hubp->funcs->hubp_program_3dlut_fl_addr)
			hubp->funcs->hubp_program_3dlut_fl_addr(hubp, mcm_luts.lut3d_data.gpu_mem_params.addr);

		if (mpc->funcs->mcm.program_bit_depth)
			mpc->funcs->mcm.program_bit_depth(mpc, mcm_luts.lut3d_data.gpu_mem_params.bit_depth, mpcc_id);

		dc_get_lut_mode(mcm_luts.lut3d_data.gpu_mem_params.layout, &mode, &addr_mode);
		if (hubp->funcs->hubp_program_3dlut_fl_mode)
			hubp->funcs->hubp_program_3dlut_fl_mode(hubp, mode);

		if (hubp->funcs->hubp_program_3dlut_fl_addressing_mode)
			hubp->funcs->hubp_program_3dlut_fl_addressing_mode(hubp, addr_mode);

		switch (mcm_luts.lut3d_data.gpu_mem_params.format_params.format) {
		case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12MSB:
			format = hubp_3dlut_fl_format_unorm_12msb_bitslice;
			break;
		case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12LSB:
			format = hubp_3dlut_fl_format_unorm_12lsb_bitslice;
			break;
		case DC_CM2_GPU_MEM_FORMAT_16161616_FLOAT_FP1_5_10:
			format = hubp_3dlut_fl_format_float_fp1_5_10;
			break;
		}
		if (hubp->funcs->hubp_program_3dlut_fl_format)
			hubp->funcs->hubp_program_3dlut_fl_format(hubp, format);
		if (hubp->funcs->hubp_update_3dlut_fl_bias_scale &&
				mpc->funcs->mcm.program_bias_scale) {
			mpc->funcs->mcm.program_bias_scale(mpc,
				mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.bias,
				mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.scale,
				mpcc_id);
			hubp->funcs->hubp_update_3dlut_fl_bias_scale(hubp,
						mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.bias,
						mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.scale);
		}

		//navi 4x has a bug and r and blue are swapped and need to be worked around here in
		//TODO: need to make a method for get_xbar per asic OR do the workaround in program_crossbar for 4x
		dc_get_lut_xbar(
			mcm_luts.lut3d_data.gpu_mem_params.component_order,
			&crossbar_bit_slice_cr_r,
			&crossbar_bit_slice_y_g,
			&crossbar_bit_slice_cb_b);

		if (hubp->funcs->hubp_program_3dlut_fl_crossbar)
			hubp->funcs->hubp_program_3dlut_fl_crossbar(hubp,
					crossbar_bit_slice_cr_r,
					crossbar_bit_slice_y_g,
					crossbar_bit_slice_cb_b);

		if (mpc->funcs->mcm.program_lut_read_write_control)
			mpc->funcs->mcm.program_lut_read_write_control(mpc, MCM_LUT_3DLUT, lut_bank_a, true, mpcc_id);

		if (mpc->funcs->mcm.program_3dlut_size)
			mpc->funcs->mcm.program_3dlut_size(mpc, width, mpcc_id);

		if (mpc->funcs->update_3dlut_fast_load_select)
			mpc->funcs->update_3dlut_fast_load_select(mpc, mpcc_id, hubp->inst);

		if (hubp->funcs->hubp_enable_3dlut_fl)
			hubp->funcs->hubp_enable_3dlut_fl(hubp, true);
		else {
			if (mpc->funcs->program_lut_mode) {
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_SHAPER, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_1DLUT, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
			}
		}
		break;
	}
}

bool dcn42_set_mcm_luts(struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct dc *dc = pipe_ctx->stream_res.opp->ctx->dc;
	struct mpc *mpc = dc->res_pool->mpc;
	bool result;
	const struct pwl_params *lut_params = NULL;
	bool rval;

	if (plane_state->mcm_luts.lut3d_data.lut3d_src == DC_CM2_TRANSFER_FUNC_SOURCE_VIDMEM) {
		dcn42_populate_mcm_luts(dc, pipe_ctx, plane_state->mcm_luts, plane_state->lut_bank_a);
		return true;
	}

	mpc->funcs->set_movable_cm_location(mpc, MPCC_MOVABLE_CM_LOCATION_BEFORE, mpcc_id);
	pipe_ctx->plane_state->mcm_location = MPCC_MOVABLE_CM_LOCATION_BEFORE;
	// 1D LUT
	if (plane_state->blend_tf.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->blend_tf.pwl;
	else if (plane_state->blend_tf.type == TF_TYPE_DISTRIBUTED_POINTS) {
		rval = cm3_helper_translate_curve_to_hw_format(plane_state->ctx,
				&plane_state->blend_tf,
				&dpp_base->regamma_params, false);
		lut_params = rval ? &dpp_base->regamma_params : NULL;
	}
	result = mpc->funcs->program_1dlut(mpc, lut_params, mpcc_id);
	lut_params = NULL;

	// Shaper
	if (plane_state->in_shaper_func.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->in_shaper_func.pwl;
	else if (plane_state->in_shaper_func.type == TF_TYPE_DISTRIBUTED_POINTS) {
		// TODO: dpp_base replace
		rval = cm3_helper_translate_curve_to_hw_format(plane_state->ctx,
				&plane_state->in_shaper_func,
				&dpp_base->shaper_params, true);
		lut_params = rval ? &dpp_base->shaper_params : NULL;
	}
	result &= mpc->funcs->program_shaper(mpc, lut_params, mpcc_id);

	// 3D
	if (mpc->funcs->program_3dlut) {
		if (plane_state->lut3d_func.state.bits.initialized == 1)
			result &= mpc->funcs->program_3dlut(mpc, &plane_state->lut3d_func.lut_3d, mpcc_id);
		else
			result &= mpc->funcs->program_3dlut(mpc, NULL, mpcc_id);
	}

	return result;
}
void dcn42_hardware_release(struct dc *dc)
{
	dcn35_hardware_release(dc);

}
static int count_active_streams(const struct dc *dc)
{
	int i, count = 0;

	for (i = 0; i < dc->current_state->stream_count; ++i) {
		struct dc_stream_state *stream = dc->current_state->streams[i];

		if (stream && (!stream->dpms_off || dc->config.disable_ips_in_dpms_off))
			count += 1;
	}

	return count;
}

void dcn42_calc_blocks_to_gate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	bool hpo_frl_stream_enc_acquired = false;
	bool hpo_dp_stream_enc_acquired = false;
	int i = 0, j = 0;

	memset(update_state, 0, sizeof(struct pg_block_update));

	update_state->pg_res_update[PG_DIO] = true;

	for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++) {
		if (context->res_ctx.is_hpo_dp_stream_enc_acquired[i] &&
				dc->res_pool->hpo_dp_stream_enc[i]) {
			hpo_dp_stream_enc_acquired = true;
			break;
		}
	}

	if (!hpo_frl_stream_enc_acquired && !hpo_dp_stream_enc_acquired)
		update_state->pg_res_update[PG_HPO] = true;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++)
			update_state->pg_pipe_res_update[j][i] = true;

		if (!pipe_ctx)
			continue;

		if (pipe_ctx->plane_res.hubp)
			update_state->pg_pipe_res_update[PG_HUBP][pipe_ctx->plane_res.hubp->inst] = false;

		if (pipe_ctx->plane_res.dpp && pipe_ctx->plane_res.hubp)
			update_state->pg_pipe_res_update[PG_DPP][pipe_ctx->plane_res.hubp->inst] = false;

		if (pipe_ctx->plane_res.dpp || pipe_ctx->stream_res.opp)
			update_state->pg_pipe_res_update[PG_MPCC][pipe_ctx->plane_res.mpcc_inst] = false;

		if (pipe_ctx->stream_res.dsc) {
			update_state->pg_pipe_res_update[PG_DSC][pipe_ctx->stream_res.dsc->inst] = false;
			if (dc->caps.sequential_ono) {
				update_state->pg_pipe_res_update[PG_HUBP][pipe_ctx->stream_res.dsc->inst] = false;
				update_state->pg_pipe_res_update[PG_DPP][pipe_ctx->stream_res.dsc->inst] = false;
			}
		}
		if (pipe_ctx->stream_res.opp)
			update_state->pg_pipe_res_update[PG_OPP][pipe_ctx->stream_res.opp->inst] = false;

		if (pipe_ctx->stream_res.hpo_dp_stream_enc)
			update_state->pg_pipe_res_update[PG_DPSTREAM][pipe_ctx->stream_res.hpo_dp_stream_enc->inst] = false;

		if (pipe_ctx->link_res.dio_link_enc) {
			update_state->pg_res_update[PG_DIO] = false;
		}
		if (pipe_ctx->link_res.hpo_dp_link_enc) {
			update_state->pg_res_update[PG_HPO] = false;
		}
	}


	for (i = 0; i < dc->link_count; i++) {
		update_state->pg_pipe_res_update[PG_PHYSYMCLK][dc->links[i]->link_enc_hw_inst] = true;
		if (dc->links[i]->type != dc_connection_none)
			update_state->pg_pipe_res_update[PG_PHYSYMCLK][dc->links[i]->link_enc_hw_inst] = false;
	}

	/*domain24 controls all the otg, mpc, opp, as long as one otg is still up, avoid enabling OTG PG*/
	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		if (tg && tg->funcs->is_tg_enabled(tg)) {
			update_state->pg_pipe_res_update[PG_OPTC][i] = false;
		} else {
			 // Update pg_pipe_res_enable state
			if (dc->res_pool->pg_cntl->funcs->optc_pg_control)
				dc->res_pool->pg_cntl->funcs->optc_pg_control(dc->res_pool->pg_cntl, i, false);
		}
	}

}

void dcn42_prepare_bandwidth(
	struct dc *dc,
	struct dc_state *context)
{

	struct pg_block_update pg_update_state;

	if (dc->hwss.calc_blocks_to_ungate) {
		dc->hwss.calc_blocks_to_ungate(dc, context, &pg_update_state);

		if (dc->hwss.root_clock_control)
			dc->hwss.root_clock_control(dc, &pg_update_state, true);
		/*power up required HW block*/
		if (dc->hwss.hw_block_power_up)
			dc->hwss.hw_block_power_up(dc, &pg_update_state);
	}

	dcn20_prepare_bandwidth(dc, context);
}

void dcn42_optimize_bandwidth(struct dc *dc, struct dc_state *context)
{
		struct pg_block_update pg_update_state;

	print_pg_status(dc, __func__, ": before rcg and power up");

	dcn401_optimize_bandwidth(dc, context);

	if (dc->hwss.calc_blocks_to_gate) {
		dc->hwss.calc_blocks_to_gate(dc, context, &pg_update_state);
		/*try to power down unused block*/
		if (dc->hwss.hw_block_power_down)
			dc->hwss.hw_block_power_down(dc, &pg_update_state);

		if (dc->hwss.root_clock_control)
			dc->hwss.root_clock_control(dc, &pg_update_state, false);
	}

	print_pg_status(dc, __func__, ": after rcg and power up");

}

void dcn42_calc_blocks_to_ungate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	bool hpo_dp_stream_enc_acquired = false;
	int i = 0, j = 0;

	memset(update_state, 0, sizeof(struct pg_block_update));

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *cur_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];

		if (cur_pipe == NULL || new_pipe == NULL)
			continue;

		if ((!cur_pipe->plane_state && new_pipe->plane_state) ||
			(!cur_pipe->stream && new_pipe->stream) ||
			(cur_pipe->stream != new_pipe->stream && new_pipe->stream)) {
			// New pipe addition
			for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++) {
				if (j == PG_HUBP && new_pipe->plane_res.hubp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.hubp->inst] = true;

				if (j == PG_DPP && new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.dpp->inst] = true;

				if (j == PG_MPCC && new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.mpcc_inst] = true;

				if (j == PG_DSC && new_pipe->stream_res.dsc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.dsc->inst] = true;

				if (j == PG_OPP && new_pipe->stream_res.opp)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.opp->inst] = true;

				if (j == PG_OPTC && new_pipe->stream_res.tg)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.tg->inst] = true;

				if (j == PG_DPSTREAM && new_pipe->stream_res.hpo_dp_stream_enc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.hpo_dp_stream_enc->inst] = true;
			}
		} else if (cur_pipe->plane_state == new_pipe->plane_state ||
				cur_pipe == new_pipe) {
			//unchanged pipes
			for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++) {
				if (j == PG_HUBP &&
					cur_pipe->plane_res.hubp != new_pipe->plane_res.hubp &&
					new_pipe->plane_res.hubp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.hubp->inst] = true;

				if (j == PG_DPP &&
					cur_pipe->plane_res.dpp != new_pipe->plane_res.dpp &&
					new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.dpp->inst] = true;

				if (j == PG_OPP &&
					cur_pipe->stream_res.opp != new_pipe->stream_res.opp &&
					new_pipe->stream_res.opp)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.opp->inst] = true;

				if (j == PG_DSC &&
					cur_pipe->stream_res.dsc != new_pipe->stream_res.dsc &&
					new_pipe->stream_res.dsc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.dsc->inst] = true;

				if (j == PG_OPTC &&
					cur_pipe->stream_res.tg != new_pipe->stream_res.tg &&
					new_pipe->stream_res.tg)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.tg->inst] = true;

				if (j == PG_DPSTREAM &&
					cur_pipe->stream_res.hpo_dp_stream_enc != new_pipe->stream_res.hpo_dp_stream_enc &&
					new_pipe->stream_res.hpo_dp_stream_enc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.hpo_dp_stream_enc->inst] = true;
			}
		}
	}

	for (i = 0; i < dc->link_count; i++)
		if (dc->links[i]->type != dc_connection_none)
			update_state->pg_pipe_res_update[PG_PHYSYMCLK][dc->links[i]->link_enc_hw_inst] = true;

	for (i = 0; i < dc->res_pool->stream_enc_count; i++) {
		if (dc->current_state->res_ctx.is_stream_enc_acquired[i]) {
			update_state->pg_res_update[PG_DIO] = true;
			break;
		}
	}
	for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++) {
		if (context->res_ctx.is_hpo_dp_stream_enc_acquired[i] &&
				dc->res_pool->hpo_dp_stream_enc[i]) {
			hpo_dp_stream_enc_acquired = true;
			break;
		}
	}

	if (hpo_dp_stream_enc_acquired)
		update_state->pg_res_update[PG_HPO] = true;

	if (count_active_streams(dc) > 0) {
		update_state->pg_res_update[PG_DCCG] = true;
		update_state->pg_res_update[PG_DCIO] = true;
		update_state->pg_res_update[PG_DCHUBBUB] = true;
		update_state->pg_res_update[PG_DCHVM] = true;
		update_state->pg_res_update[PG_DCOH] = true;
	}
}
/**
 * dcn42_hw_block_power_down() - power down sequence
 *
 * The following sequence describes the ON-OFF (ONO) for power down:
 *
 *	ONO Region 4, DCPG 25: hpo
 *	ONO Region 11, DCPG 3: dchubp3, dpp3
 *	ONO Region 9, DCPG 2: dchubp2, dpp2
 *	ONO Region 7, DCPG 1: dchubp1, dpp1
 *	ONO Region 5, DCPG 0: dchubp0, dpp0
 *	ONO Region 2, DCPG 23: dchubbub, dchububmem, dchvm
 *	ONO Region 12, DCPG 19: dsc3
 *	ONO Region 10, DCPG 18: dsc2
 *	ONO Region 8, DCPG 17: dsc1
 *	ONO Region 6, DCPG 16: dsc0
 *	ONO Region 3, DCPG 24: mpc, opp, optc, dwb
 *	ONO Region 1, DCPG 26: dio
 *	ONO Region 0, DCPG 22: dccg dcio dcoh - SKIPPED
 *
 *  No seuential ONO power up/down order for DCN42
 *  Driver PG should only be limited to DCHUBP/DPP, DSC, HPO and DIO, so further optimization can be done
 *
 * @dc: Current DC state
 * @update_state: update PG sequence states for HW block
 */
 void dcn42_hw_block_power_down(struct dc *dc,
	struct pg_block_update *update_state)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;
	bool block_disabled = true;

	if (!pg_cntl)
		return;
	if (dc->debug.ignore_pg)
		return;

	if (update_state->pg_res_update[PG_HPO]) {
		if (pg_cntl->funcs->hpo_pg_control)
			pg_cntl->funcs->hpo_pg_control(pg_cntl, false);
	}

	for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (pg_cntl->funcs->hubp_dpp_pg_control)
				pg_cntl->funcs->hubp_dpp_pg_control(pg_cntl, i, false);
		}
	}

	if (update_state->pg_res_update[PG_DCHUBBUB]) {
		if (pg_cntl->funcs->mem_pg_control)
			pg_cntl->funcs->mem_pg_control(pg_cntl, false);
	}

	for (i = dc->res_pool->res_cap->num_dsc-1; i >= 0; i--) {
		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (pg_cntl->funcs->dsc_pg_control)
				pg_cntl->funcs->dsc_pg_control(pg_cntl, i, false);
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!update_state->pg_pipe_res_update[PG_MPCC][i] ||
			!update_state->pg_pipe_res_update[PG_OPP][i] ||
			!update_state->pg_pipe_res_update[PG_OPTC][i]) {
				block_disabled = false;
				break;
		}
	}

	if (block_disabled) {
		if (pg_cntl->funcs->plane_otg_pg_control)
			pg_cntl->funcs->plane_otg_pg_control(pg_cntl, false);
	}

	if (update_state->pg_res_update[PG_DIO]) {
		if (pg_cntl->funcs->dio_pg_control)
			pg_cntl->funcs->dio_pg_control(pg_cntl, false);
	}

	if (update_state->pg_res_update[PG_DCCG] &&
		update_state->pg_res_update[PG_DCIO] &&
		update_state->pg_res_update[PG_DCOH]) {
		// Driver PG should not power down DCCG, DCIO, DCOH. This is handled by IPS.
		if (pg_cntl->funcs->io_clk_pg_control)
			pg_cntl->funcs->io_clk_pg_control(pg_cntl, false);
		}
}

/**
 * dcn42_hw_block_power_up() - power up sequence
 *
 * The following sequence describes the ON-OFF (ONO) for power up:
 *
 *	ONO Region 0, DCPG 22: dccg dcio dcoh
 *  ONO Region 1, DCPG 26: dio
 *	ONO Region 3, DCPG 24: mpc, opp, optc, dwb
 *  ONO Region 6, DCPG 16: dsc0
 *  ONO Region 8, DCPG 17: dsc1
 *  ONO Region 10, DCPG 18: dsc2
 *	ONO Region 12, DCPG 19: dsc3
 *	ONO Region 2, DCPG 23: dchubbub, dchububmem, dchvm
 *	ONO Region 5, DCPG 0: dchubp0, dpp0
 *	ONO Region 7, DCPG 1: dchubp1, dpp1
 *	ONO Region 9, DCPG 2: dchubp2, dpp2
 *	ONO Region 11, DCPG 3: dchubp3, dpp3
 *	ONO Region 4, DCPG 25: hpo
 *
 *  No sequential power up/down ordering for DCN42
 *
 * @dc: Current DC state
 * @update_state: update PG sequence states for HW block
 */
void dcn42_hw_block_power_up(struct dc *dc,
	struct pg_block_update *update_state)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;
	bool block_enabled = false;

	if (!pg_cntl)
		return;
	if (dc->debug.ignore_pg)
		return;

	if (update_state->pg_res_update[PG_DCCG] ||
		update_state->pg_res_update[PG_DCIO] ||
		update_state->pg_res_update[PG_DCOH]) {
		if (pg_cntl->funcs->io_clk_pg_control)
			pg_cntl->funcs->io_clk_pg_control(pg_cntl, true);
	}

	if (update_state->pg_res_update[PG_DIO]) {
		if (pg_cntl->funcs->dio_pg_control)
			pg_cntl->funcs->dio_pg_control(pg_cntl, true);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (update_state->pg_pipe_res_update[PG_MPCC][i] ||
			update_state->pg_pipe_res_update[PG_OPP][i] ||
			update_state->pg_pipe_res_update[PG_OPTC][i]) {
				block_enabled = true;
				break;
		}
	}
	if (block_enabled) {
		if (pg_cntl->funcs->plane_otg_pg_control)
			pg_cntl->funcs->plane_otg_pg_control(pg_cntl, true);
	}

	for (i = 0; i < dc->res_pool->res_cap->num_dsc; i++) {
		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (pg_cntl->funcs->dsc_pg_control)
				pg_cntl->funcs->dsc_pg_control(pg_cntl, i, true);
		}
	}

	if (update_state->pg_res_update[PG_DCHUBBUB]) {
		if (pg_cntl->funcs->mem_pg_control)
			pg_cntl->funcs->mem_pg_control(pg_cntl, true);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (pg_cntl->funcs->hubp_dpp_pg_control)
				pg_cntl->funcs->hubp_dpp_pg_control(pg_cntl, i, true);
		}
	}
	if (update_state->pg_res_update[PG_HPO]) {
		if (pg_cntl->funcs->hpo_pg_control)
			pg_cntl->funcs->hpo_pg_control(pg_cntl, true);
	}
}
void dcn42_root_clock_control(struct dc *dc,
	struct pg_block_update *update_state, bool power_on)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;

	if (!pg_cntl)
		return;
	/*enable root clock first when power up*/
	if (power_on) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
				update_state->pg_pipe_res_update[PG_DPP][i]) {
				if (dc->hwseq->funcs.dpp_root_clock_control)
					dc->hwseq->funcs.dpp_root_clock_control(dc->hwseq, i, power_on);
			}
			if (update_state->pg_pipe_res_update[PG_DPSTREAM][i])
				if (dc->hwseq->funcs.dpstream_root_clock_control)
					dc->hwseq->funcs.dpstream_root_clock_control(dc->hwseq, i, power_on);
		}

		for (i = 0; i < dc->res_pool->dig_link_enc_count; i++)
			if (update_state->pg_pipe_res_update[PG_PHYSYMCLK][i])
				if (dc->hwseq->funcs.physymclk_root_clock_control)
					dc->hwseq->funcs.physymclk_root_clock_control(dc->hwseq, i, power_on);

	}
	for (i = 0; i < dc->res_pool->res_cap->num_dsc; i++) {
		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (power_on) {
				if (dc->res_pool->dccg->funcs->enable_dsc)
					dc->res_pool->dccg->funcs->enable_dsc(dc->res_pool->dccg, i);
			} else {
				if (dc->res_pool->dccg->funcs->disable_dsc)
					dc->res_pool->dccg->funcs->disable_dsc(dc->res_pool->dccg, i);
			}
		}
	}
	/*disable root clock first when power down*/
	if (!power_on) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
				update_state->pg_pipe_res_update[PG_DPP][i]) {
				if (dc->hwseq->funcs.dpp_root_clock_control)
					dc->hwseq->funcs.dpp_root_clock_control(dc->hwseq, i, power_on);
			}
			if (update_state->pg_pipe_res_update[PG_DPSTREAM][i])
				if (dc->hwseq->funcs.dpstream_root_clock_control)
					dc->hwseq->funcs.dpstream_root_clock_control(dc->hwseq, i, power_on);
		}

		for (i = 0; i < dc->res_pool->dig_link_enc_count; i++)
			if (update_state->pg_pipe_res_update[PG_PHYSYMCLK][i])
				if (dc->hwseq->funcs.physymclk_root_clock_control)
					dc->hwseq->funcs.physymclk_root_clock_control(dc->hwseq, i, power_on);

	}
}
void dcn42_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc)
{
	struct crtc_stereo_flags flags = { 0 };
	struct dc_stream_state *stream = pipe_ctx->stream;

	dcn10_config_stereo_parameters(stream, &flags);

	pipe_ctx->stream_res.opp->funcs->opp_program_stereo(
		pipe_ctx->stream_res.opp,
		flags.PROGRAM_STEREO == 1,
		&stream->timing);

	pipe_ctx->stream_res.tg->funcs->program_stereo(
		pipe_ctx->stream_res.tg,
		&stream->timing,
		&flags);

	return;
}
void dcn42_dmub_hw_control_lock(struct dc *dc, struct dc_state *context, bool lock)
{

	union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

	if (!dc->ctx || !dc->ctx->dmub_srv)
		return;

	/* Use helper to check PSR/Replay for all streams in context */

	if (!dc->debug.fams2_config.bits.enable && !dc_dmub_srv_is_cursor_offload_enabled(dc)
		&& !dmub_hw_lock_mgr_does_context_require_lock(dc, context))
		return;

	hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
	hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
	hw_lock_cmd.bits.lock = lock;
	hw_lock_cmd.bits.should_release = !lock;
	dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
}

void dcn42_dmub_hw_control_lock_fast(union block_sequence_params *params)
{
	struct dc *dc = params->dmub_hw_control_lock_fast_params.dc;
	bool lock = params->dmub_hw_control_lock_fast_params.lock;

	/* Use helper to check PSR/Replay for the given stream in fast path */
	if (params->dmub_hw_control_lock_fast_params.is_required) {
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };
		hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
		hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
		hw_lock_cmd.bits.lock = lock;
		hw_lock_cmd.bits.should_release = !lock;
		dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
	}
}
