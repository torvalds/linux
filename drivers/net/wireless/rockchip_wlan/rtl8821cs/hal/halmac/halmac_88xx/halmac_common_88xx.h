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

#ifndef _HALMAC_COMMON_88XX_H_
#define _HALMAC_COMMON_88XX_H_

#include "../halmac_api.h"
#include "../halmac_pwr_seq_cmd.h"
#include "../halmac_gpio_cmd.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
ofld_func_cfg_88xx(struct halmac_adapter *adapter,
		   struct halmac_ofld_func_info *info);

enum halmac_ret_status
dl_drv_rsvd_page_88xx(struct halmac_adapter *adapter, u8 pg_offset, u8 *buf,
		      u32 size);

enum halmac_ret_status
dl_rsvd_page_88xx(struct halmac_adapter *adapter, u16 pg_addr, u8 *buf,
		  u32 size);

enum halmac_ret_status
get_hw_value_88xx(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		  void *value);

enum halmac_ret_status
set_hw_value_88xx(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		  void *value);

enum halmac_ret_status
get_watcher_88xx(struct halmac_adapter *adapter, enum halmac_watcher_sel sel,
		 void *value);

enum halmac_ret_status
set_h2c_pkt_hdr_88xx(struct halmac_adapter *adapter, u8 *hdr,
		     struct halmac_h2c_header_info *info, u16 *seq_num);

enum halmac_ret_status
send_h2c_pkt_88xx(struct halmac_adapter *adapter, u8 *pkt);

enum halmac_ret_status
get_h2c_buf_free_space_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
get_c2h_info_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
mac_debug_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
cfg_parameter_88xx(struct halmac_adapter *adapter,
		   struct halmac_phy_parameter_info *info, u8 full_fifo);

enum halmac_ret_status
update_packet_88xx(struct halmac_adapter *adapter, enum halmac_packet_id pkt_id,
		   u8 *pkt, u32 size);

enum halmac_ret_status
send_scan_packet_88xx(struct halmac_adapter *adapter, u8 index,
		      u8 *pkt, u32 size);

enum halmac_ret_status
drop_scan_packet_88xx(struct halmac_adapter *adapter,
		      struct halmac_drop_pkt_option *option);

enum halmac_ret_status
bcn_ie_filter_88xx(struct halmac_adapter *adapter,
		   struct halmac_bcn_ie_info *info);

enum halmac_ret_status
update_datapack_88xx(struct halmac_adapter *adapter,
		     enum halmac_data_type data_type,
		     struct halmac_phy_parameter_info *info);

enum halmac_ret_status
run_datapack_88xx(struct halmac_adapter *adapter,
		  enum halmac_data_type data_type);

enum halmac_ret_status
send_bt_coex_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size, u8 ack);

enum halmac_ret_status
dump_fifo_88xx(struct halmac_adapter *adapter, enum hal_fifo_sel sel,
	       u32 start_addr, u32 size, u8 *data);

u32
get_fifo_size_88xx(struct halmac_adapter *adapter, enum hal_fifo_sel sel);

enum halmac_ret_status
set_h2c_header_88xx(struct halmac_adapter *adapter, u8 *hdr, u16 *seq, u8 ack);

enum halmac_ret_status
add_ch_info_88xx(struct halmac_adapter *adapter, struct halmac_ch_info *info);

enum halmac_ret_status
add_extra_ch_info_88xx(struct halmac_adapter *adapter,
		       struct halmac_ch_extra_info *info);

enum halmac_ret_status
ctrl_ch_switch_88xx(struct halmac_adapter *adapter,
		    struct halmac_ch_switch_option *opt);

enum halmac_ret_status
clear_ch_info_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
chk_txdesc_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
get_version_88xx(struct halmac_adapter *adapter, struct halmac_ver *ver);

enum halmac_ret_status
p2pps_88xx(struct halmac_adapter *adapter, struct halmac_p2pps *info);

enum halmac_ret_status
query_status_88xx(struct halmac_adapter *adapter,
		  enum halmac_feature_id feature_id,
		  enum halmac_cmd_process_status *proc_status, u8 *data,
		  u32 *size);

enum halmac_ret_status
cfg_drv_rsvd_pg_num_88xx(struct halmac_adapter *adapter,
			 enum halmac_drv_rsvd_pg_num pg_num);

enum halmac_ret_status
h2c_lb_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
pwr_seq_parser_88xx(struct halmac_adapter *adapter,
		    struct halmac_wlan_pwr_cfg **cmd_seq);

enum halmac_ret_status
parse_intf_phy_88xx(struct halmac_adapter *adapter,
		    struct halmac_intf_phy_para *param,
		    enum halmac_intf_phy_platform pltfm,
		    enum hal_intf_phy intf_phy);

enum halmac_ret_status
txfifo_is_empty_88xx(struct halmac_adapter *adapter, u32 chk_num);

u8*
smart_malloc_88xx(struct halmac_adapter *adapter, u32 size, u32 *new_size);

enum halmac_ret_status
ltecoex_reg_read_88xx(struct halmac_adapter *adapter, u16 offset, u32 *value);

enum halmac_ret_status
ltecoex_reg_write_88xx(struct halmac_adapter *adapter, u16 offset, u32 value);

#endif/* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_COMMON_88XX_H_ */
