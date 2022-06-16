// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "coex.h"
#include "core.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "ps.h"
#include "reg.h"
#include "util.h"

static int rtw89_fw_leave_lps_check(struct rtw89_dev *rtwdev, u8 macid)
{
	u32 pwr_en_bit = 0xE;
	u32 chk_msk = pwr_en_bit << (4 * macid);
	u32 polling;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_read32_mask, polling, !polling,
				       1000, 50000, false, rtwdev,
				       R_AX_PPWRBIT_SETTING, chk_msk);
	if (ret) {
		rtw89_info(rtwdev, "rtw89: failed to leave lps state\n");
		return -EBUSY;
	}

	return 0;
}

static void rtw89_ps_power_mode_change_with_hci(struct rtw89_dev *rtwdev,
						bool enter)
{
	ieee80211_stop_queues(rtwdev->hw);
	rtwdev->hci.paused = true;
	flush_work(&rtwdev->txq_work);
	ieee80211_wake_queues(rtwdev->hw);

	rtw89_hci_pause(rtwdev, true);
	rtw89_mac_power_mode_change(rtwdev, enter);
	rtw89_hci_switch_mode(rtwdev, enter);
	rtw89_hci_pause(rtwdev, false);

	rtwdev->hci.paused = false;

	if (!enter) {
		local_bh_disable();
		napi_schedule(&rtwdev->napi);
		local_bh_enable();
	}
}

static void rtw89_ps_power_mode_change(struct rtw89_dev *rtwdev, bool enter)
{
	if (rtwdev->chip->low_power_hci_modes & BIT(rtwdev->ps_mode))
		rtw89_ps_power_mode_change_with_hci(rtwdev, enter);
	else
		rtw89_mac_power_mode_change(rtwdev, enter);
}

static void __rtw89_enter_ps_mode(struct rtw89_dev *rtwdev)
{
	if (!rtwdev->ps_mode)
		return;

	if (test_and_set_bit(RTW89_FLAG_LOW_POWER_MODE, rtwdev->flags))
		return;

	rtw89_ps_power_mode_change(rtwdev, true);
}

void __rtw89_leave_ps_mode(struct rtw89_dev *rtwdev)
{
	if (!rtwdev->ps_mode)
		return;

	if (test_and_clear_bit(RTW89_FLAG_LOW_POWER_MODE, rtwdev->flags))
		rtw89_ps_power_mode_change(rtwdev, false);
}

static void __rtw89_enter_lps(struct rtw89_dev *rtwdev, u8 mac_id)
{
	struct rtw89_lps_parm lps_param = {
		.macid = mac_id,
		.psmode = RTW89_MAC_AX_PS_MODE_LEGACY,
		.lastrpwm = RTW89_LAST_RPWM_PS,
	};

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_FW_CTRL);
	rtw89_fw_h2c_lps_parm(rtwdev, &lps_param);
}

static void __rtw89_leave_lps(struct rtw89_dev *rtwdev, u8 mac_id)
{
	struct rtw89_lps_parm lps_param = {
		.macid = mac_id,
		.psmode = RTW89_MAC_AX_PS_MODE_ACTIVE,
		.lastrpwm = RTW89_LAST_RPWM_ACTIVE,
	};

	rtw89_fw_h2c_lps_parm(rtwdev, &lps_param);
	rtw89_fw_leave_lps_check(rtwdev, 0);
	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_ON);
}

void rtw89_leave_ps_mode(struct rtw89_dev *rtwdev)
{
	lockdep_assert_held(&rtwdev->mutex);

	__rtw89_leave_ps_mode(rtwdev);
}

void rtw89_enter_lps(struct rtw89_dev *rtwdev, u8 mac_id)
{
	lockdep_assert_held(&rtwdev->mutex);

	if (test_and_set_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	__rtw89_enter_lps(rtwdev, mac_id);
	__rtw89_enter_ps_mode(rtwdev);
}

static void rtw89_leave_lps_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	if (rtwvif->wifi_role != RTW89_WIFI_ROLE_STATION)
		return;

	__rtw89_leave_ps_mode(rtwdev);
	__rtw89_leave_lps(rtwdev, rtwvif->mac_id);
}

void rtw89_leave_lps(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;

	lockdep_assert_held(&rtwdev->mutex);

	if (!test_and_clear_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_leave_lps_vif(rtwdev, rtwvif);
}

void rtw89_enter_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;

	set_bit(RTW89_FLAG_INACTIVE_PS, rtwdev->flags);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_mac_vif_deinit(rtwdev, rtwvif);

	rtw89_core_stop(rtwdev);
}

void rtw89_leave_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;
	int ret;

	ret = rtw89_core_start(rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to leave idle state\n");

	rtw89_set_channel(rtwdev);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_mac_vif_init(rtwdev, rtwvif);

	clear_bit(RTW89_FLAG_INACTIVE_PS, rtwdev->flags);
}

void rtw89_set_coex_ctrl_lps(struct rtw89_dev *rtwdev, bool btc_ctrl)
{
	if (btc_ctrl)
		rtw89_leave_lps(rtwdev);
}
