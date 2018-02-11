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
#ifndef _HALMAC_API_88XX_H_
#define _HALMAC_API_88XX_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

void halmac_init_state_machine_88xx(struct halmac_adapter *halmac_adapter);

void halmac_init_adapter_para_88xx(struct halmac_adapter *halmac_adapter);

void halmac_init_adapter_dynamic_para_88xx(
	struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_mount_api_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_download_firmware_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *hamacl_fw, u32 halmac_fw_size);

enum halmac_ret_status
halmac_free_download_firmware_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_dlfw_mem dlfw_mem, u8 *hamacl_fw,
				   u32 halmac_fw_size);

enum halmac_ret_status
halmac_get_fw_version_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_fw_version *fw_version);

enum halmac_ret_status
halmac_cfg_mac_addr_88xx(struct halmac_adapter *halmac_adapter, u8 halmac_port,
			 union halmac_wlan_addr *hal_address);

enum halmac_ret_status
halmac_cfg_bssid_88xx(struct halmac_adapter *halmac_adapter, u8 halmac_port,
		      union halmac_wlan_addr *hal_address);

enum halmac_ret_status
halmac_cfg_multicast_addr_88xx(struct halmac_adapter *halmac_adapter,
			       union halmac_wlan_addr *hal_address);

enum halmac_ret_status
halmac_pre_init_system_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_init_system_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_rxagg_cfg halmac_rxagg_cfg);

enum halmac_ret_status
halmac_init_edca_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_operation_mode_88xx(struct halmac_adapter *halmac_adapter,
			       enum halmac_wireless_mode wireless_mode);

enum halmac_ret_status
halmac_cfg_ch_bw_88xx(struct halmac_adapter *halmac_adapter, u8 channel,
		      enum halmac_pri_ch_idx pri_ch_idx, enum halmac_bw bw);

enum halmac_ret_status halmac_cfg_ch_88xx(struct halmac_adapter *halmac_adapter,
					  u8 channel);

enum halmac_ret_status
halmac_cfg_pri_ch_idx_88xx(struct halmac_adapter *halmac_adapter,
			   enum halmac_pri_ch_idx pri_ch_idx);

enum halmac_ret_status halmac_cfg_bw_88xx(struct halmac_adapter *halmac_adapter,
					  enum halmac_bw bw);

enum halmac_ret_status
halmac_init_wmac_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_init_mac_cfg_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_trx_mode mode);

enum halmac_ret_status
halmac_dump_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
			   enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
halmac_dump_efuse_map_bt_88xx(struct halmac_adapter *halmac_adapter,
			      enum halmac_efuse_bank halmac_efuse_bank,
			      u32 bt_efuse_map_size, u8 *bt_efuse_map);

enum halmac_ret_status
halmac_write_efuse_bt_88xx(struct halmac_adapter *halmac_adapter,
			   u32 halmac_offset, u8 halmac_value,
			   enum halmac_efuse_bank halmac_efuse_bank);

enum halmac_ret_status
halmac_pg_efuse_by_map_88xx(struct halmac_adapter *halmac_adapter,
			    struct halmac_pg_efuse_info *pg_efuse_info,
			    enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
halmac_get_efuse_size_88xx(struct halmac_adapter *halmac_adapter,
			   u32 *halmac_size);

enum halmac_ret_status
halmac_get_efuse_available_size_88xx(struct halmac_adapter *halmac_adapter,
				     u32 *halmac_size);

enum halmac_ret_status
halmac_get_c2h_info_88xx(struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
			 u32 halmac_size);

enum halmac_ret_status
halmac_get_logical_efuse_size_88xx(struct halmac_adapter *halmac_adapter,
				   u32 *halmac_size);

enum halmac_ret_status
halmac_dump_logical_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_efuse_read_cfg cfg);

enum halmac_ret_status
halmac_write_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset, u8 halmac_value);

enum halmac_ret_status
halmac_read_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset, u8 *value);

enum halmac_ret_status
halmac_cfg_fwlps_option_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_fwlps_option *lps_option);

enum halmac_ret_status
halmac_cfg_fwips_option_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_fwips_option *ips_option);

enum halmac_ret_status
halmac_enter_wowlan_88xx(struct halmac_adapter *halmac_adapter,
			 struct halmac_wowlan_option *wowlan_option);

enum halmac_ret_status
halmac_leave_wowlan_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_enter_ps_88xx(struct halmac_adapter *halmac_adapter,
		     enum halmac_ps_state ps_state);

enum halmac_ret_status
halmac_leave_ps_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_h2c_lb_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_debug_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_parameter_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_phy_parameter_info *para_info,
			  u8 full_fifo);

enum halmac_ret_status
halmac_update_packet_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_packet_id pkt_id, u8 *pkt, u32 pkt_size);

enum halmac_ret_status
halmac_bcn_ie_filter_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_bcn_ie_info *bcn_ie_info);

enum halmac_ret_status
halmac_send_original_h2c_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *original_h2c, u16 *seq, u8 ack);

enum halmac_ret_status
halmac_update_datapack_88xx(struct halmac_adapter *halmac_adapter,
			    enum halmac_data_type halmac_data_type,
			    struct halmac_phy_parameter_info *para_info);

enum halmac_ret_status
halmac_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_data_type halmac_data_type);

enum halmac_ret_status
halmac_cfg_drv_info_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_drv_info halmac_drv_info);

enum halmac_ret_status
halmac_send_bt_coex_88xx(struct halmac_adapter *halmac_adapter, u8 *bt_buf,
			 u32 bt_size, u8 ack);

enum halmac_ret_status
halmac_verify_platform_api_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_timer_2s_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_fill_txdesc_check_sum_88xx(struct halmac_adapter *halmac_adapter,
				  u8 *cur_desc);

enum halmac_ret_status
halmac_dump_fifo_88xx(struct halmac_adapter *halmac_adapter,
		      enum hal_fifo_sel halmac_fifo_sel, u32 halmac_start_addr,
		      u32 halmac_fifo_dump_size, u8 *fifo_map);

u32 halmac_get_fifo_size_88xx(struct halmac_adapter *halmac_adapter,
			      enum hal_fifo_sel halmac_fifo_sel);

enum halmac_ret_status
halmac_cfg_txbf_88xx(struct halmac_adapter *halmac_adapter, u8 userid,
		     enum halmac_bw bw, u8 txbf_en);

enum halmac_ret_status
halmac_cfg_mumimo_88xx(struct halmac_adapter *halmac_adapter,
		       struct halmac_cfg_mumimo_para *cfgmu);

enum halmac_ret_status
halmac_cfg_sounding_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_snd_role role,
			 enum halmac_data_rate datarate);

enum halmac_ret_status
halmac_del_sounding_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_snd_role role);

enum halmac_ret_status
halmac_su_bfee_entry_init_88xx(struct halmac_adapter *halmac_adapter, u8 userid,
			       u16 paid);

enum halmac_ret_status
halmac_su_bfer_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_su_bfer_init_para *su_bfer_init);

enum halmac_ret_status
halmac_mu_bfee_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_mu_bfee_init_para *mu_bfee_init);

enum halmac_ret_status
halmac_mu_bfer_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_mu_bfer_init_para *mu_bfer_init);

enum halmac_ret_status
halmac_su_bfee_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid);

enum halmac_ret_status
halmac_su_bfer_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid);

enum halmac_ret_status
halmac_mu_bfee_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid);

enum halmac_ret_status
halmac_mu_bfer_entry_del_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_add_ch_info_88xx(struct halmac_adapter *halmac_adapter,
			struct halmac_ch_info *ch_info);

enum halmac_ret_status
halmac_add_extra_ch_info_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_ch_extra_info *ch_extra_info);

enum halmac_ret_status
halmac_ctrl_ch_switch_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_ch_switch_option *cs_option);

enum halmac_ret_status halmac_p2pps_88xx(struct halmac_adapter *halmac_adapter,
					 struct halmac_p2pps *p2p_ps);

enum halmac_ret_status
halmac_func_p2pps_88xx(struct halmac_adapter *halmac_adapter,
		       struct halmac_p2pps *p2p_ps);

enum halmac_ret_status
halmac_clear_ch_info_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_send_general_info_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_general_info *general_info);

enum halmac_ret_status
halmac_start_iqk_88xx(struct halmac_adapter *halmac_adapter,
		      struct halmac_iqk_para_ *iqk_para);

enum halmac_ret_status halmac_ctrl_pwr_tracking_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_pwr_tracking_option *pwr_tracking_opt);

enum halmac_ret_status
halmac_query_status_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_feature_id feature_id,
			 enum halmac_cmd_process_status *process_status,
			 u8 *data, u32 *size);

enum halmac_ret_status
halmac_reset_feature_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_feature_id feature_id);

enum halmac_ret_status
halmac_check_fw_status_88xx(struct halmac_adapter *halmac_adapter,
			    bool *fw_status);

enum halmac_ret_status
halmac_dump_fw_dmem_88xx(struct halmac_adapter *halmac_adapter, u8 *dmem,
			 u32 *size);

enum halmac_ret_status
halmac_cfg_max_dl_size_88xx(struct halmac_adapter *halmac_adapter, u32 size);

enum halmac_ret_status halmac_psd_88xx(struct halmac_adapter *halmac_adapter,
				       u16 start_psd, u16 end_psd);

enum halmac_ret_status
halmac_cfg_la_mode_88xx(struct halmac_adapter *halmac_adapter,
			enum halmac_la_mode la_mode);

enum halmac_ret_status halmac_cfg_rx_fifo_expanding_mode_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_rx_fifo_expanding_mode rx_fifo_expanding_mode);

enum halmac_ret_status
halmac_config_security_88xx(struct halmac_adapter *halmac_adapter,
			    struct halmac_security_setting *sec_setting);

u8 halmac_get_used_cam_entry_num_88xx(struct halmac_adapter *halmac_adapter,
				      enum hal_security_type sec_type);

enum halmac_ret_status
halmac_write_cam_88xx(struct halmac_adapter *halmac_adapter, u32 entry_index,
		      struct halmac_cam_entry_info *cam_entry_info);

enum halmac_ret_status
halmac_read_cam_entry_88xx(struct halmac_adapter *halmac_adapter,
			   u32 entry_index,
			   struct halmac_cam_entry_format *content);

enum halmac_ret_status
halmac_clear_cam_entry_88xx(struct halmac_adapter *halmac_adapter,
			    u32 entry_index);

enum halmac_ret_status
halmac_get_hw_value_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_hw_id hw_id, void *pvalue);

enum halmac_ret_status
halmac_set_hw_value_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_hw_id hw_id, void *pvalue);

enum halmac_ret_status
halmac_cfg_drv_rsvd_pg_num_88xx(struct halmac_adapter *halmac_adapter,
				enum halmac_drv_rsvd_pg_num pg_num);

enum halmac_ret_status
halmac_get_chip_version_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_ver *version);

enum halmac_ret_status
halmac_chk_txdesc_88xx(struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		       u32 halmac_size);

enum halmac_ret_status
halmac_dl_drv_rsvd_page_88xx(struct halmac_adapter *halmac_adapter,
			     u8 pg_offset, u8 *halmac_buf, u32 halmac_size);

enum halmac_ret_status
halmac_cfg_csi_rate_88xx(struct halmac_adapter *halmac_adapter, u8 rssi,
			 u8 current_rate, u8 fixrate_en, u8 *new_rate);

enum halmac_ret_status halmac_sdio_cmd53_4byte_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte_mode);

enum halmac_ret_status
halmac_txfifo_is_empty_88xx(struct halmac_adapter *halmac_adapter, u32 chk_num);

#endif /* _HALMAC_API_H_ */
