/*
* Copyright 2018 Advanced Micro Devices, Inc.
 * Copyright 2019 Raptor Engineering, LLC
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

#include <linux/slab.h>

#include "dm_services.h"
#include "dc.h"

#include "dcn21_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn20/dcn20_resource.h"
#include "dcn21/dcn21_resource.h"

#include "dml/dcn20/dcn20_fpu.h"

#include "clk_mgr.h"
#include "dcn10/dcn10_hubp.h"
#include "dcn10/dcn10_ipp.h"
#include "dcn20/dcn20_hubbub.h"
#include "dcn20/dcn20_mpc.h"
#include "dcn20/dcn20_hubp.h"
#include "dcn21_hubp.h"
#include "irq/dcn21/irq_service_dcn21.h"
#include "dcn20/dcn20_dpp.h"
#include "dcn20/dcn20_optc.h"
#include "dcn21/dcn21_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn20/dcn20_opp.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn21/dcn21_link_encoder.h"
#include "dcn20/dcn20_stream_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "virtual/virtual_stream_encoder.h"
#include "dml/display_mode_vba.h"
#include "dcn20/dcn20_dccg.h"
#include "dcn21/dcn21_dccg.h"
#include "dcn21_hubbub.h"
#include "dcn10/dcn10_resource.h"
#include "dce/dce_panel_cntl.h"

#include "dcn20/dcn20_dwb.h"
#include "dcn20/dcn20_mmhubbub.h"
#include "dpcs/dpcs_2_1_0_offset.h"
#include "dpcs/dpcs_2_1_0_sh_mask.h"

#include "renoir_ip_offset.h"
#include "dcn/dcn_2_1_0_offset.h"
#include "dcn/dcn_2_1_0_sh_mask.h"

#include "nbio/nbio_7_0_offset.h"

#include "mmhub/mmhub_2_0_0_offset.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"

#include "reg_helper.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"
#include "dcn21_resource.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"
#include "dce/dmub_psr.h"
#include "dce/dmub_abm.h"

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define BASE_INNER(seg) DMU_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
					mm ## reg_name ## _ ## block ## id

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIF0_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) + \
					mm ## reg_name

/* MMHUB */
#define MMHUB_BASE_INNER(seg) \
	MMHUB_BASE__INST0_SEG ## seg

#define MMHUB_BASE(seg) \
	MMHUB_BASE_INNER(seg)

#define MMHUB_SR(reg_name)\
		.reg_name = MMHUB_BASE(mmMM ## reg_name ## _BASE_IDX) + \
					mmMM ## reg_name

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN2_1(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E),
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

static const struct dce_dmcu_registers dmcu_regs = {
		DMCU_DCN20_REG_LIST()
};

static const struct dce_dmcu_shift dmcu_shift = {
		DMCU_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_dmcu_mask dmcu_mask = {
		DMCU_MASK_SH_LIST_DCN10(_MASK)
};

static const struct dce_abm_registers abm_regs = {
		ABM_DCN20_REG_LIST()
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN20(_MASK)
};

#define audio_regs(id)\
[id] = {\
		AUD_COMMON_REG_LIST(id)\
}

static const struct dce_audio_registers audio_regs[] = {
	audio_regs(0),
	audio_regs(1),
	audio_regs(2),
	audio_regs(3),
	audio_regs(4),
	audio_regs(5),
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

static const struct dccg_registers dccg_regs = {
		DCCG_COMMON_REG_LIST_DCN_BASE()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN2_1(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN2_1(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN20(id),\
}

static const struct dcn20_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
	opp_regs(4),
	opp_regs(5),
};

static const struct dcn20_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN20(_MASK)
};

#define tg_regs(id)\
[id] = {TG_COMMON_REG_LIST_DCN2_0(id)}

static const struct dcn_optc_registers tg_regs[] = {
	tg_regs(0),
	tg_regs(1),
	tg_regs(2),
	tg_regs(3)
};

static const struct dcn_optc_shift tg_shift = {
	TG_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dcn_optc_mask tg_mask = {
	TG_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static const struct dcn20_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN2_0(0),
		MPC_REG_LIST_DCN2_0(1),
		MPC_REG_LIST_DCN2_0(2),
		MPC_REG_LIST_DCN2_0(3),
		MPC_REG_LIST_DCN2_0(4),
		MPC_REG_LIST_DCN2_0(5),
		MPC_OUT_MUX_REG_LIST_DCN2_0(0),
		MPC_OUT_MUX_REG_LIST_DCN2_0(1),
		MPC_OUT_MUX_REG_LIST_DCN2_0(2),
		MPC_OUT_MUX_REG_LIST_DCN2_0(3),
		MPC_DBG_REG_LIST_DCN2_0()
};

static const struct dcn20_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT),
	MPC_DEBUG_REG_LIST_SH_DCN20
};

static const struct dcn20_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN2_0(_MASK),
	MPC_DEBUG_REG_LIST_MASK_DCN20
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN21(id)\
}

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3)
};

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN21(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN21(_MASK)
};

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN21()
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN21(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN21(_MASK)
};


#define vmid_regs(id)\
[id] = {\
		DCN20_VMID_REG_LIST(id)\
}

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

#define dsc_regsDCN20(id)\
[id] = {\
	DSC_REG_LIST_DCN20(id)\
}

static const struct dcn20_dsc_registers dsc_regs[] = {
	dsc_regsDCN20(0),
	dsc_regsDCN20(1),
	dsc_regsDCN20(2),
	dsc_regsDCN20(3),
	dsc_regsDCN20(4),
	dsc_regsDCN20(5)
};

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

#define ipp_regs(id)\
[id] = {\
	IPP_REG_LIST_DCN20(id),\
}

static const struct dcn10_ipp_registers ipp_regs[] = {
	ipp_regs(0),
	ipp_regs(1),
	ipp_regs(2),
	ipp_regs(3),
};

static const struct dcn10_ipp_shift ipp_shift = {
		IPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn10_ipp_mask ipp_mask = {
		IPP_MASK_SH_LIST_DCN20(_MASK),
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN20(id),\
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
		aux_engine_regs(1),
		aux_engine_regs(2),
		aux_engine_regs(3),
		aux_engine_regs(4),
};

#define tf_regs(id)\
[id] = {\
	TF_REG_LIST_DCN20(id),\
	TF_REG_LIST_DCN20_COMMON_APPEND(id),\
}

static const struct dcn2_dpp_registers tf_regs[] = {
	tf_regs(0),
	tf_regs(1),
	tf_regs(2),
	tf_regs(3),
};

static const struct dcn2_dpp_shift tf_shift = {
		TF_REG_LIST_SH_MASK_DCN20(__SHIFT),
		TF_DEBUG_REG_LIST_SH_DCN20
};

static const struct dcn2_dpp_mask tf_mask = {
		TF_REG_LIST_SH_MASK_DCN20(_MASK),
		TF_DEBUG_REG_LIST_MASK_DCN20
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN2_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
};

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN20(_MASK)
};

static void dcn21_pp_smu_destroy(struct pp_smu_funcs **pp_smu);

static struct input_pixel_processor *dcn21_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_ipp *ipp =
		kzalloc(sizeof(struct dcn10_ipp), GFP_KERNEL);

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}

static struct dpp *dcn21_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_dpp *dpp =
		kzalloc(sizeof(struct dcn20_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp2_construct(dpp, ctx, inst,
			&tf_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

static struct dce_aux *dcn21_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst],
					&aux_mask,
					&aux_shift,
					ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}

#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN2(_MASK)
};

static struct dce_i2c_hw *dcn21_i2c_hw_create(struct dc_context *ctx,
					      uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static const struct resource_caps res_cap_rn = {
		.num_timing_generator = 4,
		.num_opp = 4,
		.num_video_plane = 4,
		.num_audio = 4, // 4 audio endpoints.  4 audio streams
		.num_stream_encoder = 5,
		.num_pll = 5,  // maybe 3 because the last two used for USB-c
		.num_dwb = 1,
		.num_ddc = 5,
		.num_vmid = 16,
		.num_dsc = 3,
};

#ifdef DIAGS_BUILD
static const struct resource_caps res_cap_rn_FPGA_4pipe = {
		.num_timing_generator = 4,
		.num_opp = 4,
		.num_video_plane = 4,
		.num_audio = 7,
		.num_stream_encoder = 4,
		.num_pll = 4,
		.num_dwb = 1,
		.num_ddc = 4,
		.num_dsc = 0,
};

static const struct resource_caps res_cap_rn_FPGA_2pipe_dsc = {
		.num_timing_generator = 2,
		.num_opp = 2,
		.num_video_plane = 2,
		.num_audio = 7,
		.num_stream_encoder = 2,
		.num_pll = 4,
		.num_dwb = 1,
		.num_ddc = 4,
		.num_dsc = 2,
};
#endif

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.per_pixel_alpha = true,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = true,
			.fp16 = true,
			.p010 = true
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 16000
	},

	.max_downscale_factor = {
			.argb8888 = 250,
			.nv12 = 250,
			.fp16 = 250
	},
	64,
	64
};

static const struct dc_debug_options debug_defaults_drv = {
		.disable_dmcu = false,
		.force_abm_enable = false,
		.timing_trace = false,
		.clock_trace = true,
		.disable_pplib_clock_request = true,
		.min_disp_clk_khz = 100000,
		.pipe_split_policy = MPC_SPLIT_DYNAMIC,
		.force_single_disp_pipe_split = false,
		.disable_dcc = DCC_ENABLE,
		.vsr_support = true,
		.performance_trace = false,
		.max_downscale_src_width = 4096,
		.disable_pplib_wm_range = false,
		.scl_reset_length10 = true,
		.sanity_checks = true,
		.disable_48mhz_pwrdwn = false,
		.usbc_combo_phy_reset_wa = true,
		.dmub_command_table = true,
		.use_max_lb = true,
		.enable_legacy_fast_update = true,
};

static const struct dc_panel_config panel_config_defaults = {
		.psr = {
			.disable_psr = false,
			.disallow_psrsu = false,
		},
		.ilr = {
			.optimize_edp_link_rate = true,
		},
};

enum dcn20_clk_src_array_id {
	DCN20_CLK_SRC_PLL0,
	DCN20_CLK_SRC_PLL1,
	DCN20_CLK_SRC_PLL2,
	DCN20_CLK_SRC_PLL3,
	DCN20_CLK_SRC_PLL4,
	DCN20_CLK_SRC_TOTAL_DCN21
};

static void dcn21_resource_destruct(struct dcn21_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		if (pool->base.dscs[i] != NULL)
			dcn20_dsc_destroy(&pool->base.dscs[i]);
	}

	if (pool->base.mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}
	if (pool->base.hubbub != NULL) {
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn20_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		if (pool->base.engines[i] != NULL)
			dce110_engine_destroy(&pool->base.engines[i]);
		if (pool->base.hw_i2cs[i] != NULL) {
			kfree(pool->base.hw_i2cs[i]);
			pool->base.hw_i2cs[i] = NULL;
		}
		if (pool->base.sw_i2cs[i] != NULL) {
			kfree(pool->base.sw_i2cs[i]);
			pool->base.sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dwb; i++) {
		if (pool->base.dwbc[i] != NULL) {
			kfree(TO_DCN20_DWBC(pool->base.dwbc[i]));
			pool->base.dwbc[i] = NULL;
		}
		if (pool->base.mcif_wb[i] != NULL) {
			kfree(TO_DCN20_MMHUBBUB(pool->base.mcif_wb[i]));
			pool->base.mcif_wb[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn20_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn20_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	if (pool->base.abm != NULL) {
		if (pool->base.abm->ctx->dc->config.disable_dmcu)
			dmub_abm_destroy(&pool->base.abm);
		else
			dce_abm_destroy(&pool->base.abm);
	}

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);

	if (pool->base.psr != NULL)
		dmub_psr_destroy(&pool->base.psr);

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.pp_smu != NULL)
		dcn21_pp_smu_destroy(&pool->base.pp_smu);
}

bool dcn21_fast_validate_bw(struct dc *dc,
			    struct dc_state *context,
			    display_e2e_pipe_params_st *pipes,
			    int *pipe_cnt_out,
			    int *pipe_split_from,
			    int *vlevel_out,
			    bool fast_validate)
{
	bool out = false;
	int split[MAX_PIPES] = { 0 };
	int pipe_cnt, i, pipe_idx, vlevel;

	ASSERT(pipes);
	if (!pipes)
		return false;

	dcn20_merge_pipes_for_validate(dc, context);

	DC_FP_START();
	pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);
	DC_FP_END();

	*pipe_cnt_out = pipe_cnt;

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}
	/*
	 * DML favors voltage over p-state, but we're more interested in
	 * supporting p-state over voltage. We can't support p-state in
	 * prefetch mode > 0 so try capping the prefetch mode to start.
	 */
	context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
				dm_allow_self_refresh_and_mclk_switch;
	vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (vlevel > context->bw_ctx.dml.soc.num_states) {
		/*
		 * If mode is unsupported or there's still no p-state support then
		 * fall back to favoring voltage.
		 *
		 * We don't actually support prefetch mode 2, so require that we
		 * at least support prefetch mode 1.
		 */
		context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
					dm_allow_self_refresh;
		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		if (vlevel > context->bw_ctx.dml.soc.num_states)
			goto validate_fail;
	}

	vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, NULL);

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *mpo_pipe = pipe->bottom_pipe;
		struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

		if (!pipe->stream)
			continue;

		/* We only support full screen mpo with ODM */
		if (vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled
				&& pipe->plane_state && mpo_pipe
				&& memcmp(&mpo_pipe->plane_res.scl_data.recout,
						&pipe->plane_res.scl_data.recout,
						sizeof(struct rect)) != 0) {
			ASSERT(mpo_pipe->plane_state != pipe->plane_state);
			goto validate_fail;
		}
		pipe_idx++;
	}

	/*initialize pipe_just_split_from to invalid idx*/
	for (i = 0; i < MAX_PIPES; i++)
		pipe_split_from[i] = -1;

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = pipe->bottom_pipe;

		if (!pipe->stream || pipe_split_from[i] >= 0)
			continue;

		pipe_idx++;

		if (!pipe->top_pipe && !pipe->plane_state && context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx]) {
			hsplit_pipe = dcn20_find_secondary_pipe(dc, &context->res_ctx, dc->res_pool, pipe);
			ASSERT(hsplit_pipe);
			if (!dcn20_split_stream_for_odm(
					dc, &context->res_ctx,
					pipe, hsplit_pipe))
				goto validate_fail;
			pipe_split_from[hsplit_pipe->pipe_idx] = pipe_idx;
			dcn20_build_mapped_resource(dc, context, pipe->stream);
		}

		if (!pipe->plane_state)
			continue;
		/* Skip 2nd half of already split pipe */
		if (pipe->top_pipe && pipe->plane_state == pipe->top_pipe->plane_state)
			continue;

		if (split[i] == 2) {
			if (!hsplit_pipe || hsplit_pipe->plane_state != pipe->plane_state) {
				/* pipe not split previously needs split */
				hsplit_pipe = dcn20_find_secondary_pipe(dc, &context->res_ctx, dc->res_pool, pipe);
				ASSERT(hsplit_pipe);
				if (!hsplit_pipe) {
					DC_FP_START();
					dcn20_fpu_adjust_dppclk(&context->bw_ctx.dml.vba, vlevel, context->bw_ctx.dml.vba.maxMpcComb, pipe_idx, true);
					DC_FP_END();
					continue;
				}
				if (context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx]) {
					if (!dcn20_split_stream_for_odm(
							dc, &context->res_ctx,
							pipe, hsplit_pipe))
						goto validate_fail;
					dcn20_build_mapped_resource(dc, context, pipe->stream);
				} else {
					dcn20_split_stream_for_mpc(
							&context->res_ctx, dc->res_pool,
							pipe, hsplit_pipe);
					resource_build_scaling_params(pipe);
					resource_build_scaling_params(hsplit_pipe);
				}
				pipe_split_from[hsplit_pipe->pipe_idx] = pipe_idx;
			}
		} else if (hsplit_pipe && hsplit_pipe->plane_state == pipe->plane_state) {
			/* merge should already have been done */
			ASSERT(0);
		}
	}
	/* Actual dsc count per stream dsc validation*/
	if (!dcn20_validate_dsc(dc, context)) {
		context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states] =
				DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	*vlevel_out = vlevel;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}

/*
 * Some of the functions further below use the FPU, so we need to wrap this
 * with DC_FP_START()/DC_FP_END(). Use the same approach as for
 * dcn20_validate_bandwidth in dcn20_resource.c.
 */
static bool dcn21_validate_bandwidth(struct dc *dc, struct dc_state *context,
		bool fast_validate)
{
	bool voltage_supported;
	DC_FP_START();
	voltage_supported = dcn21_validate_bandwidth_fp(dc, context, fast_validate);
	DC_FP_END();
	return voltage_supported;
}

static void dcn21_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn21_resource_pool *dcn21_pool = TO_DCN21_RES_POOL(*pool);

	dcn21_resource_destruct(dcn21_pool);
	kfree(dcn21_pool);
	*pool = NULL;
}

static struct clock_source *dcn21_clock_source_create(
		struct dc_context *ctx,
		struct dc_bios *bios,
		enum clock_source_id id,
		const struct dce110_clk_src_regs *regs,
		bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn20_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static struct hubp *dcn21_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn21_hubp *hubp21 =
		kzalloc(sizeof(struct dcn21_hubp), GFP_KERNEL);

	if (!hubp21)
		return NULL;

	if (hubp21_construct(hubp21, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp21->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp21);
	return NULL;
}

static struct hubbub *dcn21_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub)
		return NULL;

	hubbub21_construct(hubbub, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask);

	for (i = 0; i < res_cap_rn.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}
	hubbub->num_vmid = res_cap_rn.num_vmid;

	return &hubbub->base;
}

static struct output_pixel_processor *dcn21_opp_create(struct dc_context *ctx,
						       uint32_t inst)
{
	struct dcn20_opp *opp =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

static struct timing_generator *dcn21_timing_generator_create(struct dc_context *ctx,
							      uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &tg_regs[instance];
	tgn10->tg_shift = &tg_shift;
	tgn10->tg_mask = &tg_mask;

	dcn20_timing_generator_init(tgn10);

	return &tgn10->base;
}

static struct mpc *dcn21_mpc_create(struct dc_context *ctx)
{
	struct dcn20_mpc *mpc20 = kzalloc(sizeof(struct dcn20_mpc),
					  GFP_KERNEL);

	if (!mpc20)
		return NULL;

	dcn20_mpc_construct(mpc20, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			6);

	return &mpc20->base;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}


static struct display_stream_compressor *dcn21_dsc_create(struct dc_context *ctx,
							  uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

static struct pp_smu_funcs *dcn21_pp_smu_create(struct dc_context *ctx)
{
	struct pp_smu_funcs *pp_smu = kzalloc(sizeof(*pp_smu), GFP_KERNEL);

	if (!pp_smu)
		return pp_smu;

	dm_pp_get_funcs(ctx, pp_smu);

	if (pp_smu->ctx.ver != PP_SMU_VER_RN)
		pp_smu = memset(pp_smu, 0, sizeof(struct pp_smu_funcs));


	return pp_smu;
}

static void dcn21_pp_smu_destroy(struct pp_smu_funcs **pp_smu)
{
	if (pp_smu && *pp_smu) {
		kfree(*pp_smu);
		*pp_smu = NULL;
	}
}

static struct audio *dcn21_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

static struct stream_encoder *dcn21_stream_encoder_create(enum engine_id eng_id,
							  struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1 =
		kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);

	if (!enc1)
		return NULL;

	dcn20_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN21_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN21_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN21_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn21_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
		hws->wa.DEGVIDCN21 = true;
		hws->wa.disallow_self_refresh_during_multi_plane_transition = true;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = dcn21_create_audio,
	.create_stream_encoder = dcn21_stream_encoder_create,
	.create_hwseq = dcn21_hwseq_create,
};

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
	LE_DCN2_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid), \
	DPCS_DCN21_REG_LIST(id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
	link_regs(2, C),
	link_regs(3, D),
	link_regs(4, E),
};

static const struct dce_panel_cntl_registers panel_cntl_regs[] = {
	{ DCN_PANEL_CNTL_REG_LIST() }
};

static const struct dce_panel_cntl_shift panel_cntl_shift = {
	DCE_PANEL_CNTL_MASK_SH_LIST(__SHIFT)
};

static const struct dce_panel_cntl_mask panel_cntl_mask = {
	DCE_PANEL_CNTL_MASK_SH_LIST(_MASK)
};

#define aux_regs(id)\
[id] = {\
	DCN2_AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4)
};

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN20(__SHIFT),\
	DPCS_DCN21_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN20(_MASK),\
	DPCS_DCN21_MASK_SH_LIST(_MASK)
};

static int map_transmitter_id_to_phy_instance(
	enum transmitter transmitter)
{
	switch (transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return 0;
	break;
	case TRANSMITTER_UNIPHY_B:
		return 1;
	break;
	case TRANSMITTER_UNIPHY_C:
		return 2;
	break;
	case TRANSMITTER_UNIPHY_D:
		return 3;
	break;
	case TRANSMITTER_UNIPHY_E:
		return 4;
	break;
	default:
		ASSERT(0);
		return 0;
	}
}

static struct link_encoder *dcn21_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn21_link_encoder *enc21 =
		kzalloc(sizeof(struct dcn21_link_encoder), GFP_KERNEL);
	int link_regs_id;

	if (!enc21)
		return NULL;

	link_regs_id =
		map_transmitter_id_to_phy_instance(enc_init_data->transmitter);

	dcn21_link_encoder_construct(enc21,
				      enc_init_data,
				      &link_enc_feature,
				      &link_enc_regs[link_regs_id],
				      &link_enc_aux_regs[enc_init_data->channel - 1],
				      &link_enc_hpd_regs[enc_init_data->hpd_source],
				      &le_shift,
				      &le_mask);

	return &enc21->enc10.base;
}

static struct panel_cntl *dcn21_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dce_panel_cntl *panel_cntl =
		kzalloc(sizeof(struct dce_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dce_panel_cntl_construct(panel_cntl,
			init_data,
			&panel_cntl_regs[init_data->inst],
			&panel_cntl_shift,
			&panel_cntl_mask);

	return &panel_cntl->base;
}

static void dcn21_get_panel_config_defaults(struct dc_panel_config *panel_config)
{
	*panel_config = panel_config_defaults;
}

#define CTX ctx

#define REG(reg_name) \
	(DCN_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = REG_READ(CC_DC_PIPE_DIS);
	/* RV1 support max 4 pipes */
	value = value & 0xf;
	return value;
}

static enum dc_status dcn21_patch_unknown_plane_state(struct dc_plane_state *plane_state)
{
	if (plane_state->ctx->dc->debug.disable_dcc == DCC_ENABLE) {
		plane_state->dcc.enable = 1;
		/* align to our worst case block width */
		plane_state->dcc.meta_pitch = ((plane_state->src_rect.width + 1023) / 1024) * 1024;
	}

	return dcn20_patch_unknown_plane_state(plane_state);
}

static const struct resource_funcs dcn21_res_pool_funcs = {
	.destroy = dcn21_destroy_resource_pool,
	.link_enc_create = dcn21_link_encoder_create,
	.panel_cntl_create = dcn21_panel_cntl_create,
	.validate_bandwidth = dcn21_validate_bandwidth,
	.populate_dml_pipes = dcn21_populate_dml_pipes_from_context,
	.add_stream_to_ctx = dcn20_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.acquire_idle_pipe_for_layer = dcn20_acquire_idle_pipe_for_layer,
	.populate_dml_writeback_from_context = dcn20_populate_dml_writeback_from_context,
	.patch_unknown_plane_state = dcn21_patch_unknown_plane_state,
	.set_mcif_arb_params = dcn20_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.update_bw_bounding_box = dcn21_update_bw_bounding_box,
	.get_panel_config_defaults = dcn21_get_panel_config_defaults,
};

static bool dcn21_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn21_resource_pool *pool)
{
	int i, j;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	uint32_t pipe_fuses = read_pipe_fuses(ctx);
	uint32_t num_pipes;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_rn;
#ifdef DIAGS_BUILD
	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		//pool->base.res_cap = &res_cap_nv10_FPGA_2pipe_dsc;
		pool->base.res_cap = &res_cap_rn_FPGA_4pipe;
#endif

	pool->base.funcs = &dcn21_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	/* max pipe num for ASIC before check pipe fuses */
	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;

	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 5; /*1.4 w/a applied by default*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;

	dc->caps.max_slave_planes = 1;
	dc->caps.max_slave_yuv_planes = 1;
	dc->caps.max_slave_rgb_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;
	dc->caps.is_apu = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 1;
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.dgam_rom_caps.pq = 0;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 0;
	dc->caps.color.dpp.post_csc = 0;
	dc->caps.color.dpp.gamma_corr = 0;
	dc->caps.color.dpp.dgam_rom_for_yuv = 1;

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN2
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 0;
	dc->caps.color.mpc.num_3dluts = 0;
	dc->caps.color.mpc.shared_3d_lut = 0;
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	dc->caps.dp_hdmi21_pcon_support = true;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCN20_CLK_SRC_PLL0] =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL1] =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL2] =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL3] =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL4] =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);

	pool->base.clk_src_count = DCN20_CLK_SRC_TOTAL_DCN21;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn21_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	pool->base.dccg = dccg21_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	if (!dc->config.disable_dmcu) {
		pool->base.dmcu = dcn21_dmcu_create(ctx,
				&dmcu_regs,
				&dmcu_shift,
				&dmcu_mask);
		if (pool->base.dmcu == NULL) {
			dm_error("DC: failed to create dmcu!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}

		dc->debug.dmub_command_table = false;
	}

	if (dc->config.disable_dmcu) {
		pool->base.psr = dmub_psr_create(ctx);

		if (pool->base.psr == NULL) {
			dm_error("DC: failed to create psr obj!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	if (dc->config.disable_dmcu)
		pool->base.abm = dmub_abm_create(ctx,
			&abm_regs,
			&abm_shift,
			&abm_mask);
	else
		pool->base.abm = dce_abm_create(ctx,
			&abm_regs,
			&abm_shift,
			&abm_mask);

	pool->base.pp_smu = dcn21_pp_smu_create(ctx);

	num_pipes = dcn2_1_ip.max_num_dpp;

	for (i = 0; i < dcn2_1_ip.max_num_dpp; i++)
		if (pipe_fuses & 1 << i)
			num_pipes--;
	dcn2_1_ip.max_num_dpp = num_pipes;
	dcn2_1_ip.max_num_otg = num_pipes;

	dml_init_instance(&dc->dml, &dcn2_1_soc, &dcn2_1_ip, DML_PROJECT_DCN21);

	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn21_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	j = 0;
	/* mem input -> ipp -> dpp -> opp -> TG */
	for (i = 0; i < pool->base.pipe_count; i++) {
		/* if pipe is disabled, skip instance of HW pipe,
		 * i.e, skip ASIC register instance
		 */
		if ((pipe_fuses & (1 << i)) != 0)
			continue;

		pool->base.hubps[j] = dcn21_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto create_fail;
		}

		pool->base.ipps[j] = dcn21_ipp_create(ctx, i);
		if (pool->base.ipps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto create_fail;
		}

		pool->base.dpps[j] = dcn21_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}

		pool->base.opps[j] = dcn21_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}

		pool->base.timing_generators[j] = dcn21_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
		j++;
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn21_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn21_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	pool->base.timing_generator_count = j;
	pool->base.pipe_count = j;
	pool->base.mpcc_count = j;

	pool->base.mpc = dcn21_mpc_create(ctx);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	pool->base.hubbub = dcn21_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn21_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	if (!dcn20_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}
	if (!dcn20_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			&res_create_funcs))
		goto create_fail;

	dcn21_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	return true;

create_fail:

	dcn21_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn21_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn21_resource_pool *pool =
		kzalloc(sizeof(struct dcn21_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn21_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
