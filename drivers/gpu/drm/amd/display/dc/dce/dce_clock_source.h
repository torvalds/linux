/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_CLOCK_SOURCE_DCE_H__
#define __DC_CLOCK_SOURCE_DCE_H__

#include "../inc/clock_source.h"

#define TO_DCE110_CLK_SRC(clk_src)\
	container_of(clk_src, struct dce110_clk_src, base)

#define CS_COMMON_REG_LIST_DCE_100_110(id) \
		SRI(RESYNC_CNTL, PIXCLK, id), \
		SRI(PLL_CNTL, BPHYC_PLL, id)

#define CS_COMMON_REG_LIST_DCE_80(id) \
		SRI(RESYNC_CNTL, PIXCLK, id), \
		SRI(PLL_CNTL, DCCG_PLL, id)

#define CS_COMMON_REG_LIST_DCE_112(id) \
		SRI(PIXCLK_RESYNC_CNTL, PHYPLL, id)


#define CS_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define CS_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh)\
	CS_SF(PLL_CNTL, PLL_REF_DIV_SRC, mask_sh),\
	CS_SF(PIXCLK1_RESYNC_CNTL, DCCG_DEEP_COLOR_CNTL1, mask_sh),\
	CS_SF(PLL_POST_DIV, PLL_POST_DIV_PIXCLK, mask_sh),\
	CS_SF(PLL_REF_DIV, PLL_REF_DIV, mask_sh)

#define CS_COMMON_MASK_SH_LIST_DCE_112(mask_sh)\
	CS_SF(PHYPLLA_PIXCLK_RESYNC_CNTL, PHYPLLA_DCCG_DEEP_COLOR_CNTL, mask_sh),\
	CS_SF(PHYPLLA_PIXCLK_RESYNC_CNTL, PHYPLLA_PIXCLK_DOUBLE_RATE_ENABLE, mask_sh)

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
#define CS_COMMON_REG_LIST_DCN2_0(index, pllid) \
		SRI(PIXCLK_RESYNC_CNTL, PHYPLL, pllid),\
		SRII(PHASE, DP_DTO, 0),\
		SRII(PHASE, DP_DTO, 1),\
		SRII(PHASE, DP_DTO, 2),\
		SRII(PHASE, DP_DTO, 3),\
		SRII(PHASE, DP_DTO, 4),\
		SRII(PHASE, DP_DTO, 5),\
		SRII(MODULO, DP_DTO, 0),\
		SRII(MODULO, DP_DTO, 1),\
		SRII(MODULO, DP_DTO, 2),\
		SRII(MODULO, DP_DTO, 3),\
		SRII(MODULO, DP_DTO, 4),\
		SRII(MODULO, DP_DTO, 5),\
		SRII(PIXEL_RATE_CNTL, OTG, 0),\
		SRII(PIXEL_RATE_CNTL, OTG, 1),\
		SRII(PIXEL_RATE_CNTL, OTG, 2),\
		SRII(PIXEL_RATE_CNTL, OTG, 3),\
		SRII(PIXEL_RATE_CNTL, OTG, 4),\
		SRII(PIXEL_RATE_CNTL, OTG, 5)
#endif

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
#define CS_COMMON_MASK_SH_LIST_DCN2_0(mask_sh)\
	CS_SF(DP_DTO0_PHASE, DP_DTO0_PHASE, mask_sh),\
	CS_SF(DP_DTO0_MODULO, DP_DTO0_MODULO, mask_sh),\
	CS_SF(PHYPLLA_PIXCLK_RESYNC_CNTL, PHYPLLA_DCCG_DEEP_COLOR_CNTL, mask_sh),\
	CS_SF(OTG0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE, mask_sh)
#endif

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)

#define CS_COMMON_REG_LIST_DCN1_0(index, pllid) \
		SRI(PIXCLK_RESYNC_CNTL, PHYPLL, pllid),\
		SRII(PHASE, DP_DTO, 0),\
		SRII(PHASE, DP_DTO, 1),\
		SRII(PHASE, DP_DTO, 2),\
		SRII(PHASE, DP_DTO, 3),\
		SRII(MODULO, DP_DTO, 0),\
		SRII(MODULO, DP_DTO, 1),\
		SRII(MODULO, DP_DTO, 2),\
		SRII(MODULO, DP_DTO, 3),\
		SRII(PIXEL_RATE_CNTL, OTG, 0), \
		SRII(PIXEL_RATE_CNTL, OTG, 1), \
		SRII(PIXEL_RATE_CNTL, OTG, 2), \
		SRII(PIXEL_RATE_CNTL, OTG, 3)

#define CS_COMMON_MASK_SH_LIST_DCN1_0(mask_sh)\
	CS_SF(DP_DTO0_PHASE, DP_DTO0_PHASE, mask_sh),\
	CS_SF(DP_DTO0_MODULO, DP_DTO0_MODULO, mask_sh),\
	CS_SF(PHYPLLA_PIXCLK_RESYNC_CNTL, PHYPLLA_DCCG_DEEP_COLOR_CNTL, mask_sh),\
	CS_SF(OTG0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE, mask_sh)

#endif

#define CS_REG_FIELD_LIST(type) \
	type PLL_REF_DIV_SRC; \
	type DCCG_DEEP_COLOR_CNTL1; \
	type PHYPLLA_DCCG_DEEP_COLOR_CNTL; \
	type PHYPLLA_PIXCLK_DOUBLE_RATE_ENABLE; \
	type PLL_POST_DIV_PIXCLK; \
	type PLL_REF_DIV; \
	type DP_DTO0_PHASE; \
	type DP_DTO0_MODULO; \
	type DP_DTO0_ENABLE;

struct dce110_clk_src_shift {
	CS_REG_FIELD_LIST(uint8_t)
};

struct dce110_clk_src_mask{
	CS_REG_FIELD_LIST(uint32_t)
};

struct dce110_clk_src_regs {
	uint32_t RESYNC_CNTL;
	uint32_t PIXCLK_RESYNC_CNTL;
	uint32_t PLL_CNTL;

	/* below are for DTO.
	 * todo: should probably use different struct to not waste space
	 */
	uint32_t PHASE[MAX_PIPES];
	uint32_t MODULO[MAX_PIPES];
	uint32_t PIXEL_RATE_CNTL[MAX_PIPES];
};

struct dce110_clk_src {
	struct clock_source base;
	const struct dce110_clk_src_regs *regs;
	const struct dce110_clk_src_mask *cs_mask;
	const struct dce110_clk_src_shift *cs_shift;
	struct dc_bios *bios;

	struct spread_spectrum_data *dp_ss_params;
	uint32_t dp_ss_params_cnt;
	struct spread_spectrum_data *hdmi_ss_params;
	uint32_t hdmi_ss_params_cnt;
	struct spread_spectrum_data *dvi_ss_params;
	uint32_t dvi_ss_params_cnt;
	struct spread_spectrum_data *lvds_ss_params;
	uint32_t lvds_ss_params_cnt;

	uint32_t ext_clk_khz;
	uint32_t ref_freq_khz;

	struct calc_pll_clock_source calc_pll;
	struct calc_pll_clock_source calc_pll_hdmi;
};

bool dce110_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask);

bool dce112_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask);

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
bool dcn20_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask);
#endif

#endif
