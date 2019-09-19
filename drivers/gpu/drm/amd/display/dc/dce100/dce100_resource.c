/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "link_encoder.h"
#include "stream_encoder.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "../virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dce110/dce110_timing_generator.h"
#include "irq/dce110/irq_service_dce110.h"
#include "dce/dce_link_encoder.h"
#include "dce/dce_stream_encoder.h"
#include "dce/dce_mem_input.h"
#include "dce/dce_ipp.h"
#include "dce/dce_transform.h"
#include "dce/dce_opp.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "dce100/dce100_hw_sequencer.h"

#include "reg_helper.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#include "dce/dce_dmcu.h"
#include "dce/dce_aux.h"
#include "dce/dce_abm.h"
#include "dce/dce_i2c.h"

#ifndef mmMC_HUB_RDREQ_DMIF_LIMIT
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"
#endif

#ifndef mmDP_DPHY_INTERNAL_CTRL
	#define mmDP_DPHY_INTERNAL_CTRL 0x4aa7
	#define mmDP0_DP_DPHY_INTERNAL_CTRL 0x4aa7
	#define mmDP1_DP_DPHY_INTERNAL_CTRL 0x4ba7
	#define mmDP2_DP_DPHY_INTERNAL_CTRL 0x4ca7
	#define mmDP3_DP_DPHY_INTERNAL_CTRL 0x4da7
	#define mmDP4_DP_DPHY_INTERNAL_CTRL 0x4ea7
	#define mmDP5_DP_DPHY_INTERNAL_CTRL 0x4fa7
	#define mmDP6_DP_DPHY_INTERNAL_CTRL 0x54a7
	#define mmDP7_DP_DPHY_INTERNAL_CTRL 0x56a7
	#define mmDP8_DP_DPHY_INTERNAL_CTRL 0x57a7
#endif

#ifndef mmBIOS_SCRATCH_2
	#define mmBIOS_SCRATCH_2 0x05CB
	#define mmBIOS_SCRATCH_3 0x05CC
	#define mmBIOS_SCRATCH_6 0x05CF
#endif

#ifndef mmDP_DPHY_BS_SR_SWAP_CNTL
	#define mmDP_DPHY_BS_SR_SWAP_CNTL                       0x4ADC
	#define mmDP0_DP_DPHY_BS_SR_SWAP_CNTL                   0x4ADC
	#define mmDP1_DP_DPHY_BS_SR_SWAP_CNTL                   0x4BDC
	#define mmDP2_DP_DPHY_BS_SR_SWAP_CNTL                   0x4CDC
	#define mmDP3_DP_DPHY_BS_SR_SWAP_CNTL                   0x4DDC
	#define mmDP4_DP_DPHY_BS_SR_SWAP_CNTL                   0x4EDC
	#define mmDP5_DP_DPHY_BS_SR_SWAP_CNTL                   0x4FDC
	#define mmDP6_DP_DPHY_BS_SR_SWAP_CNTL                   0x54DC
#endif

#ifndef mmDP_DPHY_FAST_TRAINING
	#define mmDP_DPHY_FAST_TRAINING                         0x4ABC
	#define mmDP0_DP_DPHY_FAST_TRAINING                     0x4ABC
	#define mmDP1_DP_DPHY_FAST_TRAINING                     0x4BBC
	#define mmDP2_DP_DPHY_FAST_TRAINING                     0x4CBC
	#define mmDP3_DP_DPHY_FAST_TRAINING                     0x4DBC
	#define mmDP4_DP_DPHY_FAST_TRAINING                     0x4EBC
	#define mmDP5_DP_DPHY_FAST_TRAINING                     0x4FBC
	#define mmDP6_DP_DPHY_FAST_TRAINING                     0x54BC
#endif

static const struct dce110_timing_generator_offsets dce100_tg_offsets[] = {
	{
		.crtc = (mmCRTC0_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp =  (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC1_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC2_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC3_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp =  (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC4_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC5_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
	}
};

/* set register offset */
#define SR(reg_name)\
	.reg_name = mm ## reg_name

/* set register offset with instance */
#define SRI(reg_name, block, id)\
	.reg_name = mm ## block ## id ## _ ## reg_name

#define ipp_regs(id)\
[id] = {\
		IPP_DCE100_REG_LIST_DCE_BASE(id)\
}

static const struct dce_ipp_registers ipp_regs[] = {
		ipp_regs(0),
		ipp_regs(1),
		ipp_regs(2),
		ipp_regs(3),
		ipp_regs(4),
		ipp_regs(5)
};

static const struct dce_ipp_shift ipp_shift = {
		IPP_DCE100_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce_ipp_mask ipp_mask = {
		IPP_DCE100_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

#define transform_regs(id)\
[id] = {\
		XFM_COMMON_REG_LIST_DCE100(id)\
}

static const struct dce_transform_registers xfm_regs[] = {
		transform_regs(0),
		transform_regs(1),
		transform_regs(2),
		transform_regs(3),
		transform_regs(4),
		transform_regs(5)
};

static const struct dce_transform_shift xfm_shift = {
		XFM_COMMON_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_transform_mask xfm_mask = {
		XFM_COMMON_MASK_SH_LIST_DCE110(_MASK)
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
	LE_DCE100_REG_LIST(id)\
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

#define stream_enc_regs(id)\
[id] = {\
	SE_COMMON_REG_LIST_DCE_BASE(id),\
	.AFMT_CNTL = 0,\
}

static const struct dce110_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
	stream_enc_regs(5),
	stream_enc_regs(6)
};

static const struct dce_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCE80_100(__SHIFT)
};

static const struct dce_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCE80_100(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_DCE_100_REG_LIST(id),\
}

static const struct dce_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
	opp_regs(4),
	opp_regs(5)
};

static const struct dce_opp_shift opp_shift = {
	OPP_COMMON_MASK_SH_LIST_DCE_100(__SHIFT)
};

static const struct dce_opp_mask opp_mask = {
	OPP_COMMON_MASK_SH_LIST_DCE_100(_MASK)
};
#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST(id), \
	.AUX_RESET_MASK = 0 \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1),
		aux_engine_regs(2),
		aux_engine_regs(3),
		aux_engine_regs(4),
		aux_engine_regs(5)
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
	audio_regs(6),
};

static const struct dce_audio_shift audio_shift = {
		AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_aduio_mask audio_mask = {
		AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define clk_src_regs(id)\
[id] = {\
	CS_COMMON_REG_LIST_DCE_100_110(id),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0),
	clk_src_regs(1),
	clk_src_regs(2)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

static const struct dce_dmcu_registers dmcu_regs = {
		DMCU_DCE110_COMMON_REG_LIST()
};

static const struct dce_dmcu_shift dmcu_shift = {
		DMCU_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_dmcu_mask dmcu_mask = {
		DMCU_MASK_SH_LIST_DCE110(_MASK)
};

static const struct dce_abm_registers abm_regs = {
		ABM_DCE110_COMMON_REG_LIST()
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCE110(_MASK)
};

#define DCFE_MEM_PWR_CTRL_REG_BASE 0x1b03

static const struct bios_registers bios_regs = {
	.BIOS_SCRATCH_3 = mmBIOS_SCRATCH_3,
	.BIOS_SCRATCH_6 = mmBIOS_SCRATCH_6
};

static const struct resource_caps res_cap = {
	.num_timing_generator = 6,
	.num_audio = 6,
	.num_stream_encoder = 6,
	.num_pll = 3,
	.num_ddc = 6,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCE_RGB,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = false,
			.fp16 = false
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 1,
			.fp16 = 1
	},

	.max_downscale_factor = {
			.argb8888 = 250,
			.nv12 = 1,
			.fp16 = 1
	}
};

#define CTX  ctx
#define REG(reg) mm ## reg

#ifndef mmCC_DC_HDMI_STRAPS
#define mmCC_DC_HDMI_STRAPS 0x1918
#define CC_DC_HDMI_STRAPS__HDMI_DISABLE_MASK 0x40
#define CC_DC_HDMI_STRAPS__HDMI_DISABLE__SHIFT 0x6
#define CC_DC_HDMI_STRAPS__AUDIO_STREAM_NUMBER_MASK 0x700
#define CC_DC_HDMI_STRAPS__AUDIO_STREAM_NUMBER__SHIFT 0x8
#endif

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	REG_GET_2(CC_DC_HDMI_STRAPS,
			HDMI_DISABLE, &straps->hdmi_disable,
			AUDIO_STREAM_NUMBER, &straps->audio_stream_number);

	REG_GET(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO, &straps->dc_pinstraps_audio);
}

static struct audio *create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct timing_generator *dce100_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		kzalloc(sizeof(struct dce110_timing_generator), GFP_KERNEL);

	if (!tg110)
		return NULL;

	dce110_timing_generator_construct(tg110, ctx, instance, offsets);
	return &tg110->base;
}

static struct stream_encoder *dce100_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dce110_stream_encoder *enc110 =
		kzalloc(sizeof(struct dce110_stream_encoder), GFP_KERNEL);

	if (!enc110)
		return NULL;

	dce110_stream_encoder_construct(enc110, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id], &se_shift, &se_mask);
	return &enc110->base;
}

#define SRII(reg_name, block, id)\
	.reg_name[id] = mm ## block ## id ## _ ## reg_name

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCE10_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCE10_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCE10_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dce100_hwseq_create(
	struct dc_context *ctx)
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

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dce100_stream_encoder_create,
	.create_hwseq = dce100_hwseq_create,
};

#define mi_inst_regs(id) { \
	MI_DCE8_REG_LIST(id), \
	.MC_HUB_RDREQ_DMIF_LIMIT = mmMC_HUB_RDREQ_DMIF_LIMIT \
}
static const struct dce_mem_input_registers mi_regs[] = {
		mi_inst_regs(0),
		mi_inst_regs(1),
		mi_inst_regs(2),
		mi_inst_regs(3),
		mi_inst_regs(4),
		mi_inst_regs(5),
};

static const struct dce_mem_input_shift mi_shifts = {
		MI_DCE8_MASK_SH_LIST(__SHIFT),
		.ENABLE = MC_HUB_RDREQ_DMIF_LIMIT__ENABLE__SHIFT
};

static const struct dce_mem_input_mask mi_masks = {
		MI_DCE8_MASK_SH_LIST(_MASK),
		.ENABLE = MC_HUB_RDREQ_DMIF_LIMIT__ENABLE_MASK
};

static struct mem_input *dce100_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_mem_input *dce_mi = kzalloc(sizeof(struct dce_mem_input),
					       GFP_KERNEL);

	if (!dce_mi) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_mem_input_construct(dce_mi, ctx, inst, &mi_regs[inst], &mi_shifts, &mi_masks);
	dce_mi->wa.single_head_rdreq_dmif_limit = 2;
	return &dce_mi->base;
}

static void dce100_transform_destroy(struct transform **xfm)
{
	kfree(TO_DCE_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce100_transform_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_transform *transform =
		kzalloc(sizeof(struct dce_transform), GFP_KERNEL);

	if (!transform)
		return NULL;

	dce_transform_construct(transform, ctx, inst,
				&xfm_regs[inst], &xfm_shift, &xfm_mask);
	return &transform->base;
}

static struct input_pixel_processor *dce100_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dce_ipp *ipp = kzalloc(sizeof(struct dce_ipp), GFP_KERNEL);

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 300000,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true
};

struct link_encoder *dce100_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		kzalloc(sizeof(struct dce110_link_encoder), GFP_KERNEL);

	if (!enc110)
		return NULL;

	dce110_link_encoder_construct(enc110,
				      enc_init_data,
				      &link_enc_feature,
				      &link_enc_regs[enc_init_data->transmitter],
				      &link_enc_aux_regs[enc_init_data->channel - 1],
				      &link_enc_hpd_regs[enc_init_data->hpd_source]);
	return &enc110->base;
}

struct output_pixel_processor *dce100_opp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce110_opp *opp =
		kzalloc(sizeof(struct dce110_opp), GFP_KERNEL);

	if (!opp)
		return NULL;

	dce110_opp_construct(opp,
			     ctx, inst, &opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

struct dce_aux *dce100_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst]);

	return &aux_engine->base;
}
#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
		i2c_inst_regs(6),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

struct dce_i2c_hw *dce100_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dce100_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}
struct clock_source *dce100_clock_source_create(
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

	if (dce110_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	BREAK_TO_DEBUGGER();
	return NULL;
}

void dce100_clock_source_destroy(struct clock_source **clk_src)
{
	kfree(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

static void destruct(struct dce110_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			dce110_opp_destroy(&pool->base.opps[i]);

		if (pool->base.transforms[i] != NULL)
			dce100_transform_destroy(&pool->base.transforms[i]);

		if (pool->base.ipps[i] != NULL)
			dce_ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.mis[i] != NULL) {
			kfree(TO_DCE_MEM_INPUT(pool->base.mis[i]));
			pool->base.mis[i] = NULL;
		}

		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCE110TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
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

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL)
			kfree(DCE110STRENC_FROM_STRENC(pool->base.stream_enc[i]));
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL)
			dce100_clock_source_destroy(&pool->base.clock_sources[i]);
	}

	if (pool->base.dp_clock_source != NULL)
		dce100_clock_source_destroy(&pool->base.dp_clock_source);

	for (i = 0; i < pool->base.audio_count; i++)	{
		if (pool->base.audios[i] != NULL)
			dce_aud_destroy(&pool->base.audios[i]);
	}

	if (pool->base.abm != NULL)
				dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
			dce_dmcu_destroy(&pool->base.dmcu);

	if (pool->base.irqs != NULL)
		dal_irq_service_destroy(&pool->base.irqs);
}

static enum dc_status build_mapped_resource(
		const struct dc  *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;

	dce110_resource_build_pipe_hw_param(pipe_ctx);

	resource_build_info_frame(pipe_ctx);

	return DC_OK;
}

bool dce100_validate_bandwidth(
	struct dc  *dc,
	struct dc_state *context,
	bool fast_validate)
{
	int i;
	bool at_least_one_pipe = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].stream)
			at_least_one_pipe = true;
	}

	if (at_least_one_pipe) {
		/* TODO implement when needed but for now hardcode max value*/
		context->bw_ctx.bw.dce.dispclk_khz = 681000;
		context->bw_ctx.bw.dce.yclk_khz = 250000 * MEMORY_TYPE_MULTIPLIER_CZ;
	} else {
		context->bw_ctx.bw.dce.dispclk_khz = 0;
		context->bw_ctx.bw.dce.yclk_khz = 0;
	}

	return true;
}

static bool dce100_validate_surface_sets(
		struct dc_state *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count == 0)
			continue;

		if (context->stream_status[i].plane_count > 1)
			return false;

		if (context->stream_status[i].plane_states[0]->format
				>= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
			return false;
	}

	return true;
}

enum dc_status dce100_validate_global(
		struct dc  *dc,
		struct dc_state *context)
{
	if (!dce100_validate_surface_sets(context))
		return DC_FAIL_SURFACE_VALIDATE;

	return DC_OK;
}

enum dc_status dce100_add_stream_to_ctx(
		struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *dc_stream)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;

	result = resource_map_pool_resources(dc, new_ctx, dc_stream);

	if (result == DC_OK)
		result = resource_map_clock_resources(dc, new_ctx, dc_stream);

	if (result == DC_OK)
		result = build_mapped_resource(dc, new_ctx, dc_stream);

	return result;
}

static void dce100_destroy_resource_pool(struct resource_pool **pool)
{
	struct dce110_resource_pool *dce110_pool = TO_DCE110_RES_POOL(*pool);

	destruct(dce110_pool);
	kfree(dce110_pool);
	*pool = NULL;
}

enum dc_status dce100_validate_plane(const struct dc_plane_state *plane_state, struct dc_caps *caps)
{

	if (plane_state->format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return DC_OK;

	return DC_FAIL_SURFACE_VALIDATE;
}

struct stream_encoder *dce100_find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;
	int j = -1;
	struct dc_link *link = stream->link;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
				pool->stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case
			 */
			j = i;
			if (pool->stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return pool->stream_enc[i];
		}
	}

	/*
	 * below can happen in cases when stream encoder is acquired:
	 * 1) for second MST display in chain, so preferred engine already
	 * acquired;
	 * 2) for another link, which preferred engine already acquired by any
	 * MST configuration.
	 *
	 * If signal is of DP type and preferred engine not found, return last available
	 *
	 * TODO - This is just a patch up and a generic solution is
	 * required for non DP connectors.
	 */

	if (j >= 0 && link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT)
		return pool->stream_enc[j];

	return NULL;
}

static const struct resource_funcs dce100_res_pool_funcs = {
	.destroy = dce100_destroy_resource_pool,
	.link_enc_create = dce100_link_encoder_create,
	.validate_bandwidth = dce100_validate_bandwidth,
	.validate_plane = dce100_validate_plane,
	.add_stream_to_ctx = dce100_add_stream_to_ctx,
	.validate_global = dce100_validate_global,
	.find_first_free_match_stream_enc_for_link = dce100_find_first_free_match_stream_enc_for_link
};

static bool construct(
	uint8_t num_virtual_links,
	struct dc  *dc,
	struct dce110_resource_pool *pool)
{
	unsigned int i;
	struct dc_context *ctx = dc->ctx;
	struct dc_firmware_info info;
	struct dc_bios *bp;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap;
	pool->base.funcs = &dce100_res_pool_funcs;
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	bp = ctx->dc_bios;

	if ((bp->funcs->get_firmware_info(bp, &info) == BP_RESULT_OK) &&
		info.external_clock_source_frequency_for_dp != 0) {
		pool->base.dp_clock_source =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_EXTERNAL, NULL, true);

		pool->base.clock_sources[0] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL0, &clk_src_regs[0], false);
		pool->base.clock_sources[1] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL1, &clk_src_regs[1], false);
		pool->base.clock_sources[2] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL2, &clk_src_regs[2], false);
		pool->base.clk_src_count = 3;

	} else {
		pool->base.dp_clock_source =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL0, &clk_src_regs[0], true);

		pool->base.clock_sources[0] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL1, &clk_src_regs[1], false);
		pool->base.clock_sources[1] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL2, &clk_src_regs[2], false);
		pool->base.clk_src_count = 2;
	}

	if (pool->base.dp_clock_source == NULL) {
		dm_error("DC: failed to create dp clock source!\n");
		BREAK_TO_DEBUGGER();
		goto res_create_fail;
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto res_create_fail;
		}
	}

	pool->base.dmcu = dce_dmcu_create(ctx,
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

	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->base.irqs = dal_irq_service_dce110_create(&init_data);
		if (!pool->base.irqs)
			goto res_create_fail;
	}

	/*************************************************
	*  Resource + asic cap harcoding                *
	*************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.pipe_count = res_cap.num_timing_generator;
	pool->base.timing_generator_count = pool->base.res_cap->num_timing_generator;
	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 40;
	dc->caps.max_cursor_size = 128;
	dc->caps.dual_link_dvi = true;
	dc->caps.disable_dp_clk_share = true;
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.timing_generators[i] =
			dce100_timing_generator_create(
				ctx,
				i,
				&dce100_tg_offsets[i]);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto res_create_fail;
		}

		pool->base.mis[i] = dce100_mem_input_create(ctx, i);
		if (pool->base.mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto res_create_fail;
		}

		pool->base.ipps[i] = dce100_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto res_create_fail;
		}

		pool->base.transforms[i] = dce100_transform_create(ctx, i);
		if (pool->base.transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto res_create_fail;
		}

		pool->base.opps[i] = dce100_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto res_create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dce100_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto res_create_fail;
		}
		pool->base.hw_i2cs[i] = dce100_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create i2c engine!!\n");
			goto res_create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			&res_create_funcs))
		goto res_create_fail;

	/* Create hardware sequencer */
	dce100_hw_sequencer_construct(dc);
	return true;

res_create_fail:
	destruct(pool);

	return false;
}

struct resource_pool *dce100_create_resource_pool(
	uint8_t num_virtual_links,
	struct dc  *dc)
{
	struct dce110_resource_pool *pool =
		kzalloc(sizeof(struct dce110_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (construct(num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}

