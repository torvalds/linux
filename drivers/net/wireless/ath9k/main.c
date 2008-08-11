/*
 * Copyright (c) 2008 Atheros Communications Inc.
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

/* mac80211 and PCI callbacks */

#include <linux/nl80211.h>
#include "core.h"

#define ATH_PCI_VERSION "0.1"

#define IEEE80211_HTCAP_MAXRXAMPDU_FACTOR	13
#define IEEE80211_ACTION_CAT_HT			7
#define IEEE80211_ACTION_HT_TXCHWIDTH		0

static char *dev_info = "ath9k";

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Support for Atheros 802.11n wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_device_id ath_pci_id_table[] __devinitdata = {
	{ PCI_VDEVICE(ATHEROS, 0x0023) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0024) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0027) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0029) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002A) }, /* PCI-E */
	{ 0 }
};

static int ath_get_channel(struct ath_softc *sc,
			   struct ieee80211_channel *chan)
{
	int i;

	for (i = 0; i < sc->sc_ah->ah_nchan; i++) {
		if (sc->sc_ah->ah_channels[i].channel == chan->center_freq)
			return i;
	}

	return -1;
}

static u32 ath_get_extchanmode(struct ath_softc *sc,
				     struct ieee80211_channel *chan)
{
	u32 chanmode = 0;
	u8 ext_chan_offset = sc->sc_ht_info.ext_chan_offset;
	enum ath9k_ht_macmode tx_chan_width = sc->sc_ht_info.tx_chan_width;

	switch (chan->band) {
	case IEEE80211_BAND_2GHZ:
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_NONE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_20))
			chanmode = CHANNEL_G_HT20;
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_ABOVE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_G_HT40PLUS;
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_BELOW) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_G_HT40MINUS;
		break;
	case IEEE80211_BAND_5GHZ:
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_NONE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_20))
			chanmode = CHANNEL_A_HT20;
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_ABOVE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_A_HT40PLUS;
		if ((ext_chan_offset == IEEE80211_HT_IE_CHA_SEC_BELOW) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_A_HT40MINUS;
		break;
	default:
		break;
	}

	return chanmode;
}


static int ath_setkey_tkip(struct ath_softc *sc,
			   struct ieee80211_key_conf *key,
			   struct ath9k_keyval *hk,
			   const u8 *addr)
{
	u8 *key_rxmic = NULL;
	u8 *key_txmic = NULL;

	key_txmic = key->key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY;
	key_rxmic = key->key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY;

	if (addr == NULL) {
		/* Group key installation */
		memcpy(hk->kv_mic,  key_rxmic, sizeof(hk->kv_mic));
		return ath_keyset(sc, key->keyidx, hk, addr);
	}
	if (!sc->sc_splitmic) {
		/*
		 * data key goes at first index,
		 * the hal handles the MIC keys at index+64.
		 */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_txmic));
		return ath_keyset(sc, key->keyidx, hk, addr);
	}
	/*
	 * TX key goes at first index, RX key at +32.
	 * The hal handles the MIC keys at index+64.
	 */
	memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
	if (!ath_keyset(sc, key->keyidx, hk, NULL)) {
		/* Txmic entry failed. No need to proceed further */
		DPRINTF(sc, ATH_DBG_KEYCACHE,
			"%s Setting TX MIC Key Failed\n", __func__);
		return 0;
	}

	memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
	/* XXX delete tx key on failure? */
	return ath_keyset(sc, key->keyidx+32, hk, addr);
}

static int ath_key_config(struct ath_softc *sc,
			  const u8 *addr,
			  struct ieee80211_key_conf *key)
{
	struct ieee80211_vif *vif;
	struct ath9k_keyval hk;
	const u8 *mac = NULL;
	int ret = 0;
	enum ieee80211_if_types opmode;

	memset(&hk, 0, sizeof(hk));

	switch (key->alg) {
	case ALG_WEP:
		hk.kv_type = ATH9K_CIPHER_WEP;
		break;
	case ALG_TKIP:
		hk.kv_type = ATH9K_CIPHER_TKIP;
		break;
	case ALG_CCMP:
		hk.kv_type = ATH9K_CIPHER_AES_CCM;
		break;
	default:
		return -EINVAL;
	}

	hk.kv_len  = key->keylen;
	memcpy(hk.kv_val, key->key, key->keylen);

	if (!sc->sc_vaps[0])
		return -EIO;

	vif = sc->sc_vaps[0]->av_if_data;
	opmode = vif->type;

	/*
	 *  Strategy:
	 *   For _M_STA mc tx, we will not setup a key at all since we never
	 *   tx mc.
	 *   _M_STA mc rx, we will use the keyID.
	 *   for _M_IBSS mc tx, we will use the keyID, and no macaddr.
	 *   for _M_IBSS mc rx, we will alloc a slot and plumb the mac of the
	 *   peer node. BUT we will plumb a cleartext key so that we can do
	 *   perSta default key table lookup in software.
	 */
	if (is_broadcast_ether_addr(addr)) {
		switch (opmode) {
		case IEEE80211_IF_TYPE_STA:
			/* default key:  could be group WPA key
			 * or could be static WEP key */
			mac = NULL;
			break;
		case IEEE80211_IF_TYPE_IBSS:
			break;
		case IEEE80211_IF_TYPE_AP:
			break;
		default:
			ASSERT(0);
			break;
		}
	} else {
		mac = addr;
	}

	if (key->alg == ALG_TKIP)
		ret = ath_setkey_tkip(sc, key, &hk, mac);
	else
		ret = ath_keyset(sc, key->keyidx, &hk, mac);

	if (!ret)
		return -EIO;

	sc->sc_keytype = hk.kv_type;
	return 0;
}

static void ath_key_delete(struct ath_softc *sc, struct ieee80211_key_conf *key)
{
#define ATH_MAX_NUM_KEYS 4
	int freeslot;

	freeslot = (key->keyidx >= ATH_MAX_NUM_KEYS) ? 1 : 0;
	ath_key_reset(sc, key->keyidx, freeslot);
#undef ATH_MAX_NUM_KEYS
}

static void setup_ht_cap(struct ieee80211_ht_info *ht_info)
{
/* Until mac80211 includes these fields */

#define IEEE80211_HT_CAP_DSSSCCK40 0x1000
#define	IEEE80211_HT_CAP_MAXRXAMPDU_65536 0x3   /* 2 ^ 16 */
#define	IEEE80211_HT_CAP_MPDUDENSITY_8 0x6     	/* 8 usec */

	ht_info->ht_supported = 1;
	ht_info->cap = (u16)IEEE80211_HT_CAP_SUP_WIDTH
			|(u16)IEEE80211_HT_CAP_MIMO_PS
			|(u16)IEEE80211_HT_CAP_SGI_40
			|(u16)IEEE80211_HT_CAP_DSSSCCK40;

	ht_info->ampdu_factor = IEEE80211_HT_CAP_MAXRXAMPDU_65536;
	ht_info->ampdu_density = IEEE80211_HT_CAP_MPDUDENSITY_8;
	/* setup supported mcs set */
	memset(ht_info->supp_mcs_set, 0, 16);
	ht_info->supp_mcs_set[0] = 0xff;
	ht_info->supp_mcs_set[1] = 0xff;
	ht_info->supp_mcs_set[12] = IEEE80211_HT_CAP_MCS_TX_DEFINED;
}

static int ath_rate2idx(struct ath_softc *sc, int rate)
{
	int i = 0, cur_band, n_rates;
	struct ieee80211_hw *hw = sc->hw;

	cur_band = hw->conf.channel->band;
	n_rates = sc->sbands[cur_band].n_bitrates;

	for (i = 0; i < n_rates; i++) {
		if (sc->sbands[cur_band].bitrates[i].bitrate == rate)
			break;
	}

	/*
	 * NB:mac80211 validates rx rate index against the supported legacy rate
	 * index only (should be done against ht rates also), return the highest
	 * legacy rate index for rx rate which does not match any one of the
	 * supported basic and extended rates to make mac80211 happy.
	 * The following hack will be cleaned up once the issue with
	 * the rx rate index validation in mac80211 is fixed.
	 */
	if (i == n_rates)
		return n_rates - 1;
	return i;
}

static void ath9k_rx_prepare(struct ath_softc *sc,
			     struct sk_buff *skb,
			     struct ath_recv_status *status,
			     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_channel *curchan = hw->conf.channel;

	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	rx_status->mactime = status->tsf;
	rx_status->band = curchan->band;
	rx_status->freq =  curchan->center_freq;
	rx_status->noise = ATH_DEFAULT_NOISE_FLOOR;
	rx_status->signal = rx_status->noise + status->rssi;
	rx_status->rate_idx = ath_rate2idx(sc, (status->rateKbps / 100));
	rx_status->antenna = status->antenna;
	rx_status->qual = status->rssi * 100 / 64;

	if (status->flags & ATH_RX_MIC_ERROR)
		rx_status->flag |= RX_FLAG_MMIC_ERROR;
	if (status->flags & ATH_RX_FCS_ERROR)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	rx_status->flag |= RX_FLAG_TSFT;
}

static u8 parse_mpdudensity(u8 mpdudensity)
{
	/*
	 * 802.11n D2.0 defined values for "Minimum MPDU Start Spacing":
	 *   0 for no restriction
	 *   1 for 1/4 us
	 *   2 for 1/2 us
	 *   3 for 1 us
	 *   4 for 2 us
	 *   5 for 4 us
	 *   6 for 8 us
	 *   7 for 16 us
	 */
	switch (mpdudensity) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
		/* Our lower layer calculations limit our precision to
		   1 microsecond */
		return 1;
	case 4:
		return 2;
	case 5:
		return 4;
	case 6:
		return 8;
	case 7:
		return 16;
	default:
		return 0;
	}
}

static int ath9k_start(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_channel *curchan = hw->conf.channel;
	int error = 0, pos;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Starting driver with "
		"initial channel: %d MHz\n", __func__, curchan->center_freq);

	/* setup initial channel */

	pos = ath_get_channel(sc, curchan);
	if (pos == -1) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid channel\n", __func__);
		return -EINVAL;
	}

	sc->sc_ah->ah_channels[pos].chanmode =
		(curchan->band == IEEE80211_BAND_2GHZ) ? CHANNEL_G : CHANNEL_A;

	/* open ath_dev */
	error = ath_open(sc, &sc->sc_ah->ah_channels[pos]);
	if (error) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to complete ath_open\n", __func__);
		return error;
	}

	ieee80211_wake_queues(hw);
	return 0;
}

static int ath9k_tx(struct ieee80211_hw *hw,
		    struct sk_buff *skb)
{
	struct ath_softc *sc = hw->priv;
	int hdrlen, padsize;

	/* Add the padding after the header if this is not already done */
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	if (hdrlen & 3) {
		padsize = hdrlen % 4;
		if (skb_headroom(skb) < padsize)
			return -1;
		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, hdrlen);
	}

	DPRINTF(sc, ATH_DBG_XMIT, "%s: transmitting packet, skb: %p\n",
		__func__,
		skb);

	if (ath_tx_start(sc, skb) != 0) {
		DPRINTF(sc, ATH_DBG_XMIT, "%s: TX failed\n", __func__);
		dev_kfree_skb_any(skb);
		/* FIXME: Check for proper return value from ATH_DEV */
		return 0;
	}

	return 0;
}

static void ath9k_stop(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	int error;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Driver halt\n", __func__);

	error = ath_suspend(sc);
	if (error)
		DPRINTF(sc, ATH_DBG_CONFIG,
			"%s: Device is no longer present\n", __func__);

	ieee80211_stop_queues(hw);
}

static int ath9k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_if_init_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	int error, ic_opmode = 0;

	/* Support only vap for now */

	if (sc->sc_nvaps)
		return -ENOBUFS;

	switch (conf->type) {
	case IEEE80211_IF_TYPE_STA:
		ic_opmode = ATH9K_M_STA;
		break;
	case IEEE80211_IF_TYPE_IBSS:
		ic_opmode = ATH9K_M_IBSS;
		break;
	case IEEE80211_IF_TYPE_AP:
		ic_opmode = ATH9K_M_HOSTAP;
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Interface type %d not yet supported\n",
			__func__, conf->type);
		return -EOPNOTSUPP;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Attach a VAP of type: %d\n",
		__func__,
		ic_opmode);

	error = ath_vap_attach(sc, 0, conf->vif, ic_opmode);
	if (error) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to attach vap, error: %d\n",
			__func__, error);
		return error;
	}

	return 0;
}

static void ath9k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_if_init_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_vap *avp;
	int error;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Detach VAP\n", __func__);

	avp = sc->sc_vaps[0];
	if (avp == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid interface\n",
			__func__);
		return;
	}

#ifdef CONFIG_SLOW_ANT_DIV
	ath_slow_ant_div_stop(&sc->sc_antdiv);
#endif

	/* Update ratectrl */
	ath_rate_newstate(sc, avp);

	/* Reclaim beacon resources */
	if (sc->sc_ah->ah_opmode == ATH9K_M_HOSTAP ||
	    sc->sc_ah->ah_opmode == ATH9K_M_IBSS) {
		ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);
		ath_beacon_return(sc, avp);
	}

	/* Set interrupt mask */
	sc->sc_imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	ath9k_hw_set_interrupts(sc->sc_ah, sc->sc_imask & ~ATH9K_INT_GLOBAL);
	sc->sc_flags &= ~SC_OP_BEACONS;

	error = ath_vap_detach(sc, 0);
	if (error)
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to detach vap, error: %d\n",
			__func__, error);
}

static int ath9k_config(struct ieee80211_hw *hw,
			struct ieee80211_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_channel *curchan = hw->conf.channel;
	int pos;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set channel: %d MHz\n",
		__func__,
		curchan->center_freq);

	pos = ath_get_channel(sc, curchan);
	if (pos == -1) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid channel\n", __func__);
		return -EINVAL;
	}

	sc->sc_ah->ah_channels[pos].chanmode =
		(curchan->band == IEEE80211_BAND_2GHZ) ?
		CHANNEL_G : CHANNEL_A;

	if (sc->sc_curaid && hw->conf.ht_conf.ht_supported)
		sc->sc_ah->ah_channels[pos].chanmode =
			ath_get_extchanmode(sc, curchan);

	sc->sc_config.txpowlimit = 2 * conf->power_level;

	/* set h/w channel */
	if (ath_set_channel(sc, &sc->sc_ah->ah_channels[pos]) < 0)
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Unable to set channel\n",
			__func__);

	return 0;
}

static int ath9k_config_interface(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_if_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_vap *avp;
	u32 rfilt = 0;
	int error, i;
	DECLARE_MAC_BUF(mac);

	avp = sc->sc_vaps[0];
	if (avp == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid interface\n",
			__func__);
		return -EINVAL;
	}

	/* TODO: Need to decide which hw opmode to use for multi-interface
	 * cases */
	if (vif->type == IEEE80211_IF_TYPE_AP &&
	    ah->ah_opmode != ATH9K_M_HOSTAP) {
		ah->ah_opmode = ATH9K_M_HOSTAP;
		ath9k_hw_setopmode(ah);
		ath9k_hw_write_associd(ah, sc->sc_myaddr, 0);
		/* Request full reset to get hw opmode changed properly */
		sc->sc_flags |= SC_OP_FULL_RESET;
	}

	if ((conf->changed & IEEE80211_IFCC_BSSID) &&
	    !is_zero_ether_addr(conf->bssid)) {
		switch (vif->type) {
		case IEEE80211_IF_TYPE_STA:
		case IEEE80211_IF_TYPE_IBSS:
			/* Update ratectrl about the new state */
			ath_rate_newstate(sc, avp);

			/* Set BSSID */
			memcpy(sc->sc_curbssid, conf->bssid, ETH_ALEN);
			sc->sc_curaid = 0;
			ath9k_hw_write_associd(sc->sc_ah, sc->sc_curbssid,
					       sc->sc_curaid);

			/* Set aggregation protection mode parameters */
			sc->sc_config.ath_aggr_prot = 0;

			/*
			 * Reset our TSF so that its value is lower than the
			 * beacon that we are trying to catch.
			 * Only then hw will update its TSF register with the
			 * new beacon. Reset the TSF before setting the BSSID
			 * to avoid allowing in any frames that would update
			 * our TSF only to have us clear it
			 * immediately thereafter.
			 */
			ath9k_hw_reset_tsf(sc->sc_ah);

			/* Disable BMISS interrupt when we're not associated */
			ath9k_hw_set_interrupts(sc->sc_ah,
					sc->sc_imask &
					~(ATH9K_INT_SWBA | ATH9K_INT_BMISS));
			sc->sc_imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);

			DPRINTF(sc, ATH_DBG_CONFIG,
				"%s: RX filter 0x%x bssid %s aid 0x%x\n",
				__func__, rfilt,
				print_mac(mac, sc->sc_curbssid), sc->sc_curaid);

			/* need to reconfigure the beacon */
			sc->sc_flags &= ~SC_OP_BEACONS ;

			break;
		default:
			break;
		}
	}

	if ((conf->changed & IEEE80211_IFCC_BEACON) &&
	    (vif->type == IEEE80211_IF_TYPE_IBSS)) {
		/*
		 * Allocate and setup the beacon frame.
		 *
		 * Stop any previous beacon DMA.  This may be
		 * necessary, for example, when an ibss merge
		 * causes reconfiguration; we may be called
		 * with beacon transmission active.
		 */
		ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);

		error = ath_beacon_alloc(sc, 0);
		if (error != 0)
			return error;

		ath_beacon_sync(sc, 0);
	}

	if ((conf->changed & IEEE80211_IFCC_BEACON) &&
	    (vif->type == IEEE80211_IF_TYPE_AP)) {
		ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);

		error = ath_beacon_alloc(sc, 0);
		if (error != 0)
			return error;

		ath_beacon_config(sc, 0);
		sc->sc_flags |= SC_OP_BEACONS;
	}

	/* Check for WLAN_CAPABILITY_PRIVACY ? */
	if ((avp->av_opmode != IEEE80211_IF_TYPE_STA)) {
		for (i = 0; i < IEEE80211_WEP_NKID; i++)
			if (ath9k_hw_keyisvalid(sc->sc_ah, (u16)i))
				ath9k_hw_keysetmac(sc->sc_ah,
						   (u16)i,
						   sc->sc_curbssid);
	}

	/* Only legacy IBSS for now */
	if (vif->type == IEEE80211_IF_TYPE_IBSS)
		ath_update_chainmask(sc, 0);

	return 0;
}

#define SUPPORTED_FILTERS			\
	(FIF_PROMISC_IN_BSS |			\
	FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_FCSFAIL)

/* FIXME: sc->sc_full_reset ? */
static void ath9k_configure_filter(struct ieee80211_hw *hw,
				   unsigned int changed_flags,
				   unsigned int *total_flags,
				   int mc_count,
				   struct dev_mc_list *mclist)
{
	struct ath_softc *sc = hw->priv;
	u32 rfilt;

	changed_flags &= SUPPORTED_FILTERS;
	*total_flags &= SUPPORTED_FILTERS;

	sc->rx_filter = *total_flags;
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(sc->sc_ah, rfilt);

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			ath9k_hw_write_associd(sc->sc_ah, ath_bcast_mac, 0);
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set HW RX filter: 0x%x\n",
		__func__, sc->rx_filter);
}

static void ath9k_sta_notify(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     enum sta_notify_cmd cmd,
			     const u8 *addr)
{
	struct ath_softc *sc = hw->priv;
	struct ath_node *an;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	spin_lock_irqsave(&sc->node_lock, flags);
	an = ath_node_find(sc, (u8 *) addr);
	spin_unlock_irqrestore(&sc->node_lock, flags);

	switch (cmd) {
	case STA_NOTIFY_ADD:
		spin_lock_irqsave(&sc->node_lock, flags);
		if (!an) {
			ath_node_attach(sc, (u8 *)addr, 0);
			DPRINTF(sc, ATH_DBG_CONFIG, "%s: Attach a node: %s\n",
				__func__,
				print_mac(mac, addr));
		} else {
			ath_node_get(sc, (u8 *)addr);
		}
		spin_unlock_irqrestore(&sc->node_lock, flags);
		break;
	case STA_NOTIFY_REMOVE:
		if (!an)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Removal of a non-existent node\n",
				__func__);
		else {
			ath_node_put(sc, an, ATH9K_BH_STATUS_INTACT);
			DPRINTF(sc, ATH_DBG_CONFIG, "%s: Put a node: %s\n",
				__func__,
				print_mac(mac, addr));
		}
		break;
	default:
		break;
	}
}

static int ath9k_conf_tx(struct ieee80211_hw *hw,
			 u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct ath_softc *sc = hw->priv;
	struct ath9k_tx_queue_info qi;
	int ret = 0, qnum;

	if (queue >= WME_NUM_AC)
		return 0;

	qi.tqi_aifs = params->aifs;
	qi.tqi_cwmin = params->cw_min;
	qi.tqi_cwmax = params->cw_max;
	qi.tqi_burstTime = params->txop;
	qnum = ath_get_hal_qnum(queue, sc);

	DPRINTF(sc, ATH_DBG_CONFIG,
		"%s: Configure tx [queue/halq] [%d/%d],  "
		"aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		__func__,
		queue,
		qnum,
		params->aifs,
		params->cw_min,
		params->cw_max,
		params->txop);

	ret = ath_txq_update(sc, qnum, &qi);
	if (ret)
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: TXQ Update failed\n", __func__);

	return ret;
}

static int ath9k_set_key(struct ieee80211_hw *hw,
			 enum set_key_cmd cmd,
			 const u8 *local_addr,
			 const u8 *addr,
			 struct ieee80211_key_conf *key)
{
	struct ath_softc *sc = hw->priv;
	int ret = 0;

	DPRINTF(sc, ATH_DBG_KEYCACHE, " %s: Set HW Key\n", __func__);

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(sc, addr, key);
		if (!ret) {
			set_bit(key->keyidx, sc->sc_keymap);
			key->hw_key_idx = key->keyidx;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(sc, key);
		clear_bit(key->keyidx, sc->sc_keymap);
		sc->sc_keytype = ATH9K_CIPHER_CLR;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void ath9k_ht_conf(struct ath_softc *sc,
			  struct ieee80211_bss_conf *bss_conf)
{
#define IEEE80211_HT_CAP_40MHZ_INTOLERANT BIT(14)
	struct ath_ht_info *ht_info = &sc->sc_ht_info;

	if (bss_conf->assoc_ht) {
		ht_info->ext_chan_offset =
			bss_conf->ht_bss_conf->bss_cap &
				IEEE80211_HT_IE_CHA_SEC_OFFSET;

		if (!(bss_conf->ht_conf->cap &
			IEEE80211_HT_CAP_40MHZ_INTOLERANT) &&
			    (bss_conf->ht_bss_conf->bss_cap &
				IEEE80211_HT_IE_CHA_WIDTH))
			ht_info->tx_chan_width = ATH9K_HT_MACMODE_2040;
		else
			ht_info->tx_chan_width = ATH9K_HT_MACMODE_20;

		ath9k_hw_set11nmac2040(sc->sc_ah, ht_info->tx_chan_width);
		ht_info->maxampdu = 1 << (IEEE80211_HTCAP_MAXRXAMPDU_FACTOR +
					bss_conf->ht_conf->ampdu_factor);
		ht_info->mpdudensity =
			parse_mpdudensity(bss_conf->ht_conf->ampdu_density);

	}

#undef IEEE80211_HT_CAP_40MHZ_INTOLERANT
}

static void ath9k_bss_assoc_info(struct ath_softc *sc,
				 struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath_vap *avp;
	int pos;
	DECLARE_MAC_BUF(mac);

	if (bss_conf->assoc) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: Bss Info ASSOC %d\n",
			__func__,
			bss_conf->aid);

		avp = sc->sc_vaps[0];
		if (avp == NULL) {
			DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid interface\n",
				__func__);
			return;
		}

		/* New association, store aid */
		if (avp->av_opmode == ATH9K_M_STA) {
			sc->sc_curaid = bss_conf->aid;
			ath9k_hw_write_associd(sc->sc_ah, sc->sc_curbssid,
					       sc->sc_curaid);
		}

		/* Configure the beacon */
		ath_beacon_config(sc, 0);
		sc->sc_flags |= SC_OP_BEACONS;

		/* Reset rssi stats */
		sc->sc_halstats.ns_avgbrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgtxrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgtxrate = ATH_RATE_DUMMY_MARKER;

		/* Update chainmask */
		ath_update_chainmask(sc, bss_conf->assoc_ht);

		DPRINTF(sc, ATH_DBG_CONFIG,
			"%s: bssid %s aid 0x%x\n",
			__func__,
			print_mac(mac, sc->sc_curbssid), sc->sc_curaid);

		DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set channel: %d MHz\n",
			__func__,
			curchan->center_freq);

		pos = ath_get_channel(sc, curchan);
		if (pos == -1) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Invalid channel\n", __func__);
			return;
		}

		if (hw->conf.ht_conf.ht_supported)
			sc->sc_ah->ah_channels[pos].chanmode =
				ath_get_extchanmode(sc, curchan);
		else
			sc->sc_ah->ah_channels[pos].chanmode =
				(curchan->band == IEEE80211_BAND_2GHZ) ?
				CHANNEL_G : CHANNEL_A;

		/* set h/w channel */
		if (ath_set_channel(sc, &sc->sc_ah->ah_channels[pos]) < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to set channel\n",
				__func__);

		ath_rate_newstate(sc, avp);
		/* Update ratectrl about the new state */
		ath_rc_node_update(hw, avp->rc_node);
	} else {
		DPRINTF(sc, ATH_DBG_CONFIG,
		"%s: Bss Info DISSOC\n", __func__);
		sc->sc_curaid = 0;
	}
}

static void ath9k_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf,
				   u32 changed)
{
	struct ath_softc *sc = hw->priv;

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed PREAMBLE %d\n",
			__func__,
			bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			sc->sc_flags |= SC_OP_PREAMBLE_SHORT;
		else
			sc->sc_flags &= ~SC_OP_PREAMBLE_SHORT;
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed CTS PROT %d\n",
			__func__,
			bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
		    hw->conf.channel->band != IEEE80211_BAND_5GHZ)
			sc->sc_flags |= SC_OP_PROTECT_ENABLE;
		else
			sc->sc_flags &= ~SC_OP_PROTECT_ENABLE;
	}

	if (changed & BSS_CHANGED_HT) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed HT %d\n",
			__func__,
			bss_conf->assoc_ht);
		ath9k_ht_conf(sc, bss_conf);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed ASSOC %d\n",
			__func__,
			bss_conf->assoc);
		ath9k_bss_assoc_info(sc, bss_conf);
	}
}

static u64 ath9k_get_tsf(struct ieee80211_hw *hw)
{
	u64 tsf;
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;

	tsf = ath9k_hw_gettsf64(ah);

	return tsf;
}

static void ath9k_reset_tsf(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;

	ath9k_hw_reset_tsf(ah);
}

static int ath9k_ampdu_action(struct ieee80211_hw *hw,
		       enum ieee80211_ampdu_mlme_action action,
		       const u8 *addr,
		       u16 tid,
		       u16 *ssn)
{
	struct ath_softc *sc = hw->priv;
	int ret = 0;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		ret = ath_rx_aggr_start(sc, addr, tid, ssn);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to start RX aggregation\n",
				__func__);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = ath_rx_aggr_stop(sc, addr, tid);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to stop RX aggregation\n",
				__func__);
		break;
	case IEEE80211_AMPDU_TX_START:
		ret = ath_tx_aggr_start(sc, addr, tid, ssn);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to start TX aggregation\n",
				__func__);
		else
			ieee80211_start_tx_ba_cb_irqsafe(hw, (u8 *)addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ret = ath_tx_aggr_stop(sc, addr, tid);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to stop TX aggregation\n",
				__func__);

		ieee80211_stop_tx_ba_cb_irqsafe(hw, (u8 *)addr, tid);
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unknown AMPDU action\n", __func__);
	}

	return ret;
}

static struct ieee80211_ops ath9k_ops = {
	.tx 		    = ath9k_tx,
	.start 		    = ath9k_start,
	.stop 		    = ath9k_stop,
	.add_interface 	    = ath9k_add_interface,
	.remove_interface   = ath9k_remove_interface,
	.config 	    = ath9k_config,
	.config_interface   = ath9k_config_interface,
	.configure_filter   = ath9k_configure_filter,
	.get_stats          = NULL,
	.sta_notify         = ath9k_sta_notify,
	.conf_tx 	    = ath9k_conf_tx,
	.get_tx_stats 	    = NULL,
	.bss_info_changed   = ath9k_bss_info_changed,
	.set_tim            = NULL,
	.set_key            = ath9k_set_key,
	.hw_scan            = NULL,
	.get_tkip_seq       = NULL,
	.set_rts_threshold  = NULL,
	.set_frag_threshold = NULL,
	.set_retry_limit    = NULL,
	.get_tsf 	    = ath9k_get_tsf,
	.reset_tsf 	    = ath9k_reset_tsf,
	.tx_last_beacon     = NULL,
	.ampdu_action       = ath9k_ampdu_action
};

void ath_get_beaconconfig(struct ath_softc *sc,
			  int if_id,
			  struct ath_beacon_config *conf)
{
	struct ieee80211_hw *hw = sc->hw;

	/* fill in beacon config data */

	conf->beacon_interval = hw->conf.beacon_int;
	conf->listen_interval = 100;
	conf->dtim_count = 1;
	conf->bmiss_timeout = ATH_DEFAULT_BMISS_LIMIT * conf->listen_interval;
}

void ath_tx_complete(struct ath_softc *sc, struct sk_buff *skb,
		     struct ath_xmit_status *tx_status, struct ath_node *an)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	DPRINTF(sc, ATH_DBG_XMIT,
		"%s: TX complete: skb: %p\n", __func__, skb);

	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK ||
		tx_info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
		/* free driver's private data area of tx_info */
		if (tx_info->driver_data[0] != NULL)
			kfree(tx_info->driver_data[0]);
			tx_info->driver_data[0] = NULL;
	}

	if (tx_status->flags & ATH_TX_BAR) {
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
		tx_status->flags &= ~ATH_TX_BAR;
	}

	if (tx_status->flags & (ATH_TX_ERROR | ATH_TX_XRETRY)) {
		if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK)) {
			/* Frame was not ACKed, but an ACK was expected */
			tx_info->status.excessive_retries = 1;
		}
	} else {
		/* Frame was ACKed */
		tx_info->flags |= IEEE80211_TX_STAT_ACK;
	}

	tx_info->status.retry_count = tx_status->retries;

	ieee80211_tx_status(hw, skb);
	if (an)
		ath_node_put(sc, an, ATH9K_BH_STATUS_CHANGE);
}

int ath__rx_indicate(struct ath_softc *sc,
		     struct sk_buff *skb,
		     struct ath_recv_status *status,
		     u16 keyix)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ath_node *an = NULL;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	int padsize;
	enum ATH_RX_TYPE st;

	/* see if any padding is done by the hw and remove it */
	if (hdrlen & 3) {
		padsize = hdrlen % 4;
		memmove(skb->data + padsize, skb->data, hdrlen);
		skb_pull(skb, padsize);
	}

	/* remove FCS before passing up to protocol stack */
	skb_trim(skb, (skb->len - FCS_LEN));

	/* Prepare rx status */
	ath9k_rx_prepare(sc, skb, status, &rx_status);

	if (!(keyix == ATH9K_RXKEYIX_INVALID) &&
	    !(status->flags & ATH_RX_DECRYPT_ERROR)) {
		rx_status.flag |= RX_FLAG_DECRYPTED;
	} else if ((le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_PROTECTED)
		   && !(status->flags & ATH_RX_DECRYPT_ERROR)
		   && skb->len >= hdrlen + 4) {
		keyix = skb->data[hdrlen + 3] >> 6;

		if (test_bit(keyix, sc->sc_keymap))
			rx_status.flag |= RX_FLAG_DECRYPTED;
	}

	spin_lock_bh(&sc->node_lock);
	an = ath_node_find(sc, hdr->addr2);
	spin_unlock_bh(&sc->node_lock);

	if (an) {
		ath_rx_input(sc, an,
			     hw->conf.ht_conf.ht_supported,
			     skb, status, &st);
	}
	if (!an || (st != ATH_RX_CONSUMED))
		__ieee80211_rx(hw, skb, &rx_status);

	return 0;
}

int ath_rx_subframe(struct ath_node *an,
		    struct sk_buff *skb,
		    struct ath_recv_status *status)
{
	struct ath_softc *sc = an->an_sc;
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_rx_status rx_status;

	/* Prepare rx status */
	ath9k_rx_prepare(sc, skb, status, &rx_status);
	if (!(status->flags & ATH_RX_DECRYPT_ERROR))
		rx_status.flag |= RX_FLAG_DECRYPTED;

	__ieee80211_rx(hw, skb, &rx_status);

	return 0;
}

enum ath9k_ht_macmode ath_cwm_macmode(struct ath_softc *sc)
{
	return sc->sc_ht_info.tx_chan_width;
}

static int ath_detach(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Detach ATH hw\n", __func__);

	/* Unregister hw */

	ieee80211_unregister_hw(hw);

	/* unregister Rate control */
	ath_rate_control_unregister();

	/* tx/rx cleanup */

	ath_rx_cleanup(sc);
	ath_tx_cleanup(sc);

	/* Deinit */

	ath_deinit(sc);

	return 0;
}

static int ath_attach(u16 devid,
		      struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	int error = 0;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Attach ATH hw\n", __func__);

	error = ath_init(devid, sc);
	if (error != 0)
		return error;

	/* Init nodes */

	INIT_LIST_HEAD(&sc->node_list);
	spin_lock_init(&sc->node_lock);

	/* get mac address from hardware and set in mac80211 */

	SET_IEEE80211_PERM_ADDR(hw, sc->sc_myaddr);

	/* setup channels and rates */

	sc->sbands[IEEE80211_BAND_2GHZ].channels =
		sc->channels[IEEE80211_BAND_2GHZ];
	sc->sbands[IEEE80211_BAND_2GHZ].bitrates =
		sc->rates[IEEE80211_BAND_2GHZ];
	sc->sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;

	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT)
		/* Setup HT capabilities for 2.4Ghz*/
		setup_ht_cap(&sc->sbands[IEEE80211_BAND_2GHZ].ht_info);

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
		&sc->sbands[IEEE80211_BAND_2GHZ];

	if (test_bit(ATH9K_MODE_11A, sc->sc_ah->ah_caps.wireless_modes)) {
		sc->sbands[IEEE80211_BAND_5GHZ].channels =
			sc->channels[IEEE80211_BAND_5GHZ];
		sc->sbands[IEEE80211_BAND_5GHZ].bitrates =
			sc->rates[IEEE80211_BAND_5GHZ];
		sc->sbands[IEEE80211_BAND_5GHZ].band =
			IEEE80211_BAND_5GHZ;

		if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT)
			/* Setup HT capabilities for 5Ghz*/
			setup_ht_cap(&sc->sbands[IEEE80211_BAND_5GHZ].ht_info);

		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&sc->sbands[IEEE80211_BAND_5GHZ];
	}

	/* FIXME: Have to figure out proper hw init values later */

	hw->queues = 4;
	hw->ampdu_queues = 1;

	/* Register rate control */
	hw->rate_control_algorithm = "ath9k_rate_control";
	error = ath_rate_control_register();
	if (error != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to register rate control "
			"algorithm:%d\n", __func__, error);
		ath_rate_control_unregister();
		goto bad;
	}

	error = ieee80211_register_hw(hw);
	if (error != 0) {
		ath_rate_control_unregister();
		goto bad;
	}

	/* initialize tx/rx engine */

	error = ath_tx_init(sc, ATH_TXBUF);
	if (error != 0)
		goto bad1;

	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto bad1;

	return 0;
bad1:
	ath_detach(sc);
bad:
	return error;
}

static int ath_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	const char *athname;
	u8 csz;
	u32 val;
	int ret = 0;

	if (pci_enable_device(pdev))
		return -EIO;

	/* XXX 32-bit addressing only */
	if (pci_set_dma_mask(pdev, 0xffffffff)) {
		printk(KERN_ERR "ath_pci: 32-bit DMA not available\n");
		ret = -ENODEV;
		goto bad;
	}

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &csz);
	if (csz == 0) {
		/*
		 * Linux 2.4.18 (at least) writes the cache line size
		 * register as a 16-bit wide register which is wrong.
		 * We must have this setup properly for rx buffer
		 * DMA to work so force a reasonable value here if it
		 * comes up zero.
		 */
		csz = L1_CACHE_BYTES / sizeof(u32);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems. It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	ret = pci_request_region(pdev, 0, "ath9k");
	if (ret) {
		dev_err(&pdev->dev, "PCI memory region reserve error\n");
		ret = -ENODEV;
		goto bad;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		printk(KERN_ERR "PCI memory map error\n") ;
		ret = -EIO;
		goto bad1;
	}

	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (hw == NULL) {
		printk(KERN_ERR "ath_pci: no memory for ieee80211_hw\n");
		goto bad2;
	}

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_NOISE_DBM;

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->pdev = pdev;
	sc->mem = mem;

	if (ath_attach(id->device, sc) != 0) {
		ret = -ENODEV;
		goto bad3;
	}

	/* setup interrupt service routine */

	if (request_irq(pdev->irq, ath_isr, IRQF_SHARED, "ath", sc)) {
		printk(KERN_ERR "%s: request_irq failed\n",
			wiphy_name(hw->wiphy));
		ret = -EIO;
		goto bad4;
	}

	athname = ath9k_hw_probe(id->vendor, id->device);

	printk(KERN_INFO "%s: %s: mem=0x%lx, irq=%d\n",
	       wiphy_name(hw->wiphy),
	       athname ? athname : "Atheros ???",
	       (unsigned long)mem, pdev->irq);

	return 0;
bad4:
	ath_detach(sc);
bad3:
	ieee80211_free_hw(hw);
bad2:
	pci_iounmap(pdev, mem);
bad1:
	pci_release_region(pdev, 0);
bad:
	pci_disable_device(pdev);
	return ret;
}

static void ath_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;

	if (pdev->irq)
		free_irq(pdev->irq, sc);
	ath_detach(sc);
	pci_iounmap(pdev, sc->mem);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	ieee80211_free_hw(hw);
}

#ifdef CONFIG_PM

static int ath_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, 3);

	return 0;
}

static int ath_pci_resume(struct pci_dev *pdev)
{
	u32 val;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;
	pci_restore_state(pdev);
	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	return 0;
}

#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(pci, ath_pci_id_table);

static struct pci_driver ath_pci_driver = {
	.name       = "ath9k",
	.id_table   = ath_pci_id_table,
	.probe      = ath_pci_probe,
	.remove     = ath_pci_remove,
#ifdef CONFIG_PM
	.suspend    = ath_pci_suspend,
	.resume     = ath_pci_resume,
#endif /* CONFIG_PM */
};

static int __init init_ath_pci(void)
{
	printk(KERN_INFO "%s: %s\n", dev_info, ATH_PCI_VERSION);

	if (pci_register_driver(&ath_pci_driver) < 0) {
		printk(KERN_ERR
			"ath_pci: No devices found, driver not installed.\n");
		pci_unregister_driver(&ath_pci_driver);
		return -ENODEV;
	}

	return 0;
}
module_init(init_ath_pci);

static void __exit exit_ath_pci(void)
{
	pci_unregister_driver(&ath_pci_driver);
	printk(KERN_INFO "%s: driver unloaded\n", dev_info);
}
module_exit(exit_ath_pci);
