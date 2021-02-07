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

#include <linux/delay.h>
#include "dm_services.h"
#include "basics/dc_common.h"
#include "core_types.h"
#include "resource.h"
#include "custom_float.h"
#include "dcn10_hw_sequencer.h"
#include "dcn10_hw_sequencer_debug.h"
#include "dce/dce_hwseq.h"
#include "abm.h"
#include "dmcu.h"
#include "dcn10_optc.h"
#include "dcn10_dpp.h"
#include "dcn10_mpc.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "reg_helper.h"
#include "dcn10_hubp.h"
#include "dcn10_hubbub.h"
#include "dcn10_cm_common.h"
#include "dc_link_dp.h"
#include "dccg.h"
#include "clk_mgr.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dsc.h"
#include "dce/dmub_hw_lock_mgr.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

/*print is 17 wide, first two characters are spaces*/
#define DTN_INFO_MICRO_SEC(ref_cycle) \
	print_microsec(dc_ctx, log_ctx, ref_cycle)

#define GAMMA_HW_POINTS_NUM 256

void print_microsec(struct dc_context *dc_ctx,
	struct dc_log_buffer_ctx *log_ctx,
	uint32_t ref_cycle)
{
	const uint32_t ref_clk_mhz = dc_ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	static const unsigned int frac = 1000;
	uint32_t us_x10 = (ref_cycle * frac) / ref_clk_mhz;

	DTN_INFO("  %11d.%03d",
			us_x10 / frac,
			us_x10 % frac);
}

void dcn10_lock_all_pipes(struct dc *dc,
	struct dc_state *context,
	bool lock)
{
	struct pipe_ctx *pipe_ctx;
	struct timing_generator *tg;
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe_ctx = &context->res_ctx.pipe_ctx[i];
		tg = pipe_ctx->stream_res.tg;

		/*
		 * Only lock the top pipe's tg to prevent redundant
		 * (un)locking. Also skip if pipe is disabled.
		 */
		if (pipe_ctx->top_pipe ||
		    !pipe_ctx->stream || !pipe_ctx->plane_state ||
		    !tg->funcs->is_tg_enabled(tg))
			continue;

		if (lock)
			dc->hwss.pipe_control_lock(dc, pipe_ctx, true);
		else
			dc->hwss.pipe_control_lock(dc, pipe_ctx, false);
	}
}

static void log_mpc_crc(struct dc *dc,
	struct dc_log_buffer_ctx *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dce_hwseq *hws = dc->hwseq;

	if (REG(MPC_CRC_RESULT_GB))
		DTN_INFO("MPC_CRC_RESULT_GB:%d MPC_CRC_RESULT_C:%d MPC_CRC_RESULT_AR:%d\n",
		REG_READ(MPC_CRC_RESULT_GB), REG_READ(MPC_CRC_RESULT_C), REG_READ(MPC_CRC_RESULT_AR));
	if (REG(DPP_TOP0_DPP_CRC_VAL_B_A))
		DTN_INFO("DPP_TOP0_DPP_CRC_VAL_B_A:%d DPP_TOP0_DPP_CRC_VAL_R_G:%d\n",
		REG_READ(DPP_TOP0_DPP_CRC_VAL_B_A), REG_READ(DPP_TOP0_DPP_CRC_VAL_R_G));
}

void dcn10_log_hubbub_state(struct dc *dc, struct dc_log_buffer_ctx *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dcn_hubbub_wm wm;
	int i;

	memset(&wm, 0, sizeof(struct dcn_hubbub_wm));
	dc->res_pool->hubbub->funcs->wm_read_state(dc->res_pool->hubbub, &wm);

	DTN_INFO("HUBBUB WM:      data_urgent  pte_meta_urgent"
			"         sr_enter          sr_exit  dram_clk_change\n");

	for (i = 0; i < 4; i++) {
		struct dcn_hubbub_wm_set *s;

		s = &wm.sets[i];
		DTN_INFO("WM_Set[%d]:", s->wm_set);
		DTN_INFO_MICRO_SEC(s->data_urgent);
		DTN_INFO_MICRO_SEC(s->pte_meta_urgent);
		DTN_INFO_MICRO_SEC(s->sr_enter);
		DTN_INFO_MICRO_SEC(s->sr_exit);
		DTN_INFO_MICRO_SEC(s->dram_clk_chanage);
		DTN_INFO("\n");
	}

	DTN_INFO("\n");
}

static void dcn10_log_hubp_states(struct dc *dc, void *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	int i;

	DTN_INFO(
		"HUBP:  format  addr_hi  width  height  rot  mir  sw_mode  dcc_en  blank_en  clock_en  ttu_dis  underflow   min_ttu_vblank       qos_low_wm      qos_high_wm\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct hubp *hubp = pool->hubps[i];
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(hubp)->state);

		hubp->funcs->hubp_read_state(hubp);

		if (!s->blank_en) {
			DTN_INFO("[%2d]:  %5xh  %6xh  %5d  %6d  %2xh  %2xh  %6xh  %6d  %8d  %8d  %7d  %8xh",
					hubp->inst,
					s->pixel_format,
					s->inuse_addr_hi,
					s->viewport_width,
					s->viewport_height,
					s->rotation_angle,
					s->h_mirror_en,
					s->sw_mode,
					s->dcc_en,
					s->blank_en,
					s->clock_en,
					s->ttu_disable,
					s->underflow_status);
			DTN_INFO_MICRO_SEC(s->min_ttu_vblank);
			DTN_INFO_MICRO_SEC(s->qos_level_low_wm);
			DTN_INFO_MICRO_SEC(s->qos_level_high_wm);
			DTN_INFO("\n");
		}
	}

	DTN_INFO("\n=========RQ========\n");
	DTN_INFO("HUBP:  drq_exp_m  prq_exp_m  mrq_exp_m  crq_exp_m  plane1_ba  L:chunk_s  min_chu_s  meta_ch_s"
		"  min_m_c_s  dpte_gr_s  mpte_gr_s  swath_hei  pte_row_h  C:chunk_s  min_chu_s  meta_ch_s"
		"  min_m_c_s  dpte_gr_s  mpte_gr_s  swath_hei  pte_row_h\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_rq_regs_st *rq_regs = &s->rq_regs;

		if (!s->blank_en)
			DTN_INFO("[%2d]:  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh\n",
				pool->hubps[i]->inst, rq_regs->drq_expansion_mode, rq_regs->prq_expansion_mode, rq_regs->mrq_expansion_mode,
				rq_regs->crq_expansion_mode, rq_regs->plane1_base_address, rq_regs->rq_regs_l.chunk_size,
				rq_regs->rq_regs_l.min_chunk_size, rq_regs->rq_regs_l.meta_chunk_size,
				rq_regs->rq_regs_l.min_meta_chunk_size, rq_regs->rq_regs_l.dpte_group_size,
				rq_regs->rq_regs_l.mpte_group_size, rq_regs->rq_regs_l.swath_height,
				rq_regs->rq_regs_l.pte_row_height_linear, rq_regs->rq_regs_c.chunk_size, rq_regs->rq_regs_c.min_chunk_size,
				rq_regs->rq_regs_c.meta_chunk_size, rq_regs->rq_regs_c.min_meta_chunk_size,
				rq_regs->rq_regs_c.dpte_group_size, rq_regs->rq_regs_c.mpte_group_size,
				rq_regs->rq_regs_c.swath_height, rq_regs->rq_regs_c.pte_row_height_linear);
	}

	DTN_INFO("========DLG========\n");
	DTN_INFO("HUBP:  rc_hbe     dlg_vbe    min_d_y_n  rc_per_ht  rc_x_a_s "
			"  dst_y_a_s  dst_y_pf   dst_y_vvb  dst_y_rvb  dst_y_vfl  dst_y_rfl  rf_pix_fq"
			"  vratio_pf  vrat_pf_c  rc_pg_vbl  rc_pg_vbc  rc_mc_vbl  rc_mc_vbc  rc_pg_fll"
			"  rc_pg_flc  rc_mc_fll  rc_mc_flc  pr_nom_l   pr_nom_c   rc_pg_nl   rc_pg_nc "
			"  mr_nom_l   mr_nom_c   rc_mc_nl   rc_mc_nc   rc_ld_pl   rc_ld_pc   rc_ld_l  "
			"  rc_ld_c    cha_cur0   ofst_cur1  cha_cur1   vr_af_vc0  ddrq_limt  x_rt_dlay"
			"  x_rp_dlay  x_rr_sfl\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs = &s->dlg_attr;

		if (!s->blank_en)
			DTN_INFO("[%2d]:  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh"
				"%  8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh"
				"  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh\n",
				pool->hubps[i]->inst, dlg_regs->refcyc_h_blank_end, dlg_regs->dlg_vblank_end, dlg_regs->min_dst_y_next_start,
				dlg_regs->refcyc_per_htotal, dlg_regs->refcyc_x_after_scaler, dlg_regs->dst_y_after_scaler,
				dlg_regs->dst_y_prefetch, dlg_regs->dst_y_per_vm_vblank, dlg_regs->dst_y_per_row_vblank,
				dlg_regs->dst_y_per_vm_flip, dlg_regs->dst_y_per_row_flip, dlg_regs->ref_freq_to_pix_freq,
				dlg_regs->vratio_prefetch, dlg_regs->vratio_prefetch_c, dlg_regs->refcyc_per_pte_group_vblank_l,
				dlg_regs->refcyc_per_pte_group_vblank_c, dlg_regs->refcyc_per_meta_chunk_vblank_l,
				dlg_regs->refcyc_per_meta_chunk_vblank_c, dlg_regs->refcyc_per_pte_group_flip_l,
				dlg_regs->refcyc_per_pte_group_flip_c, dlg_regs->refcyc_per_meta_chunk_flip_l,
				dlg_regs->refcyc_per_meta_chunk_flip_c, dlg_regs->dst_y_per_pte_row_nom_l,
				dlg_regs->dst_y_per_pte_row_nom_c, dlg_regs->refcyc_per_pte_group_nom_l,
				dlg_regs->refcyc_per_pte_group_nom_c, dlg_regs->dst_y_per_meta_row_nom_l,
				dlg_regs->dst_y_per_meta_row_nom_c, dlg_regs->refcyc_per_meta_chunk_nom_l,
				dlg_regs->refcyc_per_meta_chunk_nom_c, dlg_regs->refcyc_per_line_delivery_pre_l,
				dlg_regs->refcyc_per_line_delivery_pre_c, dlg_regs->refcyc_per_line_delivery_l,
				dlg_regs->refcyc_per_line_delivery_c, dlg_regs->chunk_hdl_adjust_cur0, dlg_regs->dst_y_offset_cur1,
				dlg_regs->chunk_hdl_adjust_cur1, dlg_regs->vready_after_vcount0, dlg_regs->dst_y_delta_drq_limit,
				dlg_regs->xfc_reg_transfer_delay, dlg_regs->xfc_reg_precharge_delay,
				dlg_regs->xfc_reg_remote_surface_flip_latency);
	}

	DTN_INFO("========TTU========\n");
	DTN_INFO("HUBP:  qos_ll_wm  qos_lh_wm  mn_ttu_vb  qos_l_flp  rc_rd_p_l  rc_rd_l    rc_rd_p_c"
			"  rc_rd_c    rc_rd_c0   rc_rd_pc0  rc_rd_c1   rc_rd_pc1  qos_lf_l   qos_rds_l"
			"  qos_lf_c   qos_rds_c  qos_lf_c0  qos_rds_c0 qos_lf_c1  qos_rds_c1\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs = &s->ttu_attr;

		if (!s->blank_en)
			DTN_INFO("[%2d]:  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh  %8xh\n",
				pool->hubps[i]->inst, ttu_regs->qos_level_low_wm, ttu_regs->qos_level_high_wm, ttu_regs->min_ttu_vblank,
				ttu_regs->qos_level_flip, ttu_regs->refcyc_per_req_delivery_pre_l, ttu_regs->refcyc_per_req_delivery_l,
				ttu_regs->refcyc_per_req_delivery_pre_c, ttu_regs->refcyc_per_req_delivery_c, ttu_regs->refcyc_per_req_delivery_cur0,
				ttu_regs->refcyc_per_req_delivery_pre_cur0, ttu_regs->refcyc_per_req_delivery_cur1,
				ttu_regs->refcyc_per_req_delivery_pre_cur1, ttu_regs->qos_level_fixed_l, ttu_regs->qos_ramp_disable_l,
				ttu_regs->qos_level_fixed_c, ttu_regs->qos_ramp_disable_c, ttu_regs->qos_level_fixed_cur0,
				ttu_regs->qos_ramp_disable_cur0, ttu_regs->qos_level_fixed_cur1, ttu_regs->qos_ramp_disable_cur1);
	}
	DTN_INFO("\n");
}

void dcn10_log_hw_state(struct dc *dc,
	struct dc_log_buffer_ctx *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	int i;

	DTN_INFO_BEGIN();

	dcn10_log_hubbub_state(dc, log_ctx);

	dcn10_log_hubp_states(dc, log_ctx);

	DTN_INFO("DPP:    IGAM format  IGAM mode    DGAM mode    RGAM mode"
			"  GAMUT mode  C11 C12   C13 C14   C21 C22   C23 C24   "
			"C31 C32   C33 C34\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct dpp *dpp = pool->dpps[i];
		struct dcn_dpp_state s = {0};

		dpp->funcs->dpp_read_state(dpp, &s);

		if (!s.is_enabled)
			continue;

		DTN_INFO("[%2d]:  %11xh  %-11s  %-11s  %-11s"
				"%8x    %08xh %08xh %08xh %08xh %08xh %08xh",
				dpp->inst,
				s.igam_input_format,
				(s.igam_lut_mode == 0) ? "BypassFixed" :
					((s.igam_lut_mode == 1) ? "BypassFloat" :
					((s.igam_lut_mode == 2) ? "RAM" :
					((s.igam_lut_mode == 3) ? "RAM" :
								 "Unknown"))),
				(s.dgam_lut_mode == 0) ? "Bypass" :
					((s.dgam_lut_mode == 1) ? "sRGB" :
					((s.dgam_lut_mode == 2) ? "Ycc" :
					((s.dgam_lut_mode == 3) ? "RAM" :
					((s.dgam_lut_mode == 4) ? "RAM" :
								 "Unknown")))),
				(s.rgam_lut_mode == 0) ? "Bypass" :
					((s.rgam_lut_mode == 1) ? "sRGB" :
					((s.rgam_lut_mode == 2) ? "Ycc" :
					((s.rgam_lut_mode == 3) ? "RAM" :
					((s.rgam_lut_mode == 4) ? "RAM" :
								 "Unknown")))),
				s.gamut_remap_mode,
				s.gamut_remap_c11_c12,
				s.gamut_remap_c13_c14,
				s.gamut_remap_c21_c22,
				s.gamut_remap_c23_c24,
				s.gamut_remap_c31_c32,
				s.gamut_remap_c33_c34);
		DTN_INFO("\n");
	}
	DTN_INFO("\n");

	DTN_INFO("MPCC:  OPP  DPP  MPCCBOT  MODE  ALPHA_MODE  PREMULT  OVERLAP_ONLY  IDLE\n");
	for (i = 0; i < pool->pipe_count; i++) {
		struct mpcc_state s = {0};

		pool->mpc->funcs->read_mpcc_state(pool->mpc, i, &s);
		if (s.opp_id != 0xf)
			DTN_INFO("[%2d]:  %2xh  %2xh  %6xh  %4d  %10d  %7d  %12d  %4d\n",
				i, s.opp_id, s.dpp_id, s.bot_mpcc_id,
				s.mode, s.alpha_mode, s.pre_multiplied_alpha, s.overlap_only,
				s.idle);
	}
	DTN_INFO("\n");

	DTN_INFO("OTG:  v_bs  v_be  v_ss  v_se  vpol  vmax  vmin  vmax_sel  vmin_sel  h_bs  h_be  h_ss  h_se  hpol  htot  vtot  underflow blank_en\n");

	for (i = 0; i < pool->timing_generator_count; i++) {
		struct timing_generator *tg = pool->timing_generators[i];
		struct dcn_otg_state s = {0};
		/* Read shared OTG state registers for all DCNx */
		optc1_read_otg_state(DCN10TG_FROM_TG(tg), &s);

		/*
		 * For DCN2 and greater, a register on the OPP is used to
		 * determine if the CRTC is blanked instead of the OTG. So use
		 * dpg_is_blanked() if exists, otherwise fallback on otg.
		 *
		 * TODO: Implement DCN-specific read_otg_state hooks.
		 */
		if (pool->opps[i]->funcs->dpg_is_blanked)
			s.blank_enabled = pool->opps[i]->funcs->dpg_is_blanked(pool->opps[i]);
		else
			s.blank_enabled = tg->funcs->is_blanked(tg);

		//only print if OTG master is enabled
		if ((s.otg_enabled & 1) == 0)
			continue;

		DTN_INFO("[%d]: %5d %5d %5d %5d %5d %5d %5d %9d %9d %5d %5d %5d %5d %5d %5d %5d  %9d %8d\n",
				tg->inst,
				s.v_blank_start,
				s.v_blank_end,
				s.v_sync_a_start,
				s.v_sync_a_end,
				s.v_sync_a_pol,
				s.v_total_max,
				s.v_total_min,
				s.v_total_max_sel,
				s.v_total_min_sel,
				s.h_blank_start,
				s.h_blank_end,
				s.h_sync_a_start,
				s.h_sync_a_end,
				s.h_sync_a_pol,
				s.h_total,
				s.v_total,
				s.underflow_occurred_status,
				s.blank_enabled);

		// Clear underflow for debug purposes
		// We want to keep underflow sticky bit on for the longevity tests outside of test environment.
		// This function is called only from Windows or Diags test environment, hence it's safe to clear
		// it from here without affecting the original intent.
		tg->funcs->clear_optc_underflow(tg);
	}
	DTN_INFO("\n");

	// dcn_dsc_state struct field bytes_per_pixel was renamed to bits_per_pixel
	// TODO: Update golden log header to reflect this name change
	DTN_INFO("DSC: CLOCK_EN  SLICE_WIDTH  Bytes_pp\n");
	for (i = 0; i < pool->res_cap->num_dsc; i++) {
		struct display_stream_compressor *dsc = pool->dscs[i];
		struct dcn_dsc_state s = {0};

		dsc->funcs->dsc_read_state(dsc, &s);
		DTN_INFO("[%d]: %-9d %-12d %-10d\n",
		dsc->inst,
			s.dsc_clock_en,
			s.dsc_slice_width,
			s.dsc_bits_per_pixel);
		DTN_INFO("\n");
	}
	DTN_INFO("\n");

	DTN_INFO("S_ENC: DSC_MODE  SEC_GSP7_LINE_NUM"
			"  VBID6_LINE_REFERENCE  VBID6_LINE_NUM  SEC_GSP7_ENABLE  SEC_STREAM_ENABLE\n");
	for (i = 0; i < pool->stream_enc_count; i++) {
		struct stream_encoder *enc = pool->stream_enc[i];
		struct enc_state s = {0};

		if (enc->funcs->enc_read_state) {
			enc->funcs->enc_read_state(enc, &s);
			DTN_INFO("[%-3d]: %-9d %-18d %-21d %-15d %-16d %-17d\n",
				enc->id,
				s.dsc_mode,
				s.sec_gsp_pps_line_num,
				s.vbid6_line_reference,
				s.vbid6_line_num,
				s.sec_gsp_pps_enable,
				s.sec_stream_enable);
			DTN_INFO("\n");
		}
	}
	DTN_INFO("\n");

	DTN_INFO("L_ENC: DPHY_FEC_EN  DPHY_FEC_READY_SHADOW  DPHY_FEC_ACTIVE_STATUS  DP_LINK_TRAINING_COMPLETE\n");
	for (i = 0; i < dc->link_count; i++) {
		struct link_encoder *lenc = dc->links[i]->link_enc;

		struct link_enc_state s = {0};

		if (lenc->funcs->read_state) {
			lenc->funcs->read_state(lenc, &s);
			DTN_INFO("[%-3d]: %-12d %-22d %-22d %-25d\n",
				i,
				s.dphy_fec_en,
				s.dphy_fec_ready_shadow,
				s.dphy_fec_active_status,
				s.dp_link_training_complete);
			DTN_INFO("\n");
		}
	}
	DTN_INFO("\n");

	DTN_INFO("\nCALCULATED Clocks: dcfclk_khz:%d  dcfclk_deep_sleep_khz:%d  dispclk_khz:%d\n"
		"dppclk_khz:%d  max_supported_dppclk_khz:%d  fclk_khz:%d  socclk_khz:%d\n\n",
			dc->current_state->bw_ctx.bw.dcn.clk.dcfclk_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.dispclk_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.dppclk_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.fclk_khz,
			dc->current_state->bw_ctx.bw.dcn.clk.socclk_khz);

	log_mpc_crc(dc, log_ctx);

	DTN_INFO_END();
}

bool dcn10_did_underflow_occur(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	if (tg->funcs->is_optc_underflow_occurred(tg)) {
		tg->funcs->clear_optc_underflow(tg);
		return true;
	}

	if (hubp->funcs->hubp_get_underflow_status(hubp)) {
		hubp->funcs->hubp_clear_underflow(hubp);
		return true;
	}
	return false;
}

void dcn10_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = true; /* disable power gating */

	if (enable)
		force_on = false;

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

void dcn10_disable_vga(
	struct dce_hwseq *hws)
{
	unsigned int in_vga1_mode = 0;
	unsigned int in_vga2_mode = 0;
	unsigned int in_vga3_mode = 0;
	unsigned int in_vga4_mode = 0;

	REG_GET(D1VGA_CONTROL, D1VGA_MODE_ENABLE, &in_vga1_mode);
	REG_GET(D2VGA_CONTROL, D2VGA_MODE_ENABLE, &in_vga2_mode);
	REG_GET(D3VGA_CONTROL, D3VGA_MODE_ENABLE, &in_vga3_mode);
	REG_GET(D4VGA_CONTROL, D4VGA_MODE_ENABLE, &in_vga4_mode);

	if (in_vga1_mode == 0 && in_vga2_mode == 0 &&
			in_vga3_mode == 0 && in_vga4_mode == 0)
		return;

	REG_WRITE(D1VGA_CONTROL, 0);
	REG_WRITE(D2VGA_CONTROL, 0);
	REG_WRITE(D3VGA_CONTROL, 0);
	REG_WRITE(D4VGA_CONTROL, 0);

	/* HW Engineer's Notes:
	 *  During switch from vga->extended, if we set the VGA_TEST_ENABLE and
	 *  then hit the VGA_TEST_RENDER_START, then the DCHUBP timing gets updated correctly.
	 *
	 *  Then vBIOS will have it poll for the VGA_TEST_RENDER_DONE and unset
	 *  VGA_TEST_ENABLE, to leave it in the same state as before.
	 */
	REG_UPDATE(VGA_TEST_CONTROL, VGA_TEST_ENABLE, 1);
	REG_UPDATE(VGA_TEST_CONTROL, VGA_TEST_RENDER_START, 1);
}

void dcn10_dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_dpp_power_gate)
		return;
	if (REG(DOMAIN1_PG_CONFIG) == 0)
		return;

	switch (dpp_inst) {
	case 0: /* DPP0 */
		REG_UPDATE(DOMAIN1_PG_CONFIG,
				DOMAIN1_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN1_PG_STATUS,
				DOMAIN1_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DPP1 */
		REG_UPDATE(DOMAIN3_PG_CONFIG,
				DOMAIN3_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN3_PG_STATUS,
				DOMAIN3_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DPP2 */
		REG_UPDATE(DOMAIN5_PG_CONFIG,
				DOMAIN5_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN5_PG_STATUS,
				DOMAIN5_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DPP3 */
		REG_UPDATE(DOMAIN7_PG_CONFIG,
				DOMAIN7_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN7_PG_STATUS,
				DOMAIN7_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void dcn10_hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;
	if (REG(DOMAIN0_PG_CONFIG) == 0)
		return;

	switch (hubp_inst) {
	case 0: /* DCHUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG,
				DOMAIN0_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN0_PG_STATUS,
				DOMAIN0_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DCHUBP1 */
		REG_UPDATE(DOMAIN2_PG_CONFIG,
				DOMAIN2_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN2_PG_STATUS,
				DOMAIN2_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DCHUBP2 */
		REG_UPDATE(DOMAIN4_PG_CONFIG,
				DOMAIN4_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN4_PG_STATUS,
				DOMAIN4_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DCHUBP3 */
		REG_UPDATE(DOMAIN6_PG_CONFIG,
				DOMAIN6_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN6_PG_STATUS,
				DOMAIN6_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
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
	DC_LOGGER_INIT(hws->ctx->logger);
	if (REG(DC_IP_REQUEST_CNTL)) {
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 1);

		if (hws->funcs.dpp_pg_control)
			hws->funcs.dpp_pg_control(hws, plane_id, true);

		if (hws->funcs.hubp_pg_control)
			hws->funcs.hubp_pg_control(hws, plane_id, true);

		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 0);
		DC_LOG_DEBUG(
				"Un-gated front end for pipe %d\n", plane_id);
	}
}

static void undo_DEGVIDCN10_253_wa(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = dc->res_pool->hubps[0];

	if (!hws->wa_state.DEGVIDCN10_253_applied)
		return;

	hubp->funcs->set_blank(hubp, true);

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);

	hws->funcs.hubp_pg_control(hws, 0, false);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);

	hws->wa_state.DEGVIDCN10_253_applied = false;
}

static void apply_DEGVIDCN10_253_wa(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = dc->res_pool->hubps[0];
	int i;

	if (dc->debug.disable_stutter)
		return;

	if (!hws->wa.DEGVIDCN10_253)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!dc->res_pool->hubps[i]->power_gated)
			return;
	}

	/* all pipe power gated, apply work around to enable stutter. */

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);

	hws->funcs.hubp_pg_control(hws, 0, true);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);

	hubp->funcs->set_hubp_blank_en(hubp, false);
	hws->wa_state.DEGVIDCN10_253_applied = true;
}

void dcn10_bios_golden_init(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *bp = dc->ctx->dc_bios;
	int i;
	bool allow_self_fresh_force_enable = true;

	if (hws->funcs.s0i3_golden_init_wa && hws->funcs.s0i3_golden_init_wa(dc))
		return;

	if (dc->res_pool->hubbub->funcs->is_allow_self_refresh_enabled)
		allow_self_fresh_force_enable =
				dc->res_pool->hubbub->funcs->is_allow_self_refresh_enabled(dc->res_pool->hubbub);


	/* WA for making DF sleep when idle after resume from S0i3.
	 * DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE is set to 1 by
	 * command table, if DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE = 0
	 * before calling command table and it changed to 1 after,
	 * it should be set back to 0.
	 */

	/* initialize dcn global */
	bp->funcs->enable_disp_power_gating(bp,
			CONTROLLER_ID_D0, ASIC_PIPE_INIT);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		/* initialize dcn per pipe */
		bp->funcs->enable_disp_power_gating(bp,
				CONTROLLER_ID_D0 + i, ASIC_PIPE_DISABLE);
	}

	if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
		if (allow_self_fresh_force_enable == false &&
				dc->res_pool->hubbub->funcs->is_allow_self_refresh_enabled(dc->res_pool->hubbub))
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
										!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);

}

static void false_optc_underflow_wa(
		struct dc *dc,
		const struct dc_stream_state *stream,
		struct timing_generator *tg)
{
	int i;
	bool underflow;

	if (!dc->hwseq->wa.false_optc_underflow)
		return;

	underflow = tg->funcs->is_optc_underflow_occurred(tg);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		if (old_pipe_ctx->stream != stream)
			continue;

		dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, old_pipe_ctx);
	}

	if (tg->funcs->set_blank_data_double_buffer)
		tg->funcs->set_blank_data_double_buffer(tg, true);

	if (tg->funcs->is_optc_underflow_occurred(tg) && !underflow)
		tg->funcs->clear_optc_underflow(tg);
}

enum dc_status dcn10_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};

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
	pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, true);

	if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			&pipe_ctx->pll_settings)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	pipe_ctx->stream_res.tg->funcs->program_timing(
			pipe_ctx->stream_res.tg,
			&stream->timing,
			pipe_ctx->pipe_dlg_param.vready_offset,
			pipe_ctx->pipe_dlg_param.vstartup_start,
			pipe_ctx->pipe_dlg_param.vupdate_offset,
			pipe_ctx->pipe_dlg_param.vupdate_width,
			pipe_ctx->stream->signal,
			true);

#if 0 /* move to after enable_crtc */
	/* TODO: OPP FMT, ABM. etc. should be done here. */
	/* or FPGA now. instance 0 only. TODO: move to opp.c */

	inst_offset = reg_offsets[pipe_ctx->stream_res.tg->inst].fmt;

	pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
				pipe_ctx->stream_res.opp,
				&stream->bit_depth_params,
				&stream->clamping);
#endif
	/* program otg blank color */
	color_space = stream->output_color_space;
	color_space_to_black_color(dc, color_space, &black_color);

	/*
	 * The way 420 is packed, 2 channels carry Y component, 1 channel
	 * alternate between Cb and Cr, so both channels need the pixel
	 * value for Y
	 */
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
		black_color.color_r_cr = black_color.color_g_y;

	if (pipe_ctx->stream_res.tg->funcs->set_blank_color)
		pipe_ctx->stream_res.tg->funcs->set_blank_color(
				pipe_ctx->stream_res.tg,
				&black_color);

	if (pipe_ctx->stream_res.tg->funcs->is_blanked &&
			!pipe_ctx->stream_res.tg->funcs->is_blanked(pipe_ctx->stream_res.tg)) {
		pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, true);
		hwss_wait_for_blank_complete(pipe_ctx->stream_res.tg);
		false_optc_underflow_wa(dc, pipe_ctx->stream, pipe_ctx->stream_res.tg);
	}

	/* VTG is  within DCHUB command block. DCFCLK is always on */
	if (false == pipe_ctx->stream_res.tg->funcs->enable_crtc(pipe_ctx->stream_res.tg)) {
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

static void dcn10_reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	int i;
	struct dc_link *link;
	DC_LOGGER_INIT(dc->ctx->logger);
	if (pipe_ctx->stream_res.stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		link = pipe_ctx->stream->link;
		/* DPMS may already disable or */
		/* dpms_off status is incorrect due to fastboot
		 * feature. When system resume from S4 with second
		 * screen only, the dpms_off would be true but
		 * VBIOS lit up eDP, so check link status too.
		 */
		if (!pipe_ctx->stream->dpms_off || link->link_status.link_active)
			core_link_disable_stream(pipe_ctx);
		else if (pipe_ctx->stream_res.audio)
			dc->hwss.disable_audio_stream(pipe_ctx);

		if (pipe_ctx->stream_res.audio) {
			/*disable az_endpoint*/
			pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

			/*free audio*/
			if (dc->caps.dynamic_audio == true) {
				/*we have to dynamic arbitrate the audio endpoints*/
				/*we free the resource, need reset is_audio_acquired*/
				update_audio_usage(&dc->current_state->res_ctx, dc->res_pool,
						pipe_ctx->stream_res.audio, false);
				pipe_ctx->stream_res.audio = NULL;
			}
		}
	}

	/* by upper caller loop, parent pipe: pipe0, will be reset last.
	 * back end share by all pipes and will be disable only when disable
	 * parent pipe.
	 */
	if (pipe_ctx->top_pipe == NULL) {

		if (pipe_ctx->stream_res.abm)
			dc->hwss.set_abm_immediate_disable(pipe_ctx);

		pipe_ctx->stream_res.tg->funcs->disable_crtc(pipe_ctx->stream_res.tg);

		pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, false);
		if (pipe_ctx->stream_res.tg->funcs->set_drr)
			pipe_ctx->stream_res.tg->funcs->set_drr(
					pipe_ctx->stream_res.tg, NULL);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (&dc->current_state->res_ctx.pipe_ctx[i] == pipe_ctx)
			break;

	if (i == dc->res_pool->pipe_count)
		return;

	pipe_ctx->stream = NULL;
	DC_LOG_DEBUG("Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->stream_res.tg->inst);
}

static bool dcn10_hw_wa_force_recovery(struct dc *dc)
{
	struct hubp *hubp ;
	unsigned int i;
	bool need_recover = true;

	if (!dc->debug.recovery_enabled)
		return false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
			&dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx != NULL) {
			hubp = pipe_ctx->plane_res.hubp;
			if (hubp != NULL && hubp->funcs->hubp_get_underflow_status) {
				if (hubp->funcs->hubp_get_underflow_status(hubp) != 0) {
					/* one pipe underflow, we will reset all the pipes*/
					need_recover = true;
				}
			}
		}
	}
	if (!need_recover)
		return false;
	/*
	DCHUBP_CNTL:HUBP_BLANK_EN=1
	DCHUBBUB_SOFT_RESET:DCHUBBUB_GLOBAL_SOFT_RESET=1
	DCHUBP_CNTL:HUBP_DISABLE=1
	DCHUBP_CNTL:HUBP_DISABLE=0
	DCHUBBUB_SOFT_RESET:DCHUBBUB_GLOBAL_SOFT_RESET=0
	DCSURF_PRIMARY_SURFACE_ADDRESS
	DCHUBP_CNTL:HUBP_BLANK_EN=0
	*/

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
			&dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx != NULL) {
			hubp = pipe_ctx->plane_res.hubp;
			/*DCHUBP_CNTL:HUBP_BLANK_EN=1*/
			if (hubp != NULL && hubp->funcs->set_hubp_blank_en)
				hubp->funcs->set_hubp_blank_en(hubp, true);
		}
	}
	/*DCHUBBUB_SOFT_RESET:DCHUBBUB_GLOBAL_SOFT_RESET=1*/
	hubbub1_soft_reset(dc->res_pool->hubbub, true);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
			&dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx != NULL) {
			hubp = pipe_ctx->plane_res.hubp;
			/*DCHUBP_CNTL:HUBP_DISABLE=1*/
			if (hubp != NULL && hubp->funcs->hubp_disable_control)
				hubp->funcs->hubp_disable_control(hubp, true);
		}
	}
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
			&dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx != NULL) {
			hubp = pipe_ctx->plane_res.hubp;
			/*DCHUBP_CNTL:HUBP_DISABLE=0*/
			if (hubp != NULL && hubp->funcs->hubp_disable_control)
				hubp->funcs->hubp_disable_control(hubp, true);
		}
	}
	/*DCHUBBUB_SOFT_RESET:DCHUBBUB_GLOBAL_SOFT_RESET=0*/
	hubbub1_soft_reset(dc->res_pool->hubbub, false);
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
			&dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx != NULL) {
			hubp = pipe_ctx->plane_res.hubp;
			/*DCHUBP_CNTL:HUBP_BLANK_EN=0*/
			if (hubp != NULL && hubp->funcs->set_hubp_blank_en)
				hubp->funcs->set_hubp_blank_en(hubp, true);
		}
	}
	return true;

}


void dcn10_verify_allow_pstate_change_high(struct dc *dc)
{
	static bool should_log_hw_state; /* prevent hw state log by default */

	if (!hubbub1_verify_allow_pstate_change_high(dc->res_pool->hubbub)) {
		if (should_log_hw_state) {
			dcn10_log_hw_state(dc, NULL);
		}
		BREAK_TO_DEBUGGER();
		if (dcn10_hw_wa_force_recovery(dc)) {
		/*check again*/
			if (!hubbub1_verify_allow_pstate_change_high(dc->res_pool->hubbub))
				BREAK_TO_DEBUGGER();
		}
	}
}

/* trigger HW to start disconnect plane from stream on the next vsync */
void dcn10_plane_atomic_disconnect(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	int dpp_id = pipe_ctx->plane_res.dpp->inst;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params;
	struct mpcc *mpcc_to_remove = NULL;
	struct output_pixel_processor *opp = pipe_ctx->stream_res.opp;

	mpc_tree_params = &(opp->mpc_tree_params);
	mpcc_to_remove = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, dpp_id);

	/*Already reset*/
	if (mpcc_to_remove == NULL)
		return;

	mpc->funcs->remove_mpcc(mpc, mpc_tree_params, mpcc_to_remove);
	if (opp != NULL)
		opp->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;

	dc->optimized_required = true;

	if (hubp->funcs->hubp_disconnect)
		hubp->funcs->hubp_disconnect(hubp);

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

void dcn10_plane_atomic_power_down(struct dc *dc,
		struct dpp *dpp,
		struct hubp *hubp)
{
	struct dce_hwseq *hws = dc->hwseq;
	DC_LOGGER_INIT(dc->ctx->logger);

	if (REG(DC_IP_REQUEST_CNTL)) {
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 1);

		if (hws->funcs.dpp_pg_control)
			hws->funcs.dpp_pg_control(hws, dpp->inst, false);

		if (hws->funcs.hubp_pg_control)
			hws->funcs.hubp_pg_control(hws, hubp->inst, false);

		dpp->funcs->dpp_reset(dpp);
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 0);
		DC_LOG_DEBUG(
				"Power gated front end %d\n", hubp->inst);
	}
}

/* disable HW used by plane.
 * note:  cannot disable until disconnect is complete
 */
void dcn10_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	int opp_id = hubp->opp_id;

	dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, pipe_ctx);

	hubp->funcs->hubp_clk_cntl(hubp, false);

	dpp->funcs->dpp_dppclk_control(dpp, false, false);

	if (opp_id != 0xf && pipe_ctx->stream_res.opp->mpc_tree_params.opp_list == NULL)
		pipe_ctx->stream_res.opp->funcs->opp_pipe_clock_control(
				pipe_ctx->stream_res.opp,
				false);

	hubp->power_gated = true;
	dc->optimized_required = false; /* We're powering off, no need to optimize */

	hws->funcs.plane_atomic_power_down(dc,
			pipe_ctx->plane_res.dpp,
			pipe_ctx->plane_res.hubp);

	pipe_ctx->stream = NULL;
	memset(&pipe_ctx->stream_res, 0, sizeof(pipe_ctx->stream_res));
	memset(&pipe_ctx->plane_res, 0, sizeof(pipe_ctx->plane_res));
	pipe_ctx->top_pipe = NULL;
	pipe_ctx->bottom_pipe = NULL;
	pipe_ctx->plane_state = NULL;
}

void dcn10_disable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	DC_LOGGER_INIT(dc->ctx->logger);

	if (!pipe_ctx->plane_res.hubp || pipe_ctx->plane_res.hubp->power_gated)
		return;

	hws->funcs.plane_atomic_disable(dc, pipe_ctx);

	apply_DEGVIDCN10_253_wa(dc);

	DC_LOG_DC("Power down front end %d\n",
					pipe_ctx->pipe_idx);
}

void dcn10_init_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;
	bool can_apply_seamless_boot = false;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->apply_seamless_boot_optimization) {
			can_apply_seamless_boot = true;
			break;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		/* Blank controller using driver code instead of
		 * command table.
		 */
		if (tg->funcs->is_tg_enabled(tg)) {
			if (hws->funcs.init_blank != NULL) {
				hws->funcs.init_blank(dc, tg);
				tg->funcs->lock(tg);
			} else {
				tg->funcs->lock(tg);
				tg->funcs->set_blank(tg, true);
				hwss_wait_for_blank_complete(tg);
			}
		}
	}

	/* num_opp will be equal to number of mpcc */
	for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* Cannot reset the MPC mux if seamless boot */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		dc->res_pool->mpc->funcs->mpc_init_single_inst(
				dc->res_pool->mpc, i);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct hubp *hubp = dc->res_pool->hubps[i];
		struct dpp *dpp = dc->res_pool->dpps[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (can_apply_seamless_boot &&
			pipe_ctx->stream != NULL &&
			pipe_ctx->stream_res.tg->funcs->is_tg_enabled(
				pipe_ctx->stream_res.tg)) {
			// Enable double buffering for OTG_BLANK no matter if
			// seamless boot is enabled or not to suppress global sync
			// signals when OTG blanked. This is to prevent pipe from
			// requesting data while in PSR.
			tg->funcs->tg_init(tg);
			continue;
		}

		/* Disable on the current state so the new one isn't cleared. */
		pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		dpp->funcs->dpp_reset(dpp);

		pipe_ctx->stream_res.tg = tg;
		pipe_ctx->pipe_idx = i;

		pipe_ctx->plane_res.hubp = hubp;
		pipe_ctx->plane_res.dpp = dpp;
		pipe_ctx->plane_res.mpcc_inst = dpp->inst;
		hubp->mpcc_id = dpp->inst;
		hubp->opp_id = OPP_ID_INVALID;
		hubp->power_gated = false;

		dc->res_pool->opps[i]->mpc_tree_params.opp_id = dc->res_pool->opps[i]->inst;
		dc->res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		dc->res_pool->opps[i]->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;
		pipe_ctx->stream_res.opp = dc->res_pool->opps[i];

		hws->funcs.plane_atomic_disconnect(dc, pipe_ctx);

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->unlock(tg);

		dc->hwss.disable_plane(dc, pipe_ctx);

		pipe_ctx->stream_res.tg = NULL;
		pipe_ctx->plane_res.hubp = NULL;

		tg->funcs->tg_init(tg);
	}
}

void dcn10_init_hw(struct dc *dc)
{
	int i, j;
	struct abm *abm = dc->res_pool->abm;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	bool   is_optimized_init_done = false;

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	// Initialize the dccg
	if (dc->res_pool->dccg && dc->res_pool->dccg->funcs->dccg_init)
		dc->res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {

		REG_WRITE(REFCLK_CNTL, 0);
		REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
		REG_WRITE(DIO_MEM_PWR_CTRL, 0);

		if (!dc->debug.disable_clock_gate) {
			/* enable all DCN clock gating */
			REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

			REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

			REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
		}

		//Enable ability to power gate / don't force power on permanently
		if (hws->funcs.enable_power_gating_plane)
			hws->funcs.enable_power_gating_plane(hws, true);

		return;
	}

	if (!dcb->funcs->is_accelerated_mode(dcb))
		hws->funcs.disable_vga(dc->hwseq);

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
				// Not all ASICs have DCCG sw component
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

		if (!is_optimized_init_done)
			link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc))
			link->link_status.link_active = true;
	}

	/* Power gate DSCs */
	if (!is_optimized_init_done) {
		for (i = 0; i < res_pool->res_cap->num_dsc; i++)
			if (hws->funcs.dsc_pg_control != NULL)
				hws->funcs.dsc_pg_control(hws, res_pool->dscs[i]->inst, false);
	}

	/* we want to turn off all dp displays before doing detection */
	if (dc->config.power_down_display_on_boot) {
		uint8_t dpcd_power_state = '\0';
		enum dc_status status = DC_ERROR_UNEXPECTED;

		for (i = 0; i < dc->link_count; i++) {
			if (dc->links[i]->connector_signal != SIGNAL_TYPE_DISPLAY_PORT)
				continue;

			/*
			 * If any of the displays are lit up turn them off.
			 * The reason is that some MST hubs cannot be turned off
			 * completely until we tell them to do so.
			 * If not turned off, then displays connected to MST hub
			 * won't light up.
			 */
			status = core_link_read_dpcd(dc->links[i], DP_SET_POWER,
							&dpcd_power_state, sizeof(dpcd_power_state));
			if (status == DC_OK && dpcd_power_state == DP_POWER_STATE_D0) {
				/* blank dp stream before power off receiver*/
				if (dc->links[i]->link_enc->funcs->get_dig_frontend) {
					unsigned int fe = dc->links[i]->link_enc->funcs->get_dig_frontend(dc->links[i]->link_enc);

					for (j = 0; j < dc->res_pool->stream_enc_count; j++) {
						if (fe == dc->res_pool->stream_enc[j]->id) {
							dc->res_pool->stream_enc[j]->funcs->dp_blank(
										dc->res_pool->stream_enc[j]);
							break;
						}
					}
				}
				dp_receiver_power_ctrl(dc->links[i], false);
			}
		}
	}

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || dc->config.power_down_display_on_boot) {
		if (!is_optimized_init_done) {
			hws->funcs.init_pipes(dc, dc->current_state);
			if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
				dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
						!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);
		}
	}

	if (!is_optimized_init_done) {

		for (i = 0; i < res_pool->audio_count; i++) {
			struct audio *audio = res_pool->audios[i];

			audio->funcs->hw_init(audio);
		}

		for (i = 0; i < dc->link_count; i++) {
			struct dc_link *link = dc->links[i];

			if (link->panel_cntl)
				backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
		}

		if (abm != NULL)
			abm->funcs->abm_init(abm, backlight);

		if (dmcu != NULL && !dmcu->auto_load_dmcu)
			dmcu->funcs->dmcu_init(dmcu);
	}

	if (abm != NULL && dmcu != NULL)
		abm->dmcu_is_running = dmcu->funcs->is_dmcu_initialized(dmcu);

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	if (!is_optimized_init_done)
		REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}
	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	if (dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

#ifdef CONFIG_DRM_AMD_DC_DCN3_0
	if (dc->clk_mgr->funcs->set_hard_max_memclk)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);
#endif

}

/* In headless boot cases, DIG may be turned
 * on which causes HW/SW discrepancies.
 * To avoid this, power down hardware on boot
 * if DIG is turned on and seamless boot not enabled
 */
void dcn10_power_down_on_boot(struct dc *dc)
{
	int i = 0;
	struct dc_link *edp_link;

	if (!dc->config.power_down_display_on_boot)
		return;

	edp_link = get_edp_link(dc);
	if (edp_link &&
			edp_link->link_enc->funcs->is_dig_enabled &&
			edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
			dc->hwseq->funcs.edp_backlight_control &&
			dc->hwss.power_down &&
			dc->hwss.edp_power_control) {
		dc->hwseq->funcs.edp_backlight_control(edp_link, false);
		dc->hwss.power_down(dc);
		dc->hwss.edp_power_control(edp_link, false);
	} else {
		for (i = 0; i < dc->link_count; i++) {
			struct dc_link *link = dc->links[i];

			if (link->link_enc->funcs->is_dig_enabled &&
					link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
					dc->hwss.power_down) {
				dc->hwss.power_down(dc);
				break;
			}

		}
	}

	/*
	 * Call update_clocks with empty context
	 * to send DISPLAY_OFF
	 * Otherwise DISPLAY_OFF may not be asserted
	 */
	if (dc->clk_mgr->funcs->set_low_power_state)
		dc->clk_mgr->funcs->set_low_power_state(dc->clk_mgr);
}

void dcn10_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;

	/* Reset Back End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (pipe_ctx_old->top_pipe)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			dcn10_reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);
			if (hws->funcs.enable_stream_gating)
				hws->funcs.enable_stream_gating(dc, pipe_ctx);
			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}
}

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
		}
	}
	return false;
}

void dcn10_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	bool addr_patched = false;
	PHYSICAL_ADDRESS_LOC addr;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (plane_state == NULL)
		return;

	addr_patched = patch_address_for_sbs_tb_stereo(pipe_ctx, &addr);

	pipe_ctx->plane_res.hubp->funcs->hubp_program_surface_flip_and_addr(
			pipe_ctx->plane_res.hubp,
			&plane_state->address,
			plane_state->flip_immediate);

	plane_state->status.requested_address = plane_state->address;

	if (plane_state->flip_immediate)
		plane_state->status.current_address = plane_state->address;

	if (addr_patched)
		pipe_ctx->plane_state->address.grph_stereo.left_addr = addr;
}

bool dcn10_set_input_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	const struct dc_transfer_func *tf = NULL;
	bool result = true;

	if (dpp_base == NULL)
		return false;

	if (plane_state->in_transfer_func)
		tf = plane_state->in_transfer_func;

	if (plane_state->gamma_correction &&
		!dpp_base->ctx->dc->debug.always_use_regamma
		&& !plane_state->gamma_correction->is_identity
			&& dce_use_lut(plane_state->format))
		dpp_base->funcs->dpp_program_input_lut(dpp_base, plane_state->gamma_correction);

	if (tf == NULL)
		dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_BYPASS);
	else if (tf->type == TF_TYPE_PREDEFINED) {
		switch (tf->tf) {
		case TRANSFER_FUNCTION_SRGB:
			dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
			dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_USER_PWL);
			cm_helper_translate_curve_to_degamma_hw_format(tf, &dpp_base->degamma_params);
			dpp_base->funcs->dpp_program_degamma_pwl(dpp_base, &dpp_base->degamma_params);
			result = true;
			break;
		default:
			result = false;
			break;
		}
	} else if (tf->type == TF_TYPE_BYPASS) {
		dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_BYPASS);
	} else {
		cm_helper_translate_curve_to_degamma_hw_format(tf,
					&dpp_base->degamma_params);
		dpp_base->funcs->dpp_program_degamma_pwl(dpp_base,
				&dpp_base->degamma_params);
		result = true;
	}

	return result;
}

#define MAX_NUM_HW_POINTS 0x200

static void log_tf(struct dc_context *ctx,
				struct dc_transfer_func *tf, uint32_t hw_points_num)
{
	// DC_LOG_GAMMA is default logging of all hw points
	// DC_LOG_ALL_GAMMA logs all points, not only hw points
	// DC_LOG_ALL_TF_POINTS logs all channels of the tf
	int i = 0;

	DC_LOGGER_INIT(ctx->logger);
	DC_LOG_GAMMA("Gamma Correction TF");
	DC_LOG_ALL_GAMMA("Logging all tf points...");
	DC_LOG_ALL_TF_CHANNELS("Logging all channels...");

	for (i = 0; i < hw_points_num; i++) {
		DC_LOG_GAMMA("R\t%d\t%llu", i, tf->tf_pts.red[i].value);
		DC_LOG_ALL_TF_CHANNELS("G\t%d\t%llu", i, tf->tf_pts.green[i].value);
		DC_LOG_ALL_TF_CHANNELS("B\t%d\t%llu", i, tf->tf_pts.blue[i].value);
	}

	for (i = hw_points_num; i < MAX_NUM_HW_POINTS; i++) {
		DC_LOG_ALL_GAMMA("R\t%d\t%llu", i, tf->tf_pts.red[i].value);
		DC_LOG_ALL_TF_CHANNELS("G\t%d\t%llu", i, tf->tf_pts.green[i].value);
		DC_LOG_ALL_TF_CHANNELS("B\t%d\t%llu", i, tf->tf_pts.blue[i].value);
	}
}

bool dcn10_set_output_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	struct dpp *dpp = pipe_ctx->plane_res.dpp;

	if (dpp == NULL)
		return false;

	dpp->regamma_params.hw_points_num = GAMMA_HW_POINTS_NUM;

	if (stream->out_transfer_func &&
	    stream->out_transfer_func->type == TF_TYPE_PREDEFINED &&
	    stream->out_transfer_func->tf == TRANSFER_FUNCTION_SRGB)
		dpp->funcs->dpp_program_regamma_pwl(dpp, NULL, OPP_REGAMMA_SRGB);

	/* dcn10_translate_regamma_to_hw_format takes 750us, only do it when full
	 * update.
	 */
	else if (cm_helper_translate_curve_to_hw_format(
			stream->out_transfer_func,
			&dpp->regamma_params, false)) {
		dpp->funcs->dpp_program_regamma_pwl(
				dpp,
				&dpp->regamma_params, OPP_REGAMMA_USER);
	} else
		dpp->funcs->dpp_program_regamma_pwl(dpp, NULL, OPP_REGAMMA_BYPASS);

	if (stream != NULL && stream->ctx != NULL &&
			stream->out_transfer_func != NULL) {
		log_tf(stream->ctx,
				stream->out_transfer_func,
				dpp->regamma_params.hw_points_num);
	}

	return true;
}

void dcn10_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock)
{
	struct dce_hwseq *hws = dc->hwseq;

	/* use TG master update lock to lock everything on the TG
	 * therefore only top pipe need to lock
	 */
	if (!pipe || pipe->top_pipe)
		return;

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);

	if (lock)
		pipe->stream_res.tg->funcs->lock(pipe->stream_res.tg);
	else
		pipe->stream_res.tg->funcs->unlock(pipe->stream_res.tg);

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

/**
 * delay_cursor_until_vupdate() - Delay cursor update if too close to VUPDATE.
 *
 * Software keepout workaround to prevent cursor update locking from stalling
 * out cursor updates indefinitely or from old values from being retained in
 * the case where the viewport changes in the same frame as the cursor.
 *
 * The idea is to calculate the remaining time from VPOS to VUPDATE. If it's
 * too close to VUPDATE, then stall out until VUPDATE finishes.
 *
 * TODO: Optimize cursor programming to be once per frame before VUPDATE
 *       to avoid the need for this workaround.
 */
static void delay_cursor_until_vupdate(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct crtc_position position;
	uint32_t vupdate_start, vupdate_end;
	unsigned int lines_to_vupdate, us_to_vupdate, vpos;
	unsigned int us_per_line, us_vupdate;

	if (!dc->hwss.calc_vupdate_position || !dc->hwss.get_position)
		return;

	if (!pipe_ctx->stream_res.stream_enc || !pipe_ctx->stream_res.tg)
		return;

	dc->hwss.calc_vupdate_position(dc, pipe_ctx, &vupdate_start,
				       &vupdate_end);

	dc->hwss.get_position(&pipe_ctx, 1, &position);
	vpos = position.vertical_count;

	/* Avoid wraparound calculation issues */
	vupdate_start += stream->timing.v_total;
	vupdate_end += stream->timing.v_total;
	vpos += stream->timing.v_total;

	if (vpos <= vupdate_start) {
		/* VPOS is in VACTIVE or back porch. */
		lines_to_vupdate = vupdate_start - vpos;
	} else if (vpos > vupdate_end) {
		/* VPOS is in the front porch. */
		return;
	} else {
		/* VPOS is in VUPDATE. */
		lines_to_vupdate = 0;
	}

	/* Calculate time until VUPDATE in microseconds. */
	us_per_line =
		stream->timing.h_total * 10000u / stream->timing.pix_clk_100hz;
	us_to_vupdate = lines_to_vupdate * us_per_line;

	/* 70 us is a conservative estimate of cursor update time*/
	if (us_to_vupdate > 70)
		return;

	/* Stall out until the cursor update completes. */
	if (vupdate_end < vupdate_start)
		vupdate_end += stream->timing.v_total;
	us_vupdate = (vupdate_end - vupdate_start + 1) * us_per_line;
	udelay(us_to_vupdate + us_vupdate);
}

void dcn10_cursor_lock(struct dc *dc, struct pipe_ctx *pipe, bool lock)
{
	/* cursor lock is per MPCC tree, so only need to lock one pipe per stream */
	if (!pipe || pipe->top_pipe)
		return;

	/* Prevent cursor lock from stalling out cursor updates. */
	if (lock)
		delay_cursor_until_vupdate(dc, pipe);

	if (pipe->stream && should_use_dmub_lock(pipe->stream->link)) {
		union dmub_hw_lock_flags hw_locks = { 0 };
		struct dmub_hw_lock_inst_flags inst_flags = { 0 };

		hw_locks.bits.lock_cursor = 1;
		inst_flags.opp_inst = pipe->stream_res.opp->inst;

		dmub_hw_lock_mgr_cmd(dc->ctx->dmub_srv,
					lock,
					&hw_locks,
					&inst_flags);
	} else
		dc->res_pool->mpc->funcs->cursor_lock(dc->res_pool->mpc,
				pipe->stream_res.opp->inst, lock);
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

void dcn10_enable_timing_synchronization(
	struct dc *dc,
	int group_index,
	int group_size,
	struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	int i;

	DC_SYNC_INFO("Setting up OTG reset trigger\n");

	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->enable_reset_trigger(
				grouped_pipes[i]->stream_res.tg,
				grouped_pipes[0]->stream_res.tg->inst);

	DC_SYNC_INFO("Waiting for trigger\n");

	/* Need to get only check 1 pipe for having reset as all the others are
	 * synchronized. Look at last pipe programmed to reset.
	 */

	wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[1]->stream_res.tg);
	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->disable_reset_trigger(
				grouped_pipes[i]->stream_res.tg);

	DC_SYNC_INFO("Sync complete\n");
}

void dcn10_enable_per_frame_crtc_position_reset(
	struct dc *dc,
	int group_size,
	struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	int i;

	DC_SYNC_INFO("Setting up\n");
	for (i = 0; i < group_size; i++)
		if (grouped_pipes[i]->stream_res.tg->funcs->enable_crtc_reset)
			grouped_pipes[i]->stream_res.tg->funcs->enable_crtc_reset(
					grouped_pipes[i]->stream_res.tg,
					0,
					&grouped_pipes[i]->stream->triggered_crtc_reset);

	DC_SYNC_INFO("Waiting for trigger\n");

	for (i = 0; i < group_size; i++)
		wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[i]->stream_res.tg);

	DC_SYNC_INFO("Multi-display sync is complete\n");
}

/*static void print_rq_dlg_ttu(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	DC_LOG_BANDWIDTH_CALCS(dc->ctx->logger,
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

	DC_LOG_BANDWIDTH_CALCS(dc->ctx->logger,
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

	DC_LOG_BANDWIDTH_CALCS(dc->ctx->logger,
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

	DC_LOG_BANDWIDTH_CALCS(dc->ctx->logger,
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
*/

static void mmhub_read_vm_system_aperture_settings(struct dcn10_hubp *hubp1,
		struct vm_system_aperture_param *apt,
		struct dce_hwseq *hws)
{
	PHYSICAL_ADDRESS_LOC physical_page_number;
	uint32_t logical_addr_low;
	uint32_t logical_addr_high;

	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			PHYSICAL_PAGE_NUMBER_MSB, &physical_page_number.high_part);
	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			PHYSICAL_PAGE_NUMBER_LSB, &physical_page_number.low_part);

	REG_GET(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
			LOGICAL_ADDR, &logical_addr_low);

	REG_GET(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			LOGICAL_ADDR, &logical_addr_high);

	apt->sys_default.quad_part =  physical_page_number.quad_part << 12;
	apt->sys_low.quad_part =  (int64_t)logical_addr_low << 18;
	apt->sys_high.quad_part =  (int64_t)logical_addr_high << 18;
}

/* Temporary read settings, future will get values from kmd directly */
static void mmhub_read_vm_context0_settings(struct dcn10_hubp *hubp1,
		struct vm_context0_param *vm0,
		struct dce_hwseq *hws)
{
	PHYSICAL_ADDRESS_LOC fb_base;
	PHYSICAL_ADDRESS_LOC fb_offset;
	uint32_t fb_base_value;
	uint32_t fb_offset_value;

	REG_GET(DCHUBBUB_SDPIF_FB_BASE, SDPIF_FB_BASE, &fb_base_value);
	REG_GET(DCHUBBUB_SDPIF_FB_OFFSET, SDPIF_FB_OFFSET, &fb_offset_value);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			PAGE_DIRECTORY_ENTRY_HI32, &vm0->pte_base.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			PAGE_DIRECTORY_ENTRY_LO32, &vm0->pte_base.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_start.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_start.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_end.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_end.low_part);

	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			PHYSICAL_PAGE_ADDR_HI4, &vm0->fault_default.high_part);
	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			PHYSICAL_PAGE_ADDR_LO32, &vm0->fault_default.low_part);

	/*
	 * The values in VM_CONTEXT0_PAGE_TABLE_BASE_ADDR is in UMA space.
	 * Therefore we need to do
	 * DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR
	 * - DCHUBBUB_SDPIF_FB_OFFSET + DCHUBBUB_SDPIF_FB_BASE
	 */
	fb_base.quad_part = (uint64_t)fb_base_value << 24;
	fb_offset.quad_part = (uint64_t)fb_offset_value << 24;
	vm0->pte_base.quad_part += fb_base.quad_part;
	vm0->pte_base.quad_part -= fb_offset.quad_part;
}


void dcn10_program_pte_vm(struct dce_hwseq *hws, struct hubp *hubp)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	struct vm_system_aperture_param apt = { {{ 0 } } };
	struct vm_context0_param vm0 = { { { 0 } } };

	mmhub_read_vm_system_aperture_settings(hubp1, &apt, hws);
	mmhub_read_vm_context0_settings(hubp1, &vm0, hws);

	hubp->funcs->hubp_set_vm_system_aperture_settings(hubp, &apt);
	hubp->funcs->hubp_set_vm_context0_settings(hubp, &vm0);
}

static void dcn10_enable_plane(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;

	if (dc->debug.sanity_checks) {
		hws->funcs.verify_allow_pstate_change_high(dc);
	}

	undo_DEGVIDCN10_253_wa(dc);

	power_on_plane(dc->hwseq,
		pipe_ctx->plane_res.hubp->inst);

	/* enable DCFCLK current DCHUB */
	pipe_ctx->plane_res.hubp->funcs->hubp_clk_cntl(pipe_ctx->plane_res.hubp, true);

	/* make sure OPP_PIPE_CLOCK_EN = 1 */
	pipe_ctx->stream_res.opp->funcs->opp_pipe_clock_control(
			pipe_ctx->stream_res.opp,
			true);

/* TODO: enable/disable in dm as per update type.
	if (plane_state) {
		DC_LOG_DC(dc->ctx->logger,
				"Pipe:%d 0x%x: addr hi:0x%x, "
				"addr low:0x%x, "
				"src: %d, %d, %d,"
				" %d; dst: %d, %d, %d, %d;\n",
				pipe_ctx->pipe_idx,
				plane_state,
				plane_state->address.grph.addr.high_part,
				plane_state->address.grph.addr.low_part,
				plane_state->src_rect.x,
				plane_state->src_rect.y,
				plane_state->src_rect.width,
				plane_state->src_rect.height,
				plane_state->dst_rect.x,
				plane_state->dst_rect.y,
				plane_state->dst_rect.width,
				plane_state->dst_rect.height);

		DC_LOG_DC(dc->ctx->logger,
				"Pipe %d: width, height, x, y         format:%d\n"
				"viewport:%d, %d, %d, %d\n"
				"recout:  %d, %d, %d, %d\n",
				pipe_ctx->pipe_idx,
				plane_state->format,
				pipe_ctx->plane_res.scl_data.viewport.width,
				pipe_ctx->plane_res.scl_data.viewport.height,
				pipe_ctx->plane_res.scl_data.viewport.x,
				pipe_ctx->plane_res.scl_data.viewport.y,
				pipe_ctx->plane_res.scl_data.recout.width,
				pipe_ctx->plane_res.scl_data.recout.height,
				pipe_ctx->plane_res.scl_data.recout.x,
				pipe_ctx->plane_res.scl_data.recout.y);
		print_rq_dlg_ttu(dc, pipe_ctx);
	}
*/
	if (dc->config.gpu_vm_support)
		dcn10_program_pte_vm(hws, pipe_ctx->plane_res.hubp);

	if (dc->debug.sanity_checks) {
		hws->funcs.verify_allow_pstate_change_high(dc);
	}
}

void dcn10_program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	int i = 0;
	struct dpp_grph_csc_adjustment adjust;
	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;


	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			adjust.temperature_matrix[i] =
				pipe_ctx->stream->gamut_remap_matrix.matrix[i];
	} else if (pipe_ctx->plane_state &&
		   pipe_ctx->plane_state->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			adjust.temperature_matrix[i] =
				pipe_ctx->plane_state->gamut_remap_matrix.matrix[i];
	}

	pipe_ctx->plane_res.dpp->funcs->dpp_set_gamut_remap(pipe_ctx->plane_res.dpp, &adjust);
}


static bool dcn10_is_rear_mpo_fix_required(struct pipe_ctx *pipe_ctx, enum dc_color_space colorspace)
{
	if (pipe_ctx->plane_state && pipe_ctx->plane_state->layer_index > 0 && is_rgb_cspace(colorspace)) {
		if (pipe_ctx->top_pipe) {
			struct pipe_ctx *top = pipe_ctx->top_pipe;

			while (top->top_pipe)
				top = top->top_pipe; // Traverse to top pipe_ctx
			if (top->plane_state && top->plane_state->layer_index == 0)
				return true; // Front MPO plane not hidden
		}
	}
	return false;
}

static void dcn10_set_csc_adjustment_rgb_mpo_fix(struct pipe_ctx *pipe_ctx, uint16_t *matrix)
{
	// Override rear plane RGB bias to fix MPO brightness
	uint16_t rgb_bias = matrix[3];

	matrix[3] = 0;
	matrix[7] = 0;
	matrix[11] = 0;
	pipe_ctx->plane_res.dpp->funcs->dpp_set_csc_adjustment(pipe_ctx->plane_res.dpp, matrix);
	matrix[3] = rgb_bias;
	matrix[7] = rgb_bias;
	matrix[11] = rgb_bias;
}

void dcn10_program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id)
{
	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment == true) {
		if (pipe_ctx->plane_res.dpp->funcs->dpp_set_csc_adjustment != NULL) {

			/* MPO is broken with RGB colorspaces when OCSC matrix
			 * brightness offset >= 0 on DCN1 due to OCSC before MPC
			 * Blending adds offsets from front + rear to rear plane
			 *
			 * Fix is to set RGB bias to 0 on rear plane, top plane
			 * black value pixels add offset instead of rear + front
			 */

			int16_t rgb_bias = matrix[3];
			// matrix[3/7/11] are all the same offset value

			if (rgb_bias > 0 && dcn10_is_rear_mpo_fix_required(pipe_ctx, colorspace)) {
				dcn10_set_csc_adjustment_rgb_mpo_fix(pipe_ctx, matrix);
			} else {
				pipe_ctx->plane_res.dpp->funcs->dpp_set_csc_adjustment(pipe_ctx->plane_res.dpp, matrix);
			}
		}
	} else {
		if (pipe_ctx->plane_res.dpp->funcs->dpp_set_csc_default != NULL)
			pipe_ctx->plane_res.dpp->funcs->dpp_set_csc_default(pipe_ctx->plane_res.dpp, colorspace);
	}
}

void dcn10_get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	switch (pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set border color to red */
		color->color_r_cr = color_value;
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set border color to blue */
		color->color_b_cb = color_value;
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set border color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set border color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set border color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		break;
	default:
		break;
	}
}

void dcn10_get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	// Determine the overscan color based on the top-most (desktop) plane's context
	struct pipe_ctx *top_pipe_ctx  = pipe_ctx;

	while (top_pipe_ctx->top_pipe != NULL)
		top_pipe_ctx = top_pipe_ctx->top_pipe;

	switch (top_pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB2101010:
		if (top_pipe_ctx->stream->out_transfer_func->tf == TRANSFER_FUNCTION_PQ) {
			/* HDR10, ARGB2101010 - set border color to red */
			color->color_r_cr = color_value;
		} else if (top_pipe_ctx->stream->out_transfer_func->tf == TRANSFER_FUNCTION_GAMMA22) {
			/* FreeSync 2 ARGB2101010 - set border color to pink */
			color->color_r_cr = color_value;
			color->color_b_cb = color_value;
		}
		break;
	case PIXEL_FORMAT_FP16:
		if (top_pipe_ctx->stream->out_transfer_func->tf == TRANSFER_FUNCTION_PQ) {
			/* HDR10, FP16 - set border color to blue */
			color->color_b_cb = color_value;
		} else if (top_pipe_ctx->stream->out_transfer_func->tf == TRANSFER_FUNCTION_GAMMA22) {
			/* FreeSync 2 HDR - set border color to green */
			color->color_g_y = color_value;
		}
		break;
	default:
		/* SDR - set border color to Gray */
		color->color_r_cr = color_value/2;
		color->color_b_cb = color_value/2;
		color->color_g_y = color_value/2;
		break;
	}
}

static void dcn10_update_dpp(struct dpp *dpp, struct dc_plane_state *plane_state)
{
	struct dc_bias_and_scale bns_params = {0};

	// program the input csc
	dpp->funcs->dpp_setup(dpp,
			plane_state->format,
			EXPANSION_MODE_ZERO,
			plane_state->input_csc_color_matrix,
			plane_state->color_space,
			NULL);

	//set scale and bias registers
	build_prescale_params(&bns_params, plane_state);
	if (dpp->funcs->dpp_program_bias_and_scale)
		dpp->funcs->dpp_program_bias_and_scale(dpp, &bns_params);
}

void dcn10_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct mpcc_blnd_cfg blnd_cfg = {{0}};
	bool per_pixel_alpha = pipe_ctx->plane_state->per_pixel_alpha && pipe_ctx->bottom_pipe;
	int mpcc_id;
	struct mpcc *new_mpcc;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params = &(pipe_ctx->stream_res.opp->mpc_tree_params);

	if (dc->debug.visual_confirm == VISUAL_CONFIRM_HDR) {
		hws->funcs.get_hdr_visual_confirm_color(
				pipe_ctx, &blnd_cfg.black_color);
	} else if (dc->debug.visual_confirm == VISUAL_CONFIRM_SURFACE) {
		hws->funcs.get_surface_visual_confirm_color(
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
	blnd_cfg.global_gain = 0xff;

	if (pipe_ctx->plane_state->global_alpha)
		blnd_cfg.global_alpha = pipe_ctx->plane_state->global_alpha_value;
	else
		blnd_cfg.global_alpha = 0xff;

	/* DCN1.0 has output CM before MPC which seems to screw with
	 * pre-multiplied alpha.
	 */
	blnd_cfg.pre_multiplied_alpha = is_rgb_cspace(
			pipe_ctx->stream->output_color_space)
					&& per_pixel_alpha;


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
	if (!pipe_ctx->plane_state->update_flags.bits.full_update) {
		mpc->funcs->update_blending(mpc, &blnd_cfg, mpcc_id);
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

	ASSERT(new_mpcc != NULL);

	hubp->opp_id = pipe_ctx->stream_res.opp->inst;
	hubp->mpcc_id = mpcc_id;
}

static void update_scaler(struct pipe_ctx *pipe_ctx)
{
	bool per_pixel_alpha =
			pipe_ctx->plane_state->per_pixel_alpha && pipe_ctx->bottom_pipe;

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = per_pixel_alpha;
	pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;
	/* scaler configuration */
	pipe_ctx->plane_res.dpp->funcs->dpp_set_scaler(
			pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);
}

static void dcn10_update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct plane_size size = plane_state->plane_size;
	unsigned int compat_level = 0;
	bool should_divided_by_2 = false;

	/* depends on DML calculation, DPP clock value may change dynamically */
	/* If request max dpp clk is lower than current dispclk, no need to
	 * divided by 2
	 */
	if (plane_state->update_flags.bits.full_update) {

		/* new calculated dispclk, dppclk are stored in
		 * context->bw_ctx.bw.dcn.clk.dispclk_khz / dppclk_khz. current
		 * dispclk, dppclk are from dc->clk_mgr->clks.dispclk_khz.
		 * dcn_validate_bandwidth compute new dispclk, dppclk.
		 * dispclk will put in use after optimize_bandwidth when
		 * ramp_up_dispclk_with_dpp is called.
		 * there are two places for dppclk be put in use. One location
		 * is the same as the location as dispclk. Another is within
		 * update_dchubp_dpp which happens between pre_bandwidth and
		 * optimize_bandwidth.
		 * dppclk updated within update_dchubp_dpp will cause new
		 * clock values of dispclk and dppclk not be in use at the same
		 * time. when clocks are decreased, this may cause dppclk is
		 * lower than previous configuration and let pipe stuck.
		 * for example, eDP + external dp,  change resolution of DP from
		 * 1920x1080x144hz to 1280x960x60hz.
		 * before change: dispclk = 337889 dppclk = 337889
		 * change mode, dcn_validate_bandwidth calculate
		 *                dispclk = 143122 dppclk = 143122
		 * update_dchubp_dpp be executed before dispclk be updated,
		 * dispclk = 337889, but dppclk use new value dispclk /2 =
		 * 168944. this will cause pipe pstate warning issue.
		 * solution: between pre_bandwidth and optimize_bandwidth, while
		 * dispclk is going to be decreased, keep dppclk = dispclk
		 **/
		if (context->bw_ctx.bw.dcn.clk.dispclk_khz <
				dc->clk_mgr->clks.dispclk_khz)
			should_divided_by_2 = false;
		else
			should_divided_by_2 =
					context->bw_ctx.bw.dcn.clk.dppclk_khz <=
					dc->clk_mgr->clks.dispclk_khz / 2;

		dpp->funcs->dpp_dppclk_control(
				dpp,
				should_divided_by_2,
				true);

		if (dc->res_pool->dccg)
			dc->res_pool->dccg->funcs->update_dpp_dto(
					dc->res_pool->dccg,
					dpp->inst,
					pipe_ctx->plane_res.bw.dppclk_khz);
		else
			dc->clk_mgr->clks.dppclk_khz = should_divided_by_2 ?
						dc->clk_mgr->clks.dispclk_khz / 2 :
							dc->clk_mgr->clks.dispclk_khz;
	}

	/* TODO: Need input parameter to tell current DCHUB pipe tie to which OTG
	 * VTG is within DCHUBBUB which is commond block share by each pipe HUBP.
	 * VTG is 1:1 mapping with OTG. Each pipe HUBP will select which VTG
	 */
	if (plane_state->update_flags.bits.full_update) {
		hubp->funcs->hubp_vtg_sel(hubp, pipe_ctx->stream_res.tg->inst);

		hubp->funcs->hubp_setup(
			hubp,
			&pipe_ctx->dlg_regs,
			&pipe_ctx->ttu_regs,
			&pipe_ctx->rq_regs,
			&pipe_ctx->pipe_dlg_param);
		hubp->funcs->hubp_setup_interdependent(
			hubp,
			&pipe_ctx->dlg_regs,
			&pipe_ctx->ttu_regs);
	}

	size.surface_size = pipe_ctx->plane_res.scl_data.viewport;

	if (plane_state->update_flags.bits.full_update ||
		plane_state->update_flags.bits.bpp_change)
		dcn10_update_dpp(dpp, plane_state);

	if (plane_state->update_flags.bits.full_update ||
		plane_state->update_flags.bits.per_pixel_alpha_change ||
		plane_state->update_flags.bits.global_alpha_change)
		hws->funcs.update_mpcc(dc, pipe_ctx);

	if (plane_state->update_flags.bits.full_update ||
		plane_state->update_flags.bits.per_pixel_alpha_change ||
		plane_state->update_flags.bits.global_alpha_change ||
		plane_state->update_flags.bits.scaling_change ||
		plane_state->update_flags.bits.position_change) {
		update_scaler(pipe_ctx);
	}

	if (plane_state->update_flags.bits.full_update ||
		plane_state->update_flags.bits.scaling_change ||
		plane_state->update_flags.bits.position_change) {
		hubp->funcs->mem_program_viewport(
			hubp,
			&pipe_ctx->plane_res.scl_data.viewport,
			&pipe_ctx->plane_res.scl_data.viewport_c);
	}

	if (pipe_ctx->stream->cursor_attributes.address.quad_part != 0) {
		dc->hwss.set_cursor_position(pipe_ctx);
		dc->hwss.set_cursor_attribute(pipe_ctx);

		if (dc->hwss.set_cursor_sdr_white_level)
			dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
	}

	if (plane_state->update_flags.bits.full_update) {
		/*gamut remap*/
		dc->hwss.program_gamut_remap(pipe_ctx);

		dc->hwss.program_output_csc(dc,
				pipe_ctx,
				pipe_ctx->stream->output_color_space,
				pipe_ctx->stream->csc_color_matrix.matrix,
				pipe_ctx->stream_res.opp->inst);
	}

	if (plane_state->update_flags.bits.full_update ||
		plane_state->update_flags.bits.pixel_format_change ||
		plane_state->update_flags.bits.horizontal_mirror_change ||
		plane_state->update_flags.bits.rotation_change ||
		plane_state->update_flags.bits.swizzle_change ||
		plane_state->update_flags.bits.dcc_change ||
		plane_state->update_flags.bits.bpp_change ||
		plane_state->update_flags.bits.scaling_change ||
		plane_state->update_flags.bits.plane_size_change) {
		hubp->funcs->hubp_program_surface_config(
			hubp,
			plane_state->format,
			&plane_state->tiling_info,
			&size,
			plane_state->rotation,
			&plane_state->dcc,
			plane_state->horizontal_mirror,
			compat_level);
	}

	hubp->power_gated = false;

	hws->funcs.update_plane_addr(dc, pipe_ctx);

	if (is_pipe_tree_visible(pipe_ctx))
		hubp->funcs->set_blank(hubp, false);
}

void dcn10_blank_pixel_data(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank)
{
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	struct stream_resource *stream_res = &pipe_ctx->stream_res;
	struct dc_stream_state *stream = pipe_ctx->stream;

	/* program otg blank color */
	color_space = stream->output_color_space;
	color_space_to_black_color(dc, color_space, &black_color);

	/*
	 * The way 420 is packed, 2 channels carry Y component, 1 channel
	 * alternate between Cb and Cr, so both channels need the pixel
	 * value for Y
	 */
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
		black_color.color_r_cr = black_color.color_g_y;


	if (stream_res->tg->funcs->set_blank_color)
		stream_res->tg->funcs->set_blank_color(
				stream_res->tg,
				&black_color);

	if (!blank) {
		if (stream_res->tg->funcs->set_blank)
			stream_res->tg->funcs->set_blank(stream_res->tg, blank);
		if (stream_res->abm) {
			dc->hwss.set_pipe(pipe_ctx);
			stream_res->abm->funcs->set_abm_level(stream_res->abm, stream->abm_level);
		}
	} else if (blank) {
		dc->hwss.set_abm_immediate_disable(pipe_ctx);
		if (stream_res->tg->funcs->set_blank) {
			stream_res->tg->funcs->wait_for_state(stream_res->tg, CRTC_STATE_VBLANK);
			stream_res->tg->funcs->set_blank(stream_res->tg, blank);
		}
	}
}

void dcn10_set_hdr_multiplier(struct pipe_ctx *pipe_ctx)
{
	struct fixed31_32 multiplier = pipe_ctx->plane_state->hdr_mult;
	uint32_t hw_mult = 0x1f000; // 1.0 default multiplier
	struct custom_float_format fmt;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;


	if (!dc_fixpt_eq(multiplier, dc_fixpt_from_int(0))) // check != 0
		convert_to_custom_float_format(multiplier, &fmt, &hw_mult);

	pipe_ctx->plane_res.dpp->funcs->dpp_set_hdr_multiplier(
			pipe_ctx->plane_res.dpp, hw_mult);
}

void dcn10_program_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;

	if (pipe_ctx->plane_state->update_flags.bits.full_update)
		dcn10_enable_plane(dc, pipe_ctx, context);

	dcn10_update_dchubp_dpp(dc, pipe_ctx, context);

	hws->funcs.set_hdr_multiplier(pipe_ctx);

	if (pipe_ctx->plane_state->update_flags.bits.full_update ||
			pipe_ctx->plane_state->update_flags.bits.in_transfer_func_change ||
			pipe_ctx->plane_state->update_flags.bits.gamma_change)
		hws->funcs.set_input_transfer_func(dc, pipe_ctx, pipe_ctx->plane_state);

	/* dcn10_translate_regamma_to_hw_format takes 750us to finish
	 * only do gamma programming for full update.
	 * TODO: This can be further optimized/cleaned up
	 * Always call this for now since it does memcmp inside before
	 * doing heavy calculation and programming
	 */
	if (pipe_ctx->plane_state->update_flags.bits.full_update)
		hws->funcs.set_output_transfer_func(dc, pipe_ctx, pipe_ctx->stream);
}

static void dcn10_program_all_pipe_in_tree(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;

	if (pipe_ctx->top_pipe == NULL) {
		bool blank = !is_pipe_tree_visible(pipe_ctx);

		pipe_ctx->stream_res.tg->funcs->program_global_sync(
				pipe_ctx->stream_res.tg,
				pipe_ctx->pipe_dlg_param.vready_offset,
				pipe_ctx->pipe_dlg_param.vstartup_start,
				pipe_ctx->pipe_dlg_param.vupdate_offset,
				pipe_ctx->pipe_dlg_param.vupdate_width);

		pipe_ctx->stream_res.tg->funcs->set_vtg_params(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

		if (hws->funcs.setup_vupdate_interrupt)
			hws->funcs.setup_vupdate_interrupt(dc, pipe_ctx);

		hws->funcs.blank_pixel_data(dc, pipe_ctx, blank);
	}

	if (pipe_ctx->plane_state != NULL)
		hws->funcs.program_pipe(dc, pipe_ctx, context);

	if (pipe_ctx->bottom_pipe != NULL && pipe_ctx->bottom_pipe != pipe_ctx)
		dcn10_program_all_pipe_in_tree(dc, pipe_ctx->bottom_pipe, context);
}

static struct pipe_ctx *dcn10_find_top_pipe_for_stream(
		struct dc *dc,
		struct dc_state *context,
		const struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->plane_state && !old_pipe_ctx->plane_state)
			continue;

		if (pipe_ctx->stream != stream)
			continue;

		if (!pipe_ctx->top_pipe && !pipe_ctx->prev_odm_pipe)
			return pipe_ctx;
	}
	return NULL;
}

bool dcn10_disconnect_pipes(
		struct dc *dc,
		struct dc_state *context)
{
		bool found_pipe = false;
		int i, j;
		struct dce_hwseq *hws = dc->hwseq;
		struct dc_state *old_ctx = dc->current_state;
		bool mpcc_disconnected = false;
		struct pipe_ctx *old_pipe;
		struct pipe_ctx *new_pipe;
		DC_LOGGER_INIT(dc->ctx->logger);

		/* Set pipe update flags and lock pipes */
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
			new_pipe = &context->res_ctx.pipe_ctx[i];
			new_pipe->update_flags.raw = 0;

			if (!old_pipe->plane_state && !new_pipe->plane_state)
				continue;

			if (old_pipe->plane_state && !new_pipe->plane_state)
				new_pipe->update_flags.bits.disable = 1;

			/* Check for scl update */
			if (memcmp(&old_pipe->plane_res.scl_data, &new_pipe->plane_res.scl_data, sizeof(struct scaler_data)))
					new_pipe->update_flags.bits.scaler = 1;

			/* Check for vp update */
			if (memcmp(&old_pipe->plane_res.scl_data.viewport, &new_pipe->plane_res.scl_data.viewport, sizeof(struct rect))
					|| memcmp(&old_pipe->plane_res.scl_data.viewport_c,
						&new_pipe->plane_res.scl_data.viewport_c, sizeof(struct rect)))
				new_pipe->update_flags.bits.viewport = 1;

		}

		if (!IS_DIAG_DC(dc->ctx->dce_environment)) {
			/* Disconnect mpcc here only if losing pipe split*/
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable &&
					old_ctx->res_ctx.pipe_ctx[i].top_pipe) {

					/* Find the top pipe in the new ctx for the bottom pipe that we
					 * want to remove by comparing the streams and planes. If both
					 * pipes are being disabled then do it in the regular pipe
					 * programming sequence
					 */
					for (j = 0; j < dc->res_pool->pipe_count; j++) {
						if (old_ctx->res_ctx.pipe_ctx[i].top_pipe->stream == context->res_ctx.pipe_ctx[j].stream &&
							old_ctx->res_ctx.pipe_ctx[i].top_pipe->plane_state == context->res_ctx.pipe_ctx[j].plane_state &&
							!context->res_ctx.pipe_ctx[j].top_pipe &&
							!context->res_ctx.pipe_ctx[j].update_flags.bits.disable) {
							found_pipe = true;
							break;
						}
					}

					// Disconnect if the top pipe lost it's pipe split
					if (found_pipe && !context->res_ctx.pipe_ctx[j].bottom_pipe) {
						hws->funcs.plane_atomic_disconnect(dc, &dc->current_state->res_ctx.pipe_ctx[i]);
						DC_LOG_DC("Reset mpcc for pipe %d\n", dc->current_state->res_ctx.pipe_ctx[i].pipe_idx);
						mpcc_disconnected = true;
					}
				}
				found_pipe = false;
			}
		}

		if (mpcc_disconnected) {
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
				struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
				struct dc_plane_state *plane_state = pipe_ctx->plane_state;
				struct hubp *hubp = pipe_ctx->plane_res.hubp;

				if (!pipe_ctx || !plane_state || !pipe_ctx->stream)
					continue;

				// Only update scaler and viewport here if we lose a pipe split.
				// This is to prevent half the screen from being black when we
				// unlock after disconnecting MPCC.
				if (!(old_pipe && !pipe_ctx->top_pipe &&
					!pipe_ctx->bottom_pipe && old_pipe->bottom_pipe))
					continue;

				if (pipe_ctx->update_flags.raw || pipe_ctx->plane_state->update_flags.raw || pipe_ctx->stream->update_flags.raw) {
					if (pipe_ctx->update_flags.bits.scaler ||
						plane_state->update_flags.bits.scaling_change ||
						plane_state->update_flags.bits.position_change ||
						plane_state->update_flags.bits.per_pixel_alpha_change ||
						pipe_ctx->stream->update_flags.bits.scaling) {

						pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->plane_state->per_pixel_alpha;
						ASSERT(pipe_ctx->plane_res.scl_data.lb_params.depth == LB_PIXEL_DEPTH_30BPP);
						/* scaler configuration */
						pipe_ctx->plane_res.dpp->funcs->dpp_set_scaler(
						pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);
					}

					if (pipe_ctx->update_flags.bits.viewport ||
						(context == dc->current_state && plane_state->update_flags.bits.position_change) ||
						(context == dc->current_state && plane_state->update_flags.bits.scaling_change) ||
						(context == dc->current_state && pipe_ctx->stream->update_flags.bits.scaling)) {

						hubp->funcs->mem_program_viewport(
							hubp,
							&pipe_ctx->plane_res.scl_data.viewport,
							&pipe_ctx->plane_res.scl_data.viewport_c);
					}
				}
			}
		}
	return mpcc_disconnected;
}

void dcn10_wait_for_pending_cleared(struct dc *dc,
		struct dc_state *context)
{
		struct pipe_ctx *pipe_ctx;
		struct timing_generator *tg;
		int i;

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			pipe_ctx = &context->res_ctx.pipe_ctx[i];
			tg = pipe_ctx->stream_res.tg;

			/*
			 * Only wait for top pipe's tg penindg bit
			 * Also skip if pipe is disabled.
			 */
			if (pipe_ctx->top_pipe ||
			    !pipe_ctx->stream || !pipe_ctx->plane_state ||
			    !tg->funcs->is_tg_enabled(tg))
				continue;

			/*
			 * Wait for VBLANK then VACTIVE to ensure we get VUPDATE.
			 * For some reason waiting for OTG_UPDATE_PENDING cleared
			 * seems to not trigger the update right away, and if we
			 * lock again before VUPDATE then we don't get a separated
			 * operation.
			 */
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VBLANK);
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VACTIVE);
		}
}

void dcn10_apply_ctx_for_surface(
		struct dc *dc,
		const struct dc_stream_state *stream,
		int num_planes,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	int i;
	struct timing_generator *tg;
	uint32_t underflow_check_delay_us;
	bool interdependent_update = false;
	struct pipe_ctx *top_pipe_to_program =
			dcn10_find_top_pipe_for_stream(dc, context, stream);
	DC_LOGGER_INIT(dc->ctx->logger);

	// Clear pipe_ctx flag
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		pipe_ctx->update_flags.raw = 0;
	}

	if (!top_pipe_to_program)
		return;

	tg = top_pipe_to_program->stream_res.tg;

	interdependent_update = top_pipe_to_program->plane_state &&
		top_pipe_to_program->plane_state->update_flags.bits.full_update;

	underflow_check_delay_us = dc->debug.underflow_assert_delay_us;

	if (underflow_check_delay_us != 0xFFFFFFFF && hws->funcs.did_underflow_occur)
		ASSERT(hws->funcs.did_underflow_occur(dc, top_pipe_to_program));

	if (underflow_check_delay_us != 0xFFFFFFFF)
		udelay(underflow_check_delay_us);

	if (underflow_check_delay_us != 0xFFFFFFFF && hws->funcs.did_underflow_occur)
		ASSERT(hws->funcs.did_underflow_occur(dc, top_pipe_to_program));

	if (num_planes == 0) {
		/* OTG blank before remove all front end */
		hws->funcs.blank_pixel_data(dc, top_pipe_to_program, true);
	}

	/* Disconnect unused mpcc */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if ((!pipe_ctx->plane_state ||
		     pipe_ctx->stream_res.tg != old_pipe_ctx->stream_res.tg) &&
		    old_pipe_ctx->plane_state &&
		    old_pipe_ctx->stream_res.tg == tg) {

			hws->funcs.plane_atomic_disconnect(dc, old_pipe_ctx);
			pipe_ctx->update_flags.bits.disable = 1;

			DC_LOG_DC("Reset mpcc for pipe %d\n",
					old_pipe_ctx->pipe_idx);
		}
	}

	if (num_planes > 0)
		dcn10_program_all_pipe_in_tree(dc, top_pipe_to_program, context);

	/* Program secondary blending tree and writeback pipes */
	if ((stream->num_wb_info > 0) && (hws->funcs.program_all_writeback_pipes_in_tree))
		hws->funcs.program_all_writeback_pipes_in_tree(dc, stream, context);
	if (interdependent_update)
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			/* Skip inactive pipes and ones already updated */
			if (!pipe_ctx->stream || pipe_ctx->stream == stream ||
			    !pipe_ctx->plane_state || !tg->funcs->is_tg_enabled(tg))
				continue;

			pipe_ctx->plane_res.hubp->funcs->hubp_setup_interdependent(
				pipe_ctx->plane_res.hubp,
				&pipe_ctx->dlg_regs,
				&pipe_ctx->ttu_regs);
		}
}

void dcn10_post_unlock_program_front_end(
		struct dc *dc,
		struct dc_state *context)
{
	int i;

	DC_LOGGER_INIT(dc->ctx->logger);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->top_pipe &&
			!pipe_ctx->prev_odm_pipe &&
			pipe_ctx->stream) {
			struct timing_generator *tg = pipe_ctx->stream_res.tg;

			if (context->stream_status[i].plane_count == 0)
				false_optc_underflow_wa(dc, pipe_ctx->stream, tg);
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable)
			dc->hwss.disable_plane(dc, &dc->current_state->res_ctx.pipe_ctx[i]);

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable) {
			dc->hwss.optimize_bandwidth(dc, context);
			break;
		}

	if (dc->hwseq->wa.DEGVIDCN10_254)
		hubbub1_wm_change_req_wa(dc->res_pool->hubbub);
}

static void dcn10_stereo_hw_frame_pack_wa(struct dc *dc, struct dc_state *context)
{
	uint8_t i;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->timing.timing_3d_format
				== TIMING_3D_FORMAT_HW_FRAME_PACKING) {
			/*
			 * Disable stutter
			 */
			hubbub1_allow_self_refresh_control(dc->res_pool->hubbub, false);
			break;
		}
	}
}

void dcn10_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubbub *hubbub = dc->res_pool->hubbub;

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		if (context->stream_count == 0)
			context->bw_ctx.bw.dcn.clk.phyclk_khz = 0;

		dc->clk_mgr->funcs->update_clocks(
				dc->clk_mgr,
				context,
				false);
	}

	dc->wm_optimized_required = hubbub->funcs->program_watermarks(hubbub,
			&context->bw_ctx.bw.dcn.watermarks,
			dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
			true);
	dcn10_stereo_hw_frame_pack_wa(dc, context);

	if (dc->debug.pplib_wm_report_mode == WM_REPORT_OVERRIDE)
		dcn_bw_notify_pplib_of_wm_ranges(dc);

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

void dcn10_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubbub *hubbub = dc->res_pool->hubbub;

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		if (context->stream_count == 0)
			context->bw_ctx.bw.dcn.clk.phyclk_khz = 0;

		dc->clk_mgr->funcs->update_clocks(
				dc->clk_mgr,
				context,
				true);
	}

	hubbub->funcs->program_watermarks(hubbub,
			&context->bw_ctx.bw.dcn.watermarks,
			dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
			true);

	dcn10_stereo_hw_frame_pack_wa(dc, context);

	if (dc->debug.pplib_wm_report_mode == WM_REPORT_OVERRIDE)
		dcn_bw_notify_pplib_of_wm_ranges(dc);

	if (dc->debug.sanity_checks)
		hws->funcs.verify_allow_pstate_change_high(dc);
}

void dcn10_set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, unsigned int vmin, unsigned int vmax,
		unsigned int vmid, unsigned int vmid_frame_number)
{
	int i = 0;
	struct drr_params params = {0};
	// DRR set trigger event mapped to OTG_TRIG_A (bit 11) for manual control flow
	unsigned int event_triggers = 0x800;
	// Note DRR trigger events are generated regardless of whether num frames met.
	unsigned int num_frames = 2;

	params.vertical_total_max = vmax;
	params.vertical_total_min = vmin;
	params.vertical_total_mid = vmid;
	params.vertical_total_mid_frame_num = vmid_frame_number;

	/* TODO: If multiple pipes are to be supported, you need
	 * some GSL stuff. Static screen triggers may be programmed differently
	 * as well.
	 */
	for (i = 0; i < num_pipes; i++) {
		pipe_ctx[i]->stream_res.tg->funcs->set_drr(
			pipe_ctx[i]->stream_res.tg, &params);
		if (vmax != 0 && vmin != 0)
			pipe_ctx[i]->stream_res.tg->funcs->set_static_screen_control(
					pipe_ctx[i]->stream_res.tg,
					event_triggers, num_frames);
	}
}

void dcn10_get_position(struct pipe_ctx **pipe_ctx,
		int num_pipes,
		struct crtc_position *position)
{
	int i = 0;

	/* TODO: handle pipes > 1
	 */
	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->get_position(pipe_ctx[i]->stream_res.tg, position);
}

void dcn10_set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_params *params)
{
	unsigned int i;
	unsigned int triggers = 0;

	if (params->triggers.surface_update)
		triggers |= 0x80;
	if (params->triggers.cursor_update)
		triggers |= 0x2;
	if (params->triggers.force_trigger)
		triggers |= 0x1;

	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->
			set_static_screen_control(pipe_ctx[i]->stream_res.tg,
					triggers, params->num_frames);
}

static void dcn10_config_stereo_parameters(
		struct dc_stream_state *stream, struct crtc_stereo_flags *flags)
{
	enum view_3d_format view_format = stream->view_format;
	enum dc_timing_3d_format timing_3d_format =\
			stream->timing.timing_3d_format;
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
					stream->link->ddc->dongle_type;
			if (dongle == DISPLAY_DONGLE_DP_VGA_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_DVI_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_HDMI_CONVERTER)
				flags->DISABLE_STEREO_DP_SYNC = 1;
		}
		flags->RIGHT_EYE_POLARITY =\
				stream->timing.flags.RIGHT_EYE_3D_POLARITY;
		if (timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			flags->FRAME_PACKED = 1;
	}

	return;
}

void dcn10_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc)
{
	struct crtc_stereo_flags flags = { 0 };
	struct dc_stream_state *stream = pipe_ctx->stream;

	dcn10_config_stereo_parameters(stream, &flags);

	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_SIDEBAND_FA) {
		if (!dc_set_generic_gpio_for_stereo(true, dc->ctx->gpio_service))
			dc_set_generic_gpio_for_stereo(false, dc->ctx->gpio_service);
	} else {
		dc_set_generic_gpio_for_stereo(false, dc->ctx->gpio_service);
	}

	pipe_ctx->stream_res.opp->funcs->opp_program_stereo(
		pipe_ctx->stream_res.opp,
		flags.PROGRAM_STEREO == 1 ? true:false,
		&stream->timing);

	pipe_ctx->stream_res.tg->funcs->program_stereo(
		pipe_ctx->stream_res.tg,
		&stream->timing,
		&flags);

	return;
}

static struct hubp *get_hubp_by_inst(struct resource_pool *res_pool, int mpcc_inst)
{
	int i;

	for (i = 0; i < res_pool->pipe_count; i++) {
		if (res_pool->hubps[i]->inst == mpcc_inst)
			return res_pool->hubps[i];
	}
	ASSERT(false);
	return NULL;
}

void dcn10_wait_for_mpcc_disconnect(
		struct dc *dc,
		struct resource_pool *res_pool,
		struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	int mpcc_inst;

	if (dc->debug.sanity_checks) {
		hws->funcs.verify_allow_pstate_change_high(dc);
	}

	if (!pipe_ctx->stream_res.opp)
		return;

	for (mpcc_inst = 0; mpcc_inst < MAX_PIPES; mpcc_inst++) {
		if (pipe_ctx->stream_res.opp->mpcc_disconnect_pending[mpcc_inst]) {
			struct hubp *hubp = get_hubp_by_inst(res_pool, mpcc_inst);

			res_pool->mpc->funcs->wait_for_idle(res_pool->mpc, mpcc_inst);
			pipe_ctx->stream_res.opp->mpcc_disconnect_pending[mpcc_inst] = false;
			hubp->funcs->set_blank(hubp, true);
		}
	}

	if (dc->debug.sanity_checks) {
		hws->funcs.verify_allow_pstate_change_high(dc);
	}

}

bool dcn10_dummy_display_power_gating(
	struct dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	return true;
}

void dcn10_update_pending_status(struct pipe_ctx *pipe_ctx)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	bool flip_pending;
	struct dc *dc = plane_state->ctx->dc;

	if (plane_state == NULL)
		return;

	flip_pending = pipe_ctx->plane_res.hubp->funcs->hubp_is_flip_pending(
					pipe_ctx->plane_res.hubp);

	plane_state->status.is_flip_pending = plane_state->status.is_flip_pending || flip_pending;

	if (!flip_pending)
		plane_state->status.current_address = plane_state->status.requested_address;

	if (plane_state->status.current_address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
			tg->funcs->is_stereo_left_eye) {
		plane_state->status.is_right_eye =
				!tg->funcs->is_stereo_left_eye(pipe_ctx->stream_res.tg);
	}

	if (dc->hwseq->wa_state.disallow_self_refresh_during_multi_plane_transition_applied) {
		struct dce_hwseq *hwseq = dc->hwseq;
		struct timing_generator *tg = dc->res_pool->timing_generators[0];
		unsigned int cur_frame = tg->funcs->get_frame_count(tg);

		if (cur_frame != hwseq->wa_state.disallow_self_refresh_during_multi_plane_transition_applied_on_frame) {
			struct hubbub *hubbub = dc->res_pool->hubbub;

			hubbub->funcs->allow_self_refresh_control(hubbub, !dc->debug.disable_stutter);
			hwseq->wa_state.disallow_self_refresh_during_multi_plane_transition_applied = false;
		}
	}
}

void dcn10_update_dchub(struct dce_hwseq *hws, struct dchub_init_data *dh_data)
{
	struct hubbub *hubbub = hws->ctx->dc->res_pool->hubbub;

	/* In DCN, this programming sequence is owned by the hubbub */
	hubbub->funcs->update_dchub(hubbub, dh_data);
}

static bool dcn10_can_pipe_disable_cursor(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *test_pipe;
	const struct rect *r1 = &pipe_ctx->plane_res.scl_data.recout, *r2;
	int r1_r = r1->x + r1->width, r1_b = r1->y + r1->height, r2_r, r2_b;

	/**
	 * Disable the cursor if there's another pipe above this with a
	 * plane that contains this pipe's viewport to prevent double cursor
	 * and incorrect scaling artifacts.
	 */
	for (test_pipe = pipe_ctx->top_pipe; test_pipe;
	     test_pipe = test_pipe->top_pipe) {
		if (!test_pipe->plane_state->visible)
			continue;

		r2 = &test_pipe->plane_res.scl_data.recout;
		r2_r = r2->x + r2->width;
		r2_b = r2->y + r2->height;

		if (r1->x >= r2->x && r1->y >= r2->y && r1_r <= r2_r && r1_b <= r2_b)
			return true;
	}

	return false;
}

void dcn10_set_cursor_position(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_position pos_cpy = pipe_ctx->stream->cursor_position;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_cursor_mi_param param = {
		.pixel_clk_khz = pipe_ctx->stream->timing.pix_clk_100hz / 10,
		.ref_clk_khz = pipe_ctx->stream->ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz,
		.viewport = pipe_ctx->plane_res.scl_data.viewport,
		.h_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.horz,
		.v_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.vert,
		.rotation = pipe_ctx->plane_state->rotation,
		.mirror = pipe_ctx->plane_state->horizontal_mirror
	};
	bool pipe_split_on = (pipe_ctx->top_pipe != NULL) ||
		(pipe_ctx->bottom_pipe != NULL);

	int x_plane = pipe_ctx->plane_state->dst_rect.x;
	int y_plane = pipe_ctx->plane_state->dst_rect.y;
	int x_pos = pos_cpy.x;
	int y_pos = pos_cpy.y;

	/**
	 * DC cursor is stream space, HW cursor is plane space and drawn
	 * as part of the framebuffer.
	 *
	 * Cursor position can't be negative, but hotspot can be used to
	 * shift cursor out of the plane bounds. Hotspot must be smaller
	 * than the cursor size.
	 */

	/**
	 * Translate cursor from stream space to plane space.
	 *
	 * If the cursor is scaled then we need to scale the position
	 * to be in the approximately correct place. We can't do anything
	 * about the actual size being incorrect, that's a limitation of
	 * the hardware.
	 */
	x_pos = (x_pos - x_plane) * pipe_ctx->plane_state->src_rect.width /
			pipe_ctx->plane_state->dst_rect.width;
	y_pos = (y_pos - y_plane) * pipe_ctx->plane_state->src_rect.height /
			pipe_ctx->plane_state->dst_rect.height;

	/**
	 * If the cursor's source viewport is clipped then we need to
	 * translate the cursor to appear in the correct position on
	 * the screen.
	 *
	 * This translation isn't affected by scaling so it needs to be
	 * done *after* we adjust the position for the scale factor.
	 *
	 * This is only done by opt-in for now since there are still
	 * some usecases like tiled display that might enable the
	 * cursor on both streams while expecting dc to clip it.
	 */
	if (pos_cpy.translate_by_source) {
		x_pos += pipe_ctx->plane_state->src_rect.x;
		y_pos += pipe_ctx->plane_state->src_rect.y;
	}

	/**
	 * If the position is negative then we need to add to the hotspot
	 * to shift the cursor outside the plane.
	 */

	if (x_pos < 0) {
		pos_cpy.x_hotspot -= x_pos;
		x_pos = 0;
	}

	if (y_pos < 0) {
		pos_cpy.y_hotspot -= y_pos;
		y_pos = 0;
	}

	pos_cpy.x = (uint32_t)x_pos;
	pos_cpy.y = (uint32_t)y_pos;

	if (pipe_ctx->plane_state->address.type
			== PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
		pos_cpy.enable = false;

	if (pos_cpy.enable && dcn10_can_pipe_disable_cursor(pipe_ctx))
		pos_cpy.enable = false;

	// Swap axis and mirror horizontally
	if (param.rotation == ROTATION_ANGLE_90) {
		uint32_t temp_x = pos_cpy.x;

		pos_cpy.x = pipe_ctx->plane_res.scl_data.viewport.width -
				(pos_cpy.y - pipe_ctx->plane_res.scl_data.viewport.x) + pipe_ctx->plane_res.scl_data.viewport.x;
		pos_cpy.y = temp_x;
	}
	// Swap axis and mirror vertically
	else if (param.rotation == ROTATION_ANGLE_270) {
		uint32_t temp_y = pos_cpy.y;
		int viewport_height =
			pipe_ctx->plane_res.scl_data.viewport.height;

		if (pipe_split_on) {
			if (pos_cpy.x > viewport_height) {
				pos_cpy.x = pos_cpy.x - viewport_height;
				pos_cpy.y = viewport_height - pos_cpy.x;
			} else {
				pos_cpy.y = 2 * viewport_height - pos_cpy.x;
			}
		} else
			pos_cpy.y = viewport_height - pos_cpy.x;
		pos_cpy.x = temp_y;
	}
	// Mirror horizontally and vertically
	else if (param.rotation == ROTATION_ANGLE_180) {
		int viewport_width =
			pipe_ctx->plane_res.scl_data.viewport.width;
		int viewport_x =
			pipe_ctx->plane_res.scl_data.viewport.x;

		if (pipe_split_on) {
			if (pos_cpy.x >= viewport_width + viewport_x) {
				pos_cpy.x = 2 * viewport_width
						- pos_cpy.x + 2 * viewport_x;
			} else {
				uint32_t temp_x = pos_cpy.x;

				pos_cpy.x = 2 * viewport_x - pos_cpy.x;
				if (temp_x >= viewport_x +
					(int)hubp->curs_attr.width || pos_cpy.x
					<= (int)hubp->curs_attr.width +
					pipe_ctx->plane_state->src_rect.x) {
					pos_cpy.x = temp_x + viewport_width;
				}
			}
		} else {
			pos_cpy.x = viewport_width - pos_cpy.x + 2 * viewport_x;
		}
		pos_cpy.y = pipe_ctx->plane_res.scl_data.viewport.height - pos_cpy.y;
	}

	hubp->funcs->set_cursor_position(hubp, &pos_cpy, &param);
	dpp->funcs->set_cursor_position(dpp, &pos_cpy, &param, hubp->curs_attr.width, hubp->curs_attr.height);
}

void dcn10_set_cursor_attribute(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_attributes *attributes = &pipe_ctx->stream->cursor_attributes;

	pipe_ctx->plane_res.hubp->funcs->set_cursor_attributes(
			pipe_ctx->plane_res.hubp, attributes);
	pipe_ctx->plane_res.dpp->funcs->set_cursor_attributes(
		pipe_ctx->plane_res.dpp, attributes);
}

void dcn10_set_cursor_sdr_white_level(struct pipe_ctx *pipe_ctx)
{
	uint32_t sdr_white_level = pipe_ctx->stream->cursor_attributes.sdr_white_level;
	struct fixed31_32 multiplier;
	struct dpp_cursor_attributes opt_attr = { 0 };
	uint32_t hw_scale = 0x3c00; // 1.0 default multiplier
	struct custom_float_format fmt;

	if (!pipe_ctx->plane_res.dpp->funcs->set_optional_cursor_attributes)
		return;

	fmt.exponenta_bits = 5;
	fmt.mantissa_bits = 10;
	fmt.sign = true;

	if (sdr_white_level > 80) {
		multiplier = dc_fixpt_from_fraction(sdr_white_level, 80);
		convert_to_custom_float_format(multiplier, &fmt, &hw_scale);
	}

	opt_attr.scale = hw_scale;
	opt_attr.bias = 0;

	pipe_ctx->plane_res.dpp->funcs->set_optional_cursor_attributes(
			pipe_ctx->plane_res.dpp, &opt_attr);
}

/*
 * apply_front_porch_workaround  TODO FPGA still need?
 *
 * This is a workaround for a bug that has existed since R5xx and has not been
 * fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
 */
static void apply_front_porch_workaround(
	struct dc_crtc_timing *timing)
{
	if (timing->flags.INTERLACE == 1) {
		if (timing->v_front_porch < 2)
			timing->v_front_porch = 2;
	} else {
		if (timing->v_front_porch < 1)
			timing->v_front_porch = 1;
	}
}

int dcn10_get_vupdate_offset_from_vsync(struct pipe_ctx *pipe_ctx)
{
	const struct dc_crtc_timing *dc_crtc_timing = &pipe_ctx->stream->timing;
	struct dc_crtc_timing patched_crtc_timing;
	int vesa_sync_start;
	int asic_blank_end;
	int interlace_factor;
	int vertical_line_start;

	patched_crtc_timing = *dc_crtc_timing;
	apply_front_porch_workaround(&patched_crtc_timing);

	interlace_factor = patched_crtc_timing.flags.INTERLACE ? 2 : 1;

	vesa_sync_start = patched_crtc_timing.v_addressable +
			patched_crtc_timing.v_border_bottom +
			patched_crtc_timing.v_front_porch;

	asic_blank_end = (patched_crtc_timing.v_total -
			vesa_sync_start -
			patched_crtc_timing.v_border_top)
			* interlace_factor;

	vertical_line_start = asic_blank_end -
			pipe_ctx->pipe_dlg_param.vstartup_start + 1;

	return vertical_line_start;
}

void dcn10_calc_vupdate_position(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		uint32_t *start_line,
		uint32_t *end_line)
{
	const struct dc_crtc_timing *dc_crtc_timing = &pipe_ctx->stream->timing;
	int vline_int_offset_from_vupdate =
			pipe_ctx->stream->periodic_interrupt0.lines_offset;
	int vupdate_offset_from_vsync = dc->hwss.get_vupdate_offset_from_vsync(pipe_ctx);
	int start_position;

	if (vline_int_offset_from_vupdate > 0)
		vline_int_offset_from_vupdate--;
	else if (vline_int_offset_from_vupdate < 0)
		vline_int_offset_from_vupdate++;

	start_position = vline_int_offset_from_vupdate + vupdate_offset_from_vsync;

	if (start_position >= 0)
		*start_line = start_position;
	else
		*start_line = dc_crtc_timing->v_total + start_position - 1;

	*end_line = *start_line + 2;

	if (*end_line >= dc_crtc_timing->v_total)
		*end_line = 2;
}

static void dcn10_cal_vline_position(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum vline_select vline,
		uint32_t *start_line,
		uint32_t *end_line)
{
	enum vertical_interrupt_ref_point ref_point = INVALID_POINT;

	if (vline == VLINE0)
		ref_point = pipe_ctx->stream->periodic_interrupt0.ref_point;
	else if (vline == VLINE1)
		ref_point = pipe_ctx->stream->periodic_interrupt1.ref_point;

	switch (ref_point) {
	case START_V_UPDATE:
		dcn10_calc_vupdate_position(
				dc,
				pipe_ctx,
				start_line,
				end_line);
		break;
	case START_V_SYNC:
		// Suppose to do nothing because vsync is 0;
		break;
	default:
		ASSERT(0);
		break;
	}
}

void dcn10_setup_periodic_interrupt(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum vline_select vline)
{
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	if (vline == VLINE0) {
		uint32_t start_line = 0;
		uint32_t end_line = 0;

		dcn10_cal_vline_position(dc, pipe_ctx, vline, &start_line, &end_line);

		tg->funcs->setup_vertical_interrupt0(tg, start_line, end_line);

	} else if (vline == VLINE1) {
		pipe_ctx->stream_res.tg->funcs->setup_vertical_interrupt1(
				tg,
				pipe_ctx->stream->periodic_interrupt1.lines_offset);
	}
}

void dcn10_setup_vupdate_interrupt(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	int start_line = dc->hwss.get_vupdate_offset_from_vsync(pipe_ctx);

	if (start_line < 0) {
		ASSERT(0);
		start_line = 0;
	}

	if (tg->funcs->setup_vertical_interrupt2)
		tg->funcs->setup_vertical_interrupt2(tg, start_line);
}

void dcn10_unblank_stream(struct pipe_ctx *pipe_ctx,
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
		if (params.timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			params.timing.pix_clk_100hz /= 2;
		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(pipe_ctx->stream_res.stream_enc, &params);
	}

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
		hws->funcs.edp_backlight_control(link, true);
	}
}

void dcn10_send_immediate_sdp_message(struct pipe_ctx *pipe_ctx,
				const uint8_t *custom_sdp_message,
				unsigned int sdp_message_size)
{
	if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		pipe_ctx->stream_res.stream_enc->funcs->send_immediate_sdp_message(
				pipe_ctx->stream_res.stream_enc,
				custom_sdp_message,
				sdp_message_size);
	}
}
enum dc_status dcn10_set_clock(struct dc *dc,
			enum dc_clock_type clock_type,
			uint32_t clk_khz,
			uint32_t stepping)
{
	struct dc_state *context = dc->current_state;
	struct dc_clock_config clock_cfg = {0};
	struct dc_clocks *current_clocks = &context->bw_ctx.bw.dcn.clk;

	if (dc->clk_mgr && dc->clk_mgr->funcs->get_clock)
				dc->clk_mgr->funcs->get_clock(dc->clk_mgr,
						context, clock_type, &clock_cfg);

	if (!dc->clk_mgr->funcs->get_clock)
		return DC_FAIL_UNSUPPORTED_1;

	if (clk_khz > clock_cfg.max_clock_khz)
		return DC_FAIL_CLK_EXCEED_MAX;

	if (clk_khz < clock_cfg.min_clock_khz)
		return DC_FAIL_CLK_BELOW_MIN;

	if (clk_khz < clock_cfg.bw_requirequired_clock_khz)
		return DC_FAIL_CLK_BELOW_CFG_REQUIRED;

	/*update internal request clock for update clock use*/
	if (clock_type == DC_CLOCK_TYPE_DISPCLK)
		current_clocks->dispclk_khz = clk_khz;
	else if (clock_type == DC_CLOCK_TYPE_DPPCLK)
		current_clocks->dppclk_khz = clk_khz;
	else
		return DC_ERROR_UNEXPECTED;

	if (dc->clk_mgr && dc->clk_mgr->funcs->update_clocks)
				dc->clk_mgr->funcs->update_clocks(dc->clk_mgr,
				context, true);
	return DC_OK;

}

void dcn10_get_clock(struct dc *dc,
			enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg)
{
	struct dc_state *context = dc->current_state;

	if (dc->clk_mgr && dc->clk_mgr->funcs->get_clock)
				dc->clk_mgr->funcs->get_clock(dc->clk_mgr, context, clock_type, clock_cfg);

}
