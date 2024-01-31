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

#include "reg_helper.h"
#include "core_types.h"
#include "dc_dmub_srv.h"
#include "dcn301_panel_cntl.h"
#include "atom.h"

#define TO_DCN301_PANEL_CNTL(panel_cntl)\
	container_of(panel_cntl, struct dcn301_panel_cntl, base)

#define CTX \
	dcn301_panel_cntl->base.ctx

#define DC_LOGGER \
	dcn301_panel_cntl->base.ctx->logger

#define REG(reg)\
	dcn301_panel_cntl->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dcn301_panel_cntl->shift->field_name, dcn301_panel_cntl->mask->field_name

static unsigned int dcn301_get_16_bit_backlight_from_pwm(struct panel_cntl *panel_cntl)
{
	uint64_t current_backlight;
	uint32_t round_result;
	uint32_t bl_period, bl_int_count;
	uint32_t bl_pwm, fractional_duty_cycle_en;
	uint32_t bl_period_mask, bl_pwm_mask;
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(panel_cntl);

	REG_GET(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD, &bl_period);
	REG_GET(BL_PWM_PERIOD_CNTL, BL_PWM_PERIOD_BITCNT, &bl_int_count);

	REG_GET(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, &bl_pwm);
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

	current_backlight = (uint64_t)bl_pwm << (1 + bl_int_count);

	if (bl_period == 0)
		bl_period = 0xFFFF;

	current_backlight = div_u64(current_backlight, bl_period);
	current_backlight = (current_backlight + 1) >> 1;

	current_backlight = (uint64_t)(current_backlight) * bl_period;

	round_result = (uint32_t)(current_backlight & 0xFFFFFFFF);

	round_result = (round_result >> (bl_int_count-1)) & 1;

	current_backlight >>= bl_int_count;
	current_backlight += round_result;

	return (uint32_t)(current_backlight);
}

static uint32_t dcn301_panel_cntl_hw_init(struct panel_cntl *panel_cntl)
{
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(panel_cntl);
	uint32_t value;
	uint32_t current_backlight;

	/* It must not be 0, so we have to restore them
	 * Bios bug w/a - period resets to zero,
	 * restoring to cache values which is always correct
	 */
	REG_GET(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, &value);

	if (value == 0 || value == 1) {
		if (panel_cntl->stored_backlight_registers.BL_PWM_CNTL != 0) {
			REG_WRITE(BL_PWM_CNTL,
					panel_cntl->stored_backlight_registers.BL_PWM_CNTL);
			REG_WRITE(BL_PWM_CNTL2,
					panel_cntl->stored_backlight_registers.BL_PWM_CNTL2);
			REG_WRITE(BL_PWM_PERIOD_CNTL,
					panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL);
			REG_UPDATE(PWRSEQ_REF_DIV,
				BL_PWM_REF_DIV,
				panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV);
		} else {
			/* TODO: Note: This should not really happen since VBIOS
			 * should have initialized PWM registers on boot.
			 */
			REG_WRITE(BL_PWM_CNTL, 0xC000FA00);
			REG_WRITE(BL_PWM_PERIOD_CNTL, 0x000C0FA0);
		}
	} else {
		panel_cntl->stored_backlight_registers.BL_PWM_CNTL =
				REG_READ(BL_PWM_CNTL);
		panel_cntl->stored_backlight_registers.BL_PWM_CNTL2 =
				REG_READ(BL_PWM_CNTL2);
		panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL =
				REG_READ(BL_PWM_PERIOD_CNTL);

		REG_GET(PWRSEQ_REF_DIV, BL_PWM_REF_DIV,
				&panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV);
	}

	// Enable the backlight output
	REG_UPDATE(BL_PWM_CNTL, BL_PWM_EN, 1);

	// Unlock group 2 backlight registers
	REG_UPDATE(BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_REG_LOCK, 0);

	current_backlight = dcn301_get_16_bit_backlight_from_pwm(panel_cntl);

	return current_backlight;
}

static void dcn301_panel_cntl_destroy(struct panel_cntl **panel_cntl)
{
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(*panel_cntl);

	kfree(dcn301_panel_cntl);
	*panel_cntl = NULL;
}

static bool dcn301_is_panel_backlight_on(struct panel_cntl *panel_cntl)
{
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(panel_cntl);
	uint32_t value;

	REG_GET(PWRSEQ_CNTL, PANEL_BLON, &value);

	return value;
}

static bool dcn301_is_panel_powered_on(struct panel_cntl *panel_cntl)
{
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(panel_cntl);
	uint32_t pwr_seq_state, dig_on, dig_on_ovrd;

	REG_GET(PWRSEQ_STATE, PANEL_PWRSEQ_TARGET_STATE_R, &pwr_seq_state);

	REG_GET_2(PWRSEQ_CNTL, PANEL_DIGON, &dig_on, PANEL_DIGON_OVRD, &dig_on_ovrd);

	return (pwr_seq_state == 1) || (dig_on == 1 && dig_on_ovrd == 1);
}

static void dcn301_store_backlight_level(struct panel_cntl *panel_cntl)
{
	struct dcn301_panel_cntl *dcn301_panel_cntl = TO_DCN301_PANEL_CNTL(panel_cntl);

	panel_cntl->stored_backlight_registers.BL_PWM_CNTL =
		REG_READ(BL_PWM_CNTL);
	panel_cntl->stored_backlight_registers.BL_PWM_CNTL2 =
		REG_READ(BL_PWM_CNTL2);
	panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL =
		REG_READ(BL_PWM_PERIOD_CNTL);

	REG_GET(PWRSEQ_REF_DIV, BL_PWM_REF_DIV,
		&panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV);
}

static const struct panel_cntl_funcs dcn301_link_panel_cntl_funcs = {
	.destroy = dcn301_panel_cntl_destroy,
	.hw_init = dcn301_panel_cntl_hw_init,
	.is_panel_backlight_on = dcn301_is_panel_backlight_on,
	.is_panel_powered_on = dcn301_is_panel_powered_on,
	.store_backlight_level = dcn301_store_backlight_level,
	.get_current_backlight = dcn301_get_16_bit_backlight_from_pwm,
};

void dcn301_panel_cntl_construct(
	struct dcn301_panel_cntl *dcn301_panel_cntl,
	const struct panel_cntl_init_data *init_data,
	const struct dce_panel_cntl_registers *regs,
	const struct dcn301_panel_cntl_shift *shift,
	const struct dcn301_panel_cntl_mask *mask)
{
	dcn301_panel_cntl->regs = regs;
	dcn301_panel_cntl->shift = shift;
	dcn301_panel_cntl->mask = mask;

	dcn301_panel_cntl->base.funcs = &dcn301_link_panel_cntl_funcs;
	dcn301_panel_cntl->base.ctx = init_data->ctx;
	dcn301_panel_cntl->base.inst = init_data->inst;
	dcn301_panel_cntl->base.pwrseq_inst = 0;
}
