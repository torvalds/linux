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

#ifndef __DAL_GPIO_INTERFACE_H__
#define __DAL_GPIO_INTERFACE_H__

#include "gpio_types.h"
#include "grph_object_defs.h"

struct gpio;

/* Open the handle for future use */
enum gpio_result dal_gpio_open(
	struct gpio *gpio,
	enum gpio_mode mode);

enum gpio_result dal_gpio_open_ex(
	struct gpio *gpio,
	enum gpio_mode mode);

/* Get high or low from the pin */
enum gpio_result dal_gpio_get_value(
	const struct gpio *gpio,
	uint32_t *value);

/* Set pin high or low */
enum gpio_result dal_gpio_set_value(
	const struct gpio *gpio,
	uint32_t value);

/* Get current mode */
enum gpio_mode dal_gpio_get_mode(
	const struct gpio *gpio);

/* Change mode of the handle */
enum gpio_result dal_gpio_change_mode(
	struct gpio *gpio,
	enum gpio_mode mode);

/* Lock Pin */
enum gpio_result dal_gpio_lock_pin(
	struct gpio *gpio);

/* Unlock Pin */
enum gpio_result dal_gpio_unlock_pin(
	struct gpio *gpio);

/* Get the GPIO id */
enum gpio_id dal_gpio_get_id(
	const struct gpio *gpio);

/* Get the GPIO enum */
uint32_t dal_gpio_get_enum(
	const struct gpio *gpio);

/* Set the GPIO pin configuration */
enum gpio_result dal_gpio_set_config(
	struct gpio *gpio,
	const struct gpio_config_data *config_data);

/* Obtain GPIO pin info */
enum gpio_result dal_gpio_get_pin_info(
	const struct gpio *gpio,
	struct gpio_pin_info *pin_info);

/* Obtain GPIO sync source */
enum sync_source dal_gpio_get_sync_source(
	const struct gpio *gpio);

/* Obtain GPIO pin output state (active low or active high) */
enum gpio_pin_output_state dal_gpio_get_output_state(
	const struct gpio *gpio);

struct hw_ddc *dal_gpio_get_ddc(struct gpio *gpio);

struct hw_hpd *dal_gpio_get_hpd(struct gpio *gpio);

struct hw_generic *dal_gpio_get_generic(struct gpio *gpio);

/* Close the handle */
void dal_gpio_close(
	struct gpio *gpio);




#endif
