// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "fw.h"
#include "wow.h"
#include "reg.h"
#include "debug.h"
#include "mac.h"
#include "ps.h"

static void rtw_wow_show_wakeup_reason(struct rtw_dev *rtwdev)
{
	u8 reason;

	reason = rtw_read8(rtwdev, REG_WOWLAN_WAKE_REASON);

	if (reason == RTW_WOW_RSN_RX_DEAUTH)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx deauth\n");
	else if (reason == RTW_WOW_RSN_DISCONNECT)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: AP is off\n");
	else if (reason == RTW_WOW_RSN_RX_MAGIC_PKT)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx magic packet\n");
	else if (reason == RTW_WOW_RSN_RX_GTK_REKEY)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx gtk rekey\n");
	else if (reason == RTW_WOW_RSN_RX_PTK_REKEY)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx ptk rekey\n");
	else
		rtw_warn(rtwdev, "Unknown wakeup reason %x\n", reason);
}

static void rtw_wow_bb_stop(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	/* wait 100ms for firmware to finish TX */
	msleep(100);

	if (!rtw_read32_mask(rtwdev, REG_BCNQ_INFO, BIT_MGQ_CPU_EMPTY))
		rtw_warn(rtwdev, "Wrong status of MGQ_CPU empty!\n");

	rtw_wow->txpause = rtw_read8(rtwdev, REG_TXPAUSE);
	rtw_write8(rtwdev, REG_TXPAUSE, 0xff);
	rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
}

static void rtw_wow_bb_start(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
	rtw_write8(rtwdev, REG_TXPAUSE, rtw_wow->txpause);
}

static void rtw_wow_rx_dma_stop(struct rtw_dev *rtwdev)
{
	/* wait 100ms for HW to finish rx dma */
	msleep(100);

	rtw_write32_set(rtwdev, REG_RXPKT_NUM, BIT_RW_RELEASE);

	if (!check_hw_ready(rtwdev, REG_RXPKT_NUM, BIT_RXDMA_IDLE, 1))
		rtw_err(rtwdev, "failed to stop rx dma\n");
}

static void rtw_wow_rx_dma_start(struct rtw_dev *rtwdev)
{
	rtw_write32_clr(rtwdev, REG_RXPKT_NUM, BIT_RW_RELEASE);
}

static bool rtw_wow_check_fw_status(struct rtw_dev *rtwdev, bool wow_enable)
{
	bool ret;

	/* wait 100ms for wow firmware to finish work */
	msleep(100);

	if (wow_enable) {
		if (!rtw_read8(rtwdev, REG_WOWLAN_WAKE_REASON))
			ret = 0;
	} else {
		if (rtw_read32_mask(rtwdev, REG_FE1IMR, BIT_FS_RXDONE) == 0 &&
		    rtw_read32_mask(rtwdev, REG_RXPKT_NUM, BIT_RW_RELEASE) == 0)
			ret = 0;
	}

	if (ret)
		rtw_err(rtwdev, "failed to check wow status %s\n",
			wow_enable ? "enabled" : "disabled");

	return ret;
}

static void rtw_wow_fw_security_type_iter(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_key_conf *key,
					  void *data)
{
	struct rtw_fw_key_type_iter_data *iter_data = data;
	struct rtw_dev *rtwdev = hw->priv;
	u8 hw_key_type;

	if (vif != rtwdev->wow.wow_vif)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		hw_key_type = RTW_CAM_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		hw_key_type = RTW_CAM_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		hw_key_type = RTW_CAM_TKIP;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		hw_key_type = RTW_CAM_AES;
		key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	default:
		rtw_err(rtwdev, "Unsupported key type for wowlan mode\n");
		hw_key_type = 0;
		break;
	}

	if (sta)
		iter_data->pairwise_key_type = hw_key_type;
	else
		iter_data->group_key_type = hw_key_type;
}

static void rtw_wow_fw_security_type(struct rtw_dev *rtwdev)
{
	struct rtw_fw_key_type_iter_data data = {};
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;

	data.rtwdev = rtwdev;
	rtw_iterate_keys(rtwdev, wow_vif,
			 rtw_wow_fw_security_type_iter, &data);
	rtw_fw_set_aoac_global_info_cmd(rtwdev, data.pairwise_key_type,
					data.group_key_type);
}

static int rtw_wow_fw_start(struct rtw_dev *rtwdev)
{
	if (rtw_wow_mgd_linked(rtwdev)) {
		rtw_send_rsvd_page_h2c(rtwdev);
		rtw_wow_fw_security_type(rtwdev);
		rtw_fw_set_disconnect_decision_cmd(rtwdev, true);
		rtw_fw_set_keep_alive_cmd(rtwdev, true);
	}

	rtw_fw_set_wowlan_ctrl_cmd(rtwdev, true);
	rtw_fw_set_remote_wake_ctrl_cmd(rtwdev, true);

	return rtw_wow_check_fw_status(rtwdev, true);
}

static int rtw_wow_fw_stop(struct rtw_dev *rtwdev)
{
	if (rtw_wow_mgd_linked(rtwdev)) {
		rtw_fw_set_disconnect_decision_cmd(rtwdev, false);
		rtw_fw_set_keep_alive_cmd(rtwdev, false);
	}

	rtw_fw_set_wowlan_ctrl_cmd(rtwdev, false);
	rtw_fw_set_remote_wake_ctrl_cmd(rtwdev, false);

	return rtw_wow_check_fw_status(rtwdev, false);
}

static void rtw_wow_avoid_reset_mac(struct rtw_dev *rtwdev)
{
	/* When resuming from wowlan mode, some hosts issue signal
	 * (PCIE: PREST, USB: SE0RST) to device, and lead to reset
	 * mac core. If it happens, the connection to AP will be lost.
	 * Setting REG_RSV_CTRL Register can avoid this process.
	 */
	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
	case RTW_HCI_TYPE_USB:
		rtw_write8(rtwdev, REG_RSV_CTRL, BIT_WLOCK_1C_B6);
		rtw_write8(rtwdev, REG_RSV_CTRL,
			   BIT_WLOCK_1C_B6 | BIT_R_DIS_PRST);
		break;
	default:
		rtw_warn(rtwdev, "Unsupported hci type to disable reset MAC\n");
		break;
	}
}

static void rtw_wow_fw_media_status_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	struct rtw_fw_media_status_iter_data *iter_data = data;
	struct rtw_dev *rtwdev = iter_data->rtwdev;

	rtw_fw_media_status_report(rtwdev, si->mac_id, iter_data->connect);
}

static void rtw_wow_fw_media_status(struct rtw_dev *rtwdev, bool connect)
{
	struct rtw_fw_media_status_iter_data data;

	data.rtwdev = rtwdev;
	data.connect = connect;

	rtw_iterate_stas_atomic(rtwdev, rtw_wow_fw_media_status_iter, &data);
}

static void rtw_wow_config_linked_rsvd_page(struct rtw_dev *rtwdev)
{
	rtw_add_rsvd_page(rtwdev, RSVD_PS_POLL, true);
	rtw_add_rsvd_page(rtwdev, RSVD_QOS_NULL, true);
	rtw_add_rsvd_page(rtwdev, RSVD_NULL, true);
	rtw_add_rsvd_page(rtwdev, RSVD_LPS_PG_DPK, true);
	rtw_add_rsvd_page(rtwdev, RSVD_LPS_PG_INFO, true);
}

static void rtw_wow_config_rsvd_page(struct rtw_dev *rtwdev)
{
	rtw_reset_rsvd_page(rtwdev);

	if (rtw_wow_mgd_linked(rtwdev))
		rtw_wow_config_linked_rsvd_page(rtwdev);
}

static int rtw_wow_dl_fw_rsvd_page(struct rtw_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;

	rtw_wow_config_rsvd_page(rtwdev);

	return rtw_fw_download_rsvd_page(rtwdev, wow_vif);
}

static int rtw_wow_swap_fw(struct rtw_dev *rtwdev, enum rtw_fw_type type)
{
	struct rtw_fw_state *fw;
	int ret;

	switch (type) {
	case RTW_WOWLAN_FW:
		fw = &rtwdev->wow_fw;
		break;

	case RTW_NORMAL_FW:
		fw = &rtwdev->fw;
		break;

	default:
		rtw_warn(rtwdev, "unsupported firmware type to swap\n");
		return -ENOENT;
	}

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret)
		goto out;

	rtw_fw_send_general_info(rtwdev);
	rtw_fw_send_phydm_info(rtwdev);
	rtw_wow_fw_media_status(rtwdev, true);

out:
	return ret;
}

static int rtw_wow_leave_linked_ps(struct rtw_dev *rtwdev)
{
	if (!test_bit(RTW_FLAG_WOWLAN, rtwdev->flags))
		cancel_delayed_work_sync(&rtwdev->watch_dog_work);

	return 0;
}

static int rtw_wow_leave_ps(struct rtw_dev *rtwdev)
{
	int ret = 0;

	if (rtw_wow_mgd_linked(rtwdev))
		ret = rtw_wow_leave_linked_ps(rtwdev);

	return ret;
}

static int rtw_wow_enter_linked_ps(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw_vif *rtwvif = (struct rtw_vif *)wow_vif->drv_priv;

	rtw_enter_lps(rtwdev, rtwvif->port);

	return 0;
}

static int rtw_wow_enter_ps(struct rtw_dev *rtwdev)
{
	int ret = 0;

	if (rtw_wow_mgd_linked(rtwdev))
		ret = rtw_wow_enter_linked_ps(rtwdev);

	return ret;
}

static void rtw_wow_stop_trx(struct rtw_dev *rtwdev)
{
	rtw_wow_bb_stop(rtwdev);
	rtw_wow_rx_dma_stop(rtwdev);
}

static int rtw_wow_start(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_wow_fw_start(rtwdev);
	if (ret)
		goto out;

	rtw_hci_stop(rtwdev);
	rtw_wow_bb_start(rtwdev);
	rtw_wow_avoid_reset_mac(rtwdev);

out:
	return ret;
}

static int rtw_wow_enable(struct rtw_dev *rtwdev)
{
	int ret = 0;

	rtw_wow_stop_trx(rtwdev);

	ret = rtw_wow_swap_fw(rtwdev, RTW_WOWLAN_FW);
	if (ret) {
		rtw_err(rtwdev, "failed to swap wow fw\n");
		goto error;
	}

	set_bit(RTW_FLAG_WOWLAN, rtwdev->flags);

	ret = rtw_wow_dl_fw_rsvd_page(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to download wowlan rsvd page\n");
		goto error;
	}

	ret = rtw_wow_start(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to start wow\n");
		goto error;
	}

	return ret;

error:
	clear_bit(RTW_FLAG_WOWLAN, rtwdev->flags);
	return ret;
}

static int rtw_wow_stop(struct rtw_dev *rtwdev)
{
	int ret;

	/* some HCI related registers will be reset after resume,
	 * need to set them again.
	 */
	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		return ret;
	}

	ret = rtw_hci_start(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to start hci\n");
		return ret;
	}

	ret = rtw_wow_fw_stop(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to stop wowlan fw\n");

	rtw_wow_bb_stop(rtwdev);

	return ret;
}

static void rtw_wow_resume_trx(struct rtw_dev *rtwdev)
{
	rtw_wow_rx_dma_start(rtwdev);
	rtw_wow_bb_start(rtwdev);
	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);
}

static int rtw_wow_disable(struct rtw_dev *rtwdev)
{
	int ret;

	clear_bit(RTW_FLAG_WOWLAN, rtwdev->flags);

	ret = rtw_wow_stop(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to stop wow\n");
		goto out;
	}

	ret = rtw_wow_swap_fw(rtwdev, RTW_NORMAL_FW);
	if (ret) {
		rtw_err(rtwdev, "failed to swap normal fw\n");
		goto out;
	}

	ret = rtw_wow_dl_fw_rsvd_page(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to download normal rsvd page\n");

out:
	rtw_wow_resume_trx(rtwdev);
	return ret;
}

static void rtw_wow_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	/* Current wowlan function support setting of only one STATION vif.
	 * So when one suitable vif is found, stop the iteration.
	 */
	if (rtw_wow->wow_vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	switch (rtwvif->net_type) {
	case RTW_NET_MGD_LINKED:
		rtw_wow->wow_vif = vif;
		break;
	default:
		break;
	}
}

static int rtw_wow_set_wakeups(struct rtw_dev *rtwdev,
			       struct cfg80211_wowlan *wowlan)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	if (wowlan->disconnect)
		set_bit(RTW_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags);
	if (wowlan->magic_pkt)
		set_bit(RTW_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags);
	if (wowlan->gtk_rekey_failure)
		set_bit(RTW_WOW_FLAG_EN_REKEY_PKT, rtw_wow->flags);

	rtw_iterate_vifs_atomic(rtwdev, rtw_wow_vif_iter, rtwdev);
	if (!rtw_wow->wow_vif)
		return -EPERM;

	return 0;
}

static void rtw_wow_clear_wakeups(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	memset(rtw_wow, 0, sizeof(rtwdev->wow));
}

int rtw_wow_suspend(struct rtw_dev *rtwdev, struct cfg80211_wowlan *wowlan)
{
	int ret = 0;

	ret = rtw_wow_set_wakeups(rtwdev, wowlan);
	if (ret) {
		rtw_err(rtwdev, "failed to set wakeup event\n");
		goto out;
	}

	ret = rtw_wow_leave_ps(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to leave ps from normal mode\n");
		goto out;
	}

	ret = rtw_wow_enable(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to enable wow\n");
		goto out;
	}

	ret = rtw_wow_enter_ps(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to enter ps for wow\n");

out:
	return ret;
}

int rtw_wow_resume(struct rtw_dev *rtwdev)
{
	int ret;

	/* If wowlan mode is not enabled, do nothing */
	if (!test_bit(RTW_FLAG_WOWLAN, rtwdev->flags)) {
		rtw_err(rtwdev, "wow is not enabled\n");
		ret = -EPERM;
		goto out;
	}

	ret = rtw_wow_leave_ps(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to leave ps from wowlan mode\n");
		goto out;
	}

	rtw_wow_show_wakeup_reason(rtwdev);

	ret = rtw_wow_disable(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to disable wow\n");

out:
	rtw_wow_clear_wakeups(rtwdev);
	return ret;
}
