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

#ifndef _HALMAC_GPIO_88XX_H_
#define _HALMAC_GPIO_88XX_H_

#include "../halmac_api.h"
#include "../halmac_gpio_cmd.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
pinmux_wl_led_mode_88xx(struct halmac_adapter *adapter,
			enum halmac_wlled_mode mode);

void
pinmux_wl_led_sw_ctrl_88xx(struct halmac_adapter *adapter, u8 on);

void
pinmux_sdio_int_polarity_88xx(struct halmac_adapter *adapter, u8 low_active);

enum halmac_ret_status
pinmux_gpio_mode_88xx(struct halmac_adapter *adapter, u8 gpio_id, u8 output);

enum halmac_ret_status
pinmux_gpio_output_88xx(struct halmac_adapter *adapter, u8 gpio_id, u8 high);

enum halmac_ret_status
pinmux_pin_status_88xx(struct halmac_adapter *adapter, u8 pin_id, u8 *high);

enum halmac_ret_status
pinmux_parser_88xx(struct halmac_adapter *adapter,
		   const struct halmac_gpio_pimux_list *list, u32 size,
		   u32 gpio_id, u32 *cur_func);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_GPIO_88XX_H_ */
