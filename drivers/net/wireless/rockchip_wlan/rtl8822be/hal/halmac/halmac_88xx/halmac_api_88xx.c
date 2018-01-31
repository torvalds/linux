/* SPDX-License-Identifier: GPL-2.0 */
#include "halmac_88xx_cfg.h"

/**
 * halmac_init_adapter_para_88xx() - int halmac adapter
 * @pHalmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : VOID
 */
VOID
halmac_init_adapter_para_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	pHalmac_adapter->api_record.array_wptr = 0;
	pHalmac_adapter->pHalAdapter_backup = pHalmac_adapter;
	pHalmac_adapter->h2c_packet_seq = 0;
	pHalmac_adapter->pHalEfuse_map = (u8 *)NULL;
	pHalmac_adapter->hal_efuse_map_valid = _FALSE;
	pHalmac_adapter->efuse_end = 0;
	pHalmac_adapter->pHal_mac_addr[0].Address_L_H.Address_Low = 0;
	pHalmac_adapter->pHal_mac_addr[0].Address_L_H.Address_High = 0;
	pHalmac_adapter->pHal_mac_addr[1].Address_L_H.Address_Low = 0;
	pHalmac_adapter->pHal_mac_addr[1].Address_L_H.Address_High = 0;
	pHalmac_adapter->pHal_bss_addr[0].Address_L_H.Address_Low = 0;
	pHalmac_adapter->pHal_bss_addr[0].Address_L_H.Address_High = 0;
	pHalmac_adapter->pHal_bss_addr[1].Address_L_H.Address_Low = 0;
	pHalmac_adapter->pHal_bss_addr[1].Address_L_H.Address_High = 0;


	pHalmac_adapter->low_clk = _FALSE;
	pHalmac_adapter->h2c_buf_free_space = 0;
	pHalmac_adapter->max_download_size = HALMAC_FW_MAX_DL_SIZE_88XX;

	/* Init LPS Option */
	pHalmac_adapter->fwlps_option.mode = 0x01; /*0:Active 1:LPS 2:WMMPS*/
	pHalmac_adapter->fwlps_option.awake_interval = 1;
	pHalmac_adapter->fwlps_option.enter_32K = 1;
	pHalmac_adapter->fwlps_option.clk_request = 0;
	pHalmac_adapter->fwlps_option.rlbm = 0;
	pHalmac_adapter->fwlps_option.smart_ps = 0;
	pHalmac_adapter->fwlps_option.awake_interval = 1;
	pHalmac_adapter->fwlps_option.all_queue_uapsd = 0;
	pHalmac_adapter->fwlps_option.pwr_state = 0;
	pHalmac_adapter->fwlps_option.low_pwr_rx_beacon = 0;
	pHalmac_adapter->fwlps_option.ant_auto_switch = 0;
	pHalmac_adapter->fwlps_option.ps_allow_bt_high_Priority = 0;
	pHalmac_adapter->fwlps_option.protect_bcn = 0;
	pHalmac_adapter->fwlps_option.silence_period = 0;
	pHalmac_adapter->fwlps_option.fast_bt_connect = 0;
	pHalmac_adapter->fwlps_option.two_antenna_en = 0;
	pHalmac_adapter->fwlps_option.adopt_user_Setting = 1;
	pHalmac_adapter->fwlps_option.drv_bcn_early_shift = 0;

	pHalmac_adapter->config_para_info.pCfg_para_buf = NULL;
	pHalmac_adapter->config_para_info.pPara_buf_w = NULL;
	pHalmac_adapter->config_para_info.para_num = 0;
	pHalmac_adapter->config_para_info.full_fifo_mode = _FALSE;
	pHalmac_adapter->config_para_info.para_buf_size = 0;
	pHalmac_adapter->config_para_info.avai_para_buf_size = 0;
	pHalmac_adapter->config_para_info.offset_accumulation = 0;
	pHalmac_adapter->config_para_info.value_accumulation = 0;
	pHalmac_adapter->config_para_info.datapack_segment = 0;

	pHalmac_adapter->ch_sw_info.ch_info_buf = NULL;
	pHalmac_adapter->ch_sw_info.ch_info_buf_w = NULL;
	pHalmac_adapter->ch_sw_info.extra_info_en = 0;
	pHalmac_adapter->ch_sw_info.buf_size = 0;
	pHalmac_adapter->ch_sw_info.avai_buf_size = 0;
	pHalmac_adapter->ch_sw_info.total_size = 0;
	pHalmac_adapter->ch_sw_info.ch_num = 0;

	pHalmac_adapter->gen_info_valid = _FALSE;

	PLATFORM_RTL_MEMSET(pHalmac_adapter->pDriver_adapter, pHalmac_adapter->api_record.api_array, HALMAC_API_STUFF, sizeof(pHalmac_adapter->api_record.api_array));

	halmac_init_state_machine_88xx(pHalmac_adapter);

}

/**
 * halmac_init_state_machine_88xx() - init halmac software state machine
 * @pHalmac_adapter
 *
 * SD1 internal use.
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : VOID
 */
VOID
halmac_init_state_machine_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	PHALMAC_STATE pState = &(pHalmac_adapter->halmac_state);

	pState->efuse_state_set.efuse_cmd_construct_state = HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
	pState->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->efuse_state_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->cfg_para_state_set.cfg_para_cmd_construct_state = HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
	pState->cfg_para_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->cfg_para_state_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->scan_state_set.scan_cmd_construct_state = HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
	pState->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->scan_state_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->update_packet_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->update_packet_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->iqk_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->iqk_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->power_tracking_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->power_tracking_set.seq_num = pHalmac_adapter->h2c_packet_seq;

	pState->psd_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	pState->psd_set.seq_num = pHalmac_adapter->h2c_packet_seq;
	pState->psd_set.data_size = 0;
	pState->psd_set.segment_size = 0;
	pState->psd_set.pData = NULL;

	pState->api_state = HALMAC_API_STATE_INIT;

	pState->dlfw_state = HALMAC_DLFW_NONE;
	pState->mac_power = HALMAC_MAC_POWER_OFF;
	pState->ps_state = HALMAC_PS_STATE_UNDEFINE;
}

/**
 * halmac_mount_api_88xx() - attach functions to function pointer
 * @pHalmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mount_api_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	PHALMAC_API pHalmac_api = (PHALMAC_API)NULL;
	u8 chip_id, chip_version;
	u32 polling_count;

	pHalmac_adapter->pHalmac_api = (PHALMAC_API)PLATFORM_RTL_MALLOC(pDriver_adapter, sizeof(HALMAC_API));
	if (NULL == pHalmac_adapter->pHalmac_api)
		return HALMAC_RET_MALLOC_FAIL;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, HALMAC_SVN_VER_88XX"\n");
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_MAJOR_VER_88XX = %x\n", HALMAC_MAJOR_VER_88XX);
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_PROTOTYPE_88XX = %x\n", HALMAC_PROTOTYPE_VER_88XX);
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_MINOR_VER_88XX = %x\n", HALMAC_MINOR_VER_88XX);


	/* Mount function pointer */
	pHalmac_api->halmac_download_firmware = halmac_download_firmware_88xx;
	pHalmac_api->halmac_get_fw_version = halmac_get_fw_version_88xx;
	pHalmac_api->halmac_cfg_mac_addr = halmac_cfg_mac_addr_88xx;
	pHalmac_api->halmac_cfg_bssid = halmac_cfg_bssid_88xx;
	pHalmac_api->halmac_cfg_multicast_addr = halmac_cfg_multicast_addr_88xx;
	pHalmac_api->halmac_pre_init_system_cfg = halmac_pre_init_system_cfg_88xx;
	pHalmac_api->halmac_init_system_cfg = halmac_init_system_cfg_88xx;
	pHalmac_api->halmac_init_protocol_cfg = halmac_init_protocol_cfg_88xx;
	pHalmac_api->halmac_init_edca_cfg = halmac_init_edca_cfg_88xx;
	pHalmac_api->halmac_cfg_operation_mode = halmac_cfg_operation_mode_88xx;
	pHalmac_api->halmac_cfg_ch_bw = halmac_cfg_ch_bw_88xx;
	pHalmac_api->halmac_cfg_bw = halmac_cfg_bw_88xx;
	pHalmac_api->halmac_init_wmac_cfg = halmac_init_wmac_cfg_88xx;
	pHalmac_api->halmac_init_mac_cfg = halmac_init_mac_cfg_88xx;
	pHalmac_api->halmac_init_sdio_cfg = halmac_init_sdio_cfg_88xx;
	pHalmac_api->halmac_init_usb_cfg = halmac_init_usb_cfg_88xx;
	pHalmac_api->halmac_init_pcie_cfg = halmac_init_pcie_cfg_88xx;
	pHalmac_api->halmac_deinit_sdio_cfg = halmac_deinit_sdio_cfg_88xx;
	pHalmac_api->halmac_deinit_usb_cfg = halmac_deinit_usb_cfg_88xx;
	pHalmac_api->halmac_deinit_pcie_cfg = halmac_deinit_pcie_cfg_88xx;
	pHalmac_api->halmac_dump_efuse_map = halmac_dump_efuse_map_88xx;
	pHalmac_api->halmac_dump_efuse_map_bt = halmac_dump_efuse_map_bt_88xx;
	pHalmac_api->halmac_write_efuse_bt = halmac_write_efuse_bt_88xx;
	pHalmac_api->halmac_dump_logical_efuse_map = halmac_dump_logical_efuse_map_88xx;
	/* pHalmac_api->halmac_write_efuse = halmac_write_efuse_88xx; */
	pHalmac_api->halmac_pg_efuse_by_map = halmac_pg_efuse_by_map_88xx;
	pHalmac_api->halmac_get_efuse_size = halmac_get_efuse_size_88xx;
	pHalmac_api->halmac_get_efuse_available_size = halmac_get_efuse_available_size_88xx;
	pHalmac_api->halmac_get_c2h_info = halmac_get_c2h_info_88xx;
	/* pHalmac_api->halmac_read_efuse = halmac_read_efuse_88xx; */

	pHalmac_api->halmac_get_logical_efuse_size = halmac_get_logical_efuse_size_88xx;

	pHalmac_api->halmac_write_logical_efuse = halmac_write_logical_efuse_88xx;
	pHalmac_api->halmac_read_logical_efuse = halmac_read_logical_efuse_88xx;

	pHalmac_api->halmac_cfg_fwlps_option = halmac_cfg_fwlps_option_88xx;
	pHalmac_api->halmac_cfg_fwips_option = halmac_cfg_fwips_option_88xx;
	pHalmac_api->halmac_enter_wowlan = halmac_enter_wowlan_88xx;
	pHalmac_api->halmac_leave_wowlan = halmac_leave_wowlan_88xx;
	pHalmac_api->halmac_enter_ps = halmac_enter_ps_88xx;
	pHalmac_api->halmac_leave_ps = halmac_leave_ps_88xx;
	pHalmac_api->halmac_h2c_lb = halmac_h2c_lb_88xx;
	pHalmac_api->halmac_debug = halmac_debug_88xx;
	pHalmac_api->halmac_cfg_parameter = halmac_cfg_parameter_88xx;
	pHalmac_api->halmac_update_datapack = halmac_update_datapack_88xx;
	pHalmac_api->halmac_run_datapack = halmac_run_datapack_88xx;
	pHalmac_api->halmac_cfg_drv_info = halmac_cfg_drv_info_88xx;
	pHalmac_api->halmac_send_bt_coex = halmac_send_bt_coex_88xx;
	pHalmac_api->halmac_verify_platform_api = halmac_verify_platform_api_88xx;
	pHalmac_api->halmac_update_packet = halmac_update_packet_88xx;
	pHalmac_api->halmac_bcn_ie_filter = halmac_bcn_ie_filter_88xx;
	pHalmac_api->halmac_cfg_txbf = halmac_cfg_txbf_88xx;
	pHalmac_api->halmac_cfg_mumimo = halmac_cfg_mumimo_88xx;
	pHalmac_api->halmac_cfg_sounding = halmac_cfg_sounding_88xx;
	pHalmac_api->halmac_del_sounding = halmac_del_sounding_88xx;
	pHalmac_api->halmac_su_bfer_entry_init = halmac_su_bfer_entry_init_88xx;
	pHalmac_api->halmac_su_bfee_entry_init = halmac_su_bfee_entry_init_88xx;
	pHalmac_api->halmac_mu_bfer_entry_init = halmac_mu_bfer_entry_init_88xx;
	pHalmac_api->halmac_mu_bfee_entry_init = halmac_mu_bfee_entry_init_88xx;
	pHalmac_api->halmac_su_bfer_entry_del = halmac_su_bfer_entry_del_88xx;
	pHalmac_api->halmac_su_bfee_entry_del = halmac_su_bfee_entry_del_88xx;
	pHalmac_api->halmac_mu_bfer_entry_del = halmac_mu_bfer_entry_del_88xx;
	pHalmac_api->halmac_mu_bfee_entry_del = halmac_mu_bfee_entry_del_88xx;

	pHalmac_api->halmac_add_ch_info = halmac_add_ch_info_88xx;
	pHalmac_api->halmac_add_extra_ch_info = halmac_add_extra_ch_info_88xx;
	pHalmac_api->halmac_ctrl_ch_switch = halmac_ctrl_ch_switch_88xx;
	pHalmac_api->halmac_clear_ch_info = halmac_clear_ch_info_88xx;
	pHalmac_api->halmac_send_general_info = halmac_send_general_info_88xx;

	pHalmac_api->halmac_start_iqk = halmac_start_iqk_88xx;
	pHalmac_api->halmac_ctrl_pwr_tracking = halmac_ctrl_pwr_tracking_88xx;
	pHalmac_api->halmac_psd = halmac_psd_88xx;
	pHalmac_api->halmac_cfg_la_mode = halmac_cfg_la_mode_88xx;

	pHalmac_api->halmac_get_hw_value = halmac_get_hw_value_88xx;
	pHalmac_api->halmac_set_hw_value = halmac_set_hw_value_88xx;

	pHalmac_api->halmac_cfg_drv_rsvd_pg_num = halmac_cfg_drv_rsvd_pg_num_88xx;
	pHalmac_api->halmac_get_chip_version = halmac_get_chip_version_88xx;

	pHalmac_api->halmac_query_status = halmac_query_status_88xx;
	pHalmac_api->halmac_reset_feature = halmac_reset_feature_88xx;
	pHalmac_api->halmac_check_fw_status = halmac_check_fw_status_88xx;
	pHalmac_api->halmac_dump_fw_dmem = halmac_dump_fw_dmem_88xx;
	pHalmac_api->halmac_cfg_max_dl_size = halmac_cfg_max_dl_size_88xx;

	pHalmac_api->halmac_dump_fifo = halmac_dump_fifo_88xx;
	pHalmac_api->halmac_get_fifo_size = halmac_get_fifo_size_88xx;

	pHalmac_api->halmac_chk_txdesc = halmac_chk_txdesc_88xx;

	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		pHalmac_api->halmac_cfg_rx_aggregation = halmac_cfg_rx_aggregation_88xx_sdio;
		pHalmac_api->halmac_init_interface_cfg = halmac_init_sdio_cfg_88xx;
		pHalmac_api->halmac_deinit_interface_cfg = halmac_deinit_sdio_cfg_88xx;
		pHalmac_api->halmac_reg_read_8 = halmac_reg_read_8_sdio_88xx;
		pHalmac_api->halmac_reg_write_8 = halmac_reg_write_8_sdio_88xx;
		pHalmac_api->halmac_reg_read_16 = halmac_reg_read_16_sdio_88xx;
		pHalmac_api->halmac_reg_write_16 = halmac_reg_write_16_sdio_88xx;
		pHalmac_api->halmac_reg_read_32 = halmac_reg_read_32_sdio_88xx;
		pHalmac_api->halmac_reg_write_32 = halmac_reg_write_32_sdio_88xx;
	} else if (HALMAC_INTERFACE_USB == pHalmac_adapter->halmac_interface) {
		pHalmac_api->halmac_cfg_rx_aggregation = halmac_cfg_rx_aggregation_88xx_usb;
		pHalmac_api->halmac_init_interface_cfg = halmac_init_usb_cfg_88xx;
		pHalmac_api->halmac_deinit_interface_cfg = halmac_deinit_usb_cfg_88xx;
		pHalmac_api->halmac_reg_read_8 = halmac_reg_read_8_usb_88xx;
		pHalmac_api->halmac_reg_write_8 = halmac_reg_write_8_usb_88xx;
		pHalmac_api->halmac_reg_read_16 = halmac_reg_read_16_usb_88xx;
		pHalmac_api->halmac_reg_write_16 = halmac_reg_write_16_usb_88xx;
		pHalmac_api->halmac_reg_read_32 = halmac_reg_read_32_usb_88xx;
		pHalmac_api->halmac_reg_write_32 = halmac_reg_write_32_usb_88xx;
	} else if (HALMAC_INTERFACE_PCIE == pHalmac_adapter->halmac_interface) {
		pHalmac_api->halmac_cfg_rx_aggregation = halmac_cfg_rx_aggregation_88xx_pcie;
		pHalmac_api->halmac_init_interface_cfg = halmac_init_pcie_cfg_88xx;
		pHalmac_api->halmac_deinit_interface_cfg = halmac_deinit_pcie_cfg_88xx;
		pHalmac_api->halmac_reg_read_8 = halmac_reg_read_8_pcie_88xx;
		pHalmac_api->halmac_reg_write_8 = halmac_reg_write_8_pcie_88xx;
		pHalmac_api->halmac_reg_read_16 = halmac_reg_read_16_pcie_88xx;
		pHalmac_api->halmac_reg_write_16 = halmac_reg_write_16_pcie_88xx;
		pHalmac_api->halmac_reg_read_32 = halmac_reg_read_32_pcie_88xx;
		pHalmac_api->halmac_reg_write_32 = halmac_reg_write_32_pcie_88xx;
	} else {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Set halmac io function Error!!\n");
	}

	pHalmac_api->halmac_set_bulkout_num = halmac_set_bulkout_num_88xx;
	pHalmac_api->halmac_get_sdio_tx_addr = halmac_get_sdio_tx_addr_88xx;
	pHalmac_api->halmac_get_usb_bulkout_id = halmac_get_usb_bulkout_id_88xx;
	pHalmac_api->halmac_timer_2s = halmac_timer_2s_88xx;
	pHalmac_api->halmac_fill_txdesc_checksum = halmac_fill_txdesc_check_sum_88xx;

	/* Get Chip_id and Chip_version */
	chip_id = HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_CFG2);
	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		if (chip_id == 0xEA) {
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SDIO_HSUS_CTRL, HALMAC_REG_READ_8(pHalmac_adapter, REG_SDIO_HSUS_CTRL) & ~(BIT(0)));

			polling_count = HALMAC_POLLING_READY_TIMEOUT_COUNT;
			while (!(HALMAC_REG_READ_8(pHalmac_adapter, REG_SDIO_HSUS_CTRL) & 0x02)) {
				polling_count--;
				if (polling_count == 0)
					return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
			}
		}
		chip_id = HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_CFG2);
	}
	chip_version = (u8)HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_CFG1 + 1) >> 4;

	pHalmac_adapter->chip_version = (HALMAC_CHIP_VER)chip_version;

	if (HALMAC_CHIP_ID_HW_DEF_8822B == chip_id) {
#if HALMAC_8822B_SUPPORT
		/*mount 8822b function and data*/
		halmac_mount_api_8822b(pHalmac_adapter);
#endif

	} else if (HALMAC_CHIP_ID_HW_DEF_8821C == chip_id) {
#if HALMAC_8821C_SUPPORT
		/*mount 8822b function and data*/
		halmac_mount_api_8821c(pHalmac_adapter);
#endif
	} else if (HALMAC_CHIP_ID_HW_DEF_8197F == chip_id) {
#if HALMAC_8197F_SUPPORT
		/*mount 8822b function and data*/
		halmac_mount_api_8197f(pHalmac_adapter);
#endif
	} else {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Chip ID undefine!!\n");
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}

	pHalmac_adapter->txff_allocation.tx_fifo_pg_num = 0;
	pHalmac_adapter->txff_allocation.ac_q_pg_num = 0;
	pHalmac_adapter->txff_allocation.rsvd_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.rsvd_drv_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy = 0;
	pHalmac_adapter->txff_allocation.pub_queue_pg_num = 0;
	pHalmac_adapter->txff_allocation.high_queue_pg_num = 0;
	pHalmac_adapter->txff_allocation.low_queue_pg_num = 0;
	pHalmac_adapter->txff_allocation.normal_queue_pg_num = 0;
	pHalmac_adapter->txff_allocation.extra_queue_pg_num = 0;

	pHalmac_adapter->txff_allocation.la_mode = HALMAC_LA_MODE_DISABLE;

#if HALMAC_PLATFORM_TESTPROGRAM
	pHalmac_api->halmac_write_efuse = halmac_write_efuse_88xx;
	pHalmac_api->halmac_read_efuse = halmac_read_efuse_88xx;
	pHalmac_api->halmac_switch_efuse_bank = halmac_switch_efuse_bank_88xx;

	pHalmac_api->halmac_gen_txdesc = halmac_gen_tx_desc_88xx;
	pHalmac_api->halmac_txdesc_parser = halmac_tx_desc_parser_88xx;
	pHalmac_api->halmac_rxdesc_parser = halmac_rx_desc_parser_88xx;
	pHalmac_api->halmac_get_txdesc_size = halmac_get_txdesc_size_88xx;
	pHalmac_api->halmac_send_packet = halmac_send_packet_88xx;
	pHalmac_api->halmac_parse_packet = halmac_parse_packet_88xx;

	pHalmac_api->halmac_get_pcie_packet = halmac_get_pcie_packet_88xx;
	pHalmac_api->halmac_gen_txagg_desc = halmac_gen_txagg_desc_88xx;

	pHalmac_api->halmac_bb_reg_read = halmac_bb_reg_read_88xx;
	pHalmac_api->halmac_bb_reg_write = halmac_bb_reg_write_88xx;

	pHalmac_api->halmac_rf_reg_read = halmac_rf_ac_reg_read_88xx;
	pHalmac_api->halmac_rf_reg_write = halmac_rf_ac_reg_write_88xx;
	pHalmac_api->halmac_init_antenna_selection = halmac_init_antenna_selection_88xx;
	pHalmac_api->halmac_bb_preconfig = halmac_bb_preconfig_88xx;
	pHalmac_api->halmac_init_crystal_capacity = halmac_init_crystal_capacity_88xx;
	pHalmac_api->halmac_trx_antenna_setting = halmac_trx_antenna_setting_88xx;

	pHalmac_api->halmac_himr_setting_sdio = halmac_himr_setting_88xx_sdio;

	pHalmac_api->halmac_send_beacon = halmac_send_beacon_88xx;
	pHalmac_api->halmac_stop_beacon = halmac_stop_beacon_88xx;
	pHalmac_api->halmac_check_trx_status = halmac_check_trx_status_88xx;
	pHalmac_api->halmac_set_agg_num = halmac_set_agg_num_88xx;
	pHalmac_api->halmac_get_management_txdesc = halmac_get_management_txdesc_88xx;
	pHalmac_api->halmac_send_control = halmac_send_control_88xx;
	pHalmac_api->halmac_send_hiqueue = halmac_send_hiqueue_88xx;
	pHalmac_api->halmac_media_status_rpt = halmac_media_status_rpt_88xx;

	pHalmac_api->halmac_timer_10ms = halmac_timer_10ms_88xx;

	pHalmac_api->halmac_download_firmware_fpag = halmac_download_firmware_fpga_88xx;
	pHalmac_api->halmac_download_rom_fpga = halmac_download_rom_fpga_88xx;
	pHalmac_api->halmac_download_flash = halmac_download_flash_88xx;
	pHalmac_api->halmac_erase_flash = halmac_erase_flash_88xx;
	pHalmac_api->halmac_check_flash = halmac_check_flash_88xx;
	pHalmac_api->halmac_send_nlo = halmac_send_nlo_88xx;

	pHalmac_api->halmac_config_security = halmac_config_security_88xx;
	pHalmac_api->halmac_read_cam = halmac_read_cam_88xx;
	pHalmac_api->halmac_write_cam = halmac_write_cam_88xx;
	pHalmac_api->halmac_dump_cam_table = halmac_dump_cam_table_88xx;
	pHalmac_api->halmac_load_cam_table = halmac_load_cam_table_88xx;

	pHalmac_api->halmac_get_chip_type = halmac_get_chip_type_88xx;

	pHalmac_api->halmac_get_rx_agg_num = halmac_get_rx_agg_num_88xx;

	if (pHalmac_adapter->chip_id == HALMAC_CHIP_ID_8822B)
		pHalmac_api->halmac_run_pwrseq = halmac_run_pwrseq_8822b;
	else if (pHalmac_adapter->chip_id == HALMAC_CHIP_ID_8821C)
		pHalmac_api->halmac_run_pwrseq = halmac_run_pwrseq_8821c;

	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		pHalmac_api->halmac_reg_read_8 = halmac_reg_read_8_sdio_tp_88xx;
		pHalmac_api->halmac_reg_write_8 = halmac_reg_write_8_sdio_tp_88xx;
		pHalmac_api->halmac_reg_read_16 = halmac_reg_read_16_sdio_tp_88xx;
		pHalmac_api->halmac_reg_write_16 = halmac_reg_write_16_sdio_tp_88xx;
		pHalmac_api->halmac_reg_read_32 = halmac_reg_read_32_sdio_tp_88xx;
		pHalmac_api->halmac_reg_write_32 = halmac_reg_write_32_sdio_tp_88xx;
	}
#endif
	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_download_firmware_88xx() - download Firmware
 * @pHalmac_adapter
 * @pHamacl_fw : FW bin file
 * @halmac_fw_size
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_download_firmware_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pHamacl_fw,
	IN u32 halmac_fw_size
)
{
	u8 value8;
	u8 *pFile_ptr;
	u16 value16;
	u32 restore_index = 0;
	u32 halmac_h2c_ver = 0, fw_h2c_ver = 0;
	u32 iram_pkt_size, dmem_pkt_size, eram_pkt_size = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RESTORE_INFO restore_info[DLFW_RESTORE_REG_NUM_88XX];
	HALMAC_RET_STATUS status;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DOWNLOAD_FIRMWARE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_download_firmware_88xx ==========>\n");
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_download_firmware_88xx start!!\n");

	if (halmac_fw_size > HALMAC_FW_SIZE_MAX_88XX || halmac_fw_size < HALMAC_FWHDR_SIZE_88XX) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "FW size error!\n");
		return HALMAC_RET_FW_SIZE_ERR;
	}

	fw_h2c_ver = *((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_H2C_FORMAT_VER_88XX));
	halmac_h2c_ver = H2C_FORMAT_VERSION;
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac h2c/c2h format = %x, fw h2c/c2h format = %x!!\n", halmac_h2c_ver, fw_h2c_ver);
	if (fw_h2c_ver != halmac_h2c_ver)
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_WARN, "H2C/C2H version mismatch between HALMAC and FW, Offload Feature May fail!\n");

	pHalmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_FUNC_EN + 1);
	value8 = (u8)(value8 & ~(BIT(2)));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SYS_FUNC_EN + 1, value8); /* Disable CPU reset */


	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_TXDMA_PQ_MAP + 1;
	restore_info[restore_index].value = HALMAC_REG_READ_8(pHalmac_adapter, REG_TXDMA_PQ_MAP + 1);
	restore_index++;
	value8 = HALMAC_DMA_MAPPING_HIGH << 6;
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TXDMA_PQ_MAP + 1, value8);  /* set HIQ to hi priority */

	/* DLFW only use HIQ, map HIQ to hi priority */
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_CR;
	restore_info[restore_index].value = HALMAC_REG_READ_8(pHalmac_adapter, REG_CR);
	restore_index++;
	restore_info[restore_index].length = 4;
	restore_info[restore_index].mac_register = REG_H2CQ_CSR;
	restore_info[restore_index].value = BIT(31);
	restore_index++;
	value8 = BIT_HCI_TXDMA_EN | BIT_TXDMA_EN;
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CR, value8);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_H2CQ_CSR, BIT(31));

	/* Config hi priority queue and public priority queue page number (only for DLFW) */
	restore_info[restore_index].length = 2;
	restore_info[restore_index].mac_register = REG_FIFOPAGE_INFO_1;
	restore_info[restore_index].value = HALMAC_REG_READ_16(pHalmac_adapter, REG_FIFOPAGE_INFO_1);
	restore_index++;
	restore_info[restore_index].length = 4;
	restore_info[restore_index].mac_register = REG_RQPN_CTRL_2;
	restore_info[restore_index].value = HALMAC_REG_READ_32(pHalmac_adapter, REG_RQPN_CTRL_2) | BIT(31);
	restore_index++;
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_1, 0x200);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RQPN_CTRL_2, restore_info[restore_index - 1].value);

	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		HALMAC_REG_READ_32(pHalmac_adapter, REG_SDIO_FREE_TXPG);
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_SDIO_TX_CTRL, 0x00000000);
	}

	pHalmac_adapter->fw_version.version = rtk_le16_to_cpu(*((u16 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_VERSION_88XX)));
	pHalmac_adapter->fw_version.sub_version = *(pHamacl_fw + HALMAC_FWHDR_OFFSET_SUBVERSION_88XX);
	pHalmac_adapter->fw_version.sub_index = *(pHamacl_fw + HALMAC_FWHDR_OFFSET_SUBINDEX_88XX);

	dmem_pkt_size = *((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_DMEM_SIZE_88XX));
	iram_pkt_size = *((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_IRAM_SIZE_88XX));
	if (0 != ((*(pHamacl_fw + HALMAC_FWHDR_OFFSET_MEM_USAGE_88XX)) & BIT(4)))
		eram_pkt_size = *((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_ERAM_SIZE_88XX));

	dmem_pkt_size = rtk_le32_to_cpu(dmem_pkt_size);
	iram_pkt_size = rtk_le32_to_cpu(iram_pkt_size);
	eram_pkt_size = rtk_le32_to_cpu(eram_pkt_size);

	dmem_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	iram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;
	if (0 != eram_pkt_size)
		eram_pkt_size += HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX;

	if (halmac_fw_size != (HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size + iram_pkt_size + eram_pkt_size)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "FW size mismatch the real fw size!\n");
		goto DLFW_FAIL;
	}

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_CR + 1);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_CR + 1;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CR + 1, value8); /* Enable SW TX beacon */

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_BCN_CTRL;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)((value8 & (~BIT(3))) | BIT(4));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL, value8); /* Disable beacon related functions */

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_FWHW_TXQ_CTRL + 2);
	restore_info[restore_index].length = 1;
	restore_info[restore_index].mac_register = REG_FWHW_TXQ_CTRL + 2;
	restore_info[restore_index].value = value8;
	restore_index++;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_FWHW_TXQ_CTRL + 2, value8); /* Disable ptcl tx bcnq */

	restore_info[restore_index].length = 2;
	restore_info[restore_index].mac_register = REG_FIFOPAGE_CTRL_2;
	restore_info[restore_index].value = HALMAC_REG_READ_16(pHalmac_adapter, REG_FIFOPAGE_CTRL_2) | BIT(15);
	restore_index++;
	value16 = 0x8000;
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_CTRL_2, value16); /* Set beacon header to  0 */

	value16 = (u16)(HALMAC_REG_READ_16(pHalmac_adapter, REG_MCUFW_CTRL) & 0x3800);
	value16 |= BIT(0);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_MCUFW_CTRL, value16); /* MCU/FW setting */

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_CPU_DMEM_CON + 2);
	value8 &= ~(BIT(0));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CPU_DMEM_CON + 2, value8);
	value8 |= BIT(0);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CPU_DMEM_CON + 2, value8);

	/* Download to DMEM */
	pFile_ptr = pHamacl_fw + HALMAC_FWHDR_SIZE_88XX;
	/* if (HALMAC_RET_SUCCESS != halmac_dlfw_to_mem_88xx(pHalmac_adapter, pFile_ptr, HALMAC_OCPBASE_DMEM_88XX, dmem_pkt_size)) */
	if (HALMAC_RET_SUCCESS != halmac_dlfw_to_mem_88xx(pHalmac_adapter, pFile_ptr,
		    (*((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_DMEM_ADDR_88XX))) & ~(BIT(31)), dmem_pkt_size))
		goto DLFW_END;

	/* Download to IMEM */
	pFile_ptr = pHamacl_fw + HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size;
	/* if (HALMAC_RET_SUCCESS != halmac_dlfw_to_mem_88xx(pHalmac_adapter, pFile_ptr, HALMAC_OCPBASE_IMEM_88XX, iram_pkt_size)) */
	if (HALMAC_RET_SUCCESS != halmac_dlfw_to_mem_88xx(pHalmac_adapter, pFile_ptr,
		    (*((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_IRAM_ADDR_88XX))) & ~(BIT(31)), iram_pkt_size))
		goto DLFW_END;

	/* Download to EMEM */
	if (0 != eram_pkt_size) {
		pFile_ptr = pHamacl_fw + HALMAC_FWHDR_SIZE_88XX + dmem_pkt_size + iram_pkt_size;
		if (HALMAC_RET_SUCCESS != halmac_dlfw_to_mem_88xx(pHalmac_adapter, pFile_ptr,
			    (*((u32 *)(pHamacl_fw + HALMAC_FWHDR_OFFSET_EMEM_ADDR_88XX))) & ~(BIT(31)), eram_pkt_size))
			goto DLFW_END;
	}

DLFW_END:

	halmac_restore_mac_register_88xx(pHalmac_adapter, restore_info, DLFW_RESTORE_REG_NUM_88XX);

	if (HALMAC_RET_SUCCESS != halmac_dlfw_end_flow_88xx(pHalmac_adapter))
		goto DLFW_FAIL;

	pHalmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_DONE;


	if (_TRUE == pHalmac_adapter->gen_info_valid) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Auto send halmac_send_general_info after redownload fw\n");
		status = halmac_send_general_info_88xx(pHalmac_adapter, &(pHalmac_adapter->general_info));

		if (HALMAC_RET_SUCCESS != status) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_send_general_info error = %x\n", status);
			return status;
		}
		if (HALMAC_DLFW_DONE == pHalmac_adapter->halmac_state.dlfw_state)
			pHalmac_adapter->halmac_state.dlfw_state = HALMAC_GEN_INFO_SENT;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_download_firmware_88xx <==========\n");

	return HALMAC_RET_SUCCESS;

DLFW_FAIL:

	/* Disable FWDL_EN */
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MCUFW_CTRL, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_MCUFW_CTRL) & ~(BIT(0))));

	return HALMAC_RET_DLFW_FAIL;
}

/**
 * halmac_get_fw_version_88xx() - get FW version
 * @pHalmac_adapter
 * @pFw_version
 * Author : Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_fw_version_88xx(
	IN PHALMAC_ADAPTER	pHalmac_adapter,
	OUT PHALMAC_FW_VERSION	pFw_version
)
{
	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (0 == pHalmac_adapter->halmac_state.dlfw_state) {
		return HALMAC_RET_DLFW_FAIL;
	} else {
		pFw_version->version = pHalmac_adapter->fw_version.version;
		pFw_version->sub_version = pHalmac_adapter->fw_version.sub_version;
		pFw_version->sub_index = pHalmac_adapter->fw_version.sub_index;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_mac_addr_88xx() - config Mac Address
 * @pHalmac_adapter
 * @halmac_port : 0 : port0 1 : port1
 * @pHal_address : mac address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_mac_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 halmac_port,
	IN PHALMAC_WLAN_ADDR pHal_address
)
{
	u16 mac_address_H;
	u32 mac_address_L;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_MAC_ADDR);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_mac_addr_88xx ==========>\n");

	if (halmac_port > 2) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "port index > 2\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	mac_address_L = pHal_address->Address_L_H.Address_Low;
	mac_address_H = pHal_address->Address_L_H.Address_High;

	mac_address_L = rtk_le32_to_cpu(mac_address_L);
	mac_address_H = rtk_le16_to_cpu(mac_address_H);

	pHalmac_adapter->pHal_mac_addr[halmac_port].Address_L_H.Address_Low = mac_address_L;
	pHalmac_adapter->pHal_mac_addr[halmac_port].Address_L_H.Address_High = mac_address_H;

	if (0 == halmac_port) {
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MACID, mac_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_MACID + 4, mac_address_H);
	} else {
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MACID1, mac_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_MACID1 + 4, mac_address_H);
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_mac_addr_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_bssid_88xx() - config BSSID
 * @pHalmac_adapter
 * @halmac_port : 0 : port0 1 : port1
 * @pHal_address : mac address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_bssid_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 halmac_port,
	IN PHALMAC_WLAN_ADDR pHal_address
)
{
	u16 bssid_address_H;
	u32 bssid_address_L;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_BSSID);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_bssid_88xx ==========>\n");

	if (halmac_port > 2) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "port index > 2\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	bssid_address_L = pHal_address->Address_L_H.Address_Low;
	bssid_address_H = pHal_address->Address_L_H.Address_High;

	bssid_address_L = rtk_le32_to_cpu(bssid_address_L);
	bssid_address_H = rtk_le16_to_cpu(bssid_address_H);

	pHalmac_adapter->pHal_bss_addr[halmac_port].Address_L_H.Address_Low = bssid_address_L;
	pHalmac_adapter->pHal_bss_addr[halmac_port].Address_L_H.Address_High = bssid_address_H;

	if (0 == halmac_port) {
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_BSSID, bssid_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_BSSID + 4, bssid_address_H);
	} else {
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_BSSID1, bssid_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_BSSID1 + 4, bssid_address_H);
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_bssid_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_multicast_addr_88xx() - config multicast address
 * @pHalmac_adapter
 * @pHal_address : mac address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_multicast_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_WLAN_ADDR pHal_address
)
{
	u16 address_H;
	u32 address_L;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_MULTICAST_ADDR);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_multicast_addr_88xx ==========>\n");

	address_L = pHal_address->Address_L_H.Address_Low;
	address_H = pHal_address->Address_L_H.Address_High;

	address_L = rtk_le32_to_cpu(address_L);
	address_H = rtk_le16_to_cpu(address_H);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MAR, address_L);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_MAR + 4, address_H);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_multicast_addr_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pre_init_system_cfg_88xx() - config system register before power on
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_pre_init_system_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	u32 value32;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	u8 enable_bb;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_PRE_INIT_SYSTEM_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_pre_init_system_cfg ==========>\n");

	/* Config PIN Mux */
	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_PAD_CTRL1);
	value32 = value32 & (~(BIT(28) | BIT(29)));
	value32 = value32 | BIT(28) | BIT(29);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_PAD_CTRL1, value32);

	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_LED_CFG);
	value32 = value32 & (~(BIT(25) | BIT(26)));
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_LED_CFG, value32);

	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_GPIO_MUXCFG);
	value32 = value32 & (~(BIT(2)));
	value32 = value32 | BIT(2);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_GPIO_MUXCFG, value32);

	enable_bb = _FALSE;
	halmac_set_hw_value_88xx(pHalmac_adapter, HALMAC_HW_EN_BB_RF, &enable_bb);


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_pre_init_system_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_system_cfg_88xx() - config system register after power on
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_system_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;


	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_SYSTEM_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_system_cfg ==========>\n");

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SYS_FUNC_EN + 1, HALMAC_FUNCTION_ENABLE_88XX);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_SYS_SDIO_CTRL, (u32)(HALMAC_REG_READ_32(pHalmac_adapter, REG_SYS_SDIO_CTRL) | BIT_LTE_MUX_CTRL_PATH));
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_CPU_DMEM_CON, (u32)(HALMAC_REG_READ_32(pHalmac_adapter, REG_CPU_DMEM_CON) | BIT_WL_PLATFORM_RST));

	/* pHalmac_api->halmac_init_h2c(pHalmac_adapter); */

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_system_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_protocol_cfg_88xx() - config protocol related register
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_protocol_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	u16 value16;
	u32 value32;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_PROTOCOL_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_protocol_cfg_88xx ==========>\n");

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BASIC_CFEND_RATE, HALMAC_BASIC_CFEND_RATE_88XX);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_STBC_CFEND_RATE, HALMAC_STBC_CFEND_RATE_88XX);

	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_RRSR);
	value32 = (value32 & ~BIT_MASK_RRSC_BITMAP) | HALMAC_RESPONSE_RATE_88XX;
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RRSR, value32);

	value16 = HALMAC_SIFS_CCK_PTCL_88XX | (HALMAC_SIFS_OFDM_PTCL_88XX << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_SPEC_SIFS, value16);

	value16 = BIT_LRL(HALMAC_LONG_RETRY_LIMIT_88XX) | BIT_SRL(HALMAC_SHORT_RETRY_LIMIT_88XX);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RETRY_LIMIT, value16);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_protocol_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_edca_cfg_88xx() - config EDCA register
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_edca_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	u8 value8;
	u32 value32;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_EDCA_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_edca_cfg_88xx ==========>\n");

	/* Clear TX pause */
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXPAUSE, 0x0000);

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SLOT, HALMAC_SLOT_TIME_88XX);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_PIFS, HALMAC_PIFS_TIME_88XX);
	value32 = HALMAC_SIFS_CCK_CTX_88XX | (HALMAC_SIFS_OFDM_CTX_88XX << BIT_SHIFT_SIFS_OFDM_CTX) |
		  (HALMAC_SIFS_CCK_TRX_88XX << BIT_SHIFT_SIFS_CCK_TRX) | (HALMAC_SIFS_OFDM_TRX_88XX << BIT_SHIFT_SIFS_OFDM_TRX);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_SIFS, value32);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_EDCA_VO_PARAM, HALMAC_REG_READ_32(pHalmac_adapter, REG_EDCA_VO_PARAM) & 0xFFFF);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_EDCA_VO_PARAM + 2, HALMAC_VO_TXOP_LIMIT_88XX);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_EDCA_VI_PARAM + 2, HALMAC_VI_TXOP_LIMIT_88XX);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RD_NAV_NXT, HALMAC_RDG_NAV_88XX | (HALMAC_TXOP_NAV_88XX << 16));
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RXTSF_OFFSET_CCK, HALMAC_CCK_RX_TSF_88XX | (HALMAC_OFDM_RX_TSF_88XX) << 8);

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_RD_CTRL + 1);
	value8 |= (BIT_VOQ_RD_INIT_EN | BIT_VIQ_RD_INIT_EN | BIT_BEQ_RD_INIT_EN);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RD_CTRL + 1, value8);

	/* Set beacon cotrol - enable TSF and other related functions */
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL) | BIT_EN_BCN_FUNCTION));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL_CLINT0, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL_CLINT0) | BIT_CLI0_EN_BCN_FUNCTION));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL_CLINT1, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL_CLINT1) | BIT_CLI1_EN_BCN_FUNCTION));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL_CLINT2, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL_CLINT2) | BIT_CLI2_EN_BCN_FUNCTION));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCN_CTRL_CLINT3, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_BCN_CTRL_CLINT3) | BIT_CLI3_EN_BCN_FUNCTION));

	/* Set send beacon related registers */
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_TBTT_PROHIBIT, HALMAC_TBTT_PROHIBIT_88XX | (HALMAC_TBTT_HOLD_TIME_88XX << BIT_SHIFT_TBTT_HOLD_TIME_AP));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_DRVERLYINT, HALMAC_DRIVER_EARLY_INT_88XX);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BCNDMATIM, HALMAC_BEACON_DMA_TIM_88XX);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_edca_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_wmac_cfg_88xx() - config WMAC register
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_wmac_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_WMAC_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_wmac_cfg_88xx ==========>\n");

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RXFLTMAP0, HALMAC_RX_FILTER0_88XX);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RXFLTMAP, HALMAC_RX_FILTER_88XX);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RCR, HALMAC_RCR_CONFIG_88XX);

	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_SECCFG, HALMAC_SECURITY_CONFIG_88XX);

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TCR + 1, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_TCR + 1) | 0x30));
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TCR + 2, 0x30);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TCR + 1, 0x00);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WMAC_OPTION_FUNCTION + 8, 0x30810041);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WMAC_OPTION_FUNCTION + 4, 0x50802080);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1, 0xC00F0038);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_wmac_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_mac_cfg_88xx() - config page1~page7 register
 * @pHalmac_adapter
 * @mode : normal, trxshare, wmm, p2p, loopback
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_mac_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE mode
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_MAC_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_mac_cfg_88xx ==========>mode = %d\n", mode);

	status = pHalmac_api->halmac_init_trx_cfg(pHalmac_adapter, mode);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg errorr = %x\n", status);
		return status;
	}
#if 1
	status = halmac_init_protocol_cfg_88xx(pHalmac_adapter);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_protocol_cfg_88xx error = %x\n", status);
		return status;
	}

	status = halmac_init_edca_cfg_88xx(pHalmac_adapter);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_edca_cfg_88xx error = %x\n", status);
		return status;
	}

	status = halmac_init_wmac_cfg_88xx(pHalmac_adapter);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_wmac_cfg_88xx error = %x\n", status);
		return status;
	}
#endif
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_mac_cfg_88xx <==========\n");

	return status;
}

/**
 * halmac_cfg_operation_mode_88xx() - config operation mode
 * @pHalmac_adapter
 * @wireless_mode : b/g/n/ac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_operation_mode_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_WIRELESS_MODE wireless_mode
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_WIRELESS_MODE wireless_mode_local = HALMAC_WIRELESS_MODE_UNDEFINE;

	wireless_mode_local = wireless_mode;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_OPERATION_MODE);
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_operation_mode_88xx ==========>wireless_mode = %d\n", wireless_mode);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_operation_mode_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_bw_88xx() - config channel & bandwidth
 * @pHalmac_adapter
 * @channel : WLAN channel, support 2.4G & 5G
 * @pri_ch_idx : idx1, idx2, idx3, idx4
 * @bw : 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_ch_bw_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 channel,
	IN HALMAC_PRI_CH_IDX pri_ch_idx,
	IN HALMAC_BW bw
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_CH_BW);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_ch_bw_88xx ==========>ch = %d, idx=%d, bw=%d\n", channel, pri_ch_idx, bw);

	halmac_cfg_pri_ch_idx_88xx(pHalmac_adapter,  pri_ch_idx);

	halmac_cfg_bw_88xx(pHalmac_adapter, bw);

	halmac_cfg_ch_88xx(pHalmac_adapter,  channel);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_ch_bw_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_cfg_bw_88xx() - config channel & bandwidth
 * @pHalmac_adapter
 * @channel : WLAN channel, support 2.4G & 5G
 * @pri_ch_idx : idx1, idx2, idx3, idx4
 * @bw : 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_ch_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 channel
)
{
	u8 value8;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_CH_BW);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_ch_88xx ==========>ch = %d\n", channel);

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_CCK_CHECK);
	value8 = value8 & (~(BIT(7)));

	if (channel > 35)
		value8 = value8 | BIT(7);

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CCK_CHECK, value8);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_ch_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_bw_88xx() - config channel & bandwidth
 * @pHalmac_adapter
 * @channel : WLAN channel, support 2.4G & 5G
 * @pri_ch_idx : idx1, idx2, idx3, idx4
 * @bw : 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_pri_ch_idx_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PRI_CH_IDX pri_ch_idx
)
{
	u8 txsc_40 = 0, txsc_20 = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_CH_BW);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_pri_ch_idx_88xx ==========> idx=%d\n",  pri_ch_idx);

	txsc_20 = pri_ch_idx;
	if ((HALMAC_CH_IDX_1 == txsc_20) || (HALMAC_CH_IDX_3 == txsc_20))
		txsc_40 = 9;
	else
		txsc_40 = 10;

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_DATA_SC, BIT_TXSC_20M(txsc_20) | BIT_TXSC_40M(txsc_40));

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_pri_ch_idx_88xx <==========\n");

	return HALMAC_RET_SUCCESS;

}

/**
 * halmac_cfg_bw_88xx() - config bandwidth
 * @pHalmac_adapter
 * @bw : 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_bw_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_BW bw
)
{
	u32 value32;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_BW);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_bw_88xx ==========>bw=%d\n", bw);

	/* RF Mode */
	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_WMAC_TRXPTCL_CTL);
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
		value32 = value32;
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_cfg_bw_88xx switch case not support\n");
		break;
	}
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WMAC_TRXPTCL_CTL, value32);

	/* MAC CLK */
	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_AFE_CTRL1);
	value32 = (value32 & (~(BIT(20) | BIT(21)))) | (HALMAC_MAC_CLOCK_HW_DEF_80M << BIT_SHIFT_MAC_CLK_SEL);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_AFE_CTRL1, value32);

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_USTIME_TSF, HALMAC_MAC_CLOCK_88XX);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_USTIME_EDCA, HALMAC_MAC_CLOCK_88XX);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_bw_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_clear_security_cam_88xx() - clear security CAM
 * @pHalmac_adapter
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_clear_security_cam_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_clear_security_cam_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_clear_security_cam_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_efuse_map_88xx() - dump "physical" efuse map
 * @pHalmac_adapter
 * @cfg : dump with auto/driver/FW
 * Author : Ivan Lin/KaiYuan Chang
 *
 * halmac_dump_efuse_map_88xx is async architecture, user can
 * refer to DumpEfuseMap page of FlowChart.vsd.
 * dump_efuse_map page of Halmac_flow_control.vsd is halmac api control
 * flow, only for SD1 internal use.
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_dump_efuse_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_READ_CFG cfg
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DUMP_EFUSE_MAP);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_dump_efuse_map_88xx ==========>cfg=%d\n", cfg);

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_IDLE;
	pHalmac_adapter->event_trigger.physical_efuse_map = 1;

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, HALMAC_EFUSE_BANK_WIFI);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_dump_efuse_88xx(pHalmac_adapter, cfg);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_read_efuse error = %x\n", status);
		return status;
	}

	if (_TRUE == pHalmac_adapter->hal_efuse_map_valid) {
		*pProcess_status = HALMAC_CMD_PROCESS_DONE;

		PLATFORM_EVENT_INDICATION(pDriver_adapter, HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE, *pProcess_status,
			pHalmac_adapter->pHalEfuse_map, pHalmac_adapter->hw_config_info.efuse_size);
		pHalmac_adapter->event_trigger.physical_efuse_map = 0;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_dump_efuse_map_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_efuse_map_bt_88xx() - dump "BT physical" efuse map
 * @pHalmac_adapter
 * @cfg : dump with auto/driver/FW
 * Author : Soar / Ivan Lin
 *
 * halmac_dump_efuse_map_bt_88xx is async architecture, user can
 * refer to DumpEfuseMap page of FlowChart.vsd.
 * dump_efuse_map_bt page of Halmac_flow_control.vsd is halmac api control
 * flow, only for SD1 internal use.
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_dump_efuse_map_bt_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_BANK halmac_efuse_bank,
	IN u32 bt_efuse_map_size,
	OUT u8 *pBT_efuse_map
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DUMP_EFUSE_MAP_BT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_dump_efuse_map_bt_88xx ==========>\n");

	if (pHalmac_adapter->hw_config_info.bt_efuse_size != bt_efuse_map_size)
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;

	if ((halmac_efuse_bank >= HALMAC_EFUSE_BANK_MAX) || (halmac_efuse_bank == HALMAC_EFUSE_BANK_WIFI)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, halmac_efuse_bank);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_read_hw_efuse_88xx(pHalmac_adapter, 0, bt_efuse_map_size, pBT_efuse_map);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_read_hw_efuse_88xx error = %x\n", status);
		return status;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_dump_efuse_map_bt_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_read_efuse_88xx() - read "physical" efuse offset
 * @pHalmac_adapter
 * @halmac_offset
 * @pValue
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
/*
 * HALMAC_RET_STATUS
 * halmac_read_efuse_88xx(
 *	IN PHALMAC_ADAPTER pHalmac_adapter,
 *	IN u32 halmac_offset,
 *	OUT u8 *pValue
 *)
 */

/**
 * halmac_write_efuse_88xx() - write "physical" efuse offset
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_value
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
/*
 * HALMAC_RET_STATUS
 * halmac_write_efuse_88xx(
 *	IN PHALMAC_ADAPTER	pHalmac_adapter,
 *	IN u32 halmac_offset,
 *	IN u8 halmac_value
 *)
 */

/**
 * halmac_write_efuse_bt_88xx() - write "BT physical" efuse offset
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_value
 * Author : Soar
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_write_efuse_bt_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_value,
	IN HALMAC_EFUSE_BANK halmac_efuse_bank
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;


	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_WRITE_EFUSE_BT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_write_efuse_bt_88xx ==========>\n");
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "offset : %X value : %X Bank : %X\n", halmac_offset, halmac_value, halmac_efuse_bank);

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (halmac_offset >= pHalmac_adapter->hw_config_info.efuse_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((halmac_efuse_bank > HALMAC_EFUSE_BANK_MAX) || (halmac_efuse_bank == HALMAC_EFUSE_BANK_WIFI)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, halmac_efuse_bank);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_write_efuse_88xx(pHalmac_adapter, halmac_offset, halmac_value);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_write_efuse error = %x\n", status);
		return status;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_write_efuse_bt_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_efuse_available_size_88xx() - get efuse available size
 * @pHalmac_adapter : the adapter of halmac
 * @halmac_size : physical efuse available size
 * Author : Soar
 * Return : HALMAC_RET_STATUS
 * More details of status code can be found in prototype document
 */
HALMAC_RET_STATUS
halmac_get_efuse_available_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
)
{
	HALMAC_RET_STATUS status;
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_efuse_available_size_88xx ==========>\n");

	status = halmac_dump_logical_efuse_map_88xx(pHalmac_adapter, HALMAC_EFUSE_R_DRV);

	if (HALMAC_RET_SUCCESS != status)
		return status;

	*halmac_size = pHalmac_adapter->hw_config_info.efuse_size - HALMAC_PROTECTED_EFUSE_SIZE_88XX - pHalmac_adapter->efuse_end;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_efuse_available_size_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_efuse_size_88xx() - get "physical" efuse size
 * @pHalmac_adapter
 * @halmac_size : Output physical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_efuse_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_EFUSE_SIZE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_efuse_size_88xx ==========>\n");

	*halmac_size = pHalmac_adapter->hw_config_info.efuse_size;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_efuse_size_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_logical_efuse_size_88xx() - get "logical" efuse size
 * @pHalmac_adapter
 * @halmac_size : Output logical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_logical_efuse_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_LOGICAL_EFUSE_SIZE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_logical_efuse_size_88xx ==========>\n");

	*halmac_size = pHalmac_adapter->hw_config_info.eeprom_size;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_get_logical_efuse_size_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_logical_efuse_map_88xx() - dump "logical" efuse map
 * @pHalmac_adapter
 * @cfg : dump with auto/driver/FW
 * Author : Soar
 *
 * halmac_dump_logical_efuse_map_88xx is async architecture, user can
 * refer to DumpEEPROMMap page of FlowChart.vsd.
 * dump_efuse_map page of Halmac_flow_control.vsd is halmac api control
 * flow, only for SD1 internal use.
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_dump_logical_efuse_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_READ_CFG cfg
)
{
	u8 *pEeprom_map = NULL;
	u32 eeprom_size = pHalmac_adapter->hw_config_info.eeprom_size;
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DUMP_LOGICAL_EFUSE_MAP);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_dump_logical_efuse_map_88xx ==========>cfg = %d\n", cfg);

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_IDLE;
	pHalmac_adapter->event_trigger.logical_efuse_map = 1;

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, HALMAC_EFUSE_BANK_WIFI);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_dump_efuse_88xx(pHalmac_adapter, cfg);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_eeprom_parser_88xx error = %x\n", status);
		return status;
	}

	if (_TRUE == pHalmac_adapter->hal_efuse_map_valid) {
		*pProcess_status = HALMAC_CMD_PROCESS_DONE;

		pEeprom_map = (u8 *)PLATFORM_RTL_MALLOC(pDriver_adapter, eeprom_size);
		if (NULL == pEeprom_map) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac allocate local eeprom map Fail!!\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLATFORM_RTL_MEMSET(pDriver_adapter, pEeprom_map, 0xFF, eeprom_size);

		if (HALMAC_RET_SUCCESS != halmac_eeprom_parser_88xx(pHalmac_adapter, pHalmac_adapter->pHalEfuse_map, pEeprom_map))
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		PLATFORM_EVENT_INDICATION(pDriver_adapter, HALMAC_FEATURE_DUMP_LOGICAL_EFUSE, *pProcess_status, pEeprom_map, eeprom_size);
		pHalmac_adapter->event_trigger.logical_efuse_map = 0;

		PLATFORM_RTL_FREE(pDriver_adapter, pEeprom_map, eeprom_size);
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_dump_logical_efuse_map_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_read_logical_efuse_88xx() - read "logical" efuse offset
 * @pHalmac_adapter
 * @halmac_offset
 * @pValue
 * Author : Soar
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_read_logical_efuse_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	OUT u8 *pValue
)
{
	u8 *pEeprom_map = NULL;
	u32 eeprom_size = pHalmac_adapter->hw_config_info.eeprom_size;
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_READ_LOGICAL_EFUSE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_read_logical_efuse_88xx ==========>\n");

	if (halmac_offset >= eeprom_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}
	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	pEeprom_map = (u8 *)PLATFORM_RTL_MALLOC(pDriver_adapter, eeprom_size);
	if (NULL == pEeprom_map) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac allocate local eeprom map Fail!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLATFORM_RTL_MEMSET(pDriver_adapter, pEeprom_map, 0xFF, eeprom_size);

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, HALMAC_EFUSE_BANK_WIFI);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_read_logical_efuse_map_88xx(pHalmac_adapter, pEeprom_map);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_read_logical_efuse_map error = %x\n", status);
		return status;
	}

	*pValue = *(pEeprom_map + halmac_offset);

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_read_logical_efuse_88xx <==========\n");

	PLATFORM_RTL_FREE(pDriver_adapter, pEeprom_map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_write_logical_efuse_88xx() - write "logical" efuse offset
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_value
 * Author : Soar
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_write_logical_efuse_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_value
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_WRITE_LOGICAL_EFUSE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_write_logical_efuse_88xx ==========>\n");

	if (halmac_offset >= pHalmac_adapter->hw_config_info.eeprom_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, HALMAC_EFUSE_BANK_WIFI);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_write_logical_efuse_88xx(pHalmac_adapter, halmac_offset, halmac_value);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_write_logical_efuse error = %x\n", status);
		return status;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_write_logical_efuse_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pg_efuse_by_map_88xx() - pg efuse by map
 * @pHalmac_adapter
 * @pPg_efuse_info : map, map size, mask, mask size
 * @cfg : dump with auto/driver/FW
 * Author : Soar
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_pg_efuse_by_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PG_EFUSE_INFO pPg_efuse_info,
	IN HALMAC_EFUSE_READ_CFG cfg
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.efuse_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_PG_EFUSE_BY_MAP);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_pg_efuse_by_map_88xx ==========>\n");

	if (pPg_efuse_info->efuse_map_size != pHalmac_adapter->hw_config_info.eeprom_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "efuse_map_size is incorrect, should be %d bytes\n", pHalmac_adapter->hw_config_info.eeprom_size);
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((pPg_efuse_info->efuse_map_size & 0xF) > 0) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "efuse_map_size should be multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (pPg_efuse_info->efuse_mask_size != pPg_efuse_info->efuse_map_size >> 4) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "efuse_mask_size is incorrect, should be %d bytes\n", pPg_efuse_info->efuse_map_size >> 4);
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (NULL == pPg_efuse_info->pEfuse_map) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "efuse_map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (NULL == pPg_efuse_info->pEfuse_mask) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "efuse_mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait/Rcvd event(dump efuse)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (HALMAC_EFUSE_CMD_CONSTRUCT_IDLE != halmac_query_efuse_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(dump efuse)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = halmac_func_switch_efuse_bank_88xx(pHalmac_adapter, HALMAC_EFUSE_BANK_WIFI);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_func_switch_efuse_bank error = %x\n", status);
		return status;
	}

	status = halmac_func_pg_efuse_by_map_88xx(pHalmac_adapter, pPg_efuse_info, cfg);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_pg_efuse_by_map error = %x\n", status);
		return status;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_efuse_state_88xx(pHalmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_IDLE))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_TRACE, "halmac_pg_efuse_by_map_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_c2h_info_88xx() - process halmac C2H packet
 * @pHalmac_adapter
 * @halmac_buf
 * @halmac_size
 * Author : KaiYuan Chang/Ivan Lin
 *
 * Used to process c2h packet info from RX path. After receiving the packet,
 * user need to call this api and pass the packet pointer.
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_c2h_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *halmac_buf,
	IN u32 halmac_size
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;


	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_C2H_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_c2h_info_88xx ==========>\n"); */

	/* Check if it is C2H packet */
	if (_TRUE == GET_RX_DESC_C2H(halmac_buf)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "C2H packet, start parsing!\n");

		status = halmac_parse_c2h_packet_88xx(pHalmac_adapter, halmac_buf, halmac_size);

		if (HALMAC_RET_SUCCESS != status) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_EFUSE, HALMAC_DBG_ERR, "halmac_parse_c2h_packet_88xx error = %x\n", status);
			return status;
		}
	}

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_c2h_info_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_fwlps_option_88xx() -config FW LPS option
 * @pHalmac_adapter
 * @pLps_option : refer to HALMAC_FWLPS_OPTION structure
 * Author : KaiYuan Chang/Ivan Lin
 *
 * Used to config FW LPS option. If user has called this function,
 * halmac uses this setting to run FW LPS. If user never called this function,
 * halmac uses default setting to run FW LPS
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_fwlps_option_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_FWLPS_OPTION pLps_option
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_FWLPS_OPTION pHal_fwlps_option;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_FWLPS_OPTION);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHal_fwlps_option = &(pHalmac_adapter->fwlps_option);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_cfg_fwlps_option_88xx ==========>\n");

	pHal_fwlps_option->mode = pLps_option->mode;
	pHal_fwlps_option->clk_request = pLps_option->clk_request;
	pHal_fwlps_option->rlbm = pLps_option->rlbm;
	pHal_fwlps_option->smart_ps = pLps_option->smart_ps;
	pHal_fwlps_option->awake_interval = pLps_option->awake_interval;
	pHal_fwlps_option->all_queue_uapsd = pLps_option->all_queue_uapsd;
	pHal_fwlps_option->pwr_state = pLps_option->pwr_state;
	pHal_fwlps_option->low_pwr_rx_beacon = pLps_option->low_pwr_rx_beacon;
	pHal_fwlps_option->ant_auto_switch = pLps_option->ant_auto_switch;
	pHal_fwlps_option->ps_allow_bt_high_Priority = pLps_option->ps_allow_bt_high_Priority;
	pHal_fwlps_option->protect_bcn = pLps_option->protect_bcn;
	pHal_fwlps_option->silence_period = pLps_option->silence_period;
	pHal_fwlps_option->fast_bt_connect = pLps_option->fast_bt_connect;
	pHal_fwlps_option->two_antenna_en = pLps_option->two_antenna_en;
	pHal_fwlps_option->adopt_user_Setting = pLps_option->adopt_user_Setting;
	pHal_fwlps_option->drv_bcn_early_shift = pLps_option->drv_bcn_early_shift;
	pHal_fwlps_option->enter_32K = pLps_option->enter_32K;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_cfg_fwlps_option_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_fwips_option_88xx() -config FW IPS option
 * @pHalmac_adapter
 * @pIps_option : refer to HALMAC_FWIPS_OPTION structure
 * Author : KaiYuan Chang/Ivan Lin
 *
 * Used to config FW IPS option. If user has called this function,
 * halmac uses this setting to run FW IPS. If user never called this function,
 * halmac uses default setting to run FW IPS
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_fwips_option_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_FWIPS_OPTION pIps_option
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_FWIPS_OPTION pIps_option_local;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_FWIPS_OPTION);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_cfg_fwips_option_88xx ==========>\n");

	pIps_option_local = pIps_option;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_cfg_fwips_option_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_enter_wowlan_88xx() - enter wowlan
 * @pHalmac_adapter
 * @pWowlan_option
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_enter_wowlan_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_WOWLAN_OPTION pWowlan_option
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_WOWLAN_OPTION pWowlan_option_local;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_ENTER_WOWLAN);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_enter_wowlan_88xx ==========>\n");

	pWowlan_option_local = pWowlan_option;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_enter_wowlan_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_leave_wowlan_88xx() - leave wowlan
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_leave_wowlan_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_LEAVE_WOWLAN);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_leave_wowlan_88xx ==========>\n");


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_leave_wowlan_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_enter_ps_88xx() - enter power saving state
 * @pHalmac_adapter
 * @ps_state
 *
 * If user has called halmac_cfg_fwlps_option or
 * halmac_cfg_fwips_option, halmac uses the specified setting.
 * Otherwise, halmac uses default setting.
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_enter_ps_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PS_STATE ps_state
)
{
	u8 rpwm;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_ENTER_PS);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_enter_ps_88xx ==========>\n");

	if (ps_state == pHalmac_adapter->halmac_state.ps_state) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "power state is already in PS State!!\n");
		return HALMAC_RET_SUCCESS;
	}

	if (HALMAC_PS_STATE_LPS == ps_state) {
		status = halmac_send_h2c_set_pwr_mode_88xx(pHalmac_adapter, &(pHalmac_adapter->fwlps_option));
		if (HALMAC_RET_SUCCESS != status) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_send_h2c_set_pwr_mode_88xx error = %x!!\n", status);
			return status;
		}
	} else if (HALMAC_PS_STATE_IPS == ps_state) {
	}

	pHalmac_adapter->halmac_state.ps_state = ps_state;

	/* Enter 32K */
	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		if (_TRUE == pHalmac_adapter->fwlps_option.enter_32K) {
			rpwm = (u8)(((pHalmac_adapter->rpwm_record ^ (BIT(7))) | (BIT(0))) & 0x81);
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SDIO_HRPWM1, rpwm);
			pHalmac_adapter->low_clk = _TRUE;
		}
	} else if (HALMAC_INTERFACE_USB == pHalmac_adapter->halmac_interface) {
		if (_TRUE == pHalmac_adapter->fwlps_option.enter_32K) {
			rpwm = (u8)(((pHalmac_adapter->rpwm_record ^ (BIT(7))) | (BIT(0))) & 0x81);
			HALMAC_REG_WRITE_8(pHalmac_adapter, 0xFE58, rpwm);
			pHalmac_adapter->low_clk = _TRUE;
		}
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_enter_ps_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_leave_ps_88xx() - leave power saving state
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_leave_ps_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	u8 rpwm, cpwm;
	u32 counter;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_FWLPS_OPTION fw_lps_option;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;


	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_LEAVE_PS);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_leave_ps_88xx ==========>\n");

	if (HALMAC_PS_STATE_ACT == pHalmac_adapter->halmac_state.ps_state) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "power state is already in active!!\n");
		return HALMAC_RET_SUCCESS;
	}

	if (_TRUE == pHalmac_adapter->low_clk) {
		cpwm = HALMAC_REG_READ_8(pHalmac_adapter, REG_SDIO_HRPWM1);
		rpwm = (u8)(((pHalmac_adapter->rpwm_record ^ (BIT(7))) | (BIT(6))) & 0xC0);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SDIO_HRPWM1, rpwm);

		cpwm = (u8)((cpwm ^ BIT(7)) & BIT(7));
		counter = 100;
		while (cpwm != (HALMAC_REG_READ_8(pHalmac_adapter, REG_SDIO_HRPWM1) & BIT(7))) {
			PLATFORM_RTL_DELAY_US(pDriver_adapter, 50);
			counter--;
			if (0 == counter)
				return HALMAC_RET_CHANGE_PS_FAIL;
		}
		pHalmac_adapter->low_clk = _FALSE;
	}

	PLATFORM_RTL_MEMCPY(pDriver_adapter, &fw_lps_option, &(pHalmac_adapter->fwlps_option), sizeof(HALMAC_FWLPS_OPTION));
	fw_lps_option.mode = 0;

	status = halmac_send_h2c_set_pwr_mode_88xx(pHalmac_adapter, &(fw_lps_option));
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_send_h2c_set_pwr_mode_88xx error!!=%x\n", status);
		return status;
	}

	pHalmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_TRACE, "halmac_leave_ps_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_h2c_lb_88xx() - send h2c loopback packet
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_h2c_lb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_H2C_LB);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_h2c_lb_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_h2c_lb_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_debug_88xx() - read some registers for debug
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_debug_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	u8 temp8 = 0;
	u32 i = 0, temp32 = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DEBUG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug_88xx ==========>\n");

	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		/* Dump CCCR, it needs new platform api */

		/*Dump SDIO Local Register, use CMD52*/
		for (i = 0x10250000; i < 0x102500ff; i++) {
			temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: sdio[%x]=%x\n", i, temp8);
		}

		/*Dump MAC Register*/
		for (i = 0x0000; i < 0x17ff; i++) {
			temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp8);
		}

		/*Check RX Fifo status*/
		i = REG_RXFF_PTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_WTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_PTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp8);
		i = REG_RXFF_WTR_V1;
		temp8 = PLATFORM_SDIO_CMD52_READ(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp8);
	} else {
		/*Dump MAC Register*/
		for (i = 0x0000; i < 0x17fc; i += 4) {
			temp32 = HALMAC_REG_READ_32(pHalmac_adapter, i);
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp32);
		}

		/*Check RX Fifo status*/
		i = REG_RXFF_PTR_V1;
		temp32 = HALMAC_REG_READ_32(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_WTR_V1;
		temp32 = HALMAC_REG_READ_32(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_PTR_V1;
		temp32 = HALMAC_REG_READ_32(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp32);
		i = REG_RXFF_WTR_V1;
		temp32 = HALMAC_REG_READ_32(pHalmac_adapter, i);
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug: mac[%x]=%x\n", i, temp32);
	}

	/*	TODO: Add check register code, including MAC CLK, CPU CLK */

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_debug_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_parameter_88xx() - config register with register array
 * @pHalmac_adapter
 * @para_info : cmd id, content
 * @full_fifo
 *
 * If msk_en = _TRUE, the format of array is {reg_info, mask, value}.
 * If msk_en =_FAUSE, the format of array is {reg_info, value}
 * The format of reg_info is
 * reg_info[31]=rf_reg, 0: MAC_BB reg, 1: RF reg
 * reg_info[27:24]=rf_path, 0: path_A, 1: path_B
 * if rf_reg=0(MAC_BB reg), rf_path is meaningless.
 * ref_info[15:0]=offset
 *
 * Example: msk_en = _FALSE
 * {0x8100000a, 0x00001122}
 * =>Set RF register, path_B, offset 0xA to 0x00001122
 * {0x00000824, 0x11224433}
 * =>Set MAC_BB register, offset 0x800 to 0x11224433
 *
 * Note : full fifo mode only for init flow
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_parameter_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PHY_PARAMETER_INFO para_info,
	IN u8 full_fifo
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS ret_status = HALMAC_RET_SUCCESS;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.cfg_para_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_PARAMETER);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_parameter_88xx ==========>\n"); */

	if (HALMAC_DLFW_NONE == pHalmac_adapter->halmac_state.dlfw_state) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_cfg_parameter_88xx Fail due to DLFW NONE!!\n");
		return HALMAC_RET_DLFW_FAIL;
	}

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(cfg para)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if ((HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE != halmac_query_cfg_para_curr_state_88xx(pHalmac_adapter)) &&
	    (HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING != halmac_query_cfg_para_curr_state_88xx(pHalmac_adapter))) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Not idle state(cfg para)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_IDLE;

	ret_status = halmac_send_h2c_phy_parameter_88xx(pHalmac_adapter, para_info, full_fifo);

	if (HALMAC_RET_SUCCESS != ret_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_phy_parameter_88xx Fail!! = %x\n", ret_status);
		return ret_status;
	}

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_parameter_88xx <==========\n"); */

	return ret_status;
}

/**
 * halmac_update_packet_88xx() - send some specified packet to FW
 * @pHalmac_adapter
 * @pkt_id : probe request, sync beacon, discovery beacon
 * @pkt
 * @pkt_size
 *
 * Send new specified packet to FW.
 * Note : TX_DESC is not included in the @pkt
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_update_packet_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PACKET_ID	pkt_id,
	IN u8 *pkt,
	IN u32 pkt_size
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.update_packet_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_UPDATE_PACKET);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_update_packet_88xx ==========>\n");

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(update_packet)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_SENDING;

	status = halmac_send_h2c_update_packet_88xx(pHalmac_adapter, pkt_id, pkt, pkt_size);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_update_packet_88xx packet = %x,  fail = %x!!\n", pkt_id, status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_update_packet_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_bcn_ie_filter_88xx() - filter beacon & probe response
 * @pHalmac_adapter
 * @pBcn_ie_info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_bcn_ie_filter_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_BCN_IE_INFO pBcn_ie_info
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_BCN_IE_FILTER);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_bcn_ie_filter_88xx ==========>\n");

	status = halmac_send_h2c_update_bcn_parse_info_88xx(pHalmac_adapter, pBcn_ie_info);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_update_bcn_parse_info_88xx fail = %x\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_bcn_ie_filter_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_update_datapack_88xx() -
 * @pHalmac_adapter
 * @halmac_data_type
 * @para_info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_update_datapack_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DATA_TYPE halmac_data_type,
	IN PHALMAC_PHY_PARAMETER_INFO para_info
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS ret_status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_UPDATE_DATAPACK);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_update_datapack_88xx ==========>\n");

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	/* ret_status =  halmac_send_h2c_update_datapack_88xx(pHalmac_adapter, halmac_data_type, para_info); */

	if (HALMAC_RET_SUCCESS != ret_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_update_datapack_88xx Fail, datatype = %x, status = %x\n", halmac_data_type, ret_status);
		return ret_status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_update_datapack_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_run_datapack_88xx() -
 * @pHalmac_adapter
 * @halmac_data_type
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_run_datapack_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DATA_TYPE halmac_data_type
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS ret_status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_RUN_DATAPACK);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_run_datapack_88xx ==========>\n");

	ret_status = halmac_send_h2c_run_datapack_88xx(pHalmac_adapter, halmac_data_type);

	if (HALMAC_RET_SUCCESS != ret_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_run_datapack_88xx Fail, datatype = %x, status = %x!!\n", halmac_data_type, ret_status);
		return ret_status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_update_datapack_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_drv_info_88xx() - config driver info
 * @pHalmac_adapter
 * @halmac_drv_info : none, phy status, phy sniffer, phy plcp
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_drv_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DRV_INFO halmac_drv_info
)
{
	u8 drv_info_size = 0;
	u8 phy_status_en = 0;
	u8 sniffer_en = 0;
	u8 plcp_hdr_en = 0;
	u32 value32;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_DRV_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_drv_info_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_drv_info = %d\n", halmac_drv_info);

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
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_cfg_drv_info_88xx error = %x\n", status);
		return status;
	}

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RX_DRVINFO_SZ, drv_info_size);

	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_RCR);
	value32 = (value32 & (~BIT_APP_PHYSTS));
	if (1 == phy_status_en)
		value32 = value32 | BIT_APP_PHYSTS;
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RCR, value32);

	value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_WMAC_OPTION_FUNCTION + 4);
	value32 = (value32 & (~(BIT(8) | BIT(9))));
	if (1 == sniffer_en)
		value32 = value32 | BIT(9);
	if (1 == plcp_hdr_en)
		value32 = value32 | BIT(8);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WMAC_OPTION_FUNCTION + 4, value32);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_drv_info_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_send_bt_coex_88xx() -
 * @pHalmac_adapter
 * @pBt_buf
 * @bt_size
 * @ack
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_send_bt_coex_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pBt_buf,
	IN u32 bt_size,
	IN u8 ack
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS ret_status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SEND_BT_COEX);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_bt_coex_88xx ==========>\n");

	ret_status = halmac_send_bt_coex_cmd_88xx(pHalmac_adapter, pBt_buf, bt_size, ack);

	if (HALMAC_RET_SUCCESS != ret_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_bt_coex_cmd_88xx Fail = %x!!\n", ret_status);
		return ret_status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_bt_coex_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_verify_platform_api_88xx() - verify platform api
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_verify_platform_api_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS ret_status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_VERIFY_PLATFORM_API);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_verify_platform_api_88xx ==========>\n");

	ret_status = halmac_verify_io_88xx(pHalmac_adapter);

	if (HALMAC_RET_SUCCESS != ret_status)
		return ret_status;

	if (HALMAC_LA_MODE_FULL != pHalmac_adapter->txff_allocation.la_mode)
		ret_status = halmac_verify_send_rsvd_page_88xx(pHalmac_adapter);

	if (HALMAC_RET_SUCCESS != ret_status)
		return ret_status;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_verify_platform_api_88xx <==========\n");

	return ret_status;
}

/**
 * halmac_send_original_h2c_88xx() - send original format h2c packet
 * @pHalmac_adapter
 * @original_h2c
 * @seq
 * @ack
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_send_original_h2c_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *original_h2c,
	IN u16 *seq,
	IN u8 ack
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SEND_ORIGINAL_H2C);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_original_h2c_88xx ==========>\n");

	status = halmac_func_send_original_h2c_88xx(pHalmac_adapter, original_h2c, seq, ack);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_original_h2c FAIL = %x!!\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_original_h2c_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_timer_2s_88xx() - periodic operation
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_timer_2s_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_timer_2s_88xx ==========>\n");


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_timer_2s_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_fill_txdesc_check_sum_88xx() -  fill in tx desc check sum
 * @pHalmac_adapter
 * @pCur_desc
 *
 * User input tx descriptor, halmac output tx descriptor check sum
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_fill_txdesc_check_sum_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	INOUT u8 *pCur_desc
)
{
	u16 chk_result = 0;
	u16 *pData = (u16 *)NULL;
	u32 i;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_FILL_TXDESC_CHECKSUM);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;


	if (NULL == pCur_desc) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_fill_txdesc_check_sum_88xx NULL PTR");
		return HALMAC_RET_NULL_POINTER;
	}

	SET_TX_DESC_TXDESC_CHECKSUM(pCur_desc, 0x0000);

	pData = (u16 *)(pCur_desc);

	/* HW clculates only 32byte */
	for (i = 0; i < 8; i++)
		chk_result ^= (*(pData + 2 * i) ^ *(pData + (2 * i + 1)));

	SET_TX_DESC_TXDESC_CHECKSUM(pCur_desc, chk_result);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_dump_fifo_88xx() - dump fifo data
 * @pHalmac_adapter
 * @halmac_fifo_sel : tx, rx, rsvd page, report buff, llt
 * @pFifo_map
 * @halmac_fifo_dump_size
 *
 * Note : before dump fifo, user need to call halmac_get_fifo_size to
 * get fifo size. Then input this size to halmac_dump_fifo.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_dump_fifo_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HAL_FIFO_SEL halmac_fifo_sel,
	OUT u8 *pFifo_map,
	IN u32 halmac_fifo_dump_size
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DUMP_FIFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_dump_fifo_88xx ==========>\n");

	if (HAL_FIFO_SEL_TX == halmac_fifo_sel && halmac_fifo_dump_size > pHalmac_adapter->hw_config_info.tx_fifo_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "TX fifo dump size is too large\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (HAL_FIFO_SEL_RX == halmac_fifo_sel && halmac_fifo_dump_size > pHalmac_adapter->hw_config_info.rx_fifo_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "RX fifo dump size is too large\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (0 != (halmac_fifo_dump_size & (4 - 1))) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_fifo_dump_size shall 4byte align\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (NULL == pFifo_map) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "pFifo_map address is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	status = halmac_buffer_read_88xx(pHalmac_adapter, 0x00, halmac_fifo_dump_size, halmac_fifo_sel, pFifo_map);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_buffer_read_88xx error = %x\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_dump_fifo_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_fifo_size_88xx() - get fifo size
 * @pHalmac_adapter
 * @halmac_fifo_sel : tx, rx, rsvd page, report buff, llt
 * Author : Ivan Lin/KaiYuan Chang
 * Return : fifo size
 */
u32
halmac_get_fifo_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HAL_FIFO_SEL halmac_fifo_sel
)
{
	u32 fifo_size = 0;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_FIFO_SIZE);

	if (HAL_FIFO_SEL_TX == halmac_fifo_sel)
		fifo_size = pHalmac_adapter->hw_config_info.tx_fifo_size;
	else if (HAL_FIFO_SEL_RX == halmac_fifo_sel)
		fifo_size = pHalmac_adapter->hw_config_info.rx_fifo_size;
	else if (HAL_FIFO_SEL_RSVD_PAGE == halmac_fifo_sel)
		fifo_size = ((pHalmac_adapter->hw_config_info.tx_fifo_size >> 7) - pHalmac_adapter->txff_allocation.rsvd_pg_bndy) << 7;
	else if (HAL_FIFO_SEL_REPORT == halmac_fifo_sel)
		fifo_size = 65536;
	else if (HAL_FIFO_SEL_LLT == halmac_fifo_sel)
		fifo_size = 65536;

	return fifo_size;
}

/**
 * halmac_cfg_txbf_88xx() - enable/disable specific user's txbf
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_txbf_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid,
	IN HALMAC_BW bw,
	IN u8 txbf_en
)
{
	u16 temp42C = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_TXBF);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

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
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_cfg_txbf_88xx invalid TXBF BW setting 0x%x of userid %d\n", bw, userid);
			return HALMAC_RET_INVALID_SOUNDING_SETTING;
		}
	}

	switch (userid) {
	case 0:
		temp42C |= HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL) & ~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL, temp42C);
		break;
	case 1:
		temp42C |= HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL + 2) & ~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL + 2, temp42C);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_cfg_txbf_88xx invalid userid %d\n", userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_cfg_txbf_88xx, txbf_en = %x <==========\n", txbf_en);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_mumimo_88xx() -
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_mumimo_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CFG_MUMIMO_PARA pCfgmu
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	u8 i, idx, id0, id1, gid, mu_tab_sel;
	u8 mu_tab_valid = 0;
	u32 gid_valid[6] = {0};
	u8 temp14C0 = 0;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_MUMIMO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (pCfgmu->role == HAL_BFEE) {
		/*config MU BFEE*/
		temp14C0 = HALMAC_REG_READ_8(pHalmac_adapter, REG_MU_TX_CTL) & ~BIT_MASK_R_MU_TABLE_VALID;
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL, (temp14C0|BIT(0)|BIT(1)) & ~(BIT(7)));	/*enable MU table 0 and 1, disable MU TX*/

		/*config GID valid table and user position table*/
		mu_tab_sel = HALMAC_REG_READ_8(pHalmac_adapter, REG_MU_TX_CTL+1) & ~(BIT(0)|BIT(1)|BIT(2));
		for (i = 0; i < 2; i++) {
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL+1, mu_tab_sel | i);
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_GID_VLD, pCfgmu->given_gid_tab[i]);
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_USER_POS_INFO, pCfgmu->given_user_pos[i*2]);
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_USER_POS_INFO+4, pCfgmu->given_user_pos[i*2+1]);
		}
	} else {
		/*config MU BFER*/
		if (_FALSE == pCfgmu->mu_tx_en) {
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL, HALMAC_REG_READ_8(pHalmac_adapter, REG_MU_TX_CTL) & ~(BIT(7)));
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_cfg_mumimo_88xx disable mu tx <==========\n");
			return HALMAC_RET_SUCCESS;
		}

		/*Transform BB grouping bitmap[14:0] to MAC GID_valid table*/
		for (idx = 0; idx < 15; idx++) {
			if (idx < 5) {
				/*grouping_bitmap bit0~4, MU_STA0 with MUSTA1~5*/
				id0 = 0;
				id1 = (u8)(idx + 1);
			} else if (idx < 9) {
				/*grouping_bitmap bit5~8, MU_STA1 with MUSTA2~5*/
				id0 = 1;
				id1 = (u8)(idx - 3);
			} else if (idx < 12) {
				/*grouping_bitmap bit9~11, MU_STA2 with MUSTA3~5*/
				id0 = 2;
				id1 = (u8)(idx - 6);
			} else if (idx < 14) {
				/*grouping_bitmap bit12~13, MU_STA3 with MUSTA4~5*/
				id0 = 3;
				id1 = (u8)(idx - 8);
			} else {
				/*grouping_bitmap bit14, MU_STA4 with MUSTA5*/
				id0 = 4;
				id1 = (u8)(idx - 9);
			}
			if (pCfgmu->grouping_bitmap & BIT(idx)) {
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
		mu_tab_sel = HALMAC_REG_READ_8(pHalmac_adapter, REG_MU_TX_CTL+1) & ~(BIT(0)|BIT(1)|BIT(2));
		for (idx = 0; idx < 6; idx++) {
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL+1, idx | mu_tab_sel);
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_GID_VLD, gid_valid[idx]);
		}

		/*To validate the sounding successful MU STA and enable MU TX*/
		for (i = 0; i < 6; i++) {
			if (_TRUE == pCfgmu->sounding_sts[i])
				mu_tab_valid |= BIT(i);
		}
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL, mu_tab_valid  | BIT(7));
	}
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_cfg_mumimo_88xx <==========\n");
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_sounding_88xx() - set general sounding control registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_sounding_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_SND_ROLE role,
	IN HALMAC_DATA_RATE	datarate
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_SOUNDING);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_TXBF_CTRL, HALMAC_REG_READ_32(pHalmac_adapter, REG_TXBF_CTRL) | BIT_R_ENABLE_NDPA
			| BIT_USE_NDPA_PARAMETER | BIT_R_EN_NDPA_INT | BIT_DIS_NDP_BFEN);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_NDPA_RATE, datarate);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_NDPA_OPT_CTRL, HALMAC_REG_READ_8(pHalmac_adapter, REG_NDPA_OPT_CTRL) & (~(BIT(0) | BIT(1))));
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SND_PTCL_CTRL + 1, 0x2);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SND_PTCL_CTRL + 2, 0x2);
		break;
	case HAL_BFEE:
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SND_PTCL_CTRL, 0xDB);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SND_PTCL_CTRL + 3, 0x50);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_BBPSF_CTRL + 3, datarate);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RRSR, HALMAC_REG_READ_16(pHalmac_adapter, REG_RRSR) | BIT(datarate));
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_cfg_sounding_88xx invalid role \n");
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_cfg_sounding_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_del_sounding_88xx() - reset general sounding control registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_del_sounding_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_SND_ROLE role
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DEL_SOUNDING);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TXBF_CTRL + 3, 0);
		break;
	case HAL_BFEE:
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SND_PTCL_CTRL, 0);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_del_sounding_88xx invalid role \n");
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_del_sounding_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_init_88xx() - config SU beamformee's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_su_bfee_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid,
	IN u16 paid
)
{
	u16 temp42C = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SU_BFEE_ENTRY_INIT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	switch (userid) {
	case 0:
		temp42C = HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL) & ~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL, temp42C | paid);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_ASSOCIATED_BFMEE_SEL + 3, 0x60);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMEE_SEL, paid | BIT(9));
		break;
	case 1:
		temp42C = HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL + 2) & ~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL + 2, temp42C | paid);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMEE_SEL + 2, paid | BIT(9) | 0xe000);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_su_bfee_entry_init_88xx invalid userid %d \n", userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_su_bfee_entry_init_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_init_88xx() - config SU beamformer's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_su_bfer_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_SU_BFER_INIT_PARA pSu_bfer_init
)
{
	u16 mac_address_H;
	u32 mac_address_L;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SU_BFER_ENTRY_INIT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	/* mac_address_L = bfer_address.Address_L_H.Address_Low; */
	/* mac_address_H = bfer_address.Address_L_H.Address_High; */

	mac_address_L = rtk_le32_to_cpu(pSu_bfer_init->pbfer_address->Address_L_H.Address_Low);
	mac_address_H = rtk_le16_to_cpu(pSu_bfer_init->pbfer_address->Address_L_H.Address_High);

	switch (pSu_bfer_init->userid) {
	case 0:
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO, mac_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4, mac_address_H);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 6, pSu_bfer_init->paid);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TX_CSI_RPT_PARAM_BW20, pSu_bfer_init->csi_para);
		break;
	case 1:
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER1_INFO, mac_address_L);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER1_INFO + 4, mac_address_H);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER1_INFO + 6, pSu_bfer_init->paid);
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TX_CSI_RPT_PARAM_BW20 + 2, pSu_bfer_init->csi_para);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_su_bfer_entry_init_88xx invalid userid %d\n", pSu_bfer_init->userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_su_bfer_entry_init_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfee_entry_init_88xx() - config MU beamformee's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mu_bfee_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_MU_BFEE_INIT_PARA pMu_bfee_init
)
{
	u16 temp168X = 0, temp14C0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MU_BFEE_ENTRY_INIT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	temp168X |= pMu_bfee_init->paid | BIT(9);
	HALMAC_REG_WRITE_16(pHalmac_adapter, (0x1680 + pMu_bfee_init->userid * 2), temp168X);

	temp14C0 = HALMAC_REG_READ_16(pHalmac_adapter, REG_MU_TX_CTL) & ~(BIT(8)|BIT(9)|BIT(10));
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_MU_TX_CTL, temp14C0|((pMu_bfee_init->userid-2)<<8));
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_GID_VLD, 0);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_USER_POS_INFO, pMu_bfee_init->user_position_l);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_MU_STA_USER_POS_INFO+4, pMu_bfee_init->user_position_h);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_mu_bfee_entry_init_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfer_entry_init_88xx() - config SU beamformer's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mu_bfer_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_MU_BFER_INIT_PARA pMu_bfer_init
)
{
	u16 temp1680 = 0;
	u16 mac_address_H;
	u32 mac_address_L;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MU_BFER_ENTRY_INIT);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	/* mac_address_L = pHalmac_adapter->snd_info.bfer_address.Address_L_H.Address_Low; */
	/* mac_address_H = pHalmac_adapter->snd_info.bfer_address.Address_L_H.Address_High; */

	mac_address_L = rtk_le32_to_cpu(pMu_bfer_init->pbfer_address->Address_L_H.Address_Low);
	mac_address_H = rtk_le16_to_cpu(pMu_bfer_init->pbfer_address->Address_L_H.Address_High);

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO, mac_address_L);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4, mac_address_H);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 6, pMu_bfer_init->paid);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TX_CSI_RPT_PARAM_BW20, pMu_bfer_init->csi_para);

	temp1680 = HALMAC_REG_READ_16(pHalmac_adapter, 0x1680) & 0xC000;
	temp1680 |= pMu_bfer_init->my_aid | (pMu_bfer_init->csi_length_sel << 12);
	HALMAC_REG_WRITE_16(pHalmac_adapter, 0x1680, temp1680);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_mu_bfer_entry_init_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_del_88xx() - reset SU beamformee's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_su_bfee_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SU_BFEE_ENTRY_DEL);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	switch (userid) {
	case 0:
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL, HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL) & \
			~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M));
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMEE_SEL, 0);
		break;
	case 1:
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXBF_CTRL + 2, HALMAC_REG_READ_16(pHalmac_adapter, REG_TXBF_CTRL + 2) & \
			~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M));
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_ASSOCIATED_BFMEE_SEL + 2, 0);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_su_bfee_entry_del_88xx invalid userid %d\n", userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_su_bfee_entry_del_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_su_bfee_entry_del_88xx() - reset SU beamformer's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_su_bfer_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SU_BFER_ENTRY_DEL);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	switch (userid) {
	case 0:
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO, 0);
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4, 0);
		break;
	case 1:
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER1_INFO, 0);
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER1_INFO + 4, 0);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_su_bfer_entry_del_88xx invalid userid %d\n", userid);
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_su_bfer_entry_del_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfee_entry_del_88xx() - reset MU beamformee's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mu_bfee_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MU_BFEE_ENTRY_DEL);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	HALMAC_REG_WRITE_16(pHalmac_adapter, 0x1680 + userid * 2, 0);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL, HALMAC_REG_READ_8(pHalmac_adapter, REG_MU_TX_CTL) & ~(BIT(userid-2)));

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_mu_bfee_entry_del_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_mu_bfer_entry_del_88xx() -reset MU beamformer's registers
 * @pHalmac_adapter
 * Author : chunchu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mu_bfer_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MU_BFER_ENTRY_DEL);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO, 0);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_ASSOCIATED_BFMER0_INFO + 4, 0);
	HALMAC_REG_WRITE_16(pHalmac_adapter, 0x1680, 0);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_MU_TX_CTL, 0);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_TRACE, "halmac_mu_bfer_entry_del_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_add_ch_info_88xx() -used to construct channel info
 * @pHalmac_adapter
 * @pCh_info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_add_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_INFO pCh_info
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_CS_INFO pCh_sw_info;
	HALMAC_SCAN_CMD_CONSTRUCT_STATE state_scan;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_ADD_CH_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pCh_sw_info = &(pHalmac_adapter->ch_sw_info);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_add_ch_info_88xx ==========>\n");

	if (HALMAC_GEN_INFO_SENT != pHalmac_adapter->halmac_state.dlfw_state) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_ch_info_88xx: gen_info is not send to FW!!!!\n");
		return HALMAC_RET_GEN_INFO_NOT_SENT;
	}

	state_scan = halmac_query_scan_curr_state_88xx(pHalmac_adapter);
	if ((HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED != state_scan) && (HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING != state_scan)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Scan machine fail(add ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (NULL == pCh_sw_info->ch_info_buf) {
		pCh_sw_info->ch_info_buf = (u8 *)PLATFORM_RTL_MALLOC(pDriver_adapter, HALMAC_EXTRA_INFO_BUFF_SIZE_88XX);
		pCh_sw_info->ch_info_buf_w = pCh_sw_info->ch_info_buf;
		pCh_sw_info->buf_size = HALMAC_EXTRA_INFO_BUFF_SIZE_88XX;
		pCh_sw_info->avai_buf_size = HALMAC_EXTRA_INFO_BUFF_SIZE_88XX;
		pCh_sw_info->total_size = 0;
		pCh_sw_info->extra_info_en = 0;
		pCh_sw_info->ch_num = 0;
	}

	if (1 == pCh_sw_info->extra_info_en) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_ch_info_88xx: construct sequence wrong!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (4 > pCh_sw_info->avai_buf_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_ch_info_88xx: no availabe buffer!!\n");
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_scan_state_88xx(pHalmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING))
		return HALMAC_RET_ERROR_STATE;

	CHANNEL_INFO_SET_CHANNEL(pCh_sw_info->ch_info_buf_w, pCh_info->channel);
	CHANNEL_INFO_SET_PRI_CH_IDX(pCh_sw_info->ch_info_buf_w, pCh_info->pri_ch_idx);
	CHANNEL_INFO_SET_BANDWIDTH(pCh_sw_info->ch_info_buf_w, pCh_info->bw);
	CHANNEL_INFO_SET_TIMEOUT(pCh_sw_info->ch_info_buf_w, pCh_info->timeout);
	CHANNEL_INFO_SET_ACTION_ID(pCh_sw_info->ch_info_buf_w, pCh_info->action_id);
	CHANNEL_INFO_SET_CH_EXTRA_INFO(pCh_sw_info->ch_info_buf_w, pCh_info->extra_info);

	pCh_sw_info->avai_buf_size = pCh_sw_info->avai_buf_size - 4;
	pCh_sw_info->total_size = pCh_sw_info->total_size + 4;
	pCh_sw_info->ch_num++;
	pCh_sw_info->extra_info_en = pCh_info->extra_info;
	pCh_sw_info->ch_info_buf_w = pCh_sw_info->ch_info_buf_w + 4;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_add_ch_info_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_add_extra_ch_info_88xx() -used to construct extra channel info
 * @pHalmac_adapter
 * @pCh_extra_info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_add_extra_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_EXTRA_INFO pCh_extra_info
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_CS_INFO pCh_sw_info;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_ADD_EXTRA_CH_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pCh_sw_info = &(pHalmac_adapter->ch_sw_info);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_add_extra_ch_info_88xx ==========>\n");

	if (NULL == pCh_sw_info->ch_info_buf) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_extra_ch_info_88xx: NULL==pCh_sw_info->ch_info_buf!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (0 == pCh_sw_info->extra_info_en) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_extra_ch_info_88xx: construct sequence wrong!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (pCh_sw_info->avai_buf_size < (u32)(pCh_extra_info->extra_info_size + 2)) {/* 2:ch_extra_info_id, ch_extra_info, ch_extra_info_size are totally 2Byte */
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_add_extra_ch_info_88xx: no availabe buffer!!\n");
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING != halmac_query_scan_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Scan machine fail(add extra ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_scan_state_88xx(pHalmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING))
		return HALMAC_RET_ERROR_STATE;

	CH_EXTRA_INFO_SET_CH_EXTRA_INFO_ID(pCh_sw_info->ch_info_buf_w, pCh_extra_info->extra_action_id);
	CH_EXTRA_INFO_SET_CH_EXTRA_INFO(pCh_sw_info->ch_info_buf_w, pCh_extra_info->extra_info);
	CH_EXTRA_INFO_SET_CH_EXTRA_INFO_SIZE(pCh_sw_info->ch_info_buf_w, pCh_extra_info->extra_info_size);
	PLATFORM_RTL_MEMCPY(pDriver_adapter, pCh_sw_info->ch_info_buf_w + 2, pCh_extra_info->extra_info_data, pCh_extra_info->extra_info_size);

	pCh_sw_info->avai_buf_size = pCh_sw_info->avai_buf_size - (2 + pCh_extra_info->extra_info_size);
	pCh_sw_info->total_size = pCh_sw_info->total_size + (2 + pCh_extra_info->extra_info_size);
	pCh_sw_info->extra_info_en = pCh_extra_info->extra_info;
	pCh_sw_info->ch_info_buf_w = pCh_sw_info->ch_info_buf_w + (2 + pCh_extra_info->extra_info_size);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_add_extra_ch_info_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_ctrl_ch_switch_88xx() -used to send channel switch cmd
 * @pHalmac_adapter
 * @pCs_option
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_ctrl_ch_switch_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_SWITCH_OPTION	pCs_option
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_SCAN_CMD_CONSTRUCT_STATE state_scan;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.scan_state_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CTRL_CH_SWITCH);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_ctrl_ch_switch_88xx  pCs_option->switch_en = %d==========>\n", pCs_option->switch_en);

	if (_FALSE == pCs_option->switch_en)
		*pProcess_status = HALMAC_CMD_PROCESS_IDLE;

	if ((HALMAC_CMD_PROCESS_SENDING == *pProcess_status) || (HALMAC_CMD_PROCESS_RCVD == *pProcess_status)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(ctrl ch switch)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	state_scan = halmac_query_scan_curr_state_88xx(pHalmac_adapter);
	if (_TRUE == pCs_option->switch_en) {
		if (HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING != state_scan) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_ctrl_ch_switch_88xx(on)  invalid in state %x\n", state_scan);
			return HALMAC_RET_ERROR_STATE;
		}
	} else {
		if (HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED != state_scan) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_ctrl_ch_switch_88xx(off)  invalid in state %x\n", state_scan);
			return HALMAC_RET_ERROR_STATE;
		}
	}

	status = halmac_func_ctrl_ch_switch_88xx(pHalmac_adapter, pCs_option);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_ctrl_ch_switch FAIL = %x!!\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_ctrl_ch_switch_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_clear_ch_info_88xx() -used to clear channel info
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_clear_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CLEAR_CH_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_clear_ch_info_88xx ==========>\n");

	if (HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT == halmac_query_scan_curr_state_88xx(pHalmac_adapter)) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Scan machine fail(clear ch info)...\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (HALMAC_RET_SUCCESS != halmac_transition_scan_state_88xx(pHalmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED))
		return HALMAC_RET_ERROR_STATE;

	PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter->ch_sw_info.ch_info_buf, pHalmac_adapter->ch_sw_info.buf_size);
	pHalmac_adapter->ch_sw_info.ch_info_buf = NULL;
	pHalmac_adapter->ch_sw_info.ch_info_buf_w = NULL;
	pHalmac_adapter->ch_sw_info.extra_info_en = 0;
	pHalmac_adapter->ch_sw_info.buf_size = 0;
	pHalmac_adapter->ch_sw_info.avai_buf_size = 0;
	pHalmac_adapter->ch_sw_info.total_size = 0;
	pHalmac_adapter->ch_sw_info.ch_num = 0;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_clear_ch_info_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_send_general_info_88xx() -send general info
 * @pHalmac_adapter
 * @pGeneral_info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_send_general_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_GENERAL_INFO pGeneral_info
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SEND_GENERAL_INFO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_general_info_88xx ==========>\n");

	if (HALMAC_DLFW_NONE == pHalmac_adapter->halmac_state.dlfw_state) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_general_info_88xx Fail due to DLFW NONE!!\n");
		return HALMAC_RET_DLFW_FAIL;
	}

	status = halmac_func_send_general_info_88xx(pHalmac_adapter, pGeneral_info);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_send_general_info error = %x\n", status);
		return status;
	}

	if (HALMAC_DLFW_DONE == pHalmac_adapter->halmac_state.dlfw_state)
		pHalmac_adapter->halmac_state.dlfw_state = HALMAC_GEN_INFO_SENT;

	pHalmac_adapter->gen_info_valid = _TRUE;
	PLATFORM_RTL_MEMCPY(pDriver_adapter, &(pHalmac_adapter->general_info), pGeneral_info, sizeof(HALMAC_GENERAL_INFO));

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_send_general_info_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_start_iqk_88xx() -start iqk
 * @pHalmac_adapter
 * @clear
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_start_iqk_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 clear
)
{
	u8 pH2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = { 0 };
	u16 h2c_seq_num = 0;
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_H2C_HEADER_INFO h2c_header_info;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.iqk_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_START_IQK);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_start_iqk_88xx ==========>\n");

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(iqk)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_SENDING;

	IQK_SET_CLEAR(pH2c_buff, clear);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_IQK;
	h2c_header_info.content_size = 1;
	h2c_header_info.ack = _TRUE;
	halmac_set_fw_offload_h2c_header_88xx(pHalmac_adapter, pH2c_buff, &h2c_header_info, &h2c_seq_num);

	pHalmac_adapter->halmac_state.iqk_set.seq_num = h2c_seq_num;

	status = halmac_send_h2c_pkt_88xx(pHalmac_adapter, pH2c_buff, HALMAC_H2C_CMD_SIZE_88XX, _TRUE);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_start_iqk_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_ctrl_pwr_tracking_88xx() -control power tracking
 * @pHalmac_adapter
 * @pPwr_tracking_opt
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_ctrl_pwr_tracking_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PWR_TRACKING_OPTION pPwr_tracking_opt
)
{
	u8 pH2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = { 0 };
	u16 h2c_seq_mum = 0;
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_H2C_HEADER_INFO h2c_header_info;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.power_tracking_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CTRL_PWR_TRACKING);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_start_iqk_88xx ==========>\n");

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(pwr tracking)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*pProcess_status = HALMAC_CMD_PROCESS_SENDING;

	POWER_TRACKING_SET_TYPE(pH2c_buff, pPwr_tracking_opt->type);
	POWER_TRACKING_SET_BBSWING_INDEX(pH2c_buff, pPwr_tracking_opt->bbswing_index);
	POWER_TRACKING_SET_ENABLE_A(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_A(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A].tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_A(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A].pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_A(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_A].tssi_value);
	POWER_TRACKING_SET_ENABLE_B(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_B(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B].tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_B(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B].pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_B(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_B].tssi_value);
	POWER_TRACKING_SET_ENABLE_C(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_C(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C].tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_C(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C].pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_C(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_C].tssi_value);
	POWER_TRACKING_SET_ENABLE_D(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D].enable);
	POWER_TRACKING_SET_TX_PWR_INDEX_D(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D].tx_pwr_index);
	POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_D(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D].pwr_tracking_offset_value);
	POWER_TRACKING_SET_TSSI_VALUE_D(pH2c_buff, pPwr_tracking_opt->pwr_tracking_para[HALMAC_RF_PATH_D].tssi_value);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_POWER_TRACKING;
	h2c_header_info.content_size = 20;
	h2c_header_info.ack = _TRUE;
	halmac_set_fw_offload_h2c_header_88xx(pHalmac_adapter, pH2c_buff, &h2c_header_info, &h2c_seq_mum);

	pHalmac_adapter->halmac_state.power_tracking_set.seq_num = h2c_seq_mum;

	status = halmac_send_h2c_pkt_88xx(pHalmac_adapter, pH2c_buff, HALMAC_H2C_CMD_SIZE_88XX, _TRUE);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_start_iqk_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_query_status_88xx() -query async feature status
 * @pHalmac_adapter
 * @feature_id
 * @pProcess_status
 * @data
 * @size
 *
 * Note :
 * If user wants to know the data size, use can allocate random
 * size buffer first. If this size less than the data size, halmac
 * will return  HALMAC_RET_BUFFER_TOO_SMALL. User need to
 * re-allocate data buffer with correct data size.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_query_status_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_FEATURE_ID feature_id,
	OUT HALMAC_CMD_PROCESS_STATUS *pProcess_status,
	INOUT u8 *data,
	INOUT u32 *size
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_QUERY_STATE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_query_status_88xx ==========>\n"); */

	if (NULL == pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "null pointer!!\n");
		return HALMAC_RET_NULL_POINTER;
	}

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		status = halmac_query_cfg_para_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		status = halmac_query_dump_physical_efuse_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		status = halmac_query_dump_logical_efuse_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		status = halmac_query_channel_switch_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		status = halmac_query_update_packet_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_IQK:
		status = halmac_query_iqk_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		status = halmac_query_power_tracking_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	case HALMAC_FEATURE_PSD:
		status = halmac_query_psd_status_88xx(pHalmac_adapter, pProcess_status, data, size);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_query_status_88xx invalid feature id %d\n", feature_id);
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_query_status_88xx <==========\n"); */

	return status;
}

/**
 * halmac_reset_feature_88xx() -reset async feature status
 * @pHalmac_adapter
 * @feature_id
 * Author : Ivan Lin/KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reset_feature_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_FEATURE_ID feature_id
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_STATE pState = &(pHalmac_adapter->halmac_state);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_RESET_FEATURE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_reset_feature_88xx ==========>\n");

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		pState->cfg_para_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->cfg_para_state_set.cfg_para_cmd_construct_state = HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		pState->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->efuse_state_set.efuse_cmd_construct_state = HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		pState->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->scan_state_set.scan_cmd_construct_state = HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		pState->update_packet_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	case HALMAC_FEATURE_ALL:
		pState->cfg_para_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->cfg_para_state_set.cfg_para_cmd_construct_state = HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
		pState->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->efuse_state_set.efuse_cmd_construct_state = HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
		pState->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		pState->scan_state_set.scan_cmd_construct_state = HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
		pState->update_packet_set.process_status = HALMAC_CMD_PROCESS_IDLE;
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_SND, HALMAC_DBG_ERR, "halmac_reset_feature_88xx invalid feature id %d \n", feature_id);
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_reset_feature_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_check_fw_status_88xx() -check fw status
 * @pHalmac_adapter
 * @fw_status
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_check_fw_status_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u8 *fw_status
)
{
	u32 value32 = 0, value32_backup = 0, i = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CHECK_FW_STATUS);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_check_fw_status_88xx ==========>\n");

	value32 = PLATFORM_REG_READ_32(pDriver_adapter, REG_FW_DBG6);

	if (0 != value32) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_check_fw_status REG_FW_DBG6 !=0\n");
		*fw_status = _FALSE;
		return status;
	}

	value32_backup = PLATFORM_REG_READ_32(pDriver_adapter, REG_FW_DBG7);

	for (i = 0; i <= 10; i++) {
		value32 = PLATFORM_REG_READ_32(pDriver_adapter, REG_FW_DBG7);
		if (value32_backup != value32) {
			break;
		} else {
			if (10 == i) {
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_check_fw_status Polling FW PC fail\n");
				*fw_status = _FALSE;
				return status;
			}
		}
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_check_fw_status_88xx <==========\n");

	return status;
}

/**
 * halmac_dump_fw_dmem_88xx() -dump dmem
 * @pHalmac_adapter
 * @dmem
 * @size
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_dump_fw_dmem_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	INOUT u8 *dmem,
	INOUT u32 *size
)
{
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DUMP_FW_DMEM);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_dump_fw_dmem_88xx ==========>\n");



	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_dump_fw_dmem_88xx <==========\n");

	return status;
}

/**
 * halmac_cfg_max_dl_size_88xx() - config max download size
 * @pHalmac_adapter
 * @halmac_offset
 * Author : Ivan Lin/KaiYuan Chang
 *
 * Halmac uses this setting to set max packet size for
 * download FW.
 * If user has not called this API, halmac use default
 * setting for download FW
 * Note1 : size need power of 2
 * Note2 : max size is 31K
 *
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_max_dl_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 size
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_MAX_DL_SIZE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_FW, HALMAC_DBG_TRACE, "halmac_cfg_max_dl_size_88xx ==========>\n");

	if (size > HALMAC_FW_CFG_MAX_DL_SIZE_MAX_88XX) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_FW, HALMAC_DBG_ERR, "size > HALMAC_FW_CFG_MAX_DL_SIZE_MAX!\n");
		return HALMAC_RET_CFG_DLFW_SIZE_FAIL;
	}

	if (0 != (size & (2 - 1))) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_FW, HALMAC_DBG_ERR, "size is not power of 2!\n");
		return HALMAC_RET_CFG_DLFW_SIZE_FAIL;
	}

	pHalmac_adapter->max_download_size = size;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_FW, HALMAC_DBG_TRACE, "Cfg max size is : %X\n", size);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_FW, HALMAC_DBG_TRACE, "halmac_cfg_max_dl_size_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_psd_88xx() - trigger fw offload psd
 * @pHalmac_adapter
 * @start_psd
 * @end_psd
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_psd_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u16 start_psd,
	IN u16 end_psd
)
{
	u8 pH2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = { 0 };
	u16 h2c_seq_mum = 0;
	VOID *pDriver_adapter = NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	HALMAC_H2C_HEADER_INFO h2c_header_info;
	HALMAC_CMD_PROCESS_STATUS *pProcess_status = &(pHalmac_adapter->halmac_state.psd_set.process_status);

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_fw_validate(pHalmac_adapter))
		return HALMAC_RET_NO_DLFW;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_PSD);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_psd_88xx ==========>\n");

	if (HALMAC_CMD_PROCESS_SENDING == *pProcess_status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Wait event(psd)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (NULL != pHalmac_adapter->halmac_state.psd_set.pData) {
		PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter->halmac_state.psd_set.pData, pHalmac_adapter->halmac_state.psd_set.data_size);
		pHalmac_adapter->halmac_state.psd_set.pData = (u8 *)NULL;
	}

	pHalmac_adapter->halmac_state.psd_set.data_size = 0;
	pHalmac_adapter->halmac_state.psd_set.segment_size = 0;

	*pProcess_status = HALMAC_CMD_PROCESS_SENDING;

	PSD_SET_START_PSD(pH2c_buff, start_psd);
	PSD_SET_END_PSD(pH2c_buff, end_psd);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_PSD;
	h2c_header_info.content_size = 4;
	h2c_header_info.ack = _TRUE;
	halmac_set_fw_offload_h2c_header_88xx(pHalmac_adapter, pH2c_buff, &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(pHalmac_adapter, pH2c_buff, HALMAC_H2C_CMD_SIZE_88XX, _TRUE);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_psd_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_la_mode_88xx() - config la mode
 * @pHalmac_adapter
 * @la_mode
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_la_mode_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_LA_MODE la_mode
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_LA_MODE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_la_mode_88xx ==========>la_mode = %d\n", la_mode);

	pHalmac_adapter->txff_allocation.la_mode = la_mode;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_la_mode_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_get_hw_value_88xx() -
 * @pHalmac_adapter
 * @hw_id
 * @pvalue
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_hw_value_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_HW_ID hw_id,
	OUT VOID *pvalue
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_RQPN_MAP pRQPN_Map;
	u32 *pEfuse_size, *pTxff_size;
	u16 *pDrv_pg_bndy;
	u16 hcpwm2 = 0;
	u8 hcpwm = 0;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_HW_VALUE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_hw_value_88xx ==========>\n");

	if (NULL == pvalue) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_get_hw_value_88xx (NULL ==pvalue)==========>\n");
		return HALMAC_RET_NULL_POINTER;
	}
	switch (hw_id) {
	case HALMAC_HW_RQPN_MAPPING:
		pRQPN_Map = (PHALMAC_RQPN_MAP)pvalue;
		pRQPN_Map->dma_map_vo = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		pRQPN_Map->dma_map_vi = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		pRQPN_Map->dma_map_be = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		pRQPN_Map->dma_map_bk = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		pRQPN_Map->dma_map_mg = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		pRQPN_Map->dma_map_hi = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	case HALMAC_HW_EFUSE_SIZE:
		pEfuse_size = (pu32)pvalue;
		halmac_get_efuse_size_88xx(pHalmac_adapter, pEfuse_size);
		break;
	case HALMAC_HW_EEPROM_SIZE:
		pEfuse_size = (pu32)pvalue;
		*pEfuse_size = pHalmac_adapter->hw_config_info.eeprom_size;
		halmac_get_logical_efuse_size_88xx(pHalmac_adapter, pEfuse_size);
		break;
	case HALMAC_HW_BT_BANK_EFUSE_SIZE:
		*(u32 *)pvalue = pHalmac_adapter->hw_config_info.bt_efuse_size;
		break;
	case HALMAC_HW_BT_BANK1_EFUSE_SIZE:
	case HALMAC_HW_BT_BANK2_EFUSE_SIZE:
		*(u32 *)pvalue = 0;
		break;
	case HALMAC_HW_TXFIFO_SIZE:
		pTxff_size = (pu32)pvalue;
		*pTxff_size = pHalmac_adapter->hw_config_info.tx_fifo_size;
		break;
	case HALMAC_HW_RSVD_PG_BNDY:
		pDrv_pg_bndy = (pu16)pvalue;
		*pDrv_pg_bndy = pHalmac_adapter->txff_allocation.rsvd_drv_pg_bndy;
		break;
	case HALMAC_HW_CAM_ENTRY_NUM:
		*(u8 *)pvalue = pHalmac_adapter->hw_config_info.cam_entry_num;
		break;
	case HALMAC_HW_HCPWM:
		halmac_get_hcpwm_88xx(pHalmac_adapter, &hcpwm);
		*(u8 *)pvalue = hcpwm;
		break;
	case HALMAC_HW_HCPWM2:
		halmac_get_hcpwm2_88xx(pHalmac_adapter, &hcpwm2);
		*(u16 *)pvalue = hcpwm2;
		break;
	case HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE:
		status = halmac_dump_logical_efuse_map_88xx(pHalmac_adapter, HALMAC_EFUSE_R_DRV);
		if (HALMAC_RET_SUCCESS != status)
			return status;
		pEfuse_size = (pu32)pvalue;
		*pEfuse_size = pHalmac_adapter->hw_config_info.efuse_size - HALMAC_RESERVED_EFUSE_SIZE_88XX - pHalmac_adapter->efuse_end;
		break;

	case HALMAC_HW_TXFF_ALLOCATION:
		PLATFORM_RTL_MEMCPY(pDriver_adapter, pvalue, &(pHalmac_adapter->txff_allocation), sizeof(HALMAC_TXFF_ALLOCATION));
		break;
	default:
		break;
	}



	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_hw_value_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_set_hw_value_88xx() -
 * @pHalmac_adapter
 * @hw_id
 * @pvalue
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_set_hw_value_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_HW_ID hw_id,
	IN void *pvalue
)
{
	VOID *pDriver_adapter = NULL;
	u16 hrpwm2 = 0;
	u8 hrpwm = 0, value8 = 0;
	HALMAC_USB_MODE usb_mode = HALMAC_USB_MODE_U2, current_usb_mode = HALMAC_USB_MODE_U2;
	u8 hw_seq_en = 0;
	u32 value32 = 0;
	u32 usb_temp = 0;
	HALMAC_BW bw;
	u8 channel, enable_bb;
	HALMAC_PRI_CH_IDX	pri_ch_idx;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_HW_VALUE);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_set_hw_value_88xx ==========>\n");

	if (NULL == pvalue) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "halmac_set_hw_value_88xx (NULL ==pvalue)==========>\n");
		return HALMAC_RET_NULL_POINTER;
	}

	switch (hw_id) {
	case HALMAC_HW_ID_UNDEFINE:
		break;
	case HALMAC_HW_HRPWM:
		hrpwm = *(u8 *)pvalue;
		halmac_set_hrpwm_88xx(pHalmac_adapter, hrpwm);
		break;
	case HALMAC_HW_HRPWM2:
		hrpwm2 = *(u16 *)pvalue;
		halmac_set_hrpwm2_88xx(pHalmac_adapter, hrpwm2);
		break;
	case HALMAC_HW_USB_MODE:
		/* Get driver config USB mode*/
		usb_mode = *(HALMAC_USB_MODE *)pvalue;

		/* Get current USB mode*/
		current_usb_mode = (HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_CFG2 + 3) == 0x20) ? HALMAC_USB_MODE_U3 : HALMAC_USB_MODE_U2;

		/*check if HW supports usb2_usb3 swtich*/
		usb_temp = HALMAC_REG_READ_32(pHalmac_adapter, REG_PAD_CTRL2);
		if (_FALSE == (BIT_GET_USB23_SW_MODE_V1(usb_temp) | (usb_temp & BIT_USB3_USB2_TRANSITION))) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "HALMAC_HW_USB_MODE usb mode HW unsupport\n");
			return HALMAC_RET_USB2_3_SWITCH_UNSUPPORT;
		}

		if (usb_mode == current_usb_mode) {
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_ERR, "HALMAC_HW_USB_MODE usb mode unchange\n");
			return HALMAC_RET_USB_MODE_UNCHANGE;
		}

		usb_temp &=  ~(BIT_USB23_SW_MODE_V1(0x3));   /* clear 0xC6[3:2] */

		if (HALMAC_USB_MODE_U2 == usb_mode) {
			/* usb3 to usb2 */
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_PAD_CTRL2, usb_temp | BIT_USB23_SW_MODE_V1(HALMAC_USB_MODE_U2) | BIT_RSM_EN_V1); /* set usb mode and enable timer */
		} else {
			/* usb2 to usb3 */
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_PAD_CTRL2, usb_temp | BIT_USB23_SW_MODE_V1(HALMAC_USB_MODE_U3) | BIT_RSM_EN_V1);       /* set usb mode and enable timer */
		}

		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_PAD_CTRL2 + 1, 4);                                                                              /* set counter down timer 4x64 ms */
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_SYS_PW_CTRL, HALMAC_REG_READ_16(pHalmac_adapter, REG_SYS_PW_CTRL) | BIT_APFM_OFFMAC);          /* auto MAC off */
		PLATFORM_RTL_DELAY_US(pDriver_adapter, 1000);
		HALMAC_REG_WRITE_32(pHalmac_adapter, REG_PAD_CTRL2, HALMAC_REG_READ_32(pHalmac_adapter, REG_PAD_CTRL2) | BIT_NO_PDN_CHIPOFF_V1);        /* chip off */
		break;

	case HALMAC_HW_SEQ_EN:

		hw_seq_en = *(u8 *)pvalue;

		break;
	case HALMAC_HW_BANDWIDTH:

		bw = *(HALMAC_BW *)pvalue;
		halmac_cfg_bw_88xx(pHalmac_adapter, bw);

		break;
	case HALMAC_HW_CHANNEL:

		channel = *(u8 *)pvalue;
		halmac_cfg_ch_88xx(pHalmac_adapter, channel);

		break;
	case HALMAC_HW_PRI_CHANNEL_IDX:

		pri_ch_idx = *(HALMAC_PRI_CH_IDX *)pvalue;
		halmac_cfg_pri_ch_idx_88xx(pHalmac_adapter, pri_ch_idx);

		break;
	case HALMAC_HW_EN_BB_RF:

		enable_bb = *(u8 *)pvalue;

		if (_TRUE == enable_bb) {
			/* enable bb, rf */
			value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_FUNC_EN);
			value8 = value8 | BIT(0) | BIT(1);
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SYS_FUNC_EN, value8);

			value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_RF_CTRL);
			value8 = value8 | BIT(0) | BIT(1) | BIT(2);
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RF_CTRL, value8);

			value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_WLRF1);
			value32 = value32 | BIT(24) | BIT(25) | BIT(26);
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WLRF1, value32);
		} else {
			value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_SYS_FUNC_EN);
			value8 = value8 & (~(BIT(0) | BIT(1)));
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SYS_FUNC_EN, value8);

			value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_RF_CTRL);
			value8 = value8 & (~(BIT(0) | BIT(1) | BIT(2)));
			HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RF_CTRL, value8);

			value32 = HALMAC_REG_READ_32(pHalmac_adapter, REG_WLRF1);
			value32 = value32 & (~(BIT(24) | BIT(25) | BIT(26)));
			HALMAC_REG_WRITE_32(pHalmac_adapter, REG_WLRF1, value32);
		}

		break;

	default:
		break;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_set_hw_value_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_cfg_drv_rsvd_pg_num_88xx() -
 * @pHalmac_adapter
 * @pg_num
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_drv_rsvd_pg_num_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DRV_RSVD_PG_NUM pg_num
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_DRV_RSVD_PG_NUM);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_drv_rsvd_pg_num_88xx ==========>pg_num = %d\n", pg_num);

	switch (pg_num) {
	case HALMAC_RSVD_PG_NUM16:
		pHalmac_adapter->txff_allocation.rsvd_drv_pg_num = 16;
		break;
	case HALMAC_RSVD_PG_NUM24:
		pHalmac_adapter->txff_allocation.rsvd_drv_pg_num = 24;
		break;
	case HALMAC_RSVD_PG_NUM32:
		pHalmac_adapter->txff_allocation.rsvd_drv_pg_num = 32;
		break;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_cfg_drv_rsvd_pg_num_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}


/**
 * halmac_get_chip_version_88xx() -
 * @pHalmac_adapter
 * @version
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_chip_version_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_VER pVersion
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SWITCH_EFUSE_BANK);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_chip_version_88xx ==========>\n");
	pVersion->major_ver = (u8)HALMAC_MAJOR_VER_88XX;
	pVersion->prototype_ver = (u8)HALMAC_PROTOTYPE_VER_88XX;
	pVersion->minor_ver = (u8)HALMAC_MINOR_VER_88XX;
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_H2C, HALMAC_DBG_TRACE, "halmac_get_chip_version_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_chk_txdesc_88xx() -
 * @pHalmac_adapter
 * @halmac_buf
 * @halmac_size
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_chk_txdesc_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pHalmac_buf,
	IN u32 halmac_size
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_chk_txdesc_88xx ==========>\n");

	if (_TRUE == GET_TX_DESC_BMC(pHalmac_buf))
		if (_TRUE == GET_TX_DESC_AGG_EN(pHalmac_buf))
			PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "TxDesc: Agg should not be set when BMC\n");

	if (halmac_size < (GET_TX_DESC_TXPKTSIZE(pHalmac_buf) + GET_TX_DESC_OFFSET(pHalmac_buf)))
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "TxDesc: PktSize too small\n");

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_WARN, "halmac_chk_txdesc_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}
