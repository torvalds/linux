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
#include "core_types.h"
#include "resource.h"
#include "custom_float.h"
#include "dcn10_hw_sequencer.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dce/dce_hwseq.h"
#include "abm.h"
#include "dmcu.h"
#include "dcn10_optc.h"
#include "dcn10/dcn10_dpp.h"
#include "dcn10/dcn10_mpc.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "reg_helper.h"
#include "dcn10_hubp.h"
#include "dcn10_hubbub.h"
#include "dcn10_cm_common.h"
#include "clk_mgr.h"

unsigned int snprintf_count(char *pBuf, unsigned int bufSize, char *fmt, ...)
{
	unsigned int ret_vsnprintf;
	unsigned int chars_printed;

	va_list args;
	va_start(args, fmt);

	ret_vsnprintf = vsnprintf(pBuf, bufSize, fmt, args);

	va_end(args);

	if (ret_vsnprintf > 0) {
		if (ret_vsnprintf < bufSize)
			chars_printed = ret_vsnprintf;
		else
			chars_printed = bufSize - 1;
	} else
		chars_printed = 0;

	return chars_printed;
}

static unsigned int dcn10_get_hubbub_state(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dcn_hubbub_wm wm;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	const uint32_t ref_clk_mhz = dc_ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	static const unsigned int frac = 1000;

	memset(&wm, 0, sizeof(struct dcn_hubbub_wm));
	dc->res_pool->hubbub->funcs->wm_read_state(dc->res_pool->hubbub, &wm);

	chars_printed = snprintf_count(pBuf, remaining_buffer, "wm_set_index,data_urgent,pte_meta_urgent,sr_enter,sr_exit,dram_clk_chanage\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < 4; i++) {
		struct dcn_hubbub_wm_set *s;

		s = &wm.sets[i];

		chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%d.%03d,%d.%03d,%d.%03d,%d.%03d,%d.%03d\n",
			s->wm_set,
			(s->data_urgent * frac) / ref_clk_mhz / frac, (s->data_urgent * frac) / ref_clk_mhz % frac,
			(s->pte_meta_urgent * frac) / ref_clk_mhz / frac, (s->pte_meta_urgent * frac) / ref_clk_mhz % frac,
			(s->sr_enter * frac) / ref_clk_mhz / frac, (s->sr_enter * frac) / ref_clk_mhz % frac,
			(s->sr_exit * frac) / ref_clk_mhz / frac, (s->sr_exit * frac) / ref_clk_mhz % frac,
			(s->dram_clk_chanage * frac) / ref_clk_mhz / frac, (s->dram_clk_chanage * frac) / ref_clk_mhz % frac);
		remaining_buffer -= chars_printed;
		pBuf += chars_printed;
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_hubp_states(struct dc *dc, char *pBuf, unsigned int bufSize, bool invarOnly)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	const uint32_t ref_clk_mhz = dc_ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	static const unsigned int frac = 1000;

	if (invarOnly)
		chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,format,addr_hi,width,height,rotation,mirror,sw_mode,dcc_en,blank_en,ttu_dis,underflow,"
			"min_ttu_vblank,qos_low_wm,qos_high_wm"
			"\n");
	else
		chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,format,addr_hi,addr_lo,width,height,rotation,mirror,sw_mode,dcc_en,blank_en,ttu_dis,underflow,"
					"min_ttu_vblank,qos_low_wm,qos_high_wm"
					"\n");

	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct hubp *hubp = pool->hubps[i];
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(hubp)->state);

		hubp->funcs->hubp_read_state(hubp);

		if (!s->blank_en) {
			if (invarOnly)
				chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%d,%d,%x,%x,%x,%x,%x,%x,%x,"
					"%d.%03d,%d.%03d,%d.%03d"
					"\n",
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
					s->ttu_disable,
					s->underflow_status,
					(s->min_ttu_vblank * frac) / ref_clk_mhz / frac, (s->min_ttu_vblank * frac) / ref_clk_mhz % frac,
					(s->qos_level_low_wm * frac) / ref_clk_mhz / frac, (s->qos_level_low_wm * frac) / ref_clk_mhz % frac,
					(s->qos_level_high_wm * frac) / ref_clk_mhz / frac, (s->qos_level_high_wm * frac) / ref_clk_mhz % frac);
			else
				chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%x,%d,%d,%x,%x,%x,%x,%x,%x,%x,"
					"%d.%03d,%d.%03d,%d.%03d"
					"\n",
					hubp->inst,
					s->pixel_format,
					s->inuse_addr_hi,
					s->inuse_addr_lo,
					s->viewport_width,
					s->viewport_height,
					s->rotation_angle,
					s->h_mirror_en,
					s->sw_mode,
					s->dcc_en,
					s->blank_en,
					s->ttu_disable,
					s->underflow_status,
					(s->min_ttu_vblank * frac) / ref_clk_mhz / frac, (s->min_ttu_vblank * frac) / ref_clk_mhz % frac,
					(s->qos_level_low_wm * frac) / ref_clk_mhz / frac, (s->qos_level_low_wm * frac) / ref_clk_mhz % frac,
					(s->qos_level_high_wm * frac) / ref_clk_mhz / frac, (s->qos_level_high_wm * frac) / ref_clk_mhz % frac);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_rq_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,drq_exp_m,prq_exp_m,mrq_exp_m,crq_exp_m,plane1_ba,"
		"luma_chunk_s,luma_min_chu_s,luma_meta_ch_s,luma_min_m_c_s,luma_dpte_gr_s,luma_mpte_gr_s,luma_swath_hei,luma_pte_row_h,"
		"chroma_chunk_s,chroma_min_chu_s,chroma_meta_ch_s,chroma_min_m_c_s,chroma_dpte_gr_s,chroma_mpte_gr_s,chroma_swath_hei,chroma_pte_row_h"
		"\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_rq_regs_st *rq_regs = &s->rq_regs;

		if (!s->blank_en) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,%x"
				"\n",
				pool->hubps[i]->inst, rq_regs->drq_expansion_mode, rq_regs->prq_expansion_mode, rq_regs->mrq_expansion_mode,
				rq_regs->crq_expansion_mode, rq_regs->plane1_base_address, rq_regs->rq_regs_l.chunk_size,
				rq_regs->rq_regs_l.min_chunk_size, rq_regs->rq_regs_l.meta_chunk_size,
				rq_regs->rq_regs_l.min_meta_chunk_size, rq_regs->rq_regs_l.dpte_group_size,
				rq_regs->rq_regs_l.mpte_group_size, rq_regs->rq_regs_l.swath_height,
				rq_regs->rq_regs_l.pte_row_height_linear, rq_regs->rq_regs_c.chunk_size, rq_regs->rq_regs_c.min_chunk_size,
				rq_regs->rq_regs_c.meta_chunk_size, rq_regs->rq_regs_c.min_meta_chunk_size,
				rq_regs->rq_regs_c.dpte_group_size, rq_regs->rq_regs_c.mpte_group_size,
				rq_regs->rq_regs_c.swath_height, rq_regs->rq_regs_c.pte_row_height_linear);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_dlg_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,rc_hbe,dlg_vbe,min_d_y_n,rc_per_ht,rc_x_a_s,"
		"dst_y_a_s,dst_y_pf,dst_y_vvb,dst_y_rvb,dst_y_vfl,dst_y_rfl,rf_pix_fq,"
		"vratio_pf,vrat_pf_c,rc_pg_vbl,rc_pg_vbc,rc_mc_vbl,rc_mc_vbc,rc_pg_fll,"
		"rc_pg_flc,rc_mc_fll,rc_mc_flc,pr_nom_l,pr_nom_c,rc_pg_nl,rc_pg_nc,"
		"mr_nom_l,mr_nom_c,rc_mc_nl,rc_mc_nc,rc_ld_pl,rc_ld_pc,rc_ld_l,"
		"rc_ld_c,cha_cur0,ofst_cur1,cha_cur1,vr_af_vc0,ddrq_limt,x_rt_dlay,x_rp_dlay,x_rr_sfl"
		"\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs = &s->dlg_attr;

		if (!s->blank_en) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,%x,%x,%x"
				"\n",
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

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_ttu_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,qos_ll_wm,qos_lh_wm,mn_ttu_vb,qos_l_flp,rc_rd_p_l,rc_rd_l,rc_rd_p_c,"
		"rc_rd_c,rc_rd_c0,rc_rd_pc0,rc_rd_c1,rc_rd_pc1,qos_lf_l,qos_rds_l,"
		"qos_lf_c,qos_rds_c,qos_lf_c0,qos_rds_c0,qos_lf_c1,qos_rds_c1"
		"\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(pool->hubps[i])->state);
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs = &s->ttu_attr;

		if (!s->blank_en) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x,%x,"
				"%x,%x,%x,%x,%x,%x"
				"\n",
				pool->hubps[i]->inst, ttu_regs->qos_level_low_wm, ttu_regs->qos_level_high_wm, ttu_regs->min_ttu_vblank,
				ttu_regs->qos_level_flip, ttu_regs->refcyc_per_req_delivery_pre_l, ttu_regs->refcyc_per_req_delivery_l,
				ttu_regs->refcyc_per_req_delivery_pre_c, ttu_regs->refcyc_per_req_delivery_c, ttu_regs->refcyc_per_req_delivery_cur0,
				ttu_regs->refcyc_per_req_delivery_pre_cur0, ttu_regs->refcyc_per_req_delivery_cur1,
				ttu_regs->refcyc_per_req_delivery_pre_cur1, ttu_regs->qos_level_fixed_l, ttu_regs->qos_ramp_disable_l,
				ttu_regs->qos_level_fixed_c, ttu_regs->qos_ramp_disable_c, ttu_regs->qos_level_fixed_cur0,
				ttu_regs->qos_ramp_disable_cur0, ttu_regs->qos_level_fixed_cur1, ttu_regs->qos_ramp_disable_cur1);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_cm_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,igam_format,igam_mode,dgam_mode,rgam_mode,gamut_mode,"
		"c11_c12,c13_c14,c21_c22,c23_c24,c31_c32,c33_c34"
		"\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct dpp *dpp = pool->dpps[i];
		struct dcn_dpp_state s = {0};

		dpp->funcs->dpp_read_state(dpp, &s);

		if (s.is_enabled) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,"
					"%s,%s,%s,"
					"%x,%08x,%08x,%08x,%08x,%08x,%08x"
				"\n",
				dpp->inst, s.igam_input_format,
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
				s.gamut_remap_mode, s.gamut_remap_c11_c12,
				s.gamut_remap_c13_c14, s.gamut_remap_c21_c22, s.gamut_remap_c23_c24,
				s.gamut_remap_c31_c32, s.gamut_remap_c33_c34);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_mpcc_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,opp,dpp,mpccbot,mode,alpha_mode,premult,overlap_only,idle\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->pipe_count; i++) {
		struct mpcc_state s = {0};

		pool->mpc->funcs->read_mpcc_state(pool->mpc, i, &s);

		if (s.opp_id != 0xf) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
				i, s.opp_id, s.dpp_id, s.bot_mpcc_id,
				s.mode, s.alpha_mode, s.pre_multiplied_alpha, s.overlap_only,
				s.idle);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_otg_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, remaining_buffer, "instance,v_bs,v_be,v_ss,v_se,vpol,vmax,vmin,vmax_sel,vmin_sel,"
			"h_bs,h_be,h_ss,h_se,hpol,htot,vtot,underflow,pixelclk[khz]\n");
	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	for (i = 0; i < pool->timing_generator_count; i++) {
		struct timing_generator *tg = pool->timing_generators[i];
		struct dcn_otg_state s = {0};
		int pix_clk = 0;

		optc1_read_otg_state(DCN10TG_FROM_TG(tg), &s);
		pix_clk = dc->current_state->res_ctx.pipe_ctx[i].stream_res.pix_clk_params.requested_pix_clk_100hz / 10;

		//only print if OTG master is enabled
		if (s.otg_enabled & 1) {
			chars_printed = snprintf_count(pBuf, remaining_buffer, "%x,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,%d"
				"\n",
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
				pix_clk);

			remaining_buffer -= chars_printed;
			pBuf += chars_printed;
		}
	}

	return bufSize - remaining_buffer;
}

static unsigned int dcn10_get_clock_states(struct dc *dc, char *pBuf, unsigned int bufSize)
{
	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = bufSize;

	chars_printed = snprintf_count(pBuf, bufSize, "dcfclk,dcfclk_deep_sleep,dispclk,"
		"dppclk,fclk,socclk\n"
		"%d,%d,%d,%d,%d,%d\n",
		dc->current_state->bw_ctx.bw.dcn.clk.dcfclk_khz,
		dc->current_state->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz,
		dc->current_state->bw_ctx.bw.dcn.clk.dispclk_khz,
		dc->current_state->bw_ctx.bw.dcn.clk.dppclk_khz,
		dc->current_state->bw_ctx.bw.dcn.clk.fclk_khz,
		dc->current_state->bw_ctx.bw.dcn.clk.socclk_khz);

	remaining_buffer -= chars_printed;
	pBuf += chars_printed;

	return bufSize - remaining_buffer;
}

static void dcn10_clear_otpc_underflow(struct dc *dc)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	for (i = 0; i < pool->timing_generator_count; i++) {
		struct timing_generator *tg = pool->timing_generators[i];
		struct dcn_otg_state s = {0};

		optc1_read_otg_state(DCN10TG_FROM_TG(tg), &s);

		if (s.otg_enabled & 1)
			tg->funcs->clear_optc_underflow(tg);
	}
}

static void dcn10_clear_hubp_underflow(struct dc *dc)
{
	struct resource_pool *pool = dc->res_pool;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		struct hubp *hubp = pool->hubps[i];
		struct dcn_hubp_state *s = &(TO_DCN10_HUBP(hubp)->state);

		hubp->funcs->hubp_read_state(hubp);

		if (!s->blank_en)
			hubp->funcs->hubp_clear_underflow(hubp);
	}
}

void dcn10_clear_status_bits(struct dc *dc, unsigned int mask)
{
	/*
	 *  Mask Format
	 *  Bit 0 - 31: Status bit to clear
	 *
	 *  Mask = 0x0 means clear all status bits
	 */
	const unsigned int DC_HW_STATE_MASK_HUBP_UNDERFLOW	= 0x1;
	const unsigned int DC_HW_STATE_MASK_OTPC_UNDERFLOW	= 0x2;

	if (mask == 0x0)
		mask = 0xFFFFFFFF;

	if (mask & DC_HW_STATE_MASK_HUBP_UNDERFLOW)
		dcn10_clear_hubp_underflow(dc);

	if (mask & DC_HW_STATE_MASK_OTPC_UNDERFLOW)
		dcn10_clear_otpc_underflow(dc);
}

void dcn10_get_hw_state(struct dc *dc, char *pBuf, unsigned int bufSize, unsigned int mask)
{
	/*
	 *  Mask Format
	 *  Bit 0 - 15: Hardware block mask
	 *  Bit 15: 1 = Invariant Only, 0 = All
	 */
	const unsigned int DC_HW_STATE_MASK_HUBBUB			= 0x1;
	const unsigned int DC_HW_STATE_MASK_HUBP			= 0x2;
	const unsigned int DC_HW_STATE_MASK_RQ				= 0x4;
	const unsigned int DC_HW_STATE_MASK_DLG				= 0x8;
	const unsigned int DC_HW_STATE_MASK_TTU				= 0x10;
	const unsigned int DC_HW_STATE_MASK_CM				= 0x20;
	const unsigned int DC_HW_STATE_MASK_MPCC			= 0x40;
	const unsigned int DC_HW_STATE_MASK_OTG				= 0x80;
	const unsigned int DC_HW_STATE_MASK_CLOCKS			= 0x100;
	const unsigned int DC_HW_STATE_INVAR_ONLY			= 0x8000;

	unsigned int chars_printed = 0;
	unsigned int remaining_buf_size = bufSize;

	if (mask == 0x0)
		mask = 0xFFFF; // Default, capture all, invariant only

	if ((mask & DC_HW_STATE_MASK_HUBBUB) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_hubbub_state(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_HUBP) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_hubp_states(dc, pBuf, remaining_buf_size, mask & DC_HW_STATE_INVAR_ONLY);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_RQ) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_rq_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_DLG) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_dlg_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_TTU) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_ttu_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_CM) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_cm_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_MPCC) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_mpcc_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_OTG) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_otg_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}

	if ((mask & DC_HW_STATE_MASK_CLOCKS) && remaining_buf_size > 0) {
		chars_printed = dcn10_get_clock_states(dc, pBuf, remaining_buf_size);
		pBuf += chars_printed;
		remaining_buf_size -= chars_printed;
	}
}
