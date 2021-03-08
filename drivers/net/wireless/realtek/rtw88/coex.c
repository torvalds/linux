// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "ps.h"
#include "debug.h"
#include "reg.h"
#include "phy.h"

static u8 rtw_coex_next_rssi_state(struct rtw_dev *rtwdev, u8 pre_state,
				   u8 rssi, u8 rssi_thresh)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 tol = chip->rssi_tolerance;
	u8 next_state;

	if (pre_state == COEX_RSSI_STATE_LOW ||
	    pre_state == COEX_RSSI_STATE_STAY_LOW) {
		if (rssi >= (rssi_thresh + tol))
			next_state = COEX_RSSI_STATE_HIGH;
		else
			next_state = COEX_RSSI_STATE_STAY_LOW;
	} else {
		if (rssi < rssi_thresh)
			next_state = COEX_RSSI_STATE_LOW;
		else
			next_state = COEX_RSSI_STATE_STAY_HIGH;
	}

	return next_state;
}

static void rtw_coex_limited_tx(struct rtw_dev *rtwdev,
				bool tx_limit_en, bool ampdu_limit_en)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 num_of_active_port = 1;

	if (!chip->scbd_support)
		return;

	/* force max tx retry limit = 8 */
	if (coex_stat->wl_tx_limit_en == tx_limit_en &&
	    coex_stat->wl_ampdu_limit_en == ampdu_limit_en)
		return;

	if (!coex_stat->wl_tx_limit_en) {
		coex_stat->darfrc = rtw_read32(rtwdev, REG_DARFRC);
		coex_stat->darfrch = rtw_read32(rtwdev, REG_DARFRCH);
		coex_stat->retry_limit = rtw_read16(rtwdev, REG_RETRY_LIMIT);
	}

	if (!coex_stat->wl_ampdu_limit_en)
		coex_stat->ampdu_max_time =
				rtw_read8(rtwdev, REG_AMPDU_MAX_TIME_V1);

	coex_stat->wl_tx_limit_en = tx_limit_en;
	coex_stat->wl_ampdu_limit_en = ampdu_limit_en;

	if (tx_limit_en) {
		/* set BT polluted packet on for tx rate adaptive,
		 * not including tx retry broken by PTA
		 */
		rtw_write8_set(rtwdev, REG_TX_HANG_CTRL, BIT_EN_GNT_BT_AWAKE);

		/* set queue life time to avoid can't reach tx retry limit
		 * if tx is always broken by GNT_BT
		 */
		if (num_of_active_port <= 1)
			rtw_write8_set(rtwdev, REG_LIFETIME_EN, 0xf);
		rtw_write16(rtwdev, REG_RETRY_LIMIT, 0x0808);

		/* auto rate fallback step within 8 retries */
		rtw_write32(rtwdev, REG_DARFRC, 0x1000000);
		rtw_write32(rtwdev, REG_DARFRCH, 0x4030201);
	} else {
		rtw_write8_clr(rtwdev, REG_TX_HANG_CTRL, BIT_EN_GNT_BT_AWAKE);
		rtw_write8_clr(rtwdev, REG_LIFETIME_EN, 0xf);

		rtw_write16(rtwdev, REG_RETRY_LIMIT, coex_stat->retry_limit);
		rtw_write32(rtwdev, REG_DARFRC, coex_stat->darfrc);
		rtw_write32(rtwdev, REG_DARFRCH, coex_stat->darfrch);
	}

	if (ampdu_limit_en)
		rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1, 0x20);
	else
		rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1,
			   coex_stat->ampdu_max_time);
}

static void rtw_coex_limited_wl(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	bool tx_limit = false;
	bool tx_agg_ctrl = false;

	if (!coex->under_5g && coex_dm->bt_status != COEX_BTSTATUS_NCON_IDLE) {
		tx_limit = true;
		tx_agg_ctrl = true;
	}

	rtw_coex_limited_tx(rtwdev, tx_limit, tx_agg_ctrl);
}

static bool rtw_coex_freerun_check(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 bt_rssi;
	u8 ant_distance = 10;

	if (coex_stat->bt_disabled)
		return false;

	if (efuse->share_ant || ant_distance <= 5 || !coex_stat->wl_gl_busy)
		return false;

	if (ant_distance >= 40 || coex_stat->bt_hid_pair_num >= 2)
		return true;

	/* ant_distance = 5 ~ 40  */
	if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[1]) &&
	    COEX_RSSI_HIGH(coex_dm->bt_rssi_state[0]))
		return true;

	if (coex_stat->wl_tput_dir == COEX_WL_TPUT_TX)
		bt_rssi = coex_dm->bt_rssi_state[0];
	else
		bt_rssi = coex_dm->bt_rssi_state[1];

	if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[3]) &&
	    COEX_RSSI_HIGH(bt_rssi) &&
	    coex_stat->cnt_wl[COEX_CNT_WL_SCANAP] <= 5)
		return true;

	return false;
}

static void rtw_coex_wl_slot_extend(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 para[6] = {0};

	para[0] = COEX_H2C69_WL_LEAKAP;
	para[1] = PARA1_H2C69_DIS_5MS;

	if (enable)
		para[1] = PARA1_H2C69_EN_5MS;
	else
		coex_stat->cnt_wl[COEX_CNT_WL_5MS_NOEXTEND] = 0;

	coex_stat->wl_slot_extend = enable;
	rtw_fw_bt_wifi_control(rtwdev, para[0], &para[1]);
}

static void rtw_coex_wl_ccklock_action(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;


	if (coex_stat->tdma_timer_base == 3 && coex_stat->wl_slot_extend) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], set h2c 0x69 opcode 12 to turn off 5ms WL slot extend!!\n");
		rtw_coex_wl_slot_extend(rtwdev, false);
		return;
	}

	if (coex_stat->wl_slot_extend && coex_stat->wl_force_lps_ctrl &&
	    !coex_stat->wl_cck_lock_ever) {
		if (coex_stat->wl_fw_dbg_info[7] <= 5)
			coex_stat->cnt_wl[COEX_CNT_WL_5MS_NOEXTEND]++;
		else
			coex_stat->cnt_wl[COEX_CNT_WL_5MS_NOEXTEND] = 0;

		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], 5ms WL slot extend cnt = %d!!\n",
			coex_stat->cnt_wl[COEX_CNT_WL_5MS_NOEXTEND]);

		if (coex_stat->cnt_wl[COEX_CNT_WL_5MS_NOEXTEND] == 7) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], set h2c 0x69 opcode 12 to turn off 5ms WL slot extend!!\n");
			rtw_coex_wl_slot_extend(rtwdev, false);
		}
	} else if (!coex_stat->wl_slot_extend && coex_stat->wl_cck_lock) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], set h2c 0x69 opcode 12 to turn on 5ms WL slot extend!!\n");

		rtw_coex_wl_slot_extend(rtwdev, true);
	}
}

static void rtw_coex_wl_ccklock_detect(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	bool is_cck_lock_rate = false;

	if (coex_dm->bt_status == COEX_BTSTATUS_INQ_PAGE ||
	    coex_stat->bt_setup_link) {
		coex_stat->wl_cck_lock = false;
		coex_stat->wl_cck_lock_pre = false;
		return;
	}

	if (coex_stat->wl_rx_rate <= COEX_CCK_2 ||
	    coex_stat->wl_rts_rx_rate <= COEX_CCK_2)
		is_cck_lock_rate = true;

	if (coex_stat->wl_connected && coex_stat->wl_gl_busy &&
	    COEX_RSSI_HIGH(coex_dm->wl_rssi_state[3]) &&
	    (coex_dm->bt_status == COEX_BTSTATUS_ACL_BUSY ||
	     coex_dm->bt_status == COEX_BTSTATUS_ACL_SCO_BUSY ||
	     coex_dm->bt_status == COEX_BTSTATUS_SCO_BUSY)) {
		if (is_cck_lock_rate) {
			coex_stat->wl_cck_lock = true;

			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], cck locking...\n");

		} else {
			coex_stat->wl_cck_lock = false;

			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], cck unlock...\n");
		}
	} else {
		coex_stat->wl_cck_lock = false;
	}

	/* CCK lock identification */
	if (coex_stat->wl_cck_lock && !coex_stat->wl_cck_lock_pre)
		ieee80211_queue_delayed_work(rtwdev->hw, &coex->wl_ccklock_work,
					     3 * HZ);

	coex_stat->wl_cck_lock_pre = coex_stat->wl_cck_lock;
}

static void rtw_coex_wl_noisy_detect(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cnt_cck;
	bool wl_cck_lock = false;

	/* wifi noisy environment identification */
	cnt_cck = dm_info->cck_ok_cnt + dm_info->cck_err_cnt;

	if (!coex_stat->wl_gl_busy && !wl_cck_lock) {
		if (cnt_cck > 250) {
			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY2] < 5)
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY2]++;

			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY2] == 5) {
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY0] = 0;
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY1] = 0;
			}
		} else if (cnt_cck < 100) {
			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY0] < 5)
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY0]++;

			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY0] == 5) {
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY1] = 0;
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY2] = 0;
			}
		} else {
			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY1] < 5)
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY1]++;

			if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY1] == 5) {
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY0] = 0;
				coex_stat->cnt_wl[COEX_CNT_WL_NOISY2] = 0;
			}
		}

		if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY2] == 5)
			coex_stat->wl_noisy_level = 2;
		else if (coex_stat->cnt_wl[COEX_CNT_WL_NOISY1] == 5)
			coex_stat->wl_noisy_level = 1;
		else
			coex_stat->wl_noisy_level = 0;

		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], wl_noisy_level = %d\n",
			coex_stat->wl_noisy_level);
	}
}

static void rtw_coex_tdma_timer_base(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 para[2] = {0};
	u8 times;
	u16 tbtt_interval = coex_stat->wl_beacon_interval;

	if (coex_stat->tdma_timer_base == type)
		return;

	coex_stat->tdma_timer_base = type;

	para[0] = COEX_H2C69_TDMA_SLOT;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], tbtt_interval = %d\n",
		tbtt_interval);

	if (type == TDMA_TIMER_TYPE_4SLOT && tbtt_interval < 120) {
		para[1] = PARA1_H2C69_TDMA_4SLOT; /* 4-slot */
	} else if (tbtt_interval < 80 && tbtt_interval > 0) {
		times = 100 / tbtt_interval;
		if (100 % tbtt_interval != 0)
			times++;

		para[1] = FIELD_PREP(PARA1_H2C69_TBTT_TIMES, times);
	} else if (tbtt_interval >= 180) {
		times = tbtt_interval / 100;
		if (tbtt_interval % 100 <= 80)
			times--;

		para[1] = FIELD_PREP(PARA1_H2C69_TBTT_TIMES, times) |
			  FIELD_PREP(PARA1_H2C69_TBTT_DIV100, 1);
	} else {
		para[1] = PARA1_H2C69_TDMA_2SLOT;
	}

	rtw_fw_bt_wifi_control(rtwdev, para[0], &para[1]);

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): h2c_0x69 = 0x%x\n",
		__func__, para[1]);

	/* no 5ms_wl_slot_extend for 4-slot mode  */
	if (coex_stat->tdma_timer_base == 3)
		rtw_coex_wl_ccklock_action(rtwdev);
}

static void rtw_coex_set_wl_pri_mask(struct rtw_dev *rtwdev, u8 bitmap,
				     u8 data)
{
	u32 addr;

	addr = REG_BT_COEX_TABLE_H + (bitmap / 8);
	bitmap = bitmap % 8;

	rtw_write8_mask(rtwdev, addr, BIT(bitmap), data);
}

void rtw_coex_write_scbd(struct rtw_dev *rtwdev, u16 bitpos, bool set)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u16 val = 0x2;

	if (!chip->scbd_support)
		return;

	val |= coex_stat->score_board;

	/* for 8822b, scbd[10] is CQDDR on
	 * for 8822c, scbd[10] is no fix 2M
	 */
	if (!chip->new_scbd10_def && (bitpos & COEX_SCBD_FIX2M)) {
		if (set)
			val &= ~COEX_SCBD_FIX2M;
		else
			val |= COEX_SCBD_FIX2M;
	} else {
		if (set)
			val |= bitpos;
		else
			val &= ~bitpos;
	}

	if (val != coex_stat->score_board) {
		coex_stat->score_board = val;
		val |= BIT_BT_INT_EN;
		rtw_write16(rtwdev, REG_WIFI_BT_INFO, val);
	}
}
EXPORT_SYMBOL(rtw_coex_write_scbd);

static u16 rtw_coex_read_scbd(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (!chip->scbd_support)
		return 0;

	return (rtw_read16(rtwdev, REG_WIFI_BT_INFO)) & ~(BIT_BT_INT_EN);
}

static void rtw_coex_check_rfk(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	u8 cnt = 0;
	u32 wait_cnt;
	bool btk, wlk;

	if (coex_rfe->wlg_at_btg && chip->scbd_support &&
	    coex_stat->bt_iqk_state != 0xff) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], (Before Ant Setup) Delay by IQK\n");

		wait_cnt = COEX_RFK_TIMEOUT / COEX_MIN_DELAY;
		do {
			/* BT RFK */
			btk = !!(rtw_coex_read_scbd(rtwdev) & COEX_SCBD_BT_RFK);

			/* WL RFK */
			wlk = !!(rtw_read8(rtwdev, REG_ARFR4) & BIT_WL_RFK);

			if (!btk && !wlk)
				break;

			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], (Before Ant Setup) wlk = %d, btk = %d\n",
				wlk, btk);

			mdelay(COEX_MIN_DELAY);
		} while (++cnt < wait_cnt);

		if (cnt >= wait_cnt)
			coex_stat->bt_iqk_state = 0xff;
	}
}

static void rtw_coex_query_bt_info(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex_stat->bt_disabled)
		return;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_fw_query_bt_info(rtwdev);
}

static void rtw_coex_gnt_workaround(struct rtw_dev *rtwdev, bool force, u8 mode)
{
	rtw_coex_set_gnt_fix(rtwdev);
}

static void rtw_coex_monitor_bt_enable(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	bool bt_disabled = false;
	u16 score_board;

	if (chip->scbd_support) {
		score_board = rtw_coex_read_scbd(rtwdev);
		bt_disabled = !(score_board & COEX_SCBD_ONOFF);
	}

	if (coex_stat->bt_disabled != bt_disabled) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], BT state changed (%d) -> (%d)\n",
			coex_stat->bt_disabled, bt_disabled);

		coex_stat->bt_disabled = bt_disabled;
		coex_stat->bt_ble_scan_type = 0;
		coex_dm->cur_bt_lna_lvl = 0;

		if (!coex_stat->bt_disabled) {
			coex_stat->bt_reenable = true;
			ieee80211_queue_delayed_work(rtwdev->hw,
						     &coex->bt_reenable_work,
						     15 * HZ);
		} else {
			coex_stat->bt_mailbox_reply = false;
			coex_stat->bt_reenable = false;
		}
	}
}

static void rtw_coex_update_wl_link_info(struct rtw_dev *rtwdev, u8 reason)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_traffic_stats *stats = &rtwdev->stats;
	bool is_5G = false;
	bool wl_busy = false;
	bool scan = false, link = false;
	int i;
	u8 rssi_state;
	u8 rssi_step;
	u8 rssi;

	scan = test_bit(RTW_FLAG_SCANNING, rtwdev->flags);
	coex_stat->wl_connected = !!rtwdev->sta_cnt;

	wl_busy = test_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);
	if (wl_busy != coex_stat->wl_gl_busy) {
		if (wl_busy)
			coex_stat->wl_gl_busy = true;
		else
			ieee80211_queue_delayed_work(rtwdev->hw,
						     &coex->wl_remain_work,
						     12 * HZ);
	}

	if (stats->tx_throughput > stats->rx_throughput)
		coex_stat->wl_tput_dir = COEX_WL_TPUT_TX;
	else
		coex_stat->wl_tput_dir = COEX_WL_TPUT_RX;

	if (scan || link || reason == COEX_RSN_2GCONSTART ||
	    reason == COEX_RSN_2GSCANSTART || reason == COEX_RSN_2GSWITCHBAND)
		coex_stat->wl_linkscan_proc = true;
	else
		coex_stat->wl_linkscan_proc = false;

	rtw_coex_wl_noisy_detect(rtwdev);

	for (i = 0; i < 4; i++) {
		rssi_state = coex_dm->wl_rssi_state[i];
		rssi_step = chip->wl_rssi_step[i];
		rssi = rtwdev->dm_info.min_rssi;
		rssi_state = rtw_coex_next_rssi_state(rtwdev, rssi_state,
						      rssi, rssi_step);
		coex_dm->wl_rssi_state[i] = rssi_state;
	}

	if (coex_stat->wl_linkscan_proc || coex_stat->wl_hi_pri_task1 ||
	    coex_stat->wl_hi_pri_task2 || coex_stat->wl_gl_busy)
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_SCAN, true);
	else
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_SCAN, false);

	switch (reason) {
	case COEX_RSN_5GSCANSTART:
	case COEX_RSN_5GSWITCHBAND:
	case COEX_RSN_5GCONSTART:

		is_5G = true;
		break;
	case COEX_RSN_2GSCANSTART:
	case COEX_RSN_2GSWITCHBAND:
	case COEX_RSN_2GCONSTART:

		is_5G = false;
		break;
	default:
		if (rtwdev->hal.current_band_type == RTW_BAND_5G)
			is_5G = true;
		else
			is_5G = false;
		break;
	}

	coex->under_5g = is_5G;
}

static inline u8 *get_payload_from_coex_resp(struct sk_buff *resp)
{
	struct rtw_c2h_cmd *c2h;
	u32 pkt_offset;

	pkt_offset = *((u32 *)resp->cb);
	c2h = (struct rtw_c2h_cmd *)(resp->data + pkt_offset);

	return c2h->payload;
}

void rtw_coex_info_response(struct rtw_dev *rtwdev, struct sk_buff *skb)
{
	struct rtw_coex *coex = &rtwdev->coex;
	u8 *payload = get_payload_from_coex_resp(skb);

	if (payload[0] != COEX_RESP_ACK_BY_WL_FW)
		return;

	skb_queue_tail(&coex->queue, skb);
	wake_up(&coex->wait);
}

static struct sk_buff *rtw_coex_info_request(struct rtw_dev *rtwdev,
					     struct rtw_coex_info_req *req)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct sk_buff *skb_resp = NULL;

	mutex_lock(&coex->mutex);

	rtw_fw_query_bt_mp_info(rtwdev, req);

	if (!wait_event_timeout(coex->wait, !skb_queue_empty(&coex->queue),
				COEX_REQUEST_TIMEOUT)) {
		rtw_err(rtwdev, "coex request time out\n");
		goto out;
	}

	skb_resp = skb_dequeue(&coex->queue);
	if (!skb_resp) {
		rtw_err(rtwdev, "failed to get coex info response\n");
		goto out;
	}

out:
	mutex_unlock(&coex->mutex);
	return skb_resp;
}

static bool rtw_coex_get_bt_scan_type(struct rtw_dev *rtwdev, u8 *scan_type)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	u8 *payload;
	bool ret = false;

	req.op_code = BT_MP_INFO_OP_SCAN_TYPE;
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb)
		goto out;

	payload = get_payload_from_coex_resp(skb);
	*scan_type = GET_COEX_RESP_BT_SCAN_TYPE(payload);
	dev_kfree_skb_any(skb);
	ret = true;

out:
	return ret;
}

static bool rtw_coex_set_lna_constrain_level(struct rtw_dev *rtwdev,
					     u8 lna_constrain_level)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	bool ret = false;

	req.op_code = BT_MP_INFO_OP_LNA_CONSTRAINT;
	req.para1 = lna_constrain_level;
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb)
		goto out;

	dev_kfree_skb_any(skb);
	ret = true;

out:
	return ret;
}

#define case_BTSTATUS(src) \
	case COEX_BTSTATUS_##src: return #src

static const char *rtw_coex_get_bt_status_string(u8 bt_status)
{
	switch (bt_status) {
	case_BTSTATUS(NCON_IDLE);
	case_BTSTATUS(CON_IDLE);
	case_BTSTATUS(INQ_PAGE);
	case_BTSTATUS(ACL_BUSY);
	case_BTSTATUS(SCO_BUSY);
	case_BTSTATUS(ACL_SCO_BUSY);
	default:
		return "Unknown";
	}
}

static void rtw_coex_update_bt_link_info(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 i;
	u8 rssi_state;
	u8 rssi_step;
	u8 rssi;

	/* update wl/bt rssi by btinfo */
	for (i = 0; i < COEX_RSSI_STEP; i++) {
		rssi_state = coex_dm->bt_rssi_state[i];
		rssi_step = chip->bt_rssi_step[i];
		rssi = coex_stat->bt_rssi;
		rssi_state = rtw_coex_next_rssi_state(rtwdev, rssi_state, rssi,
						      rssi_step);
		coex_dm->bt_rssi_state[i] = rssi_state;
	}

	if (coex_stat->bt_ble_scan_en &&
	    coex_stat->cnt_bt[COEX_CNT_BT_INFOUPDATE] % 3 == 0) {
		u8 scan_type;

		if (rtw_coex_get_bt_scan_type(rtwdev, &scan_type)) {
			coex_stat->bt_ble_scan_type = scan_type;
			if ((coex_stat->bt_ble_scan_type & 0x1) == 0x1)
				coex_stat->bt_init_scan = true;
			else
				coex_stat->bt_init_scan = false;
		}
	}

	coex_stat->bt_profile_num = 0;

	/* set link exist status */
	if (!(coex_stat->bt_info_lb2 & COEX_INFO_CONNECTION)) {
		coex_stat->bt_link_exist = false;
		coex_stat->bt_pan_exist = false;
		coex_stat->bt_a2dp_exist = false;
		coex_stat->bt_hid_exist = false;
		coex_stat->bt_hfp_exist = false;
	} else {
		/* connection exists */
		coex_stat->bt_link_exist = true;
		if (coex_stat->bt_info_lb2 & COEX_INFO_FTP) {
			coex_stat->bt_pan_exist = true;
			coex_stat->bt_profile_num++;
		} else {
			coex_stat->bt_pan_exist = false;
		}

		if (coex_stat->bt_info_lb2 & COEX_INFO_A2DP) {
			coex_stat->bt_a2dp_exist = true;
			coex_stat->bt_profile_num++;
		} else {
			coex_stat->bt_a2dp_exist = false;
		}

		if (coex_stat->bt_info_lb2 & COEX_INFO_HID) {
			coex_stat->bt_hid_exist = true;
			coex_stat->bt_profile_num++;
		} else {
			coex_stat->bt_hid_exist = false;
		}

		if (coex_stat->bt_info_lb2 & COEX_INFO_SCO_ESCO) {
			coex_stat->bt_hfp_exist = true;
			coex_stat->bt_profile_num++;
		} else {
			coex_stat->bt_hfp_exist = false;
		}
	}

	if (coex_stat->bt_info_lb2 & COEX_INFO_INQ_PAGE) {
		coex_dm->bt_status = COEX_BTSTATUS_INQ_PAGE;
	} else if (!(coex_stat->bt_info_lb2 & COEX_INFO_CONNECTION)) {
		coex_dm->bt_status = COEX_BTSTATUS_NCON_IDLE;
		coex_stat->bt_multi_link_remain = false;
	} else if (coex_stat->bt_info_lb2 == COEX_INFO_CONNECTION) {
		coex_dm->bt_status = COEX_BTSTATUS_CON_IDLE;
	} else if ((coex_stat->bt_info_lb2 & COEX_INFO_SCO_ESCO) ||
		   (coex_stat->bt_info_lb2 & COEX_INFO_SCO_BUSY)) {
		if (coex_stat->bt_info_lb2 & COEX_INFO_ACL_BUSY)
			coex_dm->bt_status = COEX_BTSTATUS_ACL_SCO_BUSY;
		else
			coex_dm->bt_status = COEX_BTSTATUS_SCO_BUSY;
	} else if (coex_stat->bt_info_lb2 & COEX_INFO_ACL_BUSY) {
		coex_dm->bt_status = COEX_BTSTATUS_ACL_BUSY;
	} else {
		coex_dm->bt_status = COEX_BTSTATUS_MAX;
	}

	coex_stat->cnt_bt[COEX_CNT_BT_INFOUPDATE]++;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(), %s!!!\n", __func__,
		rtw_coex_get_bt_status_string(coex_dm->bt_status));
}

static void rtw_coex_update_wl_ch_info(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex_dm *coex_dm = &rtwdev->coex.dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 link = 0;
	u8 center_chan = 0;
	u8 bw;
	int i;

	bw = rtwdev->hal.current_band_width;

	if (type != COEX_MEDIA_DISCONNECT)
		center_chan = rtwdev->hal.current_channel;

	if (center_chan == 0 || (efuse->share_ant && center_chan <= 14)) {
		link = 0;
		center_chan = 0;
		bw = 0;
	} else if (center_chan <= 14) {
		link = 0x1;

		if (bw == RTW_CHANNEL_WIDTH_40)
			bw = chip->bt_afh_span_bw40;
		else
			bw = chip->bt_afh_span_bw20;
	} else if (chip->afh_5g_num > 1) {
		for (i = 0; i < chip->afh_5g_num; i++) {
			if (center_chan == chip->afh_5g[i].wl_5g_ch) {
				link = 0x3;
				center_chan = chip->afh_5g[i].bt_skip_ch;
				bw = chip->afh_5g[i].bt_skip_span;
				break;
			}
		}
	}

	coex_dm->wl_ch_info[0] = link;
	coex_dm->wl_ch_info[1] = center_chan;
	coex_dm->wl_ch_info[2] = bw;

	rtw_fw_wl_ch_info(rtwdev, link, center_chan, bw);
	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], %s: para[0:2] = 0x%x 0x%x 0x%x\n", __func__, link,
		center_chan, bw);
}

static void rtw_coex_set_bt_tx_power(struct rtw_dev *rtwdev, u8 bt_pwr_dec_lvl)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	if (bt_pwr_dec_lvl == coex_dm->cur_bt_pwr_lvl)
		return;

	coex_dm->cur_bt_pwr_lvl = bt_pwr_dec_lvl;

	rtw_fw_force_bt_tx_power(rtwdev, bt_pwr_dec_lvl);
}

static void rtw_coex_set_bt_rx_gain(struct rtw_dev *rtwdev, u8 bt_lna_lvl)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	if (bt_lna_lvl == coex_dm->cur_bt_lna_lvl)
		return;

	coex_dm->cur_bt_lna_lvl = bt_lna_lvl;

	/* notify BT rx gain table changed */
	if (bt_lna_lvl < 7) {
		rtw_coex_set_lna_constrain_level(rtwdev, bt_lna_lvl);
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_RXGAIN, true);
	} else {
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_RXGAIN, false);
	}
	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): bt_rx_LNA_level = %d\n",
		__func__, bt_lna_lvl);
}

static void rtw_coex_set_rf_para(struct rtw_dev *rtwdev,
				 struct coex_rf_para para)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 offset = 0;

	if (coex->freerun && coex_stat->cnt_wl[COEX_CNT_WL_SCANAP] <= 5)
		offset = 3;

	rtw_coex_set_wl_tx_power(rtwdev, para.wl_pwr_dec_lvl);
	rtw_coex_set_bt_tx_power(rtwdev, para.bt_pwr_dec_lvl + offset);
	rtw_coex_set_wl_rx_gain(rtwdev, para.wl_low_gain_en);
	rtw_coex_set_bt_rx_gain(rtwdev, para.bt_lna_lvl);
}

u32 rtw_coex_read_indirect_reg(struct rtw_dev *rtwdev, u16 addr)
{
	u32 val;

	if (!ltecoex_read_reg(rtwdev, addr, &val)) {
		rtw_err(rtwdev, "failed to read indirect register\n");
		return 0;
	}

	return val;
}
EXPORT_SYMBOL(rtw_coex_read_indirect_reg);

void rtw_coex_write_indirect_reg(struct rtw_dev *rtwdev, u16 addr,
				 u32 mask, u32 val)
{
	u32 shift = __ffs(mask);
	u32 tmp;

	tmp = rtw_coex_read_indirect_reg(rtwdev, addr);
	tmp = (tmp & (~mask)) | ((val << shift) & mask);

	if (!ltecoex_reg_write(rtwdev, addr, tmp))
		rtw_err(rtwdev, "failed to write indirect register\n");
}
EXPORT_SYMBOL(rtw_coex_write_indirect_reg);

static void rtw_coex_coex_ctrl_owner(struct rtw_dev *rtwdev, bool wifi_control)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_hw_reg *btg_reg = chip->btg_reg;

	if (wifi_control) {
		rtw_write8_set(rtwdev, REG_SYS_SDIO_CTRL + 3,
			       BIT_LTE_MUX_CTRL_PATH >> 24);
		if (btg_reg)
			rtw_write8_set(rtwdev, btg_reg->addr, btg_reg->mask);
	} else {
		rtw_write8_clr(rtwdev, REG_SYS_SDIO_CTRL + 3,
			       BIT_LTE_MUX_CTRL_PATH >> 24);
		if (btg_reg)
			rtw_write8_clr(rtwdev, btg_reg->addr, btg_reg->mask);
	}
}

static void rtw_coex_set_gnt_bt(struct rtw_dev *rtwdev, u8 state)
{
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0xc000, state);
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x0c00, state);
}

static void rtw_coex_set_gnt_wl(struct rtw_dev *rtwdev, u8 state)
{
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x3000, state);
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x0300, state);
}

static void rtw_btc_wltoggle_table_a(struct rtw_dev *rtwdev, bool force,
				     u8 table_case)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 h2c_para[6] = {0};
	u32 table_wl = 0x5a5a5a5a;

	h2c_para[0] = COEX_H2C69_TOGGLE_TABLE_A;
	/* no definition */
	h2c_para[1] = 0x1;

	if (efuse->share_ant) {
		if (table_case < chip->table_sant_num)
			table_wl = chip->table_sant[table_case].wl;
	} else {
		if (table_case < chip->table_nsant_num)
			table_wl = chip->table_nsant[table_case].wl;
	}

	/* tell WL FW WL slot toggle table-A*/
	h2c_para[2] = (u8)u32_get_bits(table_wl, GENMASK(7, 0));
	h2c_para[3] = (u8)u32_get_bits(table_wl, GENMASK(15, 8));
	h2c_para[4] = (u8)u32_get_bits(table_wl, GENMASK(23, 16));
	h2c_para[5] = (u8)u32_get_bits(table_wl, GENMASK(31, 24));

	rtw_fw_bt_wifi_control(rtwdev, h2c_para[0], &h2c_para[1]);

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], %s(): H2C = [%02x %02x %02x %02x %02x %02x]\n",
		__func__, h2c_para[0], h2c_para[1], h2c_para[2],
		h2c_para[3], h2c_para[4], h2c_para[5]);
}

#define COEX_WL_SLOT_TOGLLE 0x5a5a5aaa
static void rtw_btc_wltoggle_table_b(struct rtw_dev *rtwdev, bool force,
				     u8 interval, u32 table)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 cur_h2c_para[6] = {0};
	u8 i;

	cur_h2c_para[0] = COEX_H2C69_TOGGLE_TABLE_B;
	cur_h2c_para[1] = interval;
	cur_h2c_para[2] = (u8)u32_get_bits(table, GENMASK(7, 0));
	cur_h2c_para[3] = (u8)u32_get_bits(table, GENMASK(15, 8));
	cur_h2c_para[4] = (u8)u32_get_bits(table, GENMASK(23, 16));
	cur_h2c_para[5] = (u8)u32_get_bits(table, GENMASK(31, 24));

	coex_stat->wl_toggle_interval = interval;

	for (i = 0; i <= 5; i++)
		coex_stat->wl_toggle_para[i] = cur_h2c_para[i];

	rtw_fw_bt_wifi_control(rtwdev, cur_h2c_para[0], &cur_h2c_para[1]);

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], %s(): H2C = [%02x %02x %02x %02x %02x %02x]\n",
		__func__, cur_h2c_para[0], cur_h2c_para[1], cur_h2c_para[2],
		cur_h2c_para[3], cur_h2c_para[4], cur_h2c_para[5]);
}

static void rtw_coex_set_table(struct rtw_dev *rtwdev, bool force, u32 table0,
			       u32 table1)
{
#define DEF_BRK_TABLE_VAL 0xf0ffffff
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	/* If last tdma is wl slot toggle, force write table*/
	if (!force && coex_dm->reason != COEX_RSN_LPS) {
		if (table0 == rtw_read32(rtwdev, REG_BT_COEX_TABLE0) &&
		    table1 == rtw_read32(rtwdev, REG_BT_COEX_TABLE1))
			return;
	}
	rtw_write32(rtwdev, REG_BT_COEX_TABLE0, table0);
	rtw_write32(rtwdev, REG_BT_COEX_TABLE1, table1);
	rtw_write32(rtwdev, REG_BT_COEX_BRK_TABLE, DEF_BRK_TABLE_VAL);

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], %s(): 0x6c0 = %x, 0x6c4 = %x\n", __func__, table0,
		table1);
}

static void rtw_coex_table(struct rtw_dev *rtwdev, bool force, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	coex_dm->cur_table = type;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], Coex_Table - %d\n", type);

	if (efuse->share_ant) {
		if (type < chip->table_sant_num)
			rtw_coex_set_table(rtwdev, force,
					   chip->table_sant[type].bt,
					   chip->table_sant[type].wl);
	} else {
		type = type - 100;
		if (type < chip->table_nsant_num)
			rtw_coex_set_table(rtwdev, force,
					   chip->table_nsant[type].bt,
					   chip->table_nsant[type].wl);
	}
	if (coex_stat->wl_slot_toggle_change)
		rtw_btc_wltoggle_table_a(rtwdev, true, type);
}

static void rtw_coex_ignore_wlan_act(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_coex *coex = &rtwdev->coex;

	if (coex->manual_control || coex->stop_dm)
		return;

	rtw_fw_bt_ignore_wlan_action(rtwdev, enable);
}

static void rtw_coex_power_save_state(struct rtw_dev *rtwdev, u8 ps_type,
				      u8 lps_val, u8 rpwm_val)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 lps_mode = 0x0;

	lps_mode = rtwdev->lps_conf.mode;

	switch (ps_type) {
	case COEX_PS_WIFI_NATIVE:
		/* recover to original 32k low power setting */
		coex_stat->wl_force_lps_ctrl = false;
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s(): COEX_PS_WIFI_NATIVE\n", __func__);
		rtw_leave_lps(rtwdev);
		break;
	case COEX_PS_LPS_OFF:
		coex_stat->wl_force_lps_ctrl = true;
		if (lps_mode)
			rtw_fw_coex_tdma_type(rtwdev, 0, 0, 0, 0, 0);

		rtw_leave_lps(rtwdev);
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s(): COEX_PS_LPS_OFF\n", __func__);
		break;
	default:
		break;
	}
}

static void rtw_coex_set_tdma(struct rtw_dev *rtwdev, u8 byte1, u8 byte2,
			      u8 byte3, u8 byte4, u8 byte5)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 ps_type = COEX_PS_WIFI_NATIVE;
	bool ap_enable = false;

	if (ap_enable && (byte1 & BIT(4) && !(byte1 & BIT(5)))) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): AP mode\n",
			__func__);

		byte1 &= ~BIT(4);
		byte1 |= BIT(5);

		byte5 |= BIT(5);
		byte5 &= ~BIT(6);

		ps_type = COEX_PS_WIFI_NATIVE;
		rtw_coex_power_save_state(rtwdev, ps_type, 0x0, 0x0);
	} else if (byte1 & BIT(4) && !(byte1 & BIT(5))) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s(): Force LPS (byte1 = 0x%x)\n", __func__,
			byte1);

		if (chip->pstdma_type == COEX_PSTDMA_FORCE_LPSOFF)
			ps_type = COEX_PS_LPS_OFF;
		else
			ps_type = COEX_PS_LPS_ON;
		rtw_coex_power_save_state(rtwdev, ps_type, 0x50, 0x4);
	} else {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s(): native power save (byte1 = 0x%x)\n",
			__func__, byte1);

		ps_type = COEX_PS_WIFI_NATIVE;
		rtw_coex_power_save_state(rtwdev, ps_type, 0x0, 0x0);
	}

	coex_dm->ps_tdma_para[0] = byte1;
	coex_dm->ps_tdma_para[1] = byte2;
	coex_dm->ps_tdma_para[2] = byte3;
	coex_dm->ps_tdma_para[3] = byte4;
	coex_dm->ps_tdma_para[4] = byte5;

	rtw_fw_coex_tdma_type(rtwdev, byte1, byte2, byte3, byte4, byte5);

	if (byte1 & BIT(2)) {
		coex_stat->wl_slot_toggle = true;
		coex_stat->wl_slot_toggle_change = false;
	} else {
		coex_stat->wl_slot_toggle_change = coex_stat->wl_slot_toggle;
		coex_stat->wl_slot_toggle = false;
	}
}

static void rtw_coex_tdma(struct rtw_dev *rtwdev, bool force, u32 tcase)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 n, type;
	bool turn_on;
	bool wl_busy = false;

	if (tcase & TDMA_4SLOT) /* 4-slot (50ms) mode */
		rtw_coex_tdma_timer_base(rtwdev, TDMA_TIMER_TYPE_4SLOT);
	else
		rtw_coex_tdma_timer_base(rtwdev, TDMA_TIMER_TYPE_2SLOT);

	type = (u8)(tcase & 0xff);

	turn_on = (type == 0 || type == 100) ? false : true;

	if (!force && turn_on == coex_dm->cur_ps_tdma_on &&
	    type == coex_dm->cur_ps_tdma) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], Skip TDMA because no change TDMA(%s, %d)\n",
			(coex_dm->cur_ps_tdma_on ? "on" : "off"),
			coex_dm->cur_ps_tdma);

		return;
	}
	wl_busy = test_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);

	if ((coex_stat->bt_a2dp_exist &&
	     (coex_stat->bt_inq_remain || coex_stat->bt_multi_link)) ||
	    !wl_busy)
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_TDMA, false);
	else
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_TDMA, true);

	/* update pre state */
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (efuse->share_ant) {
		if (type < chip->tdma_sant_num)
			rtw_coex_set_tdma(rtwdev,
					  chip->tdma_sant[type].para[0],
					  chip->tdma_sant[type].para[1],
					  chip->tdma_sant[type].para[2],
					  chip->tdma_sant[type].para[3],
					  chip->tdma_sant[type].para[4]);
	} else {
		n = type - 100;
		if (n < chip->tdma_nsant_num)
			rtw_coex_set_tdma(rtwdev,
					  chip->tdma_nsant[n].para[0],
					  chip->tdma_nsant[n].para[1],
					  chip->tdma_nsant[n].para[2],
					  chip->tdma_nsant[n].para[3],
					  chip->tdma_nsant[n].para[4]);
	}


	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], coex tdma type(%s, %d)\n",
		turn_on ? "on" : "off", type);
}

static void rtw_coex_set_ant_path(struct rtw_dev *rtwdev, bool force, u8 phase)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	u8 ctrl_type = COEX_SWITCH_CTRL_MAX;
	u8 pos_type = COEX_SWITCH_TO_MAX;

	if (!force && coex_dm->cur_ant_pos_type == phase)
		return;

	coex_dm->cur_ant_pos_type = phase;

	/* avoid switch coex_ctrl_owner during BT IQK */
	rtw_coex_check_rfk(rtwdev);

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex],  coex_stat->bt_disabled = 0x%x\n",
		coex_stat->bt_disabled);

	switch (phase) {
	case COEX_SET_ANT_POWERON:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_COEX_POWERON\n", __func__);
		/* set path control owner to BT at power-on */
		if (coex_stat->bt_disabled)
			rtw_coex_coex_ctrl_owner(rtwdev, true);
		else
			rtw_coex_coex_ctrl_owner(rtwdev, false);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_BT;
		break;
	case COEX_SET_ANT_INIT:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_COEX_INIT\n", __func__);
		if (coex_stat->bt_disabled) {
			/* set GNT_BT to SW low */
			rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_SW_LOW);

			/* set GNT_WL to SW high */
			rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_SW_HIGH);
		} else {
			/* set GNT_BT to SW high */
			rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_SW_HIGH);

			/* set GNT_WL to SW low */
			rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_SW_LOW);
		}

		/* set path control owner to wl at initial step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_BT;
		break;
	case COEX_SET_ANT_WONLY:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_WLANONLY_INIT\n", __func__);
		/* set GNT_BT to SW Low */
		rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_SW_LOW);

		/* set GNT_WL to SW high */
		rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_SW_HIGH);

		/* set path control owner to wl at initial step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_WLG;
		break;
	case COEX_SET_ANT_WOFF:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_WLAN_OFF\n", __func__);
		/* set path control owner to BT */
		rtw_coex_coex_ctrl_owner(rtwdev, false);

		ctrl_type = COEX_SWITCH_CTRL_BY_BT;
		pos_type = COEX_SWITCH_TO_NOCARE;
		break;
	case COEX_SET_ANT_2G:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_2G_RUNTIME\n", __func__);
		/* set GNT_BT to PTA */
		rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_HW_PTA);

		/* set GNT_WL to PTA */
		rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_HW_PTA);

		/* set path control owner to wl at runtime step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_PTA;
		pos_type = COEX_SWITCH_TO_NOCARE;
		break;
	case COEX_SET_ANT_5G:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_5G_RUNTIME\n", __func__);

		/* set GNT_BT to HW PTA */
		rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_HW_PTA);

		/* set GNT_WL to SW high */
		rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_SW_HIGH);

		/* set path control owner to wl at runtime step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_WLA;
		break;
	case COEX_SET_ANT_2G_FREERUN:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_2G_FREERUN\n", __func__);

		/* set GNT_BT to HW PTA */
		rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_HW_PTA);

		/* Set GNT_WL to SW high */
		rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_SW_HIGH);

		/* set path control owner to wl at runtime step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_WLG_BT;
		break;
	case COEX_SET_ANT_2G_WLBT:
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s() - PHASE_2G_WLBT\n", __func__);
		/* set GNT_BT to HW PTA */
		rtw_coex_set_gnt_bt(rtwdev, COEX_GNT_SET_HW_PTA);

		/* Set GNT_WL to HW PTA */
		rtw_coex_set_gnt_wl(rtwdev, COEX_GNT_SET_HW_PTA);

		/* set path control owner to wl at runtime step */
		rtw_coex_coex_ctrl_owner(rtwdev, true);

		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;
		pos_type = COEX_SWITCH_TO_WLG_BT;
		break;
	default:
		WARN(1, "unknown phase when setting antenna path\n");
		return;
	}

	if (ctrl_type < COEX_SWITCH_CTRL_MAX && pos_type < COEX_SWITCH_TO_MAX &&
	    coex_rfe->ant_switch_exist)
		rtw_coex_set_ant_switch(rtwdev, ctrl_type, pos_type);
}

#define case_ALGO(src) \
	case COEX_ALGO_##src: return #src

static const char *rtw_coex_get_algo_string(u8 algo)
{
	switch (algo) {
	case_ALGO(NOPROFILE);
	case_ALGO(HFP);
	case_ALGO(HID);
	case_ALGO(A2DP);
	case_ALGO(PAN);
	case_ALGO(A2DP_HID);
	case_ALGO(A2DP_PAN);
	case_ALGO(PAN_HID);
	case_ALGO(A2DP_PAN_HID);
	default:
		return "Unknown";
	}
}

#define case_BT_PROFILE(src) \
	case BPM_##src: return #src

static const char *rtw_coex_get_bt_profile_string(u8 bt_profile)
{
	switch (bt_profile) {
	case_BT_PROFILE(NOPROFILE);
	case_BT_PROFILE(HFP);
	case_BT_PROFILE(HID);
	case_BT_PROFILE(A2DP);
	case_BT_PROFILE(PAN);
	case_BT_PROFILE(HID_HFP);
	case_BT_PROFILE(A2DP_HFP);
	case_BT_PROFILE(A2DP_HID);
	case_BT_PROFILE(A2DP_HID_HFP);
	case_BT_PROFILE(PAN_HFP);
	case_BT_PROFILE(PAN_HID);
	case_BT_PROFILE(PAN_HID_HFP);
	case_BT_PROFILE(PAN_A2DP);
	case_BT_PROFILE(PAN_A2DP_HFP);
	case_BT_PROFILE(PAN_A2DP_HID);
	case_BT_PROFILE(PAN_A2DP_HID_HFP);
	default:
		return "Unknown";
	}
}

static u8 rtw_coex_algorithm(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 algorithm = COEX_ALGO_NOPROFILE;
	u8 profile_map = 0;

	if (coex_stat->bt_hfp_exist)
		profile_map |= BPM_HFP;
	if (coex_stat->bt_hid_exist)
		profile_map |= BPM_HID;
	if (coex_stat->bt_a2dp_exist)
		profile_map |= BPM_A2DP;
	if (coex_stat->bt_pan_exist)
		profile_map |= BPM_PAN;

	switch (profile_map) {
	case BPM_HFP:
		algorithm = COEX_ALGO_HFP;
		break;
	case           BPM_HID:
	case BPM_HFP + BPM_HID:
		algorithm = COEX_ALGO_HID;
		break;
	case BPM_HFP           + BPM_A2DP:
	case           BPM_HID + BPM_A2DP:
	case BPM_HFP + BPM_HID + BPM_A2DP:
		algorithm = COEX_ALGO_A2DP_HID;
		break;
	case BPM_HFP                      + BPM_PAN:
	case           BPM_HID            + BPM_PAN:
	case BPM_HFP + BPM_HID            + BPM_PAN:
		algorithm = COEX_ALGO_PAN_HID;
		break;
	case BPM_HFP           + BPM_A2DP + BPM_PAN:
	case           BPM_HID + BPM_A2DP + BPM_PAN:
	case BPM_HFP + BPM_HID + BPM_A2DP + BPM_PAN:
		algorithm = COEX_ALGO_A2DP_PAN_HID;
		break;
	case                                BPM_PAN:
		algorithm = COEX_ALGO_PAN;
		break;
	case                     BPM_A2DP + BPM_PAN:
		algorithm = COEX_ALGO_A2DP_PAN;
		break;
	case                     BPM_A2DP:
		if (coex_stat->bt_multi_link) {
			if (coex_stat->bt_hid_pair_num > 0)
				algorithm = COEX_ALGO_A2DP_HID;
			else
				algorithm = COEX_ALGO_A2DP_PAN;
		} else {
			algorithm = COEX_ALGO_A2DP;
		}
		break;
	default:
		algorithm = COEX_ALGO_NOPROFILE;
		break;
	}

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], BT Profile = %s => Algorithm = %s\n",
		rtw_coex_get_bt_profile_string(profile_map),
		rtw_coex_get_algo_string(algorithm));
	return algorithm;
}

static void rtw_coex_action_coex_all_off(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 2;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_freerun(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 level = 0;
	bool bt_afh_loss = true;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	if (efuse->share_ant)
		return;

	coex->freerun = true;

	if (bt_afh_loss)
		rtw_coex_update_wl_ch_info(rtwdev, COEX_MEDIA_CONNECT);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G_FREERUN);

	rtw_coex_write_scbd(rtwdev, COEX_SCBD_FIX2M, false);

	if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[0]))
		level = 2;
	else if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[1]))
		level = 3;
	else if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[2]))
		level = 4;
	else
		level = 5;

	if (level > chip->wl_rf_para_num - 1)
		level = chip->wl_rf_para_num - 1;

	if (coex_stat->wl_tput_dir == COEX_WL_TPUT_TX)
		rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_tx[level]);
	else
		rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[level]);

	rtw_coex_table(rtwdev, false, 100);
	rtw_coex_tdma(rtwdev, false, 100);
}

static void rtw_coex_action_rf4ce(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 9;
		tdma_case = 16;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_whql_test(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 2;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_relink(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	u32 slot_type = 0;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) { /* Shared-Ant */
		if (coex_stat->wl_gl_busy) {
			table_case = 26;
			if (coex_stat->bt_hid_exist &&
			    coex_stat->bt_profile_num == 1) {
				slot_type = TDMA_4SLOT;
				tdma_case = 20;
			} else {
				tdma_case = 20;
			}
		} else {
			table_case = 1;
			tdma_case = 0;
		}
	} else { /* Non-Shared-Ant */
		if (coex_stat->wl_gl_busy)
			table_case = 115;
		else
			table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_bt_idle(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	u8 table_case = 0xff, tdma_case = 0xff;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (coex_rfe->ant_switch_with_bt &&
	    coex_dm->bt_status == COEX_BTSTATUS_NCON_IDLE) {
		if (efuse->share_ant &&
		    COEX_RSSI_HIGH(coex_dm->wl_rssi_state[3]) &&
		    coex_stat->wl_gl_busy) {
			table_case = 0;
			tdma_case = 0;
		} else if (!efuse->share_ant) {
			table_case = 100;
			tdma_case = 100;
		}
	}

	if (table_case != 0xff && tdma_case != 0xff) {
		rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G_FREERUN);
		goto exit;
	}

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (!coex_stat->wl_gl_busy) {
			table_case = 10;
			tdma_case = 3;
		} else if (coex_dm->bt_status == COEX_BTSTATUS_NCON_IDLE) {
			table_case = 11;

			if (coex_stat->lo_pri_rx + coex_stat->lo_pri_tx > 250)
				tdma_case = 17;
			else
				tdma_case = 7;
		} else {
			table_case = 12;
			tdma_case = 7;
		}
	} else {
		/* Non-Shared-Ant */
		if (!coex_stat->wl_gl_busy) {
			table_case = 112;
			tdma_case = 104;
		} else if ((coex_stat->bt_ble_scan_type & 0x2) &&
			   coex_dm->bt_status == COEX_BTSTATUS_NCON_IDLE) {
			table_case = 114;
			tdma_case = 103;
		} else {
			table_case = 112;
			tdma_case = 103;
		}
	}

exit:
	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_inquiry(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	bool wl_hi_pri = false;
	u8 table_case, tdma_case;
	u32 slot_type = 0;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (coex_stat->wl_linkscan_proc || coex_stat->wl_hi_pri_task1 ||
	    coex_stat->wl_hi_pri_task2)
		wl_hi_pri = true;

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (wl_hi_pri) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page +  wifi hi-pri task\n");
			table_case = 15;

			if (coex_stat->bt_profile_num > 0)
				tdma_case = 10;
			else if (coex_stat->wl_hi_pri_task1)
				tdma_case = 6;
			else if (!coex_stat->bt_page)
				tdma_case = 8;
			else
				tdma_case = 9;
		} else if (coex_stat->wl_gl_busy) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page + wifi busy\n");
			if (coex_stat->bt_profile_num == 0) {
				table_case = 12;
				tdma_case = 18;
			} else if (coex_stat->bt_profile_num == 1 &&
				   !coex_stat->bt_a2dp_exist) {
				slot_type = TDMA_4SLOT;
				table_case = 12;
				tdma_case = 20;
			} else {
				slot_type = TDMA_4SLOT;
				table_case = 12;
				tdma_case = 26;
			}
		} else if (coex_stat->wl_connected) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page + wifi connected\n");
			table_case = 9;
			tdma_case = 27;
		} else {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page + wifi not-connected\n");
			table_case = 1;
			tdma_case = 0;
		}
	} else {
		/* Non_Shared-Ant */
		if (wl_hi_pri) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page +  wifi hi-pri task\n");
			table_case = 114;

			if (coex_stat->bt_profile_num > 0)
				tdma_case = 110;
			else if (coex_stat->wl_hi_pri_task1)
				tdma_case = 106;
			else if (!coex_stat->bt_page)
				tdma_case = 108;
			else
				tdma_case = 109;
		}  else if (coex_stat->wl_gl_busy) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page + wifi busy\n");
			table_case = 114;
			tdma_case = 121;
		} else if (coex_stat->wl_connected) {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page +  wifi connected\n");
			table_case = 101;
			tdma_case = 100;
		} else {
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], bt inq/page +  wifi not-connected\n");
			table_case = 101;
			tdma_case = 100;
		}
	}

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], wifi hi(%d), bt page(%d)\n",
		wl_hi_pri, coex_stat->bt_page);

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_bt_hfp(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (coex_stat->bt_multi_link) {
			table_case = 10;
			tdma_case = 17;
		} else {
			table_case = 10;
			tdma_case = 5;
		}
	} else {
		/* Non-Shared-Ant */
		if (coex_stat->bt_multi_link) {
			table_case = 112;
			tdma_case = 117;
		} else {
			table_case = 105;
			tdma_case = 100;
		}
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_hid(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	u32 slot_type = 0;
	bool bt_multi_link_remain = false, is_toggle_table = false;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (coex_stat->bt_ble_exist) {
			/* RCU */
			if (coex_stat->cnt_wl[COEX_CNT_WL_SCANAP] > 5) {
				table_case = 26;
				tdma_case = 2;
			} else {
				table_case = 27;
				tdma_case = 9;
			}
		} else {
			/* Legacy HID  */
			if (coex_stat->bt_profile_num == 1 &&
			    (coex_stat->bt_multi_link ||
			    (coex_stat->lo_pri_rx +
			     coex_stat->lo_pri_tx > 360) ||
			     coex_stat->bt_slave ||
			     bt_multi_link_remain)) {
				slot_type = TDMA_4SLOT;
				table_case = 12;
				tdma_case = 20;
			} else if (coex_stat->bt_a2dp_active) {
				table_case = 9;
				tdma_case = 18;
			} else if (coex_stat->bt_418_hid_exist &&
				   coex_stat->wl_gl_busy) {
				is_toggle_table = true;
				slot_type = TDMA_4SLOT;
				table_case = 9;
				tdma_case = 24;
			} else if (coex_stat->bt_ble_hid_exist &&
				   coex_stat->wl_gl_busy) {
				table_case = 32;
				tdma_case = 9;
			} else {
				table_case = 9;
				tdma_case = 9;
			}
		}
	} else {
		/* Non-Shared-Ant */
		if (coex_stat->bt_ble_exist) {
			/* BLE */
			if (coex_stat->cnt_wl[COEX_CNT_WL_SCANAP] > 5) {
				table_case = 121;
				tdma_case = 102;
			} else {
				table_case = 122;
				tdma_case = 109;
			}
		} else if (coex_stat->bt_a2dp_active) {
			table_case = 113;
			tdma_case = 118;
		} else {
			table_case = 113;
			tdma_case = 104;
		}
	}

	rtw_coex_table(rtwdev, false, table_case);
	if (is_toggle_table) {
		rtw_btc_wltoggle_table_a(rtwdev, true, table_case);
		rtw_btc_wltoggle_table_b(rtwdev, false, 1, COEX_WL_SLOT_TOGLLE);
	}

	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_bt_a2dp(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	u32 slot_type = 0;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	slot_type = TDMA_4SLOT;

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (coex_stat->wl_gl_busy && coex_stat->wl_noisy_level == 0)
			table_case = 12;
		else
			table_case = 9;

		if (coex_stat->wl_connecting || !coex_stat->wl_gl_busy)
			tdma_case = 14;
		else
			tdma_case = 13;
	} else {
		/* Non-Shared-Ant */
		table_case = 112;

		if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[1]))
			tdma_case = 112;
		else
			tdma_case = 113;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_bt_a2dpsink(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	bool ap_enable = false;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) { /* Shared-Ant */
		if (ap_enable) {
			table_case = 2;
			tdma_case = 0;
		} else if (coex_stat->wl_gl_busy) {
			table_case = 28;
			tdma_case = 20;
		} else {
			table_case = 28;
			tdma_case = 26;
		}
	} else { /* Non-Shared-Ant */
		if (ap_enable) {
			table_case = 100;
			tdma_case = 100;
		} else {
			table_case = 119;
			tdma_case = 120;
		}
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_pan(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (coex_stat->wl_gl_busy && coex_stat->wl_noisy_level == 0)
			table_case = 14;
		else
			table_case = 10;

		if (coex_stat->wl_gl_busy)
			tdma_case = 17;
		else
			tdma_case = 20;
	} else {
		/* Non-Shared-Ant */
		table_case = 112;

		if (coex_stat->wl_gl_busy)
			tdma_case = 117;
		else
			tdma_case = 119;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_a2dp_hid(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case, interval = 0;
	u32 slot_type = 0;
	bool is_toggle_table = false;

	slot_type = TDMA_4SLOT;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		if (coex_stat->bt_ble_exist) {
			table_case = 26; /* for RCU */
		} else if (coex_stat->bt_418_hid_exist) {
			table_case = 9;
			interval = 1;
		} else {
			table_case = 9;
		}

		if (coex_stat->wl_connecting || !coex_stat->wl_gl_busy) {
			tdma_case = 14;
		} else if (coex_stat->bt_418_hid_exist) {
			is_toggle_table = true;
			tdma_case = 23;
		} else {
			tdma_case = 13;
		}
	} else {
		/* Non-Shared-Ant */
		if (coex_stat->bt_ble_exist)
			table_case = 121;
		else
			table_case = 113;

		if (COEX_RSSI_HIGH(coex_dm->wl_rssi_state[1]))
			tdma_case = 112;
		else
			tdma_case = 113;
	}

	rtw_coex_table(rtwdev, false, table_case);
	if (is_toggle_table) {
		rtw_btc_wltoggle_table_a(rtwdev, true, table_case);
		rtw_btc_wltoggle_table_b(rtwdev, false, interval, COEX_WL_SLOT_TOGLLE);
	}
	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_bt_a2dp_pan(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	bool wl_cpt_test = false, bt_cpt_test = false;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);
	if (efuse->share_ant) {
		/* Shared-Ant */
		if (wl_cpt_test) {
			if (coex_stat->wl_gl_busy) {
				table_case = 20;
				tdma_case = 17;
			} else {
				table_case = 10;
				tdma_case = 15;
			}
		} else if (bt_cpt_test) {
			table_case = 26;
			tdma_case = 26;
		} else {
			if (coex_stat->wl_gl_busy &&
			    coex_stat->wl_noisy_level == 0)
				table_case = 14;
			else
				table_case = 10;

			if (coex_stat->wl_gl_busy)
				tdma_case = 15;
			else
				tdma_case = 20;
		}
	} else {
		/* Non-Shared-Ant */
		table_case = 112;

		if (coex_stat->wl_gl_busy)
			tdma_case = 115;
		else
			tdma_case = 120;
	}

	if (wl_cpt_test)
		rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[1]);
	else
		rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_pan_hid(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 9;

		if (coex_stat->wl_gl_busy)
			tdma_case = 18;
		else
			tdma_case = 19;
	} else {
		/* Non-Shared-Ant */
		table_case = 113;

		if (coex_stat->wl_gl_busy)
			tdma_case = 117;
		else
			tdma_case = 119;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_bt_a2dp_pan_hid(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 10;

		if (coex_stat->wl_gl_busy)
			tdma_case = 15;
		else
			tdma_case = 20;
	} else {
		/* Non-Shared-Ant */
		table_case = 113;

		if (coex_stat->wl_gl_busy)
			tdma_case = 115;
		else
			tdma_case = 120;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_wl_under5g(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_5G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	rtw_coex_write_scbd(rtwdev, COEX_SCBD_FIX2M, false);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 0;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_wl_only(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 2;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_wl_native_lps(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	if (coex->under_5g)
		return;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 28;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_wl_linkscan(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;
	u32 slot_type = 0;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) { /* Shared-Ant */
		if (coex_stat->bt_a2dp_exist) {
			slot_type = TDMA_4SLOT;
			table_case = 9;
			tdma_case = 11;
		} else {
			table_case = 9;
			tdma_case = 7;
		}
	} else { /* Non-Shared-Ant */
		if (coex_stat->bt_a2dp_exist) {
			slot_type = TDMA_4SLOT;
			table_case = 112;
			tdma_case = 111;
		} else {
			table_case = 112;
			tdma_case = 107;
		}
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case | slot_type);
}

static void rtw_coex_action_wl_not_connected(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 table_case, tdma_case;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	rtw_coex_set_ant_path(rtwdev, false, COEX_SET_ANT_2G);
	rtw_coex_set_rf_para(rtwdev, chip->wl_rf_para_rx[0]);

	if (efuse->share_ant) {
		/* Shared-Ant */
		table_case = 1;
		tdma_case = 0;
	} else {
		/* Non-Shared-Ant */
		table_case = 100;
		tdma_case = 100;
	}

	rtw_coex_table(rtwdev, false, table_case);
	rtw_coex_tdma(rtwdev, false, tdma_case);
}

static void rtw_coex_action_wl_connected(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 algorithm;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	algorithm = rtw_coex_algorithm(rtwdev);

	switch (algorithm) {
	case COEX_ALGO_HFP:
		rtw_coex_action_bt_hfp(rtwdev);
		break;
	case COEX_ALGO_HID:
		if (rtw_coex_freerun_check(rtwdev))
			rtw_coex_action_freerun(rtwdev);
		else
			rtw_coex_action_bt_hid(rtwdev);
		break;
	case COEX_ALGO_A2DP:
		if (rtw_coex_freerun_check(rtwdev))
			rtw_coex_action_freerun(rtwdev);
		else if (coex_stat->bt_a2dp_sink)
			rtw_coex_action_bt_a2dpsink(rtwdev);
		else
			rtw_coex_action_bt_a2dp(rtwdev);
		break;
	case COEX_ALGO_PAN:
		rtw_coex_action_bt_pan(rtwdev);
		break;
	case COEX_ALGO_A2DP_HID:
		if (rtw_coex_freerun_check(rtwdev))
			rtw_coex_action_freerun(rtwdev);
		else
			rtw_coex_action_bt_a2dp_hid(rtwdev);
		break;
	case COEX_ALGO_A2DP_PAN:
		rtw_coex_action_bt_a2dp_pan(rtwdev);
		break;
	case COEX_ALGO_PAN_HID:
		rtw_coex_action_bt_pan_hid(rtwdev);
		break;
	case COEX_ALGO_A2DP_PAN_HID:
		rtw_coex_action_bt_a2dp_pan_hid(rtwdev);
		break;
	default:
	case COEX_ALGO_NOPROFILE:
		rtw_coex_action_bt_idle(rtwdev);
		break;
	}
}

static void rtw_coex_run_coex(struct rtw_dev *rtwdev, u8 reason)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	bool rf4ce_en = false;

	lockdep_assert_held(&rtwdev->mutex);

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags))
		return;

	coex_dm->reason = reason;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): reason = %d\n", __func__,
		reason);

	/* update wifi_link_info_ext variable */
	rtw_coex_update_wl_link_info(rtwdev, reason);

	rtw_coex_monitor_bt_enable(rtwdev);

	if (coex->manual_control) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], return for Manual CTRL!!\n");
		return;
	}

	if (coex->stop_dm) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], return for Stop Coex DM!!\n");
		return;
	}

	if (coex_stat->wl_under_ips) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], return for wifi is under IPS!!\n");
		return;
	}

	if (coex->freeze && coex_dm->reason == COEX_RSN_BTINFO &&
	    !coex_stat->bt_setup_link) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], return for coex_freeze!!\n");
		return;
	}

	coex_stat->cnt_wl[COEX_CNT_WL_COEXRUN]++;
	coex->freerun = false;

	/* Pure-5G Coex Process */
	if (coex->under_5g) {
		coex_stat->wl_coex_mode = COEX_WLINK_5G;
		rtw_coex_action_wl_under5g(rtwdev);
		goto exit;
	}

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], WiFi is single-port 2G!!\n");
	coex_stat->wl_coex_mode = COEX_WLINK_2G1PORT;

	if (coex_stat->bt_disabled) {
		if (coex_stat->wl_connected && rf4ce_en)
			rtw_coex_action_rf4ce(rtwdev);
		else if (!coex_stat->wl_connected)
			rtw_coex_action_wl_not_connected(rtwdev);
		else
			rtw_coex_action_wl_only(rtwdev);
		goto exit;
	}

	if (coex_stat->wl_under_lps && !coex_stat->wl_force_lps_ctrl) {
		rtw_coex_action_wl_native_lps(rtwdev);
		goto exit;
	}

	if (coex_stat->bt_whck_test) {
		rtw_coex_action_bt_whql_test(rtwdev);
		goto exit;
	}

	if (coex_stat->bt_setup_link) {
		rtw_coex_action_bt_relink(rtwdev);
		goto exit;
	}

	if (coex_stat->bt_inq_page) {
		rtw_coex_action_bt_inquiry(rtwdev);
		goto exit;
	}

	if ((coex_dm->bt_status == COEX_BTSTATUS_NCON_IDLE ||
	     coex_dm->bt_status == COEX_BTSTATUS_CON_IDLE) &&
	     coex_stat->wl_connected) {
		rtw_coex_action_bt_idle(rtwdev);
		goto exit;
	}

	if (coex_stat->wl_linkscan_proc && !coex->freerun) {
		rtw_coex_action_wl_linkscan(rtwdev);
		goto exit;
	}

	if (coex_stat->wl_connected) {
		rtw_coex_action_wl_connected(rtwdev);
		goto exit;
	} else {
		rtw_coex_action_wl_not_connected(rtwdev);
		goto exit;
	}

exit:
	rtw_coex_gnt_workaround(rtwdev, false, coex_stat->wl_coex_mode);
	rtw_coex_limited_wl(rtwdev);
}

static void rtw_coex_init_coex_var(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	u8 i;

	memset(coex_dm, 0, sizeof(*coex_dm));
	memset(coex_stat, 0, sizeof(*coex_stat));

	for (i = 0; i < COEX_CNT_WL_MAX; i++)
		coex_stat->cnt_wl[i] = 0;

	for (i = 0; i < COEX_CNT_BT_MAX; i++)
		coex_stat->cnt_bt[i] = 0;

	for (i = 0; i < ARRAY_SIZE(coex_dm->bt_rssi_state); i++)
		coex_dm->bt_rssi_state[i] = COEX_RSSI_STATE_LOW;

	for (i = 0; i < ARRAY_SIZE(coex_dm->wl_rssi_state); i++)
		coex_dm->wl_rssi_state[i] = COEX_RSSI_STATE_LOW;

	coex_stat->wl_coex_mode = COEX_WLINK_MAX;
	coex_stat->wl_rx_rate = DESC_RATE5_5M;
	coex_stat->wl_rts_rx_rate = DESC_RATE5_5M;
}

static void __rtw_coex_init_hw_config(struct rtw_dev *rtwdev, bool wifi_only)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	rtw_coex_init_coex_var(rtwdev);

	coex_stat->kt_ver = u8_get_bits(rtw_read8(rtwdev, 0xf1), GENMASK(7, 4));

	rtw_coex_monitor_bt_enable(rtwdev);
	rtw_coex_wl_slot_extend(rtwdev, coex_stat->wl_slot_extend);

	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	rtw_coex_set_rfe_type(rtwdev);
	rtw_coex_set_init(rtwdev);

	/* set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS) */
	rtw_coex_set_wl_pri_mask(rtwdev, COEX_WLPRI_TX_RSP, 1);

	/* set Tx beacon = Hi-Pri */
	rtw_coex_set_wl_pri_mask(rtwdev, COEX_WLPRI_TX_BEACON, 1);

	/* set Tx beacon queue = Hi-Pri */
	rtw_coex_set_wl_pri_mask(rtwdev, COEX_WLPRI_TX_BEACONQ, 1);

	/* antenna config */
	if (coex->wl_rf_off) {
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_WOFF);
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ALL, false);
		coex->stop_dm = true;

		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): RF Off\n",
			__func__);
	} else if (wifi_only) {
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_WONLY);
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF,
				    true);
		coex->stop_dm = true;
	} else {
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_INIT);
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF,
				    true);
		coex->stop_dm = false;
		coex->freeze = true;
	}

	/* PTA parameter */
	rtw_coex_table(rtwdev, true, 1);
	rtw_coex_tdma(rtwdev, true, 0);
	rtw_coex_query_bt_info(rtwdev);
}

void rtw_coex_power_on_setting(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	u8 table_case = 1;

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);

	coex->stop_dm = true;
	coex->wl_rf_off = false;

	/* enable BB, we can write 0x948 */
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN,
		       BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);

	rtw_coex_monitor_bt_enable(rtwdev);
	rtw_coex_set_rfe_type(rtwdev);

	/* set antenna path to BT */
	rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_POWERON);

	rtw_coex_table(rtwdev, true, table_case);
	/* red x issue */
	rtw_write8(rtwdev, 0xff1a, 0x0);
	rtw_coex_set_gnt_debug(rtwdev);
}

void rtw_coex_init_hw_config(struct rtw_dev *rtwdev, bool wifi_only)
{
	__rtw_coex_init_hw_config(rtwdev, wifi_only);
}

void rtw_coex_ips_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;

	if (type == COEX_IPS_ENTER) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], IPS ENTER notify\n");

		coex_stat->wl_under_ips = true;

		/* for lps off */
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ALL, false);

		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_WOFF);
		rtw_coex_action_coex_all_off(rtwdev);
	} else if (type == COEX_IPS_LEAVE) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], IPS LEAVE notify\n");

		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF, true);
		/* run init hw config (exclude wifi only) */
		__rtw_coex_init_hw_config(rtwdev, false);

		coex_stat->wl_under_ips = false;
	}
}

void rtw_coex_lps_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;

	if (type == COEX_LPS_ENABLE) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], LPS ENABLE notify\n");

		coex_stat->wl_under_lps = true;

		if (coex_stat->wl_force_lps_ctrl) {
			/* for ps-tdma */
			rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE, true);
		} else {
			/* for native ps */
			rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE, false);
			rtw_coex_write_scbd(rtwdev, COEX_SCBD_WLBUSY, false);

			rtw_coex_run_coex(rtwdev, COEX_RSN_LPS);
		}
	} else if (type == COEX_LPS_DISABLE) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], LPS DISABLE notify\n");

		coex_stat->wl_under_lps = false;

		/* for lps off */
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE, true);

		if (!coex_stat->wl_force_lps_ctrl)
			rtw_coex_query_bt_info(rtwdev);

		rtw_coex_run_coex(rtwdev, COEX_RSN_LPS);
	}
}

void rtw_coex_scan_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;

	coex->freeze = false;
	rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF, true);

	if (type == COEX_SCAN_START_5G) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], SCAN START notify (5G)\n");

		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_5G);
		rtw_coex_run_coex(rtwdev, COEX_RSN_5GSCANSTART);
	} else if ((type == COEX_SCAN_START_2G) || (type == COEX_SCAN_START)) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], SCAN START notify (2G)\n");

		coex_stat->wl_hi_pri_task2 = true;

		/* Force antenna setup for no scan result issue */
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_2G);
		rtw_coex_run_coex(rtwdev, COEX_RSN_2GSCANSTART);
	} else {
		coex_stat->cnt_wl[COEX_CNT_WL_SCANAP] = 30; /* To do */

		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], SCAN FINISH notify (Scan-AP = %d)\n",
			coex_stat->cnt_wl[COEX_CNT_WL_SCANAP]);

		coex_stat->wl_hi_pri_task2 = false;
		rtw_coex_run_coex(rtwdev, COEX_RSN_SCANFINISH);
	}
}

void rtw_coex_switchband_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;

	if (coex->manual_control || coex->stop_dm)
		return;

	if (type == COEX_SWITCH_TO_5G) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): TO_5G\n",
			__func__);
	} else if (type == COEX_SWITCH_TO_24G_NOFORSCAN) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], %s(): TO_24G_NOFORSCAN\n", __func__);
	} else {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): TO_2G\n",
			__func__);
	}

	if (type == COEX_SWITCH_TO_5G)
		rtw_coex_run_coex(rtwdev, COEX_RSN_5GSWITCHBAND);
	else if (type == COEX_SWITCH_TO_24G_NOFORSCAN)
		rtw_coex_run_coex(rtwdev, COEX_RSN_2GSWITCHBAND);
	else
		rtw_coex_scan_notify(rtwdev, COEX_SCAN_START_2G);
}

void rtw_coex_connect_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;

	rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF, true);

	if (type == COEX_ASSOCIATE_5G_START) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 5G start\n",
			__func__);

		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_5G);
		rtw_coex_run_coex(rtwdev, COEX_RSN_5GCONSTART);
	} else if (type == COEX_ASSOCIATE_5G_FINISH) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 5G finish\n",
			__func__);

		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_5G);
		rtw_coex_run_coex(rtwdev, COEX_RSN_5GCONFINISH);
	} else if (type == COEX_ASSOCIATE_START) {
		coex_stat->wl_hi_pri_task1 = true;
		coex_stat->wl_connecting = true;
		coex_stat->cnt_wl[COEX_CNT_WL_CONNPKT] = 2;
		coex_stat->wl_connecting = true;
		ieee80211_queue_delayed_work(rtwdev->hw,
					     &coex->wl_connecting_work, 2 * HZ);

		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 2G start\n",
			__func__);
		/* Force antenna setup for no scan result issue */
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_2G);

		rtw_coex_run_coex(rtwdev, COEX_RSN_2GCONSTART);

		/* To keep TDMA case during connect process,
		 * to avoid changed by Btinfo and runcoexmechanism
		 */
		coex->freeze = true;
		ieee80211_queue_delayed_work(rtwdev->hw, &coex->defreeze_work,
					     5 * HZ);
	} else {
		coex_stat->wl_hi_pri_task1 = false;
		coex->freeze = false;
		coex_stat->wl_connecting = false;

		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 2G finish\n",
			__func__);
		rtw_coex_run_coex(rtwdev, COEX_RSN_2GCONFINISH);
	}
}

void rtw_coex_media_status_notify(struct rtw_dev *rtwdev, u8 type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;

	if (coex->manual_control || coex->stop_dm)
		return;

	if (type == COEX_MEDIA_CONNECT_5G) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 5G\n", __func__);

		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE, true);

		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_5G);
		rtw_coex_run_coex(rtwdev, COEX_RSN_5GMEDIA);
	} else if (type == COEX_MEDIA_CONNECT) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): 2G\n", __func__);

		coex_stat->wl_connecting = false;

		rtw_coex_write_scbd(rtwdev, COEX_SCBD_ACTIVE, true);

		/* Force antenna setup for no scan result issue */
		rtw_coex_set_ant_path(rtwdev, true, COEX_SET_ANT_2G);

		/* Set CCK Rx high Pri */
		rtw_coex_set_wl_pri_mask(rtwdev, COEX_WLPRI_RX_CCK, 1);
		rtw_coex_run_coex(rtwdev, COEX_RSN_2GMEDIA);
	} else {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s(): disconnect!!\n",
			__func__);
		rtw_coex_set_wl_pri_mask(rtwdev, COEX_WLPRI_RX_CCK, 0);
		rtw_coex_run_coex(rtwdev, COEX_RSN_MEDIADISCON);
	}

	rtw_coex_update_wl_ch_info(rtwdev, type);
}

void rtw_coex_bt_info_notify(struct rtw_dev *rtwdev, u8 *buf, u8 length)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	u32 bt_relink_time;
	u8 i, rsp_source = 0, type;
	bool inq_page = false;

	rsp_source = buf[0] & 0xf;
	if (rsp_source >= COEX_BTINFO_SRC_MAX)
		return;
	coex_stat->cnt_bt_info_c2h[rsp_source]++;

	if (rsp_source == COEX_BTINFO_SRC_BT_IQK) {
		coex_stat->bt_iqk_state = buf[1];
		if (coex_stat->bt_iqk_state == 0)
			coex_stat->cnt_bt[COEX_CNT_BT_IQK]++;
		else if (coex_stat->bt_iqk_state == 2)
			coex_stat->cnt_bt[COEX_CNT_BT_IQKFAIL]++;

		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], BT IQK by bt_info, data0 = 0x%02x\n",
			buf[1]);

		return;
	}

	if (rsp_source == COEX_BTINFO_SRC_BT_SCBD) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], BT Scoreboard change notify by WL FW c2h, 0xaa = 0x%02x, 0xab = 0x%02x\n",
			buf[1], buf[2]);

		rtw_coex_monitor_bt_enable(rtwdev);
		if (coex_stat->bt_disabled != coex_stat->bt_disabled_pre) {
			coex_stat->bt_disabled_pre = coex_stat->bt_disabled;
			rtw_coex_run_coex(rtwdev, COEX_RSN_BTINFO);
		}
		return;
	}

	if (rsp_source == COEX_BTINFO_SRC_H2C60) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], H2C 0x60 content replied by WL FW: H2C_0x60 = [%02x %02x %02x %02x %02x]\n",
			buf[1], buf[2], buf[3], buf[4], buf[5]);

		for (i = 1; i <= COEX_WL_TDMA_PARA_LENGTH; i++)
			coex_dm->fw_tdma_para[i - 1] = buf[i];
		return;
	}

	if (rsp_source == COEX_BTINFO_SRC_WL_FW) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], bt_info reply by WL FW\n");

		rtw_coex_update_bt_link_info(rtwdev);
		return;
	}

	if (rsp_source == COEX_BTINFO_SRC_BT_RSP ||
	    rsp_source == COEX_BTINFO_SRC_BT_ACT) {
		if (coex_stat->bt_disabled) {
			coex_stat->bt_disabled = false;
			coex_stat->bt_reenable = true;
			ieee80211_queue_delayed_work(rtwdev->hw,
						     &coex->bt_reenable_work,
						     15 * HZ);
			rtw_dbg(rtwdev, RTW_DBG_COEX,
				"[BTCoex], BT enable detected by bt_info\n");
		}
	}

	if (length != COEX_BTINFO_LENGTH) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], Bt_info length = %d invalid!!\n", length);

		return;
	}

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], Bt_info[%d], len=%d, data=[%02x %02x %02x %02x %02x %02x]\n",
		buf[0], length, buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

	for (i = 0; i < COEX_BTINFO_LENGTH; i++)
		coex_stat->bt_info_c2h[rsp_source][i] = buf[i];

	/* get the same info from bt, skip it */
	if (coex_stat->bt_info_c2h[rsp_source][1] == coex_stat->bt_info_lb2 &&
	    coex_stat->bt_info_c2h[rsp_source][2] == coex_stat->bt_info_lb3 &&
	    coex_stat->bt_info_c2h[rsp_source][3] == coex_stat->bt_info_hb0 &&
	    coex_stat->bt_info_c2h[rsp_source][4] == coex_stat->bt_info_hb1 &&
	    coex_stat->bt_info_c2h[rsp_source][5] == coex_stat->bt_info_hb2 &&
	    coex_stat->bt_info_c2h[rsp_source][6] == coex_stat->bt_info_hb3) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], Return because Btinfo duplicate!!\n");
		return;
	}

	coex_stat->bt_info_lb2 = coex_stat->bt_info_c2h[rsp_source][1];
	coex_stat->bt_info_lb3 = coex_stat->bt_info_c2h[rsp_source][2];
	coex_stat->bt_info_hb0 = coex_stat->bt_info_c2h[rsp_source][3];
	coex_stat->bt_info_hb1 = coex_stat->bt_info_c2h[rsp_source][4];
	coex_stat->bt_info_hb2 = coex_stat->bt_info_c2h[rsp_source][5];
	coex_stat->bt_info_hb3 = coex_stat->bt_info_c2h[rsp_source][6];

	/* 0xff means BT is under WHCK test */
	coex_stat->bt_whck_test = (coex_stat->bt_info_lb2 == 0xff);

	inq_page = ((coex_stat->bt_info_lb2 & BIT(2)) == BIT(2));

	if (inq_page != coex_stat->bt_inq_page) {
		cancel_delayed_work_sync(&coex->bt_remain_work);
		coex_stat->bt_inq_page = inq_page;

		if (inq_page)
			coex_stat->bt_inq_remain = true;
		else
			ieee80211_queue_delayed_work(rtwdev->hw,
						     &coex->bt_remain_work,
						     4 * HZ);
	}
	coex_stat->bt_acl_busy = ((coex_stat->bt_info_lb2 & BIT(3)) == BIT(3));
	if (chip->ble_hid_profile_support) {
		if (coex_stat->bt_info_lb2 & BIT(5)) {
			if (coex_stat->bt_info_hb1 & BIT(0)) {
				/*BLE HID*/
				coex_stat->bt_ble_hid_exist = true;
			} else {
				coex_stat->bt_ble_hid_exist = false;
			}
			coex_stat->bt_ble_exist = false;
		} else if (coex_stat->bt_info_hb1 & BIT(0)) {
			/*RCU*/
			coex_stat->bt_ble_hid_exist = false;
			coex_stat->bt_ble_exist = true;
		} else {
			coex_stat->bt_ble_hid_exist = false;
			coex_stat->bt_ble_exist = false;
		}
	} else {
		if (coex_stat->bt_info_hb1 & BIT(0)) {
			if (coex_stat->bt_hid_slot == 1 &&
			    coex_stat->hi_pri_rx + 100 < coex_stat->hi_pri_tx &&
			    coex_stat->hi_pri_rx < 100) {
				coex_stat->bt_ble_hid_exist = true;
				coex_stat->bt_ble_exist = false;
			} else {
				coex_stat->bt_ble_hid_exist = false;
				coex_stat->bt_ble_exist = true;
			}
		} else {
			coex_stat->bt_ble_hid_exist = false;
			coex_stat->bt_ble_exist = false;
		}
	}

	coex_stat->cnt_bt[COEX_CNT_BT_RETRY] = coex_stat->bt_info_lb3 & 0xf;
	if (coex_stat->cnt_bt[COEX_CNT_BT_RETRY] >= 1)
		coex_stat->cnt_bt[COEX_CNT_BT_POPEVENT]++;

	coex_stat->bt_fix_2M = ((coex_stat->bt_info_lb3 & BIT(4)) == BIT(4));
	coex_stat->bt_inq = ((coex_stat->bt_info_lb3 & BIT(5)) == BIT(5));
	if (coex_stat->bt_inq)
		coex_stat->cnt_bt[COEX_CNT_BT_INQ]++;

	coex_stat->bt_page = ((coex_stat->bt_info_lb3 & BIT(7)) == BIT(7));
	if (coex_stat->bt_page)
		coex_stat->cnt_bt[COEX_CNT_BT_PAGE]++;

	/* unit: % (value-100 to translate to unit: dBm in coex info) */
	if (chip->bt_rssi_type == COEX_BTRSSI_RATIO) {
		coex_stat->bt_rssi = coex_stat->bt_info_hb0 * 2 + 10;
	} else {
		if (coex_stat->bt_info_hb0 <= 127)
			coex_stat->bt_rssi = 100;
		else if (256 - coex_stat->bt_info_hb0 <= 100)
			coex_stat->bt_rssi = 100 - (256 - coex_stat->bt_info_hb0);
		else
			coex_stat->bt_rssi = 0;
	}

	if (coex_stat->bt_info_hb1 & BIT(1))
		coex_stat->cnt_bt[COEX_CNT_BT_REINIT]++;

	if (coex_stat->bt_info_hb1 & BIT(2)) {
		coex_stat->cnt_bt[COEX_CNT_BT_SETUPLINK]++;
		coex_stat->bt_setup_link = true;
		if (coex_stat->bt_reenable)
			bt_relink_time = 6 * HZ;
		else
			bt_relink_time = 1 * HZ;

		ieee80211_queue_delayed_work(rtwdev->hw,
					     &coex->bt_relink_work,
					     bt_relink_time);

		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], Re-Link start in BT info!!\n");
	}

	if (coex_stat->bt_info_hb1 & BIT(3))
		coex_stat->cnt_bt[COEX_CNT_BT_IGNWLANACT]++;

	coex_stat->bt_ble_voice = ((coex_stat->bt_info_hb1 & BIT(4)) == BIT(4));
	coex_stat->bt_ble_scan_en = ((coex_stat->bt_info_hb1 & BIT(5)) == BIT(5));
	if (coex_stat->bt_info_hb1 & BIT(6))
		coex_stat->cnt_bt[COEX_CNT_BT_ROLESWITCH]++;

	coex_stat->bt_multi_link = ((coex_stat->bt_info_hb1 & BIT(7)) == BIT(7));
	/* for multi_link = 0 but bt pkt remain exist */
	/* Use PS-TDMA to protect WL RX */
	if (!coex_stat->bt_multi_link && coex_stat->bt_multi_link_pre) {
		coex_stat->bt_multi_link_remain = true;
		ieee80211_queue_delayed_work(rtwdev->hw,
					     &coex->bt_multi_link_remain_work,
					     3 * HZ);
	}
	coex_stat->bt_multi_link_pre = coex_stat->bt_multi_link;

	/* resend wifi info to bt, it is reset and lost the info */
	if (coex_stat->bt_info_hb1 & BIT(1)) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], BT Re-init, send wifi BW & Chnl to BT!!\n");

		if (coex_stat->wl_connected)
			type = COEX_MEDIA_CONNECT;
		else
			type = COEX_MEDIA_DISCONNECT;
		rtw_coex_update_wl_ch_info(rtwdev, type);
	}

	/* if ignore_wlan_act && not set_up_link */
	if ((coex_stat->bt_info_hb1 & BIT(3)) &&
	    (!(coex_stat->bt_info_hb1 & BIT(2)))) {
		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
		rtw_coex_ignore_wlan_act(rtwdev, false);
	}

	coex_stat->bt_opp_exist = ((coex_stat->bt_info_hb2 & BIT(0)) == BIT(0));
	if (coex_stat->bt_info_hb2 & BIT(1))
		coex_stat->cnt_bt[COEX_CNT_BT_AFHUPDATE]++;

	coex_stat->bt_a2dp_active = (coex_stat->bt_info_hb2 & BIT(2)) == BIT(2);
	coex_stat->bt_slave = ((coex_stat->bt_info_hb2 & BIT(3)) == BIT(3));
	coex_stat->bt_hid_slot = (coex_stat->bt_info_hb2 & 0x30) >> 4;
	coex_stat->bt_hid_pair_num = (coex_stat->bt_info_hb2 & 0xc0) >> 6;
	if (coex_stat->bt_hid_pair_num > 0 && coex_stat->bt_hid_slot >= 2)
		coex_stat->bt_418_hid_exist = true;
	else if (coex_stat->bt_hid_pair_num == 0 || coex_stat->bt_hid_slot == 1)
		coex_stat->bt_418_hid_exist = false;

	if ((coex_stat->bt_info_lb2 & 0x49) == 0x49)
		coex_stat->bt_a2dp_bitpool = (coex_stat->bt_info_hb3 & 0x7f);
	else
		coex_stat->bt_a2dp_bitpool = 0;

	coex_stat->bt_a2dp_sink = ((coex_stat->bt_info_hb3 & BIT(7)) == BIT(7));

	rtw_coex_update_bt_link_info(rtwdev);
	rtw_coex_run_coex(rtwdev, COEX_RSN_BTINFO);
}

void rtw_coex_wl_fwdbginfo_notify(struct rtw_dev *rtwdev, u8 *buf, u8 length)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u8 val;
	int i;

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], WiFi Fw Dbg info = %8ph (len = %d)\n",
		buf, length);
	if (WARN(length < 8, "invalid wl info c2h length\n"))
		return;

	if (buf[0] != 0x08)
		return;

	for (i = 1; i < 8; i++) {
		val = coex_stat->wl_fw_dbg_info_pre[i];
		if (buf[i] >= val)
			coex_stat->wl_fw_dbg_info[i] = buf[i] - val;
		else
			coex_stat->wl_fw_dbg_info[i] = 255 - val + buf[i];

		coex_stat->wl_fw_dbg_info_pre[i] = buf[i];
	}

	coex_stat->cnt_wl[COEX_CNT_WL_FW_NOTIFY]++;
	rtw_coex_wl_ccklock_action(rtwdev);
	rtw_coex_wl_ccklock_detect(rtwdev);
}

void rtw_coex_wl_status_change_notify(struct rtw_dev *rtwdev, u32 type)
{
	rtw_coex_run_coex(rtwdev, COEX_RSN_WLSTATUS);
}

void rtw_coex_bt_relink_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.bt_relink_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->bt_setup_link = false;
	rtw_coex_run_coex(rtwdev, COEX_RSN_WLSTATUS);
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_bt_reenable_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.bt_reenable_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->bt_reenable = false;
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_defreeze_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.defreeze_work.work);
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex->freeze = false;
	coex_stat->wl_hi_pri_task1 = false;
	rtw_coex_run_coex(rtwdev, COEX_RSN_WLSTATUS);
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_wl_remain_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.wl_remain_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->wl_gl_busy = test_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);
	rtw_coex_run_coex(rtwdev, COEX_RSN_WLSTATUS);
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_bt_remain_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.bt_remain_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->bt_inq_remain = coex_stat->bt_inq_page;
	rtw_coex_run_coex(rtwdev, COEX_RSN_BTSTATUS);
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_wl_connecting_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.wl_connecting_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->wl_connecting = false;
	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], WL connecting stop!!\n");
	rtw_coex_run_coex(rtwdev, COEX_RSN_WLSTATUS);
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_bt_multi_link_remain_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
		coex.bt_multi_link_remain_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->bt_multi_link_remain = false;
	mutex_unlock(&rtwdev->mutex);
}

void rtw_coex_wl_ccklock_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      coex.wl_ccklock_work.work);
	struct rtw_coex_stat *coex_stat = &rtwdev->coex.stat;

	mutex_lock(&rtwdev->mutex);
	coex_stat->wl_cck_lock = false;
	mutex_unlock(&rtwdev->mutex);
}

#ifdef CONFIG_RTW88_DEBUGFS
#define INFO_SIZE	80

#define case_BTINFO(src) \
	case COEX_BTINFO_SRC_##src: return #src

static const char *rtw_coex_get_bt_info_src_string(u8 bt_info_src)
{
	switch (bt_info_src) {
	case_BTINFO(WL_FW);
	case_BTINFO(BT_RSP);
	case_BTINFO(BT_ACT);
	default:
		return "Unknown";
	}
}

#define case_RSN(src) \
	case COEX_RSN_##src: return #src

static const char *rtw_coex_get_reason_string(u8 reason)
{
	switch (reason) {
	case_RSN(2GSCANSTART);
	case_RSN(5GSCANSTART);
	case_RSN(SCANFINISH);
	case_RSN(2GSWITCHBAND);
	case_RSN(5GSWITCHBAND);
	case_RSN(2GCONSTART);
	case_RSN(5GCONSTART);
	case_RSN(2GCONFINISH);
	case_RSN(5GCONFINISH);
	case_RSN(2GMEDIA);
	case_RSN(5GMEDIA);
	case_RSN(MEDIADISCON);
	case_RSN(BTINFO);
	case_RSN(LPS);
	case_RSN(WLSTATUS);
	default:
		return "Unknown";
	}
}

static u8 rtw_coex_get_table_index(struct rtw_dev *rtwdev, u32 wl_reg_6c0,
				   u32 wl_reg_6c4)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 ans = 0xFF;
	u8 n, i;
	u32 load_bt_val;
	u32 load_wl_val;
	bool share_ant = efuse->share_ant;

	if (share_ant)
		n = chip->table_sant_num;
	else
		n = chip->table_nsant_num;

	for (i = 0; i < n; i++) {
		if (share_ant) {
			load_bt_val = chip->table_sant[i].bt;
			load_wl_val = chip->table_sant[i].wl;
		} else {
			load_bt_val = chip->table_nsant[i].bt;
			load_wl_val = chip->table_nsant[i].wl;
		}

		if (wl_reg_6c0 == load_bt_val &&
		    wl_reg_6c4 == load_wl_val) {
			ans = i;
			if (!share_ant)
				ans += 100;
			break;
		}
	}

	return ans;
}

static u8 rtw_coex_get_tdma_index(struct rtw_dev *rtwdev, u8 *tdma_para)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 ans = 0xFF;
	u8 n, i, j;
	u8 load_cur_tab_val;
	bool valid = false;
	bool share_ant = efuse->share_ant;

	if (share_ant)
		n = chip->tdma_sant_num;
	else
		n = chip->tdma_nsant_num;

	for (i = 0; i < n; i++) {
		valid = false;
		for (j = 0; j < 5; j++) {
			if (share_ant)
				load_cur_tab_val = chip->tdma_sant[i].para[j];
			else
				load_cur_tab_val = chip->tdma_nsant[i].para[j];

			if (*(tdma_para + j) != load_cur_tab_val)
				break;

			if (j == 4)
				valid = true;
		}
		if (valid) {
			ans = i;
			break;
		}
	}

	return ans;
}

static int rtw_coex_addr_info(struct rtw_dev *rtwdev,
			      const struct rtw_reg_domain *reg,
			      char addr_info[], int n)
{
	const char *rf_prefix = "";
	const char *sep = n == 0 ? "" : "/ ";
	int ffs, fls;
	int max_fls;

	if (INFO_SIZE - n <= 0)
		return 0;

	switch (reg->domain) {
	case RTW_REG_DOMAIN_MAC32:
		max_fls = 31;
		break;
	case RTW_REG_DOMAIN_MAC16:
		max_fls = 15;
		break;
	case RTW_REG_DOMAIN_MAC8:
		max_fls = 7;
		break;
	case RTW_REG_DOMAIN_RF_A:
	case RTW_REG_DOMAIN_RF_B:
		rf_prefix = "RF_";
		max_fls = 19;
		break;
	default:
		return 0;
	}

	ffs = __ffs(reg->mask);
	fls = __fls(reg->mask);

	if (ffs == 0 && fls == max_fls)
		return scnprintf(addr_info + n, INFO_SIZE - n, "%s%s%x",
				 sep, rf_prefix, reg->addr);
	else if (ffs == fls)
		return scnprintf(addr_info + n, INFO_SIZE - n, "%s%s%x[%d]",
				 sep, rf_prefix, reg->addr, ffs);
	else
		return scnprintf(addr_info + n, INFO_SIZE - n, "%s%s%x[%d:%d]",
				 sep, rf_prefix, reg->addr, fls, ffs);
}

static int rtw_coex_val_info(struct rtw_dev *rtwdev,
			     const struct rtw_reg_domain *reg,
			     char val_info[], int n)
{
	const char *sep = n == 0 ? "" : "/ ";
	u8 rf_path;

	if (INFO_SIZE - n <= 0)
		return 0;

	switch (reg->domain) {
	case RTW_REG_DOMAIN_MAC32:
		return scnprintf(val_info + n, INFO_SIZE - n, "%s0x%x", sep,
				 rtw_read32_mask(rtwdev, reg->addr, reg->mask));
	case RTW_REG_DOMAIN_MAC16:
		return scnprintf(val_info + n, INFO_SIZE - n, "%s0x%x", sep,
				 rtw_read16_mask(rtwdev, reg->addr, reg->mask));
	case RTW_REG_DOMAIN_MAC8:
		return scnprintf(val_info + n, INFO_SIZE - n, "%s0x%x", sep,
				 rtw_read8_mask(rtwdev, reg->addr, reg->mask));
	case RTW_REG_DOMAIN_RF_A:
		rf_path = RF_PATH_A;
		break;
	case RTW_REG_DOMAIN_RF_B:
		rf_path = RF_PATH_B;
		break;
	default:
		return 0;
	}

	/* only RF go through here */
	return scnprintf(val_info + n, INFO_SIZE - n, "%s0x%x", sep,
			 rtw_read_rf(rtwdev, rf_path, reg->addr, reg->mask));
}

static void rtw_coex_set_coexinfo_hw(struct rtw_dev *rtwdev, struct seq_file *m)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_reg_domain *reg;
	char addr_info[INFO_SIZE];
	int n_addr = 0;
	char val_info[INFO_SIZE];
	int n_val = 0;
	int i;

	for (i = 0; i < chip->coex_info_hw_regs_num; i++) {
		reg = &chip->coex_info_hw_regs[i];

		n_addr += rtw_coex_addr_info(rtwdev, reg, addr_info, n_addr);
		n_val += rtw_coex_val_info(rtwdev, reg, val_info, n_val);

		if (reg->domain == RTW_REG_DOMAIN_NL) {
			seq_printf(m, "%-40s = %s\n", addr_info, val_info);
			n_addr = 0;
			n_val = 0;
		}
	}

	if (n_addr != 0 && n_val != 0)
		seq_printf(m, "%-40s = %s\n", addr_info, val_info);
}

static bool rtw_coex_get_bt_reg(struct rtw_dev *rtwdev,
				u8 type, u16 addr, u16 *val)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	__le16 le_addr;
	u8 *payload;

	le_addr = cpu_to_le16(addr);
	req.op_code = BT_MP_INFO_OP_READ_REG;
	req.para1 = type;
	req.para2 = le16_get_bits(le_addr, GENMASK(7, 0));
	req.para3 = le16_get_bits(le_addr, GENMASK(15, 8));
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb) {
		*val = 0xeaea;
		return false;
	}

	payload = get_payload_from_coex_resp(skb);
	*val = GET_COEX_RESP_BT_REG_VAL(payload);

	return true;
}

static bool rtw_coex_get_bt_patch_version(struct rtw_dev *rtwdev,
					  u32 *patch_version)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	u8 *payload;
	bool ret = false;

	req.op_code = BT_MP_INFO_OP_PATCH_VER;
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb)
		goto out;

	payload = get_payload_from_coex_resp(skb);
	*patch_version = GET_COEX_RESP_BT_PATCH_VER(payload);
	ret = true;

out:
	return ret;
}

static bool rtw_coex_get_bt_supported_version(struct rtw_dev *rtwdev,
					      u32 *supported_version)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	u8 *payload;
	bool ret = false;

	req.op_code = BT_MP_INFO_OP_SUPP_VER;
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb)
		goto out;

	payload = get_payload_from_coex_resp(skb);
	*supported_version = GET_COEX_RESP_BT_SUPP_VER(payload);
	ret = true;

out:
	return ret;
}

static bool rtw_coex_get_bt_supported_feature(struct rtw_dev *rtwdev,
					      u32 *supported_feature)
{
	struct rtw_coex_info_req req = {0};
	struct sk_buff *skb;
	u8 *payload;
	bool ret = false;

	req.op_code = BT_MP_INFO_OP_SUPP_FEAT;
	skb = rtw_coex_info_request(rtwdev, &req);
	if (!skb)
		goto out;

	payload = get_payload_from_coex_resp(skb);
	*supported_feature = GET_COEX_RESP_BT_SUPP_FEAT(payload);
	ret = true;

out:
	return ret;
}

struct rtw_coex_sta_stat_iter_data {
	struct rtw_vif *rtwvif;
	struct seq_file *file;
};

static void rtw_coex_sta_stat_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_coex_sta_stat_iter_data *sta_iter_data = data;
	struct rtw_vif *rtwvif = sta_iter_data->rtwvif;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	struct seq_file *m = sta_iter_data->file;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u8 rssi;

	if (si->vif != vif)
		return;

	rssi = ewma_rssi_read(&si->avg_rssi);
	seq_printf(m, "\tPeer %3d\n", si->mac_id);
	seq_printf(m, "\t\t%-24s = %d\n", "RSSI", rssi);
	seq_printf(m, "\t\t%-24s = %d\n", "BW mode", si->bw_mode);
}

struct rtw_coex_vif_stat_iter_data {
	struct rtw_dev *rtwdev;
	struct seq_file *file;
};

static void rtw_coex_vif_stat_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct rtw_coex_vif_stat_iter_data *vif_iter_data = data;
	struct rtw_coex_sta_stat_iter_data sta_iter_data;
	struct rtw_dev *rtwdev = vif_iter_data->rtwdev;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct seq_file *m = vif_iter_data->file;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	seq_printf(m, "Iface on Port (%d)\n", rtwvif->port);
	seq_printf(m, "\t%-32s = %d\n",
		   "Beacon interval", bss_conf->beacon_int);
	seq_printf(m, "\t%-32s = %d\n",
		   "Network Type", rtwvif->net_type);

	sta_iter_data.rtwvif = rtwvif;
	sta_iter_data.file = m;
	rtw_iterate_stas_atomic(rtwdev, rtw_coex_sta_stat_iter,
				&sta_iter_data);
}

#define case_WLINK(src) \
	case COEX_WLINK_##src: return #src

static const char *rtw_coex_get_wl_coex_mode(u8 coex_wl_link_mode)
{
	switch (coex_wl_link_mode) {
	case_WLINK(2G1PORT);
	case_WLINK(5G);
	default:
		return "Unknown";
	}
}

void rtw_coex_display_coex_info(struct rtw_dev *rtwdev, struct seq_file *m)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_fw_state *fw = &rtwdev->fw;
	struct rtw_coex_vif_stat_iter_data vif_iter_data;
	u8 reason = coex_dm->reason;
	u8 sys_lte;
	u16 score_board_WB, score_board_BW;
	u32 wl_reg_6c0, wl_reg_6c4, wl_reg_6c8, wl_reg_778, wl_reg_6cc;
	u32 lte_coex, bt_coex;
	u32 bt_hi_pri, bt_lo_pri;
	int i;

	score_board_BW = rtw_coex_read_scbd(rtwdev);
	score_board_WB = coex_stat->score_board;
	wl_reg_6c0 = rtw_read32(rtwdev, REG_BT_COEX_TABLE0);
	wl_reg_6c4 = rtw_read32(rtwdev, REG_BT_COEX_TABLE1);
	wl_reg_6c8 = rtw_read32(rtwdev, REG_BT_COEX_BRK_TABLE);
	wl_reg_6cc = rtw_read32(rtwdev, REG_BT_COEX_TABLE_H);
	wl_reg_778 = rtw_read8(rtwdev, REG_BT_STAT_CTRL);

	bt_hi_pri = rtw_read32(rtwdev, REG_BT_ACT_STATISTICS);
	bt_lo_pri = rtw_read32(rtwdev, REG_BT_ACT_STATISTICS_1);
	rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL,
		   BIT_R_GRANTALL_WLMASK | BIT_STATIS_BT_EN);

	coex_stat->hi_pri_tx = FIELD_GET(MASKLWORD, bt_hi_pri);
	coex_stat->hi_pri_rx = FIELD_GET(MASKHWORD, bt_hi_pri);

	coex_stat->lo_pri_tx = FIELD_GET(MASKLWORD, bt_lo_pri);
	coex_stat->lo_pri_rx = FIELD_GET(MASKHWORD, bt_lo_pri);

	sys_lte = rtw_read8(rtwdev, 0x73);
	lte_coex = rtw_coex_read_indirect_reg(rtwdev, 0x38);
	bt_coex = rtw_coex_read_indirect_reg(rtwdev, 0x54);

	if (!coex_stat->bt_disabled && !coex_stat->bt_mailbox_reply) {
		rtw_coex_get_bt_supported_version(rtwdev,
				&coex_stat->bt_supported_version);
		rtw_coex_get_bt_patch_version(rtwdev, &coex_stat->patch_ver);
		rtw_coex_get_bt_supported_feature(rtwdev,
				&coex_stat->bt_supported_feature);
		rtw_coex_get_bt_reg(rtwdev, 3, 0xae, &coex_stat->bt_reg_vendor_ae);
		rtw_coex_get_bt_reg(rtwdev, 3, 0xac, &coex_stat->bt_reg_vendor_ac);

		if (coex_stat->patch_ver != 0)
			coex_stat->bt_mailbox_reply = true;
	}

	rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], %s()\n", __func__);
	seq_printf(m, "**********************************************\n");
	seq_printf(m, "\t\tBT Coexist info %x\n", chip->id);
	seq_printf(m, "**********************************************\n");

	if (coex->manual_control) {
		seq_puts(m, "============[Under Manual Control]============\n");
		seq_puts(m, "==========================================\n");

	} else if (coex->stop_dm) {
		seq_puts(m, "============[Coex is STOPPED]============\n");
		seq_puts(m, "==========================================\n");

	} else if (coex->freeze) {
		seq_puts(m, "============[coex_freeze]============\n");
		seq_puts(m, "==========================================\n");
	}

	seq_printf(m, "%-40s = %s/ %d\n",
		   "Mech/ RFE",
		   efuse->share_ant ? "Shared" : "Non-Shared",
		   efuse->rfe_option);
	seq_printf(m, "%-40s = %08x/ 0x%02x/ 0x%08x %s\n",
		   "Coex Ver/ BT Dez/ BT Rpt",
		   chip->coex_para_ver, chip->bt_desired_ver,
		   coex_stat->bt_supported_version,
		   coex_stat->bt_disabled ? "(BT disabled)" :
		   coex_stat->bt_supported_version >= chip->bt_desired_ver ?
		   "(Match)" : "(Mismatch)");
	seq_printf(m, "%-40s = %s/ %u/ %d\n",
		   "Role/ RoleSwCnt/ IgnWL/ Feature",
		   coex_stat->bt_slave ? "Slave" : "Master",
		   coex_stat->cnt_bt[COEX_CNT_BT_ROLESWITCH],
		   coex_dm->ignore_wl_act);
	seq_printf(m, "%-40s = %u.%u/ 0x%x/ 0x%x/ %c\n",
		   "WL FW/ BT FW/ BT FW Desired/ KT",
		   fw->version, fw->sub_version,
		   coex_stat->patch_ver,
		   chip->wl_fw_desired_ver, coex_stat->kt_ver + 65);
	seq_printf(m, "%-40s = %u/ %u/ %u/ ch-(%u)\n",
		   "AFH Map",
		   coex_dm->wl_ch_info[0], coex_dm->wl_ch_info[1],
		   coex_dm->wl_ch_info[2], hal->current_channel);

	rtw_debugfs_get_simple_phy_info(m);
	seq_printf(m, "**********************************************\n");
	seq_printf(m, "\t\tBT Status\n");
	seq_printf(m, "**********************************************\n");
	seq_printf(m, "%-40s = %s/ %ddBm/ %u/ %u\n",
		   "BT status/ rssi/ retry/ pop",
		   coex_dm->bt_status == COEX_BTSTATUS_NCON_IDLE ? "non-conn" :
		   coex_dm->bt_status == COEX_BTSTATUS_CON_IDLE ? "conn-idle" : "busy",
		   coex_stat->bt_rssi - 100,
		   coex_stat->cnt_bt[COEX_CNT_BT_RETRY],
		   coex_stat->cnt_bt[COEX_CNT_BT_POPEVENT]);
	seq_printf(m, "%-40s = %s%s%s%s%s (multi-link %d)\n",
		   "Profiles",
		   coex_stat->bt_a2dp_exist ? (coex_stat->bt_a2dp_sink ?
					       "A2DP sink," : "A2DP,") : "",
		   coex_stat->bt_hfp_exist ? "HFP," : "",
		   coex_stat->bt_hid_exist ?
		   (coex_stat->bt_ble_exist ? "HID(RCU)," :
		    coex_stat->bt_hid_slot >= 2 ? "HID(4/18)" :
		    coex_stat->bt_ble_hid_exist ? "HID(BLE)" :
		    "HID(2/18),") : "",
		   coex_stat->bt_pan_exist ? coex_stat->bt_opp_exist ?
		   "OPP," : "PAN," : "",
		   coex_stat->bt_ble_voice ? "Voice," : "",
		   coex_stat->bt_multi_link);
	seq_printf(m, "%-40s = %u/ %u/ %u/ 0x%08x\n",
		   "Reinit/ Relink/ IgnWl/ Feature",
		   coex_stat->cnt_bt[COEX_CNT_BT_REINIT],
		   coex_stat->cnt_bt[COEX_CNT_BT_SETUPLINK],
		   coex_stat->cnt_bt[COEX_CNT_BT_IGNWLANACT],
		   coex_stat->bt_supported_feature);
	seq_printf(m, "%-40s = %u/ %u/ %u/ %u\n",
		   "Page/ Inq/ iqk/ iqk fail",
		   coex_stat->cnt_bt[COEX_CNT_BT_PAGE],
		   coex_stat->cnt_bt[COEX_CNT_BT_INQ],
		   coex_stat->cnt_bt[COEX_CNT_BT_IQK],
		   coex_stat->cnt_bt[COEX_CNT_BT_IQKFAIL]);
	seq_printf(m, "%-40s = 0x%04x/ 0x%04x/ 0x%04x/ 0x%04x\n",
		   "0xae/ 0xac/ score board (W->B)/ (B->W)",
		   coex_stat->bt_reg_vendor_ae,
		   coex_stat->bt_reg_vendor_ac,
		   score_board_WB, score_board_BW);
	seq_printf(m, "%-40s = %u/%u, %u/%u\n",
		   "Hi-Pri TX/RX, Lo-Pri TX/RX",
		   coex_stat->hi_pri_tx, coex_stat->hi_pri_rx,
		   coex_stat->lo_pri_tx, coex_stat->lo_pri_rx);
	for (i = 0; i < COEX_BTINFO_SRC_BT_IQK; i++)
		seq_printf(m, "%-40s = %7ph\n",
			   rtw_coex_get_bt_info_src_string(i),
			   coex_stat->bt_info_c2h[i]);

	seq_printf(m, "**********************************************\n");
	seq_printf(m, "\t\tWiFi Status\n");
	seq_printf(m, "**********************************************\n");
	seq_printf(m, "%-40s = %d\n",
		   "Scanning", test_bit(RTW_FLAG_SCANNING, rtwdev->flags));
	seq_printf(m, "%-40s = %u/ TX %d Mbps/ RX %d Mbps\n",
		   "G_busy/ TX/ RX",
		   coex_stat->wl_gl_busy,
		   rtwdev->stats.tx_throughput, rtwdev->stats.rx_throughput);
	seq_printf(m, "%-40s = %u/ %u/ %u\n",
		   "IPS/ Low Power/ PS mode",
		   test_bit(RTW_FLAG_INACTIVE_PS, rtwdev->flags),
		   test_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags),
		   rtwdev->lps_conf.mode);

	vif_iter_data.rtwdev = rtwdev;
	vif_iter_data.file = m;
	rtw_iterate_vifs_atomic(rtwdev, rtw_coex_vif_stat_iter, &vif_iter_data);

	if (coex->manual_control) {
		seq_printf(m, "**********************************************\n");
		seq_printf(m, "\t\tMechanism (Under Manual)\n");
		seq_printf(m, "**********************************************\n");
		seq_printf(m, "%-40s = %5ph (%d)\n",
			   "TDMA Now",
			   coex_dm->fw_tdma_para,
			   rtw_coex_get_tdma_index(rtwdev,
						   &coex_dm->fw_tdma_para[0]));
	} else {
		seq_printf(m, "**********************************************\n");
		seq_printf(m, "\t\tMechanism\n");
		seq_printf(m, "**********************************************\n");
		seq_printf(m, "%-40s = %5ph (case-%d)\n",
			   "TDMA",
			   coex_dm->ps_tdma_para, coex_dm->cur_ps_tdma);
	}
	seq_printf(m, "%-40s = %s/ %s/ %d\n",
		   "Coex Mode/Free Run/Timer base",
		   rtw_coex_get_wl_coex_mode(coex_stat->wl_coex_mode),
		   coex->freerun ? "Yes" : "No",
		   coex_stat->tdma_timer_base);
	seq_printf(m, "%-40s = %d(%d)/ 0x%08x/ 0x%08x/ 0x%08x\n",
		   "Table/ 0x6c0/ 0x6c4/ 0x6c8",
		   coex_dm->cur_table,
		   rtw_coex_get_table_index(rtwdev, wl_reg_6c0, wl_reg_6c4),
		   wl_reg_6c0, wl_reg_6c4, wl_reg_6c8);
	seq_printf(m, "%-40s = 0x%08x/ 0x%08x/ %d/ reason (%s)\n",
		   "0x778/ 0x6cc/ Run Count/ Reason",
		   wl_reg_778, wl_reg_6cc,
		   coex_stat->cnt_wl[COEX_CNT_WL_COEXRUN],
		   rtw_coex_get_reason_string(reason));
	seq_printf(m, "%-40s = %3ph\n",
		   "AFH Map to BT",
		   coex_dm->wl_ch_info);
	seq_printf(m, "%-40s = %s/ %d\n",
		   "AntDiv/ BtCtrlLPS/ g_busy",
		   coex_stat->wl_force_lps_ctrl ? "On" : "Off",
		   coex_stat->wl_gl_busy);
	seq_printf(m, "%-40s = %u/ %u/ %u/ %u/ %u\n",
		   "Null All/ Retry/ Ack/ BT Empty/ BT Late",
		   coex_stat->wl_fw_dbg_info[1], coex_stat->wl_fw_dbg_info[2],
		   coex_stat->wl_fw_dbg_info[3], coex_stat->wl_fw_dbg_info[4],
		   coex_stat->wl_fw_dbg_info[5]);
	seq_printf(m, "%-40s = %u/ %u/ %s/ %u\n",
		   "Cnt TDMA Toggle/ Lk 5ms/ Lk 5ms on/ FW",
		   coex_stat->wl_fw_dbg_info[6],
		   coex_stat->wl_fw_dbg_info[7],
		   coex_stat->wl_slot_extend ? "Yes" : "No",
		   coex_stat->cnt_wl[COEX_CNT_WL_FW_NOTIFY]);
	seq_printf(m, "%-40s = %d/ %d/ %s/ %d\n",
		   "WL_TxPw/ BT_TxPw/ WL_Rx/ BT_LNA_Lvl",
		   coex_dm->cur_wl_pwr_lvl,
		   coex_dm->cur_bt_pwr_lvl,
		   coex_dm->cur_wl_rx_low_gain_en ? "On" : "Off",
		   coex_dm->cur_bt_lna_lvl);

	seq_printf(m, "**********************************************\n");
	seq_printf(m, "\t\tHW setting\n");
	seq_printf(m, "**********************************************\n");
	seq_printf(m, "%-40s = %s/ %s\n",
		   "LTE Coex/ Path Owner",
		   lte_coex & BIT(7) ? "ON" : "OFF",
		   sys_lte & BIT(2) ? "WL" : "BT");
	seq_printf(m, "%-40s = RF:%s_BB:%s/ RF:%s_BB:%s/ %s\n",
		   "GNT_WL_CTRL/ GNT_BT_CTRL/ Dbg",
		   lte_coex & BIT(12) ? "SW" : "HW",
		   lte_coex & BIT(8) ? "SW" : "HW",
		   lte_coex & BIT(14) ? "SW" : "HW",
		   lte_coex & BIT(10) ? "SW" : "HW",
		   sys_lte & BIT(3) ? "On" : "Off");
	seq_printf(m, "%-40s = %lu/ %lu\n",
		   "GNT_WL/ GNT_BT",
		   (bt_coex & BIT(2)) >> 2, (bt_coex & BIT(3)) >> 3);
	seq_printf(m, "%-40s = %u/ %u/ %u/ %u\n",
		   "CRC OK CCK/ OFDM/ HT/ VHT",
		   dm_info->cck_ok_cnt, dm_info->ofdm_ok_cnt,
		   dm_info->ht_ok_cnt, dm_info->vht_ok_cnt);
	seq_printf(m, "%-40s = %u/ %u/ %u/ %u\n",
		   "CRC ERR CCK/ OFDM/ HT/ VHT",
		   dm_info->cck_err_cnt, dm_info->ofdm_err_cnt,
		   dm_info->ht_err_cnt, dm_info->vht_err_cnt);
	seq_printf(m, "%-40s = %s/ %s/ %s/ %u\n",
		   "HiPr/ Locking/ Locked/ Noisy",
		   coex_stat->wl_hi_pri_task1 ? "Y" : "N",
		   coex_stat->wl_cck_lock ? "Y" : "N",
		   coex_stat->wl_cck_lock_ever ? "Y" : "N",
		   coex_stat->wl_noisy_level);

	rtw_coex_set_coexinfo_hw(rtwdev, m);
	seq_printf(m, "%-40s = %d/ %d/ %d/ %d\n",
		   "EVM A/ EVM B/ SNR A/ SNR B",
		   -dm_info->rx_evm_dbm[RF_PATH_A],
		   -dm_info->rx_evm_dbm[RF_PATH_B],
		   -dm_info->rx_snr[RF_PATH_A],
		   -dm_info->rx_snr[RF_PATH_B]);
	seq_printf(m, "%-40s = %d/ %d/ %d/ %d\n",
		   "CCK-CCA/CCK-FA/OFDM-CCA/OFDM-FA",
		   dm_info->cck_cca_cnt, dm_info->cck_fa_cnt,
		   dm_info->ofdm_cca_cnt, dm_info->ofdm_fa_cnt);
	seq_printf(m, "%-40s = %d/ %d/ %d/ %d\n", "CRC OK CCK/11g/11n/11ac",
		   dm_info->cck_ok_cnt, dm_info->ofdm_ok_cnt,
		   dm_info->ht_ok_cnt, dm_info->vht_ok_cnt);
	seq_printf(m, "%-40s = %d/ %d/ %d/ %d\n", "CRC Err CCK/11g/11n/11ac",
		   dm_info->cck_err_cnt, dm_info->ofdm_err_cnt,
		   dm_info->ht_err_cnt, dm_info->vht_err_cnt);

}
#endif /* CONFIG_RTW88_DEBUGFS */
