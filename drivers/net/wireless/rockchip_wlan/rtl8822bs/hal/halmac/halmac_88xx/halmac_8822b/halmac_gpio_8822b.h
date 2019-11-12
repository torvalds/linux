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

#ifndef _HALMAC_GPIO_8822B_H_
#define _HALMAC_GPIO_8822B_H_

#include "../../halmac_api.h"
#include "../../halmac_gpio_cmd.h"

#if HALMAC_8822B_SUPPORT

enum halmac_ret_status
pinmux_get_func_8822b(struct halmac_adapter *adapter,
		      enum halmac_gpio_func gpio_func, u8 *enable);

enum halmac_ret_status
pinmux_set_func_8822b(struct halmac_adapter *adapter,
		      enum halmac_gpio_func gpio_func);

enum halmac_ret_status
pinmux_free_func_8822b(struct halmac_adapter *adapter,
		       enum halmac_gpio_func gpio_func);

#endif /* HALMAC_8822B_SUPPORT */

#endif/* _HALMAC_GPIO_8822B_H_ */
