/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
#define _RTL8723D_CMD_C_

#include <rtl8723d_hal.h>
#include "hal_com_h2c.h"

#define MAX_H2C_BOX_NUMS	4
#define MESSAGE_BOX_SIZE		4

#define RTL8723D_MAX_CMD_LEN	7
#define RTL8723D_EX_MESSAGE_BOX_SIZE	4

static u8 _is_fw_read_cmd_down(_adapter *padapter, u8 msgbox_num)
{
	u8	read_down = _FALSE;
	int	retry_cnts = 100;

	u8 valid;

	/* RTW_INFO(" _is_fw_read_cmd_down ,reg_1cc(%x),msg_box(%d)...\n",rtw_read8(padapter,REG_HMETFR),msgbox_num); */

	do {
		valid = rtw_read8(padapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid)
			read_down = _TRUE;
		else
			rtw_msleep_os(1);
	} while ((!read_down) && (retry_cnts--));

	return read_down;

}


/*****************************************
* H2C Msg format :
*| 31 - 8		|7-5	| 4 - 0	|
*| h2c_msg	|Class	|CMD_ID	|
*| 31-0						|
*| Ext msg					|
*
******************************************/
s32 FillH2CCmd8723D(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 h2c_box_num;
	u8 h2c[RTL8723D_MAX_CMD_LEN + 1] = {0};
	u32	msgbox_addr;
	u32 msgbox_ex_addr = 0;
	u32	h2c_cmd = 0;
	u32	h2c_cmd_ex = 0;
	s32 ret = _FAIL;
	PHAL_DATA_TYPE pHalData;		
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;


	padapter = GET_PRIMARY_ADAPTER(padapter);
	pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CHECK_FW_PS_STATE
#ifdef DBG_CHECK_FW_PS_STATE_H2C
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("%s: h2c doesn't leave 32k ElementID=%02x\n", __FUNCTION__, ElementID);
		pdbgpriv->dbg_h2c_leave32k_fail_cnt++;
	}

	/* RTW_INFO("H2C ElementID=%02x , pHalData->LastHMEBoxNum=%02x\n", ElementID, pHalData->LastHMEBoxNum); */
#endif /* DBG_CHECK_FW_PS_STATE_H2C */
#endif /* DBG_CHECK_FW_PS_STATE */
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

	if (!pCmdBuffer)
		goto exit;
	if (CmdLen > RTL8723D_MAX_CMD_LEN)
		goto exit;
	if (rtw_is_surprise_removed(padapter))
		goto exit;

	h2c[0] = ElementID;
	_rtw_memcpy(h2c + 1, pCmdBuffer, CmdLen);
	
	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = pHalData->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(padapter, h2c_box_num)) {
			RTW_INFO(" fw read cmd failed...\n");
#ifdef DBG_CHECK_FW_PS_STATE
			RTW_INFO("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
				, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
#endif /* DBG_CHECK_FW_PS_STATE */
			/* RTW_INFO(" 0x1c0: 0x%8x\n", rtw_read32(padapter, 0x1c0)); */
			/* RTW_INFO(" 0x1c4: 0x%8x\n", rtw_read32(padapter, 0x1c4)); */
			goto exit;
		}

		/* Write Ext command (byte 4~7) */
		msgbox_ex_addr = REG_HMEBOX_EXT0_8723D + (h2c_box_num * RTL8723D_EX_MESSAGE_BOX_SIZE);
		_rtw_memcpy((u8 *)(&h2c_cmd_ex), h2c + 4, RTL8723D_EX_MESSAGE_BOX_SIZE);
		h2c_cmd_ex = le32_to_cpu(h2c_cmd_ex);
		rtw_write32(padapter, msgbox_ex_addr, h2c_cmd_ex);
		/* Write command (byte 0~3) */
		msgbox_addr = REG_HMEBOX_0_8723D + (h2c_box_num * MESSAGE_BOX_SIZE);
		_rtw_memcpy((u8 *)(&h2c_cmd), h2c, 4);
		h2c_cmd = le32_to_cpu(h2c_cmd);
		rtw_write32(padapter, msgbox_addr, h2c_cmd);

		/* RTW_INFO("MSG_BOX:%d, CmdLen(%d), CmdID(0x%x), reg:0x%x =>h2c_cmd:0x%.8x, reg:0x%x =>h2c_cmd_ex:0x%.8x\n" */
		/*	,pHalData->LastHMEBoxNum , CmdLen, ElementID, msgbox_addr, h2c_cmd, msgbox_ex_addr, h2c_cmd_ex); */

		/* update last msg box number */
		pHalData->LastHMEBoxNum = (h2c_box_num + 1) % MAX_H2C_BOX_NUMS;

	} while (0);

	ret = _SUCCESS;

exit:

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);


	return ret;
}

/*
 * Description: Get the reserved page number in Tx packet buffer.
 * Retrun value: the page number.
 * 2012.08.09, by tynli.
 *   */
u8 GetTxBufferRsvdPageNum8723D(_adapter *padapter, bool wowlan)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	RsvdPageNum = 0;
	/* default reseved 1 page for the IC type which is undefined. */
	u8	TxPageBndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8723D;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&TxPageBndy);

	RsvdPageNum = LAST_ENTRY_OF_TX_PKT_BUFFER_8723D - TxPageBndy + 1;

	return RsvdPageNum;
}

void rtl8723d_set_FwPwrMode_cmd(PADAPTER padapter, u8 psmode)
{
	u8 smart_ps = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8 u1H2CPwrModeParm[H2C_PWRMODE_LEN] = {0};
	u8 PowerState = 0, awake_intvl = 1, rlbm = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info *wdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */
	u8 allQueueUAPSD = 0;

#ifdef CONFIG_PLATFORM_INTEL_BYT
	if (psmode == PS_MODE_DTIM)
		psmode = PS_MODE_MAX;
#endif /* CONFIG_PLATFORM_INTEL_BYT */


	if (pwrpriv->dtim > 0)
		RTW_INFO("%s(): FW LPS mode = %d, SmartPS=%d, dtim=%d\n", __func__, psmode, pwrpriv->smart_ps, pwrpriv->dtim);
	else
		RTW_INFO("%s(): FW LPS mode = %d, SmartPS=%d\n", __func__, psmode, pwrpriv->smart_ps);

	if (psmode == PS_MODE_MIN) {
		rlbm = 0;
		awake_intvl = 2;
		smart_ps = pwrpriv->smart_ps;
	} else if (psmode == PS_MODE_MAX) {
		rlbm = 1;
		awake_intvl = 2;
		smart_ps = pwrpriv->smart_ps;
	} else if (psmode == PS_MODE_DTIM) { /* For WOWLAN LPS, DTIM = (awake_intvl - 1) */
		if (pwrpriv->dtim > 0 && pwrpriv->dtim < 16)
			awake_intvl = pwrpriv->dtim + 1; /* DTIM = (awake_intvl - 1) */
		else
			awake_intvl = 4;/* DTIM=3 */


		rlbm = 2;
		smart_ps = pwrpriv->smart_ps;
	} else {
		rlbm = 2;
		awake_intvl = 4;
		smart_ps = pwrpriv->smart_ps;
	}

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(wdinfo, P2P_STATE_NONE)) {
		awake_intvl = 2;
		rlbm = 1;
	}
#endif /* CONFIG_P2P */

	if (padapter->registrypriv.wifi_spec == 1) {
		awake_intvl = 2;
		rlbm = 1;
	}

	if (psmode > 0) {
#ifdef CONFIG_BT_COEXIST
		if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
			PowerState = rtw_btcoex_RpwmVal(padapter);
		else
#endif /* CONFIG_BT_COEXIST */
			PowerState = 0x00;/* AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	} else
		PowerState = 0x0C;/* AllON(0x0C), RFON(0x04), RFOFF(0x00) */

	SET_8723D_H2CCMD_PWRMODE_PARM_MODE(u1H2CPwrModeParm, (psmode > 0) ? 1 : 0);
	SET_8723D_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CPwrModeParm, smart_ps);
	SET_8723D_H2CCMD_PWRMODE_PARM_RLBM(u1H2CPwrModeParm, rlbm);
	SET_8723D_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CPwrModeParm, awake_intvl);
	SET_8723D_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CPwrModeParm, allQueueUAPSD);
	SET_8723D_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CPwrModeParm, PowerState);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_RecordPwrMode(padapter, u1H2CPwrModeParm, H2C_PWRMODE_LEN);
#endif /* CONFIG_BT_COEXIST */

	RTW_DBG_DUMP("u1H2CPwrModeParm:",
		     u1H2CPwrModeParm, H2C_PWRMODE_LEN);

	FillH2CCmd8723D(padapter, H2C_8723D_SET_PWR_MODE, H2C_PWRMODE_LEN, u1H2CPwrModeParm);
}

#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
void rtl8723d_set_BcnEarly_C2H_Rpt_cmd(PADAPTER padapter, u8 enable)
{
	u8	u1H2CSetPwrMode[H2C_PWRMODE_LEN] = {0};

	SET_8723D_H2CCMD_PWRMODE_PARM_MODE(u1H2CSetPwrMode, 1);
	SET_8723D_H2CCMD_PWRMODE_PARM_RLBM(u1H2CSetPwrMode, 1);
	SET_8723D_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CSetPwrMode, 0);
	SET_8723D_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CSetPwrMode, 0);
	SET_8723D_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CSetPwrMode, 0);
	SET_8723D_H2CCMD_PWRMODE_PARM_BCN_EARLY_C2H_RPT(u1H2CSetPwrMode, enable);
	SET_8723D_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CSetPwrMode, 0x0C);
	FillH2CCmd8723D(padapter, H2C_8723D_SET_PWR_MODE, sizeof(u1H2CSetPwrMode), u1H2CSetPwrMode);
}
#endif
#endif

void rtl8723d_set_FwPsTuneParam_cmd(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8 u1H2CPsTuneParm[H2C_PSTUNEPARAM_LEN] = {0};
	u8 bcn_to_limit = 10; /* 10 * 100 * awakeinterval (ms) */
	u8 dtim_timeout = 5; /* ms //wait broadcast data timer */
	u8 ps_timeout = 20;  /* ms //Keep awake when tx */
	u8 dtim_period = 3;

	/* RTW_INFO("%s(): FW LPS mode = %d\n", __func__, psmode); */

	SET_8723D_H2CCMD_PSTUNE_PARM_BCN_TO_LIMIT(u1H2CPsTuneParm, bcn_to_limit);
	SET_8723D_H2CCMD_PSTUNE_PARM_DTIM_TIMEOUT(u1H2CPsTuneParm, dtim_timeout);
	SET_8723D_H2CCMD_PSTUNE_PARM_PS_TIMEOUT(u1H2CPsTuneParm, ps_timeout);
	SET_8723D_H2CCMD_PSTUNE_PARM_ADOPT(u1H2CPsTuneParm, 1);
	SET_8723D_H2CCMD_PSTUNE_PARM_DTIM_PERIOD(u1H2CPsTuneParm, dtim_period);

	RTW_DBG_DUMP("u1H2CPsTuneParm:",
		     u1H2CPsTuneParm, H2C_PSTUNEPARAM_LEN);

	FillH2CCmd8723D(padapter, H2C_8723D_PS_TUNING_PARA, H2C_PSTUNEPARAM_LEN, u1H2CPsTuneParm);
}

void rtl8723d_download_rsvd_page(PADAPTER padapter, u8 mstatus)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	BOOLEAN		bcn_valid = _FALSE;
	u8	DLBcnCount = 0;
	u32 poll = 0;
	u8 RegFwHwTxQCtrl;


	RTW_INFO("+" FUNC_ADPT_FMT ": hw_port=%d mstatus(%x)\n",
		 FUNC_ADPT_ARG(padapter), get_hw_port(padapter), mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
		u8 bcn_ctrl = rtw_read8(padapter, REG_BCN_CTRL);
		BOOLEAN bRecover = _FALSE;
		u8 v8;

		/* We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		/* Suggested by filen. Added by tynli. */
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000 | pmlmeinfo->aid));

		/* set REG_CR bit 8 */
		v8 = rtw_read8(padapter, REG_CR + 1);
		v8 |= BIT(0); /* ENSWBCN */
		rtw_write8(padapter,  REG_CR + 1, v8);

		/* Disable Hw protection for a time which revserd for Hw sending beacon. */
		/* Fix download reserved page packet fail that access collision with the protection time. */
		/* 2010.05.11. Added by tynli. */
		rtw_write8(padapter, REG_BCN_CTRL, (bcn_ctrl & (~EN_BCN_FUNCTION)) | DIS_TSF_UDT);

		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl = rtw_read8(padapter, REG_FWHW_TXQ_CTRL + 2);
		if (RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		/* To tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl &= ~BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, RegFwHwTxQCtrl);

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
			/* download rsvd page. */
			rtw_hal_set_fw_rsvd_page(padapter, _FALSE);
			DLBcnCount++;
			do {
				rtw_yield_os();
				/* rtw_mdelay_os(10); */
				/* check rsvd page download OK. */
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll % 10) != 0 && !RTW_CANNOT_RUN(padapter));

		} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(padapter));

		if (RTW_CANNOT_RUN(padapter))
			;
		else if (!bcn_valid)
			RTW_ERR(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(padapter) , DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			rtw_hal_set_fw_rsvd_page(padapter, _TRUE);
			RTW_INFO(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(padapter), DLBcnCount, poll);
		}

		/* restore bcn_ctrl */
		rtw_write8(padapter, REG_BCN_CTRL, bcn_ctrl);

		/* To make sure that if there exists an adapter which would like to send beacon. */
		/* If exists, the origianl value of 0x422[6] will be 1, we should check this to */
		/* prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
		/* the beacon cannot be sent by HW. */
		/* 2010.06.23. Added by tynli. */
		if (bRecover) {
			RegFwHwTxQCtrl |= BIT(6);
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, RegFwHwTxQCtrl);
		}

		/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
#ifndef CONFIG_PCI_HCI
		v8 = rtw_read8(padapter, REG_CR + 1);
		v8 &= ~BIT(0); /* ~ENSWBCN */
		rtw_write8(padapter, REG_CR + 1, v8);
#endif
	}

}

void rtl8723d_set_FwJoinBssRpt_cmd(PADAPTER padapter, u8 mstatus)
{
	if (mstatus == 1)
		rtl8723d_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
}

#ifdef CONFIG_BT_COEXIST
void rtl8723d_download_BTCoex_AP_mode_rsvd_page(PADAPTER padapter)
{
	rtl8723d_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
}
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_P2P
void rtl8723d_set_p2p_ps_offload_cmd(_adapter *padapter, u8 p2p_ps_state)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrpriv = adapter_to_pwrctl(padapter);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	struct P2P_PS_Offload_t	*p2p_ps_offload = (struct P2P_PS_Offload_t *)(&pHalData->p2p_ps_offload);
	u8	i;


#if 1
	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		RTW_INFO("P2P_PS_DISABLE\n");
		_rtw_memset(p2p_ps_offload, 0 , 1);
		break;
	case P2P_PS_ENABLE:
		RTW_INFO("P2P_PS_ENABLE\n");
		/* update CTWindow value. */
		if (pwdinfo->ctwindow > 0) {
			p2p_ps_offload->CTWindow_En = 1;
			rtw_write8(padapter, REG_P2P_CTWIN, pwdinfo->ctwindow);
		}

		/* hw only support 2 set of NoA */
		for (i = 0 ; i < pwdinfo->noa_num ; i++) {
			/* To control the register setting for which NOA */
			rtw_write8(padapter, REG_NOA_DESC_SEL, (i << 4));
			if (i == 0)
				p2p_ps_offload->NoA0_En = 1;
			else
				p2p_ps_offload->NoA1_En = 1;

			/* config P2P NoA Descriptor Register */
			/* RTW_INFO("%s(): noa_duration = %x\n",__FUNCTION__,pwdinfo->noa_duration[i]); */
			rtw_write32(padapter, REG_NOA_DESC_DURATION, pwdinfo->noa_duration[i]);

			/* RTW_INFO("%s(): noa_interval = %x\n",__FUNCTION__,pwdinfo->noa_interval[i]); */
			rtw_write32(padapter, REG_NOA_DESC_INTERVAL, pwdinfo->noa_interval[i]);

			/* RTW_INFO("%s(): start_time = %x\n",__FUNCTION__,pwdinfo->noa_start_time[i]); */
			rtw_write32(padapter, REG_NOA_DESC_START, pwdinfo->noa_start_time[i]);

			/* RTW_INFO("%s(): noa_count = %x\n",__FUNCTION__,pwdinfo->noa_count[i]); */
			rtw_write8(padapter, REG_NOA_DESC_COUNT, pwdinfo->noa_count[i]);
		}

		if ((pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0)) {
			/* rst p2p circuit */
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(4));

			p2p_ps_offload->Offload_En = 1;

			if (pwdinfo->role == P2P_ROLE_GO) {
				p2p_ps_offload->role = 1;
				p2p_ps_offload->AllStaSleep = 0;
			} else
				p2p_ps_offload->role = 0;

			p2p_ps_offload->discovery = 0;
		}
		break;
	case P2P_PS_SCAN:
		RTW_INFO("P2P_PS_SCAN\n");
		p2p_ps_offload->discovery = 1;
		break;
	case P2P_PS_SCAN_DONE:
		RTW_INFO("P2P_PS_SCAN_DONE\n");
		p2p_ps_offload->discovery = 0;
		pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
		break;
	default:
		break;
	}

	FillH2CCmd8723D(padapter, H2C_8723D_P2P_PS_OFFLOAD, 1, (u8 *)p2p_ps_offload);
#endif


}
#endif /* CONFIG_P2P */

