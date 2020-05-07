/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_gpio_88xx.h"

#if HALMAC_88XX_SUPPORT

/**
 * pinmux_wl_led_mode_88xx() -control wlan led gpio function
 * @adapter : the adapter of halmac
 * @mode : wlan led mode
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pinmux_wl_led_mode_88xx(struct halmac_adapter *adapter,
			enum halmac_wlled_mode mode)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value8 = HALMAC_REG_R8(REG_LED_CFG + 2);
	value8 &= ~(BIT(6));
	value8 |= BIT(3);
	value8 &= ~(BIT(0) | BIT(1) | BIT(2));

	switch (mode) {
	case HALMAC_WLLED_MODE_TRX:
		value8 |= 2;
		break;
	case HALMAC_WLLED_MODE_TX:
		value8 |= 4;
		break;
	case HALMAC_WLLED_MODE_RX:
		value8 |= 6;
		break;
	case HALMAC_WLLED_MODE_SW_CTRL:
		value8 |= 0;
		break;
	default:
		return HALMAC_RET_SWITCH_CASE_ERROR;
	}

	HALMAC_REG_W8(REG_LED_CFG + 2, value8);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * pinmux_wl_led_sw_ctrl_88xx() -control wlan led on/off
 * @adapter : the adapter of halmac
 * @on : on(1), off(0)
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
void
pinmux_wl_led_sw_ctrl_88xx(struct halmac_adapter *adapter, u8 on)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_LED_CFG + 2);
	value8 = (on == 0) ? value8 | BIT(3) : value8 & ~(BIT(3));

	HALMAC_REG_W8(REG_LED_CFG + 2, value8);
}

/**
 * pinmux_sdio_int_polarity_88xx() -control sdio int polarity
 * @adapter : the adapter of halmac
 * @low_active : low active(1), high active(0)
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
void
pinmux_sdio_int_polarity_88xx(struct halmac_adapter *adapter, u8 low_active)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_SYS_SDIO_CTRL + 2);
	value8 = (low_active == 0) ? value8 | BIT(3) : value8 & ~(BIT(3));

	HALMAC_REG_W8(REG_SYS_SDIO_CTRL + 2, value8);
}

/**
 * pinmux_gpio_mode_88xx() -control gpio io mode
 * @adapter : the adapter of halmac
 * @gpio_id : gpio0~15(0~15)
 * @output : output(1), input(0)
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pinmux_gpio_mode_88xx(struct halmac_adapter *adapter, u8 gpio_id, u8 output)
{
	u16 value16;
	u8 in_out;
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (gpio_id <= 7)
		offset = REG_GPIO_PIN_CTRL + 2;
	else if (gpio_id >= 8 && gpio_id <= 15)
		offset = REG_GPIO_EXT_CTRL + 2;
	else
		return HALMAC_RET_WRONG_GPIO;

	in_out = (output == 0) ? 0 : 1;
	gpio_id &= (8 - 1);

	value16 = HALMAC_REG_R16(offset);
	value16 &= ~((1 << gpio_id) | (1 << gpio_id << 8));
	value16 |= (in_out << gpio_id);
	HALMAC_REG_W16(offset, value16);

	return HALMAC_RET_SUCCESS;
}

/**
 * pinmux_gpio_output_88xx() -control gpio output high/low
 * @adapter : the adapter of halmac
 * @gpio_id : gpio0~15(0~15)
 * @high : high(1), low(0)
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pinmux_gpio_output_88xx(struct halmac_adapter *adapter, u8 gpio_id, u8 high)
{
	u8 value8;
	u8 hi_low;
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (gpio_id <= 7)
		offset = REG_GPIO_PIN_CTRL + 1;
	else if (gpio_id >= 8 && gpio_id <= 15)
		offset = REG_GPIO_EXT_CTRL + 1;
	else
		return HALMAC_RET_WRONG_GPIO;

	hi_low = (high == 0) ? 0 : 1;
	gpio_id &= (8 - 1);

	value8 = HALMAC_REG_R8(offset);
	value8 &= ~(1 << gpio_id);
	value8 |= (hi_low << gpio_id);
	HALMAC_REG_W8(offset, value8);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pinmux_status_88xx() -get current gpio status(high/low)
 * @adapter : the adapter of halmac
 * @pin_id : 0~15(0~15)
 * @phigh : high(1), low(0)
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pinmux_pin_status_88xx(struct halmac_adapter *adapter, u8 pin_id, u8 *high)
{
	u8 value8;
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (pin_id <= 7)
		offset = REG_GPIO_PIN_CTRL;
	else if (pin_id >= 8 && pin_id <= 15)
		offset = REG_GPIO_EXT_CTRL;
	else
		return HALMAC_RET_WRONG_GPIO;

	pin_id &= (8 - 1);

	value8 = HALMAC_REG_R8(offset);
	*high = (value8 & (1 << pin_id)) >> pin_id;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
pinmux_parser_88xx(struct halmac_adapter *adapter,
		   const struct halmac_gpio_pimux_list *list, u32 size,
		   u32 gpio_id, u32 *cur_func)
{
	u32 i;
	u8 value8;
	const struct halmac_gpio_pimux_list *cur_list = list;
	enum halmac_gpio_cfg_state *state;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	state = &adapter->halmac_state.gpio_cfg_state;

	if (*state == HALMAC_GPIO_CFG_STATE_BUSY)
		return HALMAC_RET_BUSY_STATE;

	*state = HALMAC_GPIO_CFG_STATE_BUSY;

	for (i = 0; i < size; i++) {
		if (gpio_id != cur_list->id) {
			PLTFM_MSG_ERR("[ERR]offset:%X, value:%X, func:%X\n",
				      cur_list->offset, cur_list->value,
				      cur_list->func);
			PLTFM_MSG_ERR("[ERR]id1 : %X, id2 : %X\n",
				      gpio_id, cur_list->id);
			*state = HALMAC_GPIO_CFG_STATE_IDLE;
			return HALMAC_RET_GET_PINMUX_ERR;
		}
		value8 = HALMAC_REG_R8(cur_list->offset);
		value8 &= cur_list->msk;
		if (value8 == cur_list->value) {
			*cur_func = cur_list->func;
			break;
		}
		cur_list++;
	}

	*state = HALMAC_GPIO_CFG_STATE_IDLE;

	if (i == size)
		return HALMAC_RET_GET_PINMUX_ERR;

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_88XX_SUPPORT */

