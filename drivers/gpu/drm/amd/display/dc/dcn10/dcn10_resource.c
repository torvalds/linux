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

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn10/dcn10_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn10/dcn10_mpc.h"
#include "irq/dcn10/irq_service_dcn10.h"
#include "dcn10/dcn10_transform.h"
#include "dcn10/dcn10_timing_generator.h"
#include "dcn10/dcn10_hw_sequencer.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn10/dcn10_opp.h"
#include "dce/dce_link_encoder.h"
#include "dce/dce_stream_encoder.h"
#include "dce/dce_clocks.h"
#include "dce/dce_clock_source.h"
#include "dcn10/dcn10_mem_input.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "../virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"

#include "vega10/soc15ip.h"

#include "raven1/DCN/dcn_1_0_offset.h"
#include "raven1/DCN/dcn_1_0_sh_mask.h"

#include "raven1/NBIO/nbio_7_0_offset.h"

#include "raven1/MMHUB/mmhub_9_1_offset.h"
#include "raven1/MMHUB/mmhub_9_1_sh_mask.h"

#include "reg_helper.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"

#ifndef mmDP0_DP_DPHY_INTERNAL_CTRL
	#define mmDP0_DP_DPHY_INTERNAL_CTRL		0x210f
	#define mmDP0_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP1_DP_DPHY_INTERNAL_CTRL		0x220f
	#define mmDP1_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP2_DP_DPHY_INTERNAL_CTRL		0x230f
	#define mmDP2_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP3_DP_DPHY_INTERNAL_CTRL		0x240f
	#define mmDP3_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP4_DP_DPHY_INTERNAL_CTRL		0x250f
	#define mmDP4_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP5_DP_DPHY_INTERNAL_CTRL		0x260f
	#define mmDP5_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP6_DP_DPHY_INTERNAL_CTRL		0x270f
	#define mmDP6_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
#endif


enum dcn10_clk_src_array_id {
	DCN10_CLK_SRC_PLL0,
	DCN10_CLK_SRC_PLL1,
	DCN10_CLK_SRC_PLL2,
	DCN10_CLK_SRC_PLL3,
	DCN10_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define BASE_INNER(seg) \
	DCE_BASE__INST0_SEG ## seg

#define BASE(seg) \
	BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name


#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIF_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

/* GC */
#define GC_BASE_INNER(seg) \
	GC_BASE__INST0_SEG ## seg

#define GC_BASE(seg) \
	GC_BASE_INNER(seg)

#define GC_SR(reg_name)\
		.reg_name = GC_BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

/* macros to expend register list macro defined in HW object header file
 * end *********************/

static const struct dce_disp_clk_registers disp_clk_regs = {
		CLK_DCN10_REG_LIST()
};

static const struct dce_disp_clk_shift disp_clk_shift = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce_disp_clk_mask disp_clk_mask = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

static const struct dce_dmcu_registers dmcu_regs = {
		DMCU_DCN10_REG_LIST()
};

static const struct dce_dmcu_shift dmcu_shift = {
		DMCU_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_dmcu_mask dmcu_mask = {
		DMCU_MASK_SH_LIST_DCN10(_MASK)
};

static const struct dce_abm_registers abm_regs = {
		ABM_DCN10_REG_LIST(0)
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN10(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN_REG_LIST(id),\
	.TMDS_CNTL = 0,\
	.AFMT_AVI_INFO0 = 0,\
	.AFMT_AVI_INFO1 = 0,\
	.AFMT_AVI_INFO2 = 0,\
	.AFMT_AVI_INFO3 = 0,\
}

static const struct dce110_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
};

static const struct dce_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN10(_MASK),
		.AFMT_GENERIC0_UPDATE = 0,
		.AFMT_GENERIC2_UPDATE = 0,
		.DP_DYN_RANGE = 0,
		.DP_YCBCR_RANGE = 0,
		.HDMI_AVI_INFO_SEND = 0,
		.HDMI_AVI_INFO_CONT = 0,
		.HDMI_AVI_INFO_LINE = 0,
		.DP_SEC_AVI_ENABLE = 0,
		.AFMT_AVI_INFO_VERSION = 0
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
};

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_aduio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define aux_regs(id)\
[id] = {\
	AUX_REG_LIST(id)\
}

static const struct dce110_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4),
		aux_regs(5)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dce110_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4),
		hpd_regs(5)
};

#define link_regs(id)\
[id] = {\
	LE_DCN10_REG_LIST(id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
}

static const struct dce110_link_enc_registers link_enc_regs[] = {
	link_regs(0),
	link_regs(1),
	link_regs(2),
	link_regs(3),
	link_regs(4),
	link_regs(5),
	link_regs(6),
};

#define ipp_regs(id)\
[id] = {\
	IPP_DCN10_REG_LIST(id),\
}

static const struct dcn10_ipp_registers ipp_regs[] = {
	ipp_regs(0),
	ipp_regs(1),
	ipp_regs(2),
	ipp_regs(3),
};

static const struct dcn10_ipp_shift ipp_shift = {
		IPP_DCN10_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_ipp_mask ipp_mask = {
		IPP_DCN10_MASK_SH_LIST(_MASK),
};

#define opp_regs(id)\
[id] = {\
	OPP_DCN10_REG_LIST(id),\
}

static const struct dcn10_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
};

static const struct dcn10_opp_shift opp_shift = {
		OPP_DCN10_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_opp_mask opp_mask = {
		OPP_DCN10_MASK_SH_LIST(_MASK),
};

#define tf_regs(id)\
[id] = {\
	TF_REG_LIST_DCN(id),\
}

static const struct dcn_transform_registers tf_regs[] = {
	tf_regs(0),
	tf_regs(1),
	tf_regs(2),
	tf_regs(3),
};

static const struct dcn_transform_shift tf_shift = {
		TF_REG_LIST_SH_MASK_DCN(__SHIFT)
};

static const struct dcn_transform_mask tf_mask = {
		TF_REG_LIST_SH_MASK_DCN(_MASK),
};


static const struct dcn_mpc_registers mpc_regs = {
	MPC_COMMON_REG_LIST_DCN1_0(0),
	MPC_COMMON_REG_LIST_DCN1_0(1),
	MPC_COMMON_REG_LIST_DCN1_0(2),
	MPC_COMMON_REG_LIST_DCN1_0(3),
};

static const struct dcn_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT)
};

static const struct dcn_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN1_0(_MASK),
};

#define tg_regs(id)\
[id] = {TG_COMMON_REG_LIST_DCN1_0(id)}

static const struct dcn_tg_registers tg_regs[] = {
	tg_regs(0),
	tg_regs(1),
	tg_regs(2),
	tg_regs(3),
};

static const struct dcn_tg_shift tg_shift = {
	TG_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT)
};

static const struct dcn_tg_mask tg_mask = {
	TG_COMMON_MASK_SH_LIST_DCN1_0(_MASK)
};


static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_6)
};

#define mi_regs(id)\
[id] = {\
	MI_DCN10_REG_LIST(id)\
}


static const struct dcn_mi_registers mi_regs[] = {
	mi_regs(0),
	mi_regs(1),
	mi_regs(2),
	mi_regs(3),
};

static const struct dcn_mi_shift mi_shift = {
		MI_DCN10_MASK_SH_LIST(__SHIFT)
};

static const struct dcn_mi_mask mi_mask = {
		MI_DCN10_MASK_SH_LIST(_MASK)
};

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN1_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN1_0(_MASK)
};


static const struct resource_caps res_cap = {
		.num_timing_generator = 4,
		.num_video_plane = 4,
		.num_audio = 4,
		.num_stream_encoder = 4,
		.num_pll = 4,
};

static const struct dc_debug debug_defaults_drv = {
		.disable_dcc = false,
		.disable_dpp_power_gate = false,
		.disable_hubp_power_gate = false,
		.disable_dmcu = true,
		.force_abm_enable = false,
		.timing_trace = false,
		.disable_pplib_clock_request = true,
		.disable_pplib_wm_range = true,
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
		.use_dml_wm = false,
		.use_max_voltage = true
#endif
};

static const struct dc_debug debug_defaults_diags = {
		.disable_dpp_power_gate = false,
		.disable_hubp_power_gate = false,
		.disable_clock_gate = true,
		.disable_dmcu = true,
		.force_abm_enable = false,
		.timing_trace = true,
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
		.disable_pplib_clock_request = true,
		.disable_pplib_wm_range = true,
		.use_dml_wm = false,
		.use_max_voltage = false
#endif
};

static void dcn10_transform_destroy(struct transform **xfm)
{
	dm_free(TO_DCN10_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dcn10_transform_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn10_transform *transform =
		dm_alloc(sizeof(struct dcn10_transform));

	if (!transform)
		return NULL;

	if (dcn10_transform_construct(transform, ctx,
			&tf_regs[inst], &tf_shift, &tf_mask))
		return &transform->base;

	BREAK_TO_DEBUGGER();
	dm_free(transform);
	return NULL;
}

static struct input_pixel_processor *dcn10_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_ipp *ipp =
		dm_alloc(sizeof(struct dcn10_ipp));

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn10_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}


static struct output_pixel_processor *dcn10_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_opp *opp =
		dm_alloc(sizeof(struct dcn10_opp));

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn10_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

static struct mpc *dcn10_mpc_create(
	struct dc_context *ctx)
{
	struct dcn10_mpc *mpc = dm_alloc(sizeof(struct dcn10_mpc));

	if (!mpc)
		return NULL;

	mpc->base.ctx = ctx;
	mpc->mpc_regs = &mpc_regs;
	mpc->mpc_shift = &mpc_shift;
	mpc->mpc_mask = &mpc_mask;

	return &mpc->base;
}

static void dcn10_mpc_destroy(struct mpc **mpc_base)
{
	if (*mpc_base)
		dm_free(TO_DCN10_MPC(*mpc_base));

	*mpc_base = NULL;
}

static struct timing_generator *dcn10_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct dcn10_timing_generator *tgn10 =
		dm_alloc(sizeof(struct dcn10_timing_generator));

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &tg_regs[instance];
	tgn10->tg_shift = &tg_shift;
	tgn10->tg_mask = &tg_mask;

	dcn10_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.ycbcr420_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true,
		.flags.bits.IS_YCBCR_CAPABLE = true
};

struct link_encoder *dcn10_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_link_encoder));

	if (!enc110)
		return NULL;

	if (dce110_link_encoder_construct(
			enc110,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source])) {

		return &enc110->base;
	}

	BREAK_TO_DEBUGGER();
	dm_free(enc110);
	return NULL;
}

struct clock_source *dcn10_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		dm_alloc(sizeof(struct dce110_clk_src));

	if (!clk_src)
		return NULL;

	if (dce110_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	BREAK_TO_DEBUGGER();
	return NULL;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	/* TODO: Registers are missing */
	/*REG_GET_2(CC_DC_HDMI_STRAPS,
			HDMI_DISABLE, &straps->hdmi_disable,
			AUDIO_STREAM_NUMBER, &straps->audio_stream_number);

	REG_GET(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO, &straps->dc_pinstraps_audio);*/
}

static struct audio *create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct stream_encoder *dcn10_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dce110_stream_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_stream_encoder));

	if (!enc110)
		return NULL;

	if (dce110_stream_encoder_construct(
			enc110, ctx, ctx->dc_bios, eng_id,
			&stream_enc_regs[eng_id], &se_shift, &se_mask))
		return &enc110->base;

	BREAK_TO_DEBUGGER();
	dm_free(enc110);
	return NULL;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN1_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN1_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN1_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn10_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = dm_alloc(sizeof(struct dce_hwseq));

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dcn10_stream_encoder_create,
	.create_hwseq = dcn10_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hwseq = dcn10_hwseq_create,
};

void dcn10_clock_source_destroy(struct clock_source **clk_src)
{
	dm_free(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

static void destruct(struct dcn10_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			/* TODO: free dcn version of stream encoder once implemented
			 * rather than using virtual stream encoder
			 */
			dm_free(pool->base.stream_enc[i]);
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);

		if (pool->base.transforms[i] != NULL)
			dcn10_transform_destroy(&pool->base.transforms[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.mis[i] != NULL) {
			dm_free(TO_DCN10_MEM_INPUT(pool->base.mis[i]));
			pool->base.mis[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}

		if (pool->base.timing_generators[i] != NULL)	{
			dm_free(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL)
		dm_free(DCE110STRENC_FROM_STRENC(pool->base.stream_enc[i]));
	}

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn10_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn10_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	if (pool->base.mpc != NULL)
		dcn10_mpc_destroy(&pool->base.mpc);

	if (pool->base.abm != NULL)
		dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);

	if (pool->base.display_clock != NULL)
		dce_disp_clk_destroy(&pool->base.display_clock);
}

static struct mem_input *dcn10_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn10_mem_input *mem_inputn10 =
		dm_alloc(sizeof(struct dcn10_mem_input));

	if (!mem_inputn10)
		return NULL;

	if (dcn10_mem_input_construct(mem_inputn10, ctx, inst,
			&mi_regs[inst], &mi_shift, &mi_mask))
		return &mem_inputn10->base;

	BREAK_TO_DEBUGGER();
	dm_free(mem_inputn10);
	return NULL;
}

static void get_pixel_clock_parameters(
	const struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct core_stream *stream = pipe_ctx->stream;
	pixel_clk_params->requested_pix_clk = stream->public.timing.pix_clk_khz;
	pixel_clk_params->encoder_object_id = stream->sink->link->link_enc->id;
	pixel_clk_params->signal_type = pipe_ctx->stream->signal;
	pixel_clk_params->controller_id = pipe_ctx->pipe_idx + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->public.timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
	pixel_clk_params->pixel_encoding = stream->public.timing.pixel_encoding;

	if (stream->public.timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
		pixel_clk_params->color_depth = COLOR_DEPTH_888;

	if (stream->public.timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pixel_clk_params->requested_pix_clk  /= 2;

	if (stream->public.timing. timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING ||
		stream->public.timing. timing_3d_format == TIMING_3D_FORMAT_SW_FRAME_PACKING ||
		stream->public.timing. timing_3d_format == TIMING_3D_FORMAT_DP_HDMI_INBAND_FA)
		pixel_clk_params->requested_pix_clk *= 2;
}

static void build_clamping_params(struct core_stream *stream)
{
	stream->clamping.clamping_level = CLAMPING_FULL_RANGE;
	stream->clamping.c_depth = stream->public.timing.display_color_depth;
	stream->clamping.pixel_encoding = stream->public.timing.pixel_encoding;
}

static enum dc_status build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
{

	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->pix_clk_params);

	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
		pipe_ctx->clock_source,
		&pipe_ctx->pix_clk_params,
		&pipe_ctx->pll_settings);

	pipe_ctx->stream->clamping.pixel_encoding = pipe_ctx->stream->public.timing.pixel_encoding;

	resource_build_bit_depth_reduction_params(pipe_ctx->stream,
					&pipe_ctx->stream->bit_depth_params);
	build_clamping_params(pipe_ctx->stream);

	return DC_OK;
}

static enum dc_status validate_mapped_resource(
		const struct core_dc *dc,
		struct validate_context *context)
{
	enum dc_status status = DC_OK;
	uint8_t i, j;

	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream = context->streams[i];
		struct core_link *link = stream->sink->link;

		if (resource_is_stream_unchanged(dc->current_context, stream)) {
			if (stream != NULL && dc->current_context->streams[i] != NULL) {
				/* todo: shouldn't have to copy missing parameter here */
				resource_build_bit_depth_reduction_params(stream,
						&stream->bit_depth_params);
				stream->clamping.pixel_encoding =
						stream->public.timing.pixel_encoding;

				resource_build_bit_depth_reduction_params(stream,
								&stream->bit_depth_params);
				build_clamping_params(stream);

				continue;
			}
		}

		for (j = 0; j < dc->res_pool->pipe_count ; j++) {
			struct pipe_ctx *pipe_ctx =
				&context->res_ctx.pipe_ctx[j];

			if (context->res_ctx.pipe_ctx[j].stream != stream)
				continue;


			if (!pipe_ctx->tg->funcs->validate_timing(
					pipe_ctx->tg, &stream->public.timing))
				return DC_FAIL_CONTROLLER_VALIDATE;

			status = build_pipe_hw_param(pipe_ctx);

			if (status != DC_OK)
				return status;

			if (!link->link_enc->funcs->validate_output_with_stream(
				link->link_enc, pipe_ctx))
				return DC_FAIL_ENC_VALIDATE;

			/* TODO: validate audio ASIC caps, encoder */

			status = dc_link_validate_mode_timing(
				stream, link, &stream->public.timing);

			if (status != DC_OK)
				return status;


			/* do not need to validate non root pipes */
			break;
		}
	}

	return DC_OK;
}

enum dc_status dcn10_validate_with_context(
		const struct core_dc *dc,
		const struct dc_validation_set set[],
		int set_count,
		struct validate_context *context)
{
	enum dc_status result = DC_OK;
	int i;

	if (set_count == 0)
		return result;

	for (i = 0; i < set_count; i++) {
		context->streams[i] = DC_STREAM_TO_CORE(set[i].stream);
		dc_stream_retain(&context->streams[i]->public);
		context->stream_count++;
	}

	result = resource_map_pool_resources(dc, context);
	if (result != DC_OK)
		return result;

	result = resource_map_phy_clock_resources(dc, context);
	if (result != DC_OK)
		return result;

	result = validate_mapped_resource(dc, context);
	if (result != DC_OK)
		return result;

	if (!resource_validate_attach_surfaces(set, set_count,
			dc->current_context, context, dc->res_pool))
		return DC_FAIL_ATTACH_SURFACES;

	result = resource_build_scaling_params_for_context(dc, context);
	if (result != DC_OK)
		return result;

	if (!dcn_validate_bandwidth(dc, context))
		return DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

enum dc_status dcn10_validate_guaranteed(
		const struct core_dc *dc,
		const struct dc_stream *dc_stream,
		struct validate_context *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;

	context->streams[0] = DC_STREAM_TO_CORE(dc_stream);
	dc_stream_retain(&context->streams[0]->public);
	context->stream_count++;

	result = resource_map_pool_resources(dc, context);

	if (result == DC_OK)
		result = resource_map_phy_clock_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK) {
		validate_guaranteed_copy_streams(
				context, dc->public.caps.max_streams);
		result = resource_build_scaling_params_for_context(dc, context);
	}
	if (result == DC_OK && !dcn_validate_bandwidth(dc, context))
		return DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

static struct pipe_ctx *dcn10_acquire_idle_pipe_for_layer(
		struct validate_context *context,
		const struct resource_pool *pool,
		struct core_stream *stream)
{
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);
	struct pipe_ctx *idle_pipe = find_idle_secondary_pipe(res_ctx, pool);

	if (!head_pipe)
		ASSERT(0);

	if (!idle_pipe)
		return false;

	idle_pipe->stream = head_pipe->stream;
	idle_pipe->tg = head_pipe->tg;

	idle_pipe->mi = pool->mis[idle_pipe->pipe_idx];
	idle_pipe->ipp = pool->ipps[idle_pipe->pipe_idx];
	idle_pipe->xfm = pool->transforms[idle_pipe->pipe_idx];
	idle_pipe->opp = pool->opps[idle_pipe->pipe_idx];

	return idle_pipe;
}

enum dcc_control {
	dcc_control__256_256_xxx,
	dcc_control__128_128_xxx,
	dcc_control__256_64_64,
};

enum segment_order {
	segment_order__na,
	segment_order__contiguous,
	segment_order__non_contiguous,
};

static bool dcc_support_pixel_format(
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
		*bytes_per_element = 4;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		*bytes_per_element = 8;
		return true;
	default:
		return false;
	}
}

static bool dcc_support_swizzle(
		enum swizzle_mode_values swizzle,
		unsigned int bytes_per_element,
		enum segment_order *segment_order_horz,
		enum segment_order *segment_order_vert)
{
	bool standard_swizzle = false;
	bool display_swizzle = false;

	switch (swizzle) {
	case DC_SW_4KB_S:
	case DC_SW_64KB_S:
	case DC_SW_VAR_S:
	case DC_SW_4KB_S_X:
	case DC_SW_64KB_S_X:
	case DC_SW_VAR_S_X:
		standard_swizzle = true;
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

	if (bytes_per_element == 1 && standard_swizzle) {
		*segment_order_horz = segment_order__contiguous;
		*segment_order_vert = segment_order__na;
		return true;
	}
	if (bytes_per_element == 2 && standard_swizzle) {
		*segment_order_horz = segment_order__non_contiguous;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 4 && standard_swizzle) {
		*segment_order_horz = segment_order__non_contiguous;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 8 && standard_swizzle) {
		*segment_order_horz = segment_order__na;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 8 && display_swizzle) {
		*segment_order_horz = segment_order__contiguous;
		*segment_order_vert = segment_order__non_contiguous;
		return true;
	}

	return false;
}

static void get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
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

static void det_request_size(
		unsigned int height,
		unsigned int width,
		unsigned int bpe,
		bool *req128_horz_wc,
		bool *req128_vert_wc)
{
	unsigned int detile_buf_size = 164 * 1024;  /* 164KB for DCN1.0 */

	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int swath_bytes_horz_wc, swath_bytes_vert_wc;

	get_blk256_size(&blk256_width, &blk256_height, bpe);

	swath_bytes_horz_wc = height * blk256_height * bpe;
	swath_bytes_vert_wc = width * blk256_width * bpe;

	*req128_horz_wc = (2 * swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*req128_vert_wc = (2 * swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */
}

static bool get_dcc_compression_cap(const struct dc *dc,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	/* implement section 1.6.2.1 of DCN1_Programming_Guide.docx */
	enum dcc_control dcc_control;
	unsigned int bpe;
	enum segment_order segment_order_horz, segment_order_vert;
	bool req128_horz_wc, req128_vert_wc;

	memset(output, 0, sizeof(*output));

	if (dc->debug.disable_dcc)
		return false;

	if (!dcc_support_pixel_format(input->format,
			&bpe))
		return false;

	if (!dcc_support_swizzle(input->swizzle_mode, bpe,
			&segment_order_horz, &segment_order_vert))
		return false;

	det_request_size(input->surface_size.height,  input->surface_size.width,
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
	}
	output->capable = true;
	output->const_color_support = false;

	return true;
}


static void dcn10_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn10_resource_pool *dcn10_pool = TO_DCN10_RES_POOL(*pool);

	destruct(dcn10_pool);
	dm_free(dcn10_pool);
	*pool = NULL;
}


static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = get_dcc_compression_cap
};

static struct resource_funcs dcn10_res_pool_funcs = {
	.destroy = dcn10_destroy_resource_pool,
	.link_enc_create = dcn10_link_encoder_create,
	.validate_with_context = dcn10_validate_with_context,
	.validate_guaranteed = dcn10_validate_guaranteed,
	.validate_bandwidth = dcn_validate_bandwidth,
	.acquire_idle_pipe_for_layer = dcn10_acquire_idle_pipe_for_layer,
};

static bool construct(
	uint8_t num_virtual_links,
	struct core_dc *dc,
	struct dcn10_resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap;
	pool->base.funcs = &dcn10_res_pool_funcs;

	/*
	 * TODO fill in from actual raven resource when we create
	 * more than virtual encoder
	 */

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	/* TODO: Hardcode to correct number of functional controllers */
	pool->base.pipe_count = 4;
	dc->public.caps.max_downscale_ratio = 200;
	dc->public.caps.i2c_speed_in_khz = 100;
	dc->public.caps.max_cursor_size = 256;

	dc->public.caps.max_slave_planes = 1;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->public.debug = debug_defaults_drv;
	else
		dc->public.debug = debug_defaults_diags;

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCN10_CLK_SRC_PLL0] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN10_CLK_SRC_PLL1] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN10_CLK_SRC_PLL2] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN10_CLK_SRC_PLL3] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);

	pool->base.clk_src_count = DCN10_CLK_SRC_TOTAL;

	pool->base.dp_clock_source =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				/* todo: not reuse phy_pll registers */
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clock_source_create_fail;
		}
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		pool->base.display_clock = dce120_disp_clk_create(ctx,
				&disp_clk_regs,
				&disp_clk_shift,
				&disp_clk_mask);
		if (pool->base.display_clock == NULL) {
			dm_error("DC: failed to create display clock!\n");
			BREAK_TO_DEBUGGER();
			goto disp_clk_create_fail;
		}
	}

	pool->base.dmcu = dcn10_dmcu_create(ctx,
			&dmcu_regs,
			&dmcu_shift,
			&dmcu_mask);
	if (pool->base.dmcu == NULL) {
		dm_error("DC: failed to create dmcu!\n");
		BREAK_TO_DEBUGGER();
		goto res_create_fail;
	}

	pool->base.abm = dce_abm_create(ctx,
			&abm_regs,
			&abm_shift,
			&abm_mask);
	if (pool->base.abm == NULL) {
		dm_error("DC: failed to create abm!\n");
		BREAK_TO_DEBUGGER();
		goto res_create_fail;
	}

	dml_init_instance(&dc->dml, DML_PROJECT_RAVEN1);
	dc->dcn_ip = dcn10_ip_defaults;
	dc->dcn_soc = dcn10_soc_defaults;
	if (!dc->public.debug.disable_pplib_clock_request)
		dcn_bw_update_from_pplib(dc);
	dcn_bw_sync_calcs_and_dml(dc);
	if (!dc->public.debug.disable_pplib_wm_range)
		dcn_bw_notify_pplib_of_wm_ranges(dc);

	{
	#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->base.irqs = dal_irq_service_dcn10_create(&init_data);
		if (!pool->base.irqs)
			goto irqs_create_fail;
	#endif
	}

	/* mem input -> ipp -> transform -> opp -> TG */
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.mis[i] = dcn10_mem_input_create(ctx, i);
		if (pool->base.mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto mi_create_fail;
		}

		pool->base.ipps[i] = dcn10_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto ipp_create_fail;
		}

		pool->base.transforms[i] = dcn10_transform_create(ctx, i);
		if (pool->base.transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto transform_create_fail;
		}

		pool->base.opps[i] = dcn10_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto opp_create_fail;
		}

		pool->base.timing_generators[i] = dcn10_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto otg_create_fail;
		}
	}

	pool->base.mpc = dcn10_mpc_create(ctx);

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto res_create_fail;

	dcn10_hw_sequencer_construct(dc);
	dc->public.caps.max_surfaces =  pool->base.pipe_count;

	dc->public.cap_funcs = cap_funcs;

	return true;

disp_clk_create_fail:
otg_create_fail:
opp_create_fail:
transform_create_fail:
ipp_create_fail:
mi_create_fail:
irqs_create_fail:
res_create_fail:
clock_source_create_fail:

	destruct(pool);

	return false;
}

struct resource_pool *dcn10_create_resource_pool(
		uint8_t num_virtual_links,
		struct core_dc *dc)
{
	struct dcn10_resource_pool *pool =
		dm_alloc(sizeof(struct dcn10_resource_pool));

	if (!pool)
		return NULL;

	if (construct(num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}
