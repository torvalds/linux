// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"
#include <linux/module.h>

static int _rtl_phydm_init_com_info(struct rtl_priv *rtlpriv,
				    enum odm_ic_type ic_type,
				    struct rtl_phydm_params *params)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	u8 odm_board_type = ODM_BOARD_DEFAULT;
	u32 support_ability;
	int i;

	dm->adapter = (void *)rtlpriv;

	odm_cmn_info_init(dm, ODM_CMNINFO_PLATFORM, ODM_CE);

	odm_cmn_info_init(dm, ODM_CMNINFO_IC_TYPE, ic_type);

	odm_cmn_info_init(dm, ODM_CMNINFO_INTERFACE, ODM_ITRF_PCIE);

	odm_cmn_info_init(dm, ODM_CMNINFO_MP_TEST_CHIP, params->mp_chip);

	odm_cmn_info_init(dm, ODM_CMNINFO_PATCH_ID, rtlhal->oem_id);

	odm_cmn_info_init(dm, ODM_CMNINFO_BWIFI_TEST, 1);

	if (rtlphy->rf_type == RF_1T1R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_1T1R);
	else if (rtlphy->rf_type == RF_1T2R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_1T2R);
	else if (rtlphy->rf_type == RF_2T2R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_2T2R);
	else if (rtlphy->rf_type == RF_2T2R_GREEN)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_2T2R_GREEN);
	else if (rtlphy->rf_type == RF_2T3R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_2T3R);
	else if (rtlphy->rf_type == RF_2T4R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_2T4R);
	else if (rtlphy->rf_type == RF_3T3R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_3T3R);
	else if (rtlphy->rf_type == RF_3T4R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_3T4R);
	else if (rtlphy->rf_type == RF_4T4R)
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_4T4R);
	else
		odm_cmn_info_init(dm, ODM_CMNINFO_RF_TYPE, ODM_XTXR);

	/* 1 ======= BoardType: ODM_CMNINFO_BOARD_TYPE ======= */
	if (rtlhal->external_lna_2g != 0) {
		odm_board_type |= ODM_BOARD_EXT_LNA;
		odm_cmn_info_init(dm, ODM_CMNINFO_EXT_LNA, 1);
	}
	if (rtlhal->external_lna_5g != 0) {
		odm_board_type |= ODM_BOARD_EXT_LNA_5G;
		odm_cmn_info_init(dm, ODM_CMNINFO_5G_EXT_LNA, 1);
	}
	if (rtlhal->external_pa_2g != 0) {
		odm_board_type |= ODM_BOARD_EXT_PA;
		odm_cmn_info_init(dm, ODM_CMNINFO_EXT_PA, 1);
	}
	if (rtlhal->external_pa_5g != 0) {
		odm_board_type |= ODM_BOARD_EXT_PA_5G;
		odm_cmn_info_init(dm, ODM_CMNINFO_5G_EXT_PA, 1);
	}
	if (rtlpriv->cfg->ops->get_btc_status())
		odm_board_type |= ODM_BOARD_BT;

	odm_cmn_info_init(dm, ODM_CMNINFO_BOARD_TYPE, odm_board_type);
	/* 1 ============== End of BoardType ============== */

	odm_cmn_info_init(dm, ODM_CMNINFO_GPA, rtlhal->type_gpa);
	odm_cmn_info_init(dm, ODM_CMNINFO_APA, rtlhal->type_apa);
	odm_cmn_info_init(dm, ODM_CMNINFO_GLNA, rtlhal->type_glna);
	odm_cmn_info_init(dm, ODM_CMNINFO_ALNA, rtlhal->type_alna);

	odm_cmn_info_init(dm, ODM_CMNINFO_RFE_TYPE, rtlhal->rfe_type);

	odm_cmn_info_init(dm, ODM_CMNINFO_EXT_TRSW, 0);

	/*Add by YuChen for kfree init*/
	odm_cmn_info_init(dm, ODM_CMNINFO_REGRFKFREEENABLE, 2);
	odm_cmn_info_init(dm, ODM_CMNINFO_RFKFREEENABLE, 0);

	/*Antenna diversity relative parameters*/
	odm_cmn_info_hook(dm, ODM_CMNINFO_ANT_DIV,
			  &rtlefuse->antenna_div_cfg);
	odm_cmn_info_init(dm, ODM_CMNINFO_RF_ANTENNA_TYPE,
			  rtlefuse->antenna_div_type);
	odm_cmn_info_init(dm, ODM_CMNINFO_BE_FIX_TX_ANT, 0);
	odm_cmn_info_init(dm, ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH, 0);

	/* (8822B) efuse 0x3D7 & 0x3D8 for TX PA bias */
	odm_cmn_info_init(dm, ODM_CMNINFO_EFUSE0X3D7, params->efuse0x3d7);
	odm_cmn_info_init(dm, ODM_CMNINFO_EFUSE0X3D8, params->efuse0x3d8);

	/*Add by YuChen for adaptivity init*/
	odm_cmn_info_hook(dm, ODM_CMNINFO_ADAPTIVITY,
			  &rtlpriv->phydm.adaptivity_en);
	phydm_adaptivity_info_init(dm, PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE,
				   false);
	phydm_adaptivity_info_init(dm, PHYDM_ADAPINFO_DCBACKOFF, 0);
	phydm_adaptivity_info_init(dm, PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY,
				   false);
	phydm_adaptivity_info_init(dm, PHYDM_ADAPINFO_TH_L2H_INI, 0);
	phydm_adaptivity_info_init(dm, PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF, 0);

	odm_cmn_info_init(dm, ODM_CMNINFO_IQKFWOFFLOAD, 0);

	/* Pointer reference */
	odm_cmn_info_hook(dm, ODM_CMNINFO_TX_UNI,
			  &rtlpriv->stats.txbytesunicast);
	odm_cmn_info_hook(dm, ODM_CMNINFO_RX_UNI,
			  &rtlpriv->stats.rxbytesunicast);
	odm_cmn_info_hook(dm, ODM_CMNINFO_BAND, &rtlhal->current_bandtype);
	odm_cmn_info_hook(dm, ODM_CMNINFO_FORCED_RATE,
			  &rtlpriv->phydm.forced_data_rate);
	odm_cmn_info_hook(dm, ODM_CMNINFO_FORCED_IGI_LB,
			  &rtlpriv->phydm.forced_igi_lb);

	odm_cmn_info_hook(dm, ODM_CMNINFO_SEC_CHNL_OFFSET,
			  &mac->cur_40_prime_sc);
	odm_cmn_info_hook(dm, ODM_CMNINFO_BW, &rtlphy->current_chan_bw);
	odm_cmn_info_hook(dm, ODM_CMNINFO_CHNL, &rtlphy->current_channel);

	odm_cmn_info_hook(dm, ODM_CMNINFO_SCAN, &mac->act_scanning);
	odm_cmn_info_hook(dm, ODM_CMNINFO_POWER_SAVING,
			  &ppsc->dot11_psmode); /* may add new boolean flag */
	/*Add by Yuchen for phydm beamforming*/
	odm_cmn_info_hook(dm, ODM_CMNINFO_TX_TP,
			  &rtlpriv->stats.txbytesunicast_inperiod_tp);
	odm_cmn_info_hook(dm, ODM_CMNINFO_RX_TP,
			  &rtlpriv->stats.rxbytesunicast_inperiod_tp);
	odm_cmn_info_hook(dm, ODM_CMNINFO_ANT_TEST,
			  &rtlpriv->phydm.antenna_test);
	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		odm_cmn_info_ptr_array_hook(dm, ODM_CMNINFO_STA_STATUS, i,
					    NULL);

	phydm_init_debug_setting(dm);

	odm_cmn_info_init(dm, ODM_CMNINFO_FAB_VER, params->fab_ver);
	odm_cmn_info_init(dm, ODM_CMNINFO_CUT_VER, params->cut_ver);

	/* after ifup, ability is updated again */
	support_ability = ODM_RF_CALIBRATION | ODM_RF_TX_PWR_TRACK;
	odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY, support_ability);

	return 0;
}

static int rtl_phydm_init_priv(struct rtl_priv *rtlpriv,
			       struct rtl_phydm_params *params)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_ic_type ic;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		ic = ODM_RTL8822B;
	else
		return 0;

	rtlpriv->phydm.internal =
		kzalloc(sizeof(struct phy_dm_struct), GFP_KERNEL);

	_rtl_phydm_init_com_info(rtlpriv, ic, params);

	odm_init_all_timers(dm);

	return 1;
}

static int rtl_phydm_deinit_priv(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	odm_cancel_all_timers(dm);

	kfree(rtlpriv->phydm.internal);
	rtlpriv->phydm.internal = NULL;

	return 0;
}

static bool rtl_phydm_load_txpower_by_rate(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum hal_status status;

	status = odm_config_bb_with_header_file(dm, CONFIG_BB_PHY_REG_PG);
	if (status != HAL_STATUS_SUCCESS)
		return false;

	return true;
}

static bool rtl_phydm_load_txpower_limit(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum hal_status status;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv)) {
		odm_read_and_config_mp_8822b_txpwr_lmt(dm);
	} else {
		status = odm_config_rf_with_header_file(dm, CONFIG_RF_TXPWR_LMT,
							0);
		if (status != HAL_STATUS_SUCCESS)
			return false;
	}

	return true;
}

static int rtl_phydm_init_dm(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	u32 support_ability = 0;

	/* clang-format off */
	support_ability = 0
			| ODM_BB_DIG
			| ODM_BB_RA_MASK
			| ODM_BB_DYNAMIC_TXPWR
			| ODM_BB_FA_CNT
			| ODM_BB_RSSI_MONITOR
			| ODM_BB_CCK_PD
	/*		| ODM_BB_PWR_SAVE*/
			| ODM_BB_CFO_TRACKING
			| ODM_MAC_EDCA_TURBO
			| ODM_RF_TX_PWR_TRACK
			| ODM_RF_CALIBRATION
			| ODM_BB_NHM_CNT
	/*		| ODM_BB_PWR_TRAIN*/
			;
	/* clang-format on */

	odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY, support_ability);

	odm_dm_init(dm);

	return 0;
}

static int rtl_phydm_deinit_dm(struct rtl_priv *rtlpriv)
{
	return 0;
}

static int rtl_phydm_reset_dm(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	odm_dm_reset(dm);

	return 0;
}

static bool rtl_phydm_parameter_init(struct rtl_priv *rtlpriv, bool post)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_parameter_init(dm, post ? ODM_POST_SETTING :
							      ODM_PRE_SETTING);

	return false;
}

static bool rtl_phydm_phy_bb_config(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum hal_status status;

	status = odm_config_bb_with_header_file(dm, CONFIG_BB_PHY_REG);
	if (status != HAL_STATUS_SUCCESS)
		return false;

	status = odm_config_bb_with_header_file(dm, CONFIG_BB_AGC_TAB);
	if (status != HAL_STATUS_SUCCESS)
		return false;

	return true;
}

static bool rtl_phydm_phy_rf_config(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	enum hal_status status;
	enum odm_rf_radio_path rfpath;

	for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
		status = odm_config_rf_with_header_file(dm, CONFIG_RF_RADIO,
							rfpath);
		if (status != HAL_STATUS_SUCCESS)
			return false;
	}

	status = odm_config_rf_with_tx_pwr_track_header_file(dm);
	if (status != HAL_STATUS_SUCCESS)
		return false;

	return true;
}

static bool rtl_phydm_phy_mac_config(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum hal_status status;

	status = odm_config_mac_with_header_file(dm);
	if (status != HAL_STATUS_SUCCESS)
		return false;

	return true;
}

static bool rtl_phydm_trx_mode(struct rtl_priv *rtlpriv,
			       enum radio_mask tx_path, enum radio_mask rx_path,
			       bool is_tx2_path)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_trx_mode_8822b(dm,
						   (enum odm_rf_path)tx_path,
						   (enum odm_rf_path)rx_path,
						   is_tx2_path);

	return false;
}

static bool rtl_phydm_watchdog(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	bool fw_current_inpsmode = false;
	bool fw_ps_awake = true;
	u8 is_linked = false;
	u8 bsta_state = false;
	u8 is_bt_enabled = false;

	/* check whether do watchdog */
	rtlpriv->cfg->ops->get_hw_reg(rtlpriv->hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_inpsmode));
	rtlpriv->cfg->ops->get_hw_reg(rtlpriv->hw, HW_VAR_FWLPS_RF_ON,
				      (u8 *)(&fw_ps_awake));
	if (ppsc->p2p_ps_info.p2p_ps_mode)
		fw_ps_awake = false;

	if ((ppsc->rfpwr_state == ERFON) &&
	    ((!fw_current_inpsmode) && fw_ps_awake) &&
	    (!ppsc->rfchange_inprogress))
		;
	else
		return false;

	/* update common info before doing watchdog */
	if (mac->link_state >= MAC80211_LINKED) {
		is_linked = true;
		if (mac->vif && mac->vif->type == NL80211_IFTYPE_STATION)
			bsta_state = true;
	}

	if (rtlpriv->cfg->ops->get_btc_status())
		is_bt_enabled = !rtlpriv->btcoexist.btc_ops->btc_is_bt_disabled(
			rtlpriv);

	odm_cmn_info_update(dm, ODM_CMNINFO_LINK, is_linked);
	odm_cmn_info_update(dm, ODM_CMNINFO_STATION_STATE, bsta_state);
	odm_cmn_info_update(dm, ODM_CMNINFO_BT_ENABLED, is_bt_enabled);

	/* do watchdog */
	odm_dm_watchdog(dm);

	return true;
}

static bool rtl_phydm_switch_band(struct rtl_priv *rtlpriv, u8 central_ch)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_switch_band_8822b(dm, central_ch);

	return false;
}

static bool rtl_phydm_switch_channel(struct rtl_priv *rtlpriv, u8 central_ch)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_switch_channel_8822b(dm, central_ch);

	return false;
}

static bool rtl_phydm_switch_bandwidth(struct rtl_priv *rtlpriv,
				       u8 primary_ch_idx,
				       enum ht_channel_width bandwidth)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_bw odm_bw = (enum odm_bw)bandwidth;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_switch_bandwidth_8822b(dm, primary_ch_idx,
							   odm_bw);

	return false;
}

static bool rtl_phydm_iq_calibrate(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		phy_iq_calibrate_8822b(dm, false);
	else
		return false;

	return true;
}

static bool rtl_phydm_clear_txpowertracking_state(struct rtl_priv *rtlpriv)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	odm_clear_txpowertracking_state(dm);

	return true;
}

static bool rtl_phydm_pause_dig(struct rtl_priv *rtlpriv, bool pause)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (pause)
		odm_pause_dig(dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_0, 0x1e);
	else /* resume */
		odm_pause_dig(dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_0, 0xff);

	return true;
}

static u32 rtl_phydm_read_rf_reg(struct rtl_priv *rtlpriv,
				 enum radio_path rfpath, u32 addr, u32 mask)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_rf_radio_path odm_rfpath = (enum odm_rf_radio_path)rfpath;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_read_rf_reg_8822b(dm, odm_rfpath, addr,
						      mask);

	return -1;
}

static bool rtl_phydm_write_rf_reg(struct rtl_priv *rtlpriv,
				   enum radio_path rfpath, u32 addr, u32 mask,
				   u32 data)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_rf_radio_path odm_rfpath = (enum odm_rf_radio_path)rfpath;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_write_rf_reg_8822b(dm, odm_rfpath, addr,
						       mask, data);

	return false;
}

static u8 rtl_phydm_read_txagc(struct rtl_priv *rtlpriv, enum radio_path rfpath,
			       u8 hw_rate)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_rf_radio_path odm_rfpath = (enum odm_rf_radio_path)rfpath;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_read_txagc_8822b(dm, odm_rfpath, hw_rate);

	return -1;
}

static bool rtl_phydm_write_txagc(struct rtl_priv *rtlpriv, u32 power_index,
				  enum radio_path rfpath, u8 hw_rate)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	enum odm_rf_radio_path odm_rfpath = (enum odm_rf_radio_path)rfpath;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		return config_phydm_write_txagc_8822b(dm, power_index,
						      odm_rfpath, hw_rate);

	return false;
}

static bool rtl_phydm_c2h_content_parsing(struct rtl_priv *rtlpriv, u8 cmd_id,
					  u8 cmd_len, u8 *content)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	if (phydm_c2H_content_parsing(dm, cmd_id, cmd_len, content))
		return true;

	return false;
}

static bool rtl_phydm_query_phy_status(struct rtl_priv *rtlpriv, u8 *phystrpt,
				       struct ieee80211_hdr *hdr,
				       struct rtl_stats *pstatus)
{
	/* NOTE: phystrpt may be NULL, and need to fill default value */

	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct dm_per_pkt_info pktinfo; /* input of pydm */
	struct dm_phy_status_info phy_info; /* output of phydm */
	__le16 fc = hdr->frame_control;

	/* fill driver pstatus */
	ether_addr_copy(pstatus->psaddr, ieee80211_get_SA(hdr));

	/* fill pktinfo */
	memset(&pktinfo, 0, sizeof(pktinfo));

	pktinfo.data_rate = pstatus->rate;

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_STATION) {
		pktinfo.station_id = 0;
	} else {
		/* TODO: use rtl_find_sta() to find ID */
		pktinfo.station_id = 0xFF;
	}

	pktinfo.is_packet_match_bssid =
		(!ieee80211_is_ctl(fc) &&
		 (ether_addr_equal(mac->bssid,
				   ieee80211_has_tods(fc) ?
					   hdr->addr1 :
					   ieee80211_has_fromds(fc) ?
					   hdr->addr2 :
					   hdr->addr3)) &&
		 (!pstatus->hwerror) && (!pstatus->crc) && (!pstatus->icv));
	pktinfo.is_packet_to_self =
		pktinfo.is_packet_match_bssid &&
		(ether_addr_equal(hdr->addr1, rtlefuse->dev_addr));
	pktinfo.is_to_self = (!pstatus->icv) && (!pstatus->crc) &&
			     (ether_addr_equal(hdr->addr1, rtlefuse->dev_addr));
	pktinfo.is_packet_beacon = (ieee80211_is_beacon(fc) ? true : false);

	/* query phy status */
	if (phystrpt)
		odm_phy_status_query(dm, &phy_info, phystrpt, &pktinfo);
	else
		memset(&phy_info, 0, sizeof(phy_info));

	/* copy phy_info from phydm to driver */
	pstatus->rx_pwdb_all = phy_info.rx_pwdb_all;
	pstatus->bt_rx_rssi_percentage = phy_info.bt_rx_rssi_percentage;
	pstatus->recvsignalpower = phy_info.recv_signal_power;
	pstatus->signalquality = phy_info.signal_quality;
	pstatus->rx_mimo_signalquality[0] = phy_info.rx_mimo_signal_quality[0];
	pstatus->rx_mimo_signalquality[1] = phy_info.rx_mimo_signal_quality[1];
	pstatus->rx_packet_bw =
		phy_info.band_width; /* HT_CHANNEL_WIDTH_20 <- ODM_BW20M */

	/* fill driver pstatus */
	pstatus->packet_matchbssid = pktinfo.is_packet_match_bssid;
	pstatus->packet_toself = pktinfo.is_packet_to_self;
	pstatus->packet_beacon = pktinfo.is_packet_beacon;

	return true;
}

static u8 rtl_phydm_rate_id_mapping(struct rtl_priv *rtlpriv,
				    enum wireless_mode wireless_mode,
				    enum rf_type rf_type,
				    enum ht_channel_width bw)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	return phydm_rate_id_mapping(dm, wireless_mode, rf_type, bw);
}

static bool rtl_phydm_get_ra_bitmap(struct rtl_priv *rtlpriv,
				    enum wireless_mode wireless_mode,
				    enum rf_type rf_type,
				    enum ht_channel_width bw,
				    u8 tx_rate_level, /* 0~6 */
				    u32 *tx_bitmap_msb,
				    u32 *tx_bitmap_lsb)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	const u8 mimo_ps_enable = 0;
	const u8 disable_cck_rate = 0;

	phydm_update_hal_ra_mask(dm, wireless_mode, rf_type, bw, mimo_ps_enable,
				 disable_cck_rate, tx_bitmap_msb, tx_bitmap_lsb,
				 tx_rate_level);

	return true;
}

static u8 _rtl_phydm_get_macid(struct rtl_priv *rtlpriv,
			       struct ieee80211_sta *sta)
{
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	if (mac->opmode == NL80211_IFTYPE_STATION ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
		return 0;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		   mac->opmode == NL80211_IFTYPE_ADHOC)
		return sta->aid + 1;

	return 0;
}

static bool rtl_phydm_add_sta(struct rtl_priv *rtlpriv,
			      struct ieee80211_sta *sta)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	struct rtl_sta_info *sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	u8 mac_id = _rtl_phydm_get_macid(rtlpriv, sta);

	odm_cmn_info_ptr_array_hook(dm, ODM_CMNINFO_STA_STATUS, mac_id,
				    sta_entry);

	return true;
}

static bool rtl_phydm_del_sta(struct rtl_priv *rtlpriv,
			      struct ieee80211_sta *sta)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	u8 mac_id = _rtl_phydm_get_macid(rtlpriv, sta);

	odm_cmn_info_ptr_array_hook(dm, ODM_CMNINFO_STA_STATUS, mac_id, NULL);

	return true;
}

static u32 rtl_phydm_get_version(struct rtl_priv *rtlpriv)
{
	u32 ver = 0;

	if (IS_HARDWARE_TYPE_8822B(rtlpriv))
		ver = RELEASE_VERSION_8822B;

	return ver;
}

static bool rtl_phydm_modify_ra_pcr_threshold(struct rtl_priv *rtlpriv,
					      u8 ra_offset_direction,
					      u8 ra_threshold_offset)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	phydm_modify_RA_PCR_threshold(dm, ra_offset_direction,
				      ra_threshold_offset);

	return true;
}

static u32 rtl_phydm_query_counter(struct rtl_priv *rtlpriv,
				   const char *info_type)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);
	static const struct query_entry {
		const char *query_name;
		enum phydm_info_query query_id;
	} query_table[] = {
#define QUERY_ENTRY(name)	{#name, name}
		QUERY_ENTRY(PHYDM_INFO_FA_OFDM),
		QUERY_ENTRY(PHYDM_INFO_FA_CCK),
		QUERY_ENTRY(PHYDM_INFO_CCA_OFDM),
		QUERY_ENTRY(PHYDM_INFO_CCA_CCK),
		QUERY_ENTRY(PHYDM_INFO_CRC32_OK_CCK),
		QUERY_ENTRY(PHYDM_INFO_CRC32_OK_LEGACY),
		QUERY_ENTRY(PHYDM_INFO_CRC32_OK_HT),
		QUERY_ENTRY(PHYDM_INFO_CRC32_OK_VHT),
		QUERY_ENTRY(PHYDM_INFO_CRC32_ERROR_CCK),
		QUERY_ENTRY(PHYDM_INFO_CRC32_ERROR_LEGACY),
		QUERY_ENTRY(PHYDM_INFO_CRC32_ERROR_HT),
		QUERY_ENTRY(PHYDM_INFO_CRC32_ERROR_VHT),
	};
#define QUERY_TABLE_SIZE	ARRAY_SIZE(query_table)

	int i;
	const struct query_entry *entry;

	if (!strcmp(info_type, "IQK_TOTAL"))
		return dm->n_iqk_cnt;

	if (!strcmp(info_type, "IQK_OK"))
		return dm->n_iqk_ok_cnt;

	if (!strcmp(info_type, "IQK_FAIL"))
		return dm->n_iqk_fail_cnt;

	for (i = 0; i < QUERY_TABLE_SIZE; i++) {
		entry = &query_table[i];

		if (!strcmp(info_type, entry->query_name))
			return phydm_cmn_info_query(dm, entry->query_id);
	}

	pr_err("Unrecognized info_type:%s!!!!:\n", info_type);

	return 0xDEADDEAD;
}

static bool rtl_phydm_debug_cmd(struct rtl_priv *rtlpriv, char *in, u32 in_len,
				char *out, u32 out_len)
{
	struct phy_dm_struct *dm = rtlpriv_to_phydm(rtlpriv);

	phydm_cmd(dm, in, in_len, 1, out, out_len);

	return true;
}

static struct rtl_phydm_ops rtl_phydm_operation = {
	/* init/deinit priv */
	.phydm_init_priv = rtl_phydm_init_priv,
	.phydm_deinit_priv = rtl_phydm_deinit_priv,
	.phydm_load_txpower_by_rate = rtl_phydm_load_txpower_by_rate,
	.phydm_load_txpower_limit = rtl_phydm_load_txpower_limit,

	/* init hw */
	.phydm_init_dm = rtl_phydm_init_dm,
	.phydm_deinit_dm = rtl_phydm_deinit_dm,
	.phydm_reset_dm = rtl_phydm_reset_dm,
	.phydm_parameter_init = rtl_phydm_parameter_init,
	.phydm_phy_bb_config = rtl_phydm_phy_bb_config,
	.phydm_phy_rf_config = rtl_phydm_phy_rf_config,
	.phydm_phy_mac_config = rtl_phydm_phy_mac_config,
	.phydm_trx_mode = rtl_phydm_trx_mode,

	/* watchdog */
	.phydm_watchdog = rtl_phydm_watchdog,

	/* channel */
	.phydm_switch_band = rtl_phydm_switch_band,
	.phydm_switch_channel = rtl_phydm_switch_channel,
	.phydm_switch_bandwidth = rtl_phydm_switch_bandwidth,
	.phydm_iq_calibrate = rtl_phydm_iq_calibrate,
	.phydm_clear_txpowertracking_state =
		rtl_phydm_clear_txpowertracking_state,
	.phydm_pause_dig = rtl_phydm_pause_dig,

	/* read/write reg */
	.phydm_read_rf_reg = rtl_phydm_read_rf_reg,
	.phydm_write_rf_reg = rtl_phydm_write_rf_reg,
	.phydm_read_txagc = rtl_phydm_read_txagc,
	.phydm_write_txagc = rtl_phydm_write_txagc,

	/* RX */
	.phydm_c2h_content_parsing = rtl_phydm_c2h_content_parsing,
	.phydm_query_phy_status = rtl_phydm_query_phy_status,

	/* TX */
	.phydm_rate_id_mapping = rtl_phydm_rate_id_mapping,
	.phydm_get_ra_bitmap = rtl_phydm_get_ra_bitmap,

	/* STA */
	.phydm_add_sta = rtl_phydm_add_sta,
	.phydm_del_sta = rtl_phydm_del_sta,

	/* BTC */
	.phydm_get_version = rtl_phydm_get_version,
	.phydm_modify_ra_pcr_threshold = rtl_phydm_modify_ra_pcr_threshold,
	.phydm_query_counter = rtl_phydm_query_counter,

	/* debug */
	.phydm_debug_cmd = rtl_phydm_debug_cmd,
};

struct rtl_phydm_ops *rtl_phydm_get_ops_pointer(void)
{
	return &rtl_phydm_operation;
}
EXPORT_SYMBOL(rtl_phydm_get_ops_pointer);

/* ********************************************************
 * Define phydm callout function in below
 * ********************************************************
 */

u8 phy_get_tx_power_index(void *adapter, u8 rf_path, u8 rate,
			  enum ht_channel_width bandwidth, u8 channel)
{
	/* rate: DESC_RATE1M */
	struct rtl_priv *rtlpriv = (struct rtl_priv *)adapter;

	return rtlpriv->cfg->ops->get_txpower_index(rtlpriv->hw, rf_path, rate,
						    bandwidth, channel);
}

void phy_set_tx_power_index_by_rs(void *adapter, u8 ch, u8 path, u8 rs)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)adapter;

	return rtlpriv->cfg->ops->set_tx_power_index_by_rs(rtlpriv->hw, ch,
							   path, rs);
}

void phy_store_tx_power_by_rate(void *adapter, u32 band, u32 rfpath, u32 txnum,
				u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)adapter;

	rtlpriv->cfg->ops->store_tx_power_by_rate(
		rtlpriv->hw, band, rfpath, txnum, regaddr, bitmask, data);
}

void phy_set_tx_power_limit(void *dm, u8 *regulation, u8 *band, u8 *bandwidth,
			    u8 *rate_section, u8 *rf_path, u8 *channel,
			    u8 *power_limit)
{
	struct rtl_priv *rtlpriv =
		(struct rtl_priv *)((struct phy_dm_struct *)dm)->adapter;

	rtlpriv->cfg->ops->phy_set_txpower_limit(rtlpriv->hw, regulation, band,
						 bandwidth, rate_section,
						 rf_path, channel, power_limit);
}

void rtl_hal_update_ra_mask(void *adapter, struct rtl_sta_info *psta,
			    u8 rssi_level)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)adapter;
	struct ieee80211_sta *sta =
		container_of((void *)psta, struct ieee80211_sta, drv_priv);

	rtlpriv->cfg->ops->update_rate_tbl(rtlpriv->hw, sta, rssi_level, false);
}

MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 802.11n PCI wireless core");
