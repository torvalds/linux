/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/slab.h>

#include "ath9k.h"

struct ath9k_vif_iter_data {
	const u8 *hw_macaddr;
	u8 mask[ETH_ALEN];
};

static void ath9k_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath9k_vif_iter_data *iter_data = data;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		iter_data->mask[i] &= ~(iter_data->hw_macaddr[i] ^ mac[i]);
}

void ath9k_set_bssid_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath9k_vif_iter_data iter_data;
	int i;

	/*
	 * Use the hardware MAC address as reference, the hardware uses it
	 * together with the BSSID mask when matching addresses.
	 */
	iter_data.hw_macaddr = common->macaddr;
	memset(&iter_data.mask, 0xff, ETH_ALEN);

	if (vif)
		ath9k_vif_iter(&iter_data, vif->addr, vif);

	/* Get list of all active MAC addresses */
	spin_lock_bh(&sc->wiphy_lock);
	ieee80211_iterate_active_interfaces_atomic(sc->hw, ath9k_vif_iter,
						   &iter_data);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] == NULL)
			continue;
		ieee80211_iterate_active_interfaces_atomic(
			sc->sec_wiphy[i]->hw, ath9k_vif_iter, &iter_data);
	}
	spin_unlock_bh(&sc->wiphy_lock);

	memcpy(common->bssidmask, iter_data.mask, ETH_ALEN);
	ath_hw_setbssidmask(common);
}

int ath9k_wiphy_add(struct ath_softc *sc)
{
	int i, error;
	struct ath_wiphy *aphy;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_hw *hw;
	u8 addr[ETH_ALEN];

	hw = ieee80211_alloc_hw(sizeof(struct ath_wiphy), &ath9k_ops);
	if (hw == NULL)
		return -ENOMEM;

	spin_lock_bh(&sc->wiphy_lock);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] == NULL)
			break;
	}

	if (i == sc->num_sec_wiphy) {
		/* No empty slot available; increase array length */
		struct ath_wiphy **n;
		n = krealloc(sc->sec_wiphy,
			     (sc->num_sec_wiphy + 1) *
			     sizeof(struct ath_wiphy *),
			     GFP_ATOMIC);
		if (n == NULL) {
			spin_unlock_bh(&sc->wiphy_lock);
			ieee80211_free_hw(hw);
			return -ENOMEM;
		}
		n[i] = NULL;
		sc->sec_wiphy = n;
		sc->num_sec_wiphy++;
	}

	SET_IEEE80211_DEV(hw, sc->dev);

	aphy = hw->priv;
	aphy->sc = sc;
	aphy->hw = hw;
	sc->sec_wiphy[i] = aphy;
	spin_unlock_bh(&sc->wiphy_lock);

	memcpy(addr, common->macaddr, ETH_ALEN);
	addr[0] |= 0x02; /* Locally managed address */
	/*
	 * XOR virtual wiphy index into the least significant bits to generate
	 * a different MAC address for each virtual wiphy.
	 */
	addr[5] ^= i & 0xff;
	addr[4] ^= (i & 0xff00) >> 8;
	addr[3] ^= (i & 0xff0000) >> 16;

	SET_IEEE80211_PERM_ADDR(hw, addr);

	ath9k_set_hw_capab(sc, hw);

	error = ieee80211_register_hw(hw);

	if (error == 0) {
		/* Make sure wiphy scheduler is started (if enabled) */
		ath9k_wiphy_set_scheduler(sc, sc->wiphy_scheduler_int);
	}

	return error;
}

int ath9k_wiphy_del(struct ath_wiphy *aphy)
{
	struct ath_softc *sc = aphy->sc;
	int i;

	spin_lock_bh(&sc->wiphy_lock);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (aphy == sc->sec_wiphy[i]) {
			sc->sec_wiphy[i] = NULL;
			spin_unlock_bh(&sc->wiphy_lock);
			ieee80211_unregister_hw(aphy->hw);
			ieee80211_free_hw(aphy->hw);
			return 0;
		}
	}
	spin_unlock_bh(&sc->wiphy_lock);
	return -ENOENT;
}

static int ath9k_send_nullfunc(struct ath_wiphy *aphy,
			       struct ieee80211_vif *vif, const u8 *bssid,
			       int ps)
{
	struct ath_softc *sc = aphy->sc;
	struct ath_tx_control txctl;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	__le16 fc;
	struct ieee80211_tx_info *info;

	skb = dev_alloc_skb(24);
	if (skb == NULL)
		return -ENOMEM;
	hdr = (struct ieee80211_hdr *) skb_put(skb, 24);
	memset(hdr, 0, 24);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_TODS);
	if (ps)
		fc |= cpu_to_le16(IEEE80211_FCTL_PM);
	hdr->frame_control = fc;
	memcpy(hdr->addr1, bssid, ETH_ALEN);
	memcpy(hdr->addr2, aphy->hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr3, bssid, ETH_ALEN);

	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->flags = IEEE80211_TX_CTL_REQ_TX_STATUS;
	info->control.vif = vif;
	info->control.rates[0].idx = 0;
	info->control.rates[0].count = 4;
	info->control.rates[1].idx = -1;

	memset(&txctl, 0, sizeof(struct ath_tx_control));
	txctl.txq = &sc->tx.txq[sc->tx.hwq_map[WME_AC_VO]];
	txctl.frame_type = ps ? ATH9K_IFT_PAUSE : ATH9K_IFT_UNPAUSE;

	if (ath_tx_start(aphy->hw, skb, &txctl) != 0)
		goto exit;

	return 0;
exit:
	dev_kfree_skb_any(skb);
	return -1;
}

static bool __ath9k_wiphy_pausing(struct ath_softc *sc)
{
	int i;
	if (sc->pri_wiphy->state == ATH_WIPHY_PAUSING)
		return true;
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state == ATH_WIPHY_PAUSING)
			return true;
	}
	return false;
}

static bool ath9k_wiphy_pausing(struct ath_softc *sc)
{
	bool ret;
	spin_lock_bh(&sc->wiphy_lock);
	ret = __ath9k_wiphy_pausing(sc);
	spin_unlock_bh(&sc->wiphy_lock);
	return ret;
}

static bool __ath9k_wiphy_scanning(struct ath_softc *sc)
{
	int i;
	if (sc->pri_wiphy->state == ATH_WIPHY_SCAN)
		return true;
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state == ATH_WIPHY_SCAN)
			return true;
	}
	return false;
}

bool ath9k_wiphy_scanning(struct ath_softc *sc)
{
	bool ret;
	spin_lock_bh(&sc->wiphy_lock);
	ret = __ath9k_wiphy_scanning(sc);
	spin_unlock_bh(&sc->wiphy_lock);
	return ret;
}

static int __ath9k_wiphy_unpause(struct ath_wiphy *aphy);

/* caller must hold wiphy_lock */
static void __ath9k_wiphy_unpause_ch(struct ath_wiphy *aphy)
{
	if (aphy == NULL)
		return;
	if (aphy->chan_idx != aphy->sc->chan_idx)
		return; /* wiphy not on the selected channel */
	__ath9k_wiphy_unpause(aphy);
}

static void ath9k_wiphy_unpause_channel(struct ath_softc *sc)
{
	int i;
	spin_lock_bh(&sc->wiphy_lock);
	__ath9k_wiphy_unpause_ch(sc->pri_wiphy);
	for (i = 0; i < sc->num_sec_wiphy; i++)
		__ath9k_wiphy_unpause_ch(sc->sec_wiphy[i]);
	spin_unlock_bh(&sc->wiphy_lock);
}

void ath9k_wiphy_chan_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, chan_work);
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_wiphy *aphy = sc->next_wiphy;

	if (aphy == NULL)
		return;

	/*
	 * All pending interfaces paused; ready to change
	 * channels.
	 */

	/* Change channels */
	mutex_lock(&sc->mutex);
	/* XXX: remove me eventually */
	ath9k_update_ichannel(sc, aphy->hw,
			      &sc->sc_ah->channels[sc->chan_idx]);

	/* sync hw configuration for hw code */
	common->hw = aphy->hw;

	ath_update_chainmask(sc, sc->chan_is_ht);
	if (ath_set_channel(sc, aphy->hw,
			    &sc->sc_ah->channels[sc->chan_idx]) < 0) {
		printk(KERN_DEBUG "ath9k: Failed to set channel for new "
		       "virtual wiphy\n");
		mutex_unlock(&sc->mutex);
		return;
	}
	mutex_unlock(&sc->mutex);

	ath9k_wiphy_unpause_channel(sc);
}

/*
 * ath9k version of ieee80211_tx_status() for TX frames that are generated
 * internally in the driver.
 */
void ath9k_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if ((tx_info->pad[0] & ATH_TX_INFO_FRAME_TYPE_PAUSE) &&
	    aphy->state == ATH_WIPHY_PAUSING) {
		if (!(tx_info->flags & IEEE80211_TX_STAT_ACK)) {
			printk(KERN_DEBUG "ath9k: %s: no ACK for pause "
			       "frame\n", wiphy_name(hw->wiphy));
			/*
			 * The AP did not reply; ignore this to allow us to
			 * continue.
			 */
		}
		aphy->state = ATH_WIPHY_PAUSED;
		if (!ath9k_wiphy_pausing(aphy->sc)) {
			/*
			 * Drop from tasklet to work to allow mutex for channel
			 * change.
			 */
			ieee80211_queue_work(aphy->sc->hw,
				   &aphy->sc->chan_work);
		}
	}

	dev_kfree_skb(skb);
}

static void ath9k_mark_paused(struct ath_wiphy *aphy)
{
	struct ath_softc *sc = aphy->sc;
	aphy->state = ATH_WIPHY_PAUSED;
	if (!__ath9k_wiphy_pausing(sc))
		ieee80211_queue_work(sc->hw, &sc->chan_work);
}

static void ath9k_pause_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = data;
	struct ath_vif *avp = (void *) vif->drv_priv;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!vif->bss_conf.assoc) {
			ath9k_mark_paused(aphy);
			break;
		}
		/* TODO: could avoid this if already in PS mode */
		if (ath9k_send_nullfunc(aphy, vif, avp->bssid, 1)) {
			printk(KERN_DEBUG "%s: failed to send PS nullfunc\n",
			       __func__);
			ath9k_mark_paused(aphy);
		}
		break;
	case NL80211_IFTYPE_AP:
		/* Beacon transmission is paused by aphy->state change */
		ath9k_mark_paused(aphy);
		break;
	default:
		break;
	}
}

/* caller must hold wiphy_lock */
static int __ath9k_wiphy_pause(struct ath_wiphy *aphy)
{
	ieee80211_stop_queues(aphy->hw);
	aphy->state = ATH_WIPHY_PAUSING;
	/*
	 * TODO: handle PAUSING->PAUSED for the case where there are multiple
	 * active vifs (now we do it on the first vif getting ready; should be
	 * on the last)
	 */
	ieee80211_iterate_active_interfaces_atomic(aphy->hw, ath9k_pause_iter,
						   aphy);
	return 0;
}

int ath9k_wiphy_pause(struct ath_wiphy *aphy)
{
	int ret;
	spin_lock_bh(&aphy->sc->wiphy_lock);
	ret = __ath9k_wiphy_pause(aphy);
	spin_unlock_bh(&aphy->sc->wiphy_lock);
	return ret;
}

static void ath9k_unpause_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = data;
	struct ath_vif *avp = (void *) vif->drv_priv;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!vif->bss_conf.assoc)
			break;
		ath9k_send_nullfunc(aphy, vif, avp->bssid, 0);
		break;
	case NL80211_IFTYPE_AP:
		/* Beacon transmission is re-enabled by aphy->state change */
		break;
	default:
		break;
	}
}

/* caller must hold wiphy_lock */
static int __ath9k_wiphy_unpause(struct ath_wiphy *aphy)
{
	ieee80211_iterate_active_interfaces_atomic(aphy->hw,
						   ath9k_unpause_iter, aphy);
	aphy->state = ATH_WIPHY_ACTIVE;
	ieee80211_wake_queues(aphy->hw);
	return 0;
}

int ath9k_wiphy_unpause(struct ath_wiphy *aphy)
{
	int ret;
	spin_lock_bh(&aphy->sc->wiphy_lock);
	ret = __ath9k_wiphy_unpause(aphy);
	spin_unlock_bh(&aphy->sc->wiphy_lock);
	return ret;
}

static void __ath9k_wiphy_mark_all_paused(struct ath_softc *sc)
{
	int i;
	if (sc->pri_wiphy->state != ATH_WIPHY_INACTIVE)
		sc->pri_wiphy->state = ATH_WIPHY_PAUSED;
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state != ATH_WIPHY_INACTIVE)
			sc->sec_wiphy[i]->state = ATH_WIPHY_PAUSED;
	}
}

/* caller must hold wiphy_lock */
static void __ath9k_wiphy_pause_all(struct ath_softc *sc)
{
	int i;
	if (sc->pri_wiphy->state == ATH_WIPHY_ACTIVE)
		__ath9k_wiphy_pause(sc->pri_wiphy);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state == ATH_WIPHY_ACTIVE)
			__ath9k_wiphy_pause(sc->sec_wiphy[i]);
	}
}

int ath9k_wiphy_select(struct ath_wiphy *aphy)
{
	struct ath_softc *sc = aphy->sc;
	bool now;

	spin_lock_bh(&sc->wiphy_lock);
	if (__ath9k_wiphy_scanning(sc)) {
		/*
		 * For now, we are using mac80211 sw scan and it expects to
		 * have full control over channel changes, so avoid wiphy
		 * scheduling during a scan. This could be optimized if the
		 * scanning control were moved into the driver.
		 */
		spin_unlock_bh(&sc->wiphy_lock);
		return -EBUSY;
	}
	if (__ath9k_wiphy_pausing(sc)) {
		if (sc->wiphy_select_failures == 0)
			sc->wiphy_select_first_fail = jiffies;
		sc->wiphy_select_failures++;
		if (time_after(jiffies, sc->wiphy_select_first_fail + HZ / 2))
		{
			printk(KERN_DEBUG "ath9k: Previous wiphy select timed "
			       "out; disable/enable hw to recover\n");
			__ath9k_wiphy_mark_all_paused(sc);
			/*
			 * TODO: this workaround to fix hardware is unlikely to
			 * be specific to virtual wiphy changes. It can happen
			 * on normal channel change, too, and as such, this
			 * should really be made more generic. For example,
			 * tricker radio disable/enable on GTT interrupt burst
			 * (say, 10 GTT interrupts received without any TX
			 * frame being completed)
			 */
			spin_unlock_bh(&sc->wiphy_lock);
			ath_radio_disable(sc, aphy->hw);
			ath_radio_enable(sc, aphy->hw);
			/* Only the primary wiphy hw is used for queuing work */
			ieee80211_queue_work(aphy->sc->hw,
				   &aphy->sc->chan_work);
			return -EBUSY; /* previous select still in progress */
		}
		spin_unlock_bh(&sc->wiphy_lock);
		return -EBUSY; /* previous select still in progress */
	}
	sc->wiphy_select_failures = 0;

	/* Store the new channel */
	sc->chan_idx = aphy->chan_idx;
	sc->chan_is_ht = aphy->chan_is_ht;
	sc->next_wiphy = aphy;

	__ath9k_wiphy_pause_all(sc);
	now = !__ath9k_wiphy_pausing(aphy->sc);
	spin_unlock_bh(&sc->wiphy_lock);

	if (now) {
		/* Ready to request channel change immediately */
		ieee80211_queue_work(aphy->sc->hw, &aphy->sc->chan_work);
	}

	/*
	 * wiphys will be unpaused in ath9k_tx_status() once channel has been
	 * changed if any wiphy needs time to become paused.
	 */

	return 0;
}

bool ath9k_wiphy_started(struct ath_softc *sc)
{
	int i;
	spin_lock_bh(&sc->wiphy_lock);
	if (sc->pri_wiphy->state != ATH_WIPHY_INACTIVE) {
		spin_unlock_bh(&sc->wiphy_lock);
		return true;
	}
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state != ATH_WIPHY_INACTIVE) {
			spin_unlock_bh(&sc->wiphy_lock);
			return true;
		}
	}
	spin_unlock_bh(&sc->wiphy_lock);
	return false;
}

static void ath9k_wiphy_pause_chan(struct ath_wiphy *aphy,
				   struct ath_wiphy *selected)
{
	if (selected->state == ATH_WIPHY_SCAN) {
		if (aphy == selected)
			return;
		/*
		 * Pause all other wiphys for the duration of the scan even if
		 * they are on the current channel now.
		 */
	} else if (aphy->chan_idx == selected->chan_idx)
		return;
	aphy->state = ATH_WIPHY_PAUSED;
	ieee80211_stop_queues(aphy->hw);
}

void ath9k_wiphy_pause_all_forced(struct ath_softc *sc,
				  struct ath_wiphy *selected)
{
	int i;
	spin_lock_bh(&sc->wiphy_lock);
	if (sc->pri_wiphy->state == ATH_WIPHY_ACTIVE)
		ath9k_wiphy_pause_chan(sc->pri_wiphy, selected);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i] &&
		    sc->sec_wiphy[i]->state == ATH_WIPHY_ACTIVE)
			ath9k_wiphy_pause_chan(sc->sec_wiphy[i], selected);
	}
	spin_unlock_bh(&sc->wiphy_lock);
}

void ath9k_wiphy_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    wiphy_work.work);
	struct ath_wiphy *aphy = NULL;
	bool first = true;

	spin_lock_bh(&sc->wiphy_lock);

	if (sc->wiphy_scheduler_int == 0) {
		/* wiphy scheduler is disabled */
		spin_unlock_bh(&sc->wiphy_lock);
		return;
	}

try_again:
	sc->wiphy_scheduler_index++;
	while (sc->wiphy_scheduler_index <= sc->num_sec_wiphy) {
		aphy = sc->sec_wiphy[sc->wiphy_scheduler_index - 1];
		if (aphy && aphy->state != ATH_WIPHY_INACTIVE)
			break;

		sc->wiphy_scheduler_index++;
		aphy = NULL;
	}
	if (aphy == NULL) {
		sc->wiphy_scheduler_index = 0;
		if (sc->pri_wiphy->state == ATH_WIPHY_INACTIVE) {
			if (first) {
				first = false;
				goto try_again;
			}
			/* No wiphy is ready to be scheduled */
		} else
			aphy = sc->pri_wiphy;
	}

	spin_unlock_bh(&sc->wiphy_lock);

	if (aphy &&
	    aphy->state != ATH_WIPHY_ACTIVE && aphy->state != ATH_WIPHY_SCAN &&
	    ath9k_wiphy_select(aphy)) {
		printk(KERN_DEBUG "ath9k: Failed to schedule virtual wiphy "
		       "change\n");
	}

	ieee80211_queue_delayed_work(sc->hw,
				     &sc->wiphy_work,
				     sc->wiphy_scheduler_int);
}

void ath9k_wiphy_set_scheduler(struct ath_softc *sc, unsigned int msec_int)
{
	cancel_delayed_work_sync(&sc->wiphy_work);
	sc->wiphy_scheduler_int = msecs_to_jiffies(msec_int);
	if (sc->wiphy_scheduler_int)
		ieee80211_queue_delayed_work(sc->hw, &sc->wiphy_work,
					     sc->wiphy_scheduler_int);
}

/* caller must hold wiphy_lock */
bool ath9k_all_wiphys_idle(struct ath_softc *sc)
{
	unsigned int i;
	if (!sc->pri_wiphy->idle)
		return false;
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (!aphy)
			continue;
		if (!aphy->idle)
			return false;
	}
	return true;
}

/* caller must hold wiphy_lock */
void ath9k_set_wiphy_idle(struct ath_wiphy *aphy, bool idle)
{
	struct ath_softc *sc = aphy->sc;

	aphy->idle = idle;
	ath_print(ath9k_hw_common(sc->sc_ah), ATH_DBG_CONFIG,
		  "Marking %s as %s\n",
		  wiphy_name(aphy->hw->wiphy),
		  idle ? "idle" : "not-idle");
}
/* Only bother starting a queue on an active virtual wiphy */
bool ath_mac80211_start_queue(struct ath_softc *sc, u16 skb_queue)
{
	struct ieee80211_hw *hw = sc->pri_wiphy->hw;
	unsigned int i;
	bool txq_started = false;

	spin_lock_bh(&sc->wiphy_lock);

	/* Start the primary wiphy */
	if (sc->pri_wiphy->state == ATH_WIPHY_ACTIVE) {
		ieee80211_wake_queue(hw, skb_queue);
		txq_started = true;
		goto unlock;
	}

	/* Now start the secondary wiphy queues */
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (!aphy)
			continue;
		if (aphy->state != ATH_WIPHY_ACTIVE)
			continue;

		hw = aphy->hw;
		ieee80211_wake_queue(hw, skb_queue);
		txq_started = true;
		break;
	}

unlock:
	spin_unlock_bh(&sc->wiphy_lock);
	return txq_started;
}

/* Go ahead and propagate information to all virtual wiphys, it won't hurt */
void ath_mac80211_stop_queue(struct ath_softc *sc, u16 skb_queue)
{
	struct ieee80211_hw *hw = sc->pri_wiphy->hw;
	unsigned int i;

	spin_lock_bh(&sc->wiphy_lock);

	/* Stop the primary wiphy */
	ieee80211_stop_queue(hw, skb_queue);

	/* Now stop the secondary wiphy queues */
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (!aphy)
			continue;
		hw = aphy->hw;
		ieee80211_stop_queue(hw, skb_queue);
	}
	spin_unlock_bh(&sc->wiphy_lock);
}
