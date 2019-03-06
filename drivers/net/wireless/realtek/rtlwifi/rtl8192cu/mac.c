// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../usb.h"
#include "../ps.h"
#include "../cam.h"
#include "../stats.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "mac.h"
#include "trx.h"
#include "../rtl8192c/fw_common.h"

#include <linux/module.h>

/* macro to shorten lines */

#define LINK_Q	ui_link_quality
#define RX_EVM	rx_evm_percentage
#define RX_SIGQ	rx_mimo_sig_qual

void rtl92c_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	enum version_8192c chip_version = VERSION_UNKNOWN;
	const char *versionid;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (value32 & TRP_VAUX_EN) {
		chip_version = (value32 & TYPE_ID) ? VERSION_TEST_CHIP_92C :
			       VERSION_TEST_CHIP_88C;
	} else {
		/* Normal mass production chip. */
		chip_version = NORMAL_CHIP;
		chip_version |= ((value32 & TYPE_ID) ? CHIP_92C : 0);
		chip_version |= ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0);
		if (IS_VENDOR_UMC(chip_version))
			chip_version |= ((value32 & CHIP_VER_RTL_MASK) ?
					 CHIP_VENDOR_UMC_B_CUT : 0);
		if (IS_92C_SERIAL(chip_version)) {
			value32 = rtl_read_dword(rtlpriv, REG_HPON_FSM);
			chip_version |= ((CHIP_BONDING_IDENTIFIER(value32) ==
				 CHIP_BONDING_92C_1T2R) ? CHIP_92C_1T2R : 0);
		}
	}
	rtlhal->version  = (enum version_8192c)chip_version;
	pr_info("Chip version 0x%x\n", chip_version);
	switch (rtlhal->version) {
	case VERSION_NORMAL_TSMC_CHIP_92C_1T2R:
		versionid = "NORMAL_B_CHIP_92C";
		break;
	case VERSION_NORMAL_TSMC_CHIP_92C:
		versionid = "NORMAL_TSMC_CHIP_92C";
		break;
	case VERSION_NORMAL_TSMC_CHIP_88C:
		versionid = "NORMAL_TSMC_CHIP_88C";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT:
		versionid = "NORMAL_UMC_CHIP_i92C_1T2R_A_CUT";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_A_CUT:
		versionid = "NORMAL_UMC_CHIP_92C_A_CUT";
		break;
	case VERSION_NORMAL_UMC_CHIP_88C_A_CUT:
		versionid = "NORMAL_UMC_CHIP_88C_A_CUT";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT:
		versionid = "NORMAL_UMC_CHIP_92C_1T2R_B_CUT";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_B_CUT:
		versionid = "NORMAL_UMC_CHIP_92C_B_CUT";
		break;
	case VERSION_NORMAL_UMC_CHIP_88C_B_CUT:
		versionid = "NORMAL_UMC_CHIP_88C_B_CUT";
		break;
	case VERSION_TEST_CHIP_92C:
		versionid = "TEST_CHIP_92C";
		break;
	case VERSION_TEST_CHIP_88C:
		versionid = "TEST_CHIP_88C";
		break;
	default:
		versionid = "UNKNOWN";
		break;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Chip Version ID: %s\n", versionid);

	if (IS_92C_SERIAL(rtlhal->version))
		rtlphy->rf_type =
			 (IS_92C_1T2R(rtlhal->version)) ? RF_1T2R : RF_2T2R;
	else
		rtlphy->rf_type = RF_1T1R;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Chip RF Type: %s\n",
		 rtlphy->rf_type == RF_2T2R ? "RF_2T2R" : "RF_1T1R");
	if (get_rf_type(rtlphy) == RF_1T1R)
		rtlpriv->dm.rfpath_rxenable[0] = true;
	else
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "VersionID = 0x%4x\n",
		 rtlhal->version);
}

/**
 * writeLLT - LLT table write access
 * @io: io callback
 * @address: LLT logical address.
 * @data: LLT data content
 *
 * Realtek hardware access function.
 *
 */
bool rtl92c_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool status = true;
	long count = 0;
	u32 value = _LLT_INIT_ADDR(address) |
	    _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtl_write_dword(rtlpriv, REG_LLT_INIT, value);
	do {
		value = rtl_read_dword(rtlpriv, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;
		if (count > POLLING_LLT_THRESHOLD) {
			pr_err("Failed to polling write LLT done at address %d! _LLT_OP_VALUE(%x)\n",
			       address, _LLT_OP_VALUE(value));
			status = false;
			break;
		}
	} while (++count);
	return status;
}

/**
 * rtl92c_init_LLT_table - Init LLT table
 * @io: io callback
 * @boundary:
 *
 * Realtek hardware access function.
 *
 */
bool rtl92c_init_llt_table(struct ieee80211_hw *hw, u32 boundary)
{
	bool rst = true;
	u32	i;

	for (i = 0; i < (boundary - 1); i++) {
		rst = rtl92c_llt_write(hw, i , i + 1);
		if (true != rst) {
			pr_err("===> %s #1 fail\n", __func__);
			return rst;
		}
	}
	/* end of list */
	rst = rtl92c_llt_write(hw, (boundary - 1), 0xFF);
	if (true != rst) {
		pr_err("===> %s #2 fail\n", __func__);
		return rst;
	}
	/* Make the other pages as ring buffer
	 * This ring buffer is used as beacon buffer if we config this MAC
	 *  as two MAC transfer.
	 * Otherwise used as local loopback buffer.
	 */
	for (i = boundary; i < LLT_LAST_ENTRY_OF_TX_PKT_BUFFER; i++) {
		rst = rtl92c_llt_write(hw, i, (i + 1));
		if (true != rst) {
			pr_err("===> %s #3 fail\n", __func__);
			return rst;
		}
	}
	/* Let last entry point to the start entry of ring buffer */
	rst = rtl92c_llt_write(hw, LLT_LAST_ENTRY_OF_TX_PKT_BUFFER, boundary);
	if (true != rst) {
		pr_err("===> %s #4 fail\n", __func__);
		return rst;
	}
	return rst;
}

void rtl92c_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 *macaddr = p_macaddr;
	u32 entry_id = 0;
	bool is_pairwise = false;
	static u8 cam_const_addr[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
	};
	static u8 cam_const_broad[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	if (clear_all) {
		u8 idx = 0;
		u8 cam_offset = 0;
		u8 clear_number = 5;

		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "clear_all\n");
		for (idx = 0; idx < clear_number; idx++) {
			rtl_cam_mark_invalid(hw, cam_offset + idx);
			rtl_cam_empty_entry(hw, cam_offset + idx);
			if (idx < 5) {
				memset(rtlpriv->sec.key_buf[idx], 0,
				       MAX_KEY_LEN);
				rtlpriv->sec.key_len[idx] = 0;
			}
		}
	} else {
		switch (enc_algo) {
		case WEP40_ENCRYPTION:
			enc_algo = CAM_WEP40;
			break;
		case WEP104_ENCRYPTION:
			enc_algo = CAM_WEP104;
			break;
		case TKIP_ENCRYPTION:
			enc_algo = CAM_TKIP;
			break;
		case AESCCMP_ENCRYPTION:
			enc_algo = CAM_AES;
			break;
		default:
			pr_err("illegal switch case\n");
			enc_algo = CAM_TKIP;
			break;
		}
		if (is_wepkey || rtlpriv->sec.use_defaultkey) {
			macaddr = cam_const_addr[key_index];
			entry_id = key_index;
		} else {
			if (is_group) {
				macaddr = cam_const_broad;
				entry_id = key_index;
			} else {
				if (mac->opmode == NL80211_IFTYPE_AP ||
				    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
					entry_id = rtl_cam_get_free_entry(hw,
								 p_macaddr);
					if (entry_id >=  TOTAL_CAM_ENTRY) {
						pr_err("Can not find free hw security cam entry\n");
						return;
					}
				} else {
					entry_id = CAM_PAIRWISE_KEY_POSITION;
				}

				key_index = PAIRWISE_KEYIDX;
				is_pairwise = true;
			}
		}
		if (rtlpriv->sec.key_len[key_index] == 0) {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "delete one entry\n");
			if (mac->opmode == NL80211_IFTYPE_AP ||
			    mac->opmode == NL80211_IFTYPE_MESH_POINT)
				rtl_cam_del_entry(hw, p_macaddr);
			rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
		} else {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 "The insert KEY length is %d\n",
				 rtlpriv->sec.key_len[PAIRWISE_KEYIDX]);
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 "The insert KEY is %x %x\n",
				 rtlpriv->sec.key_buf[0][0],
				 rtlpriv->sec.key_buf[0][1]);
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "add one entry\n");
			if (is_pairwise) {
				RT_PRINT_DATA(rtlpriv, COMP_SEC, DBG_LOUD,
					      "Pairwise Key content",
					      rtlpriv->sec.pairwise_key,
					      rtlpriv->sec.
					      key_len[PAIRWISE_KEYIDX]);
				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
					 "set Pairwise key\n");

				rtl_cam_add_one_entry(hw, macaddr, key_index,
						entry_id, enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.
						key_buf[key_index]);
			} else {
				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
					 "set group key\n");
				if (mac->opmode == NL80211_IFTYPE_ADHOC) {
					rtl_cam_add_one_entry(hw,
						rtlefuse->dev_addr,
						PAIRWISE_KEYIDX,
						CAM_PAIRWISE_KEY_POSITION,
						enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.key_buf
						[entry_id]);
				}
				rtl_cam_add_one_entry(hw, macaddr, key_index,
						entry_id, enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.key_buf[entry_id]);
			}
		}
	}
}

u32 rtl92c_get_txdma_status(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	return rtl_read_dword(rtlpriv, REG_TXDMA_STATUS);
}

void rtl92c_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	if (IS_HARDWARE_TYPE_8192CE(rtlpriv)) {
		rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] &
				0xFFFFFFFF);
		rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] &
				0xFFFFFFFF);
	} else {
		rtl_write_dword(rtlpriv, REG_HIMR, rtlusb->irq_mask[0] &
				0xFFFFFFFF);
		rtl_write_dword(rtlpriv, REG_HIMRE, rtlusb->irq_mask[1] &
				0xFFFFFFFF);
	}
}

void rtl92c_init_interrupt(struct ieee80211_hw *hw)
{
	 rtl92c_enable_interrupt(hw);
}

void rtl92c_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_dword(rtlpriv, REG_HIMR, IMR8190_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR8190_DISABLED);
}

void rtl92c_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl92c_dm_init_edca_turbo(hw);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM, (u8 *)&aci);
}

void rtl92c_init_driver_info_size(struct ieee80211_hw *hw, u8 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, size);
}

int rtl92c_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	u8 value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		value = NT_NO_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		value = NT_LINK_AD_HOC;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		value = NT_LINK_AP;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		value = NT_AS_AP;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Network type %d not supported!\n", type);
		return -EOPNOTSUPP;
	}
	rtl_write_byte(rtlpriv, MSR, value);
	return 0;
}

void rtl92c_init_network_type(struct ieee80211_hw *hw)
{
	rtl92c_set_network_type(hw, NL80211_IFTYPE_UNSPECIFIED);
}

void rtl92c_init_adaptive_ctrl(struct ieee80211_hw *hw)
{
	u16	value16;
	u32	value32;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* Response Rate Set */
	value32 = rtl_read_dword(rtlpriv, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtl_write_dword(rtlpriv, REG_RRSR, value32);
	/* SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtl_write_word(rtlpriv,  REG_SPEC_SIFS, value16);
	/* Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtl_write_dword(rtlpriv,  REG_RL, value16);
}

void rtl92c_init_rate_fallback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* Set Data Auto Rate Fallback Retry Count register. */
	rtl_write_dword(rtlpriv,  REG_DARFRC, 0x00000000);
	rtl_write_dword(rtlpriv,  REG_DARFRC+4, 0x10080404);
	rtl_write_dword(rtlpriv,  REG_RARFRC, 0x04030201);
	rtl_write_dword(rtlpriv,  REG_RARFRC+4, 0x08070605);
}

static void rtl92c_set_cck_sifs(struct ieee80211_hw *hw, u8 trx_sifs,
				u8 ctx_sifs)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SIFS_CCK, trx_sifs);
	rtl_write_byte(rtlpriv, (REG_SIFS_CCK + 1), ctx_sifs);
}

static void rtl92c_set_ofdm_sifs(struct ieee80211_hw *hw, u8 trx_sifs,
				 u8 ctx_sifs)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SIFS_OFDM, trx_sifs);
	rtl_write_byte(rtlpriv, (REG_SIFS_OFDM + 1), ctx_sifs);
}

void rtl92c_init_edca_param(struct ieee80211_hw *hw,
			    u16 queue, u16 txop, u8 cw_min, u8 cw_max, u8 aifs)
{
	/* sequence: VO, VI, BE, BK ==> the same as 92C hardware design.
	 * referenc : enum nl80211_txq_q or ieee80211_set_wmm_default function.
	 */
	u32 value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	value = (u32)aifs;
	value |= ((u32)cw_min & 0xF) << 8;
	value |= ((u32)cw_max & 0xF) << 12;
	value |= (u32)txop << 16;
	/* 92C hardware register sequence is the same as queue number. */
	rtl_write_dword(rtlpriv, (REG_EDCA_VO_PARAM + (queue * 4)), value);
}

void rtl92c_init_edca(struct ieee80211_hw *hw)
{
	u16 value16;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* disable EDCCA count down, to reduce collison and retry */
	value16 = rtl_read_word(rtlpriv, REG_RD_CTRL);
	value16 |= DIS_EDCA_CNT_DWN;
	rtl_write_word(rtlpriv, REG_RD_CTRL, value16);
	/* Update SIFS timing.  ??????????
	 * pHalData->SifsTime = 0x0e0e0a0a; */
	rtl92c_set_cck_sifs(hw, 0xa, 0xa);
	rtl92c_set_ofdm_sifs(hw, 0xe, 0xe);
	/* Set CCK/OFDM SIFS to be 10us. */
	rtl_write_word(rtlpriv, REG_SIFS_CCK, 0x0a0a);
	rtl_write_word(rtlpriv, REG_SIFS_OFDM, 0x1010);
	rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x0204);
	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x014004);
	/* TXOP */
	rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM, 0x005EA324);
	rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM, 0x002FA226);
	/* PIFS */
	rtl_write_byte(rtlpriv, REG_PIFS, 0x1C);
	/* AGGR BREAK TIME Register */
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);
	rtl_write_byte(rtlpriv, REG_BCNDMATIM, 0x02);
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x02);
}

void rtl92c_init_ampdu_aggregation(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x99997631);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
	/* init AMPDU aggregation number, tuning for Tx's TP, */
	rtl_write_word(rtlpriv, 0x4CA, 0x0708);
}

void rtl92c_init_beacon_max_error(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xFF);
}

void rtl92c_init_rdg_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_RD_CTRL, 0xFF);
	rtl_write_word(rtlpriv, REG_RD_NAV_NXT, 0x200);
	rtl_write_byte(rtlpriv, REG_RD_RESP_PKT_TH, 0x05);
}

void rtl92c_init_retry_function(struct ieee80211_hw *hw)
{
	u8	value8;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	value8 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL, value8);
	/* Set ACK timeout */
	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);
}

void rtl92c_disable_fast_edca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_word(rtlpriv, REG_FAST_EDCA_CTRL, 0);
}

void rtl92c_set_min_space(struct ieee80211_hw *hw, bool is2T)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value = is2T ? MAX_MSS_DENSITY_2T : MAX_MSS_DENSITY_1T;

	rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE, value);
}

/*==============================================================*/

static u8 _rtl92c_query_rxpwrpercentage(s8 antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return 0;
	else if (antpower >= 0)
		return 100;
	else
		return 100 + antpower;
}

static u8 _rtl92c_evm_db_to_percentage(s8 value)
{
	s8 ret_val;

	ret_val = value;
	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;
	ret_val = 0 - ret_val;
	ret_val *= 3;
	if (ret_val == 99)
		ret_val = 100;
	return ret_val;
}

static long _rtl92c_signal_scale_mapping(struct ieee80211_hw *hw,
		long currsig)
{
	long retsig;

	if (currsig >= 61 && currsig <= 100)
		retsig = 90 + ((currsig - 60) / 4);
	else if (currsig >= 41 && currsig <= 60)
		retsig = 78 + ((currsig - 40) / 2);
	else if (currsig >= 31 && currsig <= 40)
		retsig = 66 + (currsig - 30);
	else if (currsig >= 21 && currsig <= 30)
		retsig = 54 + (currsig - 20);
	else if (currsig >= 5 && currsig <= 20)
		retsig = 42 + (((currsig - 5) * 2) / 3);
	else if (currsig == 4)
		retsig = 36;
	else if (currsig == 3)
		retsig = 27;
	else if (currsig == 2)
		retsig = 18;
	else if (currsig == 1)
		retsig = 9;
	else
		retsig = currsig;
	return retsig;
}

static void _rtl92c_query_rxphystatus(struct ieee80211_hw *hw,
				      struct rtl_stats *pstats,
				      struct rx_desc_92c *p_desc,
				      struct rx_fwinfo_92c *p_drvinfo,
				      bool packet_match_bssid,
				      bool packet_toself,
				      bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct phy_sts_cck_8192s_t *cck_buf;
	s8 rx_pwr_all = 0, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool in_powersavemode = false;
	bool is_cck_rate;
	u8 *pdesc = (u8 *)p_desc;

	is_cck_rate = RX_HAL_IS_CCK_RATE(p_desc->rxmcs);
	pstats->packet_matchbssid = packet_match_bssid;
	pstats->packet_toself = packet_toself;
	pstats->packet_beacon = packet_beacon;
	pstats->is_cck = is_cck_rate;
	pstats->RX_SIGQ[0] = -1;
	pstats->RX_SIGQ[1] = -1;
	if (is_cck_rate) {
		u8 report, cck_highpwr;

		cck_buf = (struct phy_sts_cck_8192s_t *)p_drvinfo;
		if (!in_powersavemode)
			cck_highpwr = rtlphy->cck_high_power;
		else
			cck_highpwr = false;
		if (!cck_highpwr) {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;

			report = cck_buf->cck_agc_rpt & 0xc0;
			report = report >> 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - (cck_agc_rpt & 0x3e);
				break;
			case 0x2:
				rx_pwr_all = -26 - (cck_agc_rpt & 0x3e);
				break;
			case 0x1:
				rx_pwr_all = -12 - (cck_agc_rpt & 0x3e);
				break;
			case 0x0:
				rx_pwr_all = 16 - (cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;

			report = p_drvinfo->cfosho[0] & 0x60;
			report = report >> 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -26 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -12 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 16 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			}
		}
		pwdb_all = _rtl92c_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->recvsignalpower = rx_pwr_all;
		if (packet_match_bssid) {
			u8 sq;

			if (pstats->rx_pwdb_all > 40)
				sq = 100;
			else {
				sq = cck_buf->sq_rpt;
				if (sq > 64)
					sq = 0;
				else if (sq < 20)
					sq = 100;
				else
					sq = ((64 - sq) * 100) / 44;
			}
			pstats->signalquality = sq;
			pstats->RX_SIGQ[0] = sq;
			pstats->RX_SIGQ[1] = -1;
		}
	} else {
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF90_PATH_MAX; i++) {
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;
			rx_pwr[i] =
			    ((p_drvinfo->gain_trsw[i] & 0x3f) * 2) - 110;
			rssi = _rtl92c_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
			    (long)(p_drvinfo->rxsnr[i] / 2);

			if (packet_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8) rssi;
		}
		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;
		pwdb_all = _rtl92c_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;
		if (GET_RX_DESC_RX_MCS(pdesc) &&
		    GET_RX_DESC_RX_MCS(pdesc) >= DESC_RATEMCS8 &&
		    GET_RX_DESC_RX_MCS(pdesc) <= DESC_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;
		for (i = 0; i < max_spatial_stream; i++) {
			evm = _rtl92c_evm_db_to_percentage(p_drvinfo->rxevm[i]);
			if (packet_match_bssid) {
				if (i == 0)
					pstats->signalquality =
					    (u8) (evm & 0xff);
				pstats->RX_SIGQ[i] =
				    (u8) (evm & 0xff);
			}
		}
	}
	if (is_cck_rate)
		pstats->signalstrength =
		    (u8) (_rtl92c_signal_scale_mapping(hw, pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength =
		    (u8) (_rtl92c_signal_scale_mapping
			  (hw, total_rssi /= rf_rx_num));
}

void rtl92c_translate_rx_signal_stuff(struct ieee80211_hw *hw,
					       struct sk_buff *skb,
					       struct rtl_stats *pstats,
					       struct rx_desc_92c *pdesc,
					       struct rx_fwinfo_92c *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	__le16 fc;
	u16 type, cpu_fc;
	bool packet_matchbssid, packet_toself, packet_beacon = false;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;
	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	cpu_fc = le16_to_cpu(fc);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	packet_matchbssid =
	    ((IEEE80211_FTYPE_CTL != type) &&
	     ether_addr_equal(mac->bssid,
			      (cpu_fc & IEEE80211_FCTL_TODS) ? hdr->addr1 :
			      (cpu_fc & IEEE80211_FCTL_FROMDS) ? hdr->addr2 :
			      hdr->addr3) &&
	     (!pstats->hwerror) && (!pstats->crc) && (!pstats->icv));

	packet_toself = packet_matchbssid &&
	    ether_addr_equal(praddr, rtlefuse->dev_addr);
	if (ieee80211_is_beacon(fc))
		packet_beacon = true;
	_rtl92c_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
				   packet_matchbssid, packet_toself,
				   packet_beacon);
	rtl_process_phyinfo(hw, tmp_buf, pstats);
}
