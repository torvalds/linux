/* Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved. */

#include "dmub_cmd.h"

#ifndef __DMUB_ABM_CACP_H__
#define __DMUB_ABM_CACP_H__

void dmub_cacp_init(struct abm *abm, const char *src, unsigned int bytes, unsigned int panel_inst);
bool dmub_cacp_set_level(struct abm *abm, unsigned int cacp_level, unsigned char panel_mask);
bool dmub_cacp_set_pipe(struct abm *abm, unsigned int otg_inst, unsigned int pipe_option,
						unsigned int panel_inst, unsigned int pwrseq_inst);
bool dmub_cacp_set_event(struct abm *abm, unsigned int full_screen, unsigned int trans_info, unsigned int hdr_mode,
						unsigned int scaling_enable, unsigned int panel_inst);
bool dmub_cacp_set_pause(struct abm *abm, bool pause, unsigned int panel_inst, unsigned int otg_inst);
bool dmub_cacp_set_backlight_level(struct abm *abm, unsigned int backlight_pwm_u16_16, unsigned int frame_ramp,
						unsigned int panel_inst);
void dmub_cacp_enable_fractional_pwm(struct abm *abm, uint8_t panel_mask);
bool dmub_cacp_get_histogram(struct dc_context *dc, unsigned int panel_inst, unsigned int *histogram,
		enum dmub_abm_histogram_type histogram_type, unsigned int size);
#endif
