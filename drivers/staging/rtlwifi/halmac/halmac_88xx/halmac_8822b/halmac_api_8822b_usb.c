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
#include "../halmac_88xx_cfg.h"
#include "halmac_8822b_cfg.h"

/**
 * halmac_mac_power_switch_8822b_usb() - switch mac power
 * @halmac_adapter : the adapter of halmac
 * @halmac_power : power state
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mac_power_switch_8822b_usb(struct halmac_adapter *halmac_adapter,
				  enum halmac_mac_power halmac_power)
{
	u8 interface_mask;
	u8 value8;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_MAC_POWER_SWITCH);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
		"halmac_mac_power_switch_88xx_usb halmac_power = %x ==========>\n",
		halmac_power);

	interface_mask = HALMAC_PWR_INTF_USB_MSK;

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CR);
	if (value8 == 0xEA) {
		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
	} else {
		if (BIT(0) ==
		    (HALMAC_REG_READ_8(halmac_adapter, REG_SYS_STATUS1 + 1) &
		     BIT(0)))
			halmac_adapter->halmac_state.mac_power =
				HALMAC_MAC_POWER_OFF;
		else
			halmac_adapter->halmac_state.mac_power =
				HALMAC_MAC_POWER_ON;
	}

	/*Check if power switch is needed*/
	if (halmac_power == HALMAC_MAC_POWER_ON &&
	    halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_ON) {
		HALMAC_RT_TRACE(
			driver_adapter, HALMAC_MSG_PWR, DBG_WARNING,
			"halmac_mac_power_switch power state unchange!\n");
		return HALMAC_RET_PWR_UNCHANGE;
	}
	if (halmac_power == HALMAC_MAC_POWER_OFF) {
		if (halmac_pwr_seq_parser_88xx(
			    halmac_adapter, HALMAC_PWR_CUT_ALL_MSK,
			    HALMAC_PWR_FAB_TSMC_MSK, interface_mask,
			    halmac_8822b_card_disable_flow) !=
		    HALMAC_RET_SUCCESS) {
			pr_err("Handle power off cmd error\n");
			return HALMAC_RET_POWER_OFF_FAIL;
		}

		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
		halmac_adapter->halmac_state.ps_state =
			HALMAC_PS_STATE_UNDEFINE;
		halmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;
		halmac_init_adapter_dynamic_para_88xx(halmac_adapter);
	} else {
		if (halmac_pwr_seq_parser_88xx(
			    halmac_adapter, HALMAC_PWR_CUT_ALL_MSK,
			    HALMAC_PWR_FAB_TSMC_MSK, interface_mask,
			    halmac_8822b_card_enable_flow) !=
		    HALMAC_RET_SUCCESS) {
			pr_err("Handle power on cmd error\n");
			return HALMAC_RET_POWER_ON_FAIL;
		}

		HALMAC_REG_WRITE_8(
			halmac_adapter, REG_SYS_STATUS1 + 1,
			HALMAC_REG_READ_8(halmac_adapter, REG_SYS_STATUS1 + 1) &
				~(BIT(0)));

		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;
		halmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_mac_power_switch_88xx_usb <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_phy_cfg_8822b_usb() - phy config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_phy_cfg_8822b_usb(struct halmac_adapter *halmac_adapter,
			 enum halmac_intf_phy_platform platform)
{
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_PHY_CFG);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_phy_cfg ==========>\n");

	status = halmac_parse_intf_phy_88xx(halmac_adapter,
					    HALMAC_RTL8822B_USB2_PHY, platform,
					    HAL_INTF_PHY_USB2);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	status = halmac_parse_intf_phy_88xx(halmac_adapter,
					    HALMAC_RTL8822B_USB3_PHY, platform,
					    HAL_INTF_PHY_USB3);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_phy_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_interface_integration_tuning_8822b_usb() - usb interface fine tuning
 * @halmac_adapter : the adapter of halmac
 * Author : Ivan
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_interface_integration_tuning_8822b_usb(
	struct halmac_adapter *halmac_adapter)
{
	return HALMAC_RET_SUCCESS;
}
