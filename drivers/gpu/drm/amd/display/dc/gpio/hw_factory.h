/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_HW_FACTORY_H__
#define __DAL_HW_FACTORY_H__

struct hw_gpio_pin;
struct hw_hpd;
struct hw_ddc;
struct hw_generic;
struct gpio;

struct hw_factory {
	uint32_t number_of_pins[GPIO_ID_COUNT];

	const struct hw_factory_funcs {
		void (*init_ddc_data)(
				struct hw_ddc **hw_ddc,
				struct dc_context *ctx,
				enum gpio_id id,
				uint32_t en);
		void (*init_generic)(
				struct hw_generic **hw_generic,
				struct dc_context *ctx,
				enum gpio_id id,
				uint32_t en);
		void (*init_hpd)(
				struct hw_hpd **hw_hpd,
				struct dc_context *ctx,
				enum gpio_id id,
				uint32_t en);
		struct hw_gpio_pin *(*get_hpd_pin)(
				struct gpio *gpio);
		struct hw_gpio_pin *(*get_ddc_pin)(
				struct gpio *gpio);
		struct hw_gpio_pin *(*get_generic_pin)(
				struct gpio *gpio);
		void (*define_hpd_registers)(
				struct hw_gpio_pin *pin,
				uint32_t en);
		void (*define_ddc_registers)(
				struct hw_gpio_pin *pin,
				uint32_t en);
		void (*define_generic_registers)(
				struct hw_gpio_pin *pin,
				uint32_t en);
	} *funcs;
};

bool dal_hw_factory_init(
	struct hw_factory *factory,
	enum dce_version dce_version,
	enum dce_environment dce_environment);

#endif
