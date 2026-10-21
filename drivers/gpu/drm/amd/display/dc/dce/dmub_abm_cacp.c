/* Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved. */

#include "dmub_abm.h"
#include "dmub_abm_cacp.h"
#include "dce_abm.h"
#include "dc.h"
#include "dc_dmub_srv.h"
#include "dmub/dmub_srv.h"
#include "core_types.h"

#define CACP_LEVEL_NUM 4

void dmub_cacp_init(struct abm *abm, const char *src, unsigned int bytes, unsigned int panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint8_t panel_mask = 0x01 << panel_inst;
	struct dc_link *edp_links[MAX_NUM_EDP];
	unsigned int i, edp_num;

	// TODO: Optimize by only reading back final 4 bytes
	dmub_srv_flush_buffer_mem(dc->dmub_srv->dmub, &dc->dmub_srv->dmub->scratch_mem_fb);

	// Copy iramtable into cw7
	memcpy(dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, (void *)src, bytes);

	memset(&cmd, 0, sizeof(cmd));
	// Fw will copy from cw7 to fw_state
	cmd.cacp_init_config.header.type = DMUB_CMD__CACP;
	cmd.cacp_init_config.header.sub_type = DMUB_CMD__CACP_INIT_CONFIG;
	cmd.cacp_init_config.cacp_init_config_data.src.quad_part = dc->dmub_srv->dmub->scratch_mem_fb.gpu_addr;
	cmd.cacp_init_config.cacp_init_config_data.bytes = (uint16_t)bytes;
	cmd.cacp_init_config.cacp_init_config_data.panel_mask = panel_mask;
	cmd.cacp_init_config.cacp_init_config_data.visual_confirm =
		(dc->dc->debug.visual_confirm == VISUAL_CONFIRM_ABM) ? true : false;

	cmd.cacp_init_config.header.payload_bytes = sizeof(struct dmub_cmd_cacp_init_config_data);

	dc_get_edp_links(dc->dc, edp_links, &edp_num);
	for (i = 0; i < edp_num; i++) {
		if (panel_inst == i)
			break;
	}

	if (i < edp_num) {
		cmd.cacp_init_config.cacp_init_config_data.strscl_valid =
			(uint8_t)edp_links[panel_inst]->panel_config.cacp.strscl_valid;
		cmd.cacp_init_config.cacp_init_config_data.mode =
			edp_links[panel_inst]->panel_config.cacp.cacp_control_mode ?
			DMUB_CMD_CACP_CONTROL_MODE_1 : DMUB_CMD_CACP_CONTROL_MODE_0;
		for (int j = 0; j < CACP_LEVEL_NUM; j++) {
			cmd.cacp_init_config.cacp_init_config_data.strscl_sdr[j] =
				(uint8_t)edp_links[panel_inst]->panel_config.cacp.strscl_sdr[j];
			cmd.cacp_init_config.cacp_init_config_data.strscl_hdr[j] =
				(uint8_t)edp_links[panel_inst]->panel_config.cacp.strscl_hdr[j];
		}
	}

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dmub_cacp_set_level(struct abm *abm, unsigned int abm_level, unsigned char panel_mask)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_set_level.header.type = DMUB_CMD__CACP;
	cmd.cacp_set_level.header.sub_type = DMUB_CMD__CACP_SET_LEVEL;

	cmd.cacp_set_level.cacp_set_level_data.level = abm_level;
	cmd.cacp_set_level.cacp_set_level_data.version = DMUB_CMD_CACP_CONTROL_VERSION_1;
	cmd.cacp_set_level.cacp_set_level_data.panel_mask = panel_mask;

	cmd.cacp_set_level.header.payload_bytes = sizeof(struct dmub_cmd_cacp_set_level_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dmub_cacp_set_pipe(struct abm *abm, unsigned int otg_inst,
		unsigned int pipe_option, unsigned int panel_inst, unsigned int pwrseq_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_set_pipe.header.type = DMUB_CMD__CACP;
	cmd.cacp_set_pipe.header.sub_type = DMUB_CMD__CACP_SET_PIPE;

	cmd.cacp_set_pipe.cacp_set_pipe_data.otg_inst = (uint8_t)otg_inst;
	cmd.cacp_set_pipe.cacp_set_pipe_data.panel_inst = (uint8_t)panel_inst;
	cmd.cacp_set_pipe.cacp_set_pipe_data.set_pipe_option = (uint8_t)pipe_option;
	cmd.cacp_set_pipe.cacp_set_pipe_data.pwrseq_inst = (uint8_t)pwrseq_inst;
	cmd.cacp_set_pipe.header.payload_bytes = sizeof(struct dmub_cmd_cacp_set_pipe_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dmub_cacp_set_event(struct abm *abm, unsigned int full_screen, unsigned int trans_info,
		unsigned int hdr_mode, unsigned int scaling_enable, unsigned int panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_set_event.header.type = DMUB_CMD__CACP;
	cmd.cacp_set_event.header.sub_type = DMUB_CMD__CACP_SET_EVENT;

	//TODO:
	cmd.cacp_set_event.cacp_set_event_data.full_screen_mode = (uint8_t)full_screen;
	cmd.cacp_set_event.cacp_set_event_data.trans_info = trans_info;
	cmd.cacp_set_event.cacp_set_event_data.hdr_mode = (uint8_t)hdr_mode;
	cmd.cacp_set_event.cacp_set_event_data.vb_scaling_enable = (uint8_t)scaling_enable;
	cmd.cacp_set_event.cacp_set_event_data.panel_mask = (1<<panel_inst);

	cmd.cacp_set_event.header.payload_bytes = sizeof(struct dmub_cmd_cacp_set_event_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dmub_cacp_set_pause(struct abm *abm, bool pause, unsigned int panel_inst, unsigned int otg_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_pause.header.type = DMUB_CMD__CACP;
	cmd.cacp_pause.header.sub_type = DMUB_CMD__CACP_PAUSE;

	cmd.cacp_pause.cacp_pause_data.panel_mask = (1<<panel_inst);
	cmd.cacp_pause.cacp_pause_data.otg_inst = (uint8_t)otg_inst;
	cmd.cacp_pause.cacp_pause_data.enable = pause;

	cmd.cacp_pause.header.payload_bytes = sizeof(struct dmub_cmd_cacp_pause_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dmub_cacp_set_backlight_level(struct abm *abm,
		unsigned int backlight_pwm_u16_16,
		unsigned int frame_ramp,
		unsigned int panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_set_backlight.header.type = DMUB_CMD__CACP;
	cmd.cacp_set_backlight.header.sub_type = DMUB_CMD__CACP_SET_BACKLIGHT;
	cmd.cacp_set_backlight.cacp_set_backlight_data.frame_ramp = frame_ramp;
	cmd.cacp_set_backlight.cacp_set_backlight_data.backlight_user_level = backlight_pwm_u16_16;
	cmd.cacp_set_backlight.cacp_set_backlight_data.version = DMUB_CMD_CACP_CONTROL_VERSION_1;
	cmd.cacp_set_backlight.cacp_set_backlight_data.panel_mask = (0x01 << panel_inst);
	cmd.cacp_set_backlight.header.payload_bytes = sizeof(struct dmub_cmd_cacp_set_backlight_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

void dmub_cacp_enable_fractional_pwm(struct abm *abm, uint8_t panel_mask)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint32_t fractional_pwm = (dc->dc->config.disable_fractional_pwm == false) ? 1 : 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_set_pwm_frac.header.type = DMUB_CMD__CACP;
	cmd.cacp_set_pwm_frac.header.sub_type = DMUB_CMD__CACP_SET_PWM_FRAC;
	cmd.cacp_set_pwm_frac.cacp_set_pwm_frac_data.fractional_pwm = fractional_pwm;
	cmd.cacp_set_pwm_frac.cacp_set_pwm_frac_data.version = DMUB_CMD_CACP_CONTROL_VERSION_1;
	cmd.cacp_set_pwm_frac.cacp_set_pwm_frac_data.panel_mask = panel_mask;
	cmd.cacp_set_pwm_frac.header.payload_bytes = sizeof(struct dmub_cmd_cacp_set_pwm_frac_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dmub_cacp_get_histogram(
		struct dc_context *dc,
		unsigned int panel_inst,
		unsigned int *histogram,
		enum dmub_abm_histogram_type histogram_type,
		unsigned int size)
{
	union dmub_rb_cmd cmd;

	dmub_srv_flush_buffer_mem(dc->dmub_srv->dmub, &dc->dmub_srv->dmub->scratch_mem_fb);

	memset(&cmd, 0, sizeof(cmd));
	cmd.cacp_get_histogram.header.type = DMUB_CMD__CACP;
	cmd.cacp_get_histogram.header.sub_type = DMUB_CMD__CACP_GET_HISTOGRAM;

	cmd.cacp_get_histogram.dest.quad_part = dc->dmub_srv->dmub->scratch_mem_fb.gpu_addr;
	cmd.cacp_get_histogram.bytes = (uint16_t)size;
	cmd.cacp_get_histogram.panel_inst = (uint8_t)panel_inst;
	cmd.cacp_get_histogram.histogram_type = histogram_type;
	cmd.cacp_get_histogram.header.payload_bytes = sizeof(struct dmub_rb_cmd_cacp_get_histogram);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	memcpy((void *)histogram, dc->dmub_srv->dmub->scratch_mem_fb.cpu_addr, size);

	return true;
}

