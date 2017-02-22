/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "fw.h"

static void _rtl92s_fw_set_rqpn(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_dword(rtlpriv, RQPN, 0xffffffff);
	rtl_write_dword(rtlpriv, RQPN + 4, 0xffffffff);
	rtl_write_byte(rtlpriv, RQPN + 8, 0xff);
	rtl_write_byte(rtlpriv, RQPN + 0xB, 0x80);
}

static bool _rtl92s_firmware_enable_cpu(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 ichecktime = 200;
	u16 tmpu2b;
	u8 tmpu1b, cpustatus = 0;

	_rtl92s_fw_set_rqpn(hw);

	/* Enable CPU. */
	tmpu1b = rtl_read_byte(rtlpriv, SYS_CLKR);
	/* AFE source */
	rtl_write_byte(rtlpriv, SYS_CLKR, (tmpu1b | SYS_CPU_CLKSEL));

	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | FEN_CPUEN));

	/* Polling IMEM Ready after CPU has refilled. */
	do {
		cpustatus = rtl_read_byte(rtlpriv, TCR);
		if (cpustatus & IMEM_RDY) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "IMEM Ready after CPU has refilled\n");
			break;
		}

		udelay(100);
	} while (ichecktime--);

	if (!(cpustatus & IMEM_RDY))
		return false;

	return true;
}

static enum fw_status _rtl92s_firmware_get_nextstatus(
		enum fw_status fw_currentstatus)
{
	enum fw_status	next_fwstatus = 0;

	switch (fw_currentstatus) {
	case FW_STATUS_INIT:
		next_fwstatus = FW_STATUS_LOAD_IMEM;
		break;
	case FW_STATUS_LOAD_IMEM:
		next_fwstatus = FW_STATUS_LOAD_EMEM;
		break;
	case FW_STATUS_LOAD_EMEM:
		next_fwstatus = FW_STATUS_LOAD_DMEM;
		break;
	case FW_STATUS_LOAD_DMEM:
		next_fwstatus = FW_STATUS_READY;
		break;
	default:
		break;
	}

	return next_fwstatus;
}

static u8 _rtl92s_firmware_header_map_rftype(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	switch (rtlphy->rf_type) {
	case RF_1T1R:
		return 0x11;
	case RF_1T2R:
		return 0x12;
	case RF_2T2R:
		return 0x22;
	default:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG, "Unknown RF type(%x)\n",
			 rtlphy->rf_type);
		break;
	}
	return 0x22;
}

static void _rtl92s_firmwareheader_priveupdate(struct ieee80211_hw *hw,
		struct fw_priv *pfw_priv)
{
	/* Update RF types for RATR settings. */
	pfw_priv->rf_config = _rtl92s_firmware_header_map_rftype(hw);
}



static bool _rtl92s_cmd_send_packet(struct ieee80211_hw *hw,
		struct sk_buff *skb, u8 last)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	unsigned long flags;
	u8 idx = 0;

	ring = &rtlpci->tx_ring[TXCMD_QUEUE];

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	idx = (ring->idx + skb_queue_len(&ring->queue)) % ring->entries;
	pdesc = &ring->desc[idx];
	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *)pdesc, 1, 1, skb);
	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	return true;
}

static bool _rtl92s_firmware_downloadcode(struct ieee80211_hw *hw,
		u8 *code_virtual_address, u32 buffer_len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct sk_buff *skb;
	struct rtl_tcb_desc *tcb_desc;
	unsigned char *seg_ptr;
	u16 frag_threshold = MAX_FIRMWARE_CODE_SIZE;
	u16 frag_length, frag_offset = 0;
	u16 extra_descoffset = 0;
	u8 last_inipkt = 0;

	_rtl92s_fw_set_rqpn(hw);

	if (buffer_len >= MAX_FIRMWARE_CODE_SIZE) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Size over FIRMWARE_CODE_SIZE!\n");

		return false;
	}

	extra_descoffset = 0;

	do {
		if ((buffer_len - frag_offset) > frag_threshold) {
			frag_length = frag_threshold + extra_descoffset;
		} else {
			frag_length = (u16)(buffer_len - frag_offset +
					    extra_descoffset);
			last_inipkt = 1;
		}

		/* Allocate skb buffer to contain firmware */
		/* info and tx descriptor info. */
		skb = dev_alloc_skb(frag_length);
		if (!skb)
			return false;
		skb_reserve(skb, extra_descoffset);
		seg_ptr = (u8 *)skb_put(skb, (u32)(frag_length -
					extra_descoffset));
		memcpy(seg_ptr, code_virtual_address + frag_offset,
		       (u32)(frag_length - extra_descoffset));

		tcb_desc = (struct rtl_tcb_desc *)(skb->cb);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->cmd_or_init = DESC_PACKET_TYPE_INIT;
		tcb_desc->last_inipkt = last_inipkt;

		_rtl92s_cmd_send_packet(hw, skb, last_inipkt);

		frag_offset += (frag_length - extra_descoffset);

	} while (frag_offset < buffer_len);

	rtl_write_byte(rtlpriv, TP_POLL, TPPOLL_CQ);

	return true ;
}

static bool _rtl92s_firmware_checkready(struct ieee80211_hw *hw,
		u8 loadfw_status)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rt_firmware *firmware = (struct rt_firmware *)rtlhal->pfirmware;
	u32 tmpu4b;
	u8 cpustatus = 0;
	short pollingcnt = 1000;
	bool rtstatus = true;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "LoadStaus(%d)\n", loadfw_status);

	firmware->fwstatus = (enum fw_status)loadfw_status;

	switch (loadfw_status) {
	case FW_STATUS_LOAD_IMEM:
		/* Polling IMEM code done. */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & IMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & IMEM_CHK_RPT) || (pollingcnt <= 0)) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "FW_STATUS_LOAD_IMEM FAIL CPU, Status=%x\n",
				 cpustatus);
			goto status_check_fail;
		}
		break;

	case FW_STATUS_LOAD_EMEM:
		/* Check Put Code OK and Turn On CPU */
		/* Polling EMEM code done. */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & EMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & EMEM_CHK_RPT) || (pollingcnt <= 0)) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "FW_STATUS_LOAD_EMEM FAIL CPU, Status=%x\n",
				 cpustatus);
			goto status_check_fail;
		}

		/* Turn On CPU */
		rtstatus = _rtl92s_firmware_enable_cpu(hw);
		if (!rtstatus) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Enable CPU fail!\n");
			goto status_check_fail;
		}
		break;

	case FW_STATUS_LOAD_DMEM:
		/* Polling DMEM code done */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & DMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & DMEM_CODE_DONE) || (pollingcnt <= 0)) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Polling DMEM code done fail ! cpustatus(%#x)\n",
				 cpustatus);
			goto status_check_fail;
		}

		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "DMEM code download success, cpustatus(%#x)\n",
			 cpustatus);

		/* Prevent Delay too much and being scheduled out */
		/* Polling Load Firmware ready */
		pollingcnt = 2000;
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & FWRDY)
				break;
			udelay(40);
		} while (pollingcnt--);

		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Polling Load Firmware ready, cpustatus(%x)\n",
			 cpustatus);

		if (((cpustatus & LOAD_FW_READY) != LOAD_FW_READY) ||
		    (pollingcnt <= 0)) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Polling Load Firmware ready fail ! cpustatus(%x)\n",
				 cpustatus);
			goto status_check_fail;
		}

		/* If right here, we can set TCR/RCR to desired value  */
		/* and config MAC lookback mode to normal mode */
		tmpu4b = rtl_read_dword(rtlpriv, TCR);
		rtl_write_dword(rtlpriv, TCR, (tmpu4b & (~TCR_ICV)));

		tmpu4b = rtl_read_dword(rtlpriv, RCR);
		rtl_write_dword(rtlpriv, RCR, (tmpu4b | RCR_APPFCS |
				RCR_APP_ICV | RCR_APP_MIC));

		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Current RCR settings(%#x)\n", tmpu4b);

		/* Set to normal mode. */
		rtl_write_byte(rtlpriv, LBKMD_SEL, LBK_NORMAL);
		break;

	default:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 "Unknown status check!\n");
		rtstatus = false;
		break;
	}

status_check_fail:
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "loadfw_status(%d), rtstatus(%x)\n",
		 loadfw_status, rtstatus);
	return rtstatus;
}

int rtl92s_download_fw(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rt_firmware *firmware = NULL;
	struct fw_hdr *pfwheader;
	struct fw_priv *pfw_priv = NULL;
	u8 *puc_mappedfile = NULL;
	u32 ul_filelength = 0;
	u8 fwhdr_size = RT_8192S_FIRMWARE_HDR_SIZE;
	u8 fwstatus = FW_STATUS_INIT;
	bool rtstatus = true;

	if (rtlpriv->max_fw_size == 0 || !rtlhal->pfirmware)
		return 1;

	firmware = (struct rt_firmware *)rtlhal->pfirmware;
	firmware->fwstatus = FW_STATUS_INIT;

	puc_mappedfile = firmware->sz_fw_tmpbuffer;

	/* 1. Retrieve FW header. */
	firmware->pfwheader = (struct fw_hdr *) puc_mappedfile;
	pfwheader = firmware->pfwheader;
	firmware->firmwareversion =  byte(pfwheader->version, 0);
	firmware->pfwheader->fwpriv.hci_sel = 1;/* pcie */

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "signature:%x, version:%x, size:%x, imemsize:%x, sram size:%x\n",
		 pfwheader->signature,
		 pfwheader->version, pfwheader->dmem_size,
		 pfwheader->img_imem_size, pfwheader->img_sram_size);

	/* 2. Retrieve IMEM image. */
	if ((pfwheader->img_imem_size == 0) || (pfwheader->img_imem_size >
	    sizeof(firmware->fw_imem))) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "memory for data image is less than IMEM required\n");
		goto fail;
	} else {
		puc_mappedfile += fwhdr_size;

		memcpy(firmware->fw_imem, puc_mappedfile,
		       pfwheader->img_imem_size);
		firmware->fw_imem_len = pfwheader->img_imem_size;
	}

	/* 3. Retriecve EMEM image. */
	if (pfwheader->img_sram_size > sizeof(firmware->fw_emem)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "memory for data image is less than EMEM required\n");
		goto fail;
	} else {
		puc_mappedfile += firmware->fw_imem_len;

		memcpy(firmware->fw_emem, puc_mappedfile,
		       pfwheader->img_sram_size);
		firmware->fw_emem_len = pfwheader->img_sram_size;
	}

	/* 4. download fw now */
	fwstatus = _rtl92s_firmware_get_nextstatus(firmware->fwstatus);
	while (fwstatus != FW_STATUS_READY) {
		/* Image buffer redirection. */
		switch (fwstatus) {
		case FW_STATUS_LOAD_IMEM:
			puc_mappedfile = firmware->fw_imem;
			ul_filelength = firmware->fw_imem_len;
			break;
		case FW_STATUS_LOAD_EMEM:
			puc_mappedfile = firmware->fw_emem;
			ul_filelength = firmware->fw_emem_len;
			break;
		case FW_STATUS_LOAD_DMEM:
			/* Partial update the content of header private. */
			pfwheader = firmware->pfwheader;
			pfw_priv = &pfwheader->fwpriv;
			_rtl92s_firmwareheader_priveupdate(hw, pfw_priv);
			puc_mappedfile = (u8 *)(firmware->pfwheader) +
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;
			ul_filelength = fwhdr_size -
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unexpected Download step!!\n");
			goto fail;
		}

		/* <2> Download image file */
		rtstatus = _rtl92s_firmware_downloadcode(hw, puc_mappedfile,
				ul_filelength);

		if (!rtstatus) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "fail!\n");
			goto fail;
		}

		/* <3> Check whether load FW process is ready */
		rtstatus = _rtl92s_firmware_checkready(hw, fwstatus);
		if (!rtstatus) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "fail!\n");
			goto fail;
		}

		fwstatus = _rtl92s_firmware_get_nextstatus(firmware->fwstatus);
	}

	return rtstatus;
fail:
	return 0;
}

static u32 _rtl92s_fill_h2c_cmd(struct sk_buff *skb, u32 h2cbufferlen,
				u32 cmd_num, u32 *pelement_id, u32 *pcmd_len,
				u8 **pcmb_buffer, u8 *cmd_start_seq)
{
	u32 totallen = 0, len = 0, tx_desclen = 0;
	u32 pre_continueoffset = 0;
	u8 *ph2c_buffer;
	u8 i = 0;

	do {
		/* 8 - Byte aligment */
		len = H2C_TX_CMD_HDR_LEN + N_BYTE_ALIGMENT(pcmd_len[i], 8);

		/* Buffer length is not enough */
		if (h2cbufferlen < totallen + len + tx_desclen)
			break;

		/* Clear content */
		ph2c_buffer = (u8 *)skb_put(skb, (u32)len);
		memset((ph2c_buffer + totallen + tx_desclen), 0, len);

		/* CMD len */
		SET_BITS_TO_LE_4BYTE((ph2c_buffer + totallen + tx_desclen),
				      0, 16, pcmd_len[i]);

		/* CMD ID */
		SET_BITS_TO_LE_4BYTE((ph2c_buffer + totallen + tx_desclen),
				      16, 8, pelement_id[i]);

		/* CMD Sequence */
		*cmd_start_seq = *cmd_start_seq % 0x80;
		SET_BITS_TO_LE_4BYTE((ph2c_buffer + totallen + tx_desclen),
				      24, 7, *cmd_start_seq);
		++*cmd_start_seq;

		/* Copy memory */
		memcpy((ph2c_buffer + totallen + tx_desclen +
			H2C_TX_CMD_HDR_LEN), pcmb_buffer[i], pcmd_len[i]);

		/* CMD continue */
		/* set the continue in prevoius cmd. */
		if (i < cmd_num - 1)
			SET_BITS_TO_LE_4BYTE((ph2c_buffer + pre_continueoffset),
					      31, 1, 1);

		pre_continueoffset = totallen;

		totallen += len;
	} while (++i < cmd_num);

	return totallen;
}

static u32 _rtl92s_get_h2c_cmdlen(u32 h2cbufferlen, u32 cmd_num, u32 *pcmd_len)
{
	u32 totallen = 0, len = 0, tx_desclen = 0;
	u8 i = 0;

	do {
		/* 8 - Byte aligment */
		len = H2C_TX_CMD_HDR_LEN + N_BYTE_ALIGMENT(pcmd_len[i], 8);

		/* Buffer length is not enough */
		if (h2cbufferlen < totallen + len + tx_desclen)
			break;

		totallen += len;
	} while (++i < cmd_num);

	return totallen + tx_desclen;
}

static bool _rtl92s_firmware_set_h2c_cmd(struct ieee80211_hw *hw, u8 h2c_cmd,
					 u8 *pcmd_buffer)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_tcb_desc *cb_desc;
	struct sk_buff *skb;
	u32	element_id = 0;
	u32	cmd_len = 0;
	u32	len;

	switch (h2c_cmd) {
	case FW_H2C_SETPWRMODE:
		element_id = H2C_SETPWRMODE_CMD ;
		cmd_len = sizeof(struct h2c_set_pwrmode_parm);
		break;
	case FW_H2C_JOINBSSRPT:
		element_id = H2C_JOINBSSRPT_CMD;
		cmd_len = sizeof(struct h2c_joinbss_rpt_parm);
		break;
	case FW_H2C_WOWLAN_UPDATE_GTK:
		element_id = H2C_WOWLAN_UPDATE_GTK_CMD;
		cmd_len = sizeof(struct h2c_wpa_two_way_parm);
		break;
	case FW_H2C_WOWLAN_UPDATE_IV:
		element_id = H2C_WOWLAN_UPDATE_IV_CMD;
		cmd_len = sizeof(unsigned long long);
		break;
	case FW_H2C_WOWLAN_OFFLOAD:
		element_id = H2C_WOWLAN_FW_OFFLOAD;
		cmd_len = sizeof(u8);
		break;
	default:
		break;
	}

	len = _rtl92s_get_h2c_cmdlen(MAX_TRANSMIT_BUFFER_SIZE, 1, &cmd_len);
	skb = dev_alloc_skb(len);
	if (!skb)
		return false;
	cb_desc = (struct rtl_tcb_desc *)(skb->cb);
	cb_desc->queue_index = TXCMD_QUEUE;
	cb_desc->cmd_or_init = DESC_PACKET_TYPE_NORMAL;
	cb_desc->last_inipkt = false;

	_rtl92s_fill_h2c_cmd(skb, MAX_TRANSMIT_BUFFER_SIZE, 1, &element_id,
			&cmd_len, &pcmd_buffer,	&rtlhal->h2c_txcmd_seq);
	_rtl92s_cmd_send_packet(hw, skb, false);
	rtlpriv->cfg->ops->tx_polling(hw, TXCMD_QUEUE);

	return true;
}

void rtl92s_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 Mode)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct h2c_set_pwrmode_parm	pwrmode;
	u16 max_wakeup_period = 0;

	pwrmode.mode = Mode;
	pwrmode.flag_low_traffic_en = 0;
	pwrmode.flag_lpnav_en = 0;
	pwrmode.flag_rf_low_snr_en = 0;
	pwrmode.flag_dps_en = 0;
	pwrmode.bcn_rx_en = 0;
	pwrmode.bcn_to = 0;
	SET_BITS_TO_LE_2BYTE((u8 *)(&pwrmode) + 8, 0, 16,
			mac->vif->bss_conf.beacon_int);
	pwrmode.app_itv = 0;
	pwrmode.awake_bcn_itvl = ppsc->reg_max_lps_awakeintvl;
	pwrmode.smart_ps = 1;
	pwrmode.bcn_pass_period = 10;

	/* Set beacon pass count */
	if (pwrmode.mode == FW_PS_MIN_MODE)
		max_wakeup_period = mac->vif->bss_conf.beacon_int;
	else if (pwrmode.mode == FW_PS_MAX_MODE)
		max_wakeup_period = mac->vif->bss_conf.beacon_int *
			mac->vif->bss_conf.dtim_period;

	if (max_wakeup_period >= 500)
		pwrmode.bcn_pass_cnt = 1;
	else if ((max_wakeup_period >= 300) && (max_wakeup_period < 500))
		pwrmode.bcn_pass_cnt = 2;
	else if ((max_wakeup_period >= 200) && (max_wakeup_period < 300))
		pwrmode.bcn_pass_cnt = 3;
	else if ((max_wakeup_period >= 20) && (max_wakeup_period < 200))
		pwrmode.bcn_pass_cnt = 5;
	else
		pwrmode.bcn_pass_cnt = 1;

	_rtl92s_firmware_set_h2c_cmd(hw, FW_H2C_SETPWRMODE, (u8 *)&pwrmode);

}

void rtl92s_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw,
		u8 mstatus, u8 ps_qosinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct h2c_joinbss_rpt_parm joinbss_rpt;

	joinbss_rpt.opmode = mstatus;
	joinbss_rpt.ps_qos_info = ps_qosinfo;
	joinbss_rpt.bssid[0] = mac->bssid[0];
	joinbss_rpt.bssid[1] = mac->bssid[1];
	joinbss_rpt.bssid[2] = mac->bssid[2];
	joinbss_rpt.bssid[3] = mac->bssid[3];
	joinbss_rpt.bssid[4] = mac->bssid[4];
	joinbss_rpt.bssid[5] = mac->bssid[5];
	SET_BITS_TO_LE_2BYTE((u8 *)(&joinbss_rpt) + 8, 0, 16,
			mac->vif->bss_conf.beacon_int);
	SET_BITS_TO_LE_2BYTE((u8 *)(&joinbss_rpt) + 10, 0, 16, mac->assoc_id);

	_rtl92s_firmware_set_h2c_cmd(hw, FW_H2C_JOINBSSRPT, (u8 *)&joinbss_rpt);
}

