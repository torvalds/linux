// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../cam.h"
#include "../usb.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/dm_common.h"
#include "../rtl8192d/fw_common.h"
#include "../rtl8192d/hw_common.h"
#include "../rtl8192d/phy_common.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "hw.h"
#include "trx.h"

static void _rtl92du_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlusb->reg_bcn_ctrl_val |= set_bits;
	rtlusb->reg_bcn_ctrl_val &= ~clear_bits;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlusb->reg_bcn_ctrl_val);
}

static void _rtl92du_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92du_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92du_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92du_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

void rtl92du_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *)val) = mac->rx_conf;
		break;
	default:
		rtl92d_get_hw_reg(hw, variable, val);
		break;
	}
}

void rtl92du_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	switch (variable) {
	case HW_VAR_AC_PARAM:
		rtl92d_dm_init_edca_turbo(hw);
		break;
	case HW_VAR_ACM_CTRL: {
		u8 e_aci = *val;
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&mac->ac[0].aifs);
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		if (acm) {
			switch (e_aci) {
			case AC0_BE:
				acm_ctrl |= ACMHW_BEQEN;
				break;
			case AC2_VI:
				acm_ctrl |= ACMHW_VIQEN;
				break;
			case AC3_VO:
				acm_ctrl |= ACMHW_VOQEN;
				break;
			default:
				rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
					"HW_VAR_ACM_CTRL acm set failed: eACI is %d\n",
					acm);
				break;
			}
		} else {
			switch (e_aci) {
			case AC0_BE:
				acm_ctrl &= (~ACMHW_BEQEN);
				break;
			case AC2_VI:
				acm_ctrl &= (~ACMHW_VIQEN);
				break;
			case AC3_VO:
				acm_ctrl &= (~ACMHW_VOQEN);
				break;
			default:
				pr_err("%s:%d switch case %#x not processed\n",
				       __func__, __LINE__, e_aci);
				break;
			}
		}
		rtl_dbg(rtlpriv, COMP_QOS, DBG_TRACE,
			"SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		break;
	}
	case HW_VAR_RCR:
		mac->rx_conf = ((u32 *)val)[0];
		rtl_write_dword(rtlpriv, REG_RCR, mac->rx_conf);
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8 tmp_regcr, tmp_reg422;
		bool recover = false;
		u8 mstatus = *val;

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AID, NULL);
			tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       tmp_regcr | ENSWBCN);
			_rtl92du_set_bcn_ctrl_reg(hw, 0, EN_BCN_FUNCTION);
			_rtl92du_set_bcn_ctrl_reg(hw, DIS_TSF_UDT, 0);
			tmp_reg422 = rtl_read_byte(rtlpriv,
						   REG_FWHW_TXQ_CTRL + 2);
			if (tmp_reg422 & (EN_BCNQ_DL >> 16))
				recover = true;
			rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
				       tmp_reg422 & ~(EN_BCNQ_DL >> 16));

			/* We don't implement FW LPS so this is not needed. */
			/* rtl92d_set_fw_rsvdpagepkt(hw, 0); */

			_rtl92du_set_bcn_ctrl_reg(hw, EN_BCN_FUNCTION, 0);
			_rtl92du_set_bcn_ctrl_reg(hw, 0, DIS_TSF_UDT);
			if (recover)
				rtl_write_byte(rtlpriv,
					       REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       tmp_regcr & ~ENSWBCN);
		}
		rtl92d_set_fw_joinbss_report_cmd(hw, (*val));
		break;
	}
	case HW_VAR_CORRECT_TSF: {
		u8 btype_ibss = val[0];

		if (btype_ibss)
			rtl92d_stop_tx_beacon(hw);
		_rtl92du_set_bcn_ctrl_reg(hw, 0, EN_BCN_FUNCTION);
		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32)(mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32)((mac->tsf >> 32) & 0xffffffff));
		_rtl92du_set_bcn_ctrl_reg(hw, EN_BCN_FUNCTION, 0);
		if (btype_ibss)
			rtl92d_resume_tx_beacon(hw);

		break;
	}
	case HW_VAR_KEEP_ALIVE:
		/* Avoid "switch case not processed" error. RTL8192DU doesn't
		 * need to do anything here, maybe.
		 */
		break;
	default:
		rtl92d_set_hw_reg(hw, variable, val);
		break;
	}
}

static void _rtl92du_init_queue_reserved_page(struct ieee80211_hw *hw,
					      u8 out_ep_num,
					      u8 queue_sel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 txqpagenum, txqpageunit;
	u32 txqremainingpage;
	u32 value32 = 0;
	u32 numhq = 0;
	u32 numlq = 0;
	u32 numnq = 0;
	u32 numpubq;

	if (rtlhal->macphymode != SINGLEMAC_SINGLEPHY) {
		numpubq = NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC;
		txqpagenum = TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC - numpubq;
	} else {
		numpubq = TEST_PAGE_NUM_PUBQ_92DU;
		txqpagenum = TX_TOTAL_PAGE_NUMBER_92DU - numpubq;
	}

	if (rtlhal->macphymode != SINGLEMAC_SINGLEPHY && out_ep_num == 3) {
		numhq = NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC;
		numlq = NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC;
		numnq = NORMAL_PAGE_NUM_NORMALQ_92D_DUAL_MAC;
	} else {
		txqpageunit = txqpagenum / out_ep_num;
		txqremainingpage = txqpagenum % out_ep_num;

		if (queue_sel & TX_SELE_HQ)
			numhq = txqpageunit;
		if (queue_sel & TX_SELE_LQ)
			numlq = txqpageunit;
		if (queue_sel & TX_SELE_NQ)
			numnq = txqpageunit;

		/* HIGH priority queue always present in the
		 * configuration of 2 or 3 out-ep. Remainder pages
		 * assigned to High queue
		 */
		if (out_ep_num > 1 && txqremainingpage)
			numhq += txqremainingpage;
	}

	/* NOTE: This step done before writing REG_RQPN. */
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, (u8)numnq);

	/* TX DMA */
	u32p_replace_bits(&value32, numhq, HPQ_MASK);
	u32p_replace_bits(&value32, numlq, LPQ_MASK);
	u32p_replace_bits(&value32, numpubq, PUBQ_MASK);
	value32 |= LD_RQPN;
	rtl_write_dword(rtlpriv, REG_RQPN, value32);
}

static void _rtl92du_init_tx_buffer_boundary(struct ieee80211_hw *hw,
					     u8 txpktbuf_bndy)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);

	/* TXRKTBUG_PG_BNDY */
	rtl_write_byte(rtlpriv, REG_TRXFF_BNDY, txpktbuf_bndy);

	/* Beacon Head for TXDMA */
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);
}

static bool _rtl92du_llt_table_init(struct ieee80211_hw *hw, u8 txpktbuf_bndy)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	bool status;
	u8 maxpage;

	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY)
		maxpage = 255;
	else
		maxpage = 127;

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = rtl92d_llt_write(hw, i, i + 1);
		if (!status)
			return status;
	}

	/* end of list */
	status = rtl92d_llt_write(hw, txpktbuf_bndy - 1, 0xFF);
	if (!status)
		return status;

	/* Make the other pages as ring buffer
	 * This ring buffer is used as beacon buffer if we
	 * config this MAC as two MAC transfer.
	 * Otherwise used as local loopback buffer.
	 */
	for (i = txpktbuf_bndy; i < maxpage; i++) {
		status = rtl92d_llt_write(hw, i, i + 1);
		if (!status)
			return status;
	}

	/* Let last entry point to the start entry of ring buffer */
	status = rtl92d_llt_write(hw, maxpage, txpktbuf_bndy);
	if (!status)
		return status;

	return true;
}

static void _rtl92du_init_chipn_reg_priority(struct ieee80211_hw *hw, u16 beq,
					     u16 bkq, u16 viq, u16 voq,
					     u16 mgtq, u16 hiq)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 value16;

	value16 = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL) & 0x7;
	u16p_replace_bits(&value16, beq, TXDMA_BEQ_MAP);
	u16p_replace_bits(&value16, bkq, TXDMA_BKQ_MAP);
	u16p_replace_bits(&value16, viq, TXDMA_VIQ_MAP);
	u16p_replace_bits(&value16, voq, TXDMA_VOQ_MAP);
	u16p_replace_bits(&value16, mgtq, TXDMA_MGQ_MAP);
	u16p_replace_bits(&value16, hiq, TXDMA_HIQ_MAP);
	rtl_write_word(rtlpriv,  REG_TRXDMA_CTRL, value16);
}

static void _rtl92du_init_chipn_one_out_ep_priority(struct ieee80211_hw *hw,
						    u8 queue_sel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 value;

	switch (queue_sel) {
	case TX_SELE_HQ:
		value = QUEUE_HIGH;
		break;
	case TX_SELE_LQ:
		value = QUEUE_LOW;
		break;
	case TX_SELE_NQ:
		value = QUEUE_NORMAL;
		break;
	default:
		WARN_ON(1); /* Shall not reach here! */
		return;
	}
	_rtl92du_init_chipn_reg_priority(hw, value, value, value, value,
					 value, value);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"Tx queue select: 0x%02x\n", queue_sel);
}

static void _rtl92du_init_chipn_two_out_ep_priority(struct ieee80211_hw *hw,
						    u8 queue_sel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 beq, bkq, viq, voq, mgtq, hiq;
	u16 valuehi, valuelow;

	switch (queue_sel) {
	default:
		WARN_ON(1);
		fallthrough;
	case (TX_SELE_HQ | TX_SELE_LQ):
		valuehi = QUEUE_HIGH;
		valuelow = QUEUE_LOW;
		break;
	case (TX_SELE_NQ | TX_SELE_LQ):
		valuehi = QUEUE_NORMAL;
		valuelow = QUEUE_LOW;
		break;
	case (TX_SELE_HQ | TX_SELE_NQ):
		valuehi = QUEUE_HIGH;
		valuelow = QUEUE_NORMAL;
		break;
	}

	beq = valuelow;
	bkq = valuelow;
	viq = valuehi;
	voq = valuehi;
	mgtq = valuehi;
	hiq = valuehi;

	_rtl92du_init_chipn_reg_priority(hw, beq, bkq, viq, voq, mgtq, hiq);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"Tx queue select: 0x%02x\n", queue_sel);
}

static void _rtl92du_init_chipn_three_out_ep_priority(struct ieee80211_hw *hw,
						      u8 queue_sel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 beq, bkq, viq, voq, mgtq, hiq;

	beq = QUEUE_LOW;
	bkq = QUEUE_LOW;
	viq = QUEUE_NORMAL;
	voq = QUEUE_HIGH;
	mgtq = QUEUE_HIGH;
	hiq = QUEUE_HIGH;

	_rtl92du_init_chipn_reg_priority(hw, beq, bkq, viq, voq, mgtq, hiq);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"Tx queue select: 0x%02x\n", queue_sel);
}

static void _rtl92du_init_queue_priority(struct ieee80211_hw *hw,
					 u8 out_ep_num,
					 u8 queue_sel)
{
	switch (out_ep_num) {
	case 1:
		_rtl92du_init_chipn_one_out_ep_priority(hw, queue_sel);
		break;
	case 2:
		_rtl92du_init_chipn_two_out_ep_priority(hw, queue_sel);
		break;
	case 3:
		_rtl92du_init_chipn_three_out_ep_priority(hw, queue_sel);
		break;
	default:
		WARN_ON(1); /* Shall not reach here! */
		break;
	}
}

static void _rtl92du_init_wmac_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	mac->rx_conf = RCR_APM | RCR_AM | RCR_AB | RCR_ADF | RCR_APP_ICV |
		       RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC |
		       RCR_APP_PHYST_RXFF | RCR_APPFCS;

	rtl_write_dword(rtlpriv, REG_RCR, mac->rx_conf);

	/* Set Multicast Address. */
	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);
}

static void _rtl92du_init_adaptive_ctrl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 val32;

	val32 = rtl_read_dword(rtlpriv, REG_RRSR);
	val32 &= ~0xfffff;
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G)
		val32 |= 0xffff0; /* No CCK */
	else
		val32 |= 0xffff1;
	rtl_write_dword(rtlpriv, REG_RRSR, val32);

	/* Set Spec SIFS (used in NAV) */
	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x1010);

	/* Retry limit 0x30 */
	rtl_write_word(rtlpriv, REG_RL, 0x3030);
}

static void _rtl92du_init_edca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 val16;

	/* Disable EDCCA count down, to reduce collison and retry */
	val16 = rtl_read_word(rtlpriv, REG_RD_CTRL);
	val16 |= DIS_EDCA_CNT_DWN;
	rtl_write_word(rtlpriv, REG_RD_CTRL, val16);

	/* CCK SIFS shall always be 10us. */
	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x0a0a);
	/* Set SIFS for OFDM */
	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x1010);

	rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x0204);

	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x014004);

	/* TXOP */
	rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM, 0x005EA324);
	rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM, 0x002FA226);

	rtl_write_byte(rtlpriv, REG_PIFS, 0x1C);

	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);

	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);

	rtl_write_byte(rtlpriv, REG_BCNDMATIM, 0x2);
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);
}

static void _rtl92du_init_retry_function(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val8;

	val8 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL);
	val8 |= EN_AMPDU_RTY_NEW;
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL, val8);

	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);
}

static void _rtl92du_init_operation_mode(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	rtl_write_byte(rtlpriv, REG_BWOPMODE, BW_OPMODE_20MHZ);

	switch (rtlpriv->phy.rf_type) {
	case RF_1T2R:
	case RF_1T1R:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_1T << 3);
		break;
	case RF_2T2R:
	case RF_2T2R_GREEN:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_2T << 3);
		break;
	}
	rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE, rtlhal->minspace_cfg);
}

static void _rtl92du_init_beacon_parameters(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_word(rtlpriv, REG_BCN_CTRL, 0x1010);

	rtl_write_word(rtlpriv, REG_TBTT_PROHIBIT, 0x3c02);
	rtl_write_byte(rtlpriv, REG_DRVERLYINT, 0x05);
	rtl_write_byte(rtlpriv, REG_BCNDMATIM, 0x03);

	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
}

static void _rtl92du_init_ampdu_aggregation(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	/* Aggregation threshold */
	if (rtlhal->macphymode == DUALMAC_DUALPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x66525541);
	else if (rtlhal->macphymode == DUALMAC_SINGLEPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x44444441);
	else
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x88728841);

	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
}

static bool _rtl92du_init_power_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short wordtmp;
	unsigned char bytetmp;
	u16 retry = 0;

	do {
		if (rtl_read_byte(rtlpriv, REG_APS_FSMCO) & PFM_ALDN)
			break;

		if (retry++ > 1000)
			return false;
	} while (true);

	/* Unlock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);

	/* SPS0_CTRL 0x11[7:0] = 0x2b  enable SPS into PWM mode */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);

	msleep(1);

	bytetmp = rtl_read_byte(rtlpriv, REG_LDOV12D_CTRL);
	if ((bytetmp & LDV12_EN) == 0) {
		bytetmp |= LDV12_EN;
		rtl_write_byte(rtlpriv, REG_LDOV12D_CTRL, bytetmp);

		msleep(1);

		bytetmp = rtl_read_byte(rtlpriv, REG_SYS_ISO_CTRL);
		bytetmp &= ~ISO_MD2PP;
		rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, bytetmp);
	}

	/* Auto enable WLAN */
	wordtmp = rtl_read_word(rtlpriv, REG_APS_FSMCO);
	wordtmp |= APFM_ONMAC;
	rtl_write_word(rtlpriv, REG_APS_FSMCO, wordtmp);

	wordtmp = rtl_read_word(rtlpriv, REG_APS_FSMCO);
	retry = 0;
	while ((wordtmp & APFM_ONMAC) && retry < 1000) {
		retry++;
		wordtmp = rtl_read_word(rtlpriv, REG_APS_FSMCO);
	}

	/* Release RF digital isolation */
	wordtmp = rtl_read_word(rtlpriv, REG_SYS_ISO_CTRL);
	wordtmp &= ~ISO_DIOR;
	rtl_write_word(rtlpriv, REG_SYS_ISO_CTRL, wordtmp);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	wordtmp = rtl_read_word(rtlpriv, REG_CR);
	wordtmp |= HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN |
		   PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC;
	rtl_write_word(rtlpriv, REG_CR, wordtmp);

	return true;
}

static bool _rtl92du_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val8;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);

	val8 = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	val8 &= ~(FEN_MREGEN >> 8);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, val8);

	/* For s3/s4 may reset mac, Reg0xf8 may be set to 0,
	 * so reset macphy control reg here.
	 */
	rtl92d_phy_config_macphymode(hw);

	rtl92du_phy_set_poweron(hw);

	if (!_rtl92du_init_power_on(hw)) {
		pr_err("Failed to init power on!\n");
		return false;
	}

	rtl92d_phy_config_maccoexist_rfpage(hw);

	return true;
}

int rtl92du_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 val8, txpktbuf_bndy;
	int err, i;
	u32 val32;
	u16 val16;

	mutex_lock(rtlpriv->mutex_for_hw_init);

	/* we should do iqk after disable/enable */
	rtl92d_phy_reset_iqk_result(hw);

	if (!_rtl92du_init_mac(hw)) {
		pr_err("Init MAC failed\n");
		mutex_unlock(rtlpriv->mutex_for_hw_init);
		return 1;
	}

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY)
		txpktbuf_bndy = 249;
	else
		txpktbuf_bndy = 123;

	if (!_rtl92du_llt_table_init(hw, txpktbuf_bndy)) {
		pr_err("Init LLT failed\n");
		mutex_unlock(rtlpriv->mutex_for_hw_init);
		return 1;
	}

	err = rtl92du_download_fw(hw);

	/* return fail only when part number check fail */
	if (err && rtl_read_byte(rtlpriv, 0x1c5) == 0xe0) {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"Failed to download FW. Init HW without FW..\n");
		mutex_unlock(rtlpriv->mutex_for_hw_init);
		return 1;
	}
	rtlhal->last_hmeboxnum = 0;
	rtlpriv->psc.fw_current_inpsmode = false;

	rtl92du_phy_mac_config(hw);

	/* Set reserved page for each queue */
	_rtl92du_init_queue_reserved_page(hw, rtlusb->out_ep_nums,
					  rtlusb->out_queue_sel);

	_rtl92du_init_tx_buffer_boundary(hw, txpktbuf_bndy);

	_rtl92du_init_queue_priority(hw, rtlusb->out_ep_nums,
				     rtlusb->out_queue_sel);

	/* Set Tx/Rx page size (Tx must be 128 Bytes,
	 * Rx can be 64, 128, 256, 512, 1024 bytes)
	 */
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);

	/* Get Rx PHY status in order to report RSSI and others. */
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_HIMR, 0xffffffff);

	val8 = rtl_read_byte(rtlpriv, MSR);
	val8 &= ~MSR_MASK;
	val8 |= MSR_INFRA;
	rtl_write_byte(rtlpriv, MSR, val8);

	_rtl92du_init_wmac_setting(hw);
	_rtl92du_init_adaptive_ctrl(hw);
	_rtl92du_init_edca(hw);

	rtl_write_dword(rtlpriv, REG_DARFRC, 0x00000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x10080404);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x04030201);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x08070605);

	_rtl92du_init_retry_function(hw);
	/* _InitUsbAggregationSetting(padapter); no aggregation for now */
	_rtl92du_init_operation_mode(hw);
	_rtl92du_init_beacon_parameters(hw);
	_rtl92du_init_ampdu_aggregation(hw);

	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xff);

	/* unit: 256us. 256ms */
	rtl_write_word(rtlpriv, REG_PKT_VO_VI_LIFE_TIME, 0x0400);
	rtl_write_word(rtlpriv, REG_PKT_BE_BK_LIFE_TIME, 0x0400);

	/* Hardware-controlled blinking. */
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8282);
	rtl_write_byte(rtlpriv, REG_LEDCFG2, 0x82);

	val32 = rtl_read_dword(rtlpriv, REG_TXDMA_OFFSET_CHK);
	val32 |= DROP_DATA_EN;
	rtl_write_dword(rtlpriv, REG_TXDMA_OFFSET_CHK, val32);

	if (mac->rdg_en) {
		rtl_write_byte(rtlpriv, REG_RD_CTRL, 0xff);
		rtl_write_word(rtlpriv, REG_RD_NAV_NXT, 0x200);
		rtl_write_byte(rtlpriv, REG_RD_RESP_PKT_TH, 0x05);
	}

	for (i = 0; i < 4; i++)
		rtl_write_dword(rtlpriv, REG_ARFR0 + i * 4, 0x1f8ffff0);

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY) {
		if (rtlusb->out_ep_nums == 2)
			rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03066666);
		else
			rtl_write_word(rtlpriv, REG_FAST_EDCA_CTRL, 0x8888);
	} else {
		rtl_write_word(rtlpriv, REG_FAST_EDCA_CTRL, 0x5555);
	}

	val8 = rtl_read_byte(rtlpriv, 0x605);
	val8 |= 0xf0;
	rtl_write_byte(rtlpriv, 0x605, val8);

	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x30);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x30);
	rtl_write_byte(rtlpriv, 0x606, 0x30);

	/* temp for high queue and mgnt Queue corrupt in time; it may
	 * cause hang when sw beacon use high_Q, other frame use mgnt_Q;
	 * or, sw beacon use mgnt_Q, other frame use high_Q;
	 */
	rtl_write_byte(rtlpriv, REG_DIS_TXREQ_CLR, 0x10);
	val16 = rtl_read_word(rtlpriv, REG_RD_CTRL);
	val16 |= BIT(12);
	rtl_write_word(rtlpriv, REG_RD_CTRL, val16);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0);

	/* usb suspend idle time count for bitfile0927 */
	val8 = rtl_read_byte(rtlpriv, 0xfe56);
	val8 |= BIT(0) | BIT(1);
	rtl_write_byte(rtlpriv, 0xfe56, val8);

	if (rtlhal->earlymode_enable) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"EarlyMode Enabled!!!\n");

		val8 = rtl_read_byte(rtlpriv, REG_EARLY_MODE_CONTROL);
		val8 |= 0x1f;
		rtl_write_byte(rtlpriv, REG_EARLY_MODE_CONTROL, val8);

		rtl_write_byte(rtlpriv, REG_EARLY_MODE_CONTROL + 3, 0x80);

		val8 = rtl_read_byte(rtlpriv, 0x605);
		val8 |= 0x40;
		rtl_write_byte(rtlpriv, 0x605, val8);
	} else {
		rtl_write_byte(rtlpriv, REG_EARLY_MODE_CONTROL, 0);
	}

	rtl92du_phy_bb_config(hw);

	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	/* set before initialize RF */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);

	/* config RF */
	rtl92du_phy_rf_config(hw);

	/* set default value after initialize RF */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0);

	/* After load BB, RF params, we need to do more for 92D. */
	rtl92du_update_bbrf_configuration(hw);

	rtlphy->rfreg_chnlval[0] =
		rtl_get_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] =
		rtl_get_rfreg(hw, RF90_PATH_B, RF_CHNLBW, RFREG_OFFSET_MASK);

	/*---- Set CCK and OFDM Block "ON"----*/
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	/* reset hw sec */
	rtl_cam_reset_all_entry(hw);
	rtl92d_enable_hw_security_config(hw);

	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, 0xFF);

	/* schmitt trigger, improve tx evm for 92du */
	val8 = rtl_read_byte(rtlpriv, REG_AFE_XTAL_CTRL);
	val8 |= BIT(1);
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, val8);

	/* Disable bar */
	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0xffff);

	/* Nav limit */
	rtl_write_byte(rtlpriv, REG_NAV_CTRL + 2, 0);
	rtl_write_byte(rtlpriv, ROFDM0_XATXAFE + 3, 0x50);

	/* Read EEPROM TX power index and PHY_REG_PG.txt to capture correct
	 * TX power index for different rate set.
	 */
	rtl92d_phy_get_hw_reg_originalvalue(hw);

	ppsc->rfpwr_state = ERFON;

	/* do IQK for 2.4G for better scan result */
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		rtl92du_phy_iq_calibrate(hw);

	rtl92du_phy_lc_calibrate(hw, IS_92D_SINGLEPHY(rtlhal->version));

	rtl92du_phy_init_pa_bias(hw);

	mutex_unlock(rtlpriv->mutex_for_hw_init);

	rtl92du_dm_init(hw);

	/* For 2 PORT TSF SYNC */
	rtl_write_word(rtlpriv, REG_BCN_CTRL, 0x1818);
	rtlusb->reg_bcn_ctrl_val = 0x18;

	udelay(500);

	if (rtlhal->macphymode != DUALMAC_DUALPHY) {
		rtl_write_dword(rtlpriv, RFPGA1_TXINFO,
				rtl_read_dword(rtlpriv, RFPGA1_TXINFO) & ~BIT(30));

		rtl_write_dword(rtlpriv, RFPGA0_TXGAINSTAGE,
				rtl_read_dword(rtlpriv, RFPGA0_TXGAINSTAGE) & ~BIT(31));

		rtl_write_dword(rtlpriv, ROFDM0_XBTXAFE, 0xa0e40000);
	}

	val32 = rtl_read_dword(rtlpriv, REG_FWHW_TXQ_CTRL);
	val32 |= BIT(12);
	rtl_write_dword(rtlpriv, REG_FWHW_TXQ_CTRL, val32);

	return err;
}

static int _rtl92du_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);

	bt_msr &= 0xfc;

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		rtl92d_stop_tx_beacon(hw);
		_rtl92du_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		   type == NL80211_IFTYPE_AP) {
		rtl92d_resume_tx_beacon(hw);
		_rtl92du_disable_bcn_sub_func(hw);
	} else {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"Set HW_VAR_MEDIA_STATUS: No such media status(%x)\n",
			type);
	}

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= MSR_NOLINK;
		ledaction = LED_CTL_LINK;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= MSR_ADHOC;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= MSR_INFRA;
		ledaction = LED_CTL_LINK;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= MSR_AP;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"Set Network type to AP!\n");
		break;
	default:
		pr_err("Network type %d not supported!\n", type);
		return 1;
	}
	rtl_write_byte(rtlpriv, MSR, bt_msr);

	rtlpriv->cfg->ops->led_control(hw, ledaction);

	if ((bt_msr & MSR_MASK) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);

	return 0;
}

void rtl92du_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_rcr;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));

	if (check_bssid) {
		reg_rcr |= RCR_CBSSID_DATA | RCR_CBSSID_BCN;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)&reg_rcr);
		_rtl92du_set_bcn_ctrl_reg(hw, 0, DIS_TSF_UDT);
	} else if (!check_bssid) {
		reg_rcr &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		_rtl92du_set_bcn_ctrl_reg(hw, DIS_TSF_UDT, 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)&reg_rcr);
	}
}

int rtl92du_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92du_set_media_status(hw, type))
		return -EOPNOTSUPP;

	/* check bssid */
	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl92du_set_check_bssid(hw, true);
	} else {
		rtl92du_set_check_bssid(hw, false);
	}

	return 0;
}

/* do iqk or reload iqk */
/* windows just rtl92d_phy_reload_iqk_setting in set channel,
 * but it's very strict for time sequence so we add
 * rtl92d_phy_reload_iqk_setting here
 */
void rtl92du_linked_set_reg(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 channel = rtlphy->current_channel;
	u8 indexforchannel;

	indexforchannel = rtl92d_get_rightchnlplace_for_iqk(channel);
	if (!rtlphy->iqk_matrix[indexforchannel].iqk_done) {
		rtl_dbg(rtlpriv, COMP_SCAN | COMP_INIT, DBG_DMESG,
			"Do IQK for channel:%d\n", channel);
		rtl92du_phy_iq_calibrate(hw);
	}
}

void rtl92du_enable_interrupt(struct ieee80211_hw *hw)
{
	/* Nothing to do. */
}

void rtl92du_disable_interrupt(struct ieee80211_hw *hw)
{
	/* Nothing to do. */
}

static void _rtl92du_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 retry = 100;
	u8 u1b_tmp;
	u16 val16;
	u32 val32;

	rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x04);

	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);

	/* IF fw in RAM code, do reset */
	if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & MCUFWDL_RDY) {
		rtl_write_byte(rtlpriv, REG_FSIMR, 0);

		/* We need to disable other HRCV INT to influence 8051 reset. */
		rtl_write_byte(rtlpriv, REG_FWIMR, 0x20);

		/* Close mask to prevent incorrect FW write operation. */
		rtl_write_byte(rtlpriv, REG_FTIMR, 0);

		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);

		/* Set (REG_HMETFR + 3) to 0x20 is reset 8051 */
		rtl_write_byte(rtlpriv, REG_HMETFR + 3, 0x20);
		val16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
		while (val16 & FEN_CPUEN) {
			retry--;
			if (retry == 0)
				break;
			udelay(50);
			val16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
		}

		if (retry == 0) {
			rtl_write_byte(rtlpriv, REG_FWIMR, 0);

			/* if 8051 reset fail, reset MAC directly. */
			rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x50);

			mdelay(10);
		}
	}

	/* reset MCU, MAC register, DCORE */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x54);

	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/* Pull GPIO PIN to balance level and LED control */

	/* Disable GPIO[7:0] */
	rtl_write_word(rtlpriv, REG_GPIO_PIN_CTRL + 2, 0x0000);
	val32 = rtl_read_dword(rtlpriv, REG_GPIO_PIN_CTRL);
	u32p_replace_bits(&val32, val32 & 0xff, 0x0000ff00);
	u32p_replace_bits(&val32, 0xff, 0x00ff0000);
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, val32);

	/* Disable GPIO[10:8] */
	rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG, 0);
	val16 = rtl_read_word(rtlpriv, REG_GPIO_IO_SEL);
	u16p_replace_bits(&val16, val16 & 0xf, 0x00f0);
	u16p_replace_bits(&val16, 0xf, 0x0780);
	rtl_write_word(rtlpriv, REG_GPIO_IO_SEL, val16);

	/* Disable LED 0, 1, and 2 */
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8888);
	rtl_write_byte(rtlpriv, REG_LEDCFG2, 0x88);

	/* Disable analog sequence */

	/* enter PFM mode */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x23);

	rtl_write_word(rtlpriv, REG_APS_FSMCO,
		       APDM_HOST | AFSM_HSUS | PFM_ALDN);

	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"In PowerOff,reg0x%x=%X\n",
		REG_SPS0_CTRL, rtl_read_byte(rtlpriv, REG_SPS0_CTRL));

	/* 0x17[7] 1b': power off in process  0b' : power off over */
	if (rtlpriv->rtlhal.macphymode != SINGLEMAC_SINGLEPHY) {
		mutex_lock(rtlpriv->mutex_for_power_on_off);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
		u1b_tmp &= ~BIT(7);
		rtl_write_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS, u1b_tmp);
		mutex_unlock(rtlpriv->mutex_for_power_on_off);
	}

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "<=======\n");
}

void rtl92du_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;
	u32 val32;
	u16 val16;
	u8 val8;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92du_set_media_status(hw, opmode);

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	/* Power sequence for each MAC. */
	/* a. stop tx DMA  */
	/* b. close RF */
	/* c. clear rx buf */
	/* d. stop rx DMA */
	/* e. reset MAC */

	val16 = rtl_read_word(rtlpriv, REG_GPIO_MUXCFG);
	val16 &= ~BIT(12);
	rtl_write_word(rtlpriv, REG_GPIO_MUXCFG, val16);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xff);
	udelay(500);
	rtl_write_byte(rtlpriv, REG_CR, 0);

	/* RF OFF sequence */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x00);

	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);

	val8 = FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTN;
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, val8);

	/* Mac0 can not do Global reset. Mac1 can do. */
	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY ||
	    rtlhal->interfaceindex == 1) {
		/* before BB reset should do clock gated */
		val32 = rtl_read_dword(rtlpriv, RFPGA0_XCD_RFPARAMETER);
		val32 |= BIT(31);
		rtl_write_dword(rtlpriv, RFPGA0_XCD_RFPARAMETER, val32);

		val8 &= ~FEN_BB_GLB_RSTN;
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, val8);
	}

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "==> Do power off.......\n");
	if (!rtl92du_phy_check_poweroff(hw))
		return;

	_rtl92du_poweroff_adapter(hw);
}

void rtl92du_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;
	rtl92du_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x20);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G)
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x30);
	else
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x20);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
}

void rtl92du_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	rtl_dbg(rtlpriv, COMP_BEACON, DBG_DMESG,
		"beacon_interval:%d\n", bcn_interval);
	rtl92du_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl92du_enable_interrupt(hw);
}

void rtl92du_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr)
{
	/* Nothing to do here. */
}

void rtl92du_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* Chip version reading is done in rtl92d_read_eeprom_info. */

	rtlpriv->rtlhal.hw_type = HARDWARE_TYPE_RTL8192DU;
}
