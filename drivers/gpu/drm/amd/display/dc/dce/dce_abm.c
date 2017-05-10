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

#include "atom.h"


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
#define MCP_ABM_PIPE_SET 0x66
#define MCP_BL_SET 0x67

struct abm_backlight_registers {
	unsigned int BL_PWM_CNTL;
	unsigned int BL_PWM_CNTL2;
	unsigned int BL_PWM_PERIOD_CNTL;
	unsigned int LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV;
};

/* registers setting needs to be save and restored used at InitBacklight */
static struct abm_backlight_registers stored_backlight_registers = {0};


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

	current_backlight = div_u64(current_backlight, bl_period);
	current_backlight = (current_backlight + 1) >> 1;

	current_backlight = (uint64_t)(current_backlight) * bl_period;

	round_result = (uint32_t)(current_backlight & 0xFFFFFFFF);

	round_result = (round_result >> (bl_int_count-1)) & 1;

	current_backlight >>= bl_int_count;
	current_backlight += round_result;

	return (uint32_t)(current_backlight);
}

static void driver_set_backlight_level(struct dce_abm *abm_dce, uint32_t level)
{
	uint32_t backlight_24bit;
	uint32_t backlight_17bit;
	uint32_t backlight_16bit;
	uint32_t masked_pwm_period;
	uint8_t rounding_bit;
	uint8_t bit_count;
	uint64_t active_duty_cycle;
	uint32_t pwm_period_bitcnt;

	/*
	 * 1. Convert 8-bit value to 17 bit U1.16 format
	 * (1 integer, 16 fractional bits)
	 */

	/* 1.1 multiply 8 bit value by 0x10101 to get a 24 bit value,
	 * effectively multiplying value by 256/255
	 * eg. for a level of 0xEF, backlight_24bit = 0xEF * 0x10101 = 0xEFEFEF
	 */
	backlight_24bit = level * 0x10101;

	/* 1.2 The upper 16 bits of the 24 bit value is the fraction, lower 8
	 * used for rounding, take most significant bit of fraction for
	 * rounding, e.g. for 0xEFEFEF, rounding bit is 1
	 */
	rounding_bit = (backlight_24bit >> 7) & 1;

	/* 1.3 Add the upper 16 bits of the 24 bit value with the rounding bit
	 * resulting in a 17 bit value e.g. 0xEFF0 = (0xEFEFEF >> 8) + 1
	 */
	backlight_17bit = (backlight_24bit >> 8) + rounding_bit;

	/*
	 * 2. Find  16 bit backlight active duty cycle, where 0 <= backlight
	 * active duty cycle <= backlight period
	 */

	/* 2.1 Apply bitmask for backlight period value based on value of BITCNT
	 */
	REG_GET_2(BL_PWM_PERIOD_CNTL,
			BL_PWM_PERIOD_BITCNT, &pwm_period_bitcnt,
			BL_PWM_PERIOD, &masked_pwm_period);

	if (pwm_period_bitcnt == 0)
		bit_count = 16;
	else
		bit_count = pwm_period_bitcnt;

	/* e.g. maskedPwmPeriod = 0x24 when bitCount is 6 */
	masked_pwm_period = masked_pwm_period & ((1 << bit_count) - 1);

	/* 2.2 Calculate integer active duty cycle required upper 16 bits
	 * contain integer component, lower 16 bits contain fractional component
	 * of active duty cycle e.g. 0x21BDC0 = 0xEFF0 * 0x24
	 */
	active_duty_cycle = backlight_17bit * masked_pwm_period;

	/* 2.3 Calculate 16 bit active duty cycle from integer and fractional
	 * components shift by bitCount then mask 16 bits and add rounding bit
	 * from MSB of fraction e.g. 0x86F7 = ((0x21BDC0 >> 6) & 0xFFF) + 0
	 */
	backlight_16bit = active_duty_cycle >> bit_count;
	backlight_16bit &= 0xFFFF;
	backlight_16bit += (active_duty_cycle >> (bit_count - 1)) & 0x1;

	/*
	 * 3. Program register with updated value
	 */

	/* 3.1 Lock group 2 backlight registers */

	REG_UPDATE_2(BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN, 1,
			BL_PWM_GRP1_REG_LOCK, 1);

	// 3.2 Write new active duty cycle
	REG_UPDATE(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, backlight_16bit);

	/* 3.3 Unlock group 2 backlight registers */
	REG_UPDATE(BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_REG_LOCK, 0);

	/* 5.4.4 Wait for pending bit to be cleared */
	REG_WAIT(BL_PWM_GRP1_REG_LOCK, BL_PWM_GRP1_REG_UPDATE_PENDING,
			0, 10, 1000);
}

static void dmcu_set_backlight_level(
	struct dce_abm *abm_dce,
	uint32_t level,
	uint32_t frame_ramp,
	uint32_t controller_id)
{
	unsigned int backlight_16_bit = (level * 0x10101) >> 8;
	unsigned int backlight_17_bit = backlight_16_bit +
				(((backlight_16_bit & 0x80) >> 7) & 1);
	uint32_t rampingBoundary = 0xFFFF;
	uint32_t s2;

	/* set ramping boundary */
	REG_WRITE(MASTER_COMM_DATA_REG1, rampingBoundary);

	/* setDMCUParam_Pipe */
	REG_UPDATE_2(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, MCP_ABM_PIPE_SET,
			MASTER_COMM_CMD_REG_BYTE1, controller_id);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT,
			0, 100, 800);

	/* setDMCUParam_BL */
	REG_UPDATE(BL1_PWM_USER_LEVEL, BL1_PWM_USER_LEVEL, backlight_17_bit);

	/* write ramp */
	if (controller_id == 0)
		frame_ramp = 0;
	REG_WRITE(MASTER_COMM_DATA_REG1, frame_ramp);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, MCP_BL_SET);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* UpdateRequestedBacklightLevel */
	s2 = REG_READ(BIOS_SCRATCH_2);

	s2 &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	level &= (ATOM_S2_CURRENT_BL_LEVEL_MASK >>
				ATOM_S2_CURRENT_BL_LEVEL_SHIFT);
	s2 |= (level << ATOM_S2_CURRENT_BL_LEVEL_SHIFT);

	REG_WRITE(BIOS_SCRATCH_2, s2);
}

static void dce_abm_init(struct abm *abm)
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

static bool dce_abm_set_level(struct abm *abm, uint32_t level)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);

	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
			100, 800);

	/* setDMCUParam_ABMLevel */
	REG_UPDATE_2(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, MCP_ABM_LEVEL_SET,
			MASTER_COMM_CMD_REG_BYTE2, level);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	return true;
}

static bool dce_abm_init_backlight(struct abm *abm)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);
	uint32_t value;

	/* It must not be 0, so we have to restore them
	 * Bios bug w/a - period resets to zero,
	 * restoring to cache values which is always correct
	 */
	REG_GET(BL_PWM_CNTL, BL_ACTIVE_INT_FRAC_CNT, &value);
	if (value == 0 || value == 1) {
		if (stored_backlight_registers.BL_PWM_CNTL != 0) {
			REG_WRITE(BL_PWM_CNTL,
				stored_backlight_registers.BL_PWM_CNTL);
			REG_WRITE(BL_PWM_CNTL2,
				stored_backlight_registers.BL_PWM_CNTL2);
			REG_WRITE(BL_PWM_PERIOD_CNTL,
				stored_backlight_registers.BL_PWM_PERIOD_CNTL);
			REG_UPDATE(LVTMA_PWRSEQ_REF_DIV,
				BL_PWM_REF_DIV,
				stored_backlight_registers.
				LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV);
		} else {
			/* TODO: Note: This should not really happen since VBIOS
			 * should have initialized PWM registers on boot.
			 */
			REG_WRITE(BL_PWM_CNTL, 0xC000FA00);
			REG_WRITE(BL_PWM_PERIOD_CNTL, 0x000C0FA0);
		}
	} else {
		stored_backlight_registers.BL_PWM_CNTL =
				REG_READ(BL_PWM_CNTL);
		stored_backlight_registers.BL_PWM_CNTL2 =
				REG_READ(BL_PWM_CNTL2);
		stored_backlight_registers.BL_PWM_PERIOD_CNTL =
				REG_READ(BL_PWM_PERIOD_CNTL);

		REG_GET(LVTMA_PWRSEQ_REF_DIV, BL_PWM_REF_DIV,
				&stored_backlight_registers.
				LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV);
	}

	/* Have driver take backlight control
	 * TakeBacklightControl(true)
	 */
	value = REG_READ(BIOS_SCRATCH_2);
	value |= ATOM_S2_VRI_BRIGHT_ENABLE;
	REG_WRITE(BIOS_SCRATCH_2, value);

	/* Enable the backlight output */
	REG_UPDATE(BL_PWM_CNTL, BL_PWM_EN, 1);

	/* Unlock group 2 backlight registers */
	REG_UPDATE(BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_REG_LOCK, 0);

	return true;
}

static bool is_dmcu_initialized(struct abm *abm)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);
	unsigned int dmcu_uc_reset;

	REG_GET(DMCU_STATUS, UC_IN_RESET, &dmcu_uc_reset);

	return !dmcu_uc_reset;
}

static bool dce_abm_set_backlight_level(
		struct abm *abm,
		unsigned int backlight_level,
		unsigned int frame_ramp,
		unsigned int controller_id)
{
	struct dce_abm *abm_dce = TO_DCE_ABM(abm);

	dm_logger_write(abm->ctx->logger, LOG_BACKLIGHT,
			"New Backlight level: %d (0x%X)\n",
			backlight_level, backlight_level);

	/* If DMCU is in reset state, DMCU is uninitialized */
	if (is_dmcu_initialized(abm))
		dmcu_set_backlight_level(abm_dce,
				backlight_level,
				frame_ramp,
				controller_id);
	else
		driver_set_backlight_level(abm_dce, backlight_level);

	return true;
}

static const struct abm_funcs dce_funcs = {
	.abm_init = dce_abm_init,
	.set_abm_level = dce_abm_set_level,
	.init_backlight = dce_abm_init_backlight,
	.set_backlight_level = dce_abm_set_backlight_level,
	.is_dmcu_initialized = is_dmcu_initialized
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
