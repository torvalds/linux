/******************************************************************************
 *
 * Copyright(c) 2015 - 2019 Realtek Corporation.
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
 *****************************************************************************/
#define _RTL8822BS_HALINIT_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../../hal_halmac.h"	/* rtw_halmac_query_tx_page_num() */
#include "../rtl8822b.h"	/* rtl8822b_hal_init(), rtl8822b_phy_init_haldm() and etc. */


#ifdef CONFIG_FWLPS_IN_IPS
static u8 fw_ips_leave(struct _ADAPTER *a)
{
	struct sreset_priv *psrtpriv = &GET_HAL_DATA(a)->srestpriv;
	struct debug_priv *pdbgpriv = &adapter_to_dvobj(a)->drv_dbg;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(a);
	systime start_time;
	u8 cpwm_orig, cpwm_now, rpwm;
	u8 bMacPwrCtrlOn = _TRUE;


	if ((pwrctl->bips_processing == _FALSE)
	    || (psrtpriv->silent_reset_inprogress == _TRUE)
	    || (GET_HAL_DATA(a)->bFWReady == _FALSE)
	    || (pwrctl->pre_ips_type != 0))
		return _FAIL;

	RTW_INFO("%s: Leaving FW_IPS\n", __func__);

	/* for polling cpwm */
	cpwm_orig = 0;
	rtw_hal_get_hwreg(a, HW_VAR_CPWM, &cpwm_orig);

	/* set rpwm */
#if 1
	rtw_hal_get_hwreg(a, HW_VAR_RPWM_TOG, &rpwm);
	rpwm += 0x80;
#else
	rpwm = pwrctl->tog;
#endif
	rpwm |= PS_ACK;
	rtw_hal_set_hwreg(a, HW_VAR_SET_RPWM, (u8 *)(&rpwm));
	RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, rpwm);

	pwrctl->tog = (rpwm + 0x80) & 0x80;

	/* do polling cpwm */
	start_time = rtw_get_current_time();
	do {
		rtw_mdelay_os(1);

		rtw_hal_get_hwreg(a, HW_VAR_CPWM, &cpwm_now);
		if ((cpwm_orig ^ cpwm_now) & 0x80) {
#ifdef DBG_CHECK_FW_PS_STATE
			RTW_INFO("%s: polling cpwm ok when leaving IPS in FWLPS state,"
				 " cost %d ms,"
				 " cpwm_orig=0x%02x, cpwm_now=0x%02x, 0x100=0x%x\n",
				 __FUNCTION__,
				 rtw_get_passing_time_ms(start_time),
				 cpwm_orig, cpwm_now, rtw_read8(a, REG_CR_8822B));
#endif /* DBG_CHECK_FW_PS_STATE */
			break;
		}

		if (rtw_get_passing_time_ms(start_time) > 100) {
			RTW_ERR("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __FUNCTION__);
			break;
		}
	} while (1);

	rtl8822b_set_FwPwrModeInIPS_cmd(a, 0);

	rtw_hal_set_hwreg(a, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(a) == _FAIL) {
		RTW_INFO("after hal init, fw ps state in 32k\n");
		pdbgpriv->dbg_ips_drvopen_fail_cnt++;
	}
#endif /* DBG_CHECK_FW_PS_STATE */

	return _SUCCESS;
}

static u8 fw_ips_enter(struct _ADAPTER *a)
{
	struct sreset_priv *psrtpriv = &GET_HAL_DATA(a)->srestpriv;
	struct debug_priv *pdbgpriv = &adapter_to_dvobj(a)->drv_dbg;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(a);
	systime start_time;
	int cnt = 0;
	u8 val8 = 0, rpwm;


	if ((pwrctl->bips_processing == _FALSE)
	    || (psrtpriv->silent_reset_inprogress == _TRUE)
	    || (GET_HAL_DATA(a)->bFWReady == _FALSE)
	    || (a->netif_up == _FALSE)) {
		pdbgpriv->dbg_carddisable_cnt++;
		pwrctl->pre_ips_type = 1;

		return _FAIL;
	}

	RTW_INFO("%s: issue H2C to FW when entering IPS\n", __FUNCTION__);
	rtl8822b_set_FwPwrModeInIPS_cmd(a, 0x1);

	/*
	 * poll 0x1cc to make sure H2C command already finished by FW;
	 * MAC_0x1cc=0 means H2C done by FW.
	 */
	start_time = rtw_get_current_time();
	do {
		rtw_mdelay_os(10);
		val8 = rtw_read8(a, REG_HMETFR_8822B);
		cnt++;
		if (!val8)
			break;

		if (rtw_get_passing_time_ms(start_time) > 100) {
			RTW_ERR("%s: fail to wait H2C, REG_HMETFR=0x%x, cnt=%d\n",
				__FUNCTION__, val8, cnt);
#ifdef DBG_CHECK_FW_PS_STATE
			RTW_WARN("MAC_1C0=0x%08x, MAC_1C4=0x%08x, MAC_1C8=0x%08x, MAC_1CC=0x%08x\n",
				 rtw_read32(a, 0x1c0), rtw_read32(a, 0x1c4),
				 rtw_read32(a, 0x1c8), rtw_read32(a, REG_HMETFR_8822B));
#endif /* DBG_CHECK_FW_PS_STATE */
			goto exit;
		}
	} while (1);

	/* H2C done, enter 32k */
	/* set rpwm to enter 32k */
#if 1
	rtw_hal_get_hwreg(a, HW_VAR_RPWM_TOG, &rpwm);
	rpwm += 0x80;
#else
	rpwm = pwrctl->tog;
#endif
	rpwm |= PS_STATE_S0;
	rtw_hal_set_hwreg(a, HW_VAR_SET_RPWM, &rpwm);
	RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, rpwm);
	pwrctl->tog = (rpwm + 0x80) & 0x80;

	cnt = val8 = 0;
	start_time = rtw_get_current_time();
	do {
		val8 = rtw_read8(a, REG_CR_8822B);
		cnt++;
		RTW_INFO("%s: polling 0x100=0x%x, cnt=%d\n",
			 __FUNCTION__, val8, cnt);
		if (val8 == 0xEA) {
			RTW_INFO("%s: polling 0x100=0xEA, cnt=%d, cost %d ms\n",
				 __FUNCTION__, cnt,
				 rtw_get_passing_time_ms(start_time));
			break;
		}

		if (rtw_get_passing_time_ms(start_time) > 100) {
			RTW_ERR("%s: polling polling 0x100=0xEA timeout! cnt=%d\n",
				__FUNCTION__, cnt);
#ifdef DBG_CHECK_FW_PS_STATE
			RTW_WARN("MAC_1C0=0x%08x, MAC_1C4=0x%08x, MAC_1C8=0x%08x, MAC_1CC=0x%08x\n",
				 rtw_read32(a, 0x1c0), rtw_read32(a, 0x1c4),
				 rtw_read32(a, 0x1c8), rtw_read32(a, REG_HMETFR_8822B));
#endif /* DBG_CHECK_FW_PS_STATE */
			break;
		}

		rtw_mdelay_os(10);
	} while (1);

exit:
	RTW_INFO("polling done when entering IPS, check result: 0x100=0x%02x, cnt=%d, MAC_1cc=0x%02x\n",
		 rtw_read8(a, REG_CR_8822B), cnt, rtw_read8(a, REG_HMETFR_8822B));

	pwrctl->pre_ips_type = 0;

	return _SUCCESS;
}
#endif /* CONFIG_FWLPS_IN_IPS */

u32 rtl8822bs_init(PADAPTER adapter)
{
	u8 ok = _TRUE;
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

#ifdef CONFIG_FWLPS_IN_IPS
	if (fw_ips_leave(adapter) == _SUCCESS)
		return _SUCCESS;
#endif
	ok = rtl8822b_hal_init(adapter);
	if (_FALSE == ok)
		return _FAIL;

	rtw_halmac_query_tx_page_num(adapter_to_dvobj(adapter));

	rtl8822b_mac_verify(adapter);

	rtl8822b_phy_init_haldm(adapter);
#ifdef CONFIG_BEAMFORMING
	rtl8822b_phy_bf_init(adapter);
#endif

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	/*HW /FW init*/
	rtw_hal_set_default_port_id_cmd(adapter, 0);
#endif

#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	if (hal->EEPROMBluetoothCoexist == _TRUE) {
		rtw_btcoex_HAL_Initialize(adapter, _FALSE);
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		rtw_hal_set_wifi_btc_port_id_cmd(adapter);
		#endif
	} else
#endif /* CONFIG_BT_COEXIST */
		rtw_btcoex_wifionly_hw_config(adapter);

	rtl8822b_init_misc(adapter);

	return _SUCCESS;
}

u32 rtl8822bs_deinit(PADAPTER adapter)
{
#ifdef CONFIG_FWLPS_IN_IPS
	if (fw_ips_enter(adapter) == _SUCCESS)
		return _SUCCESS;
#endif

	return rtl8822b_deinit(adapter);
}

void rtl8822bs_init_default_value(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	rtl8822b_init_default_value(adapter);

	/* interface related variable */
	hal->SdioRxFIFOCnt = 0;
}
