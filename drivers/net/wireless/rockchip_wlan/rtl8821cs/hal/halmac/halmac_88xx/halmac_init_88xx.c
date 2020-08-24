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

#include "halmac_init_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_fw_88xx.h"
#include "halmac_common_88xx.h"
#include "halmac_cfg_wmac_88xx.h"
#include "halmac_efuse_88xx.h"
#include "halmac_mimo_88xx.h"
#include "halmac_bb_rf_88xx.h"
#if HALMAC_SDIO_SUPPORT
#include "halmac_sdio_88xx.h"
#endif
#if HALMAC_USB_SUPPORT
#include "halmac_usb_88xx.h"
#endif
#if HALMAC_PCIE_SUPPORT
#include "halmac_pcie_88xx.h"
#endif
#include "halmac_gpio_88xx.h"
#include "halmac_flash_88xx.h"

#if HALMAC_8822B_SUPPORT
#include "halmac_8822b/halmac_init_8822b.h"
#endif

#if HALMAC_8821C_SUPPORT
#include "halmac_8821c/halmac_init_8821c.h"
#endif

#if HALMAC_8822C_SUPPORT
#include "halmac_8822c/halmac_init_8822c.h"
#endif

#if HALMAC_8812F_SUPPORT
#include "halmac_8812f/halmac_init_8812f.h"
#endif

#if HALMAC_PLATFORM_TESTPROGRAM
#include "halmisc_api_88xx.h"
#endif

#if HALMAC_88XX_SUPPORT

#define PLTFM_INFO_MALLOC_MAX_SIZE	16384
#define PLTFM_INFO_RSVD_PG_SIZE		16384
#define DLFW_PKT_MAX_SIZE		8192 /* need multiple of 2 */

static void
init_state_machine_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
verify_io_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
verify_send_rsvd_page_88xx(struct halmac_adapter *adapter);

void
init_adapter_param_88xx(struct halmac_adapter *adapter)
{
	adapter->api_registry.rx_exp_en = 1;
	adapter->api_registry.la_mode_en = 1;
	adapter->api_registry.cfg_drv_rsvd_pg_en = 1;
	adapter->api_registry.sdio_cmd53_4byte_en = 1;

	adapter->efuse_map = (u8 *)NULL;
	adapter->efuse_map_valid = 0;
	adapter->efuse_end = 0;

	adapter->dlfw_pkt_size = DLFW_PKT_MAX_SIZE;
	adapter->pltfm_info.malloc_size = PLTFM_INFO_MALLOC_MAX_SIZE;
	adapter->pltfm_info.rsvd_pg_size = PLTFM_INFO_RSVD_PG_SIZE;

	adapter->cfg_param_info.buf = NULL;
	adapter->cfg_param_info.buf_wptr = NULL;
	adapter->cfg_param_info.num = 0;
	adapter->cfg_param_info.full_fifo_mode = 0;
	adapter->cfg_param_info.buf_size = 0;
	adapter->cfg_param_info.avl_buf_size = 0;
	adapter->cfg_param_info.offset_accum = 0;
	adapter->cfg_param_info.value_accum = 0;

	adapter->ch_sw_info.buf = NULL;
	adapter->ch_sw_info.buf_wptr = NULL;
	adapter->ch_sw_info.extra_info_en = 0;
	adapter->ch_sw_info.buf_size = 0;
	adapter->ch_sw_info.avl_buf_size = 0;
	adapter->ch_sw_info.total_size = 0;
	adapter->ch_sw_info.ch_num = 0;

	adapter->drv_info_size = 0;
	adapter->tx_desc_transfer = 0;

	adapter->txff_alloc.tx_fifo_pg_num = 0;
	adapter->txff_alloc.acq_pg_num = 0;
	adapter->txff_alloc.rsvd_boundary = 0;
	adapter->txff_alloc.rsvd_drv_addr = 0;
	adapter->txff_alloc.rsvd_h2c_info_addr = 0;
	adapter->txff_alloc.rsvd_h2cq_addr = 0;
	adapter->txff_alloc.rsvd_cpu_instr_addr = 0;
	adapter->txff_alloc.rsvd_fw_txbuf_addr = 0;
	adapter->txff_alloc.pub_queue_pg_num = 0;
	adapter->txff_alloc.high_queue_pg_num = 0;
	adapter->txff_alloc.low_queue_pg_num = 0;
	adapter->txff_alloc.normal_queue_pg_num = 0;
	adapter->txff_alloc.extra_queue_pg_num = 0;

	adapter->txff_alloc.la_mode = HALMAC_LA_MODE_DISABLE;
	adapter->txff_alloc.rx_fifo_exp_mode =
					HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE;

	adapter->hw_cfg_info.chk_security_keyid = 0;
	adapter->hw_cfg_info.acq_num = 8;
	adapter->hw_cfg_info.page_size = TX_PAGE_SIZE_88XX;
	adapter->hw_cfg_info.tx_align_size = TX_ALIGN_SIZE_88XX;
	adapter->hw_cfg_info.txdesc_size = TX_DESC_SIZE_88XX;
	adapter->hw_cfg_info.rxdesc_size = RX_DESC_SIZE_88XX;
	adapter->hw_cfg_info.rx_desc_fifo_size = 0;

	adapter->sdio_cmd53_4byte = HALMAC_SDIO_CMD53_4BYTE_MODE_DISABLE;
	adapter->sdio_hw_info.io_hi_speed_flag = 0;
	adapter->sdio_hw_info.io_indir_flag = 1;
	adapter->sdio_hw_info.io_warn_flag = 0;
	adapter->sdio_hw_info.spec_ver = HALMAC_SDIO_SPEC_VER_2_00;
	adapter->sdio_hw_info.clock_speed = 50;
	adapter->sdio_hw_info.block_size = 512;
	adapter->sdio_hw_info.tx_seq = 1;
	adapter->sdio_fs.macid_map = (u8 *)NULL;

	adapter->watcher.get_watcher.sdio_rn_not_align = 0;

	adapter->pinmux_info.wl_led = 0;
	adapter->pinmux_info.sdio_int = 0;
	adapter->pinmux_info.sw_io_0 = 0;
	adapter->pinmux_info.sw_io_1 = 0;
	adapter->pinmux_info.sw_io_2 = 0;
	adapter->pinmux_info.sw_io_3 = 0;
	adapter->pinmux_info.sw_io_4 = 0;
	adapter->pinmux_info.sw_io_5 = 0;
	adapter->pinmux_info.sw_io_6 = 0;
	adapter->pinmux_info.sw_io_7 = 0;
	adapter->pinmux_info.sw_io_8 = 0;
	adapter->pinmux_info.sw_io_9 = 0;
	adapter->pinmux_info.sw_io_10 = 0;
	adapter->pinmux_info.sw_io_11 = 0;
	adapter->pinmux_info.sw_io_12 = 0;
	adapter->pinmux_info.sw_io_13 = 0;
	adapter->pinmux_info.sw_io_14 = 0;
	adapter->pinmux_info.sw_io_15 = 0;

	adapter->pcie_refautok_en = 1;
	adapter->pwr_off_flow_flag = 0;

	adapter->rx_ignore_info.hdr_chk_mask = 1;
	adapter->rx_ignore_info.fcs_chk_mask = 1;
	adapter->rx_ignore_info.hdr_chk_en = 0;
	adapter->rx_ignore_info.fcs_chk_en = 0;
	adapter->rx_ignore_info.cck_rst_en = 0;
	adapter->rx_ignore_info.fcs_chk_thr = HALMAC_PSF_FCS_CHK_THR_28;

	init_adapter_dynamic_param_88xx(adapter);
	init_state_machine_88xx(adapter);
}

void
init_adapter_dynamic_param_88xx(struct halmac_adapter *adapter)
{
	adapter->h2c_info.seq_num = 0;
	adapter->h2c_info.buf_fs = 0;
}

enum halmac_ret_status
mount_api_88xx(struct halmac_adapter *adapter)
{
	struct halmac_api *api = NULL;

	adapter->halmac_api =
		(struct halmac_api *)PLTFM_MALLOC(sizeof(struct halmac_api));
	if (!adapter->halmac_api)
		return HALMAC_RET_MALLOC_FAIL;

	api = (struct halmac_api *)adapter->halmac_api;

	api->halmac_read_efuse = NULL;
	api->halmac_write_efuse = NULL;

	/* Mount function pointer */
	api->halmac_register_api = register_api_88xx;
	api->halmac_download_firmware = download_firmware_88xx;
	api->halmac_free_download_firmware = free_download_firmware_88xx;
	api->halmac_reset_wifi_fw = reset_wifi_fw_88xx;
	api->halmac_get_fw_version = get_fw_version_88xx;
	api->halmac_cfg_mac_addr = cfg_mac_addr_88xx;
	api->halmac_cfg_bssid = cfg_bssid_88xx;
	api->halmac_cfg_transmitter_addr = cfg_transmitter_addr_88xx;
	api->halmac_cfg_net_type = cfg_net_type_88xx;
	api->halmac_cfg_tsf_rst = cfg_tsf_rst_88xx;
	api->halmac_cfg_bcn_space = cfg_bcn_space_88xx;
	api->halmac_rw_bcn_ctrl = rw_bcn_ctrl_88xx;
	api->halmac_cfg_multicast_addr = cfg_multicast_addr_88xx;
	api->halmac_cfg_operation_mode = cfg_operation_mode_88xx;
	api->halmac_cfg_ch_bw = cfg_ch_bw_88xx;
	api->halmac_cfg_bw = cfg_bw_88xx;
	api->halmac_init_mac_cfg = init_mac_cfg_88xx;
	api->halmac_dump_efuse_map = dump_efuse_map_88xx;
	api->halmac_dump_efuse_map_bt = dump_efuse_map_bt_88xx;
	api->halmac_write_efuse_bt = write_efuse_bt_88xx;
	api->halmac_read_efuse_bt = read_efuse_bt_88xx;
	api->halmac_cfg_efuse_auto_check = cfg_efuse_auto_check_88xx;
	api->halmac_dump_logical_efuse_map = dump_log_efuse_map_88xx;
	api->halmac_dump_logical_efuse_mask = dump_log_efuse_mask_88xx;
	api->halmac_pg_efuse_by_map = pg_efuse_by_map_88xx;
	api->halmac_mask_logical_efuse = mask_log_efuse_88xx;
	api->halmac_get_efuse_size = get_efuse_size_88xx;
	api->halmac_get_efuse_available_size = get_efuse_available_size_88xx;
	api->halmac_get_c2h_info = get_c2h_info_88xx;

	api->halmac_get_logical_efuse_size = get_log_efuse_size_88xx;

	api->halmac_write_logical_efuse = write_log_efuse_88xx;
	api->halmac_write_logical_efuse_word = write_log_efuse_word_88xx;
	api->halmac_read_logical_efuse = read_logical_efuse_88xx;

	api->halmac_write_wifi_phy_efuse = write_wifi_phy_efuse_88xx;
	api->halmac_read_wifi_phy_efuse = read_wifi_phy_efuse_88xx;

	api->halmac_ofld_func_cfg = ofld_func_cfg_88xx;
	api->halmac_h2c_lb = h2c_lb_88xx;
	api->halmac_debug = mac_debug_88xx;
	api->halmac_cfg_parameter = cfg_parameter_88xx;
	api->halmac_update_datapack = update_datapack_88xx;
	api->halmac_run_datapack = run_datapack_88xx;
	api->halmac_send_bt_coex = send_bt_coex_88xx;
	api->halmac_verify_platform_api = verify_platform_api_88xx;
	api->halmac_update_packet = update_packet_88xx;
	api->halmac_bcn_ie_filter = bcn_ie_filter_88xx;
	api->halmac_cfg_txbf = cfg_txbf_88xx;
	api->halmac_cfg_mumimo = cfg_mumimo_88xx;
	api->halmac_cfg_sounding = cfg_sounding_88xx;
	api->halmac_del_sounding = del_sounding_88xx;
	api->halmac_su_bfer_entry_init = su_bfer_entry_init_88xx;
	api->halmac_su_bfee_entry_init = su_bfee_entry_init_88xx;
	api->halmac_mu_bfer_entry_init = mu_bfer_entry_init_88xx;
	api->halmac_mu_bfee_entry_init = mu_bfee_entry_init_88xx;
	api->halmac_su_bfer_entry_del = su_bfer_entry_del_88xx;
	api->halmac_su_bfee_entry_del = su_bfee_entry_del_88xx;
	api->halmac_mu_bfer_entry_del = mu_bfer_entry_del_88xx;
	api->halmac_mu_bfee_entry_del = mu_bfee_entry_del_88xx;

	api->halmac_add_ch_info = add_ch_info_88xx;
	api->halmac_add_extra_ch_info = add_extra_ch_info_88xx;
	api->halmac_ctrl_ch_switch = ctrl_ch_switch_88xx;
	api->halmac_p2pps = p2pps_88xx;
	api->halmac_clear_ch_info = clear_ch_info_88xx;
	api->halmac_send_general_info = send_general_info_88xx;
	api->halmac_send_scan_packet = send_scan_packet_88xx;
	api->halmac_drop_scan_packet = drop_scan_packet_88xx;

	api->halmac_start_iqk = start_iqk_88xx;
	api->halmac_start_dpk = start_dpk_88xx;
	api->halmac_ctrl_pwr_tracking = ctrl_pwr_tracking_88xx;
	api->halmac_psd = psd_88xx;
	api->halmac_cfg_la_mode = cfg_la_mode_88xx;
	api->halmac_cfg_rxff_expand_mode = cfg_rxfifo_expand_mode_88xx;

	api->halmac_config_security = config_security_88xx;
	api->halmac_get_used_cam_entry_num = get_used_cam_entry_num_88xx;
	api->halmac_read_cam_entry = read_cam_entry_88xx;
	api->halmac_write_cam = write_cam_88xx;
	api->halmac_clear_cam_entry = clear_cam_entry_88xx;

	api->halmac_cfg_drv_rsvd_pg_num = cfg_drv_rsvd_pg_num_88xx;
	api->halmac_get_chip_version = get_version_88xx;

	api->halmac_query_status = query_status_88xx;
	api->halmac_reset_feature = reset_ofld_feature_88xx;
	api->halmac_check_fw_status = check_fw_status_88xx;
	api->halmac_dump_fw_dmem = dump_fw_dmem_88xx;
	api->halmac_cfg_max_dl_size = cfg_max_dl_size_88xx;

	api->halmac_dump_fifo = dump_fifo_88xx;
	api->halmac_get_fifo_size = get_fifo_size_88xx;

	api->halmac_chk_txdesc = chk_txdesc_88xx;
	api->halmac_dl_drv_rsvd_page = dl_drv_rsvd_page_88xx;
	api->halmac_cfg_csi_rate = cfg_csi_rate_88xx;

	api->halmac_txfifo_is_empty = txfifo_is_empty_88xx;
	api->halmac_download_flash = download_flash_88xx;
	api->halmac_read_flash = read_flash_88xx;
	api->halmac_erase_flash = erase_flash_88xx;
	api->halmac_check_flash = check_flash_88xx;
	api->halmac_cfg_edca_para = cfg_edca_para_88xx;
	api->halmac_pinmux_wl_led_mode = pinmux_wl_led_mode_88xx;
	api->halmac_pinmux_wl_led_sw_ctrl = pinmux_wl_led_sw_ctrl_88xx;
	api->halmac_pinmux_sdio_int_polarity = pinmux_sdio_int_polarity_88xx;
	api->halmac_pinmux_gpio_mode = pinmux_gpio_mode_88xx;
	api->halmac_pinmux_gpio_output = pinmux_gpio_output_88xx;
	api->halmac_pinmux_pin_status = pinmux_pin_status_88xx;

	api->halmac_rx_cut_amsdu_cfg = rx_cut_amsdu_cfg_88xx;
	api->halmac_fw_snding = fw_snding_88xx;
	api->halmac_get_mac_addr = get_mac_addr_88xx;

	api->halmac_enter_cpu_sleep_mode = enter_cpu_sleep_mode_88xx;
	api->halmac_get_cpu_mode = get_cpu_mode_88xx;
	api->halmac_drv_fwctrl = drv_fwctrl_88xx;
	api->halmac_get_watcher = get_watcher_88xx;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
#if HALMAC_SDIO_SUPPORT
		api->halmac_deinit_sdio_cfg = deinit_sdio_cfg_88xx;
		api->halmac_cfg_rx_aggregation = cfg_sdio_rx_agg_88xx;
		api->halmac_deinit_interface_cfg = deinit_sdio_cfg_88xx;
		api->halmac_cfg_tx_agg_align = cfg_txagg_sdio_align_88xx;
		api->halmac_set_bulkout_num = set_sdio_bulkout_num_88xx;
		api->halmac_get_usb_bulkout_id = get_sdio_bulkout_id_88xx;
		api->halmac_reg_read_indirect_32 = sdio_indirect_reg_r32_88xx;
		api->halmac_reg_sdio_cmd53_read_n = sdio_reg_rn_88xx;
		api->halmac_sdio_cmd53_4byte = sdio_cmd53_4byte_88xx;
		api->halmac_sdio_hw_info = sdio_hw_info_88xx;
		api->halmac_en_ref_autok_pcie = en_ref_autok_sdio_88xx;

#endif
	} else if (adapter->intf == HALMAC_INTERFACE_USB) {
#if HALMAC_USB_SUPPORT
		api->halmac_init_usb_cfg = init_usb_cfg_88xx;
		api->halmac_deinit_usb_cfg = deinit_usb_cfg_88xx;
		api->halmac_cfg_rx_aggregation = cfg_usb_rx_agg_88xx;
		api->halmac_init_interface_cfg = init_usb_cfg_88xx;
		api->halmac_deinit_interface_cfg = deinit_usb_cfg_88xx;
		api->halmac_cfg_tx_agg_align = cfg_txagg_usb_align_88xx;
		api->halmac_tx_allowed_sdio = tx_allowed_usb_88xx;
		api->halmac_set_bulkout_num = set_usb_bulkout_num_88xx;
		api->halmac_get_sdio_tx_addr = get_usb_tx_addr_88xx;
		api->halmac_get_usb_bulkout_id = get_usb_bulkout_id_88xx;
		api->halmac_reg_read_8 = reg_r8_usb_88xx;
		api->halmac_reg_write_8 = reg_w8_usb_88xx;
		api->halmac_reg_read_16 = reg_r16_usb_88xx;
		api->halmac_reg_write_16 = reg_w16_usb_88xx;
		api->halmac_reg_read_32 = reg_r32_usb_88xx;
		api->halmac_reg_write_32 = reg_w32_usb_88xx;
		api->halmac_reg_read_indirect_32 = usb_indirect_reg_r32_88xx;
		api->halmac_reg_sdio_cmd53_read_n = usb_reg_rn_88xx;
		api->halmac_en_ref_autok_pcie = en_ref_autok_usb_88xx;
#endif
	} else if (adapter->intf == HALMAC_INTERFACE_PCIE) {
#if HALMAC_PCIE_SUPPORT
		api->halmac_init_pcie_cfg = init_pcie_cfg_88xx;
		api->halmac_deinit_pcie_cfg = deinit_pcie_cfg_88xx;
		api->halmac_cfg_rx_aggregation = cfg_pcie_rx_agg_88xx;
		api->halmac_init_interface_cfg = init_pcie_cfg_88xx;
		api->halmac_deinit_interface_cfg = deinit_pcie_cfg_88xx;
		api->halmac_cfg_tx_agg_align = cfg_txagg_pcie_align_88xx;
		api->halmac_tx_allowed_sdio = tx_allowed_pcie_88xx;
		api->halmac_set_bulkout_num = set_pcie_bulkout_num_88xx;
		api->halmac_get_sdio_tx_addr = get_pcie_tx_addr_88xx;
		api->halmac_get_usb_bulkout_id = get_pcie_bulkout_id_88xx;
		api->halmac_reg_read_8 = reg_r8_pcie_88xx;
		api->halmac_reg_write_8 = reg_w8_pcie_88xx;
		api->halmac_reg_read_16 = reg_r16_pcie_88xx;
		api->halmac_reg_write_16 = reg_w16_pcie_88xx;
		api->halmac_reg_read_32 = reg_r32_pcie_88xx;
		api->halmac_reg_write_32 = reg_w32_pcie_88xx;
		api->halmac_reg_read_indirect_32 = pcie_indirect_reg_r32_88xx;
		api->halmac_reg_sdio_cmd53_read_n = pcie_reg_rn_88xx;
		api->halmac_en_ref_autok_pcie = en_ref_autok_pcie_88xx;
#endif
	} else {
		PLTFM_MSG_ERR("[ERR]Set halmac io function Error!!\n");
	}

	if (adapter->chip_id == HALMAC_CHIP_ID_8822B) {
#if HALMAC_8822B_SUPPORT
		mount_api_8822b(adapter);
#endif
	} else if (adapter->chip_id == HALMAC_CHIP_ID_8821C) {
#if HALMAC_8821C_SUPPORT
		mount_api_8821c(adapter);
#endif
	} else if (adapter->chip_id == HALMAC_CHIP_ID_8822C) {
#if HALMAC_8822C_SUPPORT
		mount_api_8822c(adapter);
#endif
	} else if (adapter->chip_id == HALMAC_CHIP_ID_8812F) {
#if HALMAC_8812F_SUPPORT
		mount_api_8812f(adapter);
#endif
	} else {
		PLTFM_MSG_ERR("[ERR]Chip ID undefine!!\n");
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}

#if HALMAC_PLATFORM_TESTPROGRAM
	halmac_mount_misc_api_88xx(adapter);
#endif

	return HALMAC_RET_SUCCESS;
}

static void
init_state_machine_88xx(struct halmac_adapter *adapter)
{
	struct halmac_state *state = &adapter->halmac_state;

	init_ofld_feature_state_machine_88xx(adapter);

	state->api_state = HALMAC_API_STATE_INIT;

	state->dlfw_state = HALMAC_DLFW_NONE;
	state->mac_pwr = HALMAC_MAC_POWER_OFF;
	state->gpio_cfg_state = HALMAC_GPIO_CFG_STATE_IDLE;
	state->rsvd_pg_state = HALMAC_RSVD_PG_STATE_IDLE;
}

void
init_ofld_feature_state_machine_88xx(struct halmac_adapter *adapter)
{
	struct halmac_state *state = &adapter->halmac_state;

	state->efuse_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
	state->efuse_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->efuse_state.seq_num = adapter->h2c_info.seq_num;

	state->cfg_param_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
	state->cfg_param_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->cfg_param_state.seq_num = adapter->h2c_info.seq_num;

	state->scan_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
	state->scan_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->scan_state.seq_num = adapter->h2c_info.seq_num;

	state->update_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->update_pkt_state.seq_num = adapter->h2c_info.seq_num;

	state->iqk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->iqk_state.seq_num = adapter->h2c_info.seq_num;

	state->pwr_trk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->pwr_trk_state.seq_num = adapter->h2c_info.seq_num;

	state->psd_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->psd_state.seq_num = adapter->h2c_info.seq_num;
	state->psd_state.data_size = 0;
	state->psd_state.seg_size = 0;
	state->psd_state.data = NULL;

	state->fw_snding_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
	state->fw_snding_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
	state->fw_snding_state.seq_num = adapter->h2c_info.seq_num;

	state->wlcpu_mode = HALMAC_WLCPU_ACTIVE;
}

/**
 * register_api_88xx() - register feature list
 * @adapter
 * @registry : feature list, 1->enable 0->disable
 * Author : Ivan Lin
 *
 * Default is enable all api registry
 *
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
register_api_88xx(struct halmac_adapter *adapter,
		  struct halmac_api_registry *registry)
{
	if (!registry)
		return HALMAC_RET_NULL_POINTER;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	PLTFM_MEMCPY(&adapter->api_registry, registry, sizeof(*registry));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_mac_cfg_88xx() - config page1~page7 register
 * @adapter : the adapter of halmac
 * @mode : trx mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_mac_cfg_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	status = api->halmac_init_trx_cfg(adapter, mode);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]init trx %x\n", status);
		return status;
	}

	status = api->halmac_init_protocol_cfg(adapter);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]init ptcl %x\n", status);
		return status;
	}

	status = api->halmac_init_edca_cfg(adapter);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]init edca %x\n", status);
		return status;
	}

	status = api->halmac_init_wmac_cfg(adapter);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]init wmac %x\n", status);
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return status;
}

/**
 * reset_ofld_feature_88xx() -reset async api cmd status
 * @adapter : the adapter of halmac
 * @feature_id : feature_id
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status.
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reset_ofld_feature_88xx(struct halmac_adapter *adapter,
			enum halmac_feature_id feature_id)
{
	struct halmac_state *state = &adapter->halmac_state;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		state->cfg_param_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->cfg_param_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK:
		state->efuse_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->efuse_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		state->scan_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->scan_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		state->update_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_SEND_SCAN_PACKET:
		state->scan_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_DROP_SCAN_PACKET:
		state->drop_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_IQK:
		state->iqk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		state->pwr_trk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_PSD:
		state->psd_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_FW_SNDING:
		state->fw_snding_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->fw_snding_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		break;
	case HALMAC_FEATURE_DPK:
		state->dpk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_ALL:
		state->cfg_param_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->cfg_param_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		state->efuse_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->efuse_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		state->scan_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->scan_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		state->update_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->scan_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->drop_pkt_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->iqk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->pwr_trk_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->psd_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->fw_snding_state.proc_status = HALMAC_CMD_PROCESS_IDLE;
		state->fw_snding_state.cmd_cnstr_state = HALMAC_CMD_CNSTR_IDLE;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]invalid feature id\n");
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * (debug API)verify_platform_api_88xx() - verify platform api
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
verify_platform_api_88xx(struct halmac_adapter *adapter)
{
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	ret_status = verify_io_88xx(adapter);

	if (ret_status != HALMAC_RET_SUCCESS)
		return ret_status;

	if (adapter->txff_alloc.la_mode != HALMAC_LA_MODE_FULL)
		ret_status = verify_send_rsvd_page_88xx(adapter);

	if (ret_status != HALMAC_RET_SUCCESS)
		return ret_status;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return ret_status;
}

void
tx_desc_chksum_88xx(struct halmac_adapter *adapter, u8 enable)
{
	u16 value16;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->tx_desc_checksum = enable;

	value16 = HALMAC_REG_R16(REG_TXDMA_OFFSET_CHK);
	if (enable == 1)
		HALMAC_REG_W16(REG_TXDMA_OFFSET_CHK, value16 | BIT(13));
	else
		HALMAC_REG_W16(REG_TXDMA_OFFSET_CHK, value16 & ~BIT(13));
}

static enum halmac_ret_status
verify_io_88xx(struct halmac_adapter *adapter)
{
	u8 value8;
	u8 wvalue8;
	u32 value32;
	u32 value32_2;
	u32 wvalue32;
	u32 offset;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
#if HALMAC_SDIO_SUPPORT
		offset = REG_PAGE5_DUMMY;
		if (0 == (offset & 0xFFFF0000))
			offset |= WLAN_IOREG_OFFSET;

		ret_status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);

		/* Verify CMD52 R/W */
		wvalue8 = 0xab;
		PLTFM_SDIO_CMD52_W(offset, wvalue8);

		value8 = PLTFM_SDIO_CMD52_R(offset);

		if (value8 != wvalue8) {
			PLTFM_MSG_ERR("[ERR]cmd52 r/w\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}

		/* Verify CMD53 R/W */
		PLTFM_SDIO_CMD52_W(offset, 0xaa);
		PLTFM_SDIO_CMD52_W(offset + 1, 0xbb);
		PLTFM_SDIO_CMD52_W(offset + 2, 0xcc);
		PLTFM_SDIO_CMD52_W(offset + 3, 0xdd);

		value32 = PLTFM_SDIO_CMD53_R32(offset);

		if (value32 != 0xddccbbaa) {
			PLTFM_MSG_ERR("[ERR]cmd53 r\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}

		wvalue32 = 0x11223344;
		PLTFM_SDIO_CMD53_W32(offset, wvalue32);

		value32 = PLTFM_SDIO_CMD53_R32(offset);

		if (value32 != wvalue32) {
			PLTFM_MSG_ERR("[ERR]cmd53 w\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}

		/* value32 should be 0x33441122 */
		value32 = PLTFM_SDIO_CMD53_R32(offset + 2);

		wvalue32 = 0x11225566;
		PLTFM_SDIO_CMD53_W32(offset, wvalue32);

		/* value32 should be 0x55661122 */
		value32_2 = PLTFM_SDIO_CMD53_R32(offset + 2);
		if (value32_2 == value32) {
			PLTFM_MSG_ERR("[ERR]cmd52 is used\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}
#else
		return HALMAC_RET_WRONG_INTF;
#endif
	} else {
		wvalue32 = 0x77665511;
		PLTFM_REG_W32(REG_PAGE5_DUMMY, wvalue32);

		value32 = PLTFM_REG_R32(REG_PAGE5_DUMMY);
		if (value32 != wvalue32) {
			PLTFM_MSG_ERR("[ERR]reg rw\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}
	}

	return ret_status;
}

static enum halmac_ret_status
verify_send_rsvd_page_88xx(struct halmac_adapter *adapter)
{
	u8 txdesc_size = adapter->hw_cfg_info.txdesc_size;
	u8 *rsvd_buf = NULL;
	u8 *rsvd_page = NULL;
	u32 i;
	u32 pkt_size = 64;
	u32 payload = 0xab;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	rsvd_buf = (u8 *)PLTFM_MALLOC(pkt_size);

	if (!rsvd_buf) {
		PLTFM_MSG_ERR("[ERR]rsvd buf malloc!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}

	PLTFM_MEMSET(rsvd_buf, (u8)payload, pkt_size);

	ret_status = dl_rsvd_page_88xx(adapter,
				       adapter->txff_alloc.rsvd_boundary,
				       rsvd_buf, pkt_size);
	if (ret_status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(rsvd_buf, pkt_size);
		return ret_status;
	}

	rsvd_page = (u8 *)PLTFM_MALLOC(pkt_size + txdesc_size);

	if (!rsvd_page) {
		PLTFM_MSG_ERR("[ERR]rsvd page malloc!!\n");
		PLTFM_FREE(rsvd_buf, pkt_size);
		return HALMAC_RET_MALLOC_FAIL;
	}

	PLTFM_MEMSET(rsvd_page, 0x00, pkt_size + txdesc_size);

	ret_status = dump_fifo_88xx(adapter, HAL_FIFO_SEL_RSVD_PAGE, 0,
				    pkt_size + txdesc_size, rsvd_page);

	if (ret_status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(rsvd_buf, pkt_size);
		PLTFM_FREE(rsvd_page, pkt_size + txdesc_size);
		return ret_status;
	}

	for (i = 0; i < pkt_size; i++) {
		if (*(rsvd_buf + i) != *(rsvd_page + (i + txdesc_size))) {
			PLTFM_MSG_ERR("[ERR]Compare RSVD page Fail\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}
	}

	PLTFM_FREE(rsvd_buf, pkt_size);
	PLTFM_FREE(rsvd_page, pkt_size + txdesc_size);

	return ret_status;
}

enum halmac_ret_status
pg_num_parser_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode,
		   struct halmac_pg_num *tbl)
{
	u8 flag;
	u16 hpq_num = 0;
	u16 lpq_num = 0;
	u16 npq_num = 0;
	u16 gapq_num = 0;
	u16 expq_num = 0;
	u16 pubq_num = 0;
	u32 i = 0;

	flag = 0;
	for (i = 0; i < HALMAC_TRX_MODE_MAX; i++) {
		if (mode == tbl[i].mode) {
			hpq_num = tbl[i].hq_num;
			lpq_num = tbl[i].lq_num;
			npq_num = tbl[i].nq_num;
			expq_num = tbl[i].exq_num;
			gapq_num = tbl[i].gap_num;
			pubq_num = adapter->txff_alloc.acq_pg_num - hpq_num -
					lpq_num - npq_num - expq_num - gapq_num;
			flag = 1;
			PLTFM_MSG_TRACE("[TRACE]%s done\n", __func__);
			break;
		}
	}

	if (flag == 0) {
		PLTFM_MSG_ERR("[ERR]trx mode!!\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	if (adapter->txff_alloc.acq_pg_num <
	    hpq_num + lpq_num + npq_num + expq_num + gapq_num) {
		PLTFM_MSG_ERR("[ERR]acqnum = %d\n",
			      adapter->txff_alloc.acq_pg_num);
		PLTFM_MSG_ERR("[ERR]hpq_num = %d\n", hpq_num);
		PLTFM_MSG_ERR("[ERR]LPQ_num = %d\n", lpq_num);
		PLTFM_MSG_ERR("[ERR]npq_num = %d\n", npq_num);
		PLTFM_MSG_ERR("[ERR]EPQ_num = %d\n", expq_num);
		PLTFM_MSG_ERR("[ERR]gapq_num = %d\n", gapq_num);
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;
	}

	adapter->txff_alloc.high_queue_pg_num = hpq_num;
	adapter->txff_alloc.low_queue_pg_num = lpq_num;
	adapter->txff_alloc.normal_queue_pg_num = npq_num;
	adapter->txff_alloc.extra_queue_pg_num = expq_num;
	adapter->txff_alloc.pub_queue_pg_num = pubq_num;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
rqpn_parser_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode,
		 struct halmac_rqpn *tbl)
{
	u8 flag;
	u32 i;

	flag = 0;
	for (i = 0; i < HALMAC_TRX_MODE_MAX; i++) {
		if (mode == tbl[i].mode) {
			adapter->pq_map[HALMAC_PQ_MAP_VO] = tbl[i].dma_map_vo;
			adapter->pq_map[HALMAC_PQ_MAP_VI] = tbl[i].dma_map_vi;
			adapter->pq_map[HALMAC_PQ_MAP_BE] = tbl[i].dma_map_be;
			adapter->pq_map[HALMAC_PQ_MAP_BK] = tbl[i].dma_map_bk;
			adapter->pq_map[HALMAC_PQ_MAP_MG] = tbl[i].dma_map_mg;
			adapter->pq_map[HALMAC_PQ_MAP_HI] = tbl[i].dma_map_hi;
			flag = 1;
			PLTFM_MSG_TRACE("[TRACE]%s done\n", __func__);
			break;
		}
	}

	if (flag == 0) {
		PLTFM_MSG_ERR("[ERR]trx mdoe!!\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
fwff_is_empty_88xx(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 cnt;

	cnt = 5000;
	while (HALMAC_REG_R16(REG_FWFF_CTRL) !=
		HALMAC_REG_R16(REG_FWFF_PKT_INFO)) {
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]polling fwff empty fail\n");
			return HALMAC_RET_FWFF_NO_EMPTY;
		}
		cnt--;
		PLTFM_DELAY_US(50);
	}
	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_88XX_SUPPORT */
