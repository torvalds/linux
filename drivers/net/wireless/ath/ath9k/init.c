/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/ath9k_platform.h>
#include <linux/module.h>

#include "ath9k.h"

static char *dev_info = "ath9k";

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Support for Atheros 802.11n wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned int ath9k_debug = ATH_DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);
MODULE_PARM_DESC(debug, "Debugging mask");

int ath9k_modparam_nohwcrypt;
module_param_named(nohwcrypt, ath9k_modparam_nohwcrypt, int, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption");

int led_blink;
module_param_named(blink, led_blink, int, 0444);
MODULE_PARM_DESC(blink, "Enable LED blink on activity");

static int ath9k_btcoex_enable;
module_param_named(btcoex_enable, ath9k_btcoex_enable, int, 0444);
MODULE_PARM_DESC(btcoex_enable, "Enable wifi-BT coexistence");

bool is_ath9k_unloaded;
/* We use the hw_value as an index into our private channel structure */

#define CHAN2G(_freq, _idx)  { \
	.band = IEEE80211_BAND_2GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

#define CHAN5G(_freq, _idx) { \
	.band = IEEE80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

/* Some 2 GHz radios are actually tunable on 2312-2732
 * on 5 MHz steps, we support the channels which we know
 * we have calibration data for all cards though to make
 * this static */
static const struct ieee80211_channel ath9k_2ghz_chantable[] = {
	CHAN2G(2412, 0), /* Channel 1 */
	CHAN2G(2417, 1), /* Channel 2 */
	CHAN2G(2422, 2), /* Channel 3 */
	CHAN2G(2427, 3), /* Channel 4 */
	CHAN2G(2432, 4), /* Channel 5 */
	CHAN2G(2437, 5), /* Channel 6 */
	CHAN2G(2442, 6), /* Channel 7 */
	CHAN2G(2447, 7), /* Channel 8 */
	CHAN2G(2452, 8), /* Channel 9 */
	CHAN2G(2457, 9), /* Channel 10 */
	CHAN2G(2462, 10), /* Channel 11 */
	CHAN2G(2467, 11), /* Channel 12 */
	CHAN2G(2472, 12), /* Channel 13 */
	CHAN2G(2484, 13), /* Channel 14 */
};

/* Some 5 GHz radios are actually tunable on XXXX-YYYY
 * on 5 MHz steps, we support the channels which we know
 * we have calibration data for all cards though to make
 * this static */
static const struct ieee80211_channel ath9k_5ghz_chantable[] = {
	/* _We_ call this UNII 1 */
	CHAN5G(5180, 14), /* Channel 36 */
	CHAN5G(5200, 15), /* Channel 40 */
	CHAN5G(5220, 16), /* Channel 44 */
	CHAN5G(5240, 17), /* Channel 48 */
	/* _We_ call this UNII 2 */
	CHAN5G(5260, 18), /* Channel 52 */
	CHAN5G(5280, 19), /* Channel 56 */
	CHAN5G(5300, 20), /* Channel 60 */
	CHAN5G(5320, 21), /* Channel 64 */
	/* _We_ call this "Middle band" */
	CHAN5G(5500, 22), /* Channel 100 */
	CHAN5G(5520, 23), /* Channel 104 */
	CHAN5G(5540, 24), /* Channel 108 */
	CHAN5G(5560, 25), /* Channel 112 */
	CHAN5G(5580, 26), /* Channel 116 */
	CHAN5G(5600, 27), /* Channel 120 */
	CHAN5G(5620, 28), /* Channel 124 */
	CHAN5G(5640, 29), /* Channel 128 */
	CHAN5G(5660, 30), /* Channel 132 */
	CHAN5G(5680, 31), /* Channel 136 */
	CHAN5G(5700, 32), /* Channel 140 */
	/* _We_ call this UNII 3 */
	CHAN5G(5745, 33), /* Channel 149 */
	CHAN5G(5765, 34), /* Channel 153 */
	CHAN5G(5785, 35), /* Channel 157 */
	CHAN5G(5805, 36), /* Channel 161 */
	CHAN5G(5825, 37), /* Channel 165 */
};

/* Atheros hardware rate code addition for short premble */
#define SHPCHECK(__hw_rate, __flags) \
	((__flags & IEEE80211_RATE_SHORT_PREAMBLE) ? (__hw_rate | 0x04 ) : 0)

#define RATE(_bitrate, _hw_rate, _flags) {              \
	.bitrate        = (_bitrate),                   \
	.flags          = (_flags),                     \
	.hw_value       = (_hw_rate),                   \
	.hw_value_short = (SHPCHECK(_hw_rate, _flags))  \
}

static struct ieee80211_rate ath9k_legacy_rates[] = {
	RATE(10, 0x1b, 0),
	RATE(20, 0x1a, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, 0x19, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, 0x18, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0x0b, 0),
	RATE(90, 0x0f, 0),
	RATE(120, 0x0a, 0),
	RATE(180, 0x0e, 0),
	RATE(240, 0x09, 0),
	RATE(360, 0x0d, 0),
	RATE(480, 0x08, 0),
	RATE(540, 0x0c, 0),
};

#ifdef CONFIG_MAC80211_LEDS
static const struct ieee80211_tpt_blink ath9k_tpt_blink[] = {
	{ .throughput = 0 * 1024, .blink_time = 334 },
	{ .throughput = 1 * 1024, .blink_time = 260 },
	{ .throughput = 5 * 1024, .blink_time = 220 },
	{ .throughput = 10 * 1024, .blink_time = 190 },
	{ .throughput = 20 * 1024, .blink_time = 170 },
	{ .throughput = 50 * 1024, .blink_time = 150 },
	{ .throughput = 70 * 1024, .blink_time = 130 },
	{ .throughput = 100 * 1024, .blink_time = 110 },
	{ .throughput = 200 * 1024, .blink_time = 80 },
	{ .throughput = 300 * 1024, .blink_time = 50 },
};
#endif

static void ath9k_deinit_softc(struct ath_softc *sc);

/*
 * Read and write, they both share the same lock. We do this to serialize
 * reads and writes on Atheros 802.11n PCI devices only. This is required
 * as the FIFO on these devices can only accept sanely 2 requests.
 */

static void ath9k_iowrite32(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	if (NR_CPUS > 1 && ah->config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&sc->sc_serial_rw, flags);
		iowrite32(val, sc->mem + reg_offset);
		spin_unlock_irqrestore(&sc->sc_serial_rw, flags);
	} else
		iowrite32(val, sc->mem + reg_offset);
}

static unsigned int ath9k_ioread32(void *hw_priv, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	u32 val;

	if (NR_CPUS > 1 && ah->config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&sc->sc_serial_rw, flags);
		val = ioread32(sc->mem + reg_offset);
		spin_unlock_irqrestore(&sc->sc_serial_rw, flags);
	} else
		val = ioread32(sc->mem + reg_offset);
	return val;
}

static unsigned int __ath9k_reg_rmw(struct ath_softc *sc, u32 reg_offset,
				    u32 set, u32 clr)
{
	u32 val;

	val = ioread32(sc->mem + reg_offset);
	val &= ~clr;
	val |= set;
	iowrite32(val, sc->mem + reg_offset);

	return val;
}

static unsigned int ath9k_reg_rmw(void *hw_priv, u32 reg_offset, u32 set, u32 clr)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	unsigned long uninitialized_var(flags);
	u32 val;

	if (NR_CPUS > 1 && ah->config.serialize_regmode == SER_REG_MODE_ON) {
		spin_lock_irqsave(&sc->sc_serial_rw, flags);
		val = __ath9k_reg_rmw(sc, reg_offset, set, clr);
		spin_unlock_irqrestore(&sc->sc_serial_rw, flags);
	} else
		val = __ath9k_reg_rmw(sc, reg_offset, set, clr);

	return val;
}

/**************************/
/*     Initialization     */
/**************************/

static void setup_ht_cap(struct ath_softc *sc,
			 struct ieee80211_sta_ht_cap *ht_info)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u8 tx_streams, rx_streams;
	int i, max_streams;

	ht_info->ht_supported = true;
	ht_info->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SM_PS |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_LDPC)
		ht_info->cap |= IEEE80211_HT_CAP_LDPC_CODING;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_SGI_20)
		ht_info->cap |= IEEE80211_HT_CAP_SGI_20;

	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;

	if (AR_SREV_9330(ah) || AR_SREV_9485(ah))
		max_streams = 1;
	else if (AR_SREV_9462(ah))
		max_streams = 2;
	else if (AR_SREV_9300_20_OR_LATER(ah))
		max_streams = 3;
	else
		max_streams = 2;

	if (AR_SREV_9280_20_OR_LATER(ah)) {
		if (max_streams >= 2)
			ht_info->cap |= IEEE80211_HT_CAP_TX_STBC;
		ht_info->cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	}

	/* set up supported mcs set */
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));
	tx_streams = ath9k_cmn_count_streams(ah->txchainmask, max_streams);
	rx_streams = ath9k_cmn_count_streams(ah->rxchainmask, max_streams);

	ath_dbg(common, CONFIG, "TX streams %d, RX streams: %d\n",
		tx_streams, rx_streams);

	if (tx_streams != rx_streams) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_streams - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}

	for (i = 0; i < rx_streams; i++)
		ht_info->mcs.rx_mask[i] = 0xff;

	ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;
}

static int ath9k_reg_notifier(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_regulatory *reg = ath9k_hw_regulatory(ah);
	int ret;

	ret = ath_reg_notifier_apply(wiphy, request, reg);

	/* Set tx power */
	if (ah->curchan) {
		sc->config.txpowlimit = 2 * ah->curchan->chan->max_power;
		ath9k_ps_wakeup(sc);
		ath9k_hw_set_txpowerlimit(ah, sc->config.txpowlimit, false);
		sc->curtxpow = ath9k_hw_regulatory(ah)->power_limit;
		ath9k_ps_restore(sc);
	}

	return ret;
}

/*
 *  This function will allocate both the DMA descriptor structure, and the
 *  buffers it contains.  These are used to contain the descriptors used
 *  by the system.
*/
int ath_descdma_setup(struct ath_softc *sc, struct ath_descdma *dd,
		      struct list_head *head, const char *name,
		      int nbuf, int ndesc, bool is_tx)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u8 *ds;
	struct ath_buf *bf;
	int i, bsize, error, desc_len;

	ath_dbg(common, CONFIG, "%s DMA: %u buffers %u desc/buf\n",
		name, nbuf, ndesc);

	INIT_LIST_HEAD(head);

	if (is_tx)
		desc_len = sc->sc_ah->caps.tx_desc_len;
	else
		desc_len = sizeof(struct ath_desc);

	/* ath_desc must be a multiple of DWORDs */
	if ((desc_len % 4) != 0) {
		ath_err(common, "ath_desc not DWORD aligned\n");
		BUG_ON((desc_len % 4) != 0);
		error = -ENOMEM;
		goto fail;
	}

	dd->dd_desc_len = desc_len * nbuf * ndesc;

	/*
	 * Need additional DMA memory because we can't use
	 * descriptors that cross the 4K page boundary. Assume
	 * one skipped descriptor per 4K page.
	 */
	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_4KB_SPLITTRANS)) {
		u32 ndesc_skipped =
			ATH_DESC_4KB_BOUND_NUM_SKIPPED(dd->dd_desc_len);
		u32 dma_len;

		while (ndesc_skipped) {
			dma_len = ndesc_skipped * desc_len;
			dd->dd_desc_len += dma_len;

			ndesc_skipped = ATH_DESC_4KB_BOUND_NUM_SKIPPED(dma_len);
		}
	}

	/* allocate descriptors */
	dd->dd_desc = dma_alloc_coherent(sc->dev, dd->dd_desc_len,
					 &dd->dd_desc_paddr, GFP_KERNEL);
	if (dd->dd_desc == NULL) {
		error = -ENOMEM;
		goto fail;
	}
	ds = (u8 *) dd->dd_desc;
	ath_dbg(common, CONFIG, "%s DMA map: %p (%u) -> %llx (%u)\n",
		name, ds, (u32) dd->dd_desc_len,
		ito64(dd->dd_desc_paddr), /*XXX*/(u32) dd->dd_desc_len);

	/* allocate buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = kzalloc(bsize, GFP_KERNEL);
	if (bf == NULL) {
		error = -ENOMEM;
		goto fail2;
	}
	dd->dd_bufptr = bf;

	for (i = 0; i < nbuf; i++, bf++, ds += (desc_len * ndesc)) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(dd, ds);

		if (!(sc->sc_ah->caps.hw_caps &
		      ATH9K_HW_CAP_4KB_SPLITTRANS)) {
			/*
			 * Skip descriptor addresses which can cause 4KB
			 * boundary crossing (addr + length) with a 32 dword
			 * descriptor fetch.
			 */
			while (ATH_DESC_4KB_BOUND_CHECK(bf->bf_daddr)) {
				BUG_ON((caddr_t) bf->bf_desc >=
				       ((caddr_t) dd->dd_desc +
					dd->dd_desc_len));

				ds += (desc_len * ndesc);
				bf->bf_desc = ds;
				bf->bf_daddr = DS2PHYS(dd, ds);
			}
		}
		list_add_tail(&bf->list, head);
	}
	return 0;
fail2:
	dma_free_coherent(sc->dev, dd->dd_desc_len, dd->dd_desc,
			  dd->dd_desc_paddr);
fail:
	memset(dd, 0, sizeof(*dd));
	return error;
}

static int ath9k_init_queues(struct ath_softc *sc)
{
	int i = 0;

	sc->beacon.beaconq = ath9k_hw_beaconq_setup(sc->sc_ah);
	sc->beacon.cabq = ath_txq_setup(sc, ATH9K_TX_QUEUE_CAB, 0);

	sc->config.cabqReadytime = ATH_CABQ_READY_TIME;
	ath_cabq_update(sc);

	for (i = 0; i < WME_NUM_AC; i++) {
		sc->tx.txq_map[i] = ath_txq_setup(sc, ATH9K_TX_QUEUE_DATA, i);
		sc->tx.txq_map[i]->mac80211_qnum = i;
		sc->tx.txq_max_pending[i] = ATH_MAX_QDEPTH;
	}
	return 0;
}

static int ath9k_init_channels_rates(struct ath_softc *sc)
{
	void *channels;

	BUILD_BUG_ON(ARRAY_SIZE(ath9k_2ghz_chantable) +
		     ARRAY_SIZE(ath9k_5ghz_chantable) !=
		     ATH9K_NUM_CHANNELS);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ) {
		channels = kmemdup(ath9k_2ghz_chantable,
			sizeof(ath9k_2ghz_chantable), GFP_KERNEL);
		if (!channels)
		    return -ENOMEM;

		sc->sbands[IEEE80211_BAND_2GHZ].channels = channels;
		sc->sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;
		sc->sbands[IEEE80211_BAND_2GHZ].n_channels =
			ARRAY_SIZE(ath9k_2ghz_chantable);
		sc->sbands[IEEE80211_BAND_2GHZ].bitrates = ath9k_legacy_rates;
		sc->sbands[IEEE80211_BAND_2GHZ].n_bitrates =
			ARRAY_SIZE(ath9k_legacy_rates);
	}

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ) {
		channels = kmemdup(ath9k_5ghz_chantable,
			sizeof(ath9k_5ghz_chantable), GFP_KERNEL);
		if (!channels) {
			if (sc->sbands[IEEE80211_BAND_2GHZ].channels)
				kfree(sc->sbands[IEEE80211_BAND_2GHZ].channels);
			return -ENOMEM;
		}

		sc->sbands[IEEE80211_BAND_5GHZ].channels = channels;
		sc->sbands[IEEE80211_BAND_5GHZ].band = IEEE80211_BAND_5GHZ;
		sc->sbands[IEEE80211_BAND_5GHZ].n_channels =
			ARRAY_SIZE(ath9k_5ghz_chantable);
		sc->sbands[IEEE80211_BAND_5GHZ].bitrates =
			ath9k_legacy_rates + 4;
		sc->sbands[IEEE80211_BAND_5GHZ].n_bitrates =
			ARRAY_SIZE(ath9k_legacy_rates) - 4;
	}
	return 0;
}

static void ath9k_init_misc(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	int i = 0;

	setup_timer(&common->ani.timer, ath_ani_calibrate, (unsigned long)sc);

	sc->last_rssi = ATH_RSSI_DUMMY_MARKER;
	sc->config.txpowlimit = ATH_TXPOWER_MAX;
	memcpy(common->bssidmask, ath_bcast_mac, ETH_ALEN);
	sc->beacon.slottime = ATH9K_SLOT_TIME_9;

	for (i = 0; i < ARRAY_SIZE(sc->beacon.bslot); i++)
		sc->beacon.bslot[i] = NULL;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB)
		sc->ant_comb.count = ATH_ANT_DIV_COMB_INIT_COUNT;
}

static int ath9k_init_softc(u16 devid, struct ath_softc *sc,
			    const struct ath_bus_ops *bus_ops)
{
	struct ath9k_platform_data *pdata = sc->dev->platform_data;
	struct ath_hw *ah = NULL;
	struct ath_common *common;
	int ret = 0, i;
	int csz = 0;

	ah = kzalloc(sizeof(struct ath_hw), GFP_KERNEL);
	if (!ah)
		return -ENOMEM;

	ah->hw = sc->hw;
	ah->hw_version.devid = devid;
	ah->reg_ops.read = ath9k_ioread32;
	ah->reg_ops.write = ath9k_iowrite32;
	ah->reg_ops.rmw = ath9k_reg_rmw;
	atomic_set(&ah->intr_ref_cnt, -1);
	sc->sc_ah = ah;

	sc->dfs_detector = dfs_pattern_detector_init(NL80211_DFS_UNSET);

	if (!pdata) {
		ah->ah_flags |= AH_USE_EEPROM;
		sc->sc_ah->led_pin = -1;
	} else {
		sc->sc_ah->gpio_mask = pdata->gpio_mask;
		sc->sc_ah->gpio_val = pdata->gpio_val;
		sc->sc_ah->led_pin = pdata->led_pin;
		ah->is_clk_25mhz = pdata->is_clk_25mhz;
		ah->get_mac_revision = pdata->get_mac_revision;
		ah->external_reset = pdata->external_reset;
	}

	common = ath9k_hw_common(ah);
	common->ops = &ah->reg_ops;
	common->bus_ops = bus_ops;
	common->ah = ah;
	common->hw = sc->hw;
	common->priv = sc;
	common->debug_mask = ath9k_debug;
	common->btcoex_enabled = ath9k_btcoex_enable == 1;
	common->disable_ani = false;
	spin_lock_init(&common->cc_lock);

	spin_lock_init(&sc->sc_serial_rw);
	spin_lock_init(&sc->sc_pm_lock);
	mutex_init(&sc->mutex);
#ifdef CONFIG_ATH9K_DEBUGFS
	spin_lock_init(&sc->nodes_lock);
	INIT_LIST_HEAD(&sc->nodes);
#endif
#ifdef CONFIG_ATH9K_MAC_DEBUG
	spin_lock_init(&sc->debug.samp_lock);
#endif
	tasklet_init(&sc->intr_tq, ath9k_tasklet, (unsigned long)sc);
	tasklet_init(&sc->bcon_tasklet, ath9k_beacon_tasklet,
		     (unsigned long)sc);

	INIT_WORK(&sc->hw_reset_work, ath_reset_work);
	INIT_WORK(&sc->hw_check_work, ath_hw_check);
	INIT_WORK(&sc->paprd_work, ath_paprd_calibrate);
	INIT_DELAYED_WORK(&sc->hw_pll_work, ath_hw_pll_work);
	setup_timer(&sc->rx_poll_timer, ath_rx_poll, (unsigned long)sc);

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	ath_read_cachesize(common, &csz);
	common->cachelsz = csz << 2; /* convert to bytes */

	/* Initializes the hardware for all supported chipsets */
	ret = ath9k_hw_init(ah);
	if (ret)
		goto err_hw;

	if (pdata && pdata->macaddr)
		memcpy(common->macaddr, pdata->macaddr, ETH_ALEN);

	ret = ath9k_init_queues(sc);
	if (ret)
		goto err_queues;

	ret =  ath9k_init_btcoex(sc);
	if (ret)
		goto err_btcoex;

	ret = ath9k_init_channels_rates(sc);
	if (ret)
		goto err_btcoex;

	ath9k_cmn_init_crypto(sc->sc_ah);
	ath9k_init_misc(sc);

	if (common->bus_ops->aspm_init)
		common->bus_ops->aspm_init(common);

	return 0;

err_btcoex:
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);
err_queues:
	ath9k_hw_deinit(ah);
err_hw:

	kfree(ah);
	sc->sc_ah = NULL;

	return ret;
}

static void ath9k_init_band_txpower(struct ath_softc *sc, int band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	struct ath_hw *ah = sc->sc_ah;
	int i;

	sband = &sc->sbands[band];
	for (i = 0; i < sband->n_channels; i++) {
		chan = &sband->channels[i];
		ah->curchan = &ah->channels[chan->hw_value];
		ath9k_cmn_update_ichannel(ah->curchan, chan, NL80211_CHAN_HT20);
		ath9k_hw_set_txpowerlimit(ah, MAX_RATE_POWER, true);
	}
}

static void ath9k_init_txpower_limits(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_channel *curchan = ah->curchan;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		ath9k_init_band_txpower(sc, IEEE80211_BAND_2GHZ);
	if (ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		ath9k_init_band_txpower(sc, IEEE80211_BAND_5GHZ);

	ah->curchan = curchan;
}

void ath9k_reload_chainmask_settings(struct ath_softc *sc)
{
	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT))
		return;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		setup_ht_cap(sc, &sc->sbands[IEEE80211_BAND_2GHZ].ht_cap);
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		setup_ht_cap(sc, &sc->sbands[IEEE80211_BAND_5GHZ].ht_cap);
}

static const struct ieee80211_iface_limit if_limits[] = {
	{ .max = 2048,	.types = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_WDS) },
	{ .max = 8,	.types =
#ifdef CONFIG_MAC80211_MESH
				 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_GO) },
};

static const struct ieee80211_iface_combination if_comb = {
	.limits = if_limits,
	.n_limits = ARRAY_SIZE(if_limits),
	.max_interfaces = 2048,
	.num_different_channels = 1,
};

void ath9k_set_hw_capab(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_SUPPORTS_PS |
		IEEE80211_HW_PS_NULLFUNC_STACK |
		IEEE80211_HW_SPECTRUM_MGMT |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT)
		 hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;

	if (AR_SREV_9160_10_OR_LATER(sc->sc_ah) || ath9k_modparam_nohwcrypt)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_WDS) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_MESH_POINT);

	hw->wiphy->iface_combinations = &if_comb;
	hw->wiphy->n_iface_combinations = 1;

	if (AR_SREV_5416(sc->sc_ah))
		hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

#ifdef CONFIG_PM_SLEEP

	if ((ah->caps.hw_caps & ATH9K_HW_WOW_DEVICE_CAPABLE) &&
	    device_can_wakeup(sc->dev)) {

		hw->wiphy->wowlan.flags = WIPHY_WOWLAN_MAGIC_PKT |
					  WIPHY_WOWLAN_DISCONNECT;
		hw->wiphy->wowlan.n_patterns = MAX_NUM_USER_PATTERN;
		hw->wiphy->wowlan.pattern_min_len = 1;
		hw->wiphy->wowlan.pattern_max_len = MAX_PATTERN_SIZE;

	}

	atomic_set(&sc->wow_sleep_proc_intr, -1);
	atomic_set(&sc->wow_got_bmiss_intr, -1);

#endif

	hw->queues = 4;
	hw->max_rates = 4;
	hw->channel_change_time = 5000;
	hw->max_listen_interval = 1;
	hw->max_rate_tries = 10;
	hw->sta_data_size = sizeof(struct ath_node);
	hw->vif_data_size = sizeof(struct ath_vif);

	hw->wiphy->available_antennas_rx = BIT(ah->caps.max_rxchains) - 1;
	hw->wiphy->available_antennas_tx = BIT(ah->caps.max_txchains) - 1;

	/* single chain devices with rx diversity */
	if (ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB)
		hw->wiphy->available_antennas_rx = BIT(0) | BIT(1);

	sc->ant_rx = hw->wiphy->available_antennas_rx;
	sc->ant_tx = hw->wiphy->available_antennas_tx;

#ifdef CONFIG_ATH9K_RATE_CONTROL
	hw->rate_control_algorithm = "ath9k_rate_control";
#endif

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&sc->sbands[IEEE80211_BAND_2GHZ];
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&sc->sbands[IEEE80211_BAND_5GHZ];

	ath9k_reload_chainmask_settings(sc);

	SET_IEEE80211_PERM_ADDR(hw, common->macaddr);
}

int ath9k_init_device(u16 devid, struct ath_softc *sc,
		    const struct ath_bus_ops *bus_ops)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ath_common *common;
	struct ath_hw *ah;
	int error = 0;
	struct ath_regulatory *reg;

	/* Bring up device */
	error = ath9k_init_softc(devid, sc, bus_ops);
	if (error != 0)
		goto error_init;

	ah = sc->sc_ah;
	common = ath9k_hw_common(ah);
	ath9k_set_hw_capab(sc, hw);

	/* Initialize regulatory */
	error = ath_regd_init(&common->regulatory, sc->hw->wiphy,
			      ath9k_reg_notifier);
	if (error)
		goto error_regd;

	reg = &common->regulatory;

	/* Setup TX DMA */
	error = ath_tx_init(sc, ATH_TXBUF);
	if (error != 0)
		goto error_tx;

	/* Setup RX DMA */
	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto error_rx;

	ath9k_init_txpower_limits(sc);

#ifdef CONFIG_MAC80211_LEDS
	/* must be initialized before ieee80211_register_hw */
	sc->led_cdev.default_trigger = ieee80211_create_tpt_led_trigger(sc->hw,
		IEEE80211_TPT_LEDTRIG_FL_RADIO, ath9k_tpt_blink,
		ARRAY_SIZE(ath9k_tpt_blink));
#endif

	/* Register with mac80211 */
	error = ieee80211_register_hw(hw);
	if (error)
		goto error_register;

	error = ath9k_init_debug(ah);
	if (error) {
		ath_err(common, "Unable to create debugfs files\n");
		goto error_world;
	}

	/* Handle world regulatory */
	if (!ath_is_world_regd(reg)) {
		error = regulatory_hint(hw->wiphy, reg->alpha2);
		if (error)
			goto error_world;
	}

	ath_init_leds(sc);
	ath_start_rfkill_poll(sc);

	return 0;

error_world:
	ieee80211_unregister_hw(hw);
error_register:
	ath_rx_cleanup(sc);
error_rx:
	ath_tx_cleanup(sc);
error_tx:
	/* Nothing */
error_regd:
	ath9k_deinit_softc(sc);
error_init:
	return error;
}

/*****************************/
/*     De-Initialization     */
/*****************************/

static void ath9k_deinit_softc(struct ath_softc *sc)
{
	int i = 0;

	if (sc->sbands[IEEE80211_BAND_2GHZ].channels)
		kfree(sc->sbands[IEEE80211_BAND_2GHZ].channels);

	if (sc->sbands[IEEE80211_BAND_5GHZ].channels)
		kfree(sc->sbands[IEEE80211_BAND_5GHZ].channels);

	ath9k_deinit_btcoex(sc);

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);

	ath9k_hw_deinit(sc->sc_ah);
	if (sc->dfs_detector != NULL)
		sc->dfs_detector->exit(sc->dfs_detector);

	kfree(sc->sc_ah);
	sc->sc_ah = NULL;
}

void ath9k_deinit_device(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;

	ath9k_ps_wakeup(sc);

	wiphy_rfkill_stop_polling(sc->hw->wiphy);
	ath_deinit_leds(sc);

	ath9k_ps_restore(sc);

	ieee80211_unregister_hw(hw);
	ath_rx_cleanup(sc);
	ath_tx_cleanup(sc);
	ath9k_deinit_softc(sc);
}

void ath_descdma_cleanup(struct ath_softc *sc,
			 struct ath_descdma *dd,
			 struct list_head *head)
{
	dma_free_coherent(sc->dev, dd->dd_desc_len, dd->dd_desc,
			  dd->dd_desc_paddr);

	INIT_LIST_HEAD(head);
	kfree(dd->dd_bufptr);
	memset(dd, 0, sizeof(*dd));
}

/************************/
/*     Module Hooks     */
/************************/

static int __init ath9k_init(void)
{
	int error;

	/* Register rate control algorithm */
	error = ath_rate_control_register();
	if (error != 0) {
		pr_err("Unable to register rate control algorithm: %d\n",
		       error);
		goto err_out;
	}

	error = ath_pci_init();
	if (error < 0) {
		pr_err("No PCI devices found, driver not installed\n");
		error = -ENODEV;
		goto err_rate_unregister;
	}

	error = ath_ahb_init();
	if (error < 0) {
		error = -ENODEV;
		goto err_pci_exit;
	}

	return 0;

 err_pci_exit:
	ath_pci_exit();

 err_rate_unregister:
	ath_rate_control_unregister();
 err_out:
	return error;
}
module_init(ath9k_init);

static void __exit ath9k_exit(void)
{
	is_ath9k_unloaded = true;
	ath_ahb_exit();
	ath_pci_exit();
	ath_rate_control_unregister();
	pr_info("%s: Driver unloaded\n", dev_info);
}
module_exit(ath9k_exit);
