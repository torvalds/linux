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
 * panel.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef DC_PANEL_H_
#define DC_PANEL_H_

#include "dc_types.h"

struct panel_funcs {
	void (*destroy)(struct panel **panel);
	void (*hw_init)(struct panel *panel);
	bool (*is_panel_backlight_on)(struct panel *panel);
	bool (*is_panel_powered_on)(struct panel *panel);
};

struct panel_init_data {
	struct dc_context *ctx;
	uint32_t inst;
};

struct panel {
	const struct panel_funcs *funcs;
	struct dc_context *ctx;
	uint32_t inst;
};

#endif /* DC_PANEL_H_ */
