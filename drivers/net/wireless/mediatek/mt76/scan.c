// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static void mt76_scan_complete(struct mt76_dev *dev, bool abort)
{
	struct mt76_phy *phy = dev->scan.phy;
	struct cfg80211_scan_info info = {
		.aborted = abort,
	};

	if (!phy)
		return;

	clear_bit(MT76_SCANNING, &phy->state);

	if (dev->scan.chan && phy->main_chandef.chan)
		mt76_set_channel(phy, &phy->main_chandef, false);
	mt76_put_vif_phy_link(phy, dev->scan.vif, dev->scan.mlink);
	memset(&dev->scan, 0, sizeof(dev->scan));
	ieee80211_scan_completed(phy->hw, &info);
}

void mt76_abort_scan(struct mt76_dev *dev)
{
	cancel_delayed_work_sync(&dev->scan_work);
	mt76_scan_complete(dev, true);
}

static void
mt76_scan_send_probe(struct mt76_dev *dev, struct cfg80211_ssid *ssid)
{
	struct cfg80211_scan_request *req = dev->scan.req;
	struct ieee80211_vif *vif = dev->scan.vif;
	struct mt76_vif_link *mvif = dev->scan.mlink;
	enum nl80211_band band = dev->scan.chan->band;
	struct mt76_phy *phy = dev->scan.phy;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	skb = ieee80211_probereq_get(phy->hw, vif->addr, ssid->ssid,
				     ssid->ssid_len, req->ie_len);
	if (!skb)
		return;

	if (is_unicast_ether_addr(req->bssid)) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

		ether_addr_copy(hdr->addr1, req->bssid);
		ether_addr_copy(hdr->addr3, req->bssid);
	}

	if (req->ie_len)
		skb_put_data(skb, req->ie, req->ie_len);

	skb->priority = 7;
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);

	rcu_read_lock();

	if (!ieee80211_tx_prepare_skb(phy->hw, vif, skb, band, NULL)) {
		ieee80211_free_txskb(phy->hw, skb);
		goto out;
	}

	info = IEEE80211_SKB_CB(skb);
	if (req->no_cck)
		info->flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
	info->control.flags |= IEEE80211_TX_CTRL_DONT_USE_RATE_MASK;

	mt76_tx(phy, NULL, mvif->wcid, skb);

out:
	rcu_read_unlock();
}

void mt76_scan_work(struct work_struct *work)
{
	struct mt76_dev *dev = container_of(work, struct mt76_dev,
					    scan_work.work);
	struct cfg80211_scan_request *req = dev->scan.req;
	struct cfg80211_chan_def chandef = {};
	struct mt76_phy *phy = dev->scan.phy;
	int duration = HZ / 9; /* ~110 ms */
	int i;

	if (dev->scan.chan_idx >= req->n_channels) {
		mt76_scan_complete(dev, false);
		return;
	}

	if (dev->scan.chan && phy->num_sta) {
		dev->scan.chan = NULL;
		mt76_set_channel(phy, &phy->main_chandef, false);
		goto out;
	}

	dev->scan.chan = req->channels[dev->scan.chan_idx++];
	cfg80211_chandef_create(&chandef, dev->scan.chan, NL80211_CHAN_HT20);
	mt76_set_channel(phy, &chandef, true);

	if (!req->n_ssids ||
	    chandef.chan->flags & (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR))
		goto out;

	duration = HZ / 16; /* ~60 ms */
	local_bh_disable();
	for (i = 0; i < req->n_ssids; i++)
		mt76_scan_send_probe(dev, &req->ssids[i]);
	local_bh_enable();

out:
	if (!duration)
		return;

	if (dev->scan.chan)
		duration = max_t(int, duration,
			         msecs_to_jiffies(req->duration +
						  (req->duration >> 5)));

	ieee80211_queue_delayed_work(dev->phy.hw, &dev->scan_work, duration);
}

int mt76_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_scan_request *req)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_link *mlink;
	int ret = 0;

	if (hw->wiphy->n_radio > 1) {
		phy = dev->band_phys[req->req.channels[0]->band];
		if (!phy)
			return -EINVAL;
	}

	mutex_lock(&dev->mutex);

	if (dev->scan.req || phy->roc_vif) {
		ret = -EBUSY;
		goto out;
	}

	mlink = mt76_get_vif_phy_link(phy, vif);
	if (IS_ERR(mlink)) {
		ret = PTR_ERR(mlink);
		goto out;
	}

	memset(&dev->scan, 0, sizeof(dev->scan));
	dev->scan.req = &req->req;
	dev->scan.vif = vif;
	dev->scan.phy = phy;
	dev->scan.mlink = mlink;
	ieee80211_queue_delayed_work(dev->phy.hw, &dev->scan_work, 0);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_hw_scan);

void mt76_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = hw->priv;

	mt76_abort_scan(phy->dev);
}
EXPORT_SYMBOL_GPL(mt76_cancel_hw_scan);
