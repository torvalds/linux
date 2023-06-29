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

#ifndef __DMUB_ABM_LCD_H__
#define __DMUB_ABM_LCD_H__

#include "abm.h"

void dmub_abm_init(struct abm *abm, uint32_t backlight);
bool dmub_abm_set_level(struct abm *abm, uint32_t level, uint8_t panel_mask);
unsigned int dmub_abm_get_current_backlight(struct abm *abm);
unsigned int dmub_abm_get_target_backlight(struct abm *abm);
void dmub_abm_init_config(struct abm *abm,
	const char *src,
	unsigned int bytes,
	unsigned int inst);

bool dmub_abm_set_pause(struct abm *abm, bool pause, unsigned int panel_inst, unsigned int stream_inst);
bool dmub_abm_set_pipe(struct abm *abm, uint32_t otg_inst, uint32_t option, uint32_t panel_inst);
bool dmub_abm_set_backlight_level(struct abm *abm,
		unsigned int backlight_pwm_u16_16,
		unsigned int frame_ramp,
		unsigned int panel_inst);
#endif
