/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DCN20_DCCG_H__
#define __DCN20_DCCG_H__

#include "dccg.h"

#define DCCG_COMMON_REG_LIST_DCN_BASE() \
	SR(DPPCLK_DTO_CTRL),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 0),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 1),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 2),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 3),\
	SR(REFCLK_CNTL)

#define DCCG_REG_LIST_DCN2() \
	DCCG_COMMON_REG_LIST_DCN_BASE(),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 4),\
	DCCG_SRII(DTO_PARAM, DPPCLK, 5)

#define DCCG_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DCCG_SFI(reg_name, field_name, field_prefix, inst, post_fix)\
	.field_prefix ## _ ## field_name[inst] = reg_name ## __ ## field_prefix ## inst ## _ ## field_name ## post_fix

#define DCCG_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh) \
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 0, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 0, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 1, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 1, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 2, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 2, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 3, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 3, mask_sh),\
	DCCG_SF(DPPCLK0_DTO_PARAM, DPPCLK0_DTO_PHASE, mask_sh),\
	DCCG_SF(DPPCLK0_DTO_PARAM, DPPCLK0_DTO_MODULO, mask_sh),\
	DCCG_SF(REFCLK_CNTL, REFCLK_CLOCK_EN, mask_sh),\
	DCCG_SF(REFCLK_CNTL, REFCLK_SRC_SEL, mask_sh)

#define DCCG_MASK_SH_LIST_DCN2(mask_sh) \
	DCCG_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 4, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 4, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_ENABLE, DPPCLK, 5, mask_sh),\
	DCCG_SFI(DPPCLK_DTO_CTRL, DTO_DB_EN, DPPCLK, 5, mask_sh)

#define DCCG_REG_FIELD_LIST(type) \
	type DPPCLK0_DTO_PHASE;\
	type DPPCLK0_DTO_MODULO;\
	type DPPCLK_DTO_ENABLE[6];\
	type DPPCLK_DTO_DB_EN[6];\
	type REFCLK_CLOCK_EN;\
	type REFCLK_SRC_SEL;

#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
#define DCCG3_REG_FIELD_LIST(type) \
	type PHYASYMCLK_FORCE_EN;\
	type PHYASYMCLK_FORCE_SRC_SEL;\
	type PHYBSYMCLK_FORCE_EN;\
	type PHYBSYMCLK_FORCE_SRC_SEL;\
	type PHYCSYMCLK_FORCE_EN;\
	type PHYCSYMCLK_FORCE_SRC_SEL;
#endif

struct dccg_shift {
	DCCG_REG_FIELD_LIST(uint8_t)
#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
	DCCG3_REG_FIELD_LIST(uint8_t)
#endif
};

struct dccg_mask {
	DCCG_REG_FIELD_LIST(uint32_t)
#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
	DCCG3_REG_FIELD_LIST(uint32_t)
#endif
};

struct dccg_registers {
	uint32_t DPPCLK_DTO_CTRL;
	uint32_t DPPCLK_DTO_PARAM[6];
	uint32_t REFCLK_CNTL;
#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
	uint32_t HDMICHARCLK_CLOCK_CNTL[6];
	uint32_t PHYASYMCLK_CLOCK_CNTL;
	uint32_t PHYBSYMCLK_CLOCK_CNTL;
	uint32_t PHYCSYMCLK_CLOCK_CNTL;
#endif
};

struct dcn_dccg {
	struct dccg base;
	const struct dccg_registers *regs;
	const struct dccg_shift *dccg_shift;
	const struct dccg_mask *dccg_mask;
};

void dccg2_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk);

void dccg2_get_dccg_ref_freq(struct dccg *dccg,
		unsigned int xtalin_freq_inKhz,
		unsigned int *dccg_ref_freq_inKhz);

void dccg2_init(struct dccg *dccg);

struct dccg *dccg2_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *dccg_shift,
	const struct dccg_mask *dccg_mask);

void dcn_dccg_destroy(struct dccg **dccg);

#endif //__DCN20_DCCG_H__
