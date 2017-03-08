#include "halmac_88xx_cfg.h"

/**
 * halmac_init_usb_cfg_88xx() - init USB related register
 * @pHalmac_adapter
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_usb_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	u8 value8 = 0;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_INIT_USB_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_usb_cfg_88xx ==========>\n");

	value8 |= (BIT_DMA_MODE | (0x3 << BIT_SHIFT_BURST_CNT));                /* burst number = 4 */

	if (PLATFORM_REG_READ_8(pDriver_adapter, REG_SYS_CFG2 + 3) == 0x20) {   /* usb3.0 */
		value8 |= (HALMAC_USB_BURST_SIZE_3_0 << BIT_SHIFT_BURST_SIZE);
	} else {
		if ((PLATFORM_REG_READ_8(pDriver_adapter, REG_USB_USBSTAT) & 0x3) == 0x1)       /* usb2.0 */
			value8 |= HALMAC_USB_BURST_SIZE_2_0_HSPEED << BIT_SHIFT_BURST_SIZE;
		else                                                                            /* usb1.1 */
			value8 |= HALMAC_USB_BURST_SIZE_2_0_FSPEED << BIT_SHIFT_BURST_SIZE;
	}

	PLATFORM_REG_WRITE_8(pDriver_adapter, REG_RXDMA_MODE, value8);
	PLATFORM_REG_WRITE_16(pDriver_adapter, REG_TXDMA_OFFSET_CHK, PLATFORM_REG_READ_16(pDriver_adapter, REG_TXDMA_OFFSET_CHK) | BIT_DROP_DATA_EN);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_init_usb_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_usb_cfg_88xx() - init USB related register
 * @pHalmac_adapter
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_deinit_usb_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_DEINIT_USB_CFG);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_deinit_usb_cfg_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_deinit_usb_cfg_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_aggregation_88xx_usb() -  config rx aggregation
 * @pHalmac_adapter
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_rx_aggregation_88xx_usb(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_RXAGG_CFG phalmac_rxagg_cfg
)
{
	u8 dma_usb_agg;
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

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_rx_aggregation_88xx_usb ==========>\n");

	dma_usb_agg = HALMAC_REG_READ_8(pHalmac_adapter, REG_RXDMA_AGG_PG_TH + 3);
	agg_enable = HALMAC_REG_READ_8(pHalmac_adapter, REG_TXDMA_PQ_MAP);

	switch (phalmac_rxagg_cfg->mode) {
	case HALMAC_RX_AGG_MODE_NONE:
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	case HALMAC_RX_AGG_MODE_DMA:
		agg_enable |= BIT_RXDMA_AGG_EN;
		dma_usb_agg |= BIT(7);
		break;

	case HALMAC_RX_AGG_MODE_USB:
		agg_enable |= BIT_RXDMA_AGG_EN;
		dma_usb_agg &= ~BIT(7);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_cfg_rx_aggregation_88xx_usb switch case not support\n");
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	}

	if (_FALSE == phalmac_rxagg_cfg->threshold.drv_define) {
		if (PLATFORM_REG_READ_8(pDriver_adapter, REG_SYS_CFG2 + 3) == 0x20) {
			/* usb3.0 */
			size = 0x5;
			timeout = 0xA;
		} else {
			/* usb2.0 */
			size = 0x5;
			timeout = 0x20;
		}
	} else {
		size = phalmac_rxagg_cfg->threshold.size;
		timeout = phalmac_rxagg_cfg->threshold.timeout;
	}

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TXDMA_PQ_MAP, agg_enable);
	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_RXDMA_AGG_PG_TH + 3, dma_usb_agg);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_RXDMA_AGG_PG_TH, (u16)(size | (timeout << BIT_SHIFT_DMA_AGG_TO)));

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_rx_aggregation_88xx_usb <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_usb_88xx() - read 1byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
u8
halmac_reg_read_8_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	u8 value8;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_8_usb_88xx ==========>\n"); */

	value8 = PLATFORM_REG_READ_8(pDriver_adapter, halmac_offset);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_8_usb_88xx <==========\n"); */

	return value8;
}

/**
 * halmac_reg_write_8_usb_88xx() - write 1byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_8_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_data
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


	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_8_usb_88xx ==========>\n"); */

	PLATFORM_REG_WRITE_8(pDriver_adapter, halmac_offset, halmac_data);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_8_usb_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_usb_88xx() - read 2byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
u16
halmac_reg_read_16_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

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

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_16_usb_88xx ==========>\n"); */

	value16.word = PLATFORM_REG_READ_16(pDriver_adapter, halmac_offset);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_16_usb_88xx <==========\n"); */

	return value16.word;
}

/**
 * halmac_reg_write_16_usb_88xx() - write 2byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_16_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u16 halmac_data
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

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_16_usb_88xx ==========>\n"); */

	PLATFORM_REG_WRITE_16(pDriver_adapter, halmac_offset, halmac_data);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_16_usb_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_32_usb_88xx() - read 4byte register
 * @pHalmac_adapter
 * @halmac_offset
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
u32
halmac_reg_read_32_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

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

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_32_usb_88xx ==========>\n"); */

	value32.dword = PLATFORM_REG_READ_32(pDriver_adapter, halmac_offset);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_read_32_usb_88xx <==========\n"); */

	return value32.dword;
}

/**
 * halmac_reg_write_32_usb_88xx() - write 4byte register
 * @pHalmac_adapter
 * @halmac_offset
 * @halmac_data
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_reg_write_32_usb_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u32 halmac_data
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

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_32_usb_88xx ==========>\n"); */

	PLATFORM_REG_WRITE_32(pDriver_adapter, halmac_offset, halmac_data);

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_reg_write_32_usb_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_set_bulkout_num_88xx() - set bulk out endpoint number
 * @pHalmac_adapter
 * @bulkout_num
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_set_bulkout_num_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 bulkout_num
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_SET_BULKOUT_NUM);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE,"halmac_set_bulkout_num_88xx ==========>\n"); */
	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE,"bulkout_num = %d\n", bulkout_num); */

	pHalmac_adapter->halmac_bulkout_num = bulkout_num;

	/* PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE,"halmac_set_bulkout_num_88xx <==========\n"); */

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_usb_bulkout_id_88xx() - get bulk out id for the TX packet
 * @pHalmac_adapter
 * @halmac_buf
 * @halmac_size
 * @bulkout_id
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_get_usb_bulkout_id_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *halmac_buf,
	IN u32 halmac_size,
	OUT u8 *bulkout_id
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;
	HALMAC_QUEUE_SELECT queue_sel;
	HALMAC_DMA_MAPPING dma_mapping;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_GET_USB_BULKOUT_ID);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_get_usb_bulkout_id_88xx ==========>\n");

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
		dma_mapping = HALMAC_DMA_MAPPING_HIGH;
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*bulkout_id = 0;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*bulkout_id = 1;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*bulkout_id = 2;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*bulkout_id = 3;
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_get_usb_bulkout_id_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_tx_agg_align_usb_not_support_88xx() -
 * @pHalmac_adapter
 * @enable
 * @align_size
 * Author : Soar Tu
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_usb_not_support_88xx(
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


	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_usb_not_support_88xx ==========>\n");

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_usb_not_support_88xx not support\n");
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_cfg_tx_agg_align_usb_not_support_88xx <==========\n");

	return HALMAC_RET_SUCCESS;
}

