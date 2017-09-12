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

#ifndef __DAL_HW_GPIO_H__
#define __DAL_HW_GPIO_H__

#include "gpio_regs.h"

#define FROM_HW_GPIO_PIN(ptr) \
	container_of((ptr), struct hw_gpio, base)

struct addr_mask {
	uint32_t addr;
	uint32_t mask;
};

struct hw_gpio_pin {
	const struct hw_gpio_pin_funcs *funcs;
	enum gpio_id id;
	uint32_t en;
	enum gpio_mode mode;
	bool opened;
	struct dc_context *ctx;
};

struct hw_gpio_pin_funcs {
	void (*destroy)(
		struct hw_gpio_pin **ptr);
	bool (*open)(
		struct hw_gpio_pin *pin,
		enum gpio_mode mode);
	enum gpio_result (*get_value)(
		const struct hw_gpio_pin *pin,
		uint32_t *value);
	enum gpio_result (*set_value)(
		const struct hw_gpio_pin *pin,
		uint32_t value);
	enum gpio_result (*set_config)(
		struct hw_gpio_pin *pin,
		const struct gpio_config_data *config_data);
	enum gpio_result (*change_mode)(
		struct hw_gpio_pin *pin,
		enum gpio_mode mode);
	void (*close)(
		struct hw_gpio_pin *pin);
};


struct hw_gpio;

/* Register indices are represented by member variables
 * and are to be filled in by constructors of derived classes.
 * These members permit the use of common code
 * for programming registers, where the sequence is the same
 * but register sets are different.
 * Some GPIOs have HW mux which allows to choose
 * what is the source of the signal in HW mode */

struct hw_gpio_pin_reg {
	struct addr_mask DC_GPIO_DATA_MASK;
	struct addr_mask DC_GPIO_DATA_A;
	struct addr_mask DC_GPIO_DATA_EN;
	struct addr_mask DC_GPIO_DATA_Y;
};

struct hw_gpio_mux_reg {
	struct addr_mask GPIO_MUX_CONTROL;
	struct addr_mask GPIO_MUX_STEREO_SEL;
};

struct hw_gpio {
	struct hw_gpio_pin base;

	/* variables to save register value */
	struct {
		uint32_t mask;
		uint32_t a;
		uint32_t en;
		uint32_t mux;
	} store;

	/* GPIO MUX support */
	bool mux_supported;
	const struct gpio_registers *regs;
};

#define HW_GPIO_FROM_BASE(hw_gpio_pin) \
	container_of((hw_gpio_pin), struct hw_gpio, base)

bool dal_hw_gpio_construct(
	struct hw_gpio *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx);

bool dal_hw_gpio_open(
	struct hw_gpio_pin *pin,
	enum gpio_mode mode);

enum gpio_result dal_hw_gpio_get_value(
	const struct hw_gpio_pin *pin,
	uint32_t *value);

enum gpio_result dal_hw_gpio_config_mode(
	struct hw_gpio *pin,
	enum gpio_mode mode);

void dal_hw_gpio_destruct(
	struct hw_gpio *pin);

enum gpio_result dal_hw_gpio_set_value(
	const struct hw_gpio_pin *ptr,
	uint32_t value);

enum gpio_result dal_hw_gpio_change_mode(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode);

void dal_hw_gpio_close(
	struct hw_gpio_pin *ptr);

#endif
