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

#include <linux/delay.h>
#include <linux/slab.h>

#include "dm_services.h"

#include "include/gpio_interface.h"
#include "include/gpio_types.h"
#include "hw_gpio.h"
#include "hw_ddc.h"

#include "reg_helper.h"
#include "gpio_regs.h"


#undef FN
#define FN(reg_name, field_name) \
	ddc->shifts->field_name, ddc->masks->field_name

#define CTX \
	ddc->base.base.ctx
#define REG(reg)\
	(ddc->regs->reg)

struct gpio;

static void dal_hw_ddc_destruct(
	struct hw_ddc *pin)
{
	dal_hw_gpio_destruct(&pin->base);
}

static void dal_hw_ddc_destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_ddc *pin = HW_DDC_FROM_BASE(*ptr);

	dal_hw_ddc_destruct(pin);

	kfree(pin);

	*ptr = NULL;
}

static enum gpio_result set_config(
	struct hw_gpio_pin *ptr,
	const struct gpio_config_data *config_data)
{
	struct hw_ddc *ddc = HW_DDC_FROM_BASE(ptr);
	struct hw_gpio *hw_gpio = NULL;
	uint32_t regval;
	uint32_t ddc_data_pd_en = 0;
	uint32_t ddc_clk_pd_en = 0;
	uint32_t aux_pad_mode = 0;

	hw_gpio = &ddc->base;

	if (hw_gpio == NULL) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_NULL_HANDLE;
	}

	regval = REG_GET_3(gpio.MASK_reg,
			DC_GPIO_DDC1DATA_PD_EN, &ddc_data_pd_en,
			DC_GPIO_DDC1CLK_PD_EN, &ddc_clk_pd_en,
			AUX_PAD1_MODE, &aux_pad_mode);

	switch (config_data->config.ddc.type) {
	case GPIO_DDC_CONFIG_TYPE_MODE_I2C:
		/* On plug-in, there is a transient level on the pad
		 * which must be discharged through the internal pull-down.
		 * Enable internal pull-down, 2.5msec discharge time
		 * is required for detection of AUX mode */
		if (hw_gpio->base.en != GPIO_DDC_LINE_VIP_PAD) {
			if (!ddc_data_pd_en || !ddc_clk_pd_en) {

				REG_SET_2(gpio.MASK_reg, regval,
						DC_GPIO_DDC1DATA_PD_EN, 1,
						DC_GPIO_DDC1CLK_PD_EN, 1);

				if (config_data->type ==
						GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					msleep(3);
			}
		} else {
			uint32_t sda_pd_dis = 0;
			uint32_t scl_pd_dis = 0;

			REG_GET_2(gpio.MASK_reg,
				  DC_GPIO_SDA_PD_DIS, &sda_pd_dis,
				  DC_GPIO_SCL_PD_DIS, &scl_pd_dis);

			if (sda_pd_dis) {
				REG_SET(gpio.MASK_reg, regval,
						DC_GPIO_SDA_PD_DIS, 0);

				if (config_data->type ==
						GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					msleep(3);
			}

			if (!scl_pd_dis) {
				REG_SET(gpio.MASK_reg, regval,
						DC_GPIO_SCL_PD_DIS, 1);

				if (config_data->type ==
						GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					msleep(3);
			}
		}

		if (aux_pad_mode) {
			/* let pins to get de-asserted
			 * before setting pad to I2C mode */
			if (config_data->config.ddc.data_en_bit_present ||
				config_data->config.ddc.clock_en_bit_present)
				/* [anaumov] in DAL2, there was
				 * dc_service_delay_in_microseconds(2000); */
				msleep(2);

			/* set the I2C pad mode */
			/* read the register again,
			 * some bits may have been changed */
			REG_UPDATE(gpio.MASK_reg,
					AUX_PAD1_MODE, 0);
		}

		if (ddc->regs->dc_gpio_aux_ctrl_5 != 0) {
				REG_UPDATE(dc_gpio_aux_ctrl_5, DDC_PAD_I2CMODE, 1);
		}
		//set  DC_IO_aux_rxsel = 2'b01
		if (ddc->regs->phy_aux_cntl != 0) {
				REG_UPDATE(phy_aux_cntl, AUX_PAD_RXSEL, 1);
		}
		return GPIO_RESULT_OK;
	case GPIO_DDC_CONFIG_TYPE_MODE_AUX:
		/* set the AUX pad mode */
		if (!aux_pad_mode) {
			REG_SET(gpio.MASK_reg, regval,
					AUX_PAD1_MODE, 1);
		}
		if (ddc->regs->dc_gpio_aux_ctrl_5 != 0) {
			REG_UPDATE(dc_gpio_aux_ctrl_5,
					DDC_PAD_I2CMODE, 0);
		}

		return GPIO_RESULT_OK;
	case GPIO_DDC_CONFIG_TYPE_POLL_FOR_CONNECT:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			REG_UPDATE_3(ddc_setup,
				DC_I2C_DDC1_ENABLE, 1,
				DC_I2C_DDC1_EDID_DETECT_ENABLE, 1,
				DC_I2C_DDC1_EDID_DETECT_MODE, 0);
			return GPIO_RESULT_OK;
		}
	break;
	case GPIO_DDC_CONFIG_TYPE_POLL_FOR_DISCONNECT:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			REG_UPDATE_3(ddc_setup,
				DC_I2C_DDC1_ENABLE, 1,
				DC_I2C_DDC1_EDID_DETECT_ENABLE, 1,
				DC_I2C_DDC1_EDID_DETECT_MODE, 1);
			return GPIO_RESULT_OK;
		}
	break;
	case GPIO_DDC_CONFIG_TYPE_DISABLE_POLLING:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			REG_UPDATE_2(ddc_setup,
				DC_I2C_DDC1_ENABLE, 0,
				DC_I2C_DDC1_EDID_DETECT_ENABLE, 0);
			return GPIO_RESULT_OK;
		}
	break;
	}

	BREAK_TO_DEBUGGER();

	return GPIO_RESULT_NON_SPECIFIC_ERROR;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = dal_hw_ddc_destroy,
	.open = dal_hw_gpio_open,
	.get_value = dal_hw_gpio_get_value,
	.set_value = dal_hw_gpio_set_value,
	.set_config = set_config,
	.change_mode = dal_hw_gpio_change_mode,
	.close = dal_hw_gpio_close,
};

static void dal_hw_ddc_construct(
	struct hw_ddc *ddc,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	dal_hw_gpio_construct(&ddc->base, id, en, ctx);
	ddc->base.base.funcs = &funcs;
}

void dal_hw_ddc_init(
	struct hw_ddc **hw_ddc,
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	if ((en < GPIO_DDC_LINE_MIN) || (en > GPIO_DDC_LINE_MAX)) {
		ASSERT_CRITICAL(false);
		*hw_ddc = NULL;
	}

	*hw_ddc = kzalloc(sizeof(struct hw_ddc), GFP_KERNEL);
	if (!*hw_ddc) {
		ASSERT_CRITICAL(false);
		return;
	}

	dal_hw_ddc_construct(*hw_ddc, id, en, ctx);
}

struct hw_gpio_pin *dal_hw_ddc_get_pin(struct gpio *gpio)
{
	struct hw_ddc *hw_ddc = dal_gpio_get_ddc(gpio);

	return &hw_ddc->base.base;
}
