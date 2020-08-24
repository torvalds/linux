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

#include "halmac_sdio_88xx.h"
#include "halmac_88xx_cfg.h"

#if (HALMAC_88XX_SUPPORT && HALMAC_SDIO_SUPPORT)

/* define the SDIO Bus CLK threshold */
/* for avoiding CMD53 fails that result from SDIO CLK sync to ana_clk fail */
#define SDIO_CLK_HIGH_SPEED_TH		50 /* 50MHz */
#define SDIO_CLK_SPEED_MAX		208 /* 208MHz */

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u8
r_indir_cmd52_88xx(struct halmac_adapter *adapter, u32 offset);

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r_indir_cmd53_88xx(struct halmac_adapter *adapter, u32 offset);

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r8_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr);

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r16_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr);

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r32_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr);

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w_indir_cmd52_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		   enum halmac_io_size size);

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w_indir_cmd53_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		   enum halmac_io_size size);

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w8_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val);

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w16_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val);

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w32_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val);

/**
 * init_sdio_cfg_88xx() - init SDIO
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_sdio_cfg_88xx(struct halmac_adapter *adapter)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * deinit_sdio_cfg_88xx() - deinit SDIO
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
deinit_sdio_cfg_88xx(struct halmac_adapter *adapter)
{
	if (adapter->intf != HALMAC_INTERFACE_SDIO)
		return HALMAC_RET_WRONG_INTF;

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_sdio_rx_agg_88xx() - config rx aggregation
 * @adapter : the adapter of halmac
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_sdio_rx_agg_88xx(struct halmac_adapter *adapter,
		     struct halmac_rxagg_cfg *cfg)
{
	u8 value8;
	u8 size;
	u8 timeout;
	u8 agg_enable;
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	agg_enable = HALMAC_REG_R8(REG_TXDMA_PQ_MAP);

	switch (cfg->mode) {
	case HALMAC_RX_AGG_MODE_NONE:
		agg_enable &= ~(BIT_RXDMA_AGG_EN);
		break;
	case HALMAC_RX_AGG_MODE_DMA:
	case HALMAC_RX_AGG_MODE_USB:
		agg_enable |= BIT_RXDMA_AGG_EN;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]unsupported mode\n");
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	}

	if (cfg->threshold.drv_define == 0) {
		size = 0xFF;
		timeout = 0x01;
	} else {
		size = cfg->threshold.size;
		timeout = cfg->threshold.timeout;
	}

	value32 = HALMAC_REG_R32(REG_RXDMA_AGG_PG_TH);
	if (cfg->threshold.size_limit_en == 0)
		HALMAC_REG_W32(REG_RXDMA_AGG_PG_TH, value32 & ~BIT_EN_PRE_CALC);
	else
		HALMAC_REG_W32(REG_RXDMA_AGG_PG_TH, value32 | BIT_EN_PRE_CALC);

	HALMAC_REG_W8(REG_TXDMA_PQ_MAP, agg_enable);
	HALMAC_REG_W16(REG_RXDMA_AGG_PG_TH,
		       (u16)(size | (timeout << BIT_SHIFT_DMA_AGG_TO_V1)));

	value8 = HALMAC_REG_R8(REG_RXDMA_MODE);
	if (0 != (agg_enable & BIT_RXDMA_AGG_EN))
		HALMAC_REG_W8(REG_RXDMA_MODE, value8 | BIT_DMA_MODE);
	else
		HALMAC_REG_W8(REG_RXDMA_MODE, value8 & ~(BIT_DMA_MODE));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * sdio_reg_rn_88xx() - read n byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @halmac_size : register value size
 * @value : register value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
sdio_reg_rn_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
		 u8 *value)
{
	u8 *r_val = NULL;
	u32 r_size = size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;

	if (0 == (offset & 0xFFFF0000)) {
		PLTFM_MSG_ERR("[ERR]offset 0x%x\n", offset);
		return HALMAC_RET_FAIL;
	}

	status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]convert offset\n");
		return status;
	}

	if (adapter->pwr_off_flow_flag == 1 ||
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		PLTFM_MSG_ERR("[ERR]power off\n");
		return HALMAC_RET_FAIL;
	}

	if ((cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	     cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R) &&
	    (size & 0x03) != 0) {
		if (adapter->sdio_hw_info.io_warn_flag == 1) {
			PLTFM_MSG_WARN("[WARN]reg_rn !align,addr 0x%x,siz %d\n",
				       offset, size);
			adapter->watcher.get_watcher.sdio_rn_not_align++;
		}
		r_size = size - (size & 0x03) + 4;
		r_val = (u8 *)PLTFM_MALLOC(r_size);
		if (!r_val) {
			PLTFM_MSG_ERR("[ERR]malloc!!\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(r_val, 0x00, r_size);
		PLTFM_SDIO_CMD53_RN(offset, r_size, r_val);
		PLTFM_MEMCPY(value, r_val, size);
		PLTFM_FREE(r_val, r_size);
	} else {
		PLTFM_SDIO_CMD53_RN(offset, size, value);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_txagg_sdio_align_88xx() -config sdio bus tx agg alignment
 * @adapter : the adapter of halmac
 * @enable : function enable(1)/disable(0)
 * @align_size : sdio bus tx agg alignment size (2^n, n = 3~11)
 * Author : Soar Tu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_txagg_sdio_align_88xx(struct halmac_adapter *adapter, u8 enable,
			  u16 align_size)
{
	u8 i;
	u8 flag = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->chip_id == HALMAC_CHIP_ID_8822B)
		return HALMAC_RET_NOT_SUPPORT;

	if ((align_size & 0xF000) != 0) {
		PLTFM_MSG_ERR("[ERR]out of range\n");
		return HALMAC_RET_FAIL;
	}

	for (i = 3; i <= 11; i++) {
		if (align_size == 1 << i) {
			flag = 1;
			break;
		}
	}

	if (flag == 0) {
		PLTFM_MSG_ERR("[ERR]not 2^3 ~ 2^11\n");
		return HALMAC_RET_FAIL;
	}

	adapter->hw_cfg_info.tx_align_size = align_size;

	if (enable)
		HALMAC_REG_W16(REG_RQPN_CTRL_2, 0x8000 | align_size);
	else
		HALMAC_REG_W16(REG_RQPN_CTRL_2, align_size);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * sdio_indirect_reg_r32_88xx() - read MAC reg by SDIO reg
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32
sdio_indirect_reg_r32_88xx(struct halmac_adapter *adapter, u32 offset)
{
	return r_indir_sdio_88xx(adapter, offset, HALMAC_IO_DWORD);
}

/**
 * set_sdio_bulkout_num_88xx() - inform bulk-out num
 * @adapter : the adapter of halmac
 * @bulkout_num : usb bulk-out number
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
set_sdio_bulkout_num_88xx(struct halmac_adapter *adapter, u8 num)
{
	return HALMAC_RET_NOT_SUPPORT;
}

/**
 * get_sdio_bulkout_id_88xx() - get bulk out id for the TX packet
 * @adapter : the adapter of halmac
 * @halmac_buf : tx packet, include txdesc
 * @halmac_size : tx packet size
 * @bulkout_id : usb bulk-out id
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_sdio_bulkout_id_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size,
			 u8 *id)
{
	return HALMAC_RET_NOT_SUPPORT;
}

/**
 * sdio_cmd53_4byte_88xx() - cmd53 only for 4byte len register IO
 * @adapter : the adapter of halmac
 * @enable : 1->CMD53 only use in 4byte reg, 0 : No limitation
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
sdio_cmd53_4byte_88xx(struct halmac_adapter *adapter,
		      enum halmac_sdio_cmd53_4byte_mode mode)
{
	if (adapter->intf != HALMAC_INTERFACE_SDIO)
		return HALMAC_RET_WRONG_INTF;

	if (adapter->api_registry.sdio_cmd53_4byte_en == 0)
		return HALMAC_RET_NOT_SUPPORT;

	adapter->sdio_cmd53_4byte = mode;

	return HALMAC_RET_SUCCESS;
}

/**
 * sdio_hw_info_88xx() - info sdio hw info
 * @adapter : the adapter of halmac
 * @HALMAC_SDIO_CMD53_4BYTE_MODE :
 * clock_speed : sdio bus clock. Unit -> MHz
 * spec_ver : sdio spec version
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
sdio_hw_info_88xx(struct halmac_adapter *adapter,
		  struct halmac_sdio_hw_info *info)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->intf != HALMAC_INTERFACE_SDIO)
		return HALMAC_RET_WRONG_INTF;

	PLTFM_MSG_TRACE("[TRACE]SDIO clock:%d, spec:%d\n",
			info->clock_speed, info->spec_ver);

	if (info->clock_speed > SDIO_CLK_SPEED_MAX)
		return HALMAC_RET_SDIO_CLOCK_ERR;

	if (info->clock_speed > SDIO_CLK_HIGH_SPEED_TH)
		adapter->sdio_hw_info.io_hi_speed_flag = 1;

	adapter->sdio_hw_info.io_indir_flag = info->io_indir_flag;
	if (info->clock_speed > SDIO_CLK_HIGH_SPEED_TH &&
	    adapter->sdio_hw_info.io_indir_flag == 0)
		PLTFM_MSG_WARN("[WARN]SDIO clock:%d, indir access is better\n",
			       info->clock_speed);

	adapter->sdio_hw_info.clock_speed = info->clock_speed;
	adapter->sdio_hw_info.spec_ver = info->spec_ver;
	adapter->sdio_hw_info.block_size = info->block_size;

	/*SW*/
	adapter->sdio_hw_info.io_warn_flag = info->io_warn_flag;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

void
cfg_sdio_tx_page_threshold_88xx(struct halmac_adapter *adapter,
				struct halmac_tx_page_threshold_info *info)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 threshold = info->threshold;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (info->enable == 1) {
		threshold = BIT(31) | threshold;
		PLTFM_MSG_TRACE("[TRACE]enable\n");
	} else {
		threshold = ~(BIT(31)) & threshold;
		PLTFM_MSG_TRACE("[TRACE]disable\n");
	}

	switch (info->dma_queue_sel) {
	case HALMAC_MAP2_HQ:
		HALMAC_REG_W32(REG_TQPNT1, threshold);
		break;
	case HALMAC_MAP2_NQ:
		HALMAC_REG_W32(REG_TQPNT2, threshold);
		break;
	case HALMAC_MAP2_LQ:
		HALMAC_REG_W32(REG_TQPNT3, threshold);
		break;
	case HALMAC_MAP2_EXQ:
		HALMAC_REG_W32(REG_TQPNT4, threshold);
		break;
	default:
		break;
	}
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);
}

enum halmac_ret_status
cnv_to_sdio_bus_offset_88xx(struct halmac_adapter *adapter, u32 *offset)
{
	switch ((*offset) & 0xFFFF0000) {
	case WLAN_IOREG_OFFSET:
		*offset &= HALMAC_WLAN_MAC_REG_MSK;
		*offset |= HALMAC_SDIO_CMD_ADDR_MAC_REG << 13;
		break;
	case SDIO_LOCAL_OFFSET:
		*offset &= HALMAC_SDIO_LOCAL_MSK;
		*offset |= HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13;
		break;
	default:
		*offset = 0xFFFFFFFF;
		PLTFM_MSG_ERR("[ERR]base address!!\n");
		return HALMAC_RET_CONVERT_SDIO_OFFSET_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
leave_sdio_suspend_88xx(struct halmac_adapter *adapter)
{
	u8 value8;
	u32 cnt;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value8 = HALMAC_REG_R8(REG_SDIO_HSUS_CTRL);
	HALMAC_REG_W8(REG_SDIO_HSUS_CTRL, value8 & ~(BIT(0)));

	cnt = 10000;
	while (!(HALMAC_REG_R8(REG_SDIO_HSUS_CTRL) & 0x02)) {
		cnt--;
		if (cnt == 0)
			return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
	}

	value8 = HALMAC_REG_R8(REG_HCI_OPT_CTRL + 2);
	if (adapter->sdio_hw_info.spec_ver == HALMAC_SDIO_SPEC_VER_3_00)
		HALMAC_REG_W8(REG_HCI_OPT_CTRL + 2, value8 | BIT(2));
	else
		HALMAC_REG_W8(REG_HCI_OPT_CTRL + 2, value8 & ~(BIT(2)));

	return HALMAC_RET_SUCCESS;
}

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u8
r_indir_cmd52_88xx(struct halmac_adapter *adapter, u32 offset)
{
	u8 value8, tmp, cnt = 50;
	u32 reg_cfg = REG_SDIO_INDIRECT_REG_CFG;
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_cfg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_SDIO_CMD52_W(reg_cfg, (u8)offset);
	PLTFM_SDIO_CMD52_W(reg_cfg + 1, (u8)(offset >> 8));
	PLTFM_SDIO_CMD52_W(reg_cfg + 2, (u8)(BIT(3) | BIT(4)));

	do {
		tmp = PLTFM_SDIO_CMD52_R(reg_cfg + 2);
		cnt--;
	} while (((tmp & BIT(4)) == 0) && (cnt > 0));

	if (((tmp & BIT(4)) == 0) && cnt == 0)
		PLTFM_MSG_ERR("[ERR]sdio indirect CMD52 read\n");

	value8 = PLTFM_SDIO_CMD52_R(reg_data);

	return value8;
}

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r_indir_cmd53_88xx(struct halmac_adapter *adapter, u32 offset)
{
	u8 cnt = 50;
	u8 value[8];
	u32 reg_cfg = REG_SDIO_INDIRECT_REG_CFG;
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	union {
		__le32 dword;
		u8 byte[4];
	} value32 = { 0x00000000 };

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_cfg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_SDIO_CMD53_W32(reg_cfg, offset | BIT(19) | BIT(20));

	do {
		PLTFM_SDIO_CMD53_RN(reg_cfg + 2, sizeof(value), value);
		cnt--;
	} while (((value[0] & BIT(4)) == 0) && (cnt > 0));

	if (((value[0] & BIT(4)) == 0) && cnt == 0)
		PLTFM_MSG_ERR("[ERR]sdio indirect CMD53 read\n");

	value32.byte[0] = value[2];
	value32.byte[1] = value[3];
	value32.byte[2] = value[4];
	value32.byte[3] = value[5];

	return rtk_le32_to_cpu(value32.dword);
}

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r8_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr)
{
	union {
		__le32 dword;
		u8 byte[4];
	} val = { 0x00000000 };

	if (adapter->pwr_off_flow_flag == 1 ||
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		val.byte[0] = r_indir_cmd52_88xx(adapter, adr);
		return rtk_le32_to_cpu(val.dword);
	}

	return r_indir_cmd53_88xx(adapter, adr);
}

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r16_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr)
{
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	union {
		__le32 dword;
		u8 byte[4];
	} val = { 0x00000000 };

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		if (0 != (adr & (2 - 1))) {
			val.byte[0] = r_indir_cmd52_88xx(adapter, adr);
			val.byte[1] = r_indir_cmd52_88xx(adapter, adr + 1);
		} else {
			val.byte[0] = r_indir_cmd52_88xx(adapter, adr);
			val.byte[1] = PLTFM_SDIO_CMD52_R(reg_data + 1);
		}

		return  rtk_le32_to_cpu(val.dword);
	}

	if (0 != (adr & (2 - 1))) {
		val.byte[0] = (u8)r_indir_cmd53_88xx(adapter, adr);
		val.byte[1] = (u8)r_indir_cmd53_88xx(adapter, adr + 1);

		return rtk_le32_to_cpu(val.dword);
	}

	return r_indir_cmd53_88xx(adapter, adr);
}

/*only for r_indir_sdio_88xx !!, Soar 20171222*/
static u32
r32_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr)
{
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	union {
		__le32 dword;
		u8 byte[4];
	} val = { 0x00000000 };

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		if (0 != (adr & (4 - 1))) {
			val.byte[0] = r_indir_cmd52_88xx(adapter, adr);
			val.byte[1] = r_indir_cmd52_88xx(adapter, adr + 1);
			val.byte[2] = r_indir_cmd52_88xx(adapter, adr + 2);
			val.byte[3] = r_indir_cmd52_88xx(adapter, adr + 3);
		} else {
			val.byte[0] = r_indir_cmd52_88xx(adapter, adr);
			val.byte[1] = PLTFM_SDIO_CMD52_R(reg_data + 1);
			val.byte[2] = PLTFM_SDIO_CMD52_R(reg_data + 2);
			val.byte[3] = PLTFM_SDIO_CMD52_R(reg_data + 3);
		}

		return rtk_le32_to_cpu(val.dword);
	}

	if (0 != (adr & (4 - 1))) {
		val.byte[0] = (u8)r_indir_cmd53_88xx(adapter, adr);
		val.byte[1] = (u8)r_indir_cmd53_88xx(adapter, adr + 1);
		val.byte[2] = (u8)r_indir_cmd53_88xx(adapter, adr + 2);
		val.byte[3] = (u8)r_indir_cmd53_88xx(adapter, adr + 3);

		return rtk_le32_to_cpu(val.dword);
	}

	return r_indir_cmd53_88xx(adapter, adr);
}

u32
r_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr,
		  enum halmac_io_size size)
{
	u32 value32 = 0;

	PLTFM_MUTEX_LOCK(&adapter->sdio_indir_mutex);

	switch (size) {
	case HALMAC_IO_BYTE:
		value32 = r8_indir_sdio_88xx(adapter, adr);
		break;
	case HALMAC_IO_WORD:
		value32 = r16_indir_sdio_88xx(adapter, adr);
		break;
	case HALMAC_IO_DWORD:
		value32 = r32_indir_sdio_88xx(adapter, adr);
		break;
	default:
		break;
	}

	PLTFM_MUTEX_UNLOCK(&adapter->sdio_indir_mutex);

	return value32;
}

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w_indir_cmd52_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		   enum halmac_io_size size)
{
	u8 tmp, cnt = 50;
	u32 reg_cfg = REG_SDIO_INDIRECT_REG_CFG;
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_cfg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_SDIO_CMD52_W(reg_cfg, (u8)adr);
	PLTFM_SDIO_CMD52_W(reg_cfg + 1, (u8)(adr >> 8));
	switch (size) {
	case HALMAC_IO_BYTE:
		PLTFM_SDIO_CMD52_W(reg_data, (u8)val);
		PLTFM_SDIO_CMD52_W(reg_cfg + 2, (u8)(BIT(2) | BIT(4)));
		break;
	case HALMAC_IO_WORD:
		PLTFM_SDIO_CMD52_W(reg_data, (u8)val);
		PLTFM_SDIO_CMD52_W(reg_data + 1, (u8)(val >> 8));
		PLTFM_SDIO_CMD52_W(reg_cfg + 2,
				   (u8)(BIT(0) | BIT(2) | BIT(4)));
		break;
	case HALMAC_IO_DWORD:
		PLTFM_SDIO_CMD52_W(reg_data, (u8)val);
		PLTFM_SDIO_CMD52_W(reg_data + 1, (u8)(val >> 8));
		PLTFM_SDIO_CMD52_W(reg_data + 2, (u8)(val >> 16));
		PLTFM_SDIO_CMD52_W(reg_data + 3, (u8)(val >> 24));
		PLTFM_SDIO_CMD52_W(reg_cfg + 2,
				   (u8)(BIT(1) | BIT(2) | BIT(4)));
		break;
	default:
		break;
	}

	do {
		tmp = PLTFM_SDIO_CMD52_R(reg_cfg + 2);
		cnt--;
	} while (((tmp & BIT(4)) == 0) && (cnt > 0));

	if (((tmp & BIT(4)) == 0) && cnt == 0)
		PLTFM_MSG_ERR("[ERR]sdio indirect CMD52 write\n");

	return status;
}

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w_indir_cmd53_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		   enum halmac_io_size size)
{
	u8 tmp, cnt = 50;
	u32 reg_cfg = REG_SDIO_INDIRECT_REG_CFG;
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	u32 value32 = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_cfg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	switch (size) {
	case HALMAC_IO_BYTE:
		value32 = adr | BIT(18) | BIT(20);
		break;
	case HALMAC_IO_WORD:
		value32 = adr | BIT(16) | BIT(18) | BIT(20);
		break;
	case HALMAC_IO_DWORD:
		value32 = adr | BIT(17) | BIT(18) | BIT(20);
		break;
	default:
		return HALMAC_RET_FAIL;
	}

	PLTFM_SDIO_CMD53_W32(reg_data, val);
	PLTFM_SDIO_CMD53_W32(reg_cfg, value32);

	do {
		tmp = PLTFM_SDIO_CMD52_R(reg_cfg + 2);
		cnt--;
	} while (((tmp & BIT(4)) == 0) && (cnt > 0));

	if (((tmp & BIT(4)) == 0) && cnt == 0)
		PLTFM_MSG_ERR("[ERR]sdio indirect CMD53 read\n");

	return status;
}

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w8_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->pwr_off_flow_flag == 1 ||
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		status = w_indir_cmd52_88xx(adapter, adr, val, HALMAC_IO_BYTE);
	else
		status = w_indir_cmd53_88xx(adapter, adr, val, HALMAC_IO_BYTE);
	return status;
}

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w16_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		if (0 != (adr & (2 - 1))) {
			status = w_indir_cmd52_88xx(adapter, adr, val,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd52_88xx(adapter, adr + 1, val >> 8,
						    HALMAC_IO_BYTE);
		} else {
			status = w_indir_cmd52_88xx(adapter, adr, val,
						    HALMAC_IO_WORD);
		}
	} else {
		if (0 != (adr & (2 - 1))) {
			status = w_indir_cmd53_88xx(adapter, adr, val,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd53_88xx(adapter, adr + 1, val >> 8,
						    HALMAC_IO_BYTE);
		} else {
			status = w_indir_cmd53_88xx(adapter, adr, val,
						    HALMAC_IO_WORD);
		}
	}
	return status;
}

/*only for w_indir_sdio_88xx !!, Soar 20171222*/
static enum halmac_ret_status
w32_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		if (0 != (adr & (4 - 1))) {
			status = w_indir_cmd52_88xx(adapter, adr, val,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd52_88xx(adapter, adr + 1, val >> 8,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd52_88xx(adapter, adr + 2, val >> 16,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd52_88xx(adapter, adr + 3, val >> 24,
						    HALMAC_IO_BYTE);
		} else {
			status = w_indir_cmd52_88xx(adapter, adr, val,
						    HALMAC_IO_DWORD);
		}
	} else {
		if (0 != (adr & (4 - 1))) {
			status = w_indir_cmd53_88xx(adapter, adr, val,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd53_88xx(adapter, adr + 1, val >> 8,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd53_88xx(adapter, adr + 2, val >> 16,
						    HALMAC_IO_BYTE);
			if (status != HALMAC_RET_SUCCESS)
				return status;
			status = w_indir_cmd53_88xx(adapter, adr + 3, val >> 24,
						    HALMAC_IO_BYTE);
		} else {
			status = w_indir_cmd53_88xx(adapter, adr, val,
						    HALMAC_IO_DWORD);
		}
	}
	return status;
}

enum halmac_ret_status
w_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		  enum halmac_io_size size)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MUTEX_LOCK(&adapter->sdio_indir_mutex);

	switch (size) {
	case HALMAC_IO_BYTE:
		status = w8_indir_sdio_88xx(adapter, adr, val);
		break;
	case HALMAC_IO_WORD:
		status = w16_indir_sdio_88xx(adapter, adr, val);
		break;
	case HALMAC_IO_DWORD:
		status = w32_indir_sdio_88xx(adapter, adr, val);
		break;
	default:
		break;
	}

	PLTFM_MUTEX_UNLOCK(&adapter->sdio_indir_mutex);

	return status;
}

enum halmac_ret_status
en_ref_autok_sdio_88xx(struct halmac_adapter *adapter, u8 en)
{
	return HALMAC_RET_NOT_SUPPORT;
}
#endif /* HALMAC_88XX_SUPPORT */
