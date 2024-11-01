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

#ifndef __DAL_GPIO_SERVICE_INTERFACE_H__
#define __DAL_GPIO_SERVICE_INTERFACE_H__

#include "gpio_types.h"
#include "gpio_interface.h"
#include "hw/gpio.h"

struct gpio_service;

struct gpio *dal_gpio_create(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en,
	enum gpio_pin_output_state output_state);

void dal_gpio_destroy(
	struct gpio **ptr);

struct gpio_service *dal_gpio_service_create(
	enum dce_version dce_version,
	enum dce_environment dce_environment,
	struct dc_context *ctx);

struct gpio *dal_gpio_service_create_irq(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask);

struct gpio *dal_gpio_service_create_generic_mux(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask);

void dal_gpio_destroy_generic_mux(
	struct gpio **mux);

enum gpio_result dal_mux_setup_config(
	struct gpio *mux,
	struct gpio_generic_mux_config *config);

struct gpio_pin_info dal_gpio_get_generic_pin_info(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en);

struct ddc *dal_gpio_create_ddc(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	struct gpio_ddc_hw_info *info);

void dal_gpio_destroy_ddc(
	struct ddc **ddc);

void dal_gpio_service_destroy(
	struct gpio_service **ptr);

enum dc_irq_source dal_irq_get_source(
	const struct gpio *irq);

enum dc_irq_source dal_irq_get_rx_source(
	const struct gpio *irq);

enum gpio_result dal_irq_setup_hpd_filter(
	struct gpio *irq,
	struct gpio_hpd_config *config);

struct gpio *dal_gpio_create_irq(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en);

void dal_gpio_destroy_irq(
	struct gpio **ptr);


enum gpio_result dal_ddc_open(
	struct ddc *ddc,
	enum gpio_mode mode,
	enum gpio_ddc_config_type config_type);

enum gpio_result dal_ddc_change_mode(
	struct ddc *ddc,
	enum gpio_mode mode);

enum gpio_ddc_line dal_ddc_get_line(
	const struct ddc *ddc);

enum gpio_result dal_ddc_set_config(
	struct ddc *ddc,
	enum gpio_ddc_config_type config_type);

void dal_ddc_close(
	struct ddc *ddc);

#endif
