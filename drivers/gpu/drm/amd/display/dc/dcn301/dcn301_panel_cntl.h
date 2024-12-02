/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __DC_PANEL_CNTL__DCN301_H__
#define __DC_PANEL_CNTL__DCN301_H__

#include "panel_cntl.h"
#include "dce/dce_panel_cntl.h"


#define DCN301_PANEL_CNTL_REG_LIST(id)\
	SRIR(PWRSEQ_CNTL, CNTL, PANEL_PWRSEQ, id), \
	SRIR(PWRSEQ_STATE, STATE, PANEL_PWRSEQ, id), \
	SRIR(PWRSEQ_REF_DIV, REF_DIV, PANEL_PWRSEQ, id), \
	SRIR(BL_PWM_CNTL, CNTL, BL_PWM, id), \
	SRIR(BL_PWM_CNTL2, CNTL2, BL_PWM, id), \
	SRIR(BL_PWM_PERIOD_CNTL, PERIOD_CNTL, BL_PWM, id), \
	SRIR(BL_PWM_GRP1_REG_LOCK, GRP1_REG_LOCK, BL_PWM, id)

#define DCN301_PANEL_CNTL_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DCN301_PANEL_CNTL_MASK_SH_LIST(mask_sh) \
	DCN301_PANEL_CNTL_SF(PANEL_PWRSEQ0_CNTL, PANEL_BLON, mask_sh),\
	DCN301_PANEL_CNTL_SF(PANEL_PWRSEQ0_CNTL, PANEL_DIGON, mask_sh),\
	DCN301_PANEL_CNTL_SF(PANEL_PWRSEQ0_CNTL, PANEL_DIGON_OVRD, mask_sh),\
	DCN301_PANEL_CNTL_SF(PANEL_PWRSEQ0_STATE, PANEL_PWRSEQ_TARGET_STATE_R, mask_sh), \
	DCN301_PANEL_CNTL_SF(PANEL_PWRSEQ0_REF_DIV, BL_PWM_REF_DIV, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_PERIOD_CNTL, BL_PWM_PERIOD, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_PERIOD_CNTL, BL_PWM_PERIOD_BITCNT, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_CNTL, BL_ACTIVE_INT_FRAC_CNT, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_CNTL, BL_PWM_FRACTIONAL_EN, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_CNTL, BL_PWM_EN, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_GRP1_REG_LOCK, BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_GRP1_REG_LOCK, BL_PWM_GRP1_REG_LOCK, mask_sh), \
	DCN301_PANEL_CNTL_SF(BL_PWM0_GRP1_REG_LOCK, BL_PWM_GRP1_REG_UPDATE_PENDING, mask_sh)

#define DCN301_PANEL_CNTL_REG_FIELD_LIST(type) \
	type PANEL_BLON;\
	type PANEL_DIGON;\
	type PANEL_DIGON_OVRD;\
	type PANEL_PWRSEQ_TARGET_STATE_R; \
	type BL_PWM_EN; \
	type BL_ACTIVE_INT_FRAC_CNT; \
	type BL_PWM_FRACTIONAL_EN; \
	type BL_PWM_PERIOD; \
	type BL_PWM_PERIOD_BITCNT; \
	type BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN; \
	type BL_PWM_GRP1_REG_LOCK; \
	type BL_PWM_GRP1_REG_UPDATE_PENDING; \
	type BL_PWM_REF_DIV

struct dcn301_panel_cntl_shift {
	DCN301_PANEL_CNTL_REG_FIELD_LIST(uint8_t);
};

struct dcn301_panel_cntl_mask {
	DCN301_PANEL_CNTL_REG_FIELD_LIST(uint32_t);
};

struct dcn301_panel_cntl {
	struct panel_cntl base;
	const struct dce_panel_cntl_registers *regs;
	const struct dcn301_panel_cntl_shift *shift;
	const struct dcn301_panel_cntl_mask *mask;
};

void dcn301_panel_cntl_construct(
	struct dcn301_panel_cntl *panel_cntl,
	const struct panel_cntl_init_data *init_data,
	const struct dce_panel_cntl_registers *regs,
	const struct dcn301_panel_cntl_shift *shift,
	const struct dcn301_panel_cntl_mask *mask);

#endif /* __DC_PANEL_CNTL__DCN301_H__ */
