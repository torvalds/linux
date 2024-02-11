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
#include "dmub_abm_lcd.h"
#include "dce_abm.h"
#include "dc.h"
#include "dc_dmub_srv.h"
#include "dmub/dmub_srv.h"
#include "core_types.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed31_32.h"

#ifdef _WIN32
#include "atombios.h"
#else
#include "atom.h"
#endif

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



static void dmub_abm_enable_fractional_pwm(struct dc_context *dc)
{
	union dmub_rb_cmd cmd;
	uint32_t fractional_pwm = (dc->dc->config.disable_fractional_pwm == false) ? 1 : 0;
	uint32_t edp_id_count = dc->dc_edp_id_count;
	int i;
	uint8_t panel_mask = 0;

	for (i = 0; i < edp_id_count; i++)
		panel_mask |= 0x01 << i;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_pwm_frac.header.type = DMUB_CMD__ABM;
	cmd.abm_set_pwm_frac.header.sub_type = DMUB_CMD__ABM_SET_PWM_FRAC;
	cmd.abm_set_pwm_frac.abm_set_pwm_frac_data.fractional_pwm = fractional_pwm;
	cmd.abm_set_pwm_frac.abm_set_pwm_frac_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_set_pwm_frac.abm_set_pwm_frac_data.panel_mask = panel_mask;
	cmd.abm_set_pwm_frac.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_pwm_frac_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dmub_abm_init(struct abm *abm, uint32_t backlight)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);

	REG_WRITE(DC_ABM1_HG_SAMPLE_RATE, 0x3);
	REG_WRITE(DC_ABM1_HG_SAMPLE_RATE, 0x1);
	REG_WRITE(DC_ABM1_LS_SAMPLE_RATE, 0x3);
	REG_WRITE(DC_ABM1_LS_SAMPLE_RATE, 0x1);
	REG_WRITE(BL1_PWM_BL_UPDATE_SAMPLE_RATE, 0x1);

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

unsigned int dmub_abm_get_current_backlight(struct abm *abm)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);
	unsigned int backlight = REG_READ(BL1_PWM_CURRENT_ABM_LEVEL);

	/* return backlight in hardware format which is unsigned 17 bits, with
	 * 1 bit integer and 16 bit fractional
	 */
	return backlight;
}

unsigned int dmub_abm_get_target_backlight(struct abm *abm)
{
	struct dce_abm *dce_abm = TO_DMUB_ABM(abm);
	unsigned int backlight = REG_READ(BL1_PWM_TARGET_ABM_LEVEL);

	/* return backlight in hardware format which is unsigned 17 bits, with
	 * 1 bit integer and 16 bit fractional
	 */
	return backlight;
}

bool dmub_abm_set_level(struct abm *abm, uint32_t level, uint8_t panel_mask)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_level.header.type = DMUB_CMD__ABM;
	cmd.abm_set_level.header.sub_type = DMUB_CMD__ABM_SET_LEVEL;
	cmd.abm_set_level.abm_set_level_data.level = level;
	cmd.abm_set_level.abm_set_level_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_set_level.abm_set_level_data.panel_mask = panel_mask;
	cmd.abm_set_level.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_level_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

void dmub_abm_init_config(struct abm *abm,
	const char *src,
	unsigned int bytes,
	unsigned int inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint8_t panel_mask = 0x01 << inst;

	// TODO: Optimize by only reading back final 4 bytes
	dmub_flush_buffer_mem(&dc->dmub_srv->dmub->scratch_mem_fb);

	// Copy iramtable into cw7
	memcpy(dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, (void *)src, bytes);

	memset(&cmd, 0, sizeof(cmd));
	// Fw will copy from cw7 to fw_state
	cmd.abm_init_config.header.type = DMUB_CMD__ABM;
	cmd.abm_init_config.header.sub_type = DMUB_CMD__ABM_INIT_CONFIG;
	cmd.abm_init_config.abm_init_config_data.src.quad_part = dc->dmub_srv->dmub->scratch_mem_fb.gpu_addr;
	cmd.abm_init_config.abm_init_config_data.bytes = bytes;
	cmd.abm_init_config.abm_init_config_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_init_config.abm_init_config_data.panel_mask = panel_mask;

	cmd.abm_init_config.header.payload_bytes = sizeof(struct dmub_cmd_abm_init_config_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

}

bool dmub_abm_set_pause(struct abm *abm, bool pause, unsigned int panel_inst, unsigned int stream_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint8_t panel_mask = 0x01 << panel_inst;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_pause.header.type = DMUB_CMD__ABM;
	cmd.abm_pause.header.sub_type = DMUB_CMD__ABM_PAUSE;
	cmd.abm_pause.abm_pause_data.enable = pause;
	cmd.abm_pause.abm_pause_data.panel_mask = panel_mask;
	cmd.abm_set_level.header.payload_bytes = sizeof(struct dmub_cmd_abm_pause_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}


/*****************************************************************************
 *  dmub_abm_save_restore() - dmub interface for abm save+pause and restore+
 *                           un-pause
 *  @dc: dc context
 *  @panel_inst: panel instance index
 *  @pData: contains command to pause/un-pause abm and exchange abm parameters
 *
 *  When called Pause will get abm data and store in pData, and un-pause will
 *  set/apply abm data stored in pData.
 *
 *****************************************************************************/
bool dmub_abm_save_restore(
		struct dc_context *dc,
		unsigned int panel_inst,
		struct abm_save_restore *pData)
{
	union dmub_rb_cmd cmd;
	uint8_t panel_mask = 0x01 << panel_inst;
	unsigned int bytes = sizeof(struct abm_save_restore);

	// TODO: Optimize by only reading back final 4 bytes
	dmub_flush_buffer_mem(&dc->dmub_srv->dmub->scratch_mem_fb);

	// Copy iramtable into cw7
	memcpy(dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, (void *)pData, bytes);

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_save_restore.header.type = DMUB_CMD__ABM;
	cmd.abm_save_restore.header.sub_type = DMUB_CMD__ABM_SAVE_RESTORE;

	cmd.abm_save_restore.abm_init_config_data.src.quad_part = dc->dmub_srv->dmub->scratch_mem_fb.gpu_addr;
	cmd.abm_save_restore.abm_init_config_data.bytes = bytes;
	cmd.abm_save_restore.abm_init_config_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_save_restore.abm_init_config_data.panel_mask = panel_mask;

	cmd.abm_save_restore.header.payload_bytes = sizeof(struct dmub_rb_cmd_abm_save_restore);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	// Copy iramtable data into local structure
	memcpy((void *)pData, dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, bytes);

	return true;
}

bool dmub_abm_set_pipe(struct abm *abm,
		uint32_t otg_inst,
		uint32_t option,
		uint32_t panel_inst,
		uint32_t pwrseq_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint32_t ramping_boundary = 0xFFFF;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_pipe.header.type = DMUB_CMD__ABM;
	cmd.abm_set_pipe.header.sub_type = DMUB_CMD__ABM_SET_PIPE;
	cmd.abm_set_pipe.abm_set_pipe_data.otg_inst = otg_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.pwrseq_inst = pwrseq_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.set_pipe_option = option;
	cmd.abm_set_pipe.abm_set_pipe_data.panel_inst = panel_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.ramping_boundary = ramping_boundary;
	cmd.abm_set_pipe.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_pipe_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dmub_abm_set_backlight_level(struct abm *abm,
		unsigned int backlight_pwm_u16_16,
		unsigned int frame_ramp,
		unsigned int panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_backlight.header.type = DMUB_CMD__ABM;
	cmd.abm_set_backlight.header.sub_type = DMUB_CMD__ABM_SET_BACKLIGHT;
	cmd.abm_set_backlight.abm_set_backlight_data.frame_ramp = frame_ramp;
	cmd.abm_set_backlight.abm_set_backlight_data.backlight_user_level = backlight_pwm_u16_16;
	cmd.abm_set_backlight.abm_set_backlight_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_set_backlight.abm_set_backlight_data.panel_mask = (0x01 << panel_inst);
	cmd.abm_set_backlight.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_backlight_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

