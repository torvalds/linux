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

#include "dm_services.h"

#include "link_encoder.h"
#include "stream_encoder.h"

#include "resource.h"
#include "dce110/dce110_resource.h"

#include "include/irq_service_interface.h"
#include "dce/dce_audio.h"
#include "dce110/dce110_timing_generator.h"
#include "irq/dce110/irq_service_dce110.h"
#include "dce110/dce110_timing_generator_v.h"
#include "dce/dce_link_encoder.h"
#include "dce/dce_stream_encoder.h"
#include "dce/dce_mem_input.h"
#include "dce110/dce110_mem_input_v.h"
#include "dce/dce_ipp.h"
#include "dce/dce_transform.h"
#include "dce110/dce110_transform_v.h"
#include "dce/dce_opp.h"
#include "dce110/dce110_opp_v.h"
#include "dce/dce_clocks.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"

#if defined(CONFIG_DRM_AMD_DC_FBC)
#include "dce110/dce110_compressor.h"
#endif

#include "reg_helper.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

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

#ifndef DPHY_RX_FAST_TRAINING_CAPABLE
	#define DPHY_RX_FAST_TRAINING_CAPABLE 0x1
#endif

static const struct dce110_timing_generator_offsets dce110_tg_offsets[] = {
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

static const struct dce_disp_clk_registers disp_clk_regs = {
		CLK_COMMON_REG_LIST_DCE_BASE()
};

static const struct dce_disp_clk_shift disp_clk_shift = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce_disp_clk_mask disp_clk_mask = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
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

#define ipp_regs(id)\
[id] = {\
		IPP_DCE110_REG_LIST_DCE_BASE(id)\
}

static const struct dce_ipp_registers ipp_regs[] = {
		ipp_regs(0),
		ipp_regs(1),
		ipp_regs(2)
};

static const struct dce_ipp_shift ipp_shift = {
		IPP_DCE100_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct dce_ipp_mask ipp_mask = {
		IPP_DCE100_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

#define transform_regs(id)\
[id] = {\
		XFM_COMMON_REG_LIST_DCE110(id)\
}

static const struct dce_transform_registers xfm_regs[] = {
		transform_regs(0),
		transform_regs(1),
		transform_regs(2)
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
	LE_DCE110_REG_LIST(id)\
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
	SE_COMMON_REG_LIST(id),\
	.TMDS_CNTL = 0,\
}

static const struct dce110_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2)
};

static const struct dce_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCE110(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_DCE_110_REG_LIST(id),\
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
	OPP_COMMON_MASK_SH_LIST_DCE_110(__SHIFT)
};

static const struct dce_opp_mask opp_mask = {
	OPP_COMMON_MASK_SH_LIST_DCE_110(_MASK)
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

/* AG TBD Needs to be reduced back to 3 pipes once dce10 hw sequencer implemented. */


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

static const struct bios_registers bios_regs = {
	.BIOS_SCRATCH_6 = mmBIOS_SCRATCH_6
};

static const struct resource_caps carrizo_resource_cap = {
		.num_timing_generator = 3,
		.num_video_plane = 1,
		.num_audio = 3,
		.num_stream_encoder = 3,
		.num_pll = 2,
};

static const struct resource_caps stoney_resource_cap = {
		.num_timing_generator = 2,
		.num_video_plane = 1,
		.num_audio = 3,
		.num_stream_encoder = 3,
		.num_pll = 2,
};

#define CTX  ctx
#define REG(reg) mm ## reg

#ifndef mmCC_DC_HDMI_STRAPS
#define mmCC_DC_HDMI_STRAPS 0x4819
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

static struct timing_generator *dce110_timing_generator_create(
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

static struct stream_encoder *dce110_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dce110_stream_encoder *enc110 =
		kzalloc(sizeof(struct dce110_stream_encoder), GFP_KERNEL);

	if (!enc110)
		return NULL;

	dce110_stream_encoder_construct(enc110, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);
	return &enc110->base;
}

#define SRII(reg_name, block, id)\
	.reg_name[id] = mm ## block ## id ## _ ## reg_name

static const struct dce_hwseq_registers hwseq_stoney_reg = {
		HWSEQ_ST_REG_LIST()
};

static const struct dce_hwseq_registers hwseq_cz_reg = {
		HWSEQ_CZ_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCE11_MASK_SH_LIST(__SHIFT),
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCE11_MASK_SH_LIST(_MASK),
};

static struct dce_hwseq *dce110_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = ASIC_REV_IS_STONEY(ctx->asic_id.hw_internal_rev) ?
				&hwseq_stoney_reg : &hwseq_cz_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
		hws->wa.blnd_crtc_trigger = true;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dce110_stream_encoder_create,
	.create_hwseq = dce110_hwseq_create,
};

#define mi_inst_regs(id) { \
	MI_DCE11_REG_LIST(id), \
	.MC_HUB_RDREQ_DMIF_LIMIT = mmMC_HUB_RDREQ_DMIF_LIMIT \
}
static const struct dce_mem_input_registers mi_regs[] = {
		mi_inst_regs(0),
		mi_inst_regs(1),
		mi_inst_regs(2),
};

static const struct dce_mem_input_shift mi_shifts = {
		MI_DCE11_MASK_SH_LIST(__SHIFT),
		.ENABLE = MC_HUB_RDREQ_DMIF_LIMIT__ENABLE__SHIFT
};

static const struct dce_mem_input_mask mi_masks = {
		MI_DCE11_MASK_SH_LIST(_MASK),
		.ENABLE = MC_HUB_RDREQ_DMIF_LIMIT__ENABLE_MASK
};


static struct mem_input *dce110_mem_input_create(
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
	dce_mi->wa.single_head_rdreq_dmif_limit = 3;
	return &dce_mi->base;
}

static void dce110_transform_destroy(struct transform **xfm)
{
	kfree(TO_DCE_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce110_transform_create(
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

static struct input_pixel_processor *dce110_ipp_create(
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
		.max_hdmi_pixel_clock = 594000,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_YCBCR_CAPABLE = true
};

static struct link_encoder *dce110_link_encoder_create(
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

static struct output_pixel_processor *dce110_opp_create(
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

struct clock_source *dce110_clock_source_create(
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

void dce110_clock_source_destroy(struct clock_source **clk_src)
{
	struct dce110_clk_src *dce110_clk_src;

	if (!clk_src)
		return;

	dce110_clk_src = TO_DCE110_CLK_SRC(*clk_src);

	kfree(dce110_clk_src->dp_ss_params);
	kfree(dce110_clk_src->hdmi_ss_params);
	kfree(dce110_clk_src->dvi_ss_params);

	kfree(dce110_clk_src);
	*clk_src = NULL;
}

static void destruct(struct dce110_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			dce110_opp_destroy(&pool->base.opps[i]);

		if (pool->base.transforms[i] != NULL)
			dce110_transform_destroy(&pool->base.transforms[i]);

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

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL)
			kfree(DCE110STRENC_FROM_STRENC(pool->base.stream_enc[i]));
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dce110_clock_source_destroy(&pool->base.clock_sources[i]);
		}
	}

	if (pool->base.dp_clock_source != NULL)
		dce110_clock_source_destroy(&pool->base.dp_clock_source);

	for (i = 0; i < pool->base.audio_count; i++)	{
		if (pool->base.audios[i] != NULL) {
			dce_aud_destroy(&pool->base.audios[i]);
		}
	}

	if (pool->base.abm != NULL)
		dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);

	if (pool->base.display_clock != NULL)
		dce_disp_clk_destroy(&pool->base.display_clock);

	if (pool->base.irqs != NULL) {
		dal_irq_service_destroy(&pool->base.irqs);
	}
}


static void get_pixel_clock_parameters(
	const struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;

	/*TODO: is this halved for YCbCr 420? in that case we might want to move
	 * the pixel clock normalization for hdmi up to here instead of doing it
	 * in pll_adjust_pix_clk
	 */
	pixel_clk_params->requested_pix_clk = stream->timing.pix_clk_khz;
	pixel_clk_params->encoder_object_id = stream->sink->link->link_enc->id;
	pixel_clk_params->signal_type = pipe_ctx->stream->signal;
	pixel_clk_params->controller_id = pipe_ctx->pipe_idx + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
						LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
	pixel_clk_params->flags.SUPPORT_YCBCR420 = (stream->timing.pixel_encoding ==
			PIXEL_ENCODING_YCBCR420);
	pixel_clk_params->pixel_encoding = stream->timing.pixel_encoding;
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		pixel_clk_params->color_depth = COLOR_DEPTH_888;
	}
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		pixel_clk_params->requested_pix_clk  = pixel_clk_params->requested_pix_clk / 2;
	}
}

void dce110_resource_build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
{
	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->stream_res.pix_clk_params);
	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
		pipe_ctx->clock_source,
		&pipe_ctx->stream_res.pix_clk_params,
		&pipe_ctx->pll_settings);
	resource_build_bit_depth_reduction_params(pipe_ctx->stream,
			&pipe_ctx->stream->bit_depth_params);
	pipe_ctx->stream->clamping.pixel_encoding = pipe_ctx->stream->timing.pixel_encoding;
}

static bool is_surface_pixel_format_supported(struct pipe_ctx *pipe_ctx, unsigned int underlay_idx)
{
	if (pipe_ctx->pipe_idx != underlay_idx)
		return true;
	if (!pipe_ctx->plane_state)
		return false;
	if (pipe_ctx->plane_state->format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return false;
	return true;
}

static enum dc_status build_mapped_resource(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;

	if (!is_surface_pixel_format_supported(pipe_ctx,
			dc->res_pool->underlay_pipe_index))
		return DC_SURFACE_PIXEL_FORMAT_UNSUPPORTED;

	dce110_resource_build_pipe_hw_param(pipe_ctx);

	/* TODO: validate audio ASIC caps, encoder */

	resource_build_info_frame(pipe_ctx);

	return DC_OK;
}

static bool dce110_validate_bandwidth(
	struct dc *dc,
	struct dc_state *context)
{
	bool result = false;

	dm_logger_write(
		dc->ctx->logger, LOG_BANDWIDTH_CALCS,
		"%s: start",
		__func__);

	if (bw_calcs(
			dc->ctx,
			dc->bw_dceip,
			dc->bw_vbios,
			context->res_ctx.pipe_ctx,
			dc->res_pool->pipe_count,
			&context->bw.dce))
		result =  true;

	if (!result)
		dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_VALIDATION,
			"%s: %dx%d@%d Bandwidth validation failed!\n",
			__func__,
			context->streams[0]->timing.h_addressable,
			context->streams[0]->timing.v_addressable,
			context->streams[0]->timing.pix_clk_khz);

	if (memcmp(&dc->current_state->bw.dce,
			&context->bw.dce, sizeof(context->bw.dce))) {
		struct log_entry log_entry;
		dm_logger_open(
			dc->ctx->logger,
			&log_entry,
			LOG_BANDWIDTH_CALCS);
		dm_logger_append(&log_entry, "%s: finish,\n"
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			__func__,
			context->bw.dce.nbp_state_change_wm_ns[0].b_mark,
			context->bw.dce.nbp_state_change_wm_ns[0].a_mark,
			context->bw.dce.urgent_wm_ns[0].b_mark,
			context->bw.dce.urgent_wm_ns[0].a_mark,
			context->bw.dce.stutter_exit_wm_ns[0].b_mark,
			context->bw.dce.stutter_exit_wm_ns[0].a_mark);
		dm_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			context->bw.dce.nbp_state_change_wm_ns[1].b_mark,
			context->bw.dce.nbp_state_change_wm_ns[1].a_mark,
			context->bw.dce.urgent_wm_ns[1].b_mark,
			context->bw.dce.urgent_wm_ns[1].a_mark,
			context->bw.dce.stutter_exit_wm_ns[1].b_mark,
			context->bw.dce.stutter_exit_wm_ns[1].a_mark);
		dm_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d stutter_mode_enable: %d\n",
			context->bw.dce.nbp_state_change_wm_ns[2].b_mark,
			context->bw.dce.nbp_state_change_wm_ns[2].a_mark,
			context->bw.dce.urgent_wm_ns[2].b_mark,
			context->bw.dce.urgent_wm_ns[2].a_mark,
			context->bw.dce.stutter_exit_wm_ns[2].b_mark,
			context->bw.dce.stutter_exit_wm_ns[2].a_mark,
			context->bw.dce.stutter_mode_enable);
		dm_logger_append(&log_entry,
			"cstate: %d pstate: %d nbpstate: %d sync: %d dispclk: %d\n"
			"sclk: %d sclk_sleep: %d yclk: %d blackout_recovery_time_us: %d\n",
			context->bw.dce.cpuc_state_change_enable,
			context->bw.dce.cpup_state_change_enable,
			context->bw.dce.nbp_state_change_enable,
			context->bw.dce.all_displays_in_sync,
			context->bw.dce.dispclk_khz,
			context->bw.dce.sclk_khz,
			context->bw.dce.sclk_deep_sleep_khz,
			context->bw.dce.yclk_khz,
			context->bw.dce.blackout_recovery_time_us);
		dm_logger_close(&log_entry);
	}
	return result;
}

static bool dce110_validate_surface_sets(
		struct dc_state *context)
{
	int i, j;

	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count == 0)
			continue;

		if (context->stream_status[i].plane_count > 2)
			return false;

		for (j = 0; j < context->stream_status[i].plane_count; j++) {
			struct dc_plane_state *plane =
				context->stream_status[i].plane_states[j];

			/* underlay validation */
			if (plane->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {

				if ((plane->src_rect.width > 1920 ||
					plane->src_rect.height > 1080))
					return false;

				/* irrespective of plane format,
				 * stream should be RGB encoded
				 */
				if (context->streams[i]->timing.pixel_encoding
						!= PIXEL_ENCODING_RGB)
					return false;

			}

		}
	}

	return true;
}

enum dc_status dce110_validate_global(
		struct dc *dc,
		struct dc_state *context)
{
	if (!dce110_validate_surface_sets(context))
		return DC_FAIL_SURFACE_VALIDATE;

	return DC_OK;
}

static enum dc_status dce110_add_stream_to_ctx(
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

static enum dc_status dce110_validate_guaranteed(
		struct dc *dc,
		struct dc_stream_state *dc_stream,
		struct dc_state *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;

	context->streams[0] = dc_stream;
	dc_stream_retain(context->streams[0]);
	context->stream_count++;

	result = resource_map_pool_resources(dc, context, dc_stream);

	if (result == DC_OK)
		result = resource_map_clock_resources(dc, context, dc_stream);

	if (result == DC_OK)
		result = build_mapped_resource(dc, context, dc_stream);

	if (result == DC_OK) {
		validate_guaranteed_copy_streams(
				context, dc->caps.max_streams);
		result = resource_build_scaling_params_for_context(dc, context);
	}

	if (result == DC_OK)
		if (!dce110_validate_bandwidth(dc, context))
			result = DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

static struct pipe_ctx *dce110_acquire_underlay(
		struct dc_state *context,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct dc *dc = stream->ctx->dc;
	struct resource_context *res_ctx = &context->res_ctx;
	unsigned int underlay_idx = pool->underlay_pipe_index;
	struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[underlay_idx];

	if (res_ctx->pipe_ctx[underlay_idx].stream)
		return NULL;

	pipe_ctx->stream_res.tg = pool->timing_generators[underlay_idx];
	pipe_ctx->plane_res.mi = pool->mis[underlay_idx];
	/*pipe_ctx->plane_res.ipp = res_ctx->pool->ipps[underlay_idx];*/
	pipe_ctx->plane_res.xfm = pool->transforms[underlay_idx];
	pipe_ctx->stream_res.opp = pool->opps[underlay_idx];
	pipe_ctx->pipe_idx = underlay_idx;

	pipe_ctx->stream = stream;

	if (!dc->current_state->res_ctx.pipe_ctx[underlay_idx].stream) {
		struct tg_color black_color = {0};
		struct dc_bios *dcb = dc->ctx->dc_bios;

		dc->hwss.enable_display_power_gating(
				dc,
				pipe_ctx->pipe_idx,
				dcb, PIPE_GATING_CONTROL_DISABLE);

		/*
		 * This is for powering on underlay, so crtc does not
		 * need to be enabled
		 */

		pipe_ctx->stream_res.tg->funcs->program_timing(pipe_ctx->stream_res.tg,
				&stream->timing,
				false);

		pipe_ctx->stream_res.tg->funcs->enable_advanced_request(
				pipe_ctx->stream_res.tg,
				true,
				&stream->timing);

		pipe_ctx->plane_res.mi->funcs->allocate_mem_input(pipe_ctx->plane_res.mi,
				stream->timing.h_total,
				stream->timing.v_total,
				stream->timing.pix_clk_khz,
				context->stream_count);

		color_space_to_black_color(dc,
				COLOR_SPACE_YCBCR601, &black_color);
		pipe_ctx->stream_res.tg->funcs->set_blank_color(
				pipe_ctx->stream_res.tg,
				&black_color);
	}

	return pipe_ctx;
}

static void dce110_destroy_resource_pool(struct resource_pool **pool)
{
	struct dce110_resource_pool *dce110_pool = TO_DCE110_RES_POOL(*pool);

	destruct(dce110_pool);
	kfree(dce110_pool);
	*pool = NULL;
}


static const struct resource_funcs dce110_res_pool_funcs = {
	.destroy = dce110_destroy_resource_pool,
	.link_enc_create = dce110_link_encoder_create,
	.validate_guaranteed = dce110_validate_guaranteed,
	.validate_bandwidth = dce110_validate_bandwidth,
	.acquire_idle_pipe_for_layer = dce110_acquire_underlay,
	.add_stream_to_ctx = dce110_add_stream_to_ctx,
	.validate_global = dce110_validate_global
};

static bool underlay_create(struct dc_context *ctx, struct resource_pool *pool)
{
	struct dce110_timing_generator *dce110_tgv = kzalloc(sizeof(*dce110_tgv),
							     GFP_KERNEL);
	struct dce_transform *dce110_xfmv = kzalloc(sizeof(*dce110_xfmv),
						    GFP_KERNEL);
	struct dce_mem_input *dce110_miv = kzalloc(sizeof(*dce110_miv),
						   GFP_KERNEL);
	struct dce110_opp *dce110_oppv = kzalloc(sizeof(*dce110_oppv),
						 GFP_KERNEL);

	if (!dce110_tgv || !dce110_xfmv || !dce110_miv || !dce110_oppv) {
		kfree(dce110_tgv);
		kfree(dce110_xfmv);
		kfree(dce110_miv);
		kfree(dce110_oppv);
		return false;
	}

	dce110_opp_v_construct(dce110_oppv, ctx);

	dce110_timing_generator_v_construct(dce110_tgv, ctx);
	dce110_mem_input_v_construct(dce110_miv, ctx);
	dce110_transform_v_construct(dce110_xfmv, ctx);

	pool->opps[pool->pipe_count] = &dce110_oppv->base;
	pool->timing_generators[pool->pipe_count] = &dce110_tgv->base;
	pool->mis[pool->pipe_count] = &dce110_miv->base;
	pool->transforms[pool->pipe_count] = &dce110_xfmv->base;
	pool->pipe_count++;

	/* update the public caps to indicate an underlay is available */
	ctx->dc->caps.max_slave_planes = 1;
	ctx->dc->caps.max_slave_planes = 1;

	return true;
}

static void bw_calcs_data_update_from_pplib(struct dc *dc)
{
	struct dm_pp_clock_levels clks = {0};

	/*do system clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_ENGINE_CLK,
			&clks);
	/* convert all the clock fro kHz to fix point mHz */
	dc->bw_vbios->high_sclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels-1], 1000);
	dc->bw_vbios->mid1_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels/8], 1000);
	dc->bw_vbios->mid2_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels*2/8], 1000);
	dc->bw_vbios->mid3_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels*3/8], 1000);
	dc->bw_vbios->mid4_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels*4/8], 1000);
	dc->bw_vbios->mid5_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels*5/8], 1000);
	dc->bw_vbios->mid6_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels*6/8], 1000);
	dc->bw_vbios->low_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[0], 1000);
	dc->sclk_lvls = clks;

	/*do display clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_DISPLAY_CLK,
			&clks);
	dc->bw_vbios->high_voltage_max_dispclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels-1], 1000);
	dc->bw_vbios->mid_voltage_max_dispclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels>>1], 1000);
	dc->bw_vbios->low_voltage_max_dispclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[0], 1000);

	/*do memory clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_MEMORY_CLK,
			&clks);

	dc->bw_vbios->low_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[0] * MEMORY_TYPE_MULTIPLIER, 1000);
	dc->bw_vbios->mid_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[clks.num_levels>>1] * MEMORY_TYPE_MULTIPLIER,
		1000);
	dc->bw_vbios->high_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[clks.num_levels-1] * MEMORY_TYPE_MULTIPLIER,
		1000);
}

const struct resource_caps *dce110_resource_cap(
	struct hw_asic_id *asic_id)
{
	if (ASIC_REV_IS_STONEY(asic_id->hw_internal_rev))
		return &stoney_resource_cap;
	else
		return &carrizo_resource_cap;
}

static bool construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dce110_resource_pool *pool,
	struct hw_asic_id asic_id)
{
	unsigned int i;
	struct dc_context *ctx = dc->ctx;
	struct dc_firmware_info info;
	struct dc_bios *bp;
	struct dm_pp_static_clock_info static_clk_info = {0};

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = dce110_resource_cap(&ctx->asic_id);
	pool->base.funcs = &dce110_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/

	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;
	pool->base.underlay_pipe_index = pool->base.pipe_count;

	dc->caps.max_downscale_ratio = 150;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.max_cursor_size = 128;

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	bp = ctx->dc_bios;

	if ((bp->funcs->get_firmware_info(bp, &info) == BP_RESULT_OK) &&
		info.external_clock_source_frequency_for_dp != 0) {
		pool->base.dp_clock_source =
				dce110_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_EXTERNAL, NULL, true);

		pool->base.clock_sources[0] =
				dce110_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL0,
						&clk_src_regs[0], false);
		pool->base.clock_sources[1] =
				dce110_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL1,
						&clk_src_regs[1], false);

		pool->base.clk_src_count = 2;

		/* TODO: find out if CZ support 3 PLLs */
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

	pool->base.display_clock = dce110_disp_clk_create(ctx,
			&disp_clk_regs,
			&disp_clk_shift,
			&disp_clk_mask);
	if (pool->base.display_clock == NULL) {
		dm_error("DC: failed to create display clock!\n");
		BREAK_TO_DEBUGGER();
		goto res_create_fail;
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

	/* get static clock information for PPLIB or firmware, save
	 * max_clock_state
	 */
	if (dm_pp_get_static_clocks(ctx, &static_clk_info))
		pool->base.display_clock->max_clks_state =
				static_clk_info.max_clocks_state;

	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->base.irqs = dal_irq_service_dce110_create(&init_data);
		if (!pool->base.irqs)
			goto res_create_fail;
	}

	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.timing_generators[i] = dce110_timing_generator_create(
				ctx, i, &dce110_tg_offsets[i]);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto res_create_fail;
		}

		pool->base.mis[i] = dce110_mem_input_create(ctx, i);
		if (pool->base.mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto res_create_fail;
		}

		pool->base.ipps[i] = dce110_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto res_create_fail;
		}

		pool->base.transforms[i] = dce110_transform_create(ctx, i);
		if (pool->base.transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto res_create_fail;
		}

		pool->base.opps[i] = dce110_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto res_create_fail;
		}
	}

#if defined(CONFIG_DRM_AMD_DC_FBC)
	dc->fbc_compressor = dce110_compressor_create(ctx);



#endif
	if (!underlay_create(ctx, &pool->base))
		goto res_create_fail;

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			&res_create_funcs))
		goto res_create_fail;

	/* Create hardware sequencer */
	dce110_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	bw_calcs_init(dc->bw_dceip, dc->bw_vbios, dc->ctx->asic_id);

	bw_calcs_data_update_from_pplib(dc);

	return true;

res_create_fail:
	destruct(pool);
	return false;
}

struct resource_pool *dce110_create_resource_pool(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct hw_asic_id asic_id)
{
	struct dce110_resource_pool *pool =
		kzalloc(sizeof(struct dce110_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (construct(num_virtual_links, dc, pool, asic_id))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}
