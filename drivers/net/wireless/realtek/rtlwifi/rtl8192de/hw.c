// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../efuse.h"
#include "../base.h"
#include "../regd.h"
#include "../cam.h"
#include "../ps.h"
#include "../pci.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/dm_common.h"
#include "../rtl8192d/fw_common.h"
#include "../rtl8192d/hw_common.h"
#include "../rtl8192d/phy_common.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "led.h"
#include "sw.h"
#include "hw.h"

u32 rtl92de_read_dword_dbi(struct ieee80211_hw *hw, u16 offset, u8 direct)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 value;

	rtl_write_word(rtlpriv, REG_DBI_CTRL, (offset & 0xFFC));
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, BIT(1) | direct);
	udelay(10);
	value = rtl_read_dword(rtlpriv, REG_DBI_RDATA);
	return value;
}

void rtl92de_write_dword_dbi(struct ieee80211_hw *hw,
			     u16 offset, u32 value, u8 direct)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_word(rtlpriv, REG_DBI_CTRL, ((offset & 0xFFC) | 0xF000));
	rtl_write_dword(rtlpriv, REG_DBI_WDATA, value);
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, BIT(0) | direct);
}

static void _rtl92de_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
}

static void _rtl92de_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92de_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92de_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

void rtl92de_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *) (val)) = rtlpci->receive_config;
		break;
	default:
		rtl92d_get_hw_reg(hw, variable, val);
		break;
	}
}

void rtl92de_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_AC_PARAM: {
		u8 e_aci = *val;
		rtl92d_dm_init_edca_turbo(hw);
		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      &e_aci);
		break;
	}
	case HW_VAR_ACM_CTRL: {
		u8 e_aci = *val;
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&(mac->ac[0].aifs));
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl = acm_ctrl | ((rtlpci->acm_method == 2) ?  0x0 : 0x1);
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
				pr_err("switch case %#x not processed\n",
				       e_aci);
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
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
		rtlpci->receive_config = ((u32 *) (val))[0];
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8 mstatus = (*val);
		u8 tmp_regcr, tmp_reg422;
		bool recover = false;

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AID, NULL);
			tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr | BIT(0)));
			_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(3));
			_rtl92de_set_bcn_ctrl_reg(hw, BIT(4), 0);
			tmp_reg422 = rtl_read_byte(rtlpriv,
						 REG_FWHW_TXQ_CTRL + 2);
			if (tmp_reg422 & BIT(6))
				recover = true;
			rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
				       tmp_reg422 & (~BIT(6)));
			rtl92d_set_fw_rsvdpagepkt(hw, 0);
			_rtl92de_set_bcn_ctrl_reg(hw, BIT(3), 0);
			_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(4));
			if (recover)
				rtl_write_byte(rtlpriv,
					       REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr & ~(BIT(0))));
		}
		rtl92d_set_fw_joinbss_report_cmd(hw, (*val));
		break;
	}
	case HW_VAR_CORRECT_TSF: {
		u8 btype_ibss = val[0];

		if (btype_ibss)
			rtl92de_stop_tx_beacon(hw);
		_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(3));
		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32) (mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32) ((mac->tsf >> 32) & 0xffffffff));
		_rtl92de_set_bcn_ctrl_reg(hw, BIT(3), 0);
		if (btype_ibss)
			rtl92de_resume_tx_beacon(hw);

		break;
	}
	case HW_VAR_INT_MIGRATION: {
		bool int_migration = *(bool *) (val);

		if (int_migration) {
			/* Set interrupt migration timer and
			 * corresponding Tx/Rx counter.
			 * timer 25ns*0xfa0=100us for 0xf packets.
			 * 0x306:Rx, 0x307:Tx */
			rtl_write_dword(rtlpriv, REG_INT_MIG, 0xfe000fa0);
			rtlpriv->dm.interrupt_migration = int_migration;
		} else {
			/* Reset all interrupt migration settings. */
			rtl_write_dword(rtlpriv, REG_INT_MIG, 0);
			rtlpriv->dm.interrupt_migration = int_migration;
		}
		break;
	}
	case HW_VAR_INT_AC: {
		bool disable_ac_int = *((bool *) val);

		/* Disable four ACs interrupts. */
		if (disable_ac_int) {
			/* Disable VO, VI, BE and BK four AC interrupts
			 * to gain more efficient CPU utilization.
			 * When extremely highly Rx OK occurs,
			 * we will disable Tx interrupts.
			 */
			rtlpriv->cfg->ops->update_interrupt_mask(hw, 0,
						 RT_AC_INT_MASKS);
			rtlpriv->dm.disable_tx_int = disable_ac_int;
		/* Enable four ACs interrupts. */
		} else {
			rtlpriv->cfg->ops->update_interrupt_mask(hw,
						 RT_AC_INT_MASKS, 0);
			rtlpriv->dm.disable_tx_int = disable_ac_int;
		}
		break;
	}
	default:
		rtl92d_set_hw_reg(hw, variable, val);
		break;
	}
}

static bool _rtl92de_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxpage;
	bool status;
	u32 value32; /* High+low page number */
	u8 value8;	 /* normal page number */

	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY) {
		maxpage = 255;
		txpktbuf_bndy = 246;
		value8 = 0;
		value32 = 0x80bf0d29;
	} else {
		maxpage = 127;
		txpktbuf_bndy = 123;
		value8 = 0;
		value32 = 0x80750005;
	}

	/* Set reserved page for each queue */
	/* 11.  RQPN 0x200[31:0] = 0x80BD1C1C */
	/* load RQPN */
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, value8);
	rtl_write_dword(rtlpriv, REG_RQPN, value32);

	/* 12.  TXRKTBUG_PG_BNDY 0x114[31:0] = 0x27FF00F6 */
	/* TXRKTBUG_PG_BNDY */
	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY,
			(rtl_read_word(rtlpriv, REG_TRXFF_BNDY + 2) << 16 |
			txpktbuf_bndy));

	/* 13.  TDECTRL[15:8] 0x209[7:0] = 0xF6 */
	/* Beacon Head for TXDMA */
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	/* 14.  BCNQ_PGBNDY 0x424[7:0] =  0xF6 */
	/* BCNQ_PGBNDY */
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	/* 15.  WMAC_LBK_BF_HD 0x45D[7:0] =  0xF6 */
	/* WMAC_LBK_BF_HD */
	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);

	/* Set Tx/Rx page size (Tx must be 128 Bytes, */
	/* Rx can be 64,128,256,512,1024 bytes) */
	/* 16.  PBP [7:0] = 0x11 */
	/* TRX page size */
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);

	/* 17.  DRV_INFO_SZ = 0x04 */
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	/* 18.  LLT_table_init(Adapter);  */
	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = rtl92de_llt_write(hw, i, i + 1);
		if (!status)
			return status;
	}

	/* end of list */
	status = rtl92de_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (!status)
		return status;

	/* Make the other pages as ring buffer */
	/* This ring buffer is used as beacon buffer if we */
	/* config this MAC as two MAC transfer. */
	/* Otherwise used as local loopback buffer.  */
	for (i = txpktbuf_bndy; i < maxpage; i++) {
		status = rtl92de_llt_write(hw, i, (i + 1));
		if (!status)
			return status;
	}

	/* Let last entry point to the start entry of ring buffer */
	status = rtl92de_llt_write(hw, maxpage, txpktbuf_bndy);
	if (!status)
		return status;

	return true;
}

static void _rtl92de_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	enum rtl_led_pin pin0 = rtlpriv->ledctl.sw_led0;

	if (rtlpci->up_first_time)
		return;
	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92de_sw_led_on(hw, pin0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl92de_sw_led_on(hw, pin0);
	else
		rtl92de_sw_led_off(hw, pin0);
}

static bool _rtl92de_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	unsigned char bytetmp;
	unsigned short wordtmp;
	u16 retry;

	rtl92d_phy_set_poweron(hw);
	/* Add for resume sequence of power domain according
	 * to power document V11. Chapter V.11....  */
	/* 0.   RSV_CTRL 0x1C[7:0] = 0x00  */
	/* unlock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);
	rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x05);

	/* 1.   AFE_XTAL_CTRL [7:0] = 0x0F  enable XTAL */
	/* 2.   SPS0_CTRL 0x11[7:0] = 0x2b  enable SPS into PWM mode  */
	/* 3.   delay (1ms) this is not necessary when initially power on */

	/* C.   Resume Sequence */
	/* a.   SPS0_CTRL 0x11[7:0] = 0x2b */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);

	/* b.   AFE_XTAL_CTRL [7:0] = 0x0F */
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, 0x0F);

	/* c.   DRV runs power on init flow */

	/* auto enable WLAN */
	/* 4.   APS_FSMCO 0x04[8] = 1; wait till 0x04[8] = 0   */
	/* Power On Reset for MAC Block */
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) | BIT(0);
	udelay(2);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);
	udelay(2);

	/* 5.   Wait while 0x04[8] == 0 goto 2, otherwise goto 1 */
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
	udelay(50);
	retry = 0;
	while ((bytetmp & BIT(0)) && retry < 1000) {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
		udelay(50);
	}

	/* Enable Radio off, GPIO, and LED function */
	/* 6.   APS_FSMCO 0x04[15:0] = 0x0012  when enable HWPDN */
	rtl_write_word(rtlpriv, REG_APS_FSMCO, 0x1012);

	/* release RF digital isolation  */
	/* 7.  SYS_ISO_CTRL 0x01[1]    = 0x0;  */
	/*Set REG_SYS_ISO_CTRL 0x1=0x82 to prevent wake# problem. */
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, 0x82);
	udelay(2);

	/* make sure that BB reset OK. */
	/* rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3); */

	/* Disable REG_CR before enable it to assure reset */
	rtl_write_word(rtlpriv, REG_CR, 0x0);

	/* Release MAC IO register reset */
	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	/* clear stopping tx/rx dma   */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0x0);

	/* rtl_write_word(rtlpriv,REG_CR+2, 0x2); */

	/* System init */
	/* 18.  LLT_table_init(Adapter);  */
	if (!_rtl92de_llt_table_init(hw))
		return false;

	/* Clear interrupt and enable interrupt */
	/* 19.  HISR 0x124[31:0] = 0xffffffff;  */
	/*      HISRE 0x12C[7:0] = 0xFF */
	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_byte(rtlpriv, REG_HISRE, 0xff);

	/* 20.  HIMR 0x120[31:0] |= [enable INT mask bit map];  */
	/* 21.  HIMRE 0x128[7:0] = [enable INT mask bit map] */
	/* The IMR should be enabled later after all init sequence
	 * is finished. */

	/* 22.  PCIE configuration space configuration */
	/* 23.  Ensure PCIe Device 0x80[15:0] = 0x0143 (ASPM+CLKREQ),  */
	/*      and PCIe gated clock function is enabled.    */
	/* PCIE configuration space will be written after
	 * all init sequence.(Or by BIOS) */

	rtl92d_phy_config_maccoexist_rfpage(hw);

	/* THe below section is not related to power document Vxx . */
	/* This is only useful for driver and OS setting. */
	/* -------------------Software Relative Setting---------------------- */
	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF771;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	/* Reported Tx status from HW for rate adaptive. */
	/* This should be realtive to power on step 14. But in document V11  */
	/* still not contain the description.!!! */
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);

	/* Set Tx/Rx page size (Tx must be 128 Bytes,
	 * Rx can be 64,128,256,512,1024 bytes) */
	/* rtl_write_byte(rtlpriv,REG_PBP, 0x11); */

	/* Set RCR register */
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	/* rtl_write_byte(rtlpriv,REG_RX_DRVINFO_SZ, 4); */

	/*  Set TCR register */
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);

	/* disable earlymode */
	rtl_write_byte(rtlpriv, 0x4d0, 0x0);

	/* Set TX/RX descriptor physical address(from OS API). */
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			rtlpci->tx_ring[BEACON_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_MGQ_DESA, rtlpci->tx_ring[MGNT_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_VOQ_DESA, rtlpci->tx_ring[VO_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_VIQ_DESA, rtlpci->tx_ring[VI_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_BEQ_DESA, rtlpci->tx_ring[BE_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_BKQ_DESA, rtlpci->tx_ring[BK_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_HQ_DESA, rtlpci->tx_ring[HIGH_QUEUE].dma);
	/* Set RX Desc Address */
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			rtlpci->rx_ring[RX_MPDU_QUEUE].dma);

	/* if we want to support 64 bit DMA, we should set it here,
	 * but now we do not support 64 bit DMA*/

	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x33);

	/* Reset interrupt migration setting when initialization */
	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	/* Reconsider when to do this operation after asking HWSD. */
	bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, bytetmp & ~BIT(6));
	do {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	} while ((retry < 200) && !(bytetmp & BIT(7)));

	/* After MACIO reset,we must refresh LED state. */
	_rtl92de_gen_refresh_led_state(hw);

	/* Reset H2C protection register */
	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	return true;
}

static void _rtl92de_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 reg_bw_opmode = BW_OPMODE_20MHZ;
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
	rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL, 0x8);
	rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);
	rtl_write_byte(rtlpriv, REG_SLOT, 0x09);
	rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE, 0x0);
	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F80);
	rtl_write_word(rtlpriv, REG_RL, 0x0707);
	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x02012802);
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, 0xFF);
	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);
	/* Aggregation threshold */
	if (rtlhal->macphymode == DUALMAC_DUALPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0xb9726641);
	else if (rtlhal->macphymode == DUALMAC_SINGLEPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x66626641);
	else
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0xb972a841);
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0x0a);
	rtlpci->reg_bcn_ctrl_val = 0x1f;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	rtl_write_byte(rtlpriv, REG_PIFS, 0x1C);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
	/* For throughput */
	rtl_write_word(rtlpriv, REG_FAST_EDCA_CTRL, 0x6666);
	/* ACKTO for IOT issue. */
	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);
	/* Set Spec SIFS (used in NAV) */
	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x1010);
	rtl_write_word(rtlpriv, REG_MAC_SPEC_SIFS, 0x1010);
	/* Set SIFS for CCK */
	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x1010);
	/* Set SIFS for OFDM */
	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x1010);
	/* Set Multicast Address. */
	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);
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
}

static void _rtl92de_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	rtl_write_byte(rtlpriv, 0x34b, 0x93);
	rtl_write_word(rtlpriv, 0x350, 0x870c);
	rtl_write_byte(rtlpriv, 0x352, 0x1);
	if (ppsc->support_backdoor)
		rtl_write_byte(rtlpriv, 0x349, 0x1b);
	else
		rtl_write_byte(rtlpriv, 0x349, 0x03);
	rtl_write_word(rtlpriv, 0x350, 0x2718);
	rtl_write_byte(rtlpriv, 0x352, 0x1);
}

int rtl92de_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	u8 tmp_u1b;
	int i;
	int err;
	unsigned long flags;

	rtlpci->being_init_adapter = true;
	rtlpci->init_ready = false;
	spin_lock_irqsave(&globalmutex_for_power_and_efuse, flags);
	/* we should do iqk after disable/enable */
	rtl92d_phy_reset_iqk_result(hw);
	/* rtlpriv->intf_ops->disable_aspm(hw); */
	rtstatus = _rtl92de_init_mac(hw);
	if (!rtstatus) {
		pr_err("Init MAC failed\n");
		err = 1;
		spin_unlock_irqrestore(&globalmutex_for_power_and_efuse, flags);
		return err;
	}
	err = rtl92d_download_fw(hw);
	spin_unlock_irqrestore(&globalmutex_for_power_and_efuse, flags);
	if (err) {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"Failed to download FW. Init HW without FW..\n");
		return 1;
	}
	rtlhal->last_hmeboxnum = 0;
	rtlpriv->psc.fw_current_inpsmode = false;

	tmp_u1b = rtl_read_byte(rtlpriv, 0x605);
	tmp_u1b = tmp_u1b | 0x30;
	rtl_write_byte(rtlpriv, 0x605, tmp_u1b);

	if (rtlhal->earlymode_enable) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"EarlyMode Enabled!!!\n");

		tmp_u1b = rtl_read_byte(rtlpriv, 0x4d0);
		tmp_u1b = tmp_u1b | 0x1f;
		rtl_write_byte(rtlpriv, 0x4d0, tmp_u1b);

		rtl_write_byte(rtlpriv, 0x4d3, 0x80);

		tmp_u1b = rtl_read_byte(rtlpriv, 0x605);
		tmp_u1b = tmp_u1b | 0x40;
		rtl_write_byte(rtlpriv, 0x605, tmp_u1b);
	}

	if (mac->rdg_en) {
		rtl_write_byte(rtlpriv, REG_RD_CTRL, 0xff);
		rtl_write_word(rtlpriv, REG_RD_NAV_NXT, 0x200);
		rtl_write_byte(rtlpriv, REG_RD_RESP_PKT_TH, 0x05);
	}

	rtl92d_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252*/
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);

	rtl92d_phy_bb_config(hw);

	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	/* set before initialize RF */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);

	/* config RF */
	rtl92d_phy_rf_config(hw);

	/* After read predefined TXT, we must set BB/MAC/RF
	 * register as our requirement */
	/* After load BB,RF params,we need do more for 92D. */
	rtl92d_update_bbrf_configuration(hw);
	/* set default value after initialize RF,  */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0);
	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
			RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, (enum radio_path)1,
			RF_CHNLBW, RFREG_OFFSET_MASK);

	/*---- Set CCK and OFDM Block "ON"----*/
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);
	if (rtlhal->interfaceindex == 0) {
		/* RFPGA0_ANALOGPARAMETER2: cck clock select,
		 *  set to 20MHz by default */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10) |
			      BIT(11), 3);
	} else {
		/* Mac1 */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(11) |
			      BIT(10), 3);
	}

	_rtl92de_hw_configure(hw);

	/* reset hw sec */
	rtl_cam_reset_all_entry(hw);
	rtl92de_enable_hw_security_config(hw);

	/* Read EEPROM TX power index and PHY_REG_PG.txt to capture correct */
	/* TX power index for different rate set. */
	rtl92d_phy_get_hw_reg_originalvalue(hw);
	rtl92d_phy_set_txpower_level(hw, rtlphy->current_channel);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);

	_rtl92de_enable_aspm_back_door(hw);
	/* rtlpriv->intf_ops->enable_aspm(hw); */

	rtl92de_dm_init(hw);
	rtlpci->being_init_adapter = false;

	if (ppsc->rfpwr_state == ERFON) {
		rtl92d_phy_lc_calibrate(hw, IS_92D_SINGLEPHY(rtlhal->version));
		/* 5G and 2.4G must wait sometime to let RF LO ready */
		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			u32 tmp_rega;
			for (i = 0; i < 10000; i++) {
				udelay(MAX_STALL_TIME);

				tmp_rega = rtl_get_rfreg(hw,
						  (enum radio_path)RF90_PATH_A,
						  0x2a, MASKDWORD);

				if (((tmp_rega & BIT(11)) == BIT(11)))
					break;
			}
			/* check that loop was successful. If not, exit now */
			if (i == 10000) {
				rtlpci->init_ready = false;
				return 1;
			}
		}
	}
	rtlpci->init_ready = true;
	return err;
}

static int _rtl92de_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;

	bt_msr &= 0xfc;

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		rtl92de_stop_tx_beacon(hw);
		_rtl92de_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		type == NL80211_IFTYPE_AP) {
		rtl92de_resume_tx_beacon(hw);
		_rtl92de_disable_bcn_sub_func(hw);
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

void rtl92de_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_rcr;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));

	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
		_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl92de_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
	}
}

int rtl92de_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92de_set_media_status(hw, type))
		return -EOPNOTSUPP;

	/* check bssid */
	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl92de_set_check_bssid(hw, true);
	} else {
		rtl92de_set_check_bssid(hw, false);
	}
	return 0;
}

/* do iqk or reload iqk */
/* windows just rtl92d_phy_reload_iqk_setting in set channel,
 * but it's very strict for time sequence so we add
 * rtl92d_phy_reload_iqk_setting here */
void rtl92d_linked_set_reg(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 indexforchannel;
	u8 channel = rtlphy->current_channel;

	indexforchannel = rtl92d_get_rightchnlplace_for_iqk(channel);
	if (!rtlphy->iqk_matrix[indexforchannel].iqk_done) {
		rtl_dbg(rtlpriv, COMP_SCAN | COMP_INIT, DBG_DMESG,
			"Do IQK for channel:%d\n", channel);
		rtl92d_phy_iq_calibrate(hw);
	}
}

void rtl92de_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl92de_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR8190_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR8190_DISABLED);
	rtlpci->irq_enabled = false;
}

static void _rtl92de_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1b_tmp;
	unsigned long flags;

	rtlpriv->intf_ops->enable_aspm(hw);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFPARAMETER, BIT(3), 0);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFPARAMETER, BIT(15), 0);

	/* 0x20:value 05-->04 */
	rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x04);

	/*  ==== Reset digital sequence   ====== */
	rtl92d_firmware_selfreset(hw);

	/* f.   SYS_FUNC_EN 0x03[7:0]=0x51 reset MCU, MAC register, DCORE */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x51);

	/* g.   MCUFWDL 0x80[1:0]=0 reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/*  ==== Pull GPIO PIN to balance level and LED control ====== */

	/* h.     GPIO_PIN_CTRL 0x44[31:0]=0x000  */
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, 0x00000000);

	/* i.    Value = GPIO_PIN_CTRL[7:0] */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_GPIO_PIN_CTRL);

	/* j.    GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8); */
	/* write external PIN level  */
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL,
			0x00FF0000 | (u1b_tmp << 8));

	/* k.   GPIO_MUXCFG 0x42 [15:0] = 0x0780 */
	rtl_write_word(rtlpriv, REG_GPIO_IO_SEL, 0x0790);

	/* l.   LEDCFG 0x4C[15:0] = 0x8080 */
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8080);

	/*  ==== Disable analog sequence === */

	/* m.   AFE_PLL_CTRL[7:0] = 0x80  disable PLL */
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x80);

	/* n.   SPS0_CTRL 0x11[7:0] = 0x22  enter PFM mode */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x23);

	/* o.   AFE_XTAL_CTRL 0x24[7:0] = 0x0E  disable XTAL, if No BT COEX */
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, 0x0e);

	/* p.   RSV_CTRL 0x1C[7:0] = 0x0E lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);

	/*  ==== interface into suspend === */

	/* q.   APS_FSMCO[15:8] = 0x58 PCIe suspend mode */
	/* According to power document V11, we need to set this */
	/* value as 0x18. Otherwise, we may not L0s sometimes. */
	/* This indluences power consumption. Bases on SD1's test, */
	/* set as 0x00 do not affect power current. And if it */
	/* is set as 0x18, they had ever met auto load fail problem. */
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, 0x10);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"In PowerOff,reg0x%x=%X\n",
		REG_SPS0_CTRL, rtl_read_byte(rtlpriv, REG_SPS0_CTRL));
	/* r.   Note: for PCIe interface, PON will not turn */
	/* off m-bias and BandGap in PCIe suspend mode.  */

	/* 0x17[7] 1b': power off in process  0b' : power off over */
	if (rtlpriv->rtlhal.macphymode != SINGLEMAC_SINGLEPHY) {
		spin_lock_irqsave(&globalmutex_power, flags);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
		u1b_tmp &= (~BIT(7));
		rtl_write_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS, u1b_tmp);
		spin_unlock_irqrestore(&globalmutex_power, flags);
	}

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "<=======\n");
}

void rtl92de_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92de_set_media_status(hw, opmode);

	if (rtlpci->driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	/* Power sequence for each MAC. */
	/* a. stop tx DMA  */
	/* b. close RF */
	/* c. clear rx buf */
	/* d. stop rx DMA */
	/* e.  reset MAC */

	/* a. stop tx DMA */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xFE);
	udelay(50);

	/* b. TXPAUSE 0x522[7:0] = 0xFF Pause MAC TX queue */

	/* c. ========RF OFF sequence==========  */
	/* 0x88c[23:20] = 0xf. */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);

	/* APSD_CTRL 0x600[7:0] = 0x40 */
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);

	/* Close antenna 0,0xc04,0xd04 */
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0);
	rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0);

	/*  SYS_FUNC_EN 0x02[7:0] = 0xE2   reset BB state machine */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);

	/* Mac0 can not do Global reset. Mac1 can do. */
	/* SYS_FUNC_EN 0x02[7:0] = 0xE0  reset BB state machine  */
	if (rtlpriv->rtlhal.interfaceindex == 1)
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE0);
	udelay(50);

	/* d.  stop tx/rx dma before disable REG_CR (0x100) to fix */
	/* dma hang issue when disable/enable device.  */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xff);
	udelay(50);
	rtl_write_byte(rtlpriv, REG_CR, 0x0);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "==> Do power off.......\n");
	if (rtl92d_phy_check_poweroff(hw))
		_rtl92de_poweroff_adapter(hw);
	return;
}

void rtl92de_interrupt_recognized(struct ieee80211_hw *hw,
				  struct rtl_int *intvec)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	intvec->inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, intvec->inta);
}

void rtl92de_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;
	rtl92de_disable_interrupt(hw);
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

void rtl92de_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	rtl_dbg(rtlpriv, COMP_BEACON, DBG_DMESG,
		"beacon_interval:%d\n", bcn_interval);
	rtl92de_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl92de_enable_interrupt(hw);
}

void rtl92de_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_dbg(rtlpriv, COMP_INTR, DBG_LOUD, "add_msr:%x, rm_msr:%x\n",
		add_msr, rm_msr);
	if (add_msr)
		rtlpci->irq_mask[0] |= add_msr;
	if (rm_msr)
		rtlpci->irq_mask[0] &= (~rm_msr);
	rtl92de_disable_interrupt(hw);
	rtl92de_enable_interrupt(hw);
}

void rtl92de_suspend(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->rtlhal.macphyctl_reg = rtl_read_byte(rtlpriv,
		REG_MAC_PHY_CTRL_NORMAL);
}

void rtl92de_resume(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL,
		       rtlpriv->rtlhal.macphyctl_reg);
}
