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
#ifndef __DCE_HWSEQ_H__
#define __DCE_HWSEQ_H__

#include "hw_sequencer.h"

#define HWSEQ_DCEF_REG_LIST_DCE8() \
	.DCFE_CLOCK_CONTROL[0] = mmCRTC0_CRTC_DCFE_CLOCK_CONTROL, \
	.DCFE_CLOCK_CONTROL[1] = mmCRTC1_CRTC_DCFE_CLOCK_CONTROL, \
	.DCFE_CLOCK_CONTROL[2] = mmCRTC2_CRTC_DCFE_CLOCK_CONTROL, \
	.DCFE_CLOCK_CONTROL[3] = mmCRTC3_CRTC_DCFE_CLOCK_CONTROL, \
	.DCFE_CLOCK_CONTROL[4] = mmCRTC4_CRTC_DCFE_CLOCK_CONTROL, \
	.DCFE_CLOCK_CONTROL[5] = mmCRTC5_CRTC_DCFE_CLOCK_CONTROL

#define HWSEQ_DCEF_REG_LIST() \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 0), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 1), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 2), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 3), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 4), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 5), \
	SR(DC_MEM_GLOBAL_PWR_REQ_CNTL)

#define HWSEQ_BLND_REG_LIST() \
	SRII(BLND_V_UPDATE_LOCK, BLND, 0), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 1), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 2), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 3), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 4), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 5), \
	SRII(BLND_CONTROL, BLND, 0), \
	SRII(BLND_CONTROL, BLND, 1), \
	SRII(BLND_CONTROL, BLND, 2), \
	SRII(BLND_CONTROL, BLND, 3), \
	SRII(BLND_CONTROL, BLND, 4), \
	SRII(BLND_CONTROL, BLND, 5)

#define HWSEQ_PIXEL_RATE_REG_LIST(blk) \
	SRII(PIXEL_RATE_CNTL, blk, 0), \
	SRII(PIXEL_RATE_CNTL, blk, 1), \
	SRII(PIXEL_RATE_CNTL, blk, 2), \
	SRII(PIXEL_RATE_CNTL, blk, 3), \
	SRII(PIXEL_RATE_CNTL, blk, 4), \
	SRII(PIXEL_RATE_CNTL, blk, 5)

#define HWSEQ_PHYPLL_REG_LIST(blk) \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 0), \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 1), \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 2), \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 3), \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 4), \
	SRII(PHYPLL_PIXEL_RATE_CNTL, blk, 5)

#define HWSEQ_DCE11_REG_LIST_BASE() \
	SR(DC_MEM_GLOBAL_PWR_REQ_CNTL), \
	SR(DCFEV_CLOCK_CONTROL), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 0), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 1), \
	SRII(CRTC_H_BLANK_START_END, CRTC, 0),\
	SRII(CRTC_H_BLANK_START_END, CRTC, 1),\
	SRII(BLND_V_UPDATE_LOCK, BLND, 0),\
	SRII(BLND_V_UPDATE_LOCK, BLND, 1),\
	SRII(BLND_CONTROL, BLND, 0),\
	SRII(BLND_CONTROL, BLND, 1),\
	SR(BLNDV_CONTROL),\
	HWSEQ_PIXEL_RATE_REG_LIST(CRTC)

#define HWSEQ_DCE8_REG_LIST() \
	HWSEQ_DCEF_REG_LIST_DCE8(), \
	HWSEQ_BLND_REG_LIST(), \
	HWSEQ_PIXEL_RATE_REG_LIST(CRTC)

#define HWSEQ_DCE10_REG_LIST() \
	HWSEQ_DCEF_REG_LIST(), \
	HWSEQ_BLND_REG_LIST(), \
	HWSEQ_PIXEL_RATE_REG_LIST(CRTC)

#define HWSEQ_ST_REG_LIST() \
	HWSEQ_DCE11_REG_LIST_BASE(), \
	.DCFE_CLOCK_CONTROL[2] = mmDCFEV_CLOCK_CONTROL, \
	.CRTC_H_BLANK_START_END[2] = mmCRTCV_H_BLANK_START_END, \
	.BLND_V_UPDATE_LOCK[2] = mmBLNDV_V_UPDATE_LOCK, \
	.BLND_CONTROL[2] = mmBLNDV_CONTROL,

#define HWSEQ_CZ_REG_LIST() \
	HWSEQ_DCE11_REG_LIST_BASE(), \
	SRII(DCFE_CLOCK_CONTROL, DCFE, 2), \
	SRII(CRTC_H_BLANK_START_END, CRTC, 2), \
	SRII(BLND_V_UPDATE_LOCK, BLND, 2), \
	SRII(BLND_CONTROL, BLND, 2), \
	.DCFE_CLOCK_CONTROL[3] = mmDCFEV_CLOCK_CONTROL, \
	.CRTC_H_BLANK_START_END[3] = mmCRTCV_H_BLANK_START_END, \
	.BLND_V_UPDATE_LOCK[3] = mmBLNDV_V_UPDATE_LOCK, \
	.BLND_CONTROL[3] = mmBLNDV_CONTROL

#define HWSEQ_DCE112_REG_LIST() \
	HWSEQ_DCE10_REG_LIST(), \
	HWSEQ_PIXEL_RATE_REG_LIST(CRTC), \
	HWSEQ_PHYPLL_REG_LIST(CRTC)

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#define HWSEQ_DCN1_REG_LIST()\
	HWSEQ_PIXEL_RATE_REG_LIST(OTG), \
	HWSEQ_PHYPLL_REG_LIST(OTG)
#endif

struct dce_hwseq_registers {
	uint32_t DCFE_CLOCK_CONTROL[6];
	uint32_t DCFEV_CLOCK_CONTROL;
	uint32_t DC_MEM_GLOBAL_PWR_REQ_CNTL;
	uint32_t BLND_V_UPDATE_LOCK[6];
	uint32_t BLND_CONTROL[6];
	uint32_t BLNDV_CONTROL;

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	/* DCE + DCN */
#endif
	uint32_t CRTC_H_BLANK_START_END[6];
	uint32_t PIXEL_RATE_CNTL[6];
	uint32_t PHYPLL_PIXEL_RATE_CNTL[6];
};
 /* set field name */
#define HWS_SF(blk_name, reg_name, field_name, post_fix)\
	.field_name = blk_name ## reg_name ## __ ## field_name ## post_fix

#define HWS_SF1(blk_name, reg_name, field_name, post_fix)\
	.field_name = blk_name ## reg_name ## __ ## blk_name ## field_name ## post_fix


#define HWSEQ_DCEF_MASK_SH_LIST(mask_sh, blk)\
	HWS_SF(blk, CLOCK_CONTROL, DCFE_CLOCK_ENABLE, mask_sh),\
	SF(DC_MEM_GLOBAL_PWR_REQ_CNTL, DC_MEM_GLOBAL_PWR_REQ_DIS, mask_sh)

#define HWSEQ_BLND_MASK_SH_LIST(mask_sh, blk)\
	HWS_SF(blk, V_UPDATE_LOCK, BLND_DCP_GRPH_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(blk, V_UPDATE_LOCK, BLND_SCL_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(blk, V_UPDATE_LOCK, BLND_DCP_GRPH_SURF_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(blk, V_UPDATE_LOCK, BLND_BLND_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(blk, V_UPDATE_LOCK, BLND_V_UPDATE_LOCK_MODE, mask_sh),\
	HWS_SF(blk, CONTROL, BLND_FEEDTHROUGH_EN, mask_sh),\
	HWS_SF(blk, CONTROL, BLND_ALPHA_MODE, mask_sh),\
	HWS_SF(blk, CONTROL, BLND_MODE, mask_sh),\
	HWS_SF(blk, CONTROL, BLND_MULTIPLIED_MODE, mask_sh)

#define HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, blk)\
	HWS_SF1(blk, PIXEL_RATE_CNTL, PIXEL_RATE_SOURCE, mask_sh),\
	HWS_SF(blk, PIXEL_RATE_CNTL, DP_DTO0_ENABLE, mask_sh)

#define HWSEQ_PHYPLL_MASK_SH_LIST(mask_sh, blk)\
	HWS_SF1(blk, PHYPLL_PIXEL_RATE_CNTL, PHYPLL_PIXEL_RATE_SOURCE, mask_sh),\
	HWS_SF1(blk, PHYPLL_PIXEL_RATE_CNTL, PIXEL_RATE_PLL_SOURCE, mask_sh)

#define HWSEQ_DCE8_MASK_SH_LIST(mask_sh)\
	.DCFE_CLOCK_ENABLE = CRTC_DCFE_CLOCK_CONTROL__CRTC_DCFE_CLOCK_ENABLE ## mask_sh, \
	HWS_SF(BLND_, V_UPDATE_LOCK, BLND_DCP_GRPH_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(BLND_, V_UPDATE_LOCK, BLND_SCL_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(BLND_, V_UPDATE_LOCK, BLND_DCP_GRPH_SURF_V_UPDATE_LOCK, mask_sh),\
	HWS_SF(BLND_, CONTROL, BLND_MODE, mask_sh),\
	HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, CRTC0_)

#define HWSEQ_DCE10_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCEF_MASK_SH_LIST(mask_sh, DCFE_),\
	HWSEQ_BLND_MASK_SH_LIST(mask_sh, BLND_),\
	HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, CRTC0_)

#define HWSEQ_DCE11_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCE10_MASK_SH_LIST(mask_sh),\
	SF(DCFEV_CLOCK_CONTROL, DCFEV_CLOCK_ENABLE, mask_sh),\
	HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, CRTC0_)

#define HWSEQ_DCE112_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCE10_MASK_SH_LIST(mask_sh),\
	HWSEQ_PHYPLL_MASK_SH_LIST(mask_sh, CRTC0_)

#define HWSEQ_DCE12_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCEF_MASK_SH_LIST(mask_sh, DCFE0_DCFE_),\
	HWSEQ_BLND_MASK_SH_LIST(mask_sh, BLND0_BLND_),\
	HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, CRTC0_),\
	HWSEQ_PHYPLL_MASK_SH_LIST(mask_sh, CRTC0_)

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#define HWSEQ_DCN1_MASK_SH_LIST(mask_sh)\
	HWSEQ_PIXEL_RATE_MASK_SH_LIST(mask_sh, OTG0_),\
	HWSEQ_PHYPLL_MASK_SH_LIST(mask_sh, OTG0_)
#endif

#define HWSEQ_REG_FIED_LIST(type) \
	type DCFE_CLOCK_ENABLE; \
	type DCFEV_CLOCK_ENABLE; \
	type DC_MEM_GLOBAL_PWR_REQ_DIS; \
	type BLND_DCP_GRPH_V_UPDATE_LOCK; \
	type BLND_SCL_V_UPDATE_LOCK; \
	type BLND_DCP_GRPH_SURF_V_UPDATE_LOCK; \
	type BLND_BLND_V_UPDATE_LOCK; \
	type BLND_V_UPDATE_LOCK_MODE; \
	type BLND_FEEDTHROUGH_EN; \
	type BLND_ALPHA_MODE; \
	type BLND_MODE; \
	type BLND_MULTIPLIED_MODE; \
	type DP_DTO0_ENABLE; \
	type PIXEL_RATE_SOURCE; \
	type PHYPLL_PIXEL_RATE_SOURCE; \
	type PIXEL_RATE_PLL_SOURCE; \

struct dce_hwseq_shift {
	HWSEQ_REG_FIED_LIST(uint8_t)
};

struct dce_hwseq_mask {
	HWSEQ_REG_FIED_LIST(uint32_t)
};


enum blnd_mode {
	BLND_MODE_CURRENT_PIPE = 0,/* Data from current pipe only */
	BLND_MODE_OTHER_PIPE, /* Data from other pipe only */
	BLND_MODE_BLENDING,/* Alpha blending - blend 'current' and 'other' */
};

void dce_enable_fe_clock(struct dce_hwseq *hwss,
		unsigned int inst, bool enable);

void dce_pipe_control_lock(struct core_dc *dc,
		struct pipe_ctx *pipe,
		bool lock);

void dce_set_blender_mode(struct dce_hwseq *hws,
	unsigned int blnd_inst, enum blnd_mode mode);

void dce_clock_gating_power_up(struct dce_hwseq *hws,
		bool enable);

void dce_crtc_switch_to_clk_src(struct dce_hwseq *hws,
		struct clock_source *clk_src,
		unsigned int tg_inst);

bool dce_use_lut(const struct core_surface *surface);
#endif   /*__DCE_HWSEQ_H__*/
