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
#include "hw_hpd.h"

#include "reg_helper.h"
#include "hpd_regs.h"

#undef FN
#define FN(reg_name, field_name) \
	hpd->shifts->field_name, hpd->masks->field_name

#define CTX \
	hpd->base.base.ctx
#define REG(reg)\
	(hpd->regs->reg)

static void dal_hw_hpd_construct(
	struct hw_hpd *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	dal_hw_gpio_construct(&pin->base, id, en, ctx);
}

static void dal_hw_hpd_destruct(
	struct hw_hpd *pin)
{
	dal_hw_gpio_destruct(&pin->base);
}


static void destruct(
	struct hw_hpd *hpd)
{
	dal_hw_hpd_destruct(hpd);
}

static void destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_hpd *hpd = HW_HPD_FROM_BASE(*ptr);

	destruct(hpd);

	kfree(hpd);

	*ptr = NULL;
}

static enum gpio_result get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value)
{
	struct hw_hpd *hpd = HW_HPD_FROM_BASE(ptr);
	uint32_t hpd_delayed = 0;

	/* in Interrupt mode we ask for SENSE bit */

	if (ptr->mode == GPIO_MODE_INTERRUPT) {

		REG_GET(int_status,
			DC_HPD_SENSE_DELAYED, &hpd_delayed);

		*value = hpd_delayed;
		return GPIO_RESULT_OK;
	}

	/* in any other modes, operate as normal GPIO */

	return dal_hw_gpio_get_value(ptr, value);
}

static enum gpio_result set_config(
	struct hw_gpio_pin *ptr,
	const struct gpio_config_data *config_data)
{
	struct hw_hpd *hpd = HW_HPD_FROM_BASE(ptr);

	if (!config_data)
		return GPIO_RESULT_INVALID_DATA;

	REG_UPDATE_2(toggle_filt_cntl,
		DC_HPD_CONNECT_INT_DELAY, config_data->config.hpd.delay_on_connect / 10,
		DC_HPD_DISCONNECT_INT_DELAY, config_data->config.hpd.delay_on_disconnect / 10);

	return GPIO_RESULT_OK;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = destroy,
	.open = dal_hw_gpio_open,
	.get_value = get_value,
	.set_value = dal_hw_gpio_set_value,
	.set_config = set_config,
	.change_mode = dal_hw_gpio_change_mode,
	.close = dal_hw_gpio_close,
};

static void construct(
	struct hw_hpd *hpd,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	dal_hw_hpd_construct(hpd, id, en, ctx);
	hpd->base.base.funcs = &funcs;
}

struct hw_gpio_pin *dal_hw_hpd_create(
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	struct hw_hpd *hpd;

	if (id != GPIO_ID_HPD) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if ((en < GPIO_HPD_MIN) || (en > GPIO_HPD_MAX)) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	hpd = kzalloc(sizeof(struct hw_hpd), GFP_KERNEL);
	if (!hpd) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(hpd, id, en, ctx);
	return &hpd->base.base;
}
