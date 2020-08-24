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
#define _RTL8821C_CMD_C_

#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* HRTW_HALMAC_H2C_MAX_SIZE, CMD_ID_RSVD_PAGE and etc. */
#include "rtl8821c.h"

/*
 * Below functions are for C2H
 */
/*****************************************
 * H2C Msg format :
 *| 31 - 8		|7-5	| 4 - 0	|
 *| h2c_msg		|Class	|CMD_ID	|
 *| 31-0				|
 *| Ext msg				|
 *
 ******************************************/
s32 rtl8821c_fillh2ccmd(PADAPTER adapter, u8 id, u32 buf_len, u8 *pbuf)
{
	u8 h2c[RTW_HALMAC_H2C_MAX_SIZE] = {0};
#ifdef DBG_H2C_CONTENT
	u8 msg[(RTW_HALMAC_H2C_MAX_SIZE - 1) * 5 + 1] = {0};
	u8 *msg_p;
	u32 msg_size, i, n;
#endif /* CONFIG_RTW_DEBUG */
	int err;
	s32 ret = _FAIL;


	if (!pbuf)
		goto exit;

	if (buf_len > (RTW_HALMAC_H2C_MAX_SIZE - 1))
		goto exit;

	if (rtw_is_surprise_removed(adapter))
		goto exit;

#ifdef DBG_H2C_CONTENT
	msg_p = msg;
	msg_size = (RTW_HALMAC_H2C_MAX_SIZE - 1) * 5 + 1;
	for (i = 0; i < buf_len; i++) {
		n = rtw_sprintf(msg_p, msg_size, " 0x%02x", pbuf[i]);
		msg_p += n;
		msg_size -= n;
		if (msg_size == 0)
			break;
	}
	RTW_INFO(FUNC_ADPT_FMT ": id=0x%02x buf=%s\n",
		 FUNC_ADPT_ARG(adapter), id, msg);
#endif /* CONFIG_RTW_DEBUG */

	h2c[0] = id;
	_rtw_memcpy(h2c + 1, pbuf, buf_len);

	err = rtw_halmac_send_h2c(adapter_to_dvobj(adapter), h2c);
	if (!err)
		ret = _SUCCESS;

exit:

	return ret;
}

#define SET_PWR_MODE_SET_BCN_RECEIVING_TIME(h2c_pkt, value)                    \
	SET_BITS_TO_LE_4BYTE(h2c_pkt + 0X04, 24, 5, value)
#define SET_PWR_MODE_SET_ADOPT_BCN_RECEIVING_TIME(h2c_pkt, value)              \
	SET_BITS_TO_LE_4BYTE(h2c_pkt + 0X04, 31, 1, value)

void rtl8821c_set_FwPwrMode_cmd(PADAPTER adapter, u8 psmode)
{
	u8 mode = 0, smart_ps = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
#ifdef CONFIG_BCN_RECV_TIME
	u8 bcn_recv_time;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#endif
#ifdef CONFIG_WMMPS_STA
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct qos_priv	*pqospriv = &pmlmepriv->qospriv;
#endif /* CONFIG_WMMPS_STA */	
	u8 h2c[RTW_HALMAC_H2C_MAX_SIZE] = {0};
	u8 PowerState = 0, awake_intvl = 1, rlbm = 0;
	u8 allQueueUAPSD = 0;
	char *fw_psmode_str = "";
#ifdef CONFIG_P2P
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
#endif /* CONFIG_P2P */
	u8 hw_port = rtw_hal_get_port(adapter);

	if (pwrpriv->dtim > 0)
		RTW_INFO(FUNC_ADPT_FMT ": dtim=%d, HW port id=%d\n", FUNC_ADPT_ARG(adapter),
			pwrpriv->dtim, psmode == PS_MODE_ACTIVE ? pwrpriv->current_lps_hw_port_id : hw_port);
	else
		RTW_INFO(FUNC_ADPT_FMT ": HW port id=%d\n", FUNC_ADPT_ARG(adapter),
			psmode == PS_MODE_ACTIVE ? pwrpriv->current_lps_hw_port_id : hw_port);

	smart_ps = pwrpriv->smart_ps;
	switch (psmode) {
		case PS_MODE_MIN:
		case PS_MODE_MAX:
			{
#ifdef CONFIG_WMMPS_STA	
				if (rtw_is_wmmps_mode(adapter)) {
					mode = 2;

					smart_ps = pwrpriv->wmm_smart_ps;

					/* (WMMPS) allQueueUAPSD: 0: PSPoll, 1: QosNullData (if wmm_smart_ps=1) or do nothing (if wmm_smart_ps=2) */
					if ((pqospriv->uapsd_tid & BIT_MASK_TID_TC) == ALL_TID_TC_SUPPORTED_UAPSD)
						allQueueUAPSD = 1;
				} else
#endif /* CONFIG_WMMPS_STA */
				{
					mode = 1;
#ifdef CONFIG_WMMPS_STA	
					/* For WMMPS test case, the station must retain sleep mode to capture buffered data on LPS mechanism */ 
					if ((pqospriv->uapsd_tid & BIT_MASK_TID_TC)  != 0)
						smart_ps = 0;
					else
#endif /* CONFIG_WMMPS_STA */
					{
						smart_ps = pwrpriv->smart_ps;
					}
				}

				if (psmode == PS_MODE_MIN)
					rlbm = 0;
				else
					rlbm = 1;
			}
			break;
		case PS_MODE_DTIM:
			{
				mode = 1;
				rlbm = 2;

				/* For WOWLAN LPS, DTIM = (awake_intvl - 1) */
				if (pwrpriv->dtim > 0 && pwrpriv->dtim < 16) /* DTIM = (awake_intvl - 1) */
					awake_intvl = pwrpriv->dtim + 1;
				else /* DTIM = 3 */
					awake_intvl = 4;
			}
			break;
		case PS_MODE_ACTIVE:
		default:
			mode = 0;
			break;
	}

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(wdinfo, P2P_STATE_NONE)) {
		awake_intvl = 2;
		rlbm = 1;
	}
#endif /* CONFIG_P2P */

	if (adapter->registrypriv.wifi_spec == 1) {
		awake_intvl = 2;
		rlbm = 1;
	}

	if (psmode > 0) {
#ifdef CONFIG_BT_COEXIST
		if (rtw_btcoex_IsBtControlLps(adapter) == _TRUE)
			PowerState = rtw_btcoex_RpwmVal(adapter);
		else
#endif /* CONFIG_BT_COEXIST */
			PowerState = 0x00; /* AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	} else
		PowerState = 0x0C; /* AllON(0x0C), RFON(0x04), RFOFF(0x00) */

	if (mode == 0)
		fw_psmode_str = "ACTIVE";
	else if (mode == 1)
		fw_psmode_str = "LPS";
	else if (mode == 2)
		fw_psmode_str = "WMMPS";
	else
		fw_psmode_str = "UNSPECIFIED";

	RTW_INFO(FUNC_ADPT_FMT": fw ps mode = %s, drv ps mode = %d, rlbm = %d , smart_ps = %d, allQueueUAPSD = %d\n", 
				FUNC_ADPT_ARG(adapter), fw_psmode_str, psmode, rlbm, smart_ps, allQueueUAPSD);

	SET_PWR_MODE_SET_CMD_ID(h2c, CMD_ID_SET_PWR_MODE);
	SET_PWR_MODE_SET_CLASS(h2c, CLASS_SET_PWR_MODE);
	SET_PWR_MODE_SET_MODE(h2c, mode);
	SET_PWR_MODE_SET_SMART_PS(h2c, smart_ps);
	SET_PWR_MODE_SET_RLBM(h2c, rlbm);
	SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c, awake_intvl);
	SET_PWR_MODE_SET_B_ALL_QUEUE_UAPSD(h2c, allQueueUAPSD);
	SET_PWR_MODE_SET_PWR_STATE(h2c, PowerState);
	if (psmode == PS_MODE_ACTIVE) {
		/* Leave LPS, set the same HW port ID */
		SET_PWR_MODE_SET_PORT_ID(h2c, pwrpriv->current_lps_hw_port_id);
	} else {
		/* Enter LPS, record HW port ID */
		SET_PWR_MODE_SET_PORT_ID(h2c, hw_port);
		pwrpriv->current_lps_hw_port_id = hw_port;
	}

#ifdef CONFIG_BCN_RECV_TIME
	if (pmlmeext->bcn_rx_time) {
		bcn_recv_time = pmlmeext->bcn_rx_time / 128;
		if (pmlmeext->bcn_rx_time % 128)
			bcn_recv_time += 1;

		if (bcn_recv_time >= 31)
			bcn_recv_time = 31;
		SET_PWR_MODE_SET_ADOPT_BCN_RECEIVING_TIME(h2c, 1);
		SET_PWR_MODE_SET_BCN_RECEIVING_TIME(h2c, bcn_recv_time);
	}
#endif

	RTW_INFO("%s=> psmode:%02x, smart_ps:%02x, PowerState:%02x\n", __func__, psmode, smart_ps, PowerState);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_RecordPwrMode(adapter, h2c + 1, RTW_HALMAC_H2C_MAX_SIZE - 1);
#endif /* CONFIG_BT_COEXIST */

	RTW_DBG_DUMP("H2C-PwrMode Parm:", h2c, RTW_HALMAC_H2C_MAX_SIZE);
	rtw_halmac_send_h2c(adapter_to_dvobj(adapter), h2c);
}

void rtl8821c_set_FwPwrModeInIPS_cmd(PADAPTER adapter, u8 cmd_param)
{
	u8 h2c[RTW_HALMAC_H2C_MAX_SIZE] = {0};

	INACTIVE_PS_SET_CMD_ID(h2c, CMD_ID_INACTIVE_PS);
	INACTIVE_PS_SET_CLASS(h2c, CLASS_INACTIVE_PS);

	if (cmd_param & BIT0)
		INACTIVE_PS_SET_ENABLE(h2c, 1);
	if (cmd_param & BIT1)
		INACTIVE_PS_SET_IGNORE_PS_CONDITION(h2c, 1);

	RTW_DBG_DUMP("FW_IPS Parm:", h2c, RTW_HALMAC_H2C_MAX_SIZE);
	rtw_halmac_send_h2c(adapter_to_dvobj(adapter), h2c);
}

#ifdef CONFIG_BT_COEXIST
void rtl8821c_download_BTCoex_AP_mode_rsvd_page(PADAPTER adapter)
{
	rtl8821c_dl_rsvd_page(adapter, RT_MEDIA_CONNECT);
}
#endif /* CONFIG_BT_COEXIST */

/*
* Below functions are for C2H
*/
static void c2h_ccx_rpt(PADAPTER adapter, u8 *pdata)
{
#ifdef CONFIG_XMIT_ACK
		u8 tx_state;

		tx_state = CCX_RPT_GET_TX_STATE(pdata);

		/* 0 means success, 1 means retry drop */
		if (tx_state == 0)
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
#endif /* CONFIG_XMIT_ACK */
}
#ifdef CONFIG_FW_HANDLE_TXBCN
#define C2H_SUB_CMD_ID_FW_TBTT_RPT  0X23
#define TBTT_RPT_GET_SN(c2h_pkt)	LE_BITS_TO_4BYTE(c2h_pkt + 0X01, 0, 8)
#define TBTT_RPT_GET_PORT_ID(c2h_pkt)	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)

#define TBTT_ROOT	0x00
#define TBTT_VAP1	0x10
#define TBTT_VAP2	0x20
#define TBTT_VAP3	0x30

static void c2h_tbtt_rpt(PADAPTER adapter, u8 *pdata)
{
	u8 ap_id, c2h_sn;

	ap_id = TBTT_RPT_GET_PORT_ID(pdata);
	c2h_sn = TBTT_RPT_GET_SN(pdata);
#ifdef DBG_FW_TBTT_RPT
	if (ap_id == TBTT_ROOT)
		RTW_INFO("== TBTT ROOT SN:%d==\n", c2h_sn);
	else if (ap_id == TBTT_VAP1)
		RTW_INFO("== TBTT_VAP1 SN:%d==\n", c2h_sn);
	else if (ap_id == TBTT_VAP2)
		RTW_INFO("== TBTT_VAP2 SN:%d==\n", c2h_sn);
	else if (ap_id == TBTT_VAP3)
		RTW_INFO("== TBTT_VAP3 SN:%d==\n", c2h_sn);
	else
		RTW_ERR("TBTT RPT INFO ERROR\n");
#endif
}
#endif

/**
* pbuf = RXDESC + c2h packet
* length = RXDESC_SIZE + c2h packet size
* c2h format => ID(1B) | SN(1B) | Payload
* C2H - 0xFF format
*	u8 CMD_ID
*	u8 CMD_SEQ
*	u8 SUB_CMD_ID
*	u8 CMD_LEN
*	u8 *pContent
*/
void c2h_handler_rtl8821c(PADAPTER adapter, u8 *pbuf, u16 length)
{
	u8 c2h_id, c2h_sn;
	int c2h_len;
	u8 *pc2h_hdr;
	u8 *pc2h_data;
	u8 c2h_sub_cmd_id = 0;
	u32 desc_size = 0;
#ifdef CONFIG_WOWLAN
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
#endif /* CONFIG_WOWLAN*/

	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(adapter), &desc_size);

#ifdef CONFIG_WOWLAN
	if (pwrpriv->wowlan_mode == _TRUE) {
		RTW_INFO("%s: return because wowolan_mode==TRUE! CMDID=%d\n",
			 __FUNCTION__, C2H_GET_CMD_ID(pbuf + desc_size));
		return;
	}
#endif /* CONFIG_WOWLAN*/
	if (pbuf == NULL)
		return;

	if (length < desc_size) {
		RTW_INFO("%s: [ERROR] c2h length(%d) is smaller than RXDESC_SIZE(%d)!!\n",
			 __func__, length, desc_size);
		return;
	}

	pc2h_hdr = pbuf + desc_size;
	pc2h_data = pbuf + desc_size + 2; /* cmd ID not 0xFF original C2H have 2 bytes C2H header */
	c2h_id = C2H_GET_CMD_ID(pc2h_hdr);
	c2h_sn = C2H_GET_SEQ(pc2h_hdr);
	c2h_len = length - desc_size - 2;

	if (c2h_len < 0) {
		RTW_ERR("%s: [ERROR] C2H_ID(%02x) C2H_SN(%d) warn c2h_len :%d (length:%d)\n", __func__, c2h_id, c2h_sn, c2h_len, length);
		rtw_warn_on(1);
		return;
	}
#ifdef DBG_C2H_CONTENT
	RTW_INFO("%s "ADPT_FMT" C2H, ID=%d seq=%d len=%d\n", __func__, ADPT_ARG(adapter), c2h_id, c2h_sn, length);
#endif
	switch (c2h_id) {
	case CMD_ID_C2H_SND_TXBF:
		/*C2HTxBeamformingHandler_8821C(adapter, pc2h_data, c2h_len);*/
		break;

	/* FW offload C2H is 0xFF cmd according to halmac function - halmac_parse_c2h_packet */
	case 0xFF:
		/* Get C2H sub cmd ID */
		c2h_sub_cmd_id = (u8)C2H_HDR_GET_C2H_SUB_CMD_ID(pc2h_hdr);
		if (c2h_sub_cmd_id == C2H_SUB_CMD_ID_CCX_RPT)
			c2h_ccx_rpt(adapter, pbuf + desc_size);
		#ifdef CONFIG_FW_HANDLE_TXBCN
		else if (c2h_sub_cmd_id == C2H_SUB_CMD_ID_FW_TBTT_RPT)
			c2h_tbtt_rpt(adapter, pbuf + desc_size);
		#endif
		else
		/* indicate  rx desc + c2h pkt to halmac */
			if (rtw_halmac_c2h_handle(adapter_to_dvobj(adapter), pbuf, length) == -1)
				RTW_ERR("%s "ADPT_FMT" C2H, ID=%d, SubID=%d seq=%d len=%d ,HALMAC not to handle\n",
					__func__, ADPT_ARG(adapter), c2h_id, c2h_sub_cmd_id, c2h_sn, length);
		break;
	/* others for c2h common code */
	default:
		/* shift 2 byte to remove cmd id & seq */
		c2h_handler(adapter, c2h_id, c2h_sn, c2h_len, pc2h_data);
		break;
	}
}


static inline u8 is_c2h_id_handle_directly(u8 c2h_id, u8 c2h_sub_cmd_id)
{
	switch (c2h_id) {
	case CMD_ID_C2H_CCX_RPT:
	case C2H_IQK_FINISH:
	case C2H_EXTEND:

	#if defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)
	case C2H_BCN_EARLY_RPT:
	case C2H_FW_CHNL_SWITCH_COMPLETE:
	#endif

	#ifdef CONFIG_BT_COEXIST
	case C2H_BT_MP_INFO:
	#endif

	#ifdef CONFIG_MCC_MODE
	case C2H_MCC:
	#endif
	case C2H_LPS_STATUS_RPT:
		return _TRUE;
	default:
		return _FALSE;
	}

}

/*
* pbuf = RXDESC + c2h packet
* length = RXDESC_SIZE + c2h packet size
*/
void c2h_pre_handler_rtl8821c(_adapter *adapter, u8 *pbuf, s32 length)
{
	u8 c2h_id;
	u8 c2h_sub_cmd_id = 0;
	u32 desc_size = 0;

	if ((length <= 0) || (!pbuf))
		return;

	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(adapter), &desc_size);

	c2h_id = C2H_GET_CMD_ID(pbuf + desc_size);

	/* Get C2H sub cmd ID */
	if (c2h_id == 0xFF)
		c2h_sub_cmd_id = (u8)C2H_HDR_GET_C2H_SUB_CMD_ID(pbuf + desc_size);

	if (is_c2h_id_handle_directly(c2h_id, c2h_sub_cmd_id))
		c2h_handler_rtl8821c(adapter, pbuf, length);
	else
		rtw_c2h_packet_wk_cmd(adapter, pbuf, length);
}
