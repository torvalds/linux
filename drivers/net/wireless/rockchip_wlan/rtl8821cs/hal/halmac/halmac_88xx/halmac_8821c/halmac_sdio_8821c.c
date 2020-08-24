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

#include "halmac_sdio_8821c.h"
#include "halmac_pwr_seq_8821c.h"
#include "../halmac_init_88xx.h"
#include "../halmac_common_88xx.h"
#include "../halmac_sdio_88xx.h"

#if (HALMAC_8821C_SUPPORT && HALMAC_SDIO_SUPPORT)

#define WLAN_ACQ_NUM_MAX	8

static enum halmac_ret_status
chk_oqt_8821c(struct halmac_adapter *adapter, u32 tx_agg_num, u8 *buf,
	      u8 macid_cnt);

static enum halmac_ret_status
update_oqt_free_space_8821c(struct halmac_adapter *adapter);

static enum halmac_ret_status
update_sdio_free_page_8821c(struct halmac_adapter *adapter);

static enum halmac_ret_status
chk_qsel_8821c(struct halmac_adapter *adapter, u8 qsel_first, u8 *pkt,
	       u8 *macid_cnt);

static enum halmac_ret_status
chk_dma_mapping_8821c(struct halmac_adapter *adapter, u16 **cur_fs,
		      u8 qsel_first);

static enum halmac_ret_status
chk_rqd_page_num_8821c(struct halmac_adapter *adapter, u8 *buf, u32 *rqd_pg_num,
		       u16 **cur_fs, u8 *macid_cnt, u32 tx_agg_num);

/**
 * init_sdio_cfg_8821c() - init SDIO
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_sdio_cfg_8821c(struct halmac_adapter *adapter)
{
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (adapter->intf != HALMAC_INTERFACE_SDIO)
		return HALMAC_RET_WRONG_INTF;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_R32(REG_SDIO_FREE_TXPG);

	value32 = HALMAC_REG_R32(REG_SDIO_TX_CTRL) & 0xFFFF;
	value32 &= ~(BIT_CMD_ERR_STOP_INT_EN | BIT_EN_MASK_TIMER |
							BIT_EN_RXDMA_MASK_INT);
	HALMAC_REG_W32(REG_SDIO_TX_CTRL, value32);

	HALMAC_REG_W8_SET(REG_SDIO_BUS_CTRL, BIT_EN_RPT_TXCRC);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mac_pwr_switch_sdio_8821c() - switch mac power
 * @adapter : the adapter of halmac
 * @pwr : power state
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mac_pwr_switch_sdio_8821c(struct halmac_adapter *adapter,
			  enum halmac_mac_power pwr)
{
	u8 value8;
	u8 rpwm;
	u32 imr_backup;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]halmac_mac_power_switch_88xx_sdio pwr\n");
	PLTFM_MSG_TRACE("[TRACE]%x\n", pwr);
	PLTFM_MSG_TRACE("[TRACE]8821C pwr seq ver = %s\n",
			HALMAC_8821C_PWR_SEQ_VER);

	adapter->rpwm = HALMAC_REG_R8(REG_SDIO_HRPWM1);

	/* Check FW still exist or not */
	if (HALMAC_REG_R16(REG_MCUFW_CTRL) == 0xC078) {
		/* Leave 32K */
		rpwm = (u8)((adapter->rpwm ^ BIT(7)) & 0x80);
		HALMAC_REG_W8(REG_SDIO_HRPWM1, rpwm);
	}

	value8 = HALMAC_REG_R8(REG_CR);
	if (value8 == 0xEA)
		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_OFF;
	else
		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_ON;

	/*Check if power switch is needed*/
	if (pwr == HALMAC_MAC_POWER_ON &&
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_ON) {
		PLTFM_MSG_WARN("[WARN]power state unchange!!\n");
		return HALMAC_RET_PWR_UNCHANGE;
	}

	imr_backup = HALMAC_REG_R32(REG_SDIO_HIMR);
	HALMAC_REG_W32(REG_SDIO_HIMR, 0);

	if (pwr == HALMAC_MAC_POWER_OFF) {
		adapter->pwr_off_flow_flag = 1;
		if (pwr_seq_parser_88xx(adapter, card_dis_flow_8821c) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]Handle power off cmd error\n");
			HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);
			return HALMAC_RET_POWER_OFF_FAIL;
		}

		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_OFF;
		adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;
		adapter->pwr_off_flow_flag = 0;
		init_adapter_dynamic_param_88xx(adapter);
	} else {
		if (pwr_seq_parser_88xx(adapter, card_en_flow_8821c) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]Handle power on cmd error\n");
			HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);
			return HALMAC_RET_POWER_ON_FAIL;
		}

		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_ON;
	}

	HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_tx_allowed_sdio_88xx() - check tx status
 * @adapter : the adapter of halmac
 * @buf : tx packet, include txdesc
 * @size : tx packet size, include txdesc
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
tx_allowed_sdio_8821c(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u16 *cur_fs = NULL;
	u32 cnt;
	u32 tx_agg_num;
	u32 rqd_pg_num = 0;
	u8 macid_cnt = 0;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_qsel qsel;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!fs_info->macid_map) {
		PLTFM_MSG_ERR("[ERR]halmac allocate Macid_map Fail!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}

	PLTFM_MEMSET(fs_info->macid_map, 0x00, fs_info->macid_map_size);

	tx_agg_num = GET_TX_DESC_DMA_TXAGG_NUM(buf);
	tx_agg_num = (tx_agg_num == 0) ? 1 : tx_agg_num;

	status = chk_rqd_page_num_8821c(adapter, buf, &rqd_pg_num, &cur_fs,
					&macid_cnt, tx_agg_num);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	qsel = (enum halmac_qsel)GET_TX_DESC_QSEL(buf);
	if (qsel == HALMAC_QSEL_BCN || qsel == HALMAC_QSEL_CMD)
		return HALMAC_RET_SUCCESS;

	cnt = 10;
	do {
		if ((u32)(*cur_fs + fs_info->pubq_pg_num) > rqd_pg_num) {
			status = chk_oqt_8821c(adapter, tx_agg_num, buf,
					       macid_cnt);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_WARN("[WARN]oqt buffer full!!\n");
				return status;
			}

			if (*cur_fs >= rqd_pg_num) {
				*cur_fs -= (u16)rqd_pg_num;
			} else {
				fs_info->pubq_pg_num -=
						(u16)(rqd_pg_num - *cur_fs);
				*cur_fs = 0;
			}

			break;
		}

		update_sdio_free_page_8821c(adapter);

		cnt--;
		if (cnt == 0)
			return HALMAC_RET_FREE_SPACE_NOT_ENOUGH;
	} while (1);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_sdio_88xx() - read 1byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8
reg_r8_sdio_8821c(struct halmac_adapter *adapter, u32 offset)
{
	u8 value8;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		if (0 == (offset & 0xFFFF0000))
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		value8 = PLTFM_SDIO_CMD52_R(offset);
	} else {
		if ((offset & 0xFFFF0000) == 0) {
			value8 = (u8)r_indir_sdio_88xx(adapter, offset,
						       HALMAC_IO_BYTE);
		} else {
			status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]convert offset\n");
				return status;
			}
			value8 = PLTFM_SDIO_CMD52_R(offset);
		}
	}

	return value8;
}

/**
 * halmac_reg_write_8_sdio_88xx() - write 1byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w8_sdio_8821c(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		if (0 == (offset & 0xFFFF0000))
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);

		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		PLTFM_SDIO_CMD52_W(offset, value);
	} else {
		if ((offset & 0xFFFF0000) == 0) {
			w_indir_sdio_88xx(adapter, offset, value,
					  HALMAC_IO_BYTE);
		} else {
			status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]convert offset\n");
				return status;
			}
			PLTFM_SDIO_CMD52_W(offset, value);
		}
	}
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_sdio_88xx() - read 2byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u16
reg_r16_sdio_8821c(struct halmac_adapter *adapter, u32 offset)
{
	u32 offset_old = 0;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	union {
		__le16 word;
		u8 byte[2];
	} val = { 0x0000 };

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		offset_old = offset;

		if ((offset & 0xFFFF0000) == 0)
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    ((offset & (2 - 1)) != 0) ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R ||
		    (adapter->sdio_hw_info.io_hi_speed_flag != 0 &&
		     (offset_old & 0xffffef00) == 0x00000000)) {
			val.byte[0] = PLTFM_SDIO_CMD52_R(offset);
			val.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);

			return rtk_le16_to_cpu(val.word);
		}

		return PLTFM_SDIO_CMD53_R16(offset);
	}

	if ((offset & 0xFFFF0000) == 0)
		return (u16)r_indir_sdio_88xx(adapter, offset, HALMAC_IO_WORD);

	status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]convert offset\n");
		return status;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
	    ((offset & (2 - 1)) != 0) ||
	    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R) {
		val.byte[0] = PLTFM_SDIO_CMD52_R(offset);
		val.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);

		return rtk_le16_to_cpu(val.word);
	}

	return PLTFM_SDIO_CMD53_R16(offset);
}

/**
 * halmac_reg_write_16_sdio_88xx() - write 2byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w16_sdio_8821c(struct halmac_adapter *adapter, u32 offset, u16 val)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		if (0 == (offset & 0xFFFF0000))
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);

		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    ((offset & (2 - 1)) != 0) ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_W) {
			PLTFM_SDIO_CMD52_W(offset, (u8)(val & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((val & 0xFF00) >> 8));
		} else {
			PLTFM_SDIO_CMD53_W16(offset, val);
		}
	} else {
		if ((offset & 0xFFFF0000) == 0) {
			status = w_indir_sdio_88xx(adapter, offset, val,
						   HALMAC_IO_WORD);
			return status;
		}

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    ((offset & (2 - 1)) != 0) ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_W) {
			PLTFM_SDIO_CMD52_W(offset, (u8)(val & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((val & 0xFF00) >> 8));
		} else {
			PLTFM_SDIO_CMD53_W16(offset, val);
		}
	}
	return status;
}

/**
 * halmac_reg_read_32_sdio_88xx() - read 4byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32
reg_r32_sdio_8821c(struct halmac_adapter *adapter, u32 offset)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u32 offset_old = 0;
	union {
		__le32 dword;
		u8 byte[4];
	} val = { 0x00000000 };

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		offset_old = offset;
		if (0 == (offset & 0xFFFF0000))
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    (offset & (4 - 1)) != 0 ||
		    (adapter->sdio_hw_info.io_hi_speed_flag != 0 &&
		     (offset_old & 0xffffef00) == 0x00000000)) {
			val.byte[0] = PLTFM_SDIO_CMD52_R(offset);
			val.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);
			val.byte[2] = PLTFM_SDIO_CMD52_R(offset + 2);
			val.byte[3] = PLTFM_SDIO_CMD52_R(offset + 3);

			return rtk_le32_to_cpu(val.dword);
		}

		return PLTFM_SDIO_CMD53_R32(offset);
	}

	if ((offset & 0xFFFF0000) == 0)
		return r_indir_sdio_88xx(adapter, offset, HALMAC_IO_DWORD);

	status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]convert offset\n");
		return status;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
	    (offset & (4 - 1)) != 0) {
		val.byte[0] = PLTFM_SDIO_CMD52_R(offset);
		val.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);
		val.byte[2] = PLTFM_SDIO_CMD52_R(offset + 2);
		val.byte[3] = PLTFM_SDIO_CMD52_R(offset + 3);

		return rtk_le32_to_cpu(val.dword);
	}

	return PLTFM_SDIO_CMD53_R32(offset);
}

/**
 * halmac_reg_write_32_sdio_88xx() - write 4byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w32_sdio_8821c(struct halmac_adapter *adapter, u32 offset, u32 val)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->sdio_hw_info.io_indir_flag == 0) {
		if ((offset & 0xFFFF0000) == 0)
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    (offset & (4 - 1)) !=  0) {
			PLTFM_SDIO_CMD52_W(offset, (u8)(val & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((val >> 8) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 2,
					   (u8)((val >> 16) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 3,
					   (u8)((val >> 24) & 0xFF));
		} else {
			PLTFM_SDIO_CMD53_W32(offset, val);
		}
	} else {
		if ((offset & 0xFFFF0000) == 0) {
			status = w_indir_sdio_88xx(adapter, offset, val,
						   HALMAC_IO_DWORD);
			return status;
		}

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    (offset & (4 - 1)) !=  0) {
			PLTFM_SDIO_CMD52_W(offset, (u8)(val & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((val >> 8) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 2,
					   (u8)((val >> 16) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 3,
					   (u8)((val >> 24) & 0xFF));
		} else {
			PLTFM_SDIO_CMD53_W32(offset, val);
		}
	}
	return status;
}

static enum halmac_ret_status
chk_oqt_8821c(struct halmac_adapter *adapter, u32 tx_agg_num, u8 *buf,
	      u8 macid_cnt)
{
	u32 cnt = 10;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;

	/*S0, S1 are not allowed to use, 0x4E4[0] should be 0. Soar 20160323*/
	/*no need to check non_ac_oqt_number*/
	/*HI and MGQ blocked will cause protocal issue before H_OQT being full*/
	switch ((enum halmac_qsel)GET_TX_DESC_QSEL(buf)) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		if (macid_cnt > WLAN_ACQ_NUM_MAX &&
		    tx_agg_num > OQT_ENTRY_AC_8821C) {
			PLTFM_MSG_WARN("[WARN]txagg num %d > oqt entry\n",
				       tx_agg_num);
			PLTFM_MSG_WARN("[WARN]macid cnt %d > acq max\n",
				       macid_cnt);
		}

		cnt = 10;
		do {
			if (fs_info->ac_empty >= macid_cnt) {
				fs_info->ac_empty -= macid_cnt;
				break;
			}

			if (fs_info->ac_oqt_num >= tx_agg_num) {
				fs_info->ac_empty = 0;
				fs_info->ac_oqt_num -= (u8)tx_agg_num;
				break;
			}

			update_oqt_free_space_8821c(adapter);

			cnt--;
			if (cnt == 0)
				return HALMAC_RET_OQT_NOT_ENOUGH;
		} while (1);
		break;
	case HALMAC_QSEL_MGNT:
	case HALMAC_QSEL_HIGH:
		if (tx_agg_num > OQT_ENTRY_NOAC_8821C)
			PLTFM_MSG_WARN("[WARN]tx_agg_num %d > oqt entry\n",
				       tx_agg_num);
		cnt = 10;
		do {
			if (fs_info->non_ac_oqt_num >= tx_agg_num) {
				fs_info->non_ac_oqt_num -= (u8)tx_agg_num;
				break;
			}

			update_oqt_free_space_8821c(adapter);

			cnt--;
			if (cnt == 0)
				return HALMAC_RET_OQT_NOT_ENOUGH;
		} while (1);
		break;
	default:
		break;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_oqt_free_space_8821c(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	u8 value;
	u32 oqt_free_page;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	oqt_free_page = HALMAC_REG_R32(REG_SDIO_OQT_FREE_TXPG_V1);
	fs_info->ac_oqt_num = (u8)BIT_GET_AC_OQT_FREEPG_V1(oqt_free_page);
	fs_info->non_ac_oqt_num = (u8)BIT_GET_NOAC_OQT_FREEPG_V1(oqt_free_page);
	fs_info->ac_empty = 0;
	if (fs_info->ac_oqt_num == OQT_ENTRY_AC_8821C) {
		value = HALMAC_REG_R8(REG_TXPKT_EMPTY);
		while (value > 0) {
			value = value & (value - 1);
			fs_info->ac_empty++;
		};
	} else {
		PLTFM_MSG_TRACE("[TRACE]free_space->ac_oqt_num %d != %d\n",
				fs_info->ac_oqt_num, OQT_ENTRY_AC_8821C);
	}
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_sdio_free_page_8821c(struct halmac_adapter *adapter)
{
	u32 free_page = 0;
	u32 free_page2 = 0;
	u32 free_page3 = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	u8 data[12] = {0};

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_SDIO_RN(REG_SDIO_FREE_TXPG, 12, data);

	free_page = rtk_le32_to_cpu(*(__le32 *)(data + 0));
	free_page2 = rtk_le32_to_cpu(*(__le32 *)(data + 4));
	free_page3 = rtk_le32_to_cpu(*(__le32 *)(data + 8));

	fs_info->hiq_pg_num = (u16)BIT_GET_HIQ_FREEPG_V1(free_page);
	fs_info->miq_pg_num = (u16)BIT_GET_MID_FREEPG_V1(free_page);
	fs_info->lowq_pg_num = (u16)BIT_GET_LOW_FREEPG_V1(free_page2);
	fs_info->pubq_pg_num = (u16)BIT_GET_PUB_FREEPG_V1(free_page2);
	fs_info->exq_pg_num = (u16)BIT_GET_EXQ_FREEPG_V1(free_page3);
	fs_info->ac_oqt_num = (u8)BIT_GET_AC_OQT_FREEPG_V1(free_page3);
	fs_info->non_ac_oqt_num = (u8)BIT_GET_NOAC_OQT_FREEPG_V1(free_page3);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * phy_cfg_sdio_8821c() - phy config
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
phy_cfg_sdio_8821c(struct halmac_adapter *adapter,
		   enum halmac_intf_phy_platform pltfm)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pcie_switch_8821c() - pcie gen1/gen2 switch
 * @adapter : the adapter of halmac
 * @cfg : gen1/gen2 selection
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pcie_switch_sdio_8821c(struct halmac_adapter *adapter,
		       enum halmac_pcie_cfg cfg)
{
	return HALMAC_RET_NOT_SUPPORT;
}

/**
 * intf_tun_sdio_8821c() - sdio interface fine tuning
 * @adapter : the adapter of halmac
 * Author : Ivan
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
intf_tun_sdio_8821c(struct halmac_adapter *adapter)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_sdio_tx_addr_sdio_88xx() - get CMD53 addr for the TX packet
 * @adapter : the adapter of halmac
 * @buf : tx packet, include txdesc
 * @size : tx packet size
 * @cmd53_addr : cmd53 addr value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_sdio_tx_addr_8821c(struct halmac_adapter *adapter, u8 *buf, u32 size,
		       u32 *cmd53_addr)
{
	u32 len_unit4;
	enum halmac_qsel queue_sel;
	enum halmac_dma_mapping dma_mapping;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!buf) {
		PLTFM_MSG_ERR("[ERR]buf is NULL!!\n");
		return HALMAC_RET_DATA_BUF_NULL;
	}

	if (size == 0) {
		PLTFM_MSG_ERR("[ERR]size is 0!!\n");
		return HALMAC_RET_DATA_SIZE_INCORRECT;
	}

	queue_sel = (enum halmac_qsel)GET_TX_DESC_QSEL(buf);

	switch (queue_sel) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VO];
		break;
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VI];
		break;
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BE];
		break;
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BK];
		break;
	case HALMAC_QSEL_MGNT:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_MG];
		break;
	case HALMAC_QSEL_HIGH:
	case HALMAC_QSEL_BCN:
	case HALMAC_QSEL_CMD:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_HI];
		break;
	default:
		PLTFM_MSG_ERR("[ERR]Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	len_unit4 = (size >> 2) + ((size & (4 - 1)) ? 1 : 0);

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_HIGH;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_NORMAL;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_LOW;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_EXTRA;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	*cmd53_addr = (*cmd53_addr << 13) |
				(len_unit4 & HALMAC_SDIO_4BYTE_LEN_MASK);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_qsel_8821c(struct halmac_adapter *adapter, u8 qsel_first, u8 *pkt,
	       u8 *macid_cnt)
{
	u8 flag = 0;
	u8 qsel_now;
	u8 macid;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;

	macid = (u8)GET_TX_DESC_MACID(pkt);
	qsel_now = (u8)GET_TX_DESC_QSEL(pkt);
	if (qsel_first == qsel_now) {
		if (*(fs_info->macid_map + macid) == 0) {
			*(fs_info->macid_map + macid) = 1;
			(*macid_cnt)++;
		}
	} else {
		switch ((enum halmac_qsel)qsel_now) {
		case HALMAC_QSEL_VO:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VO_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_VO_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VO)
				flag = 1;
			break;
		case HALMAC_QSEL_VI:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VI_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_VI_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VI)
				flag = 1;
			break;
		case HALMAC_QSEL_BE:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BE_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_BE_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BE)
				flag = 1;
			break;
		case HALMAC_QSEL_BK:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BK_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_BK_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BK)
				flag = 1;
			break;
		case HALMAC_QSEL_MGNT:
		case HALMAC_QSEL_HIGH:
		case HALMAC_QSEL_BCN:
		case HALMAC_QSEL_CMD:
			flag = 1;
			break;
		default:
			PLTFM_MSG_ERR("[ERR]Qsel is out of range\n");
			return HALMAC_RET_QSEL_INCORRECT;
		}
		if (flag == 1) {
			PLTFM_MSG_ERR("[ERR]Multi-Qsel is not allowed\n");
			PLTFM_MSG_ERR("[ERR]qsel = %d, %d\n",
				      qsel_first, qsel_now);
			return HALMAC_RET_QSEL_INCORRECT;
		}
		if (*(fs_info->macid_map + macid + MACID_MAX_8821C) == 0) {
			*(fs_info->macid_map + macid + MACID_MAX_8821C) = 1;
			(*macid_cnt)++;
		}
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_dma_mapping_8821c(struct halmac_adapter *adapter, u16 **cur_fs,
		      u8 qsel_first)
{
	enum halmac_dma_mapping dma_mapping;

	switch ((enum halmac_qsel)qsel_first) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VO];
		break;
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VI];
		break;
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BE];
		break;
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BK];
		break;
	case HALMAC_QSEL_MGNT:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_MG];
		break;
	case HALMAC_QSEL_HIGH:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_HI];
		break;
	case HALMAC_QSEL_BCN:
	case HALMAC_QSEL_CMD:
		*cur_fs = &adapter->sdio_fs.hiq_pg_num;
		return HALMAC_RET_SUCCESS;
	default:
		PLTFM_MSG_ERR("[ERR]Qsel is out of range: %d\n", qsel_first);
		return HALMAC_RET_QSEL_INCORRECT;
	}

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*cur_fs = &adapter->sdio_fs.hiq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*cur_fs = &adapter->sdio_fs.miq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*cur_fs = &adapter->sdio_fs.lowq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*cur_fs = &adapter->sdio_fs.exq_pg_num;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_rqd_page_num_8821c(struct halmac_adapter *adapter, u8 *buf, u32 *rqd_pg_num,
		       u16 **cur_fs, u8 *macid_cnt, u32 tx_agg_num)
{
	u8 *pkt;
	u8 qsel_first;
	u32 i;
	u32 pkt_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	pkt = buf;

	qsel_first = (u8)GET_TX_DESC_QSEL(pkt);

	status = chk_dma_mapping_8821c(adapter, cur_fs, qsel_first);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	for (i = 0; i < tx_agg_num; i++) {
		/*QSEL parser*/
		status = chk_qsel_8821c(adapter, qsel_first, pkt, macid_cnt);
		if (status != HALMAC_RET_SUCCESS)
			return status;

		/*Page number parser*/
		pkt_size = GET_TX_DESC_TXPKTSIZE(pkt) + GET_TX_DESC_OFFSET(pkt);
		*rqd_pg_num += (pkt_size >> TX_PAGE_SIZE_SHIFT_88XX) +
				((pkt_size & (TX_PAGE_SIZE_88XX - 1)) ? 1 : 0);

		pkt += HALMAC_ALIGN(GET_TX_DESC_TXPKTSIZE(pkt) +
				    (GET_TX_DESC_PKT_OFFSET(pkt) << 3) +
				    TX_DESC_SIZE_88XX, 8);
	}

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_8821C_SUPPORT */
