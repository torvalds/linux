/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _HALMAC_FLASH_88XX_H_
#define _HALMAC_FLASH_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
download_flash_88xx(struct halmac_adapter *adapter, u8 *fw_bin, u32 size,
		    u32 rom_addr);

enum halmac_ret_status
read_flash_88xx(struct halmac_adapter *adapter, u32 addr, u32 length, u8 *data);

enum halmac_ret_status
erase_flash_88xx(struct halmac_adapter *adapter, u8 erase_cmd, u32 addr);

enum halmac_ret_status
check_flash_88xx(struct halmac_adapter *adapter, u8 *fw_bin, u32 size,
		 u32 addr);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_FLASH_88XX_H_ */
