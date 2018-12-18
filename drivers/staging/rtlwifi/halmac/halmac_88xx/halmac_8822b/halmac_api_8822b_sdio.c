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
#include "halmac_8822b_cfg.h"

/**
 * halmac_mac_power_switch_8822b_sdio() - switch mac power
 * @halmac_adapter : the adapter of halmac
 * @halmac_power : power state
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_mac_power_switch_8822b_sdio(struct halmac_adapter *halmac_adapter,
				   enum halmac_mac_power halmac_power)
{
	u8 interface_mask;
	u8 value8;
	u8 rpwm;
	u32 imr_backup;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	if (halmac_adapter_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ADAPTER_INVALID;

	if (halmac_api_validate(halmac_adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_API_INVALID;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
		"[TRACE]halmac_mac_power_switch_88xx_sdio==========>\n");
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"[TRACE]halmac_power = %x ==========>\n", halmac_power);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"[TRACE]8822B pwr seq ver = %s\n",
			HALMAC_8822B_PWR_SEQ_VER);

	interface_mask = HALMAC_PWR_INTF_SDIO_MSK;

	halmac_adapter->rpwm_record =
		HALMAC_REG_READ_8(halmac_adapter, REG_SDIO_HRPWM1);

	/* Check FW still exist or not */
	if (HALMAC_REG_READ_16(halmac_adapter, REG_MCUFW_CTRL) == 0xC078) {
		/* Leave 32K */
		rpwm = (u8)((halmac_adapter->rpwm_record ^ BIT(7)) & 0x80);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SDIO_HRPWM1, rpwm);
	}

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CR);
	if (value8 == 0xEA)
		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
	else
		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;

	/*Check if power switch is needed*/
	if (halmac_power == HALMAC_MAC_POWER_ON &&
	    halmac_adapter->halmac_state.mac_power == HALMAC_MAC_POWER_ON) {
		HALMAC_RT_TRACE(
			driver_adapter, HALMAC_MSG_PWR, DBG_WARNING,
			"[WARN]halmac_mac_power_switch power state unchange!\n");
		return HALMAC_RET_PWR_UNCHANGE;
	}

	imr_backup = HALMAC_REG_READ_32(halmac_adapter, REG_SDIO_HIMR);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_HIMR, 0);

	if (halmac_power == HALMAC_MAC_POWER_OFF) {
		if (halmac_pwr_seq_parser_88xx(
			    halmac_adapter, HALMAC_PWR_CUT_ALL_MSK,
			    HALMAC_PWR_FAB_TSMC_MSK, interface_mask,
			    halmac_8822b_card_disable_flow) !=
		    HALMAC_RET_SUCCESS) {
			pr_err("[ERR]Handle power off cmd error\n");
			HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_HIMR,
					    imr_backup);
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
			pr_err("[ERR]Handle power on cmd error\n");
			HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_HIMR,
					    imr_backup);
			return HALMAC_RET_POWER_ON_FAIL;
		}

		halmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;
		halmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;
	}

	HALMAC_REG_WRITE_32(halmac_adapter, REG_SDIO_HIMR, imr_backup);

	HALMAC_RT_TRACE(
		driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
		"[TRACE]halmac_mac_power_switch_88xx_sdio <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_phy_cfg_8822b_sdio() - phy config
 * @halmac_adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_phy_cfg_8822b_sdio(struct halmac_adapter *halmac_adapter,
			  enum halmac_intf_phy_platform platform)
{
	void *driver_adapter = NULL;
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

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"sdio no phy\n");

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"halmac_phy_cfg <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_interface_integration_tuning_8822b_sdio() - sdio interface fine tuning
 * @halmac_adapter : the adapter of halmac
 * Author : Ivan
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status halmac_interface_integration_tuning_8822b_sdio(
	struct halmac_adapter *halmac_adapter)
{
	return HALMAC_RET_SUCCESS;
}
