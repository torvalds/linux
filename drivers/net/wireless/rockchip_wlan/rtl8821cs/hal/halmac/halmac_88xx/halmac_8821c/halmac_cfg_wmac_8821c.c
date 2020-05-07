/******************************************************************************
 *
 * Copyright(c) 2017 - 2019 Realtek Corporation. All rights reserved.
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

#include "halmac_cfg_wmac_8821c.h"
#include "halmac_8821c_cfg.h"

#if HALMAC_8821C_SUPPORT

/**
 * cfg_drv_info_8821c() - config driver info
 * @adapter : the adapter of halmac
 * @drv_info : driver information selection
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_drv_info_8821c(struct halmac_adapter *adapter,
		   enum halmac_drv_info drv_info)
{
	u8 drv_info_size = 0;
	u8 phy_status_en = 0;
	u8 sniffer_en = 0;
	u8 plcp_hdr_en = 0;
	u8 value8;
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]drv info = %d\n", drv_info);

	switch (drv_info) {
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
		return HALMAC_RET_SW_CASE_NOT_SUPPORT;
	}

	if (adapter->txff_alloc.rx_fifo_exp_mode !=
	    HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE)
		drv_info_size = RX_DESC_DUMMY_SIZE_8821C >> 3;

	HALMAC_REG_W8(REG_RX_DRVINFO_SZ, drv_info_size);

	value8 = HALMAC_REG_R8(REG_TRXFF_BNDY + 1);
	value8 &= 0xF0;
	/* For rxdesc len = 0 issue */
	value8 |= 0xF;
	HALMAC_REG_W8(REG_TRXFF_BNDY + 1, value8);

	adapter->drv_info_size = drv_info_size;

	value32 = HALMAC_REG_R32(REG_RCR);
	value32 = (value32 & (~BIT_APP_PHYSTS));
	if (phy_status_en == 1)
		value32 = value32 | BIT_APP_PHYSTS;
	HALMAC_REG_W32(REG_RCR, value32);

	value32 = HALMAC_REG_R32(REG_WMAC_OPTION_FUNCTION + 4);
	value32 = (value32 & (~(BIT(8) | BIT(9))));
	if (sniffer_en == 1)
		value32 = value32 | BIT(9);
	if (plcp_hdr_en == 1)
		value32 = value32 | BIT(8);
	HALMAC_REG_W32(REG_WMAC_OPTION_FUNCTION + 4, value32);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_low_pwr_8821c() - config WMAC register
 * @adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_low_pwr_8821c(struct halmac_adapter *adapter)
{
	return HALMAC_RET_SUCCESS;
}

void
cfg_rx_ignore_8821c(struct halmac_adapter *adapter,
		    struct halmac_mac_rx_ignore_cfg *cfg)
{
}

enum halmac_ret_status
cfg_ampdu_8821c(struct halmac_adapter *adapter,
		struct halmac_ampdu_config *cfg)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (cfg->ht_max_len != cfg->vht_max_len) {
		PLTFM_MSG_ERR("[ERR]max len ht != vht!!\n");
		return HALMAC_RET_PARA_NOT_SUPPORT;
	}

	HALMAC_REG_W8(REG_PROT_MODE_CTRL + 2, cfg->max_agg_num);
	HALMAC_REG_W8(REG_PROT_MODE_CTRL + 3, cfg->max_agg_num);

	if (cfg->max_len_en == 1)
		HALMAC_REG_W32(REG_AMPDU_MAX_LENGTH, cfg->ht_max_len);

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_8821C_SUPPORT */
