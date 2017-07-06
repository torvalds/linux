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
#include "dc.h"
#include "core_dc.h"
#include "core_types.h"
#include "core_status.h"
#include "resource.h"
#include "dcn10_hw_sequencer.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dce/dce_hwseq.h"
#include "abm.h"
#include "mem_input.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "dc_bios_types.h"
#include "raven1/DCN/dcn_1_0_offset.h"
#include "raven1/DCN/dcn_1_0_sh_mask.h"
#include "vega10/soc15ip.h"
#include "custom_float.h"
#include "reg_helper.h"

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

static void enable_dppclk(
	struct dce_hwseq *hws,
	uint8_t plane_id,
	uint32_t requested_pix_clk,
	bool dppclk_div)
{
	dm_logger_write(hws->ctx->logger, LOG_SURFACE,
			"dppclk_rate_control for pipe %d programed to %d\n",
			plane_id,
			dppclk_div);

	if (dppclk_div) {
		/* 1/2 DISPCLK*/
		REG_UPDATE_2(DPP_CONTROL[plane_id],
			DPPCLK_RATE_CONTROL, 1,
			DPP_CLOCK_ENABLE, 1);
	} else {
		/* DISPCLK */
		REG_UPDATE_2(DPP_CONTROL[plane_id],
			DPPCLK_RATE_CONTROL, 0,
			DPP_CLOCK_ENABLE, 1);
	}
}

static void enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = 1; /* disable power gating */

	if (enable)
		force_on = 0;

	/* DCHUBP0/1/2/3 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN0_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN2_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN4_PG_CONFIG, DOMAIN4_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN6_PG_CONFIG, DOMAIN6_POWER_FORCEON, force_on);

	/* DPP0/1/2/3 */
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN1_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN3_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN5_PG_CONFIG, DOMAIN5_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN7_PG_CONFIG, DOMAIN7_POWER_FORCEON, force_on);
}

static void dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_dpp_power_gate)
		return;

	switch (dpp_inst) {
	case 0: /* DPP0 */
		REG_UPDATE(DOMAIN1_PG_CONFIG,
				DOMAIN1_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN1_PG_STATUS,
				DOMAIN1_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 1: /* DPP1 */
		REG_UPDATE(DOMAIN3_PG_CONFIG,
				DOMAIN3_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN3_PG_STATUS,
				DOMAIN3_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 2: /* DPP2 */
		REG_UPDATE(DOMAIN5_PG_CONFIG,
				DOMAIN5_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN5_PG_STATUS,
				DOMAIN5_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 3: /* DPP3 */
		REG_UPDATE(DOMAIN7_PG_CONFIG,
				DOMAIN7_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN7_PG_STATUS,
				DOMAIN7_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;

	switch (hubp_inst) {
	case 0: /* DCHUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG,
				DOMAIN0_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN0_PG_STATUS,
				DOMAIN0_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 1: /* DCHUBP1 */
		REG_UPDATE(DOMAIN2_PG_CONFIG,
				DOMAIN2_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN2_PG_STATUS,
				DOMAIN2_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 2: /* DCHUBP2 */
		REG_UPDATE(DOMAIN4_PG_CONFIG,
				DOMAIN4_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN4_PG_STATUS,
				DOMAIN4_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	case 3: /* DCHUBP3 */
		REG_UPDATE(DOMAIN6_PG_CONFIG,
				DOMAIN6_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN6_PG_STATUS,
				DOMAIN6_PGFSM_PWR_STATUS, pwr_status, 20000, 200000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void power_on_plane(
	struct dce_hwseq *hws,
	int plane_id)
{
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);
	dpp_pg_control(hws, plane_id, true);
	hubp_pg_control(hws, plane_id, true);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);
	dm_logger_write(hws->ctx->logger, LOG_DC,
			"Un-gated front end for pipe %d\n", plane_id);
}

static void bios_golden_init(struct core_dc *dc)
{
	struct dc_bios *bp = dc->ctx->dc_bios;
	int i;

	/* initialize dcn global */
	bp->funcs->enable_disp_power_gating(bp,
			CONTROLLER_ID_D0, ASIC_PIPE_INIT);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		/* initialize dcn per pipe */
		bp->funcs->enable_disp_power_gating(bp,
				CONTROLLER_ID_D0 + i, ASIC_PIPE_DISABLE);
	}
}

/*
 * This should be done within BIOS, we are doing it for maximus only
 */
static void dchubup_setup_timer(struct dce_hwseq *hws)
{
	REG_WRITE(REFCLK_CNTL, 0);

	REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
}

static void init_hw(struct core_dc *dc)
{
	int i;
	struct transform *xfm;
	struct abm *abm;
	struct dce_hwseq *hws = dc->hwseq;

#if 1
	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		dchubup_setup_timer(dc->hwseq);

		/* TODO: dchubp_map_fb_to_mc will be moved to dchub interface
		 * between dc and kmd
		 */
		/*dchubp_map_fb_to_mc(dc->hwseq);*/
		REG_WRITE(DIO_MEM_PWR_CTRL, 0);

		if (!dc->public.debug.disable_clock_gate) {
			/* enable all DCN clock gating */
			REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

			REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

			REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
		}

		enable_power_gating_plane(dc->hwseq, true);
		return;
	}
	/* end of FPGA. Below if real ASIC */
#endif

	bios_golden_init(dc);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		xfm = dc->res_pool->transforms[i];
		xfm->funcs->transform_reset(xfm);
	}

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct core_link *link = dc->links[i];

		link->link_enc->funcs->hw_init(link->link_enc);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg =
				dc->res_pool->timing_generators[i];
		struct mpcc *mpcc =
				dc->res_pool->mpcc[i];
		struct mpcc_cfg mpcc_cfg;

		tg->funcs->lock(tg);
		mpcc_cfg.opp_id = 0xf;
		mpcc_cfg.top_dpp_id = 0xf;
		mpcc_cfg.bot_mpcc_id = 0xf;
		mpcc_cfg.top_of_tree = true;
		mpcc->funcs->set(mpcc, &mpcc_cfg);
		tg->funcs->unlock(tg);

		tg->funcs->disable_vga(tg);
		/* Blank controller using driver code instead of
		 * command table.
		 */
		tg->funcs->set_blank(tg, true);
		hwss_wait_for_blank_complete(tg);
	}

	for (i = 0; i < dc->res_pool->audio_count; i++) {
		struct audio *audio = dc->res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	abm = dc->res_pool->abm;
	if (abm != NULL) {
		abm->funcs->init_backlight(abm);
		abm->funcs->abm_init(abm);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->public.debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	enable_power_gating_plane(dc->hwseq, true);
}

static enum dc_status dcn10_prog_pixclk_crtc_otg(
		struct pipe_ctx *pipe_ctx,
		struct validate_context *context,
		struct core_dc *dc)
{
	struct core_stream *stream = pipe_ctx->stream;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	bool enableStereo    = stream->public.timing.timing_3d_format == TIMING_3D_FORMAT_NONE ?
			false:true;
	bool rightEyePolarity = stream->public.timing.flags.RIGHT_EYE_3D_POLARITY;


	/* by upper caller loop, pipe0 is parent pipe and be called first.
	 * back end is set up by for pipe0. Other children pipe share back end
	 * with pipe 0. No program is needed.
	 */
	if (pipe_ctx->top_pipe != NULL)
		return DC_OK;

	/* TODO check if timing_changed, disable stream if timing changed */

	/* HW program guide assume display already disable
	 * by unplug sequence. OTG assume stop.
	 */
	pipe_ctx->tg->funcs->enable_optc_clock(pipe_ctx->tg, true);

	if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
			pipe_ctx->clock_source,
			&pipe_ctx->pix_clk_params,
			&pipe_ctx->pll_settings)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}
	pipe_ctx->tg->dlg_otg_param.vready_offset = pipe_ctx->pipe_dlg_param.vready_offset;
	pipe_ctx->tg->dlg_otg_param.vstartup_start = pipe_ctx->pipe_dlg_param.vstartup_start;
	pipe_ctx->tg->dlg_otg_param.vupdate_offset = pipe_ctx->pipe_dlg_param.vupdate_offset;
	pipe_ctx->tg->dlg_otg_param.vupdate_width = pipe_ctx->pipe_dlg_param.vupdate_width;

	pipe_ctx->tg->dlg_otg_param.signal =  pipe_ctx->stream->signal;

	pipe_ctx->tg->funcs->program_timing(
			pipe_ctx->tg,
			&stream->public.timing,
			true);

	pipe_ctx->opp->funcs->opp_set_stereo_polarity(
				pipe_ctx->opp,
				enableStereo,
				rightEyePolarity);

#if 0 /* move to after enable_crtc */
	/* TODO: OPP FMT, ABM. etc. should be done here. */
	/* or FPGA now. instance 0 only. TODO: move to opp.c */

	inst_offset = reg_offsets[pipe_ctx->tg->inst].fmt;

	pipe_ctx->opp->funcs->opp_program_fmt(
				pipe_ctx->opp,
				&stream->bit_depth_params,
				&stream->clamping);
#endif
	/* program otg blank color */
	color_space = stream->public.output_color_space;
	color_space_to_black_color(dc, color_space, &black_color);
	pipe_ctx->tg->funcs->set_blank_color(
			pipe_ctx->tg,
			&black_color);

	pipe_ctx->tg->funcs->set_blank(pipe_ctx->tg, true);
	hwss_wait_for_blank_complete(pipe_ctx->tg);

	/* VTG is  within DCHUB command block. DCFCLK is always on */
	if (false == pipe_ctx->tg->funcs->enable_crtc(pipe_ctx->tg)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	/* TODO program crtc source select for non-virtual signal*/
	/* TODO program FMT */
	/* TODO setup link_enc */
	/* TODO set stream attributes */
	/* TODO program audio */
	/* TODO enable stream if timing changed */
	/* TODO unblank stream if DP */

	return DC_OK;
}

static void reset_back_end_for_pipe(
		struct core_dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct validate_context *context)
{
	int i;

	if (pipe_ctx->stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}

	/* TODOFPGA break core_link_disable_stream into 2 functions:
	 * disable_stream and disable_link. disable_link will disable PHYPLL
	 * which is used by otg. Move disable_link after disable_crtc
	 */
	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		core_link_disable_stream(pipe_ctx);

	/* by upper caller loop, parent pipe: pipe0, will be reset last.
	 * back end share by all pipes and will be disable only when disable
	 * parent pipe.
	 */
	if (pipe_ctx->top_pipe == NULL) {
		pipe_ctx->tg->funcs->disable_crtc(pipe_ctx->tg);

		pipe_ctx->tg->funcs->enable_optc_clock(pipe_ctx->tg, false);
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		resource_unreference_clock_source(
			&context->res_ctx, dc->res_pool,
			&pipe_ctx->clock_source);

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (&dc->current_context->res_ctx.pipe_ctx[i] == pipe_ctx)
			break;

	if (i == dc->res_pool->pipe_count)
		return;

	pipe_ctx->stream = NULL;
	dm_logger_write(dc->ctx->logger, LOG_DC,
					"Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->tg->inst);
}

static void reset_front_end(
		struct core_dc *dc,
		int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct mpcc_cfg mpcc_cfg;
	struct mem_input *mi = dc->res_pool->mis[fe_idx];
	struct transform *xfm = dc->res_pool->transforms[fe_idx];
	struct mpcc *mpcc = dc->res_pool->mpcc[fe_idx];
	struct timing_generator *tg = dc->res_pool->timing_generators[mpcc->opp_id];

	/*Already reset*/
	if (mpcc->opp_id == 0xf)
		return;

	mi->funcs->dcc_control(mi, false, false);
	tg->funcs->lock(tg);

	mpcc_cfg.opp_id = 0xf;
	mpcc_cfg.top_dpp_id = 0xf;
	mpcc_cfg.bot_mpcc_id = 0xf;
	mpcc_cfg.top_of_tree = tg->inst == mpcc->inst;
	mpcc->funcs->set(mpcc, &mpcc_cfg);

	REG_UPDATE(OTG_GLOBAL_SYNC_STATUS[tg->inst], VUPDATE_NO_LOCK_EVENT_CLEAR, 1);
	tg->funcs->unlock(tg);
	REG_WAIT(OTG_GLOBAL_SYNC_STATUS[tg->inst], VUPDATE_NO_LOCK_EVENT_OCCURRED, 1, 20000, 200000);

	mpcc->funcs->wait_for_idle(mpcc);
	mi->funcs->set_blank(mi, true);
	REG_WAIT(DCHUBP_CNTL[fe_idx], HUBP_NO_OUTSTANDING_REQ, 1, 20000, 200000);
	REG_UPDATE(HUBP_CLK_CNTL[fe_idx], HUBP_CLOCK_ENABLE, 0);
	REG_UPDATE(DPP_CONTROL[fe_idx], DPP_CLOCK_ENABLE, 0);

	xfm->funcs->transform_reset(xfm);

	dm_logger_write(dc->ctx->logger, LOG_DC,
					"Reset front end %d\n",
					fe_idx);
}

static void dcn10_power_down_fe(struct core_dc *dc, int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;

	reset_front_end(dc, fe_idx);

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);
	dpp_pg_control(hws, fe_idx, false);
	hubp_pg_control(hws, fe_idx, false);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);
	dm_logger_write(dc->ctx->logger, LOG_DC,
			"Power gated front end %d\n", fe_idx);
}

static void reset_hw_ctx_wrap(
		struct core_dc *dc,
		struct validate_context *context)
{
	int i;

	/* Reset Front End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/*if (!pipe_ctx_old->stream)
			continue;*/

		if (!pipe_ctx->stream || !pipe_ctx->surface)
			dcn10_power_down_fe(dc, i);
		else if (pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			reset_front_end(dc, i);
	}
	/* Reset Back End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_context);
	}
}

static bool patch_address_for_sbs_tb_stereo(
		struct pipe_ctx *pipe_ctx, PHYSICAL_ADDRESS_LOC *addr)
{
	struct core_surface *surface = pipe_ctx->surface;
	bool sec_split = pipe_ctx->top_pipe &&
			pipe_ctx->top_pipe->surface == pipe_ctx->surface;
	if (sec_split && surface->public.address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
		(pipe_ctx->stream->public.timing.timing_3d_format ==
		 TIMING_3D_FORMAT_SIDE_BY_SIDE ||
		 pipe_ctx->stream->public.timing.timing_3d_format ==
		 TIMING_3D_FORMAT_TOP_AND_BOTTOM)) {
		*addr = surface->public.address.grph_stereo.left_addr;
		surface->public.address.grph_stereo.left_addr =
		surface->public.address.grph_stereo.right_addr;
		return true;
	} else {
		if (pipe_ctx->stream->public.view_format != VIEW_3D_FORMAT_NONE &&
			surface->public.address.type != PLN_ADDR_TYPE_GRPH_STEREO) {
			surface->public.address.type = PLN_ADDR_TYPE_GRPH_STEREO;
			surface->public.address.grph_stereo.right_addr =
			surface->public.address.grph_stereo.left_addr;
		}
	}
	return false;
}

static void update_plane_addr(const struct core_dc *dc, struct pipe_ctx *pipe_ctx)
{
	bool addr_patched = false;
	PHYSICAL_ADDRESS_LOC addr;
	struct core_surface *surface = pipe_ctx->surface;

	if (surface == NULL)
		return;
	addr_patched = patch_address_for_sbs_tb_stereo(pipe_ctx, &addr);
	pipe_ctx->mi->funcs->mem_input_program_surface_flip_and_addr(
			pipe_ctx->mi,
			&surface->public.address,
			surface->public.flip_immediate);
	surface->status.requested_address = surface->public.address;
	if (addr_patched)
		pipe_ctx->surface->public.address.grph_stereo.left_addr = addr;
}

static bool dcn10_set_input_transfer_func(
	struct pipe_ctx *pipe_ctx, const struct core_surface *surface)
{
	struct input_pixel_processor *ipp = pipe_ctx->ipp;
	const struct core_transfer_func *tf = NULL;
	bool result = true;

	if (ipp == NULL)
		return false;

	if (surface->public.in_transfer_func)
		tf = DC_TRANSFER_FUNC_TO_CORE(surface->public.in_transfer_func);

	if (surface->public.gamma_correction && dce_use_lut(surface))
		ipp->funcs->ipp_program_input_lut(ipp,
				surface->public.gamma_correction);

	if (tf == NULL)
		ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_BYPASS);
	else if (tf->public.type == TF_TYPE_PREDEFINED) {
		switch (tf->public.tf) {
		case TRANSFER_FUNCTION_SRGB:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
			result = false;
			break;
		default:
			result = false;
			break;
		}
	} else if (tf->public.type == TF_TYPE_BYPASS) {
		ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_BYPASS);
	} else {
		/*TF_TYPE_DISTRIBUTED_POINTS*/
		result = false;
	}

	return result;
}
/*modify the method to handle rgb for arr_points*/
static bool convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[0].x,
		&fmt,
		&arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].offset,
		&fmt,
		&arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].slope,
		&fmt,
		&arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[1].x,
		&fmt,
		&arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].y,
		&fmt,
		&arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].slope,
		&fmt,
		&arr_points[1].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_blue,
			&fmt,
			&rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}
#define MAX_REGIONS_NUMBER 34
#define MAX_LOW_POINT      25
#define NUMBER_SEGMENTS    32

static bool dcn10_translate_regamma_to_hw_format(const struct dc_transfer_func
		*output_tf, struct pwl_params *regamma_params)
{
	struct curve_points *arr_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;
	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;
	struct fixed31_32 y1_min;
	struct fixed31_32 y3_max;

	int32_t segment_start, segment_end;
	int32_t i;
	uint32_t j, k, seg_distr[MAX_REGIONS_NUMBER], increment, start_index, hw_points;

	if (output_tf == NULL || regamma_params == NULL ||
			output_tf->type == TF_TYPE_BYPASS)
		return false;

	arr_points = regamma_params->arr_points;
	rgb_resulted = regamma_params->rgb_resulted;
	hw_points = 0;

	memset(regamma_params, 0, sizeof(struct pwl_params));
	memset(seg_distr, 0, sizeof(seg_distr));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* 32 segments
		 * segments are from 2^-25 to 2^7
		 */
		for (i = 0; i < 32 ; i++)
			seg_distr[i] = 3;

		segment_start = -25;
		segment_end   = 7;
	} else {
		/* 10 segments
		 * segment is from 2^-10 to 2^0
		 * There are less than 256 points, for optimization
		 */
		seg_distr[0] = 3;
		seg_distr[1] = 4;
		seg_distr[2] = 4;
		seg_distr[3] = 4;
		seg_distr[4] = 4;
		seg_distr[5] = 4;
		seg_distr[6] = 4;
		seg_distr[7] = 4;
		seg_distr[8] = 5;
		seg_distr[9] = 5;

		segment_start = -10;
		segment_end = 0;
	}

	for (i = segment_end - segment_start; i < MAX_REGIONS_NUMBER ; i++)
		seg_distr[i] = -1;

	for (k = 0; k < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (segment_end - segment_start); k++) {
		increment = NUMBER_SEGMENTS / (1 << seg_distr[k]);
		start_index = (segment_start + k + MAX_LOW_POINT) * NUMBER_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SEGMENTS; i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (segment_end + MAX_LOW_POINT) * NUMBER_SEGMENTS;
	rgb_resulted[hw_points - 1].red =
			output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green =
			output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue =
			output_tf->tf_pts.blue[start_index];

	arr_points[0].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_start));
	arr_points[1].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));
	arr_points[2].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	arr_points[0].y = y1_min;
	arr_points[0].slope = dal_fixed31_32_div(
					arr_points[0].y,
					arr_points[0].x);
	y_r = rgb_resulted[hw_points - 1].red;
	y_g = rgb_resulted[hw_points - 1].green;
	y_b = rgb_resulted[hw_points - 1].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[1].y = y3_max;
	arr_points[2].y = y3_max;

	arr_points[1].slope = dal_fixed31_32_zero;
	arr_points[2].slope = dal_fixed31_32_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value =
				dal_fixed31_32_from_int(125);

		arr_points[1].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
		arr_points[2].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
	}

	regamma_params->hw_points_num = hw_points;

	i = 1;
	for (k = 0; k < MAX_REGIONS_NUMBER && i < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1) {
			regamma_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			regamma_params->arr_curve_points[i].offset =
					regamma_params->arr_curve_points[k].
					offset + (1 << seg_distr[k]);
		}
		i++;
	}

	if (seg_distr[k] != -1)
		regamma_params->arr_curve_points[k].segments_num =
				seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;

	while (i != hw_points + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dal_fixed31_32_sub(
			rgb_plus_1->red,
			rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(
			rgb_plus_1->green,
			rgb->green);
		rgb->delta_blue = dal_fixed31_32_sub(
			rgb_plus_1->blue,
			rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}

	convert_to_custom_float(rgb_resulted, arr_points, hw_points);

	return true;
}

static bool dcn10_set_output_transfer_func(
	struct pipe_ctx *pipe_ctx,
	const struct core_stream *stream)
{
	struct output_pixel_processor *opp = pipe_ctx->opp;

	if (opp == NULL)
		return false;

	opp->regamma_params.hw_points_num = GAMMA_HW_POINTS_NUM;

	if (stream->public.out_transfer_func &&
		stream->public.out_transfer_func->type ==
			TF_TYPE_PREDEFINED &&
		stream->public.out_transfer_func->tf ==
			TRANSFER_FUNCTION_SRGB) {
		opp->funcs->opp_set_regamma_mode(opp, OPP_REGAMMA_SRGB);
	} else if (dcn10_translate_regamma_to_hw_format(
				stream->public.out_transfer_func, &opp->regamma_params)) {
			opp->funcs->opp_program_regamma_pwl(opp, &opp->regamma_params);
			opp->funcs->opp_set_regamma_mode(opp, OPP_REGAMMA_USER);
	} else {
		opp->funcs->opp_set_regamma_mode(opp, OPP_REGAMMA_BYPASS);
	}

	return true;
}

static void dcn10_pipe_control_lock(
	struct core_dc *dc,
	struct pipe_ctx *pipe,
	bool lock)
{
	/* use TG master update lock to lock everything on the TG
	 * therefore only top pipe need to lock
	 */
	if (pipe->top_pipe)
		return;

	if (lock)
		pipe->tg->funcs->lock(pipe->tg);
	else
		pipe->tg->funcs->unlock(pipe->tg);
}

static bool wait_for_reset_trigger_to_occur(
	struct dc_context *dc_ctx,
	struct timing_generator *tg)
{
	bool rc = false;

	/* To avoid endless loop we wait at most
	 * frames_to_wait_on_triggered_reset frames for the reset to occur. */
	const uint32_t frames_to_wait_on_triggered_reset = 10;
	int i;

	for (i = 0; i < frames_to_wait_on_triggered_reset; i++) {

		if (!tg->funcs->is_counter_moving(tg)) {
			DC_ERROR("TG counter is not moving!\n");
			break;
		}

		if (tg->funcs->did_triggered_reset_occur(tg)) {
			rc = true;
			/* usually occurs at i=1 */
			DC_SYNC_INFO("GSL: reset occurred at wait count: %d\n",
					i);
			break;
		}

		/* Wait for one frame. */
		tg->funcs->wait_for_state(tg, CRTC_STATE_VACTIVE);
		tg->funcs->wait_for_state(tg, CRTC_STATE_VBLANK);
	}

	if (false == rc)
		DC_ERROR("GSL: Timeout on reset trigger!\n");

	return rc;
}

static void dcn10_enable_timing_synchronization(
	struct core_dc *dc,
	int group_index,
	int group_size,
	struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	int i;

	DC_SYNC_INFO("Setting up OTG reset trigger\n");

	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->tg->funcs->enable_reset_trigger(
				grouped_pipes[i]->tg, grouped_pipes[0]->tg->inst);


	DC_SYNC_INFO("Waiting for trigger\n");

	/* Need to get only check 1 pipe for having reset as all the others are
	 * synchronized. Look at last pipe programmed to reset.
	 */
	wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[1]->tg);
	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->tg->funcs->disable_reset_trigger(
				grouped_pipes[i]->tg);

	DC_SYNC_INFO("Sync complete\n");
}

static void print_rq_dlg_ttu(
		struct core_dc *core_dc,
		struct pipe_ctx *pipe_ctx)
{
	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML TTU Output parameters [%d] ==============\n"
			"qos_level_low_wm: %d, \n"
			"qos_level_high_wm: %d, \n"
			"min_ttu_vblank: %d, \n"
			"qos_level_flip: %d, \n"
			"refcyc_per_req_delivery_l: %d, \n"
			"qos_level_fixed_l: %d, \n"
			"qos_ramp_disable_l: %d, \n"
			"refcyc_per_req_delivery_pre_l: %d, \n"
			"refcyc_per_req_delivery_c: %d, \n"
			"qos_level_fixed_c: %d, \n"
			"qos_ramp_disable_c: %d, \n"
			"refcyc_per_req_delivery_pre_c: %d\n"
			"=============================================================\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->ttu_regs.qos_level_low_wm,
			pipe_ctx->ttu_regs.qos_level_high_wm,
			pipe_ctx->ttu_regs.min_ttu_vblank,
			pipe_ctx->ttu_regs.qos_level_flip,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_l,
			pipe_ctx->ttu_regs.qos_level_fixed_l,
			pipe_ctx->ttu_regs.qos_ramp_disable_l,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_pre_l,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_c,
			pipe_ctx->ttu_regs.qos_level_fixed_c,
			pipe_ctx->ttu_regs.qos_ramp_disable_c,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_pre_c
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML DLG Output parameters [%d] ==============\n"
			"refcyc_h_blank_end: %d, \n"
			"dlg_vblank_end: %d, \n"
			"min_dst_y_next_start: %d, \n"
			"refcyc_per_htotal: %d, \n"
			"refcyc_x_after_scaler: %d, \n"
			"dst_y_after_scaler: %d, \n"
			"dst_y_prefetch: %d, \n"
			"dst_y_per_vm_vblank: %d, \n"
			"dst_y_per_row_vblank: %d, \n"
			"ref_freq_to_pix_freq: %d, \n"
			"vratio_prefetch: %d, \n"
			"refcyc_per_pte_group_vblank_l: %d, \n"
			"refcyc_per_meta_chunk_vblank_l: %d, \n"
			"dst_y_per_pte_row_nom_l: %d, \n"
			"refcyc_per_pte_group_nom_l: %d, \n",
			pipe_ctx->pipe_idx,
			pipe_ctx->dlg_regs.refcyc_h_blank_end,
			pipe_ctx->dlg_regs.dlg_vblank_end,
			pipe_ctx->dlg_regs.min_dst_y_next_start,
			pipe_ctx->dlg_regs.refcyc_per_htotal,
			pipe_ctx->dlg_regs.refcyc_x_after_scaler,
			pipe_ctx->dlg_regs.dst_y_after_scaler,
			pipe_ctx->dlg_regs.dst_y_prefetch,
			pipe_ctx->dlg_regs.dst_y_per_vm_vblank,
			pipe_ctx->dlg_regs.dst_y_per_row_vblank,
			pipe_ctx->dlg_regs.ref_freq_to_pix_freq,
			pipe_ctx->dlg_regs.vratio_prefetch,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_vblank_l,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_vblank_l,
			pipe_ctx->dlg_regs.dst_y_per_pte_row_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_nom_l
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\ndst_y_per_meta_row_nom_l: %d, \n"
			"refcyc_per_meta_chunk_nom_l: %d, \n"
			"refcyc_per_line_delivery_pre_l: %d, \n"
			"refcyc_per_line_delivery_l: %d, \n"
			"vratio_prefetch_c: %d, \n"
			"refcyc_per_pte_group_vblank_c: %d, \n"
			"refcyc_per_meta_chunk_vblank_c: %d, \n"
			"dst_y_per_pte_row_nom_c: %d, \n"
			"refcyc_per_pte_group_nom_c: %d, \n"
			"dst_y_per_meta_row_nom_c: %d, \n"
			"refcyc_per_meta_chunk_nom_c: %d, \n"
			"refcyc_per_line_delivery_pre_c: %d, \n"
			"refcyc_per_line_delivery_c: %d \n"
			"========================================================\n",
			pipe_ctx->dlg_regs.dst_y_per_meta_row_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_pre_l,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_l,
			pipe_ctx->dlg_regs.vratio_prefetch_c,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_vblank_c,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_vblank_c,
			pipe_ctx->dlg_regs.dst_y_per_pte_row_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_nom_c,
			pipe_ctx->dlg_regs.dst_y_per_meta_row_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_pre_c,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_c
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML RQ Output parameters [%d] ==============\n"
			"chunk_size: %d \n"
			"min_chunk_size: %d \n"
			"meta_chunk_size: %d \n"
			"min_meta_chunk_size: %d \n"
			"dpte_group_size: %d \n"
			"mpte_group_size: %d \n"
			"swath_height: %d \n"
			"pte_row_height_linear: %d \n"
			"========================================================\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->rq_regs.rq_regs_l.chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.min_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.meta_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.min_meta_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.dpte_group_size,
			pipe_ctx->rq_regs.rq_regs_l.mpte_group_size,
			pipe_ctx->rq_regs.rq_regs_l.swath_height,
			pipe_ctx->rq_regs.rq_regs_l.pte_row_height_linear
			);
}

static void dcn10_power_on_fe(
	struct core_dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct validate_context *context)
{
	struct dc_surface *dc_surface = &pipe_ctx->surface->public;
	struct dce_hwseq *hws = dc->hwseq;

	power_on_plane(dc->hwseq,
		pipe_ctx->pipe_idx);

	/* enable DCFCLK current DCHUB */
	REG_UPDATE(HUBP_CLK_CNTL[pipe_ctx->pipe_idx], HUBP_CLOCK_ENABLE, 1);

	if (dc_surface) {
		dm_logger_write(dc->ctx->logger, LOG_DC,
				"Pipe:%d 0x%x: addr hi:0x%x, "
				"addr low:0x%x, "
				"src: %d, %d, %d,"
				" %d; dst: %d, %d, %d, %d;\n",
				pipe_ctx->pipe_idx,
				dc_surface,
				dc_surface->address.grph.addr.high_part,
				dc_surface->address.grph.addr.low_part,
				dc_surface->src_rect.x,
				dc_surface->src_rect.y,
				dc_surface->src_rect.width,
				dc_surface->src_rect.height,
				dc_surface->dst_rect.x,
				dc_surface->dst_rect.y,
				dc_surface->dst_rect.width,
				dc_surface->dst_rect.height);

		dm_logger_write(dc->ctx->logger, LOG_HW_SET_MODE,
				"Pipe %d: width, height, x, y\n"
				"viewport:%d, %d, %d, %d\n"
				"recout:  %d, %d, %d, %d\n",
				pipe_ctx->pipe_idx,
				pipe_ctx->scl_data.viewport.width,
				pipe_ctx->scl_data.viewport.height,
				pipe_ctx->scl_data.viewport.x,
				pipe_ctx->scl_data.viewport.y,
				pipe_ctx->scl_data.recout.width,
				pipe_ctx->scl_data.recout.height,
				pipe_ctx->scl_data.recout.x,
				pipe_ctx->scl_data.recout.y);
		print_rq_dlg_ttu(dc, pipe_ctx);
	}
}

static void program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	struct xfm_grph_csc_adjustment adjust;
	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;


	if (pipe_ctx->stream->public.gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		adjust.temperature_matrix[0] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[0];
		adjust.temperature_matrix[1] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[1];
		adjust.temperature_matrix[2] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[2];
		adjust.temperature_matrix[3] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[4];
		adjust.temperature_matrix[4] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[5];
		adjust.temperature_matrix[5] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[6];
		adjust.temperature_matrix[6] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[8];
		adjust.temperature_matrix[7] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[9];
		adjust.temperature_matrix[8] =
				pipe_ctx->stream->
				public.gamut_remap_matrix.matrix[10];
	}

	pipe_ctx->xfm->funcs->transform_set_gamut_remap(pipe_ctx->xfm, &adjust);
}


static void program_csc_matrix(struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix)
{
	int i;
	struct out_csc_color_matrix tbl_entry;

	if (pipe_ctx->stream->public.csc_color_matrix.enable_adjustment
				== true) {
			enum dc_color_space color_space =
				pipe_ctx->stream->public.output_color_space;

			//uint16_t matrix[12];
			for (i = 0; i < 12; i++)
				tbl_entry.regval[i] = pipe_ctx->stream->public.csc_color_matrix.matrix[i];

			tbl_entry.color_space = color_space;
			//tbl_entry.regval = matrix;
			pipe_ctx->opp->funcs->opp_set_csc_adjustment(pipe_ctx->opp, &tbl_entry);
	}
}
static bool is_lower_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->surface->public.visible)
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

static bool is_upper_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->surface->public.visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	return false;
}

static bool is_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->surface->public.visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

static bool is_rgb_cspace(enum dc_color_space output_color_space)
{
	switch (output_color_space) {
	case COLOR_SPACE_SRGB:
	case COLOR_SPACE_SRGB_LIMITED:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_ADOBERGB:
		return true;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_2020_YCBCR:
		return false;
	default:
		/* Add a case to switch */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

static void dcn10_get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	switch (pipe_ctx->scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set boarder color to red */
		color->color_r_cr = color_value;
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set boarder color to blue */
		color->color_b_cb = color_value;
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set boarder color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set boarder color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set boarder color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		break;
	default:
		break;
	}
}

static void update_dchubp_dpp(
	struct core_dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct validate_context *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct mem_input *mi = pipe_ctx->mi;
	struct input_pixel_processor *ipp = pipe_ctx->ipp;
	struct core_surface *surface = pipe_ctx->surface;
	union plane_size size = surface->public.plane_size;
	struct default_adjustment ocsc = {0};
	struct tg_color black_color = {0};
	struct mpcc_cfg mpcc_cfg;
	bool per_pixel_alpha = surface->public.per_pixel_alpha && pipe_ctx->bottom_pipe;

	/* TODO: proper fix once fpga works */
	/* depends on DML calculation, DPP clock value may change dynamically */
	enable_dppclk(
		dc->hwseq,
		pipe_ctx->pipe_idx,
		pipe_ctx->pix_clk_params.requested_pix_clk,
		context->bw.dcn.calc_clk.dppclk_div);
	dc->current_context->bw.dcn.cur_clk.dppclk_div =
			context->bw.dcn.calc_clk.dppclk_div;
	context->bw.dcn.cur_clk.dppclk_div = context->bw.dcn.calc_clk.dppclk_div;

	/* TODO: Need input parameter to tell current DCHUB pipe tie to which OTG
	 * VTG is within DCHUBBUB which is commond block share by each pipe HUBP.
	 * VTG is 1:1 mapping with OTG. Each pipe HUBP will select which VTG
	 */
	REG_UPDATE(DCHUBP_CNTL[pipe_ctx->pipe_idx], HUBP_VTG_SEL, pipe_ctx->tg->inst);

	update_plane_addr(dc, pipe_ctx);

	mi->funcs->mem_input_setup(
		mi,
		&pipe_ctx->dlg_regs,
		&pipe_ctx->ttu_regs,
		&pipe_ctx->rq_regs,
		&pipe_ctx->pipe_dlg_param);

	size.grph.surface_size = pipe_ctx->scl_data.viewport;

	if (dc->public.config.gpu_vm_support)
		mi->funcs->mem_input_program_pte_vm(
				pipe_ctx->mi,
				surface->public.format,
				&surface->public.tiling_info,
				surface->public.rotation);

	ipp->funcs->ipp_setup(ipp,
			surface->public.format,
			1,
			IPP_OUTPUT_FORMAT_12_BIT_FIX);

	pipe_ctx->scl_data.lb_params.alpha_en = per_pixel_alpha;
	mpcc_cfg.top_dpp_id = pipe_ctx->pipe_idx;
	if (pipe_ctx->bottom_pipe)
		mpcc_cfg.bot_mpcc_id = pipe_ctx->bottom_pipe->mpcc->inst;
	else
		mpcc_cfg.bot_mpcc_id = 0xf;
	mpcc_cfg.opp_id = pipe_ctx->tg->inst;
	mpcc_cfg.top_of_tree = pipe_ctx->pipe_idx == pipe_ctx->tg->inst;
	mpcc_cfg.per_pixel_alpha = per_pixel_alpha;
	/* DCN1.0 has output CM before MPC which seems to screw with
	 * pre-multiplied alpha.
	 */
	mpcc_cfg.pre_multiplied_alpha = is_rgb_cspace(
			pipe_ctx->stream->public.output_color_space)
					&& per_pixel_alpha;
	pipe_ctx->mpcc->funcs->set(pipe_ctx->mpcc, &mpcc_cfg);

	if (dc->public.debug.surface_visual_confirm) {
		dcn10_get_surface_visual_confirm_color(pipe_ctx, &black_color);
	} else {
		color_space_to_black_color(
			dc, pipe_ctx->stream->public.output_color_space,
			&black_color);
	}
	pipe_ctx->mpcc->funcs->set_bg_color(pipe_ctx->mpcc, &black_color);

	pipe_ctx->scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;
	/* scaler configuration */
	pipe_ctx->xfm->funcs->transform_set_scaler(
			pipe_ctx->xfm, &pipe_ctx->scl_data);

	/*gamut remap*/
	program_gamut_remap(pipe_ctx);

	/*TODO add adjustments parameters*/
	ocsc.out_color_space = pipe_ctx->stream->public.output_color_space;
	pipe_ctx->opp->funcs->opp_set_csc_default(pipe_ctx->opp, &ocsc);

	mi->funcs->mem_input_program_surface_config(
		mi,
		surface->public.format,
		&surface->public.tiling_info,
		&size,
		surface->public.rotation,
		&surface->public.dcc,
		surface->public.horizontal_mirror);

	mi->funcs->set_blank(mi, !is_pipe_tree_visible(pipe_ctx));
}

static void program_all_pipe_in_tree(
		struct core_dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct validate_context *context)
{
	unsigned int ref_clk_mhz = dc->res_pool->ref_clock_inKhz/1000;

	if (pipe_ctx->top_pipe == NULL) {

		/* lock otg_master_update to process all pipes associated with
		 * this OTG. this is done only one time.
		 */
		/* watermark is for all pipes */
		pipe_ctx->mi->funcs->program_watermarks(
				pipe_ctx->mi, &context->bw.dcn.watermarks, ref_clk_mhz);
		pipe_ctx->tg->funcs->lock(pipe_ctx->tg);

		pipe_ctx->tg->dlg_otg_param.vready_offset = pipe_ctx->pipe_dlg_param.vready_offset;
		pipe_ctx->tg->dlg_otg_param.vstartup_start = pipe_ctx->pipe_dlg_param.vstartup_start;
		pipe_ctx->tg->dlg_otg_param.vupdate_offset = pipe_ctx->pipe_dlg_param.vupdate_offset;
		pipe_ctx->tg->dlg_otg_param.vupdate_width = pipe_ctx->pipe_dlg_param.vupdate_width;
		pipe_ctx->tg->dlg_otg_param.signal =  pipe_ctx->stream->signal;

		pipe_ctx->tg->funcs->program_global_sync(
				pipe_ctx->tg);
		pipe_ctx->tg->funcs->set_blank(pipe_ctx->tg, !is_pipe_tree_visible(pipe_ctx));
	}

	if (pipe_ctx->surface != NULL) {
		dcn10_power_on_fe(dc, pipe_ctx, context);
		update_dchubp_dpp(dc, pipe_ctx, context);
	}

	if (pipe_ctx->bottom_pipe != NULL)
		program_all_pipe_in_tree(dc, pipe_ctx->bottom_pipe, context);
}

static void dcn10_pplib_apply_display_requirements(
	struct core_dc *dc,
	struct validate_context *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->all_displays_in_sync = false;/*todo*/
	pp_display_cfg->nb_pstate_switch_disable = false;
	pp_display_cfg->min_engine_clock_khz = context->bw.dcn.cur_clk.dcfclk_khz;
	pp_display_cfg->min_memory_clock_khz = context->bw.dcn.cur_clk.fclk_khz;
	pp_display_cfg->min_engine_clock_deep_sleep_khz = context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz;
	pp_display_cfg->min_dcfc_deep_sleep_clock_khz = context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz;
	pp_display_cfg->avail_mclk_switch_time_us =
			context->bw.dcn.cur_clk.dram_ccm_us > 0 ? context->bw.dcn.cur_clk.dram_ccm_us : 0;
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us =
			context->bw.dcn.cur_clk.min_active_dram_ccm_us > 0 ? context->bw.dcn.cur_clk.min_active_dram_ccm_us : 0;
	pp_display_cfg->min_dcfclock_khz = context->bw.dcn.cur_clk.dcfclk_khz;
	pp_display_cfg->disp_clk_khz = context->bw.dcn.cur_clk.dispclk_khz;
	dce110_fill_display_configs(context, pp_display_cfg);

	if (memcmp(&dc->prev_display_config, pp_display_cfg, sizeof(
			struct dm_pp_display_configuration)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);

	dc->prev_display_config = *pp_display_cfg;
}

static void dcn10_apply_ctx_for_surface(
		struct core_dc *dc,
		struct core_surface *surface,
		struct validate_context *context)
{
	int i;

	/* reset unused mpcc */
	/*for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx =
				&dc->current_context->res_ctx.pipe_ctx[i];

		if ((!pipe_ctx->surface && old_pipe_ctx->surface)
				|| (!pipe_ctx->stream && old_pipe_ctx->stream)) {
			struct mpcc_cfg mpcc_cfg;

			mpcc_cfg.opp_id = 0xf;
			mpcc_cfg.top_dpp_id = 0xf;
			mpcc_cfg.bot_mpcc_id = 0xf;
			mpcc_cfg.top_of_tree = !old_pipe_ctx->top_pipe;
			old_pipe_ctx->mpcc->funcs->set(old_pipe_ctx->mpcc, &mpcc_cfg);

			old_pipe_ctx->top_pipe = NULL;
			old_pipe_ctx->bottom_pipe = NULL;
			old_pipe_ctx->surface = NULL;

			dm_logger_write(dc->ctx->logger, LOG_DC,
					"Reset mpcc for pipe %d\n",
					old_pipe_ctx->pipe_idx);
		}
	}*/

	if (!surface)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->surface != surface)
			continue;

		/* looking for top pipe to program */
		if (!pipe_ctx->top_pipe)
			program_all_pipe_in_tree(dc, pipe_ctx, context);
	}

	dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== Watermark parameters ==============\n"
			"a.urgent_ns: %d \n"
			"a.cstate_enter_plus_exit: %d \n"
			"a.cstate_exit: %d \n"
			"a.pstate_change: %d \n"
			"a.pte_meta_urgent: %d \n"
			"b.urgent_ns: %d \n"
			"b.cstate_enter_plus_exit: %d \n"
			"b.cstate_exit: %d \n"
			"b.pstate_change: %d \n"
			"b.pte_meta_urgent: %d \n",
			context->bw.dcn.watermarks.a.urgent_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.a.pte_meta_urgent_ns,
			context->bw.dcn.watermarks.b.urgent_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.b.pte_meta_urgent_ns
			);
	dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\nc.urgent_ns: %d \n"
			"c.cstate_enter_plus_exit: %d \n"
			"c.cstate_exit: %d \n"
			"c.pstate_change: %d \n"
			"c.pte_meta_urgent: %d \n"
			"d.urgent_ns: %d \n"
			"d.cstate_enter_plus_exit: %d \n"
			"d.cstate_exit: %d \n"
			"d.pstate_change: %d \n"
			"d.pte_meta_urgent: %d \n"
			"========================================================\n",
			context->bw.dcn.watermarks.c.urgent_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.c.pte_meta_urgent_ns,
			context->bw.dcn.watermarks.d.urgent_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.d.pte_meta_urgent_ns
			);
}

static void dcn10_set_bandwidth(
		struct core_dc *dc,
		struct validate_context *context,
		bool decrease_allowed)
{
	struct dm_pp_clock_for_voltage_req clock;

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		return;

	if (decrease_allowed || context->bw.dcn.calc_clk.dispclk_khz
			> dc->current_context->bw.dcn.cur_clk.dispclk_khz) {
		dc->res_pool->display_clock->funcs->set_clock(
				dc->res_pool->display_clock,
				context->bw.dcn.calc_clk.dispclk_khz);
		dc->current_context->bw.dcn.cur_clk.dispclk_khz =
				context->bw.dcn.calc_clk.dispclk_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.dcfclk_khz
			> dc->current_context->bw.dcn.cur_clk.dcfclk_khz) {
		clock.clk_type = DM_PP_CLOCK_TYPE_DCFCLK;
		clock.clocks_in_khz = context->bw.dcn.calc_clk.dcfclk_khz;
		dm_pp_apply_clock_for_voltage_request(dc->ctx, &clock);
		dc->current_context->bw.dcn.cur_clk.dcfclk_khz = clock.clocks_in_khz;
		context->bw.dcn.cur_clk.dcfclk_khz = clock.clocks_in_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.fclk_khz
			> dc->current_context->bw.dcn.cur_clk.fclk_khz) {
		clock.clk_type = DM_PP_CLOCK_TYPE_FCLK;
		clock.clocks_in_khz = context->bw.dcn.calc_clk.fclk_khz;
		dm_pp_apply_clock_for_voltage_request(dc->ctx, &clock);
		dc->current_context->bw.dcn.calc_clk.fclk_khz = clock.clocks_in_khz;
		context->bw.dcn.cur_clk.fclk_khz = clock.clocks_in_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz
			> dc->current_context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz) {
		dc->current_context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz =
				context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz;
		context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz =
				context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz;
	}
	/* Decrease in freq is increase in period so opposite comparison for dram_ccm */
	if (decrease_allowed || context->bw.dcn.calc_clk.dram_ccm_us
			< dc->current_context->bw.dcn.cur_clk.dram_ccm_us) {
		dc->current_context->bw.dcn.calc_clk.dram_ccm_us =
				context->bw.dcn.calc_clk.dram_ccm_us;
		context->bw.dcn.cur_clk.dram_ccm_us =
				context->bw.dcn.calc_clk.dram_ccm_us;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.min_active_dram_ccm_us
			< dc->current_context->bw.dcn.cur_clk.min_active_dram_ccm_us) {
		dc->current_context->bw.dcn.calc_clk.min_active_dram_ccm_us =
				context->bw.dcn.calc_clk.min_active_dram_ccm_us;
		context->bw.dcn.cur_clk.min_active_dram_ccm_us =
				context->bw.dcn.calc_clk.min_active_dram_ccm_us;
	}
	dcn10_pplib_apply_display_requirements(dc, context);
}

static void set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, int vmin, int vmax)
{
	int i = 0;
	struct drr_params params = {0};

	params.vertical_total_max = vmax;
	params.vertical_total_min = vmin;

	/* TODO: If multiple pipes are to be supported, you need
	 * some GSL stuff
	 */
	for (i = 0; i < num_pipes; i++) {
		pipe_ctx[i]->tg->funcs->set_drr(pipe_ctx[i]->tg, &params);
	}
}

static void get_position(struct pipe_ctx **pipe_ctx,
		int num_pipes,
		struct crtc_position *position)
{
	int i = 0;

	/* TODO: handle pipes > 1
	 */
	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->tg->funcs->get_position(pipe_ctx[i]->tg, position);
}

static void set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_events *events)
{
	unsigned int i;
	unsigned int value = 0;

	if (events->surface_update)
		value |= 0x80;
	if (events->cursor_update)
		value |= 0x2;

	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->tg->funcs->
			set_static_screen_control(pipe_ctx[i]->tg, value);
}

static void set_plane_config(
	const struct core_dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct resource_context *res_ctx)
{
	/* TODO */
	program_gamut_remap(pipe_ctx);
}

static void dcn10_config_stereo_parameters(
		struct core_stream *stream, struct crtc_stereo_flags *flags)
{
	enum view_3d_format view_format = stream->public.view_format;
	enum dc_timing_3d_format timing_3d_format =\
			stream->public.timing.timing_3d_format;
	bool non_stereo_timing = false;

	if (timing_3d_format == TIMING_3D_FORMAT_NONE ||
		timing_3d_format == TIMING_3D_FORMAT_SIDE_BY_SIDE ||
		timing_3d_format == TIMING_3D_FORMAT_TOP_AND_BOTTOM)
		non_stereo_timing = true;

	if (non_stereo_timing == false &&
		view_format == VIEW_3D_FORMAT_FRAME_SEQUENTIAL) {

		flags->PROGRAM_STEREO         = 1;
		flags->PROGRAM_POLARITY       = 1;
		if (timing_3d_format == TIMING_3D_FORMAT_INBAND_FA ||
			timing_3d_format == TIMING_3D_FORMAT_DP_HDMI_INBAND_FA ||
			timing_3d_format == TIMING_3D_FORMAT_SIDEBAND_FA) {
			enum display_dongle_type dongle = \
					stream->sink->link->public.ddc->dongle_type;
			if (dongle == DISPLAY_DONGLE_DP_VGA_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_DVI_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_HDMI_CONVERTER)
				flags->DISABLE_STEREO_DP_SYNC = 1;
		}
		flags->RIGHT_EYE_POLARITY =\
				stream->public.timing.flags.RIGHT_EYE_3D_POLARITY;
		if (timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			flags->FRAME_PACKED = 1;
	}

	return;
}

static void dcn10_setup_stereo(struct pipe_ctx *pipe_ctx, struct core_dc *dc)
{
	struct crtc_stereo_flags flags = { 0 };
	struct core_stream *stream = pipe_ctx->stream;

	dcn10_config_stereo_parameters(stream, &flags);

	pipe_ctx->opp->funcs->opp_set_stereo_polarity(
		pipe_ctx->opp,
		flags.PROGRAM_STEREO == 1 ? true:false,
		stream->public.timing.flags.RIGHT_EYE_3D_POLARITY == 1 ? true:false);

	pipe_ctx->tg->funcs->program_stereo(
		pipe_ctx->tg,
		&stream->public.timing,
		&flags);

	return;
}

static bool dcn10_dummy_display_power_gating(
	struct core_dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating) {return true; }

static const struct hw_sequencer_funcs dcn10_funcs = {
	.program_gamut_remap = program_gamut_remap,
	.program_csc_matrix = program_csc_matrix,
	.init_hw = init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = dcn10_apply_ctx_for_surface,
	.set_plane_config = set_plane_config,
	.update_plane_addr = update_plane_addr,
	.update_pending_status = dce110_update_pending_status,
	.set_input_transfer_func = dcn10_set_input_transfer_func,
	.set_output_transfer_func = dcn10_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.update_info_frame = dce110_update_info_frame,
	.enable_stream = dce110_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dce110_unblank_stream,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.power_down_front_end = dcn10_power_down_fe,
	.power_on_front_end = dcn10_power_on_fe,
	.pipe_control_lock = dcn10_pipe_control_lock,
	.set_bandwidth = dcn10_set_bandwidth,
	.reset_hw_ctx_wrap = reset_hw_ctx_wrap,
	.prog_pixclk_crtc_otg = dcn10_prog_pixclk_crtc_otg,
	.set_drr = set_drr,
	.get_position = get_position,
	.set_static_screen_control = set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo
};


bool dcn10_hw_sequencer_construct(struct core_dc *dc)
{
	dc->hwss = dcn10_funcs;
	return true;
}

