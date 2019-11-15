/*
* Copyright 2012-15 Advanced Micro Devices, Inc.cls
*
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


#include "stream_encoder.h"
#include "resource.h"
#include "include/irq_service_interface.h"
#include "dce120_resource.h"

#include "dce112/dce112_resource.h"

#include "dce110/dce110_resource.h"
#include "../virtual/virtual_stream_encoder.h"
#include "dce120_timing_generator.h"
#include "irq/dce120/irq_service_dce120.h"
#include "dce/dce_opp.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_ipp.h"
#include "dce/dce_mem_input.h"

#include "dce110/dce110_hw_sequencer.h"
#include "dce120/dce120_hw_sequencer.h"
#include "dce/dce_transform.h"
#include "clk_mgr.h"
#include "dce/dce_audio.h"
#include "dce/dce_link_encoder.h"
#include "dce/dce_stream_encoder.h"
#include "dce/dce_hwseq.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"
#include "nbio/nbio_6_1_offset.h"
#include "mmhub/mmhub_9_4_0_offset.h"
#include "mmhub/mmhub_9_4_0_sh_mask.h"
#include "reg_helper.h"

#include "dce100/dce100_resource.h"

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

enum dce120_clk_src_array_id {
	DCE120_CLK_SRC_PLL0,
	DCE120_CLK_SRC_PLL1,
	DCE120_CLK_SRC_PLL2,
	DCE120_CLK_SRC_PLL3,
	DCE120_CLK_SRC_PLL4,
	DCE120_CLK_SRC_PLL5,

	DCE120_CLK_SRC_TOTAL
};

static const struct dce110_timing_generator_offsets dce120_tg_offsets[] = {
	{
		.crtc = (mmCRTC0_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	},
	{
		.crtc = (mmCRTC1_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	},
	{
		.crtc = (mmCRTC2_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	},
	{
		.crtc = (mmCRTC3_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	},
	{
		.crtc = (mmCRTC4_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	},
	{
		.crtc = (mmCRTC5_CRTC_CONTROL - mmCRTC0_CRTC_CONTROL),
	}
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

#define BASE_INNER(seg) \
	DCE_BASE__INST0_SEG ## seg

#define NBIO_BASE_INNER(seg) \
	NBIF_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

/* compile time expand base address. */
#define BASE(seg) \
	BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

/* MMHUB */
#define MMHUB_BASE_INNER(seg) \
	MMHUB_BASE__INST0_SEG ## seg

#define MMHUB_BASE(seg) \
	MMHUB_BASE_INNER(seg)

#define MMHUB_SR(reg_name)\
		.reg_name = MMHUB_BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

/* macros to expend register list macro defined in HW object header file
 * end *********************/


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
		IPP_DCE120_MASK_SH_LIST_SOC_BASE(__SHIFT)
};

static const struct dce_ipp_mask ipp_mask = {
		IPP_DCE120_MASK_SH_LIST_SOC_BASE(_MASK)
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
		XFM_COMMON_MASK_SH_LIST_SOC_BASE(__SHIFT)
};

static const struct dce_transform_mask xfm_mask = {
		XFM_COMMON_MASK_SH_LIST_SOC_BASE(_MASK)
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
	LE_DCE120_REG_LIST(id), \
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
		SE_COMMON_MASK_SH_LIST_DCE120(__SHIFT)
};

static const struct dce_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCE120(_MASK)
};

static const struct dce110_aux_registers_shift aux_shift = {
	DCE12_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCE12_AUX_MASK_SH_LIST(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_DCE_120_REG_LIST(id),\
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
	OPP_COMMON_MASK_SH_LIST_DCE_120(__SHIFT)
};

static const struct dce_opp_mask opp_mask = {
	OPP_COMMON_MASK_SH_LIST_DCE_120(_MASK)
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
	audio_regs(5)
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
	case TRANSMITTER_UNIPHY_F:
		return 5;
	break;
	case TRANSMITTER_UNIPHY_G:
		return 6;
	break;
	default:
		ASSERT(0);
		return 0;
	}
}

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

struct output_pixel_processor *dce120_opp_create(
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
struct dce_aux *dce120_aux_engine_create(
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
		i2c_inst_regs(6),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCE110(_MASK)
};

struct dce_i2c_hw *dce120_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dce112_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}
static const struct bios_registers bios_regs = {
	.BIOS_SCRATCH_3 = mmBIOS_SCRATCH_3 + NBIO_BASE(mmBIOS_SCRATCH_3_BASE_IDX),
	.BIOS_SCRATCH_6 = mmBIOS_SCRATCH_6 + NBIO_BASE(mmBIOS_SCRATCH_6_BASE_IDX)
};

static const struct resource_caps res_cap = {
		.num_timing_generator = 6,
		.num_audio = 7,
		.num_stream_encoder = 6,
		.num_pll = 6,
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

static const struct dc_debug_options debug_defaults = {
		.disable_clock_gate = true,
};

static struct clock_source *dce120_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(*clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dce112_clk_src_construct(clk_src, ctx, bios, id,
				     regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static void dce120_clock_source_destroy(struct clock_source **clk_src)
{
	kfree(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}


static bool dce120_hw_sequencer_create(struct dc *dc)
{
	/* All registers used by dce11.2 match those in dce11 in offset and
	 * structure
	 */
	dce120_hw_sequencer_construct(dc);

	/*TODO	Move to separate file and Override what is needed */

	return true;
}

static struct timing_generator *dce120_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		kzalloc(sizeof(struct dce110_timing_generator), GFP_KERNEL);

	if (!tg110)
		return NULL;

	dce120_timing_generator_construct(tg110, ctx, instance, offsets);
	return &tg110->base;
}

static void dce120_transform_destroy(struct transform **xfm)
{
	kfree(TO_DCE_TRANSFORM(*xfm));
	*xfm = NULL;
}

static void destruct(struct dce110_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			dce110_opp_destroy(&pool->base.opps[i]);

		if (pool->base.transforms[i] != NULL)
			dce120_transform_destroy(&pool->base.transforms[i]);

		if (pool->base.ipps[i] != NULL)
			dce_ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.mis[i] != NULL) {
			kfree(TO_DCE_MEM_INPUT(pool->base.mis[i]));
			pool->base.mis[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}

		if (pool->base.timing_generators[i] != NULL) {
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

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL)
			kfree(DCE110STRENC_FROM_STRENC(pool->base.stream_enc[i]));
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL)
			dce120_clock_source_destroy(
				&pool->base.clock_sources[i]);
	}

	if (pool->base.dp_clock_source != NULL)
		dce120_clock_source_destroy(&pool->base.dp_clock_source);

	if (pool->base.abm != NULL)
		dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	uint32_t reg_val = dm_read_reg_soc15(ctx, mmCC_DC_MISC_STRAPS, 0);

	straps->audio_stream_number = get_reg_field_value(reg_val,
							  CC_DC_MISC_STRAPS,
							  AUDIO_STREAM_NUMBER);
	straps->hdmi_disable = get_reg_field_value(reg_val,
						   CC_DC_MISC_STRAPS,
						   HDMI_DISABLE);

	reg_val = dm_read_reg_soc15(ctx, mmDC_PINSTRAPS, 0);
	straps->dc_pinstraps_audio = get_reg_field_value(reg_val,
							 DC_PINSTRAPS,
							 DC_PINSTRAPS_AUDIO);
}

static struct audio *create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = false,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true,
};

static struct link_encoder *dce120_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		kzalloc(sizeof(struct dce110_link_encoder), GFP_KERNEL);
	int link_regs_id;

	if (!enc110)
		return NULL;

	link_regs_id =
		map_transmitter_id_to_phy_instance(enc_init_data->transmitter);

	dce110_link_encoder_construct(enc110,
				      enc_init_data,
				      &link_enc_feature,
				      &link_enc_regs[link_regs_id],
				      &link_enc_aux_regs[enc_init_data->channel - 1],
				      &link_enc_hpd_regs[enc_init_data->hpd_source]);

	return &enc110->base;
}

static struct input_pixel_processor *dce120_ipp_create(
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

static struct stream_encoder *dce120_stream_encoder_create(
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
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCE120_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCE12_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCE12_MASK_SH_LIST(_MASK)
};

/* HWSEQ regs for VG20 */
static const struct dce_hwseq_registers dce121_hwseq_reg = {
		HWSEQ_VG20_REG_LIST()
};

static const struct dce_hwseq_shift dce121_hwseq_shift = {
		HWSEQ_VG20_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask dce121_hwseq_mask = {
		HWSEQ_VG20_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dce120_hwseq_create(
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

static struct dce_hwseq *dce121_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &dce121_hwseq_reg;
		hws->shifts = &dce121_hwseq_shift;
		hws->masks = &dce121_hwseq_mask;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dce120_stream_encoder_create,
	.create_hwseq = dce120_hwseq_create,
};

static const struct resource_create_funcs dce121_res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dce120_stream_encoder_create,
	.create_hwseq = dce121_hwseq_create,
};


#define mi_inst_regs(id) { MI_DCE12_REG_LIST(id) }
static const struct dce_mem_input_registers mi_regs[] = {
		mi_inst_regs(0),
		mi_inst_regs(1),
		mi_inst_regs(2),
		mi_inst_regs(3),
		mi_inst_regs(4),
		mi_inst_regs(5),
};

static const struct dce_mem_input_shift mi_shifts = {
		MI_DCE12_MASK_SH_LIST(__SHIFT)
};

static const struct dce_mem_input_mask mi_masks = {
		MI_DCE12_MASK_SH_LIST(_MASK)
};

static struct mem_input *dce120_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_mem_input *dce_mi = kzalloc(sizeof(struct dce_mem_input),
					       GFP_KERNEL);

	if (!dce_mi) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce120_mem_input_construct(dce_mi, ctx, inst, &mi_regs[inst], &mi_shifts, &mi_masks);
	return &dce_mi->base;
}

static struct transform *dce120_transform_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_transform *transform =
		kzalloc(sizeof(struct dce_transform), GFP_KERNEL);

	if (!transform)
		return NULL;

	dce_transform_construct(transform, ctx, inst,
				&xfm_regs[inst], &xfm_shift, &xfm_mask);
	transform->lb_memory_size = 0x1404; /*5124*/
	return &transform->base;
}

static void dce120_destroy_resource_pool(struct resource_pool **pool)
{
	struct dce110_resource_pool *dce110_pool = TO_DCE110_RES_POOL(*pool);

	destruct(dce110_pool);
	kfree(dce110_pool);
	*pool = NULL;
}

static const struct resource_funcs dce120_res_pool_funcs = {
	.destroy = dce120_destroy_resource_pool,
	.link_enc_create = dce120_link_encoder_create,
	.validate_bandwidth = dce112_validate_bandwidth,
	.validate_plane = dce100_validate_plane,
	.add_stream_to_ctx = dce112_add_stream_to_ctx,
	.find_first_free_match_stream_enc_for_link = dce110_find_first_free_match_stream_enc_for_link
};

static void bw_calcs_data_update_from_pplib(struct dc *dc)
{
	struct dm_pp_clock_levels_with_latency eng_clks = {0};
	struct dm_pp_clock_levels_with_latency mem_clks = {0};
	struct dm_pp_wm_sets_with_clock_ranges clk_ranges = {0};
	int i;
	unsigned int clk;
	unsigned int latency;
	/*original logic in dal3*/
	int memory_type_multiplier = MEMORY_TYPE_MULTIPLIER_CZ;

	/*do system clock*/
	if (!dm_pp_get_clock_levels_by_type_with_latency(
				dc->ctx,
				DM_PP_CLOCK_TYPE_ENGINE_CLK,
				&eng_clks) || eng_clks.num_levels == 0) {

		eng_clks.num_levels = 8;
		clk = 300000;

		for (i = 0; i < eng_clks.num_levels; i++) {
			eng_clks.data[i].clocks_in_khz = clk;
			clk += 100000;
		}
	}

	/* convert all the clock fro kHz to fix point mHz  TODO: wloop data */
	dc->bw_vbios->high_sclk = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels-1].clocks_in_khz, 1000);
	dc->bw_vbios->mid1_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels/8].clocks_in_khz, 1000);
	dc->bw_vbios->mid2_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*2/8].clocks_in_khz, 1000);
	dc->bw_vbios->mid3_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz, 1000);
	dc->bw_vbios->mid4_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*4/8].clocks_in_khz, 1000);
	dc->bw_vbios->mid5_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*5/8].clocks_in_khz, 1000);
	dc->bw_vbios->mid6_sclk  = bw_frc_to_fixed(
		eng_clks.data[eng_clks.num_levels*6/8].clocks_in_khz, 1000);
	dc->bw_vbios->low_sclk  = bw_frc_to_fixed(
			eng_clks.data[0].clocks_in_khz, 1000);

	/*do memory clock*/
	if (!dm_pp_get_clock_levels_by_type_with_latency(
			dc->ctx,
			DM_PP_CLOCK_TYPE_MEMORY_CLK,
			&mem_clks) || mem_clks.num_levels == 0) {

		mem_clks.num_levels = 3;
		clk = 250000;
		latency = 45;

		for (i = 0; i < eng_clks.num_levels; i++) {
			mem_clks.data[i].clocks_in_khz = clk;
			mem_clks.data[i].latency_in_us = latency;
			clk += 500000;
			latency -= 5;
		}

	}

	/* we don't need to call PPLIB for validation clock since they
	 * also give us the highest sclk and highest mclk (UMA clock).
	 * ALSO always convert UMA clock (from PPLIB)  to YCLK (HW formula):
	 * YCLK = UMACLK*m_memoryTypeMultiplier
	 */
	if (dc->bw_vbios->memory_type == bw_def_hbm)
		memory_type_multiplier = MEMORY_TYPE_HBM;

	dc->bw_vbios->low_yclk = bw_frc_to_fixed(
		mem_clks.data[0].clocks_in_khz * memory_type_multiplier, 1000);
	dc->bw_vbios->mid_yclk = bw_frc_to_fixed(
		mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz * memory_type_multiplier,
		1000);
	dc->bw_vbios->high_yclk = bw_frc_to_fixed(
		mem_clks.data[mem_clks.num_levels-1].clocks_in_khz * memory_type_multiplier,
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
	clk_ranges.wm_clk_ranges[0].wm_min_mem_clk_in_khz =
			mem_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[0].wm_max_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz - 1;

	clk_ranges.wm_clk_ranges[1].wm_set_id = WM_SET_B;
	clk_ranges.wm_clk_ranges[1].wm_min_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz;
	/* 5 GHz instead of data[7].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[1].wm_max_eng_clk_in_khz = 5000000;
	clk_ranges.wm_clk_ranges[1].wm_min_mem_clk_in_khz =
			mem_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[1].wm_max_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz - 1;

	clk_ranges.wm_clk_ranges[2].wm_set_id = WM_SET_C;
	clk_ranges.wm_clk_ranges[2].wm_min_eng_clk_in_khz =
			eng_clks.data[0].clocks_in_khz;
	clk_ranges.wm_clk_ranges[2].wm_max_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz - 1;
	clk_ranges.wm_clk_ranges[2].wm_min_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz;
	/* 5 GHz instead of data[2].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[2].wm_max_mem_clk_in_khz = 5000000;

	clk_ranges.wm_clk_ranges[3].wm_set_id = WM_SET_D;
	clk_ranges.wm_clk_ranges[3].wm_min_eng_clk_in_khz =
			eng_clks.data[eng_clks.num_levels*3/8].clocks_in_khz;
	/* 5 GHz instead of data[7].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[3].wm_max_eng_clk_in_khz = 5000000;
	clk_ranges.wm_clk_ranges[3].wm_min_mem_clk_in_khz =
			mem_clks.data[mem_clks.num_levels>>1].clocks_in_khz;
	/* 5 GHz instead of data[2].clockInKHz to cover Overdrive */
	clk_ranges.wm_clk_ranges[3].wm_max_mem_clk_in_khz = 5000000;

	/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
	dm_pp_notify_wm_clock_changes(dc->ctx, &clk_ranges);
}

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = dm_read_reg_soc15(ctx, mmCC_DC_PIPE_DIS, 0);
	/* VG20 support max 6 pipes */
	value = value & 0x3f;
	return value;
}

static bool construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dce110_resource_pool *pool)
{
	unsigned int i;
	int j;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data irq_init_data;
	static const struct resource_create_funcs *res_funcs;
	bool is_vg20 = ASICREV_IS_VEGA20_P(ctx->asic_id.hw_internal_rev);
	uint32_t pipe_fuses;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap;
	pool->base.funcs = &dce120_res_pool_funcs;

	/* TODO: Fill more data from GreenlandAsicCapability.cpp */
	pool->base.pipe_count = res_cap.num_timing_generator;
	pool->base.timing_generator_count = pool->base.res_cap->num_timing_generator;
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.max_cursor_size = 128;
	dc->caps.dual_link_dvi = true;
	dc->caps.psp_setup_panel_mode = true;
	dc->caps.extended_aux_timeout_support = false;
	dc->debug = debug_defaults;

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCE120_CLK_SRC_PLL0] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCE120_CLK_SRC_PLL1] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCE120_CLK_SRC_PLL2] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCE120_CLK_SRC_PLL3] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCE120_CLK_SRC_PLL4] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);
	pool->base.clock_sources[DCE120_CLK_SRC_PLL5] =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL5,
				&clk_src_regs[5], false);
	pool->base.clk_src_count = DCE120_CLK_SRC_TOTAL;

	pool->base.dp_clock_source =
			dce120_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
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


	irq_init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dce120_create(&irq_init_data);
	if (!pool->base.irqs)
		goto irqs_create_fail;

	/* VG20: Pipe harvesting enabled, retrieve valid pipe fuses */
	if (is_vg20)
		pipe_fuses = read_pipe_fuses(ctx);

	/* index to valid pipe resource */
	j = 0;
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (is_vg20) {
			if ((pipe_fuses & (1 << i)) != 0) {
				dm_error("DC: skip invalid pipe %d!\n", i);
				continue;
			}
		}

		pool->base.timing_generators[j] =
				dce120_timing_generator_create(
					ctx,
					i,
					&dce120_tg_offsets[i]);
		if (pool->base.timing_generators[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->base.mis[j] = dce120_mem_input_create(ctx, i);

		if (pool->base.mis[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->base.ipps[j] = dce120_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->base.transforms[j] = dce120_transform_create(ctx, i);
		if (pool->base.transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto res_create_fail;
		}

		pool->base.opps[j] = dce120_opp_create(
			ctx,
			i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
		}

		/* check next valid pipe */
		j++;
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dce120_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto res_create_fail;
		}
		pool->base.hw_i2cs[i] = dce120_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create i2c engine!!\n");
			goto res_create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* valid pipe num */
	pool->base.pipe_count = j;
	pool->base.timing_generator_count = j;

	if (is_vg20)
		res_funcs = &dce121_res_create_funcs;
	else
		res_funcs = &res_create_funcs;

	if (!resource_construct(num_virtual_links, dc, &pool->base, res_funcs))
		goto res_create_fail;

	/* Create hardware sequencer */
	if (!dce120_hw_sequencer_create(dc))
		goto controller_create_fail;

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	bw_calcs_init(dc->bw_dceip, dc->bw_vbios, dc->ctx->asic_id);

	bw_calcs_data_update_from_pplib(dc);

	return true;

irqs_create_fail:
controller_create_fail:
clk_src_create_fail:
res_create_fail:

	destruct(pool);

	return false;
}

struct resource_pool *dce120_create_resource_pool(
	uint8_t num_virtual_links,
	struct dc *dc)
{
	struct dce110_resource_pool *pool =
		kzalloc(sizeof(struct dce110_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (construct(num_virtual_links, dc, pool))
		return &pool->base;

	kfree(pool);
	BREAK_TO_DEBUGGER();
	return NULL;
}
