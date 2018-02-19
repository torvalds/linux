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
#ifndef __DAL_AUDIO_DCE_110_H__
#define __DAL_AUDIO_DCE_110_H__

#include "audio.h"

#define AUD_COMMON_REG_LIST(id)\
	SRI(AZALIA_F0_CODEC_ENDPOINT_INDEX, AZF0ENDPOINT, id),\
	SRI(AZALIA_F0_CODEC_ENDPOINT_DATA, AZF0ENDPOINT, id),\
	SR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS),\
	SR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES),\
	SR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES),\
	SR(DCCG_AUDIO_DTO_SOURCE),\
	SR(DCCG_AUDIO_DTO0_MODULE),\
	SR(DCCG_AUDIO_DTO0_PHASE),\
	SR(DCCG_AUDIO_DTO1_MODULE),\
	SR(DCCG_AUDIO_DTO1_PHASE)


 /* set field name */
#define SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix


#define AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_SOURCE_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO2_USE_512FBR_DTO, mask_sh),\
		SF(DCCG_AUDIO_DTO0_MODULE, DCCG_AUDIO_DTO0_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO0_PHASE, DCCG_AUDIO_DTO0_PHASE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_MODULE, DCCG_AUDIO_DTO1_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_PHASE, DCCG_AUDIO_DTO1_PHASE, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES, AUDIO_RATE_CAPABILITIES, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, CLKSTOP, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, EPSS, mask_sh)

#define AUD_COMMON_MASK_SH_LIST(mask_sh)\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh),\
		SF(AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh)


struct dce_audio_registers {
	uint32_t AZALIA_F0_CODEC_ENDPOINT_INDEX;
	uint32_t AZALIA_F0_CODEC_ENDPOINT_DATA;

	uint32_t AZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS;
	uint32_t AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES;
	uint32_t AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES;

	uint32_t DCCG_AUDIO_DTO_SOURCE;
	uint32_t DCCG_AUDIO_DTO0_MODULE;
	uint32_t DCCG_AUDIO_DTO0_PHASE;
	uint32_t DCCG_AUDIO_DTO1_MODULE;
	uint32_t DCCG_AUDIO_DTO1_PHASE;

	uint32_t AUDIO_RATE_CAPABILITIES;
};

struct dce_audio_shift {
	uint8_t AZALIA_ENDPOINT_REG_INDEX;
	uint8_t AZALIA_ENDPOINT_REG_DATA;

	uint8_t AUDIO_RATE_CAPABILITIES;
	uint8_t CLKSTOP;
	uint8_t EPSS;

	uint8_t DCCG_AUDIO_DTO0_SOURCE_SEL;
	uint8_t DCCG_AUDIO_DTO_SEL;
	uint8_t DCCG_AUDIO_DTO0_MODULE;
	uint8_t DCCG_AUDIO_DTO0_PHASE;
	uint8_t DCCG_AUDIO_DTO1_MODULE;
	uint8_t DCCG_AUDIO_DTO1_PHASE;
	uint8_t DCCG_AUDIO_DTO2_USE_512FBR_DTO;
};

struct dce_aduio_mask {
	uint32_t AZALIA_ENDPOINT_REG_INDEX;
	uint32_t AZALIA_ENDPOINT_REG_DATA;

	uint32_t AUDIO_RATE_CAPABILITIES;
	uint32_t CLKSTOP;
	uint32_t EPSS;

	uint32_t DCCG_AUDIO_DTO0_SOURCE_SEL;
	uint32_t DCCG_AUDIO_DTO_SEL;
	uint32_t DCCG_AUDIO_DTO0_MODULE;
	uint32_t DCCG_AUDIO_DTO0_PHASE;
	uint32_t DCCG_AUDIO_DTO1_MODULE;
	uint32_t DCCG_AUDIO_DTO1_PHASE;
	uint32_t DCCG_AUDIO_DTO2_USE_512FBR_DTO;
};

struct dce_audio {
	struct audio base;
	const struct dce_audio_registers *regs;
	const struct dce_audio_shift *shifts;
	const struct dce_aduio_mask *masks;
};

struct audio *dce_audio_create(
		struct dc_context *ctx,
		unsigned int inst,
		const struct dce_audio_registers *reg,
		const struct dce_audio_shift *shifts,
		const struct dce_aduio_mask *masks);

void dce_aud_destroy(struct audio **audio);

void dce_aud_hw_init(struct audio *audio);

void dce_aud_az_enable(struct audio *audio);
void dce_aud_az_disable(struct audio *audio);

void dce_aud_az_configure(struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_info *audio_info);

void dce_aud_wall_dto_setup(struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info);

#endif   /*__DAL_AUDIO_DCE_110_H__*/
