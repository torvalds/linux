// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/fs.h>
#include "mt7921.h"
#include "mt7921_trace.h"
#include "mcu.h"
#include "mac.h"

#define MT_STA_BFER			BIT(0)
#define MT_STA_BFEE			BIT(1)

static int
mt7921_mcu_parse_eeprom(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_eeprom_info *res;
	u8 *buf;

	if (!skb)
		return -EINVAL;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));

	res = (struct mt7921_mcu_eeprom_info *)skb->data;
	buf = dev->eeprom.data + le32_to_cpu(res->addr);
	memcpy(buf, res->data, 16);

	return 0;
}

int mt7921_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq)
{
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt76_connac2_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		mt7921_reset(mdev);

		return -ETIMEDOUT;
	}

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL) ||
	    cmd == MCU_CMD(PATCH_FINISH_REQ)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if (cmd == MCU_EXT_CMD(THERMAL_CTRL)) {
		skb_pull(skb, sizeof(*rxd) + 4);
		ret = le32_to_cpu(*(__le32 *)skb->data);
	} else if (cmd == MCU_EXT_CMD(EFUSE_ACCESS)) {
		ret = mt7921_mcu_parse_eeprom(mdev, skb);
	} else if (cmd == MCU_UNI_CMD(DEV_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(BSS_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(STA_REC_UPDATE) ||
		   cmd == MCU_UNI_CMD(HIF_CTRL) ||
		   cmd == MCU_UNI_CMD(OFFLOAD) ||
		   cmd == MCU_UNI_CMD(SUSPEND)) {
		struct mt7921_mcu_uni_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7921_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		/* skip invalid event */
		if (mcu_cmd != event->cid)
			ret = -EAGAIN;
	} else if (cmd == MCU_CE_QUERY(REG_READ)) {
		struct mt7921_mcu_reg_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7921_mcu_reg_event *)skb->data;
		ret = (int)le32_to_cpu(event->val);
	} else {
		skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mt7921_mcu_parse_response);

#ifdef CONFIG_PM

static int
mt7921_mcu_set_ipv6_ns_filter(struct mt76_dev *dev,
			      struct ieee80211_vif *vif, bool suspend)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_arpns_tlv arpns;
	} req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.arpns = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ND),
			.len = cpu_to_le16(sizeof(struct mt76_connac_arpns_tlv)),
			.mode = suspend,
		},
	};

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_OFFLOAD, &req, sizeof(req),
				 true);
}

void mt7921_mcu_set_suspend_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (IS_ENABLED(CONFIG_IPV6)) {
		struct mt76_phy *phy = priv;

		mt7921_mcu_set_ipv6_ns_filter(phy->dev, vif,
					      !test_bit(MT76_STATE_RUNNING,
					      &phy->state));
	}

	mt76_connac_mcu_set_suspend_iter(priv, mac, vif);
}

#endif /* CONFIG_PM */

static void
mt7921_mcu_scan_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7921_phy *phy = (struct mt7921_phy *)mphy->priv;

	spin_lock_bh(&dev->mt76.lock);
	__skb_queue_tail(&phy->scan_event_list, skb);
	spin_unlock_bh(&dev->mt76.lock);

	ieee80211_queue_delayed_work(mphy->hw, &phy->scan_work,
				     MT7921_HW_SCAN_TIMEOUT);
}

static void
mt7921_mcu_connection_loss_iter(void *priv, u8 *mac,
				struct ieee80211_vif *vif)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_connac_beacon_loss_event *event = priv;

	if (mvif->idx != event->bss_idx)
		return;

	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER) ||
	    vif->type != NL80211_IFTYPE_STATION)
		return;

	ieee80211_connection_loss(vif);
}

static void
mt7921_mcu_connection_loss_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac_beacon_loss_event *event;
	struct mt76_phy *mphy = &dev->mt76.phy;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	event = (struct mt76_connac_beacon_loss_event *)skb->data;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7921_mcu_connection_loss_iter, event);
}

static void
mt7921_mcu_bss_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_mcu_bss_event *event;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	event = (struct mt76_connac_mcu_bss_event *)skb->data;
	if (event->is_absent)
		ieee80211_stop_queues(mphy->hw);
	else
		ieee80211_wake_queues(mphy->hw);
}

static void
mt7921_mcu_debug_msg_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_debug_msg {
		__le16 id;
		u8 type;
		u8 flag;
		__le32 value;
		__le16 len;
		u8 content[512];
	} __packed * msg;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	msg = (struct mt7921_debug_msg *)skb->data;

	if (msg->type == 3) { /* fw log */
		u16 len = min_t(u16, le16_to_cpu(msg->len), 512);
		int i;

		for (i = 0 ; i < len; i++) {
			if (!msg->content[i])
				msg->content[i] = ' ';
		}
		wiphy_info(mt76_hw(dev)->wiphy, "%.*s", len, msg->content);
	}
}

static void
mt7921_mcu_low_power_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_lp_event {
		u8 state;
		u8 reserved[3];
	} __packed * event;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	event = (struct mt7921_mcu_lp_event *)skb->data;

	trace_lp_event(dev, event->state);
}

static void
mt7921_mcu_tx_done_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_tx_done_event *event;

	skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	event = (struct mt7921_mcu_tx_done_event *)skb->data;

	mt7921_mac_add_txs(dev, event->txs);
}

static void
mt7921_mcu_rx_unsolicited_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	switch (rxd->eid) {
	case MCU_EVENT_BSS_BEACON_LOSS:
		mt7921_mcu_connection_loss_event(dev, skb);
		break;
	case MCU_EVENT_SCHED_SCAN_DONE:
	case MCU_EVENT_SCAN_DONE:
		mt7921_mcu_scan_event(dev, skb);
		return;
	case MCU_EVENT_BSS_ABSENCE:
		mt7921_mcu_bss_event(dev, skb);
		break;
	case MCU_EVENT_DBG_MSG:
		mt7921_mcu_debug_msg_event(dev, skb);
		break;
	case MCU_EVENT_COREDUMP:
		dev->fw_assert = true;
		mt76_connac_mcu_coredump_event(&dev->mt76, skb,
					       &dev->coredump);
		return;
	case MCU_EVENT_LP_INFO:
		mt7921_mcu_low_power_event(dev, skb);
		break;
	case MCU_EVENT_TX_DONE:
		mt7921_mcu_tx_done_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7921_mcu_rx_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;

	if (skb_linearize(skb))
		return;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;

	if (rxd->eid == 0x6) {
		mt76_mcu_rx_event(&dev->mt76, skb);
		return;
	}

	if (rxd->ext_eid == MCU_EXT_EVENT_RATE_REPORT ||
	    rxd->eid == MCU_EVENT_BSS_BEACON_LOSS ||
	    rxd->eid == MCU_EVENT_SCHED_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_BSS_ABSENCE ||
	    rxd->eid == MCU_EVENT_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_TX_DONE ||
	    rxd->eid == MCU_EVENT_DBG_MSG ||
	    rxd->eid == MCU_EVENT_COREDUMP ||
	    rxd->eid == MCU_EVENT_LP_INFO ||
	    !rxd->seq)
		mt7921_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

/** starec & wtbl **/
int mt7921_mcu_uni_tx_ba(struct mt7921_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)params->sta->drv_priv;

	if (enable && !params->amsdu)
		msta->wcid.amsdu = false;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &msta->vif->mt76, params,
				      MCU_UNI_CMD(STA_REC_UPDATE),
				      enable, true);
}

int mt7921_mcu_uni_rx_ba(struct mt7921_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)params->sta->drv_priv;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &msta->vif->mt76, params,
				      MCU_UNI_CMD(STA_REC_UPDATE),
				      enable, false);
}

static char *mt7921_patch_name(struct mt7921_dev *dev)
{
	char *ret;

	if (is_mt7922(&dev->mt76))
		ret = MT7922_ROM_PATCH;
	else
		ret = MT7921_ROM_PATCH;

	return ret;
}

static char *mt7921_ram_name(struct mt7921_dev *dev)
{
	char *ret;

	if (is_mt7922(&dev->mt76))
		ret = MT7922_FIRMWARE_WM;
	else
		ret = MT7921_FIRMWARE_WM;

	return ret;
}

static int mt7921_load_firmware(struct mt7921_dev *dev)
{
	int ret;

	ret = mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY);
	if (ret && mt76_is_mmio(&dev->mt76)) {
		dev_dbg(dev->mt76.dev, "Firmware is already download\n");
		goto fw_loaded;
	}

	ret = mt76_connac2_load_patch(&dev->mt76, mt7921_patch_name(dev));
	if (ret)
		return ret;

	if (mt76_is_sdio(&dev->mt76)) {
		/* activate again */
		ret = __mt7921_mcu_fw_pmctrl(dev);
		if (!ret)
			ret = __mt7921_mcu_drv_pmctrl(dev);
	}

	ret = mt76_connac2_load_ram(&dev->mt76, mt7921_ram_name(dev), NULL);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
			    MT_TOP_MISC2_FW_N9_RDY, 1500)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");

		return -EIO;
	}

fw_loaded:

#ifdef CONFIG_PM
	dev->mt76.hw->wiphy->wowlan = &mt76_connac_wowlan_support;
#endif /* CONFIG_PM */

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7921_mcu_fw_log_2_host(struct mt7921_dev *dev, u8 ctrl)
{
	struct {
		u8 ctrl_val;
		u8 pad[3];
	} data = {
		.ctrl_val = ctrl
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(FWLOG_2_HOST),
				 &data, sizeof(data), false);
}

int mt7921_run_firmware(struct mt7921_dev *dev)
{
	int err;

	err = mt7921_load_firmware(dev);
	if (err)
		return err;

	err = mt76_connac_mcu_get_nic_capability(&dev->mphy);
	if (err)
		return err;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	return mt7921_mcu_fw_log_2_host(dev, 1);
}
EXPORT_SYMBOL_GPL(mt7921_run_firmware);

int mt7921_mcu_set_tx(struct mt7921_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct edca {
		__le16 cw_min;
		__le16 cw_max;
		__le16 txop;
		__le16 aifs;
		u8 guardtime;
		u8 acm;
	} __packed;
	struct mt7921_mcu_tx {
		struct edca edca[IEEE80211_NUM_ACS];
		u8 bss_idx;
		u8 qos;
		u8 wmm_idx;
		u8 pad;
	} __packed req = {
		.bss_idx = mvif->mt76.idx,
		.qos = vif->bss_conf.qos,
		.wmm_idx = mvif->mt76.wmm_idx,
	};
	struct mu_edca {
		u8 cw_min;
		u8 cw_max;
		u8 aifsn;
		u8 acm;
		u8 timer;
		u8 padding[3];
	};
	struct mt7921_mcu_mu_tx {
		u8 ver;
		u8 pad0;
		__le16 len;
		u8 bss_idx;
		u8 qos;
		u8 wmm_idx;
		u8 pad1;
		struct mu_edca edca[IEEE80211_NUM_ACS];
		u8 pad3[32];
	} __packed req_mu = {
		.bss_idx = mvif->mt76.idx,
		.qos = vif->bss_conf.qos,
		.wmm_idx = mvif->mt76.wmm_idx,
	};
	static const int to_aci[] = { 1, 0, 2, 3 };
	int ac, ret;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e = &req.edca[to_aci[ac]];

		e->aifs = cpu_to_le16(q->aifs);
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = cpu_to_le16(q->cw_min);
		else
			e->cw_min = cpu_to_le16(5);

		if (q->cw_max)
			e->cw_max = cpu_to_le16(q->cw_max);
		else
			e->cw_max = cpu_to_le16(10);
	}

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_EDCA_PARMS), &req,
				sizeof(req), false);
	if (ret)
		return ret;

	if (!vif->bss_conf.he_support)
		return 0;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_he_mu_edca_param_ac_rec *q;
		struct mu_edca *e;

		if (!mvif->queue_params[ac].mu_edca)
			break;

		q = &mvif->queue_params[ac].mu_edca_param_rec;
		e = &(req_mu.edca[to_aci[ac]]);

		e->cw_min = q->ecw_min_max & 0xf;
		e->cw_max = (q->ecw_min_max & 0xf0) >> 4;
		e->aifsn = q->aifsn;
		e->timer = q->mu_edca_timer;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_MU_EDCA_PARMS),
				 &req_mu, sizeof(req_mu), false);
}

int mt7921_mcu_set_chan_info(struct mt7921_phy *phy, int cmd)
{
	struct mt7921_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	struct {
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_streams_num;
		u8 rx_streams;	/* mask or num */
		u8 switch_reason;
		u8 band_idx;
		u8 center_ch2;	/* for 80+80 only */
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 ap_bw;
		u8 ap_center_ch;
		u8 rsv1[57];
	} __packed req = {
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt76_connac_chan_bw(chandef),
		.tx_streams_num = hweight8(phy->mt76->antenna_mask),
		.rx_streams = phy->mt76->antenna_mask,
		.band_idx = phy != &dev->phy,
	};

	if (chandef->chan->band == NL80211_BAND_6GHZ)
		req.channel_band = 2;
	else
		req.channel_band = chandef->chan->band;

	if (cmd == MCU_EXT_CMD(SET_RX_PATH) ||
	    dev->mt76.hw->conf.flags & IEEE80211_CONF_MONITOR)
		req.switch_reason = CH_SWITCH_NORMAL;
	else if (dev->mt76.hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(dev->mt76.hw->wiphy, chandef,
					  NL80211_IFTYPE_AP))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (cmd == MCU_EXT_CMD(CHANNEL_SWITCH))
		req.rx_streams = hweight8(req.rx_streams);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

int mt7921_mcu_set_eeprom(struct mt7921_dev *dev)
{
	struct req_hdr {
		u8 buffer_mode;
		u8 format;
		__le16 len;
	} __packed req = {
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EFUSE_BUFFER_MODE),
				 &req, sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt7921_mcu_set_eeprom);

int mt7921_mcu_uni_bss_ps(struct mt7921_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct ps_tlv {
			__le16 tag;
			__le16 len;
			u8 ps_state; /* 0: device awake
				      * 1: static power save
				      * 2: dynamic power saving
				      * 3: enter TWT power saving
				      * 4: leave TWT power saving
				      */
			u8 pad[3];
		} __packed ps;
	} __packed ps_req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.ps = {
			.tag = cpu_to_le16(UNI_BSS_INFO_PS),
			.len = cpu_to_le16(sizeof(struct ps_tlv)),
			.ps_state = vif->cfg.ps ? 2 : 0,
		},
	};

	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &ps_req, sizeof(ps_req), true);
}

static int
mt7921_mcu_uni_bss_bcnft(struct mt7921_dev *dev, struct ieee80211_vif *vif,
			 bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct bcnft_tlv {
			__le16 tag;
			__le16 len;
			__le16 bcn_interval;
			u8 dtim_period;
			u8 pad;
		} __packed bcnft;
	} __packed bcnft_req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.bcnft = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCNFT),
			.len = cpu_to_le16(sizeof(struct bcnft_tlv)),
			.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
			.dtim_period = vif->bss_conf.dtim_period,
		},
	};

	if (vif->type != NL80211_IFTYPE_STATION)
		return 0;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &bcnft_req, sizeof(bcnft_req), true);
}

int
mt7921_mcu_set_bss_pm(struct mt7921_dev *dev, struct ieee80211_vif *vif,
		      bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		u8 bss_idx;
		u8 dtim_period;
		__le16 aid;
		__le16 bcn_interval;
		__le16 atim_window;
		u8 uapsd;
		u8 bmc_delivered_ac;
		u8 bmc_triggered_ac;
		u8 pad;
	} req = {
		.bss_idx = mvif->mt76.idx,
		.aid = cpu_to_le16(vif->cfg.aid),
		.dtim_period = vif->bss_conf.dtim_period,
		.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
	};
	struct {
		u8 bss_idx;
		u8 pad[3];
	} req_hdr = {
		.bss_idx = mvif->mt76.idx,
	};
	int err;

	err = mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_BSS_ABORT),
				&req_hdr, sizeof(req_hdr), false);
	if (err < 0 || !enable)
		return err;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_BSS_CONNECTED),
				 &req, sizeof(req), false);
}

int mt7921_mcu_sta_update(struct mt7921_dev *dev, struct ieee80211_sta *sta,
			  struct ieee80211_vif *vif, bool enable,
			  enum mt76_sta_info_state state)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	int rssi = -ewma_rssi_read(&mvif->rssi);
	struct mt76_sta_cmd_info info = {
		.sta = sta,
		.vif = vif,
		.enable = enable,
		.cmd = MCU_UNI_CMD(STA_REC_UPDATE),
		.state = state,
		.offload_fw = true,
		.rcpi = to_rcpi(rssi),
	};
	struct mt7921_sta *msta;

	msta = sta ? (struct mt7921_sta *)sta->drv_priv : NULL;
	info.wcid = msta ? &msta->wcid : &mvif->sta.wcid;
	info.newly = msta ? state != MT76_STA_INFO_STATE_ASSOC : true;

	return mt76_connac_mcu_sta_cmd(&dev->mphy, &info);
}

int mt7921_mcu_drv_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err = 0;

	mutex_lock(&pm->mutex);

	if (!test_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	err = __mt7921_mcu_drv_pmctrl(dev);
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt7921_reset(&dev->mt76);

	return err;
}
EXPORT_SYMBOL_GPL(mt7921_mcu_drv_pmctrl);

int mt7921_mcu_fw_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err = 0;

	mutex_lock(&pm->mutex);

	if (mt76_connac_skip_fw_pmctrl(mphy, pm))
		goto out;

	err = __mt7921_mcu_fw_pmctrl(dev);
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt7921_reset(&dev->mt76);

	return err;
}
EXPORT_SYMBOL_GPL(mt7921_mcu_fw_pmctrl);

int mt7921_mcu_set_beacon_filter(struct mt7921_dev *dev,
				 struct ieee80211_vif *vif,
				 bool enable)
{
	int err;

	if (enable) {
		err = mt7921_mcu_uni_bss_bcnft(dev, vif, true);
		if (err)
			return err;

		mt76_set(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);

		return 0;
	}

	err = mt7921_mcu_set_bss_pm(dev, vif, false);
	if (err)
		return err;

	mt76_clear(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);

	return 0;
}

int mt7921_get_txpwr_info(struct mt7921_dev *dev, struct mt7921_txpwr *txpwr)
{
	struct mt7921_txpwr_event *event;
	struct mt7921_txpwr_req req = {
		.dbdc_idx = 0,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CE_CMD(GET_TXPWR),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	event = (struct mt7921_txpwr_event *)skb->data;
	WARN_ON(skb->len != le16_to_cpu(event->len));
	memcpy(txpwr, &event->txpwr, sizeof(event->txpwr));

	dev_kfree_skb(skb);

	return 0;
}

int mt7921_mcu_set_sniffer(struct mt7921_dev *dev, struct ieee80211_vif *vif,
			   bool enable)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		struct {
			u8 band_idx;
			u8 pad[3];
		} __packed hdr;
		struct sniffer_enable_tlv {
			__le16 tag;
			__le16 len;
			u8 enable;
			u8 pad[3];
		} __packed enable;
	} req = {
		.hdr = {
			.band_idx = mvif->band_idx,
		},
		.enable = {
			.tag = cpu_to_le16(0),
			.len = cpu_to_le16(sizeof(struct sniffer_enable_tlv)),
			.enable = enable,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(SNIFFER), &req, sizeof(req),
				 true);
}

int
mt7921_mcu_uni_add_beacon_offload(struct mt7921_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct ieee80211_mutable_offsets offs;
	struct {
		struct req_hdr {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct bcn_content_tlv {
			__le16 tag;
			__le16 len;
			__le16 tim_ie_pos;
			__le16 csa_ie_pos;
			__le16 bcc_ie_pos;
			/* 0: disable beacon offload
			 * 1: enable beacon offload
			 * 2: update probe respond offload
			 */
			u8 enable;
			/* 0: legacy format (TXD + payload)
			 * 1: only cap field IE
			 */
			u8 type;
			__le16 pkt_len;
			u8 pkt[512];
		} __packed beacon_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.beacon_tlv = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCN_CONTENT),
			.len = cpu_to_le16(sizeof(struct bcn_content_tlv)),
			.enable = enable,
		},
	};
	struct sk_buff *skb;

	/* support enable/update process only
	 * disable flow would be handled in bss stop handler automatically
	 */
	if (!enable)
		return -EOPNOTSUPP;

	skb = ieee80211_beacon_get_template(mt76_hw(dev), vif, &offs, 0);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "beacon size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	mt76_connac2_mac_write_txwi(&dev->mt76, (__le32 *)(req.beacon_tlv.pkt),
				    skb, wcid, NULL, 0, 0, BSS_CHANGED_BEACON);
	memcpy(req.beacon_tlv.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.beacon_tlv.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.beacon_tlv.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + offs.tim_offset);

	if (offs.cntdwn_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = MT_TXD_SIZE + offs.cntdwn_counter_offs[0] - 4;
		req.beacon_tlv.csa_ie_pos = cpu_to_le16(csa_offs);
	}
	dev_kfree_skb(skb);

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &req, sizeof(req), true);
}
