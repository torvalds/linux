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
 * halmac_init_sdio_cfg_88xx() - init SDIO
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_sdio_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_SDIO_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_REG_READ_32(halmac_adapter, REG_SDIO_FREE_TXPG);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_TX_CTRL, 0x00000000);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_sdio_cfg_88xx() - deinit SDIO
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_deinit_sdio_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DEINIT_SDIO_CFG);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_aggregation_88xx_sdio() - config rx aggregation
 * @halmac_adapter : the adapter of halmac
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_sdio(struct halmac_adapter *halmac_adapter,
				    struct halmac_rxagg_cfg *phalmac_rxagg_cfg)
{
	u8 value8;
	u8 size = 0, timeout = 0, agg_enable = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_RX_AGGREGATION);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	agg_enable = HALMAC_REG_READ_8(halmac_adapter, REG_TXDMA_PQ_MAP);

	switch (phalmac_rxagg_cfg->mode) {
	case HALMAC_RX_AGG_MODE_NONE:
		agg_enable &= ~(BIT_RXDMA_AGG_EN);
		break;
	case HALMAC_RX_AGG_MODE_DMA:
	case HALMAC_RX_AGG_MODE_USB:
		agg_enable |= BIT_RXDMA_AGG_EN;
		break;
	default:
		pr_err("halmac_cfg_rx_aggregation_88xx_usb switch case not support\n");
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	}

	if (!phalmac_rxagg_cfg->threshold.drv_define) {
		size = 0xFF;
		timeout = 0x01;
	} else {
		size = phalmac_rxagg_cfg->threshold.size;
		timeout = phalmac_rxagg_cfg->threshold.timeout;
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_PQ_MAP, agg_enable);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_RXDMA_AGG_PG_TH,
			    (u16)(size | (timeout << BIT_SHIFT_DMA_AGG_TO)));

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RXDMA_MODE);
	if ((agg_enable & BIT_RXDMA_AGG_EN) != 0)
		HALMAC_REG_WRITE_8(halmac_adapter, REG_RXDMA_MODE,
				   value8 | BIT_DMA_MODE);
	else
		HALMAC_REG_WRITE_8(halmac_adapter, REG_RXDMA_MODE,
				   value8 & ~(BIT_DMA_MODE));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_sdio_88xx() - read 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8 halmac_reg_read_8_sdio_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset)
{
	u8 value8;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	value8 = PLATFORM_SDIO_CMD52_READ(driver_adapter, halmac_offset);

	return value8;
}

/**
 * halmac_reg_write_8_sdio_88xx() - write 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_8_sdio_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u8 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset, halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_sdio_88xx() - read 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u16 halmac_reg_read_16_sdio_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	union {
		u16 word;
		u8 byte[2];
		__le16 le_word;
	} value16 = {0x0000};

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF ||
	    (halmac_offset & (2 - 1)) != 0 ||
	    halmac_adapter->sdio_cmd53_4byte ==
		    HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    halmac_adapter->sdio_cmd53_4byte ==
		    HALMAC_SDIO_CMD53_4BYTE_MODE_R) {
		value16.byte[0] =
			PLATFORM_SDIO_CMD52_READ(driver_adapter, halmac_offset);
		value16.byte[1] = PLATFORM_SDIO_CMD52_READ(driver_adapter,
							   halmac_offset + 1);
		value16.word = le16_to_cpu(value16.le_word);
	} else {
#if (PLATFORM_SD_CLK > HALMAC_SD_CLK_THRESHOLD_88XX)
		if ((halmac_offset & 0xffffef00) == 0x00000000) {
			value16.byte[0] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset);
			value16.byte[1] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset + 1);
			value16.word = le16_to_cpu(value16.word);
		} else {
			value16.word = PLATFORM_SDIO_CMD53_READ_16(
				driver_adapter, halmac_offset);
		}
#else
		value16.word = PLATFORM_SDIO_CMD53_READ_16(driver_adapter,
							   halmac_offset);
#endif
	}

	return value16.word;
}

/**
 * halmac_reg_write_16_sdio_88xx() - write 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_16_sdio_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u16 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF ||
	    (halmac_offset & (2 - 1)) != 0 ||
	    halmac_adapter->sdio_cmd53_4byte ==
		    HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    halmac_adapter->sdio_cmd53_4byte ==
		    HALMAC_SDIO_CMD53_4BYTE_MODE_W) {
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset,
					  (u8)(halmac_data & 0xFF));
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 1,
					  (u8)((halmac_data & 0xFF00) >> 8));
	} else {
		PLATFORM_SDIO_CMD53_WRITE_16(driver_adapter, halmac_offset,
					     halmac_data);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_32_sdio_88xx() - read 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32 halmac_reg_read_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u32 halmac_offset_old = 0;

	union {
		u32 dword;
		u8 byte[4];
		__le32 le_dword;
	} value32 = {0x00000000};

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	halmac_offset_old = halmac_offset;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF ||
	    (halmac_offset & (4 - 1)) != 0) {
		value32.byte[0] =
			PLATFORM_SDIO_CMD52_READ(driver_adapter, halmac_offset);
		value32.byte[1] = PLATFORM_SDIO_CMD52_READ(driver_adapter,
							   halmac_offset + 1);
		value32.byte[2] = PLATFORM_SDIO_CMD52_READ(driver_adapter,
							   halmac_offset + 2);
		value32.byte[3] = PLATFORM_SDIO_CMD52_READ(driver_adapter,
							   halmac_offset + 3);
		value32.dword = le32_to_cpu(value32.le_dword);
	} else {
#if (PLATFORM_SD_CLK > HALMAC_SD_CLK_THRESHOLD_88XX)
		if ((halmac_offset_old & 0xffffef00) == 0x00000000) {
			value32.byte[0] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset);
			value32.byte[1] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset + 1);
			value32.byte[2] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset + 2);
			value32.byte[3] = PLATFORM_SDIO_CMD52_READ(
				driver_adapter, halmac_offset + 3);
			value32.dword = le32_to_cpu(value32.dword);
		} else {
			value32.dword = PLATFORM_SDIO_CMD53_READ_32(
				driver_adapter, halmac_offset);
		}
#else
		value32.dword = PLATFORM_SDIO_CMD53_READ_32(driver_adapter,
							    halmac_offset);
#endif
	}

	return value32.dword;
}

/**
 * halmac_reg_write_32_sdio_88xx() - write 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u32 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0)
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF ||
	    (halmac_offset & (4 - 1)) != 0) {
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset,
					  (u8)(halmac_data & 0xFF));
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 1,
					  (u8)((halmac_data & 0xFF00) >> 8));
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 2,
					  (u8)((halmac_data & 0xFF0000) >> 16));
		PLATFORM_SDIO_CMD52_WRITE(
			driver_adapter, halmac_offset + 3,
			(u8)((halmac_data & 0xFF000000) >> 24));
	} else {
		PLATFORM_SDIO_CMD53_WRITE_32(driver_adapter, halmac_offset,
					     halmac_data);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_nbyte_sdio_88xx() - read n byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_size : register value size
 * @halmac_data : register value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8 halmac_reg_read_nbyte_sdio_88xx(struct halmac_adapter *halmac_adapter,
				   u32 halmac_offset, u32 halmac_size,
				   u8 *halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if ((halmac_offset & 0xFFFF0000) == 0) {
		pr_err("halmac_offset error = 0x%x\n", halmac_offset);
		return HALMAC_RET_FAIL;
	}

	status = halmac_convert_to_sdio_bus_offset_88xx(halmac_adapter,
							&halmac_offset);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s error = %x\n", __func__, status);
		return status;
	}

	if (halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_OFF) {
		pr_err("halmac_state error = 0x%x\n",
		       halmac_adapter->halmac_state.mac_power);
		return HALMAC_RET_FAIL;
	}

	PLATFORM_SDIO_CMD53_READ_N(driver_adapter, halmac_offset, halmac_size,
				   halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_sdio_tx_addr_sdio_88xx() - get CMD53 addr for the TX packet
 * @halmac_adapter : the adapter of halmac
 * @halmac_buf : tx packet, include txdesc
 * @halmac_size : tx packet size
 * @pcmd53_addr : cmd53 addr value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_sdio_tx_addr_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *halmac_buf, u32 halmac_size, u32 *pcmd53_addr)
{
	u32 four_byte_len;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_queue_select queue_sel;
	enum halmac_dma_mapping dma_mapping;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_GET_SDIO_TX_ADDR);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if (!halmac_buf) {
		pr_err("halmac_buf is NULL!!\n");
		return HALMAC_RET_DATA_BUF_NULL;
	}

	if (halmac_size == 0) {
		pr_err("halmac_size is 0!!\n");
		return HALMAC_RET_DATA_SIZE_INCORRECT;
	}

	queue_sel = (enum halmac_queue_select)GET_TX_DESC_QSEL(halmac_buf);

	switch (queue_sel) {
	case HALMAC_QUEUE_SELECT_VO:
	case HALMAC_QUEUE_SELECT_VO_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		break;
	case HALMAC_QUEUE_SELECT_VI:
	case HALMAC_QUEUE_SELECT_VI_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		break;
	case HALMAC_QUEUE_SELECT_BE:
	case HALMAC_QUEUE_SELECT_BE_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		break;
	case HALMAC_QUEUE_SELECT_BK:
	case HALMAC_QUEUE_SELECT_BK_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		break;
	case HALMAC_QUEUE_SELECT_MGNT:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		break;
	case HALMAC_QUEUE_SELECT_HIGH:
	case HALMAC_QUEUE_SELECT_BCN:
	case HALMAC_QUEUE_SELECT_CMD:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	default:
		pr_err("Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	four_byte_len = (halmac_size >> 2) + ((halmac_size & (4 - 1)) ? 1 : 0);

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*pcmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_HIGH;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*pcmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_NORMAL;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*pcmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_LOW;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*pcmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_EXTRA;
		break;
	default:
		pr_err("DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	*pcmd53_addr = (*pcmd53_addr << 13) |
		       (four_byte_len & HALMAC_SDIO_4BYTE_LEN_MASK);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_tx_agg_align_sdio_88xx() -config sdio bus tx agg alignment
 * @halmac_adapter : the adapter of halmac
 * @enable : function enable(1)/disable(0)
 * @align_size : sdio bus tx agg alignment size (2^n, n = 3~11)
 * Author : Soar Tu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_tx_agg_align_sdio_88xx(struct halmac_adapter *halmac_adapter,
				  u8 enable, u16 align_size)
{
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;
	u8 i, align_size_ok = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_TX_AGG_ALIGN);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	if ((align_size & 0xF000) != 0) {
		pr_err("Align size is out of range\n");
		return HALMAC_RET_FAIL;
	}

	for (i = 3; i <= 11; i++) {
		if (align_size == 1 << i) {
			align_size_ok = 1;
			break;
		}
	}
	if (align_size_ok == 0) {
		pr_err("Align size is not 2^3 ~ 2^11\n");
		return HALMAC_RET_FAIL;
	}

	/*Keep sdio tx agg alignment size for driver query*/
	halmac_adapter->hw_config_info.tx_align_size = align_size;

	if (enable)
		HALMAC_REG_WRITE_16(halmac_adapter, REG_RQPN_CTRL_2,
				    0x8000 | align_size);
	else
		HALMAC_REG_WRITE_16(halmac_adapter, REG_RQPN_CTRL_2,
				    align_size);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_cfg_tx_agg_align_sdio_not_support_88xx(
	struct halmac_adapter *halmac_adapter, u8 enable, u16 align_size)
{
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_CFG_TX_AGG_ALIGN);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
		"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
		"%s not support\n", __func__);
	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
		"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_tx_allowed_sdio_88xx() - check tx status
 * @halmac_adapter : the adapter of halmac
 * @halmac_buf : tx packet, include txdesc
 * @halmac_size : tx packet size, include txdesc
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_tx_allowed_sdio_88xx(struct halmac_adapter *halmac_adapter,
			    u8 *halmac_buf, u32 halmac_size)
{
	u8 *curr_packet;
	u16 *curr_free_space;
	u32 i, counter;
	u32 tx_agg_num, packet_size = 0;
	u32 tx_required_page_num, total_required_page_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	void *driver_adapter = NULL;
	enum halmac_dma_mapping dma_mapping;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_TX_ALLOWED_SDIO);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	tx_agg_num = GET_TX_DESC_DMA_TXAGG_NUM(halmac_buf);
	curr_packet = halmac_buf;

	tx_agg_num = tx_agg_num == 0 ? 1 : tx_agg_num;

	switch ((enum halmac_queue_select)GET_TX_DESC_QSEL(curr_packet)) {
	case HALMAC_QUEUE_SELECT_VO:
	case HALMAC_QUEUE_SELECT_VO_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		break;
	case HALMAC_QUEUE_SELECT_VI:
	case HALMAC_QUEUE_SELECT_VI_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		break;
	case HALMAC_QUEUE_SELECT_BE:
	case HALMAC_QUEUE_SELECT_BE_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		break;
	case HALMAC_QUEUE_SELECT_BK:
	case HALMAC_QUEUE_SELECT_BK_V2:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		break;
	case HALMAC_QUEUE_SELECT_MGNT:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		break;
	case HALMAC_QUEUE_SELECT_HIGH:
		dma_mapping =
			halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	case HALMAC_QUEUE_SELECT_BCN:
	case HALMAC_QUEUE_SELECT_CMD:
		return HALMAC_RET_SUCCESS;
	default:
		pr_err("Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		curr_free_space =
			&halmac_adapter->sdio_free_space.high_queue_number;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		curr_free_space =
			&halmac_adapter->sdio_free_space.normal_queue_number;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		curr_free_space =
			&halmac_adapter->sdio_free_space.low_queue_number;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		curr_free_space =
			&halmac_adapter->sdio_free_space.extra_queue_number;
		break;
	default:
		pr_err("DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	for (i = 0; i < tx_agg_num; i++) {
		packet_size = GET_TX_DESC_TXPKTSIZE(curr_packet) +
			      GET_TX_DESC_OFFSET(curr_packet) +
			      (GET_TX_DESC_PKT_OFFSET(curr_packet) << 3);
		tx_required_page_num =
			(packet_size >>
			 halmac_adapter->hw_config_info.page_size_2_power) +
			((packet_size &
			  (halmac_adapter->hw_config_info.page_size - 1)) ?
				 1 :
				 0);
		total_required_page_num += tx_required_page_num;

		packet_size = HALMAC_ALIGN(packet_size, 8);

		curr_packet += packet_size;
	}

	counter = 10;
	do {
		if ((u32)(*curr_free_space +
			  halmac_adapter->sdio_free_space.public_queue_number) >
		    total_required_page_num) {
			if (*curr_free_space >= total_required_page_num) {
				*curr_free_space -=
					(u16)total_required_page_num;
			} else {
				halmac_adapter->sdio_free_space
					.public_queue_number -=
					(u16)(total_required_page_num -
					      *curr_free_space);
				*curr_free_space = 0;
			}

			status = halmac_check_oqt_88xx(halmac_adapter,
						       tx_agg_num, halmac_buf);

			if (status != HALMAC_RET_SUCCESS)
				return status;

			break;
		}

		halmac_update_sdio_free_page_88xx(halmac_adapter);

		counter--;
		if (counter == 0)
			return HALMAC_RET_FREE_SPACE_NOT_ENOUGH;
	} while (1);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_indirect_32_sdio_88xx() - read MAC reg by SDIO reg
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32 halmac_reg_read_indirect_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
					  u32 halmac_offset)
{
	u8 rtemp;
	u32 counter = 1000;
	void *driver_adapter = NULL;

	union {
		u32 dword;
		u8 byte[4];
	} value32 = {0x00000000};

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;

	PLATFORM_SDIO_CMD53_WRITE_32(
		driver_adapter,
		(HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13) |
			(REG_SDIO_INDIRECT_REG_CFG & HALMAC_SDIO_LOCAL_MSK),
		halmac_offset | BIT(19) | BIT(17));

	do {
		rtemp = PLATFORM_SDIO_CMD52_READ(
			driver_adapter,
			(HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13) |
				((REG_SDIO_INDIRECT_REG_CFG + 2) &
				 HALMAC_SDIO_LOCAL_MSK));
		counter--;
	} while ((rtemp & BIT(4)) != 0 && counter > 0);

	value32.dword = PLATFORM_SDIO_CMD53_READ_32(
		driver_adapter,
		(HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13) |
			(REG_SDIO_INDIRECT_REG_DATA & HALMAC_SDIO_LOCAL_MSK));

	return value32.dword;
}
