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
#include "halmac_88xx_cfg.h"

/**
 * halmac_init_adapter_para_88xx() - int halmac adapter
 * @halmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : void
 */
void halmac_init_adapter_para_88xx(struct halmac_adapter *halmac_adapter)
{
	halmac_adapter->api_record.array_wptr = 0;
	halmac_adapter->hal_adapter_backup = halmac_adapter;
	halmac_adapter->hal_efuse_map = (u8 *)NULL;
	halmac_adapter->hal_efuse_map_valid = false;
	halmac_adapter->efuse_end = 0;
	halmac_adapter->hal_mac_addr[0].address_l_h.address_low = 0;
	halmac_adapter->hal_mac_addr[0].address_l_h.address_high = 0;
	halmac_adapter->hal_mac_addr[1].address_l_h.address_low = 0;
	halmac_adapter->hal_mac_addr[1].address_l_h.address_high = 0;
	halmac_adapter->hal_bss_addr[0].address_l_h.address_low = 0;
	halmac_adapter->hal_bss_addr[0].address_l_h.address_high = 0;
	halmac_adapter->hal_bss_addr[1].address_l_h.address_low = 0;
	halmac_adapter->hal_bss_addr[1].address_l_h.address_high = 0;

	halmac_adapter->low_clk = false;
	halmac_adapter->max_download_size = HALMAC_FW_MAX_DL_SIZE_88XX;

	/* Init LPS Option */
	halmac_adapter->fwlps_option.mode = 0x01; /*0:Active 1:LPS 2:WMMPS*/
	halmac_adapter->fwlps_option.awake_interval = 1;
	halmac_adapter->fwlps_option.enter_32K = 1;
	halmac_adapter->fwlps_option.clk_request = 0;
	halmac_adapter->fwlps_option.rlbm = 0;
	halmac_adapter->fwlps_option.smart_ps = 0;
	halmac_adapter->fwlps_option.awake_interval = 1;
	halmac_adapter->fwlps_option.all_queue_uapsd = 0;
	halmac_adapter->fwlps_option.pwr_state = 0;
	halmac_adapter->fwlps_option.low_pwr_rx_beacon = 0;
	halmac_adapter->fwlps_option.ant_auto_switch = 0;
	halmac_adapter->fwlps_option.ps_allow_bt_high_priority = 0;
	halmac_adapter->fwlps_option.protect_bcn = 0;
	halmac_adapter->fwlps_option.silence_period = 0;
	halmac_adapter->fwlps_option.fast_bt_connect = 0;
	halmac_adapter->fwlps_option.two_antenna_en = 0;
	halmac_adapter->fwlps_option.adopt_user_setting = 1;
	halmac_adapter->fwlps_option.drv_bcn_early_shift = 0;

	halmac_adapter->config_para_info.cfg_para_buf = NULL;
	halmac_adapter->config_para_info.para_buf_w = NULL;
	halmac_adapter->config_para_info.para_num = 0;
	halmac_adapter->config_para_info.full_fifo_mode = false;
	halmac_adapter->config_para_info.para_buf_size = 0;
	halmac_adapter->config_para_info.avai_para_buf_size = 0;
	halmac_adapter->config_para_info.offset_accumulation = 0;
	halmac_adapter->config_para_info.value_accumulation = 0;
	halmac_adapter->config_para_info.datapack_segment = 0;

	halmac_adapter->ch_sw_info.ch_info_buf = NULL;
	halmac_adapter->ch_sw_info.ch_info_buf_w = NULL;
	halmac_adapter->ch_sw_info.extra_info_en = 0;
	halmac_adapter->ch_sw_info.buf_size = 0;
	halmac_adapter->ch_sw_info.avai_buf_size = 0;
	halmac_adapter->ch_sw_info.total_size = 0;
	halmac_adapter->ch_sw_info.ch_num = 0;

	halmac_adapter->drv_info_size = 0;

	memset(halmac_adapter->api_record.api_array, HALMAC_API_STUFF,
	       sizeof(halmac_adapter->api_record.api_array));

	halmac_adapter->txff_allocation.tx_fifo_pg_num = 0;
	halmac_adapter->txff_allocation.ac_q_pg_num = 0;
	halmac_adapter->txff_allocation.rsvd_pg_bndy = 0;
	halmac_adapter->txff_allocation.rsvd_drv_pg_bndy = 0;
	halmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy = 0;
	halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy = 0;
	halmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy = 0;
	halmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy = 0;
	halmac_adapter->txff_allocation.pub_queue_pg_num = 0;
	halmac_adapter->txff_allocation.high_queue_pg_num = 0;
	halmac_adapter->txff_allocation.low_queue_pg_num = 0;
	halmac_adapter->txff_allocation.normal_queue_pg_num = 0;
	halmac_adapter->txff_allocation.extra_queue_pg_num = 0;

	halmac_adapter->txff_allocation.la_mode = HALMAC_LA_MODE_DISABLE;
	halmac_adapter->txff_allocation.rx_fifo_expanding_mode =
		HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE;

	halmac_init_adapter_dynamic_para_88xx(halmac_adapter);
	halmac_init_state_machine_88xx(halmac_adapter);
}

/**
 * halmac_init_adapter_dynamic_para_88xx() - int halmac adapter
 * @halmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : void
 */
void halmac_init_adapter_dynamic_para_88xx(
	struct halmac_adapter *halmac_adapter)
{
	halmac_adapter->h2c_packet_seq = 0;
	halmac_adapter->h2c_buf_free_space = 0;
	halmac_adapter->gen_info_valid = false;
}

/**
 * halmac_init_state_machine_88xx() - init halmac software state machine
 * @halmac_adapter
 *
 * SD1 internal use.
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : void
 */
void halmac_init_state_machine_88xx(struct halmac_adapter *halmac_adapter)
{
	struct halmac_state *state = &halmac_adapter->halmac_state;

	halmac_init_offload_feature_state_machine_88xx(halmac_adapter);

	state->api_state = HALMAC_API_STATE_INIT;

	state->dlfw_state = HALMAC_DLFW_NONE;
	state->mac_power = HALMAC_MAC_POWER_OFF;
	state->ps_state = HALMAC_PS_STATE_UNDEFINE;
}

/**
 * halmac_mount_api_88xx() - attach functions to function pointer
 * @halmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
halmac_mount_api_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = halmac_adapter->driver_adapter;
	struct halmac_api *halmac_api = (struct halmac_api *)NULL;

	halmac_adapter->halmac_api =
		kzalloc(sizeof(struct halmac_api), GFP_KERNEL);
	if (!halmac_adapter->halmac_api)
		return HALMAC_RET_MALLOC_FAIL;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			HALMAC_SVN_VER_88XX "\n");
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"HALMAC_MAJOR_VER_88XX = %x\n", HALMAC_MAJOR_VER_88XX);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"HALMAC_PROTOTYPE_88XX = %x\n",
			HALMAC_PROTOTYPE_VER_88XX);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"HALMAC_MINOR_VER_88XX = %x\n", HALMAC_MINOR_VER_88XX);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"HALMAC_PATCH_VER_88XX = %x\n", HALMAC_PATCH_VER_88XX);

	/* Mount function pointer */
	halmac_api->halmac_download_firmware = halmac_download_firmware_88xx;
	halmac_api->halmac_free_download_firmware =
		halmac_free_download_firmware_88xx;
	halmac_api->halmac_get_fw_version = halmac_get_fw_version_88xx;
	halmac_api->halmac_cfg_mac_addr = halmac_cfg_mac_addr_88xx;
	halmac_api->halmac_cfg_bssid = halmac_cfg_bssid_88xx;
	halmac_api->halmac_cfg_multicast_addr = halmac_cfg_multicast_addr_88xx;
	halmac_api->halmac_pre_init_system_cfg =
		halmac_pre_init_system_cfg_88xx;
	halmac_api->halmac_init_system_cfg = halmac_init_system_cfg_88xx;
	halmac_api->halmac_init_edca_cfg = halmac_init_edca_cfg_88xx;
	halmac_api->halmac_cfg_operation_mode = halmac_cfg_operation_mode_88xx;
	halmac_api->halmac_cfg_ch_bw = halmac_cfg_ch_bw_88xx;
	halmac_api->halmac_cfg_bw = halmac_cfg_bw_88xx;
	halmac_api->halmac_init_wmac_cfg = halmac_init_wmac_cfg_88xx;
	halmac_api->halmac_init_mac_cfg = halmac_init_mac_cfg_88xx;
	halmac_api->halmac_init_sdio_cfg = halmac_init_sdio_cfg_88xx;
	halmac_api->halmac_init_usb_cfg = halmac_init_usb_cfg_88xx;
	halmac_api->halmac_init_pcie_cfg = halmac_init_pcie_cfg_88xx;
	halmac_api->halmac_deinit_sdio_cfg = halmac_deinit_sdio_cfg_88xx;
	halmac_api->halmac_deinit_usb_cfg = halmac_deinit_usb_cfg_88xx;
	halmac_api->halmac_deinit_pcie_cfg = halmac_deinit_pcie_cfg_88xx;
	halmac_api->halmac_dump_efuse_map = halmac_dump_efuse_map_88xx;
	halmac_api->halmac_dump_efuse_map_bt = halmac_dump_efuse_map_bt_88xx;
	halmac_api->halmac_write_efuse_bt = halmac_write_efuse_bt_88xx;
	halmac_api->halmac_dump_logical_efuse_map =
		halmac_dump_logical_efuse_map_88xx;
	halmac_api->halmac_pg_efuse_by_map = halmac_pg_efuse_by_map_88xx;
	halmac_api->halmac_get_efuse_size = halmac_get_efuse_size_88xx;
	halmac_api->halmac_get_efuse_available_size =
		halmac_get_efuse_available_size_88xx;
	halmac_api->halmac_get_c2h_info = halmac_get_c2h_info_88xx;

	halmac_api->halmac_get_logical_efuse_size =
		halmac_get_logical_efuse_size_88xx;

	halmac_api->halmac_write_logical_efuse =
		halmac_write_logical_efuse_88xx;
	halmac_api->halmac_read_logical_efuse = halmac_read_logical_efuse_88xx;

	halmac_api->halmac_cfg_fwlps_option = halmac_cfg_fwlps_option_88xx;
	halmac_api->halmac_cfg_fwips_option = halmac_cfg_fwips_option_88xx;
	halmac_api->halmac_enter_wowlan = halmac_enter_wowlan_88xx;
	halmac_api->halmac_leave_wowlan = halmac_leave_wowlan_88xx;
	halmac_api->halmac_enter_ps = halmac_enter_ps_88xx;
	halmac_api->halmac_leave_ps = halmac_leave_ps_88xx;
	halmac_api->halmac_h2c_lb = halmac_h2c_lb_88xx;
	halmac_api->halmac_debug = halmac_debug_88xx;
	halmac_api->halmac_cfg_parameter = halmac_cfg_parameter_88xx;
	halmac_api->halmac_update_datapack = halmac_update_datapack_88xx;
	halmac_api->halmac_run_datapack = halmac_run_datapack_88xx;
	halmac_api->halmac_cfg_drv_info = halmac_cfg_drv_info_88xx;
	halmac_api->halmac_send_bt_coex = halmac_send_bt_coex_88xx;
	halmac_api->halmac_verify_platform_api =
		halmac_verify_platform_api_88xx;
	halmac_api->halmac_update_packet = halmac_update_packet_88xx;
	halmac_api->halmac_bcn_ie_filter = halmac_bcn_ie_filter_88xx;
	halmac_api->halmac_cfg_txbf = halmac_cfg_txbf_88xx;
	halmac_api->halmac_cfg_mumimo = halmac_cfg_mumimo_88xx;
	halmac_api->halmac_cfg_sounding = halmac_cfg_sounding_88xx;
	halmac_api->halmac_del_sounding = halmac_del_sounding_88xx;
	halmac_api->halmac_su_bfer_entry_init = halmac_su_bfer_entry_init_88xx;
	halmac_api->halmac_su_bfee_entry_init = halmac_su_bfee_entry_init_88xx;
	halmac_api->halmac_mu_bfer_entry_init = halmac_mu_bfer_entry_init_88xx;
	halmac_api->halmac_mu_bfee_entry_init = halmac_mu_bfee_entry_init_88xx;
	halmac_api->halmac_su_bfer_entry_del = halmac_su_bfer_entry_del_88xx;
	halmac_api->halmac_su_bfee_entry_del = halmac_su_bfee_entry_del_88xx;
	halmac_api->halmac_mu_bfer_entry_del = halmac_mu_bfer_entry_del_88xx;
	halmac_api->halmac_mu_bfee_entry_del = halmac_mu_bfee_entry_del_88xx;

	halmac_api->halmac_add_ch_info = halmac_add_ch_info_88xx;
	halmac_api->halmac_add_extra_ch_info = halmac_add_extra_ch_info_88xx;
	halmac_api->halmac_ctrl_ch_switch = halmac_ctrl_ch_switch_88xx;
	halmac_api->halmac_p2pps = halmac_p2pps_88xx;
	halmac_api->halmac_clear_ch_info = halmac_clear_ch_info_88xx;
	halmac_api->halmac_send_general_info = halmac_send_general_info_88xx;

	halmac_api->halmac_start_iqk = halmac_start_iqk_88xx;
	halmac_api->halmac_ctrl_pwr_tracking = halmac_ctrl_pwr_tracking_88xx;
	halmac_api->halmac_psd = halmac_psd_88xx;
	halmac_api->halmac_cfg_la_mode = halmac_cfg_la_mode_88xx;
	halmac_api->halmac_cfg_rx_fifo_expanding_mode =
		halmac_cfg_rx_fifo_expanding_mode_88xx;

	halmac_api->halmac_config_security = halmac_config_security_88xx;
	halmac_api->halmac_get_used_cam_entry_num =
		halmac_get_used_cam_entry_num_88xx;
	halmac_api->halmac_read_cam_entry = halmac_read_cam_entry_88xx;
	halmac_api->halmac_write_cam = halmac_write_cam_88xx;
	halmac_api->halmac_clear_cam_entry = halmac_clear_cam_entry_88xx;

	halmac_api->halmac_get_hw_value = halmac_get_hw_value_88xx;
	halmac_api->halmac_set_hw_value = halmac_set_hw_value_88xx;

	halmac_api->halmac_cfg_drv_rsvd_pg_num =
		halmac_cfg_drv_rsvd_pg_num_88xx;
	halmac_api->halmac_get_chip_version = halmac_get_chip_version_88xx;

	halmac_api->halmac_query_status = halmac_query_status_88xx;
	halmac_api->halmac_reset_feature = halmac_reset_feature_88xx;
	halmac_api->halmac_check_fw_status = halmac_check_fw_status_88xx;
	halmac_api->halmac_dump_fw_dmem = halmac_dump_fw_dmem_88xx;
	halmac_api->halmac_cfg_max_dl_size = halmac_cfg_max_dl_size_88xx;

	halmac_api->halmac_dump_fifo = halmac_dump_fifo_88xx;
	halmac_api->halmac_get_fifo_size = halmac_get_fifo_size_88xx;

	halmac_api->halmac_chk_txdesc = halmac_chk_txdesc_88xx;
	halmac_api->halmac_dl_drv_rsvd_page = halmac_dl_drv_rsvd_page_88xx;
	halmac_api->halmac_cfg_csi_rate = halmac_cfg_csi_rate_88xx;

	halmac_api->halmac_sdio_cmd53_4byte = halmac_sdio_cmd53_4byte_88xx;
	halmac_api->halmac_txfifo_is_empty = halmac_txfifo_is_empty_88xx;

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		halmac_api->halmac_cfg_rx_aggregation =
			halmac_cfg_rx_aggregation_88xx_sdio;
		halmac_api->halmac_init_interface_cfg =
			halmac_init_sdio_cfg_88xx;
		halmac_api->halmac_deinit_interface_cfg =
			halmac_deinit_sdio_cfg_88xx;
		halmac_api->halmac_reg_read_8 = halmac_reg_read_8_sdio_88xx;
		halmac_api->halmac_reg_write_8 = halmac_reg_write_8_sdio_88xx;
		halmac_api->halmac_reg_read_16 = halmac_reg_read_16_sdio_88xx;
		halmac_api->halmac_reg_write_16 = halmac_reg_write_16_sdio_88xx;
		halmac_api->halmac_reg_read_32 = halmac_reg_read_32_sdio_88xx;
		halmac_api->halmac_reg_write_32 = halmac_reg_write_32_sdio_88xx;
		halmac_api->halmac_reg_read_indirect_32 =
			halmac_reg_read_indirect_32_sdio_88xx;
		halmac_api->halmac_reg_sdio_cmd53_read_n =
			halmac_reg_read_nbyte_sdio_88xx;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		halmac_api->halmac_cfg_rx_aggregation =
			halmac_cfg_rx_aggregation_88xx_usb;
		halmac_api->halmac_init_interface_cfg =
			halmac_init_usb_cfg_88xx;
		halmac_api->halmac_deinit_interface_cfg =
			halmac_deinit_usb_cfg_88xx;
		halmac_api->halmac_reg_read_8 = halmac_reg_read_8_usb_88xx;
		halmac_api->halmac_reg_write_8 = halmac_reg_write_8_usb_88xx;
		halmac_api->halmac_reg_read_16 = halmac_reg_read_16_usb_88xx;
		halmac_api->halmac_reg_write_16 = halmac_reg_write_16_usb_88xx;
		halmac_api->halmac_reg_read_32 = halmac_reg_read_32_usb_88xx;
		halmac_api->halmac_reg_write_32 = halmac_reg_write_32_usb_88xx;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_PCIE) {
		halmac_api->halmac_cfg_rx_aggregation =
			halmac_cfg_rx_aggregation_88xx_pcie;
		halmac_api->halmac_init_interface_cfg =
			halmac_init_pcie_cfg_88xx;
		halmac_api->halmac_deinit_interface_cfg =
			halmac_deinit_pcie_cfg_88xx;
		halmac_api->halmac_reg_read_8 = halmac_reg_read_8_pcie_88xx;
		halmac_api->halmac_reg_write_8 = halmac_reg_write_8_pcie_88xx;
		halmac_api->halmac_reg_read_16 = halmac_reg_read_16_pcie_88xx;
		halmac_api->halmac_reg_write_16 = halmac_reg_write_16_pcie_88xx;
		halmac_api->halmac_reg_read_32 = halmac_reg_read_32_pcie_88xx;
		halmac_api->halmac_reg_write_32 = halmac_reg_write_32_pcie_88xx;
	} else {
		pr_err("Set halmac io function Error!!\n");
	}

	halmac_api->halmac_set_bulkout_num = halmac_set_bulkout_num_88xx;
	halmac_api->halmac_get_sdio_tx_addr = halmac_get_sdio_tx_addr_88xx;
	halmac_api->halmac_get_usb_bulkout_id = halmac_get_usb_bulkout_id_88xx;
	halmac_api->halmac_timer_2s = halmac_timer_2s_88xx;
	halmac_api->halmac_fill_txdesc_checksum =
		halmac_fill_txdesc_check_sum_88xx;

	if (halmac_adapter->chip_id == HALMAC_CHIP_ID_8822B) {
		/*mount 8822b function and data*/
		halmac_mount_api_8822b(halmac_adapter);

	} else if (halmac_adapter->chip_id == HALMAC_CHIP_ID_8821C) {
	} else if (halmac_adapter->chip_id == HALMAC_CHIP_ID_8814B) {
	} else if (halmac_adapter->chip_id == HALMAC_CHIP_ID_8197F) {
	} else {
		pr_err("Chip ID undefine!!\n");
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_download_firmware_88xx() - download Firmware
 * @halmac_adapter : the adapter of halmac
 * @hamacl_fw : firmware bin
 * @halmac_fw_size : firmware size
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_download_firmware_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *hamacl_fw, u32 halmac_fw_size)
{
	u8 value8;
	u8 *file_ptr;
	u32 dest;
	u16 value16;
	u32 restore_index = 0;
	u32 halmac_h2c_ver = 0, fw_h2c_ver = 0;
	u32 iram_pkt_size, dmem_pkt_size, eram_pkt_size = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_restore_info restore_info[DLFW_RESTORE_REG_NUM_88XX];
	u32 temp;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DOWNLOAD_FIRMWARE);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s start!!\n", __func__);

	if (halmac_fw_size > HALMAC_FW_SIZE_MAX_88XX ||
	    halmac_fw_size < HALMAC_FWHDR_SIZE_88XX) {
		pr_err("FW size error!\n");
		return HALMAC_RET_FW_SIZE_ERR;
	}

	fw_h2c_ver = le32_to_cpu(
		*((__le32 *)
		  (hamacl_fw + HALMAC_FWHDR_OFFSET_H2C_FORMAT_VER_88XX)));
	halmac_h2c_ver = H2C_FORMAT_VERSION;
	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
		"halmac h2c/c2h format = %x, fw h2c/c2h format = %x!!\n",
		halmac_h2c_ver, fw_h2c_ver);
	if (fw_h2c_ver != halmac_h2c_ver)
		HALMAC_RT_TRACE(
			driver_adapter, HALMAC_MSG_INIT, DBG_WARNING,
			"[WARN]H2C/C2H version between HALMAC and FW is compatible!!\n");

	halmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_SYS_FUNC_EN + 1);
	value8 = (u8)(value8 & ~(BIT(2)));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_SYS_FUNC_EN + 1,
			   value8); /* Disable CPU reset */

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RSV_CTRL + 1);
	value8 = (u8)(value8 & ~(BIT(0)));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_RSV_CTRL + 1, value8);

	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_TXDMA_PQ_MAP + 1;
	restore_info[restore_index].value =
		HALMAC_REG_READ_8(halmac_adapter, REG_TXDMA_PQ_MAP + 1);
	restore_index++;
	value8 = HALMAC_DMA_MAPPING_HIGH << 6;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_PQ_MAP + 1,
			   value8); /* set HIQ to hi priority */

	/* DLFW only use HIQ, map HIQ to hi priority */
	halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] =
		HALMAC_DMA_MAPPING_HIGH;
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_CR;
	restore_info[restore_index].value =
		HALMAC_REG_READ_8(halmac_adapter, REG_CR);
	restore_index++;
	restore_info[restore_index].length = 4;
	restore_info[restore_index].mac_register = REG_H2CQ_CSR;
	restore_info[restore_index].value = BIT(31);
	restore_index++;
	value8 = BIT_HCI_TXDMA_EN | BIT_TXDMA_EN;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR, value8);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2CQ_CSR, BIT(31));

	/* Config hi priority queue and public priority queue page number
	 * (only for DLFW)
	 */
	restore_info[restore_index].length = 2;
	restore_info[restore_index].mac_register = REG_FIFOPAGE_INFO_1;
	restore_info[restore_index].value =
		HALMAC_REG_READ_16(halmac_adapter, REG_FIFOPAGE_INFO_1);
	restore_index++;
	restore_info[restore_index].length = 4;
	restore_info[restore_index].mac_register = REG_RQPN_CTRL_2;
	restore_info[restore_index].value =
		HALMAC_REG_READ_32(halmac_adapter, REG_RQPN_CTRL_2) | BIT(31);
	restore_index++;
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_INFO_1, 0x200);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_RQPN_CTRL_2,
			    restore_info[restore_index - 1].value);

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		HALMAC_REG_READ_32(halmac_adapter, REG_SDIO_FREE_TXPG);
		HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_TX_CTRL,
				    0x00000000);
	}

	halmac_adapter->fw_version.version = le16_to_cpu(
		*((__le16 *)(hamacl_fw + HALMAC_FWHDR_OFFSET_VERSION_88XX)));
	halmac_adapter->fw_version.sub_version =
		*(hamacl_fw + HALMAC_FWHDR_OFFSET_SUBVERSION_88XX);
	halmac_adapter->fw_version.sub_index =
		*(hamacl_fw + HALMAC_FWHDR_OFFSET_SUBINDEX_88XX);
	halmac_adapter->fw_version.h2c_version = (u16)fw_h2c_ver;

	dmem_pkt_size = le32_to_cpu(*((__le32 *)(hamacl_fw +
				      HALMAC_FWHDR_OFFSET_DMEM_SIZE_88XX)));
	iram_pkt_size = le32_to_cpu(*((__le32 *)(hamacl_fw +
				      HALMAC_FWHDR_OFFSET_IRAM_SIZE_88XX)));
	if (((*(hamacl_fw + HALMAC_FWHDR_OFFSET_MEM_USAGE_88XX)) & BIT(4)) != 0)
		eram_pkt_size =
		     le32_to_cpu(*((__le32 *)(hamacl_fw +
				   HALMAC_FWHDR_OFFSET_ERAM_SIZE_88XX)));

	dmem_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	iram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	if (eram_pkt_size != 0)
		eram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;

	if (halmac_fw_size != (HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size +
			       iram_pkt_size + eram_pkt_size)) {
		pr_err("FW size mismatch the real fw size!\n");
		goto DLFW_FAIL;
	}

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CR + 1);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_CR + 1;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR + 1,
			   value8); /* Enable SW TX beacon */

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_BCN_CTRL);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_BCN_CTRL;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)((value8 & (~BIT(3))) | BIT(4));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_BCN_CTRL,
			   value8); /* Disable beacon related functions */

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_FWHW_TXQ_CTRL + 2);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_FWHW_TXQ_CTRL + 2;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FWHW_TXQ_CTRL + 2,
			   value8); /* Disable ptcl tx bcnq */

	restore_info[restore_index].length = 2;
	restore_info[restore_index].mac_register = REG_FIFOPAGE_CTRL_2;
	restore_info[restore_index].value =
		HALMAC_REG_READ_16(halmac_adapter, REG_FIFOPAGE_CTRL_2) |
		BIT(15);
	restore_index++;
	value16 = 0x8000;
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    value16); /* Set beacon header to  0 */

	value16 = (u16)(HALMAC_REG_READ_16(halmac_adapter, REG_MCUFW_CTRL) &
			0x3800);
	value16 |= BIT(0);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_MCUFW_CTRL,
			    value16); /* MCU/FW setting */

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CPU_DMEM_CON + 2);
	value8 &= ~(BIT(0));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CPU_DMEM_CON + 2, value8);
	value8 |= BIT(0);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CPU_DMEM_CON + 2, value8);

	/* Download to DMEM */
	file_ptr = hamacl_fw + HALMAC_FWHDR_SIZE_88XX;
	temp = le32_to_cpu(*((__le32 *)(hamacl_fw +
			   HALMAC_FWHDR_OFFSET_DMEM_ADDR_88XX))) &
			   ~(BIT(31));
	if (halmac_dlfw_to_mem_88xx(halmac_adapter, file_ptr, temp,
				    dmem_pkt_size) != HALMAC_RET_SUCCESS)
		goto DLFW_END;

	/* Download to IMEM */
	file_ptr = hamacl_fw + HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size;
	temp = le32_to_cpu(*((__le32 *)(hamacl_fw +
			   HALMAC_FWHDR_OFFSET_IRAM_ADDR_88XX))) &
			   ~(BIT(31));
	if (halmac_dlfw_to_mem_88xx(halmac_adapter, file_ptr, temp,
				    iram_pkt_size) != HALMAC_RET_SUCCESS)
		goto DLFW_END;

	/* Download to EMEM */
	if (eram_pkt_size != 0) {
		file_ptr = hamacl_fw + HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size +
			   iram_pkt_size;
		dest = le32_to_cpu((*((__le32 *)(hamacl_fw +
				    HALMAC_FWHDR_OFFSET_EMEM_ADDR_88XX)))) &
				   ~(BIT(31));
		if (halmac_dlfw_to_mem_88xx(halmac_adapter, file_ptr, dest,
					    eram_pkt_size) !=
		    HALMAC_RET_SUCCESS)
			goto DLFW_END;
	}

	halmac_init_offload_feature_state_machine_88xx(halmac_adapter);
DLFW_END:

	halmac_restore_mac_register_88xx(halmac_adapter, restore_info,
					 DLFW_RESTORE_REG_NUM_88XX);

	if (halmac_dlfw_end_flow_88xx(halmac_adapter) != HALMAC_RET_SUCCESS)
		goto DLFW_FAIL;

	halmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_DONE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;

DLFW_FAIL:

	/* Disable FWDL_EN */
	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_MCUFW_CTRL,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_MCUFW_CTRL) &
		     ~(BIT(0))));

	return HALMAC_RET_DLFW_FAIL;
}

/**
 * halmac_free_download_firmware_88xx() - download specific memory firmware
 * @halmac_adapter
 * @dlfw_mem : memory selection
 * @hamacl_fw : firmware bin
 * @halmac_fw_size : firmware size
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
halmac_free_download_firmware_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_dlfw_mem dlfw_mem, u8 *hamacl_fw,
				   u32 halmac_fw_size)
{
	u8 tx_pause_backup;
	u8 *file_ptr;
	u32 dest;
	u16 bcn_head_backup;
	u32 iram_pkt_size, dmem_pkt_size, eram_pkt_size = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_DLFW_FAIL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	if (halmac_fw_size > HALMAC_FW_SIZE_MAX_88XX ||
	    halmac_fw_size < HALMAC_FWHDR_SIZE_88XX) {
		pr_err("[ERR]FW size error!\n");
		return HALMAC_RET_FW_SIZE_ERR;
	}

	dmem_pkt_size =
	    le32_to_cpu(*(__le32 *)(hamacl_fw +
				    HALMAC_FWHDR_OFFSET_DMEM_SIZE_88XX));
	iram_pkt_size =
	    le32_to_cpu(*(__le32 *)(hamacl_fw +
				    HALMAC_FWHDR_OFFSET_IRAM_SIZE_88XX));
	if (((*(hamacl_fw + HALMAC_FWHDR_OFFSET_MEM_USAGE_88XX)) & BIT(4)) != 0)
		eram_pkt_size =
		  le32_to_cpu(*(__le32 *)(hamacl_fw +
					  HALMAC_FWHDR_OFFSET_ERAM_SIZE_88XX));

	dmem_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	iram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	if (eram_pkt_size != 0)
		eram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;

	if (halmac_fw_size != (HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size +
			       iram_pkt_size + eram_pkt_size)) {
		pr_err("[ERR]FW size mismatch the real fw size!\n");
		return HALMAC_RET_DLFW_FAIL;
	}

	tx_pause_backup = HALMAC_REG_READ_8(halmac_adapter, REG_TXPAUSE);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXPAUSE,
			   tx_pause_backup | BIT(7));

	bcn_head_backup =
		HALMAC_REG_READ_16(halmac_adapter, REG_FIFOPAGE_CTRL_2) |
		BIT(15);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2, 0x8000);

	if (eram_pkt_size != 0) {
		file_ptr = hamacl_fw + HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size +
			   iram_pkt_size;
		dest = le32_to_cpu(*((__le32 *)(hamacl_fw +
				   HALMAC_FWHDR_OFFSET_EMEM_ADDR_88XX))) &
				   ~(BIT(31));
		status = halmac_dlfw_to_mem_88xx(halmac_adapter, file_ptr, dest,
						 eram_pkt_size);
		if (status != HALMAC_RET_SUCCESS)
			goto DL_FREE_FW_END;
	}

	status = halmac_free_dl_fw_end_flow_88xx(halmac_adapter);

DL_FREE_FW_END:
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXPAUSE, tx_pause_backup);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    bcn_head_backup);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return status;
}

/**
 * halmac_get_fw_version_88xx() - get FW version
 * @halmac_adapter : the adapter of halmac
 * @fw_version : fw version info
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_fw_version_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_fw_version *fw_version)
{
	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_adapter->halmac_state.dlfw_state == 0)
		return HALMAC_RET_DLFW_FAIL;

	fw_version->version = halmac_adapter->fw_version.version;
	fw_version->sub_version = halmac_adapter->fw_version.sub_version;
	fw_version->sub_index = halmac_adapter->fw_version.sub_index;

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_mac_addr_88xx() - config mac address
 * @halmac_adapter : the adapter of halmac
 * @halmac_port :0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @hal_address : mac address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_mac_addr_88xx(struct halmac_adapter *halmac_adapter, u8 halmac_port,
			 union halmac_wlan_addr *hal_address)
{
	u16 mac_address_H;
	u32 mac_address_L;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	if (halmac_port >= HALMAC_PORTIDMAX) {
		pr_err("[ERR]port index > 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	mac_address_L = le32_to_cpu(hal_address->address_l_h.le_address_low);
	mac_address_H = le16_to_cpu(hal_address->address_l_h.le_address_high);

	halmac_adapter->hal_mac_addr[halmac_port].address_l_h.address_low =
		mac_address_L;
	halmac_adapter->hal_mac_addr[halmac_port].address_l_h.address_high =
		mac_address_H;

	switch (halmac_port) {
	case HALMAC_PORTID0:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_MACID, mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MACID + 4,
				    mac_address_H);
		break;

	case HALMAC_PORTID1:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_MACID1, mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MACID1 + 4,
				    mac_address_H);
		break;

	case HALMAC_PORTID2:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_MACID2, mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MACID2 + 4,
				    mac_address_H);
		break;

	case HALMAC_PORTID3:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_MACID3, mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MACID3 + 4,
				    mac_address_H);
		break;

	case HALMAC_PORTID4:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_MACID4, mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MACID4 + 4,
				    mac_address_H);
		break;

	default:

		break;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_bssid_88xx() - config BSSID
 * @halmac_adapter : the adapter of halmac
 * @halmac_port :0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @hal_address : bssid
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_bssid_88xx(struct halmac_adapter *halmac_adapter, u8 halmac_port,
		      union halmac_wlan_addr *hal_address)
{
	u16 bssid_address_H;
	u32 bssid_address_L;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	if (halmac_port >= HALMAC_PORTIDMAX) {
		pr_err("[ERR]port index > 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	bssid_address_L = le32_to_cpu(hal_address->address_l_h.le_address_low);
	bssid_address_H = le16_to_cpu(hal_address->address_l_h.le_address_high);

	halmac_adapter->hal_bss_addr[halmac_port].address_l_h.address_low =
		bssid_address_L;
	halmac_adapter->hal_bss_addr[halmac_port].address_l_h.address_high =
		bssid_address_H;

	switch (halmac_port) {
	case HALMAC_PORTID0:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_BSSID, bssid_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_BSSID + 4,
				    bssid_address_H);
		break;

	case HALMAC_PORTID1:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_BSSID1,
				    bssid_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_BSSID1 + 4,
				    bssid_address_H);
		break;

	case HALMAC_PORTID2:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_BSSID2,
				    bssid_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_BSSID2 + 4,
				    bssid_address_H);
		break;

	case HALMAC_PORTID3:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_BSSID3,
				    bssid_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_BSSID3 + 4,
				    bssid_address_H);
		break;

	case HALMAC_PORTID4:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_BSSID4,
				    bssid_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_BSSID4 + 4,
				    bssid_address_H);
		break;

	default:

		break;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_multicast_addr_88xx() - config multicast address
 * @halmac_adapter : the adapter of halmac
 * @hal_address : multicast address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_multicast_addr_88xx(struct halmac_adapter *halmac_adapter,
			       union halmac_wlan_addr *hal_address)
{
	u16 address_H;
	u32 address_L;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_MULTICAST_ADDR);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	address_L = le32_to_cpu(hal_address->address_l_h.le_address_low);
	address_H = le16_to_cpu(hal_address->address_l_h.le_address_high);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_MAR, address_L);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_MAR + 4, address_H);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pre_init_system_cfg_88xx() - pre-init system config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_pre_init_system_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	u32 value32, counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	bool enable_bb;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_PRE_INIT_SYSTEM_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_pre_init_system_cfg ==========>\n");

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SDIO_HSUS_CTRL,
			HALMAC_REG_READ_8(halmac_adapter, REG_SDIO_HSUS_CTRL) &
				~(BIT(0)));
		counter = 10000;
		while (!(HALMAC_REG_READ_8(halmac_adapter, REG_SDIO_HSUS_CTRL) &
			 0x02)) {
			counter--;
			if (counter == 0)
				return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
		}
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		if (HALMAC_REG_READ_8(halmac_adapter, REG_SYS_CFG2 + 3) ==
		    0x20) /* usb3.0 */
			HALMAC_REG_WRITE_8(
				halmac_adapter, 0xFE5B,
				HALMAC_REG_READ_8(halmac_adapter, 0xFE5B) |
					BIT(4));
	}

	/* Config PIN Mux */
	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_PAD_CTRL1);
	value32 = value32 & (~(BIT(28) | BIT(29)));
	value32 = value32 | BIT(28) | BIT(29);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_PAD_CTRL1, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_LED_CFG);
	value32 = value32 & (~(BIT(25) | BIT(26)));
	HALMAC_REG_WRITE_32(halmac_adapter, REG_LED_CFG, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_GPIO_MUXCFG);
	value32 = value32 & (~(BIT(2)));
	value32 = value32 | BIT(2);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_GPIO_MUXCFG, value32);

	enable_bb = false;
	halmac_set_hw_value_88xx(halmac_adapter, HALMAC_HW_EN_BB_RF,
				 &enable_bb);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_pre_init_system_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_system_cfg_88xx() -  init system config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_system_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_SYSTEM_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_init_system_cfg ==========>\n");

	HALMAC_REG_WRITE_8(halmac_adapter, REG_SYS_FUNC_EN + 1,
			   HALMAC_FUNCTION_ENABLE_88XX);
	HALMAC_REG_WRITE_32(
		halmac_adapter, REG_SYS_SDIO_CTRL,
		(u32)(HALMAC_REG_READ_32(halmac_adapter, REG_SYS_SDIO_CTRL) |
		      BIT_LTE_MUX_CTRL_PATH));
	HALMAC_REG_WRITE_32(
		halmac_adapter, REG_CPU_DMEM_CON,
		(u32)(HALMAC_REG_READ_32(halmac_adapter, REG_CPU_DMEM_CON) |
		      BIT_WL_PLATFORM_RST));

	/* halmac_api->halmac_init_h2c(halmac_adapter); */

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_init_system_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_edca_cfg_88xx() - init EDCA config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_edca_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 value8;
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_EDCA_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	/* Clear TX pause */
	HALMAC_REG_WRITE_16(halmac_adapter, REG_TXPAUSE, 0x0000);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_SLOT, HALMAC_SLOT_TIME_88XX);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_PIFS, HALMAC_PIFS_TIME_88XX);
	value32 = HALMAC_SIFS_CCK_CTX_88XX |
		  (HALMAC_SIFS_OFDM_CTX_88XX << BIT_SHIFT_SIFS_OFDM_CTX) |
		  (HALMAC_SIFS_CCK_TRX_88XX << BIT_SHIFT_SIFS_CCK_TRX) |
		  (HALMAC_SIFS_OFDM_TRX_88XX << BIT_SHIFT_SIFS_OFDM_TRX);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_SIFS, value32);

	HALMAC_REG_WRITE_32(
		halmac_adapter, REG_EDCA_VO_PARAM,
		HALMAC_REG_READ_32(halmac_adapter, REG_EDCA_VO_PARAM) & 0xFFFF);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_EDCA_VO_PARAM + 2,
			    HALMAC_VO_TXOP_LIMIT_88XX);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_EDCA_VI_PARAM + 2,
			    HALMAC_VI_TXOP_LIMIT_88XX);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_RD_NAV_NXT,
			    HALMAC_RDG_NAV_88XX | (HALMAC_TXOP_NAV_88XX << 16));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_RXTSF_OFFSET_CCK,
			    HALMAC_CCK_RX_TSF_88XX |
				    (HALMAC_OFDM_RX_TSF_88XX) << 8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RD_CTRL + 1);
	value8 |=
		(BIT_VOQ_RD_INIT_EN | BIT_VIQ_RD_INIT_EN | BIT_BEQ_RD_INIT_EN);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_RD_CTRL + 1, value8);

	/* Set beacon cotnrol - enable TSF and other related functions */
	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_BCN_CTRL,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_BCN_CTRL) |
		     BIT_EN_BCN_FUNCTION));

	/* Set send beacon related registers */
	HALMAC_REG_WRITE_32(halmac_adapter, REG_TBTT_PROHIBIT,
			    HALMAC_TBTT_PROHIBIT_88XX |
				    (HALMAC_TBTT_HOLD_TIME_88XX
				     << BIT_SHIFT_TBTT_HOLD_TIME_AP));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_DRVERLYINT,
			   HALMAC_DRIVER_EARLY_INT_88XX);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_BCNDMATIM,
			   HALMAC_BEACON_DMA_TIM_88XX);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_wmac_cfg_88xx() - init wmac config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_wmac_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_WMAC_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_RXFLTMAP0,
			    HALMAC_RX_FILTER0_88XX);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_RXFLTMAP,
			    HALMAC_RX_FILTER_88XX);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_RCR, HALMAC_RCR_CONFIG_88XX);

	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_TCR + 1,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_TCR + 1) | 0x30));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TCR + 2, 0x30);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TCR + 1, 0x00);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_WMAC_OPTION_FUNCTION + 8,
			    0x30810041);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_WMAC_OPTION_FUNCTION + 4,
			    0x50802080);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_mac_cfg_88xx() - config page1~page7 register
 * @halmac_adapter : the adapter of halmac
 * @mode : trx mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_mac_cfg_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_trx_mode mode)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_MAC_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>mode = %d\n", __func__,
			mode);

	status = halmac_api->halmac_init_trx_cfg(halmac_adapter, mode);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_init_trx_cfg error = %x\n", status);
		return status;
	}
	status = halmac_api->halmac_init_protocol_cfg(halmac_adapter);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_init_protocol_cfg_88xx error = %x\n", status);
		return status;
	}

	status = halmac_init_edca_cfg_88xx(halmac_adapter);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_init_edca_cfg_88xx error = %x\n", status);
		return status;
	}

	status = halmac_init_wmac_cfg_88xx(halmac_adapter);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_init_wmac_cfg_88xx error = %x\n", status);
		return status;
	}
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return status;
}

/**
 * halmac_cfg_operation_mode_88xx() - config operation mode
 * @halmac_adapter : the adapter of halmac
 * @wireless_mode : 802.11 standard(b/g/n/ac)
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_operation_mode_88xx(struct halmac_adapter *halmac_adapter,
			       enum halmac_wireless_mode wireless_mode)
{
	void *driver_adapter = NULL;
	enum halmac_wireless_mode wireless_mode_local =
		HALMAC_WIRELESS_MODE_UNDEFINE;

	wireless_mode_local = wireless_mode;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_OPERATION_MODE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
		"%s ==========>wireless_mode = %d\n", __func__,
		wireless_mode);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_ch_bw_88xx() - config channel & bandwidth
 * @halmac_adapter : the adapter of halmac
 * @channel : WLAN channel, support 2.4G & 5G
 * @pri_ch_idx : primary channel index, idx1, idx2, idx3, idx4
 * @bw : band width, 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_ch_bw_88xx(struct halmac_adapter *halmac_adapter, u8 channel,
		      enum halmac_pri_ch_idx pri_ch_idx, enum halmac_bw bw)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_CH_BW);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>ch = %d, idx=%d, bw=%d\n", __func__,
			channel, pri_ch_idx, bw);

	halmac_cfg_pri_ch_idx_88xx(halmac_adapter, pri_ch_idx);

	halmac_cfg_bw_88xx(halmac_adapter, bw);

	halmac_cfg_ch_88xx(halmac_adapter, channel);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_cfg_ch_88xx(struct halmac_adapter *halmac_adapter,
					  u8 channel)
{
	u8 value8;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_CH_BW);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>ch = %d\n", __func__, channel);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CCK_CHECK);
	value8 = value8 & (~(BIT(7)));

	if (channel > 35)
		value8 = value8 | BIT(7);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_CCK_CHECK, value8);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_cfg_pri_ch_idx_88xx(struct halmac_adapter *halmac_adapter,
			   enum halmac_pri_ch_idx pri_ch_idx)
{
	u8 txsc_40 = 0, txsc_20 = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_CH_BW);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========> idx=%d\n", __func__,
			pri_ch_idx);

	txsc_20 = pri_ch_idx;
	if (txsc_20 == HALMAC_CH_IDX_1 || txsc_20 == HALMAC_CH_IDX_3)
		txsc_40 = 9;
	else
		txsc_40 = 10;

	HALMAC_REG_WRITE_8(halmac_adapter, REG_DATA_SC,
			   BIT_TXSC_20M(txsc_20) | BIT_TXSC_40M(txsc_40));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_bw_88xx() - config bandwidth
 * @halmac_adapter : the adapter of halmac
 * @bw : band width, 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_cfg_bw_88xx(struct halmac_adapter *halmac_adapter,
					  enum halmac_bw bw)
{
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_BW);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>bw=%d\n", __func__, bw);

	/* RF mode */
	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_WMAC_TRXPTCL_CTL);
	value32 = value32 & (~(BIT(7) | BIT(8)));

	switch (bw) {
	case HALMAC_BW_80:
		value32 = value32 | BIT(7);
		break;
	case HALMAC_BW_40:
		value32 = value32 | BIT(8);
		break;
	case HALMAC_BW_20:
	case HALMAC_BW_10:
	case HALMAC_BW_5:
		break;
	default:
		pr_err("%s switch case not support\n", __func__);
		break;
	}
	HALMAC_REG_WRITE_32(halmac_adapter, REG_WMAC_TRXPTCL_CTL, value32);

	/* MAC CLK */
	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_AFE_CTRL1);
	value32 = (value32 & (~(BIT(20) | BIT(21)))) |
		  (HALMAC_MAC_CLOCK_HW_DEF_80M << BIT_SHIFT_MAC_CLK_SEL);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_AFE_CTRL1, value32);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_USTIME_TSF,
			   HALMAC_MAC_CLOCK_88XX);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_USTIME_EDCA,
			   HALMAC_MAC_CLOCK_88XX);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_efuse_map_88xx() - dump "physical" efuse map
 * @halmac_adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_dump_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
			   enum halmac_efuse_read_cfg cfg)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DUMP_EFUSE_MAP);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>cfg=%d\n", __func__, cfg);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF)
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_WARNING,
				"[WARN]Dump efuse in suspend mode\n");

	*process_status = HALMAC_CMD_PROCESS_IDLE;
	halmac_adapter->event_trigger.physical_efuse_map = 1;

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_dump_efuse_88xx(halmac_adapter, cfg);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_read_efuse error = %x\n", status);
		return status;
	}

	if (halmac_adapter->hal_efuse_map_valid) {
		*process_status = HALMAC_CMD_PROCESS_DONE;

		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,
			*process_status, halmac_adapter->hal_efuse_map,
			halmac_adapter->hw_config_info.efuse_size);
		halmac_adapter->event_trigger.physical_efuse_map = 0;
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_efuse_map_bt_88xx() - dump "BT physical" efuse map
 * @halmac_adapter : the adapter of halmac
 * @halmac_efuse_bank : bt efuse bank
 * @bt_efuse_map_size : bt efuse map size. get from halmac_get_efuse_size API
 * @bt_efuse_map : bt efuse map
 * Author : Soar / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_dump_efuse_map_bt_88xx(struct halmac_adapter *halmac_adapter,
			      enum halmac_efuse_bank halmac_efuse_bank,
			      u32 bt_efuse_map_size, u8 *bt_efuse_map)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DUMP_EFUSE_MAP_BT);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_adapter->hw_config_info.bt_efuse_size != bt_efuse_map_size)
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;

	if ((halmac_efuse_bank >= HALMAC_EFUSE_BANK_MAX) ||
	    halmac_efuse_bank == HALMAC_EFUSE_BANK_WIFI) {
		pr_err("Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    halmac_efuse_bank);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_read_hw_efuse_88xx(halmac_adapter, 0, bt_efuse_map_size,
					   bt_efuse_map);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_read_hw_efuse_88xx error = %x\n", status);
		return status;
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_write_efuse_bt_88xx() - write "BT physical" efuse offset
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : offset
 * @halmac_value : Write value
 * @bt_efuse_map : bt efuse map
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_write_efuse_bt_88xx(struct halmac_adapter *halmac_adapter,
			   u32 halmac_offset, u8 halmac_value,
			   enum halmac_efuse_bank halmac_efuse_bank)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_WRITE_EFUSE_BT);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"offset : %X value : %X Bank : %X\n", halmac_offset,
			halmac_value, halmac_efuse_bank);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_offset >= halmac_adapter->hw_config_info.efuse_size) {
		pr_err("Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (halmac_efuse_bank > HALMAC_EFUSE_BANK_MAX ||
	    halmac_efuse_bank == HALMAC_EFUSE_BANK_WIFI) {
		pr_err("Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    halmac_efuse_bank);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_write_efuse_88xx(halmac_adapter, halmac_offset,
					      halmac_value);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_write_efuse error = %x\n", status);
		return status;
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_efuse_available_size_88xx() - get efuse available size
 * @halmac_adapter : the adapter of halmac
 * @halmac_size : physical efuse available size
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_efuse_available_size_88xx(struct halmac_adapter *halmac_adapter,
				     u32 *halmac_size)
{
	enum halmac_ret_status status;
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	status = halmac_dump_logical_efuse_map_88xx(halmac_adapter,
						    HALMAC_EFUSE_R_DRV);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	*halmac_size = halmac_adapter->hw_config_info.efuse_size -
		       HALMAC_PROTECTED_EFUSE_SIZE_88XX -
		       halmac_adapter->efuse_end;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_efuse_size_88xx() - get "physical" efuse size
 * @halmac_adapter : the adapter of halmac
 * @halmac_size : physical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_efuse_size_88xx(struct halmac_adapter *halmac_adapter,
			   u32 *halmac_size)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_EFUSE_SIZE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	*halmac_size = halmac_adapter->hw_config_info.efuse_size;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_logical_efuse_size_88xx() - get "logical" efuse size
 * @halmac_adapter : the adapter of halmac
 * @halmac_size : logical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_logical_efuse_size_88xx(struct halmac_adapter *halmac_adapter,
				   u32 *halmac_size)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_GET_LOGICAL_EFUSE_SIZE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	*halmac_size = halmac_adapter->hw_config_info.eeprom_size;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_logical_efuse_map_88xx() - dump "logical" efuse map
 * @halmac_adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_dump_logical_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_efuse_read_cfg cfg)
{
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_DUMP_LOGICAL_EFUSE_MAP);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>cfg = %d\n", __func__, cfg);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF)
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_WARNING,
				"[WARN]Dump logical efuse in suspend mode\n");

	*process_status = HALMAC_CMD_PROCESS_IDLE;
	halmac_adapter->event_trigger.logical_efuse_map = 1;

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_dump_efuse_88xx(halmac_adapter, cfg);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_eeprom_parser_88xx error = %x\n", status);
		return status;
	}

	if (halmac_adapter->hal_efuse_map_valid) {
		*process_status = HALMAC_CMD_PROCESS_DONE;

		eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
		if (!eeprom_map) {
			/* out of memory */
			return HALMAC_RET_MALLOC_FAIL;
		}
		memset(eeprom_map, 0xFF, eeprom_size);

		if (halmac_eeprom_parser_88xx(halmac_adapter,
					      halmac_adapter->hal_efuse_map,
					      eeprom_map) != HALMAC_RET_SUCCESS) {
			kfree(eeprom_map);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,
			*process_status, eeprom_map, eeprom_size);
		halmac_adapter->event_trigger.logical_efuse_map = 0;

		kfree(eeprom_map);
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_read_logical_efuse_88xx() - read logical efuse map 1 byte
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : offset
 * @value : 1 byte efuse value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_read_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset, u8 *value)
{
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_READ_LOGICAL_EFUSE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_offset >= eeprom_size) {
		pr_err("Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}
	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
	if (!eeprom_map) {
		/* out of memory */
		return HALMAC_RET_MALLOC_FAIL;
	}
	memset(eeprom_map, 0xFF, eeprom_size);

	status = halmac_read_logical_efuse_map_88xx(halmac_adapter, eeprom_map);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_read_logical_efuse_map error = %x\n", status);
		kfree(eeprom_map);
		return status;
	}

	*value = *(eeprom_map + halmac_offset);

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS) {
		kfree(eeprom_map);
		return HALMAC_RET_ERROR_STATE;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	kfree(eeprom_map);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_write_logical_efuse_88xx() - write "logical" efuse offset
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : offset
 * @halmac_value : value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_write_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset, u8 halmac_value)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_WRITE_LOGICAL_EFUSE);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_offset >= halmac_adapter->hw_config_info.eeprom_size) {
		pr_err("Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_write_logical_efuse_88xx(
		halmac_adapter, halmac_offset, halmac_value);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_write_logical_efuse error = %x\n", status);
		return status;
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pg_efuse_by_map_88xx() - pg logical efuse by map
 * @halmac_adapter : the adapter of halmac
 * @pg_efuse_info : efuse map information
 * @cfg : dump efuse method
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_pg_efuse_by_map_88xx(struct halmac_adapter *halmac_adapter,
			    struct halmac_pg_efuse_info *pg_efuse_info,
			    enum halmac_efuse_read_cfg cfg)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_PG_EFUSE_BY_MAP);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (pg_efuse_info->efuse_map_size !=
	    halmac_adapter->hw_config_info.eeprom_size) {
		pr_err("efuse_map_size is incorrect, should be %d bytes\n",
		       halmac_adapter->hw_config_info.eeprom_size);
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((pg_efuse_info->efuse_map_size & 0xF) > 0) {
		pr_err("efuse_map_size should be multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (pg_efuse_info->efuse_mask_size !=
	    pg_efuse_info->efuse_map_size >> 4) {
		pr_err("efuse_mask_size is incorrect, should be %d bytes\n",
		       pg_efuse_info->efuse_map_size >> 4);
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (!pg_efuse_info->efuse_map) {
		pr_err("efuse_map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (!pg_efuse_info->efuse_mask) {
		pr_err("efuse_mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_efuse_curr_state_88xx(halmac_adapter) !=
	    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(halmac_adapter,
						    HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_pg_efuse_by_map_88xx(halmac_adapter, pg_efuse_info,
						  cfg);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_pg_efuse_by_map error = %x\n", status);
		return status;
	}

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_EFUSE, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_c2h_info_88xx() - process halmac C2H packet
 * @halmac_adapter : the adapter of halmac
 * @halmac_buf : RX Packet pointer
 * @halmac_size : RX Packet size
 * Author : KaiYuan Chang/Ivan Lin
 *
 * Used to process c2h packet info from RX path. After receiving the packet,
 * user need to call this api and pass the packet pointer.
 *
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_c2h_info_88xx(struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
			 u32 halmac_size)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_C2H_INFO);

	driver_adapter = halmac_adapter->driver_adapter;

	/* Check if it is C2H packet */
	if (GET_RX_DESC_C2H(halmac_buf)) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"C2H packet, start parsing!\n");

		status = halmac_parse_c2h_packet_88xx(halmac_adapter,
						      halmac_buf, halmac_size);

		if (status != HALMAC_RET_SUCCESS) {
			pr_err("halmac_parse_c2h_packet_88xx error = %x\n",
			       status);
			return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_cfg_fwlps_option_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_fwlps_option *lps_option)
{
	void *driver_adapter = NULL;
	struct halmac_fwlps_option *hal_fwlps_option;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_FWLPS_OPTION);

	driver_adapter = halmac_adapter->driver_adapter;
	hal_fwlps_option = &halmac_adapter->fwlps_option;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	hal_fwlps_option->mode = lps_option->mode;
	hal_fwlps_option->clk_request = lps_option->clk_request;
	hal_fwlps_option->rlbm = lps_option->rlbm;
	hal_fwlps_option->smart_ps = lps_option->smart_ps;
	hal_fwlps_option->awake_interval = lps_option->awake_interval;
	hal_fwlps_option->all_queue_uapsd = lps_option->all_queue_uapsd;
	hal_fwlps_option->pwr_state = lps_option->pwr_state;
	hal_fwlps_option->low_pwr_rx_beacon = lps_option->low_pwr_rx_beacon;
	hal_fwlps_option->ant_auto_switch = lps_option->ant_auto_switch;
	hal_fwlps_option->ps_allow_bt_high_priority =
		lps_option->ps_allow_bt_high_priority;
	hal_fwlps_option->protect_bcn = lps_option->protect_bcn;
	hal_fwlps_option->silence_period = lps_option->silence_period;
	hal_fwlps_option->fast_bt_connect = lps_option->fast_bt_connect;
	hal_fwlps_option->two_antenna_en = lps_option->two_antenna_en;
	hal_fwlps_option->adopt_user_setting = lps_option->adopt_user_setting;
	hal_fwlps_option->drv_bcn_early_shift = lps_option->drv_bcn_early_shift;
	hal_fwlps_option->enter_32K = lps_option->enter_32K;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_cfg_fwips_option_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_fwips_option *ips_option)
{
	void *driver_adapter = NULL;
	struct halmac_fwips_option *ips_option_local;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_FWIPS_OPTION);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	ips_option_local = ips_option;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_enter_wowlan_88xx(struct halmac_adapter *halmac_adapter,
			 struct halmac_wowlan_option *wowlan_option)
{
	void *driver_adapter = NULL;
	struct halmac_wowlan_option *wowlan_option_local;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_ENTER_WOWLAN);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	wowlan_option_local = wowlan_option;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_leave_wowlan_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_LEAVE_WOWLAN);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_enter_ps_88xx(struct halmac_adapter *halmac_adapter,
		     enum halmac_ps_state ps_state)
{
	u8 rpwm;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_ENTER_PS);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (ps_state == halmac_adapter->halmac_state.ps_state) {
		pr_err("power state is already in PS State!!\n");
		return HALMAC_RET_SUCCESS;
	}

	if (ps_state == HALMAC_PS_STATE_LPS) {
		status = halmac_send_h2c_set_pwr_mode_88xx(
			halmac_adapter, &halmac_adapter->fwlps_option);
		if (status != HALMAC_RET_SUCCESS) {
			pr_err("halmac_send_h2c_set_pwr_mode_88xx error = %x!!\n",
			       status);
			return status;
		}
	} else if (ps_state == HALMAC_PS_STATE_IPS) {
	}

	halmac_adapter->halmac_state.ps_state = ps_state;

	/* Enter 32K */
	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		if (halmac_adapter->fwlps_option.enter_32K) {
			rpwm = (u8)(((halmac_adapter->rpwm_record ^ (BIT(7))) |
				     (BIT(0))) &
				    0x81);
			HALMAC_REG_WRITE_8(halmac_adapter, REG_SDIO_HRPWM1,
					   rpwm);
			halmac_adapter->low_clk = true;
		}
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		if (halmac_adapter->fwlps_option.enter_32K) {
			rpwm = (u8)(((halmac_adapter->rpwm_record ^ (BIT(7))) |
				     (BIT(0))) &
				    0x81);
			HALMAC_REG_WRITE_8(halmac_adapter, 0xFE58, rpwm);
			halmac_adapter->low_clk = true;
		}
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_leave_ps_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 rpwm, cpwm;
	u32 counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_fwlps_option fw_lps_option;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_LEAVE_PS);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_adapter->halmac_state.ps_state == HALMAC_PS_STATE_ACT) {
		pr_err("power state is already in active!!\n");
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->low_clk) {
		cpwm = HALMAC_REG_READ_8(halmac_adapter, REG_SDIO_HRPWM1);
		rpwm = (u8)(
			((halmac_adapter->rpwm_record ^ (BIT(7))) | (BIT(6))) &
			0xC0);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SDIO_HRPWM1, rpwm);

		cpwm = (u8)((cpwm ^ BIT(7)) & BIT(7));
		counter = 100;
		while (cpwm !=
		       (HALMAC_REG_READ_8(halmac_adapter, REG_SDIO_HRPWM1) &
			BIT(7))) {
			usleep_range(50, 60);
			counter--;
			if (counter == 0)
				return HALMAC_RET_CHANGE_PS_FAIL;
		}
		halmac_adapter->low_clk = false;
	}

	memcpy(&fw_lps_option, &halmac_adapter->fwlps_option,
	       sizeof(struct halmac_fwlps_option));
	fw_lps_option.mode = 0;

	status = halmac_send_h2c_set_pwr_mode_88xx(halmac_adapter,
						   &fw_lps_option);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_set_pwr_mode_88xx error!!=%x\n",
		       status);
		return status;
	}

	halmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * (debug API)halmac_h2c_lb_88xx() - send h2c loopback packet
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_h2c_lb_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_H2C_LB);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_debug_88xx() - dump information for debugging
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_debug_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 temp8 = 0;
	u32 i = 0, temp32 = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DEBUG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		/* Dump CCCR, it needs new platform api */

		/*Dump SDIO Local Register, use CMD52*/
		for (i = 0x10250000; i < 0x102500ff; i++) {
			temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
			HALMAC_RT_TRACE(
				driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: sdio[%x]=%x\n", i, temp8);
		}

		/*Dump MAC Register*/
		for (i = 0x0000; i < 0x17ff; i++) {
			temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "halmac_debug: mac[%x]=%x\n",
					i, temp8);
		}

		/*Check RX Fifo status*/
		i = REG_RXFF_PTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_WTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_PTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_WTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp8);
	} else {
		/*Dump MAC Register*/
		for (i = 0x0000; i < 0x17fc; i += 4) {
			temp32 = HALMAC_REG_READ_32(halmac_adapter, i);
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "halmac_debug: mac[%x]=%x\n",
					i, temp32);
		}

		/*Check RX Fifo status*/
		i = REG_RXFF_PTR_V1;
		temp32 = HALMAC_REG_READ_32(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_WTR_V1;
		temp32 = HALMAC_REG_READ_32(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_PTR_V1;
		temp32 = HALMAC_REG_READ_32(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_WTR_V1;
		temp32 = HALMAC_REG_READ_32(halmac_adapter, i);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_debug: mac[%x]=%x\n", i, temp32);
	}

	/*	TODO: Add check register code, including MAC CLK, CPU CLK */

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_parameter_88xx() - config parameter by FW
 * @halmac_adapter : the adapter of halmac
 * @para_info : cmd id, content
 * @full_fifo : parameter information
 *
 * If msk_en = true, the format of array is {reg_info, mask, value}.
 * If msk_en =_FAUSE, the format of array is {reg_info, value}
 * The format of reg_info is
 * reg_info[31]=rf_reg, 0: MAC_BB reg, 1: RF reg
 * reg_info[27:24]=rf_path, 0: path_A, 1: path_B
 * if rf_reg=0(MAC_BB reg), rf_path is meaningless.
 * ref_info[15:0]=offset
 *
 * Example: msk_en = false
 * {0x8100000a, 0x00001122}
 * =>Set RF register, path_B, offset 0xA to 0x00001122
 * {0x00000824, 0x11224433}
 * =>Set MAC_BB register, offset 0x800 to 0x11224433
 *
 * Note : full fifo mode only for init flow
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_parameter_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_phy_parameter_info *para_info,
			  u8 full_fifo)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.cfg_para_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_PARAMETER);

	driver_adapter = halmac_adapter->driver_adapter;

	if (halmac_adapter->halmac_state.dlfw_state == HALMAC_DLFW_NONE) {
		pr_err("%s Fail due to DLFW NONE!!\n", __func__);
		return HALMAC_RET_DLFW_FAIL;
	}

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(cfg para)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (halmac_query_cfg_para_curr_state_88xx(halmac_adapter) !=
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE &&
	    halmac_query_cfg_para_curr_state_88xx(halmac_adapter) !=
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Not idle state(cfg para)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*process_status = HALMAC_CMD_PROCESS_IDLE;

	ret_status = halmac_send_h2c_phy_parameter_88xx(halmac_adapter,
							para_info, full_fifo);

	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_phy_parameter_88xx Fail!! = %x\n",
		       ret_status);
		return ret_status;
	}

	return ret_status;
}

/**
 * halmac_update_packet_88xx() - send specific packet to FW
 * @halmac_adapter : the adapter of halmac
 * @pkt_id : packet id, to know the purpose of this packet
 * @pkt : packet
 * @pkt_size : packet size
 *
 * Note : TX_DESC is not included in the pkt
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_update_packet_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_packet_id pkt_id, u8 *pkt, u32 pkt_size)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.update_packet_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_UPDATE_PACKET);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(update_packet)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	status = halmac_send_h2c_update_packet_88xx(halmac_adapter, pkt_id, pkt,
						    pkt_size);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_update_packet_88xx packet = %x,  fail = %x!!\n",
		       pkt_id, status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_bcn_ie_filter_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_bcn_ie_info *bcn_ie_info)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_BCN_IE_FILTER);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	status = halmac_send_h2c_update_bcn_parse_info_88xx(halmac_adapter,
							    bcn_ie_info);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_update_bcn_parse_info_88xx fail = %x\n",
		       status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_update_datapack_88xx(struct halmac_adapter *halmac_adapter,
			    enum halmac_data_type halmac_data_type,
			    struct halmac_phy_parameter_info *para_info)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_data_type halmac_data_type)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_RUN_DATAPACK);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	ret_status = halmac_send_h2c_run_datapack_88xx(halmac_adapter,
						       halmac_data_type);

	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_run_datapack_88xx Fail, datatype = %x, status = %x!!\n",
		       halmac_data_type, ret_status);
		return ret_status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_update_datapack_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_drv_info_88xx() - config driver info
 * @halmac_adapter : the adapter of halmac
 * @halmac_drv_info : driver information selection
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_drv_info_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_drv_info halmac_drv_info)
{
	u8 drv_info_size = 0;
	u8 phy_status_en = 0;
	u8 sniffer_en = 0;
	u8 plcp_hdr_en = 0;
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_DRV_INFO);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_cfg_drv_info = %d\n", halmac_drv_info);

	switch (halmac_drv_info) {
	case HALMAC_DRV_INFO_NONE:
		drv_info_size = 0;
		phy_status_en = 0;
		sniffer_en = 0;
		plcp_hdr_en = 0;
		break;
	case HALMAC_DRV_INFO_PHY_STATUS:
		drv_info_size = 4;
		phy_status_en = 1;
		sniffer_en = 0;
		plcp_hdr_en = 0;
		break;
	case HALMAC_DRV_INFO_PHY_SNIFFER:
		drv_info_size = 5; /* phy status 4byte, sniffer info 1byte */
		phy_status_en = 1;
		sniffer_en = 1;
		plcp_hdr_en = 0;
		break;
	case HALMAC_DRV_INFO_PHY_PLCP:
		drv_info_size = 6; /* phy status 4byte, plcp header 2byte */
		phy_status_en = 1;
		sniffer_en = 0;
		plcp_hdr_en = 1;
		break;
	default:
		status = HALMAC_RET_SW_CASE_NOT_SUPPORT;
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->txff_allocation.rx_fifo_expanding_mode !=
	    HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE)
		drv_info_size = 0xF;

	HALMAC_REG_WRITE_8(halmac_adapter, REG_RX_DRVINFO_SZ, drv_info_size);

	halmac_adapter->drv_info_size = drv_info_size;

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_RCR);
	value32 = (value32 & (~BIT_APP_PHYSTS));
	if (phy_status_en == 1)
		value32 = value32 | BIT_APP_PHYSTS;
	HALMAC_REG_WRITE_32(halmac_adapter, REG_RCR, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter,
				     REG_WMAC_OPTION_FUNCTION + 4);
	value32 = (value32 & (~(BIT(8) | BIT(9))));
	if (sniffer_en == 1)
		value32 = value32 | BIT(9);
	if (plcp_hdr_en == 1)
		value32 = value32 | BIT(8);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_WMAC_OPTION_FUNCTION + 4,
			    value32);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_bt_coex_88xx(struct halmac_adapter *halmac_adapter, u8 *bt_buf,
			 u32 bt_size, u8 ack)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SEND_BT_COEX);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	ret_status = halmac_send_bt_coex_cmd_88xx(halmac_adapter, bt_buf,
						  bt_size, ack);

	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_bt_coex_cmd_88xx Fail = %x!!\n",
		       ret_status);
		return ret_status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * (debug API)halmac_verify_platform_api_88xx() - verify platform api
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_verify_platform_api_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_VERIFY_PLATFORM_API);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	ret_status = halmac_verify_io_88xx(halmac_adapter);

	if (ret_status != HALMAC_RET_SUCCESS)
		return ret_status;

	if (halmac_adapter->txff_allocation.la_mode != HALMAC_LA_MODE_FULL)
		ret_status = halmac_verify_send_rsvd_page_88xx(halmac_adapter);

	if (ret_status != HALMAC_RET_SUCCESS)
		return ret_status;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return ret_status;
}

enum halmac_ret_status
halmac_send_original_h2c_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *original_h2c, u16 *seq, u8 ack)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SEND_ORIGINAL_H2C);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	status = halmac_func_send_original_h2c_88xx(halmac_adapter,
						    original_h2c, seq, ack);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_original_h2c FAIL = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_timer_2s_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_fill_txdesc_check_sum_88xx() -  fill in tx desc check sum
 * @halmac_adapter : the adapter of halmac
 * @cur_desc : tx desc packet
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_fill_txdesc_check_sum_88xx(struct halmac_adapter *halmac_adapter,
				  u8 *cur_desc)
{
	u16 chk_result = 0;
	u16 *data = (u16 *)NULL;
	u32 i;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_FILL_TXDESC_CHECKSUM);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (!cur_desc) {
		pr_err("%s NULL PTR", __func__);
		return HALMAC_RET_NULL_POINTER;
	}

	SET_TX_DESC_TXDESC_CHECKSUM(cur_desc, 0x0000);

	data = (u16 *)(cur_desc);

	/* HW clculates only 32byte */
	for (i = 0; i < 8; i++)
		chk_result ^= (*(data + 2 * i) ^ *(data + (2 * i + 1)));

	SET_TX_DESC_TXDESC_CHECKSUM(cur_desc, chk_result);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_fifo_88xx() - dump fifo data
 * @halmac_adapter : the adapter of halmac
 * @halmac_fifo_sel : FIFO selection
 * @halmac_start_addr : start address of selected FIFO
 * @halmac_fifo_dump_size : dump size of selected FIFO
 * @fifo_map : FIFO data
 *
 * Note : before dump fifo, user need to call halmac_get_fifo_size to
 * get fifo size. Then input this size to halmac_dump_fifo.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_dump_fifo_88xx(struct halmac_adapter *halmac_adapter,
		      enum hal_fifo_sel halmac_fifo_sel, u32 halmac_start_addr,
		      u32 halmac_fifo_dump_size, u8 *fifo_map)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DUMP_FIFO);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_fifo_sel == HAL_FIFO_SEL_TX &&
	    (halmac_start_addr + halmac_fifo_dump_size) >
		    halmac_adapter->hw_config_info.tx_fifo_size) {
		pr_err("TX fifo dump size is too large\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (halmac_fifo_sel == HAL_FIFO_SEL_RX &&
	    (halmac_start_addr + halmac_fifo_dump_size) >
		    halmac_adapter->hw_config_info.rx_fifo_size) {
		pr_err("RX fifo dump size is too large\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if ((halmac_fifo_dump_size & (4 - 1)) != 0) {
		pr_err("halmac_fifo_dump_size shall 4byte align\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (!fifo_map) {
		pr_err("fifo_map address is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	status = halmac_buffer_read_88xx(halmac_adapter, halmac_start_addr,
					 halmac_fifo_dump_size, halmac_fifo_sel,
					 fifo_map);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_buffer_read_88xx error = %x\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_fifo_size_88xx() - get fifo size
 * @halmac_adapter : the adapter of halmac
 * @halmac_fifo_sel : FIFO selection
 * Author : Ivan Lin/KaiYuan Chang
 * Return : u32
 * More details of status code can be found in prototype document
 */
u32 halmac_get_fifo_size_88xx(struct halmac_adapter *halmac_adapter,
			      enum hal_fifo_sel halmac_fifo_sel)
{
	u32 fifo_size = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_FIFO_SIZE);

	if (halmac_fifo_sel == HAL_FIFO_SEL_TX)
		fifo_size = halmac_adapter->hw_config_info.tx_fifo_size;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_RX)
		fifo_size = halmac_adapter->hw_config_info.rx_fifo_size;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_RSVD_PAGE)
		fifo_size =
			((halmac_adapter->hw_config_info.tx_fifo_size >> 7) -
			 halmac_adapter->txff_allocation.rsvd_pg_bndy)
			<< 7;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_REPORT)
		fifo_size = 65536;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_LLT)
		fifo_size = 65536;

	return fifo_size;
}

/**
 * halmac_cfg_txbf_88xx() - enable/disable specific user's txbf
 * @halmac_adapter : the adapter of halmac
 * @userid : su bfee userid = 0 or 1 to apply TXBF
 * @bw : the sounding bandwidth
 * @txbf_en : 0: disable TXBF, 1: enable TXBF
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_txbf_88xx(struct halmac_adapter *halmac_adapter, u8 userid,
		     enum halmac_bw bw, u8 txbf_en)
{
	u16 temp42C = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_TXBF);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (txbf_en) {
		switch (bw) {
		case HALMAC_BW_80:
			temp42C |= BIT_R_TXBF0_80M;
		case HALMAC_BW_40:
			temp42C |= BIT_R_TXBF0_40M;
		case HALMAC_BW_20:
			temp42C |= BIT_R_TXBF0_20M;
			break;
		default:
			pr_err("%s invalid TXBF BW setting 0x%x of userid %d\n",
			       __func__, bw, userid);
			return HALMAC_RET_INVALID_SOUNDING_SETTING;
		}
	}

	switch (userid) {
	case 0:
		temp42C |=
			HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL) &
			~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_TXBF_CTRL, temp42C);
		break;
	case 1:
		temp42C |=
			HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL + 2) &
			~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_TXBF_CTRL + 2, temp42C);
		break;
	default:
		pr_err("%s invalid userid %d\n", __func__, userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s, txbf_en = %x <==========\n", __func__,
			txbf_en);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_mumimo_88xx() -config mumimo
 * @halmac_adapter : the adapter of halmac
 * @cfgmu : parameters to configure MU PPDU Tx/Rx
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_mumimo_88xx(struct halmac_adapter *halmac_adapter,
		       struct halmac_cfg_mumimo_para *cfgmu)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u8 i, idx, id0, id1, gid, mu_tab_sel;
	u8 mu_tab_valid = 0;
	u32 gid_valid[6] = {0};
	u8 temp14C0 = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_MUMIMO);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (cfgmu->role == HAL_BFEE) {
		/*config MU BFEE*/
		temp14C0 = HALMAC_REG_READ_8(halmac_adapter, REG_MU_TX_CTL) &
			   ~BIT_MASK_R_MU_TABLE_VALID;
		/*enable MU table 0 and 1, disable MU TX*/
		HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL,
				   (temp14C0 | BIT(0) | BIT(1)) & ~(BIT(7)));

		/*config GID valid table and user position table*/
		mu_tab_sel =
			HALMAC_REG_READ_8(halmac_adapter, REG_MU_TX_CTL + 1) &
			~(BIT(0) | BIT(1) | BIT(2));
		for (i = 0; i < 2; i++) {
			HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL + 1,
					   mu_tab_sel | i);
			HALMAC_REG_WRITE_32(halmac_adapter, REG_MU_STA_GID_VLD,
					    cfgmu->given_gid_tab[i]);
			HALMAC_REG_WRITE_32(halmac_adapter,
					    REG_MU_STA_USER_POS_INFO,
					    cfgmu->given_user_pos[i * 2]);
			HALMAC_REG_WRITE_32(halmac_adapter,
					    REG_MU_STA_USER_POS_INFO + 4,
					    cfgmu->given_user_pos[i * 2 + 1]);
		}
	} else {
		/*config MU BFER*/
		if (!cfgmu->mu_tx_en) {
			HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL,
					   HALMAC_REG_READ_8(halmac_adapter,
							     REG_MU_TX_CTL) &
						   ~(BIT(7)));
			HALMAC_RT_TRACE(
				driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
				"%s disable mu tx <==========\n", __func__);
			return HALMAC_RET_SUCCESS;
		}

		/*Transform BB grouping bitmap[14:0] to MAC GID_valid table*/
		for (idx = 0; idx < 15; idx++) {
			if (idx < 5) {
				/*group_bitmap bit0~4, MU_STA0 with MUSTA1~5*/
				id0 = 0;
				id1 = (u8)(idx + 1);
			} else if (idx < 9) {
				/*group_bitmap bit5~8, MU_STA1 with MUSTA2~5*/
				id0 = 1;
				id1 = (u8)(idx - 3);
			} else if (idx < 12) {
				/*group_bitmap bit9~11, MU_STA2 with MUSTA3~5*/
				id0 = 2;
				id1 = (u8)(idx - 6);
			} else if (idx < 14) {
				/*group_bitmap bit12~13, MU_STA3 with MUSTA4~5*/
				id0 = 3;
				id1 = (u8)(idx - 8);
			} else {
				/*group_bitmap bit14, MU_STA4 with MUSTA5*/
				id0 = 4;
				id1 = (u8)(idx - 9);
			}
			if (cfgmu->grouping_bitmap & BIT(idx)) {
				/*Pair 1*/
				gid = (idx << 1) + 1;
				gid_valid[id0] |= (BIT(gid));
				gid_valid[id1] |= (BIT(gid));
				/*Pair 2*/
				gid += 1;
				gid_valid[id0] |= (BIT(gid));
				gid_valid[id1] |= (BIT(gid));
			} else {
				/*Pair 1*/
				gid = (idx << 1) + 1;
				gid_valid[id0] &= ~(BIT(gid));
				gid_valid[id1] &= ~(BIT(gid));
				/*Pair 2*/
				gid += 1;
				gid_valid[id0] &= ~(BIT(gid));
				gid_valid[id1] &= ~(BIT(gid));
			}
		}

		/*set MU STA GID valid TABLE*/
		mu_tab_sel =
			HALMAC_REG_READ_8(halmac_adapter, REG_MU_TX_CTL + 1) &
			~(BIT(0) | BIT(1) | BIT(2));
		for (idx = 0; idx < 6; idx++) {
			HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL + 1,
					   idx | mu_tab_sel);
			HALMAC_REG_WRITE_32(halmac_adapter, REG_MU_STA_GID_VLD,
					    gid_valid[idx]);
		}

		/*To validate the sounding successful MU STA and enable MU TX*/
		for (i = 0; i < 6; i++) {
			if (cfgmu->sounding_sts[i])
				mu_tab_valid |= BIT(i);
		}
		HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL,
				   mu_tab_valid | BIT(7));
	}
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_sounding_88xx() - configure general sounding
 * @halmac_adapter : the adapter of halmac
 * @role : driver's role, BFer or BFee
 * @datarate : set ndpa tx rate if driver is BFer, or set csi response rate
 *             if driver is BFee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_sounding_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_snd_role role,
			 enum halmac_data_rate datarate)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_SOUNDING);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_WRITE_32(
			halmac_adapter, REG_TXBF_CTRL,
			HALMAC_REG_READ_32(halmac_adapter, REG_TXBF_CTRL) |
				BIT_R_ENABLE_NDPA | BIT_USE_NDPA_PARAMETER |
				BIT_R_EN_NDPA_INT | BIT_DIS_NDP_BFEN);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_NDPA_RATE, datarate);
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_NDPA_OPT_CTRL,
			HALMAC_REG_READ_8(halmac_adapter, REG_NDPA_OPT_CTRL) &
				(~(BIT(0) | BIT(1))));
		/*service file length 2 bytes; fix non-STA1 csi start offset */
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SND_PTCL_CTRL + 1,
				   0x2 | BIT(7));
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SND_PTCL_CTRL + 2, 0x2);
		break;
	case HAL_BFEE:
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SND_PTCL_CTRL, 0xDB);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SND_PTCL_CTRL + 3, 0x50);
		/*use ndpa rx rate to decide csi rate*/
		HALMAC_REG_WRITE_8(halmac_adapter, REG_BBPSF_CTRL + 3,
				   HALMAC_OFDM54 | BIT(6));
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_RRSR,
			HALMAC_REG_READ_16(halmac_adapter, REG_RRSR) |
				BIT(datarate));
		/*RXFF do not accept BF Rpt Poll, avoid CSI crc error*/
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_RXFLTMAP1,
			HALMAC_REG_READ_8(halmac_adapter, REG_RXFLTMAP1) &
				(~(BIT(4))));
		/*FWFF do not accept BF Rpt Poll, avoid CSI crc error*/
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_RXFLTMAP4,
			HALMAC_REG_READ_8(halmac_adapter, REG_RXFLTMAP4) &
				(~(BIT(4))));
		break;
	default:
		pr_err("%s invalid role\n", __func__);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_del_sounding_88xx() - reset general sounding
 * @halmac_adapter : the adapter of halmac
 * @role : driver's role, BFer or BFee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_del_sounding_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_snd_role role)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DEL_SOUNDING);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_WRITE_8(halmac_adapter, REG_TXBF_CTRL + 3, 0);
		break;
	case HAL_BFEE:
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SND_PTCL_CTRL, 0);
		break;
	default:
		pr_err("%s invalid role\n", __func__);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_init_88xx() - config SU beamformee's registers
 * @halmac_adapter : the adapter of halmac
 * @userid : SU bfee userid = 0 or 1 to be added
 * @paid : partial AID of this bfee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_su_bfee_entry_init_88xx(struct halmac_adapter *halmac_adapter, u8 userid,
			       u16 paid)
{
	u16 temp42C = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_SU_BFEE_ENTRY_INIT);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (userid) {
	case 0:
		temp42C = HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL) &
			  ~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M |
			    BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_TXBF_CTRL,
				    temp42C | paid);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_ASSOCIATED_BFMEE_SEL,
				    paid);
		break;
	case 1:
		temp42C =
			HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL + 2) &
			~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M |
			  BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_TXBF_CTRL + 2,
				    temp42C | paid);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMEE_SEL + 2,
				    paid | BIT(9));
		break;
	default:
		pr_err("%s invalid userid %d\n", __func__,
		       userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_init_88xx() - config SU beamformer's registers
 * @halmac_adapter : the adapter of halmac
 * @su_bfer_init : parameters to configure SU BFER entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_su_bfer_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_su_bfer_init_para *su_bfer_init)
{
	u16 mac_address_H;
	u32 mac_address_L;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_SU_BFER_ENTRY_INIT);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	/* mac_address_L = bfer_address.address_l_h.address_low; */
	/* mac_address_H = bfer_address.address_l_h.address_high; */

	mac_address_L = le32_to_cpu(
		su_bfer_init->bfer_address.address_l_h.le_address_low);
	mac_address_H = le16_to_cpu(
		su_bfer_init->bfer_address.address_l_h.le_address_high);

	switch (su_bfer_init->userid) {
	case 0:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO,
				    mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMER0_INFO + 4,
				    mac_address_H);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMER0_INFO + 6,
				    su_bfer_init->paid);
		HALMAC_REG_WRITE_16(halmac_adapter, REG_TX_CSI_RPT_PARAM_BW20,
				    su_bfer_init->csi_para);
		break;
	case 1:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER1_INFO,
				    mac_address_L);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMER1_INFO + 4,
				    mac_address_H);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMER1_INFO + 6,
				    su_bfer_init->paid);
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_TX_CSI_RPT_PARAM_BW20 + 2,
				    su_bfer_init->csi_para);
		break;
	default:
		pr_err("%s invalid userid %d\n", __func__,
		       su_bfer_init->userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfee_entry_init_88xx() - config MU beamformee's registers
 * @halmac_adapter : the adapter of halmac
 * @mu_bfee_init : parameters to configure MU BFEE entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mu_bfee_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_mu_bfee_init_para *mu_bfee_init)
{
	u16 temp168X = 0, temp14C0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_MU_BFEE_ENTRY_INIT);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	temp168X |= mu_bfee_init->paid | BIT(9);
	HALMAC_REG_WRITE_16(halmac_adapter, (0x1680 + mu_bfee_init->userid * 2),
			    temp168X);

	temp14C0 = HALMAC_REG_READ_16(halmac_adapter, REG_MU_TX_CTL) &
		   ~(BIT(8) | BIT(9) | BIT(10));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_MU_TX_CTL,
			    temp14C0 | ((mu_bfee_init->userid - 2) << 8));
	HALMAC_REG_WRITE_32(halmac_adapter, REG_MU_STA_GID_VLD, 0);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_MU_STA_USER_POS_INFO,
			    mu_bfee_init->user_position_l);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_MU_STA_USER_POS_INFO + 4,
			    mu_bfee_init->user_position_h);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfer_entry_init_88xx() - config MU beamformer's registers
 * @halmac_adapter : the adapter of halmac
 * @mu_bfer_init : parameters to configure MU BFER entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mu_bfer_entry_init_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_mu_bfer_init_para *mu_bfer_init)
{
	u16 temp1680 = 0;
	u16 mac_address_H;
	u32 mac_address_L;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_MU_BFER_ENTRY_INIT);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	mac_address_L =
	    le32_to_cpu(mu_bfer_init->bfer_address.address_l_h.le_address_low);
	mac_address_H =
	    le16_to_cpu(mu_bfer_init->bfer_address.address_l_h.le_address_high);

	HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO,
			    mac_address_L);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4,
			    mac_address_H);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 6,
			    mu_bfer_init->paid);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_TX_CSI_RPT_PARAM_BW20,
			    mu_bfer_init->csi_para);

	temp1680 = HALMAC_REG_READ_16(halmac_adapter, 0x1680) & 0xC000;
	temp1680 |= mu_bfer_init->my_aid | (mu_bfer_init->csi_length_sel << 12);
	HALMAC_REG_WRITE_16(halmac_adapter, 0x1680, temp1680);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_del_88xx() - reset SU beamformee's registers
 * @halmac_adapter : the adapter of halmac
 * @userid : the SU BFee userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_su_bfee_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SU_BFEE_ENTRY_DEL);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (userid) {
	case 0:
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_TXBF_CTRL,
			HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL) &
				~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M |
				  BIT_R_TXBF0_40M | BIT_R_TXBF0_80M));
		HALMAC_REG_WRITE_16(halmac_adapter, REG_ASSOCIATED_BFMEE_SEL,
				    0);
		break;
	case 1:
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_TXBF_CTRL + 2,
			HALMAC_REG_READ_16(halmac_adapter, REG_TXBF_CTRL + 2) &
				~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M |
				  BIT_R_TXBF0_40M | BIT_R_TXBF0_80M));
		HALMAC_REG_WRITE_16(halmac_adapter,
				    REG_ASSOCIATED_BFMEE_SEL + 2, 0);
		break;
	default:
		pr_err("%s invalid userid %d\n", __func__,
		       userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_del_88xx() - reset SU beamformer's registers
 * @halmac_adapter : the adapter of halmac
 * @userid : the SU BFer userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_su_bfer_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SU_BFER_ENTRY_DEL);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (userid) {
	case 0:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO,
				    0);
		HALMAC_REG_WRITE_32(halmac_adapter,
				    REG_ASSOCIATED_BFMER0_INFO + 4, 0);
		break;
	case 1:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER1_INFO,
				    0);
		HALMAC_REG_WRITE_32(halmac_adapter,
				    REG_ASSOCIATED_BFMER1_INFO + 4, 0);
		break;
	default:
		pr_err("%s invalid userid %d\n", __func__,
		       userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfee_entry_del_88xx() - reset MU beamformee's registers
 * @halmac_adapter : the adapter of halmac
 * @userid : the MU STA userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mu_bfee_entry_del_88xx(struct halmac_adapter *halmac_adapter, u8 userid)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_MU_BFEE_ENTRY_DEL);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_16(halmac_adapter, 0x1680 + userid * 2, 0);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL,
			   HALMAC_REG_READ_8(halmac_adapter, REG_MU_TX_CTL) &
				   ~(BIT(userid - 2)));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfer_entry_del_88xx() -reset MU beamformer's registers
 * @halmac_adapter : the adapter of halmac
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mu_bfer_entry_del_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_MU_BFER_ENTRY_DEL);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO, 0);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4, 0);
	HALMAC_REG_WRITE_16(halmac_adapter, 0x1680, 0);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_MU_TX_CTL, 0);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_add_ch_info_88xx() -add channel information
 * @halmac_adapter : the adapter of halmac
 * @ch_info : channel information
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_add_ch_info_88xx(struct halmac_adapter *halmac_adapter,
			struct halmac_ch_info *ch_info)
{
	void *driver_adapter = NULL;
	struct halmac_cs_info *ch_sw_info;
	enum halmac_scan_cmd_construct_state state_scan;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	ch_sw_info = &halmac_adapter->ch_sw_info;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	if (halmac_adapter->halmac_state.dlfw_state != HALMAC_GEN_INFO_SENT) {
		pr_err("[ERR]%s: gen_info is not send to FW!!!!\n", __func__);
		return HALMAC_RET_GEN_INFO_NOT_SENT;
	}

	state_scan = halmac_query_scan_curr_state_88xx(halmac_adapter);
	if (state_scan != HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED &&
	    state_scan != HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_WARNING,
				"[WARN]Scan machine fail(add ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (!ch_sw_info->ch_info_buf) {
		ch_sw_info->ch_info_buf =
			kzalloc(HALMAC_EXTRA_INFO_BUFF_SIZE_88XX, GFP_KERNEL);
		if (!ch_sw_info->ch_info_buf)
			return HALMAC_RET_NULL_POINTER;
		ch_sw_info->ch_info_buf_w = ch_sw_info->ch_info_buf;
		ch_sw_info->buf_size = HALMAC_EXTRA_INFO_BUFF_SIZE_88XX;
		ch_sw_info->avai_buf_size = HALMAC_EXTRA_INFO_BUFF_SIZE_88XX;
		ch_sw_info->total_size = 0;
		ch_sw_info->extra_info_en = 0;
		ch_sw_info->ch_num = 0;
	}

	if (ch_sw_info->extra_info_en == 1) {
		pr_err("[ERR]%s: construct sequence wrong!!\n", __func__);
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->avai_buf_size < 4) {
		pr_err("[ERR]%s: no available buffer!!\n", __func__);
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (halmac_transition_scan_state_88xx(
		    halmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	CHANNEL_INFO_SET_CHANNEL(ch_sw_info->ch_info_buf_w, ch_info->channel);
	CHANNEL_INFO_SET_PRI_CH_IDX(ch_sw_info->ch_info_buf_w,
				    ch_info->pri_ch_idx);
	CHANNEL_INFO_SET_BANDWIDTH(ch_sw_info->ch_info_buf_w, ch_info->bw);
	CHANNEL_INFO_SET_TIMEOUT(ch_sw_info->ch_info_buf_w, ch_info->timeout);
	CHANNEL_INFO_SET_ACTION_ID(ch_sw_info->ch_info_buf_w,
				   ch_info->action_id);
	CHANNEL_INFO_SET_CH_EXTRA_INFO(ch_sw_info->ch_info_buf_w,
				       ch_info->extra_info);

	ch_sw_info->avai_buf_size = ch_sw_info->avai_buf_size - 4;
	ch_sw_info->total_size = ch_sw_info->total_size + 4;
	ch_sw_info->ch_num++;
	ch_sw_info->extra_info_en = ch_info->extra_info;
	ch_sw_info->ch_info_buf_w = ch_sw_info->ch_info_buf_w + 4;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_add_extra_ch_info_88xx() -add extra channel information
 * @halmac_adapter : the adapter of halmac
 * @ch_extra_info : extra channel information
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_add_extra_ch_info_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_ch_extra_info *ch_extra_info)
{
	void *driver_adapter = NULL;
	struct halmac_cs_info *ch_sw_info;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_ADD_EXTRA_CH_INFO);

	driver_adapter = halmac_adapter->driver_adapter;
	ch_sw_info = &halmac_adapter->ch_sw_info;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (!ch_sw_info->ch_info_buf) {
		pr_err("%s: NULL==ch_sw_info->ch_info_buf!!\n", __func__);
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->extra_info_en == 0) {
		pr_err("%s: construct sequence wrong!!\n", __func__);
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->avai_buf_size <
	    (u32)(ch_extra_info->extra_info_size + 2)) {
		/* +2: ch_extra_info_id, ch_extra_info, ch_extra_info_size
		 * are totally 2Byte
		 */
		pr_err("%s: no available buffer!!\n", __func__);
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (halmac_query_scan_curr_state_88xx(halmac_adapter) !=
	    HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Scan machine fail(add extra ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_transition_scan_state_88xx(
		    halmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	CH_EXTRA_INFO_SET_CH_EXTRA_INFO_ID(ch_sw_info->ch_info_buf_w,
					   ch_extra_info->extra_action_id);
	CH_EXTRA_INFO_SET_CH_EXTRA_INFO(ch_sw_info->ch_info_buf_w,
					ch_extra_info->extra_info);
	CH_EXTRA_INFO_SET_CH_EXTRA_INFO_SIZE(ch_sw_info->ch_info_buf_w,
					     ch_extra_info->extra_info_size);
	memcpy(ch_sw_info->ch_info_buf_w + 2, ch_extra_info->extra_info_data,
	       ch_extra_info->extra_info_size);

	ch_sw_info->avai_buf_size = ch_sw_info->avai_buf_size -
				    (2 + ch_extra_info->extra_info_size);
	ch_sw_info->total_size =
		ch_sw_info->total_size + (2 + ch_extra_info->extra_info_size);
	ch_sw_info->extra_info_en = ch_extra_info->extra_info;
	ch_sw_info->ch_info_buf_w = ch_sw_info->ch_info_buf_w +
				    (2 + ch_extra_info->extra_info_size);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_ctrl_ch_switch_88xx() -send channel switch cmd
 * @halmac_adapter : the adapter of halmac
 * @cs_option : channel switch config
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_ctrl_ch_switch_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_ch_switch_option *cs_option)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_scan_cmd_construct_state state_scan;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.scan_state_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CTRL_CH_SWITCH);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s  cs_option->switch_en = %d==========>\n", __func__,
			cs_option->switch_en);

	if (!cs_option->switch_en)
		*process_status = HALMAC_CMD_PROCESS_IDLE;

	if (*process_status == HALMAC_CMD_PROCESS_SENDING ||
	    *process_status == HALMAC_CMD_PROCESS_RCVD) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(ctrl ch switch)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	state_scan = halmac_query_scan_curr_state_88xx(halmac_adapter);
	if (cs_option->switch_en) {
		if (state_scan != HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C,
					DBG_DMESG,
					"%s(on)  invalid in state %x\n",
					__func__, state_scan);
			return HALMAC_RET_ERROR_STATE;
		}
	} else {
		if (state_scan != HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED) {
			HALMAC_RT_TRACE(
				driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"%s(off)  invalid in state %x\n", __func__,
				state_scan);
			return HALMAC_RET_ERROR_STATE;
		}
	}

	status = halmac_func_ctrl_ch_switch_88xx(halmac_adapter, cs_option);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_ctrl_ch_switch FAIL = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_clear_ch_info_88xx() -clear channel information
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_clear_ch_info_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CLEAR_CH_INFO);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_query_scan_curr_state_88xx(halmac_adapter) ==
	    HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Scan machine fail(clear ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_transition_scan_state_88xx(
		    halmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	kfree(halmac_adapter->ch_sw_info.ch_info_buf);
	halmac_adapter->ch_sw_info.ch_info_buf = NULL;
	halmac_adapter->ch_sw_info.ch_info_buf_w = NULL;
	halmac_adapter->ch_sw_info.extra_info_en = 0;
	halmac_adapter->ch_sw_info.buf_size = 0;
	halmac_adapter->ch_sw_info.avai_buf_size = 0;
	halmac_adapter->ch_sw_info.total_size = 0;
	halmac_adapter->ch_sw_info.ch_num = 0;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_p2pps_88xx(struct halmac_adapter *halmac_adapter,
					 struct halmac_p2pps *p2p_ps)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 6)
		return HALMAC_RET_FW_NO_SUPPORT;

	driver_adapter = halmac_adapter->driver_adapter;

	status = halmac_func_p2pps_88xx(halmac_adapter, p2p_ps);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("[ERR]halmac_p2pps FAIL = %x!!\n", status);
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_p2pps_88xx(struct halmac_adapter *halmac_adapter,
		       struct halmac_p2pps *p2p_ps)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = halmac_adapter->driver_adapter;
	struct halmac_api *halmac_api;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]halmac_p2pps !!\n");

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	P2PPS_SET_OFFLOAD_EN(h2c_buff, p2p_ps->offload_en);
	P2PPS_SET_ROLE(h2c_buff, p2p_ps->role);
	P2PPS_SET_CTWINDOW_EN(h2c_buff, p2p_ps->ctwindow_en);
	P2PPS_SET_NOA_EN(h2c_buff, p2p_ps->noa_en);
	P2PPS_SET_NOA_SEL(h2c_buff, p2p_ps->noa_sel);
	P2PPS_SET_ALLSTASLEEP(h2c_buff, p2p_ps->all_sta_sleep);
	P2PPS_SET_DISCOVERY(h2c_buff, p2p_ps->discovery);
	P2PPS_SET_P2P_PORT_ID(h2c_buff, p2p_ps->p2p_port_id);
	P2PPS_SET_P2P_GROUP(h2c_buff, p2p_ps->p2p_group);
	P2PPS_SET_P2P_MACID(h2c_buff, p2p_ps->p2p_macid);

	P2PPS_SET_CTWINDOW_LENGTH(h2c_buff, p2p_ps->ctwindow_length);

	P2PPS_SET_NOA_DURATION_PARA(h2c_buff, p2p_ps->noa_duration_para);
	P2PPS_SET_NOA_INTERVAL_PARA(h2c_buff, p2p_ps->noa_interval_para);
	P2PPS_SET_NOA_START_TIME_PARA(h2c_buff, p2p_ps->noa_start_time_para);
	P2PPS_SET_NOA_COUNT_PARA(h2c_buff, p2p_ps->noa_count_para);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_P2PPS;
	h2c_header_info.content_size = 24;
	h2c_header_info.ack = false;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, false);

	if (status != HALMAC_RET_SUCCESS)
		pr_err("[ERR]halmac_send_h2c_p2pps_88xx Fail = %x!!\n", status);

	return status;
}

/**
 * halmac_send_general_info_88xx() -send general information to FW
 * @halmac_adapter : the adapter of halmac
 * @general_info : general information
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_send_general_info_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_general_info *general_info)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (halmac_adapter->fw_version.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SEND_GENERAL_INFO);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (halmac_adapter->halmac_state.dlfw_state == HALMAC_DLFW_NONE) {
		pr_err("%s Fail due to DLFW NONE!!\n", __func__);
		return HALMAC_RET_DLFW_FAIL;
	}

	status = halmac_func_send_general_info_88xx(halmac_adapter,
						    general_info);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_general_info error = %x\n", status);
		return status;
	}

	if (halmac_adapter->halmac_state.dlfw_state == HALMAC_DLFW_DONE)
		halmac_adapter->halmac_state.dlfw_state = HALMAC_GEN_INFO_SENT;

	halmac_adapter->gen_info_valid = true;
	memcpy(&halmac_adapter->general_info, general_info,
	       sizeof(struct halmac_general_info));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_start_iqk_88xx() -trigger FW IQK
 * @halmac_adapter : the adapter of halmac
 * @iqk_para : IQK parameter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_start_iqk_88xx(struct halmac_adapter *halmac_adapter,
		      struct halmac_iqk_para_ *iqk_para)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_num = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.iqk_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_START_IQK);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(iqk)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	IQK_SET_CLEAR(h2c_buff, iqk_para->clear);
	IQK_SET_SEGMENT_IQK(h2c_buff, iqk_para->segment_iqk);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_IQK;
	h2c_header_info.content_size = 1;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_num);

	halmac_adapter->halmac_state.iqk_set.seq_num = h2c_seq_num;

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_ctrl_pwr_tracking_88xx() -trigger FW power tracking
 * @halmac_adapter : the adapter of halmac
 * @pwr_tracking_opt : power tracking option
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_ctrl_pwr_tracking_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_pwr_tracking_option *pwr_tracking_opt)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.power_tracking_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CTRL_PWR_TRACKING);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_start_iqk_88xx ==========>\n");

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(pwr tracking)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	POWER_TRACKING_SET_TYPE(h2c_buff, pwr_tracking_opt->type);
	POWER_TRACKING_SET_BBSWING_INDEX(h2c_buff,
					 pwr_tracking_opt->bbswing_index);
	POWER_TRACKING_SET_ENABLE_A(
		h2c_buff,
		pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_A(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A]
				  .tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_A(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A]
				  .pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_A(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A]
				  .tssi_value);
	POWER_TRACKING_SET_ENABLE_B(
		h2c_buff,
		pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_B(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B]
				  .tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_B(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B]
				  .pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_B(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B]
				  .tssi_value);
	POWER_TRACKING_SET_ENABLE_C(
		h2c_buff,
		pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_C(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C]
				  .tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_C(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C]
				  .pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_C(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C]
				  .tssi_value);
	POWER_TRACKING_SET_ENABLE_D(
		h2c_buff,
		pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_D(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D]
				  .tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_D(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D]
				  .pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_D(
		h2c_buff, pwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D]
				  .tssi_value);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_POWER_TRACKING;
	h2c_header_info.content_size = 20;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	halmac_adapter->halmac_state.power_tracking_set.seq_num = h2c_seq_mum;

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_start_iqk_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_query_status_88xx() -query the offload feature status
 * @halmac_adapter : the adapter of halmac
 * @feature_id : feature_id
 * @process_status : feature_status
 * @data : data buffer
 * @size : data size
 *
 * Note :
 * If user wants to know the data size, use can allocate zero
 * size buffer first. If this size less than the data size, halmac
 * will return  HALMAC_RET_BUFFER_TOO_SMALL. User need to
 * re-allocate data buffer with correct data size.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_query_status_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_feature_id feature_id,
			 enum halmac_cmd_process_status *process_status,
			 u8 *data, u32 *size)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_QUERY_STATE);

	driver_adapter = halmac_adapter->driver_adapter;

	if (!process_status) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"null pointer!!\n");
		return HALMAC_RET_NULL_POINTER;
	}

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		status = halmac_query_cfg_para_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		status = halmac_query_dump_physical_efuse_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		status = halmac_query_dump_logical_efuse_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		status = halmac_query_channel_switch_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		status = halmac_query_update_packet_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_IQK:
		status = halmac_query_iqk_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		status = halmac_query_power_tracking_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	case HALMAC_FEATURE_PSD:
		status = halmac_query_psd_status_88xx(
			halmac_adapter, process_status, data, size);
		break;
	default:
		pr_err("%s invalid feature id %d\n", __func__,
		       feature_id);
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	return status;
}

/**
 * halmac_reset_feature_88xx() -reset async api cmd status
 * @halmac_adapter : the adapter of halmac
 * @feature_id : feature_id
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status.
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reset_feature_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_feature_id feature_id)
{
	void *driver_adapter = NULL;
	struct halmac_state *state = &halmac_adapter->halmac_state;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_RESET_FEATURE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		state->cfg_para_state_set.process_status =
			HALMAC_CMD_PROCESS_IDLE;
		state->cfg_para_state_set.cfg_para_cmd_construct_state =
			HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		state->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		state->efuse_state_set.efuse_cmd_construct_state =
			HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		state->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		state->scan_state_set.scan_cmd_construct_state =
			HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		state->update_packet_set.process_status =
			HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_ALL:
		state->cfg_para_state_set.process_status =
			HALMAC_CMD_PROCESS_IDLE;
		state->cfg_para_state_set.cfg_para_cmd_construct_state =
			HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
		state->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		state->efuse_state_set.efuse_cmd_construct_state =
			HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
		state->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		state->scan_state_set.scan_cmd_construct_state =
			HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
		state->update_packet_set.process_status =
			HALMAC_CMD_PROCESS_IDLE;
		break;
	default:
		pr_err("%s invalid feature id %d\n", __func__,
		       feature_id);
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_check_fw_status_88xx() -check fw status
 * @halmac_adapter : the adapter of halmac
 * @fw_status : fw status
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_check_fw_status_88xx(struct halmac_adapter *halmac_adapter,
			    bool *fw_status)
{
	u32 value32 = 0, value32_backup = 0, i = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CHECK_FW_STATUS);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	value32 = PLATFORM_REG_READ_32(driver_adapter, REG_FW_DBG6);

	if (value32 != 0) {
		pr_err("halmac_check_fw_status REG_FW_DBG6 !=0\n");
		*fw_status = false;
		return status;
	}

	value32_backup = PLATFORM_REG_READ_32(driver_adapter, REG_FW_DBG7);

	for (i = 0; i <= 10; i++) {
		value32 = PLATFORM_REG_READ_32(driver_adapter, REG_FW_DBG7);
		if (value32_backup != value32)
			break;

		if (i == 10) {
			pr_err("halmac_check_fw_status Polling FW PC fail\n");
			*fw_status = false;
			return status;
		}
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return status;
}

enum halmac_ret_status
halmac_dump_fw_dmem_88xx(struct halmac_adapter *halmac_adapter, u8 *dmem,
			 u32 *size)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DUMP_FW_DMEM);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return status;
}

/**
 * halmac_cfg_max_dl_size_88xx() - config max download FW size
 * @halmac_adapter : the adapter of halmac
 * @size : max download fw size
 *
 * Halmac uses this setting to set max packet size for
 * download FW.
 * If user has not called this API, halmac use default
 * setting for download FW
 * Note1 : size need multiple of 2
 * Note2 : max size is 31K
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_max_dl_size_88xx(struct halmac_adapter *halmac_adapter, u32 size)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_MAX_DL_SIZE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_FW, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (size > HALMAC_FW_CFG_MAX_DL_SIZE_MAX_88XX) {
		pr_err("size > HALMAC_FW_CFG_MAX_DL_SIZE_MAX!\n");
		return HALMAC_RET_CFG_DLFW_SIZE_FAIL;
	}

	if ((size & (2 - 1)) != 0) {
		pr_err("size is not power of 2!\n");
		return HALMAC_RET_CFG_DLFW_SIZE_FAIL;
	}

	halmac_adapter->max_download_size = size;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_FW, DBG_DMESG,
			"Cfg max size is : %X\n", size);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_FW, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_psd_88xx() - trigger fw psd
 * @halmac_adapter : the adapter of halmac
 * @start_psd : start PSD
 * @end_psd : end PSD
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_psd_88xx(struct halmac_adapter *halmac_adapter,
				       u16 start_psd, u16 end_psd)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.psd_set.process_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	if (halmac_fw_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_PSD);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (*process_status == HALMAC_CMD_PROCESS_SENDING) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Wait event(psd)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	kfree(halmac_adapter->halmac_state.psd_set.data);
	halmac_adapter->halmac_state.psd_set.data = (u8 *)NULL;

	halmac_adapter->halmac_state.psd_set.data_size = 0;
	halmac_adapter->halmac_state.psd_set.segment_size = 0;

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	PSD_SET_START_PSD(h2c_buff, start_psd);
	PSD_SET_END_PSD(h2c_buff, end_psd);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_PSD;
	h2c_header_info.content_size = 4;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_la_mode_88xx() - config la mode
 * @halmac_adapter : the adapter of halmac
 * @la_mode :
 *	disable : no TXFF space reserved for LA debug
 *	partial : partial TXFF space is reserved for LA debug
 *	full : all TXFF space is reserved for LA debug
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_la_mode_88xx(struct halmac_adapter *halmac_adapter,
			enum halmac_la_mode la_mode)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_LA_MODE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>la_mode = %d\n", __func__,
			la_mode);

	halmac_adapter->txff_allocation.la_mode = la_mode;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_fifo_expanding_mode_88xx() - rx fifo expanding
 * @halmac_adapter : the adapter of halmac
 * @la_mode :
 *	disable : normal mode
 *	1 block : Rx FIFO + 1 FIFO block; Tx fifo - 1 FIFO block
 *	2 block : Rx FIFO + 2 FIFO block; Tx fifo - 2 FIFO block
 *	3 block : Rx FIFO + 3 FIFO block; Tx fifo - 3 FIFO block
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_cfg_rx_fifo_expanding_mode_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_rx_fifo_expanding_mode rx_fifo_expanding_mode)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_RX_FIFO_EXPANDING_MODE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
		"%s ==========>rx_fifo_expanding_mode = %d\n", __func__,
		rx_fifo_expanding_mode);

	halmac_adapter->txff_allocation.rx_fifo_expanding_mode =
		rx_fifo_expanding_mode;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_config_security_88xx(struct halmac_adapter *halmac_adapter,
			    struct halmac_security_setting *sec_setting)
{
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;
	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_REG_WRITE_16(halmac_adapter, REG_CR,
			    (u16)(HALMAC_REG_READ_16(halmac_adapter, REG_CR) |
				  BIT_MAC_SEC_EN));

	if (sec_setting->tx_encryption == 1)
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SECCFG,
			HALMAC_REG_READ_8(halmac_adapter, REG_SECCFG) | BIT(2));
	else
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SECCFG,
			HALMAC_REG_READ_8(halmac_adapter, REG_SECCFG) &
				~(BIT(2)));

	if (sec_setting->rx_decryption == 1)
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SECCFG,
			HALMAC_REG_READ_8(halmac_adapter, REG_SECCFG) | BIT(3));
	else
		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SECCFG,
			HALMAC_REG_READ_8(halmac_adapter, REG_SECCFG) &
				~(BIT(3)));

	if (sec_setting->bip_enable == 1) {
		if (halmac_adapter->chip_id == HALMAC_CHIP_ID_8822B)
			return HALMAC_RET_BIP_NO_SUPPORT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

u8 halmac_get_used_cam_entry_num_88xx(struct halmac_adapter *halmac_adapter,
				      enum hal_security_type sec_type)
{
	u8 entry_num;
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s ==========>\n", __func__);

	switch (sec_type) {
	case HAL_SECURITY_TYPE_WEP40:
	case HAL_SECURITY_TYPE_WEP104:
	case HAL_SECURITY_TYPE_TKIP:
	case HAL_SECURITY_TYPE_AES128:
	case HAL_SECURITY_TYPE_GCMP128:
	case HAL_SECURITY_TYPE_GCMSMS4:
	case HAL_SECURITY_TYPE_BIP:
		entry_num = 1;
		break;
	case HAL_SECURITY_TYPE_WAPI:
	case HAL_SECURITY_TYPE_AES256:
	case HAL_SECURITY_TYPE_GCMP256:
		entry_num = 2;
		break;
	default:
		entry_num = 0;
		break;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s <==========\n", __func__);

	return entry_num;
}

enum halmac_ret_status
halmac_write_cam_88xx(struct halmac_adapter *halmac_adapter, u32 entry_index,
		      struct halmac_cam_entry_info *cam_entry_info)
{
	u32 i;
	u32 command = 0x80010000;
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;
	struct halmac_cam_entry_format *cam_entry_format = NULL;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	if (entry_index >= halmac_adapter->hw_config_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	if (cam_entry_info->key_id > 3)
		return HALMAC_RET_FAIL;

	cam_entry_format = kzalloc(sizeof(*cam_entry_format), GFP_KERNEL);
	if (!cam_entry_format)
		return HALMAC_RET_NULL_POINTER;

	cam_entry_format->key_id = cam_entry_info->key_id;
	cam_entry_format->valid = cam_entry_info->valid;
	memcpy(cam_entry_format->mac_address, cam_entry_info->mac_address, 6);
	memcpy(cam_entry_format->key, cam_entry_info->key, 16);

	switch (cam_entry_info->security_type) {
	case HAL_SECURITY_TYPE_NONE:
		cam_entry_format->type = 0;
		break;
	case HAL_SECURITY_TYPE_WEP40:
		cam_entry_format->type = 1;
		break;
	case HAL_SECURITY_TYPE_WEP104:
		cam_entry_format->type = 5;
		break;
	case HAL_SECURITY_TYPE_TKIP:
		cam_entry_format->type = 2;
		break;
	case HAL_SECURITY_TYPE_AES128:
		cam_entry_format->type = 4;
		break;
	case HAL_SECURITY_TYPE_WAPI:
		cam_entry_format->type = 6;
		break;
	case HAL_SECURITY_TYPE_AES256:
		cam_entry_format->type = 4;
		cam_entry_format->ext_sectype = 1;
		break;
	case HAL_SECURITY_TYPE_GCMP128:
		cam_entry_format->type = 7;
		break;
	case HAL_SECURITY_TYPE_GCMP256:
	case HAL_SECURITY_TYPE_GCMSMS4:
		cam_entry_format->type = 7;
		cam_entry_format->ext_sectype = 1;
		break;
	case HAL_SECURITY_TYPE_BIP:
		cam_entry_format->type = cam_entry_info->unicast == 1 ? 4 : 0;
		cam_entry_format->mgnt = 1;
		cam_entry_format->grp = cam_entry_info->unicast == 1 ? 0 : 1;
		break;
	default:
		kfree(cam_entry_format);
		return HALMAC_RET_FAIL;
	}

	for (i = 0; i < 8; i++) {
		HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMWRITE,
				    *((u32 *)cam_entry_format + i));
		HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMCMD,
				    command | ((entry_index << 3) + i));
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
				"[TRACE]1 - CAM entry format : %X\n",
				*((u32 *)cam_entry_format + i));
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
				"[TRACE]1 - REG_CAMCMD : %X\n",
				command | ((entry_index << 3) + i));
	}

	if (cam_entry_info->security_type == HAL_SECURITY_TYPE_WAPI ||
	    cam_entry_info->security_type == HAL_SECURITY_TYPE_AES256 ||
	    cam_entry_info->security_type == HAL_SECURITY_TYPE_GCMP256 ||
	    cam_entry_info->security_type == HAL_SECURITY_TYPE_GCMSMS4) {
		cam_entry_format->mic = 1;
		memcpy(cam_entry_format->key, cam_entry_info->key_ext, 16);

		for (i = 0; i < 8; i++) {
			HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMWRITE,
					    *((u32 *)cam_entry_format + i));
			HALMAC_REG_WRITE_32(
				halmac_adapter, REG_CAMCMD,
				command | (((entry_index + 1) << 3) + i));
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON,
					DBG_DMESG,
					"[TRACE]2 - CAM entry format : %X\n",
					*((u32 *)cam_entry_format + i));
			HALMAC_RT_TRACE(
				driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
				"[TRACE]2 - REG_CAMCMD : %X\n",
				command | (((entry_index + 1) << 3) + i));
		}
	}

	kfree(cam_entry_format);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_read_cam_entry_88xx(struct halmac_adapter *halmac_adapter,
			   u32 entry_index,
			   struct halmac_cam_entry_format *content)
{
	u32 i;
	u32 command = 0x80000000;
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (entry_index >= halmac_adapter->hw_config_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	for (i = 0; i < 8; i++) {
		HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMCMD,
				    command | ((entry_index << 3) + i));
		*((u32 *)content + i) =
			HALMAC_REG_READ_32(halmac_adapter, REG_CAMREAD);
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_clear_cam_entry_88xx(struct halmac_adapter *halmac_adapter,
			    u32 entry_index)
{
	u32 i;
	u32 command = 0x80010000;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_cam_entry_format *cam_entry_format;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]halmac_clear_security_cam_88xx ==========>\n");

	if (entry_index >= halmac_adapter->hw_config_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	cam_entry_format = kzalloc(sizeof(*cam_entry_format), GFP_KERNEL);
	if (!cam_entry_format)
		return HALMAC_RET_NULL_POINTER;

	for (i = 0; i < 8; i++) {
		HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMWRITE,
				    *((u32 *)cam_entry_format + i));
		HALMAC_REG_WRITE_32(halmac_adapter, REG_CAMCMD,
				    command | ((entry_index << 3) + i));
	}

	kfree(cam_entry_format);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]halmac_clear_security_cam_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_hw_value_88xx() -get hw config value
 * @halmac_adapter : the adapter of halmac
 * @hw_id : hw id for driver to query
 * @pvalue : hw value, reference table to get data type
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_hw_value_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_hw_id hw_id, void *pvalue)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_HW_VALUE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (!pvalue) {
		pr_err("%s (!pvalue)==========>\n", __func__);
		return HALMAC_RET_NULL_POINTER;
	}

	switch (hw_id) {
	case HALMAC_HW_RQPN_MAPPING:
		((struct halmac_rqpn_map *)pvalue)->dma_map_vo =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		((struct halmac_rqpn_map *)pvalue)->dma_map_vi =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		((struct halmac_rqpn_map *)pvalue)->dma_map_be =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		((struct halmac_rqpn_map *)pvalue)->dma_map_bk =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		((struct halmac_rqpn_map *)pvalue)->dma_map_mg =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		((struct halmac_rqpn_map *)pvalue)->dma_map_hi =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	case HALMAC_HW_EFUSE_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.efuse_size;
		break;
	case HALMAC_HW_EEPROM_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.eeprom_size;
		break;
	case HALMAC_HW_BT_BANK_EFUSE_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.bt_efuse_size;
		break;
	case HALMAC_HW_BT_BANK1_EFUSE_SIZE:
	case HALMAC_HW_BT_BANK2_EFUSE_SIZE:
		*(u32 *)pvalue = 0;
		break;
	case HALMAC_HW_TXFIFO_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.tx_fifo_size;
		break;
	case HALMAC_HW_RSVD_PG_BNDY:
		*(u16 *)pvalue =
			halmac_adapter->txff_allocation.rsvd_drv_pg_bndy;
		break;
	case HALMAC_HW_CAM_ENTRY_NUM:
		*(u8 *)pvalue = halmac_adapter->hw_config_info.cam_entry_num;
		break;
	case HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE: /*Remove later*/
		status = halmac_dump_logical_efuse_map_88xx(halmac_adapter,
							    HALMAC_EFUSE_R_DRV);
		if (status != HALMAC_RET_SUCCESS)
			return status;
		*(u32 *)pvalue = halmac_adapter->hw_config_info.efuse_size -
				 HALMAC_PROTECTED_EFUSE_SIZE_88XX -
				 halmac_adapter->efuse_end;
		break;
	case HALMAC_HW_IC_VERSION:
		*(u8 *)pvalue = halmac_adapter->chip_version;
		break;
	case HALMAC_HW_PAGE_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.page_size;
		break;
	case HALMAC_HW_TX_AGG_ALIGN_SIZE:
		*(u16 *)pvalue = halmac_adapter->hw_config_info.tx_align_size;
		break;
	case HALMAC_HW_RX_AGG_ALIGN_SIZE:
		*(u8 *)pvalue = 8;
		break;
	case HALMAC_HW_DRV_INFO_SIZE:
		*(u8 *)pvalue = halmac_adapter->drv_info_size;
		break;
	case HALMAC_HW_TXFF_ALLOCATION:
		memcpy(pvalue, &halmac_adapter->txff_allocation,
		       sizeof(struct halmac_txff_allocation));
		break;
	case HALMAC_HW_TX_DESC_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.txdesc_size;
		break;
	case HALMAC_HW_RX_DESC_SIZE:
		*(u32 *)pvalue = halmac_adapter->hw_config_info.rxdesc_size;
		break;
	default:
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_set_hw_value_88xx() -set hw config value
 * @halmac_adapter : the adapter of halmac
 * @hw_id : hw id for driver to config
 * @pvalue : hw value, reference table to get data type
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_set_hw_value_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_hw_id hw_id, void *pvalue)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_HW_VALUE);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (!pvalue) {
		pr_err("%s (!pvalue)==========>\n", __func__);
		return HALMAC_RET_NULL_POINTER;
	}

	switch (hw_id) {
	case HALMAC_HW_USB_MODE:
		status = halmac_set_usb_mode_88xx(
			halmac_adapter, *(enum halmac_usb_mode *)pvalue);
		if (status != HALMAC_RET_SUCCESS)
			return status;
		break;
	case HALMAC_HW_SEQ_EN:
		break;
	case HALMAC_HW_BANDWIDTH:
		halmac_cfg_bw_88xx(halmac_adapter, *(enum halmac_bw *)pvalue);
		break;
	case HALMAC_HW_CHANNEL:
		halmac_cfg_ch_88xx(halmac_adapter, *(u8 *)pvalue);
		break;
	case HALMAC_HW_PRI_CHANNEL_IDX:
		halmac_cfg_pri_ch_idx_88xx(halmac_adapter,
					   *(enum halmac_pri_ch_idx *)pvalue);
		break;
	case HALMAC_HW_EN_BB_RF:
		halmac_enable_bb_rf_88xx(halmac_adapter, *(u8 *)pvalue);
		break;
	case HALMAC_HW_SDIO_TX_PAGE_THRESHOLD:
		halmac_config_sdio_tx_page_threshold_88xx(
			halmac_adapter,
			(struct halmac_tx_page_threshold_info *)pvalue);
		break;
	case HALMAC_HW_AMPDU_CONFIG:
		halmac_config_ampdu_88xx(halmac_adapter,
					 (struct halmac_ampdu_config *)pvalue);
		break;
	default:
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_drv_rsvd_pg_num_88xx() -config reserved page number for driver
 * @halmac_adapter : the adapter of halmac
 * @pg_num : page number
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_drv_rsvd_pg_num_88xx(struct halmac_adapter *halmac_adapter,
				enum halmac_drv_rsvd_pg_num pg_num)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_DRV_RSVD_PG_NUM);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>pg_num = %d\n", __func__,
			pg_num);

	switch (pg_num) {
	case HALMAC_RSVD_PG_NUM16:
		halmac_adapter->txff_allocation.rsvd_drv_pg_num = 16;
		break;
	case HALMAC_RSVD_PG_NUM24:
		halmac_adapter->txff_allocation.rsvd_drv_pg_num = 24;
		break;
	case HALMAC_RSVD_PG_NUM32:
		halmac_adapter->txff_allocation.rsvd_drv_pg_num = 32;
		break;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_get_chip_version_88xx(struct halmac_adapter *halmac_adapter,
			     struct halmac_ver *version)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s ==========>\n", __func__);
	version->major_ver = (u8)HALMAC_MAJOR_VER_88XX;
	version->prototype_ver = (u8)HALMAC_PROTOTYPE_VER_88XX;
	version->minor_ver = (u8)HALMAC_MINOR_VER_88XX;
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_chk_txdesc_88xx() -check if the tx packet format is incorrect
 * @halmac_adapter : the adapter of halmac
 * @halmac_buf : tx Packet buffer, tx desc is included
 * @halmac_size : tx packet size
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_chk_txdesc_88xx(struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		       u32 halmac_size)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (GET_TX_DESC_BMC(halmac_buf))
		if (GET_TX_DESC_AGG_EN(halmac_buf))
			pr_err("TxDesc: Agg should not be set when BMC\n");

	if (halmac_size < (GET_TX_DESC_TXPKTSIZE(halmac_buf) +
			   GET_TX_DESC_OFFSET(halmac_buf)))
		pr_err("TxDesc: PktSize too small\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dl_drv_rsvd_page_88xx() - download packet to rsvd page
 * @halmac_adapter : the adapter of halmac
 * @pg_offset : page offset of driver's rsvd page
 * @halmac_buf : data to be downloaded, tx_desc is not included
 * @halmac_size : data size to be downloaded
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_dl_drv_rsvd_page_88xx(struct halmac_adapter *halmac_adapter,
			     u8 pg_offset, u8 *halmac_buf, u32 halmac_size)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status ret_status;
	u16 drv_pg_bndy = 0;
	u32 dl_pg_num = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DL_DRV_RSVD_PG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	/*check boundary and size valid*/
	dl_pg_num = halmac_size / halmac_adapter->hw_config_info.page_size +
		    ((halmac_size &
		      (halmac_adapter->hw_config_info.page_size - 1)) ?
			     1 :
			     0);
	if (pg_offset + dl_pg_num >
	    halmac_adapter->txff_allocation.rsvd_drv_pg_num) {
		pr_err("[ERROR] driver download offset or size error ==========>\n");
		return HALMAC_RET_DRV_DL_ERR;
	}

	/*update to target download boundary*/
	drv_pg_bndy =
		halmac_adapter->txff_allocation.rsvd_drv_pg_bndy + pg_offset;
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(drv_pg_bndy & BIT_MASK_BCN_HEAD_1_V1));

	ret_status = halmac_download_rsvd_page_88xx(halmac_adapter, halmac_buf,
						    halmac_size);

	/*restore to original bundary*/
	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_download_rsvd_page_88xx Fail = %x!!\n",
		       ret_status);
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_FIFOPAGE_CTRL_2,
			(u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
			      BIT_MASK_BCN_HEAD_1_V1));
		return ret_status;
	}

	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s < ==========\n", __func__);
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_csi_rate_88xx() - config CSI frame Tx rate
 * @halmac_adapter : the adapter of halmac
 * @rssi : rssi in decimal value
 * @current_rate : current CSI frame rate
 * @fixrate_en : enable to fix CSI frame in VHT rate, otherwise legacy OFDM rate
 * @new_rate : API returns the final CSI frame rate
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_csi_rate_88xx(struct halmac_adapter *halmac_adapter, u8 rssi,
			 u8 current_rate, u8 fixrate_en, u8 *new_rate)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u32 temp_csi_setting;
	u16 current_rrsr;
	enum halmac_ret_status ret_status;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_CSI_RATE);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_SND, DBG_DMESG,
			"<%s ==========>\n", __func__);

	temp_csi_setting = HALMAC_REG_READ_32(halmac_adapter, REG_BBPSF_CTRL) &
			   ~(BIT_MASK_WMAC_CSI_RATE << BIT_SHIFT_WMAC_CSI_RATE);

	current_rrsr = HALMAC_REG_READ_16(halmac_adapter, REG_RRSR);

	if (rssi >= 40) {
		if (current_rate != HALMAC_OFDM54) {
			HALMAC_REG_WRITE_16(halmac_adapter, REG_RRSR,
					    current_rrsr | BIT(HALMAC_OFDM54));
			HALMAC_REG_WRITE_32(
				halmac_adapter, REG_BBPSF_CTRL,
				temp_csi_setting |
					BIT_WMAC_CSI_RATE(HALMAC_OFDM54));
		}
		*new_rate = HALMAC_OFDM54;
		ret_status = HALMAC_RET_SUCCESS;
	} else {
		if (current_rate != HALMAC_OFDM24) {
			HALMAC_REG_WRITE_16(halmac_adapter, REG_RRSR,
					    current_rrsr &
						    ~(BIT(HALMAC_OFDM54)));
			HALMAC_REG_WRITE_32(
				halmac_adapter, REG_BBPSF_CTRL,
				temp_csi_setting |
					BIT_WMAC_CSI_RATE(HALMAC_OFDM24));
		}
		*new_rate = HALMAC_OFDM24;
		ret_status = HALMAC_RET_SUCCESS;
	}

	return ret_status;
}

/**
 * halmac_sdio_cmd53_4byte_88xx() - cmd53 only for 4byte len register IO
 * @halmac_adapter : the adapter of halmac
 * @enable : 1->CMD53 only use in 4byte reg, 0 : No limitation
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_sdio_cmd53_4byte_88xx(struct halmac_adapter *halmac_adapter,
			     enum halmac_sdio_cmd53_4byte_mode cmd53_4byte_mode)
{
	halmac_adapter->sdio_cmd53_4byte = cmd53_4byte_mode;

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_txfifo_is_empty_88xx() -check if txfifo is empty
 * @halmac_adapter : the adapter of halmac
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_txfifo_is_empty_88xx(struct halmac_adapter *halmac_adapter, u32 chk_num)
{
	u32 counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s ==========>\n", __func__);

	counter = (chk_num <= 10) ? 10 : chk_num;
	do {
		if (HALMAC_REG_READ_8(halmac_adapter, REG_TXPKT_EMPTY) != 0xFF)
			return HALMAC_RET_TXFIFO_NO_EMPTY;

		if ((HALMAC_REG_READ_8(halmac_adapter, REG_TXPKT_EMPTY + 1) &
		     0x07) != 0x07)
			return HALMAC_RET_TXFIFO_NO_EMPTY;
		counter--;

	} while (counter != 0);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_COMMON, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}
