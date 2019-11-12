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

#include "halmac_cfg_wmac_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_efuse_88xx.h"

#if HALMAC_88XX_SUPPORT

#define MAC_CLK_SPEED	80 /* 80M */
#define EFUSE_PCB_INFO_OFFSET	0xCA

enum mac_clock_hw_def {
	MAC_CLK_HW_DEF_80M = 0,
	MAC_CLK_HW_DEF_40M = 1,
	MAC_CLK_HW_DEF_20M = 2,
};

static enum halmac_ret_status
board_rf_fine_tune_88xx(struct halmac_adapter *adapter);

/**
 * cfg_mac_addr_88xx() - config mac address
 * @adapter : the adapter of halmac
 * @port : 0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @addr : mac address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_mac_addr_88xx(struct halmac_adapter *adapter, u8 port,
		  union halmac_wlan_addr *addr)
{
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (port >= HALMAC_PORTID_NUM) {
		PLTFM_MSG_ERR("[ERR]port index >= 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	switch (port) {
	case HALMAC_PORTID0:
		offset = REG_MACID;
		break;
	case HALMAC_PORTID1:
		offset = REG_MACID1;
		break;
	case HALMAC_PORTID2:
		offset = REG_MACID2;
		break;
	case HALMAC_PORTID3:
		offset = REG_MACID3;
		break;
	case HALMAC_PORTID4:
		offset = REG_MACID4;
		break;
	default:
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	HALMAC_REG_W32(offset, rtk_le32_to_cpu(addr->addr_l_h.low));
	HALMAC_REG_W16(offset + 4, rtk_le16_to_cpu(addr->addr_l_h.high));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_bssid_88xx() - config BSSID
 * @adapter : the adapter of halmac
 * @port : 0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @addr : bssid
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_bssid_88xx(struct halmac_adapter *adapter, u8 port,
	       union halmac_wlan_addr *addr)
{
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (port >= HALMAC_PORTID_NUM) {
		PLTFM_MSG_ERR("[ERR]port index > 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	switch (port) {
	case HALMAC_PORTID0:
		offset = REG_BSSID;
		break;
	case HALMAC_PORTID1:
		offset = REG_BSSID1;
		break;
	case HALMAC_PORTID2:
		offset = REG_BSSID2;
		break;
	case HALMAC_PORTID3:
		offset = REG_BSSID3;
		break;
	case HALMAC_PORTID4:
		offset = REG_BSSID4;
		break;
	default:
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	HALMAC_REG_W32(offset, rtk_le32_to_cpu(addr->addr_l_h.low));
	HALMAC_REG_W16(offset + 4, rtk_le16_to_cpu(addr->addr_l_h.high));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_transmitter_addr_88xx() - config transmitter address
 * @adapter : the adapter of halmac
 * @port :  0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @addr :
 * Author : Alan
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
cfg_transmitter_addr_88xx(struct halmac_adapter *adapter, u8 port,
			  union halmac_wlan_addr *addr)
{
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (port >= HALMAC_PORTID_NUM) {
		PLTFM_MSG_ERR("[ERR]port index > 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	switch (port) {
	case HALMAC_PORTID0:
		offset = REG_TRANSMIT_ADDRSS_0;
		break;
	case HALMAC_PORTID1:
		offset = REG_TRANSMIT_ADDRSS_1;
		break;
	case HALMAC_PORTID2:
		offset = REG_TRANSMIT_ADDRSS_2;
		break;
	case HALMAC_PORTID3:
		offset = REG_TRANSMIT_ADDRSS_3;
		break;
	case HALMAC_PORTID4:
		offset = REG_TRANSMIT_ADDRSS_4;
		break;
	default:
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	HALMAC_REG_W32(offset, rtk_le32_to_cpu(addr->addr_l_h.low));
	HALMAC_REG_W16(offset + 4, rtk_le16_to_cpu(addr->addr_l_h.high));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_net_type_88xx() - config network type
 * @adapter : the adapter of halmac
 * @port :  0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @addr : mac address
 * Author : Alan
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
cfg_net_type_88xx(struct halmac_adapter *adapter, u8 port,
		  enum halmac_network_type_select net_type)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 value8 = 0;
	u8 net_type_tmp = 0;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (net_type == HALMAC_NETWORK_AP) {
		if (port >= HALMAC_PORTID1) {
			PLTFM_MSG_ERR("[ERR]AP port > 1\n");
			return HALMAC_RET_PORT_NOT_SUPPORT;
		}
	}

	switch (port) {
	case HALMAC_PORTID0:
		net_type_tmp = net_type;
		value8 = ((HALMAC_REG_R8(REG_CR + 2) & 0xFC) | net_type_tmp);
		HALMAC_REG_W8(REG_CR + 2, value8);
		break;
	case HALMAC_PORTID1:
		net_type_tmp = (net_type << 2);
		value8 = ((HALMAC_REG_R8(REG_CR + 2) & 0xF3) | net_type_tmp);
		HALMAC_REG_W8(REG_CR + 2, value8);
		break;
	case HALMAC_PORTID2:
		net_type_tmp = net_type;
		value8 = ((HALMAC_REG_R8(REG_CR_EXT) & 0xFC) | net_type_tmp);
		HALMAC_REG_W8(REG_CR_EXT, value8);
		break;
	case HALMAC_PORTID3:
		net_type_tmp = (net_type << 2);
		value8 = ((HALMAC_REG_R8(REG_CR_EXT) & 0xF3) | net_type_tmp);
		HALMAC_REG_W8(REG_CR_EXT, value8);
		break;
	case HALMAC_PORTID4:
		net_type_tmp = (net_type << 4);
		value8 = ((HALMAC_REG_R8(REG_CR_EXT) & 0xCF) | net_type_tmp);
		HALMAC_REG_W8(REG_CR_EXT, value8);
		break;
	default:
		break;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_tsf_rst_88xx() - tsf reset
 * @adapter : the adapter of halmac
 * @port :  0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * Author : Alan
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
cfg_tsf_rst_88xx(struct halmac_adapter *adapter, u8 port)
{
	u8 tsf_rst = 0;
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (port) {
	case HALMAC_PORTID0:
		tsf_rst = BIT_TSFTR_RST;
		break;
	case HALMAC_PORTID1:
		tsf_rst = BIT_TSFTR_CLI0_RST;
		break;
	case HALMAC_PORTID2:
		tsf_rst = BIT_TSFTR_CLI1_RST;
		break;
	case HALMAC_PORTID3:
		tsf_rst = BIT_TSFTR_CLI2_RST;
		break;
	case HALMAC_PORTID4:
		tsf_rst = BIT_TSFTR_CLI3_RST;
		break;
	default:
		break;
	}

	value8 = HALMAC_REG_R8(REG_DUAL_TSF_RST);
	HALMAC_REG_W8(REG_DUAL_TSF_RST, value8 | tsf_rst);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_bcn_space_88xx() - config beacon space
 * @adapter : the adapter of halmac
 * @port :  0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @bcn_space : beacon space
 * Author : Alan
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
cfg_bcn_space_88xx(struct halmac_adapter *adapter, u8 port, u32 bcn_space)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u16 bcn_space_real = 0;
	u16 value16 = 0;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	bcn_space_real = ((u16)bcn_space);

	switch (port) {
	case HALMAC_PORTID0:
		HALMAC_REG_W16(REG_MBSSID_BCN_SPACE, bcn_space_real);
		break;
	case HALMAC_PORTID1:
		value16 = HALMAC_REG_R16(REG_MBSSID_BCN_SPACE + 2) & 0xF000;
		value16 |= bcn_space_real;
		HALMAC_REG_W16(REG_MBSSID_BCN_SPACE + 2, value16);
		break;
	case HALMAC_PORTID2:
		value16 = HALMAC_REG_R16(REG_MBSSID_BCN_SPACE2) & 0xF000;
		value16 |= bcn_space_real;
		HALMAC_REG_W16(REG_MBSSID_BCN_SPACE2, value16);
		break;
	case HALMAC_PORTID3:
		value16 = HALMAC_REG_R16(REG_MBSSID_BCN_SPACE2 + 2) & 0xF000;
		value16 |= bcn_space_real;
		HALMAC_REG_W16(REG_MBSSID_BCN_SPACE2 + 2, value16);
		break;
	case HALMAC_PORTID4:
		value16 = HALMAC_REG_R16(REG_MBSSID_BCN_SPACE3) & 0xF000;
		value16 |= bcn_space_real;
		HALMAC_REG_W16(REG_MBSSID_BCN_SPACE3, value16);
		break;
	default:
		break;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * rw_bcn_ctrl_88xx() - r/w beacon control
 * @adapter : the adapter of halmac
 * @port :  0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @write_en : 1->write beacon function 0->read beacon function
 * @pBcn_ctrl : beacon control info
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 */
enum halmac_ret_status
rw_bcn_ctrl_88xx(struct halmac_adapter *adapter, u8 port, u8 write_en,
		 struct halmac_bcn_ctrl *ctrl)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 ctrl_value = 0;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (write_en) {
		if (ctrl->dis_rx_bssid_fit == 1)
			ctrl_value |= BIT_DIS_RX_BSSID_FIT;

		if (ctrl->en_txbcn_rpt == 1)
			ctrl_value |= BIT_P0_EN_TXBCN_RPT;

		if (ctrl->dis_tsf_udt == 1)
			ctrl_value |= BIT_DIS_TSF_UDT;

		if (ctrl->en_bcn == 1)
			ctrl_value |= BIT_EN_BCN_FUNCTION;

		if (ctrl->en_rxbcn_rpt == 1)
			ctrl_value |= BIT_P0_EN_RXBCN_RPT;

		if (ctrl->en_p2p_ctwin == 1)
			ctrl_value |= BIT_EN_P2P_CTWINDOW;

		if (ctrl->en_p2p_bcn_area == 1)
			ctrl_value |= BIT_EN_P2P_BCNQ_AREA;

		switch (port) {
		case HALMAC_PORTID0:
			HALMAC_REG_W8(REG_BCN_CTRL, ctrl_value);
			break;
		case HALMAC_PORTID1:
			HALMAC_REG_W8(REG_BCN_CTRL_CLINT0, ctrl_value);
			break;
		case HALMAC_PORTID2:
			HALMAC_REG_W8(REG_BCN_CTRL_CLINT1, ctrl_value);
			break;
		case HALMAC_PORTID3:
			HALMAC_REG_W8(REG_BCN_CTRL_CLINT2, ctrl_value);
			break;
		case HALMAC_PORTID4:
			HALMAC_REG_W8(REG_BCN_CTRL_CLINT3, ctrl_value);
			break;
		default:
			break;
		}

	} else {
		switch (port) {
		case HALMAC_PORTID0:
			ctrl_value = HALMAC_REG_R8(REG_BCN_CTRL);
			break;
		case HALMAC_PORTID1:
			ctrl_value = HALMAC_REG_R8(REG_BCN_CTRL_CLINT0);
			break;
		case HALMAC_PORTID2:
			ctrl_value = HALMAC_REG_R8(REG_BCN_CTRL_CLINT1);
			break;
		case HALMAC_PORTID3:
			ctrl_value = HALMAC_REG_R8(REG_BCN_CTRL_CLINT2);
			break;
		case HALMAC_PORTID4:
			ctrl_value = HALMAC_REG_R8(REG_BCN_CTRL_CLINT3);
			break;
		default:
			break;
		}

		if (ctrl_value & BIT_EN_P2P_BCNQ_AREA)
			ctrl->en_p2p_bcn_area = 1;
		else
			ctrl->en_p2p_bcn_area = 0;

		if (ctrl_value & BIT_EN_P2P_CTWINDOW)
			ctrl->en_p2p_ctwin = 1;
		else
			ctrl->en_p2p_ctwin = 0;

		if (ctrl_value & BIT_P0_EN_RXBCN_RPT)
			ctrl->en_rxbcn_rpt = 1;
		else
			ctrl->en_rxbcn_rpt = 0;

		if (ctrl_value & BIT_EN_BCN_FUNCTION)
			ctrl->en_bcn = 1;
		else
			ctrl->en_bcn = 0;

		if (ctrl_value & BIT_DIS_TSF_UDT)
			ctrl->dis_tsf_udt = 1;
		else
			ctrl->dis_tsf_udt = 0;

		if (ctrl_value & BIT_P0_EN_TXBCN_RPT)
			ctrl->en_txbcn_rpt = 1;
		else
			ctrl->en_txbcn_rpt = 0;

		if (ctrl_value & BIT_DIS_RX_BSSID_FIT)
			ctrl->dis_rx_bssid_fit = 1;
		else
			ctrl->dis_rx_bssid_fit = 0;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_multicast_addr_88xx() - config multicast address
 * @adapter : the adapter of halmac
 * @addr : multicast address
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_multicast_addr_88xx(struct halmac_adapter *adapter,
			union halmac_wlan_addr *addr)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_W32(REG_MAR, rtk_le32_to_cpu(addr->addr_l_h.low));
	HALMAC_REG_W16(REG_MAR + 4, rtk_le16_to_cpu(addr->addr_l_h.high));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_operation_mode_88xx() - config operation mode
 * @adapter : the adapter of halmac
 * @mode : 802.11 standard(b/g/n/ac)
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_operation_mode_88xx(struct halmac_adapter *adapter,
			enum halmac_wireless_mode mode)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_ch_bw_88xx() - config channel & bandwidth
 * @adapter : the adapter of halmac
 * @ch : WLAN channel, support 2.4G & 5G
 * @idx : primary channel index, idx1, idx2, idx3, idx4
 * @bw : band width, 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_ch_bw_88xx(struct halmac_adapter *adapter, u8 ch,
	       enum halmac_pri_ch_idx idx, enum halmac_bw bw)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	cfg_pri_ch_idx_88xx(adapter, idx);
	cfg_bw_88xx(adapter, bw);
	cfg_ch_88xx(adapter, ch);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
cfg_ch_88xx(struct halmac_adapter *adapter, u8 ch)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value8 = HALMAC_REG_R8(REG_CCK_CHECK);
	value8 = value8 & (~(BIT(7)));

	if (ch > 35)
		value8 = value8 | BIT(7);

	HALMAC_REG_W8(REG_CCK_CHECK, value8);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
cfg_pri_ch_idx_88xx(struct halmac_adapter *adapter, enum halmac_pri_ch_idx idx)
{
	u8 txsc40 = 0, txsc20 = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	txsc20 = idx;
	if (txsc20 == HALMAC_CH_IDX_1 || txsc20 == HALMAC_CH_IDX_3)
		txsc40 = 9;
	else
		txsc40 = 10;

	HALMAC_REG_W8(REG_DATA_SC, BIT_TXSC_20M(txsc20) | BIT_TXSC_40M(txsc40));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_bw_88xx() - config bandwidth
 * @adapter : the adapter of halmac
 * @bw : band width, 20, 40, 80, 160, 5 ,10
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_bw_88xx(struct halmac_adapter *adapter, enum halmac_bw bw)
{
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value32 = HALMAC_REG_R32(REG_WMAC_TRXPTCL_CTL);
	value32 = value32 & (~(BIT(7) | BIT(8)));

	switch (bw) {
	case HALMAC_BW_80:
		value32 = value32 | BIT(8);
		break;
	case HALMAC_BW_40:
		value32 = value32 | BIT(7);
		break;
	case HALMAC_BW_20:
	case HALMAC_BW_10:
	case HALMAC_BW_5:
		break;
	default:
		break;
	}

	HALMAC_REG_W32(REG_WMAC_TRXPTCL_CTL, value32);

	cfg_mac_clk_88xx(adapter);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

void
cfg_txfifo_lt_88xx(struct halmac_adapter *adapter,
		   struct halmac_txfifo_lifetime_cfg *cfg)
{
	u8 value8;
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (cfg->enable == 1) {
		value8 = HALMAC_REG_R8(REG_LIFETIME_EN);
		value8 = value8 | BIT(0) | BIT(1) | BIT(2) | BIT(3);
		HALMAC_REG_W8(REG_LIFETIME_EN, value8);

		value32 = (cfg->lifetime) >> 8;
		value32 = value32 + (value32 << 16);
		HALMAC_REG_W32(REG_PKT_LIFE_TIME, value32);
	} else {
		value8 = HALMAC_REG_R8(REG_LIFETIME_EN);
		value8 = value8 & (~(BIT(0) | BIT(1) | BIT(2) | BIT(3)));
		HALMAC_REG_W8(REG_LIFETIME_EN, value8);
	}
}

enum halmac_ret_status
enable_bb_rf_88xx(struct halmac_adapter *adapter, u8 enable)
{
	u8 value8;
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (enable == 1) {
		status = board_rf_fine_tune_88xx(adapter);
		value8 = HALMAC_REG_R8(REG_SYS_FUNC_EN);
		value8 = value8 | BIT(0) | BIT(1);
		HALMAC_REG_W8(REG_SYS_FUNC_EN, value8);

		value8 = HALMAC_REG_R8(REG_RF_CTRL);
		value8 = value8 | BIT(0) | BIT(1) | BIT(2);
		HALMAC_REG_W8(REG_RF_CTRL, value8);

		value32 = HALMAC_REG_R32(REG_WLRF1);
		value32 = value32 | BIT(24) | BIT(25) | BIT(26);
		HALMAC_REG_W32(REG_WLRF1, value32);
	} else {
		value8 = HALMAC_REG_R8(REG_SYS_FUNC_EN);
		value8 = value8 & (~(BIT(0) | BIT(1)));
		HALMAC_REG_W8(REG_SYS_FUNC_EN, value8);

		value8 = HALMAC_REG_R8(REG_RF_CTRL);
		value8 = value8 & (~(BIT(0) | BIT(1) | BIT(2)));
		HALMAC_REG_W8(REG_RF_CTRL, value8);

		value32 = HALMAC_REG_R32(REG_WLRF1);
		value32 = value32 & (~(BIT(24) | BIT(25) | BIT(26)));
		HALMAC_REG_W32(REG_WLRF1, value32);
	}

	return status;
}

static enum halmac_ret_status
board_rf_fine_tune_88xx(struct halmac_adapter *adapter)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (adapter->chip_id == HALMAC_CHIP_ID_8822B) {
		if (!adapter->efuse_map_valid || !adapter->efuse_map) {
			PLTFM_MSG_ERR("[ERR]efuse map invalid!!\n");
			return HALMAC_RET_EFUSE_R_FAIL;
		}

		map = (u8 *)PLTFM_MALLOC(size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		PLTFM_MEMSET(map, 0xFF, size);

		if (eeprom_parser_88xx(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		/* Fine-tune XTAL voltage for 2L PCB board */
		if (*(map + EFUSE_PCB_INFO_OFFSET) == 0x0C)
			HALMAC_REG_W8_SET(REG_AFE_CTRL1 + 1, BIT(1));

		PLTFM_FREE(map, size);
	}

	return HALMAC_RET_SUCCESS;
}

void
cfg_mac_clk_88xx(struct halmac_adapter *adapter)
{
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value32 = HALMAC_REG_R32(REG_AFE_CTRL1) & ~(BIT(20) | BIT(21));
	value32 |= (MAC_CLK_HW_DEF_80M << BIT_SHIFT_MAC_CLK_SEL);
	HALMAC_REG_W32(REG_AFE_CTRL1, value32);

	HALMAC_REG_W8(REG_USTIME_TSF, MAC_CLK_SPEED);
	HALMAC_REG_W8(REG_USTIME_EDCA, MAC_CLK_SPEED);
}

/**
 * cfg_la_mode_88xx() - config la mode
 * @adapter : the adapter of halmac
 * @mode :
 *	disable : no TXFF space reserved for LA debug
 *	partial : partial TXFF space is reserved for LA debug
 *	full : all TXFF space is reserved for LA debug
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_la_mode_88xx(struct halmac_adapter *adapter, enum halmac_la_mode mode)
{
	if (adapter->api_registry.la_mode_en == 0)
		return HALMAC_RET_NOT_SUPPORT;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->txff_alloc.la_mode = mode;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_rxfifo_expand_mode_88xx() - rx fifo expanding
 * @adapter : the adapter of halmac
 * @mode :
 *	disable : normal mode
 *	1 block : Rx FIFO + 1 FIFO block; Tx fifo - 1 FIFO block
 *	2 block : Rx FIFO + 2 FIFO block; Tx fifo - 2 FIFO block
 *	3 block : Rx FIFO + 3 FIFO block; Tx fifo - 3 FIFO block
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_rxfifo_expand_mode_88xx(struct halmac_adapter *adapter,
			    enum halmac_rx_fifo_expanding_mode mode)
{
	if (adapter->api_registry.rx_exp_en == 0)
		return HALMAC_RET_NOT_SUPPORT;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->txff_alloc.rx_fifo_exp_mode = mode;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
config_security_88xx(struct halmac_adapter *adapter,
		     struct halmac_security_setting *setting)
{
	u8 sec_cfg;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_W16_SET(REG_CR, BIT_MAC_SEC_EN);

	if (setting->compare_keyid == 1) {
		HALMAC_REG_W8_SET(REG_SECCFG + 1, BIT(0));
		adapter->hw_cfg_info.chk_security_keyid = 1;
	} else {
		adapter->hw_cfg_info.chk_security_keyid = 0;
	}

	sec_cfg = HALMAC_REG_R8(REG_SECCFG);

	/* BC/MC uses default key */
	/* cam entry 0~3, kei id = 0 -> entry0, kei id = 1 -> entry1... */
	sec_cfg |= (BIT_TXBCUSEDK | BIT_RXBCUSEDK);

	if (setting->tx_encryption == 1)
		sec_cfg |= BIT_TXENC;
	else
		sec_cfg &= ~BIT_TXENC;

	if (setting->rx_decryption == 1)
		sec_cfg |= BIT_RXDEC;
	else
		sec_cfg &= ~BIT_RXDEC;

	HALMAC_REG_W8(REG_SECCFG, sec_cfg);

	if (setting->bip_enable == 1) {
		if (adapter->chip_id == HALMAC_CHIP_ID_8822B)
			return HALMAC_RET_BIP_NO_SUPPORT;
#if (HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)
		sec_cfg = HALMAC_REG_R8(REG_WSEC_OPTION + 2);

		if (setting->tx_encryption == 1)
			sec_cfg |= (BIT(3) | BIT(5));
		else
			sec_cfg &= ~(BIT(3) | BIT(5));

		if (setting->rx_decryption == 1)
			sec_cfg |= (BIT(4) | BIT(6));
		else
			sec_cfg &= ~(BIT(4) | BIT(6));

		HALMAC_REG_W8(REG_WSEC_OPTION + 2, sec_cfg);
#endif
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

u8
get_used_cam_entry_num_88xx(struct halmac_adapter *adapter,
			    enum hal_security_type sec_type)
{
	u8 entry_num;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

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

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return entry_num;
}

enum halmac_ret_status
write_cam_88xx(struct halmac_adapter *adapter, u32 idx,
	       struct halmac_cam_entry_info *info)
{
	u32 i;
	u32 cmd = 0x80010000;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_cam_entry_format *fmt = NULL;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (idx >= adapter->hw_cfg_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	if (info->key_id > 3)
		return HALMAC_RET_FAIL;

	fmt = (struct halmac_cam_entry_format *)PLTFM_MALLOC(sizeof(*fmt));
	if (!fmt)
		return HALMAC_RET_NULL_POINTER;
	PLTFM_MEMSET(fmt, 0x00, sizeof(*fmt));

	if (adapter->hw_cfg_info.chk_security_keyid == 1)
		fmt->key_id = info->key_id;
	fmt->valid = info->valid;
	PLTFM_MEMCPY(fmt->mac_address, info->mac_address, 6);
	PLTFM_MEMCPY(fmt->key, info->key, 16);

	switch (info->security_type) {
	case HAL_SECURITY_TYPE_NONE:
		fmt->type = 0;
		break;
	case HAL_SECURITY_TYPE_WEP40:
		fmt->type = 1;
		break;
	case HAL_SECURITY_TYPE_WEP104:
		fmt->type = 5;
		break;
	case HAL_SECURITY_TYPE_TKIP:
		fmt->type = 2;
		break;
	case HAL_SECURITY_TYPE_AES128:
		fmt->type = 4;
		break;
	case HAL_SECURITY_TYPE_WAPI:
		fmt->type = 6;
		break;
	case HAL_SECURITY_TYPE_AES256:
		fmt->type = 4;
		fmt->ext_sectype = 1;
		break;
	case HAL_SECURITY_TYPE_GCMP128:
		fmt->type = 7;
		break;
	case HAL_SECURITY_TYPE_GCMP256:
	case HAL_SECURITY_TYPE_GCMSMS4:
		fmt->type = 7;
		fmt->ext_sectype = 1;
		break;
	case HAL_SECURITY_TYPE_BIP:
		fmt->type = (info->unicast == 1) ? 4 : 0;
		fmt->mgnt = 1;
		fmt->grp = (info->unicast == 1) ? 0 : 1;
		break;
	default:
		PLTFM_FREE(fmt, sizeof(*fmt));
		return HALMAC_RET_FAIL;
	}

	for (i = 0; i < 8; i++) {
		HALMAC_REG_W32(REG_CAMWRITE, *((u32 *)fmt + i));
		HALMAC_REG_W32(REG_CAMCMD, cmd | ((idx << 3) + i));
	}

	if (info->security_type == HAL_SECURITY_TYPE_WAPI ||
	    info->security_type == HAL_SECURITY_TYPE_AES256 ||
	    info->security_type == HAL_SECURITY_TYPE_GCMP256 ||
	    info->security_type == HAL_SECURITY_TYPE_GCMSMS4) {
		fmt->mic = 1;
		PLTFM_MEMCPY(fmt->key, info->key_ext, 16);
		idx++;
		for (i = 0; i < 8; i++) {
			HALMAC_REG_W32(REG_CAMWRITE, *((u32 *)fmt + i));
			HALMAC_REG_W32(REG_CAMCMD, cmd | ((idx << 3) + i));
		}
	}

	PLTFM_FREE(fmt, sizeof(*fmt));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
read_cam_entry_88xx(struct halmac_adapter *adapter, u32 idx,
		    struct halmac_cam_entry_format *content)
{
	u32 i;
	u32 cmd = 0x80000000;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (idx >= adapter->hw_cfg_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	for (i = 0; i < 8; i++) {
		HALMAC_REG_W32(REG_CAMCMD, cmd | ((idx << 3) + i));
		*((u32 *)content + i) = HALMAC_REG_R32(REG_CAMREAD);
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
clear_cam_entry_88xx(struct halmac_adapter *adapter, u32 idx)
{
	u32 i;
	u32 cmd = 0x80010000;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_cam_entry_format *fmt = NULL;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (idx >= adapter->hw_cfg_info.cam_entry_num)
		return HALMAC_RET_ENTRY_INDEX_ERROR;

	fmt = (struct halmac_cam_entry_format *)PLTFM_MALLOC(sizeof(*fmt));
	if (!fmt)
		return HALMAC_RET_NULL_POINTER;
	PLTFM_MEMSET(fmt, 0x00, sizeof(*fmt));

	for (i = 0; i < 8; i++) {
		HALMAC_REG_W32(REG_CAMWRITE, *((u32 *)fmt + i));
		HALMAC_REG_W32(REG_CAMCMD, cmd | ((idx << 3) + i));
	}

	PLTFM_FREE(fmt, sizeof(*fmt));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

void
rx_shift_88xx(struct halmac_adapter *adapter, u8 enable)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_TXDMA_PQ_MAP);

	if (enable == 1)
		HALMAC_REG_W8(REG_TXDMA_PQ_MAP, value8 | BIT(1));
	else
		HALMAC_REG_W8(REG_TXDMA_PQ_MAP, value8 & ~(BIT(1)));
}

/**
 * cfg_edca_para_88xx() - config edca parameter
 * @adapter : the adapter of halmac
 * @acq_id : VO/VI/BE/BK
 * @param : aifs, cw, txop limit
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_edca_para_88xx(struct halmac_adapter *adapter, enum halmac_acq_id acq_id,
		   struct halmac_edca_para *param)
{
	u32 offset;
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (acq_id) {
	case HALMAC_ACQ_ID_VO:
		offset = REG_EDCA_VO_PARAM;
		break;
	case HALMAC_ACQ_ID_VI:
		offset = REG_EDCA_VI_PARAM;
		break;
	case HALMAC_ACQ_ID_BE:
		offset = REG_EDCA_BE_PARAM;
		break;
	case HALMAC_ACQ_ID_BK:
		offset = REG_EDCA_BK_PARAM;
		break;
	default:
		return HALMAC_RET_SWITCH_CASE_ERROR;
	}

	param->txop_limit &= 0x7FF;
	value32 = (param->aifs) | (param->cw << 8) | (param->txop_limit << 16);

	HALMAC_REG_W32(offset, value32);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

void
rx_clk_gate_88xx(struct halmac_adapter *adapter, u8 enable)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_RCR + 2);

	if (enable == 1)
		HALMAC_REG_W8(REG_RCR + 2, value8 & ~(BIT(3)));
	else
		HALMAC_REG_W8(REG_RCR + 2, value8 | BIT(3));
}

enum halmac_ret_status
rx_cut_amsdu_cfg_88xx(struct halmac_adapter *adapter,
		      struct halmac_cut_amsdu_cfg *cfg)
{
	return HALMAC_RET_NOT_SUPPORT;
}

enum halmac_ret_status
fast_edca_cfg_88xx(struct halmac_adapter *adapter,
		   struct halmac_fast_edca_cfg *cfg)
{
	u16 value16;
	u32 offset;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	switch (cfg->acq_id) {
	case HALMAC_ACQ_ID_VO:
		offset = REG_FAST_EDCA_VOVI_SETTING;
		break;
	case HALMAC_ACQ_ID_VI:
		offset = REG_FAST_EDCA_VOVI_SETTING + 2;
		break;
	case HALMAC_ACQ_ID_BE:
		offset = REG_FAST_EDCA_BEBK_SETTING;
		break;
	case HALMAC_ACQ_ID_BK:
		offset = REG_FAST_EDCA_BEBK_SETTING + 2;
		break;
	default:
		return HALMAC_RET_SWITCH_CASE_ERROR;
	}

	value16 = HALMAC_REG_R16(offset);
	value16 &= 0xFF;
	value16 = value16 | (cfg->queue_to << 8);

	HALMAC_REG_W16(offset, value16);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_mac_addr_88xx() - get mac address
 * @adapter : the adapter of halmac
 * @port : 0 for port0, 1 for port1, 2 for port2, 3 for port3, 4 for port4
 * @addr : mac address
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_mac_addr_88xx(struct halmac_adapter *adapter, u8 port,
		  union halmac_wlan_addr *addr)
{
	u16 mac_addr_h;
	u32 mac_addr_l;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (port >= HALMAC_PORTID_NUM) {
		PLTFM_MSG_ERR("[ERR]port index >= 5\n");
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	switch (port) {
	case HALMAC_PORTID0:
		mac_addr_l = HALMAC_REG_R32(REG_MACID);
		mac_addr_h = HALMAC_REG_R16(REG_MACID + 4);
		break;
	case HALMAC_PORTID1:
		mac_addr_l = HALMAC_REG_R32(REG_MACID1);
		mac_addr_h = HALMAC_REG_R16(REG_MACID1 + 4);
		break;
	case HALMAC_PORTID2:
		mac_addr_l = HALMAC_REG_R32(REG_MACID2);
		mac_addr_h = HALMAC_REG_R16(REG_MACID2 + 4);
		break;
	case HALMAC_PORTID3:
		mac_addr_l = HALMAC_REG_R32(REG_MACID3);
		mac_addr_h = HALMAC_REG_R16(REG_MACID3 + 4);
		break;
	case HALMAC_PORTID4:
		mac_addr_l = HALMAC_REG_R32(REG_MACID4);
		mac_addr_h = HALMAC_REG_R16(REG_MACID4 + 4);
		break;
	default:
		return HALMAC_RET_PORT_NOT_SUPPORT;
	}

	addr->addr_l_h.low = rtk_cpu_to_le32(mac_addr_l);
	addr->addr_l_h.high = rtk_cpu_to_le16(mac_addr_h);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

void
rts_full_bw_88xx(struct halmac_adapter *adapter, u8 enable)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_INIRTS_RATE_SEL);

	if (enable == 1)
		HALMAC_REG_W8(REG_INIRTS_RATE_SEL, value8 | BIT(5));
	else
		HALMAC_REG_W8(REG_INIRTS_RATE_SEL, value8 & ~(BIT(5)));
}

#endif /* HALMAC_88XX_SUPPORT */
