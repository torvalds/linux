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
#define _RTL8821CS_OPS_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE, GET_HAL_DATA() and etc. */
#include <hal_intf.h>		/* struct hal_ops */
#include "../rtl8821c.h"
#include "rtl8821cs.h"		/* rtl8821cs_hal_init() */
#include "rtl8821cs_xmit.h"
#include "rtl8821cs_recv.h"
#include "rtl8821cs_io.h"
#include "rtl8821cs_led.h"
#include "rtl8821cs_halmac.h"


static void intf_chip_configure(PADAPTER adapter)
{
#if 0
	u8 try_cnt = 0;

	/*adjust SDIO output driving -output delay time about 8ns (SDIO SPEC must last than 7.5ns),offset 0x74[18]*/
	if (rtw_is_sdio30(adapter)) {
		rtw_write8(adapter, REG_SDIO_HSUS_CTRL, rtw_read8(adapter, REG_SDIO_HSUS_CTRL) & ~BIT(0));

		do {
			rtw_mdelay_os(2);
			if ((rtw_read8(adapter, REG_SDIO_HSUS_CTRL) & BIT(1)))
				break;
			try_cnt++;
		} while (try_cnt <= 50);

		if (try_cnt == 10)
			RTW_ERR("%s: SDIO active state change failed!!\n", __func__);

		rtw_write32(adapter, REG_HCI_OPT_CTRL,
			rtw_read32(adapter, REG_HCI_OPT_CTRL) | BIT(18));
	}
#endif
}

u32 rtl8821cs_get_interrupt(PADAPTER adapter)
{
	return rtw_read32(adapter, REG_SDIO_HISR_8821C);
}

void rtl8821cs_clear_interrupt(PADAPTER adapter, u32 hisr)
{
	/* Perform write one clear operation */
	if (hisr)
		rtw_write32(adapter, REG_SDIO_HISR_8821C, hisr);
}

u32 rtl8821cs_get_himr(PADAPTER adapter)
{
	return rtw_read32(adapter, REG_SDIO_HIMR_8821C);
}

void rtl8821cs_update_himr(PADAPTER adapter, u32 himr)
{
	rtw_write32(adapter, REG_SDIO_HIMR_8821C, himr);
}

void rtl8821cs_update_interrupt_mask(PADAPTER padapter,u32 AddMSR, u32 RemoveMSR)
{
	HAL_DATA_TYPE *pHalData;
	pHalData = GET_HAL_DATA(padapter);

	if (AddMSR)
		pHalData->sdio_himr |= AddMSR;

	if (RemoveMSR)
		pHalData->sdio_himr &= (~RemoveMSR);

	rtl8821cs_update_himr(padapter,pHalData->sdio_himr);
}

/*
 * Description:
 *	Initialize SDIO Host Interrupt Mask configuration variables for future use.
 *
 * Assumption:
 *	Using SDIO Local register ONLY for configuration.
 */
void rtl8821cs_init_interrupt(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);
	hal->sdio_himr = (u32)(\
			       BIT_RX_REQUEST_MSK_8821C	|
#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
			       BIT_SDIO_AVAL_MSK_8821C		|
#endif /* CONFIG_SDIO_TX_ENABLE_AVAL_INT */

#ifdef CONFIG_ERROR_STATE_MONITOR
			       BIT_SDIO_TXERR_MSK_8821C	|
			       BIT_SDIO_RXERR_MSK_8821C	|
#endif
#ifdef CONFIG_MONITOR_OVERFLOW
			       BIT_SDIO_TXFOVW_MSK_8821C	|
			       BIT_SDIO_RXFOVW_MSK_8821C	|
#endif
#ifdef CONFIG_INTERRUPT_BASED_TXBCN

				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				BIT_SDIO_TXBCNOK_MSK_8821C	|
				BIT_SDIO_TXBCNERR_MSK_8821C	|
				#endif

				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				BIT_SDIO_BCNERLY_INT_MSK_8821C	|
				#endif
#endif
#if 0
			       BIT_SDIO_C2HCMD_INT_MSK_8821C	|
#endif
#if defined(CONFIG_LPS_LCLK) && !defined(CONFIG_DETECT_CPWM_BY_POLLING)
			       BIT_SDIO_CPWM1_MSK_8821C	|
#if 0
			       BIT_SDIO_CPWM2_MSK_8821C	|
#endif
#endif /* CONFIG_LPS_LCLK && !CONFIG_DETECT_CPWM_BY_POLLING */
#if 0
			       BIT_SDIO_HSISR_IND_MSK_8821C	|
			       BIT_SDIO_GTINT3_MSK_8821C	|
			       BIT_SDIO_GTINT4_MSK_8821C	|
			       BIT_SDIO_PSTIMEOUT_MSK_8821C	|
			       BIT_SDIO_OCPINT_MSK_8821C	|
			       BIT_SDIIO_ATIMend_MSK_8821C	|
			       BIT_SDIO_ATIMend_E_MSK_8821C	|
			       BIT_SDIO_CTWend_MSK_8821C	|
			       BIT_SDIO_CRCERR_MSK_8821C	|
#endif
			       0);

}

/*
 * Description:
 *	Clear corresponding SDIO Host ISR interrupt service.
 *
 * Assumption:
 *	Using SDIO Local register ONLY for configuration.
 */
 #if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
static void clear_interrupt_all(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	if (rtw_is_surprise_removed(adapter))
		return;

	hal = GET_HAL_DATA(adapter);
	rtl8821cs_clear_interrupt(adapter, 0xFFFFFFFF);
}
#endif /*#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)*/
/*
 * Description:
 *	Enalbe SDIO Host Interrupt Mask configuration on SDIO local domain.
 *
 * Assumption:
 *	1. Using SDIO Local register ONLY for configuration.
 *	2. PASSIVE LEVEL
 */
static void enable_interrupt(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	rtl8821cs_update_himr(adapter, hal->sdio_himr);
	RTW_INFO(FUNC_ADPT_FMT ": update SDIO HIMR=0x%08X\n",
		 FUNC_ADPT_ARG(adapter), hal->sdio_himr);
}

/*
 * Description:
 *	Disable SDIO Host IMR configuration to mask unnecessary interrupt service.
 *
 * Assumption:
 *	Using SDIO Local register ONLY for configuration.
 */
static void disable_interrupt(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	rtl8821cs_update_himr(adapter, 0);
	RTW_INFO("%s: update SDIO HIMR=0\n", __FUNCTION__);
}

#ifdef CONFIG_WOWLAN
void rtl8821cs_disable_interrupt_but_cpwm2(PADAPTER adapter)
{
	u32 himr, tmp;

	tmp = rtw_read32(adapter, REG_SDIO_HIMR);
	RTW_INFO("%s: Read SDIO_REG_HIMR: 0x%08x\n", __FUNCTION__, tmp);

	himr = BIT_SDIO_CPWM2_MSK;
	rtl8821cs_update_himr(adapter, himr);

	tmp = rtw_read32(adapter, REG_SDIO_HIMR);
	RTW_INFO("%s: Read again SDIO_REG_HIMR: 0x%08x\n", __FUNCTION__, tmp);
}
#endif /* CONFIG_WOWLAN */

static void _run_thread(PADAPTER adapter)
{
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &adapter->xmitpriv;

	if (xmitpriv->SdioXmitThread == NULL) {
		RTW_INFO(FUNC_ADPT_FMT " start RTWHALXT\n", FUNC_ADPT_ARG(adapter));
		xmitpriv->SdioXmitThread = kthread_run(rtl8821cs_xmit_thread, adapter, "RTWHALXT");
		if (IS_ERR(xmitpriv->SdioXmitThread)) {
			RTW_ERR("%s: start rtl8821cs_xmit_thread FAIL!!\n", __FUNCTION__);
			xmitpriv->SdioXmitThread = NULL;
		}
	}
#endif /* !CONFIG_SDIO_TX_TASKLET */
}

static void run_thread(PADAPTER adapter)
{
	_run_thread(adapter);
	rtl8821c_run_thread(adapter);
}

static void _cancel_thread(PADAPTER adapter)
{
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &adapter->xmitpriv;

	/* stop xmit_buf_thread */
	if (xmitpriv->SdioXmitThread) {
		_rtw_up_sema(&xmitpriv->SdioXmitSema);
		rtw_thread_stop(xmitpriv->SdioXmitThread);
		xmitpriv->SdioXmitThread = NULL;
	}
#endif /* !CONFIG_SDIO_TX_TASKLET */
}

static void cancel_thread(PADAPTER adapter)
{
	rtl8821c_cancel_thread(adapter);
	_cancel_thread(adapter);
}

#ifdef CONFIG_SDIO_OOB
/*
REG_SYS_SDIO_CTRL_8821C
BIT_SDIO_INT - 1: Enabled (GPIO4:SDIO_INT); 0 : Disabled
BIT_SDIO_INT_POLARITY - 0: Low Active 1: High Active
*/

void rtl8821cs_gpio4_sdio_int_enable(_adapter *adapter, u8 enable, u8 polarity_high)
{
	u32 sdio_ctrl = 0;

	sdio_ctrl = rtw_read32(adapter, REG_SYS_SDIO_CTRL_8821C);
	if (enable) {
		sdio_ctrl |= BIT_SDIO_INT;
		if (polarity_high == _TRUE)
			sdio_ctrl |= BIT_SDIO_INT_POLARITY;
		else
			sdio_ctrl &= (~BIT_SDIO_INT_POLARITY);
	} else
		sdio_ctrl &= (~BIT_SDIO_INT);
	rtw_write32(adapter, REG_SYS_SDIO_CTRL_8821C, sdio_ctrl);
}
#endif

/*
 * If variable not handled here,
 * some variables will be processed in rtl8821c_sethwreg()
 */
static u8 sethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	u8 ret = _SUCCESS;

	switch (variable) {
	case HW_VAR_SET_RPWM:
		{
			/*
			 * rpwm value only use BIT0(clock bit), and BIT7(Toggle bit)
			 * BIT0 value - 1: 32k, 0:40MHz.
			 * BIT4 value - 1: enable power gated, 0: disable power gated
			 * BIT6 value - 1: report cpwm value after success set, 0:do not report.
			 * BIT7 value - Toggling Bit.
			 */
			u8 val8 = 0;
			struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

			val8 = (*val) & 0xC1;

#ifdef CONFIG_LPS_PG
			if ((val8 & BIT(0)) && (LPS_PG == pwrpriv->lps_level))
				val8 |= BIT(4);
#endif
			rtw_write8(adapter, REG_SDIO_HRPWM1_8821C, val8);
		}
		break;

#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL:
	{
	}
	break;
#endif
/*
#ifdef CONFIG_SDIO_OOB
	case HW_SET_SDIO_OOB:
	{
		u8 sdio_ctrl = *val;
		u8 enable, is_high_active;

		enable = (sdio_ctrl & BIT(0)) ? _TRUE : _FALSE;
		is_high_active = (sdio_ctrl & BIT(4)) ? _TRUE : _FALSE;
		rtl8821cs_gpio4_sdio_int_enable(adapter, enable, is_high_active);
	}
	break;
#endif
*/
	default:
		ret = rtl8821c_sethwreg(adapter, variable, val);
		break;
	}

	return ret;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723B()
 */
static void gethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
	case HW_VAR_CPWM:
		*val = rtw_read8(adapter, REG_SDIO_HCPWM1_V2_8821C);
		break;

	case HW_VAR_RPWM_TOG:
		*val = rtw_read8(adapter, REG_SDIO_HRPWM1_8821C);
		*val &= BIT_TOGGLE_8821C;
		break;

#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL:

		break;
#endif

	default:
		rtl8821c_gethwreg(adapter, variable, val);
		break;
	}
}

/*
 * Description:
 *	Query setting of specified variable.
 */
static u8 gethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE eVariable, void *pval)
{
	u8 bResult = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PSDIO_DATA psdio_data = &dvobj->intf_data;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	switch (eVariable) {

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		{
			if (psdio_data->clock > RTW_SDIO_CLK_40M) {
				*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_32K;
				RTW_INFO("AMPDU FACTOR - 32K\n");
			} else if (psdio_data->clock > RTW_SDIO_CLK_33M) {
				*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_16K;
				RTW_INFO("AMPDU FACTOR - 16K\n");
			} else {
				*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_8K;
				RTW_INFO("AMPDU FACTOR - 8K\n");
			}
		}
		break;

	default:
		bResult = rtl8821c_gethaldefvar(adapter, eVariable, pval);
		break;
	}

	return bResult;
}

/*
 * Description:
 *	Change default setting of specified variable.
 */
static u8 sethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE eVariable, void *pval)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 bResult = _SUCCESS;

	switch (eVariable) {
	default:
		bResult = rtl8821c_sethaldefvar(adapter, eVariable, pval);
		break;
	}

	return bResult;
}

u8 rtl8821cs_set_hal_ops(PADAPTER adapter)
{
	struct hal_ops *ops_func = &adapter->hal_func;

	u8 err;

	err = rtl8821cs_halmac_init_adapter(adapter);
	if (err) {
		RTW_INFO("%s: [ERROR]HALMAC initialize FAIL!\n", __FUNCTION__);
		return _FAIL;
	}

	rtl8821c_set_hal_ops(adapter);
	rtl8821cs_init_interrupt(adapter);

	ops_func->init_default_value = rtl8821cs_init_default_value;
	ops_func->intf_chip_configure = intf_chip_configure;

	ops_func->hal_init = rtl8821cs_hal_init;
	ops_func->hal_deinit = rtl8821cs_hal_deinit;

	ops_func->init_xmit_priv = rtl8821cs_init_xmit_priv;
	ops_func->free_xmit_priv = rtl8821cs_free_xmit_priv;
	ops_func->hal_xmit = rtl8821cs_hal_xmit;
	ops_func->mgnt_xmit = rtl8821cs_mgnt_xmit;
	ops_func->hal_xmitframe_enqueue = rtl8821cs_hal_xmit_enqueue;
#ifdef CONFIG_XMIT_THREAD_MODE
	ops_func->xmit_thread_handler = rtl8821cs_xmit_buf_handler;
#endif
	ops_func->run_thread = run_thread;
	ops_func->cancel_thread = cancel_thread;

	ops_func->init_recv_priv = rtl8821cs_init_recv_priv;
	ops_func->free_recv_priv = rtl8821cs_free_recv_priv;
#ifdef CONFIG_RECV_THREAD_MODE
	ops_func->recv_hdl = rtl8821cs_recv_hdl;
#endif

	ops_func->enable_interrupt = enable_interrupt;
	ops_func->disable_interrupt = disable_interrupt;
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	ops_func->clear_interrupt = clear_interrupt_all;
#endif
#ifdef CONFIG_RTW_SW_LED
	ops_func->InitSwLeds = rtl8821cs_initswleds;
	ops_func->DeInitSwLeds = rtl8821cs_deinitswleds;
#endif
	ops_func->set_hw_reg_handler = sethwreg;
	ops_func->GetHwRegHandler = gethwreg;
	ops_func->get_hal_def_var_handler = gethaldefvar;
	ops_func->SetHalDefVarHandler = sethaldefvar;
	return _TRUE;
}
