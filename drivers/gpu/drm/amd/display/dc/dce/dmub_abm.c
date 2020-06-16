/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "dmub_abm.h"
#include "dce_abm.h"
#include "dc.h"
#include "dc_dmub_srv.h"
#include "dmub/dmub_srv.h"
#include "core_types.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed31_32.h"

#include "atom.h"

#define TO_DMUB_ABM(abm)\
	container_of(abm, struct dce_abm, base)

#define REG(reg) \
	(dce_abm->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	dce_abm->abm_shift->field_name, dce_abm->abm_mask->field_name

#define CTX \
	dce_abm->base.ctx

#define DISABLE_ABM_IMMEDIATELY 255

static bool dmub_abm_set_pipe(struct abm *abm, uint32_t otg_inst, uint32_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint32_t ramping_boundary = 0xFFFF;

	cmd.abm_set_pipe.header.type = DMUB_CMD__ABM;
	cmd.abm_set_pipe.header.sub_type = DMUB_CMD__ABM_SET_PIPE;
	cmd.abm_set_pipe.abm_set_pipe_data.otg_inst = otg_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.panel_inst = panel_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.ramping_boundary = ramping_boundary;
	cmd.abm_set_pipe.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_pipe_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

static void dmcub_set_backlight_level(
	struct dce_abm *dce_abm,
	uint32_t backlight_pwm_u16_16,
	uint32_t frame_ramp,
	uint32_t otg_inst,
	uint32_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dce_abm->base.ctx;
	unsigned int backlight_8_bit = 0;
	uint32_t s2;

	if (backlight_pwm_u16_16 & 0x10000)
		// Check for max backlight condition
		backlight_8_bit = 0xFF;
	else
		// Take MSB of fractional part since backlight is not max
		backlight_8_bit = (backlight_pwm_u16_16 >> 8) & 0xFF;

	dmub_abm_set_pipe(&dce_abm->base, otg_inst, panel_inst);

	REG_UPDATE(BL1_PWM_USER_LEVEL, BL1_PWM_USER_LEVEL, backlight_pwm_u16_16);

	if (otg_inst == 0)
		frame_ramp = 0;

	cmd.abm_set_backlight.header.type = DMUB_CMD__ABM;
	cmd.abm_set_backlight.header.sub_type = DMUB_CMD__ABM_SET_BACKLIGHT;
	cmd.abm_set_backlight.abm_set_backlight_data.frame_ramp = frame_ramp;
	cmd.abm_set_backlight.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_backlight_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	// Update requested backlight level
	s2 = REG_READ(BIOS_SCRATCH_2);

	s2 &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	backlight_8_bit &= (ATOM_S2_CURRENT_BL_LEVEL_MASK >>
				ATOM_S2_CURRENT_BL_LEVEL_SHIFT);
	s2 |= (backlight_8_bit << ATOM_S2_CURRENT_BL_LEVEL_SHIFT);

	REG_WRITE(BIOS_SCRATCH_2, s2);
}

static void dmub_abm_enable_fractional_pwm(struct dc_context *dc)
{
	union dmub_rb_cmd cmd;
	uint32_t fractional_pwm = (dc->dc->config.disable_fractional_pwm == false) ? 1 : 0;

	cmd.abm_set_pwm_frac.header.type = DMUB_CMD__ABM;
	cmd.abm_set_pwm_frac.header.sub_type = DMUB_CMD__ABM_SET_PWM_FRAC;
	cmd.abm_set_pwm_frac.abm_set_pwm_frac_data.fractional_pwm = fractional_pwm;
	cmd.abm_set_pwm_frac.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_pwm_frac_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);
}

static void dmub_abm_init(struct abm *abm, uint32_t backlight)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);

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

	dmub_abm_enable_fractional_pwm(abm->ctx);
}

static unsigned int dmub_abm_get_current_backlight(struct abm *abm)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);
	unsigned int backlight = REG_READ(BL1_PWM_CURRENT_ABM_LEVEL);

	/* return backlight in hardware format which is unsigned 17 bits, with
	 * 1 bit integer and 16 bit fractional
	 */
	return backlight;
}

static unsigned int dmub_abm_get_target_backlight(struct abm *abm)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);
	unsigned int backlight = REG_READ(BL1_PWM_TARGET_ABM_LEVEL);

	/* return backlight in hardware format which is unsigned 17 bits, with
	 * 1 bit integer and 16 bit fractional
	 */
	return backlight;
}

static bool dmub_abm_set_level(struct abm *abm, uint32_t level)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	cmd.abm_set_level.header.type = DMUB_CMD__ABM;
	cmd.abm_set_level.header.sub_type = DMUB_CMD__ABM_SET_LEVEL;
	cmd.abm_set_level.abm_set_level_data.level = level;
	cmd.abm_set_level.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_level_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

static bool dmub_abm_immediate_disable(struct abm *abm, uint32_t panel_inst)
{
	dmub_abm_set_pipe(abm, DISABLE_ABM_IMMEDIATELY, panel_inst);

	return true;
}

static bool dmub_abm_set_backlight_level_pwm(
		struct abm *abm,
		unsigned int backlight_pwm_u16_16,
		unsigned int frame_ramp,
		unsigned int otg_inst,
		uint32_t panel_inst)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);

	dmcub_set_backlight_level(dce_abm,
			backlight_pwm_u16_16,
			frame_ramp,
			otg_inst,
			panel_inst);

	return true;
}

static bool dmub_abm_init_config(struct abm *abm,
	const char *src,
	unsigned int bytes)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	// TODO: Optimize by only reading back final 4 bytes
	dmub_flush_buffer_mem(&dc->dmub_srv->dmub->scratch_mem_fb);

	// Copy iramtable into cw7
	memcpy(dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, (void *)src, bytes);

	// Fw will copy from cw7 to fw_state
	cmd.abm_init_config.header.type = DMUB_CMD__ABM;
	cmd.abm_init_config.header.sub_type = DMUB_CMD__ABM_INIT_CONFIG;
	cmd.abm_init_config.abm_init_config_data.src.quad_part = dc->dmub_srv->dmub->scratch_mem_fb.gpu_addr;
	cmd.abm_init_config.abm_init_config_data.bytes = bytes;
	cmd.abm_init_config.header.payload_bytes = sizeof(struct dmub_cmd_abm_init_config_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

static const struct abm_funcs abm_funcs = {
	.abm_init = dmub_abm_init,
	.set_abm_level = dmub_abm_set_level,
	.set_pipe = dmub_abm_set_pipe,
	.set_backlight_level_pwm = dmub_abm_set_backlight_level_pwm,
	.get_current_backlight = dmub_abm_get_current_backlight,
	.get_target_backlight = dmub_abm_get_target_backlight,
	.set_abm_immediate_disable = dmub_abm_immediate_disable,
	.init_abm_config = dmub_abm_init_config,
};

static void dmub_abm_construct(
	struct dce_abm *abm_dce,
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	struct abm *base = &abm_dce->base;

	base->ctx = ctx;
	base->funcs = &abm_funcs;
	base->dmcu_is_running = false;

	abm_dce->regs = regs;
	abm_dce->abm_shift = abm_shift;
	abm_dce->abm_mask = abm_mask;
}

struct abm *dmub_abm_create(
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	struct dce_abm *abm_dce = kzalloc(sizeof(*abm_dce), GFP_KERNEL);

	if (abm_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dmub_abm_construct(abm_dce, ctx, regs, abm_shift, abm_mask);

	return &abm_dce->base;
}

void dmub_abm_destroy(struct abm **abm)
{
	struct dce_abm *abm_dce = TO_DMUB_ABM(*abm);

	kfree(abm_dce);
	*abm = NULL;
}
