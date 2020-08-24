/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#define _RTL8821CS_HALINIT_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../rtl8821c.h"		/* rtl8821c_mac_init(), rtl8821c_phy_init() and etc. */
#include "../../hal_halmac.h"	/* rtw_halmac_get_oqt_size*/

/*
#define HALMAC_NORMAL_HPQ_PGNUM_8821C	16
#define HALMAC_NORMAL_NPQ_PGNUM_8821C	16
#define HALMAC_NORMAL_LPQ_PGNUM_8821C	16
#define HALMAC_NORMAL_EXPQ_PGNUM_8821C	14
#define HALMAC_NORMAL_GAP_PGNUM_8821C	1

	case HALMAC_TRX_MODE_NORMAL:
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_LOW;
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_EXTRA;
	pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;

*/

static void rtl8821cs_init_xmit_info(_adapter *adapter)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	u32 page_size;

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	hal_data->tx_high_page = rtw_read16(adapter, REG_FIFOPAGE_INFO_1) & 0xFFF;
	hal_data->tx_low_page = rtw_read16(adapter, REG_FIFOPAGE_INFO_2) & 0xFFF;
	hal_data->tx_normal_page = rtw_read16(adapter, REG_FIFOPAGE_INFO_3) & 0xFFF;
	hal_data->tx_extra_page = rtw_read16(adapter, REG_FIFOPAGE_INFO_4) & 0xFFF;
	hal_data->tx_pub_page = rtw_read16(adapter, REG_FIFOPAGE_INFO_5) & 0xFFF;

#ifdef DBG_DUMP_RQPN
	{
	u32 tx_fifo_size;

	rtw_halmac_get_tx_fifo_size(adapter_to_dvobj(adapter), &tx_fifo_size);
	RTW_INFO("%s => High Pages:%d, LOW Pages:%d, Normal Pages:%d, Extra Pages:%d, Pub Pages:%d, Total Pages:%d\n"
		 , __func__
		 , hal_data->tx_high_page
		 , hal_data->tx_low_page
		 , hal_data->tx_normal_page
		 , hal_data->tx_extra_page
		 , hal_data->tx_pub_page
		 , (tx_fifo_size / page_size));
	}
#endif

	hal_data->max_xmit_page = PageNum(MAX_XMITBUF_SZ, page_size);

	if (adapter->registrypriv.wifi_spec) {
		/*HALMAC_TRX_MODE_WMM - VO - HQ , VI - NQ , BE - LQ , BK - NQ , MG - HQ , HI -HQ*/
		hal_data->max_xmit_page_vo = hal_data->tx_high_page + hal_data->tx_pub_page;
		hal_data->max_xmit_page_vi = (hal_data->tx_normal_page + hal_data->tx_pub_page) >> 1;
		hal_data->max_xmit_page_be = hal_data->tx_low_page + hal_data->tx_pub_page;
		hal_data->max_xmit_page_bk = (hal_data->tx_normal_page + hal_data->tx_pub_page) >> 1;
	} else {
		/*HALMAC_TRX_MODE_NORMAL - VO - NQ , VI - NQ , BE - LQ , BK - LQ , MG - EXQ , HI -HQ*/
		#ifdef XMIT_BUF_SIZE
		hal_data->max_xmit_size_vovi = ((hal_data->tx_normal_page + hal_data->tx_pub_page) >> 1) * page_size;
		hal_data->max_xmit_size_bebk = ((hal_data->tx_low_page + hal_data->tx_pub_page) >> 1) * page_size;
		#endif

		hal_data->max_xmit_page_vo = (hal_data->tx_normal_page + hal_data->tx_pub_page) >> 1;
		hal_data->max_xmit_page_vi = (hal_data->tx_normal_page + hal_data->tx_pub_page) >> 1;
		hal_data->max_xmit_page_be = (hal_data->tx_low_page + hal_data->tx_pub_page) >> 1;
		hal_data->max_xmit_page_bk = (hal_data->tx_low_page + hal_data->tx_pub_page) >> 1;
	}

#ifdef DBG_DUMP_RQPN
	#ifdef XMIT_BUF_SIZE
	RTW_INFO("%s => max_xmit_size_vovi:%d, max_xmit_size_bebk:%d\n", __func__, hal_data->max_xmit_size_vovi, hal_data->max_xmit_size_bebk);
	#endif

	RTW_INFO("%s => VO max_xmit_page:%d, VI max_xmit_page:%d\n", __func__, hal_data->max_xmit_page_vo, hal_data->max_xmit_page_vi);
	RTW_INFO("%s => BE max_xmit_page:%d, BK max_xmit_page:%d\n", __func__, hal_data->max_xmit_page_be, hal_data->max_xmit_page_bk);
#endif

	if (rtw_halmac_get_oqt_size(adapter_to_dvobj(adapter), &hal_data->max_oqt_size)) {
		RTW_WARN("%s: Fail to get Max OQT size! use default max-size : 32\n", __func__);
		hal_data->max_oqt_size = 32;
	}
}

#ifdef CONFIG_FWLPS_IN_IPS
u8 rtl8821cs_fw_ips_init(_adapter *padapter)
{
	struct sreset_priv *psrtpriv = &GET_HAL_DATA(padapter)->srestpriv;
	struct debug_priv *pdbgpriv = &adapter_to_dvobj(padapter)->drv_dbg;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	if (pwrctl->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE
		&& GET_HAL_DATA(padapter)->bFWReady == _TRUE && pwrctl->pre_ips_type == 0) {
		systime start_time;
		u8 cpwm_orig, cpwm_now, rpwm;
		u8 bMacPwrCtrlOn = _TRUE;

		RTW_INFO("%s: Leaving FW_IPS\n", __func__);

		/* for polling cpwm */
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		/* set rpwm */
		rtw_hal_get_hwreg(padapter, HW_VAR_RPWM_TOG, &rpwm);
		rpwm += 0x80;
		rpwm |= PS_ACK;
		rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));


		RTW_INFO("%s: write rpwm=%02x\n", __func__, rpwm);

		pwrctl->tog = (rpwm + 0x80) & 0x80;

		/* do polling cpwm */
		start_time = rtw_get_current_time();
		do {

			rtw_mdelay_os(1);

			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80) {
				#ifdef DBG_CHECK_FW_PS_STATE
				RTW_INFO("%s: polling cpwm ok when leaving IPS in FWLPS state, cpwm_orig=%02x, cpwm_now=%02x, 0x100=0x%x\n"
					, __func__, cpwm_orig, cpwm_now, rtw_read8(padapter, REG_CR));
				#endif /* DBG_CHECK_FW_PS_STATE */
				break;
			}

			if (rtw_get_passing_time_ms(start_time) > 100) {
				RTW_INFO("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __func__);
				break;
			}
		} while (1);

		rtl8821c_set_FwPwrModeInIPS_cmd(padapter, 0);

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

		#ifdef DBG_CHECK_FW_PS_STATE
		if (rtw_fw_ps_state(padapter) == _FAIL) {
			RTW_INFO("after hal init, fw ps state in 32k\n");
			pdbgpriv->dbg_ips_drvopen_fail_cnt++;
		}
		#endif /* DBG_CHECK_FW_PS_STATE */
		return _SUCCESS;
	}
	return _FAIL;
}

u8 rtl8821cs_fw_ips_deinit(_adapter *padapter)
{
	struct sreset_priv *psrtpriv =  &GET_HAL_DATA(padapter)->srestpriv;
	struct debug_priv *pdbgpriv = &adapter_to_dvobj(padapter)->drv_dbg;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	if (pwrctl->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE
		&& GET_HAL_DATA(padapter)->bFWReady == _TRUE && padapter->netif_up == _TRUE) {
		int cnt = 0;
		u8 val8 = 0, rpwm;

		RTW_INFO("%s: issue H2C to FW when entering IPS\n", __func__);

		rtl8821c_set_FwPwrModeInIPS_cmd(padapter, 0x1);
		/* poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc=0 means H2C done by FW. */
		do {
			val8 = rtw_read8(padapter, REG_HMETFR);
			cnt++;
			RTW_INFO("%s  polling REG_HMETFR=0x%x, cnt=%d\n", __func__, val8, cnt);
			rtw_mdelay_os(10);
		} while (cnt < 100 && (val8 != 0));

		/* H2C done, enter 32k */
		if (val8 == 0) {
			/* set rpwm to enter 32k */
			rtw_hal_get_hwreg(padapter, HW_VAR_RPWM_TOG, &rpwm);
			rpwm += 0x80;
			rpwm |= BIT_SYS_CLK_8821C;
			rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));
			RTW_INFO("%s: write rpwm=%02x\n", __func__, rpwm);
			pwrctl->tog = (val8 + 0x80) & 0x80;

			cnt = val8 = 0;
			do {
				val8 = rtw_read8(padapter, REG_CR);
				cnt++;
				RTW_INFO("%s  polling 0x100=0x%x, cnt=%d\n", __func__, val8, cnt);
				rtw_mdelay_os(10);
			} while (cnt < 100 && (val8 != 0xEA));

			#ifdef DBG_CHECK_FW_PS_STATE
			if (val8 != 0xEA)
				RTW_INFO("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n"
					, rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
					, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, REG_HMETFR));
			#endif /* DBG_CHECK_FW_PS_STATE */
		} else {
			RTW_INFO("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n"
				, rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
				, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, REG_HMETFR));
		}

		RTW_INFO("polling done when entering IPS, check result : 0x100=0x%x, cnt=%d, MAC_1cc=0x%02x\n"
			, rtw_read8(padapter, REG_CR), cnt, rtw_read8(padapter, REG_HMETFR));

		pwrctl->pre_ips_type = 0;

		return _SUCCESS;
	}

	pdbgpriv->dbg_carddisable_cnt++;
	pwrctl->pre_ips_type = 1;

	return _FAIL;

}
#endif /*CONFIG_FWLPS_IN_IPS*/

#define MAX_AMPDU_NUMBER		0x1212 /*MAX AMPDU Number = 18*/
static void rtl8821cs_ampdu_num_cfg(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PSDIO_DATA psdio_data = &dvobj->intf_data;
	u16 ampdu_num = MAX_AMPDU_NUMBER;

	if (psdio_data->clock < RTW_SDIO_CLK_80M)
		ampdu_num = 0x1F1F;

	rtw_write16(adapter, REG_PROT_MODE_CTRL_8821C + 2, ampdu_num);
}

static void rtl8821cs_init_misc(_adapter *adapter)
{
	#ifdef DBG_DL_FW_MEM
	rtw_write8(adapter, 0xf6, 0x01);
	rtw_write8(adapter, 0x3a, 0x28);
	#endif

	#ifdef CONFIG_BT_WAKE_HST_OPEN_DRAIN
	/*PAD Type control select for GPIO14 (DEV_WAKE_HST) in USB or SDIO interface
	0: Push-Pull , 1: Open-Drain*/
	rtw_write32(adapter, REG_WL_BT_PWR_CTRL,
		rtw_read32(adapter, REG_WL_BT_PWR_CTRL) | BIT_DEVWAKE_PAD_TYPE_SEL);
	#endif

	rtl8821cs_ampdu_num_cfg(adapter);
}

u32 rtl8821cs_hal_init(PADAPTER adapter)
{
#ifdef CONFIG_FWLPS_IN_IPS
	if (_SUCCESS == rtl8821cs_fw_ips_init(adapter))
		return _SUCCESS;
#endif
	if (_FALSE == rtl8821c_hal_init(adapter))
		return _FAIL;

	rtl8821cs_init_xmit_info(adapter);
	rtl8821cs_init_misc(adapter);

	return _SUCCESS;
}
u32 rtl8821cs_hal_deinit(PADAPTER adapter)
{
#ifdef CONFIG_FWLPS_IN_IPS
	if (_SUCCESS == rtl8821cs_fw_ips_deinit(adapter))
		return _SUCCESS;
#endif

	return rtl8821c_hal_deinit(adapter);
}

void rtl8821cs_init_default_value(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	rtl8821c_init_default_value(adapter);

	/* interface related variable */
	hal->SdioRxFIFOCnt = 0;
}
