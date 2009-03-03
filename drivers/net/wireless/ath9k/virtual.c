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

#include "ath9k.h"

struct ath9k_vif_iter_data {
	int count;
	u8 *addr;
};

static void ath9k_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath9k_vif_iter_data *iter_data = data;
	u8 *nbuf;

	nbuf = krealloc(iter_data->addr, (iter_data->count + 1) * ETH_ALEN,
			GFP_ATOMIC);
	if (nbuf == NULL)
		return;

	memcpy(nbuf + iter_data->count * ETH_ALEN, mac, ETH_ALEN);
	iter_data->addr = nbuf;
	iter_data->count++;
}

void ath9k_set_bssid_mask(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath9k_vif_iter_data iter_data;
	int i, j;
	u8 mask[ETH_ALEN];

	/*
	 * Add primary MAC address even if it is not in active use since it
	 * will be configured to the hardware as the starting point and the
	 * BSSID mask will need to be changed if another address is active.
	 */
	iter_data.addr = kmalloc(ETH_ALEN, GFP_ATOMIC);
	if (iter_data.addr) {
		memcpy(iter_data.addr, sc->sc_ah->macaddr, ETH_ALEN);
		iter_data.count = 1;
	} else
		iter_data.count = 0;

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

	/* Generate an address mask to cover all active addresses */
	memset(mask, 0, ETH_ALEN);
	for (i = 0; i < iter_data.count; i++) {
		u8 *a1 = iter_data.addr + i * ETH_ALEN;
		for (j = i + 1; j < iter_data.count; j++) {
			u8 *a2 = iter_data.addr + j * ETH_ALEN;
			mask[0] |= a1[0] ^ a2[0];
			mask[1] |= a1[1] ^ a2[1];
			mask[2] |= a1[2] ^ a2[2];
			mask[3] |= a1[3] ^ a2[3];
			mask[4] |= a1[4] ^ a2[4];
			mask[5] |= a1[5] ^ a2[5];
		}
	}

	kfree(iter_data.addr);

	/* Invert the mask and configure hardware */
	sc->bssidmask[0] = ~mask[0];
	sc->bssidmask[1] = ~mask[1];
	sc->bssidmask[2] = ~mask[2];
	sc->bssidmask[3] = ~mask[3];
	sc->bssidmask[4] = ~mask[4];
	sc->bssidmask[5] = ~mask[5];

	ath9k_hw_setbssidmask(sc);
}

int ath9k_wiphy_add(struct ath_softc *sc)
{
	int i, error;
	struct ath_wiphy *aphy;
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

	memcpy(addr, sc->sc_ah->macaddr, ETH_ALEN);
	addr[0] |= 0x02; /* Locally managed address */
	/*
	 * XOR virtual wiphy index into the least significant bits to generate
	 * a different MAC address for each virtual wiphy.
	 */
	addr[5] ^= i & 0xff;
	addr[4] ^= (i & 0xff00) >> 8;
	addr[3] ^= (i & 0xff0000) >> 16;

	SET_IEEE80211_PERM_ADDR(hw, addr);

	ath_set_hw_capab(sc, hw);

	error = ieee80211_register_hw(hw);

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
	txctl.txq = &sc->tx.txq[sc->tx.hwq_map[ATH9K_WME_AC_VO]];
	txctl.frame_type = ps ? ATH9K_INT_PAUSE : ATH9K_INT_UNPAUSE;

	if (ath_tx_start(aphy->hw, skb, &txctl) != 0)
		goto exit;

	return 0;
exit:
	dev_kfree_skb_any(skb);
	return -1;
}

/*
 * ath9k version of ieee80211_tx_status() for TX frames that are generated
 * internally in the driver.
 */
void ath9k_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_tx_info_priv *tx_info_priv = ATH_TX_INFO_PRIV(tx_info);

	if (tx_info_priv && tx_info_priv->frame_type == ATH9K_INT_PAUSE &&
	    aphy->state == ATH_WIPHY_PAUSING) {
		if (!(info->flags & IEEE80211_TX_STAT_ACK)) {
			printk(KERN_DEBUG "ath9k: %s: no ACK for pause "
			       "frame\n", wiphy_name(hw->wiphy));
			/*
			 * The AP did not reply; ignore this to allow us to
			 * continue.
			 */
		}
		aphy->state = ATH_WIPHY_PAUSED;
	}

	kfree(tx_info_priv);
	tx_info->rate_driver_data[0] = NULL;

	dev_kfree_skb(skb);
}

static void ath9k_pause_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = data;
	struct ath_vif *avp = (void *) vif->drv_priv;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!vif->bss_conf.assoc) {
			aphy->state = ATH_WIPHY_PAUSED;
			break;
		}
		/* TODO: could avoid this if already in PS mode */
		ath9k_send_nullfunc(aphy, vif, avp->bssid, 1);
		break;
	case NL80211_IFTYPE_AP:
		/* Beacon transmission is paused by aphy->state change */
		aphy->state = ATH_WIPHY_PAUSED;
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
