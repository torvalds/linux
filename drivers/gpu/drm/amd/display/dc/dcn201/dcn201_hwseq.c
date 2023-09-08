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

#include "dm_services.h"
#include "basics/dc_common.h"
#include "core_types.h"
#include "resource.h"
#include "dcn201_hwseq.h"
#include "dcn201_optc.h"
#include "dce/dce_hwseq.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "dccg.h"
#include "clk_mgr.h"
#include "reg_helper.h"

#define CTX \
	hws->ctx

#define REG(reg)\
	hws->regs->reg

#define DC_LOGGER \
	dc->ctx->logger

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

static bool patch_address_for_sbs_tb_stereo(
		struct pipe_ctx *pipe_ctx, PHYSICAL_ADDRESS_LOC *addr)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	bool sec_split = pipe_ctx->top_pipe &&
		pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state;

	if (sec_split && plane_state->address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
		(pipe_ctx->stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_SIDE_BY_SIDE ||
		pipe_ctx->stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_TOP_AND_BOTTOM)) {
		*addr = plane_state->address.grph_stereo.left_addr;
		plane_state->address.grph_stereo.left_addr =
			plane_state->address.grph_stereo.right_addr;
		return true;
	} else {
		if (pipe_ctx->stream->view_format != VIEW_3D_FORMAT_NONE &&
			plane_state->address.type != PLN_ADDR_TYPE_GRPH_STEREO) {
			plane_state->address.type = PLN_ADDR_TYPE_GRPH_STEREO;
			plane_state->address.grph_stereo.right_addr =
			plane_state->address.grph_stereo.left_addr;
			plane_state->address.grph_stereo.right_meta_addr =
			plane_state->address.grph_stereo.left_meta_addr;
		}
	}
	return false;
}

static bool gpu_addr_to_uma(struct dce_hwseq *hwseq,
		PHYSICAL_ADDRESS_LOC *addr)
{
	bool is_in_uma;

	if (hwseq->fb_base.quad_part <= addr->quad_part &&
			addr->quad_part < hwseq->fb_top.quad_part) {
		addr->quad_part -= hwseq->fb_base.quad_part;
		addr->quad_part += hwseq->fb_offset.quad_part;
		is_in_uma = true;
	} else if (hwseq->fb_offset.quad_part <= addr->quad_part &&
			addr->quad_part <= hwseq->uma_top.quad_part) {
		is_in_uma = true;
	} else {
		is_in_uma = false;
	}
	return is_in_uma;
}

static void plane_address_in_gpu_space_to_uma(struct dce_hwseq *hwseq,
		struct dc_plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		gpu_addr_to_uma(hwseq, &addr->grph.addr);
		gpu_addr_to_uma(hwseq, &addr->grph.meta_addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		gpu_addr_to_uma(hwseq, &addr->grph_stereo.left_addr);
		gpu_addr_to_uma(hwseq, &addr->grph_stereo.left_meta_addr);
		gpu_addr_to_uma(hwseq, &addr->grph_stereo.right_addr);
		gpu_addr_to_uma(hwseq, &addr->grph_stereo.right_meta_addr);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		gpu_addr_to_uma(hwseq, &addr->video_progressive.luma_addr);
		gpu_addr_to_uma(hwseq, &addr->video_progressive.luma_meta_addr);
		gpu_addr_to_uma(hwseq, &addr->video_progressive.chroma_addr);
		gpu_addr_to_uma(hwseq, &addr->video_progressive.chroma_meta_addr);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void dcn201_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	bool addr_patched = false;
	PHYSICAL_ADDRESS_LOC addr;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_plane_address uma;

	if (plane_state == NULL)
		return;

	uma = plane_state->address;
	addr_patched = patch_address_for_sbs_tb_stereo(pipe_ctx, &addr);

	plane_address_in_gpu_space_to_uma(hws, &uma);

	pipe_ctx->plane_res.hubp->funcs->hubp_program_surface_flip_and_addr(
			pipe_ctx->plane_res.hubp,
			&uma,
			plane_state->flip_immediate);

	plane_state->status.requested_address = plane_state->address;

	if (plane_state->flip_immediate)
		plane_state->status.current_address = plane_state->address;

	if (addr_patched)
		pipe_ctx->plane_state->address.grph_stereo.left_addr = addr;
}

/* Blank pixel data during initialization */
void dcn201_init_blank(
		struct dc *dc,
		struct timing_generator *tg)
{
	struct dce_hwseq *hws = dc->hwseq;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	struct output_pixel_processor *opp = NULL;
	uint32_t num_opps, opp_id_src0, opp_id_src1;
	uint32_t otg_active_width, otg_active_height;

	/* program opp dpg blank color */
	color_space = COLOR_SPACE_SRGB;
	color_space_to_black_color(dc, color_space, &black_color);

	/* get the OTG active size */
	tg->funcs->get_otg_active_size(tg,
			&otg_active_width,
			&otg_active_height);

	/* get the OPTC source */
	tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);
	ASSERT(opp_id_src0 < dc->res_pool->res_cap->num_opp);
	opp = dc->res_pool->opps[opp_id_src0];

	opp->funcs->opp_set_disp_pattern_generator(
			opp,
			CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR,
			CONTROLLER_DP_COLOR_SPACE_UDEFINED,
			COLOR_DEPTH_UNDEFINED,
			&black_color,
			otg_active_width,
			otg_active_height,
			0);

	hws->funcs.wait_for_blank_complete(opp);
}

static void read_mmhub_vm_setup(struct dce_hwseq *hws)
{
	uint32_t fb_base = REG_READ(MC_VM_FB_LOCATION_BASE);
	uint32_t fb_top = REG_READ(MC_VM_FB_LOCATION_TOP);
	uint32_t fb_offset = REG_READ(MC_VM_FB_OFFSET);

	/* MC_VM_FB_LOCATION_TOP is in pages, actual top should add 1 */
	fb_top++;

	/* bit 23:0 in register map to bit 47:24 in address */
	hws->fb_base.low_part = fb_base;
	hws->fb_base.quad_part <<= 24;

	hws->fb_top.low_part  = fb_top;
	hws->fb_top.quad_part <<= 24;
	hws->fb_offset.low_part = fb_offset;
	hws->fb_offset.quad_part <<= 24;

	hws->uma_top.quad_part = hws->fb_top.quad_part
			- hws->fb_base.quad_part + hws->fb_offset.quad_part;
}

void dcn201_init_hw(struct dc *dc)
{
	int i, j;
	struct dce_hwseq *hws = dc->hwseq;
	struct resource_pool *res_pool = dc->res_pool;
	struct dc_state  *context = dc->current_state;

	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		REG_WRITE(RBBMIF_TIMEOUT_DIS, 0xFFFFFFFF);
		REG_WRITE(RBBMIF_TIMEOUT_DIS_2, 0xFFFFFFFF);

		hws->funcs.dccg_init(hws);

		REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, 2);
		REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
		REG_WRITE(REFCLK_CNTL, 0);
	} else {
		hws->funcs.bios_golden_init(dc);

		if (dc->ctx->dc_bios->fw_info_valid) {
			res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

			if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
				if (res_pool->dccg && res_pool->hubbub) {
					(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
							dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
							&res_pool->ref_clocks.dccg_ref_clock_inKhz);

					(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
							res_pool->ref_clocks.dccg_ref_clock_inKhz,
							&res_pool->ref_clocks.dchub_ref_clock_inKhz);
				} else {
					res_pool->ref_clocks.dccg_ref_clock_inKhz =
							res_pool->ref_clocks.xtalin_clock_inKhz;
					res_pool->ref_clocks.dchub_ref_clock_inKhz =
							res_pool->ref_clocks.xtalin_clock_inKhz;
				}
			}
		} else
			ASSERT_CRITICAL(false);
		for (i = 0; i < dc->link_count; i++) {
			/* Power up AND update implementation according to the
			 * required signal (which may be different from the
			 * default signal on connector).
			 */
			struct dc_link *link = dc->links[i];

			link->link_enc->funcs->hw_init(link->link_enc);
		}
		if (hws->fb_offset.quad_part == 0)
			read_mmhub_vm_setup(hws);
	}

	/* Blank pixel data with OPP DPG */
	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg)) {
			dcn201_init_blank(dc, tg);
		}
	}

	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->lock(tg);
	}

	for (i = 0; i < res_pool->pipe_count; i++) {
		struct dpp *dpp = res_pool->dpps[i];

		dpp->funcs->dpp_reset(dpp);
	}

	/* Reset all MPCC muxes */
	res_pool->mpc->funcs->mpc_init(res_pool->mpc);

	/* initialize OPP mpc_tree parameter */
	for (i = 0; i < res_pool->res_cap->num_opp; i++) {
		res_pool->opps[i]->mpc_tree_params.opp_id = res_pool->opps[i]->inst;
		res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		for (j = 0; j < MAX_PIPES; j++)
			res_pool->opps[i]->mpcc_disconnect_pending[j] = false;
	}

	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = res_pool->timing_generators[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = res_pool->hubps[i];
		struct dpp *dpp = res_pool->dpps[i];

		pipe_ctx->stream_res.tg = tg;
		pipe_ctx->pipe_idx = i;

		pipe_ctx->plane_res.hubp = hubp;
		pipe_ctx->plane_res.dpp = dpp;
		pipe_ctx->plane_res.mpcc_inst = dpp->inst;
		hubp->mpcc_id = dpp->inst;
		hubp->opp_id = OPP_ID_INVALID;
		hubp->power_gated = false;
		pipe_ctx->stream_res.opp = NULL;

		hubp->funcs->hubp_init(hubp);

		res_pool->opps[i]->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;
		pipe_ctx->stream_res.opp = res_pool->opps[i];
		/*To do: number of MPCC != number of opp*/
		hws->funcs.plane_atomic_disconnect(dc, pipe_ctx);
	}

	/* initialize DWB pointer to MCIF_WB */
	for (i = 0; i < res_pool->res_cap->num_dwb; i++)
		res_pool->dwbc[i]->mcif = res_pool->mcif_wb[i];

	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->unlock(tg);
	}

	for (i = 0; i < res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		dc->hwss.disable_plane(dc, pipe_ctx);

		pipe_ctx->stream_res.tg = NULL;
		pipe_ctx->plane_res.hubp = NULL;
	}

	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = res_pool->timing_generators[i];

		tg->funcs->tg_init(tg);
	}

	/* end of FPGA. Below if real ASIC */
	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		return;

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}
}

/* trigger HW to start disconnect plane from stream on the next vsync */
void dcn201_plane_atomic_disconnect(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	int dpp_id = pipe_ctx->plane_res.dpp->inst;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params;
	struct mpcc *mpcc_to_remove = NULL;
	struct output_pixel_processor *opp = pipe_ctx->stream_res.opp;
	bool mpcc_removed = false;

	mpc_tree_params = &(opp->mpc_tree_params);

	/* check if this plane is being used by an MPCC in the secondary blending chain */
	if (mpc->funcs->get_mpcc_for_dpp_from_secondary)
		mpcc_to_remove = mpc->funcs->get_mpcc_for_dpp_from_secondary(mpc_tree_params, dpp_id);

	/* remove MPCC from secondary if being used */
	if (mpcc_to_remove != NULL && mpc->funcs->remove_mpcc_from_secondary) {
		mpc->funcs->remove_mpcc_from_secondary(mpc, mpc_tree_params, mpcc_to_remove);
		mpcc_removed = true;
	}

	/* check if this MPCC is already being used for this plane (dpp) in the primary blending chain */
	mpcc_to_remove = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, dpp_id);
	if (mpcc_to_remove != NULL) {
		mpc->funcs->remove_mpcc(mpc, mpc_tree_params, mpcc_to_remove);
		mpcc_removed = true;
	}

	/*Already reset*/
	if (mpcc_removed == false)
		return;

	if (opp != NULL)
		opp->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;

	dc->optimized_required = true;

	if (hubp->funcs->hubp_disconnect)
		hubp->funcs->hubp_disconnect(hubp);

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

void dcn201_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct mpcc_blnd_cfg blnd_cfg;
	bool per_pixel_alpha = pipe_ctx->plane_state->per_pixel_alpha && pipe_ctx->bottom_pipe;
	int mpcc_id, dpp_id;
	struct mpcc *new_mpcc;
	struct mpcc *remove_mpcc = NULL;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params = &(pipe_ctx->stream_res.opp->mpc_tree_params);

	if (dc->debug.visual_confirm == VISUAL_CONFIRM_HDR) {
		get_hdr_visual_confirm_color(
				pipe_ctx, &blnd_cfg.black_color);
	} else if (dc->debug.visual_confirm == VISUAL_CONFIRM_SURFACE) {
		get_surface_visual_confirm_color(
				pipe_ctx, &blnd_cfg.black_color);
	} else {
		color_space_to_black_color(
				dc, pipe_ctx->stream->output_color_space,
				&blnd_cfg.black_color);
	}

	if (per_pixel_alpha)
		blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA;
	else
		blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;

	blnd_cfg.overlap_only = false;

	if (pipe_ctx->plane_state->global_alpha_value)
		blnd_cfg.global_alpha = pipe_ctx->plane_state->global_alpha_value;
	else
		blnd_cfg.global_alpha = 0xff;

	blnd_cfg.global_gain = 0xff;
	blnd_cfg.background_color_bpc = 4;
	blnd_cfg.bottom_gain_mode = 0;
	blnd_cfg.top_gain = 0x1f000;
	blnd_cfg.bottom_inside_gain = 0x1f000;
	blnd_cfg.bottom_outside_gain = 0x1f000;
	/*the input to MPCC is RGB*/
	blnd_cfg.black_color.color_b_cb = 0;
	blnd_cfg.black_color.color_g_y = 0;
	blnd_cfg.black_color.color_r_cr = 0;

	/* DCN1.0 has output CM before MPC which seems to screw with
	 * pre-multiplied alpha. This is a w/a hopefully unnecessary for DCN2.
	 */
	blnd_cfg.pre_multiplied_alpha = per_pixel_alpha;

	/*
	 * TODO: remove hack
	 * Note: currently there is a bug in init_hw such that
	 * on resume from hibernate, BIOS sets up MPCC0, and
	 * we do mpcc_remove but the mpcc cannot go to idle
	 * after remove. This cause us to pick mpcc1 here,
	 * which causes a pstate hang for yet unknown reason.
	 */
	dpp_id = hubp->inst;
	mpcc_id = dpp_id;

	/* If there is no full update, don't need to touch MPC tree*/
	if (!pipe_ctx->plane_state->update_flags.bits.full_update) {
		dc->hwss.update_visual_confirm_color(dc, pipe_ctx, &blnd_cfg.black_color, mpcc_id);
		mpc->funcs->update_blending(mpc, &blnd_cfg, mpcc_id);
		return;
	}

	/* check if this plane is being used by an MPCC in the secondary blending chain */
	if (mpc->funcs->get_mpcc_for_dpp_from_secondary)
		remove_mpcc = mpc->funcs->get_mpcc_for_dpp_from_secondary(mpc_tree_params, dpp_id);

	/* remove MPCC from secondary if being used */
	if (remove_mpcc != NULL && mpc->funcs->remove_mpcc_from_secondary)
		mpc->funcs->remove_mpcc_from_secondary(mpc, mpc_tree_params, remove_mpcc);

	/* check if this MPCC is already being used for this plane (dpp) in the primary blending chain */
	remove_mpcc = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, dpp_id);
	/* remove MPCC if being used */

	if (remove_mpcc != NULL)
		mpc->funcs->remove_mpcc(mpc, mpc_tree_params, remove_mpcc);
	else
		if (dc->debug.sanity_checks)
			mpc->funcs->assert_mpcc_idle_before_connect(
					dc->res_pool->mpc, mpcc_id);

	/* Call MPC to insert new plane */
	dc->hwss.update_visual_confirm_color(dc, pipe_ctx, &blnd_cfg.black_color, mpcc_id);
	new_mpcc = mpc->funcs->insert_plane(dc->res_pool->mpc,
			mpc_tree_params,
			&blnd_cfg,
			NULL,
			NULL,
			dpp_id,
			mpcc_id);

	ASSERT(new_mpcc != NULL);
	hubp->opp_id = pipe_ctx->stream_res.opp->inst;
	hubp->mpcc_id = mpcc_id;
}

void dcn201_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock)
{
	struct dce_hwseq *hws = dc->hwseq;
	/* use TG master update lock to lock everything on the TG
	 * therefore only top pipe need to lock
	 */
	if (pipe->top_pipe)
		return;

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);

	if (pipe->plane_state != NULL && pipe->plane_state->triplebuffer_flips) {
		if (lock)
			pipe->stream_res.tg->funcs->triplebuffer_lock(pipe->stream_res.tg);
		else
			pipe->stream_res.tg->funcs->triplebuffer_unlock(pipe->stream_res.tg);
	} else {
		if (lock)
			pipe->stream_res.tg->funcs->lock(pipe->stream_res.tg);
		else
			pipe->stream_res.tg->funcs->unlock(pipe->stream_res.tg);
	}

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

void dcn201_set_cursor_attribute(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_attributes *attributes = &pipe_ctx->stream->cursor_attributes;

	gpu_addr_to_uma(pipe_ctx->stream->ctx->dc->hwseq, &attributes->address);

	pipe_ctx->plane_res.hubp->funcs->set_cursor_attributes(
			pipe_ctx->plane_res.hubp, attributes);
	pipe_ctx->plane_res.dpp->funcs->set_cursor_attributes(
		pipe_ctx->plane_res.dpp, attributes);
}

void dcn201_set_dmdata_attributes(struct pipe_ctx *pipe_ctx)
{
	struct dc_dmdata_attributes attr = { 0 };
	struct hubp *hubp = pipe_ctx->plane_res.hubp;

	gpu_addr_to_uma(pipe_ctx->stream->ctx->dc->hwseq,
			&pipe_ctx->stream->dmdata_address);

	attr.dmdata_mode = DMDATA_HW_MODE;
	attr.dmdata_size =
		dc_is_hdmi_signal(pipe_ctx->stream->signal) ? 32 : 36;
	attr.address.quad_part =
			pipe_ctx->stream->dmdata_address.quad_part;
	attr.dmdata_dl_delta = 0;
	attr.dmdata_qos_mode = 0;
	attr.dmdata_qos_level = 0;
	attr.dmdata_repeat = 1; /* always repeat */
	attr.dmdata_updated = 1;
	attr.dmdata_sw_data = NULL;

	hubp->funcs->dmdata_set_attributes(hubp, &attr);
}

void dcn201_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = { { 0 } };
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;

	/* only 3 items below are used by unblank */
	params.timing = pipe_ctx->stream->timing;

	params.link_settings.link_rate = link_settings->link_rate;

	if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		/*check whether it is half the rate*/
		if (optc201_is_two_pixels_per_containter(&stream->timing))
			params.timing.pix_clk_100hz /= 2;

		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(link, pipe_ctx->stream_res.stream_enc, &params);
	}

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
		hws->funcs.edp_backlight_control(link, true);
	}
}
