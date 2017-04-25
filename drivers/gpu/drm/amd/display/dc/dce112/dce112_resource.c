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
#include "include/irq_service_interface.h"
#include "dce110/dce110_resource.h"
#include "dce110/dce110_timing_generator.h"
#include "dce112/dce112_mem_input.h"

#include "irq/dce110/irq_service_dce110.h"
#include "dce/dce_transform.h"
#include "dce/dce_link_encoder.h"
#include "dce/dce_stream_encoder.h"
#include "dce/dce_audio.h"
#include "dce/dce_opp.h"
#include "dce/dce_ipp.h"
#include "dce/dce_clocks.h"
#include "dce/dce_clock_source.h"

#include "dce/dce_hwseq.h"
#include "dce112/dce112_hw_sequencer.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"

#include "reg_helper.h"

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

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

enum dce112_clk_src_array_id {
	DCE112_CLK_SRC_PLL0,
	DCE112_CLK_SRC_PLL1,
	DCE112_CLK_SRC_PLL2,
	DCE112_CLK_SRC_PLL3,
	DCE112_CLK_SRC_PLL4,
	DCE112_CLK_SRC_PLL5,

	DCE112_CLK_SRC_TOTAL
};

static const struct dce110_timing_generator_offsets dce112_tg_offsets[] = {
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
		.dcp = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
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

static const struct dce110_mem_input_reg_offsets dce112_mi_reg_offsets[] = {
	{
		.dcp = (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG0_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE0_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG1_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE1_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG2_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE2_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG3_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE3_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG4_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE4_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG5_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE5_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
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
		XFM_COMMON_REG_LIST_DCE110(id)\
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
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
	stream_enc_regs(5)
};

static const struct dce_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCE112(__SHIFT)
};

static const struct dce_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCE112(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_DCE_112_REG_LIST(id),\
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
	OPP_COMMON_MASK_SH_LIST_DCE_112(__SHIFT)
};

static const struct dce_opp_mask opp_mask = {
	OPP_COMMON_MASK_SH_LIST_DCE_112(_MASK)
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
	audio_regs(5)
};

static const struct dce_audio_shift audio_shift = {
		AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_aduio_mask audio_mask = {
		AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define clk_src_regs(index, id)\
[index] = {\
	CS_COMMON_REG_LIST_DCE_112(id),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E),
	clk_src_regs(5, F)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCE_112(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCE_112(_MASK)
};

static const struct bios_registers bios_regs = {
	.BIOS_SCRATCH_6 = mmBIOS_SCRATCH_6
};

static const struct resource_caps polaris_10_resource_cap = {
		.num_timing_generator = 6,
		.num_audio = 6,
		.num_stream_encoder = 6,
		.num_pll = 8, /* why 8? 6 combo PHY PLL + 2 regular PLLs? */
};

static const struct resource_caps polaris_11_resource_cap = {
		.num_timing_generator = 5,
		.num_audio = 5,
		.num_stream_encoder = 5,
		.num_pll = 8, /* why 8? 6 combo PHY PLL + 2 regular PLLs? */
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


static struct timing_generator *dce112_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		dm_alloc(sizeof(struct dce110_timing_generator));

	if (!tg110)
		return NULL;

	if (dce110_timing_generator_construct(tg110, ctx, instance, offsets))
		return &tg110->base;

	BREAK_TO_DEBUGGER();
	dm_free(tg110);
	return NULL;
}

static struct stream_encoder *dce112_stream_encoder_create(
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

#define SRII(reg_name, block, id)\
	.reg_name[id] = mm ## block ## id ## _ ## reg_name

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCE112_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCE112_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCE112_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dce112_hwseq_create(
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
	.create_stream_encoder = dce112_stream_encoder_create,
	.create_hwseq = dce112_hwseq_create,
};

#define mi_inst_regs(id) { MI_DCE11_2_REG_LIST(id) }
static const struct dce_mem_input_registers mi_regs[] = {
		mi_inst_regs(0),
		mi_inst_regs(1),
		mi_inst_regs(2),
		mi_inst_regs(3),
		mi_inst_regs(4),
		mi_inst_regs(5),
};

static const struct dce_mem_input_shift mi_shifts = {
		MI_DCE11_2_MASK_SH_LIST(__SHIFT)
};

static const struct dce_mem_input_mask mi_masks = {
		MI_DCE11_2_MASK_SH_LIST(_MASK)
};

static struct mem_input *dce112_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offset)
{
	struct dce110_mem_input *mem_input110 =
		dm_alloc(sizeof(struct dce110_mem_input));

	if (!mem_input110)
		return NULL;

	if (dce112_mem_input_construct(mem_input110, ctx, inst, offset)) {
		struct mem_input *mi = &mem_input110->base;

		mi->regs = &mi_regs[inst];
		mi->shifts = &mi_shifts;
		mi->masks = &mi_masks;
		return mi;
	}

	BREAK_TO_DEBUGGER();
	dm_free(mem_input110);
	return NULL;
}

static void dce112_transform_destroy(struct transform **xfm)
{
	dm_free(TO_DCE_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce112_transform_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_transform *transform =
		dm_alloc(sizeof(struct dce_transform));

	if (!transform)
		return NULL;

	if (dce_transform_construct(transform, ctx, inst,
			&xfm_regs[inst], &xfm_shift, &xfm_mask)) {
		transform->lb_memory_size = 0x1404; /*5124*/
		return &transform->base;
	}

	BREAK_TO_DEBUGGER();
	dm_free(transform);
	return NULL;
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

struct link_encoder *dce112_link_encoder_create(
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

static struct input_pixel_processor *dce112_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dce_ipp *ipp = dm_alloc(sizeof(struct dce_ipp));

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}

struct output_pixel_processor *dce112_opp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce110_opp *opp =
		dm_alloc(sizeof(struct dce110_opp));

	if (!opp)
		return NULL;

	if (dce110_opp_construct(opp,
			ctx, inst, &opp_regs[inst], &opp_shift, &opp_mask))
		return &opp->base;

	BREAK_TO_DEBUGGER();
	dm_free(opp);
	return NULL;
}

struct clock_source *dce112_clock_source_create(
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

void dce112_clock_source_destroy(struct clock_source **clk_src)
{
	dm_free(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

static void destruct(struct dce110_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			dce110_opp_destroy(&pool->base.opps[i]);

		if (pool->base.transforms[i] != NULL)
			dce112_transform_destroy(&pool->base.transforms[i]);

		if (pool->base.ipps[i] != NULL)
			dce_ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.mis[i] != NULL) {
			dm_free(TO_DCE110_MEM_INPUT(pool->base.mis[i]));
			pool->base.mis[i] = NULL;
		}

		if (pool->base.timing_generators[i] != NULL) {
			dm_free(DCE110TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL)
			dm_free(DCE110STRENC_FROM_STRENC(pool->base.stream_enc[i]));
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dce112_clock_source_destroy(&pool->base.clock_sources[i]);
		}
	}

	if (pool->base.dp_clock_source != NULL)
		dce112_clock_source_destroy(&pool->base.dp_clock_source);

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

static struct clock_source *find_matching_pll(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct core_stream *const stream)
{
	switch (stream->sink->link->link_enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return pool->clock_sources[DCE112_CLK_SRC_PLL0];
	case TRANSMITTER_UNIPHY_B:
		return pool->clock_sources[DCE112_CLK_SRC_PLL1];
	case TRANSMITTER_UNIPHY_C:
		return pool->clock_sources[DCE112_CLK_SRC_PLL2];
	case TRANSMITTER_UNIPHY_D:
		return pool->clock_sources[DCE112_CLK_SRC_PLL3];
	case TRANSMITTER_UNIPHY_E:
		return pool->clock_sources[DCE112_CLK_SRC_PLL4];
	case TRANSMITTER_UNIPHY_F:
		return pool->clock_sources[DCE112_CLK_SRC_PLL5];
	default:
		return NULL;
	};

	return 0;
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

		if (resource_is_stream_unchanged(dc->current_context, stream))
			continue;

		for (j = 0; j < MAX_PIPES; j++) {
			struct pipe_ctx *pipe_ctx =
				&context->res_ctx.pipe_ctx[j];

			if (context->res_ctx.pipe_ctx[j].stream != stream)
				continue;

			if (!pipe_ctx->tg->funcs->validate_timing(
				pipe_ctx->tg, &stream->public.timing))
				return DC_FAIL_CONTROLLER_VALIDATE;

			status = dce110_resource_build_pipe_hw_param(pipe_ctx);

			if (status != DC_OK)
				return status;

			if (!link->link_enc->funcs->validate_output_with_stream(
				link->link_enc,
				pipe_ctx))
				return DC_FAIL_ENC_VALIDATE;

			/* TODO: validate audio ASIC caps, encoder */

			status = dc_link_validate_mode_timing(stream,
							      link,
							      &stream->public.timing);

			if (status != DC_OK)
				return status;

			resource_build_info_frame(pipe_ctx);

			/* do not need to validate non root pipes */
			break;
		}
	}

	return DC_OK;
}

bool dce112_validate_bandwidth(
	const struct core_dc *dc,
	struct validate_context *context)
{
	bool result = false;

	dm_logger_write(
		dc->ctx->logger, LOG_BANDWIDTH_CALCS,
		"%s: start",
		__func__);

	if (bw_calcs(
			dc->ctx,
			&dc->bw_dceip,
			&dc->bw_vbios,
			context->res_ctx.pipe_ctx,
			dc->res_pool->pipe_count,
			&context->bw_results))
		result = true;
	context->dispclk_khz = context->bw_results.dispclk_khz;

	if (!result)
		dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_VALIDATION,
			"%s: Bandwidth validation failed!",
			__func__);

	if (memcmp(&dc->current_context->bw_results,
			&context->bw_results, sizeof(context->bw_results))) {
		struct log_entry log_entry;
		dm_logger_open(
			dc->ctx->logger,
			&log_entry,
			LOG_BANDWIDTH_CALCS);
		dm_logger_append(&log_entry, "%s: finish,\n"
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			__func__,
			context->bw_results.nbp_state_change_wm_ns[0].b_mark,
			context->bw_results.nbp_state_change_wm_ns[0].a_mark,
			context->bw_results.urgent_wm_ns[0].b_mark,
			context->bw_results.urgent_wm_ns[0].a_mark,
			context->bw_results.stutter_exit_wm_ns[0].b_mark,
			context->bw_results.stutter_exit_wm_ns[0].a_mark);
		dm_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			context->bw_results.nbp_state_change_wm_ns[1].b_mark,
			context->bw_results.nbp_state_change_wm_ns[1].a_mark,
			context->bw_results.urgent_wm_ns[1].b_mark,
			context->bw_results.urgent_wm_ns[1].a_mark,
			context->bw_results.stutter_exit_wm_ns[1].b_mark,
			context->bw_results.stutter_exit_wm_ns[1].a_mark);
		dm_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d stutter_mode_enable: %d\n",
			context->bw_results.nbp_state_change_wm_ns[2].b_mark,
			context->bw_results.nbp_state_change_wm_ns[2].a_mark,
			context->bw_results.urgent_wm_ns[2].b_mark,
			context->bw_results.urgent_wm_ns[2].a_mark,
			context->bw_results.stutter_exit_wm_ns[2].b_mark,
			context->bw_results.stutter_exit_wm_ns[2].a_mark,
			context->bw_results.stutter_mode_enable);
		dm_logger_append(&log_entry,
			"cstate: %d pstate: %d nbpstate: %d sync: %d dispclk: %d\n"
			"sclk: %d sclk_sleep: %d yclk: %d blackout_recovery_time_us: %d\n",
			context->bw_results.cpuc_state_change_enable,
			context->bw_results.cpup_state_change_enable,
			context->bw_results.nbp_state_change_enable,
			context->bw_results.all_displays_in_sync,
			context->bw_results.dispclk_khz,
			context->bw_results.required_sclk,
			context->bw_results.required_sclk_deep_sleep,
			context->bw_results.required_yclk,
			context->bw_results.blackout_recovery_time_us);
		dm_logger_close(&log_entry);
	}
	return result;
}

enum dc_status resource_map_phy_clock_resources(
		const struct core_dc *dc,
		struct validate_context *context)
{
	uint8_t i, j;

	/* acquire new resources */
	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream = context->streams[i];

		if (resource_is_stream_unchanged(dc->current_context, stream))
			continue;

		for (j = 0; j < MAX_PIPES; j++) {
			struct pipe_ctx *pipe_ctx =
				&context->res_ctx.pipe_ctx[j];

			if (context->res_ctx.pipe_ctx[j].stream != stream)
				continue;

			if (dc_is_dp_signal(pipe_ctx->stream->signal)
				|| pipe_ctx->stream->signal == SIGNAL_TYPE_VIRTUAL)
				pipe_ctx->clock_source =
						dc->res_pool->dp_clock_source;
			else
				pipe_ctx->clock_source = find_matching_pll(
					&context->res_ctx, dc->res_pool,
					stream);

			if (pipe_ctx->clock_source == NULL)
				return DC_NO_CLOCK_SOURCE_RESOURCE;

			resource_reference_clock_source(
				&context->res_ctx,
				dc->res_pool,
				pipe_ctx->clock_source);

			/* only one cs per stream regardless of mpo */
			break;
		}
	}

	return DC_OK;
}

static bool dce112_validate_surface_sets(
		const struct dc_validation_set set[],
		int set_count)
{
	int i;

	for (i = 0; i < set_count; i++) {
		if (set[i].surface_count == 0)
			continue;

		if (set[i].surface_count > 1)
			return false;

		if (set[i].surfaces[0]->format
				>= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
			return false;
	}

	return true;
}

enum dc_status dce112_validate_with_context(
		const struct core_dc *dc,
		const struct dc_validation_set set[],
		int set_count,
		struct validate_context *context)
{
	struct dc_context *dc_ctx = dc->ctx;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	int i;

	if (!dce112_validate_surface_sets(set, set_count))
		return DC_FAIL_SURFACE_VALIDATE;

	for (i = 0; i < set_count; i++) {
		context->streams[i] = DC_STREAM_TO_CORE(set[i].stream);
		dc_stream_retain(&context->streams[i]->public);
		context->stream_count++;
	}

	result = resource_map_pool_resources(dc, context);

	if (result == DC_OK)
		result = resource_map_phy_clock_resources(dc, context);

	if (!resource_validate_attach_surfaces(set, set_count,
			dc->current_context, context, dc->res_pool)) {
		DC_ERROR("Failed to attach surface to stream!\n");
		return DC_FAIL_ATTACH_SURFACES;
	}

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		result = resource_build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		if (!dce112_validate_bandwidth(dc, context))
			result = DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

enum dc_status dce112_validate_guaranteed(
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

	if (result == DC_OK)
		if (!dce112_validate_bandwidth(dc, context))
			result = DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

static void dce112_destroy_resource_pool(struct resource_pool **pool)
{
	struct dce110_resource_pool *dce110_pool = TO_DCE110_RES_POOL(*pool);

	destruct(dce110_pool);
	dm_free(dce110_pool);
	*pool = NULL;
}

static const struct resource_funcs dce112_res_pool_funcs = {
	.destroy = dce112_destroy_resource_pool,
	.link_enc_create = dce112_link_encoder_create,
	.validate_with_context = dce112_validate_with_context,
	.validate_guaranteed = dce112_validate_guaranteed,
	.validate_bandwidth = dce112_validate_bandwidth,
};

static void bw_calcs_data_update_from_pplib(struct core_dc *dc)
{
	struct dm_pp_clock_levels_with_latency eng_clks = {0};
	struct dm_pp_clock_levels_with_latency mem_clks = {0};
	struct dm_pp_wm_sets_with_clock_ranges clk_ranges = {0};
	struct dm_pp_clock_levels clks = {0};

	/*do system clock  TODO PPLIB: after PPLIB implement,
	 * then remove old way
	 */
	if (!dm_pp_get_clock_levels_by_type_with_latency(
			dc->ctx,
			DM_PP_CLOCK_TYPE_ENGINE_CLK,
			&eng_clks)) {

		/* This is only for temporary */
		dm_pp_get_clock_levels_by_type(
				dc->ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK,
				&clks);
		/* convert all the clock fro kHz to fix point mHz */
		dc->bw_vbios.high_sclk = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels-1], 1000);
		dc->bw_vbios.mid1_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels/8], 1000);
		dc->bw_vbios.mid2_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels*2/8], 1000);
		dc->bw_vbios.mid3_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels*3/8], 1000);
		dc->bw_vbios.mid4_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels*4/8], 1000);
		dc->bw_vbios.mid5_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels*5/8], 1000);
		dc->bw_vbios.mid6_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[clks.num_levels*6/8], 1000);
		dc->bw_vbios.low_sclk  = bw_frc_to_fixed(
				clks.clocks_in_khz[0], 1000);

		/*do memory clock*/
		dm_pp_get_clock_levels_by_type(
				dc->ctx,
				DM_PP_CLOCK_TYPE_MEMORY_CLK,
				&clks);

		dc->bw_vbios.low_yclk = bw_frc_to_fixed(
			clks.clocks_in_khz[0] * MEMORY_TYPE_MULTIPLIER, 1000);
		dc->bw_vbios.mid_yclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels>>1] * MEMORY_TYPE_MULTIPLIER,
			1000);
		dc->bw_vbios.high_yclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels-1] * MEMORY_TYPE_MULTIPLIER,
			1000);

		return;
	}

	/* convert all the clock fro kHz to fix point mHz  TODO: wloop data */
	dc->bw_vbios.high_sclk = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels-1].clocks_in_khz, 1000);
	dc->bw_vbios.mid1_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels/8].clocks_in_khz, 1000);
	dc->bw_vbios.mid2_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*2/8].clocks_in_khz, 1000);
	dc->bw_vbios.mid3_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz, 1000);
	dc->bw_vbios.mid4_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*4/8].clocks_in_khz, 1000);
	dc->bw_vbios.mid5_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*5/8].clocks_in_khz, 1000);
	dc->bw_vbios.mid6_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*6/8].clocks_in_khz, 1000);
	dc->bw_vbios.low_sclk  = bw_frc_to_fixed(
			eng_clks.data[0].clocks_in_khz, 1000);

	/*do memory clock*/
	dm_pp_get_clock_levels_by_type_with_latency(
			dc->ctx,
			DM_PP_CLOCK_TYPE_MEMORY_CLK,
			&mem_clks);

	/* we don't need to call PPLIB for validation clock since they
	 * also give us the highest sclk and highest mclk (UMA clock).
	 * ALSO always convert UMA clock (from PPLIB)  to YCLK (HW formula):
	 * YCLK = UMACLK*m_memoryTypeMultiplier
	 */
	dc->bw_vbios.low_yclk = bw_frc_to_fixed(
		mem_clks.data[0].clocks_in_khz * MEMORY_TYPE_MULTIPLIER, 1000);
	dc->bw_vbios.mid_yclk = bw_frc_to_fixed(
		mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz * MEMORY_TYPE_MULTIPLIER,
		1000);
	dc->bw_vbios.high_yclk = bw_frc_to_fixed(
		mem_clks.data[mem_clks.num_levels-1].clocks_in_khz * MEMORY_TYPE_MULTIPLIER,
		1000);

	/* Now notify PPLib/SMU about which Watermarks sets they should select
	 * depending on DPM state they are in. And update BW MGR GFX Engine and
	 * Memory clock member variables for Watermarks calculations for each
	 * Watermark Set
	 */
	clk_ranges.num_wm_sets = 4;
	clk_ranges.wm_clk_ranges[0].wm_set_id = WM_SET_A;
	clk_ranges.wm_clk_ranges[0].wm_min_eng_clk_in_khz =
			eng_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[0].wm_max_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz - 1;
	clk_ranges.wm_clk_ranges[0].wm_min_memg_clk_in_khz =
			mem_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[0].wm_max_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz - 1;

	clk_ranges.wm_clk_ranges[1].wm_set_id = WM_SET_B;
	clk_ranges.wm_clk_ranges[1].wm_min_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz;
	/* 5 GHz instead of data[7].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[1].wm_max_eng_clk_in_khz = 5000000;
	clk_ranges.wm_clk_ranges[1].wm_min_memg_clk_in_khz =
			mem_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[1].wm_max_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz - 1;

	clk_ranges.wm_clk_ranges[2].wm_set_id = WM_SET_C;
	clk_ranges.wm_clk_ranges[2].wm_min_eng_clk_in_khz =
			eng_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[2].wm_max_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz - 1;
	clk_ranges.wm_clk_ranges[2].wm_min_memg_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz;
	/* 5 GHz instead of data[2].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[2].wm_max_mem_clk_in_khz = 5000000;

	clk_ranges.wm_clk_ranges[3].wm_set_id = WM_SET_D;
	clk_ranges.wm_clk_ranges[3].wm_min_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz;
	/* 5 GHz instead of data[7].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[3].wm_max_eng_clk_in_khz = 5000000;
	clk_ranges.wm_clk_ranges[3].wm_min_memg_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz;
	/* 5 GHz instead of data[2].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[3].wm_max_mem_clk_in_khz = 5000000;

	/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
	dm_pp_notify_wm_clock_changes(dc->ctx, &clk_ranges);
}

const struct resource_caps *dce112_resource_cap(
	struct hw_asic_id *asic_id)
{
	if (ASIC_REV_IS_POLARIS11_M(asic_id->hw_internal_rev) ||
	    ASIC_REV_IS_POLARIS12_V(asic_id->hw_internal_rev))
		return &polaris_11_resource_cap;
	else
		return &polaris_10_resource_cap;
}

static bool construct(
	uint8_t num_virtual_links,
	struct core_dc *dc,
	struct dce110_resource_pool *pool)
{
	unsigned int i;
	struct dc_context *ctx = dc->ctx;
	struct dm_pp_static_clock_info static_clk_info = {0};

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = dce112_resource_cap(&ctx->asic_id);
	pool->base.funcs = &dce112_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;
	dc->public.caps.max_downscale_ratio = 200;
	dc->public.caps.i2c_speed_in_khz = 100;
	dc->public.caps.max_cursor_size = 128;

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCE112_CLK_SRC_PLL0] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCE112_CLK_SRC_PLL1] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCE112_CLK_SRC_PLL2] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCE112_CLK_SRC_PLL3] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCE112_CLK_SRC_PLL4] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);
	pool->base.clock_sources[DCE112_CLK_SRC_PLL5] =
			dce112_clock_source_create(
				ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL5,
				&clk_src_regs[5], false);
	pool->base.clk_src_count = DCE112_CLK_SRC_TOTAL;

	pool->base.dp_clock_source =  dce112_clock_source_create(
		ctx, ctx->dc_bios,
		CLOCK_SOURCE_ID_DP_DTO, &clk_src_regs[0], true);


	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto res_create_fail;
		}
	}

	pool->base.display_clock = dce112_disp_clk_create(ctx,
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
		pool->base.timing_generators[i] =
				dce112_timing_generator_create(
					ctx,
					i,
					&dce112_tg_offsets[i]);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto res_create_fail;
		}

		pool->base.mis[i] = dce112_mem_input_create(
			ctx,
			i,
			&dce112_mi_reg_offsets[i]);
		if (pool->base.mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto res_create_fail;
		}

		pool->base.ipps[i] = dce112_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create input pixel processor!\n");
			goto res_create_fail;
		}

		pool->base.transforms[i] = dce112_transform_create(ctx, i);
		if (pool->base.transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto res_create_fail;
		}

		pool->base.opps[i] = dce112_opp_create(
			ctx,
			i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create output pixel processor!\n");
			goto res_create_fail;
		}
	}

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			  &res_create_funcs))
		goto res_create_fail;

	dc->public.caps.max_surfaces =  pool->base.pipe_count;

	/* Create hardware sequencer */
	if (!dce112_hw_sequencer_construct(dc))
		goto res_create_fail;

	bw_calcs_init(&dc->bw_dceip, &dc->bw_vbios, dc->ctx->asic_id);

	bw_calcs_data_update_from_pplib(dc);

	return true;

res_create_fail:
	destruct(pool);
	return false;
}

struct resource_pool *dce112_create_resource_pool(
	uint8_t num_virtual_links,
	struct core_dc *dc)
{
	struct dce110_resource_pool *pool =
		dm_alloc(sizeof(struct dce110_resource_pool));

	if (!pool)
		return NULL;

	if (construct(num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}
