/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#include "dce_abm.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed32_32.h"
#include "dc.h"

#define TO_DCE_ABM(abm)\
	container_of(abm, struct dce_abm, base)

#define REG(reg) \
	(abm_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	abm_dce->abm_shift->field_name, abm_dce->abm_mask->field_name

#define CTX \
	abm_dce->base.ctx

#define MCP_ABM_LEVEL_SET 0x65

static unsigned int get_current_backlight(struct dce_abm *abm_dce)
{
	uint64_t current_backlight;
	uint32_t round_result;
	uint32_t pwm_period_cntl, bl_period, bl_int_count;
	uint32_t bl_pwm_cntl, bl_pwm, fractional_duty_cycle_en;
	uint32_t bl_period_mask, bl_pwm_mask;

	pwm_period_cntl = REG_READ(BL_PWM_PERIOD_CNTL);
	REG_GET(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD, &bl_period);
	REG_GET(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD_BITCNT, &bl_int_count);

	bl_pwm_cntl = REG_READ(BL_PWM_CNTL);
	REG_GET(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, (uint32_t *)(&bl_pwm));
	REG_GET(BL_PWM_CNTL, BL_PWM_FRACTIONAL_EN, &fractional_duty_cycle_en);

	if (bl_int_count == 0)
		bl_int_count = 16;

	bl_period_mask = (1 << bl_int_count) - 1;
	bl_period &= bl_period_mask;

	bl_pwm_mask = bl_period_mask << (16 - bl_int_count);

	if (fractional_duty_cycle_en == 0)
		bl_pwm &= bl_pwm_mask;
	else
		bl_pwm &= 0xFFFF;

	current_backlight = bl_pwm << (1 + bl_int_count);

	if (bl_period == 0)
		bl_period = 0xFFFF;

	current_backlight /= bl_period;
	current_backlight = (current_backlight + 1) >> 1;

	current_backlight = (uint64_t)(current_backlight) * bl_period;

	round_result = (uint32_t)(current_backlight & 0xFFFFFFFF);

	round_result = (round_result >> (bl_int_count-1)) & 1;

	current_backlight >>= bl_int_count;
	current_backlight += round_result;

	return (uint32_t)(current_backlight);
}

void dce_abm_init(struct abm *abm)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);
	unsigned int backlight = get_current_backlight(abm_dce);

	REG_WRITE(DC_ABM1_HG_SAMPLE_RATE, 0x103);
	REG_WRITE(DC_ABM1_HG_SAMPLE_RATE, 0x101);
	REG_WRITE(DC_ABM1_LS_SAMPLE_RATE, 0x103);
	REG_WRITE(DC_ABM1_LS_SAMPLE_RATE, 0x101);
	REG_WRITE(BL1_PWM_BL_UPDATE_SAMPLE_RATE, 0x101);

	REG_SET_3(DC_ABM1_HG_MISC_CTRL, 0,
			ABM1_HG_NUM_OF_BINS_SEL, 0,
			ABM1_HG_VMAX_SEL, 1,
			ABM1_HG_BIN_BITWIDTH_SIZE_SEL, 0);

	REG_SET_3(DC_ABM1_IPCSC_COEFF_SEL, 0,
			ABM1_IPCSC_COEFF_SEL_R, 2,
			ABM1_IPCSC_COEFF_SEL_G, 4,
			ABM1_IPCSC_COEFF_SEL_B, 2);

	REG_UPDATE(BL1_PWM_CURRENT_ABM_LEVEL,
			BL1_PWM_CURRENT_ABM_LEVEL, backlight);

	REG_UPDATE(BL1_PWM_TARGET_ABM_LEVEL,
			BL1_PWM_TARGET_ABM_LEVEL, backlight);

	REG_UPDATE(BL1_PWM_USER_LEVEL,
			BL1_PWM_USER_LEVEL, backlight);

	REG_UPDATE_2(DC_ABM1_LS_MIN_MAX_PIXEL_VALUE_THRES,
			ABM1_LS_MIN_PIXEL_VALUE_THRES, 0,
			ABM1_LS_MAX_PIXEL_VALUE_THRES, 1000);

	REG_SET_3(DC_ABM1_HGLS_REG_READ_PROGRESS, 0,
			ABM1_HG_REG_READ_MISSED_FRAME_CLEAR, 1,
			ABM1_LS_REG_READ_MISSED_FRAME_CLEAR, 1,
			ABM1_BL_REG_READ_MISSED_FRAME_CLEAR, 1);
}

bool dce_abm_set_level(struct abm *abm, uint32_t level)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);
	struct dc_context *ctx = abm_dce->base.ctx;

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;
	unsigned int value;

	/* waitDMCUReadyForCmd */
	do {
		dm_delay_in_microseconds(ctx, dmcu_wait_reg_ready_interval);
		REG_GET(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, &value);
		dmcu_max_retry_on_wait_reg_ready--;
	} while
	/* expected value is 0, loop while not 0*/
	((value & abm_dce->abm_mask->MASTER_COMM_INTERRUPT) &&
		dmcu_max_retry_on_wait_reg_ready > 0);

	/* setDMCUParam_ABMLevel */
	REG_UPDATE_2(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, MCP_ABM_LEVEL_SET,
			MASTER_COMM_CMD_REG_BYTE2, level);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	return true;
}

static const struct abm_funcs dce_funcs = {
	.abm_init = dce_abm_init,
	.set_abm_level = dce_abm_set_level,
};

static void dce_abm_construct(
	struct dce_abm *abm_dce,
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	struct abm *base = &abm_dce->base;

	base->ctx = ctx;
	base->funcs = &dce_funcs;

	abm_dce->regs = regs;
	abm_dce->abm_shift = abm_shift;
	abm_dce->abm_mask = abm_mask;
}

struct abm *dce_abm_create(
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	struct dce_abm *abm_dce = dm_alloc(sizeof(*abm_dce));

	if (abm_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_abm_construct(abm_dce, ctx, regs, abm_shift, abm_mask);

	abm_dce->base.funcs = &dce_funcs;

	return &abm_dce->base;
}

void dce_abm_destroy(struct abm **abm)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(*abm);

	dm_free(abm_dce);
	*abm = NULL;
}
