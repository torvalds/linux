#include "../halmac_88xx_cfg.h"
#include "halmac_8822b_cfg.h"

/**
 * halmac_mac_power_switch_8822b_usb() - change mac power
 * @pHalmac_adapter
 * @halmac_power
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mac_power_switch_8822b_usb(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_MAC_POWER	halmac_power
)
{
	u8 interface_mask;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MAC_POWER_SWITCH);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_mac_power_switch_88xx_usb halmac_power = %x ==========>\n", halmac_power);

	interface_mask = HALMAC_PWR_INTF_USB_MSK;

	if (0xEA == HALMAC_REG_READ_8(pHalmac_adapter, REG_CR))
		pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;

	/*Check if power switch is needed*/
	if (halmac_power == pHalmac_adapter->halmac_state.mac_power) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_WARN, "halmac_mac_power_switch power state unchange!\n");
	} else {
		if (HALMAC_MAC_POWER_OFF == halmac_power) {
			if (HALMAC_RET_SUCCESS != halmac_pwr_seq_parser_88xx(pHalmac_adapter, HALMAC_PWR_CUT_TESTCHIP_MSK, HALMAC_PWR_FAB_TSMC_MSK,
				    interface_mask, halmac_8822b_card_disable_flow)) {
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "Handle power off cmd error\n");
				return HALMAC_RET_POWER_OFF_FAIL;
			}

			pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
			pHalmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_UNDEFINE;
			pHalmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;
		} else {
			if (HALMAC_RET_SUCCESS != halmac_pwr_seq_parser_88xx(pHalmac_adapter, HALMAC_PWR_CUT_TESTCHIP_MSK, HALMAC_PWR_FAB_TSMC_MSK,
				    interface_mask, halmac_8822b_card_enable_flow)) {
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "Handle power on cmd error\n");
				return HALMAC_RET_POWER_ON_FAIL;
			}

			pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;
			pHalmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;
		}
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_mac_power_switch_88xx_usb <==========\n");

	return HALMAC_RET_SUCCESS;
}
