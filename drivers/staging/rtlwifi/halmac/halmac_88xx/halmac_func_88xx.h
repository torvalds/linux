/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_FUNC_88XX_H_
#define _HALMAC_FUNC_88XX_H_

#include "../halmac_type.h"

void halmac_init_offload_feature_state_machine_88xx(
	struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_send_h2c_pkt_88xx(struct halmac_adapter *halmac_adapter, u8 *hal_buff,
			 u32 size, bool ack);

enum halmac_ret_status
halmac_download_rsvd_page_88xx(struct halmac_adapter *halmac_adapter,
			       u8 *hal_buf, u32 size);

enum halmac_ret_status
halmac_set_h2c_header_88xx(struct halmac_adapter *halmac_adapter,
			   u8 *hal_h2c_hdr, u16 *seq, bool ack);

enum halmac_ret_status halmac_set_fw_offload_h2c_header_88xx(
	struct halmac_adapter *halmac_adapter, u8 *hal_h2c_hdr,
	struct halmac_h2c_header_info *h2c_header_info, u16 *seq_num);

enum halmac_ret_status
halmac_dump_efuse_88xx(struct halmac_adapter *halmac_adapter,
		       enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
halmac_func_read_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			    u32 size, u8 *efuse_map);

enum halmac_ret_status
halmac_func_write_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			     u8 value);

enum halmac_ret_status
halmac_func_switch_efuse_bank_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_efuse_bank efuse_bank);

enum halmac_ret_status
halmac_read_logical_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *map);

enum halmac_ret_status
halmac_func_write_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
				     u32 offset, u8 value);

enum halmac_ret_status
halmac_func_pg_efuse_by_map_88xx(struct halmac_adapter *halmac_adapter,
				 struct halmac_pg_efuse_info *pg_efuse_info,
				 enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
halmac_eeprom_parser_88xx(struct halmac_adapter *halmac_adapter,
			  u8 *physical_efuse_map, u8 *logical_efuse_map);

enum halmac_ret_status
halmac_read_hw_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			  u32 size, u8 *efuse_map);

enum halmac_ret_status
halmac_dlfw_to_mem_88xx(struct halmac_adapter *halmac_adapter, u8 *ram_code,
			u32 dest, u32 code_size);

enum halmac_ret_status
halmac_send_fwpkt_88xx(struct halmac_adapter *halmac_adapter, u8 *ram_code,
		       u32 code_size);

enum halmac_ret_status
halmac_iddma_dlfw_88xx(struct halmac_adapter *halmac_adapter, u32 source,
		       u32 dest, u32 length, u8 first);

enum halmac_ret_status
halmac_check_fw_chksum_88xx(struct halmac_adapter *halmac_adapter,
			    u32 memory_address);

enum halmac_ret_status
halmac_dlfw_end_flow_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_free_dl_fw_end_flow_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_pwr_seq_parser_88xx(struct halmac_adapter *halmac_adapter, u8 cut,
			   u8 fab, u8 intf,
			   struct halmac_wl_pwr_cfg_ **pp_pwr_seq_cfg

			   );

enum halmac_ret_status
halmac_get_h2c_buff_free_space_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_send_h2c_set_pwr_mode_88xx(struct halmac_adapter *halmac_adapter,
				  struct halmac_fwlps_option *hal_fw_lps_opt);

enum halmac_ret_status
halmac_func_send_original_h2c_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *original_h2c, u16 *seq, u8 ack);

enum halmac_ret_status
halmac_media_status_rpt_88xx(struct halmac_adapter *halmac_adapter, u8 op_mode,
			     u8 mac_id_ind, u8 mac_id, u8 mac_id_end);

enum halmac_ret_status halmac_send_h2c_update_datapack_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_data_type halmac_data_type,
	struct halmac_phy_parameter_info *para_info);

enum halmac_ret_status
halmac_send_h2c_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
				  enum halmac_data_type halmac_data_type);

enum halmac_ret_status
halmac_send_bt_coex_cmd_88xx(struct halmac_adapter *halmac_adapter, u8 *bt_buf,
			     u32 bt_size, u8 ack);

enum halmac_ret_status
halmac_func_ctrl_ch_switch_88xx(struct halmac_adapter *halmac_adapter,
				struct halmac_ch_switch_option *cs_option);

enum halmac_ret_status
halmac_func_send_general_info_88xx(struct halmac_adapter *halmac_adapter,
				   struct halmac_general_info *general_info);

enum halmac_ret_status
halmac_send_h2c_ps_tuning_para_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_parse_c2h_packet_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *halmac_buf, u32 halmac_size);

enum halmac_ret_status
halmac_send_h2c_update_packet_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_packet_id pkt_id, u8 *pkt,
				   u32 pkt_size);

enum halmac_ret_status
halmac_send_h2c_phy_parameter_88xx(struct halmac_adapter *halmac_adapter,
				   struct halmac_phy_parameter_info *para_info,
				   bool full_fifo);

enum halmac_ret_status
halmac_dump_physical_efuse_fw_88xx(struct halmac_adapter *halmac_adapter,
				   u32 offset, u32 size, u8 *efuse_map);

enum halmac_ret_status halmac_send_h2c_update_bcn_parse_info_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_bcn_ie_info *bcn_ie_info);

enum halmac_ret_status
halmac_convert_to_sdio_bus_offset_88xx(struct halmac_adapter *halmac_adapter,
				       u32 *halmac_offset);

enum halmac_ret_status
halmac_update_sdio_free_page_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_update_oqt_free_space_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_efuse_cmd_construct_state
halmac_query_efuse_curr_state_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_transition_efuse_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_efuse_cmd_construct_state dest_state);

enum halmac_cfg_para_cmd_construct_state
halmac_query_cfg_para_curr_state_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_transition_cfg_para_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cfg_para_cmd_construct_state dest_state);

enum halmac_scan_cmd_construct_state
halmac_query_scan_curr_state_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_transition_scan_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_scan_cmd_construct_state dest_state);

enum halmac_ret_status halmac_query_cfg_para_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status halmac_query_dump_physical_efuse_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status halmac_query_dump_logical_efuse_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status halmac_query_channel_switch_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status halmac_query_update_packet_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status
halmac_query_iqk_status_88xx(struct halmac_adapter *halmac_adapter,
			     enum halmac_cmd_process_status *process_status,
			     u8 *data, u32 *size);

enum halmac_ret_status halmac_query_power_tracking_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size);

enum halmac_ret_status
halmac_query_psd_status_88xx(struct halmac_adapter *halmac_adapter,
			     enum halmac_cmd_process_status *process_status,
			     u8 *data, u32 *size);

enum halmac_ret_status
halmac_verify_io_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_verify_send_rsvd_page_88xx(struct halmac_adapter *halmac_adapter);

void halmac_power_save_cb_88xx(void *cb_data);

enum halmac_ret_status
halmac_buffer_read_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			u32 size, enum hal_fifo_sel halmac_fifo_sel,
			u8 *fifo_map);

void halmac_restore_mac_register_88xx(struct halmac_adapter *halmac_adapter,
				      struct halmac_restore_info *restore_info,
				      u32 restore_num);

void halmac_api_record_id_88xx(struct halmac_adapter *halmac_adapter,
			       enum halmac_api_id api_id);

enum halmac_ret_status
halmac_set_usb_mode_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_usb_mode usb_mode);

void halmac_enable_bb_rf_88xx(struct halmac_adapter *halmac_adapter, u8 enable);

void halmac_config_sdio_tx_page_threshold_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_tx_page_threshold_info *threshold_info);

enum halmac_ret_status
halmac_rqpn_parser_88xx(struct halmac_adapter *halmac_adapter,
			enum halmac_trx_mode halmac_trx_mode,
			struct halmac_rqpn_ *pwr_seq_cfg);

enum halmac_ret_status
halmac_check_oqt_88xx(struct halmac_adapter *halmac_adapter, u32 tx_agg_num,
		      u8 *halmac_buf);

enum halmac_ret_status
halmac_pg_num_parser_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_trx_mode halmac_trx_mode,
			  struct halmac_pg_num_ *pg_num_table);

enum halmac_ret_status
halmac_parse_intf_phy_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_intf_phy_para_ *intf_phy_para,
			   enum halmac_intf_phy_platform platform,
			   enum hal_intf_phy intf_phy);

enum halmac_ret_status
halmac_dbi_write32_88xx(struct halmac_adapter *halmac_adapter, u16 addr,
			u32 data);

u32 halmac_dbi_read32_88xx(struct halmac_adapter *halmac_adapter, u16 addr);

enum halmac_ret_status
halmac_dbi_write8_88xx(struct halmac_adapter *halmac_adapter, u16 addr,
		       u8 data);

u8 halmac_dbi_read8_88xx(struct halmac_adapter *halmac_adapter, u16 addr);

u16 halmac_mdio_read_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			  u8 speed

			  );

enum halmac_ret_status
halmac_mdio_write_88xx(struct halmac_adapter *halmac_adapter, u8 addr, u16 data,
		       u8 speed);

void halmac_config_ampdu_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_ampdu_config *ampdu_config);

enum halmac_ret_status
halmac_usbphy_write_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			 u16 data, u8 speed);

u16 halmac_usbphy_read_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			    u8 speed);
#endif /* _HALMAC_FUNC_88XX_H_ */
