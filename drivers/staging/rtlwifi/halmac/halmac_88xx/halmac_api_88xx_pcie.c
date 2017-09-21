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
 * halmac_init_pcie_cfg_88xx() -  init PCIe
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_pcie_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_INIT_PCIE_CFG);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_pcie_cfg_88xx() - deinit PCIE
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_deinit_pcie_cfg_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_DEINIT_PCIE_CFG);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_cfg_rx_aggregation_88xx_pcie() - config rx aggregation
 * @halmac_adapter : the adapter of halmac
 * @halmac_rx_agg_mode
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_pcie(struct halmac_adapter *halmac_adapter,
				    struct halmac_rxagg_cfg *phalmac_rxagg_cfg)
{
	void *driver_adapter = NULL;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter,
				  HALMAC_API_CFG_RX_AGGREGATION);

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_pcie_88xx() - read 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8 halmac_reg_read_8_pcie_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	return PLATFORM_REG_READ_8(driver_adapter, halmac_offset);
}

/**
 * halmac_reg_write_8_pcie_88xx() - write 1byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_8_pcie_88xx(struct halmac_adapter *halmac_adapter,
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
 * halmac_reg_read_16_pcie_88xx() - read 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u16 halmac_reg_read_16_pcie_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	return PLATFORM_REG_READ_16(driver_adapter, halmac_offset);
}

/**
 * halmac_reg_write_16_pcie_88xx() - write 2byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_16_pcie_88xx(struct halmac_adapter *halmac_adapter,
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
 * halmac_reg_read_32_pcie_88xx() - read 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32 halmac_reg_read_32_pcie_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	return PLATFORM_REG_READ_32(driver_adapter, halmac_offset);
}

/**
 * halmac_reg_write_32_pcie_88xx() - write 4byte register
 * @halmac_adapter : the adapter of halmac
 * @halmac_offset : register offset
 * @halmac_data : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_reg_write_32_pcie_88xx(struct halmac_adapter *halmac_adapter,
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
 * halmac_cfg_tx_agg_align_pcie_88xx() -config sdio bus tx agg alignment
 * @halmac_adapter : the adapter of halmac
 * @enable : function enable(1)/disable(0)
 * @align_size : sdio bus tx agg alignment size (2^n, n = 3~11)
 * Author : Soar Tu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_cfg_tx_agg_align_pcie_not_support_88xx(
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
