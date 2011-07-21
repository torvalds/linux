/*-
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2006 Devicescape Software, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/if.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/cache.h>
#include <linux/ethtool.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>

#include <net/ieee80211_radiotap.h>

#include <asm/unaligned.h>

#include "base.h"
#include "reg.h"
#include "debug.h"
#include "ani.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

int ath5k_modparam_nohwcrypt;
module_param_named(nohwcrypt, ath5k_modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static int modparam_all_channels;
module_param_named(all_channels, modparam_all_channels, bool, S_IRUGO);
MODULE_PARM_DESC(all_channels, "Expose all channels the device can use.");

static int modparam_fastchanswitch;
module_param_named(fastchanswitch, modparam_fastchanswitch, bool, S_IRUGO);
MODULE_PARM_DESC(fastchanswitch, "Enable fast channel switching for AR2413/AR5413 radios.");


/* Module info */
MODULE_AUTHOR("Jiri Slaby");
MODULE_AUTHOR("Nick Kossifidis");
MODULE_DESCRIPTION("Support for 5xxx series of Atheros 802.11 wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 5xxx WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static int ath5k_init(struct ieee80211_hw *hw);
static int ath5k_reset(struct ath5k_hw *ah, struct ieee80211_channel *chan,
								bool skip_pcu);

/* Known SREVs */
static const struct ath5k_srev_name srev_names[] = {
#ifdef CONFIG_ATHEROS_AR231X
	{ "5312",	AR5K_VERSION_MAC,	AR5K_SREV_AR5312_R2 },
	{ "5312",	AR5K_VERSION_MAC,	AR5K_SREV_AR5312_R7 },
	{ "2313",	AR5K_VERSION_MAC,	AR5K_SREV_AR2313_R8 },
	{ "2315",	AR5K_VERSION_MAC,	AR5K_SREV_AR2315_R6 },
	{ "2315",	AR5K_VERSION_MAC,	AR5K_SREV_AR2315_R7 },
	{ "2317",	AR5K_VERSION_MAC,	AR5K_SREV_AR2317_R1 },
	{ "2317",	AR5K_VERSION_MAC,	AR5K_SREV_AR2317_R2 },
#else
	{ "5210",	AR5K_VERSION_MAC,	AR5K_SREV_AR5210 },
	{ "5311",	AR5K_VERSION_MAC,	AR5K_SREV_AR5311 },
	{ "5311A",	AR5K_VERSION_MAC,	AR5K_SREV_AR5311A },
	{ "5311B",	AR5K_VERSION_MAC,	AR5K_SREV_AR5311B },
	{ "5211",	AR5K_VERSION_MAC,	AR5K_SREV_AR5211 },
	{ "5212",	AR5K_VERSION_MAC,	AR5K_SREV_AR5212 },
	{ "5213",	AR5K_VERSION_MAC,	AR5K_SREV_AR5213 },
	{ "5213A",	AR5K_VERSION_MAC,	AR5K_SREV_AR5213A },
	{ "2413",	AR5K_VERSION_MAC,	AR5K_SREV_AR2413 },
	{ "2414",	AR5K_VERSION_MAC,	AR5K_SREV_AR2414 },
	{ "5424",	AR5K_VERSION_MAC,	AR5K_SREV_AR5424 },
	{ "5413",	AR5K_VERSION_MAC,	AR5K_SREV_AR5413 },
	{ "5414",	AR5K_VERSION_MAC,	AR5K_SREV_AR5414 },
	{ "2415",	AR5K_VERSION_MAC,	AR5K_SREV_AR2415 },
	{ "5416",	AR5K_VERSION_MAC,	AR5K_SREV_AR5416 },
	{ "5418",	AR5K_VERSION_MAC,	AR5K_SREV_AR5418 },
	{ "2425",	AR5K_VERSION_MAC,	AR5K_SREV_AR2425 },
	{ "2417",	AR5K_VERSION_MAC,	AR5K_SREV_AR2417 },
#endif
	{ "xxxxx",	AR5K_VERSION_MAC,	AR5K_SREV_UNKNOWN },
	{ "5110",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5110 },
	{ "5111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5111 },
	{ "5111A",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5111A },
	{ "2111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2111 },
	{ "5112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112 },
	{ "5112A",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112A },
	{ "5112B",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112B },
	{ "2112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112 },
	{ "2112A",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112A },
	{ "2112B",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112B },
	{ "2413",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2413 },
	{ "5413",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5413 },
	{ "5424",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5424 },
	{ "5133",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5133 },
#ifdef CONFIG_ATHEROS_AR231X
	{ "2316",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2316 },
	{ "2317",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2317 },
#endif
	{ "xxxxx",	AR5K_VERSION_RAD,	AR5K_SREV_UNKNOWN },
};

static const struct ieee80211_rate ath5k_rates[] = {
	{ .bitrate = 10,
	  .hw_value = ATH5K_RATE_CODE_1M, },
	{ .bitrate = 20,
	  .hw_value = ATH5K_RATE_CODE_2M,
	  .hw_value_short = ATH5K_RATE_CODE_2M | AR5K_SET_SHORT_PREAMBLE,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = ATH5K_RATE_CODE_5_5M,
	  .hw_value_short = ATH5K_RATE_CODE_5_5M | AR5K_SET_SHORT_PREAMBLE,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = ATH5K_RATE_CODE_11M,
	  .hw_value_short = ATH5K_RATE_CODE_11M | AR5K_SET_SHORT_PREAMBLE,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = ATH5K_RATE_CODE_6M,
	  .flags = 0 },
	{ .bitrate = 90,
	  .hw_value = ATH5K_RATE_CODE_9M,
	  .flags = 0 },
	{ .bitrate = 120,
	  .hw_value = ATH5K_RATE_CODE_12M,
	  .flags = 0 },
	{ .bitrate = 180,
	  .hw_value = ATH5K_RATE_CODE_18M,
	  .flags = 0 },
	{ .bitrate = 240,
	  .hw_value = ATH5K_RATE_CODE_24M,
	  .flags = 0 },
	{ .bitrate = 360,
	  .hw_value = ATH5K_RATE_CODE_36M,
	  .flags = 0 },
	{ .bitrate = 480,
	  .hw_value = ATH5K_RATE_CODE_48M,
	  .flags = 0 },
	{ .bitrate = 540,
	  .hw_value = ATH5K_RATE_CODE_54M,
	  .flags = 0 },
	/* XR missing */
};

static inline u64 ath5k_extend_tsf(struct ath5k_hw *ah, u32 rstamp)
{
	u64 tsf = ath5k_hw_get_tsf64(ah);

	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;

	return (tsf & ~0x7fff) | rstamp;
}

const char *
ath5k_chip_name(enum ath5k_srev_type type, u_int16_t val)
{
	const char *name = "xxxxx";
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(srev_names); i++) {
		if (srev_names[i].sr_type != type)
			continue;

		if ((val & 0xf0) == srev_names[i].sr_val)
			name = srev_names[i].sr_name;

		if ((val & 0xff) == srev_names[i].sr_val) {
			name = srev_names[i].sr_name;
			break;
		}
	}

	return name;
}
static unsigned int ath5k_ioread32(void *hw_priv, u32 reg_offset)
{
	struct ath5k_hw *ah = (struct ath5k_hw *) hw_priv;
	return ath5k_hw_reg_read(ah, reg_offset);
}

static void ath5k_iowrite32(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath5k_hw *ah = (struct ath5k_hw *) hw_priv;
	ath5k_hw_reg_write(ah, val, reg_offset);
}

static const struct ath_ops ath5k_common_ops = {
	.read = ath5k_ioread32,
	.write = ath5k_iowrite32,
};

/***********************\
* Driver Initialization *
\***********************/

static int ath5k_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath5k_hw *ah = hw->priv;
	struct ath_regulatory *regulatory = ath5k_hw_regulatory(ah);

	return ath_reg_notifier_apply(wiphy, request, regulatory);
}

/********************\
* Channel/mode setup *
\********************/

/*
 * Returns true for the channel numbers used without all_channels modparam.
 */
static bool ath5k_is_standard_channel(short chan, enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_2GHZ && chan <= 14)
		return true;

	return	/* UNII 1,2 */
		(((chan & 3) == 0 && chan >= 36 && chan <= 64) ||
		/* midband */
		((chan & 3) == 0 && chan >= 100 && chan <= 140) ||
		/* UNII-3 */
		((chan & 3) == 1 && chan >= 149 && chan <= 165) ||
		/* 802.11j 5.030-5.080 GHz (20MHz) */
		(chan == 8 || chan == 12 || chan == 16) ||
		/* 802.11j 4.9GHz (20MHz) */
		(chan == 184 || chan == 188 || chan == 192 || chan == 196));
}

static unsigned int
ath5k_setup_channels(struct ath5k_hw *ah, struct ieee80211_channel *channels,
		unsigned int mode, unsigned int max)
{
	unsigned int count, size, chfreq, freq, ch;
	enum ieee80211_band band;

	switch (mode) {
	case AR5K_MODE_11A:
		/* 1..220, but 2GHz frequencies are filtered by check_channel */
		size = 220;
		chfreq = CHANNEL_5GHZ;
		band = IEEE80211_BAND_5GHZ;
		break;
	case AR5K_MODE_11B:
	case AR5K_MODE_11G:
		size = 26;
		chfreq = CHANNEL_2GHZ;
		band = IEEE80211_BAND_2GHZ;
		break;
	default:
		ATH5K_WARN(ah, "bad mode, not copying channels\n");
		return 0;
	}

	count = 0;
	for (ch = 1; ch <= size && count < max; ch++) {
		freq = ieee80211_channel_to_frequency(ch, band);

		if (freq == 0) /* mapping failed - not a standard channel */
			continue;

		/* Check if channel is supported by the chipset */
		if (!ath5k_channel_ok(ah, freq, chfreq))
			continue;

		if (!modparam_all_channels &&
		    !ath5k_is_standard_channel(ch, band))
			continue;

		/* Write channel info and increment counter */
		channels[count].center_freq = freq;
		channels[count].band = band;
		switch (mode) {
		case AR5K_MODE_11A:
		case AR5K_MODE_11G:
			channels[count].hw_value = chfreq | CHANNEL_OFDM;
			break;
		case AR5K_MODE_11B:
			channels[count].hw_value = CHANNEL_B;
		}

		count++;
	}

	return count;
}

static void
ath5k_setup_rate_idx(struct ath5k_hw *ah, struct ieee80211_supported_band *b)
{
	u8 i;

	for (i = 0; i < AR5K_MAX_RATES; i++)
		ah->rate_idx[b->band][i] = -1;

	for (i = 0; i < b->n_bitrates; i++) {
		ah->rate_idx[b->band][b->bitrates[i].hw_value] = i;
		if (b->bitrates[i].hw_value_short)
			ah->rate_idx[b->band][b->bitrates[i].hw_value_short] = i;
	}
}

static int
ath5k_setup_bands(struct ieee80211_hw *hw)
{
	struct ath5k_hw *ah = hw->priv;
	struct ieee80211_supported_band *sband;
	int max_c, count_c = 0;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(ah->sbands) < IEEE80211_NUM_BANDS);
	max_c = ARRAY_SIZE(ah->channels);

	/* 2GHz band */
	sband = &ah->sbands[IEEE80211_BAND_2GHZ];
	sband->band = IEEE80211_BAND_2GHZ;
	sband->bitrates = &ah->rates[IEEE80211_BAND_2GHZ][0];

	if (test_bit(AR5K_MODE_11G, ah->ah_capabilities.cap_mode)) {
		/* G mode */
		memcpy(sband->bitrates, &ath5k_rates[0],
		       sizeof(struct ieee80211_rate) * 12);
		sband->n_bitrates = 12;

		sband->channels = ah->channels;
		sband->n_channels = ath5k_setup_channels(ah, sband->channels,
					AR5K_MODE_11G, max_c);

		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;
		count_c = sband->n_channels;
		max_c -= count_c;
	} else if (test_bit(AR5K_MODE_11B, ah->ah_capabilities.cap_mode)) {
		/* B mode */
		memcpy(sband->bitrates, &ath5k_rates[0],
		       sizeof(struct ieee80211_rate) * 4);
		sband->n_bitrates = 4;

		/* 5211 only supports B rates and uses 4bit rate codes
		 * (e.g normally we have 0x1B for 1M, but on 5211 we have 0x0B)
		 * fix them up here:
		 */
		if (ah->ah_version == AR5K_AR5211) {
			for (i = 0; i < 4; i++) {
				sband->bitrates[i].hw_value =
					sband->bitrates[i].hw_value & 0xF;
				sband->bitrates[i].hw_value_short =
					sband->bitrates[i].hw_value_short & 0xF;
			}
		}

		sband->channels = ah->channels;
		sband->n_channels = ath5k_setup_channels(ah, sband->channels,
					AR5K_MODE_11B, max_c);

		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;
		count_c = sband->n_channels;
		max_c -= count_c;
	}
	ath5k_setup_rate_idx(ah, sband);

	/* 5GHz band, A mode */
	if (test_bit(AR5K_MODE_11A, ah->ah_capabilities.cap_mode)) {
		sband = &ah->sbands[IEEE80211_BAND_5GHZ];
		sband->band = IEEE80211_BAND_5GHZ;
		sband->bitrates = &ah->rates[IEEE80211_BAND_5GHZ][0];

		memcpy(sband->bitrates, &ath5k_rates[4],
		       sizeof(struct ieee80211_rate) * 8);
		sband->n_bitrates = 8;

		sband->channels = &ah->channels[count_c];
		sband->n_channels = ath5k_setup_channels(ah, sband->channels,
					AR5K_MODE_11A, max_c);

		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = sband;
	}
	ath5k_setup_rate_idx(ah, sband);

	ath5k_debug_dump_bands(ah);

	return 0;
}

/*
 * Set/change channels. We always reset the chip.
 * To accomplish this we must first cleanup any pending DMA,
 * then restart stuff after a la  ath5k_init.
 *
 * Called with ah->lock.
 */
int
ath5k_chan_set(struct ath5k_hw *ah, struct ieee80211_channel *chan)
{
	ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
		  "channel set, resetting (%u -> %u MHz)\n",
		  ah->curchan->center_freq, chan->center_freq);

	/*
	 * To switch channels clear any pending DMA operations;
	 * wait long enough for the RX fifo to drain, reset the
	 * hardware at the new frequency, and then re-enable
	 * the relevant bits of the h/w.
	 */
	return ath5k_reset(ah, chan, true);
}

void ath5k_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath5k_vif_iter_data *iter_data = data;
	int i;
	struct ath5k_vif *avf = (void *)vif->drv_priv;

	if (iter_data->hw_macaddr)
		for (i = 0; i < ETH_ALEN; i++)
			iter_data->mask[i] &=
				~(iter_data->hw_macaddr[i] ^ mac[i]);

	if (!iter_data->found_active) {
		iter_data->found_active = true;
		memcpy(iter_data->active_mac, mac, ETH_ALEN);
	}

	if (iter_data->need_set_hw_addr && iter_data->hw_macaddr)
		if (compare_ether_addr(iter_data->hw_macaddr, mac) == 0)
			iter_data->need_set_hw_addr = false;

	if (!iter_data->any_assoc) {
		if (avf->assoc)
			iter_data->any_assoc = true;
	}

	/* Calculate combined mode - when APs are active, operate in AP mode.
	 * Otherwise use the mode of the new interface. This can currently
	 * only deal with combinations of APs and STAs. Only one ad-hoc
	 * interfaces is allowed.
	 */
	if (avf->opmode == NL80211_IFTYPE_AP)
		iter_data->opmode = NL80211_IFTYPE_AP;
	else {
		if (avf->opmode == NL80211_IFTYPE_STATION)
			iter_data->n_stas++;
		if (iter_data->opmode == NL80211_IFTYPE_UNSPECIFIED)
			iter_data->opmode = avf->opmode;
	}
}

void
ath5k_update_bssid_mask_and_opmode(struct ath5k_hw *ah,
				   struct ieee80211_vif *vif)
{
	struct ath_common *common = ath5k_hw_common(ah);
	struct ath5k_vif_iter_data iter_data;
	u32 rfilt;

	/*
	 * Use the hardware MAC address as reference, the hardware uses it
	 * together with the BSSID mask when matching addresses.
	 */
	iter_data.hw_macaddr = common->macaddr;
	memset(&iter_data.mask, 0xff, ETH_ALEN);
	iter_data.found_active = false;
	iter_data.need_set_hw_addr = true;
	iter_data.opmode = NL80211_IFTYPE_UNSPECIFIED;
	iter_data.n_stas = 0;

	if (vif)
		ath5k_vif_iter(&iter_data, vif->addr, vif);

	/* Get list of all active MAC addresses */
	ieee80211_iterate_active_interfaces_atomic(ah->hw, ath5k_vif_iter,
						   &iter_data);
	memcpy(ah->bssidmask, iter_data.mask, ETH_ALEN);

	ah->opmode = iter_data.opmode;
	if (ah->opmode == NL80211_IFTYPE_UNSPECIFIED)
		/* Nothing active, default to station mode */
		ah->opmode = NL80211_IFTYPE_STATION;

	ath5k_hw_set_opmode(ah, ah->opmode);
	ATH5K_DBG(ah, ATH5K_DEBUG_MODE, "mode setup opmode %d (%s)\n",
		  ah->opmode, ath_opmode_to_string(ah->opmode));

	if (iter_data.need_set_hw_addr && iter_data.found_active)
		ath5k_hw_set_lladdr(ah, iter_data.active_mac);

	if (ath5k_hw_hasbssidmask(ah))
		ath5k_hw_set_bssid_mask(ah, ah->bssidmask);

	/* Set up RX Filter */
	if (iter_data.n_stas > 1) {
		/* If you have multiple STA interfaces connected to
		 * different APs, ARPs are not received (most of the time?)
		 * Enabling PROMISC appears to fix that problem.
		 */
		ah->filter_flags |= AR5K_RX_FILTER_PROM;
	}

	rfilt = ah->filter_flags;
	ath5k_hw_set_rx_filter(ah, rfilt);
	ATH5K_DBG(ah, ATH5K_DEBUG_MODE, "RX filter 0x%x\n", rfilt);
}

static inline int
ath5k_hw_to_driver_rix(struct ath5k_hw *ah, int hw_rix)
{
	int rix;

	/* return base rate on errors */
	if (WARN(hw_rix < 0 || hw_rix >= AR5K_MAX_RATES,
			"hw_rix out of bounds: %x\n", hw_rix))
		return 0;

	rix = ah->rate_idx[ah->curchan->band][hw_rix];
	if (WARN(rix < 0, "invalid hw_rix: %x\n", hw_rix))
		rix = 0;

	return rix;
}

/***************\
* Buffers setup *
\***************/

static
struct sk_buff *ath5k_rx_skb_alloc(struct ath5k_hw *ah, dma_addr_t *skb_addr)
{
	struct ath_common *common = ath5k_hw_common(ah);
	struct sk_buff *skb;

	/*
	 * Allocate buffer with headroom_needed space for the
	 * fake physical layer header at the start.
	 */
	skb = ath_rxbuf_alloc(common,
			      common->rx_bufsize,
			      GFP_ATOMIC);

	if (!skb) {
		ATH5K_ERR(ah, "can't alloc skbuff of size %u\n",
				common->rx_bufsize);
		return NULL;
	}

	*skb_addr = dma_map_single(ah->dev,
				   skb->data, common->rx_bufsize,
				   DMA_FROM_DEVICE);

	if (unlikely(dma_mapping_error(ah->dev, *skb_addr))) {
		ATH5K_ERR(ah, "%s: DMA mapping failed\n", __func__);
		dev_kfree_skb(skb);
		return NULL;
	}
	return skb;
}

static int
ath5k_rxbuf_setup(struct ath5k_hw *ah, struct ath5k_buf *bf)
{
	struct sk_buff *skb = bf->skb;
	struct ath5k_desc *ds;
	int ret;

	if (!skb) {
		skb = ath5k_rx_skb_alloc(ah, &bf->skbaddr);
		if (!skb)
			return -ENOMEM;
		bf->skb = skb;
	}

	/*
	 * Setup descriptors.  For receive we always terminate
	 * the descriptor list with a self-linked entry so we'll
	 * not get overrun under high load (as can happen with a
	 * 5212 when ANI processing enables PHY error frames).
	 *
	 * To ensure the last descriptor is self-linked we create
	 * each descriptor as self-linked and add it to the end.  As
	 * each additional descriptor is added the previous self-linked
	 * entry is "fixed" naturally.  This should be safe even
	 * if DMA is happening.  When processing RX interrupts we
	 * never remove/process the last, self-linked, entry on the
	 * descriptor list.  This ensures the hardware always has
	 * someplace to write a new frame.
	 */
	ds = bf->desc;
	ds->ds_link = bf->daddr;	/* link to self */
	ds->ds_data = bf->skbaddr;
	ret = ath5k_hw_setup_rx_desc(ah, ds, ah->common.rx_bufsize, 0);
	if (ret) {
		ATH5K_ERR(ah, "%s: could not setup RX desc\n", __func__);
		return ret;
	}

	if (ah->rxlink != NULL)
		*ah->rxlink = bf->daddr;
	ah->rxlink = &ds->ds_link;
	return 0;
}

static enum ath5k_pkt_type get_hw_packet_type(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	enum ath5k_pkt_type htype;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	if (ieee80211_is_beacon(fc))
		htype = AR5K_PKT_TYPE_BEACON;
	else if (ieee80211_is_probe_resp(fc))
		htype = AR5K_PKT_TYPE_PROBE_RESP;
	else if (ieee80211_is_atim(fc))
		htype = AR5K_PKT_TYPE_ATIM;
	else if (ieee80211_is_pspoll(fc))
		htype = AR5K_PKT_TYPE_PSPOLL;
	else
		htype = AR5K_PKT_TYPE_NORMAL;

	return htype;
}

static int
ath5k_txbuf_setup(struct ath5k_hw *ah, struct ath5k_buf *bf,
		  struct ath5k_txq *txq, int padsize)
{
	struct ath5k_desc *ds = bf->desc;
	struct sk_buff *skb = bf->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned int pktlen, flags, keyidx = AR5K_TXKEYIX_INVALID;
	struct ieee80211_rate *rate;
	unsigned int mrr_rate[3], mrr_tries[3];
	int i, ret;
	u16 hw_rate;
	u16 cts_rate = 0;
	u16 duration = 0;
	u8 rc_flags;

	flags = AR5K_TXDESC_INTREQ | AR5K_TXDESC_CLRDMASK;

	/* XXX endianness */
	bf->skbaddr = dma_map_single(ah->dev, skb->data, skb->len,
			DMA_TO_DEVICE);

	rate = ieee80211_get_tx_rate(ah->hw, info);
	if (!rate) {
		ret = -EINVAL;
		goto err_unmap;
	}

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		flags |= AR5K_TXDESC_NOACK;

	rc_flags = info->control.rates[0].flags;
	hw_rate = (rc_flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE) ?
		rate->hw_value_short : rate->hw_value;

	pktlen = skb->len;

	/* FIXME: If we are in g mode and rate is a CCK rate
	 * subtract ah->ah_txpower.txp_cck_ofdm_pwr_delta
	 * from tx power (value is in dB units already) */
	if (info->control.hw_key) {
		keyidx = info->control.hw_key->hw_key_idx;
		pktlen += info->control.hw_key->icv_len;
	}
	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		flags |= AR5K_TXDESC_RTSENA;
		cts_rate = ieee80211_get_rts_cts_rate(ah->hw, info)->hw_value;
		duration = le16_to_cpu(ieee80211_rts_duration(ah->hw,
			info->control.vif, pktlen, info));
	}
	if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		flags |= AR5K_TXDESC_CTSENA;
		cts_rate = ieee80211_get_rts_cts_rate(ah->hw, info)->hw_value;
		duration = le16_to_cpu(ieee80211_ctstoself_duration(ah->hw,
			info->control.vif, pktlen, info));
	}
	ret = ah->ah_setup_tx_desc(ah, ds, pktlen,
		ieee80211_get_hdrlen_from_skb(skb), padsize,
		get_hw_packet_type(skb),
		(ah->power_level * 2),
		hw_rate,
		info->control.rates[0].count, keyidx, ah->ah_tx_ant, flags,
		cts_rate, duration);
	if (ret)
		goto err_unmap;

	memset(mrr_rate, 0, sizeof(mrr_rate));
	memset(mrr_tries, 0, sizeof(mrr_tries));
	for (i = 0; i < 3; i++) {
		rate = ieee80211_get_alt_retry_rate(ah->hw, info, i);
		if (!rate)
			break;

		mrr_rate[i] = rate->hw_value;
		mrr_tries[i] = info->control.rates[i + 1].count;
	}

	ath5k_hw_setup_mrr_tx_desc(ah, ds,
		mrr_rate[0], mrr_tries[0],
		mrr_rate[1], mrr_tries[1],
		mrr_rate[2], mrr_tries[2]);

	ds->ds_link = 0;
	ds->ds_data = bf->skbaddr;

	spin_lock_bh(&txq->lock);
	list_add_tail(&bf->list, &txq->q);
	txq->txq_len++;
	if (txq->link == NULL) /* is this first packet? */
		ath5k_hw_set_txdp(ah, txq->qnum, bf->daddr);
	else /* no, so only link it */
		*txq->link = bf->daddr;

	txq->link = &ds->ds_link;
	ath5k_hw_start_tx_dma(ah, txq->qnum);
	mmiowb();
	spin_unlock_bh(&txq->lock);

	return 0;
err_unmap:
	dma_unmap_single(ah->dev, bf->skbaddr, skb->len, DMA_TO_DEVICE);
	return ret;
}

/*******************\
* Descriptors setup *
\*******************/

static int
ath5k_desc_alloc(struct ath5k_hw *ah)
{
	struct ath5k_desc *ds;
	struct ath5k_buf *bf;
	dma_addr_t da;
	unsigned int i;
	int ret;

	/* allocate descriptors */
	ah->desc_len = sizeof(struct ath5k_desc) *
			(ATH_TXBUF + ATH_RXBUF + ATH_BCBUF + 1);

	ah->desc = dma_alloc_coherent(ah->dev, ah->desc_len,
				&ah->desc_daddr, GFP_KERNEL);
	if (ah->desc == NULL) {
		ATH5K_ERR(ah, "can't allocate descriptors\n");
		ret = -ENOMEM;
		goto err;
	}
	ds = ah->desc;
	da = ah->desc_daddr;
	ATH5K_DBG(ah, ATH5K_DEBUG_ANY, "DMA map: %p (%zu) -> %llx\n",
		ds, ah->desc_len, (unsigned long long)ah->desc_daddr);

	bf = kcalloc(1 + ATH_TXBUF + ATH_RXBUF + ATH_BCBUF,
			sizeof(struct ath5k_buf), GFP_KERNEL);
	if (bf == NULL) {
		ATH5K_ERR(ah, "can't allocate bufptr\n");
		ret = -ENOMEM;
		goto err_free;
	}
	ah->bufptr = bf;

	INIT_LIST_HEAD(&ah->rxbuf);
	for (i = 0; i < ATH_RXBUF; i++, bf++, ds++, da += sizeof(*ds)) {
		bf->desc = ds;
		bf->daddr = da;
		list_add_tail(&bf->list, &ah->rxbuf);
	}

	INIT_LIST_HEAD(&ah->txbuf);
	ah->txbuf_len = ATH_TXBUF;
	for (i = 0; i < ATH_TXBUF; i++, bf++, ds++, da += sizeof(*ds)) {
		bf->desc = ds;
		bf->daddr = da;
		list_add_tail(&bf->list, &ah->txbuf);
	}

	/* beacon buffers */
	INIT_LIST_HEAD(&ah->bcbuf);
	for (i = 0; i < ATH_BCBUF; i++, bf++, ds++, da += sizeof(*ds)) {
		bf->desc = ds;
		bf->daddr = da;
		list_add_tail(&bf->list, &ah->bcbuf);
	}

	return 0;
err_free:
	dma_free_coherent(ah->dev, ah->desc_len, ah->desc, ah->desc_daddr);
err:
	ah->desc = NULL;
	return ret;
}

void
ath5k_txbuf_free_skb(struct ath5k_hw *ah, struct ath5k_buf *bf)
{
	BUG_ON(!bf);
	if (!bf->skb)
		return;
	dma_unmap_single(ah->dev, bf->skbaddr, bf->skb->len,
			DMA_TO_DEVICE);
	dev_kfree_skb_any(bf->skb);
	bf->skb = NULL;
	bf->skbaddr = 0;
	bf->desc->ds_data = 0;
}

void
ath5k_rxbuf_free_skb(struct ath5k_hw *ah, struct ath5k_buf *bf)
{
	struct ath_common *common = ath5k_hw_common(ah);

	BUG_ON(!bf);
	if (!bf->skb)
		return;
	dma_unmap_single(ah->dev, bf->skbaddr, common->rx_bufsize,
			DMA_FROM_DEVICE);
	dev_kfree_skb_any(bf->skb);
	bf->skb = NULL;
	bf->skbaddr = 0;
	bf->desc->ds_data = 0;
}

static void
ath5k_desc_free(struct ath5k_hw *ah)
{
	struct ath5k_buf *bf;

	list_for_each_entry(bf, &ah->txbuf, list)
		ath5k_txbuf_free_skb(ah, bf);
	list_for_each_entry(bf, &ah->rxbuf, list)
		ath5k_rxbuf_free_skb(ah, bf);
	list_for_each_entry(bf, &ah->bcbuf, list)
		ath5k_txbuf_free_skb(ah, bf);

	/* Free memory associated with all descriptors */
	dma_free_coherent(ah->dev, ah->desc_len, ah->desc, ah->desc_daddr);
	ah->desc = NULL;
	ah->desc_daddr = 0;

	kfree(ah->bufptr);
	ah->bufptr = NULL;
}


/**************\
* Queues setup *
\**************/

static struct ath5k_txq *
ath5k_txq_setup(struct ath5k_hw *ah,
		int qtype, int subtype)
{
	struct ath5k_txq *txq;
	struct ath5k_txq_info qi = {
		.tqi_subtype = subtype,
		/* XXX: default values not correct for B and XR channels,
		 * but who cares? */
		.tqi_aifs = AR5K_TUNE_AIFS,
		.tqi_cw_min = AR5K_TUNE_CWMIN,
		.tqi_cw_max = AR5K_TUNE_CWMAX
	};
	int qnum;

	/*
	 * Enable interrupts only for EOL and DESC conditions.
	 * We mark tx descriptors to receive a DESC interrupt
	 * when a tx queue gets deep; otherwise we wait for the
	 * EOL to reap descriptors.  Note that this is done to
	 * reduce interrupt load and this only defers reaping
	 * descriptors, never transmitting frames.  Aside from
	 * reducing interrupts this also permits more concurrency.
	 * The only potential downside is if the tx queue backs
	 * up in which case the top half of the kernel may backup
	 * due to a lack of tx descriptors.
	 */
	qi.tqi_flags = AR5K_TXQ_FLAG_TXEOLINT_ENABLE |
				AR5K_TXQ_FLAG_TXDESCINT_ENABLE;
	qnum = ath5k_hw_setup_tx_queue(ah, qtype, &qi);
	if (qnum < 0) {
		/*
		 * NB: don't print a message, this happens
		 * normally on parts with too few tx queues
		 */
		return ERR_PTR(qnum);
	}
	if (qnum >= ARRAY_SIZE(ah->txqs)) {
		ATH5K_ERR(ah, "hw qnum %u out of range, max %tu!\n",
			qnum, ARRAY_SIZE(ah->txqs));
		ath5k_hw_release_tx_queue(ah, qnum);
		return ERR_PTR(-EINVAL);
	}
	txq = &ah->txqs[qnum];
	if (!txq->setup) {
		txq->qnum = qnum;
		txq->link = NULL;
		INIT_LIST_HEAD(&txq->q);
		spin_lock_init(&txq->lock);
		txq->setup = true;
		txq->txq_len = 0;
		txq->txq_max = ATH5K_TXQ_LEN_MAX;
		txq->txq_poll_mark = false;
		txq->txq_stuck = 0;
	}
	return &ah->txqs[qnum];
}

static int
ath5k_beaconq_setup(struct ath5k_hw *ah)
{
	struct ath5k_txq_info qi = {
		/* XXX: default values not correct for B and XR channels,
		 * but who cares? */
		.tqi_aifs = AR5K_TUNE_AIFS,
		.tqi_cw_min = AR5K_TUNE_CWMIN,
		.tqi_cw_max = AR5K_TUNE_CWMAX,
		/* NB: for dynamic turbo, don't enable any other interrupts */
		.tqi_flags = AR5K_TXQ_FLAG_TXDESCINT_ENABLE
	};

	return ath5k_hw_setup_tx_queue(ah, AR5K_TX_QUEUE_BEACON, &qi);
}

static int
ath5k_beaconq_config(struct ath5k_hw *ah)
{
	struct ath5k_txq_info qi;
	int ret;

	ret = ath5k_hw_get_tx_queueprops(ah, ah->bhalq, &qi);
	if (ret)
		goto err;

	if (ah->opmode == NL80211_IFTYPE_AP ||
	    ah->opmode == NL80211_IFTYPE_MESH_POINT) {
		/*
		 * Always burst out beacon and CAB traffic
		 * (aifs = cwmin = cwmax = 0)
		 */
		qi.tqi_aifs = 0;
		qi.tqi_cw_min = 0;
		qi.tqi_cw_max = 0;
	} else if (ah->opmode == NL80211_IFTYPE_ADHOC) {
		/*
		 * Adhoc mode; backoff between 0 and (2 * cw_min).
		 */
		qi.tqi_aifs = 0;
		qi.tqi_cw_min = 0;
		qi.tqi_cw_max = 2 * AR5K_TUNE_CWMIN;
	}

	ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
		"beacon queueprops tqi_aifs:%d tqi_cw_min:%d tqi_cw_max:%d\n",
		qi.tqi_aifs, qi.tqi_cw_min, qi.tqi_cw_max);

	ret = ath5k_hw_set_tx_queueprops(ah, ah->bhalq, &qi);
	if (ret) {
		ATH5K_ERR(ah, "%s: unable to update parameters for beacon "
			"hardware queue!\n", __func__);
		goto err;
	}
	ret = ath5k_hw_reset_tx_queue(ah, ah->bhalq); /* push to h/w */
	if (ret)
		goto err;

	/* reconfigure cabq with ready time to 80% of beacon_interval */
	ret = ath5k_hw_get_tx_queueprops(ah, AR5K_TX_QUEUE_ID_CAB, &qi);
	if (ret)
		goto err;

	qi.tqi_ready_time = (ah->bintval * 80) / 100;
	ret = ath5k_hw_set_tx_queueprops(ah, AR5K_TX_QUEUE_ID_CAB, &qi);
	if (ret)
		goto err;

	ret = ath5k_hw_reset_tx_queue(ah, AR5K_TX_QUEUE_ID_CAB);
err:
	return ret;
}

/**
 * ath5k_drain_tx_buffs - Empty tx buffers
 *
 * @ah The &struct ath5k_hw
 *
 * Empty tx buffers from all queues in preparation
 * of a reset or during shutdown.
 *
 * NB:	this assumes output has been stopped and
 *	we do not need to block ath5k_tx_tasklet
 */
static void
ath5k_drain_tx_buffs(struct ath5k_hw *ah)
{
	struct ath5k_txq *txq;
	struct ath5k_buf *bf, *bf0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ah->txqs); i++) {
		if (ah->txqs[i].setup) {
			txq = &ah->txqs[i];
			spin_lock_bh(&txq->lock);
			list_for_each_entry_safe(bf, bf0, &txq->q, list) {
				ath5k_debug_printtxbuf(ah, bf);

				ath5k_txbuf_free_skb(ah, bf);

				spin_lock_bh(&ah->txbuflock);
				list_move_tail(&bf->list, &ah->txbuf);
				ah->txbuf_len++;
				txq->txq_len--;
				spin_unlock_bh(&ah->txbuflock);
			}
			txq->link = NULL;
			txq->txq_poll_mark = false;
			spin_unlock_bh(&txq->lock);
		}
	}
}

static void
ath5k_txq_release(struct ath5k_hw *ah)
{
	struct ath5k_txq *txq = ah->txqs;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ah->txqs); i++, txq++)
		if (txq->setup) {
			ath5k_hw_release_tx_queue(ah, txq->qnum);
			txq->setup = false;
		}
}


/*************\
* RX Handling *
\*************/

/*
 * Enable the receive h/w following a reset.
 */
static int
ath5k_rx_start(struct ath5k_hw *ah)
{
	struct ath_common *common = ath5k_hw_common(ah);
	struct ath5k_buf *bf;
	int ret;

	common->rx_bufsize = roundup(IEEE80211_MAX_FRAME_LEN, common->cachelsz);

	ATH5K_DBG(ah, ATH5K_DEBUG_RESET, "cachelsz %u rx_bufsize %u\n",
		  common->cachelsz, common->rx_bufsize);

	spin_lock_bh(&ah->rxbuflock);
	ah->rxlink = NULL;
	list_for_each_entry(bf, &ah->rxbuf, list) {
		ret = ath5k_rxbuf_setup(ah, bf);
		if (ret != 0) {
			spin_unlock_bh(&ah->rxbuflock);
			goto err;
		}
	}
	bf = list_first_entry(&ah->rxbuf, struct ath5k_buf, list);
	ath5k_hw_set_rxdp(ah, bf->daddr);
	spin_unlock_bh(&ah->rxbuflock);

	ath5k_hw_start_rx_dma(ah);	/* enable recv descriptors */
	ath5k_update_bssid_mask_and_opmode(ah, NULL); /* set filters, etc. */
	ath5k_hw_start_rx_pcu(ah);	/* re-enable PCU/DMA engine */

	return 0;
err:
	return ret;
}

/*
 * Disable the receive logic on PCU (DRU)
 * In preparation for a shutdown.
 *
 * Note: Doesn't stop rx DMA, ath5k_hw_dma_stop
 * does.
 */
static void
ath5k_rx_stop(struct ath5k_hw *ah)
{

	ath5k_hw_set_rx_filter(ah, 0);	/* clear recv filter */
	ath5k_hw_stop_rx_pcu(ah);	/* disable PCU */

	ath5k_debug_printrxbuffs(ah);
}

static unsigned int
ath5k_rx_decrypted(struct ath5k_hw *ah, struct sk_buff *skb,
		   struct ath5k_rx_status *rs)
{
	struct ath_common *common = ath5k_hw_common(ah);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int keyix, hlen;

	if (!(rs->rs_status & AR5K_RXERR_DECRYPT) &&
			rs->rs_keyix != AR5K_RXKEYIX_INVALID)
		return RX_FLAG_DECRYPTED;

	/* Apparently when a default key is used to decrypt the packet
	   the hw does not set the index used to decrypt.  In such cases
	   get the index from the packet. */
	hlen = ieee80211_hdrlen(hdr->frame_control);
	if (ieee80211_has_protected(hdr->frame_control) &&
	    !(rs->rs_status & AR5K_RXERR_DECRYPT) &&
	    skb->len >= hlen + 4) {
		keyix = skb->data[hlen + 3] >> 6;

		if (test_bit(keyix, common->keymap))
			return RX_FLAG_DECRYPTED;
	}

	return 0;
}


static void
ath5k_check_ibss_tsf(struct ath5k_hw *ah, struct sk_buff *skb,
		     struct ieee80211_rx_status *rxs)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u64 tsf, bc_tstamp;
	u32 hw_tu;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	if (ieee80211_is_beacon(mgmt->frame_control) &&
	    le16_to_cpu(mgmt->u.beacon.capab_info) & WLAN_CAPABILITY_IBSS &&
	    memcmp(mgmt->bssid, common->curbssid, ETH_ALEN) == 0) {
		/*
		 * Received an IBSS beacon with the same BSSID. Hardware *must*
		 * have updated the local TSF. We have to work around various
		 * hardware bugs, though...
		 */
		tsf = ath5k_hw_get_tsf64(ah);
		bc_tstamp = le64_to_cpu(mgmt->u.beacon.timestamp);
		hw_tu = TSF_TO_TU(tsf);

		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			"beacon %llx mactime %llx (diff %lld) tsf now %llx\n",
			(unsigned long long)bc_tstamp,
			(unsigned long long)rxs->mactime,
			(unsigned long long)(rxs->mactime - bc_tstamp),
			(unsigned long long)tsf);

		/*
		 * Sometimes the HW will give us a wrong tstamp in the rx
		 * status, causing the timestamp extension to go wrong.
		 * (This seems to happen especially with beacon frames bigger
		 * than 78 byte (incl. FCS))
		 * But we know that the receive timestamp must be later than the
		 * timestamp of the beacon since HW must have synced to that.
		 *
		 * NOTE: here we assume mactime to be after the frame was
		 * received, not like mac80211 which defines it at the start.
		 */
		if (bc_tstamp > rxs->mactime) {
			ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
				"fixing mactime from %llx to %llx\n",
				(unsigned long long)rxs->mactime,
				(unsigned long long)tsf);
			rxs->mactime = tsf;
		}

		/*
		 * Local TSF might have moved higher than our beacon timers,
		 * in that case we have to update them to continue sending
		 * beacons. This also takes care of synchronizing beacon sending
		 * times with other stations.
		 */
		if (hw_tu >= ah->nexttbtt)
			ath5k_beacon_update_timers(ah, bc_tstamp);

		/* Check if the beacon timers are still correct, because a TSF
		 * update might have created a window between them - for a
		 * longer description see the comment of this function: */
		if (!ath5k_hw_check_beacon_timers(ah, ah->bintval)) {
			ath5k_beacon_update_timers(ah, bc_tstamp);
			ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
				"fixed beacon timers after beacon receive\n");
		}
	}
}

static void
ath5k_update_beacon_rssi(struct ath5k_hw *ah, struct sk_buff *skb, int rssi)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ath_common *common = ath5k_hw_common(ah);

	/* only beacons from our BSSID */
	if (!ieee80211_is_beacon(mgmt->frame_control) ||
	    memcmp(mgmt->bssid, common->curbssid, ETH_ALEN) != 0)
		return;

	ewma_add(&ah->ah_beacon_rssi_avg, rssi);

	/* in IBSS mode we should keep RSSI statistics per neighbour */
	/* le16_to_cpu(mgmt->u.beacon.capab_info) & WLAN_CAPABILITY_IBSS */
}

/*
 * Compute padding position. skb must contain an IEEE 802.11 frame
 */
static int ath5k_common_padpos(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 frame_control = hdr->frame_control;
	int padpos = 24;

	if (ieee80211_has_a4(frame_control))
		padpos += ETH_ALEN;

	if (ieee80211_is_data_qos(frame_control))
		padpos += IEEE80211_QOS_CTL_LEN;

	return padpos;
}

/*
 * This function expects an 802.11 frame and returns the number of
 * bytes added, or -1 if we don't have enough header room.
 */
static int ath5k_add_padding(struct sk_buff *skb)
{
	int padpos = ath5k_common_padpos(skb);
	int padsize = padpos & 3;

	if (padsize && skb->len > padpos) {

		if (skb_headroom(skb) < padsize)
			return -1;

		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, padpos);
		return padsize;
	}

	return 0;
}

/*
 * The MAC header is padded to have 32-bit boundary if the
 * packet payload is non-zero. The general calculation for
 * padsize would take into account odd header lengths:
 * padsize = 4 - (hdrlen & 3); however, since only
 * even-length headers are used, padding can only be 0 or 2
 * bytes and we can optimize this a bit.  We must not try to
 * remove padding from short control frames that do not have a
 * payload.
 *
 * This function expects an 802.11 frame and returns the number of
 * bytes removed.
 */
static int ath5k_remove_padding(struct sk_buff *skb)
{
	int padpos = ath5k_common_padpos(skb);
	int padsize = padpos & 3;

	if (padsize && skb->len >= padpos + padsize) {
		memmove(skb->data + padsize, skb->data, padpos);
		skb_pull(skb, padsize);
		return padsize;
	}

	return 0;
}

static void
ath5k_receive_frame(struct ath5k_hw *ah, struct sk_buff *skb,
		    struct ath5k_rx_status *rs)
{
	struct ieee80211_rx_status *rxs;

	ath5k_remove_padding(skb);

	rxs = IEEE80211_SKB_RXCB(skb);

	rxs->flag = 0;
	if (unlikely(rs->rs_status & AR5K_RXERR_MIC))
		rxs->flag |= RX_FLAG_MMIC_ERROR;

	/*
	 * always extend the mac timestamp, since this information is
	 * also needed for proper IBSS merging.
	 *
	 * XXX: it might be too late to do it here, since rs_tstamp is
	 * 15bit only. that means TSF extension has to be done within
	 * 32768usec (about 32ms). it might be necessary to move this to
	 * the interrupt handler, like it is done in madwifi.
	 *
	 * Unfortunately we don't know when the hardware takes the rx
	 * timestamp (beginning of phy frame, data frame, end of rx?).
	 * The only thing we know is that it is hardware specific...
	 * On AR5213 it seems the rx timestamp is at the end of the
	 * frame, but I'm not sure.
	 *
	 * NOTE: mac80211 defines mactime at the beginning of the first
	 * data symbol. Since we don't have any time references it's
	 * impossible to comply to that. This affects IBSS merge only
	 * right now, so it's not too bad...
	 */
	rxs->mactime = ath5k_extend_tsf(ah, rs->rs_tstamp);
	rxs->flag |= RX_FLAG_MACTIME_MPDU;

	rxs->freq = ah->curchan->center_freq;
	rxs->band = ah->curchan->band;

	rxs->signal = ah->ah_noise_floor + rs->rs_rssi;

	rxs->antenna = rs->rs_antenna;

	if (rs->rs_antenna > 0 && rs->rs_antenna < 5)
		ah->stats.antenna_rx[rs->rs_antenna]++;
	else
		ah->stats.antenna_rx[0]++; /* invalid */

	rxs->rate_idx = ath5k_hw_to_driver_rix(ah, rs->rs_rate);
	rxs->flag |= ath5k_rx_decrypted(ah, skb, rs);

	if (rxs->rate_idx >= 0 && rs->rs_rate ==
	    ah->sbands[ah->curchan->band].bitrates[rxs->rate_idx].hw_value_short)
		rxs->flag |= RX_FLAG_SHORTPRE;

	trace_ath5k_rx(ah, skb);

	ath5k_update_beacon_rssi(ah, skb, rs->rs_rssi);

	/* check beacons in IBSS mode */
	if (ah->opmode == NL80211_IFTYPE_ADHOC)
		ath5k_check_ibss_tsf(ah, skb, rxs);

	ieee80211_rx(ah->hw, skb);
}

/** ath5k_frame_receive_ok() - Do we want to receive this frame or not?
 *
 * Check if we want to further process this frame or not. Also update
 * statistics. Return true if we want this frame, false if not.
 */
static bool
ath5k_receive_frame_ok(struct ath5k_hw *ah, struct ath5k_rx_status *rs)
{
	ah->stats.rx_all_count++;
	ah->stats.rx_bytes_count += rs->rs_datalen;

	if (unlikely(rs->rs_status)) {
		if (rs->rs_status & AR5K_RXERR_CRC)
			ah->stats.rxerr_crc++;
		if (rs->rs_status & AR5K_RXERR_FIFO)
			ah->stats.rxerr_fifo++;
		if (rs->rs_status & AR5K_RXERR_PHY) {
			ah->stats.rxerr_phy++;
			if (rs->rs_phyerr > 0 && rs->rs_phyerr < 32)
				ah->stats.rxerr_phy_code[rs->rs_phyerr]++;
			return false;
		}
		if (rs->rs_status & AR5K_RXERR_DECRYPT) {
			/*
			 * Decrypt error.  If the error occurred
			 * because there was no hardware key, then
			 * let the frame through so the upper layers
			 * can process it.  This is necessary for 5210
			 * parts which have no way to setup a ``clear''
			 * key cache entry.
			 *
			 * XXX do key cache faulting
			 */
			ah->stats.rxerr_decrypt++;
			if (rs->rs_keyix == AR5K_RXKEYIX_INVALID &&
			    !(rs->rs_status & AR5K_RXERR_CRC))
				return true;
		}
		if (rs->rs_status & AR5K_RXERR_MIC) {
			ah->stats.rxerr_mic++;
			return true;
		}

		/* reject any frames with non-crypto errors */
		if (rs->rs_status & ~(AR5K_RXERR_DECRYPT))
			return false;
	}

	if (unlikely(rs->rs_more)) {
		ah->stats.rxerr_jumbo++;
		return false;
	}
	return true;
}

static void
ath5k_set_current_imask(struct ath5k_hw *ah)
{
	enum ath5k_int imask;
	unsigned long flags;

	spin_lock_irqsave(&ah->irqlock, flags);
	imask = ah->imask;
	if (ah->rx_pending)
		imask &= ~AR5K_INT_RX_ALL;
	if (ah->tx_pending)
		imask &= ~AR5K_INT_TX_ALL;
	ath5k_hw_set_imr(ah, imask);
	spin_unlock_irqrestore(&ah->irqlock, flags);
}

static void
ath5k_tasklet_rx(unsigned long data)
{
	struct ath5k_rx_status rs = {};
	struct sk_buff *skb, *next_skb;
	dma_addr_t next_skb_addr;
	struct ath5k_hw *ah = (void *)data;
	struct ath_common *common = ath5k_hw_common(ah);
	struct ath5k_buf *bf;
	struct ath5k_desc *ds;
	int ret;

	spin_lock(&ah->rxbuflock);
	if (list_empty(&ah->rxbuf)) {
		ATH5K_WARN(ah, "empty rx buf pool\n");
		goto unlock;
	}
	do {
		bf = list_first_entry(&ah->rxbuf, struct ath5k_buf, list);
		BUG_ON(bf->skb == NULL);
		skb = bf->skb;
		ds = bf->desc;

		/* bail if HW is still using self-linked descriptor */
		if (ath5k_hw_get_rxdp(ah) == bf->daddr)
			break;

		ret = ah->ah_proc_rx_desc(ah, ds, &rs);
		if (unlikely(ret == -EINPROGRESS))
			break;
		else if (unlikely(ret)) {
			ATH5K_ERR(ah, "error in processing rx descriptor\n");
			ah->stats.rxerr_proc++;
			break;
		}

		if (ath5k_receive_frame_ok(ah, &rs)) {
			next_skb = ath5k_rx_skb_alloc(ah, &next_skb_addr);

			/*
			 * If we can't replace bf->skb with a new skb under
			 * memory pressure, just skip this packet
			 */
			if (!next_skb)
				goto next;

			dma_unmap_single(ah->dev, bf->skbaddr,
					 common->rx_bufsize,
					 DMA_FROM_DEVICE);

			skb_put(skb, rs.rs_datalen);

			ath5k_receive_frame(ah, skb, &rs);

			bf->skb = next_skb;
			bf->skbaddr = next_skb_addr;
		}
next:
		list_move_tail(&bf->list, &ah->rxbuf);
	} while (ath5k_rxbuf_setup(ah, bf) == 0);
unlock:
	spin_unlock(&ah->rxbuflock);
	ah->rx_pending = false;
	ath5k_set_current_imask(ah);
}


/*************\
* TX Handling *
\*************/

void
ath5k_tx_queue(struct ieee80211_hw *hw, struct sk_buff *skb,
	       struct ath5k_txq *txq)
{
	struct ath5k_hw *ah = hw->priv;
	struct ath5k_buf *bf;
	unsigned long flags;
	int padsize;

	trace_ath5k_tx(ah, skb, txq);

	/*
	 * The hardware expects the header padded to 4 byte boundaries.
	 * If this is not the case, we add the padding after the header.
	 */
	padsize = ath5k_add_padding(skb);
	if (padsize < 0) {
		ATH5K_ERR(ah, "tx hdrlen not %%4: not enough"
			  " headroom to pad");
		goto drop_packet;
	}

	if (txq->txq_len >= txq->txq_max &&
	    txq->qnum <= AR5K_TX_QUEUE_ID_DATA_MAX)
		ieee80211_stop_queue(hw, txq->qnum);

	spin_lock_irqsave(&ah->txbuflock, flags);
	if (list_empty(&ah->txbuf)) {
		ATH5K_ERR(ah, "no further txbuf available, dropping packet\n");
		spin_unlock_irqrestore(&ah->txbuflock, flags);
		ieee80211_stop_queues(hw);
		goto drop_packet;
	}
	bf = list_first_entry(&ah->txbuf, struct ath5k_buf, list);
	list_del(&bf->list);
	ah->txbuf_len--;
	if (list_empty(&ah->txbuf))
		ieee80211_stop_queues(hw);
	spin_unlock_irqrestore(&ah->txbuflock, flags);

	bf->skb = skb;

	if (ath5k_txbuf_setup(ah, bf, txq, padsize)) {
		bf->skb = NULL;
		spin_lock_irqsave(&ah->txbuflock, flags);
		list_add_tail(&bf->list, &ah->txbuf);
		ah->txbuf_len++;
		spin_unlock_irqrestore(&ah->txbuflock, flags);
		goto drop_packet;
	}
	return;

drop_packet:
	dev_kfree_skb_any(skb);
}

static void
ath5k_tx_frame_completed(struct ath5k_hw *ah, struct sk_buff *skb,
			 struct ath5k_txq *txq, struct ath5k_tx_status *ts)
{
	struct ieee80211_tx_info *info;
	u8 tries[3];
	int i;

	ah->stats.tx_all_count++;
	ah->stats.tx_bytes_count += skb->len;
	info = IEEE80211_SKB_CB(skb);

	tries[0] = info->status.rates[0].count;
	tries[1] = info->status.rates[1].count;
	tries[2] = info->status.rates[2].count;

	ieee80211_tx_info_clear_status(info);

	for (i = 0; i < ts->ts_final_idx; i++) {
		struct ieee80211_tx_rate *r =
			&info->status.rates[i];

		r->count = tries[i];
	}

	info->status.rates[ts->ts_final_idx].count = ts->ts_final_retry;
	info->status.rates[ts->ts_final_idx + 1].idx = -1;

	if (unlikely(ts->ts_status)) {
		ah->stats.ack_fail++;
		if (ts->ts_status & AR5K_TXERR_FILT) {
			info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
			ah->stats.txerr_filt++;
		}
		if (ts->ts_status & AR5K_TXERR_XRETRY)
			ah->stats.txerr_retry++;
		if (ts->ts_status & AR5K_TXERR_FIFO)
			ah->stats.txerr_fifo++;
	} else {
		info->flags |= IEEE80211_TX_STAT_ACK;
		info->status.ack_signal = ts->ts_rssi;

		/* count the successful attempt as well */
		info->status.rates[ts->ts_final_idx].count++;
	}

	/*
	* Remove MAC header padding before giving the frame
	* back to mac80211.
	*/
	ath5k_remove_padding(skb);

	if (ts->ts_antenna > 0 && ts->ts_antenna < 5)
		ah->stats.antenna_tx[ts->ts_antenna]++;
	else
		ah->stats.antenna_tx[0]++; /* invalid */

	trace_ath5k_tx_complete(ah, skb, txq, ts);
	ieee80211_tx_status(ah->hw, skb);
}

static void
ath5k_tx_processq(struct ath5k_hw *ah, struct ath5k_txq *txq)
{
	struct ath5k_tx_status ts = {};
	struct ath5k_buf *bf, *bf0;
	struct ath5k_desc *ds;
	struct sk_buff *skb;
	int ret;

	spin_lock(&txq->lock);
	list_for_each_entry_safe(bf, bf0, &txq->q, list) {

		txq->txq_poll_mark = false;

		/* skb might already have been processed last time. */
		if (bf->skb != NULL) {
			ds = bf->desc;

			ret = ah->ah_proc_tx_desc(ah, ds, &ts);
			if (unlikely(ret == -EINPROGRESS))
				break;
			else if (unlikely(ret)) {
				ATH5K_ERR(ah,
					"error %d while processing "
					"queue %u\n", ret, txq->qnum);
				break;
			}

			skb = bf->skb;
			bf->skb = NULL;

			dma_unmap_single(ah->dev, bf->skbaddr, skb->len,
					DMA_TO_DEVICE);
			ath5k_tx_frame_completed(ah, skb, txq, &ts);
		}

		/*
		 * It's possible that the hardware can say the buffer is
		 * completed when it hasn't yet loaded the ds_link from
		 * host memory and moved on.
		 * Always keep the last descriptor to avoid HW races...
		 */
		if (ath5k_hw_get_txdp(ah, txq->qnum) != bf->daddr) {
			spin_lock(&ah->txbuflock);
			list_move_tail(&bf->list, &ah->txbuf);
			ah->txbuf_len++;
			txq->txq_len--;
			spin_unlock(&ah->txbuflock);
		}
	}
	spin_unlock(&txq->lock);
	if (txq->txq_len < ATH5K_TXQ_LEN_LOW && txq->qnum < 4)
		ieee80211_wake_queue(ah->hw, txq->qnum);
}

static void
ath5k_tasklet_tx(unsigned long data)
{
	int i;
	struct ath5k_hw *ah = (void *)data;

	for (i = 0; i < AR5K_NUM_TX_QUEUES; i++)
		if (ah->txqs[i].setup && (ah->ah_txq_isr & BIT(i)))
			ath5k_tx_processq(ah, &ah->txqs[i]);

	ah->tx_pending = false;
	ath5k_set_current_imask(ah);
}


/*****************\
* Beacon handling *
\*****************/

/*
 * Setup the beacon frame for transmit.
 */
static int
ath5k_beacon_setup(struct ath5k_hw *ah, struct ath5k_buf *bf)
{
	struct sk_buff *skb = bf->skb;
	struct	ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath5k_desc *ds;
	int ret = 0;
	u8 antenna;
	u32 flags;
	const int padsize = 0;

	bf->skbaddr = dma_map_single(ah->dev, skb->data, skb->len,
			DMA_TO_DEVICE);
	ATH5K_DBG(ah, ATH5K_DEBUG_BEACON, "skb %p [data %p len %u] "
			"skbaddr %llx\n", skb, skb->data, skb->len,
			(unsigned long long)bf->skbaddr);

	if (dma_mapping_error(ah->dev, bf->skbaddr)) {
		ATH5K_ERR(ah, "beacon DMA mapping failed\n");
		return -EIO;
	}

	ds = bf->desc;
	antenna = ah->ah_tx_ant;

	flags = AR5K_TXDESC_NOACK;
	if (ah->opmode == NL80211_IFTYPE_ADHOC && ath5k_hw_hasveol(ah)) {
		ds->ds_link = bf->daddr;	/* self-linked */
		flags |= AR5K_TXDESC_VEOL;
	} else
		ds->ds_link = 0;

	/*
	 * If we use multiple antennas on AP and use
	 * the Sectored AP scenario, switch antenna every
	 * 4 beacons to make sure everybody hears our AP.
	 * When a client tries to associate, hw will keep
	 * track of the tx antenna to be used for this client
	 * automatically, based on ACKed packets.
	 *
	 * Note: AP still listens and transmits RTS on the
	 * default antenna which is supposed to be an omni.
	 *
	 * Note2: On sectored scenarios it's possible to have
	 * multiple antennas (1 omni -- the default -- and 14
	 * sectors), so if we choose to actually support this
	 * mode, we need to allow the user to set how many antennas
	 * we have and tweak the code below to send beacons
	 * on all of them.
	 */
	if (ah->ah_ant_mode == AR5K_ANTMODE_SECTOR_AP)
		antenna = ah->bsent & 4 ? 2 : 1;


	/* FIXME: If we are in g mode and rate is a CCK rate
	 * subtract ah->ah_txpower.txp_cck_ofdm_pwr_delta
	 * from tx power (value is in dB units already) */
	ds->ds_data = bf->skbaddr;
	ret = ah->ah_setup_tx_desc(ah, ds, skb->len,
			ieee80211_get_hdrlen_from_skb(skb), padsize,
			AR5K_PKT_TYPE_BEACON, (ah->power_level * 2),
			ieee80211_get_tx_rate(ah->hw, info)->hw_value,
			1, AR5K_TXKEYIX_INVALID,
			antenna, flags, 0, 0);
	if (ret)
		goto err_unmap;

	return 0;
err_unmap:
	dma_unmap_single(ah->dev, bf->skbaddr, skb->len, DMA_TO_DEVICE);
	return ret;
}

/*
 * Updates the beacon that is sent by ath5k_beacon_send.  For adhoc,
 * this is called only once at config_bss time, for AP we do it every
 * SWBA interrupt so that the TIM will reflect buffered frames.
 *
 * Called with the beacon lock.
 */
int
ath5k_beacon_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	int ret;
	struct ath5k_hw *ah = hw->priv;
	struct ath5k_vif *avf = (void *)vif->drv_priv;
	struct sk_buff *skb;

	if (WARN_ON(!vif)) {
		ret = -EINVAL;
		goto out;
	}

	skb = ieee80211_beacon_get(hw, vif);

	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	ath5k_txbuf_free_skb(ah, avf->bbuf);
	avf->bbuf->skb = skb;
	ret = ath5k_beacon_setup(ah, avf->bbuf);
	if (ret)
		avf->bbuf->skb = NULL;
out:
	return ret;
}

/*
 * Transmit a beacon frame at SWBA.  Dynamic updates to the
 * frame contents are done as needed and the slot time is
 * also adjusted based on current state.
 *
 * This is called from software irq context (beacontq tasklets)
 * or user context from ath5k_beacon_config.
 */
static void
ath5k_beacon_send(struct ath5k_hw *ah)
{
	struct ieee80211_vif *vif;
	struct ath5k_vif *avf;
	struct ath5k_buf *bf;
	struct sk_buff *skb;

	ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON, "in beacon_send\n");

	/*
	 * Check if the previous beacon has gone out.  If
	 * not, don't don't try to post another: skip this
	 * period and wait for the next.  Missed beacons
	 * indicate a problem and should not occur.  If we
	 * miss too many consecutive beacons reset the device.
	 */
	if (unlikely(ath5k_hw_num_tx_pending(ah, ah->bhalq) != 0)) {
		ah->bmisscount++;
		ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
			"missed %u consecutive beacons\n", ah->bmisscount);
		if (ah->bmisscount > 10) {	/* NB: 10 is a guess */
			ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
				"stuck beacon time (%u missed)\n",
				ah->bmisscount);
			ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
				  "stuck beacon, resetting\n");
			ieee80211_queue_work(ah->hw, &ah->reset_work);
		}
		return;
	}
	if (unlikely(ah->bmisscount != 0)) {
		ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
			"resume beacon xmit after %u misses\n",
			ah->bmisscount);
		ah->bmisscount = 0;
	}

	if ((ah->opmode == NL80211_IFTYPE_AP && ah->num_ap_vifs > 1) ||
			ah->opmode == NL80211_IFTYPE_MESH_POINT) {
		u64 tsf = ath5k_hw_get_tsf64(ah);
		u32 tsftu = TSF_TO_TU(tsf);
		int slot = ((tsftu % ah->bintval) * ATH_BCBUF) / ah->bintval;
		vif = ah->bslot[(slot + 1) % ATH_BCBUF];
		ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
			"tsf %llx tsftu %x intval %u slot %u vif %p\n",
			(unsigned long long)tsf, tsftu, ah->bintval, slot, vif);
	} else /* only one interface */
		vif = ah->bslot[0];

	if (!vif)
		return;

	avf = (void *)vif->drv_priv;
	bf = avf->bbuf;
	if (unlikely(bf->skb == NULL || ah->opmode == NL80211_IFTYPE_STATION ||
		     ah->opmode == NL80211_IFTYPE_MONITOR)) {
		ATH5K_WARN(ah, "bf=%p bf_skb=%p\n", bf, bf ? bf->skb : NULL);
		return;
	}

	/*
	 * Stop any current dma and put the new frame on the queue.
	 * This should never fail since we check above that no frames
	 * are still pending on the queue.
	 */
	if (unlikely(ath5k_hw_stop_beacon_queue(ah, ah->bhalq))) {
		ATH5K_WARN(ah, "beacon queue %u didn't start/stop ?\n", ah->bhalq);
		/* NB: hw still stops DMA, so proceed */
	}

	/* refresh the beacon for AP or MESH mode */
	if (ah->opmode == NL80211_IFTYPE_AP ||
	    ah->opmode == NL80211_IFTYPE_MESH_POINT)
		ath5k_beacon_update(ah->hw, vif);

	trace_ath5k_tx(ah, bf->skb, &ah->txqs[ah->bhalq]);

	ath5k_hw_set_txdp(ah, ah->bhalq, bf->daddr);
	ath5k_hw_start_tx_dma(ah, ah->bhalq);
	ATH5K_DBG(ah, ATH5K_DEBUG_BEACON, "TXDP[%u] = %llx (%p)\n",
		ah->bhalq, (unsigned long long)bf->daddr, bf->desc);

	skb = ieee80211_get_buffered_bc(ah->hw, vif);
	while (skb) {
		ath5k_tx_queue(ah->hw, skb, ah->cabq);

		if (ah->cabq->txq_len >= ah->cabq->txq_max)
			break;

		skb = ieee80211_get_buffered_bc(ah->hw, vif);
	}

	ah->bsent++;
}

/**
 * ath5k_beacon_update_timers - update beacon timers
 *
 * @ah: struct ath5k_hw pointer we are operating on
 * @bc_tsf: the timestamp of the beacon. 0 to reset the TSF. -1 to perform a
 *          beacon timer update based on the current HW TSF.
 *
 * Calculate the next target beacon transmit time (TBTT) based on the timestamp
 * of a received beacon or the current local hardware TSF and write it to the
 * beacon timer registers.
 *
 * This is called in a variety of situations, e.g. when a beacon is received,
 * when a TSF update has been detected, but also when an new IBSS is created or
 * when we otherwise know we have to update the timers, but we keep it in this
 * function to have it all together in one place.
 */
void
ath5k_beacon_update_timers(struct ath5k_hw *ah, u64 bc_tsf)
{
	u32 nexttbtt, intval, hw_tu, bc_tu;
	u64 hw_tsf;

	intval = ah->bintval & AR5K_BEACON_PERIOD;
	if (ah->opmode == NL80211_IFTYPE_AP && ah->num_ap_vifs > 1) {
		intval /= ATH_BCBUF;	/* staggered multi-bss beacons */
		if (intval < 15)
			ATH5K_WARN(ah, "intval %u is too low, min 15\n",
				   intval);
	}
	if (WARN_ON(!intval))
		return;

	/* beacon TSF converted to TU */
	bc_tu = TSF_TO_TU(bc_tsf);

	/* current TSF converted to TU */
	hw_tsf = ath5k_hw_get_tsf64(ah);
	hw_tu = TSF_TO_TU(hw_tsf);

#define FUDGE (AR5K_TUNE_SW_BEACON_RESP + 3)
	/* We use FUDGE to make sure the next TBTT is ahead of the current TU.
	 * Since we later subtract AR5K_TUNE_SW_BEACON_RESP (10) in the timer
	 * configuration we need to make sure it is bigger than that. */

	if (bc_tsf == -1) {
		/*
		 * no beacons received, called internally.
		 * just need to refresh timers based on HW TSF.
		 */
		nexttbtt = roundup(hw_tu + FUDGE, intval);
	} else if (bc_tsf == 0) {
		/*
		 * no beacon received, probably called by ath5k_reset_tsf().
		 * reset TSF to start with 0.
		 */
		nexttbtt = intval;
		intval |= AR5K_BEACON_RESET_TSF;
	} else if (bc_tsf > hw_tsf) {
		/*
		 * beacon received, SW merge happened but HW TSF not yet updated.
		 * not possible to reconfigure timers yet, but next time we
		 * receive a beacon with the same BSSID, the hardware will
		 * automatically update the TSF and then we need to reconfigure
		 * the timers.
		 */
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			"need to wait for HW TSF sync\n");
		return;
	} else {
		/*
		 * most important case for beacon synchronization between STA.
		 *
		 * beacon received and HW TSF has been already updated by HW.
		 * update next TBTT based on the TSF of the beacon, but make
		 * sure it is ahead of our local TSF timer.
		 */
		nexttbtt = bc_tu + roundup(hw_tu + FUDGE - bc_tu, intval);
	}
#undef FUDGE

	ah->nexttbtt = nexttbtt;

	intval |= AR5K_BEACON_ENA;
	ath5k_hw_init_beacon(ah, nexttbtt, intval);

	/*
	 * debugging output last in order to preserve the time critical aspect
	 * of this function
	 */
	if (bc_tsf == -1)
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			"reconfigured timers based on HW TSF\n");
	else if (bc_tsf == 0)
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			"reset HW TSF and timers\n");
	else
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			"updated timers based on beacon TSF\n");

	ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON,
			  "bc_tsf %llx hw_tsf %llx bc_tu %u hw_tu %u nexttbtt %u\n",
			  (unsigned long long) bc_tsf,
			  (unsigned long long) hw_tsf, bc_tu, hw_tu, nexttbtt);
	ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_BEACON, "intval %u %s %s\n",
		intval & AR5K_BEACON_PERIOD,
		intval & AR5K_BEACON_ENA ? "AR5K_BEACON_ENA" : "",
		intval & AR5K_BEACON_RESET_TSF ? "AR5K_BEACON_RESET_TSF" : "");
}

/**
 * ath5k_beacon_config - Configure the beacon queues and interrupts
 *
 * @ah: struct ath5k_hw pointer we are operating on
 *
 * In IBSS mode we use a self-linked tx descriptor if possible. We enable SWBA
 * interrupts to detect TSF updates only.
 */
void
ath5k_beacon_config(struct ath5k_hw *ah)
{
	unsigned long flags;

	spin_lock_irqsave(&ah->block, flags);
	ah->bmisscount = 0;
	ah->imask &= ~(AR5K_INT_BMISS | AR5K_INT_SWBA);

	if (ah->enable_beacon) {
		/*
		 * In IBSS mode we use a self-linked tx descriptor and let the
		 * hardware send the beacons automatically. We have to load it
		 * only once here.
		 * We use the SWBA interrupt only to keep track of the beacon
		 * timers in order to detect automatic TSF updates.
		 */
		ath5k_beaconq_config(ah);

		ah->imask |= AR5K_INT_SWBA;

		if (ah->opmode == NL80211_IFTYPE_ADHOC) {
			if (ath5k_hw_hasveol(ah))
				ath5k_beacon_send(ah);
		} else
			ath5k_beacon_update_timers(ah, -1);
	} else {
		ath5k_hw_stop_beacon_queue(ah, ah->bhalq);
	}

	ath5k_hw_set_imr(ah, ah->imask);
	mmiowb();
	spin_unlock_irqrestore(&ah->block, flags);
}

static void ath5k_tasklet_beacon(unsigned long data)
{
	struct ath5k_hw *ah = (struct ath5k_hw *) data;

	/*
	 * Software beacon alert--time to send a beacon.
	 *
	 * In IBSS mode we use this interrupt just to
	 * keep track of the next TBTT (target beacon
	 * transmission time) in order to detect whether
	 * automatic TSF updates happened.
	 */
	if (ah->opmode == NL80211_IFTYPE_ADHOC) {
		/* XXX: only if VEOL supported */
		u64 tsf = ath5k_hw_get_tsf64(ah);
		ah->nexttbtt += ah->bintval;
		ATH5K_DBG(ah, ATH5K_DEBUG_BEACON,
				"SWBA nexttbtt: %x hw_tu: %x "
				"TSF: %llx\n",
				ah->nexttbtt,
				TSF_TO_TU(tsf),
				(unsigned long long) tsf);
	} else {
		spin_lock(&ah->block);
		ath5k_beacon_send(ah);
		spin_unlock(&ah->block);
	}
}


/********************\
* Interrupt handling *
\********************/

static void
ath5k_intr_calibration_poll(struct ath5k_hw *ah)
{
	if (time_is_before_eq_jiffies(ah->ah_cal_next_ani) &&
	    !(ah->ah_cal_mask & AR5K_CALIBRATION_FULL)) {
		/* run ANI only when full calibration is not active */
		ah->ah_cal_next_ani = jiffies +
			msecs_to_jiffies(ATH5K_TUNE_CALIBRATION_INTERVAL_ANI);
		tasklet_schedule(&ah->ani_tasklet);

	} else if (time_is_before_eq_jiffies(ah->ah_cal_next_full)) {
		ah->ah_cal_next_full = jiffies +
			msecs_to_jiffies(ATH5K_TUNE_CALIBRATION_INTERVAL_FULL);
		tasklet_schedule(&ah->calib);
	}
	/* we could use SWI to generate enough interrupts to meet our
	 * calibration interval requirements, if necessary:
	 * AR5K_REG_ENABLE_BITS(ah, AR5K_CR, AR5K_CR_SWI); */
}

static void
ath5k_schedule_rx(struct ath5k_hw *ah)
{
	ah->rx_pending = true;
	tasklet_schedule(&ah->rxtq);
}

static void
ath5k_schedule_tx(struct ath5k_hw *ah)
{
	ah->tx_pending = true;
	tasklet_schedule(&ah->txtq);
}

static irqreturn_t
ath5k_intr(int irq, void *dev_id)
{
	struct ath5k_hw *ah = dev_id;
	enum ath5k_int status;
	unsigned int counter = 1000;

	if (unlikely(test_bit(ATH_STAT_INVALID, ah->status) ||
		((ath5k_get_bus_type(ah) != ATH_AHB) &&
				!ath5k_hw_is_intr_pending(ah))))
		return IRQ_NONE;

	do {
		ath5k_hw_get_isr(ah, &status);		/* NB: clears IRQ too */
		ATH5K_DBG(ah, ATH5K_DEBUG_INTR, "status 0x%x/0x%x\n",
				status, ah->imask);
		if (unlikely(status & AR5K_INT_FATAL)) {
			/*
			 * Fatal errors are unrecoverable.
			 * Typically these are caused by DMA errors.
			 */
			ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
				  "fatal int, resetting\n");
			ieee80211_queue_work(ah->hw, &ah->reset_work);
		} else if (unlikely(status & AR5K_INT_RXORN)) {
			/*
			 * Receive buffers are full. Either the bus is busy or
			 * the CPU is not fast enough to process all received
			 * frames.
			 * Older chipsets need a reset to come out of this
			 * condition, but we treat it as RX for newer chips.
			 * We don't know exactly which versions need a reset -
			 * this guess is copied from the HAL.
			 */
			ah->stats.rxorn_intr++;
			if (ah->ah_mac_srev < AR5K_SREV_AR5212) {
				ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
					  "rx overrun, resetting\n");
				ieee80211_queue_work(ah->hw, &ah->reset_work);
			} else
				ath5k_schedule_rx(ah);
		} else {
			if (status & AR5K_INT_SWBA)
				tasklet_hi_schedule(&ah->beacontq);

			if (status & AR5K_INT_RXEOL) {
				/*
				* NB: the hardware should re-read the link when
				*     RXE bit is written, but it doesn't work at
				*     least on older hardware revs.
				*/
				ah->stats.rxeol_intr++;
			}
			if (status & AR5K_INT_TXURN) {
				/* bump tx trigger level */
				ath5k_hw_update_tx_triglevel(ah, true);
			}
			if (status & (AR5K_INT_RXOK | AR5K_INT_RXERR))
				ath5k_schedule_rx(ah);
			if (status & (AR5K_INT_TXOK | AR5K_INT_TXDESC
					| AR5K_INT_TXERR | AR5K_INT_TXEOL))
				ath5k_schedule_tx(ah);
			if (status & AR5K_INT_BMISS) {
				/* TODO */
			}
			if (status & AR5K_INT_MIB) {
				ah->stats.mib_intr++;
				ath5k_hw_update_mib_counters(ah);
				ath5k_ani_mib_intr(ah);
			}
			if (status & AR5K_INT_GPIO)
				tasklet_schedule(&ah->rf_kill.toggleq);

		}

		if (ath5k_get_bus_type(ah) == ATH_AHB)
			break;

	} while (ath5k_hw_is_intr_pending(ah) && --counter > 0);

	if (ah->rx_pending || ah->tx_pending)
		ath5k_set_current_imask(ah);

	if (unlikely(!counter))
		ATH5K_WARN(ah, "too many interrupts, giving up for now\n");

	ath5k_intr_calibration_poll(ah);

	return IRQ_HANDLED;
}

/*
 * Periodically recalibrate the PHY to account
 * for temperature/environment changes.
 */
static void
ath5k_tasklet_calibrate(unsigned long data)
{
	struct ath5k_hw *ah = (void *)data;

	/* Only full calibration for now */
	ah->ah_cal_mask |= AR5K_CALIBRATION_FULL;

	ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE, "channel %u/%x\n",
		ieee80211_frequency_to_channel(ah->curchan->center_freq),
		ah->curchan->hw_value);

	if (ath5k_hw_gainf_calibrate(ah) == AR5K_RFGAIN_NEED_CHANGE) {
		/*
		 * Rfgain is out of bounds, reset the chip
		 * to load new gain values.
		 */
		ATH5K_DBG(ah, ATH5K_DEBUG_RESET, "calibration, resetting\n");
		ieee80211_queue_work(ah->hw, &ah->reset_work);
	}
	if (ath5k_hw_phy_calibrate(ah, ah->curchan))
		ATH5K_ERR(ah, "calibration of channel %u failed\n",
			ieee80211_frequency_to_channel(
				ah->curchan->center_freq));

	/* Noise floor calibration interrupts rx/tx path while I/Q calibration
	 * doesn't.
	 * TODO: We should stop TX here, so that it doesn't interfere.
	 * Note that stopping the queues is not enough to stop TX! */
	if (time_is_before_eq_jiffies(ah->ah_cal_next_nf)) {
		ah->ah_cal_next_nf = jiffies +
			msecs_to_jiffies(ATH5K_TUNE_CALIBRATION_INTERVAL_NF);
		ath5k_hw_update_noise_floor(ah);
	}

	ah->ah_cal_mask &= ~AR5K_CALIBRATION_FULL;
}


static void
ath5k_tasklet_ani(unsigned long data)
{
	struct ath5k_hw *ah = (void *)data;

	ah->ah_cal_mask |= AR5K_CALIBRATION_ANI;
	ath5k_ani_calibration(ah);
	ah->ah_cal_mask &= ~AR5K_CALIBRATION_ANI;
}


static void
ath5k_tx_complete_poll_work(struct work_struct *work)
{
	struct ath5k_hw *ah = container_of(work, struct ath5k_hw,
			tx_complete_work.work);
	struct ath5k_txq *txq;
	int i;
	bool needreset = false;

	mutex_lock(&ah->lock);

	for (i = 0; i < ARRAY_SIZE(ah->txqs); i++) {
		if (ah->txqs[i].setup) {
			txq = &ah->txqs[i];
			spin_lock_bh(&txq->lock);
			if (txq->txq_len > 1) {
				if (txq->txq_poll_mark) {
					ATH5K_DBG(ah, ATH5K_DEBUG_XMIT,
						  "TX queue stuck %d\n",
						  txq->qnum);
					needreset = true;
					txq->txq_stuck++;
					spin_unlock_bh(&txq->lock);
					break;
				} else {
					txq->txq_poll_mark = true;
				}
			}
			spin_unlock_bh(&txq->lock);
		}
	}

	if (needreset) {
		ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
			  "TX queues stuck, resetting\n");
		ath5k_reset(ah, NULL, true);
	}

	mutex_unlock(&ah->lock);

	ieee80211_queue_delayed_work(ah->hw, &ah->tx_complete_work,
		msecs_to_jiffies(ATH5K_TX_COMPLETE_POLL_INT));
}


/*************************\
* Initialization routines *
\*************************/

int __devinit
ath5k_init_softc(struct ath5k_hw *ah, const struct ath_bus_ops *bus_ops)
{
	struct ieee80211_hw *hw = ah->hw;
	struct ath_common *common;
	int ret;
	int csz;

	/* Initialize driver private data */
	SET_IEEE80211_DEV(hw, ah->dev);
	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
			IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
			IEEE80211_HW_SIGNAL_DBM |
			IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_MESH_POINT);

	/* both antennas can be configured as RX or TX */
	hw->wiphy->available_antennas_tx = 0x3;
	hw->wiphy->available_antennas_rx = 0x3;

	hw->extra_tx_headroom = 2;
	hw->channel_change_time = 5000;

	/*
	 * Mark the device as detached to avoid processing
	 * interrupts until setup is complete.
	 */
	__set_bit(ATH_STAT_INVALID, ah->status);

	ah->opmode = NL80211_IFTYPE_STATION;
	ah->bintval = 1000;
	mutex_init(&ah->lock);
	spin_lock_init(&ah->rxbuflock);
	spin_lock_init(&ah->txbuflock);
	spin_lock_init(&ah->block);
	spin_lock_init(&ah->irqlock);

	/* Setup interrupt handler */
	ret = request_irq(ah->irq, ath5k_intr, IRQF_SHARED, "ath", ah);
	if (ret) {
		ATH5K_ERR(ah, "request_irq failed\n");
		goto err;
	}

	common = ath5k_hw_common(ah);
	common->ops = &ath5k_common_ops;
	common->bus_ops = bus_ops;
	common->ah = ah;
	common->hw = hw;
	common->priv = ah;
	common->clockrate = 40;

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	ath5k_read_cachesize(common, &csz);
	common->cachelsz = csz << 2; /* convert to bytes */

	spin_lock_init(&common->cc_lock);

	/* Initialize device */
	ret = ath5k_hw_init(ah);
	if (ret)
		goto err_irq;

	/* set up multi-rate retry capabilities */
	if (ah->ah_version == AR5K_AR5212) {
		hw->max_rates = 4;
		hw->max_rate_tries = max(AR5K_INIT_RETRY_SHORT,
					 AR5K_INIT_RETRY_LONG);
	}

	hw->vif_data_size = sizeof(struct ath5k_vif);

	/* Finish private driver data initialization */
	ret = ath5k_init(hw);
	if (ret)
		goto err_ah;

	ATH5K_INFO(ah, "Atheros AR%s chip found (MAC: 0x%x, PHY: 0x%x)\n",
			ath5k_chip_name(AR5K_VERSION_MAC, ah->ah_mac_srev),
					ah->ah_mac_srev,
					ah->ah_phy_revision);

	if (!ah->ah_single_chip) {
		/* Single chip radio (!RF5111) */
		if (ah->ah_radio_5ghz_revision &&
			!ah->ah_radio_2ghz_revision) {
			/* No 5GHz support -> report 2GHz radio */
			if (!test_bit(AR5K_MODE_11A,
				ah->ah_capabilities.cap_mode)) {
				ATH5K_INFO(ah, "RF%s 2GHz radio found (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						ah->ah_radio_5ghz_revision),
						ah->ah_radio_5ghz_revision);
			/* No 2GHz support (5110 and some
			 * 5GHz only cards) -> report 5GHz radio */
			} else if (!test_bit(AR5K_MODE_11B,
				ah->ah_capabilities.cap_mode)) {
				ATH5K_INFO(ah, "RF%s 5GHz radio found (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						ah->ah_radio_5ghz_revision),
						ah->ah_radio_5ghz_revision);
			/* Multiband radio */
			} else {
				ATH5K_INFO(ah, "RF%s multiband radio found"
					" (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						ah->ah_radio_5ghz_revision),
						ah->ah_radio_5ghz_revision);
			}
		}
		/* Multi chip radio (RF5111 - RF2111) ->
		 * report both 2GHz/5GHz radios */
		else if (ah->ah_radio_5ghz_revision &&
				ah->ah_radio_2ghz_revision) {
			ATH5K_INFO(ah, "RF%s 5GHz radio found (0x%x)\n",
				ath5k_chip_name(AR5K_VERSION_RAD,
					ah->ah_radio_5ghz_revision),
					ah->ah_radio_5ghz_revision);
			ATH5K_INFO(ah, "RF%s 2GHz radio found (0x%x)\n",
				ath5k_chip_name(AR5K_VERSION_RAD,
					ah->ah_radio_2ghz_revision),
					ah->ah_radio_2ghz_revision);
		}
	}

	ath5k_debug_init_device(ah);

	/* ready to process interrupts */
	__clear_bit(ATH_STAT_INVALID, ah->status);

	return 0;
err_ah:
	ath5k_hw_deinit(ah);
err_irq:
	free_irq(ah->irq, ah);
err:
	return ret;
}

static int
ath5k_stop_locked(struct ath5k_hw *ah)
{

	ATH5K_DBG(ah, ATH5K_DEBUG_RESET, "invalid %u\n",
			test_bit(ATH_STAT_INVALID, ah->status));

	/*
	 * Shutdown the hardware and driver:
	 *    stop output from above
	 *    disable interrupts
	 *    turn off timers
	 *    turn off the radio
	 *    clear transmit machinery
	 *    clear receive machinery
	 *    drain and release tx queues
	 *    reclaim beacon resources
	 *    power down hardware
	 *
	 * Note that some of this work is not possible if the
	 * hardware is gone (invalid).
	 */
	ieee80211_stop_queues(ah->hw);

	if (!test_bit(ATH_STAT_INVALID, ah->status)) {
		ath5k_led_off(ah);
		ath5k_hw_set_imr(ah, 0);
		synchronize_irq(ah->irq);
		ath5k_rx_stop(ah);
		ath5k_hw_dma_stop(ah);
		ath5k_drain_tx_buffs(ah);
		ath5k_hw_phy_disable(ah);
	}

	return 0;
}

int ath5k_start(struct ieee80211_hw *hw)
{
	struct ath5k_hw *ah = hw->priv;
	struct ath_common *common = ath5k_hw_common(ah);
	int ret, i;

	mutex_lock(&ah->lock);

	ATH5K_DBG(ah, ATH5K_DEBUG_RESET, "mode %d\n", ah->opmode);

	/*
	 * Stop anything previously setup.  This is safe
	 * no matter this is the first time through or not.
	 */
	ath5k_stop_locked(ah);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	ah->curchan = ah->hw->conf.channel;
	ah->imask = AR5K_INT_RXOK | AR5K_INT_RXERR | AR5K_INT_RXEOL |
		AR5K_INT_RXORN | AR5K_INT_TXDESC | AR5K_INT_TXEOL |
		AR5K_INT_FATAL | AR5K_INT_GLOBAL | AR5K_INT_MIB;

	ret = ath5k_reset(ah, NULL, false);
	if (ret)
		goto done;

	ath5k_rfkill_hw_start(ah);

	/*
	 * Reset the key cache since some parts do not reset the
	 * contents on initial power up or resume from suspend.
	 */
	for (i = 0; i < common->keymax; i++)
		ath_hw_keyreset(common, (u16) i);

	/* Use higher rates for acks instead of base
	 * rate */
	ah->ah_ack_bitrate_high = true;

	for (i = 0; i < ARRAY_SIZE(ah->bslot); i++)
		ah->bslot[i] = NULL;

	ret = 0;
done:
	mmiowb();
	mutex_unlock(&ah->lock);

	ieee80211_queue_delayed_work(ah->hw, &ah->tx_complete_work,
			msecs_to_jiffies(ATH5K_TX_COMPLETE_POLL_INT));

	return ret;
}

static void ath5k_stop_tasklets(struct ath5k_hw *ah)
{
	ah->rx_pending = false;
	ah->tx_pending = false;
	tasklet_kill(&ah->rxtq);
	tasklet_kill(&ah->txtq);
	tasklet_kill(&ah->calib);
	tasklet_kill(&ah->beacontq);
	tasklet_kill(&ah->ani_tasklet);
}

/*
 * Stop the device, grabbing the top-level lock to protect
 * against concurrent entry through ath5k_init (which can happen
 * if another thread does a system call and the thread doing the
 * stop is preempted).
 */
void ath5k_stop(struct ieee80211_hw *hw)
{
	struct ath5k_hw *ah = hw->priv;
	int ret;

	mutex_lock(&ah->lock);
	ret = ath5k_stop_locked(ah);
	if (ret == 0 && !test_bit(ATH_STAT_INVALID, ah->status)) {
		/*
		 * Don't set the card in full sleep mode!
		 *
		 * a) When the device is in this state it must be carefully
		 * woken up or references to registers in the PCI clock
		 * domain may freeze the bus (and system).  This varies
		 * by chip and is mostly an issue with newer parts
		 * (madwifi sources mentioned srev >= 0x78) that go to
		 * sleep more quickly.
		 *
		 * b) On older chips full sleep results a weird behaviour
		 * during wakeup. I tested various cards with srev < 0x78
		 * and they don't wake up after module reload, a second
		 * module reload is needed to bring the card up again.
		 *
		 * Until we figure out what's going on don't enable
		 * full chip reset on any chip (this is what Legacy HAL
		 * and Sam's HAL do anyway). Instead Perform a full reset
		 * on the device (same as initial state after attach) and
		 * leave it idle (keep MAC/BB on warm reset) */
		ret = ath5k_hw_on_hold(ah);

		ATH5K_DBG(ah, ATH5K_DEBUG_RESET,
				"putting device to sleep\n");
	}

	mmiowb();
	mutex_unlock(&ah->lock);

	ath5k_stop_tasklets(ah);

	cancel_delayed_work_sync(&ah->tx_complete_work);

	ath5k_rfkill_hw_stop(ah);
}

/*
 * Reset the hardware.  If chan is not NULL, then also pause rx/tx
 * and change to the given channel.
 *
 * This should be called with ah->lock.
 */
static int
ath5k_reset(struct ath5k_hw *ah, struct ieee80211_channel *chan,
							bool skip_pcu)
{
	struct ath_common *common = ath5k_hw_common(ah);
	int ret, ani_mode;
	bool fast;

	ATH5K_DBG(ah, ATH5K_DEBUG_RESET, "resetting\n");

	ath5k_hw_set_imr(ah, 0);
	synchronize_irq(ah->irq);
	ath5k_stop_tasklets(ah);

	/* Save ani mode and disable ANI during
	 * reset. If we don't we might get false
	 * PHY error interrupts. */
	ani_mode = ah->ani_state.ani_mode;
	ath5k_ani_init(ah, ATH5K_ANI_MODE_OFF);

	/* We are going to empty hw queues
	 * so we should also free any remaining
	 * tx buffers */
	ath5k_drain_tx_buffs(ah);
	if (chan)
		ah->curchan = chan;

	fast = ((chan != NULL) && modparam_fastchanswitch) ? 1 : 0;

	ret = ath5k_hw_reset(ah, ah->opmode, ah->curchan, fast, skip_pcu);
	if (ret) {
		ATH5K_ERR(ah, "can't reset hardware (%d)\n", ret);
		goto err;
	}

	ret = ath5k_rx_start(ah);
	if (ret) {
		ATH5K_ERR(ah, "can't start recv logic\n");
		goto err;
	}

	ath5k_ani_init(ah, ani_mode);

	ah->ah_cal_next_full = jiffies + msecs_to_jiffies(100);
	ah->ah_cal_next_ani = jiffies;
	ah->ah_cal_next_nf = jiffies;
	ewma_init(&ah->ah_beacon_rssi_avg, 1024, 8);

	/* clear survey data and cycle counters */
	memset(&ah->survey, 0, sizeof(ah->survey));
	spin_lock_bh(&common->cc_lock);
	ath_hw_cycle_counters_update(common);
	memset(&common->cc_survey, 0, sizeof(common->cc_survey));
	memset(&common->cc_ani, 0, sizeof(common->cc_ani));
	spin_unlock_bh(&common->cc_lock);

	/*
	 * Change channels and update the h/w rate map if we're switching;
	 * e.g. 11a to 11b/g.
	 *
	 * We may be doing a reset in response to an ioctl that changes the
	 * channel so update any state that might change as a result.
	 *
	 * XXX needed?
	 */
/*	ath5k_chan_change(ah, c); */

	ath5k_beacon_config(ah);
	/* intrs are enabled by ath5k_beacon_config */

	ieee80211_wake_queues(ah->hw);

	return 0;
err:
	return ret;
}

static void ath5k_reset_work(struct work_struct *work)
{
	struct ath5k_hw *ah = container_of(work, struct ath5k_hw,
		reset_work);

	mutex_lock(&ah->lock);
	ath5k_reset(ah, NULL, true);
	mutex_unlock(&ah->lock);
}

static int __devinit
ath5k_init(struct ieee80211_hw *hw)
{

	struct ath5k_hw *ah = hw->priv;
	struct ath_regulatory *regulatory = ath5k_hw_regulatory(ah);
	struct ath5k_txq *txq;
	u8 mac[ETH_ALEN] = {};
	int ret;


	/*
	 * Check if the MAC has multi-rate retry support.
	 * We do this by trying to setup a fake extended
	 * descriptor.  MACs that don't have support will
	 * return false w/o doing anything.  MACs that do
	 * support it will return true w/o doing anything.
	 */
	ret = ath5k_hw_setup_mrr_tx_desc(ah, NULL, 0, 0, 0, 0, 0, 0);

	if (ret < 0)
		goto err;
	if (ret > 0)
		__set_bit(ATH_STAT_MRRETRY, ah->status);

	/*
	 * Collect the channel list.  The 802.11 layer
	 * is responsible for filtering this list based
	 * on settings like the phy mode and regulatory
	 * domain restrictions.
	 */
	ret = ath5k_setup_bands(hw);
	if (ret) {
		ATH5K_ERR(ah, "can't get channels\n");
		goto err;
	}

	/*
	 * Allocate tx+rx descriptors and populate the lists.
	 */
	ret = ath5k_desc_alloc(ah);
	if (ret) {
		ATH5K_ERR(ah, "can't allocate descriptors\n");
		goto err;
	}

	/*
	 * Allocate hardware transmit queues: one queue for
	 * beacon frames and one data queue for each QoS
	 * priority.  Note that hw functions handle resetting
	 * these queues at the needed time.
	 */
	ret = ath5k_beaconq_setup(ah);
	if (ret < 0) {
		ATH5K_ERR(ah, "can't setup a beacon xmit queue\n");
		goto err_desc;
	}
	ah->bhalq = ret;
	ah->cabq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_CAB, 0);
	if (IS_ERR(ah->cabq)) {
		ATH5K_ERR(ah, "can't setup cab queue\n");
		ret = PTR_ERR(ah->cabq);
		goto err_bhal;
	}

	/* 5211 and 5212 usually support 10 queues but we better rely on the
	 * capability information */
	if (ah->ah_capabilities.cap_queues.q_tx_num >= 6) {
		/* This order matches mac80211's queue priority, so we can
		* directly use the mac80211 queue number without any mapping */
		txq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_VO);
		if (IS_ERR(txq)) {
			ATH5K_ERR(ah, "can't setup xmit queue\n");
			ret = PTR_ERR(txq);
			goto err_queues;
		}
		txq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_VI);
		if (IS_ERR(txq)) {
			ATH5K_ERR(ah, "can't setup xmit queue\n");
			ret = PTR_ERR(txq);
			goto err_queues;
		}
		txq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_BE);
		if (IS_ERR(txq)) {
			ATH5K_ERR(ah, "can't setup xmit queue\n");
			ret = PTR_ERR(txq);
			goto err_queues;
		}
		txq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_BK);
		if (IS_ERR(txq)) {
			ATH5K_ERR(ah, "can't setup xmit queue\n");
			ret = PTR_ERR(txq);
			goto err_queues;
		}
		hw->queues = 4;
	} else {
		/* older hardware (5210) can only support one data queue */
		txq = ath5k_txq_setup(ah, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_BE);
		if (IS_ERR(txq)) {
			ATH5K_ERR(ah, "can't setup xmit queue\n");
			ret = PTR_ERR(txq);
			goto err_queues;
		}
		hw->queues = 1;
	}

	tasklet_init(&ah->rxtq, ath5k_tasklet_rx, (unsigned long)ah);
	tasklet_init(&ah->txtq, ath5k_tasklet_tx, (unsigned long)ah);
	tasklet_init(&ah->calib, ath5k_tasklet_calibrate, (unsigned long)ah);
	tasklet_init(&ah->beacontq, ath5k_tasklet_beacon, (unsigned long)ah);
	tasklet_init(&ah->ani_tasklet, ath5k_tasklet_ani, (unsigned long)ah);

	INIT_WORK(&ah->reset_work, ath5k_reset_work);
	INIT_DELAYED_WORK(&ah->tx_complete_work, ath5k_tx_complete_poll_work);

	ret = ath5k_hw_common(ah)->bus_ops->eeprom_read_mac(ah, mac);
	if (ret) {
		ATH5K_ERR(ah, "unable to read address from EEPROM\n");
		goto err_queues;
	}

	SET_IEEE80211_PERM_ADDR(hw, mac);
	memcpy(&ah->lladdr, mac, ETH_ALEN);
	/* All MAC address bits matter for ACKs */
	ath5k_update_bssid_mask_and_opmode(ah, NULL);

	regulatory->current_rd = ah->ah_capabilities.cap_eeprom.ee_regdomain;
	ret = ath_regd_init(regulatory, hw->wiphy, ath5k_reg_notifier);
	if (ret) {
		ATH5K_ERR(ah, "can't initialize regulatory system\n");
		goto err_queues;
	}

	ret = ieee80211_register_hw(hw);
	if (ret) {
		ATH5K_ERR(ah, "can't register ieee80211 hw\n");
		goto err_queues;
	}

	if (!ath_is_world_regd(regulatory))
		regulatory_hint(hw->wiphy, regulatory->alpha2);

	ath5k_init_leds(ah);

	ath5k_sysfs_register(ah);

	return 0;
err_queues:
	ath5k_txq_release(ah);
err_bhal:
	ath5k_hw_release_tx_queue(ah, ah->bhalq);
err_desc:
	ath5k_desc_free(ah);
err:
	return ret;
}

void
ath5k_deinit_softc(struct ath5k_hw *ah)
{
	struct ieee80211_hw *hw = ah->hw;

	/*
	 * NB: the order of these is important:
	 * o call the 802.11 layer before detaching ath5k_hw to
	 *   ensure callbacks into the driver to delete global
	 *   key cache entries can be handled
	 * o reclaim the tx queue data structures after calling
	 *   the 802.11 layer as we'll get called back to reclaim
	 *   node state and potentially want to use them
	 * o to cleanup the tx queues the hal is called, so detach
	 *   it last
	 * XXX: ??? detach ath5k_hw ???
	 * Other than that, it's straightforward...
	 */
	ieee80211_unregister_hw(hw);
	ath5k_desc_free(ah);
	ath5k_txq_release(ah);
	ath5k_hw_release_tx_queue(ah, ah->bhalq);
	ath5k_unregister_leds(ah);

	ath5k_sysfs_unregister(ah);
	/*
	 * NB: can't reclaim these until after ieee80211_ifdetach
	 * returns because we'll get called back to reclaim node
	 * state and potentially want to use them.
	 */
	ath5k_hw_deinit(ah);
	free_irq(ah->irq, ah);
}

bool
ath5k_any_vif_assoc(struct ath5k_hw *ah)
{
	struct ath5k_vif_iter_data iter_data;
	iter_data.hw_macaddr = NULL;
	iter_data.any_assoc = false;
	iter_data.need_set_hw_addr = false;
	iter_data.found_active = true;

	ieee80211_iterate_active_interfaces_atomic(ah->hw, ath5k_vif_iter,
						   &iter_data);
	return iter_data.any_assoc;
}

void
ath5k_set_beacon_filter(struct ieee80211_hw *hw, bool enable)
{
	struct ath5k_hw *ah = hw->priv;
	u32 rfilt;
	rfilt = ath5k_hw_get_rx_filter(ah);
	if (enable)
		rfilt |= AR5K_RX_FILTER_BEACON;
	else
		rfilt &= ~AR5K_RX_FILTER_BEACON;
	ath5k_hw_set_rx_filter(ah, rfilt);
	ah->filter_flags = rfilt;
}
