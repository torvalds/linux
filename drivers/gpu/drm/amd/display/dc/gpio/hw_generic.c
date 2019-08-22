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

#include <linux/slab.h>

#include "dm_services.h"

#include "include/gpio_types.h"
#include "hw_gpio.h"
#include "hw_generic.h"

#include "reg_helper.h"
#include "generic_regs.h"

#undef FN
#define FN(reg_name, field_name) \
	generic->shifts->field_name, generic->masks->field_name

#define CTX \
	generic->base.base.ctx
#define REG(reg)\
	(generic->regs->reg)

static void dal_hw_generic_construct(
	struct hw_generic *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	dal_hw_gpio_construct(&pin->base, id, en, ctx);
}

static void dal_hw_generic_destruct(
	struct hw_generic *pin)
{
	dal_hw_gpio_destruct(&pin->base);
}

static void destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_generic *generic = HW_GENERIC_FROM_BASE(*ptr);

	dal_hw_generic_destruct(generic);

	kfree(generic);

	*ptr = NULL;
}

static enum gpio_result set_config(
	struct hw_gpio_pin *ptr,
	const struct gpio_config_data *config_data)
{
	struct hw_generic *generic = HW_GENERIC_FROM_BASE(ptr);

	if (!config_data)
		return GPIO_RESULT_INVALID_DATA;

	REG_UPDATE_2(mux,
		GENERIC_EN, config_data->config.generic_mux.enable_output_from_mux,
		GENERIC_SEL, config_data->config.generic_mux.mux_select);

	return GPIO_RESULT_OK;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = destroy,
	.open = dal_hw_gpio_open,
	.get_value = dal_hw_gpio_get_value,
	.set_value = dal_hw_gpio_set_value,
	.set_config = set_config,
	.change_mode = dal_hw_gpio_change_mode,
	.close = dal_hw_gpio_close,
};

static void construct(
	struct hw_generic *generic,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	dal_hw_generic_construct(generic, id, en, ctx);
	generic->base.base.funcs = &funcs;
}

struct hw_gpio_pin *dal_hw_generic_create(
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	struct hw_generic *generic;

	if (id != GPIO_ID_GENERIC) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if ((en < GPIO_GENERIC_MIN) || (en > GPIO_GENERIC_MAX)) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	generic = kzalloc(sizeof(struct hw_generic), GFP_KERNEL);
	if (!generic) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(generic, id, en, ctx);
	return &generic->base.base;
}
