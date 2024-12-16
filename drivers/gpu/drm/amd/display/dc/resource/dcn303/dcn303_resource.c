// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
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
 */

#include "dcn303/dcn303_init.h"
#include "dcn303_resource.h"
#include "dcn303/dcn303_dccg.h"
#include "irq/dcn303/irq_service_dcn303.h"

#include "dcn30/dcn30_dio_link_encoder.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn30/dcn30_dwb.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn30/dcn30_hubp.h"
#include "dcn30/dcn30_mmhubbub.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn30/dcn30_opp.h"
#include "dcn30/dcn30_optc.h"
#include "dcn30/dcn30_resource.h"

#include "dcn20/dcn20_dsc.h"
#include "dcn20/dcn20_resource.h"

#include "dml/dcn30/dcn30_fpu.h"

#include "dcn10/dcn10_resource.h"

#include "link.h"

#include "dce/dce_abm.h"
#include "dce/dce_audio.h"
#include "dce/dce_aux.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_hwseq.h"
#include "dce/dce_i2c_hw.h"
#include "dce/dce_panel_cntl.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "clk_mgr.h"

#include "hw_sequencer_private.h"
#include "reg_helper.h"
#include "resource.h"
#include "vm_helper.h"

#include "sienna_cichlid_ip_offset.h"
#include "dcn/dcn_3_0_3_offset.h"
#include "dcn/dcn_3_0_3_sh_mask.h"
#include "dpcs/dpcs_3_0_3_offset.h"
#include "dpcs/dpcs_3_0_3_sh_mask.h"
#include "nbio/nbio_2_3_offset.h"

#include "dml/dcn303/dcn303_fpu.h"

#define DC_LOGGER \
	dc->ctx->logger
#define DC_LOGGER_INIT(logger)


static const struct dc_debug_options debug_defaults_drv = {
		.disable_dmcu = true,
		.force_abm_enable = false,
		.clock_trace = true,
		.disable_pplib_clock_request = true,
		.pipe_split_policy = MPC_SPLIT_AVOID,
		.force_single_disp_pipe_split = false,
		.disable_dcc = DCC_ENABLE,
		.vsr_support = true,
		.performance_trace = false,
		.max_downscale_src_width = 7680,/*upto 8K*/
		.disable_pplib_wm_range = false,
		.scl_reset_length10 = true,
		.sanity_checks = false,
		.underflow_assert_delay_us = 0xFFFFFFFF,
		.dwb_fi_phase = -1, // -1 = disable,
		.dmub_command_table = true,
		.use_max_lb = true,
		.exit_idle_opt_for_cursor_updates = true,
		.enable_legacy_fast_update = false,
		.using_dml2 = false,
};

static const struct dc_panel_config panel_config_defaults = {
		.psr = {
			.disable_psr = false,
			.disallow_psrsu = false,
			.disallow_replay = false,
		},
};

enum dcn303_clk_src_array_id {
	DCN303_CLK_SRC_PLL0,
	DCN303_CLK_SRC_PLL1,
	DCN303_CLK_SRC_TOTAL
};

static const struct resource_caps res_cap_dcn303 = {
		.num_timing_generator = 2,
		.num_opp = 2,
		.num_video_plane = 2,
		.num_audio = 2,
		.num_stream_encoder = 2,
		.num_dwb = 1,
		.num_ddc = 2,
		.num_vmid = 16,
		.num_mpc_3dlut = 1,
		.num_dsc = 2,
};

static const struct dc_plane_cap plane_cap = {
		.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
		.per_pixel_alpha = true,
		.pixel_format_support = {
				.argb8888 = true,
				.nv12 = true,
				.fp16 = true,
				.p010 = true,
				.ayuv = false,
		},
		.max_upscale_factor = {
				.argb8888 = 16000,
				.nv12 = 16000,
				.fp16 = 16000
		},
		.max_downscale_factor = {
				.argb8888 = 167,
				.nv12 = 167,
				.fp16 = 167
		},
		16,
		16
};

/* NBIO */
#define NBIO_BASE_INNER(seg) \
		NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
		NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) + \
		mm ## reg_name

/* DCN */
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) + mm ## reg_name

#define SF(reg_name, field_name, post_fix)\
		.field_name = reg_name ## __ ## field_name ## post_fix

#define SRI(reg_name, block, id)\
		.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + mm ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) + mm ## reg_name

#define SRII(reg_name, block, id)\
		.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

#define DCCG_SRII(reg_name, block, id)\
		.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
		.reg_name[id] = BASE(mm ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
		mm ## reg_name ## _ ## block ## id

#define SRII_DWB(reg_name, temp_name, block, id)\
		.reg_name[id] = BASE(mm ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## temp_name

#define SF_DWB2(reg_name, block, id, field_name, post_fix)	\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define SRII_MPC_RMU(reg_name, block, id)\
		.RMU##_##reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN30(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN30(_MASK)
};

#define vmid_regs(id)\
		[id] = { DCN20_VMID_REG_LIST(id) }

static const struct dcn_vmid_registers vmid_regs[] = {
		vmid_regs(0),
		vmid_regs(1),
		vmid_regs(2),
		vmid_regs(3),
		vmid_regs(4),
		vmid_regs(5),
		vmid_regs(6),
		vmid_regs(7),
		vmid_regs(8),
		vmid_regs(9),
		vmid_regs(10),
		vmid_regs(11),
		vmid_regs(12),
		vmid_regs(13),
		vmid_regs(14),
		vmid_regs(15)
};

static const struct dcn20_vmid_shift vmid_shifts = {
		DCN20_VMID_MASK_SH_LIST(__SHIFT)
};

static const struct dcn20_vmid_mask vmid_masks = {
		DCN20_VMID_MASK_SH_LIST(_MASK)
};

static struct hubbub *dcn303_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub3 = kzalloc(sizeof(struct dcn20_hubbub), GFP_KERNEL);

	if (!hubbub3)
		return NULL;

	hubbub3_construct(hubbub3, ctx, &hubbub_reg, &hubbub_shift, &hubbub_mask);

	for (i = 0; i < res_cap_dcn303.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub3->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub3->base;
}

#define vpg_regs(id)\
		[id] = { VPG_DCN3_REG_LIST(id) }

static const struct dcn30_vpg_registers vpg_regs[] = {
		vpg_regs(0),
		vpg_regs(1),
		vpg_regs(2)
};

static const struct dcn30_vpg_shift vpg_shift = {
		DCN3_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_vpg_mask vpg_mask = {
		DCN3_VPG_MASK_SH_LIST(_MASK)
};

static struct vpg *dcn303_vpg_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn30_vpg *vpg3 = kzalloc(sizeof(struct dcn30_vpg), GFP_KERNEL);

	if (!vpg3)
		return NULL;

	vpg3_construct(vpg3, ctx, inst, &vpg_regs[inst], &vpg_shift, &vpg_mask);

	return &vpg3->base;
}

#define afmt_regs(id)\
		[id] = { AFMT_DCN3_REG_LIST(id) }

static const struct dcn30_afmt_registers afmt_regs[] = {
		afmt_regs(0),
		afmt_regs(1),
		afmt_regs(2)
};

static const struct dcn30_afmt_shift afmt_shift = {
		DCN3_AFMT_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_afmt_mask afmt_mask = {
		DCN3_AFMT_MASK_SH_LIST(_MASK)
};

static struct afmt *dcn303_afmt_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn30_afmt *afmt3 = kzalloc(sizeof(struct dcn30_afmt), GFP_KERNEL);

	if (!afmt3)
		return NULL;

	afmt3_construct(afmt3, ctx, inst, &afmt_regs[inst], &afmt_shift, &afmt_mask);

	return &afmt3->base;
}

#define audio_regs(id)\
		[id] = { AUD_COMMON_REG_LIST(id) }

static const struct dce_audio_registers audio_regs[] = {
		audio_regs(0),
		audio_regs(1),
		audio_regs(2),
		audio_regs(3),
		audio_regs(4),
		audio_regs(5),
		audio_regs(6)
};

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

static struct audio *dcn303_create_audio(struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst, &audio_regs[inst], &audio_shift, &audio_mask);
}

#define stream_enc_regs(id)\
		[id] = { SE_DCN3_REG_LIST(id) }

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
		stream_enc_regs(0),
		stream_enc_regs(1)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct stream_encoder *dcn303_stream_encoder_create(enum engine_id eng_id, struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGB) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn303_vpg_create(ctx, vpg_inst);
	afmt = dcn303_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt) {
		kfree(enc1);
		kfree(vpg);
		kfree(afmt);
		return NULL;
	}

	dcn30_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id, vpg, afmt, &stream_enc_regs[eng_id],
			&se_shift, &se_mask);

	return &enc1->base;
}

#define clk_src_regs(index, pllid)\
		[index] = { CS_COMMON_REG_LIST_DCN3_03(index, pllid) }

static const struct dce110_clk_src_regs clk_src_regs[] = {
		clk_src_regs(0, A),
		clk_src_regs(1, B)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static struct clock_source *dcn303_clock_source_create(struct dc_context *ctx, struct dc_bios *bios,
		enum clock_source_id id, const struct dce110_clk_src_regs *regs, bool dp_clk_src)
{
	struct dce110_clk_src *clk_src = kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn3_clk_src_construct(clk_src, ctx, bios, id, regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN303_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN303_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN303_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn303_hwseq_create(struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
	}
	return hws;
}

#define hubp_regs(id)\
		[id] = { HUBP_REG_LIST_DCN30(id) }

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1)
};

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN30(_MASK)
};

static struct hubp *dcn303_hubp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_hubp *hubp2 = kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp3_construct(hubp2, ctx, inst, &hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

#define dpp_regs(id)\
		[id] = { DPP_REG_LIST_DCN30(id) }

static const struct dcn3_dpp_registers dpp_regs[] = {
		dpp_regs(0),
		dpp_regs(1)
};

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30(_MASK)
};

static struct dpp *dcn303_dpp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn3_dpp *dpp = kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp3_construct(dpp, ctx, inst, &dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

#define opp_regs(id)\
		[id] = { OPP_REG_LIST_DCN30(id) }

static const struct dcn20_opp_registers opp_regs[] = {
		opp_regs(0),
		opp_regs(1)
};

static const struct dcn20_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN20(_MASK)
};

static struct output_pixel_processor *dcn303_opp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp = kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst, &opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

#define optc_regs(id)\
		[id] = { OPTC_COMMON_REG_LIST_DCN3_0(id) }

static const struct dcn_optc_registers optc_regs[] = {
		optc_regs(0),
		optc_regs(1)
};

static const struct dcn_optc_shift optc_shift = {
		OPTC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
		OPTC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct timing_generator *dcn303_timing_generator_create(struct dc_context *ctx, uint32_t instance)
{
	struct optc *tgn10 = kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn30_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct dcn30_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN3_0(0),
		MPC_REG_LIST_DCN3_0(1),
		MPC_OUT_MUX_REG_LIST_DCN3_0(0),
		MPC_OUT_MUX_REG_LIST_DCN3_0(1),
		MPC_RMU_GLOBAL_REG_LIST_DCN3AG,
		MPC_RMU_REG_LIST_DCN3AG(0),
		MPC_DWB_MUX_REG_LIST_DCN3_0(0),
};

static const struct dcn30_mpc_shift mpc_shift = {
		MPC_COMMON_MASK_SH_LIST_DCN303(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
		MPC_COMMON_MASK_SH_LIST_DCN303(_MASK)
};

static struct mpc *dcn303_mpc_create(struct dc_context *ctx, int num_mpcc, int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc), GFP_KERNEL);

	if (!mpc30)
		return NULL;

	dcn30_mpc_construct(mpc30, ctx, &mpc_regs, &mpc_shift, &mpc_mask, num_mpcc, num_rmu);

	return &mpc30->base;
}

#define dsc_regsDCN20(id)\
[id] = { DSC_REG_LIST_DCN20(id) }

static const struct dcn20_dsc_registers dsc_regs[] = {
		dsc_regsDCN20(0),
		dsc_regsDCN20(1)
};

static const struct dcn20_dsc_shift dsc_shift = {
		DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
		DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static struct display_stream_compressor *dcn303_dsc_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc = kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

#define dwbc_regs_dcn3(id)\
[id] = { DWBC_COMMON_REG_LIST_DCN30(id) }

static const struct dcn30_dwbc_registers dwbc30_regs[] = {
		dwbc_regs_dcn3(0)
};

static const struct dcn30_dwbc_shift dwbc30_shift = {
		DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
		DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn303_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc), GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

		dcn30_dwbc_construct(dwbc30, ctx, &dwbc30_regs[i], &dwbc30_shift, &dwbc30_mask, i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

#define mcif_wb_regs_dcn3(id)\
[id] = { MCIF_WB_COMMON_REG_LIST_DCN30(id) }

static const struct dcn30_mmhubbub_registers mcif_wb30_regs[] = {
		mcif_wb_regs_dcn3(0)
};

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
		MCIF_WB_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
		MCIF_WB_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn303_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub), GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

		dcn30_mmhubbub_construct(mcif_wb30, ctx, &mcif_wb30_regs[i], &mcif_wb30_shift, &mcif_wb30_mask, i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

#define aux_engine_regs(id)\
[id] = {\
		AUX_COMMON_REG_LIST0(id), \
		.AUXN_IMPCAL = 0, \
		.AUXP_IMPCAL = 0, \
		.AUX_RESET_MASK = DP_AUX0_AUX_CONTROL__AUX_RESET_MASK, \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1)
};

static const struct dce110_aux_registers_shift aux_shift = {
		DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
		DCN_AUX_MASK_SH_LIST(_MASK)
};

static struct dce_aux *dcn303_aux_engine_create(struct dc_context *ctx, uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine = kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst, SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
			&aux_engine_regs[inst], &aux_mask, &aux_shift, ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}

#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2)
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN2(_MASK)
};

static struct dce_i2c_hw *dcn303_i2c_hw_create(struct dc_context *ctx, uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw = kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst, &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = true,
		.fec_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true
};

#define link_regs(id, phyid)\
		[id] = {\
				LE_DCN3_REG_LIST(id), \
				UNIPHY_DCN2_REG_LIST(phyid), \
				SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
		}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
		link_regs(0, A),
		link_regs(1, B)
};

static const struct dcn10_link_enc_shift le_shift = {
		LINK_ENCODER_MASK_SH_LIST_DCN30(__SHIFT),
		DPCS_DCN2_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
		LINK_ENCODER_MASK_SH_LIST_DCN30(_MASK),
		DPCS_DCN2_MASK_SH_LIST(_MASK)
};

#define aux_regs(id)\
		[id] = { DCN2_AUX_REG_LIST(id) }

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1)
};

#define hpd_regs(id)\
		[id] = { HPD_REG_LIST(id) }

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1)
};

static struct link_encoder *dcn303_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 = kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20 || enc_init_data->hpd_source >= ARRAY_SIZE(link_enc_hpd_regs))
		return NULL;

	dcn30_link_encoder_construct(enc20, enc_init_data, &link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter], &link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source], &le_shift, &le_mask);

	return &enc20->enc10.base;
}

static const struct dce_panel_cntl_registers panel_cntl_regs[] = {
		{ DCN_PANEL_CNTL_REG_LIST() }
};

static const struct dce_panel_cntl_shift panel_cntl_shift = {
		DCE_PANEL_CNTL_MASK_SH_LIST(__SHIFT)
};

static const struct dce_panel_cntl_mask panel_cntl_mask = {
		DCE_PANEL_CNTL_MASK_SH_LIST(_MASK)
};

static struct panel_cntl *dcn303_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dce_panel_cntl *panel_cntl = kzalloc(sizeof(struct dce_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dce_panel_cntl_construct(panel_cntl, init_data, &panel_cntl_regs[init_data->inst],
			&panel_cntl_shift, &panel_cntl_mask);

	return &panel_cntl->base;
}

static void read_dce_straps(struct dc_context *ctx, struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),
			FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);
}

static const struct resource_create_funcs res_create_funcs = {
		.read_dce_straps = read_dce_straps,
		.create_audio = dcn303_create_audio,
		.create_stream_encoder = dcn303_stream_encoder_create,
		.create_hwseq = dcn303_hwseq_create,
};

static bool is_soc_bounding_box_valid(struct dc *dc)
{
	uint32_t hw_internal_rev = dc->ctx->asic_id.hw_internal_rev;

	if (ASICREV_IS_BEIGE_GOBY_P(hw_internal_rev))
		return true;

	return false;
}

static bool init_soc_bounding_box(struct dc *dc,  struct resource_pool *pool)
{
	struct _vcs_dpi_soc_bounding_box_st *loaded_bb = &dcn3_03_soc;
	struct _vcs_dpi_ip_params_st *loaded_ip = &dcn3_03_ip;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!is_soc_bounding_box_valid(dc)) {
		DC_LOG_ERROR("%s: not valid soc bounding box/n", __func__);
		return false;
	}

	loaded_ip->max_num_otg = pool->pipe_count;
	loaded_ip->max_num_dpp = pool->pipe_count;
	loaded_ip->clamp_min_dcfclk = dc->config.clamp_min_dcfclk;
	DC_FP_START();
	dcn20_patch_bounding_box(dc, loaded_bb);
	DC_FP_END();

	if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
		struct bp_soc_bb_info bb_info = { 0 };

		if (dc->ctx->dc_bios->funcs->get_soc_bb_info(
			    dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
					DC_FP_START();
					dcn303_fpu_init_soc_bounding_box(bb_info);
					DC_FP_END();
		}
	}

	return true;
}

static void dcn303_resource_destruct(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL) {
			if (pool->stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->stream_enc[i]->vpg));
				pool->stream_enc[i]->vpg = NULL;
			}
			if (pool->stream_enc[i]->afmt != NULL) {
				kfree(DCN30_AFMT_FROM_AFMT(pool->stream_enc[i]->afmt));
				pool->stream_enc[i]->afmt = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->stream_enc[i]));
			pool->stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_dsc; i++) {
		if (pool->dscs[i] != NULL)
			dcn20_dsc_destroy(&pool->dscs[i]);
	}

	if (pool->mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->mpc));
		pool->mpc = NULL;
	}

	if (pool->hubbub != NULL) {
		kfree(pool->hubbub);
		pool->hubbub = NULL;
	}

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->dpps[i] != NULL) {
			kfree(TO_DCN20_DPP(pool->dpps[i]));
			pool->dpps[i] = NULL;
		}

		if (pool->hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->hubps[i]));
			pool->hubps[i] = NULL;
		}

		if (pool->irqs != NULL)
			dal_irq_service_destroy(&pool->irqs);
	}

	for (i = 0; i < pool->res_cap->num_ddc; i++) {
		if (pool->engines[i] != NULL)
			dce110_engine_destroy(&pool->engines[i]);
		if (pool->hw_i2cs[i] != NULL) {
			kfree(pool->hw_i2cs[i]);
			pool->hw_i2cs[i] = NULL;
		}
		if (pool->sw_i2cs[i] != NULL) {
			kfree(pool->sw_i2cs[i]);
			pool->sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_opp; i++) {
		if (pool->opps[i] != NULL)
			pool->opps[i]->funcs->opp_destroy(&pool->opps[i]);
	}

	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		if (pool->timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->timing_generators[i]));
			pool->timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_dwb; i++) {
		if (pool->dwbc[i] != NULL) {
			kfree(TO_DCN30_DWBC(pool->dwbc[i]));
			pool->dwbc[i] = NULL;
		}
		if (pool->mcif_wb[i] != NULL) {
			kfree(TO_DCN30_MMHUBBUB(pool->mcif_wb[i]));
			pool->mcif_wb[i] = NULL;
		}
	}

	for (i = 0; i < pool->audio_count; i++) {
		if (pool->audios[i])
			dce_aud_destroy(&pool->audios[i]);
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL)
			dcn20_clock_source_destroy(&pool->clock_sources[i]);
	}

	if (pool->dp_clock_source != NULL)
		dcn20_clock_source_destroy(&pool->dp_clock_source);

	for (i = 0; i < pool->res_cap->num_mpc_3dlut; i++) {
		if (pool->mpc_lut[i] != NULL) {
			dc_3dlut_func_release(pool->mpc_lut[i]);
			pool->mpc_lut[i] = NULL;
		}
		if (pool->mpc_shaper[i] != NULL) {
			dc_transfer_func_release(pool->mpc_shaper[i]);
			pool->mpc_shaper[i] = NULL;
		}
	}

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->multiple_abms[i]);
	}

	if (pool->psr != NULL)
		dmub_psr_destroy(&pool->psr);

	if (pool->dccg != NULL)
		dcn_dccg_destroy(&pool->dccg);

	if (pool->oem_device != NULL) {
		struct dc *dc = pool->oem_device->ctx->dc;

		dc->link_srv->destroy_ddc_service(&pool->oem_device);
	}
}

static void dcn303_destroy_resource_pool(struct resource_pool **pool)
{
	dcn303_resource_destruct(*pool);
	kfree(*pool);
	*pool = NULL;
}

static void dcn303_get_panel_config_defaults(struct dc_panel_config *panel_config)
{
	*panel_config = panel_config_defaults;
}

void dcn303_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	DC_FP_START();
	dcn303_fpu_update_bw_bounding_box(dc, bw_params);
	DC_FP_END();
}

static struct resource_funcs dcn303_res_pool_funcs = {
		.destroy = dcn303_destroy_resource_pool,
		.link_enc_create = dcn303_link_encoder_create,
		.panel_cntl_create = dcn303_panel_cntl_create,
		.validate_bandwidth = dcn30_validate_bandwidth,
		.calculate_wm_and_dlg = dcn30_calculate_wm_and_dlg,
		.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
		.populate_dml_pipes = dcn30_populate_dml_pipes_from_context,
		.acquire_free_pipe_as_secondary_dpp_pipe = dcn20_acquire_free_pipe_for_layer,
		.release_pipe = dcn20_release_pipe,
		.add_stream_to_ctx = dcn30_add_stream_to_ctx,
		.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
		.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
		.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
		.set_mcif_arb_params = dcn30_set_mcif_arb_params,
		.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
		.acquire_post_bldn_3dlut = dcn30_acquire_post_bldn_3dlut,
		.release_post_bldn_3dlut = dcn30_release_post_bldn_3dlut,
		.update_bw_bounding_box = dcn303_update_bw_bounding_box,
		.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
		.get_panel_config_defaults = dcn303_get_panel_config_defaults,
};

static struct dc_cap_funcs cap_funcs = {
		.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN3_03()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN3_03(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN3_03(_MASK)
};

#define abm_regs(id)\
		[id] = { ABM_DCN302_REG_LIST(id) }

static const struct dce_abm_registers abm_regs[] = {
		abm_regs(0),
		abm_regs(1)
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn303_resource_construct(
		uint8_t num_virtual_links,
		struct dc *dc,
		struct resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	struct ddc_service_init_data ddc_init_data = {0};

	ctx->dc_bios->regs = &bios_regs;

	pool->res_cap = &res_cap_dcn303;

	pool->funcs = &dcn303_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->pipe_count = pool->res_cap->num_timing_generator;
	pool->mpcc_count = pool->res_cap->num_timing_generator;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 5; /*1.4 w/a applied by derfault*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 4;
	/* total size = mall per channel * num channels * 1024 * 1024 */
	dc->caps.mall_size_total = dc->caps.mall_size_per_mem_channel *
				   dc->ctx->dc_bios->vram_info.num_chans *
				   1024 * 1024;
	dc->caps.cursor_cache_size =
		dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;
	dc->caps.max_slave_planes = 1;
	dc->caps.max_slave_yuv_planes = 1;
	dc->caps.max_slave_rgb_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;
	dc->caps.max_v_total = (1 << 15) - 1;
	dc->caps.vtotal_limited_by_fp2 = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 0; // must use gamma_corr
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 1;
	dc->caps.color.dpp.dgam_rom_caps.pq = 1;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 1;
	dc->caps.color.dpp.post_csc = 1;
	dc->caps.color.dpp.gamma_corr = 1;
	dc->caps.color.dpp.dgam_rom_for_yuv = 0;

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN3
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->res_cap->num_mpc_3dlut; //3
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	dc->caps.dp_hdmi21_pcon_support = true;

	dc->config.dc_mode_clk_limit_support = true;
	/* read VBIOS LTTPR caps */
	if (ctx->dc_bios->funcs->get_lttpr_caps) {
		enum bp_result bp_query_result;
		uint8_t is_vbios_lttpr_enable = 0;

		bp_query_result = ctx->dc_bios->funcs->get_lttpr_caps(ctx->dc_bios, &is_vbios_lttpr_enable);
		dc->caps.vbios_lttpr_enable = (bp_query_result == BP_RESULT_OK) && !!is_vbios_lttpr_enable;
	}

	if (ctx->dc_bios->funcs->get_lttpr_interop) {
		enum bp_result bp_query_result;
		uint8_t is_vbios_interop_enabled = 0;

		bp_query_result = ctx->dc_bios->funcs->get_lttpr_interop(ctx->dc_bios, &is_vbios_interop_enabled);
		dc->caps.vbios_lttpr_aware = (bp_query_result == BP_RESULT_OK) && !!is_vbios_interop_enabled;
	}

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->clock_sources[DCN303_CLK_SRC_PLL0] =
			dcn303_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL0,
					&clk_src_regs[0], false);
	pool->clock_sources[DCN303_CLK_SRC_PLL1] =
			dcn303_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL1,
					&clk_src_regs[1], false);

	pool->clk_src_count = DCN303_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->dp_clock_source =
			dcn303_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_ID_DP_DTO,
					&clk_src_regs[0], true);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->dccg = dccg30_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* PP Lib and SMU interfaces */
	init_soc_bounding_box(dc, pool);

	/* DML */
	dml_init_instance(&dc->dml, &dcn3_03_soc, &dcn3_03_ip, DML_PROJECT_DCN30);

	/* IRQ */
	init_data.ctx = dc->ctx;
	pool->irqs = dal_irq_service_dcn303_create(&init_data);
	if (!pool->irqs)
		goto create_fail;

	/* HUBBUB */
	pool->hubbub = dcn303_hubbub_create(ctx);
	if (pool->hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs and TGs */
	for (i = 0; i < pool->pipe_count; i++) {
		pool->hubps[i] = dcn303_hubp_create(ctx, i);
		if (pool->hubps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->dpps[i] = dcn303_dpp_create(ctx, i);
		if (pool->dpps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create dpps!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->res_cap->num_opp; i++) {
		pool->opps[i] = dcn303_opp_create(ctx, i);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		pool->timing_generators[i] = dcn303_timing_generator_create(ctx, i);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}
	pool->timing_generator_count = i;

	/* PSR */
	pool->psr = dmub_psr_create(ctx);
	if (pool->psr == NULL) {
		dm_error("DC: failed to create psr!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* ABM */
	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		pool->multiple_abms[i] = dmub_abm_create(ctx, &abm_regs[i], &abm_shift, &abm_mask);
		if (pool->multiple_abms[i] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* MPC and DSC */
	pool->mpc = dcn303_mpc_create(ctx, pool->mpcc_count, pool->res_cap->num_mpc_3dlut);
	if (pool->mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->res_cap->num_dsc; i++) {
		pool->dscs[i] = dcn303_dsc_create(ctx, i);
		if (pool->dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB and MMHUBBUB */
	if (!dcn303_dwbc_create(ctx, pool)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	if (!dcn303_mmhubbub_create(ctx, pool)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->res_cap->num_ddc; i++) {
		pool->engines[i] = dcn303_aux_engine_create(ctx, i);
		if (pool->engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->hw_i2cs[i] = dcn303_i2c_hw_create(ctx, i);
		if (pool->hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->sw_i2cs[i] = NULL;
	}

	/* Audio, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, pool,
			&res_create_funcs))
		goto create_fail;

	/* HW Sequencer and Plane caps */
	dcn303_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	if (dc->ctx->dc_bios->fw_info.oem_i2c_present) {
		ddc_init_data.ctx = dc->ctx;
		ddc_init_data.link = NULL;
		ddc_init_data.id.id = dc->ctx->dc_bios->fw_info.oem_i2c_obj_id;
		ddc_init_data.id.enum_id = 0;
		ddc_init_data.id.type = OBJECT_TYPE_GENERIC;
		pool->oem_device = dc->link_srv->create_ddc_service(&ddc_init_data);
	} else {
		pool->oem_device = NULL;
	}

	return true;

create_fail:

	dcn303_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn303_create_resource_pool(const struct dc_init_data *init_data, struct dc *dc)
{
	struct resource_pool *pool = kzalloc(sizeof(struct resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn303_resource_construct(init_data->num_virtual_links, dc, pool))
		return pool;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
