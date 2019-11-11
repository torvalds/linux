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

	rtw_set_channel(rtwdev);
	clear_bit(RTW_FLAG_INACTIVE_PS, rtwdev->flags);

	return ret;
}

int rtw_enter_ips(struct rtw_dev *rtwdev)
{
	set_bit(RTW_FLAG_INACTIVE_PS, rtwdev->flags);

	rtw_coex_ips_notify(rtwdev, COEX_IPS_ENTER);

	rtw_core_stop(rtwdev);

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

	ret = rtw_ips_pwr_up(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to leave ips state\n");
		return ret;
	}

	rtw_iterate_vifs_atomic(rtwdev, rtw_restore_port_cfg_iter, rtwdev);

	rtw_coex_ips_notify(rtwdev, COEX_IPS_LEAVE);

	return 0;
}

void rtw_power_mode_change(struct rtw_dev *rtwdev, bool enter)
{
	u8 request, confirm, polling;
	u8 polling_cnt;
	u8 retry_cnt = 0;

	for (retry_cnt = 0; retry_cnt < 3; retry_cnt++) {
		request = rtw_read8(rtwdev, rtwdev->hci.rpwm_addr);
		confirm = rtw_read8(rtwdev, rtwdev->hci.cpwm_addr);

		/* toggle to request power mode, others remain 0 */
		request ^= request | BIT_RPWM_TOGGLE;
		if (!enter) {
			request |= POWER_MODE_ACK;
		} else {
			request |= POWER_MODE_LCLK;
			if (rtw_fw_lps_deep_mode == LPS_DEEP_MODE_PG)
				request |= POWER_MODE_PG;
		}

		rtw_write8(rtwdev, rtwdev->hci.rpwm_addr, request);

		if (enter)
			return;

		/* check confirm power mode has left power save state */
		for (polling_cnt = 0; polling_cnt < 3; polling_cnt++) {
			polling = rtw_read8(rtwdev, rtwdev->hci.cpwm_addr);
			if ((polling ^ confirm) & BIT_RPWM_TOGGLE)
				return;
			mdelay(20);
		}

		/* in case of fw/hw missed the request, retry */
		rtw_warn(rtwdev, "failed to leave deep PS, retry=%d\n",
			 retry_cnt);
	}

	/* Hit here means that driver failed to change hardware power mode to
	 * active state after retry 3 times. If the power state is locked at
	 * Deep sleep, most of the hardware circuits is not working, even
	 * register read/write. It should be treated as fatal error and
	 * requires an entire analysis about the firmware/hardware
	 */
	WARN(1, "Hardware power state locked\n");
}
EXPORT_SYMBOL(rtw_power_mode_change);

static void __rtw_leave_lps_deep(struct rtw_dev *rtwdev)
{
	rtw_hci_deep_ps(rtwdev, false);
}

static void rtw_fw_leave_lps_state_check(struct rtw_dev *rtwdev)
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
			return;
		msleep(20);
	}

	rtw_write32_mask(rtwdev, REG_TCR, BIT_PWRMGT_HWDATA_EN, 0);
	rtw_warn(rtwdev, "firmware failed to restore hardware setting\n");
}

static void rtw_leave_lps_core(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;

	conf->state = RTW_ALL_ON;
	conf->awake_interval = 1;
	conf->rlbm = 0;
	conf->smart_ps = 0;

	rtw_fw_set_pwr_mode(rtwdev);
	rtw_fw_leave_lps_state_check(rtwdev);

	clear_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags);

	rtw_coex_lps_notify(rtwdev, COEX_LPS_DISABLE);
}

static void __rtw_enter_lps_deep(struct rtw_dev *rtwdev)
{
	if (rtwdev->lps_conf.deep_mode == LPS_DEEP_MODE_NONE)
		return;

	if (!test_bit(RTW_FLAG_LEISURE_PS, rtwdev->flags)) {
		rtw_dbg(rtwdev, RTW_DBG_PS,
			"Should enter LPS before entering deep PS\n");
		return;
	}

	if (rtw_fw_lps_deep_mode == LPS_DEEP_MODE_PG)
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
