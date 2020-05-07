/******************************************************************************
 *
 * Copyright(c) 2017 - 2019 Realtek Corporation. All rights reserved.
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

#ifndef _HALMAC_COMMON_8821C_H_
#define _HALMAC_COMMON_8821C_H_

#include "../../halmac_api.h"

#if HALMAC_8821C_SUPPORT

enum halmac_ret_status
get_hw_value_8821c(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		   void *value);

enum halmac_ret_status
set_hw_value_8821c(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		   void *value);

enum halmac_ret_status
fill_txdesc_check_sum_8821c(struct halmac_adapter *adapter, u8 *txdesc);

#endif/* HALMAC_8821C_SUPPORT */

#endif/* _HALMAC_COMMON_8821C_H_ */
