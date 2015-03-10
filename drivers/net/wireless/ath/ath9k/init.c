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
#include <linux/relay.h>
#include <net/ieee80211_radiotap.h>

#include "ath9k.h"

struct ath9k_eeprom_ctx {
	struct completion complete;
	struct ath_hw *ah;
};

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

int ath9k_led_blink;
module_param_named(blink, ath9k_led_blink, int, 0444);
MODULE_PARM_DESC(blink, "Enable LED blink on activity");

static int ath9k_btcoex_enable;
module_param_named(btcoex_enable, ath9k_btcoex_enable, int, 0444);
MODULE_PARM_DESC(btcoex_enable, "Enable wifi-BT coexistence");

static int ath9k_bt_ant_diversity;
module_param_named(bt_ant_diversity, ath9k_bt_ant_diversity, int, 0444);
MODULE_PARM_DESC(bt_ant_diversity, "Enable WLAN/BT RX antenna diversity");

static int ath9k_ps_enable;
module_param_named(ps_enable, ath9k_ps_enable, int, 0444);
MODULE_PARM_DESC(ps_enable, "Enable WLAN PowerSave");

#ifdef CONFIG_ATH9K_CHANNEL_CONTEXT

int ath9k_use_chanctx;
module_param_named(use_chanctx, ath9k_use_chanctx, int, 0444);
MODULE_PARM_DESC(use_chanctx, "Enable channel context for concurrency");

#endif /* CONFIG_ATH9K_CHANNEL_CONTEXT */

bool is_ath9k_unloaded;

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

static void ath9k_op_ps_wakeup(struct ath_common *common)
{
	ath9k_ps_wakeup((struct ath_softc *) common->priv);
}

static void ath9k_op_ps_restore(struct ath_common *common)
{
	ath9k_ps_restore((struct ath_softc *) common->priv);
}

static struct ath_ps_ops ath9k_ps_ops = {
	.wakeup = ath9k_op_ps_wakeup,
	.restore = ath9k_op_ps_restore,
};

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

static void ath9k_reg_notifier(struct wiphy *wiphy,
			       struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_regulatory *reg = ath9k_hw_regulatory(ah);

	ath_reg_notifier_apply(wiphy, request, reg);

	/* Set tx power */
	if (!ah->curchan)
		return;

	sc->cur_chan->txpower = 2 * ah->curchan->chan->max_power;
	ath9k_ps_wakeup(sc);
	ath9k_hw_set_txpowerlimit(ah, sc->cur_chan->txpower, false);
	ath9k_cmn_update_txpow(ah, sc->cur_chan->cur_txpower,
			       sc->cur_chan->txpower,
			       &sc->cur_chan->cur_txpower);
	/* synchronize DFS detector if regulatory domain changed */
	if (sc->dfs_detector != NULL)
		sc->dfs_detector->set_dfs_domain(sc->dfs_detector,
						 request->dfs_region);
	ath9k_ps_restore(sc);
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
	int i, bsize, desc_len;

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
		return -ENOMEM;
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
	dd->dd_desc = dmam_alloc_coherent(sc->dev, dd->dd_desc_len,
					  &dd->dd_desc_paddr, GFP_KERNEL);
	if (!dd->dd_desc)
		return -ENOMEM;

	ds = (u8 *) dd->dd_desc;
	ath_dbg(common, CONFIG, "%s DMA map: %p (%u) -> %llx (%u)\n",
		name, ds, (u32) dd->dd_desc_len,
		ito64(dd->dd_desc_paddr), /*XXX*/(u32) dd->dd_desc_len);

	/* allocate buffers */
	if (is_tx) {
		struct ath_buf *bf;

		bsize = sizeof(struct ath_buf) * nbuf;
		bf = devm_kzalloc(sc->dev, bsize, GFP_KERNEL);
		if (!bf)
			return -ENOMEM;

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
	} else {
		struct ath_rxbuf *bf;

		bsize = sizeof(struct ath_rxbuf) * nbuf;
		bf = devm_kzalloc(sc->dev, bsize, GFP_KERNEL);
		if (!bf)
			return -ENOMEM;

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
	}
	return 0;
}

static int ath9k_init_queues(struct ath_softc *sc)
{
	int i = 0;

	sc->beacon.beaconq = ath9k_hw_beaconq_setup(sc->sc_ah);
	sc->beacon.cabq = ath_txq_setup(sc, ATH9K_TX_QUEUE_CAB, 0);
	ath_cabq_update(sc);

	sc->tx.uapsdq = ath_txq_setup(sc, ATH9K_TX_QUEUE_UAPSD, 0);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		sc->tx.txq_map[i] = ath_txq_setup(sc, ATH9K_TX_QUEUE_DATA, i);
		sc->tx.txq_map[i]->mac80211_qnum = i;
		sc->tx.txq_max_pending[i] = ATH_MAX_QDEPTH;
	}
	return 0;
}

static void ath9k_init_misc(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	int i = 0;

	setup_timer(&common->ani.timer, ath_ani_calibrate, (unsigned long)sc);

	common->last_rssi = ATH_RSSI_DUMMY_MARKER;
	memcpy(common->bssidmask, ath_bcast_mac, ETH_ALEN);
	sc->beacon.slottime = ATH9K_SLOT_TIME_9;

	for (i = 0; i < ARRAY_SIZE(sc->beacon.bslot); i++)
		sc->beacon.bslot[i] = NULL;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB)
		sc->ant_comb.count = ATH_ANT_DIV_COMB_INIT_COUNT;

	sc->spec_priv.ah = sc->sc_ah;
	sc->spec_priv.spec_config.enabled = 0;
	sc->spec_priv.spec_config.short_repeat = true;
	sc->spec_priv.spec_config.count = 8;
	sc->spec_priv.spec_config.endless = false;
	sc->spec_priv.spec_config.period = 0xFF;
	sc->spec_priv.spec_config.fft_period = 0xF;
}

static void ath9k_init_pcoem_platform(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!IS_ENABLED(CONFIG_ATH9K_PCOEM))
		return;

	if (common->bus_ops->ath_bus_type != ATH_PCI)
		return;

	if (sc->driver_data & (ATH9K_PCI_CUS198 |
			       ATH9K_PCI_CUS230)) {
		ah->config.xlna_gpio = 9;
		ah->config.xatten_margin_cfg = true;
		ah->config.alt_mingainidx = true;
		ah->config.ant_ctrl_comm2g_switch_enable = 0x000BBB88;
		sc->ant_comb.low_rssi_thresh = 20;
		sc->ant_comb.fast_div_bias = 3;

		ath_info(common, "Set parameters for %s\n",
			 (sc->driver_data & ATH9K_PCI_CUS198) ?
			 "CUS198" : "CUS230");
	}

	if (sc->driver_data & ATH9K_PCI_CUS217)
		ath_info(common, "CUS217 card detected\n");

	if (sc->driver_data & ATH9K_PCI_CUS252)
		ath_info(common, "CUS252 card detected\n");

	if (sc->driver_data & ATH9K_PCI_AR9565_1ANT)
		ath_info(common, "WB335 1-ANT card detected\n");

	if (sc->driver_data & ATH9K_PCI_AR9565_2ANT)
		ath_info(common, "WB335 2-ANT card detected\n");

	if (sc->driver_data & ATH9K_PCI_KILLER)
		ath_info(common, "Killer Wireless card detected\n");

	/*
	 * Some WB335 cards do not support antenna diversity. Since
	 * we use a hardcoded value for AR9565 instead of using the
	 * EEPROM/OTP data, remove the combining feature from
	 * the HW capabilities bitmap.
	 */
	if (sc->driver_data & (ATH9K_PCI_AR9565_1ANT | ATH9K_PCI_AR9565_2ANT)) {
		if (!(sc->driver_data & ATH9K_PCI_BT_ANT_DIV))
			pCap->hw_caps &= ~ATH9K_HW_CAP_ANT_DIV_COMB;
	}

	if (sc->driver_data & ATH9K_PCI_BT_ANT_DIV) {
		pCap->hw_caps |= ATH9K_HW_CAP_BT_ANT_DIV;
		ath_info(common, "Set BT/WLAN RX diversity capability\n");
	}

	if (sc->driver_data & ATH9K_PCI_D3_L1_WAR) {
		ah->config.pcie_waen = 0x0040473b;
		ath_info(common, "Enable WAR for ASPM D3/L1\n");
	}

	/*
	 * The default value of pll_pwrsave is 1.
	 * For certain AR9485 cards, it is set to 0.
	 * For AR9462, AR9565 it's set to 7.
	 */
	ah->config.pll_pwrsave = 1;

	if (sc->driver_data & ATH9K_PCI_NO_PLL_PWRSAVE) {
		ah->config.pll_pwrsave = 0;
		ath_info(common, "Disable PLL PowerSave\n");
	}

	if (sc->driver_data & ATH9K_PCI_LED_ACT_HI)
		ah->config.led_active_high = true;
}

static void ath9k_eeprom_request_cb(const struct firmware *eeprom_blob,
				    void *ctx)
{
	struct ath9k_eeprom_ctx *ec = ctx;

	if (eeprom_blob)
		ec->ah->eeprom_blob = eeprom_blob;

	complete(&ec->complete);
}

static int ath9k_eeprom_request(struct ath_softc *sc, const char *name)
{
	struct ath9k_eeprom_ctx ec;
	struct ath_hw *ah = ah = sc->sc_ah;
	int err;

	/* try to load the EEPROM content asynchronously */
	init_completion(&ec.complete);
	ec.ah = sc->sc_ah;

	err = request_firmware_nowait(THIS_MODULE, 1, name, sc->dev, GFP_KERNEL,
				      &ec, ath9k_eeprom_request_cb);
	if (err < 0) {
		ath_err(ath9k_hw_common(ah),
			"EEPROM request failed\n");
		return err;
	}

	wait_for_completion(&ec.complete);

	if (!ah->eeprom_blob) {
		ath_err(ath9k_hw_common(ah),
			"Unable to load EEPROM file %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static void ath9k_eeprom_release(struct ath_softc *sc)
{
	release_firmware(sc->sc_ah->eeprom_blob);
}

static int ath9k_init_soc_platform(struct ath_softc *sc)
{
	struct ath9k_platform_data *pdata = sc->dev->platform_data;
	struct ath_hw *ah = sc->sc_ah;
	int ret = 0;

	if (!pdata)
		return 0;

	if (pdata->eeprom_name) {
		ret = ath9k_eeprom_request(sc, pdata->eeprom_name);
		if (ret)
			return ret;
	}

	if (pdata->tx_gain_buffalo)
		ah->config.tx_gain_buffalo = true;

	return ret;
}

static int ath9k_init_softc(u16 devid, struct ath_softc *sc,
			    const struct ath_bus_ops *bus_ops)
{
	struct ath9k_platform_data *pdata = sc->dev->platform_data;
	struct ath_hw *ah = NULL;
	struct ath9k_hw_capabilities *pCap;
	struct ath_common *common;
	int ret = 0, i;
	int csz = 0;

	ah = devm_kzalloc(sc->dev, sizeof(struct ath_hw), GFP_KERNEL);
	if (!ah)
		return -ENOMEM;

	ah->dev = sc->dev;
	ah->hw = sc->hw;
	ah->hw_version.devid = devid;
	ah->reg_ops.read = ath9k_ioread32;
	ah->reg_ops.write = ath9k_iowrite32;
	ah->reg_ops.rmw = ath9k_reg_rmw;
	pCap = &ah->caps;

	common = ath9k_hw_common(ah);

	/* Will be cleared in ath9k_start() */
	set_bit(ATH_OP_INVALID, &common->op_flags);

	sc->sc_ah = ah;
	sc->dfs_detector = dfs_pattern_detector_init(common, NL80211_DFS_UNSET);
	sc->tx99_power = MAX_RATE_POWER + 1;
	init_waitqueue_head(&sc->tx_wait);
	sc->cur_chan = &sc->chanctx[0];
	if (!ath9k_is_chanctx_enabled())
		sc->cur_chan->hw_queue_base = 0;

	if (!pdata || pdata->use_eeprom) {
		ah->ah_flags |= AH_USE_EEPROM;
		sc->sc_ah->led_pin = -1;
	} else {
		sc->sc_ah->gpio_mask = pdata->gpio_mask;
		sc->sc_ah->gpio_val = pdata->gpio_val;
		sc->sc_ah->led_pin = pdata->led_pin;
		ah->is_clk_25mhz = pdata->is_clk_25mhz;
		ah->get_mac_revision = pdata->get_mac_revision;
		ah->external_reset = pdata->external_reset;
		ah->disable_2ghz = pdata->disable_2ghz;
		ah->disable_5ghz = pdata->disable_5ghz;
		if (!pdata->endian_check)
			ah->ah_flags |= AH_NO_EEP_SWAP;
	}

	common->ops = &ah->reg_ops;
	common->bus_ops = bus_ops;
	common->ps_ops = &ath9k_ps_ops;
	common->ah = ah;
	common->hw = sc->hw;
	common->priv = sc;
	common->debug_mask = ath9k_debug;
	common->btcoex_enabled = ath9k_btcoex_enable == 1;
	common->disable_ani = false;

	/*
	 * Platform quirks.
	 */
	ath9k_init_pcoem_platform(sc);

	ret = ath9k_init_soc_platform(sc);
	if (ret)
		return ret;

	/*
	 * Enable WLAN/BT RX Antenna diversity only when:
	 *
	 * - BTCOEX is disabled.
	 * - the user manually requests the feature.
	 * - the HW cap is set using the platform data.
	 */
	if (!common->btcoex_enabled && ath9k_bt_ant_diversity &&
	    (pCap->hw_caps & ATH9K_HW_CAP_BT_ANT_DIV))
		common->bt_ant_diversity = 1;

	spin_lock_init(&common->cc_lock);
	spin_lock_init(&sc->sc_serial_rw);
	spin_lock_init(&sc->sc_pm_lock);
	spin_lock_init(&sc->chan_lock);
	mutex_init(&sc->mutex);
	tasklet_init(&sc->intr_tq, ath9k_tasklet, (unsigned long)sc);
	tasklet_init(&sc->bcon_tasklet, ath9k_beacon_tasklet,
		     (unsigned long)sc);

	setup_timer(&sc->sleep_timer, ath_ps_full_sleep, (unsigned long)sc);
	INIT_WORK(&sc->hw_reset_work, ath_reset_work);
	INIT_WORK(&sc->paprd_work, ath_paprd_calibrate);
	INIT_DELAYED_WORK(&sc->hw_pll_work, ath_hw_pll_work);

	ath9k_init_channel_context(sc);

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

	ret = ath9k_cmn_init_channels_rates(common);
	if (ret)
		goto err_btcoex;

	ret = ath9k_init_p2p(sc);
	if (ret)
		goto err_btcoex;

	ath9k_cmn_init_crypto(sc->sc_ah);
	ath9k_init_misc(sc);
	ath_fill_led_pin(sc);
	ath_chanctx_init(sc);
	ath9k_offchannel_init(sc);

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
	ath9k_eeprom_release(sc);
	dev_kfree_skb_any(sc->tx99_skb);
	return ret;
}

static void ath9k_init_band_txpower(struct ath_softc *sc, int band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct cfg80211_chan_def chandef;
	int i;

	sband = &common->sbands[band];
	for (i = 0; i < sband->n_channels; i++) {
		chan = &sband->channels[i];
		ah->curchan = &ah->channels[chan->hw_value];
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		ath9k_cmn_get_channel(sc->hw, ah, &chandef);
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

static const struct ieee80211_iface_limit if_limits[] = {
	{ .max = 2048,	.types = BIT(NL80211_IFTYPE_STATION) },
	{ .max = 8,	.types =
#ifdef CONFIG_MAC80211_MESH
				 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
				 BIT(NL80211_IFTYPE_AP) },
	{ .max = 1,	.types = BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_P2P_GO) },
};

static const struct ieee80211_iface_limit wds_limits[] = {
	{ .max = 2048,	.types = BIT(NL80211_IFTYPE_WDS) },
};

#ifdef CONFIG_ATH9K_CHANNEL_CONTEXT

static const struct ieee80211_iface_limit if_limits_multi[] = {
	{ .max = 2,	.types = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_P2P_GO) },
	{ .max = 1,	.types = BIT(NL80211_IFTYPE_ADHOC) },
};

static const struct ieee80211_iface_combination if_comb_multi[] = {
	{
		.limits = if_limits_multi,
		.n_limits = ARRAY_SIZE(if_limits_multi),
		.max_interfaces = 2,
		.num_different_channels = 2,
		.beacon_int_infra_match = true,
	},
};

#endif /* CONFIG_ATH9K_CHANNEL_CONTEXT */

static const struct ieee80211_iface_limit if_dfs_limits[] = {
	{ .max = 1,	.types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
				 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
				 BIT(NL80211_IFTYPE_ADHOC) },
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 2048,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	},
	{
		.limits = wds_limits,
		.n_limits = ARRAY_SIZE(wds_limits),
		.max_interfaces = 2048,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	},
#ifdef CONFIG_ATH9K_DFS_CERTIFIED
	{
		.limits = if_dfs_limits,
		.n_limits = ARRAY_SIZE(if_dfs_limits),
		.max_interfaces = 1,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
		.radar_detect_widths =	BIT(NL80211_CHAN_WIDTH_20_NOHT) |
					BIT(NL80211_CHAN_WIDTH_20) |
					BIT(NL80211_CHAN_WIDTH_40),
	}
#endif
};

#ifdef CONFIG_ATH9K_CHANNEL_CONTEXT
static void ath9k_set_mcc_capab(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!ath9k_is_chanctx_enabled())
		return;

	hw->flags |= IEEE80211_HW_QUEUE_CONTROL;
	hw->queues = ATH9K_NUM_TX_QUEUES;
	hw->offchannel_tx_hw_queue = hw->queues - 1;
	hw->wiphy->interface_modes &= ~ BIT(NL80211_IFTYPE_WDS);
	hw->wiphy->iface_combinations = if_comb_multi;
	hw->wiphy->n_iface_combinations = ARRAY_SIZE(if_comb_multi);
	hw->wiphy->max_scan_ssids = 255;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	hw->wiphy->max_remain_on_channel_duration = 10000;
	hw->chanctx_data_size = sizeof(void *);
	hw->extra_beacon_tailroom =
		sizeof(struct ieee80211_p2p_noa_attr) + 9;

	ath_dbg(common, CHAN_CTX, "Use channel contexts\n");
}
#endif /* CONFIG_ATH9K_CHANNEL_CONTEXT */

static void ath9k_set_hw_capab(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_PS_NULLFUNC_STACK |
		IEEE80211_HW_SPECTRUM_MGMT |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		IEEE80211_HW_SUPPORTS_RC_TABLE |
		IEEE80211_HW_SUPPORTS_HT_CCK_RATES;

	if (ath9k_ps_enable)
		hw->flags |= IEEE80211_HW_SUPPORTS_PS;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT) {
		hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;

		if (AR_SREV_9280_20_OR_LATER(ah))
			hw->radiotap_mcs_details |=
				IEEE80211_RADIOTAP_MCS_HAVE_STBC;
	}

	if (AR_SREV_9160_10_OR_LATER(sc->sc_ah) || ath9k_modparam_nohwcrypt)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR |
			       NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			       NL80211_FEATURE_P2P_GO_CTWIN;

	if (!config_enabled(CONFIG_ATH9K_TX99)) {
		hw->wiphy->interface_modes =
			BIT(NL80211_IFTYPE_P2P_GO) |
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_AP) |
			BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_ADHOC) |
			BIT(NL80211_IFTYPE_MESH_POINT) |
			BIT(NL80211_IFTYPE_WDS);

			hw->wiphy->iface_combinations = if_comb;
			hw->wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	}

	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_5_10_MHZ;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;

	hw->queues = 4;
	hw->max_rates = 4;
	hw->max_listen_interval = 10;
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

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&common->sbands[IEEE80211_BAND_2GHZ];
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&common->sbands[IEEE80211_BAND_5GHZ];

#ifdef CONFIG_ATH9K_CHANNEL_CONTEXT
	ath9k_set_mcc_capab(sc, hw);
#endif
	ath9k_init_wow(hw);
	ath9k_cmn_reload_chainmask(ah);

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
	if (error)
		return error;

	ah = sc->sc_ah;
	common = ath9k_hw_common(ah);
	ath9k_set_hw_capab(sc, hw);

	/* Initialize regulatory */
	error = ath_regd_init(&common->regulatory, sc->hw->wiphy,
			      ath9k_reg_notifier);
	if (error)
		goto deinit;

	reg = &common->regulatory;

	/* Setup TX DMA */
	error = ath_tx_init(sc, ATH_TXBUF);
	if (error != 0)
		goto deinit;

	/* Setup RX DMA */
	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto deinit;

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
		goto rx_cleanup;

	error = ath9k_init_debug(ah);
	if (error) {
		ath_err(common, "Unable to create debugfs files\n");
		goto unregister;
	}

	/* Handle world regulatory */
	if (!ath_is_world_regd(reg)) {
		error = regulatory_hint(hw->wiphy, reg->alpha2);
		if (error)
			goto debug_cleanup;
	}

	ath_init_leds(sc);
	ath_start_rfkill_poll(sc);

	return 0;

debug_cleanup:
	ath9k_deinit_debug(sc);
unregister:
	ieee80211_unregister_hw(hw);
rx_cleanup:
	ath_rx_cleanup(sc);
deinit:
	ath9k_deinit_softc(sc);
	return error;
}

/*****************************/
/*     De-Initialization     */
/*****************************/

static void ath9k_deinit_softc(struct ath_softc *sc)
{
	int i = 0;

	ath9k_deinit_p2p(sc);
	ath9k_deinit_btcoex(sc);

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);

	del_timer_sync(&sc->sleep_timer);
	ath9k_hw_deinit(sc->sc_ah);
	if (sc->dfs_detector != NULL)
		sc->dfs_detector->exit(sc->dfs_detector);

	ath9k_eeprom_release(sc);
}

void ath9k_deinit_device(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;

	ath9k_ps_wakeup(sc);

	wiphy_rfkill_stop_polling(sc->hw->wiphy);
	ath_deinit_leds(sc);

	ath9k_ps_restore(sc);

	ath9k_deinit_debug(sc);
	ath9k_deinit_wow(hw);
	ieee80211_unregister_hw(hw);
	ath_rx_cleanup(sc);
	ath9k_deinit_softc(sc);
}

/************************/
/*     Module Hooks     */
/************************/

static int __init ath9k_init(void)
{
	int error;

	error = ath_pci_init();
	if (error < 0) {
		pr_err("No PCI devices found, driver not installed\n");
		error = -ENODEV;
		goto err_out;
	}

	error = ath_ahb_init();
	if (error < 0) {
		error = -ENODEV;
		goto err_pci_exit;
	}

	return 0;

 err_pci_exit:
	ath_pci_exit();
 err_out:
	return error;
}
module_init(ath9k_init);

static void __exit ath9k_exit(void)
{
	is_ath9k_unloaded = true;
	ath_ahb_exit();
	ath_pci_exit();
	pr_info("%s: Driver unloaded\n", dev_info);
}
module_exit(ath9k_exit);
