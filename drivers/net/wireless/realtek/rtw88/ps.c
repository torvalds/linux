// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "reg.h"
#include "fw.h"
#include "ps.h"
#include "mac.h"
#include "coex.h"
#include "debug.h"

static int rtw_ips_pwr_up(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_core_start(rtwdev);
	if (ret)
		rtw_err(rtwdev, "leave idle state failed\n");

	rtw_coex_ips_notify(rtwdev, COEX_IPS_LEAVE);
	rtw_set_channel(rtwdev);

	return ret;
}

int rtw_enter_ips(struct rtw_dev *rtwdev)
{
	if (!test_bit(RTW_FLAG_POWERON, rtwdev->flags))
		return 0;

	rtw_coex_ips_notify(rtwdev, COEX_IPS_ENTER);

	rtw_core_stop(rtwdev);
	rtw_hci_link_ps(rtwdev, true);

	return 0;
}

static void rtw_restore_port_cfg_iter(void *data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = ~0;

	rtw_vif_port_config(rtwdev, rtwvif, config);
}

int rtw_leave_ips(struct rtw_dev *rtwdev)
{
	int ret;

	if (test_bit(RTW_FLAG_POWERON, rtwdev->flags))
		return 0;

	rtw_hci_link_ps(rtwdev, false);

	ret = rtw_ips_pwr_up(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to leave ips state\n");
		return ret;
	}

	rtw_iterate_vifs(rtwdev, rtw_restore_port_cfg_iter, rtwdev);

	return 0;
}

void rtw_power_mode_change(struct rtw_dev *rtwdev, bool enter)
{
	u8 request, confirm, polling;
	int ret;

	request = rtw_read8(rtwdev, rtwdev->hci.rpwm_addr);
	confirm = rtw_read8(rtwdev, rtwdev->hci.cpwm_addr);

	/* toggle to request power mode, others remain 0 */
	request ^= request | BIT_RPWM_TOGGLE;
	if (enter) {
		request |= POWER_MODE_LCLK;
		if (rtw_get_lps_deep_mode(rtwdev) == LPS_DEEP_MODE_PG)
			request |= POWER_MODE_PG;
	}
	/* Each request require an ack from firmware */
	request |= POWER_MODE_ACK;

	if (rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_TX_WAKE))
		request |= POWER_TX_WAKE;

	rtw_write8(rtwdev, rtwdev->hci.rpwm_addr, request);

	/* Check firmware get the power requset and ack via cpwm register */
	ret = read_poll_timeout_atomic(rtw_read8, polling,
				       (polling ^ confirm) & BIT_RPWM_TOGGLE,
				       100, 15000, true, rtwdev,
				       rtwdev->hci.cpwm_addr);
	if (ret) {
		/* Hit here means that driver failed to get an ack from firmware.
		 * The reason could be that hardware is locked at Deep sleep,
		 * so most of the hardware circuits are not working, even
		 * register read/write; or firmware is locked in some state and
		 * cannot get the request. It should be treated as fatal error
		 * and requires an entire analysis about the firmware/hardware.
		 */
		WARN(1, "firmware failed to ack driver for %s Deep Power mode\n",
		     enter ? "entering" : "leaving");
	}
}
EXPORT_SYMBOL(rtw_power_mode_change);

static void __rtw_leave_lps_deep(struct rtw_dev *rtwdev)
{
	rtw_hci_deep_ps(rtwdev, false);
}

static int __rtw_fw_leave_lps_check_reg(struct rtw_dev *rtwdev)
{
	int i;

	/* Driver needs to wait for firmware to leave LPS state
	 * successfully. Firmware will send null packet to inform AP,
	 * and see if AP sends an ACK back, then firmware will restore
	 * the REG_TCR register.
	 *
	 * If driver does not wait for firmware, null packet with
	 * PS bit could be sent due to incorrect REG_TCR setting.
	 *
	 * In our test, 100ms should be enough for firmware to finish
	 * the flow. If REG_TCR Register is still incorrect after 100ms,
	 * just modify it directly, and throw a warn message.
	 */
	for (i = 0 ; i < LEAVE_LPS_TRY_CNT; i++) {
		if (rtw_read32_mask(rtwdev, REG_TCR, BIT_PWRMGT_HWDATA_EN) == 0)
			return 0;
		msleep(20);
	}

	return -EBUSY;
}

static  int __rtw_fw_leave_lps_check_c2h(struct rtw_dev *rtwdev)
{
	if (wait_for_completion_timeout(&rtwdev->lps_leave_check,
					LEAVE_LPS_TIMEOUT))
		return 0;
	return -EBUSY;
}

static void rtw_fw_leave_lps_check(struct rtw_dev *rtwdev)
{
	bool ret = false;
	struct rtw_fw_state *fw;

	if (test_bit(RTW_FLAG_WOWLAN, rtwdev->flags))
		fw = &rtwdev->wow_fw;
	else
		fw = &rtwdev->fw;

	if (rtw_fw_feature_check(fw, FW_FEATURE_LPS_C2H))
		ret = __rtw_fw_leave_lps_check_c2h(rtwdev);
	else
		ret = __rtw_fw_leave_lps_check_reg(rtwdev);

	if (ret) {
		rtw_write32_clr(rtwdev, REG_TCR, BIT_PWRMGT_HWDATA_EN);
		rtw_warn(rtwdev, "firmware failed to leave lps state\n");
	}
}

static void rtw_fw_leave_lps_check_prepare(struct rtw_dev *rtwdev)
{
	struct rtw_fw_state *fw;

	if (test_bit(RTW_FLAG_WOWLAN, rtwdev->flags))
		fw = &rtwdev->wow_fw;
	else
		fw = &rtwdev->fw;

	if (rtw_fw_feature_check(fw, FW_FEATURE_LPS_C2H))
		reinit_completion(&rtwdev->lps_leave_check);
}

static void rtw_leave_lps_core(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;

	conf->state = RTW_ALL_ON;
	conf->awake_interval = 1;
	conf->rlbm = 0;
	conf->smart_ps = 0;

	rtw_hci_link_ps(rtwdev, false);
	rtw_fw_leave_lps_check_prepare(rtwdev);
	rtw_fw_set_pwr_mode(rtwdev);
	rtw_fw_leave_lps_check(rtwdev);

	clear_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags);

	rtw_coex_lps_notify(rtwdev, COEX_LPS_DISABLE);
}

enum rtw_lps_deep_mode rtw_get_lps_deep_mode(struct rtw_dev *rtwdev)
{
	if (test_bit(RTW_FLAG_WOWLAN, rtwdev->flags))
		return rtwdev->lps_conf.wow_deep_mode;
	else
		return rtwdev->lps_conf.deep_mode;
}

static void __rtw_enter_lps_deep(struct rtw_dev *rtwdev)
{
	if (rtw_get_lps_deep_mode(rtwdev) == LPS_DEEP_MODE_NONE)
		return;

	if (!test_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags)) {
		rtw_dbg(rtwdev, RTW_DBG_PS,
			"Should enter LPS before entering deep PS\n");
		return;
	}

	if (rtw_get_lps_deep_mode(rtwdev) == LPS_DEEP_MODE_PG)
		rtw_fw_set_pg_info(rtwdev);

	rtw_hci_deep_ps(rtwdev, true);
}

static void rtw_enter_lps_core(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;

	conf->state = RTW_RF_OFF;
	conf->awake_interval = 1;
	conf->rlbm = 1;
	conf->smart_ps = 2;

	rtw_coex_lps_notify(rtwdev, COEX_LPS_ENABLE);

	rtw_fw_set_pwr_mode(rtwdev);
	rtw_hci_link_ps(rtwdev, true);

	set_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags);
}

static void __rtw_enter_lps(struct rtw_dev *rtwdev, u8 port_id)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;

	if (test_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	conf->mode = RTW_MODE_LPS;
	conf->port_id = port_id;

	rtw_enter_lps_core(rtwdev);
}

static void __rtw_leave_lps(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;

	if (test_and_clear_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags)) {
		rtw_dbg(rtwdev, RTW_DBG_PS,
			"Should leave deep PS before leaving LPS\n");
		__rtw_leave_lps_deep(rtwdev);
	}

	if (!test_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	conf->mode = RTW_MODE_ACTIVE;

	rtw_leave_lps_core(rtwdev);
}

void rtw_enter_lps(struct rtw_dev *rtwdev, u8 port_id)
{
	lockdep_assert_held(&rtwdev->mutex);

	if (rtwdev->coex.stat.wl_force_lps_ctrl)
		return;

	__rtw_enter_lps(rtwdev, port_id);
	__rtw_enter_lps_deep(rtwdev);
}

void rtw_leave_lps(struct rtw_dev *rtwdev)
{
	lockdep_assert_held(&rtwdev->mutex);

	__rtw_leave_lps_deep(rtwdev);
	__rtw_leave_lps(rtwdev);
}

void rtw_leave_lps_deep(struct rtw_dev *rtwdev)
{
	lockdep_assert_held(&rtwdev->mutex);

	__rtw_leave_lps_deep(rtwdev);
}

struct rtw_vif_recalc_lps_iter_data {
	struct rtw_dev *rtwdev;
	struct ieee80211_vif *found_vif;
	int count;
};

static void __rtw_vif_recalc_lps(struct rtw_vif_recalc_lps_iter_data *data,
				 struct ieee80211_vif *vif)
{
	if (data->count < 0)
		return;

	if (vif->type != NL80211_IFTYPE_STATION) {
		data->count = -1;
		return;
	}

	data->count++;
	data->found_vif = vif;
}

static void rtw_vif_recalc_lps_iter(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	__rtw_vif_recalc_lps(data, vif);
}

void rtw_recalc_lps(struct rtw_dev *rtwdev, struct ieee80211_vif *new_vif)
{
	struct rtw_vif_recalc_lps_iter_data data = { .rtwdev = rtwdev };

	if (new_vif)
		__rtw_vif_recalc_lps(&data, new_vif);
	rtw_iterate_vifs(rtwdev, rtw_vif_recalc_lps_iter, &data);

	if (data.count == 1 && data.found_vif->cfg.ps) {
		rtwdev->ps_enabled = true;
	} else {
		rtwdev->ps_enabled = false;
		rtw_leave_lps(rtwdev);
	}
}
