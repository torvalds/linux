// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/fs.h>
#include <linux/firmware.h>
#include "mt7925.h"
#include "mcu.h"
#include "mac.h"

#define MT_STA_BFER			BIT(0)
#define MT_STA_BFEE			BIT(1)

static bool mt7925_disable_clc;
module_param_named(disable_clc, mt7925_disable_clc, bool, 0644);
MODULE_PARM_DESC(disable_clc, "disable CLC support");

int mt7925_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq)
{
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt7925_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n", cmd, seq);
		mt792x_reset(mdev);

		return -ETIMEDOUT;
	}

	rxd = (struct mt7925_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL) ||
	    cmd == MCU_CMD(PATCH_FINISH_REQ)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if (cmd == MCU_UNI_CMD(DEV_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(BSS_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(STA_REC_UPDATE) ||
		   cmd == MCU_UNI_CMD(OFFLOAD) ||
		   cmd == MCU_UNI_CMD(SUSPEND)) {
		struct mt7925_mcu_uni_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7925_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		/* skip invalid event */
		if (mcu_cmd != event->cid)
			ret = -EAGAIN;
	} else {
		skb_pull(skb, sizeof(*rxd));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_parse_response);

int mt7925_mcu_regval(struct mt792x_dev *dev, u32 regidx, u32 *val, bool set)
{
#define MT_RF_REG_HDR           GENMASK(31, 24)
#define MT_RF_REG_ANT           GENMASK(23, 16)
#define RF_REG_PREFIX           0x99
	struct {
		u8 __rsv[4];
		union {
			struct uni_cmd_access_reg_basic {
				__le16 tag;
				__le16 len;
				__le32 idx;
				__le32 data;
			} __packed reg;
			struct uni_cmd_access_rf_reg_basic {
				__le16 tag;
				__le16 len;
				__le16 ant;
				u8 __rsv[2];
				__le32 idx;
				__le32 data;
			} __packed rf_reg;
		};
	} __packed * res, req;
	struct sk_buff *skb;
	int ret;

	if (u32_get_bits(regidx, MT_RF_REG_HDR) == RF_REG_PREFIX) {
		req.rf_reg.tag = cpu_to_le16(UNI_CMD_ACCESS_RF_REG_BASIC);
		req.rf_reg.len = cpu_to_le16(sizeof(req.rf_reg));
		req.rf_reg.ant = cpu_to_le16(u32_get_bits(regidx, MT_RF_REG_ANT));
		req.rf_reg.idx = cpu_to_le32(regidx);
		req.rf_reg.data = set ? cpu_to_le32(*val) : 0;
	} else {
		req.reg.tag = cpu_to_le16(UNI_CMD_ACCESS_REG_BASIC);
		req.reg.len = cpu_to_le16(sizeof(req.reg));
		req.reg.idx = cpu_to_le32(regidx);
		req.reg.data = set ? cpu_to_le32(*val) : 0;
	}

	if (set)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REG_ACCESS),
					 &req, sizeof(req), true);

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(REG_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	if (u32_get_bits(regidx, MT_RF_REG_HDR) == RF_REG_PREFIX)
		*val = le32_to_cpu(res->rf_reg.data);
	else
		*val = le32_to_cpu(res->reg.data);

	dev_kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_regval);

int mt7925_mcu_update_arp_filter(struct mt76_dev *dev,
				 struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct ieee80211_vif *mvif = link_conf->vif;
	struct sk_buff *skb;
	int i, len = min_t(int, mvif->cfg.arp_addr_cnt,
			   IEEE80211_BSS_ARP_ADDR_LIST_LEN);
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7925_arpns_tlv arp;
	} req = {
		.hdr = {
			.bss_idx = mconf->mt76.idx,
		},
		.arp = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ARP),
			.len = cpu_to_le16(sizeof(req) - 4 + len * 2 * sizeof(__be32)),
			.ips_num = len,
			.enable = true,
		},
	};

	skb = mt76_mcu_msg_alloc(dev, NULL, sizeof(req) + len * 2 * sizeof(__be32));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req, sizeof(req));
	for (i = 0; i < len; i++) {
		skb_put_data(skb, &mvif->cfg.arp_addr_list[i], sizeof(__be32));
		skb_put_zero(skb, sizeof(__be32));
	}

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD(OFFLOAD), true);
}

#ifdef CONFIG_PM
static int
mt7925_connac_mcu_set_wow_ctrl(struct mt76_phy *phy, struct ieee80211_vif *vif,
			       bool suspend, struct cfg80211_wowlan *wowlan)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_wow_ctrl_tlv wow_ctrl_tlv;
		struct mt76_connac_wow_gpio_param_tlv gpio_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.wow_ctrl_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_CTRL),
			.len = cpu_to_le16(sizeof(struct mt76_connac_wow_ctrl_tlv)),
			.cmd = suspend ? 1 : 2,
		},
		.gpio_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_GPIO_PARAM),
			.len = cpu_to_le16(sizeof(struct mt76_connac_wow_gpio_param_tlv)),
			.gpio_pin = 0xff, /* follow fw about GPIO pin */
		},
	};

	if (wowlan->magic_pkt)
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_MAGIC;
	if (wowlan->disconnect)
		req.wow_ctrl_tlv.trigger |= (UNI_WOW_DETECT_TYPE_DISCONNECT |
					     UNI_WOW_DETECT_TYPE_BCN_LOST);
	if (wowlan->nd_config) {
		mt7925_mcu_sched_scan_req(phy, vif, wowlan->nd_config);
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_SCH_SCAN_HIT;
		mt7925_mcu_sched_scan_enable(phy, vif, suspend);
	}
	if (wowlan->n_patterns)
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_BITMAP;

	if (mt76_is_mmio(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_PCIE;
	else if (mt76_is_usb(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_USB;
	else if (mt76_is_sdio(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_GPIO;

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD(SUSPEND), &req,
				 sizeof(req), true);
}

static int
mt7925_mcu_set_wow_pattern(struct mt76_dev *dev,
			   struct ieee80211_vif *vif,
			   u8 index, bool enable,
			   struct cfg80211_pkt_pattern *pattern)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct mt7925_wow_pattern_tlv *tlv;
	struct sk_buff *skb;
	struct {
		u8 bss_idx;
		u8 pad[3];
	} __packed hdr = {
		.bss_idx = mvif->idx,
	};

	skb = mt76_mcu_msg_alloc(dev, NULL, sizeof(hdr) + sizeof(*tlv));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = (struct mt7925_wow_pattern_tlv *)skb_put(skb, sizeof(*tlv));
	tlv->tag = cpu_to_le16(UNI_SUSPEND_WOW_PATTERN);
	tlv->len = cpu_to_le16(sizeof(*tlv));
	tlv->bss_idx = 0xF;
	tlv->data_len = pattern->pattern_len;
	tlv->enable = enable;
	tlv->index = index;
	tlv->offset = 0;

	memcpy(tlv->pattern, pattern->pattern, pattern->pattern_len);
	memcpy(tlv->mask, pattern->mask, DIV_ROUND_UP(pattern->pattern_len, 8));

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD(SUSPEND), true);
}

void mt7925_mcu_set_suspend_iter(void *priv, u8 *mac,
				 struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = priv;
	bool suspend = !test_bit(MT76_STATE_RUNNING, &phy->state);
	struct ieee80211_hw *hw = phy->hw;
	struct cfg80211_wowlan *wowlan = hw->wiphy->wowlan_config;
	int i;

	mt76_connac_mcu_set_gtk_rekey(phy->dev, vif, suspend);

	mt76_connac_mcu_set_suspend_mode(phy->dev, vif, suspend, 1, true);

	for (i = 0; i < wowlan->n_patterns; i++)
		mt7925_mcu_set_wow_pattern(phy->dev, vif, i, suspend,
					   &wowlan->patterns[i]);
	mt7925_connac_mcu_set_wow_ctrl(phy, vif, suspend, wowlan);
}

#endif /* CONFIG_PM */

static void
mt7925_mcu_connection_loss_iter(void *priv, u8 *mac,
				struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct mt7925_uni_beacon_loss_event *event = priv;

	if (mvif->idx != event->hdr.bss_idx)
		return;

	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER) ||
	    vif->type != NL80211_IFTYPE_STATION)
		return;

	ieee80211_connection_loss(vif);
}

static void
mt7925_mcu_connection_loss_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct mt7925_uni_beacon_loss_event *event;
	struct mt76_phy *mphy = &dev->mt76.phy;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd));
	event = (struct mt7925_uni_beacon_loss_event *)skb->data;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7925_mcu_connection_loss_iter, event);
}

static void
mt7925_mcu_roc_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct mt7925_roc_grant_tlv *grant = priv;

	if (ieee80211_vif_is_mld(vif) && vif->type == NL80211_IFTYPE_STATION)
		return;

	if (mvif->idx != grant->bss_idx)
		return;

	mvif->band_idx = grant->dbdcband;
}

static void mt7925_mcu_roc_handle_grant(struct mt792x_dev *dev,
					struct tlv *tlv)
{
	struct ieee80211_hw *hw = dev->mt76.hw;
	struct mt7925_roc_grant_tlv *grant;
	int duration;

	grant = (struct mt7925_roc_grant_tlv *)tlv;

	/* should never happen */
	WARN_ON_ONCE((le16_to_cpu(grant->tag) != UNI_EVENT_ROC_GRANT));

	if (grant->reqtype == MT7925_ROC_REQ_ROC)
		ieee80211_ready_on_channel(hw);
	else if (grant->reqtype == MT7925_ROC_REQ_JOIN)
		ieee80211_iterate_active_interfaces_atomic(hw,
						IEEE80211_IFACE_ITER_RESUME_ALL,
						mt7925_mcu_roc_iter, grant);
	dev->phy.roc_grant = true;
	wake_up(&dev->phy.roc_wait);
	duration = le32_to_cpu(grant->max_interval);
	mod_timer(&dev->phy.roc_timer,
		  jiffies + msecs_to_jiffies(duration));
}

static void
mt7925_mcu_handle_hif_ctrl_basic(struct mt792x_dev *dev, struct tlv *tlv)
{
	struct mt7925_mcu_hif_ctrl_basic_tlv *basic;

	basic = (struct mt7925_mcu_hif_ctrl_basic_tlv *)tlv;

	if (basic->hifsuspend) {
		dev->hif_idle = true;
		if (!(basic->hif_tx_traffic_status == HIF_TRAFFIC_IDLE &&
		      basic->hif_rx_traffic_status == HIF_TRAFFIC_IDLE))
			dev_info(dev->mt76.dev, "Hif traffic not idle.\n");
	} else {
		dev->hif_resumed = true;
	}
	wake_up(&dev->wait);
}

static void
mt7925_mcu_uni_hif_ctrl_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct tlv *tlv;
	u32 tlv_len;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);
	tlv = (struct tlv *)skb->data;
	tlv_len = skb->len;

	while (tlv_len > 0 && le16_to_cpu(tlv->len) <= tlv_len) {
		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_HIF_CTRL_BASIC:
			mt7925_mcu_handle_hif_ctrl_basic(dev, tlv);
			break;
		default:
			break;
		}
		tlv_len -= le16_to_cpu(tlv->len);
		tlv = (struct tlv *)((char *)(tlv) + le16_to_cpu(tlv->len));
	}
}

static void
mt7925_mcu_uni_roc_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct tlv *tlv;
	int i = 0;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);

	while (i < skb->len) {
		tlv = (struct tlv *)(skb->data + i);

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_ROC_GRANT:
			mt7925_mcu_roc_handle_grant(dev, tlv);
			break;
		case UNI_EVENT_ROC_GRANT_SUB_LINK:
			break;
		}

		i += le16_to_cpu(tlv->len);
	}
}

static void
mt7925_mcu_scan_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt792x_phy *phy = mphy->priv;

	spin_lock_bh(&dev->mt76.lock);
	__skb_queue_tail(&phy->scan_event_list, skb);
	spin_unlock_bh(&dev->mt76.lock);

	ieee80211_queue_delayed_work(mphy->hw, &phy->scan_work,
				     MT792x_HW_SCAN_TIMEOUT);
}

static void
mt7925_mcu_tx_done_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_TX_DONE_MSG 0
#define UNI_EVENT_TX_DONE_RAW 1
	struct mt7925_mcu_txs_event {
		u8 ver;
		u8 rsv[3];
		u8 data[];
	} __packed * txs;
	struct tlv *tlv;
	u32 tlv_len;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);
	tlv = (struct tlv *)skb->data;
	tlv_len = skb->len;

	while (tlv_len > 0 && le16_to_cpu(tlv->len) <= tlv_len) {
		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_TX_DONE_RAW:
			txs = (struct mt7925_mcu_txs_event *)tlv->data;
			mt7925_mac_add_txs(dev, txs->data);
			break;
		default:
			break;
		}
		tlv_len -= le16_to_cpu(tlv->len);
		tlv = (struct tlv *)((char *)(tlv) + le16_to_cpu(tlv->len));
	}
}

static void
mt7925_mcu_uni_debug_msg_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct mt7925_uni_debug_msg {
		__le16 tag;
		__le16 len;
		u8 fmt;
		u8 rsv[3];
		u8 id;
		u8 type:3;
		u8 nr_args:5;
		union {
			struct idxlog {
				__le16 rsv;
				__le32 ts;
				__le32 idx;
				u8 data[];
			} __packed idx;
			struct txtlog {
				u8 len;
				u8 rsv;
				__le32 ts;
				u8 data[];
			} __packed txt;
		};
	} __packed * hdr;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);
	hdr = (struct mt7925_uni_debug_msg *)skb->data;

	if (hdr->id == 0x28) {
		skb_pull(skb, offsetof(struct mt7925_uni_debug_msg, id));
		wiphy_info(mt76_hw(dev)->wiphy, "%.*s", skb->len, skb->data);
		return;
	} else if (hdr->id != 0xa8) {
		return;
	}

	if (hdr->type == 0) { /* idx log */
		int i, ret, len = PAGE_SIZE - 1, nr_val;
		struct page *page = dev_alloc_pages(get_order(len));
		__le32 *val;
		char *buf, *cur;

		if (!page)
			return;

		buf = page_address(page);
		cur = buf;

		nr_val = (le16_to_cpu(hdr->len) - sizeof(*hdr)) / 4;
		val = (__le32 *)hdr->idx.data;
		for (i = 0; i < nr_val && len > 0; i++) {
			ret = snprintf(cur, len, "0x%x,", le32_to_cpu(val[i]));
			if (ret <= 0)
				break;

			cur += ret;
			len -= ret;
		}
		if (cur > buf)
			wiphy_info(mt76_hw(dev)->wiphy, "idx: 0x%X,%d,%s",
				   le32_to_cpu(hdr->idx.idx), nr_val, buf);
		put_page(page);
	} else if (hdr->type == 2) { /* str log */
		wiphy_info(mt76_hw(dev)->wiphy, "%.*s", hdr->txt.len, hdr->txt.data);
	}
}

static void
mt7925_mcu_uni_rx_unsolicited_event(struct mt792x_dev *dev,
				    struct sk_buff *skb)
{
	struct mt7925_mcu_rxd *rxd;

	rxd = (struct mt7925_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_UNI_EVENT_HIF_CTRL:
		mt7925_mcu_uni_hif_ctrl_event(dev, skb);
		break;
	case MCU_UNI_EVENT_FW_LOG_2_HOST:
		mt7925_mcu_uni_debug_msg_event(dev, skb);
		break;
	case MCU_UNI_EVENT_ROC:
		mt7925_mcu_uni_roc_event(dev, skb);
		break;
	case MCU_UNI_EVENT_SCAN_DONE:
		mt7925_mcu_scan_event(dev, skb);
		return;
	case MCU_UNI_EVENT_TX_DONE:
		mt7925_mcu_tx_done_event(dev, skb);
		break;
	case MCU_UNI_EVENT_BSS_BEACON_LOSS:
		mt7925_mcu_connection_loss_event(dev, skb);
		break;
	case MCU_UNI_EVENT_COREDUMP:
		dev->fw_assert = true;
		mt76_connac_mcu_coredump_event(&dev->mt76, skb, &dev->coredump);
		return;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7925_mcu_rx_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct mt7925_mcu_rxd *rxd = (struct mt7925_mcu_rxd *)skb->data;

	if (skb_linearize(skb))
		return;

	if (rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT) {
		mt7925_mcu_uni_rx_unsolicited_event(dev, skb);
		return;
	}

	mt76_mcu_rx_event(&dev->mt76, skb);
}

static int
mt7925_mcu_sta_ba(struct mt76_dev *dev, struct mt76_vif_link *mvif,
		  struct ieee80211_ampdu_params *params,
		  bool enable, bool tx)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)params->sta->drv_priv;
	struct sta_rec_ba_uni *ba;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len;

	len = sizeof(struct sta_req_hdr) + sizeof(*ba);
	skb = __mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid,
					      len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba_uni *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;

	return mt76_mcu_skb_send_msg(dev, skb,
				     MCU_UNI_CMD(STA_REC_UPDATE), true);
}

/** starec & wtbl **/
int mt7925_mcu_uni_tx_ba(struct mt792x_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)params->sta->drv_priv;
	struct mt792x_vif *mvif = msta->vif;

	if (enable && !params->amsdu)
		msta->deflink.wcid.amsdu = false;

	return mt7925_mcu_sta_ba(&dev->mt76, &mvif->bss_conf.mt76, params,
				 enable, true);
}

int mt7925_mcu_uni_rx_ba(struct mt792x_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)params->sta->drv_priv;
	struct mt792x_vif *mvif = msta->vif;

	return mt7925_mcu_sta_ba(&dev->mt76, &mvif->bss_conf.mt76, params,
				 enable, false);
}

static int mt7925_mcu_read_eeprom(struct mt792x_dev *dev, u32 offset, u8 *val)
{
	struct {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		__le32 addr;
		__le32 valid;
		u8 data[MT7925_EEPROM_BLOCK_SIZE];
	} __packed req = {
		.tag = cpu_to_le16(1),
		.len = cpu_to_le16(sizeof(req) - 4),
		.addr = cpu_to_le32(round_down(offset,
				    MT7925_EEPROM_BLOCK_SIZE)),
	};
	struct evt {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ver;
		__le32 addr;
		__le32 valid;
		__le32 size;
		__le32 magic_num;
		__le32 type;
		__le32 rsv1[4];
		u8 data[32];
	} __packed *res;
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct evt *)skb->data;
	*val = res->data[offset % MT7925_EEPROM_BLOCK_SIZE];

	dev_kfree_skb(skb);

	return 0;
}

static int mt7925_load_clc(struct mt792x_dev *dev, const char *fw_name)
{
	const struct mt76_connac2_fw_trailer *hdr;
	const struct mt76_connac2_fw_region *region;
	const struct mt7925_clc *clc;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt792x_phy *phy = &dev->phy;
	const struct firmware *fw;
	u8 *clc_base = NULL, hw_encap = 0;
	int ret, i, len, offset = 0;

	dev->phy.clc_chan_conf = 0xff;
	if (mt7925_disable_clc ||
	    mt76_is_usb(&dev->mt76))
		return 0;

	if (mt76_is_mmio(&dev->mt76)) {
		ret = mt7925_mcu_read_eeprom(dev, MT_EE_HW_TYPE, &hw_encap);
		if (ret)
			return ret;
		hw_encap = u8_get_bits(hw_encap, MT_EE_HW_TYPE_ENCAP);
	}

	ret = request_firmware(&fw, fw_name, mdev->dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(mdev->dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const void *)(fw->data + fw->size - sizeof(*hdr));
	for (i = 0; i < hdr->n_region; i++) {
		region = (const void *)((const u8 *)hdr -
					(hdr->n_region - i) * sizeof(*region));
		len = le32_to_cpu(region->len);

		/* check if we have valid buffer size */
		if (offset + len > fw->size) {
			dev_err(mdev->dev, "Invalid firmware region\n");
			ret = -EINVAL;
			goto out;
		}

		if ((region->feature_set & FW_FEATURE_NON_DL) &&
		    region->type == FW_TYPE_CLC) {
			clc_base = (u8 *)(fw->data + offset);
			break;
		}
		offset += len;
	}

	if (!clc_base)
		goto out;

	for (offset = 0; offset < len; offset += le32_to_cpu(clc->len)) {
		clc = (const struct mt7925_clc *)(clc_base + offset);

		if (clc->idx >= ARRAY_SIZE(phy->clc))
			break;

		/* do not init buf again if chip reset triggered */
		if (phy->clc[clc->idx])
			continue;

		/* header content sanity */
		if ((clc->idx == MT792x_CLC_BE_CTRL &&
		     u8_get_bits(clc->t2.type, MT_EE_HW_TYPE_ENCAP) != hw_encap) ||
		    u8_get_bits(clc->t0.type, MT_EE_HW_TYPE_ENCAP) != hw_encap)
			continue;

		phy->clc[clc->idx] = devm_kmemdup(mdev->dev, clc,
						  le32_to_cpu(clc->len),
						  GFP_KERNEL);

		if (!phy->clc[clc->idx]) {
			ret = -ENOMEM;
			goto out;
		}
	}

	ret = mt7925_mcu_set_clc(dev, "00", ENVIRON_INDOOR);
out:
	release_firmware(fw);

	return ret;
}

int mt7925_mcu_fw_log_2_host(struct mt792x_dev *dev, u8 ctrl)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 ctrl;
		u8 interval;
		u8 _rsv2[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_LOG_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = ctrl,
	};
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_CMD(WSYS_CONFIG),
					&req, sizeof(req), true, NULL);
	return ret;
}

int mt7925_mcu_get_temperature(struct mt792x_phy *phy)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 _rsv2[4];
	} __packed req = {
		.tag = cpu_to_le16(0x0),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct mt7925_thermal_evt {
		u8 rsv[4];
		__le32 temperature;
	} __packed * evt;
	struct mt792x_dev *dev = phy->dev;
	int temperature, ret;
	struct sk_buff *skb;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(THERMAL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	skb_pull(skb, 4 + sizeof(struct tlv));
	evt = (struct mt7925_thermal_evt *)skb->data;

	temperature = le32_to_cpu(evt->temperature);

	dev_kfree_skb(skb);

	return temperature;
}

static void
mt7925_mcu_parse_phy_cap(struct mt792x_dev *dev, char *data)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_dev *mdev = mphy->dev;
	struct mt7925_mcu_phy_cap {
		u8 ht;
		u8 vht;
		u8 _5g;
		u8 max_bw;
		u8 nss;
		u8 dbdc;
		u8 tx_ldpc;
		u8 rx_ldpc;
		u8 tx_stbc;
		u8 rx_stbc;
		u8 hw_path;
		u8 he;
		u8 eht;
	} __packed * cap;
	enum {
		WF0_24G,
		WF0_5G
	};

	cap = (struct mt7925_mcu_phy_cap *)data;

	mdev->phy.antenna_mask = BIT(cap->nss) - 1;
	mdev->phy.chainmask = mdev->phy.antenna_mask;
	mdev->phy.cap.has_2ghz = cap->hw_path & BIT(WF0_24G);
	mdev->phy.cap.has_5ghz = cap->hw_path & BIT(WF0_5G);
}

static void
mt7925_mcu_parse_eml_cap(struct mt792x_dev *dev, char *data)
{
	struct mt7925_mcu_eml_cap {
		u8 rsv[4];
		__le16 eml_cap;
		u8 rsv2[6];
	} __packed * cap;

	cap = (struct mt7925_mcu_eml_cap *)data;

	dev->phy.eml_cap = le16_to_cpu(cap->eml_cap);
}

static int
mt7925_mcu_get_nic_capability(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CHIP_CONFIG_NIC_CAPA),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct mt76_connac_cap_hdr {
		__le16 n_element;
		u8 rsv[2];
	} __packed * hdr;
	struct sk_buff *skb;
	int ret, i;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_CMD(CHIP_CONFIG),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	hdr = (struct mt76_connac_cap_hdr *)skb->data;
	if (skb->len < sizeof(*hdr)) {
		ret = -EINVAL;
		goto out;
	}

	skb_pull(skb, sizeof(*hdr));

	for (i = 0; i < le16_to_cpu(hdr->n_element); i++) {
		struct tlv *tlv = (struct tlv *)skb->data;
		int len;

		if (skb->len < sizeof(*tlv))
			break;

		len = le16_to_cpu(tlv->len);
		if (skb->len < len)
			break;

		switch (le16_to_cpu(tlv->tag)) {
		case MT_NIC_CAP_6G:
			mphy->cap.has_6ghz = !!tlv->data[0];
			break;
		case MT_NIC_CAP_MAC_ADDR:
			memcpy(mphy->macaddr, (void *)tlv->data, ETH_ALEN);
			break;
		case MT_NIC_CAP_PHY:
			mt7925_mcu_parse_phy_cap(dev, tlv->data);
			break;
		case MT_NIC_CAP_CHIP_CAP:
			dev->phy.chip_cap = le64_to_cpu(*(__le64 *)tlv->data);
			break;
		case MT_NIC_CAP_EML_CAP:
			mt7925_mcu_parse_eml_cap(dev, tlv->data);
			break;
		default:
			break;
		}
		skb_pull(skb, len);
	}
out:
	dev_kfree_skb(skb);
	return ret;
}

int mt7925_mcu_chip_config(struct mt792x_dev *dev, const char *cmd)
{
	u16 len = strlen(cmd) + 1;
	struct {
		u8 _rsv[4];
		__le16 tag;
		__le16 len;
		struct mt76_connac_config config;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CHIP_CONFIG_CHIP_CFG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.config = {
			.resp_type = 0,
			.type = 0,
			.data_size = cpu_to_le16(len),
		},
	};

	memcpy(req.config.data, cmd, len);

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(CHIP_CONFIG),
				 &req, sizeof(req), false);
}

int mt7925_mcu_set_deep_sleep(struct mt792x_dev *dev, bool enable)
{
	char cmd[16];

	snprintf(cmd, sizeof(cmd), "KeepFullPwr %d", !enable);

	return mt7925_mcu_chip_config(dev, cmd);
}
EXPORT_SYMBOL_GPL(mt7925_mcu_set_deep_sleep);

int mt7925_mcu_set_thermal_protect(struct mt792x_dev *dev)
{
	char cmd[64];
	int ret = 0;

	snprintf(cmd, sizeof(cmd), "ThermalProtGband %d %d %d %d %d %d %d %d %d %d",
		 0, 100, 90, 80, 30, 1, 1, 115, 105, 5);
	ret = mt7925_mcu_chip_config(dev, cmd);

	snprintf(cmd, sizeof(cmd), "ThermalProtAband %d %d %d %d %d %d %d %d %d %d",
		 1, 100, 90, 80, 30, 1, 1, 115, 105, 5);
	ret |= mt7925_mcu_chip_config(dev, cmd);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_set_thermal_protect);

int mt7925_run_firmware(struct mt792x_dev *dev)
{
	int err;

	err = mt792x_load_firmware(dev);
	if (err)
		return err;

	err = mt7925_mcu_get_nic_capability(dev);
	if (err)
		return err;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	err = mt7925_load_clc(dev, mt792x_ram_name(dev));
	if (err)
		return err;

	return mt7925_mcu_fw_log_2_host(dev, 1);
}
EXPORT_SYMBOL_GPL(mt7925_run_firmware);

static void
mt7925_mcu_sta_hdr_trans_tlv(struct sk_buff *skb,
			     struct ieee80211_vif *vif,
			     struct ieee80211_link_sta *link_sta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct sta_rec_hdr_trans *hdr_trans;
	struct mt76_wcid *wcid;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDR_TRANS, sizeof(*hdr_trans));
	hdr_trans = (struct sta_rec_hdr_trans *)tlv;
	hdr_trans->dis_rx_hdr_tran = true;

	if (vif->type == NL80211_IFTYPE_STATION)
		hdr_trans->to_ds = true;
	else
		hdr_trans->from_ds = true;

	if (link_sta) {
		struct mt792x_sta *msta = (struct mt792x_sta *)link_sta->sta->drv_priv;
		struct mt792x_link_sta *mlink;

		mlink = mt792x_sta_to_link(msta, link_sta->link_id);
		wcid = &mlink->wcid;
	} else {
		wcid = &mvif->sta.deflink.wcid;
	}

	if (!wcid)
		return;

	hdr_trans->dis_rx_hdr_tran = !test_bit(MT_WCID_FLAG_HDR_TRANS, &wcid->flags);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags)) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
	}
}

int mt7925_mcu_wtbl_update_hdr_trans(struct mt792x_dev *dev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     int link_id)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct ieee80211_link_sta *link_sta = sta ? &sta->deflink : NULL;
	struct mt792x_link_sta *mlink;
	struct mt792x_bss_conf *mconf;
	struct mt792x_sta *msta;
	struct sk_buff *skb;

	msta = sta ? (struct mt792x_sta *)sta->drv_priv : &mvif->sta;

	mlink = mt792x_sta_to_link(msta, link_id);
	link_sta = mt792x_sta_to_link_sta(vif, sta, link_id);
	mconf = mt792x_vif_to_link(mvif, link_id);

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mconf->mt76,
					      &mlink->wcid,
					      MT7925_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec hdr trans */
	mt7925_mcu_sta_hdr_trans_tlv(skb, vif, link_sta);
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7925_mcu_set_tx(struct mt792x_dev *dev,
		      struct ieee80211_bss_conf *bss_conf)
{
#define MCU_EDCA_AC_PARAM	0
#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		(WMM_AIFS_SET | WMM_CW_MIN_SET | \
				 WMM_CW_MAX_SET | WMM_TXOP_SET)
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(bss_conf);
	struct {
		u8 bss_idx;
		u8 __rsv[3];
	} __packed hdr = {
		.bss_idx = mconf->mt76.idx,
	};
	struct sk_buff *skb;
	int len = sizeof(hdr) + IEEE80211_NUM_ACS * sizeof(struct edca);
	int ac;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mconf->queue_params[ac];
		struct edca *e;
		struct tlv *tlv;

		tlv = mt76_connac_mcu_add_tlv(skb, MCU_EDCA_AC_PARAM, sizeof(*e));

		e = (struct edca *)tlv;
		e->set = WMM_PARAM_SET;
		e->queue = ac;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = fls(q->cw_max);
		else
			e->cw_max = 10;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD(EDCA_UPDATE), true);
}

static int
mt7925_mcu_sta_key_tlv(struct mt76_wcid *wcid,
		       struct mt76_connac_sta_key_conf *sta_key_conf,
		       struct sk_buff *skb,
		       struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd,
		       struct mt792x_sta *msta)
{
	struct mt792x_vif *mvif = msta->vif;
	struct mt792x_bss_conf *mconf = mt792x_vif_to_link(mvif, wcid->link_id);
	struct sta_rec_sec_uni *sec;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct tlv *tlv;

	sta = msta == &mvif->sta ?
		      NULL :
		      container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_KEY_V3, sizeof(*sec));
	sec = (struct sta_rec_sec_uni *)tlv;
	sec->bss_idx = mconf->mt76.idx;
	sec->is_authenticator = 0;
	sec->mgmt_prot = 1; /* only used in MLO mode */
	sec->wlan_idx = (u8)wcid->idx;

	if (sta) {
		struct ieee80211_link_sta *link_sta;

		sec->tx_key = 1;
		sec->key_type = 1;
		link_sta = mt792x_sta_to_link_sta(vif, sta, wcid->link_id);

		if (link_sta)
			memcpy(sec->peer_addr, link_sta->addr, ETH_ALEN);
	} else {
		struct ieee80211_bss_conf *link_conf;

		link_conf = mt792x_vif_to_bss_conf(vif, wcid->link_id);

		if (link_conf)
			memcpy(sec->peer_addr, link_conf->bssid, ETH_ALEN);
	}

	if (cmd == SET_KEY) {
		u8 cipher;

		sec->add = 1;
		cipher = mt7925_mcu_get_cipher(key->cipher);
		if (cipher == CONNAC3_CIPHER_NONE)
			return -EOPNOTSUPP;

		if (cipher == CONNAC3_CIPHER_BIP_CMAC_128) {
			sec->cipher_id = CONNAC3_CIPHER_BIP_CMAC_128;
			sec->key_id = sta_key_conf->keyidx;
			sec->key_len = 32;
			memcpy(sec->key, sta_key_conf->key, 16);
			memcpy(sec->key + 16, key->key, 16);
		} else {
			sec->cipher_id = cipher;
			sec->key_id = key->keyidx;
			sec->key_len = key->keylen;
			memcpy(sec->key, key->key, key->keylen);

			if (cipher == CONNAC3_CIPHER_TKIP) {
				/* Rx/Tx MIC keys are swapped */
				memcpy(sec->key + 16, key->key + 24, 8);
				memcpy(sec->key + 24, key->key + 16, 8);
			}

			/* store key_conf for BIP batch update */
			if (cipher == CONNAC3_CIPHER_AES_CCMP) {
				memcpy(sta_key_conf->key, key->key, key->keylen);
				sta_key_conf->keyidx = key->keyidx;
			}
		}
	} else {
		sec->add = 0;
	}

	return 0;
}

int mt7925_mcu_add_key(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct mt76_connac_sta_key_conf *sta_key_conf,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd,
		       struct mt792x_sta *msta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_bss_conf *mconf = mt792x_vif_to_link(mvif, wcid->link_id);
	struct sk_buff *skb;
	int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(dev, &mconf->mt76, wcid,
					      MT7925_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7925_mcu_sta_key_tlv(wcid, sta_key_conf, skb, key, cmd, msta);
	if (ret)
		return ret;

	return mt76_mcu_skb_send_msg(dev, skb, mcu_cmd, true);
}

int mt7925_mcu_set_mlo_roc(struct mt792x_bss_conf *mconf, u16 sel_links,
			   int duration, u8 token_id)
{
	struct mt792x_vif *mvif = mconf->vif;
	struct ieee80211_vif *vif = container_of((void *)mvif,
						 struct ieee80211_vif, drv_priv);
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_channel *chan;
	const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 1,
		[NL80211_BAND_5GHZ] = 2,
		[NL80211_BAND_6GHZ] = 3,
	};
	enum mt7925_roc_req type;
	int center_ch, i = 0;
	bool is_AG_band = false;
	struct {
		u8 id;
		u8 bss_idx;
		u16 tag;
		struct mt792x_bss_conf *mconf;
		struct ieee80211_channel *chan;
	} links[2];

	struct {
		struct {
			u8 rsv[4];
		} __packed hdr;
		struct roc_acquire_tlv roc[2];
	} __packed req = {
			.roc[0].tag = cpu_to_le16(UNI_ROC_NUM),
			.roc[0].len = cpu_to_le16(sizeof(struct roc_acquire_tlv)),
			.roc[1].tag = cpu_to_le16(UNI_ROC_NUM),
			.roc[1].len = cpu_to_le16(sizeof(struct roc_acquire_tlv))
	};

	if (!mconf || hweight16(vif->valid_links) < 2 ||
	    hweight16(sel_links) != 2)
		return -EPERM;

	for (i = 0; i < ARRAY_SIZE(links); i++) {
		links[i].id = i ? __ffs(~BIT(mconf->link_id) & sel_links) :
				 mconf->link_id;
		link_conf = mt792x_vif_to_bss_conf(vif, links[i].id);
		if (WARN_ON_ONCE(!link_conf))
			return -EPERM;

		links[i].chan = link_conf->chanreq.oper.chan;
		if (WARN_ON_ONCE(!links[i].chan))
			return -EPERM;

		links[i].mconf = mt792x_vif_to_link(mvif, links[i].id);
		links[i].tag = links[i].id == mconf->link_id ?
			       UNI_ROC_ACQUIRE : UNI_ROC_SUB_LINK;

		is_AG_band |= links[i].chan->band == NL80211_BAND_2GHZ;
	}

	if (vif->cfg.eml_cap & IEEE80211_EML_CAP_EMLSR_SUPP)
		type = is_AG_band ? MT7925_ROC_REQ_MLSR_AG :
				    MT7925_ROC_REQ_MLSR_AA;
	else
		type = MT7925_ROC_REQ_JOIN;

	for (i = 0; i < ARRAY_SIZE(links) && i < hweight16(vif->active_links); i++) {
		if (WARN_ON_ONCE(!links[i].mconf || !links[i].chan))
			continue;

		chan = links[i].chan;
		center_ch = ieee80211_frequency_to_channel(chan->center_freq);
		req.roc[i].len = cpu_to_le16(sizeof(struct roc_acquire_tlv));
		req.roc[i].tag = cpu_to_le16(links[i].tag);
		req.roc[i].tokenid = token_id;
		req.roc[i].reqtype = type;
		req.roc[i].maxinterval = cpu_to_le32(duration);
		req.roc[i].bss_idx = links[i].mconf->mt76.idx;
		req.roc[i].control_channel = chan->hw_value;
		req.roc[i].bw = CMD_CBW_20MHZ;
		req.roc[i].bw_from_ap = CMD_CBW_20MHZ;
		req.roc[i].center_chan = center_ch;
		req.roc[i].center_chan_from_ap = center_ch;
		req.roc[i].center_chan2 = 0;
		req.roc[i].center_chan2_from_ap = 0;

		/* STR : 0xfe indicates BAND_ALL with enabling DBDC
		 * EMLSR : 0xff indicates (BAND_AUTO) without DBDC
		 */
		req.roc[i].dbdcband = type == MT7925_ROC_REQ_JOIN ? 0xfe : 0xff;

		if (chan->hw_value < center_ch)
			req.roc[i].sco = 1; /* SCA */
		else if (chan->hw_value > center_ch)
			req.roc[i].sco = 3; /* SCB */

		req.roc[i].band = ch_band[chan->band];
	}

	return mt76_mcu_send_msg(&mvif->phy->dev->mt76, MCU_UNI_CMD(ROC),
				 &req, sizeof(req), true);
}

int mt7925_mcu_set_roc(struct mt792x_phy *phy, struct mt792x_bss_conf *mconf,
		       struct ieee80211_channel *chan, int duration,
		       enum mt7925_roc_req type, u8 token_id)
{
	int center_ch = ieee80211_frequency_to_channel(chan->center_freq);
	struct mt792x_dev *dev = phy->dev;
	struct {
		struct {
			u8 rsv[4];
		} __packed hdr;
		struct roc_acquire_tlv roc;
	} __packed req = {
		.roc = {
			.tag = cpu_to_le16(UNI_ROC_ACQUIRE),
			.len = cpu_to_le16(sizeof(struct roc_acquire_tlv)),
			.tokenid = token_id,
			.reqtype = type,
			.maxinterval = cpu_to_le32(duration),
			.bss_idx = mconf->mt76.idx,
			.control_channel = chan->hw_value,
			.bw = CMD_CBW_20MHZ,
			.bw_from_ap = CMD_CBW_20MHZ,
			.center_chan = center_ch,
			.center_chan_from_ap = center_ch,
			.dbdcband = 0xff, /* auto */
		},
	};

	if (chan->hw_value < center_ch)
		req.roc.sco = 1; /* SCA */
	else if (chan->hw_value > center_ch)
		req.roc.sco = 3; /* SCB */

	switch (chan->band) {
	case NL80211_BAND_6GHZ:
		req.roc.band = 3;
		break;
	case NL80211_BAND_5GHZ:
		req.roc.band = 2;
		break;
	default:
		req.roc.band = 1;
		break;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(ROC),
				 &req, sizeof(req), true);
}

int mt7925_mcu_abort_roc(struct mt792x_phy *phy, struct mt792x_bss_conf *mconf,
			 u8 token_id)
{
	struct mt792x_dev *dev = phy->dev;
	struct {
		struct {
			u8 rsv[4];
		} __packed hdr;
		struct roc_abort_tlv {
			__le16 tag;
			__le16 len;
			u8 bss_idx;
			u8 tokenid;
			u8 dbdcband;
			u8 rsv[5];
		} __packed abort;
	} __packed req = {
		.abort = {
			.tag = cpu_to_le16(UNI_ROC_ABORT),
			.len = cpu_to_le16(sizeof(struct roc_abort_tlv)),
			.tokenid = token_id,
			.bss_idx = mconf->mt76.idx,
			.dbdcband = 0xff, /* auto*/
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(ROC),
				 &req, sizeof(req), true);
}

int mt7925_mcu_set_eeprom(struct mt792x_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 buffer_mode;
		u8 format;
		__le16 buf_len;
	} __packed req = {
		.tag = cpu_to_le16(UNI_EFUSE_BUFFER_MODE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE
	};

	return mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_CMD(EFUSE_CTRL),
					 &req, sizeof(req), true, NULL);
}
EXPORT_SYMBOL_GPL(mt7925_mcu_set_eeprom);

int mt7925_mcu_uni_bss_ps(struct mt792x_dev *dev,
			  struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
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
			.bss_idx = mconf->mt76.idx,
		},
		.ps = {
			.tag = cpu_to_le16(UNI_BSS_INFO_PS),
			.len = cpu_to_le16(sizeof(struct ps_tlv)),
			.ps_state = link_conf->vif->cfg.ps ? 2 : 0,
		},
	};

	if (link_conf->vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &ps_req, sizeof(ps_req), true);
}

int
mt7925_mcu_uni_bss_bcnft(struct mt792x_dev *dev,
			 struct ieee80211_bss_conf *link_conf, bool enable)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
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
			u8 bmc_delivered_ac;
			u8 bmc_triggered_ac;
			u8 pad[3];
		} __packed bcnft;
	} __packed bcnft_req = {
		.hdr = {
			.bss_idx = mconf->mt76.idx,
		},
		.bcnft = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCNFT),
			.len = cpu_to_le16(sizeof(struct bcnft_tlv)),
			.bcn_interval = cpu_to_le16(link_conf->beacon_int),
			.dtim_period = link_conf->dtim_period,
		},
	};

	if (link_conf->vif->type != NL80211_IFTYPE_STATION)
		return 0;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &bcnft_req, sizeof(bcnft_req), true);
}

int
mt7925_mcu_set_bss_pm(struct mt792x_dev *dev,
		      struct ieee80211_bss_conf *link_conf,
		      bool enable)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
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
			u8 bmc_delivered_ac;
			u8 bmc_triggered_ac;
			u8 pad[3];
		} __packed enable;
	} req = {
		.hdr = {
			.bss_idx = mconf->mt76.idx,
		},
		.enable = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCNFT),
			.len = cpu_to_le16(sizeof(struct bcnft_tlv)),
			.dtim_period = link_conf->dtim_period,
			.bcn_interval = cpu_to_le16(link_conf->beacon_int),
		},
	};
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct pm_disable {
			__le16 tag;
			__le16 len;
		} __packed disable;
	} req1 = {
		.hdr = {
			.bss_idx = mconf->mt76.idx,
		},
		.disable = {
			.tag = cpu_to_le16(UNI_BSS_INFO_PM_DISABLE),
			.len = cpu_to_le16(sizeof(struct pm_disable))
		},
	};
	int err;

	err = mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				&req1, sizeof(req1), true);
	if (err < 0 || !enable)
		return err;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &req, sizeof(req), true);
}

static void
mt7925_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	if (!link_sta->he_cap.has_he)
		return;

	mt76_connac_mcu_sta_he_tlv_v2(skb, link_sta->sta);
}

static void
mt7925_mcu_sta_he_6g_tlv(struct sk_buff *skb,
			 struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_he_6g_capa *he_6g;
	struct tlv *tlv;

	if (!link_sta->he_6ghz_capa.capa)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_6G, sizeof(*he_6g));

	he_6g = (struct sta_rec_he_6g_capa *)tlv;
	he_6g->capa = link_sta->he_6ghz_capa.capa;
}

static void
mt7925_mcu_sta_eht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_eht_mcs_nss_supp *mcs_map;
	struct ieee80211_eht_cap_elem_fixed *elem;
	struct sta_rec_eht *eht;
	struct tlv *tlv;

	if (!link_sta->eht_cap.has_eht)
		return;

	mcs_map = &link_sta->eht_cap.eht_mcs_nss_supp;
	elem = &link_sta->eht_cap.eht_cap_elem;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT, sizeof(*eht));

	eht = (struct sta_rec_eht *)tlv;
	eht->tid_bitmap = 0xff;
	eht->mac_cap = cpu_to_le16(*(u16 *)elem->mac_cap_info);
	eht->phy_cap = cpu_to_le64(*(u64 *)elem->phy_cap_info);
	eht->phy_cap_ext = cpu_to_le64(elem->phy_cap_info[8]);

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
		memcpy(eht->mcs_map_bw20, &mcs_map->only_20mhz, sizeof(eht->mcs_map_bw20));
	memcpy(eht->mcs_map_bw80, &mcs_map->bw._80, sizeof(eht->mcs_map_bw80));
	memcpy(eht->mcs_map_bw160, &mcs_map->bw._160, sizeof(eht->mcs_map_bw160));
}

static void
mt7925_mcu_sta_ht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_ht *ht;
	struct tlv *tlv;

	if (!link_sta->ht_cap.ht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));

	ht = (struct sta_rec_ht *)tlv;
	ht->ht_cap = cpu_to_le16(link_sta->ht_cap.cap);
}

static void
mt7925_mcu_sta_vht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_vht *vht;
	struct tlv *tlv;

	/* For 6G band, this tlv is necessary to let hw work normally */
	if (!link_sta->he_6ghz_capa.capa && !link_sta->vht_cap.vht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));

	vht = (struct sta_rec_vht *)tlv;
	vht->vht_cap = cpu_to_le32(link_sta->vht_cap.cap);
	vht->vht_rx_mcs_map = link_sta->vht_cap.vht_mcs.rx_mcs_map;
	vht->vht_tx_mcs_map = link_sta->vht_cap.vht_mcs.tx_mcs_map;
}

static void
mt7925_mcu_sta_amsdu_tlv(struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_link_sta *link_sta)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)link_sta->sta->drv_priv;
	struct mt792x_link_sta *mlink;
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	if (!link_sta->agg.max_amsdu_len)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;

	mlink = mt792x_sta_to_link(msta, link_sta->link_id);
	mlink->wcid.amsdu = true;

	switch (link_sta->agg.max_amsdu_len) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		amsdu->max_mpdu_size =
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		return;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		return;
	default:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		return;
	}
}

static void
mt7925_mcu_sta_phy_tlv(struct sk_buff *skb,
		       struct ieee80211_vif *vif,
		       struct ieee80211_link_sta *link_sta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf;
	struct cfg80211_chan_def *chandef;
	struct mt792x_bss_conf *mconf;
	struct sta_rec_phy *phy;
	struct tlv *tlv;
	u8 af = 0, mm = 0;

	link_conf = mt792x_vif_to_bss_conf(vif, link_sta->link_id);
	mconf = mt792x_vif_to_link(mvif, link_sta->link_id);
	chandef = mconf->mt76.ctx ? &mconf->mt76.ctx->def :
				    &link_conf->chanreq.oper;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_PHY, sizeof(*phy));
	phy = (struct sta_rec_phy *)tlv;
	phy->phy_type = mt76_connac_get_phy_mode_v2(mvif->phy->mt76, vif,
						    chandef->chan->band,
						    link_sta);
	phy->basic_rate = cpu_to_le16((u16)link_conf->basic_rates);
	if (link_sta->ht_cap.ht_supported) {
		af = link_sta->ht_cap.ampdu_factor;
		mm = link_sta->ht_cap.ampdu_density;
	}

	if (link_sta->vht_cap.vht_supported) {
		u8 vht_af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
				      link_sta->vht_cap.cap);

		af = max_t(u8, af, vht_af);
	}

	if (link_sta->he_6ghz_capa.capa) {
		af = le16_get_bits(link_sta->he_6ghz_capa.capa,
				   IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
		mm = le16_get_bits(link_sta->he_6ghz_capa.capa,
				   IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
	}

	phy->ampdu = FIELD_PREP(IEEE80211_HT_AMPDU_PARM_FACTOR, af) |
		     FIELD_PREP(IEEE80211_HT_AMPDU_PARM_DENSITY, mm);
	phy->max_ampdu_len = af;
}

static void
mt7925_mcu_sta_state_v2_tlv(struct mt76_phy *mphy, struct sk_buff *skb,
			    struct ieee80211_link_sta *link_sta,
			    struct ieee80211_vif *vif,
			    u8 rcpi, u8 sta_state)
{
	struct sta_rec_state_v2 {
		__le16 tag;
		__le16 len;
		u8 state;
		u8 rsv[3];
		__le32 flags;
		u8 vht_opmode;
		u8 action;
		u8 rsv2[2];
	} __packed * state;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_STATE, sizeof(*state));
	state = (struct sta_rec_state_v2 *)tlv;
	state->state = sta_state;

	if (link_sta->vht_cap.vht_supported) {
		state->vht_opmode = link_sta->bandwidth;
		state->vht_opmode |= link_sta->rx_nss <<
			IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
	}
}

static void
mt7925_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb,
			     struct ieee80211_vif *vif,
			     struct ieee80211_link_sta *link_sta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf;
	struct cfg80211_chan_def *chandef;
	struct sta_rec_ra_info *ra_info;
	struct mt792x_bss_conf *mconf;
	enum nl80211_band band;
	struct tlv *tlv;
	u16 supp_rates;

	link_conf = mt792x_vif_to_bss_conf(vif, link_sta->link_id);
	mconf = mt792x_vif_to_link(mvif, link_sta->link_id);
	chandef = mconf->mt76.ctx ? &mconf->mt76.ctx->def :
				    &link_conf->chanreq.oper;
	band = chandef->chan->band;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra_info));
	ra_info = (struct sta_rec_ra_info *)tlv;

	supp_rates = link_sta->supp_rates[band];
	if (band == NL80211_BAND_2GHZ)
		supp_rates = FIELD_PREP(RA_LEGACY_OFDM, supp_rates >> 4) |
			     FIELD_PREP(RA_LEGACY_CCK, supp_rates & 0xf);
	else
		supp_rates = FIELD_PREP(RA_LEGACY_OFDM, supp_rates);

	ra_info->legacy = cpu_to_le16(supp_rates);

	if (link_sta->ht_cap.ht_supported)
		memcpy(ra_info->rx_mcs_bitmask,
		       link_sta->ht_cap.mcs.rx_mask,
		       HT_MCS_MASK_NUM);
}

static void
mt7925_mcu_sta_eht_mld_tlv(struct sk_buff *skb,
			   struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct wiphy *wiphy = mvif->phy->mt76->hw->wiphy;
	const struct wiphy_iftype_ext_capab *ext_capa;
	struct sta_rec_eht_mld *eht_mld;
	struct tlv *tlv;
	u16 eml_cap;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT_MLD, sizeof(*eht_mld));
	eht_mld = (struct sta_rec_eht_mld *)tlv;
	eht_mld->mld_type = 0xff;

	if (!ieee80211_vif_is_mld(vif))
		return;

	ext_capa = cfg80211_get_iftype_ext_capa(wiphy,
						ieee80211_vif_type_p2p(vif));
	if (!ext_capa)
		return;

	eml_cap = (vif->cfg.eml_cap & (IEEE80211_EML_CAP_EMLSR_SUPP |
				       IEEE80211_EML_CAP_TRANSITION_TIMEOUT)) |
		  (ext_capa->eml_capabilities & (IEEE80211_EML_CAP_EMLSR_PADDING_DELAY |
						IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY));

	if (eml_cap & IEEE80211_EML_CAP_EMLSR_SUPP) {
		eht_mld->eml_cap[0] = u16_get_bits(eml_cap, GENMASK(7, 0));
		eht_mld->eml_cap[1] = u16_get_bits(eml_cap, GENMASK(15, 8));
	} else {
		eht_mld->str_cap[0] = BIT(1);
	}
}

static void
mt7925_mcu_sta_mld_tlv(struct sk_buff *skb,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	unsigned long valid = mvif->valid_links;
	struct mt792x_bss_conf *mconf;
	struct mt792x_link_sta *mlink;
	struct sta_rec_mld *mld;
	struct tlv *tlv;
	int i, cnt = 0;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MLD, sizeof(*mld));
	mld = (struct sta_rec_mld *)tlv;
	memcpy(mld->mac_addr, sta->addr, ETH_ALEN);
	mld->primary_id = cpu_to_le16(msta->deflink.wcid.idx);
	mld->wlan_id = cpu_to_le16(msta->deflink.wcid.idx);
	mld->link_num = min_t(u8, hweight16(mvif->valid_links), 2);

	for_each_set_bit(i, &valid, IEEE80211_MLD_MAX_NUM_LINKS) {
		if (cnt == mld->link_num)
			break;

		mconf = mt792x_vif_to_link(mvif, i);
		mlink = mt792x_sta_to_link(msta, i);
		mld->link[cnt].wlan_id = cpu_to_le16(mlink->wcid.idx);
		mld->link[cnt++].bss_idx = mconf->mt76.idx;

		if (mlink != &msta->deflink)
			mld->secondary_id = cpu_to_le16(mlink->wcid.idx);
	}
}

static void
mt7925_mcu_sta_remove_tlv(struct sk_buff *skb)
{
	struct sta_rec_remove *rem;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, 0x25, sizeof(*rem));
	rem = (struct sta_rec_remove *)tlv;
	rem->action = 0;
}

static int
mt7925_mcu_sta_cmd(struct mt76_phy *phy,
		   struct mt76_sta_cmd_info *info)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)info->vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct mt792x_bss_conf *mconf;
	struct sk_buff *skb;

	mconf = mt792x_vif_to_link(mvif, info->wcid->link_id);

	skb = __mt76_connac_mcu_alloc_sta_req(dev, &mconf->mt76, info->wcid,
					      MT7925_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (info->enable && info->link_sta) {
		mt76_connac_mcu_sta_basic_tlv(dev, skb, info->link_conf,
					      info->link_sta,
					      info->enable, info->newly);
		mt7925_mcu_sta_phy_tlv(skb, info->vif, info->link_sta);
		mt7925_mcu_sta_ht_tlv(skb, info->link_sta);
		mt7925_mcu_sta_vht_tlv(skb, info->link_sta);
		mt76_connac_mcu_sta_uapsd(skb, info->vif, info->link_sta->sta);
		mt7925_mcu_sta_amsdu_tlv(skb, info->vif, info->link_sta);
		mt7925_mcu_sta_he_tlv(skb, info->link_sta);
		mt7925_mcu_sta_he_6g_tlv(skb, info->link_sta);
		mt7925_mcu_sta_eht_tlv(skb, info->link_sta);
		mt7925_mcu_sta_rate_ctrl_tlv(skb, info->vif,
					     info->link_sta);
		mt7925_mcu_sta_state_v2_tlv(phy, skb, info->link_sta,
					    info->vif, info->rcpi,
					    info->state);

		if (info->state != MT76_STA_INFO_STATE_NONE) {
			mt7925_mcu_sta_mld_tlv(skb, info->vif, info->link_sta->sta);
			mt7925_mcu_sta_eht_mld_tlv(skb, info->vif, info->link_sta->sta);
		}
	}

	if (!info->enable) {
		mt7925_mcu_sta_remove_tlv(skb);
		mt76_connac_mcu_add_tlv(skb, STA_REC_MLD_OFF,
					sizeof(struct tlv));
	} else {
		mt7925_mcu_sta_hdr_trans_tlv(skb, info->vif, info->link_sta);
	}

	return mt76_mcu_skb_send_msg(dev, skb, info->cmd, true);
}

int mt7925_mcu_sta_update(struct mt792x_dev *dev,
			  struct ieee80211_link_sta *link_sta,
			  struct ieee80211_vif *vif, bool enable,
			  enum mt76_sta_info_state state)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	int rssi = -ewma_rssi_read(&mvif->bss_conf.rssi);
	struct mt76_sta_cmd_info info = {
		.link_sta = link_sta,
		.vif = vif,
		.link_conf = &vif->bss_conf,
		.enable = enable,
		.cmd = MCU_UNI_CMD(STA_REC_UPDATE),
		.state = state,
		.offload_fw = true,
		.rcpi = to_rcpi(rssi),
	};
	struct mt792x_sta *msta;
	struct mt792x_link_sta *mlink;

	if (link_sta) {
		msta = (struct mt792x_sta *)link_sta->sta->drv_priv;
		mlink = mt792x_sta_to_link(msta, link_sta->link_id);
	}
	info.wcid = link_sta ? &mlink->wcid : &mvif->sta.deflink.wcid;
	info.newly = state != MT76_STA_INFO_STATE_ASSOC;

	return mt7925_mcu_sta_cmd(&dev->mphy, &info);
}

int mt7925_mcu_set_beacon_filter(struct mt792x_dev *dev,
				 struct ieee80211_vif *vif,
				 bool enable)
{
#define MT7925_FIF_BIT_CLR		BIT(1)
#define MT7925_FIF_BIT_SET		BIT(0)
	int err = 0;

	if (enable) {
		err = mt7925_mcu_uni_bss_bcnft(dev, &vif->bss_conf, true);
		if (err < 0)
			return err;

		return mt7925_mcu_set_rxfilter(dev, 0,
					       MT7925_FIF_BIT_SET,
					       MT_WF_RFCR_DROP_OTHER_BEACON);
	}

	err = mt7925_mcu_set_bss_pm(dev, &vif->bss_conf, false);
	if (err < 0)
		return err;

	return mt7925_mcu_set_rxfilter(dev, 0,
				       MT7925_FIF_BIT_CLR,
				       MT_WF_RFCR_DROP_OTHER_BEACON);
}

int mt7925_get_txpwr_info(struct mt792x_dev *dev, u8 band_idx, struct mt7925_txpwr *txpwr)
{
#define TX_POWER_SHOW_INFO 0x7
#define TXPOWER_ALL_RATE_POWER_INFO 0x2
	struct mt7925_txpwr_event *event;
	struct mt7925_txpwr_req req = {
		.tag = cpu_to_le16(TX_POWER_SHOW_INFO),
		.len = cpu_to_le16(sizeof(req) - 4),
		.catg = TXPOWER_ALL_RATE_POWER_INFO,
		.band_idx = band_idx,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_CMD(TXPOWER),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	event = (struct mt7925_txpwr_event *)skb->data;
	memcpy(txpwr, &event->txpwr, sizeof(event->txpwr));

	dev_kfree_skb(skb);

	return 0;
}

int mt7925_mcu_set_sniffer(struct mt792x_dev *dev, struct ieee80211_vif *vif,
			   bool enable)
{
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
	} __packed req = {
		.hdr = {
			.band_idx = 0,
		},
		.enable = {
			.tag = cpu_to_le16(UNI_SNIFFER_ENABLE),
			.len = cpu_to_le16(sizeof(struct sniffer_enable_tlv)),
			.enable = enable,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(SNIFFER), &req, sizeof(req),
				 true);
}

int mt7925_mcu_config_sniffer(struct mt792x_vif *vif,
			      struct ieee80211_chanctx_conf *ctx)
{
	struct mt76_phy *mphy = vif->phy->mt76;
	struct cfg80211_chan_def *chandef = ctx ? &ctx->def : &mphy->chandef;
	int freq1 = chandef->center_freq1, freq2 = chandef->center_freq2;

	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 1,
		[NL80211_BAND_5GHZ] = 2,
		[NL80211_BAND_6GHZ] = 3,
	};
	static const u8 ch_width[] = {
		[NL80211_CHAN_WIDTH_20_NOHT] = 0,
		[NL80211_CHAN_WIDTH_20] = 0,
		[NL80211_CHAN_WIDTH_40] = 0,
		[NL80211_CHAN_WIDTH_80] = 1,
		[NL80211_CHAN_WIDTH_160] = 2,
		[NL80211_CHAN_WIDTH_80P80] = 3,
		[NL80211_CHAN_WIDTH_5] = 4,
		[NL80211_CHAN_WIDTH_10] = 5,
		[NL80211_CHAN_WIDTH_320] = 6,
	};

	struct {
		struct {
			u8 band_idx;
			u8 pad[3];
		} __packed hdr;
		struct config_tlv {
			__le16 tag;
			__le16 len;
			u16 aid;
			u8 ch_band;
			u8 bw;
			u8 control_ch;
			u8 sco;
			u8 center_ch;
			u8 center_ch2;
			u8 drop_err;
			u8 pad[3];
		} __packed tlv;
	} __packed req = {
		.hdr = {
			.band_idx = 0,
		},
		.tlv = {
			.tag = cpu_to_le16(UNI_SNIFFER_CONFIG),
			.len = cpu_to_le16(sizeof(req.tlv)),
			.control_ch = chandef->chan->hw_value,
			.center_ch = ieee80211_frequency_to_channel(freq1),
			.drop_err = 1,
		},
	};

	if (chandef->chan->band < ARRAY_SIZE(ch_band))
		req.tlv.ch_band = ch_band[chandef->chan->band];
	if (chandef->width < ARRAY_SIZE(ch_width))
		req.tlv.bw = ch_width[chandef->width];

	if (freq2)
		req.tlv.center_ch2 = ieee80211_frequency_to_channel(freq2);

	if (req.tlv.control_ch < req.tlv.center_ch)
		req.tlv.sco = 1; /* SCA */
	else if (req.tlv.control_ch > req.tlv.center_ch)
		req.tlv.sco = 3; /* SCB */

	return mt76_mcu_send_msg(mphy->dev, MCU_UNI_CMD(SNIFFER),
				 &req, sizeof(req), true);
}

int
mt7925_mcu_uni_add_beacon_offload(struct mt792x_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  bool enable)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
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
			.bss_idx = mvif->bss_conf.mt76.idx,
		},
		.beacon_tlv = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCN_CONTENT),
			.len = cpu_to_le16(sizeof(struct bcn_content_tlv)),
			.enable = enable,
			.type = 1,
		},
	};
	struct sk_buff *skb;
	u8 cap_offs;

	/* support enable/update process only
	 * disable flow would be handled in bss stop handler automatically
	 */
	if (!enable)
		return -EOPNOTSUPP;

	skb = ieee80211_beacon_get_template(mt76_hw(dev), vif, &offs, 0);
	if (!skb)
		return -EINVAL;

	cap_offs = offsetof(struct ieee80211_mgmt, u.beacon.capab_info);
	if (!skb_pull(skb, cap_offs)) {
		dev_err(dev->mt76.dev, "beacon format err\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (skb->len > 512) {
		dev_err(dev->mt76.dev, "beacon size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	memcpy(req.beacon_tlv.pkt, skb->data, skb->len);
	req.beacon_tlv.pkt_len = cpu_to_le16(skb->len);
	offs.tim_offset -= cap_offs;
	req.beacon_tlv.tim_ie_pos = cpu_to_le16(offs.tim_offset);

	if (offs.cntdwn_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = offs.cntdwn_counter_offs[0] - cap_offs - 4;
		req.beacon_tlv.csa_ie_pos = cpu_to_le16(csa_offs);
	}
	dev_kfree_skb(skb);

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &req, sizeof(req), true);
}

static
void mt7925_mcu_bss_rlm_tlv(struct sk_buff *skb, struct mt76_phy *phy,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *ctx)
{
	struct cfg80211_chan_def *chandef = ctx ? &ctx->def :
						  &link_conf->chanreq.oper;
	int freq1 = chandef->center_freq1, freq2 = chandef->center_freq2;
	enum nl80211_band band = chandef->chan->band;
	struct bss_rlm_tlv *req;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_RLM, sizeof(*req));
	req = (struct bss_rlm_tlv *)tlv;
	req->control_channel = chandef->chan->hw_value;
	req->center_chan = ieee80211_frequency_to_channel(freq1);
	req->center_chan2 = 0;
	req->tx_streams = hweight8(phy->antenna_mask);
	req->ht_op_info = 4; /* set HT 40M allowed */
	req->rx_streams = hweight8(phy->antenna_mask);
	req->center_chan2 = 0;
	req->sco = 0;
	req->band = 1;

	switch (band) {
	case NL80211_BAND_2GHZ:
		req->band = 1;
		break;
	case NL80211_BAND_5GHZ:
		req->band = 2;
		break;
	case NL80211_BAND_6GHZ:
		req->band = 3;
		break;
	default:
		break;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		req->bw = CMD_CBW_40MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		req->bw = CMD_CBW_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		req->bw = CMD_CBW_8080MHZ;
		req->center_chan2 = ieee80211_frequency_to_channel(freq2);
		break;
	case NL80211_CHAN_WIDTH_160:
		req->bw = CMD_CBW_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_5:
		req->bw = CMD_CBW_5MHZ;
		break;
	case NL80211_CHAN_WIDTH_10:
		req->bw = CMD_CBW_10MHZ;
		break;
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	default:
		req->bw = CMD_CBW_20MHZ;
		req->ht_op_info = 0;
		break;
	}

	if (req->control_channel < req->center_chan)
		req->sco = 1; /* SCA */
	else if (req->control_channel > req->center_chan)
		req->sco = 3; /* SCB */
}

static struct sk_buff *
__mt7925_mcu_alloc_bss_req(struct mt76_dev *dev, struct mt76_vif_link *mvif, int len)
{
	struct bss_req_hdr hdr = {
		.bss_idx = mvif->idx,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

static
void mt7925_mcu_bss_eht_tlv(struct sk_buff *skb, struct mt76_phy *phy,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *ctx)
{
	struct cfg80211_chan_def *chandef = ctx ? &ctx->def :
						  &link_conf->chanreq.oper;

	struct bss_eht_tlv *req;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_EHT, sizeof(*req));
	req = (struct bss_eht_tlv *)tlv;
	req->is_eth_dscb_present = chandef->punctured ? 1 : 0;
	req->eht_dis_sub_chan_bitmap = cpu_to_le16(chandef->punctured);
}

int mt7925_mcu_set_eht_pp(struct mt76_phy *phy, struct mt76_vif_link *mvif,
			  struct ieee80211_bss_conf *link_conf,
			  struct ieee80211_chanctx_conf *ctx)
{
	struct sk_buff *skb;

	skb = __mt7925_mcu_alloc_bss_req(phy->dev, mvif,
					 MT7925_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7925_mcu_bss_eht_tlv(skb, phy, link_conf, ctx);

	return mt76_mcu_skb_send_msg(phy->dev, skb,
				     MCU_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7925_mcu_set_chctx(struct mt76_phy *phy, struct mt76_vif_link *mvif,
			 struct ieee80211_bss_conf *link_conf,
			 struct ieee80211_chanctx_conf *ctx)
{
	struct sk_buff *skb;

	skb = __mt7925_mcu_alloc_bss_req(phy->dev, mvif,
					 MT7925_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7925_mcu_bss_rlm_tlv(skb, phy, link_conf, ctx);

	return mt76_mcu_skb_send_msg(phy->dev, skb,
				     MCU_UNI_CMD(BSS_INFO_UPDATE), true);
}

static u8
mt7925_get_phy_mode_ext(struct mt76_phy *phy, struct ieee80211_vif *vif,
			enum nl80211_band band,
			struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_he_6ghz_capa *he_6ghz_capa;
	const struct ieee80211_sta_eht_cap *eht_cap;
	__le16 capa = 0;
	u8 mode = 0;

	if (link_sta) {
		he_6ghz_capa = &link_sta->he_6ghz_capa;
		eht_cap = &link_sta->eht_cap;
	} else {
		struct ieee80211_supported_band *sband;

		sband = phy->hw->wiphy->bands[band];
		capa = ieee80211_get_he_6ghz_capa(sband, vif->type);
		he_6ghz_capa = (struct ieee80211_he_6ghz_capa *)&capa;

		eht_cap = ieee80211_get_eht_iftype_cap(sband, vif->type);
	}

	switch (band) {
	case NL80211_BAND_2GHZ:
		if (eht_cap && eht_cap->has_eht)
			mode |= PHY_MODE_BE_24G;
		break;
	case NL80211_BAND_5GHZ:
		if (eht_cap && eht_cap->has_eht)
			mode |= PHY_MODE_BE_5G;
		break;
	case NL80211_BAND_6GHZ:
		if (he_6ghz_capa && he_6ghz_capa->capa)
			mode |= PHY_MODE_AX_6G;

		if (eht_cap && eht_cap->has_eht)
			mode |= PHY_MODE_BE_6G;
		break;
	default:
		break;
	}

	return mode;
}

static void
mt7925_mcu_bss_basic_tlv(struct sk_buff *skb,
			 struct ieee80211_bss_conf *link_conf,
			 struct ieee80211_link_sta *link_sta,
			 struct ieee80211_chanctx_conf *ctx,
			 struct mt76_phy *phy, u16 wlan_idx,
			 bool enable)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct cfg80211_chan_def *chandef = ctx ? &ctx->def :
						  &link_conf->chanreq.oper;
	enum nl80211_band band = chandef->chan->band;
	struct mt76_connac_bss_basic_tlv *basic_req;
	struct mt792x_link_sta *mlink;
	struct tlv *tlv;
	int conn_type;
	u8 idx;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BASIC, sizeof(*basic_req));
	basic_req = (struct mt76_connac_bss_basic_tlv *)tlv;

	idx = mconf->mt76.omac_idx > EXT_BSSID_START ? HW_BSSID_0 :
						      mconf->mt76.omac_idx;
	basic_req->hw_bss_idx = idx;

	basic_req->phymode_ext = mt7925_get_phy_mode_ext(phy, vif, band,
							 link_sta);

	if (band == NL80211_BAND_2GHZ)
		basic_req->nonht_basic_phy = cpu_to_le16(PHY_TYPE_ERP_INDEX);
	else
		basic_req->nonht_basic_phy = cpu_to_le16(PHY_TYPE_OFDM_INDEX);

	memcpy(basic_req->bssid, link_conf->bssid, ETH_ALEN);
	basic_req->phymode = mt76_connac_get_phy_mode(phy, vif, band, link_sta);
	basic_req->bcn_interval = cpu_to_le16(link_conf->beacon_int);
	basic_req->dtim_period = link_conf->dtim_period;
	basic_req->bmc_tx_wlan_idx = cpu_to_le16(wlan_idx);
	basic_req->link_idx = mconf->mt76.idx;

	if (link_sta) {
		struct mt792x_sta *msta;

		msta = (struct mt792x_sta *)link_sta->sta->drv_priv;
		mlink = mt792x_sta_to_link(msta, link_sta->link_id);

	} else {
		mlink = &mconf->vif->sta.deflink;
	}

	basic_req->sta_idx = cpu_to_le16(mlink->wcid.idx);
	basic_req->omac_idx = mconf->mt76.omac_idx;
	basic_req->band_idx = mconf->mt76.band_idx;
	basic_req->wmm_idx = mconf->mt76.wmm_idx;
	basic_req->conn_state = !enable;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GO;
		else
			conn_type = CONNECTION_INFRA_AP;
		basic_req->conn_type = cpu_to_le32(conn_type);
		basic_req->active = enable;
		break;
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GC;
		else
			conn_type = CONNECTION_INFRA_STA;
		basic_req->conn_type = cpu_to_le32(conn_type);
		basic_req->active = true;
		break;
	case NL80211_IFTYPE_ADHOC:
		basic_req->conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		basic_req->active = true;
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static void
mt7925_mcu_bss_sec_tlv(struct sk_buff *skb,
		       struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct mt76_vif_link *mvif = &mconf->mt76;
	struct bss_sec_tlv {
		__le16 tag;
		__le16 len;
		u8 mode;
		u8 status;
		u8 cipher;
		u8 __rsv;
	} __packed * sec;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_SEC, sizeof(*sec));
	sec = (struct bss_sec_tlv *)tlv;

	switch (mvif->cipher) {
	case CONNAC3_CIPHER_GCMP_256:
	case CONNAC3_CIPHER_GCMP:
		sec->mode = MODE_WPA3_SAE;
		sec->status = 8;
		break;
	case CONNAC3_CIPHER_AES_CCMP:
		sec->mode = MODE_WPA2_PSK;
		sec->status = 6;
		break;
	case CONNAC3_CIPHER_TKIP:
		sec->mode = MODE_WPA2_PSK;
		sec->status = 4;
		break;
	case CONNAC3_CIPHER_WEP104:
	case CONNAC3_CIPHER_WEP40:
		sec->mode = MODE_SHARED;
		sec->status = 0;
		break;
	default:
		sec->mode = MODE_OPEN;
		sec->status = 1;
		break;
	}

	sec->cipher = mvif->cipher;
}

static void
mt7925_mcu_bss_bmc_tlv(struct sk_buff *skb, struct mt792x_phy *phy,
		       struct ieee80211_chanctx_conf *ctx,
		       struct ieee80211_bss_conf *link_conf)
{
	struct cfg80211_chan_def *chandef = ctx ? &ctx->def :
						  &link_conf->chanreq.oper;
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	enum nl80211_band band = chandef->chan->band;
	struct mt76_vif_link *mvif = &mconf->mt76;
	struct bss_rate_tlv *bmc;
	struct tlv *tlv;
	u8 idx = mvif->mcast_rates_idx ?
		 mvif->mcast_rates_idx : mvif->basic_rates_idx;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_RATE, sizeof(*bmc));

	bmc = (struct bss_rate_tlv *)tlv;

	if (band == NL80211_BAND_2GHZ)
		bmc->basic_rate = cpu_to_le16(HR_DSSS_ERP_BASIC_RATE);
	else
		bmc->basic_rate = cpu_to_le16(OFDM_BASIC_RATE);

	bmc->short_preamble = (band == NL80211_BAND_2GHZ);
	bmc->bc_fixed_rate = idx;
	bmc->mc_fixed_rate = idx;
}

static void
mt7925_mcu_bss_mld_tlv(struct sk_buff *skb,
		       struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct mt792x_vif *mvif = (struct mt792x_vif *)link_conf->vif->drv_priv;
	struct mt792x_phy *phy = mvif->phy;
	struct bss_mld_tlv *mld;
	struct tlv *tlv;
	bool is_mld;

	is_mld = ieee80211_vif_is_mld(link_conf->vif) ||
		 (hweight16(mvif->valid_links) > 1);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_MLD, sizeof(*mld));
	mld = (struct bss_mld_tlv *)tlv;

	mld->link_id = is_mld ? link_conf->link_id : 0xff;
	/* apply the index of the primary link */
	mld->group_mld_id = is_mld ? mvif->bss_conf.mt76.idx : 0xff;
	mld->own_mld_id = mconf->mt76.idx + 32;
	mld->remap_idx = 0xff;

	if (phy->chip_cap & MT792x_CHIP_CAP_MLO_EML_EN) {
		mld->eml_enable = !!(link_conf->vif->cfg.eml_cap &
				     IEEE80211_EML_CAP_EMLSR_SUPP);
	} else {
		mld->eml_enable = 0;
	}

	memcpy(mld->mac_addr, vif->addr, ETH_ALEN);
}

static void
mt7925_mcu_bss_qos_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *link_conf)
{
	struct mt76_connac_bss_qos_tlv *qos;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_QBSS, sizeof(*qos));
	qos = (struct mt76_connac_bss_qos_tlv *)tlv;
	qos->qos = link_conf->qos;
}

static void
mt7925_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *link_conf,
		      struct mt792x_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_uni_he *he;
	struct tlv *tlv;

	cap = mt76_connac_get_he_phy_cap(phy->mt76, link_conf->vif);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_HE_BASIC, sizeof(*he));

	he = (struct bss_info_uni_he *)tlv;
	he->he_pe_duration = link_conf->htc_trig_based_pkt_ext;
	if (!he->he_pe_duration)
		he->he_pe_duration = DEFAULT_HE_PE_DURATION;

	he->he_rts_thres = cpu_to_le16(link_conf->frame_time_rts_th);
	if (!he->he_rts_thres)
		he->he_rts_thres = cpu_to_le16(DEFAULT_HE_DURATION_RTS_THRES);

	he->max_nss_mcs[CMD_HE_MCS_BW80] = cap->he_mcs_nss_supp.tx_mcs_80;
	he->max_nss_mcs[CMD_HE_MCS_BW160] = cap->he_mcs_nss_supp.tx_mcs_160;
	he->max_nss_mcs[CMD_HE_MCS_BW8080] = cap->he_mcs_nss_supp.tx_mcs_80p80;
}

static void
mt7925_mcu_bss_color_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *link_conf,
			 bool enable)
{
	struct bss_info_uni_bss_color *color;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BSS_COLOR, sizeof(*color));
	color = (struct bss_info_uni_bss_color *)tlv;

	color->enable = enable ?
		link_conf->he_bss_color.enabled : 0;
	color->bss_color = enable ?
		link_conf->he_bss_color.color : 0;
}

static void
mt7925_mcu_bss_ifs_tlv(struct sk_buff *skb,
		       struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)link_conf->vif->drv_priv;
	struct mt792x_phy *phy = mvif->phy;
	struct bss_ifs_time_tlv *ifs_time;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_IFS_TIME, sizeof(*ifs_time));
	ifs_time = (struct bss_ifs_time_tlv *)tlv;
	ifs_time->slot_valid = true;
	ifs_time->slot_time = cpu_to_le16(phy->slottime);
}

int mt7925_mcu_set_timing(struct mt792x_phy *phy,
			  struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct mt792x_dev *dev = phy->dev;
	struct sk_buff *skb;

	skb = __mt7925_mcu_alloc_bss_req(&dev->mt76, &mconf->mt76,
					 MT7925_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7925_mcu_bss_ifs_tlv(skb, link_conf);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD(BSS_INFO_UPDATE), true);
}

void mt7925_mcu_del_dev(struct mt76_dev *mdev,
			struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct {
		struct {
			u8 omac_idx;
			u8 band_idx;
			__le16 pad;
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 link_idx; /* hw link idx */
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} dev_req = {
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = true,
		},
	};
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_bss_basic_tlv basic;
	} basic_req = {
		.basic = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BASIC),
			.len = cpu_to_le16(sizeof(struct mt76_connac_bss_basic_tlv)),
			.active = true,
			.conn_state = 1,
		},
	};

	dev_req.hdr.omac_idx = mvif->omac_idx;
	dev_req.hdr.band_idx = mvif->band_idx;

	basic_req.hdr.bss_idx = mvif->idx;
	basic_req.basic.omac_idx = mvif->omac_idx;
	basic_req.basic.band_idx = mvif->band_idx;
	basic_req.basic.link_idx = mvif->link_idx;

	mt76_mcu_send_msg(mdev, MCU_UNI_CMD(BSS_INFO_UPDATE),
			  &basic_req, sizeof(basic_req), true);

	/* recovery omac address for the legacy interface */
	memcpy(dev_req.tlv.omac_addr, vif->addr, ETH_ALEN);
	mt76_mcu_send_msg(mdev, MCU_UNI_CMD(DEV_INFO_UPDATE),
			  &dev_req, sizeof(dev_req), true);
}

int mt7925_mcu_add_bss_info(struct mt792x_phy *phy,
			    struct ieee80211_chanctx_conf *ctx,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_link_sta *link_sta,
			    int enable)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)link_conf->vif->drv_priv;
	struct mt792x_bss_conf *mconf = mt792x_link_conf_to_mconf(link_conf);
	struct mt792x_dev *dev = phy->dev;
	struct mt792x_link_sta *mlink_bc;
	struct sk_buff *skb;

	skb = __mt7925_mcu_alloc_bss_req(&dev->mt76, &mconf->mt76,
					 MT7925_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mlink_bc = mt792x_sta_to_link(&mvif->sta, mconf->link_id);

	/* bss_basic must be first */
	mt7925_mcu_bss_basic_tlv(skb, link_conf, link_sta, ctx, phy->mt76,
				 mlink_bc->wcid.idx, enable);
	mt7925_mcu_bss_sec_tlv(skb, link_conf);
	mt7925_mcu_bss_bmc_tlv(skb, phy, ctx, link_conf);
	mt7925_mcu_bss_qos_tlv(skb, link_conf);
	mt7925_mcu_bss_mld_tlv(skb, link_conf);
	mt7925_mcu_bss_ifs_tlv(skb, link_conf);

	if (link_conf->he_support) {
		mt7925_mcu_bss_he_tlv(skb, link_conf, phy);
		mt7925_mcu_bss_color_tlv(skb, link_conf, enable);
	}

	if (enable)
		mt7925_mcu_bss_rlm_tlv(skb, phy->mt76, link_conf, ctx);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7925_mcu_set_dbdc(struct mt76_phy *phy, bool enable)
{
	struct mt76_dev *mdev = phy->dev;

	struct mbmc_conf_tlv *conf;
	struct mbmc_set_req *hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int max_len, err;

	max_len = sizeof(*hdr) + sizeof(*conf);
	skb = mt76_mcu_msg_alloc(mdev, NULL, max_len);
	if (!skb)
		return -ENOMEM;

	hdr = (struct mbmc_set_req *)skb_put(skb, sizeof(*hdr));

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_MBMC_SETTING, sizeof(*conf));
	conf = (struct mbmc_conf_tlv *)tlv;

	conf->mbmc_en = enable;
	conf->band = 0; /* unused */

	err = mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(SET_DBDC_PARMS),
				    true);

	return err;
}

int mt7925_mcu_hw_scan(struct mt76_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *scan_req)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct cfg80211_scan_request *sreq = &scan_req->req;
	int n_ssids = 0, err, i;
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt76_dev *mdev = phy->dev;
	struct mt76_connac_mcu_scan_channel *chan;
	struct sk_buff *skb;
	struct scan_hdr_tlv *hdr;
	struct scan_req_tlv *req;
	struct scan_ssid_tlv *ssid;
	struct scan_bssid_tlv *bssid;
	struct scan_chan_info_tlv *chan_info;
	struct scan_ie_tlv *ie;
	struct scan_misc_tlv *misc;
	struct tlv *tlv;
	int max_len;

	if (test_bit(MT76_HW_SCANNING, &phy->state))
		return -EBUSY;

	max_len = sizeof(*hdr) + sizeof(*req) + sizeof(*ssid) +
		  sizeof(*bssid) * MT7925_RNR_SCAN_MAX_BSSIDS +
		  sizeof(*chan_info) + sizeof(*misc) + sizeof(*ie);

	skb = mt76_mcu_msg_alloc(mdev, NULL, max_len);
	if (!skb)
		return -ENOMEM;

	set_bit(MT76_HW_SCANNING, &phy->state);
	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	hdr = (struct scan_hdr_tlv *)skb_put(skb, sizeof(*hdr));
	hdr->seq_num = mvif->scan_seq_num | mvif->band_idx << 7;
	hdr->bss_idx = mvif->idx;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_REQ, sizeof(*req));
	req = (struct scan_req_tlv *)tlv;
	req->scan_type = sreq->n_ssids ? 1 : 0;
	req->probe_req_num = sreq->n_ssids ? 2 : 0;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_SSID, sizeof(*ssid));
	ssid = (struct scan_ssid_tlv *)tlv;
	for (i = 0; i < sreq->n_ssids; i++) {
		if (!sreq->ssids[i].ssid_len)
			continue;
		if (i >= MT7925_RNR_SCAN_MAX_BSSIDS)
			break;

		ssid->ssids[i].ssid_len = cpu_to_le32(sreq->ssids[i].ssid_len);
		memcpy(ssid->ssids[i].ssid, sreq->ssids[i].ssid,
		       sreq->ssids[i].ssid_len);
		n_ssids++;
	}
	ssid->ssid_type = n_ssids ? BIT(2) : BIT(0);
	ssid->ssids_num = n_ssids;

	if (sreq->n_6ghz_params) {
		u8 j;

		mt76_connac_mcu_build_rnr_scan_param(mdev, sreq);

		for (j = 0; j < mdev->rnr.bssid_num; j++) {
			if (j >= MT7925_RNR_SCAN_MAX_BSSIDS)
				break;

			tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_BSSID,
						      sizeof(*bssid));
			bssid = (struct scan_bssid_tlv *)tlv;

			ether_addr_copy(bssid->bssid, mdev->rnr.bssid[j]);
			bssid->match_ch = mdev->rnr.channel[j];
			bssid->match_ssid_ind = MT7925_RNR_SCAN_MAX_BSSIDS;
			bssid->match_short_ssid_ind = MT7925_RNR_SCAN_MAX_BSSIDS;
		}
		req->scan_func |= SCAN_FUNC_RNR_SCAN;
	} else {
		tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_BSSID, sizeof(*bssid));
		bssid = (struct scan_bssid_tlv *)tlv;

		ether_addr_copy(bssid->bssid, sreq->bssid);
	}

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_CHANNEL, sizeof(*chan_info));
	chan_info = (struct scan_chan_info_tlv *)tlv;
	chan_info->channels_num = min_t(u8, sreq->n_channels,
					ARRAY_SIZE(chan_info->channels));
	for (i = 0; i < chan_info->channels_num; i++) {
		chan = &chan_info->channels[i];

		switch (scan_list[i]->band) {
		case NL80211_BAND_2GHZ:
			chan->band = 1;
			break;
		case NL80211_BAND_6GHZ:
			chan->band = 3;
			break;
		default:
			chan->band = 2;
			break;
		}
		chan->channel_num = scan_list[i]->hw_value;
	}
	chan_info->channel_type = sreq->n_channels ? 4 : 0;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_IE, sizeof(*ie));
	ie = (struct scan_ie_tlv *)tlv;
	if (sreq->ie_len > 0) {
		memcpy(ie->ies, sreq->ie, sreq->ie_len);
		ie->ies_len = cpu_to_le16(sreq->ie_len);
	}

	req->scan_func |= SCAN_FUNC_SPLIT_SCAN;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_MISC, sizeof(*misc));
	misc = (struct scan_misc_tlv *)tlv;
	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		get_random_mask_addr(misc->random_mac, sreq->mac_addr,
				     sreq->mac_addr_mask);
		req->scan_func |= SCAN_FUNC_RANDOM_MAC;
	}

	err = mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(SCAN_REQ),
				    true);
	if (err < 0)
		clear_bit(MT76_HW_SCANNING, &phy->state);

	return err;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_hw_scan);

int mt7925_mcu_sched_scan_req(struct mt76_phy *phy,
			      struct ieee80211_vif *vif,
			      struct cfg80211_sched_scan_request *sreq)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt76_connac_mcu_scan_channel *chan;
	struct mt76_dev *mdev = phy->dev;
	struct cfg80211_match_set *cfg_match;
	struct cfg80211_ssid *cfg_ssid;

	struct scan_hdr_tlv *hdr;
	struct scan_sched_req *req;
	struct scan_ssid_tlv *ssid;
	struct scan_chan_info_tlv *chan_info;
	struct scan_ie_tlv *ie;
	struct scan_sched_ssid_match_sets *match;
	struct sk_buff *skb;
	struct tlv *tlv;
	int i, max_len;

	max_len = sizeof(*hdr) + sizeof(*req) + sizeof(*ssid) +
		  sizeof(*chan_info) + sizeof(*ie) +
		  sizeof(*match);

	skb = mt76_mcu_msg_alloc(mdev, NULL, max_len);
	if (!skb)
		return -ENOMEM;

	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	hdr = (struct scan_hdr_tlv *)skb_put(skb, sizeof(*hdr));
	hdr->seq_num = mvif->scan_seq_num | mvif->band_idx << 7;
	hdr->bss_idx = mvif->idx;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_SCHED_REQ, sizeof(*req));
	req = (struct scan_sched_req *)tlv;
	req->version = 1;

	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		req->scan_func |= SCAN_FUNC_RANDOM_MAC;

	req->intervals_num = sreq->n_scan_plans;
	for (i = 0; i < req->intervals_num; i++)
		req->intervals[i] = cpu_to_le16(sreq->scan_plans[i].interval);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_SSID, sizeof(*ssid));
	ssid = (struct scan_ssid_tlv *)tlv;

	ssid->ssids_num = sreq->n_ssids;
	ssid->ssid_type = BIT(2);
	for (i = 0; i < ssid->ssids_num; i++) {
		cfg_ssid = &sreq->ssids[i];
		memcpy(ssid->ssids[i].ssid, cfg_ssid->ssid, cfg_ssid->ssid_len);
		ssid->ssids[i].ssid_len = cpu_to_le32(cfg_ssid->ssid_len);
	}

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_SSID_MATCH_SETS, sizeof(*match));
	match = (struct scan_sched_ssid_match_sets *)tlv;
	match->match_num = sreq->n_match_sets;
	for (i = 0; i < match->match_num; i++) {
		cfg_match = &sreq->match_sets[i];
		memcpy(match->match[i].ssid, cfg_match->ssid.ssid,
		       cfg_match->ssid.ssid_len);
		match->match[i].rssi_th = cpu_to_le32(cfg_match->rssi_thold);
		match->match[i].ssid_len = cfg_match->ssid.ssid_len;
	}

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_CHANNEL, sizeof(*chan_info));
	chan_info = (struct scan_chan_info_tlv *)tlv;
	chan_info->channels_num = min_t(u8, sreq->n_channels,
					ARRAY_SIZE(chan_info->channels));
	for (i = 0; i < chan_info->channels_num; i++) {
		chan = &chan_info->channels[i];

		switch (scan_list[i]->band) {
		case NL80211_BAND_2GHZ:
			chan->band = 1;
			break;
		case NL80211_BAND_6GHZ:
			chan->band = 3;
			break;
		default:
			chan->band = 2;
			break;
		}
		chan->channel_num = scan_list[i]->hw_value;
	}
	chan_info->channel_type = sreq->n_channels ? 4 : 0;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_IE, sizeof(*ie));
	ie = (struct scan_ie_tlv *)tlv;
	if (sreq->ie_len > 0) {
		memcpy(ie->ies, sreq->ie, sreq->ie_len);
		ie->ies_len = cpu_to_le16(sreq->ie_len);
	}

	return mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(SCAN_REQ),
				     true);
}
EXPORT_SYMBOL_GPL(mt7925_mcu_sched_scan_req);

int
mt7925_mcu_sched_scan_enable(struct mt76_phy *phy,
			     struct ieee80211_vif *vif,
			     bool enable)
{
	struct mt76_dev *mdev = phy->dev;
	struct scan_sched_enable *req;
	struct scan_hdr_tlv *hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int max_len;

	max_len = sizeof(*hdr) + sizeof(*req);

	skb = mt76_mcu_msg_alloc(mdev, NULL, max_len);
	if (!skb)
		return -ENOMEM;

	hdr = (struct scan_hdr_tlv *)skb_put(skb, sizeof(*hdr));
	hdr->seq_num = 0;
	hdr->bss_idx = 0;

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_SCAN_SCHED_ENABLE, sizeof(*req));
	req = (struct scan_sched_enable *)tlv;
	req->active = !enable;

	if (enable)
		set_bit(MT76_HW_SCHED_SCANNING, &phy->state);
	else
		clear_bit(MT76_HW_SCHED_SCANNING, &phy->state);

	return mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(SCAN_REQ),
				     true);
}

int mt7925_mcu_cancel_hw_scan(struct mt76_phy *phy,
			      struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;
	struct {
		struct scan_hdr {
			u8 seq_num;
			u8 bss_idx;
			u8 pad[2];
		} __packed hdr;
		struct scan_cancel_tlv {
			__le16 tag;
			__le16 len;
			u8 is_ext_channel;
			u8 rsv[3];
		} __packed cancel;
	} req = {
		.hdr = {
			.seq_num = mvif->scan_seq_num,
			.bss_idx = mvif->idx,
		},
		.cancel = {
			.tag = cpu_to_le16(UNI_SCAN_CANCEL),
			.len = cpu_to_le16(sizeof(struct scan_cancel_tlv)),
		},
	};

	if (test_and_clear_bit(MT76_HW_SCANNING, &phy->state)) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(phy->hw, &info);
	}

	return mt76_mcu_send_msg(phy->dev, MCU_UNI_CMD(SCAN_REQ),
				 &req, sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt7925_mcu_cancel_hw_scan);

int mt7925_mcu_set_channel_domain(struct mt76_phy *phy)
{
	int len, i, n_max_channels, n_2ch = 0, n_5ch = 0, n_6ch = 0;
	struct {
		struct {
			u8 alpha2[4]; /* regulatory_request.alpha2 */
			u8 bw_2g; /* BW_20_40M		0
				   * BW_20M		1
				   * BW_20_40_80M	2
				   * BW_20_40_80_160M	3
				   * BW_20_40_80_8080M	4
				   */
			u8 bw_5g;
			u8 bw_6g;
			u8 pad;
		} __packed hdr;
		struct n_chan {
			__le16 tag;
			__le16 len;
			u8 n_2ch;
			u8 n_5ch;
			u8 n_6ch;
			u8 pad;
		} __packed n_ch;
	} req = {
		.hdr = {
			.bw_2g = 0,
			.bw_5g = 3, /* BW_20_40_80_160M */
			.bw_6g = 3,
		},
		.n_ch = {
			.tag = cpu_to_le16(2),
		},
	};
	struct mt76_connac_mcu_chan {
		__le16 hw_value;
		__le16 pad;
		__le32 flags;
	} __packed channel;
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_channel *chan;
	struct sk_buff *skb;

	n_max_channels = phy->sband_2g.sband.n_channels +
			 phy->sband_5g.sband.n_channels +
			 phy->sband_6g.sband.n_channels;
	len = sizeof(req) + n_max_channels * sizeof(channel);

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, sizeof(req));

	for (i = 0; i < phy->sband_2g.sband.n_channels; i++) {
		chan = &phy->sband_2g.sband.channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
		n_2ch++;
	}
	for (i = 0; i < phy->sband_5g.sband.n_channels; i++) {
		chan = &phy->sband_5g.sband.channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
		n_5ch++;
	}
	for (i = 0; i < phy->sband_6g.sband.n_channels; i++) {
		chan = &phy->sband_6g.sband.channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
		n_6ch++;
	}

	BUILD_BUG_ON(sizeof(dev->alpha2) > sizeof(req.hdr.alpha2));
	memcpy(req.hdr.alpha2, dev->alpha2, sizeof(dev->alpha2));
	req.n_ch.n_2ch = n_2ch;
	req.n_ch.n_5ch = n_5ch;
	req.n_ch.n_6ch = n_6ch;
	len = sizeof(struct n_chan) + (n_2ch + n_5ch + n_6ch) * sizeof(channel);
	req.n_ch.len = cpu_to_le16(len);
	memcpy(__skb_push(skb, sizeof(req)), &req, sizeof(req));

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD(SET_DOMAIN_INFO),
				     true);
}
EXPORT_SYMBOL_GPL(mt7925_mcu_set_channel_domain);

static int
__mt7925_mcu_set_clc(struct mt792x_dev *dev, u8 *alpha2,
		     enum environment_cap env_cap,
		     struct mt7925_clc *clc, u8 idx)
{
	struct mt7925_clc_segment *seg;
	struct sk_buff *skb;
	struct {
		u8 rsv[4];
		__le16 tag;
		__le16 len;

		u8 ver;
		u8 pad0;
		__le16 size;
		u8 idx;
		u8 env;
		u8 acpi_conf;
		u8 pad1;
		u8 alpha2[2];
		u8 type[2];
		u8 rsvd[64];
	} __packed req = {
		.tag = cpu_to_le16(0x3),
		.len = cpu_to_le16(sizeof(req) - 4),

		.idx = idx,
		.env = env_cap,
	};
	int ret, valid_cnt = 0;
	u8 *pos, *last_pos;

	if (!clc)
		return 0;

	req.ver = clc->ver;
	pos = clc->data + sizeof(*seg) * clc->t0.nr_seg;
	last_pos = clc->data + le32_to_cpu(*(__le32 *)(clc->data + 4));
	while (pos < last_pos) {
		struct mt7925_clc_rule *rule = (struct mt7925_clc_rule *)pos;

		pos += sizeof(*rule);
		if (rule->alpha2[0] != alpha2[0] ||
		    rule->alpha2[1] != alpha2[1])
			continue;

		seg = (struct mt7925_clc_segment *)clc->data
			  + rule->seg_idx - 1;

		memcpy(req.alpha2, rule->alpha2, 2);
		memcpy(req.type, rule->type, 2);

		req.size = cpu_to_le16(seg->len);
		dev->phy.clc_chan_conf = clc->ver == 1 ? 0xff : rule->flag;
		skb = __mt76_mcu_msg_alloc(&dev->mt76, &req,
					   le16_to_cpu(req.size) + sizeof(req),
					   sizeof(req), GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		skb_put_data(skb, clc->data + seg->offset, seg->len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_UNI_CMD(SET_POWER_LIMIT),
					    true);
		if (ret < 0)
			return ret;
		valid_cnt++;
	}

	if (!valid_cnt) {
		dev->phy.clc_chan_conf = 0xff;
		return -ENOENT;
	}

	return 0;
}

int mt7925_mcu_set_clc(struct mt792x_dev *dev, u8 *alpha2,
		       enum environment_cap env_cap)
{
	struct mt792x_phy *phy = (struct mt792x_phy *)&dev->phy;
	int i, ret;

	/* submit all clc config */
	for (i = 0; i < ARRAY_SIZE(phy->clc); i++) {
		if (i == MT792x_CLC_BE_CTRL)
			continue;

		ret = __mt7925_mcu_set_clc(dev, alpha2, env_cap,
					   phy->clc[i], i);

		/* If no country found, set "00" as default */
		if (ret == -ENOENT)
			ret = __mt7925_mcu_set_clc(dev, "00",
						   ENVIRON_INDOOR,
						   phy->clc[i], i);
		if (ret < 0)
			return ret;
	}
	return 0;
}

int mt7925_mcu_fill_message(struct mt76_dev *mdev, struct sk_buff *skb,
			    int cmd, int *wait_seq)
{
	int txd_len, mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt76_connac2_mcu_uni_txd *uni_txd;
	struct mt76_connac2_mcu_txd *mcu_txd;
	__le32 *txd;
	u32 val;
	u8 seq;

	/* TODO: make dynamic based on msg type */
	mdev->mcu.timeout = 20 * HZ;

	seq = ++mdev->mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++mdev->mcu.msg_seq & 0xf;

	if (cmd == MCU_CMD(FW_SCATTER))
		goto exit;

	txd_len = cmd & __MCU_CMD_FIELD_UNI ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	if (cmd & __MCU_CMD_FIELD_UNI) {
		uni_txd = (struct mt76_connac2_mcu_uni_txd *)txd;
		uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
		uni_txd->cid = cpu_to_le16(mcu_cmd);
		uni_txd->s2d_index = MCU_S2D_H2N;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;

		if (cmd & __MCU_CMD_FIELD_QUERY)
			uni_txd->option = MCU_CMD_UNI_QUERY_ACK;
		else
			uni_txd->option = MCU_CMD_UNI_EXT_ACK;

		if (cmd == MCU_UNI_CMD(HIF_CTRL) ||
		    cmd == MCU_UNI_CMD(CHIP_CONFIG))
			uni_txd->option &= ~MCU_CMD_ACK;

		if (mcu_cmd == MCU_UNI_CMD_TESTMODE_CTRL ||
		    mcu_cmd == MCU_UNI_CMD_TESTMODE_RX_STAT) {
			if (cmd & __MCU_CMD_FIELD_QUERY)
				uni_txd->option = 0x2;
			else
				uni_txd->option = 0x6;
		}

		goto exit;
	}

	mcu_txd = (struct mt76_connac2_mcu_txd *)txd;
	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU,
					       MT_TX_MCU_PORT_RX_Q0));
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;
	mcu_txd->cid = mcu_cmd;
	mcu_txd->ext_cid = FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd);

	if (mcu_txd->ext_cid || (cmd & __MCU_CMD_FIELD_CE)) {
		if (cmd & __MCU_CMD_FIELD_QUERY)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->ext_cid_ack = !!mcu_txd->ext_cid;
	} else {
		mcu_txd->set_query = MCU_Q_NA;
	}

	if (cmd & __MCU_CMD_FIELD_WA)
		mcu_txd->s2d_index = MCU_S2D_H2C;
	else
		mcu_txd->s2d_index = MCU_S2D_H2N;

exit:
	if (wait_seq)
		*wait_seq = seq;

	return 0;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_fill_message);

int mt7925_mcu_set_rts_thresh(struct mt792x_phy *phy, u32 val)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RTS_THRESHOLD),
		.len = cpu_to_le16(sizeof(req) - 4),
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7925_mcu_set_radio_en(struct mt792x_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RADIO_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

static void
mt7925_mcu_build_sku(struct mt76_dev *dev, s8 *sku,
		     struct mt76_power_limits *limits,
		     enum nl80211_band band)
{
	int i, offset = sizeof(limits->cck);

	memset(sku, 127, MT_CONNAC3_SKU_POWER_LIMIT);

	if (band == NL80211_BAND_2GHZ) {
		/* cck */
		memcpy(sku, limits->cck, sizeof(limits->cck));
	}

	/* ofdm */
	memcpy(&sku[offset], limits->ofdm, sizeof(limits->ofdm));
	offset += (sizeof(limits->ofdm) * 5);

	/* ht */
	for (i = 0; i < 2; i++) {
		memcpy(&sku[offset], limits->mcs[i], 8);
		offset += 8;
	}
	sku[offset++] = limits->mcs[0][0];

	/* vht */
	for (i = 0; i < ARRAY_SIZE(limits->mcs); i++) {
		memcpy(&sku[offset], limits->mcs[i],
		       ARRAY_SIZE(limits->mcs[i]));
		offset += 12;
	}

	/* he */
	for (i = 0; i < ARRAY_SIZE(limits->ru); i++) {
		memcpy(&sku[offset], limits->ru[i], ARRAY_SIZE(limits->ru[i]));
		offset += ARRAY_SIZE(limits->ru[i]);
	}

	/* eht */
	for (i = 0; i < ARRAY_SIZE(limits->eht); i++) {
		memcpy(&sku[offset], limits->eht[i], ARRAY_SIZE(limits->eht[i]));
		offset += ARRAY_SIZE(limits->eht[i]);
	}
}

static int
mt7925_mcu_rate_txpower_band(struct mt76_phy *phy,
			     enum nl80211_band band)
{
	int tx_power, n_chan, last_ch, err = 0, idx = 0;
	int i, sku_len, batch_size, batch_len = 3;
	struct mt76_dev *dev = phy->dev;
	static const u8 chan_list_2ghz[] = {
		1, 2,  3,  4,  5,  6,  7,
		8, 9, 10, 11, 12, 13, 14
	};
	static const u8 chan_list_5ghz[] = {
		 36,  38,  40,  42,  44,  46,  48,
		 50,  52,  54,  56,  58,  60,  62,
		 64, 100, 102, 104, 106, 108, 110,
		112, 114, 116, 118, 120, 122, 124,
		126, 128, 132, 134, 136, 138, 140,
		142, 144, 149, 151, 153, 155, 157,
		159, 161, 165, 167
	};
	static const u8 chan_list_6ghz[] = {
		  1,   3,   5,   7,   9,  11,  13,
		 15,  17,  19,  21,  23,  25,  27,
		 29,  33,  35,  37,  39,  41,  43,
		 45,  47,  49,  51,  53,  55,  57,
		 59,  61,  65,  67,  69,  71,  73,
		 75,  77,  79,  81,  83,  85,  87,
		 89,  91,  93,  97,  99, 101, 103,
		105, 107, 109, 111, 113, 115, 117,
		119, 121, 123, 125, 129, 131, 133,
		135, 137, 139, 141, 143, 145, 147,
		149, 151, 153, 155, 157, 161, 163,
		165, 167, 169, 171, 173, 175, 177,
		179, 181, 183, 185, 187, 189, 193,
		195, 197, 199, 201, 203, 205, 207,
		209, 211, 213, 215, 217, 219, 221,
		225, 227, 229, 233
	};
	struct mt76_power_limits *limits;
	struct mt7925_sku_tlv *sku_tlbv;
	const u8 *ch_list;

	sku_len = sizeof(*sku_tlbv);
	tx_power = 2 * phy->hw->conf.power_level;
	if (!tx_power)
		tx_power = 127;

	if (band == NL80211_BAND_2GHZ) {
		n_chan = ARRAY_SIZE(chan_list_2ghz);
		ch_list = chan_list_2ghz;
		last_ch = chan_list_2ghz[ARRAY_SIZE(chan_list_2ghz) - 1];
	} else if (band == NL80211_BAND_6GHZ) {
		n_chan = ARRAY_SIZE(chan_list_6ghz);
		ch_list = chan_list_6ghz;
		last_ch = chan_list_6ghz[ARRAY_SIZE(chan_list_6ghz) - 1];
	} else {
		n_chan = ARRAY_SIZE(chan_list_5ghz);
		ch_list = chan_list_5ghz;
		last_ch = chan_list_5ghz[ARRAY_SIZE(chan_list_5ghz) - 1];
	}
	batch_size = DIV_ROUND_UP(n_chan, batch_len);

	limits = devm_kmalloc(dev->dev, sizeof(*limits), GFP_KERNEL);
	if (!limits)
		return -ENOMEM;

	sku_tlbv = devm_kmalloc(dev->dev, sku_len, GFP_KERNEL);
	if (!sku_tlbv) {
		devm_kfree(dev->dev, limits);
		return -ENOMEM;
	}

	for (i = 0; i < batch_size; i++) {
		struct mt7925_tx_power_limit_tlv *tx_power_tlv;
		int j, msg_len, num_ch;
		struct sk_buff *skb;

		num_ch = i == batch_size - 1 ? n_chan % batch_len : batch_len;
		msg_len = sizeof(*tx_power_tlv) + num_ch * sku_len;
		skb = mt76_mcu_msg_alloc(dev, NULL, msg_len);
		if (!skb) {
			err = -ENOMEM;
			goto out;
		}

		tx_power_tlv = (struct mt7925_tx_power_limit_tlv *)
			       skb_put(skb, sizeof(*tx_power_tlv));

		BUILD_BUG_ON(sizeof(dev->alpha2) > sizeof(tx_power_tlv->alpha2));
		memcpy(tx_power_tlv->alpha2, dev->alpha2, sizeof(dev->alpha2));
		tx_power_tlv->n_chan = num_ch;
		tx_power_tlv->tag = cpu_to_le16(0x1);
		tx_power_tlv->len = cpu_to_le16(sizeof(*tx_power_tlv));

		switch (band) {
		case NL80211_BAND_2GHZ:
			tx_power_tlv->band = 1;
			break;
		case NL80211_BAND_6GHZ:
			tx_power_tlv->band = 3;
			break;
		default:
			tx_power_tlv->band = 2;
			break;
		}

		for (j = 0; j < num_ch; j++, idx++) {
			struct ieee80211_channel chan = {
				.hw_value = ch_list[idx],
				.band = band,
			};
			s8 reg_power, sar_power;

			reg_power = mt76_connac_get_ch_power(phy, &chan,
							     tx_power);
			sar_power = mt76_get_sar_power(phy, &chan, reg_power);

			mt76_get_rate_power_limits(phy, &chan, limits,
						   sar_power);

			tx_power_tlv->last_msg = ch_list[idx] == last_ch;
			sku_tlbv->channel = ch_list[idx];

			mt7925_mcu_build_sku(dev, sku_tlbv->pwr_limit,
					     limits, band);
			skb_put_data(skb, sku_tlbv, sku_len);
		}
		err = mt76_mcu_skb_send_msg(dev, skb,
					    MCU_UNI_CMD(SET_POWER_LIMIT),
					    true);
		if (err < 0)
			goto out;
	}

out:
	devm_kfree(dev->dev, sku_tlbv);
	devm_kfree(dev->dev, limits);
	return err;
}

int mt7925_mcu_set_rate_txpower(struct mt76_phy *phy)
{
	int err;

	if (phy->cap.has_2ghz) {
		err = mt7925_mcu_rate_txpower_band(phy,
						   NL80211_BAND_2GHZ);
		if (err < 0)
			return err;
	}

	if (phy->cap.has_5ghz) {
		err = mt7925_mcu_rate_txpower_band(phy,
						   NL80211_BAND_5GHZ);
		if (err < 0)
			return err;
	}

	if (phy->cap.has_6ghz) {
		err = mt7925_mcu_rate_txpower_band(phy,
						   NL80211_BAND_6GHZ);
		if (err < 0)
			return err;
	}

	return 0;
}

int mt7925_mcu_wf_rf_pin_ctrl(struct mt792x_phy *phy)
{
#define UNI_CMD_RADIO_STATUS_GET	0
	struct mt792x_dev *dev = phy->dev;
	struct sk_buff *skb;
	int ret;
	struct {
		__le16 tag;
		__le16 len;
		u8 rsv[4];
	} __packed req = {
		.tag = UNI_CMD_RADIO_STATUS_GET,
		.len = cpu_to_le16(sizeof(req)),
	};
	struct mt7925_radio_status_event {
		__le16 tag;
		__le16 len;

		u8 data;
		u8 rsv[3];
	} __packed *status;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_UNI_CMD(RADIO_STATUS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	skb_pull(skb, sizeof(struct tlv));
	status = (struct mt7925_radio_status_event *)skb->data;
	ret = status->data;

	dev_kfree_skb(skb);

	return ret;
}

int mt7925_mcu_set_rxfilter(struct mt792x_dev *dev, u32 fif,
			    u8 bit_op, u32 bit_map)
{
	struct mt792x_phy *phy = &dev->phy;
	struct {
		u8 band_idx;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 mode;
		u8 rsv2[3];
		__le32 fif;
		__le32 bit_map; /* bit_* for bitmap update */
		u8 bit_op;
		u8 pad[51];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_SET_MAC80211_RX_FILTER),
		.len = cpu_to_le16(sizeof(req) - 4),

		.mode = fif ? 0 : 1,
		.fif = cpu_to_le32(fif),
		.bit_map = cpu_to_le32(bit_map),
		.bit_op = bit_op,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}
