/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _HALMAC_EFUSE_88XX_H_
#define _HALMAC_EFUSE_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
dump_efuse_map_88xx(struct halmac_adapter *adapter,
		    enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
eeprom_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map, u8 *log_map);

enum halmac_ret_status
eeprom_mask_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map,
			u8 *log_mask);

enum halmac_ret_status
dump_efuse_map_bt_88xx(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank, u32 size, u8 *map);

enum halmac_ret_status
write_efuse_bt_88xx(struct halmac_adapter *adapter, u32 offset, u8 value,
		    enum halmac_efuse_bank bank);

enum halmac_ret_status
read_efuse_bt_88xx(struct halmac_adapter *adapter, u32 offset, u8 *value,
		   enum halmac_efuse_bank bank);

enum halmac_ret_status
cfg_efuse_auto_check_88xx(struct halmac_adapter *adapter, u8 enable);

enum halmac_ret_status
get_efuse_available_size_88xx(struct halmac_adapter *adapter, u32 *size);

enum halmac_ret_status
get_efuse_size_88xx(struct halmac_adapter *adapter, u32 *size);

enum halmac_ret_status
get_log_efuse_size_88xx(struct halmac_adapter *adapter, u32 *size);

enum halmac_ret_status
dump_log_efuse_map_88xx(struct halmac_adapter *adapter,
			enum halmac_efuse_read_cfg cfg);
enum halmac_ret_status
dump_log_efuse_mask_88xx(struct halmac_adapter *adapter,
			 enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
read_logical_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 *value);

enum halmac_ret_status
write_log_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value);

enum halmac_ret_status
write_log_efuse_word_88xx(struct halmac_adapter *adapter, u32 offset,
			  u16 value);

enum halmac_ret_status
pg_efuse_by_map_88xx(struct halmac_adapter *adapter,
		     struct halmac_pg_efuse_info *info,
		     enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
mask_log_efuse_88xx(struct halmac_adapter *adapter,
		    struct halmac_pg_efuse_info *info);

enum halmac_ret_status
read_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size, u8 *map);

enum halmac_ret_status
write_hw_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value);

enum halmac_ret_status
switch_efuse_bank_88xx(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank);

enum halmac_ret_status
get_efuse_data_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
cnv_efuse_state_88xx(struct halmac_adapter *adapter,
		     enum halmac_cmd_construct_state dest_state);

enum halmac_ret_status
get_dump_phy_efuse_status_88xx(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size);

enum halmac_ret_status
get_dump_log_efuse_status_88xx(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size);

enum halmac_ret_status
get_dump_log_efuse_mask_status_88xx(struct halmac_adapter *adapter,
				    enum halmac_cmd_process_status *proc_status,
				    u8 *data, u32 *size);

enum halmac_ret_status
get_h2c_ack_phy_efuse_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

u32
get_rsvd_efuse_size_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
write_wifi_phy_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value);

enum halmac_ret_status
read_wifi_phy_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
			 u8 *value);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_EFUSE_88XX_H_ */
