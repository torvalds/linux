// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */
#include "cam.h"
#include "core.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "util.h"
#include "wow.h"

static void rtw89_wow_leave_deep_ps(struct rtw89_dev *rtwdev)
{
	__rtw89_leave_ps_mode(rtwdev);
}

static void rtw89_wow_enter_deep_ps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)wow_vif->drv_priv;

	__rtw89_enter_ps_mode(rtwdev, rtwvif);
}

static void rtw89_wow_enter_lps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)wow_vif->drv_priv;

	rtw89_enter_lps(rtwdev, rtwvif);
}

static void rtw89_wow_leave_lps(struct rtw89_dev *rtwdev)
{
	rtw89_leave_lps(rtwdev);
}

static int rtw89_wow_config_mac(struct rtw89_dev *rtwdev, bool enable_wow)
{
	int ret;

	if (enable_wow) {
		ret = rtw89_mac_resize_ple_rx_quota(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]patch rx qta %d\n", ret);
			return ret;
		}
		rtw89_write32_set(rtwdev, R_AX_RX_FUNCTION_STOP, B_AX_HDR_RX_STOP);
		rtw89_write32_clr(rtwdev, R_AX_RX_FLTR_OPT, B_AX_SNIFFER_MODE);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD0, 0);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD1, 0);
		rtw89_write32(rtwdev, R_AX_TF_FWD, 0);
		rtw89_write32(rtwdev, R_AX_HW_RPT_FWD, 0);
	} else {
		ret = rtw89_mac_resize_ple_rx_quota(rtwdev, false);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]patch rx qta %d\n", ret);
			return ret;
		}
		rtw89_write32_clr(rtwdev, R_AX_RX_FUNCTION_STOP, B_AX_HDR_RX_STOP);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD0, TRXCFG_MPDU_PROC_ACT_FRWD);
		rtw89_write32(rtwdev, R_AX_TF_FWD, TRXCFG_MPDU_PROC_TF_FRWD);
	}

	return 0;
}

static void rtw89_wow_set_rx_filter(struct rtw89_dev *rtwdev, bool enable)
{
	enum rtw89_mac_fwd_target fwd_target = enable ?
					       RTW89_FWD_DONT_CARE :
					       RTW89_FWD_TO_HOST;

	rtw89_mac_typ_fltr_opt(rtwdev, RTW89_MGNT, fwd_target, RTW89_MAC_0);
	rtw89_mac_typ_fltr_opt(rtwdev, RTW89_CTRL, fwd_target, RTW89_MAC_0);
	rtw89_mac_typ_fltr_opt(rtwdev, RTW89_DATA, fwd_target, RTW89_MAC_0);
}

static void rtw89_wow_show_wakeup_reason(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	struct cfg80211_wowlan_nd_info nd_info;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	u32 wow_reason_reg;
	u8 reason;

	if (chip_id == RTL8852A || chip_id == RTL8852B)
		wow_reason_reg = R_AX_C2HREG_DATA3 + 3;
	else
		wow_reason_reg = R_AX_C2HREG_DATA3_V1 + 3;

	reason = rtw89_read8(rtwdev, wow_reason_reg);

	switch (reason) {
	case RTW89_WOW_RSN_RX_DEAUTH:
		wakeup.disconnect = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx deauth\n");
		break;
	case RTW89_WOW_RSN_DISCONNECT:
		wakeup.disconnect = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: AP is off\n");
		break;
	case RTW89_WOW_RSN_RX_MAGIC_PKT:
		wakeup.magic_pkt = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx magic packet\n");
		break;
	case RTW89_WOW_RSN_RX_GTK_REKEY:
		wakeup.gtk_rekey_failure = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx gtk rekey\n");
		break;
	case RTW89_WOW_RSN_RX_PATTERN_MATCH:
		/* Current firmware and driver don't report pattern index
		 * Use pattern_idx to 0 defaultly.
		 */
		wakeup.pattern_idx = 0;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx pattern match packet\n");
		break;
	case RTW89_WOW_RSN_RX_NLO:
		/* Current firmware and driver don't report ssid index.
		 * Use 0 for n_matches based on its comment.
		 */
		nd_info.n_matches = 0;
		wakeup.net_detect = &nd_info;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "Rx NLO\n");
		break;
	default:
		rtw89_warn(rtwdev, "Unknown wakeup reason %x\n", reason);
		ieee80211_report_wowlan_wakeup(rtwdev->wow.wow_vif, NULL,
					       GFP_KERNEL);
		return;
	}

	ieee80211_report_wowlan_wakeup(rtwdev->wow.wow_vif, &wakeup,
				       GFP_KERNEL);
}

static void rtw89_wow_vif_iter(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);

	/* Current wowlan function support setting of only one STATION vif.
	 * So when one suitable vif is found, stop the iteration.
	 */
	if (rtw_wow->wow_vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	switch (rtwvif->net_type) {
	case RTW89_NET_TYPE_INFRA:
		rtw_wow->wow_vif = vif;
		break;
	case RTW89_NET_TYPE_NO_LINK:
	default:
		break;
	}
}

static u16 __rtw89_cal_crc16(u8 data, u16 crc)
{
	u8 shift_in, data_bit;
	u8 crc_bit4, crc_bit11, crc_bit15;
	u16 crc_result;
	int index;

	for (index = 0; index < 8; index++) {
		crc_bit15 = crc & BIT(15) ? 1 : 0;
		data_bit = data & BIT(index) ? 1 : 0;
		shift_in = crc_bit15 ^ data_bit;

		crc_result = crc << 1;

		if (shift_in == 0)
			crc_result &= ~BIT(0);
		else
			crc_result |= BIT(0);

		crc_bit11 = (crc & BIT(11) ? 1 : 0) ^ shift_in;

		if (crc_bit11 == 0)
			crc_result &= ~BIT(12);
		else
			crc_result |= BIT(12);

		crc_bit4 = (crc & BIT(4) ? 1 : 0) ^ shift_in;

		if (crc_bit4 == 0)
			crc_result &= ~BIT(5);
		else
			crc_result |= BIT(5);

		crc = crc_result;
	}
	return crc;
}

static u16 rtw89_calc_crc(u8 *pdata, int length)
{
	u16 crc = 0xffff;
	int i;

	for (i = 0; i < length; i++)
		crc = __rtw89_cal_crc16(pdata[i], crc);

	/* get 1' complement */
	return ~crc;
}

static int rtw89_wow_pattern_get_type(struct rtw89_vif *rtwvif,
				      struct rtw89_wow_cam_info *rtw_pattern,
				      const u8 *pattern, u8 da_mask)
{
	u8 da[ETH_ALEN];

	ether_addr_copy_mask(da, pattern, da_mask);

	/* Each pattern is divided into different kinds by DA address
	 *  a. DA is broadcast address: set bc = 0;
	 *  b. DA is multicast address: set mc = 0
	 *  c. DA is unicast address same as dev's mac address: set uc = 0
	 *  d. DA is unmasked. Also called wildcard type: set uc = bc = mc = 0
	 *  e. Others is invalid type.
	 */

	if (is_broadcast_ether_addr(da))
		rtw_pattern->bc = true;
	else if (is_multicast_ether_addr(da))
		rtw_pattern->mc = true;
	else if (ether_addr_equal(da, rtwvif->mac_addr) &&
		 da_mask == GENMASK(5, 0))
		rtw_pattern->uc = true;
	else if (!da_mask) /*da_mask == 0 mean wildcard*/
		return 0;
	else
		return -EPERM;

	return 0;
}

static int rtw89_wow_pattern_generate(struct rtw89_dev *rtwdev,
				      struct rtw89_vif *rtwvif,
				      const struct cfg80211_pkt_pattern *pkt_pattern,
				      struct rtw89_wow_cam_info *rtw_pattern)
{
	u8 mask_hw[RTW89_MAX_PATTERN_MASK_SIZE * 4] = {0};
	u8 content[RTW89_MAX_PATTERN_SIZE] = {0};
	const u8 *mask;
	const u8 *pattern;
	u8 mask_len;
	u16 count;
	u32 len;
	int i, ret;

	pattern = pkt_pattern->pattern;
	len = pkt_pattern->pattern_len;
	mask = pkt_pattern->mask;
	mask_len = DIV_ROUND_UP(len, 8);
	memset(rtw_pattern, 0, sizeof(*rtw_pattern));

	ret = rtw89_wow_pattern_get_type(rtwvif, rtw_pattern, pattern,
					 mask[0] & GENMASK(5, 0));
	if (ret)
		return ret;

	/* translate mask from os to mask for hw
	 * pattern from OS uses 'ethenet frame', like this:
	 * |    6   |    6   |   2  |     20    |  Variable  |  4  |
	 * |--------+--------+------+-----------+------------+-----|
	 * |    802.3 Mac Header    | IP Header | TCP Packet | FCS |
	 * |   DA   |   SA   | Type |
	 *
	 * BUT, packet catched by our HW is in '802.11 frame', begin from LLC
	 * |     24 or 30      |    6   |   2  |     20    |  Variable  |  4  |
	 * |-------------------+--------+------+-----------+------------+-----|
	 * | 802.11 MAC Header |       LLC     | IP Header | TCP Packet | FCS |
	 *		       | Others | Tpye |
	 *
	 * Therefore, we need translate mask_from_OS to mask_to_hw.
	 * We should left-shift mask by 6 bits, then set the new bit[0~5] = 0,
	 * because new mask[0~5] means 'SA', but our HW packet begins from LLC,
	 * bit[0~5] corresponds to first 6 Bytes in LLC, they just don't match.
	 */

	/* Shift 6 bits */
	for (i = 0; i < mask_len - 1; i++) {
		mask_hw[i] = u8_get_bits(mask[i], GENMASK(7, 6)) |
			     u8_get_bits(mask[i + 1], GENMASK(5, 0)) << 2;
	}
	mask_hw[i] = u8_get_bits(mask[i], GENMASK(7, 6));

	/* Set bit 0-5 to zero */
	mask_hw[0] &= ~GENMASK(5, 0);

	memcpy(rtw_pattern->mask, mask_hw, sizeof(rtw_pattern->mask));

	/* To get the wake up pattern from the mask.
	 * We do not count first 12 bits which means
	 * DA[6] and SA[6] in the pattern to match HW design.
	 */
	count = 0;
	for (i = 12; i < len; i++) {
		if ((mask[i / 8] >> (i % 8)) & 0x01) {
			content[count] = pattern[i];
			count++;
		}
	}

	rtw_pattern->crc = rtw89_calc_crc(content, count);

	return 0;
}

static int rtw89_wow_parse_patterns(struct rtw89_dev *rtwdev,
				    struct rtw89_vif *rtwvif,
				    struct cfg80211_wowlan *wowlan)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i;
	int ret;

	if (!wowlan->n_patterns || !wowlan->patterns)
		return 0;

	for (i = 0; i < wowlan->n_patterns; i++) {
		rtw_pattern = &rtw_wow->patterns[i];
		ret = rtw89_wow_pattern_generate(rtwdev, rtwvif,
						 &wowlan->patterns[i],
						 rtw_pattern);
		if (ret) {
			rtw89_err(rtwdev, "failed to generate pattern(%d)\n", i);
			rtw_wow->pattern_cnt = 0;
			return ret;
		}

		rtw_pattern->r_w = true;
		rtw_pattern->idx = i;
		rtw_pattern->negative_pattern_match = false;
		rtw_pattern->skip_mac_hdr = true;
		rtw_pattern->valid = true;
	}
	rtw_wow->pattern_cnt = wowlan->n_patterns;

	return 0;
}

static void rtw89_wow_pattern_clear_cam(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i = 0;

	for (i = 0; i < rtw_wow->pattern_cnt; i++) {
		rtw_pattern = &rtw_wow->patterns[i];
		rtw_pattern->valid = false;
		rtw89_fw_wow_cam_update(rtwdev, rtw_pattern);
	}
}

static void rtw89_wow_pattern_write(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i;

	for (i = 0; i < rtw_wow->pattern_cnt; i++)
		rtw89_fw_wow_cam_update(rtwdev, rtw_pattern + i);
}

static void rtw89_wow_pattern_clear(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	rtw89_wow_pattern_clear_cam(rtwdev);

	rtw_wow->pattern_cnt = 0;
	memset(rtw_wow->patterns, 0, sizeof(rtw_wow->patterns));
}

static void rtw89_wow_clear_wakeups(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	rtw_wow->wow_vif = NULL;
	rtw89_core_release_all_bits_map(rtw_wow->flags, RTW89_WOW_FLAG_NUM);
	rtw_wow->pattern_cnt = 0;
}

static int rtw89_wow_set_wakeups(struct rtw89_dev *rtwdev,
				 struct cfg80211_wowlan *wowlan)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_vif *rtwvif;

	if (wowlan->disconnect)
		set_bit(RTW89_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags);
	if (wowlan->magic_pkt)
		set_bit(RTW89_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_wow_vif_iter(rtwdev, rtwvif);

	if (!rtw_wow->wow_vif)
		return -EPERM;

	rtwvif = (struct rtw89_vif *)rtw_wow->wow_vif->drv_priv;
	return rtw89_wow_parse_patterns(rtwdev, rtwvif, wowlan);
}

static int rtw89_wow_cfg_wake(struct rtw89_dev *rtwdev, bool wow)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)wow_vif->drv_priv;
	struct ieee80211_sta *wow_sta;
	struct rtw89_sta *rtwsta = NULL;
	bool is_conn = true;
	int ret;

	wow_sta = ieee80211_find_sta(wow_vif, rtwvif->bssid);
	if (wow_sta)
		rtwsta = (struct rtw89_sta *)wow_sta->drv_priv;
	else
		is_conn = false;

	if (wow) {
		if (rtw_wow->pattern_cnt)
			rtwvif->wowlan_pattern = true;
		if (test_bit(RTW89_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags))
			rtwvif->wowlan_magic = true;
	} else {
		rtwvif->wowlan_pattern = false;
		rtwvif->wowlan_magic = false;
	}

	ret = rtw89_fw_h2c_wow_wakeup_ctrl(rtwdev, rtwvif, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow wakeup ctrl\n");
		return ret;
	}

	if (wow) {
		ret = rtw89_chip_h2c_dctl_sec_cam(rtwdev, rtwvif, rtwsta);
		if (ret) {
			rtw89_err(rtwdev, "failed to update dctl cam sec entry: %d\n",
				  ret);
			return ret;
		}
	}

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, rtwsta, !is_conn);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif, rtwsta, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	ret = rtw89_fw_h2c_wow_global(rtwdev, rtwvif, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow global\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_check_fw_status(struct rtw89_dev *rtwdev, bool wow_enable)
{
	u8 polling;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_read8_mask, polling,
				       wow_enable == !!polling,
				       50, 50000, false, rtwdev,
				       R_AX_WOW_CTRL, B_AX_WOW_WOWEN);
	if (ret)
		rtw89_err(rtwdev, "failed to check wow status %s\n",
			  wow_enable ? "enabled" : "disabled");
	return ret;
}

static void rtw89_wow_release_pkt_list(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct list_head *pkt_list = &rtw_wow->pkt_list;
	struct rtw89_pktofld_info *info, *tmp;

	list_for_each_entry_safe(info, tmp, pkt_list, list) {
		rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);
		rtw89_core_release_bit_map(rtwdev->pkt_offload,
					   info->id);
		list_del(&info->list);
		kfree(info);
	}
}

static int rtw89_wow_swap_fw(struct rtw89_dev *rtwdev, bool wow)
{
	enum rtw89_fw_type fw_type = wow ? RTW89_FW_WOWLAN : RTW89_FW_NORMAL;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)wow_vif->drv_priv;
	struct ieee80211_sta *wow_sta;
	struct rtw89_sta *rtwsta = NULL;
	bool is_conn = true;
	int ret;

	rtw89_hci_disable_intr(rtwdev);

	wow_sta = ieee80211_find_sta(wow_vif, rtwvif->bssid);
	if (wow_sta)
		rtwsta = (struct rtw89_sta *)wow_sta->drv_priv;
	else
		is_conn = false;

	ret = rtw89_fw_download(rtwdev, fw_type);
	if (ret) {
		rtw89_warn(rtwdev, "download fw failed\n");
		return ret;
	}

	rtw89_phy_init_rf_reg(rtwdev, true);

	ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif, rtwsta,
					 RTW89_ROLE_FW_RESTORE);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c role maintain\n");
		return ret;
	}

	ret = rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, wow_vif, wow_sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c assoc cmac tbl\n");
		return ret;
	}

	if (!is_conn)
		rtw89_cam_reset_keys(rtwdev);

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, rtwsta, !is_conn);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif, rtwsta, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	if (is_conn) {
		rtw89_phy_ra_assoc(rtwdev, wow_sta);
		rtw89_phy_set_bss_color(rtwdev, wow_vif);
		rtw89_chip_cfg_txpwr_ul_tb_offset(rtwdev, wow_vif);
	}

	rtw89_mac_hw_mgnt_sec(rtwdev, wow);
	rtw89_hci_enable_intr(rtwdev);

	return 0;
}

static int rtw89_wow_enable_trx_pre(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_ctrl_txdma_ch(rtwdev, false);
	rtw89_hci_ctrl_txdma_fw_ch(rtwdev, true);

	rtw89_mac_ptk_drop_by_band_and_wait(rtwdev, RTW89_MAC_0);

	ret = rtw89_hci_poll_txdma_ch(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "txdma ch busy\n");
		return ret;
	}
	rtw89_wow_set_rx_filter(rtwdev, true);

	ret = rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
	if (ret) {
		rtw89_err(rtwdev, "cfg ppdu status\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_enable_trx_post(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_disable_intr(rtwdev);
	rtw89_hci_ctrl_trxhci(rtwdev, false);

	ret = rtw89_hci_poll_txdma_ch(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to poll txdma ch idle pcie\n");
		return ret;
	}

	ret = rtw89_wow_config_mac(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "failed to config mac\n");
		return ret;
	}

	rtw89_wow_set_rx_filter(rtwdev, false);
	rtw89_hci_reset(rtwdev);

	return 0;
}

static int rtw89_wow_disable_trx_pre(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_clr_idx_all(rtwdev);

	ret = rtw89_hci_rst_bdram(rtwdev);
	if (ret) {
		rtw89_warn(rtwdev, "reset bdram busy\n");
		return ret;
	}

	rtw89_hci_ctrl_trxhci(rtwdev, true);
	rtw89_hci_ctrl_txdma_ch(rtwdev, true);

	ret = rtw89_wow_config_mac(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to config mac\n");
		return ret;
	}
	rtw89_hci_enable_intr(rtwdev);

	return 0;
}

static int rtw89_wow_disable_trx_post(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
	if (ret)
		rtw89_err(rtwdev, "cfg ppdu status\n");

	return ret;
}

static int rtw89_wow_fw_start(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)rtw_wow->wow_vif->drv_priv;
	int ret;

	rtw89_wow_pattern_write(rtwdev);

	ret = rtw89_fw_h2c_keep_alive(rtwdev, rtwvif, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable keep alive\n");
		return ret;
	}

	ret = rtw89_fw_h2c_disconnect_detect(rtwdev, rtwvif, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable disconnect detect\n");
		goto out;
	}

	ret = rtw89_wow_cfg_wake(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to config wake\n");
		goto out;
	}

	ret = rtw89_wow_check_fw_status(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to check enable fw ready\n");
		goto out;
	}

out:
	return ret;
}

static int rtw89_wow_fw_stop(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)rtw_wow->wow_vif->drv_priv;
	int ret;

	rtw89_wow_pattern_clear(rtwdev);

	ret = rtw89_fw_h2c_keep_alive(rtwdev, rtwvif, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable keep alive\n");
		goto out;
	}

	rtw89_wow_release_pkt_list(rtwdev);

	ret = rtw89_fw_h2c_disconnect_detect(rtwdev, rtwvif, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable disconnect detect\n");
		goto out;
	}

	ret = rtw89_wow_cfg_wake(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable config wake\n");
		goto out;
	}

	ret = rtw89_wow_check_fw_status(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to check disable fw ready\n");
		goto out;
	}

out:
	return ret;
}

static int rtw89_wow_enable(struct rtw89_dev *rtwdev)
{
	int ret;

	set_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);

	ret = rtw89_wow_enable_trx_pre(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable trx_pre\n");
		goto out;
	}

	ret = rtw89_wow_swap_fw(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to swap to wow fw\n");
		goto out;
	}

	ret = rtw89_wow_fw_start(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to let wow fw start\n");
		goto out;
	}

	rtw89_wow_enter_lps(rtwdev);

	ret = rtw89_wow_enable_trx_post(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable trx_post\n");
		goto out;
	}

	return 0;

out:
	clear_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);
	return ret;
}

static int rtw89_wow_disable(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_wow_disable_trx_pre(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_pre\n");
		goto out;
	}

	rtw89_wow_leave_lps(rtwdev);

	ret = rtw89_wow_fw_stop(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to swap to normal fw\n");
		goto out;
	}

	ret = rtw89_wow_swap_fw(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_post\n");
		goto out;
	}

	ret = rtw89_wow_disable_trx_post(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_pre\n");
		goto out;
	}

out:
	clear_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);
	return ret;
}

int rtw89_wow_resume(struct rtw89_dev *rtwdev)
{
	int ret;

	if (!test_bit(RTW89_FLAG_WOWLAN, rtwdev->flags)) {
		rtw89_err(rtwdev, "wow is not enabled\n");
		ret = -EPERM;
		goto out;
	}

	if (!rtw89_mac_get_power_state(rtwdev)) {
		rtw89_err(rtwdev, "chip is no power when resume\n");
		ret = -EPERM;
		goto out;
	}

	rtw89_wow_leave_deep_ps(rtwdev);

	rtw89_wow_show_wakeup_reason(rtwdev);

	ret = rtw89_wow_disable(rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to disable wow\n");

out:
	rtw89_wow_clear_wakeups(rtwdev);
	return ret;
}

int rtw89_wow_suspend(struct rtw89_dev *rtwdev, struct cfg80211_wowlan *wowlan)
{
	int ret;

	ret = rtw89_wow_set_wakeups(rtwdev, wowlan);
	if (ret) {
		rtw89_err(rtwdev, "failed to set wakeup event\n");
		return ret;
	}

	rtw89_wow_leave_lps(rtwdev);

	ret = rtw89_wow_enable(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to enable wow\n");
		return ret;
	}

	rtw89_wow_enter_deep_ps(rtwdev);

	return 0;
}
