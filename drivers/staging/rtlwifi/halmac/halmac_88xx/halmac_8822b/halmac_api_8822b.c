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
#include "halmac_8822b_cfg.h"
#include "halmac_func_8822b.h"
#include "../halmac_func_88xx.h"

/**
 * halmac_mount_api_8822b() - attach functions to function pointer
 * @halmac_adapter
 *
 * SD1 internal use
 *
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
halmac_mount_api_8822b(struct halmac_adapter *halmac_adapter)
{
	struct halmac_api *halmac_api =
		(struct halmac_api *)halmac_adapter->halmac_api;

	halmac_adapter->chip_id = HALMAC_CHIP_ID_8822B;
	halmac_adapter->hw_config_info.efuse_size = HALMAC_EFUSE_SIZE_8822B;
	halmac_adapter->hw_config_info.eeprom_size = HALMAC_EEPROM_SIZE_8822B;
	halmac_adapter->hw_config_info.bt_efuse_size =
		HALMAC_BT_EFUSE_SIZE_8822B;
	halmac_adapter->hw_config_info.cam_entry_num =
		HALMAC_SECURITY_CAM_ENTRY_NUM_8822B;
	halmac_adapter->hw_config_info.txdesc_size = HALMAC_TX_DESC_SIZE_8822B;
	halmac_adapter->hw_config_info.rxdesc_size = HALMAC_RX_DESC_SIZE_8822B;
	halmac_adapter->hw_config_info.tx_fifo_size = HALMAC_TX_FIFO_SIZE_8822B;
	halmac_adapter->hw_config_info.rx_fifo_size = HALMAC_RX_FIFO_SIZE_8822B;
	halmac_adapter->hw_config_info.page_size = HALMAC_TX_PAGE_SIZE_8822B;
	halmac_adapter->hw_config_info.tx_align_size =
		HALMAC_TX_ALIGN_SIZE_8822B;
	halmac_adapter->hw_config_info.page_size_2_power =
		HALMAC_TX_PAGE_SIZE_2_POWER_8822B;

	halmac_adapter->txff_allocation.rsvd_drv_pg_num =
		HALMAC_RSVD_DRV_PGNUM_8822B;

	halmac_api->halmac_init_trx_cfg = halmac_init_trx_cfg_8822b;
	halmac_api->halmac_init_protocol_cfg = halmac_init_protocol_cfg_8822b;
	halmac_api->halmac_init_h2c = halmac_init_h2c_8822b;

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		halmac_api->halmac_tx_allowed_sdio =
			halmac_tx_allowed_sdio_88xx;
		halmac_api->halmac_cfg_tx_agg_align =
			halmac_cfg_tx_agg_align_sdio_not_support_88xx;
		halmac_api->halmac_mac_power_switch =
			halmac_mac_power_switch_8822b_sdio;
		halmac_api->halmac_phy_cfg = halmac_phy_cfg_8822b_sdio;
		halmac_api->halmac_interface_integration_tuning =
			halmac_interface_integration_tuning_8822b_sdio;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		halmac_api->halmac_mac_power_switch =
			halmac_mac_power_switch_8822b_usb;
		halmac_api->halmac_cfg_tx_agg_align =
			halmac_cfg_tx_agg_align_usb_not_support_88xx;
		halmac_api->halmac_phy_cfg = halmac_phy_cfg_8822b_usb;
		halmac_api->halmac_interface_integration_tuning =
			halmac_interface_integration_tuning_8822b_usb;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_PCIE) {
		halmac_api->halmac_mac_power_switch =
			halmac_mac_power_switch_8822b_pcie;
		halmac_api->halmac_cfg_tx_agg_align =
			halmac_cfg_tx_agg_align_pcie_not_support_88xx;
		halmac_api->halmac_pcie_switch = halmac_pcie_switch_8822b;
		halmac_api->halmac_phy_cfg = halmac_phy_cfg_8822b_pcie;
		halmac_api->halmac_interface_integration_tuning =
			halmac_interface_integration_tuning_8822b_pcie;
	} else {
		halmac_api->halmac_pcie_switch = halmac_pcie_switch_8822b_nc;
	}
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_trx_cfg_8822b() - config trx dma register
 * @halmac_adapter : the adapter of halmac
 * @halmac_trx_mode : trx mode selection
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_trx_cfg_8822b(struct halmac_adapter *halmac_adapter,
			  enum halmac_trx_mode halmac_trx_mode)
{
	u8 value8;
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_TRX_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;
	halmac_adapter->trx_mode = halmac_trx_mode;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_init_trx_cfg ==========>halmac_trx_mode = %d\n",
			halmac_trx_mode);

	status = halmac_txdma_queue_mapping_8822b(halmac_adapter,
						  halmac_trx_mode);

	if (status != HALMAC_RET_SUCCESS) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_txdma_queue_mapping fail!\n");
		return status;
	}

	value8 = 0;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR, value8);
	value8 = HALMAC_CR_TRX_ENABLE_8822B;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR, value8);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2CQ_CSR, BIT(31));

	status = halmac_priority_queue_config_8822b(halmac_adapter,
						    halmac_trx_mode);
	if (halmac_adapter->txff_allocation.rx_fifo_expanding_mode !=
	    HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE)
		HALMAC_REG_WRITE_8(halmac_adapter, REG_RX_DRVINFO_SZ, 0xF);

	if (status != HALMAC_RET_SUCCESS) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"halmac_txdma_queue_mapping fail!\n");
		return status;
	}

	/* Config H2C packet buffer */
	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_HEAD);
	value32 = (value32 & 0xFFFC0000) |
		  (halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		   << HALMAC_TX_PAGE_SIZE_2_POWER_8822B);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_HEAD, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_READ_ADDR);
	value32 = (value32 & 0xFFFC0000) |
		  (halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		   << HALMAC_TX_PAGE_SIZE_2_POWER_8822B);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_READ_ADDR, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_TAIL);
	value32 = (value32 & 0xFFFC0000) |
		  ((halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		    << HALMAC_TX_PAGE_SIZE_2_POWER_8822B) +
		   (HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B
		    << HALMAC_TX_PAGE_SIZE_2_POWER_8822B));
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_TAIL, value32);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFC) | 0x01);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_H2C_INFO, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFB) | 0x04);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_H2C_INFO, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_TXDMA_OFFSET_CHK + 1);
	value8 = (u8)((value8 & 0x7f) | 0x80);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_OFFSET_CHK + 1, value8);

	halmac_adapter->h2c_buff_size = HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B
					<< HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
	halmac_get_h2c_buff_free_space_88xx(halmac_adapter);

	if (halmac_adapter->h2c_buff_size !=
	    halmac_adapter->h2c_buf_free_space) {
		pr_err("get h2c free space error!\n");
		return HALMAC_RET_GET_H2C_SPACE_ERR;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"halmac_init_trx_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_protocol_cfg_8822b() - config protocol register
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_protocol_cfg_8822b(struct halmac_adapter *halmac_adapter)
{
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_PROTOCOL_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s ==========>\n", __func__);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_AMPDU_MAX_TIME_V1,
			   HALMAC_AMPDU_MAX_TIME_8822B);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);

	value32 = HALMAC_PROT_RTS_LEN_TH_8822B |
		  (HALMAC_PROT_RTS_TX_TIME_TH_8822B << 8) |
		  (HALMAC_PROT_MAX_AGG_PKT_LIMIT_8822B << 16) |
		  (HALMAC_PROT_RTS_MAX_AGG_PKT_LIMIT_8822B << 24);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_PROT_MODE_CTRL, value32);

	HALMAC_REG_WRITE_16(halmac_adapter, REG_BAR_MODE_CTRL + 2,
			    HALMAC_BAR_RETRY_LIMIT_8822B |
				    HALMAC_RA_TRY_RATE_AGG_LIMIT_8822B << 8);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_FAST_EDCA_VOVI_SETTING,
			   HALMAC_FAST_EDCA_VO_TH_8822B);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FAST_EDCA_VOVI_SETTING + 2,
			   HALMAC_FAST_EDCA_VI_TH_8822B);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FAST_EDCA_BEBK_SETTING,
			   HALMAC_FAST_EDCA_BE_TH_8822B);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FAST_EDCA_BEBK_SETTING + 2,
			   HALMAC_FAST_EDCA_BK_TH_8822B);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"[TRACE]%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_init_h2c_8822b() - config h2c packet buffer
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_h2c_8822b(struct halmac_adapter *halmac_adapter)
{
	u8 value8;
	u32 value32;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	value8 = 0;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR, value8);
	value8 = HALMAC_CR_TRX_ENABLE_8822B;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR, value8);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_HEAD);
	value32 = (value32 & 0xFFFC0000) |
		  (halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		   << HALMAC_TX_PAGE_SIZE_2_POWER_8822B);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_HEAD, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_READ_ADDR);
	value32 = (value32 & 0xFFFC0000) |
		  (halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		   << HALMAC_TX_PAGE_SIZE_2_POWER_8822B);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_READ_ADDR, value32);

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_TAIL);
	value32 = (value32 & 0xFFFC0000) |
		  ((halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy
		    << HALMAC_TX_PAGE_SIZE_2_POWER_8822B) +
		   (HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B
		    << HALMAC_TX_PAGE_SIZE_2_POWER_8822B));
	HALMAC_REG_WRITE_32(halmac_adapter, REG_H2C_TAIL, value32);
	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFC) | 0x01);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_H2C_INFO, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFB) | 0x04);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_H2C_INFO, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_TXDMA_OFFSET_CHK + 1);
	value8 = (u8)((value8 & 0x7f) | 0x80);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_OFFSET_CHK + 1, value8);

	halmac_adapter->h2c_buff_size = HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B
					<< HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
	halmac_get_h2c_buff_free_space_88xx(halmac_adapter);

	if (halmac_adapter->h2c_buff_size !=
	    halmac_adapter->h2c_buf_free_space) {
		pr_err("get h2c free space error!\n");
		return HALMAC_RET_GET_H2C_SPACE_ERR;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"h2c free space : %d\n",
			halmac_adapter->h2c_buf_free_space);

	return HALMAC_RET_SUCCESS;
}
