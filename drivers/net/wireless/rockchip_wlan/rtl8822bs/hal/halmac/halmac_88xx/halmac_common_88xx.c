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

#include "halmac_common_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_init_88xx.h"
#include "halmac_cfg_wmac_88xx.h"
#include "halmac_efuse_88xx.h"
#include "halmac_bb_rf_88xx.h"
#if HALMAC_USB_SUPPORT
#include "halmac_usb_88xx.h"
#endif
#if HALMAC_SDIO_SUPPORT
#include "halmac_sdio_88xx.h"
#endif
#if HALMAC_PCIE_SUPPORT
#include "halmac_pcie_88xx.h"
#endif
#include "halmac_mimo_88xx.h"

#if HALMAC_88XX_SUPPORT

#define CFG_PARAM_H2C_INFO_SIZE	12
#define ORIGINAL_H2C_CMD_SIZE	8

#define WLHDR_PROT_VER	0

#define WLHDR_TYPE_MGMT		0
#define WLHDR_TYPE_CTRL		1
#define WLHDR_TYPE_DATA		2

/* mgmt frame */
#define WLHDR_SUB_TYPE_ASSOC_REQ	0
#define WLHDR_SUB_TYPE_ASSOC_RSPNS	1
#define WLHDR_SUB_TYPE_REASSOC_REQ	2
#define WLHDR_SUB_TYPE_REASSOC_RSPNS	3
#define WLHDR_SUB_TYPE_PROBE_REQ	4
#define WLHDR_SUB_TYPE_PROBE_RSPNS	5
#define WLHDR_SUB_TYPE_BCN		8
#define WLHDR_SUB_TYPE_DISASSOC		10
#define WLHDR_SUB_TYPE_AUTH		11
#define WLHDR_SUB_TYPE_DEAUTH		12
#define WLHDR_SUB_TYPE_ACTION		13
#define WLHDR_SUB_TYPE_ACTION_NOACK	14

/* ctrl frame */
#define WLHDR_SUB_TYPE_BF_RPT_POLL	4
#define WLHDR_SUB_TYPE_NDPA		5

/* data frame */
#define WLHDR_SUB_TYPE_DATA		0
#define WLHDR_SUB_TYPE_NULL		4
#define WLHDR_SUB_TYPE_QOS_DATA		8
#define WLHDR_SUB_TYPE_QOS_NULL		12

#define LTECOEX_ACCESS_CTRL REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1

struct wlhdr_frame_ctrl {
	u16 protocol:2;
	u16 type:2;
	u16 sub_type:4;
	u16 to_ds:1;
	u16 from_ds:1;
	u16 more_frag:1;
	u16 retry:1;
	u16 pwr_mgmt:1;
	u16 more_data:1;
	u16 protect_frame:1;
	u16 order:1;
};

static enum halmac_ret_status
parse_c2h_pkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_c2h_dbg_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_h2c_ack_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_scan_ch_notify_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_scan_rpt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_h2c_ack_cfg_param_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_h2c_ack_update_pkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_h2c_ack_send_scan_pkt_88xx(struct halmac_adapter *adapter, u8 *buf,
			       u32 size);

static enum halmac_ret_status
get_h2c_ack_drop_scan_pkt_88xx(struct halmac_adapter *adapter, u8 *buf,
			       u32 size);

static enum halmac_ret_status
get_h2c_ack_update_datapkt_88xx(struct halmac_adapter *adapter, u8 *buf,
				u32 size);

static enum halmac_ret_status
get_h2c_ack_run_datapkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
get_h2c_ack_ch_switch_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

static enum halmac_ret_status
malloc_cfg_param_buf_88xx(struct halmac_adapter *adapter, u8 full_fifo);

static enum halmac_cmd_construct_state
cfg_param_cmd_cnstr_state_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
proc_cfg_param_88xx(struct halmac_adapter *adapter,
		    struct halmac_phy_parameter_info *param, u8 full_fifo);

static enum halmac_ret_status
send_cfg_param_h2c_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
cnv_cfg_param_state_88xx(struct halmac_adapter *adapter,
			 enum halmac_cmd_construct_state dest_state);

static enum halmac_ret_status
add_param_buf_88xx(struct halmac_adapter *adapter,
		   struct halmac_phy_parameter_info *param, u8 *buf,
		   u8 *end_cmd);

static enum halmac_ret_status
gen_cfg_param_h2c_88xx(struct halmac_adapter *adapter, u8 *buff);

static enum halmac_ret_status
send_h2c_update_packet_88xx(struct halmac_adapter *adapter,
			    enum halmac_packet_id pkt_id, u8 *pkt, u32 size);

static enum halmac_ret_status
send_h2c_send_scan_packet_88xx(struct halmac_adapter *adapter,
			       u8 index, u8 *pkt, u32 size);

static enum halmac_ret_status
send_h2c_drop_scan_packet_88xx(struct halmac_adapter *adapter,
			       struct halmac_drop_pkt_option *option);

static enum halmac_ret_status
send_bt_coex_cmd_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size,
		      u8 ack);

static enum halmac_ret_status
read_buf_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
	      enum hal_fifo_sel sel, u8 *data);

static enum halmac_cmd_construct_state
scan_cmd_cnstr_state_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
cnv_scan_state_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_construct_state dest_state);

static enum halmac_ret_status
proc_ctrl_ch_switch_88xx(struct halmac_adapter *adapter,
			 struct halmac_ch_switch_option *opt);

static enum halmac_ret_status
proc_p2pps_88xx(struct halmac_adapter *adapter, struct halmac_p2pps *info);

static enum halmac_ret_status
get_cfg_param_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status);

static enum halmac_ret_status
get_ch_switch_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status);

static enum halmac_ret_status
get_update_packet_status_88xx(struct halmac_adapter *adapter,
			      enum halmac_cmd_process_status *proc_status);

static enum halmac_ret_status
get_send_scan_packet_status_88xx(struct halmac_adapter *adapter,
				 enum halmac_cmd_process_status *proc_status);

static enum halmac_ret_status
get_drop_scan_packet_status_88xx(struct halmac_adapter *adapter,
				 enum halmac_cmd_process_status *proc_status);

static enum halmac_ret_status
pwr_sub_seq_parser_88xx(struct halmac_adapter *adapter, u8 cut, u8 intf,
			struct halmac_wlan_pwr_cfg *cmd);

static void
pwr_state_88xx(struct halmac_adapter *adapter, enum halmac_mac_power *state);

static enum halmac_ret_status
pwr_cmd_polling_88xx(struct halmac_adapter *adapter,
		     struct halmac_wlan_pwr_cfg *cmd);

static void
get_pq_mapping_88xx(struct halmac_adapter *adapter,
		    struct halmac_rqpn_map *mapping);

static void
dump_reg_sdio_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
wlhdr_valid_88xx(struct halmac_adapter *adapter, u8 *buf);

static u8
wlhdr_mgmt_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr);

static u8
wlhdr_ctrl_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr);

static u8
wlhdr_data_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr);

static void
dump_reg_88xx(struct halmac_adapter *adapter);

static u8
packet_in_nlo_88xx(struct halmac_adapter *adapter,
		   enum halmac_packet_id pkt_id);

static enum halmac_packet_id
get_real_pkt_id_88xx(struct halmac_adapter *adapter,
		     enum halmac_packet_id pkt_id);

static u32
get_update_packet_page_size(struct halmac_adapter *adapter, u32 size);

/**
 * ofld_func_cfg_88xx() - config offload function
 * @adapter : the adapter of halmac
 * @info : offload function information
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
ofld_func_cfg_88xx(struct halmac_adapter *adapter,
		   struct halmac_ofld_func_info *info)
{
	if (adapter->intf == HALMAC_INTERFACE_SDIO &&
	    info->rsvd_pg_drv_buf_max_sz > SDIO_TX_MAX_SIZE_88XX)
		return HALMAC_RET_FAIL;

	adapter->pltfm_info.malloc_size = info->halmac_malloc_max_sz;
	adapter->pltfm_info.rsvd_pg_size = info->rsvd_pg_drv_buf_max_sz;

	return HALMAC_RET_SUCCESS;
}

/**
 * dl_drv_rsvd_page_88xx() - download packet to rsvd page
 * @adapter : the adapter of halmac
 * @pg_offset : page offset of driver's rsvd page
 * @halmac_buf : data to be downloaded, tx_desc is not included
 * @halmac_size : data size to be downloaded
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dl_drv_rsvd_page_88xx(struct halmac_adapter *adapter, u8 pg_offset, u8 *buf,
		      u32 size)
{
	enum halmac_ret_status status;
	u32 pg_size;
	u32 pg_num = 0;
	u16 pg_addr = 0;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	pg_size = adapter->hw_cfg_info.page_size;
	pg_num = size / pg_size + ((size & (pg_size - 1)) ? 1 : 0);
	if (pg_offset + pg_num > adapter->txff_alloc.rsvd_drv_pg_num) {
		PLTFM_MSG_ERR("[ERR] pkt overflow!!\n");
		return HALMAC_RET_DRV_DL_ERR;
	}

	pg_addr = adapter->txff_alloc.rsvd_drv_addr + pg_offset;

	status = dl_rsvd_page_88xx(adapter, pg_addr, buf, size);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dl rsvd page fail!!\n");
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
dl_rsvd_page_88xx(struct halmac_adapter *adapter, u16 pg_addr, u8 *buf,
		  u32 size)
{
	u8 restore[2];
	u8 value8;
	u16 rsvd_pg_head;
	u32 cnt;
	enum halmac_rsvd_pg_state *state = &adapter->halmac_state.rsvd_pg_state;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (size == 0) {
		PLTFM_MSG_TRACE("[TRACE]pkt size = 0\n");
		return HALMAC_RET_ZERO_LEN_RSVD_PACKET;
	}

	if (*state == HALMAC_RSVD_PG_STATE_BUSY)
		return HALMAC_RET_BUSY_STATE;

	*state = HALMAC_RSVD_PG_STATE_BUSY;

	pg_addr &= BIT_MASK_BCN_HEAD_1_V1;
	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2, (u16)(pg_addr | BIT(15)));

	value8 = HALMAC_REG_R8(REG_CR + 1);
	restore[0] = value8;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_W8(REG_CR + 1, value8);

	value8 = HALMAC_REG_R8(REG_FWHW_TXQ_CTRL + 2);
	restore[1] = value8;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, value8);

	if (PLTFM_SEND_RSVD_PAGE(buf, size) == 0) {
		PLTFM_MSG_ERR("[ERR]send rvsd pg(pltfm)!!\n");
		status = HALMAC_RET_DL_RSVD_PAGE_FAIL;
		goto DL_RSVD_PG_END;
	}

	cnt = 1000;
	while (!(HALMAC_REG_R8(REG_FIFOPAGE_CTRL_2 + 1) & BIT(7))) {
		PLTFM_DELAY_US(10);
		cnt--;
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]bcn valid!!\n");
			status = HALMAC_RET_POLLING_BCN_VALID_FAIL;
			break;
		}
	}
DL_RSVD_PG_END:
	rsvd_pg_head = adapter->txff_alloc.rsvd_boundary;
	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2, rsvd_pg_head | BIT(15));
	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, restore[1]);
	HALMAC_REG_W8(REG_CR + 1, restore[0]);

	*state = HALMAC_RSVD_PG_STATE_IDLE;

	return status;
}

enum halmac_ret_status
get_hw_value_88xx(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		  void *value)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (hw_id) {
	case HALMAC_HW_RQPN_MAPPING:
		get_pq_mapping_88xx(adapter, (struct halmac_rqpn_map *)value);
		break;
	case HALMAC_HW_EFUSE_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.efuse_size;
		break;
	case HALMAC_HW_EEPROM_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.eeprom_size;
		break;
	case HALMAC_HW_BT_BANK_EFUSE_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.bt_efuse_size;
		break;
	case HALMAC_HW_BT_BANK1_EFUSE_SIZE:
	case HALMAC_HW_BT_BANK2_EFUSE_SIZE:
		*(u32 *)value = 0;
		break;
	case HALMAC_HW_TXFIFO_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.tx_fifo_size;
		break;
	case HALMAC_HW_RXFIFO_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.rx_fifo_size;
		break;
	case HALMAC_HW_RSVD_PG_BNDY:
		*(u16 *)value = adapter->txff_alloc.rsvd_drv_addr;
		break;
	case HALMAC_HW_CAM_ENTRY_NUM:
		*(u8 *)value = adapter->hw_cfg_info.cam_entry_num;
		break;
	case HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE:
		get_efuse_available_size_88xx(adapter, (u32 *)value);
		break;
	case HALMAC_HW_IC_VERSION:
		*(u8 *)value = adapter->chip_ver;
		break;
	case HALMAC_HW_PAGE_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.page_size;
		break;
	case HALMAC_HW_TX_AGG_ALIGN_SIZE:
		*(u16 *)value = adapter->hw_cfg_info.tx_align_size;
		break;
	case HALMAC_HW_RX_AGG_ALIGN_SIZE:
		*(u8 *)value = 8;
		break;
	case HALMAC_HW_DRV_INFO_SIZE:
		*(u8 *)value = adapter->drv_info_size;
		break;
	case HALMAC_HW_TXFF_ALLOCATION:
		PLTFM_MEMCPY(value, &adapter->txff_alloc,
			     sizeof(struct halmac_txff_allocation));
		break;
	case HALMAC_HW_RSVD_EFUSE_SIZE:
		*(u32 *)value = get_rsvd_efuse_size_88xx(adapter);
		break;
	case HALMAC_HW_FW_HDR_SIZE:
		*(u32 *)value = WLAN_FW_HDR_SIZE;
		break;
	case HALMAC_HW_TX_DESC_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.txdesc_size;
		break;
	case HALMAC_HW_RX_DESC_SIZE:
		*(u32 *)value = adapter->hw_cfg_info.rxdesc_size;
		break;
	case HALMAC_HW_ORI_H2C_SIZE:
		*(u32 *)value = ORIGINAL_H2C_CMD_SIZE;
		break;
	case HALMAC_HW_RSVD_DRV_PGNUM:
		*(u16 *)value = adapter->txff_alloc.rsvd_drv_pg_num;
		break;
	case HALMAC_HW_TX_PAGE_SIZE:
		*(u16 *)value = TX_PAGE_SIZE_88XX;
		break;
	case HALMAC_HW_USB_TXAGG_DESC_NUM:
		*(u8 *)value = adapter->hw_cfg_info.usb_txagg_num;
		break;
	case HALMAC_HW_AC_OQT_SIZE:
		*(u8 *)value = adapter->hw_cfg_info.ac_oqt_size;
		break;
	case HALMAC_HW_NON_AC_OQT_SIZE:
		*(u8 *)value = adapter->hw_cfg_info.non_ac_oqt_size;
		break;
	case HALMAC_HW_AC_QUEUE_NUM:
		*(u8 *)value = adapter->hw_cfg_info.acq_num;
		break;
	case HALMAC_HW_PWR_STATE:
		pwr_state_88xx(adapter, (enum halmac_mac_power *)value);
		break;
	default:
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static void
get_pq_mapping_88xx(struct halmac_adapter *adapter,
		    struct halmac_rqpn_map *mapping)
{
	mapping->dma_map_vo = adapter->pq_map[HALMAC_PQ_MAP_VO];
	mapping->dma_map_vi = adapter->pq_map[HALMAC_PQ_MAP_VI];
	mapping->dma_map_be = adapter->pq_map[HALMAC_PQ_MAP_BE];
	mapping->dma_map_bk = adapter->pq_map[HALMAC_PQ_MAP_BK];
	mapping->dma_map_mg = adapter->pq_map[HALMAC_PQ_MAP_MG];
	mapping->dma_map_hi = adapter->pq_map[HALMAC_PQ_MAP_HI];
}

/**
 * set_hw_value_88xx() -set hw config value
 * @adapter : the adapter of halmac
 * @hw_id : hw id for driver to config
 * @value : hw value, reference table to get data type
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
set_hw_value_88xx(struct halmac_adapter *adapter, enum halmac_hw_id hw_id,
		  void *value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_tx_page_threshold_info *th_info = NULL;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!value) {
		PLTFM_MSG_ERR("[ERR]null ptr-set hw value\n");
		return HALMAC_RET_NULL_POINTER;
	}

	switch (hw_id) {
#if HALMAC_USB_SUPPORT
	case HALMAC_HW_USB_MODE:
		status = set_usb_mode_88xx(adapter,
					   *(enum halmac_usb_mode *)value);
		if (status != HALMAC_RET_SUCCESS)
			return status;
		break;
#endif
	case HALMAC_HW_BANDWIDTH:
		cfg_bw_88xx(adapter, *(enum halmac_bw *)value);
		break;
	case HALMAC_HW_CHANNEL:
		cfg_ch_88xx(adapter, *(u8 *)value);
		break;
	case HALMAC_HW_PRI_CHANNEL_IDX:
		cfg_pri_ch_idx_88xx(adapter, *(enum halmac_pri_ch_idx *)value);
		break;
	case HALMAC_HW_EN_BB_RF:
		status = enable_bb_rf_88xx(adapter, *(u8 *)value);
		if (status != HALMAC_RET_SUCCESS)
			return status;
		break;
#if HALMAC_SDIO_SUPPORT
	case HALMAC_HW_SDIO_TX_PAGE_THRESHOLD:
		if (adapter->intf == HALMAC_INTERFACE_SDIO) {
			th_info = (struct halmac_tx_page_threshold_info *)value;
			cfg_sdio_tx_page_threshold_88xx(adapter, th_info);
		} else {
			return HALMAC_RET_FAIL;
		}
		break;
#endif
	case HALMAC_HW_RX_SHIFT:
		rx_shift_88xx(adapter, *(u8 *)value);
		break;
	case HALMAC_HW_TXDESC_CHECKSUM:
		tx_desc_chksum_88xx(adapter, *(u8 *)value);
		break;
	case HALMAC_HW_RX_CLK_GATE:
		rx_clk_gate_88xx(adapter, *(u8 *)value);
		break;
	case HALMAC_HW_FAST_EDCA:
		fast_edca_cfg_88xx(adapter,
				   (struct halmac_fast_edca_cfg *)value);
		break;
	case HALMAC_HW_RTS_FULL_BW:
		rts_full_bw_88xx(adapter, *(u8 *)value);
		break;
	case HALMAC_HW_FREE_CNT_EN:
		HALMAC_REG_W8_SET(REG_MISC_CTRL, BIT_EN_FREECNT);
		break;
	case HALMAC_HW_TXFIFO_LIFETIME:
		cfg_txfifo_lt_88xx(adapter,
				   (struct halmac_txfifo_lifetime_cfg *)value);
		break;
	default:
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_watcher_88xx() -get watcher value
 * @adapter : the adapter of halmac
 * @sel : id for driver to config
 * @value : value, reference table to get data type
 * Author :
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_watcher_88xx(struct halmac_adapter *adapter, enum halmac_watcher_sel sel,
		 void *value)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!value) {
		PLTFM_MSG_ERR("[ERR]null ptr-set hw value\n");
		return HALMAC_RET_NULL_POINTER;
	}

	switch (sel) {
	case HALMAC_WATCHER_SDIO_RN_FOOL_PROOFING:
		*(u32 *)value = adapter->watcher.get_watcher.sdio_rn_not_align;
		break;
	default:
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
set_h2c_pkt_hdr_88xx(struct halmac_adapter *adapter, u8 *hdr,
		     struct halmac_h2c_header_info *info, u16 *seq_num)
{
	u16 total_size;

	PLTFM_MSG_TRACE("[TRACE]%s!!\n", __func__);

	total_size = H2C_PKT_HDR_SIZE_88XX + info->content_size;
	FW_OFFLOAD_H2C_SET_TOTAL_LEN(hdr, total_size);
	FW_OFFLOAD_H2C_SET_SUB_CMD_ID(hdr, info->sub_cmd_id);

	FW_OFFLOAD_H2C_SET_CATEGORY(hdr, 0x01);
	FW_OFFLOAD_H2C_SET_CMD_ID(hdr, 0xFF);

	PLTFM_MUTEX_LOCK(&adapter->h2c_seq_mutex);
	FW_OFFLOAD_H2C_SET_SEQ_NUM(hdr, adapter->h2c_info.seq_num);
	*seq_num = adapter->h2c_info.seq_num;
	(adapter->h2c_info.seq_num)++;
	PLTFM_MUTEX_UNLOCK(&adapter->h2c_seq_mutex);

	if (info->ack == 1)
		FW_OFFLOAD_H2C_SET_ACK(hdr, 1);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
send_h2c_pkt_88xx(struct halmac_adapter *adapter, u8 *pkt)
{
	u32 cnt = 100;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	while (adapter->h2c_info.buf_fs <= H2C_PKT_SIZE_88XX) {
		get_h2c_buf_free_space_88xx(adapter);
		cnt--;
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]h2c free space!!\n");
			return HALMAC_RET_H2C_SPACE_FULL;
		}
	}

	cnt = 100;
	do {
		if (PLTFM_SEND_H2C_PKT(pkt, H2C_PKT_SIZE_88XX) == 1)
			break;
		cnt--;
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]pltfm - sned h2c pkt!!\n");
			return HALMAC_RET_SEND_H2C_FAIL;
		}
		PLTFM_DELAY_US(5);

	} while (1);

	adapter->h2c_info.buf_fs -= H2C_PKT_SIZE_88XX;

	return status;
}

enum halmac_ret_status
get_h2c_buf_free_space_88xx(struct halmac_adapter *adapter)
{
	u32 hw_wptr;
	u32 fw_rptr;
	struct halmac_h2c_info *info = &adapter->h2c_info;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	hw_wptr = HALMAC_REG_R32(REG_H2C_PKT_WRITEADDR) & 0x3FFFF;
	fw_rptr = HALMAC_REG_R32(REG_H2C_PKT_READADDR) & 0x3FFFF;

	if (hw_wptr >= fw_rptr)
		info->buf_fs = info->buf_size - (hw_wptr - fw_rptr);
	else
		info->buf_fs = fw_rptr - hw_wptr;

	return HALMAC_RET_SUCCESS;
}

/**
 * get_c2h_info_88xx() - process halmac C2H packet
 * @adapter : the adapter of halmac
 * @buf : RX Packet pointer
 * @size : RX Packet size
 *
 * Note : Don't use any IO or DELAY in this API
 *
 * Author : KaiYuan Chang/Ivan Lin
 *
 * Used to process c2h packet info from RX path. After receiving the packet,
 * user need to call this api and pass the packet pointer.
 *
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_c2h_info_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (GET_RX_DESC_C2H(buf) == 1) {
		PLTFM_MSG_TRACE("[TRACE]Parse c2h pkt\n");

		status = parse_c2h_pkt_88xx(adapter, buf, size);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]Parse c2h pkt\n");
			return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
parse_c2h_pkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 cmd_id;
	u8 sub_cmd_id;
	u8 *c2h_pkt = buf + adapter->hw_cfg_info.rxdesc_size;
	u32 c2h_size = size - adapter->hw_cfg_info.rxdesc_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	cmd_id = (u8)C2H_HDR_GET_CMD_ID(c2h_pkt);

	if (cmd_id != 0xFF) {
		PLTFM_MSG_TRACE("[TRACE]Not 0xFF cmd!!\n");
		return HALMAC_RET_C2H_NOT_HANDLED;
	}

	sub_cmd_id = (u8)C2H_HDR_GET_C2H_SUB_CMD_ID(c2h_pkt);

	switch (sub_cmd_id) {
	case C2H_SUB_CMD_ID_C2H_DBG:
		status = get_c2h_dbg_88xx(adapter, c2h_pkt, c2h_size);
		break;
	case C2H_SUB_CMD_ID_H2C_ACK_HDR:
		status = get_h2c_ack_88xx(adapter, c2h_pkt, c2h_size);
		break;
	case C2H_SUB_CMD_ID_BT_COEX_INFO:
		status = HALMAC_RET_C2H_NOT_HANDLED;
		break;
	case C2H_SUB_CMD_ID_SCAN_STATUS_RPT:
		status = get_scan_rpt_88xx(adapter, c2h_pkt, c2h_size);
		break;
	case C2H_SUB_CMD_ID_PSD_DATA:
		status = get_psd_data_88xx(adapter, c2h_pkt, c2h_size);
		break;
	case C2H_SUB_CMD_ID_EFUSE_DATA:
		status = get_efuse_data_88xx(adapter, c2h_pkt, c2h_size);
		break;
	case C2H_SUB_CMD_ID_SCAN_CH_NOTIFY:
		status = get_scan_ch_notify_88xx(adapter, c2h_pkt, c2h_size);
		break;
	default:
		PLTFM_MSG_WARN("[WARN]Sub cmd id!!\n");
		status = HALMAC_RET_C2H_NOT_HANDLED;
		break;
	}

	return status;
}

static enum halmac_ret_status
get_c2h_dbg_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 i;
	u8 next_msg = 0;
	u8 cur_msg = 0;
	u8 msg_len = 0;
	char *c2h_buf = (char *)NULL;
	u8 content_len = 0;
	u8 seq_num = 0;

	content_len = (u8)C2H_HDR_GET_LEN((u8 *)buf);

	if (content_len > C2H_DBG_CONTENT_MAX_LENGTH) {
		PLTFM_MSG_ERR("[ERR]c2h size > max len!\n");
		return HALMAC_RET_C2H_NOT_HANDLED;
	}

	for (i = 0; i < content_len; i++) {
		if (*(buf + C2H_DBG_HDR_LEN + i) == '\n') {
			if ((*(buf + C2H_DBG_HDR_LEN + i + 1) == '\0') ||
			    (*(buf + C2H_DBG_HDR_LEN + i + 1) == 0xff)) {
				next_msg = C2H_DBG_HDR_LEN + i + 1;
				goto _ENDFOUND;
			}
		}
	}

_ENDFOUND:
	msg_len = next_msg - C2H_DBG_HDR_LEN;

	c2h_buf = (char *)PLTFM_MALLOC(msg_len);
	if (!c2h_buf)
		return HALMAC_RET_MALLOC_FAIL;

	PLTFM_MEMCPY(c2h_buf, buf + C2H_DBG_HDR_LEN, msg_len);

	seq_num = (u8)(*(c2h_buf));
	*(c2h_buf + msg_len - 1) = '\0';
	PLTFM_MSG_ALWAYS("[RTKFW, SEQ=%d]: %s\n",
			 seq_num, (char *)(c2h_buf + 1));
	PLTFM_FREE(c2h_buf, msg_len);

	while (*(buf + next_msg) != '\0') {
		cur_msg = next_msg;

		msg_len = (u8)(*(buf + cur_msg + 3)) - 1;
		next_msg += C2H_DBG_HDR_LEN + msg_len;

		c2h_buf = (char *)PLTFM_MALLOC(msg_len);
		if (!c2h_buf)
			return HALMAC_RET_MALLOC_FAIL;

		PLTFM_MEMCPY(c2h_buf, buf + cur_msg + C2H_DBG_HDR_LEN, msg_len);
		*(c2h_buf + msg_len - 1) = '\0';
		seq_num = (u8)(*(c2h_buf));
		PLTFM_MSG_ALWAYS("[RTKFW, SEQ=%d]: %s\n",
				 seq_num, (char *)(c2h_buf + 1));
		PLTFM_FREE(c2h_buf, msg_len);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 cmd_id;
	u8 sub_cmd_id;
	u8 fw_rc;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]Ack for C2H!!\n");

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	if (HALMAC_H2C_RETURN_SUCCESS != (enum halmac_h2c_return_code)fw_rc)
		PLTFM_MSG_TRACE("[TRACE]fw rc = %d\n", fw_rc);

	cmd_id = (u8)H2C_ACK_HDR_GET_H2C_CMD_ID(buf);

	if (cmd_id != 0xFF) {
		PLTFM_MSG_ERR("[ERR]h2c ack cmd id!!\n");
		return HALMAC_RET_C2H_NOT_HANDLED;
	}

	sub_cmd_id = (u8)H2C_ACK_HDR_GET_H2C_SUB_CMD_ID(buf);

	switch (sub_cmd_id) {
	case H2C_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK:
		status = get_h2c_ack_phy_efuse_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_CFG_PARAM_ACK:
		status = get_h2c_ack_cfg_param_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_UPDATE_PKT_ACK:
		status = get_h2c_ack_update_pkt_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_SEND_SCAN_PKT_ACK:
		status = get_h2c_ack_send_scan_pkt_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_DROP_SCAN_PKT_ACK:
		status = get_h2c_ack_drop_scan_pkt_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_UPDATE_DATAPACK_ACK:
		status = get_h2c_ack_update_datapkt_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_RUN_DATAPACK_ACK:
		status = get_h2c_ack_run_datapkt_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_CH_SWITCH_ACK:
		status = get_h2c_ack_ch_switch_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_IQK_ACK:
		status = get_h2c_ack_iqk_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_PWR_TRK_ACK:
		status = get_h2c_ack_pwr_trk_88xx(adapter, buf, size);
		break;
	case H2C_SUB_CMD_ID_PSD_ACK:
		break;
	case H2C_SUB_CMD_ID_FW_SNDING_ACK:
		status = get_h2c_ack_fw_snding_88xx(adapter, buf, size);
		break;
	default:
		status = HALMAC_RET_C2H_NOT_HANDLED;
		break;
	}

	return status;
}

static enum halmac_ret_status
get_scan_ch_notify_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	struct halmac_scan_rpt_info *scan_rpt_info = &adapter->scan_rpt_info;

	PLTFM_MSG_TRACE("[TRACE]scan mode:%d\n", adapter->ch_sw_info.scan_mode);

	if (adapter->ch_sw_info.scan_mode == 1) {
		if (scan_rpt_info->avl_buf_size < 12) {
			PLTFM_MSG_ERR("[ERR]ch_notify buffer full!!\n");
			return HALMAC_RET_CH_SW_NO_BUF;
		}

		SCAN_CH_NOTIFY_SET_CH_NUM(scan_rpt_info->buf_wptr,
					  (u8)SCAN_CH_NOTIFY_GET_CH_NUM(buf));
		SCAN_CH_NOTIFY_SET_NOTIFY_ID(scan_rpt_info->buf_wptr,
					     SCAN_CH_NOTIFY_GET_NOTIFY_ID(buf));
		SCAN_CH_NOTIFY_SET_STATUS(scan_rpt_info->buf_wptr,
					  (u8)SCAN_CH_NOTIFY_GET_STATUS(buf));
		SCAN_CH_NOTIFY_SET_TSF_0(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_0(buf));
		SCAN_CH_NOTIFY_SET_TSF_1(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_1(buf));
		SCAN_CH_NOTIFY_SET_TSF_2(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_2(buf));
		SCAN_CH_NOTIFY_SET_TSF_3(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_3(buf));
		SCAN_CH_NOTIFY_SET_TSF_4(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_4(buf));
		SCAN_CH_NOTIFY_SET_TSF_5(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_5(buf));
		SCAN_CH_NOTIFY_SET_TSF_6(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_6(buf));
		SCAN_CH_NOTIFY_SET_TSF_7(scan_rpt_info->buf_wptr,
					 (u8)SCAN_CH_NOTIFY_GET_TSF_7(buf));

		scan_rpt_info->avl_buf_size = scan_rpt_info->avl_buf_size - 12;
		scan_rpt_info->total_size = scan_rpt_info->total_size + 12;
		scan_rpt_info->buf_wptr = scan_rpt_info->buf_wptr + 12;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_scan_rpt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 fw_rc;
	enum halmac_cmd_process_status proc_status;
	struct halmac_scan_rpt_info *scan_rpt_info = &adapter->scan_rpt_info;

	fw_rc = (u8)SCAN_STATUS_RPT_GET_H2C_RETURN_CODE(buf);
	proc_status = (HALMAC_H2C_RETURN_SUCCESS ==
		(enum halmac_h2c_return_code)fw_rc) ?
		HALMAC_CMD_PROCESS_DONE : HALMAC_CMD_PROCESS_ERROR;

	PLTFM_EVENT_SIG(HALMAC_FEATURE_CHANNEL_SWITCH, proc_status, NULL, 0);

	adapter->halmac_state.scan_state.proc_status = proc_status;

	if (adapter->ch_sw_info.scan_mode == 1) {
		scan_rpt_info->rpt_tsf_low =
			((SCAN_STATUS_RPT_GET_TSF_3(buf) << 24) |
			(SCAN_STATUS_RPT_GET_TSF_2(buf) << 16) |
			(SCAN_STATUS_RPT_GET_TSF_1(buf) << 8) |
			(SCAN_STATUS_RPT_GET_TSF_0(buf)));
		scan_rpt_info->rpt_tsf_high =
			((SCAN_STATUS_RPT_GET_TSF_7(buf) << 24) |
			(SCAN_STATUS_RPT_GET_TSF_6(buf) << 16) |
			(SCAN_STATUS_RPT_GET_TSF_5(buf) << 8) |
			(SCAN_STATUS_RPT_GET_TSF_4(buf)));
	}

	PLTFM_MSG_TRACE("[TRACE]scan : %X\n", proc_status);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_cfg_param_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	u32 offset_accum;
	u32 value_accum;
	struct halmac_cfg_param_state *state =
		&adapter->halmac_state.cfg_param_state;
	enum halmac_cmd_process_status proc_status =
		HALMAC_CMD_PROCESS_UNDEFINE;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;
	offset_accum = CFG_PARAM_ACK_GET_OFFSET_ACCUMULATION(buf);
	value_accum = CFG_PARAM_ACK_GET_VALUE_ACCUMULATION(buf);

	if (offset_accum != adapter->cfg_param_info.offset_accum ||
	    value_accum != adapter->cfg_param_info.value_accum) {
		PLTFM_MSG_ERR("[ERR][C2H]offset_accu : %x, value_accu : %xn",
			      offset_accum, value_accum);
		PLTFM_MSG_ERR("[ERR][Ada]offset_accu : %x, value_accu : %x\n",
			      adapter->cfg_param_info.offset_accum,
			      adapter->cfg_param_info.value_accum);
		proc_status = HALMAC_CMD_PROCESS_ERROR;
	}

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS &&
	    proc_status != HALMAC_CMD_PROCESS_ERROR) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_CFG_PARA, proc_status, NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_CFG_PARA, proc_status,
				&fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_update_pkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_update_pkt_state *state =
		&adapter->halmac_state.update_pkt_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if (HALMAC_H2C_RETURN_SUCCESS == (enum halmac_h2c_return_code)fw_rc) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_UPDATE_PACKET, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_UPDATE_PACKET, proc_status,
				&state->fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_send_scan_pkt_88xx(struct halmac_adapter *adapter,
			       u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_scan_pkt_state *state =
		&adapter->halmac_state.scan_pkt_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if (HALMAC_H2C_RETURN_SUCCESS == (enum halmac_h2c_return_code)fw_rc) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_SEND_SCAN_PACKET, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_SEND_SCAN_PACKET, proc_status,
				&state->fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_drop_scan_pkt_88xx(struct halmac_adapter *adapter,
			       u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_drop_pkt_state *state =
		&adapter->halmac_state.drop_pkt_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if (HALMAC_H2C_RETURN_SUCCESS == (enum halmac_h2c_return_code)fw_rc) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_DROP_SCAN_PACKET, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_DROP_SCAN_PACKET, proc_status,
				&state->fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_h2c_ack_update_datapkt_88xx(struct halmac_adapter *adapter, u8 *buf,
				u32 size)
{
	return HALMAC_RET_NOT_SUPPORT;
}

static enum halmac_ret_status
get_h2c_ack_run_datapkt_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	return HALMAC_RET_NOT_SUPPORT;
}

static enum halmac_ret_status
get_h2c_ack_ch_switch_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_scan_state *state = &adapter->halmac_state.scan_state;
	struct halmac_scan_rpt_info *scan_rpt_info = &adapter->scan_rpt_info;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if (adapter->ch_sw_info.scan_mode == 1) {
		scan_rpt_info->ack_tsf_low =
			((CH_SWITCH_ACK_GET_TSF_3(buf) << 24) |
			(CH_SWITCH_ACK_GET_TSF_2(buf) << 16) |
			(CH_SWITCH_ACK_GET_TSF_1(buf) << 8) |
			(CH_SWITCH_ACK_GET_TSF_0(buf)));
		scan_rpt_info->ack_tsf_high =
			((CH_SWITCH_ACK_GET_TSF_7(buf) << 24) |
			(CH_SWITCH_ACK_GET_TSF_6(buf) << 16) |
			(CH_SWITCH_ACK_GET_TSF_5(buf) << 8) |
			(CH_SWITCH_ACK_GET_TSF_4(buf)));
	}

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_RCVD;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_CHANNEL_SWITCH, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_CHANNEL_SWITCH, proc_status,
				&fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * mac_debug_88xx_v1() - read some registers for debug
 * @adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
mac_debug_88xx(struct halmac_adapter *adapter)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->intf == HALMAC_INTERFACE_SDIO)
		dump_reg_sdio_88xx(adapter);
	else
		dump_reg_88xx(adapter);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static void
dump_reg_sdio_88xx(struct halmac_adapter *adapter)
{
	u8 tmp8;
	u32 i;

	/* Dump CCCR, it needs new platform api */

	/*Dump SDIO Local Register, use CMD52*/
	for (i = 0x10250000; i < 0x102500ff; i++) {
		tmp8 = PLTFM_SDIO_CMD52_R(i);
		PLTFM_MSG_TRACE("[TRACE]dbg-sdio[%x]=%x\n", i, tmp8);
	}

	/*Dump MAC Register*/
	for (i = 0x0000; i < 0x17ff; i++) {
		tmp8 = PLTFM_SDIO_CMD52_R(i);
		PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp8);
	}

	tmp8 = PLTFM_SDIO_CMD52_R(REG_SDIO_CRC_ERR_IDX);
	if (tmp8)
		PLTFM_MSG_ERR("[ERR]sdio crc=%x\n", tmp8);

	/*Check RX Fifo status*/
	i = REG_RXFF_PTR_V1;
	tmp8 = PLTFM_SDIO_CMD52_R(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp8);
	i = REG_RXFF_WTR_V1;
	tmp8 = PLTFM_SDIO_CMD52_R(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp8);
	i = REG_RXFF_PTR_V1;
	tmp8 = PLTFM_SDIO_CMD52_R(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp8);
	i = REG_RXFF_WTR_V1;
	tmp8 = PLTFM_SDIO_CMD52_R(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp8);
}

static void
dump_reg_88xx(struct halmac_adapter *adapter)
{
	u32 tmp32;
	u32 i;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	/*Dump MAC Register*/
	for (i = 0x0000; i < 0x17fc; i += 4) {
		tmp32 = HALMAC_REG_R32(i);
		PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp32);
	}

	/*Check RX Fifo status*/
	i = REG_RXFF_PTR_V1;
	tmp32 = HALMAC_REG_R32(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp32);
	i = REG_RXFF_WTR_V1;
	tmp32 = HALMAC_REG_R32(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp32);
	i = REG_RXFF_PTR_V1;
	tmp32 = HALMAC_REG_R32(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp32);
	i = REG_RXFF_WTR_V1;
	tmp32 = HALMAC_REG_R32(i);
	PLTFM_MSG_TRACE("[TRACE]dbg-mac[%x]=%x\n", i, tmp32);
}

/**
 * cfg_parameter_88xx() - config parameter by FW
 * @adapter : the adapter of halmac
 * @info : cmd id, content
 * @full_fifo : parameter information
 *
 * If msk_en = 1, the format of array is {reg_info, mask, value}.
 * If msk_en =_FAUSE, the format of array is {reg_info, value}
 * The format of reg_info is
 * reg_info[31]=rf_reg, 0: MAC_BB reg, 1: RF reg
 * reg_info[27:24]=rf_path, 0: path_A, 1: path_B
 * if rf_reg=0(MAC_BB reg), rf_path is meaningless.
 * ref_info[15:0]=offset
 *
 * Example: msk_en = 0
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
cfg_parameter_88xx(struct halmac_adapter *adapter,
		   struct halmac_phy_parameter_info *info, u8 full_fifo)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;
	enum halmac_cmd_construct_state cmd_state;

	proc_status = &adapter->halmac_state.cfg_param_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(para)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	cmd_state = cfg_param_cmd_cnstr_state_88xx(adapter);
	if (cmd_state != HALMAC_CMD_CNSTR_IDLE &&
	    cmd_state != HALMAC_CMD_CNSTR_CNSTR) {
		PLTFM_MSG_TRACE("[TRACE]Not idle(para)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_IDLE;

	status = proc_cfg_param_88xx(adapter, info, full_fifo);

	if (status != HALMAC_RET_SUCCESS && status != HALMAC_RET_PARA_SENDING) {
		PLTFM_MSG_ERR("[ERR]send param h2c\n");
		return status;
	}

	return status;
}

static enum halmac_cmd_construct_state
cfg_param_cmd_cnstr_state_88xx(struct halmac_adapter *adapter)
{
	return adapter->halmac_state.cfg_param_state.cmd_cnstr_state;
}

static enum halmac_ret_status
proc_cfg_param_88xx(struct halmac_adapter *adapter,
		    struct halmac_phy_parameter_info *param, u8 full_fifo)
{
	u8 end_cmd = 0;
	u32 rsvd_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_cfg_param_info *info = &adapter->cfg_param_info;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.cfg_param_state.proc_status;

	status = malloc_cfg_param_buf_88xx(adapter, full_fifo);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	if (cnv_cfg_param_state_88xx(adapter, HALMAC_CMD_CNSTR_CNSTR) !=
	    HALMAC_RET_SUCCESS) {
		PLTFM_FREE(info->buf, info->buf_size);
		info->buf = NULL;
		info->buf_wptr = NULL;
		return HALMAC_RET_ERROR_STATE;
	}

	add_param_buf_88xx(adapter, param, info->buf_wptr, &end_cmd);
	if (param->cmd_id != HALMAC_PARAMETER_CMD_END) {
		info->num++;
		info->buf_wptr += CFG_PARAM_H2C_INFO_SIZE;
		info->avl_buf_size -= CFG_PARAM_H2C_INFO_SIZE;
	}

	rsvd_size = info->avl_buf_size - adapter->hw_cfg_info.txdesc_size;
	if (rsvd_size > CFG_PARAM_H2C_INFO_SIZE && end_cmd == 0)
		return HALMAC_RET_SUCCESS;

	if (info->num == 0) {
		PLTFM_FREE(info->buf, info->buf_size);
		info->buf = NULL;
		info->buf_wptr = NULL;
		PLTFM_MSG_TRACE("[TRACE]param num = 0!!\n");

		*proc_status = HALMAC_CMD_PROCESS_DONE;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_CFG_PARA, *proc_status, NULL, 0);

		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_CFG_PARA);

		return HALMAC_RET_SUCCESS;
	}

	status = send_cfg_param_h2c_88xx(adapter);
	if (status != HALMAC_RET_SUCCESS) {
		if (info->buf) {
			PLTFM_FREE(info->buf, info->buf_size);
			info->buf = NULL;
			info->buf_wptr = NULL;
		}
		return status;
	}

	if (end_cmd == 0) {
		PLTFM_MSG_TRACE("[TRACE]send h2c-buf full\n");
		return HALMAC_RET_PARA_SENDING;
	}

	return status;
}

static enum halmac_ret_status
send_cfg_param_h2c_88xx(struct halmac_adapter *adapter)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 pg_addr;
	u16 seq_num = 0;
	u32 info_size;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_cfg_param_info *info = &adapter->cfg_param_info;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.cfg_param_state.proc_status;

	if (cnv_cfg_param_state_88xx(adapter, HALMAC_CMD_CNSTR_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	if (info->full_fifo_mode == 1)
		pg_addr = 0;
	else
		pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;

	info_size = info->num * CFG_PARAM_H2C_INFO_SIZE;

	status = dl_rsvd_page_88xx(adapter, pg_addr, info->buf, info_size);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dl rsvd pg!!\n");
		goto CFG_PARAM_H2C_FAIL;
	}

	gen_cfg_param_h2c_88xx(adapter, h2c_buf);

	hdr_info.sub_cmd_id = SUB_CMD_ID_CFG_PARAM;
	hdr_info.content_size = 4;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.cfg_param_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_CFG_PARA);
	}

CFG_PARAM_H2C_FAIL:
	PLTFM_FREE(info->buf, info->buf_size);
	info->buf = NULL;
	info->buf_wptr = NULL;

	if (cnv_cfg_param_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	return status;
}

static enum halmac_ret_status
cnv_cfg_param_state_88xx(struct halmac_adapter *adapter,
			 enum halmac_cmd_construct_state dest_state)
{
	enum halmac_cmd_construct_state *state;

	state = &adapter->halmac_state.cfg_param_state.cmd_cnstr_state;

	if ((*state != HALMAC_CMD_CNSTR_IDLE) &&
	    (*state != HALMAC_CMD_CNSTR_CNSTR) &&
	    (*state != HALMAC_CMD_CNSTR_H2C_SENT))
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_CMD_CNSTR_IDLE) {
		if (*state == HALMAC_CMD_CNSTR_CNSTR)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_CNSTR) {
		if (*state == HALMAC_CMD_CNSTR_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_H2C_SENT) {
		if ((*state == HALMAC_CMD_CNSTR_IDLE) ||
		    (*state == HALMAC_CMD_CNSTR_H2C_SENT))
			return HALMAC_RET_ERROR_STATE;
	}

	*state = dest_state;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
add_param_buf_88xx(struct halmac_adapter *adapter,
		   struct halmac_phy_parameter_info *param, u8 *buf,
		   u8 *end_cmd)
{
	struct halmac_cfg_param_info *info = &adapter->cfg_param_info;
	union halmac_parameter_content *content = &param->content;

	*end_cmd = 0;

	PARAM_INFO_SET_LEN(buf, CFG_PARAM_H2C_INFO_SIZE);
	PARAM_INFO_SET_IO_CMD(buf, param->cmd_id);

	switch (param->cmd_id) {
	case HALMAC_PARAMETER_CMD_BB_W8:
	case HALMAC_PARAMETER_CMD_BB_W16:
	case HALMAC_PARAMETER_CMD_BB_W32:
	case HALMAC_PARAMETER_CMD_MAC_W8:
	case HALMAC_PARAMETER_CMD_MAC_W16:
	case HALMAC_PARAMETER_CMD_MAC_W32:
		PARAM_INFO_SET_IO_ADDR(buf, content->MAC_REG_W.offset);
		PARAM_INFO_SET_DATA(buf, content->MAC_REG_W.value);
		PARAM_INFO_SET_MASK(buf, content->MAC_REG_W.msk);
		PARAM_INFO_SET_MSK_EN(buf, content->MAC_REG_W.msk_en);
		info->value_accum += content->MAC_REG_W.value;
		info->offset_accum += content->MAC_REG_W.offset;
		break;
	case HALMAC_PARAMETER_CMD_RF_W:
		/*In rf register, the address is only 1 byte*/
		PARAM_INFO_SET_RF_ADDR(buf, content->RF_REG_W.offset);
		PARAM_INFO_SET_RF_PATH(buf, content->RF_REG_W.rf_path);
		PARAM_INFO_SET_DATA(buf, content->RF_REG_W.value);
		PARAM_INFO_SET_MASK(buf, content->RF_REG_W.msk);
		PARAM_INFO_SET_MSK_EN(buf, content->RF_REG_W.msk_en);
		info->value_accum += content->RF_REG_W.value;
		info->offset_accum += (content->RF_REG_W.offset +
					(content->RF_REG_W.rf_path << 8));
		break;
	case HALMAC_PARAMETER_CMD_DELAY_US:
	case HALMAC_PARAMETER_CMD_DELAY_MS:
		PARAM_INFO_SET_DELAY_VAL(buf, content->DELAY_TIME.delay_time);
		break;
	case HALMAC_PARAMETER_CMD_END:
		*end_cmd = 1;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]cmd id!!\n");
		break;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
gen_cfg_param_h2c_88xx(struct halmac_adapter *adapter, u8 *buff)
{
	struct halmac_cfg_param_info *info = &adapter->cfg_param_info;
	u16 h2c_info_addr = adapter->txff_alloc.rsvd_h2c_info_addr;
	u16 rsvd_pg_addr = adapter->txff_alloc.rsvd_boundary;

	CFG_PARAM_SET_NUM(buff, info->num);

	if (info->full_fifo_mode == 1) {
		CFG_PARAM_SET_INIT_CASE(buff, 0x1);
		CFG_PARAM_SET_LOC(buff, 0);
	} else {
		CFG_PARAM_SET_INIT_CASE(buff, 0x0);
		CFG_PARAM_SET_LOC(buff, h2c_info_addr - rsvd_pg_addr);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
malloc_cfg_param_buf_88xx(struct halmac_adapter *adapter, u8 full_fifo)
{
	struct halmac_cfg_param_info *info = &adapter->cfg_param_info;
	struct halmac_pltfm_cfg_info *pltfm_info = &adapter->pltfm_info;

	if (info->buf)
		return HALMAC_RET_SUCCESS;

	if (full_fifo == 1)
		info->buf_size = pltfm_info->malloc_size;
	else
		info->buf_size = CFG_PARAM_RSVDPG_SIZE;

	if (info->buf_size > pltfm_info->rsvd_pg_size)
		info->buf_size = pltfm_info->rsvd_pg_size;

	info->buf = smart_malloc_88xx(adapter, info->buf_size, &info->buf_size);
	if (info->buf) {
		PLTFM_MEMSET(info->buf, 0x00, info->buf_size);
		info->full_fifo_mode = full_fifo;
		info->buf_wptr = info->buf;
		info->num = 0;
		info->avl_buf_size = info->buf_size;
		info->value_accum = 0;
		info->offset_accum = 0;
	} else {
		return HALMAC_RET_MALLOC_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * update_packet_88xx() - send specific packet to FW
 * @adapter : the adapter of halmac
 * @pkt_id : packet id, to know the purpose of this packet
 * @pkt : packet
 * @size : packet size
 *
 * Note : TX_DESC is not included in the pkt
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
update_packet_88xx(struct halmac_adapter *adapter, enum halmac_packet_id pkt_id,
		   u8 *pkt, u32 size)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status =
		&adapter->halmac_state.update_pkt_state.proc_status;
	u8 *used_page = &adapter->halmac_state.update_pkt_state.used_page;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	if (size > UPDATE_PKT_RSVDPG_SIZE)
		return HALMAC_RET_RSVD_PG_OVERFLOW_FAIL;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(upd)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	status = send_h2c_update_packet_88xx(adapter, pkt_id, pkt, size);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		PLTFM_MSG_ERR("[ERR]pkt id : %X!!\n", pkt_id);
		return status;
	}

	*used_page = (u8)get_update_packet_page_size(adapter, size);

	if (packet_in_nlo_88xx(adapter, pkt_id)) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;
		adapter->nlo_flag = 1;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
send_h2c_update_packet_88xx(struct halmac_adapter *adapter,
			    enum halmac_packet_id pkt_id, u8 *pkt, u32 size)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	u16 pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;
	u16 pg_offset;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_packet_id real_pkt_id;

	status = dl_rsvd_page_88xx(adapter, pg_addr, pkt, size);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dl rsvd pg!!\n");
		return status;
	}

	real_pkt_id = get_real_pkt_id_88xx(adapter, pkt_id);
	pg_offset = pg_addr - adapter->txff_alloc.rsvd_boundary;
	UPDATE_PKT_SET_SIZE(h2c_buf, size + adapter->hw_cfg_info.txdesc_size);
	UPDATE_PKT_SET_ID(h2c_buf, real_pkt_id);
	UPDATE_PKT_SET_LOC(h2c_buf, pg_offset);

	hdr_info.sub_cmd_id = SUB_CMD_ID_UPDATE_PKT;
	hdr_info.content_size = 4;
	if (packet_in_nlo_88xx(adapter, pkt_id))
		hdr_info.ack = 0;
	else
		hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);
	adapter->halmac_state.update_pkt_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_UPDATE_PACKET);
		return status;
	}

	return status;
}

enum halmac_ret_status
send_scan_packet_88xx(struct halmac_adapter *adapter, u8 index,
		      u8 *pkt, u32 size)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status =
		&adapter->halmac_state.scan_pkt_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 13)
		return HALMAC_RET_FW_NO_SUPPORT;

	if (size > UPDATE_PKT_RSVDPG_SIZE)
		return HALMAC_RET_RSVD_PG_OVERFLOW_FAIL;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(send_scan)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	status = send_h2c_send_scan_packet_88xx(adapter, index, pkt, size);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
send_h2c_send_scan_packet_88xx(struct halmac_adapter *adapter,
			       u8 index, u8 *pkt, u32 size)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	u16 pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;
	u16 pg_offset;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = dl_rsvd_page_88xx(adapter, pg_addr, pkt, size);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dl rsvd pg!!\n");
		return status;
	}

	pg_offset = pg_addr - adapter->txff_alloc.rsvd_boundary;
	SEND_SCAN_PKT_SET_SIZE(h2c_buf, size +
			       adapter->hw_cfg_info.txdesc_size);
	SEND_SCAN_PKT_SET_INDEX(h2c_buf, index);
	SEND_SCAN_PKT_SET_LOC(h2c_buf, pg_offset);

	hdr_info.sub_cmd_id = SUB_CMD_ID_SEND_SCAN_PKT;
	hdr_info.content_size = 8;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);
	adapter->halmac_state.scan_pkt_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		reset_ofld_feature_88xx(adapter,
					HALMAC_FEATURE_SEND_SCAN_PACKET);
		return status;
	}

	return status;
}

enum halmac_ret_status
drop_scan_packet_88xx(struct halmac_adapter *adapter,
		      struct halmac_drop_pkt_option *option)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status =
		&adapter->halmac_state.drop_pkt_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 13)
		return HALMAC_RET_FW_NO_SUPPORT;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(drop_scan)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	status = send_h2c_drop_scan_packet_88xx(adapter, option);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
send_h2c_drop_scan_packet_88xx(struct halmac_adapter *adapter,
			       struct halmac_drop_pkt_option *option)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]%s\n", __func__);

	DROP_SCAN_PKT_SET_DROP_ALL(h2c_buf, option->drop_all);
	DROP_SCAN_PKT_SET_DROP_SINGLE(h2c_buf, option->drop_single);
	DROP_SCAN_PKT_SET_DROP_IDX(h2c_buf, option->drop_index);

	hdr_info.sub_cmd_id = SUB_CMD_ID_DROP_SCAN_PKT;
	hdr_info.content_size = 8;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);
	adapter->halmac_state.drop_pkt_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		reset_ofld_feature_88xx(adapter,
					HALMAC_FEATURE_DROP_SCAN_PACKET);
		return status;
	}

	return status;
}

enum halmac_ret_status
bcn_ie_filter_88xx(struct halmac_adapter *adapter,
		   struct halmac_bcn_ie_info *info)
{
	return HALMAC_RET_NOT_SUPPORT;
}

enum halmac_ret_status
update_datapack_88xx(struct halmac_adapter *adapter,
		     enum halmac_data_type data_type,
		     struct halmac_phy_parameter_info *info)
{
	return HALMAC_RET_NOT_SUPPORT;
}

enum halmac_ret_status
run_datapack_88xx(struct halmac_adapter *adapter,
		  enum halmac_data_type data_type)
{
	return HALMAC_RET_NOT_SUPPORT;
}

enum halmac_ret_status
send_bt_coex_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size, u8 ack)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	status = send_bt_coex_cmd_88xx(adapter, buf, size, ack);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]bt coex cmd!!\n");
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
send_bt_coex_cmd_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size,
		      u8 ack)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	PLTFM_MEMCPY(h2c_buf + 8, buf, size);

	hdr_info.sub_cmd_id = SUB_CMD_ID_BT_COEX;
	hdr_info.content_size = (u16)size;
	hdr_info.ack = ack;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * dump_fifo_88xx() - dump fifo data
 * @adapter : the adapter of halmac
 * @sel : FIFO selection
 * @start_addr : start address of selected FIFO
 * @size : dump size of selected FIFO
 * @data : FIFO data
 *
 * Note : before dump fifo, user need to call halmac_get_fifo_size to
 * get fifo size. Then input this size to halmac_dump_fifo.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_fifo_88xx(struct halmac_adapter *adapter, enum hal_fifo_sel sel,
	       u32 start_addr, u32 size, u8 *data)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u8 tmp8;
	u8 enable;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (sel == HAL_FIFO_SEL_TX &&
	    (start_addr + size) > adapter->hw_cfg_info.tx_fifo_size) {
		PLTFM_MSG_ERR("[ERR]size overflow!!\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (sel == HAL_FIFO_SEL_RX &&
	    (start_addr + size) > adapter->hw_cfg_info.rx_fifo_size) {
		PLTFM_MSG_ERR("[ERR]size overflow!!\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if ((size & (4 - 1)) != 0) {
		PLTFM_MSG_ERR("[ERR]not 4byte alignment!!\n");
		return HALMAC_RET_DUMP_FIFOSIZE_INCORRECT;
	}

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	tmp8 = HALMAC_REG_R8(REG_RCR + 2);
	enable = 0;
	status = api->halmac_set_hw_value(adapter, HALMAC_HW_RX_CLK_GATE,
					  &enable);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = read_buf_88xx(adapter, start_addr, size, sel, data);

	HALMAC_REG_W8(REG_RCR + 2, tmp8);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read buf!!\n");
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
read_buf_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
	      enum hal_fifo_sel sel, u8 *data)
{
	u32 start_pg;
	u32 value32;
	u32 i;
	u32 residue;
	u32 cnt = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (sel == HAL_FIFO_SEL_RSVD_PAGE)
		offset += (adapter->txff_alloc.rsvd_boundary <<
			   TX_PAGE_SIZE_SHIFT_88XX);

	start_pg = offset >> 12;
	residue = offset & (4096 - 1);

	if (sel == HAL_FIFO_SEL_TX || sel == HAL_FIFO_SEL_RSVD_PAGE)
		start_pg += 0x780;
	else if (sel == HAL_FIFO_SEL_RX)
		start_pg += 0x700;
	else if (sel == HAL_FIFO_SEL_REPORT)
		start_pg += 0x660;
	else if (sel == HAL_FIFO_SEL_LLT)
		start_pg += 0x650;
	else if (sel == HAL_FIFO_SEL_RXBUF_FW)
		start_pg += 0x680;
	else
		return HALMAC_RET_NOT_SUPPORT;

	value32 = HALMAC_REG_R16(REG_PKTBUF_DBG_CTRL) & 0xF000;

	do {
		HALMAC_REG_W16(REG_PKTBUF_DBG_CTRL, (u16)(start_pg | value32));

		for (i = 0x8000 + residue; i <= 0x8FFF; i += 4) {
			*(u32 *)(data + cnt) = HALMAC_REG_R32(i);
			*(u32 *)(data + cnt) =
				rtk_le32_to_cpu(*(u32 *)(data + cnt));
			cnt += 4;
			if (size == cnt)
				goto HALMAC_BUF_READ_OK;
		}

		residue = 0;
		start_pg++;
	} while (1);

HALMAC_BUF_READ_OK:
	HALMAC_REG_W16(REG_PKTBUF_DBG_CTRL, (u16)value32);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_fifo_size_88xx() - get fifo size
 * @adapter : the adapter of halmac
 * @sel : FIFO selection
 * Author : Ivan Lin/KaiYuan Chang
 * Return : u32
 * More details of status code can be found in prototype document
 */
u32
get_fifo_size_88xx(struct halmac_adapter *adapter, enum hal_fifo_sel sel)
{
	u32 size = 0;

	if (sel == HAL_FIFO_SEL_TX)
		size = adapter->hw_cfg_info.tx_fifo_size;
	else if (sel == HAL_FIFO_SEL_RX)
		size = adapter->hw_cfg_info.rx_fifo_size;
	else if (sel == HAL_FIFO_SEL_RSVD_PAGE)
		size = adapter->hw_cfg_info.tx_fifo_size -
		       (adapter->txff_alloc.rsvd_boundary <<
			TX_PAGE_SIZE_SHIFT_88XX);
	else if (sel == HAL_FIFO_SEL_REPORT)
		size = 65536;
	else if (sel == HAL_FIFO_SEL_LLT)
		size = 65536;
	else if (sel == HAL_FIFO_SEL_RXBUF_FW)
		size = RX_BUF_FW_88XX;

	return size;
}

enum halmac_ret_status
set_h2c_header_88xx(struct halmac_adapter *adapter, u8 *hdr, u16 *seq, u8 ack)
{
	PLTFM_MSG_TRACE("[TRACE]%s!!\n", __func__);

	H2C_CMD_HEADER_SET_CATEGORY(hdr, 0x00);
	H2C_CMD_HEADER_SET_TOTAL_LEN(hdr, 16);

	PLTFM_MUTEX_LOCK(&adapter->h2c_seq_mutex);
	H2C_CMD_HEADER_SET_SEQ_NUM(hdr, adapter->h2c_info.seq_num);
	*seq = adapter->h2c_info.seq_num;
	(adapter->h2c_info.seq_num)++;
	PLTFM_MUTEX_UNLOCK(&adapter->h2c_seq_mutex);

	if (ack == 1)
		H2C_CMD_HEADER_SET_ACK(hdr, 1);

	return HALMAC_RET_SUCCESS;
}

/**
 * add_ch_info_88xx() -add channel information
 * @adapter : the adapter of halmac
 * @info : channel information
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
add_ch_info_88xx(struct halmac_adapter *adapter, struct halmac_ch_info *info)
{
	struct halmac_ch_sw_info *ch_sw_info = &adapter->ch_sw_info;
	enum halmac_cmd_construct_state state;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->halmac_state.dlfw_state != HALMAC_GEN_INFO_SENT) {
		PLTFM_MSG_ERR("[ERR]gen info\n");
		return HALMAC_RET_GEN_INFO_NOT_SENT;
	}

	state = scan_cmd_cnstr_state_88xx(adapter);
	if (state != HALMAC_CMD_CNSTR_BUF_CLR &&
	    state != HALMAC_CMD_CNSTR_CNSTR) {
		PLTFM_MSG_WARN("[WARN]cmd state (scan)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (!ch_sw_info->buf) {
		ch_sw_info->buf = (u8 *)PLTFM_MALLOC(SCAN_INFO_RSVDPG_SIZE);
		if (!ch_sw_info->buf)
			return HALMAC_RET_NULL_POINTER;
		ch_sw_info->buf_wptr = ch_sw_info->buf;
		ch_sw_info->buf_size = SCAN_INFO_RSVDPG_SIZE;
		ch_sw_info->avl_buf_size = SCAN_INFO_RSVDPG_SIZE;
		ch_sw_info->total_size = 0;
		ch_sw_info->extra_info_en = 0;
		ch_sw_info->ch_num = 0;
	}

	if (ch_sw_info->extra_info_en == 1) {
		PLTFM_MSG_ERR("[ERR]extra info = 1!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->avl_buf_size < 4) {
		PLTFM_MSG_ERR("[ERR]buf full!!\n");
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (cnv_scan_state_88xx(adapter, HALMAC_CMD_CNSTR_CNSTR) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	CH_INFO_SET_CH(ch_sw_info->buf_wptr, info->channel);
	CH_INFO_SET_PRI_CH_IDX(ch_sw_info->buf_wptr, info->pri_ch_idx);
	CH_INFO_SET_BW(ch_sw_info->buf_wptr, info->bw);
	CH_INFO_SET_TIMEOUT(ch_sw_info->buf_wptr, info->timeout);
	CH_INFO_SET_ACTION_ID(ch_sw_info->buf_wptr, info->action_id);
	CH_INFO_SET_EXTRA_INFO(ch_sw_info->buf_wptr, info->extra_info);

	ch_sw_info->avl_buf_size = ch_sw_info->avl_buf_size - 4;
	ch_sw_info->total_size = ch_sw_info->total_size + 4;
	ch_sw_info->ch_num++;
	ch_sw_info->extra_info_en = info->extra_info;
	ch_sw_info->buf_wptr = ch_sw_info->buf_wptr + 4;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_cmd_construct_state
scan_cmd_cnstr_state_88xx(struct halmac_adapter *adapter)
{
	return adapter->halmac_state.scan_state.cmd_cnstr_state;
}

static enum halmac_ret_status
cnv_scan_state_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_construct_state dest_state)
{
	enum halmac_cmd_construct_state *state;

	state = &adapter->halmac_state.scan_state.cmd_cnstr_state;

	if (dest_state == HALMAC_CMD_CNSTR_IDLE) {
		if ((*state == HALMAC_CMD_CNSTR_BUF_CLR) ||
		    (*state == HALMAC_CMD_CNSTR_CNSTR))
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_BUF_CLR) {
		if (*state == HALMAC_CMD_CNSTR_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_CNSTR) {
		if ((*state == HALMAC_CMD_CNSTR_IDLE) ||
		    (*state == HALMAC_CMD_CNSTR_H2C_SENT))
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_H2C_SENT) {
		if ((*state != HALMAC_CMD_CNSTR_CNSTR) &&
		    (*state != HALMAC_CMD_CNSTR_BUF_CLR))
			return HALMAC_RET_ERROR_STATE;
	}

	*state = dest_state;

	return HALMAC_RET_SUCCESS;
}

/**
 * add_extra_ch_info_88xx() -add extra channel information
 * @adapter : the adapter of halmac
 * @info : extra channel information
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
add_extra_ch_info_88xx(struct halmac_adapter *adapter,
		       struct halmac_ch_extra_info *info)
{
	struct halmac_ch_sw_info *ch_sw_info = &adapter->ch_sw_info;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!ch_sw_info->buf) {
		PLTFM_MSG_ERR("[ERR]buf = null!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->extra_info_en == 0) {
		PLTFM_MSG_ERR("[ERR]extra info = 0!!\n");
		return HALMAC_RET_CH_SW_SEQ_WRONG;
	}

	if (ch_sw_info->avl_buf_size < (u32)(info->extra_info_size + 2)) {
		PLTFM_MSG_ERR("[ERR]no available buffer!!\n");
		return HALMAC_RET_CH_SW_NO_BUF;
	}

	if (scan_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_CNSTR) {
		PLTFM_MSG_WARN("[WARN]cmd state (ex scan)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_scan_state_88xx(adapter, HALMAC_CMD_CNSTR_CNSTR) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	CH_EXTRA_INFO_SET_ID(ch_sw_info->buf_wptr, info->extra_action_id);
	CH_EXTRA_INFO_SET_INFO(ch_sw_info->buf_wptr, info->extra_info);
	CH_EXTRA_INFO_SET_SIZE(ch_sw_info->buf_wptr, info->extra_info_size);
	PLTFM_MEMCPY(ch_sw_info->buf_wptr + 2, info->extra_info_data,
		     info->extra_info_size);

	ch_sw_info->avl_buf_size -= (2 + info->extra_info_size);
	ch_sw_info->total_size += (2 + info->extra_info_size);
	ch_sw_info->extra_info_en = info->extra_info;
	ch_sw_info->buf_wptr += (2 + info->extra_info_size);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * ctrl_ch_switch_88xx() -send channel switch cmd
 * @adapter : the adapter of halmac
 * @opt : channel switch config
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
ctrl_ch_switch_88xx(struct halmac_adapter *adapter,
		    struct halmac_ch_switch_option *opt)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_construct_state state;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.scan_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 4)
		return HALMAC_RET_FW_NO_SUPPORT;

	if (adapter->ch_sw_info.total_size +
	    (adapter->halmac_state.update_pkt_state.used_page <<
	    TX_PAGE_SIZE_SHIFT_88XX) >
	    (u32)adapter->txff_alloc.rsvd_pg_num << TX_PAGE_SIZE_SHIFT_88XX)
		return HALMAC_RET_RSVD_PG_OVERFLOW_FAIL;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (opt->switch_en == 0)
		*proc_status = HALMAC_CMD_PROCESS_IDLE;

	if ((*proc_status == HALMAC_CMD_PROCESS_SENDING) ||
	    (*proc_status == HALMAC_CMD_PROCESS_RCVD)) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(scan)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	state = scan_cmd_cnstr_state_88xx(adapter);
	if (opt->switch_en == 1) {
		if (state != HALMAC_CMD_CNSTR_CNSTR) {
			PLTFM_MSG_ERR("[ERR]state(en = 1)\n");
			return HALMAC_RET_ERROR_STATE;
		}
	} else {
		if (state != HALMAC_CMD_CNSTR_BUF_CLR) {
			PLTFM_MSG_ERR("[ERR]state(en = 0)\n");
			return HALMAC_RET_ERROR_STATE;
		}
	}

	status = proc_ctrl_ch_switch_88xx(adapter, opt);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]ctrl ch sw!!\n");
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_ctrl_ch_switch_88xx(struct halmac_adapter *adapter,
			 struct halmac_ch_switch_option *opt)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	u16 pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;
	struct halmac_h2c_header_info hdr_info;
	struct halmac_scan_rpt_info *scan_rpt_info = &adapter->scan_rpt_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.scan_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->halmac_state.update_pkt_state.used_page > 0 &&
	    opt->nlo_en == 1 && adapter->nlo_flag != 1)
		PLTFM_MSG_WARN("[WARN]probe req is NOT nlo pkt!!\n");

	if (cnv_scan_state_88xx(adapter, HALMAC_CMD_CNSTR_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	if (opt->switch_en != 0) {
		pg_addr += adapter->halmac_state.update_pkt_state.used_page;
		status = dl_rsvd_page_88xx(adapter, pg_addr,
					   adapter->ch_sw_info.buf,
					   adapter->ch_sw_info.total_size);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]dl rsvd pg!!\n");
			return status;
		}
		adapter->halmac_state.update_pkt_state.used_page = 0;
	}

	CH_SWITCH_SET_START(h2c_buf, opt->switch_en);
	CH_SWITCH_SET_CH_NUM(h2c_buf, adapter->ch_sw_info.ch_num);
	CH_SWITCH_SET_INFO_LOC(h2c_buf,
			       pg_addr - adapter->txff_alloc.rsvd_boundary);
	CH_SWITCH_SET_DEST_CH_EN(h2c_buf, opt->dest_ch_en);
	CH_SWITCH_SET_DEST_CH(h2c_buf, opt->dest_ch);
	CH_SWITCH_SET_PRI_CH_IDX(h2c_buf, opt->dest_pri_ch_idx);
	CH_SWITCH_SET_ABSOLUTE_TIME(h2c_buf, opt->absolute_time_en);
	CH_SWITCH_SET_TSF_LOW(h2c_buf, opt->tsf_low);
	CH_SWITCH_SET_PERIODIC_OPT(h2c_buf, opt->periodic_option);
	CH_SWITCH_SET_NORMAL_CYCLE(h2c_buf, opt->normal_cycle);
	CH_SWITCH_SET_NORMAL_PERIOD(h2c_buf, opt->normal_period);
	CH_SWITCH_SET_SLOW_PERIOD(h2c_buf, opt->phase_2_period);
	CH_SWITCH_SET_NORMAL_PERIOD_SEL(h2c_buf, opt->normal_period_sel);
	CH_SWITCH_SET_SLOW_PERIOD_SEL(h2c_buf, opt->phase_2_period_sel);
	CH_SWITCH_SET_SCAN_MODE(h2c_buf, opt->scan_mode_en);
	CH_SWITCH_SET_INFO_SIZE(h2c_buf, adapter->ch_sw_info.total_size);

	hdr_info.sub_cmd_id = SUB_CMD_ID_CH_SWITCH;
	hdr_info.content_size = 20;
	if (opt->nlo_en == 1)
		hdr_info.ack = 0;
	else
		hdr_info.ack = 1;

	if (opt->scan_mode_en == 1) {
		adapter->ch_sw_info.scan_mode = 1;
		if (!scan_rpt_info->buf) {
			scan_rpt_info->buf =
				(u8 *)PLTFM_MALLOC(SCAN_INFO_RSVDPG_SIZE);
			if (!scan_rpt_info->buf)
				return HALMAC_RET_NULL_POINTER;
		} else {
			PLTFM_MEMSET(scan_rpt_info->buf, 0,
				     SCAN_INFO_RSVDPG_SIZE);
		}
		scan_rpt_info->buf_wptr = scan_rpt_info->buf;
		scan_rpt_info->buf_size = SCAN_INFO_RSVDPG_SIZE;
		scan_rpt_info->avl_buf_size = SCAN_INFO_RSVDPG_SIZE;
		scan_rpt_info->total_size = 0;
		scan_rpt_info->ack_tsf_high = 0;
		scan_rpt_info->ack_tsf_low = 0;
		scan_rpt_info->rpt_tsf_high = 0;
		scan_rpt_info->rpt_tsf_low = 0;
	} else {
		adapter->ch_sw_info.scan_mode = 0;
		if (!scan_rpt_info->buf)
			PLTFM_FREE(scan_rpt_info->buf, scan_rpt_info->buf_size);
		scan_rpt_info->buf_wptr = NULL;
		scan_rpt_info->buf_size = 0;
		scan_rpt_info->avl_buf_size = 0;
		scan_rpt_info->total_size = 0;
	}

	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);
	adapter->halmac_state.scan_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_CHANNEL_SWITCH);
	}
	PLTFM_FREE(adapter->ch_sw_info.buf, adapter->ch_sw_info.buf_size);
	adapter->ch_sw_info.buf = NULL;
	adapter->ch_sw_info.buf_wptr = NULL;
	adapter->ch_sw_info.extra_info_en = 0;
	adapter->ch_sw_info.buf_size = 0;
	adapter->ch_sw_info.avl_buf_size = 0;
	adapter->ch_sw_info.total_size = 0;
	adapter->ch_sw_info.ch_num = 0;

	if (cnv_scan_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	adapter->nlo_flag = 0;

	return status;
}

/**
 * clear_ch_info_88xx() -clear channel information
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
clear_ch_info_88xx(struct halmac_adapter *adapter)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (scan_cmd_cnstr_state_88xx(adapter) == HALMAC_CMD_CNSTR_H2C_SENT) {
		PLTFM_MSG_WARN("[WARN]state(clear)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_scan_state_88xx(adapter, HALMAC_CMD_CNSTR_BUF_CLR) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_FREE(adapter->ch_sw_info.buf, adapter->ch_sw_info.buf_size);
	adapter->ch_sw_info.buf = NULL;
	adapter->ch_sw_info.buf_wptr = NULL;
	adapter->ch_sw_info.extra_info_en = 0;
	adapter->ch_sw_info.buf_size = 0;
	adapter->ch_sw_info.avl_buf_size = 0;
	adapter->ch_sw_info.total_size = 0;
	adapter->ch_sw_info.ch_num = 0;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * chk_txdesc_88xx() -check if the tx packet format is incorrect
 * @adapter : the adapter of halmac
 * @buf : tx Packet buffer, tx desc is included
 * @size : tx packet size
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
chk_txdesc_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u32 mac_clk = 0;
	u8 value8;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (GET_TX_DESC_BMC(buf) == 1 && GET_TX_DESC_AGG_EN(buf) == 1)
		PLTFM_MSG_ERR("[ERR]txdesc - agg + bmc\n");

	if (size < (GET_TX_DESC_TXPKTSIZE(buf) +
		    adapter->hw_cfg_info.txdesc_size +
		    (GET_TX_DESC_PKT_OFFSET(buf) << 3))) {
		PLTFM_MSG_ERR("[ERR]txdesc - total size\n");
		status = HALMAC_RET_TXDESC_SET_FAIL;
	}

	if (wlhdr_valid_88xx(adapter, buf) != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]wlhdr\n");
		status = HALMAC_RET_WLHDR_FAIL;
	}

	if (GET_TX_DESC_AMSDU_PAD_EN(buf) != 0) {
		PLTFM_MSG_ERR("[ERR]txdesc - amsdu_pad\n");
		status = HALMAC_RET_TXDESC_SET_FAIL;
	}

	if (GET_TX_DESC_USE_MAX_TIME_EN(buf) == 1) {
		value8 = (u8)GET_TX_DESC_AMPDU_MAX_TIME(buf);
		if (value8 > HALMAC_REG_R8(REG_AMPDU_MAX_TIME_V1)) {
			PLTFM_MSG_ERR("[ERR]txdesc - ampdu_max_time\n");
			status = HALMAC_RET_TXDESC_SET_FAIL;
		}
	}

	switch (BIT_GET_MAC_CLK_SEL(HALMAC_REG_R32(REG_AFE_CTRL1))) {
	case 0x0:
		mac_clk = 80;
		break;
	case 0x1:
		mac_clk = 40;
		break;
	case 0x2:
		mac_clk = 20;
		break;
	case 0x3:
		mac_clk = 10;
		break;
	}

	PLTFM_MSG_ALWAYS("MAC clock : 0x%XM\n", mac_clk);
	PLTFM_MSG_ALWAYS("mac agg en : 0x%X\n", GET_TX_DESC_AGG_EN(buf));
	PLTFM_MSG_ALWAYS("mac agg num : 0x%X\n", GET_TX_DESC_MAX_AGG_NUM(buf));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return status;
}

static enum halmac_ret_status
wlhdr_valid_88xx(struct halmac_adapter *adapter, u8 *buf)
{
	u32 txdesc_size = adapter->hw_cfg_info.txdesc_size +
						GET_TX_DESC_PKT_OFFSET(buf);
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct wlhdr_frame_ctrl *wlhdr;

	wlhdr = (struct wlhdr_frame_ctrl *)(buf + txdesc_size);

	if (wlhdr->protocol != WLHDR_PROT_VER) {
		PLTFM_MSG_ERR("[ERR]prot ver!!\n");
		return HALMAC_RET_WLHDR_FAIL;
	}

	switch (wlhdr->type) {
	case WLHDR_TYPE_MGMT:
		if (wlhdr_mgmt_valid_88xx(adapter, wlhdr) != 1)
			status = HALMAC_RET_WLHDR_FAIL;
		break;
	case WLHDR_TYPE_CTRL:
		if (wlhdr_ctrl_valid_88xx(adapter, wlhdr) != 1)
			status = HALMAC_RET_WLHDR_FAIL;
		break;
	case WLHDR_TYPE_DATA:
		if (wlhdr_data_valid_88xx(adapter, wlhdr) != 1)
			status = HALMAC_RET_WLHDR_FAIL;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]undefined type!!\n");
		status = HALMAC_RET_WLHDR_FAIL;
		break;
	}

	return status;
}

static u8
wlhdr_mgmt_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr)
{
	u8 state;

	switch (wlhdr->sub_type) {
	case WLHDR_SUB_TYPE_ASSOC_REQ:
	case WLHDR_SUB_TYPE_ASSOC_RSPNS:
	case WLHDR_SUB_TYPE_REASSOC_REQ:
	case WLHDR_SUB_TYPE_REASSOC_RSPNS:
	case WLHDR_SUB_TYPE_PROBE_REQ:
	case WLHDR_SUB_TYPE_PROBE_RSPNS:
	case WLHDR_SUB_TYPE_BCN:
	case WLHDR_SUB_TYPE_DISASSOC:
	case WLHDR_SUB_TYPE_AUTH:
	case WLHDR_SUB_TYPE_DEAUTH:
	case WLHDR_SUB_TYPE_ACTION:
	case WLHDR_SUB_TYPE_ACTION_NOACK:
		state = 1;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]mgmt invalid!!\n");
		state = 0;
		break;
	}

	return state;
}

static u8
wlhdr_ctrl_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr)
{
	u8 state;

	switch (wlhdr->sub_type) {
	case WLHDR_SUB_TYPE_BF_RPT_POLL:
	case WLHDR_SUB_TYPE_NDPA:
		state = 1;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]ctrl invalid!!\n");
		state = 0;
		break;
	}

	return state;
}

static u8
wlhdr_data_valid_88xx(struct halmac_adapter *adapter,
		      struct wlhdr_frame_ctrl *wlhdr)
{
	u8 state;

	switch (wlhdr->sub_type) {
	case WLHDR_SUB_TYPE_DATA:
	case WLHDR_SUB_TYPE_NULL:
	case WLHDR_SUB_TYPE_QOS_DATA:
	case WLHDR_SUB_TYPE_QOS_NULL:
		state = 1;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]data invalid!!\n");
		state = 0;
		break;
	}

	return state;
}

/**
 * get_version_88xx() - get HALMAC version
 * @ver : return version of major, prototype and minor information
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_version_88xx(struct halmac_adapter *adapter, struct halmac_ver *ver)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	ver->major_ver = (u8)HALMAC_MAJOR_VER;
	ver->prototype_ver = (u8)HALMAC_PROTOTYPE_VER;
	ver->minor_ver = (u8)HALMAC_MINOR_VER;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
p2pps_88xx(struct halmac_adapter *adapter, struct halmac_p2pps *info)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 6)
		return HALMAC_RET_FW_NO_SUPPORT;

	status = proc_p2pps_88xx(adapter, info);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]p2pps!!\n");
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_p2pps_88xx(struct halmac_adapter *adapter, struct halmac_p2pps *info)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	P2PPS_SET_OFFLOAD_EN(h2c_buf, info->offload_en);
	P2PPS_SET_ROLE(h2c_buf, info->role);
	P2PPS_SET_CTWINDOW_EN(h2c_buf, info->ctwindow_en);
	P2PPS_SET_NOA_EN(h2c_buf, info->noa_en);
	P2PPS_SET_NOA_SEL(h2c_buf, info->noa_sel);
	P2PPS_SET_ALLSTASLEEP(h2c_buf, info->all_sta_sleep);
	P2PPS_SET_DISCOVERY(h2c_buf, info->discovery);
	P2PPS_SET_DISABLE_CLOSERF(h2c_buf, info->disable_close_rf);
	P2PPS_SET_P2P_PORT_ID(h2c_buf, info->p2p_port_id);
	P2PPS_SET_P2P_GROUP(h2c_buf, info->p2p_group);
	P2PPS_SET_P2P_MACID(h2c_buf, info->p2p_macid);

	P2PPS_SET_CTWINDOW_LENGTH(h2c_buf, info->ctwindow_length);

	P2PPS_SET_NOA_DURATION_PARA(h2c_buf, info->noa_duration_para);
	P2PPS_SET_NOA_INTERVAL_PARA(h2c_buf, info->noa_interval_para);
	P2PPS_SET_NOA_START_TIME_PARA(h2c_buf, info->noa_start_time_para);
	P2PPS_SET_NOA_COUNT_PARA(h2c_buf, info->noa_count_para);

	hdr_info.sub_cmd_id = SUB_CMD_ID_P2PPS;
	hdr_info.content_size = 24;
	hdr_info.ack = 0;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS)
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");

	return status;
}

/**
 * query_status_88xx() -query the offload feature status
 * @adapter : the adapter of halmac
 * @feature_id : feature_id
 * @proc_status : feature_status
 * @data : data buffer
 * @size : data size
 *
 * Note :
 * If user wants to know the data size, user can allocate zero
 * size buffer first. If this size less than the data size, halmac
 * will return  HALMAC_RET_BUFFER_TOO_SMALL. User need to
 * re-allocate data buffer with correct data size.
 *
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
query_status_88xx(struct halmac_adapter *adapter,
		  enum halmac_feature_id feature_id,
		  enum halmac_cmd_process_status *proc_status, u8 *data,
		  u32 *size)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (!proc_status)
		return HALMAC_RET_NULL_POINTER;

	switch (feature_id) {
	case HALMAC_FEATURE_CFG_PARA:
		status = get_cfg_param_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		status = get_dump_phy_efuse_status_88xx(adapter, proc_status,
							data, size);
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		status = get_dump_log_efuse_status_88xx(adapter, proc_status,
							data, size);
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK:
		status = get_dump_log_efuse_mask_status_88xx(adapter,
							     proc_status,
							     data, size);
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		status = get_ch_switch_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		status = get_update_packet_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_SEND_SCAN_PACKET:
		status = get_send_scan_packet_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_DROP_SCAN_PACKET:
		status = get_drop_scan_packet_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_IQK:
		status = get_iqk_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		status = get_pwr_trk_status_88xx(adapter, proc_status);
		break;
	case HALMAC_FEATURE_PSD:
		status = get_psd_status_88xx(adapter, proc_status, data, size);
		break;
	case HALMAC_FEATURE_FW_SNDING:
		status = get_fw_snding_status_88xx(adapter, proc_status);
		break;
	default:
		return HALMAC_RET_INVALID_FEATURE_ID;
	}

	return status;
}

static enum halmac_ret_status
get_cfg_param_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.cfg_param_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_ch_switch_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.scan_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_update_packet_status_88xx(struct halmac_adapter *adapter,
			      enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.update_pkt_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_send_scan_packet_status_88xx(struct halmac_adapter *adapter,
				 enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.scan_pkt_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_drop_scan_packet_status_88xx(struct halmac_adapter *adapter,
				 enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.drop_pkt_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_drv_rsvd_pg_num_88xx() -config reserved page number for driver
 * @adapter : the adapter of halmac
 * @pg_num : page number
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_drv_rsvd_pg_num_88xx(struct halmac_adapter *adapter,
			 enum halmac_drv_rsvd_pg_num pg_num)
{
	if (adapter->api_registry.cfg_drv_rsvd_pg_en == 0)
		return HALMAC_RET_NOT_SUPPORT;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]pg_num = %d\n", pg_num);

	switch (pg_num) {
	case HALMAC_RSVD_PG_NUM8:
		adapter->txff_alloc.rsvd_drv_pg_num = 8;
		break;
	case HALMAC_RSVD_PG_NUM16:
		adapter->txff_alloc.rsvd_drv_pg_num = 16;
		break;
	case HALMAC_RSVD_PG_NUM24:
		adapter->txff_alloc.rsvd_drv_pg_num = 24;
		break;
	case HALMAC_RSVD_PG_NUM32:
		adapter->txff_alloc.rsvd_drv_pg_num = 32;
		break;
	case HALMAC_RSVD_PG_NUM64:
		adapter->txff_alloc.rsvd_drv_pg_num = 64;
		break;
	case HALMAC_RSVD_PG_NUM128:
		adapter->txff_alloc.rsvd_drv_pg_num = 128;
		break;
	case HALMAC_RSVD_PG_NUM256:
		adapter->txff_alloc.rsvd_drv_pg_num = 256;
		break;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * (debug API)h2c_lb_88xx() - send h2c loopback packet
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
h2c_lb_88xx(struct halmac_adapter *adapter)
{
	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
pwr_seq_parser_88xx(struct halmac_adapter *adapter,
		    struct halmac_wlan_pwr_cfg **cmd_seq)
{
	u8 cut;
	u8 intf;
	u32 idx = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_wlan_pwr_cfg *cmd;

	switch (adapter->chip_ver) {
	case HALMAC_CHIP_VER_A_CUT:
		cut = HALMAC_PWR_CUT_A_MSK;
		break;
	case HALMAC_CHIP_VER_B_CUT:
		cut = HALMAC_PWR_CUT_B_MSK;
		break;
	case HALMAC_CHIP_VER_C_CUT:
		cut = HALMAC_PWR_CUT_C_MSK;
		break;
	case HALMAC_CHIP_VER_D_CUT:
		cut = HALMAC_PWR_CUT_D_MSK;
		break;
	case HALMAC_CHIP_VER_E_CUT:
		cut = HALMAC_PWR_CUT_E_MSK;
		break;
	case HALMAC_CHIP_VER_F_CUT:
		cut = HALMAC_PWR_CUT_F_MSK;
		break;
	case HALMAC_CHIP_VER_TEST:
		cut = HALMAC_PWR_CUT_TESTCHIP_MSK;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]cut version!!\n");
		return HALMAC_RET_SWITCH_CASE_ERROR;
	}

	switch (adapter->intf) {
	case HALMAC_INTERFACE_PCIE:
	case HALMAC_INTERFACE_AXI:
		intf = HALMAC_PWR_INTF_PCI_MSK;
		break;
	case HALMAC_INTERFACE_USB:
		intf = HALMAC_PWR_INTF_USB_MSK;
		break;
	case HALMAC_INTERFACE_SDIO:
		intf = HALMAC_PWR_INTF_SDIO_MSK;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]interface!!\n");
		return HALMAC_RET_SWITCH_CASE_ERROR;
	}

	do {
		cmd = cmd_seq[idx];

		if (!cmd)
			break;

		status = pwr_sub_seq_parser_88xx(adapter, cut, intf, cmd);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]pwr sub seq!!\n");
			return status;
		}

		idx++;
	} while (1);

	return status;
}

static enum halmac_ret_status
pwr_sub_seq_parser_88xx(struct halmac_adapter *adapter, u8 cut, u8 intf,
			struct halmac_wlan_pwr_cfg *cmd)
{
	u8 value;
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	do {
		if ((cmd->interface_msk & intf) && (cmd->cut_msk & cut)) {
			switch (cmd->cmd) {
			case HALMAC_PWR_CMD_WRITE:
				offset = cmd->offset;

				if (cmd->base == HALMAC_PWR_ADDR_SDIO)
					offset |= SDIO_LOCAL_OFFSET;

				value = HALMAC_REG_R8(offset);
				value = (u8)(value & (u8)(~(cmd->msk)));
				value = (u8)(value | (cmd->value & cmd->msk));

				HALMAC_REG_W8(offset, value);
				break;
			case HALMAC_PWR_CMD_POLLING:
				if (pwr_cmd_polling_88xx(adapter, cmd) !=
				    HALMAC_RET_SUCCESS)
					return HALMAC_RET_PWRSEQ_POLLING_FAIL;
				break;
			case HALMAC_PWR_CMD_DELAY:
				if (cmd->value == HALMAC_PWR_DELAY_US)
					PLTFM_DELAY_US(cmd->offset);
				else
					PLTFM_DELAY_US(1000 * cmd->offset);
				break;
			case HALMAC_PWR_CMD_READ:
				break;
			case HALMAC_PWR_CMD_END:
				return HALMAC_RET_SUCCESS;
			default:
				return HALMAC_RET_PWRSEQ_CMD_INCORRECT;
			}
		}
		cmd++;
	} while (1);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
pwr_cmd_polling_88xx(struct halmac_adapter *adapter,
		     struct halmac_wlan_pwr_cfg *cmd)
{
	u8 value;
	u8 flg;
	u8 poll_bit;
	u32 offset;
	u32 cnt;
	static u32 stats;
	enum halmac_interface intf;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	poll_bit = 0;
	cnt = HALMAC_PWR_POLLING_CNT;
	flg = 0;
	intf = adapter->intf;

	if (cmd->base == HALMAC_PWR_ADDR_SDIO)
		offset = cmd->offset | SDIO_LOCAL_OFFSET;
	else
		offset = cmd->offset;

	do {
		cnt--;
		value = HALMAC_REG_R8(offset);
		value = (u8)(value & cmd->msk);

		if (value == (cmd->value & cmd->msk)) {
			poll_bit = 1;
		} else {
			if (cnt == 0) {
				if (intf == HALMAC_INTERFACE_PCIE && flg == 0) {
					/* PCIE + USB package */
					/* power bit polling timeout issue */
					stats++;
					PLTFM_MSG_WARN("[WARN]PCIE stats:%d\n",
						       stats);
					value = HALMAC_REG_R8(REG_SYS_PW_CTRL);
					value |= BIT(3);
					HALMAC_REG_W8(REG_SYS_PW_CTRL, value);
					value &= ~BIT(3);
					HALMAC_REG_W8(REG_SYS_PW_CTRL, value);
					poll_bit = 0;
					cnt = HALMAC_PWR_POLLING_CNT;
					flg = 1;
				} else {
					PLTFM_MSG_ERR("[ERR]polling to!!\n");
					PLTFM_MSG_ERR("[ERR]cmd offset:%X\n",
						      cmd->offset);
					PLTFM_MSG_ERR("[ERR]cmd value:%X\n",
						      cmd->value);
					PLTFM_MSG_ERR("[ERR]cmd msk:%X\n",
						      cmd->msk);
					PLTFM_MSG_ERR("[ERR]offset = %X\n",
						      offset);
					PLTFM_MSG_ERR("[ERR]value = %X\n",
						      value);
					return HALMAC_RET_PWRSEQ_POLLING_FAIL;
				}
			} else {
				PLTFM_DELAY_US(50);
			}
		}
	} while (!poll_bit);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
parse_intf_phy_88xx(struct halmac_adapter *adapter,
		    struct halmac_intf_phy_para *param,
		    enum halmac_intf_phy_platform pltfm,
		    enum hal_intf_phy intf_phy)
{
	u16 value;
	u16 cur_cut;
	u16 offset;
	u16 ip_sel;
	struct halmac_intf_phy_para *cur_param;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 result = HALMAC_RET_SUCCESS;

	switch (adapter->chip_ver) {
	case HALMAC_CHIP_VER_A_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_A;
		break;
	case HALMAC_CHIP_VER_B_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_B;
		break;
	case HALMAC_CHIP_VER_C_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_C;
		break;
	case HALMAC_CHIP_VER_D_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_D;
		break;
	case HALMAC_CHIP_VER_E_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_E;
		break;
	case HALMAC_CHIP_VER_F_CUT:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_F;
		break;
	case HALMAC_CHIP_VER_TEST:
		cur_cut = (u16)HALMAC_INTF_PHY_CUT_TESTCHIP;
		break;
	default:
		return HALMAC_RET_FAIL;
	}

	cur_param = param;

	do {
		if ((cur_param->cut & cur_cut) &&
		    (cur_param->plaform & (u16)pltfm)) {
			offset =  cur_param->offset;
			value = cur_param->value;
			ip_sel = cur_param->ip_sel;

			if (offset == 0xFFFF)
				break;

			if (ip_sel == HALMAC_IP_SEL_MAC) {
				HALMAC_REG_W8((u32)offset, (u8)value);
			} else if (intf_phy == HAL_INTF_PHY_USB2 ||
				   intf_phy == HAL_INTF_PHY_USB3) {
#if HALMAC_USB_SUPPORT
				result = usbphy_write_88xx(adapter, (u8)offset,
							   value, intf_phy);
				if (result != HALMAC_RET_SUCCESS)
					PLTFM_MSG_ERR("[ERR]usb phy!!\n");
#endif
			} else if (intf_phy == HAL_INTF_PHY_PCIE_GEN1 ||
				   intf_phy == HAL_INTF_PHY_PCIE_GEN2) {
#if HALMAC_PCIE_SUPPORT
				if (ip_sel == HALMAC_IP_INTF_PHY)
					result = mdio_write_88xx(adapter,
								 (u8)offset,
								 value,
								 intf_phy);
				else
					result = dbi_w8_88xx(adapter, offset,
							     (u8)value);
				if (result != HALMAC_RET_SUCCESS)
					PLTFM_MSG_ERR("[ERR]mdio/dbi!!\n");
#endif
			} else {
				PLTFM_MSG_ERR("[ERR]intf phy sel!!\n");
			}
		}
		cur_param++;
	} while (1);

	return HALMAC_RET_SUCCESS;
}

/**
 * txfifo_is_empty_88xx() -check if txfifo is empty
 * @adapter : the adapter of halmac
 * @chk_num : check number
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
txfifo_is_empty_88xx(struct halmac_adapter *adapter, u32 chk_num)
{
	u32 cnt;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	cnt = (chk_num <= 10) ? 10 : chk_num;
	do {
		if (HALMAC_REG_R8(REG_TXPKT_EMPTY) != 0xFF)
			return HALMAC_RET_TXFIFO_NO_EMPTY;

		if ((HALMAC_REG_R8(REG_TXPKT_EMPTY + 1) & 0x06) != 0x06)
			return HALMAC_RET_TXFIFO_NO_EMPTY;
		cnt--;

	} while (cnt != 0);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * (internal use)
 * smart_malloc_88xx() - adapt malloc size
 * @adapter : the adapter of halmac
 * @size : expected malloc size
 * @pNew_size : real malloc size
 * Author : Ivan Lin
 * Return : address pointer
 */
u8*
smart_malloc_88xx(struct halmac_adapter *adapter, u32 size, u32 *new_size)
{
	u8 retry_num;
	u8 *malloc_buf = NULL;

	for (retry_num = 0; retry_num < 5; retry_num++) {
		malloc_buf = (u8 *)PLTFM_MALLOC(size);

		if (malloc_buf) {
			*new_size = size;
			return malloc_buf;
		}

		size = size >> 1;

		if (size == 0)
			break;
	}

	PLTFM_MSG_ERR("[ERR]adptive malloc!!\n");

	return NULL;
}

/**
 * (internal use)
 * ltecoex_reg_read_88xx() - read ltecoex register
 * @adapter : the adapter of halmac
 * @offset : offset
 * @pValue : value
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
ltecoex_reg_read_88xx(struct halmac_adapter *adapter, u16 offset, u32 *value)
{
	u32 cnt;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	cnt = 10000;
	while ((HALMAC_REG_R8(LTECOEX_ACCESS_CTRL + 3) & BIT(5)) == 0) {
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]lte ready(R)\n");
			return HALMAC_RET_LTECOEX_READY_FAIL;
		}
		cnt--;
		PLTFM_DELAY_US(50);
	}

	HALMAC_REG_W32(LTECOEX_ACCESS_CTRL, 0x800F0000 | offset);
	*value = HALMAC_REG_R32(REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1);

	return HALMAC_RET_SUCCESS;
}

/**
 * (internal use)
 * ltecoex_reg_write_88xx() - write ltecoex register
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : value
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
ltecoex_reg_write_88xx(struct halmac_adapter *adapter, u16 offset, u32 value)
{
	u32 cnt;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	cnt = 10000;
	while ((HALMAC_REG_R8(LTECOEX_ACCESS_CTRL + 3) & BIT(5)) == 0) {
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]lte ready(W)\n");
			return HALMAC_RET_LTECOEX_READY_FAIL;
		}
		cnt--;
		PLTFM_DELAY_US(50);
	}

	HALMAC_REG_W32(REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1, value);
	HALMAC_REG_W32(LTECOEX_ACCESS_CTRL, 0xC00F0000 | offset);

	return HALMAC_RET_SUCCESS;
}

static void
pwr_state_88xx(struct halmac_adapter *adapter, enum halmac_mac_power *state)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if ((HALMAC_REG_R8(REG_SYS_FUNC_EN + 1) & BIT(3)) == 0)
		*state = HALMAC_MAC_POWER_OFF;
	else
		*state = HALMAC_MAC_POWER_ON;
}

static u8
packet_in_nlo_88xx(struct halmac_adapter *adapter,
		   enum halmac_packet_id pkt_id)
{
	enum halmac_packet_id nlo_pkt = HALMAC_PACKET_PROBE_REQ_NLO;

	if (pkt_id >= nlo_pkt)
		return 1;
	else
		return 0;
}

static enum halmac_packet_id
get_real_pkt_id_88xx(struct halmac_adapter *adapter,
		     enum halmac_packet_id pkt_id)
{
	enum halmac_packet_id real_pkt_id;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (pkt_id) {
	case HALMAC_PACKET_PROBE_REQ_NLO:
		real_pkt_id = HALMAC_PACKET_PROBE_REQ;
		break;
	case HALMAC_PACKET_SYNC_BCN_NLO:
		real_pkt_id = HALMAC_PACKET_SYNC_BCN;
		break;
	case HALMAC_PACKET_DISCOVERY_BCN_NLO:
		real_pkt_id = HALMAC_PACKET_DISCOVERY_BCN;
		break;
	default:
		real_pkt_id = pkt_id;
	}
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);
	return real_pkt_id;
}

static u32
get_update_packet_page_size(struct halmac_adapter *adapter, u32 size)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 txdesc_size;
	u32 total;

	api->halmac_get_hw_value(adapter, HALMAC_HW_TX_DESC_SIZE, &txdesc_size);

	total = size + txdesc_size;
	return (total & 0x7f) > 0 ?
		(total >> TX_PAGE_SIZE_SHIFT_88XX) + 1 :
		total >> TX_PAGE_SIZE_SHIFT_88XX;
}

#endif /* HALMAC_88XX_SUPPORT */
