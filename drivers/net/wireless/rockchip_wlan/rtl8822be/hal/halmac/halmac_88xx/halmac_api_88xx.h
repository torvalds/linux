/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HALMAC_API_88XX_H_
#define _HALMAC_API_88XX_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

VOID
halmac_init_state_machine_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

VOID
halmac_init_adapter_para_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_mount_api_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_download_firmware_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pHamacl_fw,
	IN u32 halmac_fw_size
);

HALMAC_RET_STATUS
halmac_get_fw_version_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT PHALMAC_FW_VERSION pFw_version
);

HALMAC_RET_STATUS
halmac_cfg_mac_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 halmac_port,
	IN PHALMAC_WLAN_ADDR pHal_address
);

HALMAC_RET_STATUS
halmac_cfg_bssid_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 halmac_port,
	IN PHALMAC_WLAN_ADDR pHal_address
);

HALMAC_RET_STATUS
halmac_cfg_multicast_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_WLAN_ADDR pHal_address
);

HALMAC_RET_STATUS
halmac_pre_init_system_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_init_system_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_cfg_rx_aggregation_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_RXAGG_CFG halmac_rxagg_cfg
);

HALMAC_RET_STATUS
halmac_init_protocol_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_init_edca_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_cfg_operation_mode_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_WIRELESS_MODE wireless_mode
);

HALMAC_RET_STATUS
halmac_cfg_ch_bw_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 channel,
	IN HALMAC_PRI_CH_IDX pri_ch_idx,
	IN HALMAC_BW bw
);

HALMAC_RET_STATUS
halmac_cfg_ch_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 channel
);

HALMAC_RET_STATUS
halmac_cfg_pri_ch_idx_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PRI_CH_IDX pri_ch_idx
);

HALMAC_RET_STATUS
halmac_cfg_bw_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_BW bw
);

HALMAC_RET_STATUS
halmac_init_wmac_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_init_mac_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE mode
);

HALMAC_RET_STATUS
halmac_clear_security_cam_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_dump_efuse_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_READ_CFG cfg
);

HALMAC_RET_STATUS
halmac_dump_efuse_map_bt_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_BANK halmac_efuse_bank,
	IN u32 bt_efuse_map_size,
	OUT u8 *pBT_efuse_map
);

HALMAC_RET_STATUS
halmac_write_efuse_bt_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_value,
	IN HALMAC_EFUSE_BANK halmac_efuse_bank
);

HALMAC_RET_STATUS
halmac_pg_efuse_by_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PG_EFUSE_INFO pPg_efuse_info,
	IN HALMAC_EFUSE_READ_CFG cfg
);

HALMAC_RET_STATUS
halmac_get_efuse_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
);

HALMAC_RET_STATUS
halmac_get_efuse_available_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
);

HALMAC_RET_STATUS
halmac_get_c2h_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *halmac_buf,
	IN u32 halmac_size
);

HALMAC_RET_STATUS
halmac_get_logical_efuse_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u32 *halmac_size
);

HALMAC_RET_STATUS
halmac_dump_logical_efuse_map_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_EFUSE_READ_CFG cfg
);

HALMAC_RET_STATUS
halmac_write_logical_efuse_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_value
);

HALMAC_RET_STATUS
halmac_read_logical_efuse_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	OUT u8 *pValue
);

HALMAC_RET_STATUS
halmac_cfg_fwlps_option_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_FWLPS_OPTION pLps_option
);

HALMAC_RET_STATUS
halmac_cfg_fwips_option_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_FWIPS_OPTION pIps_option
);

HALMAC_RET_STATUS
halmac_enter_wowlan_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_WOWLAN_OPTION pWowlan_option
);

HALMAC_RET_STATUS
halmac_leave_wowlan_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_enter_ps_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PS_STATE ps_state
);

HALMAC_RET_STATUS
halmac_leave_ps_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_h2c_lb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_debug_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_cfg_parameter_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PHY_PARAMETER_INFO para_info,
	IN u8 full_fifo
);

HALMAC_RET_STATUS
halmac_update_packet_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_PACKET_ID pkt_id,
	IN u8 *pkt,
	IN u32 pkt_size
);

HALMAC_RET_STATUS
halmac_bcn_ie_filter_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_BCN_IE_INFO pBcn_ie_info
);

HALMAC_RET_STATUS
halmac_send_original_h2c_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *original_h2c,
	IN u16 *seq,
	IN u8 ack
);

HALMAC_RET_STATUS
halmac_update_datapack_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DATA_TYPE halmac_data_type,
	IN PHALMAC_PHY_PARAMETER_INFO para_info
);

HALMAC_RET_STATUS
halmac_run_datapack_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DATA_TYPE halmac_data_type
);

HALMAC_RET_STATUS
halmac_cfg_drv_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DRV_INFO halmac_drv_info
);

HALMAC_RET_STATUS
halmac_send_bt_coex_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pBt_buf,
	IN u32 bt_size,
	IN u8 ack
);

HALMAC_RET_STATUS
halmac_verify_platform_api_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_timer_2s_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_fill_txdesc_check_sum_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *cur_desc
);

HALMAC_RET_STATUS
halmac_dump_fifo_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HAL_FIFO_SEL halmac_fifo_sel,
	OUT u8 *pFifo_map,
	IN u32 halmac_fifo_dump_size
);

u32
halmac_get_fifo_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HAL_FIFO_SEL halmac_fifo_sel
);

HALMAC_RET_STATUS
halmac_cfg_txbf_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid,
	IN HALMAC_BW bw,
	IN u8 txbf_en
);

HALMAC_RET_STATUS
halmac_cfg_mumimo_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CFG_MUMIMO_PARA pCfgmu
);

HALMAC_RET_STATUS
halmac_cfg_sounding_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_SND_ROLE role,
	IN HALMAC_DATA_RATE datarate
);

HALMAC_RET_STATUS
halmac_del_sounding_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_SND_ROLE role
);

HALMAC_RET_STATUS
halmac_su_bfee_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid,
	IN u16 paid
);

HALMAC_RET_STATUS
halmac_su_bfer_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_SU_BFER_INIT_PARA pSu_bfer_init
);

HALMAC_RET_STATUS
halmac_mu_bfee_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_MU_BFEE_INIT_PARA pMu_bfee_init
);

HALMAC_RET_STATUS
halmac_mu_bfer_entry_init_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_MU_BFER_INIT_PARA pMu_bfer_init
);

HALMAC_RET_STATUS
halmac_su_bfee_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
);

HALMAC_RET_STATUS
halmac_su_bfer_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
);

HALMAC_RET_STATUS
halmac_mu_bfee_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 userid
);

HALMAC_RET_STATUS
halmac_mu_bfer_entry_del_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_add_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_INFO pCh_info
);

HALMAC_RET_STATUS
halmac_add_extra_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_EXTRA_INFO pCh_extra_info
);

HALMAC_RET_STATUS
halmac_ctrl_ch_switch_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_CH_SWITCH_OPTION pCs_option
);

HALMAC_RET_STATUS
halmac_clear_ch_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_send_general_info_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_GENERAL_INFO pGeneral_info
);

HALMAC_RET_STATUS
halmac_start_iqk_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 clear
);

HALMAC_RET_STATUS
halmac_ctrl_pwr_tracking_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_PWR_TRACKING_OPTION pPwr_tracking_opt
);

HALMAC_RET_STATUS
halmac_query_status_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_FEATURE_ID feature_id,
	OUT HALMAC_CMD_PROCESS_STATUS *pProcess_status,
	INOUT u8 *data,
	INOUT u32 *size
);

HALMAC_RET_STATUS
halmac_reset_feature_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_FEATURE_ID feature_id
);

HALMAC_RET_STATUS
halmac_check_fw_status_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	OUT u8 *fw_status
);

HALMAC_RET_STATUS
halmac_dump_fw_dmem_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	INOUT u8 *dmem,
	INOUT u32 *size
);

HALMAC_RET_STATUS
halmac_cfg_max_dl_size_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 size
);


HALMAC_RET_STATUS
halmac_psd_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u16 start_psd,
	IN u16 end_psd
);

HALMAC_RET_STATUS
halmac_cfg_la_mode_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_LA_MODE la_mode
);

HALMAC_RET_STATUS
halmac_get_hw_value_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_HW_ID hw_id,
	OUT VOID *pvalue
);

HALMAC_RET_STATUS
halmac_set_hw_value_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_HW_ID hw_id,
	IN VOID *pvalue
);

HALMAC_RET_STATUS
halmac_cfg_drv_rsvd_pg_num_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_DRV_RSVD_PG_NUM pg_num
);

HALMAC_RET_STATUS
halmac_get_chip_version_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_VER pVersion
);

HALMAC_RET_STATUS
halmac_chk_txdesc_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *pHalmac_buf,
	IN u32 halmac_size
);


#endif/* _HALMAC_API_H_ */
