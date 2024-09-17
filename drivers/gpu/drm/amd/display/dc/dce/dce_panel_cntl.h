/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_PANEL_CNTL__DCE_H__
#define __DC_PANEL_CNTL__DCE_H__

#include "panel_cntl.h"

/* set register offset with instance */
#define DCE_PANEL_CNTL_SR(reg_name, block)\
	.reg_name = mm ## block ## _ ## reg_name

#define DCE_PANEL_CNTL_REG_LIST()\
	DCE_PANEL_CNTL_SR(PWRSEQ_CNTL, LVTMA), \
	DCE_PANEL_CNTL_SR(PWRSEQ_STATE, LVTMA), \
	DCE_PANEL_CNTL_SR(PWRSEQ_REF_DIV, LVTMA), \
	SR(BL_PWM_CNTL), \
	SR(BL_PWM_CNTL2), \
	SR(BL_PWM_PERIOD_CNTL), \
	SR(BL_PWM_GRP1_REG_LOCK), \
	SR(BIOS_SCRATCH_2)

#define DCN_PANEL_CNTL_SR(reg_name, block)\
	.reg_name = BASE(mm ## block ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## _ ## reg_name

#define DCN_PANEL_CNTL_REG_LIST()\
	DCN_PANEL_CNTL_SR(PWRSEQ_CNTL, LVTMA), \
	DCN_PANEL_CNTL_SR(PWRSEQ_STATE, LVTMA), \
	DCN_PANEL_CNTL_SR(PWRSEQ_REF_DIV, LVTMA), \
	SR(BL_PWM_CNTL), \
	SR(BL_PWM_CNTL2), \
	SR(BL_PWM_PERIOD_CNTL), \
	SR(BL_PWM_GRP1_REG_LOCK), \
	NBIO_SR(BIOS_SCRATCH_2)

#define DCE_PANEL_CNTL_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DCE_PANEL_CNTL_MASK_SH_LIST(mask_sh) \
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_CNTL, LVTMA_BLON, mask_sh),\
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_CNTL, LVTMA_BLON_OVRD, mask_sh),\
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_CNTL, LVTMA_DIGON, mask_sh),\
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_CNTL, LVTMA_DIGON_OVRD, mask_sh),\
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_CNTL, LVTMA_PWRSEQ_TARGET_STATE, mask_sh), \
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_STATE, LVTMA_PWRSEQ_TARGET_STATE_R, mask_sh), \
	DCE_PANEL_CNTL_SF(LVTMA_PWRSEQ_REF_DIV, BL_PWM_REF_DIV, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD_BITCNT, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_CNTL, BL_PWM_FRACTIONAL_EN, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_CNTL, BL_PWM_EN, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_GRP1_REG_LOCK, BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_GRP1_REG_LOCK, BL_PWM_GRP1_REG_LOCK, mask_sh), \
	DCE_PANEL_CNTL_SF(BL_PWM_GRP1_REG_LOCK, BL_PWM_GRP1_REG_UPDATE_PENDING, mask_sh)

#define DCE_PANEL_CNTL_REG_FIELD_LIST(type) \
	type LVTMA_BLON;\
	type LVTMA_BLON_OVRD;\
	type LVTMA_DIGON;\
	type LVTMA_DIGON_OVRD;\
	type LVTMA_PWRSEQ_TARGET_STATE; \
	type LVTMA_PWRSEQ_TARGET_STATE_R; \
	type BL_PWM_REF_DIV; \
	type BL_PWM_EN; \
	type BL_ACTIVE_INT_FRAC_CNT; \
	type BL_PWM_FRACTIONAL_EN; \
	type BL_PWM_PERIOD; \
	type BL_PWM_PERIOD_BITCNT; \
	type BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN; \
	type BL_PWM_GRP1_REG_LOCK; \
	type BL_PWM_GRP1_REG_UPDATE_PENDING

struct dce_panel_cntl_shift {
	DCE_PANEL_CNTL_REG_FIELD_LIST(uint8_t);
};

struct dce_panel_cntl_mask {
	DCE_PANEL_CNTL_REG_FIELD_LIST(uint32_t);
};

struct dce_panel_cntl_registers {
	uint32_t PWRSEQ_CNTL;
	uint32_t PWRSEQ_STATE;
	uint32_t BL_PWM_CNTL;
	uint32_t BL_PWM_CNTL2;
	uint32_t BL_PWM_PERIOD_CNTL;
	uint32_t BL_PWM_GRP1_REG_LOCK;
	uint32_t PWRSEQ_REF_DIV;
	uint32_t BIOS_SCRATCH_2;
};

struct dce_panel_cntl {
	struct panel_cntl base;
	const struct dce_panel_cntl_registers *regs;
	const struct dce_panel_cntl_shift *shift;
	const struct dce_panel_cntl_mask *mask;
};

void dce_panel_cntl_construct(
	struct dce_panel_cntl *panel_cntl,
	const struct panel_cntl_init_data *init_data,
	const struct dce_panel_cntl_registers *regs,
	const struct dce_panel_cntl_shift *shift,
	const struct dce_panel_cntl_mask *mask);

#endif /* __DC_PANEL_CNTL__DCE_H__ */
