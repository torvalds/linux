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

#ifndef __DAL_DCN31_AGP_H__
#define __DAL_DCN31_AGP_H__


#define DCN31_APG_FROM_APG(apg)\
	container_of(apg, struct dcn31_apg, base)

#define APG_DCN31_REG_LIST(id) \
	SRI(APG_CONTROL, APG, id), \
	SRI(APG_CONTROL2, APG, id),\
	SRI(APG_MEM_PWR, APG, id),\
	SRI(APG_DBG_GEN_CONTROL, APG, id)

struct dcn31_apg_registers {
	uint32_t APG_CONTROL;
	uint32_t APG_CONTROL2;
	uint32_t APG_MEM_PWR;
	uint32_t APG_DBG_GEN_CONTROL;
};


#define DCN31_APG_MASK_SH_LIST(mask_sh)\
	SE_SF(APG0_APG_CONTROL, APG_RESET, mask_sh),\
	SE_SF(APG0_APG_CONTROL, APG_RESET_DONE, mask_sh),\
	SE_SF(APG0_APG_CONTROL2, APG_ENABLE, mask_sh),\
	SE_SF(APG0_APG_CONTROL2, APG_DP_AUDIO_STREAM_ID, mask_sh),\
	SE_SF(APG0_APG_DBG_GEN_CONTROL, APG_DBG_AUDIO_CHANNEL_ENABLE, mask_sh),\
	SE_SF(APG0_APG_MEM_PWR, APG_MEM_PWR_FORCE, mask_sh)

#define APG_DCN31_REG_FIELD_LIST(type) \
		type APG_RESET;\
		type APG_RESET_DONE;\
		type APG_ENABLE;\
		type APG_DP_AUDIO_STREAM_ID;\
		type APG_DBG_AUDIO_CHANNEL_ENABLE;\
		type APG_MEM_PWR_FORCE

struct dcn31_apg_shift {
	APG_DCN31_REG_FIELD_LIST(uint8_t);
};

struct dcn31_apg_mask {
	APG_DCN31_REG_FIELD_LIST(uint32_t);
};

struct apg {
	const struct apg_funcs *funcs;
	struct dc_context *ctx;
	int inst;
};

struct apg_funcs {

	void (*setup_hdmi_audio)(
		struct apg *apg);

	void (*se_audio_setup)(
		struct apg *apg,
		unsigned int az_inst,
		struct audio_info *audio_info);

	void (*enable_apg)(
		struct apg *apg);

	void (*disable_apg)(
		struct apg *apg);
};



struct dcn31_apg {
	struct apg base;
	const struct dcn31_apg_registers *regs;
	const struct dcn31_apg_shift *apg_shift;
	const struct dcn31_apg_mask *apg_mask;
};

void apg31_construct(struct dcn31_apg *apg3,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn31_apg_registers *apg_regs,
	const struct dcn31_apg_shift *apg_shift,
	const struct dcn31_apg_mask *apg_mask);


#endif
