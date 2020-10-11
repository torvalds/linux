// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/iopoll.h>

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "reg.h"
#include "sec.h"
#include "debug.h"
#include "util.h"
#include "wow.h"

static void rtw_fw_c2h_cmd_handle_ext(struct rtw_dev *rtwdev,
				      struct sk_buff *skb)
{
	struct rtw_c2h_cmd *c2h;
	u8 sub_cmd_id;

	c2h = get_c2h_from_skb(skb);
	sub_cmd_id = c2h->payload[0];

	switch (sub_cmd_id) {
	case C2H_CCX_RPT:
		rtw_tx_report_handle(rtwdev, skb, C2H_CCX_RPT);
		break;
	default:
		break;
	}
}

static u16 get_max_amsdu_len(u32 bit_rate)
{
	/* lower than ofdm, do not aggregate */
	if (bit_rate < 550)
		return 1;

	/* lower than 20M 2ss mcs8, make it small */
	if (bit_rate < 1800)
		return 1200;

	/* lower than 40M 2ss mcs9, make it medium */
	if (bit_rate < 4000)
		return 2600;

	/* not yet 80M 2ss mcs8/9, make it twice regular packet size */
	if (bit_rate < 7000)
		return 3500;

	/* unlimited */
	return 0;
}

struct rtw_fw_iter_ra_data {
	struct rtw_dev *rtwdev;
	u8 *payload;
};

static void rtw_fw_ra_report_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_fw_iter_ra_data *ra_data = data;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	u8 mac_id, rate, sgi, bw;
	u8 mcs, nss;
	u32 bit_rate;

	mac_id = GET_RA_REPORT_MACID(ra_data->payload);
	if (si->mac_id != mac_id)
		return;

	si->ra_report.txrate.flags = 0;

	rate = GET_RA_REPORT_RATE(ra_data->payload);
	sgi = GET_RA_REPORT_SGI(ra_data->payload);
	bw = GET_RA_REPORT_BW(ra_data->payload);

	if (rate < DESC_RATEMCS0) {
		si->ra_report.txrate.legacy = rtw_desc_to_bitrate(rate);
		goto legacy;
	}

	rtw_desc_to_mcsrate(rate, &mcs, &nss);
	if (rate >= DESC_RATEVHT1SS_MCS0)
		si->ra_report.txrate.flags |= RATE_INFO_FLAGS_VHT_MCS;
	else if (rate >= DESC_RATEMCS0)
		si->ra_report.txrate.flags |= RATE_INFO_FLAGS_MCS;

	if (rate >= DESC_RATEMCS0) {
		si->ra_report.txrate.mcs = mcs;
		si->ra_report.txrate.nss = nss;
	}

	if (sgi)
		si->ra_report.txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;

	if (bw == RTW_CHANNEL_WIDTH_80)
		si->ra_report.txrate.bw = RATE_INFO_BW_80;
	else if (bw == RTW_CHANNEL_WIDTH_40)
		si->ra_report.txrate.bw = RATE_INFO_BW_40;
	else
		si->ra_report.txrate.bw = RATE_INFO_BW_20;

legacy:
	bit_rate = cfg80211_calculate_bitrate(&si->ra_report.txrate);

	si->ra_report.desc_rate = rate;
	si->ra_report.bit_rate = bit_rate;

	sta->max_rc_amsdu_len = get_max_amsdu_len(bit_rate);
}

static void rtw_fw_ra_report_handle(struct rtw_dev *rtwdev, u8 *payload,
				    u8 length)
{
	struct rtw_fw_iter_ra_data ra_data;

	if (WARN(length < 7, "invalid ra report c2h length\n"))
		return;

	rtwdev->dm_info.tx_rate = GET_RA_REPORT_RATE(payload);
	ra_data.rtwdev = rtwdev;
	ra_data.payload = payload;
	rtw_iterate_stas_atomic(rtwdev, rtw_fw_ra_report_iter, &ra_data);
}

void rtw_fw_c2h_cmd_handle(struct rtw_dev *rtwdev, struct sk_buff *skb)
{
	struct rtw_c2h_cmd *c2h;
	u32 pkt_offset;
	u8 len;

	pkt_offset = *((u32 *)skb->cb);
	c2h = (struct rtw_c2h_cmd *)(skb->data + pkt_offset);
	len = skb->len - pkt_offset - 2;

	mutex_lock(&rtwdev->mutex);

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags))
		goto unlock;

	switch (c2h->id) {
	case C2H_CCX_TX_RPT:
		rtw_tx_report_handle(rtwdev, skb, C2H_CCX_TX_RPT);
		break;
	case C2H_BT_INFO:
		rtw_coex_bt_info_notify(rtwdev, c2h->payload, len);
		break;
	case C2H_WLAN_INFO:
		rtw_coex_wl_fwdbginfo_notify(rtwdev, c2h->payload, len);
		break;
	case C2H_HALMAC:
		rtw_fw_c2h_cmd_handle_ext(rtwdev, skb);
		break;
	case C2H_RA_RPT:
		rtw_fw_ra_report_handle(rtwdev, c2h->payload, len);
		break;
	default:
		rtw_dbg(rtwdev, RTW_DBG_FW, "C2H 0x%x isn't handled\n", c2h->id);
		break;
	}

unlock:
	mutex_unlock(&rtwdev->mutex);
}

void rtw_fw_c2h_cmd_rx_irqsafe(struct rtw_dev *rtwdev, u32 pkt_offset,
			       struct sk_buff *skb)
{
	struct rtw_c2h_cmd *c2h;
	u8 len;

	c2h = (struct rtw_c2h_cmd *)(skb->data + pkt_offset);
	len = skb->len - pkt_offset - 2;
	*((u32 *)skb->cb) = pkt_offset;

	rtw_dbg(rtwdev, RTW_DBG_FW, "recv C2H, id=0x%02x, seq=0x%02x, len=%d\n",
		c2h->id, c2h->seq, len);

	switch (c2h->id) {
	case C2H_BT_MP_INFO:
		rtw_coex_info_response(rtwdev, skb);
		break;
	default:
		/* pass offset for further operation */
		*((u32 *)skb->cb) = pkt_offset;
		skb_queue_tail(&rtwdev->c2h_queue, skb);
		ieee80211_queue_work(rtwdev->hw, &rtwdev->c2h_work);
		break;
	}
}
EXPORT_SYMBOL(rtw_fw_c2h_cmd_rx_irqsafe);

void rtw_fw_c2h_cmd_isr(struct rtw_dev *rtwdev)
{
	if (rtw_read8(rtwdev, REG_MCU_TST_CFG) == VAL_FW_TRIGGER)
		rtw_fw_recovery(rtwdev);
	else
		rtw_warn(rtwdev, "unhandled firmware c2h interrupt\n");
}
EXPORT_SYMBOL(rtw_fw_c2h_cmd_isr);

static void rtw_fw_send_h2c_command(struct rtw_dev *rtwdev,
				    u8 *h2c)
{
	u8 box;
	u8 box_state;
	u32 box_reg, box_ex_reg;
	int idx;
	int ret;

	rtw_dbg(rtwdev, RTW_DBG_FW,
		"send H2C content %02x%02x%02x%02x %02x%02x%02x%02x\n",
		h2c[3], h2c[2], h2c[1], h2c[0],
		h2c[7], h2c[6], h2c[5], h2c[4]);

	spin_lock(&rtwdev->h2c.lock);

	box = rtwdev->h2c.last_box_num;
	switch (box) {
	case 0:
		box_reg = REG_HMEBOX0;
		box_ex_reg = REG_HMEBOX0_EX;
		break;
	case 1:
		box_reg = REG_HMEBOX1;
		box_ex_reg = REG_HMEBOX1_EX;
		break;
	case 2:
		box_reg = REG_HMEBOX2;
		box_ex_reg = REG_HMEBOX2_EX;
		break;
	case 3:
		box_reg = REG_HMEBOX3;
		box_ex_reg = REG_HMEBOX3_EX;
		break;
	default:
		WARN(1, "invalid h2c mail box number\n");
		goto out;
	}

	ret = read_poll_timeout_atomic(rtw_read8, box_state,
				       !((box_state >> box) & 0x1), 100, 3000,
				       false, rtwdev, REG_HMETFR);

	if (ret) {
		rtw_err(rtwdev, "failed to send h2c command\n");
		goto out;
	}

	for (idx = 0; idx < 4; idx++)
		rtw_write8(rtwdev, box_reg + idx, h2c[idx]);
	for (idx = 0; idx < 4; idx++)
		rtw_write8(rtwdev, box_ex_reg + idx, h2c[idx + 4]);

	if (++rtwdev->h2c.last_box_num >= 4)
		rtwdev->h2c.last_box_num = 0;

out:
	spin_unlock(&rtwdev->h2c.lock);
}

void rtw_fw_h2c_cmd_dbg(struct rtw_dev *rtwdev, u8 *h2c)
{
	rtw_fw_send_h2c_command(rtwdev, h2c);
}

static void rtw_fw_send_h2c_packet(struct rtw_dev *rtwdev, u8 *h2c_pkt)
{
	int ret;

	spin_lock(&rtwdev->h2c.lock);

	FW_OFFLOAD_H2C_SET_SEQ_NUM(h2c_pkt, rtwdev->h2c.seq);
	ret = rtw_hci_write_data_h2c(rtwdev, h2c_pkt, H2C_PKT_SIZE);
	if (ret)
		rtw_err(rtwdev, "failed to send h2c packet\n");
	rtwdev->h2c.seq++;

	spin_unlock(&rtwdev->h2c.lock);
}

void
rtw_fw_send_general_info(struct rtw_dev *rtwdev)
{
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u16 total_size = H2C_PKT_HDR_SIZE + 4;

	if (rtw_chip_wcpu_11n(rtwdev))
		return;

	rtw_h2c_pkt_set_header(h2c_pkt, H2C_PKT_GENERAL_INFO);

	SET_PKT_H2C_TOTAL_LEN(h2c_pkt, total_size);

	GENERAL_INFO_SET_FW_TX_BOUNDARY(h2c_pkt,
					fifo->rsvd_fw_txbuf_addr -
					fifo->rsvd_boundary);

	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt);
}

void
rtw_fw_send_phydm_info(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u16 total_size = H2C_PKT_HDR_SIZE + 8;
	u8 fw_rf_type = 0;

	if (rtw_chip_wcpu_11n(rtwdev))
		return;

	if (hal->rf_type == RF_1T1R)
		fw_rf_type = FW_RF_1T1R;
	else if (hal->rf_type == RF_2T2R)
		fw_rf_type = FW_RF_2T2R;

	rtw_h2c_pkt_set_header(h2c_pkt, H2C_PKT_PHYDM_INFO);

	SET_PKT_H2C_TOTAL_LEN(h2c_pkt, total_size);
	PHYDM_INFO_SET_REF_TYPE(h2c_pkt, efuse->rfe_option);
	PHYDM_INFO_SET_RF_TYPE(h2c_pkt, fw_rf_type);
	PHYDM_INFO_SET_CUT_VER(h2c_pkt, hal->cut_version);
	PHYDM_INFO_SET_RX_ANT_STATUS(h2c_pkt, hal->antenna_tx);
	PHYDM_INFO_SET_TX_ANT_STATUS(h2c_pkt, hal->antenna_rx);

	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt);
}

void rtw_fw_do_iqk(struct rtw_dev *rtwdev, struct rtw_iqk_para *para)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u16 total_size = H2C_PKT_HDR_SIZE + 1;

	rtw_h2c_pkt_set_header(h2c_pkt, H2C_PKT_IQK);
	SET_PKT_H2C_TOTAL_LEN(h2c_pkt, total_size);
	IQK_SET_CLEAR(h2c_pkt, para->clear);
	IQK_SET_SEGMENT_IQK(h2c_pkt, para->segment_iqk);

	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt);
}
EXPORT_SYMBOL(rtw_fw_do_iqk);

void rtw_fw_query_bt_info(struct rtw_dev *rtwdev)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_QUERY_BT_INFO);

	SET_QUERY_BT_INFO(h2c_pkt, true);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_wl_ch_info(struct rtw_dev *rtwdev, u8 link, u8 ch, u8 bw)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_WL_CH_INFO);

	SET_WL_CH_INFO_LINK(h2c_pkt, link);
	SET_WL_CH_INFO_CHNL(h2c_pkt, ch);
	SET_WL_CH_INFO_BW(h2c_pkt, bw);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_query_bt_mp_info(struct rtw_dev *rtwdev,
			     struct rtw_coex_info_req *req)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_QUERY_BT_MP_INFO);

	SET_BT_MP_INFO_SEQ(h2c_pkt, req->seq);
	SET_BT_MP_INFO_OP_CODE(h2c_pkt, req->op_code);
	SET_BT_MP_INFO_PARA1(h2c_pkt, req->para1);
	SET_BT_MP_INFO_PARA2(h2c_pkt, req->para2);
	SET_BT_MP_INFO_PARA3(h2c_pkt, req->para3);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_force_bt_tx_power(struct rtw_dev *rtwdev, u8 bt_pwr_dec_lvl)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u8 index = 0 - bt_pwr_dec_lvl;

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_FORCE_BT_TX_POWER);

	SET_BT_TX_POWER_INDEX(h2c_pkt, index);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_bt_ignore_wlan_action(struct rtw_dev *rtwdev, bool enable)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_IGNORE_WLAN_ACTION);

	SET_IGNORE_WLAN_ACTION_EN(h2c_pkt, enable);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_coex_tdma_type(struct rtw_dev *rtwdev,
			   u8 para1, u8 para2, u8 para3, u8 para4, u8 para5)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_COEX_TDMA_TYPE);

	SET_COEX_TDMA_TYPE_PARA1(h2c_pkt, para1);
	SET_COEX_TDMA_TYPE_PARA2(h2c_pkt, para2);
	SET_COEX_TDMA_TYPE_PARA3(h2c_pkt, para3);
	SET_COEX_TDMA_TYPE_PARA4(h2c_pkt, para4);
	SET_COEX_TDMA_TYPE_PARA5(h2c_pkt, para5);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_bt_wifi_control(struct rtw_dev *rtwdev, u8 op_code, u8 *data)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_BT_WIFI_CONTROL);

	SET_BT_WIFI_CONTROL_OP_CODE(h2c_pkt, op_code);

	SET_BT_WIFI_CONTROL_DATA1(h2c_pkt, *data);
	SET_BT_WIFI_CONTROL_DATA2(h2c_pkt, *(data + 1));
	SET_BT_WIFI_CONTROL_DATA3(h2c_pkt, *(data + 2));
	SET_BT_WIFI_CONTROL_DATA4(h2c_pkt, *(data + 3));
	SET_BT_WIFI_CONTROL_DATA5(h2c_pkt, *(data + 4));

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_send_rssi_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u8 rssi = ewma_rssi_read(&si->avg_rssi);
	bool stbc_en = si->stbc_en ? true : false;

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_RSSI_MONITOR);

	SET_RSSI_INFO_MACID(h2c_pkt, si->mac_id);
	SET_RSSI_INFO_RSSI(h2c_pkt, rssi);
	SET_RSSI_INFO_STBC(h2c_pkt, stbc_en);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_send_ra_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	bool no_update = si->updated;
	bool disable_pt = true;

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_RA_INFO);

	SET_RA_INFO_MACID(h2c_pkt, si->mac_id);
	SET_RA_INFO_RATE_ID(h2c_pkt, si->rate_id);
	SET_RA_INFO_INIT_RA_LVL(h2c_pkt, si->init_ra_lv);
	SET_RA_INFO_SGI_EN(h2c_pkt, si->sgi_enable);
	SET_RA_INFO_BW_MODE(h2c_pkt, si->bw_mode);
	SET_RA_INFO_LDPC(h2c_pkt, !!si->ldpc_en);
	SET_RA_INFO_NO_UPDATE(h2c_pkt, no_update);
	SET_RA_INFO_VHT_EN(h2c_pkt, si->vht_enable);
	SET_RA_INFO_DIS_PT(h2c_pkt, disable_pt);
	SET_RA_INFO_RA_MASK0(h2c_pkt, (si->ra_mask & 0xff));
	SET_RA_INFO_RA_MASK1(h2c_pkt, (si->ra_mask & 0xff00) >> 8);
	SET_RA_INFO_RA_MASK2(h2c_pkt, (si->ra_mask & 0xff0000) >> 16);
	SET_RA_INFO_RA_MASK3(h2c_pkt, (si->ra_mask & 0xff000000) >> 24);

	si->init_ra_lv = 0;
	si->updated = true;

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_media_status_report(struct rtw_dev *rtwdev, u8 mac_id, bool connect)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_MEDIA_STATUS_RPT);
	MEDIA_STATUS_RPT_SET_OP_MODE(h2c_pkt, connect);
	MEDIA_STATUS_RPT_SET_MACID(h2c_pkt, mac_id);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_pwr_mode(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_SET_PWR_MODE);

	SET_PWR_MODE_SET_MODE(h2c_pkt, conf->mode);
	SET_PWR_MODE_SET_RLBM(h2c_pkt, conf->rlbm);
	SET_PWR_MODE_SET_SMART_PS(h2c_pkt, conf->smart_ps);
	SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c_pkt, conf->awake_interval);
	SET_PWR_MODE_SET_PORT_ID(h2c_pkt, conf->port_id);
	SET_PWR_MODE_SET_PWR_STATE(h2c_pkt, conf->state);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_keep_alive_cmd(struct rtw_dev *rtwdev, bool enable)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	struct rtw_fw_wow_keep_alive_para mode = {
		.adopt = true,
		.pkt_type = KEEP_ALIVE_NULL_PKT,
		.period = 5,
	};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_KEEP_ALIVE);
	SET_KEEP_ALIVE_ENABLE(h2c_pkt, enable);
	SET_KEEP_ALIVE_ADOPT(h2c_pkt, mode.adopt);
	SET_KEEP_ALIVE_PKT_TYPE(h2c_pkt, mode.pkt_type);
	SET_KEEP_ALIVE_CHECK_PERIOD(h2c_pkt, mode.period);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_disconnect_decision_cmd(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	struct rtw_fw_wow_disconnect_para mode = {
		.adopt = true,
		.period = 30,
		.retry_count = 5,
	};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_DISCONNECT_DECISION);

	if (test_bit(RTW_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags)) {
		SET_DISCONNECT_DECISION_ENABLE(h2c_pkt, enable);
		SET_DISCONNECT_DECISION_ADOPT(h2c_pkt, mode.adopt);
		SET_DISCONNECT_DECISION_CHECK_PERIOD(h2c_pkt, mode.period);
		SET_DISCONNECT_DECISION_TRY_PKT_NUM(h2c_pkt, mode.retry_count);
	}

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_wowlan_ctrl_cmd(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_WOWLAN);

	SET_WOWLAN_FUNC_ENABLE(h2c_pkt, enable);
	if (rtw_wow_mgd_linked(rtwdev)) {
		if (test_bit(RTW_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags))
			SET_WOWLAN_MAGIC_PKT_ENABLE(h2c_pkt, enable);
		if (test_bit(RTW_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags))
			SET_WOWLAN_DEAUTH_WAKEUP_ENABLE(h2c_pkt, enable);
		if (test_bit(RTW_WOW_FLAG_EN_REKEY_PKT, rtw_wow->flags))
			SET_WOWLAN_REKEY_WAKEUP_ENABLE(h2c_pkt, enable);
		if (rtw_wow->pattern_cnt)
			SET_WOWLAN_PATTERN_MATCH_ENABLE(h2c_pkt, enable);
	}

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_aoac_global_info_cmd(struct rtw_dev *rtwdev,
				     u8 pairwise_key_enc,
				     u8 group_key_enc)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_AOAC_GLOBAL_INFO);

	SET_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(h2c_pkt, pairwise_key_enc);
	SET_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(h2c_pkt, group_key_enc);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_remote_wake_ctrl_cmd(struct rtw_dev *rtwdev, bool enable)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_REMOTE_WAKE_CTRL);

	SET_REMOTE_WAKECTRL_ENABLE(h2c_pkt, enable);

	if (rtw_wow_no_link(rtwdev))
		SET_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(h2c_pkt, enable);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

static u8 rtw_get_rsvd_page_location(struct rtw_dev *rtwdev,
				     enum rtw_rsvd_packet_type type)
{
	struct rtw_rsvd_page *rsvd_pkt;
	u8 location = 0;

	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		if (type == rsvd_pkt->type)
			location = rsvd_pkt->page;
	}

	return location;
}

void rtw_fw_set_nlo_info(struct rtw_dev *rtwdev, bool enable)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u8 loc_nlo;

	loc_nlo = rtw_get_rsvd_page_location(rtwdev, RSVD_NLO_INFO);

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_NLO_INFO);

	SET_NLO_FUN_EN(h2c_pkt, enable);
	if (enable) {
		if (rtw_fw_lps_deep_mode)
			SET_NLO_PS_32K(h2c_pkt, enable);
		SET_NLO_IGNORE_SECURITY(h2c_pkt, enable);
		SET_NLO_LOC_NLO_INFO(h2c_pkt, loc_nlo);
	}

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

void rtw_fw_set_pg_info(struct rtw_dev *rtwdev)
{
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u8 loc_pg, loc_dpk;

	loc_pg = rtw_get_rsvd_page_location(rtwdev, RSVD_LPS_PG_INFO);
	loc_dpk = rtw_get_rsvd_page_location(rtwdev, RSVD_LPS_PG_DPK);

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_LPS_PG_INFO);

	LPS_PG_INFO_LOC(h2c_pkt, loc_pg);
	LPS_PG_DPK_LOC(h2c_pkt, loc_dpk);
	LPS_PG_SEC_CAM_EN(h2c_pkt, conf->sec_cam_backup);
	LPS_PG_PATTERN_CAM_EN(h2c_pkt, conf->pattern_cam_backup);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

static u8 rtw_get_rsvd_page_probe_req_location(struct rtw_dev *rtwdev,
					       struct cfg80211_ssid *ssid)
{
	struct rtw_rsvd_page *rsvd_pkt;
	u8 location = 0;

	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		if (rsvd_pkt->type != RSVD_PROBE_REQ)
			continue;
		if ((!ssid && !rsvd_pkt->ssid) ||
		    rtw_ssid_equal(rsvd_pkt->ssid, ssid))
			location = rsvd_pkt->page;
	}

	return location;
}

static u16 rtw_get_rsvd_page_probe_req_size(struct rtw_dev *rtwdev,
					    struct cfg80211_ssid *ssid)
{
	struct rtw_rsvd_page *rsvd_pkt;
	u16 size = 0;

	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		if (rsvd_pkt->type != RSVD_PROBE_REQ)
			continue;
		if ((!ssid && !rsvd_pkt->ssid) ||
		    rtw_ssid_equal(rsvd_pkt->ssid, ssid))
			size = rsvd_pkt->skb->len;
	}

	return size;
}

void rtw_send_rsvd_page_h2c(struct rtw_dev *rtwdev)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u8 location = 0;

	SET_H2C_CMD_ID_CLASS(h2c_pkt, H2C_CMD_RSVD_PAGE);

	location = rtw_get_rsvd_page_location(rtwdev, RSVD_PROBE_RESP);
	*(h2c_pkt + 1) = location;
	rtw_dbg(rtwdev, RTW_DBG_FW, "RSVD_PROBE_RESP loc: %d\n", location);

	location = rtw_get_rsvd_page_location(rtwdev, RSVD_PS_POLL);
	*(h2c_pkt + 2) = location;
	rtw_dbg(rtwdev, RTW_DBG_FW, "RSVD_PS_POLL loc: %d\n", location);

	location = rtw_get_rsvd_page_location(rtwdev, RSVD_NULL);
	*(h2c_pkt + 3) = location;
	rtw_dbg(rtwdev, RTW_DBG_FW, "RSVD_NULL loc: %d\n", location);

	location = rtw_get_rsvd_page_location(rtwdev, RSVD_QOS_NULL);
	*(h2c_pkt + 4) = location;
	rtw_dbg(rtwdev, RTW_DBG_FW, "RSVD_QOS_NULL loc: %d\n", location);

	rtw_fw_send_h2c_command(rtwdev, h2c_pkt);
}

static struct sk_buff *rtw_nlo_info_get(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_pno_request *pno_req = &rtwdev->wow.pno_req;
	struct rtw_nlo_info_hdr *nlo_hdr;
	struct cfg80211_ssid *ssid;
	struct sk_buff *skb;
	u8 *pos, loc;
	u32 size;
	int i;

	if (!pno_req->inited || !pno_req->match_set_cnt)
		return NULL;

	size = sizeof(struct rtw_nlo_info_hdr) + pno_req->match_set_cnt *
		      IEEE80211_MAX_SSID_LEN + chip->tx_pkt_desc_sz;

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, chip->tx_pkt_desc_sz);

	nlo_hdr = skb_put_zero(skb, sizeof(struct rtw_nlo_info_hdr));

	nlo_hdr->nlo_count = pno_req->match_set_cnt;
	nlo_hdr->hidden_ap_count = pno_req->match_set_cnt;

	/* pattern check for firmware */
	memset(nlo_hdr->pattern_check, 0xA5, FW_NLO_INFO_CHECK_SIZE);

	for (i = 0; i < pno_req->match_set_cnt; i++)
		nlo_hdr->ssid_len[i] = pno_req->match_sets[i].ssid.ssid_len;

	for (i = 0; i < pno_req->match_set_cnt; i++) {
		ssid = &pno_req->match_sets[i].ssid;
		loc  = rtw_get_rsvd_page_probe_req_location(rtwdev, ssid);
		if (!loc) {
			rtw_err(rtwdev, "failed to get probe req rsvd loc\n");
			kfree_skb(skb);
			return NULL;
		}
		nlo_hdr->location[i] = loc;
	}

	for (i = 0; i < pno_req->match_set_cnt; i++) {
		pos = skb_put_zero(skb, IEEE80211_MAX_SSID_LEN);
		memcpy(pos, pno_req->match_sets[i].ssid.ssid,
		       pno_req->match_sets[i].ssid.ssid_len);
	}

	return skb;
}

static struct sk_buff *rtw_cs_channel_info_get(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_pno_request *pno_req = &rtwdev->wow.pno_req;
	struct ieee80211_channel *channels = pno_req->channels;
	struct sk_buff *skb;
	int count =  pno_req->channel_cnt;
	u8 *pos;
	int i = 0;

	skb = alloc_skb(4 * count + chip->tx_pkt_desc_sz, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, chip->tx_pkt_desc_sz);

	for (i = 0; i < count; i++) {
		pos = skb_put_zero(skb, 4);

		CHSW_INFO_SET_CH(pos, channels[i].hw_value);

		if (channels[i].flags & IEEE80211_CHAN_RADAR)
			CHSW_INFO_SET_ACTION_ID(pos, 0);
		else
			CHSW_INFO_SET_ACTION_ID(pos, 1);
		CHSW_INFO_SET_TIMEOUT(pos, 1);
		CHSW_INFO_SET_PRI_CH_IDX(pos, 1);
		CHSW_INFO_SET_BW(pos, 0);
	}

	return skb;
}

static struct sk_buff *rtw_lps_pg_dpk_get(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	struct rtw_lps_pg_dpk_hdr *dpk_hdr;
	struct sk_buff *skb;
	u32 size;

	size = chip->tx_pkt_desc_sz + sizeof(*dpk_hdr);
	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, chip->tx_pkt_desc_sz);
	dpk_hdr = skb_put_zero(skb, sizeof(*dpk_hdr));
	dpk_hdr->dpk_ch = dpk_info->dpk_ch;
	dpk_hdr->dpk_path_ok = dpk_info->dpk_path_ok[0];
	memcpy(dpk_hdr->dpk_txagc, dpk_info->dpk_txagc, 2);
	memcpy(dpk_hdr->dpk_gs, dpk_info->dpk_gs, 4);
	memcpy(dpk_hdr->coef, dpk_info->coef, 160);

	return skb;
}

static struct sk_buff *rtw_lps_pg_info_get(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_lps_conf *conf = &rtwdev->lps_conf;
	struct rtw_lps_pg_info_hdr *pg_info_hdr;
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	struct sk_buff *skb;
	u32 size;

	size = chip->tx_pkt_desc_sz + sizeof(*pg_info_hdr);
	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, chip->tx_pkt_desc_sz);
	pg_info_hdr = skb_put_zero(skb, sizeof(*pg_info_hdr));
	pg_info_hdr->tx_bu_page_count = rtwdev->fifo.rsvd_drv_pg_num;
	pg_info_hdr->macid = find_first_bit(rtwdev->mac_id_map, RTW_MAX_MAC_ID_NUM);
	pg_info_hdr->sec_cam_count =
		rtw_sec_cam_pg_backup(rtwdev, pg_info_hdr->sec_cam);
	pg_info_hdr->pattern_count = rtw_wow->pattern_cnt;

	conf->sec_cam_backup = pg_info_hdr->sec_cam_count != 0;
	conf->pattern_cam_backup = rtw_wow->pattern_cnt != 0;

	return skb;
}

static struct sk_buff *rtw_get_rsvd_page_skb(struct ieee80211_hw *hw,
					     struct rtw_rsvd_page *rsvd_pkt)
{
	struct ieee80211_vif *vif;
	struct rtw_vif *rtwvif;
	struct sk_buff *skb_new;
	struct cfg80211_ssid *ssid;

	if (rsvd_pkt->type == RSVD_DUMMY) {
		skb_new = alloc_skb(1, GFP_KERNEL);
		if (!skb_new)
			return NULL;

		skb_put(skb_new, 1);
		return skb_new;
	}

	rtwvif = rsvd_pkt->rtwvif;
	if (!rtwvif)
		return NULL;

	vif = rtwvif_to_vif(rtwvif);

	switch (rsvd_pkt->type) {
	case RSVD_BEACON:
		skb_new = ieee80211_beacon_get(hw, vif);
		break;
	case RSVD_PS_POLL:
		skb_new = ieee80211_pspoll_get(hw, vif);
		break;
	case RSVD_PROBE_RESP:
		skb_new = ieee80211_proberesp_get(hw, vif);
		break;
	case RSVD_NULL:
		skb_new = ieee80211_nullfunc_get(hw, vif, false);
		break;
	case RSVD_QOS_NULL:
		skb_new = ieee80211_nullfunc_get(hw, vif, true);
		break;
	case RSVD_LPS_PG_DPK:
		skb_new = rtw_lps_pg_dpk_get(hw);
		break;
	case RSVD_LPS_PG_INFO:
		skb_new = rtw_lps_pg_info_get(hw);
		break;
	case RSVD_PROBE_REQ:
		ssid = (struct cfg80211_ssid *)rsvd_pkt->ssid;
		if (ssid)
			skb_new = ieee80211_probereq_get(hw, vif->addr,
							 ssid->ssid,
							 ssid->ssid_len, 0);
		else
			skb_new = ieee80211_probereq_get(hw, vif->addr, NULL, 0, 0);
		break;
	case RSVD_NLO_INFO:
		skb_new = rtw_nlo_info_get(hw);
		break;
	case RSVD_CH_INFO:
		skb_new = rtw_cs_channel_info_get(hw);
		break;
	default:
		return NULL;
	}

	if (!skb_new)
		return NULL;

	return skb_new;
}

static void rtw_fill_rsvd_page_desc(struct rtw_dev *rtwdev, struct sk_buff *skb,
				    enum rtw_rsvd_packet_type type)
{
	struct rtw_tx_pkt_info pkt_info = {0};
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 *pkt_desc;

	rtw_tx_rsvd_page_pkt_info_update(rtwdev, &pkt_info, skb, type);
	pkt_desc = skb_push(skb, chip->tx_pkt_desc_sz);
	memset(pkt_desc, 0, chip->tx_pkt_desc_sz);
	rtw_tx_fill_tx_desc(&pkt_info, skb);
}

static inline u8 rtw_len_to_page(unsigned int len, u8 page_size)
{
	return DIV_ROUND_UP(len, page_size);
}

static void rtw_rsvd_page_list_to_buf(struct rtw_dev *rtwdev, u8 page_size,
				      u8 page_margin, u32 page, u8 *buf,
				      struct rtw_rsvd_page *rsvd_pkt)
{
	struct sk_buff *skb = rsvd_pkt->skb;

	if (page >= 1)
		memcpy(buf + page_margin + page_size * (page - 1),
		       skb->data, skb->len);
	else
		memcpy(buf, skb->data, skb->len);
}

static struct rtw_rsvd_page *rtw_alloc_rsvd_page(struct rtw_dev *rtwdev,
						 enum rtw_rsvd_packet_type type,
						 bool txdesc)
{
	struct rtw_rsvd_page *rsvd_pkt = NULL;

	rsvd_pkt = kzalloc(sizeof(*rsvd_pkt), GFP_KERNEL);

	if (!rsvd_pkt)
		return NULL;

	INIT_LIST_HEAD(&rsvd_pkt->vif_list);
	INIT_LIST_HEAD(&rsvd_pkt->build_list);
	rsvd_pkt->type = type;
	rsvd_pkt->add_txdesc = txdesc;

	return rsvd_pkt;
}

static void rtw_insert_rsvd_page(struct rtw_dev *rtwdev,
				 struct rtw_vif *rtwvif,
				 struct rtw_rsvd_page *rsvd_pkt)
{
	lockdep_assert_held(&rtwdev->mutex);

	list_add_tail(&rsvd_pkt->vif_list, &rtwvif->rsvd_page_list);
}

static void rtw_add_rsvd_page(struct rtw_dev *rtwdev,
			      struct rtw_vif *rtwvif,
			      enum rtw_rsvd_packet_type type,
			      bool txdesc)
{
	struct rtw_rsvd_page *rsvd_pkt;

	rsvd_pkt = rtw_alloc_rsvd_page(rtwdev, type, txdesc);
	if (!rsvd_pkt) {
		rtw_err(rtwdev, "failed to alloc rsvd page %d\n", type);
		return;
	}

	rsvd_pkt->rtwvif = rtwvif;
	rtw_insert_rsvd_page(rtwdev, rtwvif, rsvd_pkt);
}

static void rtw_add_rsvd_page_probe_req(struct rtw_dev *rtwdev,
					struct rtw_vif *rtwvif,
					struct cfg80211_ssid *ssid)
{
	struct rtw_rsvd_page *rsvd_pkt;

	rsvd_pkt = rtw_alloc_rsvd_page(rtwdev, RSVD_PROBE_REQ, true);
	if (!rsvd_pkt) {
		rtw_err(rtwdev, "failed to alloc probe req rsvd page\n");
		return;
	}

	rsvd_pkt->rtwvif = rtwvif;
	rsvd_pkt->ssid = ssid;
	rtw_insert_rsvd_page(rtwdev, rtwvif, rsvd_pkt);
}

void rtw_remove_rsvd_page(struct rtw_dev *rtwdev,
			  struct rtw_vif *rtwvif)
{
	struct rtw_rsvd_page *rsvd_pkt, *tmp;

	lockdep_assert_held(&rtwdev->mutex);

	/* remove all of the rsvd pages for vif */
	list_for_each_entry_safe(rsvd_pkt, tmp, &rtwvif->rsvd_page_list,
				 vif_list) {
		list_del(&rsvd_pkt->vif_list);
		if (!list_empty(&rsvd_pkt->build_list))
			list_del(&rsvd_pkt->build_list);
		kfree(rsvd_pkt);
	}
}

void rtw_add_rsvd_page_bcn(struct rtw_dev *rtwdev,
			   struct rtw_vif *rtwvif)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC &&
	    vif->type != NL80211_IFTYPE_MESH_POINT) {
		rtw_warn(rtwdev, "Cannot add beacon rsvd page for %d\n",
			 vif->type);
		return;
	}

	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_BEACON, false);
}

void rtw_add_rsvd_page_pno(struct rtw_dev *rtwdev,
			   struct rtw_vif *rtwvif)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw_pno_request *rtw_pno_req = &rtw_wow->pno_req;
	struct cfg80211_ssid *ssid;
	int i;

	if (vif->type != NL80211_IFTYPE_STATION) {
		rtw_warn(rtwdev, "Cannot add PNO rsvd page for %d\n",
			 vif->type);
		return;
	}

	for (i = 0 ; i < rtw_pno_req->match_set_cnt; i++) {
		ssid = &rtw_pno_req->match_sets[i].ssid;
		rtw_add_rsvd_page_probe_req(rtwdev, rtwvif, ssid);
	}

	rtw_add_rsvd_page_probe_req(rtwdev, rtwvif, NULL);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_NLO_INFO, false);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_CH_INFO, true);
}

void rtw_add_rsvd_page_sta(struct rtw_dev *rtwdev,
			   struct rtw_vif *rtwvif)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);

	if (vif->type != NL80211_IFTYPE_STATION) {
		rtw_warn(rtwdev, "Cannot add sta rsvd page for %d\n",
			 vif->type);
		return;
	}

	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_PS_POLL, true);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_QOS_NULL, true);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_NULL, true);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_LPS_PG_DPK, true);
	rtw_add_rsvd_page(rtwdev, rtwvif, RSVD_LPS_PG_INFO, true);
}

int rtw_fw_write_data_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				u8 *buf, u32 size)
{
	u8 bckp[2];
	u8 val;
	u16 rsvd_pg_head;
	u32 bcn_valid_addr;
	u32 bcn_valid_mask;
	int ret;

	lockdep_assert_held(&rtwdev->mutex);

	if (!size)
		return -EINVAL;

	if (rtw_chip_wcpu_11n(rtwdev)) {
		rtw_write32_set(rtwdev, REG_DWBCN0_CTRL, BIT_BCN_VALID);
	} else {
		pg_addr &= BIT_MASK_BCN_HEAD_1_V1;
		pg_addr |= BIT_BCN_VALID_V1;
		rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2, pg_addr);
	}

	val = rtw_read8(rtwdev, REG_CR + 1);
	bckp[0] = val;
	val |= BIT_ENSWBCN >> 8;
	rtw_write8(rtwdev, REG_CR + 1, val);

	val = rtw_read8(rtwdev, REG_FWHW_TXQ_CTRL + 2);
	bckp[1] = val;
	val &= ~(BIT_EN_BCNQ_DL >> 16);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 2, val);

	ret = rtw_hci_write_data_rsvd_page(rtwdev, buf, size);
	if (ret) {
		rtw_err(rtwdev, "failed to write data to rsvd page\n");
		goto restore;
	}

	if (rtw_chip_wcpu_11n(rtwdev)) {
		bcn_valid_addr = REG_DWBCN0_CTRL;
		bcn_valid_mask = BIT_BCN_VALID;
	} else {
		bcn_valid_addr = REG_FIFOPAGE_CTRL_2;
		bcn_valid_mask = BIT_BCN_VALID_V1;
	}

	if (!check_hw_ready(rtwdev, bcn_valid_addr, bcn_valid_mask, 1)) {
		rtw_err(rtwdev, "error beacon valid\n");
		ret = -EBUSY;
	}

restore:
	rsvd_pg_head = rtwdev->fifo.rsvd_boundary;
	rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2,
		    rsvd_pg_head | BIT_BCN_VALID_V1);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 2, bckp[1]);
	rtw_write8(rtwdev, REG_CR + 1, bckp[0]);

	return ret;
}

static int rtw_download_drv_rsvd_page(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	u32 pg_size;
	u32 pg_num = 0;
	u16 pg_addr = 0;

	pg_size = rtwdev->chip->page_size;
	pg_num = size / pg_size + ((size & (pg_size - 1)) ? 1 : 0);
	if (pg_num > rtwdev->fifo.rsvd_drv_pg_num)
		return -ENOMEM;

	pg_addr = rtwdev->fifo.rsvd_drv_addr;

	return rtw_fw_write_data_rsvd_page(rtwdev, pg_addr, buf, size);
}

static void __rtw_build_rsvd_page_reset(struct rtw_dev *rtwdev)
{
	struct rtw_rsvd_page *rsvd_pkt, *tmp;

	list_for_each_entry_safe(rsvd_pkt, tmp, &rtwdev->rsvd_page_list,
				 build_list) {
		list_del_init(&rsvd_pkt->build_list);

		/* Don't free except for the dummy rsvd page,
		 * others will be freed when removing vif
		 */
		if (rsvd_pkt->type == RSVD_DUMMY)
			kfree(rsvd_pkt);
	}
}

static void rtw_build_rsvd_page_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_rsvd_page *rsvd_pkt;

	list_for_each_entry(rsvd_pkt, &rtwvif->rsvd_page_list, vif_list) {
		if (rsvd_pkt->type == RSVD_BEACON)
			list_add(&rsvd_pkt->build_list,
				 &rtwdev->rsvd_page_list);
		else
			list_add_tail(&rsvd_pkt->build_list,
				      &rtwdev->rsvd_page_list);
	}
}

static int  __rtw_build_rsvd_page_from_vifs(struct rtw_dev *rtwdev)
{
	struct rtw_rsvd_page *rsvd_pkt;

	__rtw_build_rsvd_page_reset(rtwdev);

	/* gather rsvd page from vifs */
	rtw_iterate_vifs_atomic(rtwdev, rtw_build_rsvd_page_iter, rtwdev);

	rsvd_pkt = list_first_entry_or_null(&rtwdev->rsvd_page_list,
					    struct rtw_rsvd_page, build_list);
	if (!rsvd_pkt) {
		WARN(1, "Should not have an empty reserved page\n");
		return -EINVAL;
	}

	/* the first rsvd should be beacon, otherwise add a dummy one */
	if (rsvd_pkt->type != RSVD_BEACON) {
		struct rtw_rsvd_page *dummy_pkt;

		dummy_pkt = rtw_alloc_rsvd_page(rtwdev, RSVD_DUMMY, false);
		if (!dummy_pkt) {
			rtw_err(rtwdev, "failed to alloc dummy rsvd page\n");
			return -ENOMEM;
		}

		list_add(&dummy_pkt->build_list, &rtwdev->rsvd_page_list);
	}

	return 0;
}

static u8 *rtw_build_rsvd_page(struct rtw_dev *rtwdev, u32 *size)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct sk_buff *iter;
	struct rtw_rsvd_page *rsvd_pkt;
	u32 page = 0;
	u8 total_page = 0;
	u8 page_size, page_margin, tx_desc_sz;
	u8 *buf;
	int ret;

	page_size = chip->page_size;
	tx_desc_sz = chip->tx_pkt_desc_sz;
	page_margin = page_size - tx_desc_sz;

	ret = __rtw_build_rsvd_page_from_vifs(rtwdev);
	if (ret) {
		rtw_err(rtwdev,
			"failed to build rsvd page from vifs, ret %d\n", ret);
		return NULL;
	}

	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		iter = rtw_get_rsvd_page_skb(hw, rsvd_pkt);
		if (!iter) {
			rtw_err(rtwdev, "failed to build rsvd packet\n");
			goto release_skb;
		}

		/* Fill the tx_desc for the rsvd pkt that requires one.
		 * And iter->len will be added with size of tx_desc_sz.
		 */
		if (rsvd_pkt->add_txdesc)
			rtw_fill_rsvd_page_desc(rtwdev, iter, rsvd_pkt->type);

		rsvd_pkt->skb = iter;
		rsvd_pkt->page = total_page;

		/* Reserved page is downloaded via TX path, and TX path will
		 * generate a tx_desc at the header to describe length of
		 * the buffer. If we are not counting page numbers with the
		 * size of tx_desc added at the first rsvd_pkt (usually a
		 * beacon, firmware default refer to the first page as the
		 * content of beacon), we could generate a buffer which size
		 * is smaller than the actual size of the whole rsvd_page
		 */
		if (total_page == 0) {
			if (rsvd_pkt->type != RSVD_BEACON &&
			    rsvd_pkt->type != RSVD_DUMMY) {
				rtw_err(rtwdev, "first page should be a beacon\n");
				goto release_skb;
			}
			total_page += rtw_len_to_page(iter->len + tx_desc_sz,
						      page_size);
		} else {
			total_page += rtw_len_to_page(iter->len, page_size);
		}
	}

	if (total_page > rtwdev->fifo.rsvd_drv_pg_num) {
		rtw_err(rtwdev, "rsvd page over size: %d\n", total_page);
		goto release_skb;
	}

	*size = (total_page - 1) * page_size + page_margin;
	buf = kzalloc(*size, GFP_KERNEL);
	if (!buf)
		goto release_skb;

	/* Copy the content of each rsvd_pkt to the buf, and they should
	 * be aligned to the pages.
	 *
	 * Note that the first rsvd_pkt is a beacon no matter what vif->type.
	 * And that rsvd_pkt does not require tx_desc because when it goes
	 * through TX path, the TX path will generate one for it.
	 */
	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		rtw_rsvd_page_list_to_buf(rtwdev, page_size, page_margin,
					  page, buf, rsvd_pkt);
		if (page == 0)
			page += rtw_len_to_page(rsvd_pkt->skb->len +
						tx_desc_sz, page_size);
		else
			page += rtw_len_to_page(rsvd_pkt->skb->len, page_size);

		kfree_skb(rsvd_pkt->skb);
		rsvd_pkt->skb = NULL;
	}

	return buf;

release_skb:
	list_for_each_entry(rsvd_pkt, &rtwdev->rsvd_page_list, build_list) {
		kfree_skb(rsvd_pkt->skb);
		rsvd_pkt->skb = NULL;
	}

	return NULL;
}

static int rtw_download_beacon(struct rtw_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_rsvd_page *rsvd_pkt;
	struct sk_buff *skb;
	int ret = 0;

	rsvd_pkt = list_first_entry_or_null(&rtwdev->rsvd_page_list,
					    struct rtw_rsvd_page, build_list);
	if (!rsvd_pkt) {
		rtw_err(rtwdev, "failed to get rsvd page from build list\n");
		return -ENOENT;
	}

	if (rsvd_pkt->type != RSVD_BEACON &&
	    rsvd_pkt->type != RSVD_DUMMY) {
		rtw_err(rtwdev, "invalid rsvd page type %d, should be beacon or dummy\n",
			rsvd_pkt->type);
		return -EINVAL;
	}

	skb = rtw_get_rsvd_page_skb(hw, rsvd_pkt);
	if (!skb) {
		rtw_err(rtwdev, "failed to get beacon skb\n");
		return -ENOMEM;
	}

	ret = rtw_download_drv_rsvd_page(rtwdev, skb->data, skb->len);
	if (ret)
		rtw_err(rtwdev, "failed to download drv rsvd page\n");

	dev_kfree_skb(skb);

	return ret;
}

int rtw_fw_download_rsvd_page(struct rtw_dev *rtwdev)
{
	u8 *buf;
	u32 size;
	int ret;

	buf = rtw_build_rsvd_page(rtwdev, &size);
	if (!buf) {
		rtw_err(rtwdev, "failed to build rsvd page pkt\n");
		return -ENOMEM;
	}

	ret = rtw_download_drv_rsvd_page(rtwdev, buf, size);
	if (ret) {
		rtw_err(rtwdev, "failed to download drv rsvd page\n");
		goto free;
	}

	/* The last thing is to download the *ONLY* beacon again, because
	 * the previous tx_desc is to describe the total rsvd page. Download
	 * the beacon again to replace the TX desc header, and we will get
	 * a correct tx_desc for the beacon in the rsvd page.
	 */
	ret = rtw_download_beacon(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to download beacon\n");
		goto free;
	}

free:
	kfree(buf);

	return ret;
}

static void rtw_fw_read_fifo_page(struct rtw_dev *rtwdev, u32 offset, u32 size,
				  u32 *buf, u32 residue, u16 start_pg)
{
	u32 i;
	u16 idx = 0;
	u16 ctl;
	u8 rcr;

	rcr = rtw_read8(rtwdev, REG_RCR + 2);
	ctl = rtw_read16(rtwdev, REG_PKTBUF_DBG_CTRL) & 0xf000;
	/* disable rx clock gate */
	rtw_write8(rtwdev, REG_RCR, rcr | BIT(3));

	do {
		rtw_write16(rtwdev, REG_PKTBUF_DBG_CTRL, start_pg | ctl);

		for (i = FIFO_DUMP_ADDR + residue;
		     i < FIFO_DUMP_ADDR + FIFO_PAGE_SIZE; i += 4) {
			buf[idx++] = rtw_read32(rtwdev, i);
			size -= 4;
			if (size == 0)
				goto out;
		}

		residue = 0;
		start_pg++;
	} while (size);

out:
	rtw_write16(rtwdev, REG_PKTBUF_DBG_CTRL, ctl);
	rtw_write8(rtwdev, REG_RCR + 2, rcr);
}

static void rtw_fw_read_fifo(struct rtw_dev *rtwdev, enum rtw_fw_fifo_sel sel,
			     u32 offset, u32 size, u32 *buf)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	u32 start_pg, residue;

	if (sel >= RTW_FW_FIFO_MAX) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "wrong fw fifo sel\n");
		return;
	}
	if (sel == RTW_FW_FIFO_SEL_RSVD_PAGE)
		offset += rtwdev->fifo.rsvd_boundary << TX_PAGE_SIZE_SHIFT;
	residue = offset & (FIFO_PAGE_SIZE - 1);
	start_pg = (offset >> FIFO_PAGE_SIZE_SHIFT) + chip->fw_fifo_addr[sel];

	rtw_fw_read_fifo_page(rtwdev, offset, size, buf, residue, start_pg);
}

static bool rtw_fw_dump_check_size(struct rtw_dev *rtwdev,
				   enum rtw_fw_fifo_sel sel,
				   u32 start_addr, u32 size)
{
	switch (sel) {
	case RTW_FW_FIFO_SEL_TX:
	case RTW_FW_FIFO_SEL_RX:
		if ((start_addr + size) > rtwdev->chip->fw_fifo_addr[sel])
			return false;
		/*fall through*/
	default:
		return true;
	}
}

int rtw_fw_dump_fifo(struct rtw_dev *rtwdev, u8 fifo_sel, u32 addr, u32 size,
		     u32 *buffer)
{
	if (!rtwdev->chip->fw_fifo_addr[0]) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "chip not support dump fw fifo\n");
		return -ENOTSUPP;
	}

	if (size == 0 || !buffer)
		return -EINVAL;

	if (size & 0x3) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "not 4byte alignment\n");
		return -EINVAL;
	}

	if (!rtw_fw_dump_check_size(rtwdev, fifo_sel, addr, size)) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "fw fifo dump size overflow\n");
		return -EINVAL;
	}

	rtw_fw_read_fifo(rtwdev, fifo_sel, addr, size, buffer);

	return 0;
}

static void __rtw_fw_update_pkt(struct rtw_dev *rtwdev, u8 pkt_id, u16 size,
				u8 location)
{
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u16 total_size = H2C_PKT_HDR_SIZE + H2C_PKT_UPDATE_PKT_LEN;

	rtw_h2c_pkt_set_header(h2c_pkt, H2C_PKT_UPDATE_PKT);

	SET_PKT_H2C_TOTAL_LEN(h2c_pkt, total_size);
	UPDATE_PKT_SET_PKT_ID(h2c_pkt, pkt_id);
	UPDATE_PKT_SET_LOCATION(h2c_pkt, location);

	/* include txdesc size */
	UPDATE_PKT_SET_SIZE(h2c_pkt, size);

	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt);
}

void rtw_fw_update_pkt_probe_req(struct rtw_dev *rtwdev,
				 struct cfg80211_ssid *ssid)
{
	u8 loc;
	u32 size;

	loc = rtw_get_rsvd_page_probe_req_location(rtwdev, ssid);
	if (!loc) {
		rtw_err(rtwdev, "failed to get probe_req rsvd loc\n");
		return;
	}

	size = rtw_get_rsvd_page_probe_req_size(rtwdev, ssid);
	if (!size) {
		rtw_err(rtwdev, "failed to get probe_req rsvd size\n");
		return;
	}

	__rtw_fw_update_pkt(rtwdev, RTW_PACKET_PROBE_REQ, size, loc);
}

void rtw_fw_channel_switch(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_pno_request *rtw_pno_req = &rtwdev->wow.pno_req;
	u8 h2c_pkt[H2C_PKT_SIZE] = {0};
	u16 total_size = H2C_PKT_HDR_SIZE + H2C_PKT_CH_SWITCH_LEN;
	u8 loc_ch_info;
	const struct rtw_ch_switch_option cs_option = {
		.dest_ch_en = 1,
		.dest_ch = 1,
		.periodic_option = 2,
		.normal_period = 5,
		.normal_period_sel = 0,
		.normal_cycle = 10,
		.slow_period = 1,
		.slow_period_sel = 1,
	};

	rtw_h2c_pkt_set_header(h2c_pkt, H2C_PKT_CH_SWITCH);
	SET_PKT_H2C_TOTAL_LEN(h2c_pkt, total_size);

	CH_SWITCH_SET_START(h2c_pkt, enable);
	CH_SWITCH_SET_DEST_CH_EN(h2c_pkt, cs_option.dest_ch_en);
	CH_SWITCH_SET_DEST_CH(h2c_pkt, cs_option.dest_ch);
	CH_SWITCH_SET_NORMAL_PERIOD(h2c_pkt, cs_option.normal_period);
	CH_SWITCH_SET_NORMAL_PERIOD_SEL(h2c_pkt, cs_option.normal_period_sel);
	CH_SWITCH_SET_SLOW_PERIOD(h2c_pkt, cs_option.slow_period);
	CH_SWITCH_SET_SLOW_PERIOD_SEL(h2c_pkt, cs_option.slow_period_sel);
	CH_SWITCH_SET_NORMAL_CYCLE(h2c_pkt, cs_option.normal_cycle);
	CH_SWITCH_SET_PERIODIC_OPT(h2c_pkt, cs_option.periodic_option);

	CH_SWITCH_SET_CH_NUM(h2c_pkt, rtw_pno_req->channel_cnt);
	CH_SWITCH_SET_INFO_SIZE(h2c_pkt, rtw_pno_req->channel_cnt * 4);

	loc_ch_info = rtw_get_rsvd_page_location(rtwdev, RSVD_CH_INFO);
	CH_SWITCH_SET_INFO_LOC(h2c_pkt, loc_ch_info);

	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt);
}
