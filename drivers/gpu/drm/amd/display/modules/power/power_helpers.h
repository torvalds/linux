/* Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef MODULES_POWER_POWER_HELPERS_H_
#define MODULES_POWER_POWER_HELPERS_H_

#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"

struct resource_pool;


enum abm_defines {
	abm_defines_max_level = 4,
	abm_defines_max_config = 4,
};

struct dmcu_iram_parameters {
	unsigned int *backlight_lut_array;
	unsigned int backlight_lut_array_size;
	bool backlight_ramping_override;
	unsigned int backlight_ramping_reduction;
	unsigned int backlight_ramping_start;
	unsigned int min_abm_backlight;
	unsigned int set;
};

bool dmcu_load_iram(struct dmcu *dmcu,
		struct dmcu_iram_parameters params);
bool dmub_init_abm_config(struct resource_pool *res_pool,
		struct dmcu_iram_parameters params,
		unsigned int inst);

#endif /* MODULES_POWER_POWER_HELPERS_H_ */
