// SPDX-License-Identifier: GPL-2.0-or-later
/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2006-2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright (C) 2007-2008 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/jiffies.h>
#include <net/ieee80211_radiotap.h>

#include "zd_def.h"
#include "zd_chip.h"
#include "zd_mac.h"
#include "zd_rf.h"

struct zd_reg_alpha2_map {
	u32 reg;
	char alpha2[2];
};

static struct zd_reg_alpha2_map reg_alpha2_map[] = {
	{ ZD_REGDOMAIN_FCC, "US" },
	{ ZD_REGDOMAIN_IC, "CA" },
	{ ZD_REGDOMAIN_ETSI, "DE" }, /* Generic ETSI, use most restrictive */
	{ ZD_REGDOMAIN_JAPAN, "JP" },
	{ ZD_REGDOMAIN_JAPAN_2, "JP" },
	{ ZD_REGDOMAIN_JAPAN_3, "JP" },
	{ ZD_REGDOMAIN_SPAIN, "ES" },
	{ ZD_REGDOMAIN_FRANCE, "FR" },
};

/* This table contains the hardware specific values for the modulation rates. */
static const struct ieee80211_rate zd_rates[] = {
	{ .bitrate = 10,
	  .hw_value = ZD_CCK_RATE_1M, },
	{ .bitrate = 20,
	  .hw_value = ZD_CCK_RATE_2M,
	  .hw_value_short = ZD_CCK_RATE_2M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = ZD_CCK_RATE_5_5M,
	  .hw_value_short = ZD_CCK_RATE_5_5M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = ZD_CCK_RATE_11M,
	  .hw_value_short = ZD_CCK_RATE_11M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = ZD_OFDM_RATE_6M,
	  .flags = 0 },
	{ .bitrate = 90,
	  .hw_value = ZD_OFDM_RATE_9M,
	  .flags = 0 },
	{ .bitrate = 120,
	  .hw_value = ZD_OFDM_RATE_12M,
	  .flags = 0 },
	{ .bitrate = 180,
	  .hw_value = ZD_OFDM_RATE_18M,
	  .flags = 0 },
	{ .bitrate = 240,
	  .hw_value = ZD_OFDM_RATE_24M,
	  .flags = 0 },
	{ .bitrate = 360,
	  .hw_value = ZD_OFDM_RATE_36M,
	  .flags = 0 },
	{ .bitrate = 480,
	  .hw_value = ZD_OFDM_RATE_48M,
	  .flags = 0 },
	{ .bitrate = 540,
	  .hw_value = ZD_OFDM_RATE_54M,
	  .flags = 0 },
};

/*
 * Zydas retry rates table. Each line is listed in the same order as
 * in zd_rates[] and contains all the rate used when a packet is sent
 * starting with a given rates. Let's consider an example :
 *
 * "11 Mbits : 4, 3, 2, 1, 0" means :
 * - packet is sent using 4 different rates
 * - 1st rate is index 3 (ie 11 Mbits)
 * - 2nd rate is index 2 (ie 5.5 Mbits)
 * - 3rd rate is index 1 (ie 2 Mbits)
 * - 4th rate is index 0 (ie 1 Mbits)
 */

static const struct tx_retry_rate zd_retry_rates[] = {
	{ /*  1 Mbits */	1, { 0 }},
	{ /*  2 Mbits */	2, { 1,  0 }},
	{ /*  5.5 Mbits */	3, { 2,  1, 0 }},
	{ /* 11 Mbits */	4, { 3,  2, 1, 0 }},
	{ /*  6 Mbits */	5, { 4,  3, 2, 1, 0 }},
	{ /*  9 Mbits */	6, { 5,  4, 3, 2, 1, 0}},
	{ /* 12 Mbits */	5, { 6,  3, 2, 1, 0 }},
	{ /* 18 Mbits */	6, { 7,  6, 3, 2, 1, 0 }},
	{ /* 24 Mbits */	6, { 8,  6, 3, 2, 1, 0 }},
	{ /* 36 Mbits */	7, { 9,  8, 6, 3, 2, 1, 0 }},
	{ /* 48 Mbits */	8, {10,  9, 8, 6, 3, 2, 1, 0 }},
	{ /* 54 Mbits */	9, {11, 10, 9, 8, 6, 3, 2, 1, 0 }}
};

static const struct ieee80211_channel zd_channels[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 },
};

static void housekeeping_init(struct zd_mac *mac);
static void housekeeping_enable(struct zd_mac *mac);
static void housekeeping_disable(struct zd_mac *mac);
static void beacon_init(struct zd_mac *mac);
static void beacon_enable(struct zd_mac *mac);
static void beacon_disable(struct zd_mac *mac);
static void set_rts_cts(struct zd_mac *mac, unsigned int short_preamble);
static int zd_mac_config_beacon(struct ieee80211_hw *hw,
				struct sk_buff *beacon, bool in_intr);

static int zd_reg2alpha2(u8 regdomain, char *alpha2)
{
	unsigned int i;
	struct zd_reg_alpha2_map *reg_map;
	for (i = 0; i < ARRAY_SIZE(reg_alpha2_map); i++) {
		reg_map = &reg_alpha2_map[i];
		if (regdomain == reg_map->reg) {
			alpha2[0] = reg_map->alpha2[0];
			alpha2[1] = reg_map->alpha2[1];
			return 0;
		}
	}
	return 1;
}

static int zd_check_signal(struct ieee80211_hw *hw, int signal)
{
	struct zd_mac *mac = zd_hw_mac(hw);

	dev_dbg_f_cond(zd_mac_dev(mac), signal < 0 || signal > 100,
			"%s: signal value from device not in range 0..100, "
			"but %d.\n", __func__, signal);

	if (signal < 0)
		signal = 0;
	else if (signal > 100)
		signal = 100;

	return signal;
}

int zd_mac_preinit_hw(struct ieee80211_hw *hw)
{
	int r;
	u8 addr[ETH_ALEN];
	struct zd_mac *mac = zd_hw_mac(hw);

	r = zd_chip_read_mac_addr_fw(&mac->chip, addr);
	if (r)
		return r;

	SET_IEEE80211_PERM_ADDR(hw, addr);

	return 0;
}

int zd_mac_init_hw(struct ieee80211_hw *hw)
{
	int r;
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	char alpha2[2];
	u8 default_regdomain;

	r = zd_chip_enable_int(chip);
	if (r)
		goto out;
	r = zd_chip_init_hw(chip);
	if (r)
		goto disable_int;

	ZD_ASSERT(!irqs_disabled());

	r = zd_read_regdomain(chip, &default_regdomain);
	if (r)
		goto disable_int;
	spin_lock_irq(&mac->lock);
	mac->regdomain = mac->default_regdomain = default_regdomain;
	spin_unlock_irq(&mac->lock);

	/* We must inform the device that we are doing encryption/decryption in
	 * software at the moment. */
	r = zd_set_encryption_type(chip, ENC_SNIFFER);
	if (r)
		goto disable_int;

	r = zd_reg2alpha2(mac->regdomain, alpha2);
	if (r)
		goto disable_int;

	r = regulatory_hint(hw->wiphy, alpha2);
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

void zd_mac_clear(struct zd_mac *mac)
{
	flush_workqueue(zd_workqueue);
	zd_chip_clear(&mac->chip);
	ZD_MEMCLEAR(mac, sizeof(struct zd_mac));
}

static int set_rx_filter(struct zd_mac *mac)
{
	unsigned long flags;
	u32 filter = STA_RX_FILTER;

	spin_lock_irqsave(&mac->lock, flags);
	if (mac->pass_ctrl)
		filter |= RX_FILTER_CTRL;
	spin_unlock_irqrestore(&mac->lock, flags);

	return zd_iowrite32(&mac->chip, CR_RX_FILTER, filter);
}

static int set_mac_and_bssid(struct zd_mac *mac)
{
	int r;

	if (!mac->vif)
		return -1;

	r = zd_write_mac_addr(&mac->chip, mac->vif->addr);
	if (r)
		return r;

	/* Vendor driver after setting MAC either sets BSSID for AP or
	 * filter for other modes.
	 */
	if (mac->type != NL80211_IFTYPE_AP)
		return set_rx_filter(mac);
	else
		return zd_write_bssid(&mac->chip, mac->vif->addr);
}

static int set_mc_hash(struct zd_mac *mac)
{
	struct zd_mc_hash hash;
	zd_mc_clear(&hash);
	return zd_chip_set_multicast_hash(&mac->chip, &hash);
}

int zd_op_start(struct ieee80211_hw *hw)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	struct zd_usb *usb = &chip->usb;
	int r;

	if (!usb->initialized) {
		r = zd_usb_init_hw(usb);
		if (r)
			goto out;
	}

	r = zd_chip_enable_int(chip);
	if (r < 0)
		goto out;

	r = zd_chip_set_basic_rates(chip, CR_RATES_80211B | CR_RATES_80211G);
	if (r < 0)
		goto disable_int;
	r = set_rx_filter(mac);
	if (r)
		goto disable_int;
	r = set_mc_hash(mac);
	if (r)
		goto disable_int;

	/* Wait after setting the multicast hash table and powering on
	 * the radio otherwise interface bring up will fail. This matches
	 * what the vendor driver did.
	 */
	msleep(10);

	r = zd_chip_switch_radio_on(chip);
	if (r < 0) {
		dev_err(zd_chip_dev(chip),
			"%s: failed to set radio on\n", __func__);
		goto disable_int;
	}
	r = zd_chip_enable_rxtx(chip);
	if (r < 0)
		goto disable_radio;
	r = zd_chip_enable_hwint(chip);
	if (r < 0)
		goto disable_rxtx;

	housekeeping_enable(mac);
	beacon_enable(mac);
	set_bit(ZD_DEVICE_RUNNING, &mac->flags);
	return 0;
disable_rxtx:
	zd_chip_disable_rxtx(chip);
disable_radio:
	zd_chip_switch_radio_off(chip);
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

void zd_op_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	struct sk_buff *skb;
	struct sk_buff_head *ack_wait_queue = &mac->ack_wait_queue;

	clear_bit(ZD_DEVICE_RUNNING, &mac->flags);

	/* The order here deliberately is a little different from the open()
	 * method, since we need to make sure there is no opportunity for RX
	 * frames to be processed by mac80211 after we have stopped it.
	 */

	zd_chip_disable_rxtx(chip);
	beacon_disable(mac);
	housekeeping_disable(mac);
	flush_workqueue(zd_workqueue);

	zd_chip_disable_hwint(chip);
	zd_chip_switch_radio_off(chip);
	zd_chip_disable_int(chip);


	while ((skb = skb_dequeue(ack_wait_queue)))
		dev_kfree_skb_any(skb);
}

int zd_restore_settings(struct zd_mac *mac)
{
	struct sk_buff *beacon;
	struct zd_mc_hash multicast_hash;
	unsigned int short_preamble;
	int r, beacon_interval, beacon_period;
	u8 channel;

	dev_dbg_f(zd_mac_dev(mac), "\n");

	spin_lock_irq(&mac->lock);
	multicast_hash = mac->multicast_hash;
	short_preamble = mac->short_preamble;
	beacon_interval = mac->beacon.interval;
	beacon_period = mac->beacon.period;
	channel = mac->channel;
	spin_unlock_irq(&mac->lock);

	r = set_mac_and_bssid(mac);
	if (r < 0) {
		dev_dbg_f(zd_mac_dev(mac), "set_mac_and_bssid failed, %d\n", r);
		return r;
	}

	r = zd_chip_set_channel(&mac->chip, channel);
	if (r < 0) {
		dev_dbg_f(zd_mac_dev(mac), "zd_chip_set_channel failed, %d\n",
			  r);
		return r;
	}

	set_rts_cts(mac, short_preamble);

	r = zd_chip_set_multicast_hash(&mac->chip, &multicast_hash);
	if (r < 0) {
		dev_dbg_f(zd_mac_dev(mac),
			  "zd_chip_set_multicast_hash failed, %d\n", r);
		return r;
	}

	if (mac->type == NL80211_IFTYPE_MESH_POINT ||
	    mac->type == NL80211_IFTYPE_ADHOC ||
	    mac->type == NL80211_IFTYPE_AP) {
		if (mac->vif != NULL) {
			beacon = ieee80211_beacon_get(mac->hw, mac->vif, 0);
			if (beacon)
				zd_mac_config_beacon(mac->hw, beacon, false);
		}

		zd_set_beacon_interval(&mac->chip, beacon_interval,
					beacon_period, mac->type);

		spin_lock_irq(&mac->lock);
		mac->beacon.last_update = jiffies;
		spin_unlock_irq(&mac->lock);
	}

	return 0;
}

/**
 * zd_mac_tx_status - reports tx status of a packet if required
 * @hw: a &struct ieee80211_hw pointer
 * @skb: a sk-buffer
 * @ackssi: ACK signal strength
 * @tx_status: success and/or retry
 *
 * This information calls ieee80211_tx_status_irqsafe() if required by the
 * control information. It copies the control information into the status
 * information.
 *
 * If no status information has been requested, the skb is freed.
 */
static void zd_mac_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb,
		      int ackssi, struct tx_status *tx_status)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int i;
	int success = 1, retry = 1;
	int first_idx;
	const struct tx_retry_rate *retries;

	ieee80211_tx_info_clear_status(info);

	if (tx_status) {
		success = !tx_status->failure;
		retry = tx_status->retry + success;
	}

	if (success) {
		/* success */
		info->flags |= IEEE80211_TX_STAT_ACK;
	} else {
		/* failure */
		info->flags &= ~IEEE80211_TX_STAT_ACK;
	}

	first_idx = info->status.rates[0].idx;
	ZD_ASSERT(0<=first_idx && first_idx<ARRAY_SIZE(zd_retry_rates));
	retries = &zd_retry_rates[first_idx];
	ZD_ASSERT(1 <= retry && retry <= retries->count);

	info->status.rates[0].idx = retries->rate[0];
	info->status.rates[0].count = 1; // (retry > 1 ? 2 : 1);

	for (i=1; i<IEEE80211_TX_MAX_RATES-1 && i<retry; i++) {
		info->status.rates[i].idx = retries->rate[i];
		info->status.rates[i].count = 1; // ((i==retry-1) && success ? 1:2);
	}
	for (; i<IEEE80211_TX_MAX_RATES && i<retry; i++) {
		info->status.rates[i].idx = retries->rate[retry - 1];
		info->status.rates[i].count = 1; // (success ? 1:2);
	}
	if (i<IEEE80211_TX_MAX_RATES)
		info->status.rates[i].idx = -1; /* terminate */

	info->status.ack_signal = zd_check_signal(hw, ackssi);
	ieee80211_tx_status_irqsafe(hw, skb);
}

/**
 * zd_mac_tx_failed - callback for failed frames
 * @urb: pointer to the urb structure
 *
 * This function is called if a frame couldn't be successfully
 * transferred. The first frame from the tx queue, will be selected and
 * reported as error to the upper layers.
 */
void zd_mac_tx_failed(struct urb *urb)
{
	struct ieee80211_hw * hw = zd_usb_to_hw(urb->context);
	struct zd_mac *mac = zd_hw_mac(hw);
	struct sk_buff_head *q = &mac->ack_wait_queue;
	struct sk_buff *skb;
	struct tx_status *tx_status = (struct tx_status *)urb->transfer_buffer;
	unsigned long flags;
	int success = !tx_status->failure;
	int retry = tx_status->retry + success;
	int found = 0;
	int i, position = 0;

	spin_lock_irqsave(&q->lock, flags);

	skb_queue_walk(q, skb) {
		struct ieee80211_hdr *tx_hdr;
		struct ieee80211_tx_info *info;
		int first_idx, final_idx;
		const struct tx_retry_rate *retries;
		u8 final_rate;

		position ++;

		/* if the hardware reports a failure and we had a 802.11 ACK
		 * pending, then we skip the first skb when searching for a
		 * matching frame */
		if (tx_status->failure && mac->ack_pending &&
		    skb_queue_is_first(q, skb)) {
			continue;
		}

		tx_hdr = (struct ieee80211_hdr *)skb->data;

		/* we skip all frames not matching the reported destination */
		if (unlikely(!ether_addr_equal(tx_hdr->addr1, tx_status->mac)))
			continue;

		/* we skip all frames not matching the reported final rate */

		info = IEEE80211_SKB_CB(skb);
		first_idx = info->status.rates[0].idx;
		ZD_ASSERT(0<=first_idx && first_idx<ARRAY_SIZE(zd_retry_rates));
		retries = &zd_retry_rates[first_idx];
		if (retry <= 0 || retry > retries->count)
			continue;

		final_idx = retries->rate[retry - 1];
		final_rate = zd_rates[final_idx].hw_value;

		if (final_rate != tx_status->rate) {
			continue;
		}

		found = 1;
		break;
	}

	if (found) {
		for (i=1; i<=position; i++) {
			skb = __skb_dequeue(q);
			zd_mac_tx_status(hw, skb,
					 mac->ack_pending ? mac->ack_signal : 0,
					 i == position ? tx_status : NULL);
			mac->ack_pending = 0;
		}
	}

	spin_unlock_irqrestore(&q->lock, flags);
}

/**
 * zd_mac_tx_to_dev - callback for USB layer
 * @skb: a &sk_buff pointer
 * @error: error value, 0 if transmission successful
 *
 * Informs the MAC layer that the frame has successfully transferred to the
 * device. If an ACK is required and the transfer to the device has been
 * successful, the packets are put on the @ack_wait_queue with
 * the control set removed.
 */
void zd_mac_tx_to_dev(struct sk_buff *skb, int error)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = info->rate_driver_data[0];
	struct zd_mac *mac = zd_hw_mac(hw);

	ieee80211_tx_info_clear_status(info);

	skb_pull(skb, sizeof(struct zd_ctrlset));
	if (unlikely(error ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))) {
		/*
		 * FIXME : do we need to fill in anything ?
		 */
		ieee80211_tx_status_irqsafe(hw, skb);
	} else {
		struct sk_buff_head *q = &mac->ack_wait_queue;

		skb_queue_tail(q, skb);
		while (skb_queue_len(q) > ZD_MAC_MAX_ACK_WAITERS) {
			zd_mac_tx_status(hw, skb_dequeue(q),
					 mac->ack_pending ? mac->ack_signal : 0,
					 NULL);
			mac->ack_pending = 0;
		}
	}
}

static int zd_calc_tx_length_us(u8 *service, u8 zd_rate, u16 tx_length)
{
	/* ZD_PURE_RATE() must be used to remove the modulation type flag of
	 * the zd-rate values.
	 */
	static const u8 rate_divisor[] = {
		[ZD_PURE_RATE(ZD_CCK_RATE_1M)]   =  1,
		[ZD_PURE_RATE(ZD_CCK_RATE_2M)]	 =  2,
		/* Bits must be doubled. */
		[ZD_PURE_RATE(ZD_CCK_RATE_5_5M)] = 11,
		[ZD_PURE_RATE(ZD_CCK_RATE_11M)]	 = 11,
		[ZD_PURE_RATE(ZD_OFDM_RATE_6M)]  =  6,
		[ZD_PURE_RATE(ZD_OFDM_RATE_9M)]  =  9,
		[ZD_PURE_RATE(ZD_OFDM_RATE_12M)] = 12,
		[ZD_PURE_RATE(ZD_OFDM_RATE_18M)] = 18,
		[ZD_PURE_RATE(ZD_OFDM_RATE_24M)] = 24,
		[ZD_PURE_RATE(ZD_OFDM_RATE_36M)] = 36,
		[ZD_PURE_RATE(ZD_OFDM_RATE_48M)] = 48,
		[ZD_PURE_RATE(ZD_OFDM_RATE_54M)] = 54,
	};

	u32 bits = (u32)tx_length * 8;
	u32 divisor;

	divisor = rate_divisor[ZD_PURE_RATE(zd_rate)];
	if (divisor == 0)
		return -EINVAL;

	switch (zd_rate) {
	case ZD_CCK_RATE_5_5M:
		bits = (2*bits) + 10; /* round up to the next integer */
		break;
	case ZD_CCK_RATE_11M:
		if (service) {
			u32 t = bits % 11;
			*service &= ~ZD_PLCP_SERVICE_LENGTH_EXTENSION;
			if (0 < t && t <= 3) {
				*service |= ZD_PLCP_SERVICE_LENGTH_EXTENSION;
			}
		}
		bits += 10; /* round up to the next integer */
		break;
	}

	return bits/divisor;
}

static void cs_set_control(struct zd_mac *mac, struct zd_ctrlset *cs,
	                   struct ieee80211_hdr *header,
	                   struct ieee80211_tx_info *info)
{
	/*
	 * CONTROL TODO:
	 * - if backoff needed, enable bit 0
	 * - if burst (backoff not needed) disable bit 0
	 */

	cs->control = 0;

	/* First fragment */
	if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
		cs->control |= ZD_CS_NEED_RANDOM_BACKOFF;

	/* No ACK expected (multicast, etc.) */
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		cs->control |= ZD_CS_NO_ACK;

	/* PS-POLL */
	if (ieee80211_is_pspoll(header->frame_control))
		cs->control |= ZD_CS_PS_POLL_FRAME;

	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
		cs->control |= ZD_CS_RTS;

	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		cs->control |= ZD_CS_SELF_CTS;

	/* FIXME: Management frame? */
}

static bool zd_mac_match_cur_beacon(struct zd_mac *mac, struct sk_buff *beacon)
{
	if (!mac->beacon.cur_beacon)
		return false;

	if (mac->beacon.cur_beacon->len != beacon->len)
		return false;

	return !memcmp(beacon->data, mac->beacon.cur_beacon->data, beacon->len);
}

static void zd_mac_free_cur_beacon_locked(struct zd_mac *mac)
{
	ZD_ASSERT(mutex_is_locked(&mac->chip.mutex));

	kfree_skb(mac->beacon.cur_beacon);
	mac->beacon.cur_beacon = NULL;
}

static void zd_mac_free_cur_beacon(struct zd_mac *mac)
{
	mutex_lock(&mac->chip.mutex);
	zd_mac_free_cur_beacon_locked(mac);
	mutex_unlock(&mac->chip.mutex);
}

static int zd_mac_config_beacon(struct ieee80211_hw *hw, struct sk_buff *beacon,
				bool in_intr)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	int r, ret, num_cmds, req_pos = 0;
	u32 tmp, j = 0;
	/* 4 more bytes for tail CRC */
	u32 full_len = beacon->len + 4;
	unsigned long end_jiffies, message_jiffies;
	struct zd_ioreq32 *ioreqs;

	mutex_lock(&mac->chip.mutex);

	/* Check if hw already has this beacon. */
	if (zd_mac_match_cur_beacon(mac, beacon)) {
		r = 0;
		goto out_nofree;
	}

	/* Alloc memory for full beacon write at once. */
	num_cmds = 1 + zd_chip_is_zd1211b(&mac->chip) + full_len;
	ioreqs = kmalloc_array(num_cmds, sizeof(struct zd_ioreq32),
			       GFP_KERNEL);
	if (!ioreqs) {
		r = -ENOMEM;
		goto out_nofree;
	}

	r = zd_iowrite32_locked(&mac->chip, 0, CR_BCN_FIFO_SEMAPHORE);
	if (r < 0)
		goto out;
	r = zd_ioread32_locked(&mac->chip, &tmp, CR_BCN_FIFO_SEMAPHORE);
	if (r < 0)
		goto release_sema;
	if (in_intr && tmp & 0x2) {
		r = -EBUSY;
		goto release_sema;
	}

	end_jiffies = jiffies + HZ / 2; /*~500ms*/
	message_jiffies = jiffies + HZ / 10; /*~100ms*/
	while (tmp & 0x2) {
		r = zd_ioread32_locked(&mac->chip, &tmp, CR_BCN_FIFO_SEMAPHORE);
		if (r < 0)
			goto release_sema;
		if (time_is_before_eq_jiffies(message_jiffies)) {
			message_jiffies = jiffies + HZ / 10;
			dev_err(zd_mac_dev(mac),
					"CR_BCN_FIFO_SEMAPHORE not ready\n");
			if (time_is_before_eq_jiffies(end_jiffies))  {
				dev_err(zd_mac_dev(mac),
						"Giving up beacon config.\n");
				r = -ETIMEDOUT;
				goto reset_device;
			}
		}
		msleep(20);
	}

	ioreqs[req_pos].addr = CR_BCN_FIFO;
	ioreqs[req_pos].value = full_len - 1;
	req_pos++;
	if (zd_chip_is_zd1211b(&mac->chip)) {
		ioreqs[req_pos].addr = CR_BCN_LENGTH;
		ioreqs[req_pos].value = full_len - 1;
		req_pos++;
	}

	for (j = 0 ; j < beacon->len; j++) {
		ioreqs[req_pos].addr = CR_BCN_FIFO;
		ioreqs[req_pos].value = *((u8 *)(beacon->data + j));
		req_pos++;
	}

	for (j = 0; j < 4; j++) {
		ioreqs[req_pos].addr = CR_BCN_FIFO;
		ioreqs[req_pos].value = 0x0;
		req_pos++;
	}

	BUG_ON(req_pos != num_cmds);

	r = zd_iowrite32a_locked(&mac->chip, ioreqs, num_cmds);

release_sema:
	/*
	 * Try very hard to release device beacon semaphore, as otherwise
	 * device/driver can be left in unusable state.
	 */
	end_jiffies = jiffies + HZ / 2; /*~500ms*/
	ret = zd_iowrite32_locked(&mac->chip, 1, CR_BCN_FIFO_SEMAPHORE);
	while (ret < 0) {
		if (in_intr || time_is_before_eq_jiffies(end_jiffies)) {
			ret = -ETIMEDOUT;
			break;
		}

		msleep(20);
		ret = zd_iowrite32_locked(&mac->chip, 1, CR_BCN_FIFO_SEMAPHORE);
	}

	if (ret < 0)
		dev_err(zd_mac_dev(mac), "Could not release "
					 "CR_BCN_FIFO_SEMAPHORE!\n");
	if (r < 0 || ret < 0) {
		if (r >= 0)
			r = ret;

		/* We don't know if beacon was written successfully or not,
		 * so clear current. */
		zd_mac_free_cur_beacon_locked(mac);

		goto out;
	}

	/* Beacon has now been written successfully, update current. */
	zd_mac_free_cur_beacon_locked(mac);
	mac->beacon.cur_beacon = beacon;
	beacon = NULL;

	/* 802.11b/g 2.4G CCK 1Mb
	 * 802.11a, not yet implemented, uses different values (see GPL vendor
	 * driver)
	 */
	r = zd_iowrite32_locked(&mac->chip, 0x00000400 | (full_len << 19),
				CR_BCN_PLCP_CFG);
out:
	kfree(ioreqs);
out_nofree:
	kfree_skb(beacon);
	mutex_unlock(&mac->chip.mutex);

	return r;

reset_device:
	zd_mac_free_cur_beacon_locked(mac);
	kfree_skb(beacon);

	mutex_unlock(&mac->chip.mutex);
	kfree(ioreqs);

	/* semaphore stuck, reset device to avoid fw freeze later */
	dev_warn(zd_mac_dev(mac), "CR_BCN_FIFO_SEMAPHORE stuck, "
				  "resetting device...");
	usb_queue_reset_device(mac->chip.usb.intf);

	return r;
}

static int fill_ctrlset(struct zd_mac *mac,
			struct sk_buff *skb)
{
	int r;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	unsigned int frag_len = skb->len + FCS_LEN;
	unsigned int packet_length;
	struct ieee80211_rate *txrate;
	struct zd_ctrlset *cs = skb_push(skb, sizeof(struct zd_ctrlset));
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	ZD_ASSERT(frag_len <= 0xffff);

	/*
	 * Firmware computes the duration itself (for all frames except PSPoll)
	 * and needs the field set to 0 at input, otherwise firmware messes up
	 * duration_id and sets bits 14 and 15 on.
	 */
	if (!ieee80211_is_pspoll(hdr->frame_control))
		hdr->duration_id = 0;

	txrate = ieee80211_get_tx_rate(mac->hw, info);

	cs->modulation = txrate->hw_value;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		cs->modulation = txrate->hw_value_short;

	cs->tx_length = cpu_to_le16(frag_len);

	cs_set_control(mac, cs, hdr, info);

	packet_length = frag_len + sizeof(struct zd_ctrlset) + 10;
	ZD_ASSERT(packet_length <= 0xffff);
	/* ZD1211B: Computing the length difference this way, gives us
	 * flexibility to compute the packet length.
	 */
	cs->packet_length = cpu_to_le16(zd_chip_is_zd1211b(&mac->chip) ?
			packet_length - frag_len : packet_length);

	/*
	 * CURRENT LENGTH:
	 * - transmit frame length in microseconds
	 * - seems to be derived from frame length
	 * - see Cal_Us_Service() in zdinlinef.h
	 * - if macp->bTxBurstEnable is enabled, then multiply by 4
	 *  - bTxBurstEnable is never set in the vendor driver
	 *
	 * SERVICE:
	 * - "for PLCP configuration"
	 * - always 0 except in some situations at 802.11b 11M
	 * - see line 53 of zdinlinef.h
	 */
	cs->service = 0;
	r = zd_calc_tx_length_us(&cs->service, ZD_RATE(cs->modulation),
		                 le16_to_cpu(cs->tx_length));
	if (r < 0)
		return r;
	cs->current_length = cpu_to_le16(r);
	cs->next_frame_length = 0;

	return 0;
}

/**
 * zd_op_tx - transmits a network frame to the device
 *
 * @hw: a &struct ieee80211_hw pointer
 * @control: the control structure
 * @skb: socket buffer
 *
 * This function transmit an IEEE 802.11 network frame to the device. The
 * control block of the skbuff will be initialized. If necessary the incoming
 * mac80211 queues will be stopped.
 */
static void zd_op_tx(struct ieee80211_hw *hw,
		     struct ieee80211_tx_control *control,
		     struct sk_buff *skb)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int r;

	r = fill_ctrlset(mac, skb);
	if (r)
		goto fail;

	info->rate_driver_data[0] = hw;

	r = zd_usb_tx(&mac->chip.usb, skb);
	if (r)
		goto fail;
	return;

fail:
	dev_kfree_skb(skb);
}

/**
 * filter_ack - filters incoming packets for acknowledgements
 * @hw: a &struct ieee80211_hw pointer
 * @rx_hdr: received header
 * @stats: the status for the received packet
 *
 * This functions looks for ACK packets and tries to match them with the
 * frames in the tx queue. If a match is found the frame will be dequeued and
 * the upper layers is informed about the successful transmission. If
 * mac80211 queues have been stopped and the number of frames still to be
 * transmitted is low the queues will be opened again.
 *
 * Returns 1 if the frame was an ACK, 0 if it was ignored.
 */
static int filter_ack(struct ieee80211_hw *hw, struct ieee80211_hdr *rx_hdr,
		      struct ieee80211_rx_status *stats)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct sk_buff *skb;
	struct sk_buff_head *q;
	unsigned long flags;
	int found = 0;
	int i, position = 0;

	if (!ieee80211_is_ack(rx_hdr->frame_control))
		return 0;

	q = &mac->ack_wait_queue;
	spin_lock_irqsave(&q->lock, flags);
	skb_queue_walk(q, skb) {
		struct ieee80211_hdr *tx_hdr;

		position ++;

		if (mac->ack_pending && skb_queue_is_first(q, skb))
		    continue;

		tx_hdr = (struct ieee80211_hdr *)skb->data;
		if (likely(ether_addr_equal(tx_hdr->addr2, rx_hdr->addr1)))
		{
			found = 1;
			break;
		}
	}

	if (found) {
		for (i=1; i<position; i++) {
			skb = __skb_dequeue(q);
			zd_mac_tx_status(hw, skb,
					 mac->ack_pending ? mac->ack_signal : 0,
					 NULL);
			mac->ack_pending = 0;
		}

		mac->ack_pending = 1;
		mac->ack_signal = stats->signal;

		/* Prevent pending tx-packet on AP-mode */
		if (mac->type == NL80211_IFTYPE_AP) {
			skb = __skb_dequeue(q);
			zd_mac_tx_status(hw, skb, mac->ack_signal, NULL);
			mac->ack_pending = 0;
		}
	}

	spin_unlock_irqrestore(&q->lock, flags);
	return 1;
}

int zd_mac_rx(struct ieee80211_hw *hw, const u8 *buffer, unsigned int length)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct ieee80211_rx_status stats;
	const struct rx_status *status;
	struct sk_buff *skb;
	int bad_frame = 0;
	__le16 fc;
	int need_padding;
	int i;
	u8 rate;

	if (length < ZD_PLCP_HEADER_SIZE + 10 /* IEEE80211_1ADDR_LEN */ +
	             FCS_LEN + sizeof(struct rx_status))
		return -EINVAL;

	memset(&stats, 0, sizeof(stats));

	/* Note about pass_failed_fcs and pass_ctrl access below:
	 * mac locking intentionally omitted here, as this is the only unlocked
	 * reader and the only writer is configure_filter. Plus, if there were
	 * any races accessing these variables, it wouldn't really matter.
	 * If mac80211 ever provides a way for us to access filter flags
	 * from outside configure_filter, we could improve on this. Also, this
	 * situation may change once we implement some kind of DMA-into-skb
	 * RX path. */

	/* Caller has to ensure that length >= sizeof(struct rx_status). */
	status = (struct rx_status *)
		(buffer + (length - sizeof(struct rx_status)));
	if (status->frame_status & ZD_RX_ERROR) {
		if (mac->pass_failed_fcs &&
				(status->frame_status & ZD_RX_CRC32_ERROR)) {
			stats.flag |= RX_FLAG_FAILED_FCS_CRC;
			bad_frame = 1;
		} else {
			return -EINVAL;
		}
	}

	stats.freq = zd_channels[_zd_chip_get_channel(&mac->chip) - 1].center_freq;
	stats.band = NL80211_BAND_2GHZ;
	stats.signal = zd_check_signal(hw, status->signal_strength);

	rate = zd_rx_rate(buffer, status);

	/* todo: return index in the big switches in zd_rx_rate instead */
	for (i = 0; i < mac->band.n_bitrates; i++)
		if (rate == mac->band.bitrates[i].hw_value)
			stats.rate_idx = i;

	length -= ZD_PLCP_HEADER_SIZE + sizeof(struct rx_status);
	buffer += ZD_PLCP_HEADER_SIZE;

	/* Except for bad frames, filter each frame to see if it is an ACK, in
	 * which case our internal TX tracking is updated. Normally we then
	 * bail here as there's no need to pass ACKs on up to the stack, but
	 * there is also the case where the stack has requested us to pass
	 * control frames on up (pass_ctrl) which we must consider. */
	if (!bad_frame &&
			filter_ack(hw, (struct ieee80211_hdr *)buffer, &stats)
			&& !mac->pass_ctrl)
		return 0;

	fc = get_unaligned((__le16*)buffer);
	need_padding = ieee80211_is_data_qos(fc) ^ ieee80211_has_a4(fc);

	skb = dev_alloc_skb(length + (need_padding ? 2 : 0));
	if (skb == NULL)
		return -ENOMEM;
	if (need_padding) {
		/* Make sure the payload data is 4 byte aligned. */
		skb_reserve(skb, 2);
	}

	/* FIXME : could we avoid this big memcpy ? */
	skb_put_data(skb, buffer, length);

	memcpy(IEEE80211_SKB_RXCB(skb), &stats, sizeof(stats));
	ieee80211_rx_irqsafe(hw, skb);
	return 0;
}

static int zd_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct zd_mac *mac = zd_hw_mac(hw);

	/* using NL80211_IFTYPE_UNSPECIFIED to indicate no mode selected */
	if (mac->type != NL80211_IFTYPE_UNSPECIFIED)
		return -EOPNOTSUPP;

	switch (vif->type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		mac->type = vif->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mac->vif = vif;

	return set_mac_and_bssid(mac);
}

static void zd_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	mac->type = NL80211_IFTYPE_UNSPECIFIED;
	mac->vif = NULL;
	zd_set_beacon_interval(&mac->chip, 0, 0, NL80211_IFTYPE_UNSPECIFIED);
	zd_write_mac_addr(&mac->chip, NULL);

	zd_mac_free_cur_beacon(mac);
}

static int zd_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct ieee80211_conf *conf = &hw->conf;

	spin_lock_irq(&mac->lock);
	mac->channel = conf->chandef.chan->hw_value;
	spin_unlock_irq(&mac->lock);

	return zd_chip_set_channel(&mac->chip, conf->chandef.chan->hw_value);
}

static void zd_beacon_done(struct zd_mac *mac)
{
	struct sk_buff *skb, *beacon;

	if (!test_bit(ZD_DEVICE_RUNNING, &mac->flags))
		return;
	if (!mac->vif || mac->vif->type != NL80211_IFTYPE_AP)
		return;

	/*
	 * Send out buffered broad- and multicast frames.
	 */
	while (!ieee80211_queue_stopped(mac->hw, 0)) {
		skb = ieee80211_get_buffered_bc(mac->hw, mac->vif);
		if (!skb)
			break;
		zd_op_tx(mac->hw, NULL, skb);
	}

	/*
	 * Fetch next beacon so that tim_count is updated.
	 */
	beacon = ieee80211_beacon_get(mac->hw, mac->vif, 0);
	if (beacon)
		zd_mac_config_beacon(mac->hw, beacon, true);

	spin_lock_irq(&mac->lock);
	mac->beacon.last_update = jiffies;
	spin_unlock_irq(&mac->lock);
}

static void zd_process_intr(struct work_struct *work)
{
	u16 int_status;
	unsigned long flags;
	struct zd_mac *mac = container_of(work, struct zd_mac, process_intr);

	spin_lock_irqsave(&mac->lock, flags);
	int_status = le16_to_cpu(*(__le16 *)(mac->intr_buffer + 4));
	spin_unlock_irqrestore(&mac->lock, flags);

	if (int_status & INT_CFG_NEXT_BCN) {
		/*dev_dbg_f_limit(zd_mac_dev(mac), "INT_CFG_NEXT_BCN\n");*/
		zd_beacon_done(mac);
	} else {
		dev_dbg_f(zd_mac_dev(mac), "Unsupported interrupt\n");
	}

	zd_chip_enable_hwint(&mac->chip);
}


static u64 zd_op_prepare_multicast(struct ieee80211_hw *hw,
				   struct netdev_hw_addr_list *mc_list)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_mc_hash hash;
	struct netdev_hw_addr *ha;

	zd_mc_clear(&hash);

	netdev_hw_addr_list_for_each(ha, mc_list) {
		dev_dbg_f(zd_mac_dev(mac), "mc addr %pM\n", ha->addr);
		zd_mc_add_addr(&hash, ha->addr);
	}

	return hash.low | ((u64)hash.high << 32);
}

#define SUPPORTED_FIF_FLAGS \
	(FIF_ALLMULTI | FIF_FCSFAIL | FIF_CONTROL | \
	FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC)
static void zd_op_configure_filter(struct ieee80211_hw *hw,
			unsigned int changed_flags,
			unsigned int *new_flags,
			u64 multicast)
{
	struct zd_mc_hash hash = {
		.low = multicast,
		.high = multicast >> 32,
	};
	struct zd_mac *mac = zd_hw_mac(hw);
	unsigned long flags;
	int r;

	/* Only deal with supported flags */
	changed_flags &= SUPPORTED_FIF_FLAGS;
	*new_flags &= SUPPORTED_FIF_FLAGS;

	/*
	 * If multicast parameter (as returned by zd_op_prepare_multicast)
	 * has changed, no bit in changed_flags is set. To handle this
	 * situation, we do not return if changed_flags is 0. If we do so,
	 * we will have some issue with IPv6 which uses multicast for link
	 * layer address resolution.
	 */
	if (*new_flags & FIF_ALLMULTI)
		zd_mc_add_all(&hash);

	spin_lock_irqsave(&mac->lock, flags);
	mac->pass_failed_fcs = !!(*new_flags & FIF_FCSFAIL);
	mac->pass_ctrl = !!(*new_flags & FIF_CONTROL);
	mac->multicast_hash = hash;
	spin_unlock_irqrestore(&mac->lock, flags);

	zd_chip_set_multicast_hash(&mac->chip, &hash);

	if (changed_flags & FIF_CONTROL) {
		r = set_rx_filter(mac);
		if (r)
			dev_err(zd_mac_dev(mac), "set_rx_filter error %d\n", r);
	}

	/* no handling required for FIF_OTHER_BSS as we don't currently
	 * do BSSID filtering */
	/* FIXME: in future it would be nice to enable the probe response
	 * filter (so that the driver doesn't see them) until
	 * FIF_BCN_PRBRESP_PROMISC is set. however due to atomicity here, we'd
	 * have to schedule work to enable prbresp reception, which might
	 * happen too late. For now we'll just listen and forward them all the
	 * time. */
}

static void set_rts_cts(struct zd_mac *mac, unsigned int short_preamble)
{
	mutex_lock(&mac->chip.mutex);
	zd_chip_set_rts_cts_rate_locked(&mac->chip, short_preamble);
	mutex_unlock(&mac->chip.mutex);
}

static void zd_op_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf,
				   u64 changes)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	int associated;

	dev_dbg_f(zd_mac_dev(mac), "changes: %llx\n", changes);

	if (mac->type == NL80211_IFTYPE_MESH_POINT ||
	    mac->type == NL80211_IFTYPE_ADHOC ||
	    mac->type == NL80211_IFTYPE_AP) {
		associated = true;
		if (changes & BSS_CHANGED_BEACON) {
			struct sk_buff *beacon = ieee80211_beacon_get(hw, vif,
								      0);

			if (beacon) {
				zd_chip_disable_hwint(&mac->chip);
				zd_mac_config_beacon(hw, beacon, false);
				zd_chip_enable_hwint(&mac->chip);
			}
		}

		if (changes & BSS_CHANGED_BEACON_ENABLED) {
			u16 interval = 0;
			u8 period = 0;

			if (bss_conf->enable_beacon) {
				period = bss_conf->dtim_period;
				interval = bss_conf->beacon_int;
			}

			spin_lock_irq(&mac->lock);
			mac->beacon.period = period;
			mac->beacon.interval = interval;
			mac->beacon.last_update = jiffies;
			spin_unlock_irq(&mac->lock);

			zd_set_beacon_interval(&mac->chip, interval, period,
					       mac->type);
		}
	} else
		associated = is_valid_ether_addr(bss_conf->bssid);

	spin_lock_irq(&mac->lock);
	mac->associated = associated;
	spin_unlock_irq(&mac->lock);

	/* TODO: do hardware bssid filtering */

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		spin_lock_irq(&mac->lock);
		mac->short_preamble = bss_conf->use_short_preamble;
		spin_unlock_irq(&mac->lock);

		set_rts_cts(mac, bss_conf->use_short_preamble);
	}
}

static u64 zd_op_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	return zd_chip_get_tsf(&mac->chip);
}

static const struct ieee80211_ops zd_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx			= zd_op_tx,
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
	.start			= zd_op_start,
	.stop			= zd_op_stop,
	.add_interface		= zd_op_add_interface,
	.remove_interface	= zd_op_remove_interface,
	.config			= zd_op_config,
	.prepare_multicast	= zd_op_prepare_multicast,
	.configure_filter	= zd_op_configure_filter,
	.bss_info_changed	= zd_op_bss_info_changed,
	.get_tsf		= zd_op_get_tsf,
};

struct ieee80211_hw *zd_mac_alloc_hw(struct usb_interface *intf)
{
	struct zd_mac *mac;
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct zd_mac), &zd_ops);
	if (!hw) {
		dev_dbg_f(&intf->dev, "out of memory\n");
		return NULL;
	}

	mac = zd_hw_mac(hw);

	memset(mac, 0, sizeof(*mac));
	spin_lock_init(&mac->lock);
	mac->hw = hw;

	mac->type = NL80211_IFTYPE_UNSPECIFIED;

	memcpy(mac->channels, zd_channels, sizeof(zd_channels));
	memcpy(mac->rates, zd_rates, sizeof(zd_rates));
	mac->band.n_bitrates = ARRAY_SIZE(zd_rates);
	mac->band.bitrates = mac->rates;
	mac->band.n_channels = ARRAY_SIZE(zd_channels);
	mac->band.channels = mac->channels;

	hw->wiphy->bands[NL80211_BAND_2GHZ] = &mac->band;

	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, SIGNAL_UNSPEC);

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_MESH_POINT) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_AP);

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	hw->max_signal = 100;
	hw->queues = 1;
	hw->extra_tx_headroom = sizeof(struct zd_ctrlset);

	/*
	 * Tell mac80211 that we support multi rate retries
	 */
	hw->max_rates = IEEE80211_TX_MAX_RATES;
	hw->max_rate_tries = 18;	/* 9 rates * 2 retries/rate */

	skb_queue_head_init(&mac->ack_wait_queue);
	mac->ack_pending = 0;

	zd_chip_init(&mac->chip, hw, intf);
	housekeeping_init(mac);
	beacon_init(mac);
	INIT_WORK(&mac->process_intr, zd_process_intr);

	SET_IEEE80211_DEV(hw, &intf->dev);
	return hw;
}

#define BEACON_WATCHDOG_DELAY round_jiffies_relative(HZ)

static void beacon_watchdog_handler(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, beacon.watchdog_work.work);
	struct sk_buff *beacon;
	unsigned long timeout;
	int interval, period;

	if (!test_bit(ZD_DEVICE_RUNNING, &mac->flags))
		goto rearm;
	if (mac->type != NL80211_IFTYPE_AP || !mac->vif)
		goto rearm;

	spin_lock_irq(&mac->lock);
	interval = mac->beacon.interval;
	period = mac->beacon.period;
	timeout = mac->beacon.last_update +
			msecs_to_jiffies(interval * 1024 / 1000) * 3;
	spin_unlock_irq(&mac->lock);

	if (interval > 0 && time_is_before_jiffies(timeout)) {
		dev_dbg_f(zd_mac_dev(mac), "beacon interrupt stalled, "
					   "restarting. "
					   "(interval: %d, dtim: %d)\n",
					   interval, period);

		zd_chip_disable_hwint(&mac->chip);

		beacon = ieee80211_beacon_get(mac->hw, mac->vif, 0);
		if (beacon) {
			zd_mac_free_cur_beacon(mac);

			zd_mac_config_beacon(mac->hw, beacon, false);
		}

		zd_set_beacon_interval(&mac->chip, interval, period, mac->type);

		zd_chip_enable_hwint(&mac->chip);

		spin_lock_irq(&mac->lock);
		mac->beacon.last_update = jiffies;
		spin_unlock_irq(&mac->lock);
	}

rearm:
	queue_delayed_work(zd_workqueue, &mac->beacon.watchdog_work,
			   BEACON_WATCHDOG_DELAY);
}

static void beacon_init(struct zd_mac *mac)
{
	INIT_DELAYED_WORK(&mac->beacon.watchdog_work, beacon_watchdog_handler);
}

static void beacon_enable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");

	mac->beacon.last_update = jiffies;
	queue_delayed_work(zd_workqueue, &mac->beacon.watchdog_work,
			   BEACON_WATCHDOG_DELAY);
}

static void beacon_disable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");
	cancel_delayed_work_sync(&mac->beacon.watchdog_work);

	zd_mac_free_cur_beacon(mac);
}

#define LINK_LED_WORK_DELAY HZ

static void link_led_handler(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, housekeeping.link_led_work.work);
	struct zd_chip *chip = &mac->chip;
	int is_associated;
	int r;

	if (!test_bit(ZD_DEVICE_RUNNING, &mac->flags))
		goto requeue;

	spin_lock_irq(&mac->lock);
	is_associated = mac->associated;
	spin_unlock_irq(&mac->lock);

	r = zd_chip_control_leds(chip,
		                 is_associated ? ZD_LED_ASSOCIATED : ZD_LED_SCANNING);
	if (r)
		dev_dbg_f(zd_mac_dev(mac), "zd_chip_control_leds error %d\n", r);

requeue:
	queue_delayed_work(zd_workqueue, &mac->housekeeping.link_led_work,
		           LINK_LED_WORK_DELAY);
}

static void housekeeping_init(struct zd_mac *mac)
{
	INIT_DELAYED_WORK(&mac->housekeeping.link_led_work, link_led_handler);
}

static void housekeeping_enable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");
	queue_delayed_work(zd_workqueue, &mac->housekeeping.link_led_work,
			   0);
}

static void housekeeping_disable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");
	cancel_delayed_work_sync(&mac->housekeeping.link_led_work);
	zd_chip_control_leds(&mac->chip, ZD_LED_OFF);
}
