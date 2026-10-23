// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2024 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static struct mt76_phy *
mt76_scan_chan_phy(struct ieee80211_hw *hw, struct ieee80211_channel *chan)
{
	struct mt76_phy *phy = hw->priv;

	if (hw->wiphy->n_radio > 1)
		return phy->dev->band_phys[chan->band];

	return phy;
}

static void mt76_scan_phy_complete(struct mt76_phy *phy, bool abort)
{
	struct mt76_dev *dev = phy->dev;
	struct cfg80211_scan_info info = {};
	struct ieee80211_hw *hw = phy->hw;

	lockdep_assert_held(&dev->mutex);

	if (!mt76_phy_scanning(phy))
		return;

	clear_bit(MT76_SCANNING, &phy->state);

	/* Re-program the operating channel even when the scan never left it:
	 * any channel set during the scan ran with MT76_SCANNING held, which
	 * left DFS radar detection disabled
	 */
	if (phy->main_chandef.chan &&
	    !test_bit(MT76_MCU_RESET, &dev->phy.state)) {
		bool offchannel = phy->offchannel;

		__mt76_set_channel(phy, &phy->main_chandef, false);
		if (offchannel)
			mt76_offchannel_notify(phy, false);
	} else {
		/* A phy that has no operating channel has nothing to restore,
		 * but the flag also gates the TX queues of every station whose
		 * wcid points at this band, so leaving it set stalls them until
		 * something else happens to set a channel.
		 */
		phy->offchannel = false;
	}
	mt76_put_vif_phy_link(phy, dev->scan.vif, phy->scan.mlink);
	memset(&phy->scan, 0, sizeof(phy->scan));

	clear_bit(phy->band_idx, &dev->scan.phy_mask);
	if (abort)
		dev->scan.aborted = true;

	if (dev->scan.phy_mask)
		return;

	info.aborted = dev->scan.aborted;
	memset(&dev->scan, 0, sizeof(dev->scan));
	ieee80211_scan_completed(hw, &info);
}

void mt76_abort_scan(struct mt76_dev *dev)
{
	struct mt76_phy *phy;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->phys); i++) {
		phy = dev->phys[i];
		if (!phy)
			continue;

		spin_lock_bh(&dev->scan_lock);
		phy->scan.beacon_wait = false;
		spin_unlock_bh(&dev->scan_lock);

		disable_delayed_work_sync(&phy->scan_work);
	}

	mutex_lock(&dev->mutex);
	for (i = 0; i < ARRAY_SIZE(dev->phys); i++) {
		phy = dev->phys[i];
		if (!phy)
			continue;

		mt76_scan_phy_complete(phy, true);
		enable_delayed_work(&phy->scan_work);
	}
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_abort_scan);

static void
mt76_scan_send_probe(struct mt76_phy *phy, struct cfg80211_ssid *ssid)
{
	struct mt76_dev *dev = phy->dev;
	struct cfg80211_scan_request *req = &dev->scan.req->req;
	struct ieee80211_scan_ies *ies = &dev->scan.req->ies;
	struct ieee80211_vif *vif = dev->scan.vif;
	struct mt76_vif_link *mvif = phy->scan.mlink;
	enum nl80211_band band = phy->scan.chan->band;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	u8 link_id;

	skb = ieee80211_probereq_get(phy->hw, vif->addr, ssid->ssid,
				     ssid->ssid_len,
				     ies->len[band] + ies->common_ie_len);
	if (!skb)
		return;

	if (is_unicast_ether_addr(req->bssid)) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

		ether_addr_copy(hdr->addr1, req->bssid);
		ether_addr_copy(hdr->addr3, req->bssid);
	}

	if (ies->len[band])
		skb_put_data(skb, ies->ies[band], ies->len[band]);
	if (ies->common_ie_len)
		skb_put_data(skb, ies->common_ies, ies->common_ie_len);

	skb->priority = 7;
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);

	rcu_read_lock();

	if (!ieee80211_tx_prepare_skb(phy->hw, vif, skb, band, NULL))
		goto out;

	info = IEEE80211_SKB_CB(skb);
	if (req->no_cck)
		info->flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
	info->control.flags |= IEEE80211_TX_CTRL_DONT_USE_RATE_MASK;

	link_id = mvif->wcid ? mvif->wcid->link_id : IEEE80211_LINK_UNSPECIFIED;
	info->control.flags &= ~IEEE80211_TX_CTRL_MLO_LINK;
	info->control.flags |= u32_encode_bits(link_id, IEEE80211_TX_CTRL_MLO_LINK);

	mt76_tx(phy, NULL, mvif->wcid, skb);

out:
	rcu_read_unlock();
}

void mt76_scan_rx_beacon(struct mt76_phy *phy, struct ieee80211_channel *chan)
{
	struct mt76_dev *dev = phy->dev;

	spin_lock(&dev->scan_lock);

	if (!phy->scan.beacon_wait || phy->scan.beacon_received ||
	    phy->scan.chan != chan)
		goto out;

	phy->scan.beacon_received = true;
	ieee80211_queue_delayed_work(phy->hw, &phy->scan_work, 0);

out:
	spin_unlock(&dev->scan_lock);
}

static int mt76_scan_next_chan_idx(struct mt76_phy *phy)
{
	struct cfg80211_scan_request *req = &phy->dev->scan.req->req;
	int i;

	for (i = phy->scan.chan_idx; i < req->n_channels; i++)
		if (mt76_scan_chan_phy(phy->hw, req->channels[i]) == phy)
			return i;

	return -1;
}

void mt76_scan_work(struct work_struct *work)
{
	struct mt76_phy *phy = container_of(work, struct mt76_phy,
					    scan_work.work);
	struct mt76_dev *dev = phy->dev;
	struct cfg80211_scan_request *req;
	struct cfg80211_chan_def chandef = {};
	int duration = HZ / 9; /* ~110 ms */
	bool beacon_rx, offchannel = true;
	int i, idx;

	if (!dev->scan.req || !mt76_phy_scanning(phy))
		return;

	req = &dev->scan.req->req;

	spin_lock_bh(&dev->scan_lock);
	beacon_rx = phy->scan.beacon_wait && phy->scan.beacon_received;
	phy->scan.beacon_wait = false;
	spin_unlock_bh(&dev->scan_lock);

	if (beacon_rx)
		goto probe;

	idx = mt76_scan_next_chan_idx(phy);
	if (idx < 0) {
		mutex_lock(&dev->mutex);
		mt76_scan_phy_complete(phy, false);
		mutex_unlock(&dev->mutex);
		return;
	}

	if (phy->scan.chan && phy->num_sta && phy->offchannel) {
		phy->scan.chan = NULL;
		mt76_set_channel(phy, &phy->main_chandef, false);
		mt76_offchannel_notify(phy, false);
		goto out;
	}

	phy->scan.chan = req->channels[idx];
	phy->scan.chan_idx = idx + 1;
	offchannel = mt76_offchannel_chandef(phy, phy->scan.chan, &chandef);

	if (offchannel)
		mt76_offchannel_notify(phy, true);
	mt76_set_channel(phy, &chandef, offchannel);

	if (!req->n_ssids)
		goto out;

	if (chandef.chan->flags & (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR)) {
		spin_lock_bh(&dev->scan_lock);
		phy->scan.beacon_received = false;
		phy->scan.beacon_wait = true;
		spin_unlock_bh(&dev->scan_lock);
		goto out;
	}

probe:
	if (phy->offchannel)
		duration = HZ / 16; /* ~60 ms */
	local_bh_disable();
	for (i = 0; i < req->n_ssids; i++)
		mt76_scan_send_probe(phy, &req->ssids[i]);
	local_bh_enable();

out:
	if (phy->scan.chan && phy->offchannel)
		duration = max_t(int, duration,
			         msecs_to_jiffies(req->duration +
						  (req->duration >> 5)));

	ieee80211_queue_delayed_work(phy->hw, &phy->scan_work, duration);
}

int mt76_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_scan_request *req)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	unsigned long phy_mask = 0;
	struct mt76_vif_link *mlink;
	int i, ret = 0;

	for (i = 0; i < req->req.n_channels; i++) {
		phy = mt76_scan_chan_phy(hw, req->req.channels[i]);
		if (!phy)
			return -EINVAL;

		phy_mask |= BIT(phy->band_idx);
	}

	mutex_lock(&dev->mutex);

	if (dev->scan.req || test_bit(MT76_MCU_RESET, &dev->phy.state) ||
	    test_bit(MT76_RESTART, &dev->phy.state)) {
		ret = -EBUSY;
		goto out;
	}

	for_each_set_bit(i, &phy_mask, ARRAY_SIZE(dev->phys)) {
		if (dev->phys[i]->roc_vif) {
			ret = -EBUSY;
			goto out;
		}
	}

	for_each_set_bit(i, &phy_mask, ARRAY_SIZE(dev->phys)) {
		phy = dev->phys[i];
		mlink = mt76_get_vif_phy_link(phy, vif);
		if (IS_ERR(mlink)) {
			ret = PTR_ERR(mlink);
			goto put_links;
		}

		memset(&phy->scan, 0, sizeof(phy->scan));
		phy->scan.mlink = mlink;
	}

	dev->scan.req = req;
	dev->scan.vif = vif;
	dev->scan.phy_mask = phy_mask;
	dev->scan.aborted = false;

	for_each_set_bit(i, &phy_mask, ARRAY_SIZE(dev->phys)) {
		phy = dev->phys[i];
		set_bit(MT76_SCANNING, &phy->state);
		ieee80211_queue_delayed_work(phy->hw, &phy->scan_work, 0);
	}

	goto out;

put_links:
	for_each_set_bit(i, &phy_mask, ARRAY_SIZE(dev->phys)) {
		phy = dev->phys[i];
		if (!phy->scan.mlink)
			break;

		mt76_put_vif_phy_link(phy, vif, phy->scan.mlink);
		phy->scan.mlink = NULL;
	}

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
