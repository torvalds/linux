/* SPDX-License-Identifier: GPL-2.0 */
#include "halmac_88xx_cfg.h"

/**
 * halmac_init_sdio_cfg_88xx() - init SDIO related register
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_sdio_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_SDIO_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_sdio_cfg_88xx ==========>\n");

	HALMAC_REG_READ_32(pHalmac_adapter, REG_SDIO_FREE_TXPG);
	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_SDIO_TX_CTRL, 0x00000000);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_sdio_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_sdio_cfg_88xx() - deinit SDIO related register
 * @pHalmac_adapter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_deinit_sdio_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DEINIT_SDIO_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_deinit_sdio_cfg_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_deinit_sdio_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_aggregation_88xx_sdio() - config rx aggregation
 * @pHalmac_adapter
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_rx_aggregation_88xx_sdio(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_RXAGG_CFG phalmac_rxagg_cfg
)
{
	u8 value8, dma_usb_agg = 0;
	u8 size = 0, timeout = 0, agg_enable = 0;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_RX_AGGREGATION);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_rx_aggregation_88xx_sdio ==========>\n");

	dma_usb_agg = HALMAC_REG_READ_8(pHalmac_adapter, REG_RXDMA_AGG_PG_TH + 3);
	agg_enable = HALMAC_REG_READ_8(pHalmac_adapter, REG_TXDMA_PQ_MAP);

	switch (phalmac_rxagg_cfg->mode) {
	case HALMAC_RX_AGG_MODE_NONE:
		agg_enable &= ~(BIT_RXDMA_AGG_EN);
		break;
	case HALMAC_RX_AGG_MODE_DMA:
		agg_enable |= BIT_RXDMA_AGG_EN;
		dma_usb_agg |= BIT(7);
		break;

	case HALMAC_RX_AGG_MODE_USB:
		agg_enable |= BIT_RXDMA_AGG_EN;
		dma_usb_agg &= ~(BIT(7));
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_cfg_rx_aggregation_88xx_usb switch case not support\n");
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	}

	if (_FALSE == phalmac_rxagg_cfg->threshold.drv_define) {
		size = 0xFF;
		timeout = 0x01;
	} else {
		size = phalmac_rxagg_cfg->threshold.size;
		timeout = phalmac_rxagg_cfg->threshold.timeout;
	}


	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TXDMA_PQ_MAP, agg_enable);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RXDMA_AGG_PG_TH + 3, dma_usb_agg);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RXDMA_AGG_PG_TH, (u16)(size | (timeout << BIT_SHIFT_DMA_AGG_TO)));

	value8 = HALMAC_REG_READ_8(pHalmac_adapter, REG_RXDMA_MODE);
	if (0 != (agg_enable & BIT_RXDMA_AGG_EN))
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RXDMA_MODE, value8 | BIT_DMA_MODE);
	else
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RXDMA_MODE, value8 & ~(BIT_DMA_MODE));

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_rx_aggregation_88xx_sdio <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_sdio_88xx() - read 1byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
u8
halmac_reg_read_8_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	u8 value8;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_read_8_sdio_88xx error = %x\n", status);
		return status;
	}

	value8 = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset);

	return value8;
}

/**
 * halmac_reg_write_8_sdio_88xx() - write 1byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_8_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_data
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_write_8_sdio_88xx error = %x\n", status);
		return status;
	}

	PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset, halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_sdio_88xx() - read 2byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
u16
halmac_reg_read_16_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	union {
		u16	word;
		u8	byte[2];
	} value16 = { 0x0000 };

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_read_16_sdio_88xx error = %x\n", status);
		return status;
	}

	if (HALMAC_MAC_POWER_OFF == pHalmac_adapter->halmac_state.mac_power) {
		value16.byte[0] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset);
		value16.byte[1] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset + 1);
		value16.word = rtk_le16_to_cpu(value16.word);
	} else {
		if ((PLATFORM_SD_CLK > HALMAC_SD_CLK_THRESHOLD_88XX) && ((halmac_offset & 0xffffef00) == 0x00000000)) {
			value16.byte[0] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset);
			value16.byte[1] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset + 1);
			value16.word = rtk_le16_to_cpu(value16.word);
		} else {
			value16.word = PLATFORM_SDIO_CMD53_READ_16(pDriver_adapter, halmac_offset);
		}
	}

	return value16.word;
}

/**
 * halmac_reg_write_16_sdio_88xx() - write 2byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_16_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u16 halmac_data
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_write_16_sdio_88xx error = %x\n", status);
		return status;
	}

	if (HALMAC_MAC_POWER_OFF == pHalmac_adapter->halmac_state.mac_power) {
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset, (u8)(halmac_data & 0xFF));
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset + 1, (u8)((halmac_data & 0xFF00) >> 8));
	} else {
		PLATFORM_SDIO_CMD53_WRITE_16(pDriver_adapter, halmac_offset, halmac_data);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_32_sdio_88xx() - read 4byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
u32
halmac_reg_read_32_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	u8 rtemp = 0xFF;
	u32 counter = 1000;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	union {
		u32	dword;
		u8	byte[4];
	} value32 = { 0x00000000 };

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);
	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_read_32_sdio_88xx error = %x\n", status);
		return status;
	}

	if (HALMAC_MAC_POWER_OFF == pHalmac_adapter->halmac_state.mac_power) {
		value32.byte[0] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset);
		value32.byte[1] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset + 1);
		value32.byte[2] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset + 2);
		value32.byte[3] = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, halmac_offset + 3);
		value32.dword = rtk_le32_to_cpu(value32.dword);
	} else {
		if ((PLATFORM_SD_CLK > HALMAC_SD_CLK_THRESHOLD_88XX) && ((halmac_offset & 0xffffef00) == 0x00000000)) {
			PLATFORM_SDIO_CMD53_WRITE_32(pDriver_adapter, REG_SDIO_INDIRECT_REG_CFG, halmac_offset | BIT(19) | BIT(17));

			do {
				rtemp = PLATFORM_SDIO_CMD52_READ(pDriver_adapter, REG_SDIO_INDIRECT_REG_CFG + 2);
				counter--;
			} while (((rtemp & BIT(4)) != 0) && (counter > 0));

			value32.dword = PLATFORM_SDIO_CMD53_READ_32(pDriver_adapter, REG_SDIO_INDIRECT_REG_DATA);
		} else {
			value32.dword = PLATFORM_SDIO_CMD53_READ_32(pDriver_adapter, halmac_offset);
		}
	}

	return value32.dword;
}

/**
 * halmac_reg_write_32_sdio_88xx() - write 4byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_32_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u32 halmac_data
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (0 == (halmac_offset & 0xFFFF0000))
		halmac_offset |= WLAN_IOREG_OFFSET;

	status = halmac_convert_to_sdio_bus_offset_88xx(pHalmac_adapter, &halmac_offset);

	if (HALMAC_RET_SUCCESS != status) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_reg_write_32_sdio_88xx error = %x\n", status);
		return status;
	}

	if (HALMAC_MAC_POWER_OFF == pHalmac_adapter->halmac_state.mac_power) {
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset, (u8)(halmac_data & 0xFF));
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset + 1, (u8)((halmac_data & 0xFF00) >> 8));
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset + 2, (u8)((halmac_data & 0xFF0000) >> 16));
		PLATFORM_SDIO_CMD52_WRITE(pDriver_adapter, halmac_offset + 3, (u8)((halmac_data & 0xFF000000) >> 24));
	} else {
		PLATFORM_SDIO_CMD53_WRITE_32(pDriver_adapter, halmac_offset, halmac_data);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_sdio_tx_addr_88xx() - get CMD53 addr for the TX packet
 * @pHalmac_adapter
 * @halmac_buf
 * @halmac_size
 * @pcmd53_addr
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_sdio_tx_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *halmac_buf,
	IN u32 halmac_size,
	OUT u32 *pcmd53_addr
)
{
	u32 four_byte_len;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_QUEUE_SELECT queue_sel;
	HALMAC_DMA_MAPPING dma_mapping;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_SDIO_TX_ADDR);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_get_sdio_tx_addr_88xx ==========>\n");

	if (NULL == halmac_buf) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_buf is NULL!!\n");
		return HALMAC_RET_DATA_BUF_NULL;
	}

	if (0 == halmac_size) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_size is 0!!\n");
		return HALMAC_RET_DATA_SIZE_INCORRECT;
	}

	queue_sel = (HALMAC_QUEUE_SELECT)GET_TX_DESC_QSEL(halmac_buf);

	switch (queue_sel) {
	case HALMAC_QUEUE_SELECT_VO:
	case HALMAC_QUEUE_SELECT_VO_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		break;
	case HALMAC_QUEUE_SELECT_VI:
	case HALMAC_QUEUE_SELECT_VI_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		break;
	case HALMAC_QUEUE_SELECT_BE:
	case HALMAC_QUEUE_SELECT_BE_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		break;
	case HALMAC_QUEUE_SELECT_BK:
	case HALMAC_QUEUE_SELECT_BK_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		break;
	case HALMAC_QUEUE_SELECT_MGNT:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		break;
	case HALMAC_QUEUE_SELECT_HIGH:
	case HALMAC_QUEUE_SELECT_BCN:
	case HALMAC_QUEUE_SELECT_CMD:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Qsel is out of range\n");
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
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	*pcmd53_addr = (*pcmd53_addr << 13) | (four_byte_len & HALMAC_SDIO_4BYTE_LEN_MASK);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_get_sdio_tx_addr_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_tx_agg_align_sdio_88xx() -
 * @pHalmac_adapter
 * @enable
 * @align_size
 * Author : Soar Tu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 enable,
	IN u16 align_size
)
{
	PHALMAC_API pHalmac_api;
	VOID *pDriver_adapter = NULL;
	u8 i, align_size_ok = 0;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_TX_AGG_ALIGN);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_sdio_88xx ==========>\n");

	if ((align_size & 0xF000) != 0) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Align size is out of range\n");
		return HALMAC_RET_FAIL;
	}

	for (i = 3; i <= 11; i++) {
		if (align_size == 1 << i) {
			align_size_ok = 1;
			break;
		}
	}
	if (align_size_ok == 0) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Align size is not 2^3 ~ 2^11\n");
		return HALMAC_RET_FAIL;
	}

	if (enable)
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RQPN_CTRL_2, 0x8000 | align_size);
	else
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RQPN_CTRL_2, align_size);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_sdio_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_tx_agg_align_sdio_not_support_88xx() -
 * @pHalmac_adapter
 * @enable
 * @align_size
 * Author : Soar Tu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_sdio_not_support_88xx(
	IN PHALMAC_ADAPTER	pHalmac_adapter,
	IN u8	enable,
	IN u16	align_size
)
{
	PHALMAC_API pHalmac_api;
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_CFG_TX_AGG_ALIGN);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_sdio_not_support_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_sdio_not_support_88xx not support\n");
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_sdio_not_support_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

