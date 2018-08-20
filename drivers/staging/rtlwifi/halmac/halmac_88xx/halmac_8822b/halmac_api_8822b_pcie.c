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
#include "../halmac_88xx_cfg.h"
#include "../halmac_api_88xx_pcie.h"
#include "halmac_8822b_cfg.h"

/**
 * halmac_mac_power_switch_8822b_pcie() - switch mac power
 * @halmac_adapter : the adapter of halmac
 * @halmac_power : power state
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mac_power_switch_8822b_pcie(struct halmac_adapter *halmac_adapter,
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
		"halmac_mac_power_switch_88xx_pcie halmac_power =  %x ==========>\n",
		halmac_power);
	interface_mask = HALMAC_PWR_INTF_PCI_MSK;

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CR);
	if (value8 == 0xEA)
		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
	else
		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;

	/* Check if power switch is needed */
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

		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;
		halmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_mac_power_switch_88xx_pcie <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pcie_switch_8822b() - pcie gen1/gen2 switch
 * @halmac_adapter : the adapter of halmac
 * @pcie_cfg : gen1/gen2 selection
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_pcie_switch_8822b(struct halmac_adapter *halmac_adapter,
			 enum halmac_pcie_cfg pcie_cfg)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u8 current_link_speed = 0;
	u32 count = 0;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_PCIE_SWITCH);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	/* Link Control 2 Register[3:0] Target Link Speed
	 * Defined encodings are:
	 * 0001b Target Link 2.5 GT/s
	 * 0010b Target Link 5.0 GT/s
	 * 0100b Target Link 8.0 GT/s
	 */

	if (pcie_cfg == HALMAC_PCIE_GEN1) {
		/* cfg 0xA0[3:0]=4'b0001 */
		halmac_dbi_write8_88xx(
			halmac_adapter, LINK_CTRL2_REG_OFFSET,
			(halmac_dbi_read8_88xx(halmac_adapter,
					       LINK_CTRL2_REG_OFFSET) &
			 0xF0) | BIT(0));

		/* cfg 0x80C[17]=1 //PCIe DesignWave */
		halmac_dbi_write32_88xx(
			halmac_adapter, GEN2_CTRL_OFFSET,
			halmac_dbi_read32_88xx(halmac_adapter,
					       GEN2_CTRL_OFFSET) |
				BIT(17));

		/* check link speed if GEN1 */
		/* cfg 0x82[3:0]=4'b0001 */
		current_link_speed =
			halmac_dbi_read8_88xx(halmac_adapter,
					      LINK_STATUS_REG_OFFSET) &
			0x0F;
		count = 2000;

		while (current_link_speed != GEN1_SPEED && count != 0) {
			usleep_range(50, 60);
			current_link_speed =
				halmac_dbi_read8_88xx(halmac_adapter,
						      LINK_STATUS_REG_OFFSET) &
				0x0F;
			count--;
		}

		if (current_link_speed != GEN1_SPEED) {
			pr_err("Speed change to GEN1 fail !\n");
			return HALMAC_RET_FAIL;
		}

	} else if (pcie_cfg == HALMAC_PCIE_GEN2) {
		/* cfg 0xA0[3:0]=4'b0010 */
		halmac_dbi_write8_88xx(
			halmac_adapter, LINK_CTRL2_REG_OFFSET,
			(halmac_dbi_read8_88xx(halmac_adapter,
					       LINK_CTRL2_REG_OFFSET) &
			 0xF0) | BIT(1));

		/* cfg 0x80C[17]=1 //PCIe DesignWave */
		halmac_dbi_write32_88xx(
			halmac_adapter, GEN2_CTRL_OFFSET,
			halmac_dbi_read32_88xx(halmac_adapter,
					       GEN2_CTRL_OFFSET) |
				BIT(17));

		/* check link speed if GEN2 */
		/* cfg 0x82[3:0]=4'b0010 */
		current_link_speed =
			halmac_dbi_read8_88xx(halmac_adapter,
					      LINK_STATUS_REG_OFFSET) &
			0x0F;
		count = 2000;

		while (current_link_speed != GEN2_SPEED && count != 0) {
			usleep_range(50, 60);
			current_link_speed =
				halmac_dbi_read8_88xx(halmac_adapter,
						      LINK_STATUS_REG_OFFSET) &
				0x0F;
			count--;
		}

		if (current_link_speed != GEN2_SPEED) {
			pr_err("Speed change to GEN1 fail !\n");
			return HALMAC_RET_FAIL;
		}

	} else {
		pr_err("Error Speed !\n");
		return HALMAC_RET_FAIL;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_pcie_switch_8822b_nc(struct halmac_adapter *halmac_adapter,
			    enum halmac_pcie_cfg pcie_cfg)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(halmac_adapter, HALMAC_API_PCIE_SWITCH);

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s ==========>\n", __func__);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_phy_cfg_8822b_pcie() - phy config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_phy_cfg_8822b_pcie(struct halmac_adapter *halmac_adapter,
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
					    HALMAC_RTL8822B_PCIE_PHY_GEN1,
					    platform, HAL_INTF_PHY_PCIE_GEN1);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	status = halmac_parse_intf_phy_88xx(halmac_adapter,
					    HALMAC_RTL8822B_PCIE_PHY_GEN2,
					    platform, HAL_INTF_PHY_PCIE_GEN2);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_phy_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_interface_integration_tuning_8822b_pcie() - pcie interface fine tuning
 * @halmac_adapter : the adapter of halmac
 * Author : Rick Liu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_interface_integration_tuning_8822b_pcie(
	struct halmac_adapter *halmac_adapter)
{
	return HALMAC_RET_SUCCESS;
}
