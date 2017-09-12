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

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "dm_services.h"

#include "include/gpio_interface.h"
#include "include/gpio_service_interface.h"
#include "hw_gpio.h"
#include "hw_translate.h"
#include "hw_factory.h"
#include "gpio_service.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Public API
 */

enum gpio_result dal_gpio_open(
	struct gpio *gpio,
	enum gpio_mode mode)
{
	return dal_gpio_open_ex(gpio, mode);
}

enum gpio_result dal_gpio_open_ex(
	struct gpio *gpio,
	enum gpio_mode mode)
{
	if (gpio->pin) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_ALREADY_OPENED;
	}

	gpio->mode = mode;

	return dal_gpio_service_open(
		gpio->service, gpio->id, gpio->en, mode, &gpio->pin);
}

enum gpio_result dal_gpio_get_value(
	const struct gpio *gpio,
	uint32_t *value)
{
	if (!gpio->pin) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_NULL_HANDLE;
	}

	return gpio->pin->funcs->get_value(gpio->pin, value);
}

enum gpio_result dal_gpio_set_value(
	const struct gpio *gpio,
	uint32_t value)
{
	if (!gpio->pin) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_NULL_HANDLE;
	}

	return gpio->pin->funcs->set_value(gpio->pin, value);
}

enum gpio_mode dal_gpio_get_mode(
	const struct gpio *gpio)
{
	return gpio->mode;
}

enum gpio_result dal_gpio_change_mode(
	struct gpio *gpio,
	enum gpio_mode mode)
{
	if (!gpio->pin) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_NULL_HANDLE;
	}

	return gpio->pin->funcs->change_mode(gpio->pin, mode);
}

enum gpio_id dal_gpio_get_id(
	const struct gpio *gpio)
{
	return gpio->id;
}

uint32_t dal_gpio_get_enum(
	const struct gpio *gpio)
{
	return gpio->en;
}

enum gpio_result dal_gpio_set_config(
	struct gpio *gpio,
	const struct gpio_config_data *config_data)
{
	if (!gpio->pin) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_NULL_HANDLE;
	}

	return gpio->pin->funcs->set_config(gpio->pin, config_data);
}

enum gpio_result dal_gpio_get_pin_info(
	const struct gpio *gpio,
	struct gpio_pin_info *pin_info)
{
	return gpio->service->translate.funcs->id_to_offset(
		gpio->id, gpio->en, pin_info) ?
		GPIO_RESULT_OK : GPIO_RESULT_INVALID_DATA;
}

enum sync_source dal_gpio_get_sync_source(
	const struct gpio *gpio)
{
	switch (gpio->id) {
	case GPIO_ID_GENERIC:
		switch (gpio->en) {
		case GPIO_GENERIC_A:
			return SYNC_SOURCE_IO_GENERIC_A;
		case GPIO_GENERIC_B:
			return SYNC_SOURCE_IO_GENERIC_B;
		case GPIO_GENERIC_C:
			return SYNC_SOURCE_IO_GENERIC_C;
		case GPIO_GENERIC_D:
			return SYNC_SOURCE_IO_GENERIC_D;
		case GPIO_GENERIC_E:
			return SYNC_SOURCE_IO_GENERIC_E;
		case GPIO_GENERIC_F:
			return SYNC_SOURCE_IO_GENERIC_F;
		default:
			return SYNC_SOURCE_NONE;
		}
	break;
	case GPIO_ID_SYNC:
		switch (gpio->en) {
		case GPIO_SYNC_HSYNC_A:
			return SYNC_SOURCE_IO_HSYNC_A;
		case GPIO_SYNC_VSYNC_A:
			return SYNC_SOURCE_IO_VSYNC_A;
		case GPIO_SYNC_HSYNC_B:
			return SYNC_SOURCE_IO_HSYNC_B;
		case GPIO_SYNC_VSYNC_B:
			return SYNC_SOURCE_IO_VSYNC_B;
		default:
			return SYNC_SOURCE_NONE;
		}
	break;
	case GPIO_ID_HPD:
		switch (gpio->en) {
		case GPIO_HPD_1:
			return SYNC_SOURCE_IO_HPD1;
		case GPIO_HPD_2:
			return SYNC_SOURCE_IO_HPD2;
		default:
			return SYNC_SOURCE_NONE;
		}
	break;
	case GPIO_ID_GSL:
		switch (gpio->en) {
		case GPIO_GSL_GENLOCK_CLOCK:
			return SYNC_SOURCE_GSL_IO_GENLOCK_CLOCK;
		case GPIO_GSL_GENLOCK_VSYNC:
			return SYNC_SOURCE_GSL_IO_GENLOCK_VSYNC;
		case GPIO_GSL_SWAPLOCK_A:
			return SYNC_SOURCE_GSL_IO_SWAPLOCK_A;
		case GPIO_GSL_SWAPLOCK_B:
			return SYNC_SOURCE_GSL_IO_SWAPLOCK_B;
		default:
			return SYNC_SOURCE_NONE;
		}
	break;
	default:
		return SYNC_SOURCE_NONE;
	}
}

enum gpio_pin_output_state dal_gpio_get_output_state(
	const struct gpio *gpio)
{
	return gpio->output_state;
}

void dal_gpio_close(
	struct gpio *gpio)
{
	if (!gpio)
		return;

	dal_gpio_service_close(gpio->service, &gpio->pin);

	gpio->mode = GPIO_MODE_UNKNOWN;
}

/*
 * @brief
 * Creation and destruction
 */

struct gpio *dal_gpio_create(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en,
	enum gpio_pin_output_state output_state)
{
	struct gpio *gpio = dm_alloc(sizeof(struct gpio));

	if (!gpio) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	gpio->service = service;
	gpio->pin = NULL;
	gpio->id = id;
	gpio->en = en;
	gpio->mode = GPIO_MODE_UNKNOWN;
	gpio->output_state = output_state;

	return gpio;
}

void dal_gpio_destroy(
	struct gpio **gpio)
{
	if (!gpio || !*gpio) {
		ASSERT_CRITICAL(false);
		return;
	}

	dal_gpio_close(*gpio);

	dm_free(*gpio);

	*gpio = NULL;
}
