// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
 * halmac_init_usb_cfg_88xx() - init USB
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_usb_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	u8 value8 = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_USB_CFG);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	value8 |= (BIT_DMA_MODE |
		   (0x3 << BIT_SHIFT_BURST_CNT)); /* burst number = 4 */

	if (PLATFORM_REG_READ_8(driver_adapter, REG_SYS_CFG2 + 3) ==
	    0x20) { /* usb3.0 */
		value8 |= (HALMAC_USB_BURST_SIZE_3_0 << BIT_SHIFT_BURST_SIZE);
	} else {
		if ((PLATFORM_REG_READ_8(driver_adapter, REG_USB_USBSTAT) &
		     0x3) == 0x1) /* usb2.0 */
			value8 |= HALMAC_USB_BURST_SIZE_2_0_HSPEED
				  << BIT_SHIFT_BURST_SIZE;
		else /* usb1.1 */
			value8 |= HALMAC_USB_BURST_SIZE_2_0_FSPEED
				  << BIT_SHIFT_BURST_SIZE;
	}

	PLATFORM_REG_WRITE_8(driver_adapter, REG_RXDMA_MODE, value8);
	PLATFORM_REG_WRITE_16(
		driver_adapter, REG_TXDMA_OFFSET_CHK,
		PLATFORM_REG_READ_16(driver_adapter, REG_TXDMA_OFFSET_CHK) |
			BIT_DROP_DATA_EN);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_usb_cfg_88xx() - deinit USB
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_deinit_usb_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DEINIT_USB_CFG);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_aggregation_88xx_usb() - config rx aggregation
 * @halmac_adapter : the adapter of halmac
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_usb(struct halmac_adapter *halmac_adapter,
				   struct halmac_rxagg_cfg *phalmac_rxagg_cfg)
{
	u8 dma_usb_agg;
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

	dma_usb_agg =
		HALMAC_REG_READ_8(halmac_adapter, REG_RXDMA_AGG_PG_TH + 3);
	agg_enable = HALMAC_REG_READ_8(halmac_adapter, REG_TXDMA_PQ_MAP);

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
		pr_err("%s switch case not support\n", __func__);
		agg_enable &= ~BIT_RXDMA_AGG_EN;
		break;
	}

	if (!phalmac_rxagg_cfg->threshold.drv_define) {
		if (PLATFORM_REG_READ_8(driver_adapter, REG_SYS_CFG2 + 3) ==
		    0x20) {
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

	HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_PQ_MAP, agg_enable);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_RXDMA_AGG_PG_TH + 3,
			   dma_usb_agg);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_RXDMA_AGG_PG_TH,
			    (u16)(size | (timeout << BIT_SHIFT_DMA_AGG_TO)));

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_usb_88xx() - read 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8 halmac_reg_read_8_usb_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset)
{
	u8 value8;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	value8 = PLATFORM_REG_READ_8(driver_adapter, halmac_offset);

	return value8;
}

/**
 * halmac_reg_write_8_usb_88xx() - write 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_8_usb_88xx(struct halmac_adapter *halmac_adapter,
			    u32 halmac_offset, u8 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	PLATFORM_REG_WRITE_8(driver_adapter, halmac_offset, halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_usb_88xx() - read 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u16 halmac_reg_read_16_usb_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	union {
		u16 word;
		u8 byte[2];
	} value16 = {0x0000};

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	value16.word = PLATFORM_REG_READ_16(driver_adapter, halmac_offset);

	return value16.word;
}

/**
 * halmac_reg_write_16_usb_88xx() - write 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_16_usb_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u16 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	PLATFORM_REG_WRITE_16(driver_adapter, halmac_offset, halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_32_usb_88xx() - read 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32 halmac_reg_read_32_usb_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	union {
		u32 dword;
		u8 byte[4];
	} value32 = {0x00000000};

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	value32.dword = PLATFORM_REG_READ_32(driver_adapter, halmac_offset);

	return value32.dword;
}

/**
 * halmac_reg_write_32_usb_88xx() - write 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_32_usb_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u32 halmac_data)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	PLATFORM_REG_WRITE_32(driver_adapter, halmac_offset, halmac_data);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_set_bulkout_num_usb_88xx() - inform bulk-out num
 * @halmac_adapter : the adapter of halmac
 * @bulkout_num : usb bulk-out number
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_set_bulkout_num_88xx(struct halmac_adapter *halmac_adapter,
			    u8 bulkout_num)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_SET_BULKOUT_NUM);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	halmac_adapter->halmac_bulkout_num = bulkout_num;

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_usb_bulkout_id_usb_88xx() - get bulk out id for the TX packet
 * @halmac_adapter : the adapter of halmac
 * @halmac_buf : tx packet, include txdesc
 * @halmac_size : tx packet size
 * @bulkout_id : usb bulk-out id
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_usb_bulkout_id_88xx(struct halmac_adapter *halmac_adapter,
			       u8 *halmac_buf, u32 halmac_size, u8 *bulkout_id)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_queue_select queue_sel;
	enum halmac_dma_mapping dma_mapping;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_GET_USB_BULKOUT_ID);

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
		dma_mapping = HALMAC_DMA_MAPPING_HIGH;
		break;
	default:
		pr_err("Qsel is out of range\n");
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
		pr_err("DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_tx_agg_align_usb_88xx() -config sdio bus tx agg alignment
 * @halmac_adapter : the adapter of halmac
 * @enable : function enable(1)/disable(0)
 * @align_size : sdio bus tx agg alignment size (2^n, n = 3~11)
 * Author : Soar Tu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_cfg_tx_agg_align_usb_not_support_88xx(
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
