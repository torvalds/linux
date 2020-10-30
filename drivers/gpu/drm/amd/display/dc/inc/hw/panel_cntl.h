/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 */
/*
 * panel_cntl.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef DC_PANEL_CNTL_H_
#define DC_PANEL_CNTL_H_

#include "dc_types.h"

#define MAX_BACKLIGHT_LEVEL 0xFFFF

struct panel_cntl_backlight_registers {
	unsigned int BL_PWM_CNTL;
	unsigned int BL_PWM_CNTL2;
	unsigned int BL_PWM_PERIOD_CNTL;
	unsigned int LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV;
};

struct panel_cntl_funcs {
	void (*destroy)(struct panel_cntl **panel_cntl);
	uint32_t (*hw_init)(struct panel_cntl *panel_cntl);
	bool (*is_panel_backlight_on)(struct panel_cntl *panel_cntl);
	bool (*is_panel_powered_on)(struct panel_cntl *panel_cntl);
	void (*store_backlight_level)(struct panel_cntl *panel_cntl);
	void (*driver_set_backlight)(struct panel_cntl *panel_cntl,
			uint32_t backlight_pwm_u16_16);
	uint32_t (*get_current_backlight)(struct panel_cntl *panel_cntl);
};

struct panel_cntl_init_data {
	struct dc_context *ctx;
	uint32_t inst;
};

struct panel_cntl {
	const struct panel_cntl_funcs *funcs;
	struct dc_context *ctx;
	uint32_t inst;
	/* registers setting needs to be saved and restored at InitBacklight */
	struct panel_cntl_backlight_registers stored_backlight_registers;
};

#endif /* DC_PANEL_CNTL_H_ */
