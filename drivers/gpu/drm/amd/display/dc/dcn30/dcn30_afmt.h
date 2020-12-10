/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __DAL_DCN30_AFMT_H__
#define __DAL_DCN30_AFMT_H__


#define DCN30_AFMT_FROM_AFMT(afmt)\
	container_of(afmt, struct dcn30_afmt, base)

#define AFMT_DCN3_REG_LIST(id) \
	SRI(AFMT_INFOFRAME_CONTROL0, AFMT, id), \
	SRI(AFMT_VBI_PACKET_CONTROL, AFMT, id), \
	SRI(AFMT_AUDIO_PACKET_CONTROL, AFMT, id), \
	SRI(AFMT_AUDIO_PACKET_CONTROL2, AFMT, id), \
	SRI(AFMT_AUDIO_SRC_CONTROL, AFMT, id), \
	SRI(AFMT_60958_0, AFMT, id), \
	SRI(AFMT_60958_1, AFMT, id), \
	SRI(AFMT_60958_2, AFMT, id), \
	SRI(AFMT_MEM_PWR, AFMT, id)

struct dcn30_afmt_registers {
	uint32_t AFMT_INFOFRAME_CONTROL0;
	uint32_t AFMT_VBI_PACKET_CONTROL;
	uint32_t AFMT_AUDIO_PACKET_CONTROL;
	uint32_t AFMT_AUDIO_PACKET_CONTROL2;
	uint32_t AFMT_AUDIO_SRC_CONTROL;
	uint32_t AFMT_60958_0;
	uint32_t AFMT_60958_1;
	uint32_t AFMT_60958_2;
	uint32_t AFMT_MEM_PWR;
};

#define DCN3_AFMT_MASK_SH_LIST(mask_sh)\
	SE_SF(AFMT0_AFMT_INFOFRAME_CONTROL0, AFMT_AUDIO_INFO_UPDATE, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_SRC_CONTROL, AFMT_AUDIO_SRC_SELECT, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_PACKET_CONTROL2, AFMT_AUDIO_CHANNEL_ENABLE, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_PACKET_CONTROL, AFMT_60958_CS_UPDATE, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_PACKET_CONTROL2, AFMT_AUDIO_LAYOUT_OVRD, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_PACKET_CONTROL2, AFMT_60958_OSF_OVRD, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_0, AFMT_60958_CS_CHANNEL_NUMBER_L, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_0, AFMT_60958_CS_CLOCK_ACCURACY, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_1, AFMT_60958_CS_CHANNEL_NUMBER_R, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_2, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_3, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_4, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_5, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_6, mask_sh),\
	SE_SF(AFMT0_AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_7, mask_sh),\
	SE_SF(AFMT0_AFMT_AUDIO_PACKET_CONTROL, AFMT_AUDIO_SAMPLE_SEND, mask_sh),\
	SE_SF(AFMT0_AFMT_MEM_PWR, AFMT_MEM_PWR_FORCE, mask_sh)

#define AFMT_DCN3_REG_FIELD_LIST(type) \
		type AFMT_AUDIO_INFO_UPDATE;\
		type AFMT_AUDIO_SRC_SELECT;\
		type AFMT_AUDIO_CHANNEL_ENABLE;\
		type AFMT_60958_CS_UPDATE;\
		type AFMT_AUDIO_LAYOUT_OVRD;\
		type AFMT_60958_OSF_OVRD;\
		type AFMT_60958_CS_CHANNEL_NUMBER_L;\
		type AFMT_60958_CS_CLOCK_ACCURACY;\
		type AFMT_60958_CS_CHANNEL_NUMBER_R;\
		type AFMT_60958_CS_CHANNEL_NUMBER_2;\
		type AFMT_60958_CS_CHANNEL_NUMBER_3;\
		type AFMT_60958_CS_CHANNEL_NUMBER_4;\
		type AFMT_60958_CS_CHANNEL_NUMBER_5;\
		type AFMT_60958_CS_CHANNEL_NUMBER_6;\
		type AFMT_60958_CS_CHANNEL_NUMBER_7;\
		type AFMT_AUDIO_SAMPLE_SEND;\
		type AFMT_MEM_PWR_FORCE

struct dcn30_afmt_shift {
	AFMT_DCN3_REG_FIELD_LIST(uint8_t);
};

struct dcn30_afmt_mask {
	AFMT_DCN3_REG_FIELD_LIST(uint32_t);
};


struct afmt;

struct afmt_funcs {

	void (*setup_hdmi_audio)(
		struct afmt *afmt);

	void (*se_audio_setup)(
		struct afmt *afmt,
		unsigned int az_inst,
		struct audio_info *audio_info);

	void (*audio_mute_control)(
		struct afmt *afmt,
		bool mute);

	void (*audio_info_immediate_update)(
		struct afmt *afmt);

	void (*setup_dp_audio)(
		struct afmt *afmt);
};

struct afmt {
	const struct afmt_funcs *funcs;
	struct dc_context *ctx;
	int inst;
};

struct dcn30_afmt {
	struct afmt base;
	const struct dcn30_afmt_registers *regs;
	const struct dcn30_afmt_shift *afmt_shift;
	const struct dcn30_afmt_mask *afmt_mask;
};

void afmt3_construct(struct dcn30_afmt *afmt3,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn30_afmt_registers *afmt_regs,
	const struct dcn30_afmt_shift *afmt_shift,
	const struct dcn30_afmt_mask *afmt_mask);


#endif
