/* zd_mac.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/usb.h>
#include <linux/jiffies.h>
#include <net/ieee80211_radiotap.h>

#include "zd_def.h"
#include "zd_chip.h"
#include "zd_mac.h"
#include "zd_ieee80211.h"
#include "zd_netdev.h"
#include "zd_rf.h"
#include "zd_util.h"

static void ieee_init(struct ieee80211_device *ieee);
static void softmac_init(struct ieee80211softmac_device *sm);

static void housekeeping_init(struct zd_mac *mac);
static void housekeeping_enable(struct zd_mac *mac);
static void housekeeping_disable(struct zd_mac *mac);

int zd_mac_init(struct zd_mac *mac,
	        struct net_device *netdev,
	        struct usb_interface *intf)
{
	struct ieee80211_device *ieee = zd_netdev_ieee80211(netdev);

	memset(mac, 0, sizeof(*mac));
	spin_lock_init(&mac->lock);
	mac->netdev = netdev;

	ieee_init(ieee);
	softmac_init(ieee80211_priv(netdev));
	zd_chip_init(&mac->chip, netdev, intf);
	housekeeping_init(mac);
	return 0;
}

static int reset_channel(struct zd_mac *mac)
{
	int r;
	unsigned long flags;
	const struct channel_range *range;

	spin_lock_irqsave(&mac->lock, flags);
	range = zd_channel_range(mac->regdomain);
	if (!range->start) {
		r = -EINVAL;
		goto out;
	}
	mac->requested_channel = range->start;
	r = 0;
out:
	spin_unlock_irqrestore(&mac->lock, flags);
	return r;
}

int zd_mac_init_hw(struct zd_mac *mac, u8 device_type)
{
	int r;
	struct zd_chip *chip = &mac->chip;
	u8 addr[ETH_ALEN];
	u8 default_regdomain;

	r = zd_chip_enable_int(chip);
	if (r)
		goto out;
	r = zd_chip_init_hw(chip, device_type);
	if (r)
		goto disable_int;

	zd_get_e2p_mac_addr(chip, addr);
	r = zd_write_mac_addr(chip, addr);
	if (r)
		goto disable_int;
	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&mac->lock);
	memcpy(mac->netdev->dev_addr, addr, ETH_ALEN);
	spin_unlock_irq(&mac->lock);

	r = zd_read_regdomain(chip, &default_regdomain);
	if (r)
		goto disable_int;
	if (!zd_regdomain_supported(default_regdomain)) {
		dev_dbg_f(zd_mac_dev(mac),
			  "Regulatory Domain %#04x is not supported.\n",
		          default_regdomain);
		r = -EINVAL;
		goto disable_int;
	}
	spin_lock_irq(&mac->lock);
	mac->regdomain = mac->default_regdomain = default_regdomain;
	spin_unlock_irq(&mac->lock);
	r = reset_channel(mac);
	if (r)
		goto disable_int;

	/* We must inform the device that we are doing encryption/decryption in
	 * software at the moment. */
	r = zd_set_encryption_type(chip, ENC_SNIFFER);
	if (r)
		goto disable_int;

	r = zd_geo_init(zd_mac_to_ieee80211(mac), mac->regdomain);
	if (r)
		goto disable_int;

	r = 0;
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

void zd_mac_clear(struct zd_mac *mac)
{
	zd_chip_clear(&mac->chip);
	ZD_ASSERT(!spin_is_locked(&mac->lock));
	ZD_MEMCLEAR(mac, sizeof(struct zd_mac));
}

static int reset_mode(struct zd_mac *mac)
{
	struct ieee80211_device *ieee = zd_mac_to_ieee80211(mac);
	struct zd_ioreq32 ioreqs[3] = {
		{ CR_RX_FILTER, STA_RX_FILTER },
		{ CR_SNIFFER_ON, 0U },
	};

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		ioreqs[0].value = 0xffffffff;
		ioreqs[1].value = 0x1;
		ioreqs[2].value = ENC_SNIFFER;
	}

	return zd_iowrite32a(&mac->chip, ioreqs, 3);
}

int zd_mac_open(struct net_device *netdev)
{
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct zd_chip *chip = &mac->chip;
	int r;

	r = zd_chip_enable_int(chip);
	if (r < 0)
		goto out;

	r = zd_chip_set_basic_rates(chip, CR_RATES_80211B | CR_RATES_80211G);
	if (r < 0)
		goto disable_int;
	r = reset_mode(mac);
	if (r)
		goto disable_int;
	r = zd_chip_switch_radio_on(chip);
	if (r < 0)
		goto disable_int;
	r = zd_chip_set_channel(chip, mac->requested_channel);
	if (r < 0)
		goto disable_radio;
	r = zd_chip_enable_rx(chip);
	if (r < 0)
		goto disable_radio;
	r = zd_chip_enable_hwint(chip);
	if (r < 0)
		goto disable_rx;

	housekeeping_enable(mac);
	ieee80211softmac_start(netdev);
	return 0;
disable_rx:
	zd_chip_disable_rx(chip);
disable_radio:
	zd_chip_switch_radio_off(chip);
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

int zd_mac_stop(struct net_device *netdev)
{
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct zd_chip *chip = &mac->chip;

	netif_stop_queue(netdev);

	/*
	 * The order here deliberately is a little different from the open()
	 * method, since we need to make sure there is no opportunity for RX
	 * frames to be processed by softmac after we have stopped it.
	 */

	zd_chip_disable_rx(chip);
	housekeeping_disable(mac);
	ieee80211softmac_stop(netdev);

	zd_chip_disable_hwint(chip);
	zd_chip_switch_radio_off(chip);
	zd_chip_disable_int(chip);

	return 0;
}

int zd_mac_set_mac_address(struct net_device *netdev, void *p)
{
	int r;
	unsigned long flags;
	struct sockaddr *addr = p;
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct zd_chip *chip = &mac->chip;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	dev_dbg_f(zd_mac_dev(mac),
		  "Setting MAC to " MAC_FMT "\n", MAC_ARG(addr->sa_data));

	r = zd_write_mac_addr(chip, addr->sa_data);
	if (r)
		return r;

	spin_lock_irqsave(&mac->lock, flags);
	memcpy(netdev->dev_addr, addr->sa_data, ETH_ALEN);
	spin_unlock_irqrestore(&mac->lock, flags);

	return 0;
}

int zd_mac_set_regdomain(struct zd_mac *mac, u8 regdomain)
{
	int r;
	u8 channel;

	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&mac->lock);
	if (regdomain == 0) {
		regdomain = mac->default_regdomain;
	}
	if (!zd_regdomain_supported(regdomain)) {
		spin_unlock_irq(&mac->lock);
		return -EINVAL;
	}
	mac->regdomain = regdomain;
	channel = mac->requested_channel;
	spin_unlock_irq(&mac->lock);

	r = zd_geo_init(zd_mac_to_ieee80211(mac), regdomain);
	if (r)
		return r;
	if (!zd_regdomain_supports_channel(regdomain, channel)) {
		r = reset_channel(mac);
		if (r)
			return r;
	}

	return 0;
}

u8 zd_mac_get_regdomain(struct zd_mac *mac)
{
	unsigned long flags;
	u8 regdomain;

	spin_lock_irqsave(&mac->lock, flags);
	regdomain = mac->regdomain;
	spin_unlock_irqrestore(&mac->lock, flags);
	return regdomain;
}

static void set_channel(struct net_device *netdev, u8 channel)
{
	struct zd_mac *mac = zd_netdev_mac(netdev);

	dev_dbg_f(zd_mac_dev(mac), "channel %d\n", channel);

	zd_chip_set_channel(&mac->chip, channel);
}

/* TODO: Should not work in Managed mode. */
int zd_mac_request_channel(struct zd_mac *mac, u8 channel)
{
	unsigned long lock_flags;
	struct ieee80211_device *ieee = zd_mac_to_ieee80211(mac);

	if (ieee->iw_mode == IW_MODE_INFRA)
		return -EPERM;

	spin_lock_irqsave(&mac->lock, lock_flags);
	if (!zd_regdomain_supports_channel(mac->regdomain, channel)) {
		spin_unlock_irqrestore(&mac->lock, lock_flags);
		return -EINVAL;
	}
	mac->requested_channel = channel;
	spin_unlock_irqrestore(&mac->lock, lock_flags);
	if (netif_running(mac->netdev))
		return zd_chip_set_channel(&mac->chip, channel);
	else
		return 0;
}

int zd_mac_get_channel(struct zd_mac *mac, u8 *channel, u8 *flags)
{
	struct ieee80211_device *ieee = zd_mac_to_ieee80211(mac);

	*channel = zd_chip_get_channel(&mac->chip);
	if (ieee->iw_mode != IW_MODE_INFRA) {
		spin_lock_irq(&mac->lock);
		*flags = *channel == mac->requested_channel ?
			MAC_FIXED_CHANNEL : 0;
		spin_unlock(&mac->lock);
	} else {
		*flags = 0;
	}
	dev_dbg_f(zd_mac_dev(mac), "channel %u flags %u\n", *channel, *flags);
	return 0;
}

/* If wrong rate is given, we are falling back to the slowest rate: 1MBit/s */
static u8 cs_typed_rate(u8 cs_rate)
{
	static const u8 typed_rates[16] = {
		[ZD_CS_CCK_RATE_1M]	= ZD_CS_CCK|ZD_CS_CCK_RATE_1M,
		[ZD_CS_CCK_RATE_2M]	= ZD_CS_CCK|ZD_CS_CCK_RATE_2M,
		[ZD_CS_CCK_RATE_5_5M]	= ZD_CS_CCK|ZD_CS_CCK_RATE_5_5M,
		[ZD_CS_CCK_RATE_11M]	= ZD_CS_CCK|ZD_CS_CCK_RATE_11M,
		[ZD_OFDM_RATE_6M]	= ZD_CS_OFDM|ZD_OFDM_RATE_6M,
		[ZD_OFDM_RATE_9M]	= ZD_CS_OFDM|ZD_OFDM_RATE_9M,
		[ZD_OFDM_RATE_12M]	= ZD_CS_OFDM|ZD_OFDM_RATE_12M,
		[ZD_OFDM_RATE_18M]	= ZD_CS_OFDM|ZD_OFDM_RATE_18M,
		[ZD_OFDM_RATE_24M]	= ZD_CS_OFDM|ZD_OFDM_RATE_24M,
		[ZD_OFDM_RATE_36M]	= ZD_CS_OFDM|ZD_OFDM_RATE_36M,
		[ZD_OFDM_RATE_48M]	= ZD_CS_OFDM|ZD_OFDM_RATE_48M,
		[ZD_OFDM_RATE_54M]	= ZD_CS_OFDM|ZD_OFDM_RATE_54M,
	};

	ZD_ASSERT(ZD_CS_RATE_MASK == 0x0f);
	return typed_rates[cs_rate & ZD_CS_RATE_MASK];
}

/* Fallback to lowest rate, if rate is unknown. */
static u8 rate_to_cs_rate(u8 rate)
{
	switch (rate) {
	case IEEE80211_CCK_RATE_2MB:
		return ZD_CS_CCK_RATE_2M;
	case IEEE80211_CCK_RATE_5MB:
		return ZD_CS_CCK_RATE_5_5M;
	case IEEE80211_CCK_RATE_11MB:
		return ZD_CS_CCK_RATE_11M;
	case IEEE80211_OFDM_RATE_6MB:
		return ZD_OFDM_RATE_6M;
	case IEEE80211_OFDM_RATE_9MB:
		return ZD_OFDM_RATE_9M;
	case IEEE80211_OFDM_RATE_12MB:
		return ZD_OFDM_RATE_12M;
	case IEEE80211_OFDM_RATE_18MB:
		return ZD_OFDM_RATE_18M;
	case IEEE80211_OFDM_RATE_24MB:
		return ZD_OFDM_RATE_24M;
	case IEEE80211_OFDM_RATE_36MB:
		return ZD_OFDM_RATE_36M;
	case IEEE80211_OFDM_RATE_48MB:
		return ZD_OFDM_RATE_48M;
	case IEEE80211_OFDM_RATE_54MB:
		return ZD_OFDM_RATE_54M;
	}
	return ZD_CS_CCK_RATE_1M;
}

int zd_mac_set_mode(struct zd_mac *mac, u32 mode)
{
	struct ieee80211_device *ieee;

	switch (mode) {
	case IW_MODE_AUTO:
	case IW_MODE_ADHOC:
	case IW_MODE_INFRA:
		mac->netdev->type = ARPHRD_ETHER;
		break;
	case IW_MODE_MONITOR:
		mac->netdev->type = ARPHRD_IEEE80211_RADIOTAP;
		break;
	default:
		dev_dbg_f(zd_mac_dev(mac), "wrong mode %u\n", mode);
		return -EINVAL;
	}

	ieee = zd_mac_to_ieee80211(mac);
	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&ieee->lock);
	ieee->iw_mode = mode;
	spin_unlock_irq(&ieee->lock);

	if (netif_running(mac->netdev))
		return reset_mode(mac);

	return 0;
}

int zd_mac_get_mode(struct zd_mac *mac, u32 *mode)
{
	unsigned long flags;
	struct ieee80211_device *ieee;

	ieee = zd_mac_to_ieee80211(mac);
	spin_lock_irqsave(&ieee->lock, flags);
	*mode = ieee->iw_mode;
	spin_unlock_irqrestore(&ieee->lock, flags);
	return 0;
}

int zd_mac_get_range(struct zd_mac *mac, struct iw_range *range)
{
	int i;
	const struct channel_range *channel_range;
	u8 regdomain;

	memset(range, 0, sizeof(*range));

	/* FIXME: Not so important and depends on the mode. For 802.11g
	 * usually this value is used. It seems to be that Bit/s number is
	 * given here.
	 */
	range->throughput = 27 * 1000 * 1000;

	range->max_qual.qual = 100;
	range->max_qual.level = 100;

	/* FIXME: Needs still to be tuned. */
	range->avg_qual.qual = 71;
	range->avg_qual.level = 80;

	/* FIXME: depends on standard? */
	range->min_rts = 256;
	range->max_rts = 2346;

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->max_encoding_tokens = WEP_KEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = WEP_KEY_LEN;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 20;

	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&mac->lock);
	regdomain = mac->regdomain;
	spin_unlock_irq(&mac->lock);
	channel_range = zd_channel_range(regdomain);

	range->num_channels = channel_range->end - channel_range->start;
	range->old_num_channels = range->num_channels;
	range->num_frequency = range->num_channels;
	range->old_num_frequency = range->num_frequency;

	for (i = 0; i < range->num_frequency; i++) {
		struct iw_freq *freq = &range->freq[i];
		freq->i = channel_range->start + i;
		zd_channel_to_freq(freq, freq->i);
	}

	return 0;
}

static int zd_calc_tx_length_us(u8 *service, u8 cs_rate, u16 tx_length)
{
	static const u8 rate_divisor[] = {
		[ZD_CS_CCK_RATE_1M]	=  1,
		[ZD_CS_CCK_RATE_2M]	=  2,
		[ZD_CS_CCK_RATE_5_5M]	= 11, /* bits must be doubled */
		[ZD_CS_CCK_RATE_11M]	= 11,
		[ZD_OFDM_RATE_6M]	=  6,
		[ZD_OFDM_RATE_9M]	=  9,
		[ZD_OFDM_RATE_12M]	= 12,
		[ZD_OFDM_RATE_18M]	= 18,
		[ZD_OFDM_RATE_24M]	= 24,
		[ZD_OFDM_RATE_36M]	= 36,
		[ZD_OFDM_RATE_48M]	= 48,
		[ZD_OFDM_RATE_54M]	= 54,
	};

	u32 bits = (u32)tx_length * 8;
	u32 divisor;

	divisor = rate_divisor[cs_rate];
	if (divisor == 0)
		return -EINVAL;

	switch (cs_rate) {
	case ZD_CS_CCK_RATE_5_5M:
		bits = (2*bits) + 10; /* round up to the next integer */
		break;
	case ZD_CS_CCK_RATE_11M:
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

enum {
	R2M_SHORT_PREAMBLE = 0x01,
	R2M_11A		   = 0x02,
};

static u8 cs_rate_to_modulation(u8 cs_rate, int flags)
{
	u8 modulation;

	modulation = cs_typed_rate(cs_rate);
	if (flags & R2M_SHORT_PREAMBLE) {
		switch (ZD_CS_RATE(modulation)) {
		case ZD_CS_CCK_RATE_2M:
		case ZD_CS_CCK_RATE_5_5M:
		case ZD_CS_CCK_RATE_11M:
			modulation |= ZD_CS_CCK_PREA_SHORT;
			return modulation;
		}
	}
	if (flags & R2M_11A) {
		if (ZD_CS_TYPE(modulation) == ZD_CS_OFDM)
			modulation |= ZD_CS_OFDM_MODE_11A;
	}
	return modulation;
}

static void cs_set_modulation(struct zd_mac *mac, struct zd_ctrlset *cs,
	                      struct ieee80211_hdr_4addr *hdr)
{
	struct ieee80211softmac_device *softmac = ieee80211_priv(mac->netdev);
	u16 ftype = WLAN_FC_GET_TYPE(le16_to_cpu(hdr->frame_ctl));
	u8 rate, cs_rate;
	int is_mgt = (ftype == IEEE80211_FTYPE_MGMT) != 0;

	/* FIXME: 802.11a? short preamble? */
	rate = ieee80211softmac_suggest_txrate(softmac,
		is_multicast_ether_addr(hdr->addr1), is_mgt);

	cs_rate = rate_to_cs_rate(rate);
	cs->modulation = cs_rate_to_modulation(cs_rate, 0);
}

static void cs_set_control(struct zd_mac *mac, struct zd_ctrlset *cs,
	                   struct ieee80211_hdr_4addr *header)
{
	unsigned int tx_length = le16_to_cpu(cs->tx_length);
	u16 fctl = le16_to_cpu(header->frame_ctl);
	u16 ftype = WLAN_FC_GET_TYPE(fctl);
	u16 stype = WLAN_FC_GET_STYPE(fctl);

	/*
	 * CONTROL:
	 * - start at 0x00
	 * - if fragment 0, enable bit 0
	 * - if backoff needed, enable bit 0
	 * - if burst (backoff not needed) disable bit 0
	 * - if multicast, enable bit 1
	 * - if PS-POLL frame, enable bit 2
	 * - if in INDEPENDENT_BSS mode and zd1205_DestPowerSave, then enable
	 *   bit 4 (FIXME: wtf)
	 * - if frag_len > RTS threshold, set bit 5 as long if it isnt
	 *   multicast or mgt
	 * - if bit 5 is set, and we are in OFDM mode, unset bit 5 and set bit
	 *   7
	 */

	cs->control = 0;

	/* First fragment */
	if (WLAN_GET_SEQ_FRAG(le16_to_cpu(header->seq_ctl)) == 0)
		cs->control |= ZD_CS_NEED_RANDOM_BACKOFF;

	/* Multicast */
	if (is_multicast_ether_addr(header->addr1))
		cs->control |= ZD_CS_MULTICAST;

	/* PS-POLL */
	if (stype == IEEE80211_STYPE_PSPOLL)
		cs->control |= ZD_CS_PS_POLL_FRAME;

	if (!is_multicast_ether_addr(header->addr1) &&
	    ftype != IEEE80211_FTYPE_MGMT &&
	    tx_length > zd_netdev_ieee80211(mac->netdev)->rts)
	{
		/* FIXME: check the logic */
		if (ZD_CS_TYPE(cs->modulation) == ZD_CS_OFDM) {
			/* 802.11g */
			cs->control |= ZD_CS_SELF_CTS;
		} else { /* 802.11b */
			cs->control |= ZD_CS_RTS;
		}
	}

	/* FIXME: Management frame? */
}

static int fill_ctrlset(struct zd_mac *mac,
	                struct ieee80211_txb *txb,
			int frag_num)
{
	int r;
	struct sk_buff *skb = txb->fragments[frag_num];
	struct ieee80211_hdr_4addr *hdr =
		(struct ieee80211_hdr_4addr *) skb->data;
	unsigned int frag_len = skb->len + IEEE80211_FCS_LEN;
	unsigned int next_frag_len;
	unsigned int packet_length;
	struct zd_ctrlset *cs = (struct zd_ctrlset *)
		skb_push(skb, sizeof(struct zd_ctrlset));

	if (frag_num+1  < txb->nr_frags) {
		next_frag_len = txb->fragments[frag_num+1]->len +
			        IEEE80211_FCS_LEN;
	} else {
		next_frag_len = 0;
	}
	ZD_ASSERT(frag_len <= 0xffff);
	ZD_ASSERT(next_frag_len <= 0xffff);

	cs_set_modulation(mac, cs, hdr);

	cs->tx_length = cpu_to_le16(frag_len);

	cs_set_control(mac, cs, hdr);

	packet_length = frag_len + sizeof(struct zd_ctrlset) + 10;
	ZD_ASSERT(packet_length <= 0xffff);
	/* ZD1211B: Computing the length difference this way, gives us
	 * flexibility to compute the packet length.
	 */
	cs->packet_length = cpu_to_le16(mac->chip.is_zd1211b ?
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
	r = zd_calc_tx_length_us(&cs->service, ZD_CS_RATE(cs->modulation),
		                 le16_to_cpu(cs->tx_length));
	if (r < 0)
		return r;
	cs->current_length = cpu_to_le16(r);

	if (next_frag_len == 0) {
		cs->next_frame_length = 0;
	} else {
		r = zd_calc_tx_length_us(NULL, ZD_CS_RATE(cs->modulation),
			                 next_frag_len);
		if (r < 0)
			return r;
		cs->next_frame_length = cpu_to_le16(r);
	}

	return 0;
}

static int zd_mac_tx(struct zd_mac *mac, struct ieee80211_txb *txb, int pri)
{
	int i, r;

	for (i = 0; i < txb->nr_frags; i++) {
		struct sk_buff *skb = txb->fragments[i];

		r = fill_ctrlset(mac, txb, i);
		if (r)
			return r;
		r = zd_usb_tx(&mac->chip.usb, skb->data, skb->len);
		if (r)
			return r;
	}

	/* FIXME: shouldn't this be handled by the upper layers? */
	mac->netdev->trans_start = jiffies;

	ieee80211_txb_free(txb);
	return 0;
}

struct zd_rt_hdr {
	struct ieee80211_radiotap_header rt_hdr;
	u8  rt_flags;
	u8  rt_rate;
	u16 rt_channel;
	u16 rt_chbitmask;
} __attribute__((packed));

static void fill_rt_header(void *buffer, struct zd_mac *mac,
	                   const struct ieee80211_rx_stats *stats,
			   const struct rx_status *status)
{
	struct zd_rt_hdr *hdr = buffer;

	hdr->rt_hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->rt_hdr.it_pad = 0;
	hdr->rt_hdr.it_len = cpu_to_le16(sizeof(struct zd_rt_hdr));
	hdr->rt_hdr.it_present = cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
		                 (1 << IEEE80211_RADIOTAP_CHANNEL) |
				 (1 << IEEE80211_RADIOTAP_RATE));

	hdr->rt_flags = 0;
	if (status->decryption_type & (ZD_RX_WEP64|ZD_RX_WEP128|ZD_RX_WEP256))
		hdr->rt_flags |= IEEE80211_RADIOTAP_F_WEP;

	hdr->rt_rate = stats->rate / 5;

	/* FIXME: 802.11a */
	hdr->rt_channel = cpu_to_le16(ieee80211chan2mhz(
		                             _zd_chip_get_channel(&mac->chip)));
	hdr->rt_chbitmask = cpu_to_le16(IEEE80211_CHAN_2GHZ |
		((status->frame_status & ZD_RX_FRAME_MODULATION_MASK) ==
		ZD_RX_OFDM ? IEEE80211_CHAN_OFDM : IEEE80211_CHAN_CCK));
}

/* Returns 1 if the data packet is for us and 0 otherwise. */
static int is_data_packet_for_us(struct ieee80211_device *ieee,
	                         struct ieee80211_hdr_4addr *hdr)
{
	struct net_device *netdev = ieee->dev;
	u16 fc = le16_to_cpu(hdr->frame_ctl);

	ZD_ASSERT(WLAN_FC_GET_TYPE(fc) == IEEE80211_FTYPE_DATA);

	switch (ieee->iw_mode) {
	case IW_MODE_ADHOC:
		if ((fc & (IEEE80211_FCTL_TODS|IEEE80211_FCTL_FROMDS)) != 0 ||
		    memcmp(hdr->addr3, ieee->bssid, ETH_ALEN) != 0)
			return 0;
		break;
	case IW_MODE_AUTO:
	case IW_MODE_INFRA:
		if ((fc & (IEEE80211_FCTL_TODS|IEEE80211_FCTL_FROMDS)) !=
		    IEEE80211_FCTL_FROMDS ||
		    memcmp(hdr->addr2, ieee->bssid, ETH_ALEN) != 0)
			return 0;
		break;
	default:
		ZD_ASSERT(ieee->iw_mode != IW_MODE_MONITOR);
		return 0;
	}

	return memcmp(hdr->addr1, netdev->dev_addr, ETH_ALEN) == 0 ||
	       is_multicast_ether_addr(hdr->addr1) ||
	       (netdev->flags & IFF_PROMISC);
}

/* Filters receiving packets. If it returns 1 send it to ieee80211_rx, if 0
 * return. If an error is detected -EINVAL is returned. ieee80211_rx_mgt() is
 * called here.
 *
 * It has been based on ieee80211_rx_any.
 */
static int filter_rx(struct ieee80211_device *ieee,
	             const u8 *buffer, unsigned int length,
		     struct ieee80211_rx_stats *stats)
{
	struct ieee80211_hdr_4addr *hdr;
	u16 fc;

	if (ieee->iw_mode == IW_MODE_MONITOR)
		return 1;

	hdr = (struct ieee80211_hdr_4addr *)buffer;
	fc = le16_to_cpu(hdr->frame_ctl);
	if ((fc & IEEE80211_FCTL_VERS) != 0)
		return -EINVAL;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_MGMT:
		if (length < sizeof(struct ieee80211_hdr_3addr))
			return -EINVAL;
		ieee80211_rx_mgt(ieee, hdr, stats);
		return 0;
	case IEEE80211_FTYPE_CTL:
		/* Ignore invalid short buffers */
		return 0;
	case IEEE80211_FTYPE_DATA:
		if (length < sizeof(struct ieee80211_hdr_3addr))
			return -EINVAL;
		return is_data_packet_for_us(ieee, hdr);
	}

	return -EINVAL;
}

static void update_qual_rssi(struct zd_mac *mac,
			     const u8 *buffer, unsigned int length,
			     u8 qual_percent, u8 rssi_percent)
{
	unsigned long flags;
	struct ieee80211_hdr_3addr *hdr;
	int i;

	hdr = (struct ieee80211_hdr_3addr *)buffer;
	if (length < offsetof(struct ieee80211_hdr_3addr, addr3))
		return;
	if (memcmp(hdr->addr2, zd_mac_to_ieee80211(mac)->bssid, ETH_ALEN) != 0)
		return;

	spin_lock_irqsave(&mac->lock, flags);
	i = mac->stats_count % ZD_MAC_STATS_BUFFER_SIZE;
	mac->qual_buffer[i] = qual_percent;
	mac->rssi_buffer[i] = rssi_percent;
	mac->stats_count++;
	spin_unlock_irqrestore(&mac->lock, flags);
}

static int fill_rx_stats(struct ieee80211_rx_stats *stats,
	                 const struct rx_status **pstatus,
		         struct zd_mac *mac,
			 const u8 *buffer, unsigned int length)
{
	const struct rx_status *status;

	*pstatus = status = zd_tail(buffer, length, sizeof(struct rx_status));
	if (status->frame_status & ZD_RX_ERROR) {
		/* FIXME: update? */
		return -EINVAL;
	}
	memset(stats, 0, sizeof(struct ieee80211_rx_stats));
	stats->len = length - (ZD_PLCP_HEADER_SIZE + IEEE80211_FCS_LEN +
		               + sizeof(struct rx_status));
	/* FIXME: 802.11a */
	stats->freq = IEEE80211_24GHZ_BAND;
	stats->received_channel = _zd_chip_get_channel(&mac->chip);
	stats->rssi = zd_rx_strength_percent(status->signal_strength);
	stats->signal = zd_rx_qual_percent(buffer,
		                          length - sizeof(struct rx_status),
		                          status);
	stats->mask = IEEE80211_STATMASK_RSSI | IEEE80211_STATMASK_SIGNAL;
	stats->rate = zd_rx_rate(buffer, status);
	if (stats->rate)
		stats->mask |= IEEE80211_STATMASK_RATE;

	return 0;
}

int zd_mac_rx(struct zd_mac *mac, const u8 *buffer, unsigned int length)
{
	int r;
	struct ieee80211_device *ieee = zd_mac_to_ieee80211(mac);
	struct ieee80211_rx_stats stats;
	const struct rx_status *status;
	struct sk_buff *skb;

	if (length < ZD_PLCP_HEADER_SIZE + IEEE80211_1ADDR_LEN +
	             IEEE80211_FCS_LEN + sizeof(struct rx_status))
		return -EINVAL;

	r = fill_rx_stats(&stats, &status, mac, buffer, length);
	if (r)
		return r;

	length -= ZD_PLCP_HEADER_SIZE+IEEE80211_FCS_LEN+
		  sizeof(struct rx_status);
	buffer += ZD_PLCP_HEADER_SIZE;

	update_qual_rssi(mac, buffer, length, stats.signal, stats.rssi);

	r = filter_rx(ieee, buffer, length, &stats);
	if (r <= 0)
		return r;

	skb = dev_alloc_skb(sizeof(struct zd_rt_hdr) + length);
	if (!skb)
		return -ENOMEM;
	if (ieee->iw_mode == IW_MODE_MONITOR)
		fill_rt_header(skb_put(skb, sizeof(struct zd_rt_hdr)), mac,
			       &stats, status);
	memcpy(skb_put(skb, length), buffer, length);

	r = ieee80211_rx(ieee, skb, &stats);
	if (!r) {
		ZD_ASSERT(in_irq());
		dev_kfree_skb_irq(skb);
	}
	return 0;
}

static int netdev_tx(struct ieee80211_txb *txb, struct net_device *netdev,
		     int pri)
{
	return zd_mac_tx(zd_netdev_mac(netdev), txb, pri);
}

static void set_security(struct net_device *netdev,
			 struct ieee80211_security *sec)
{
	struct ieee80211_device *ieee = zd_netdev_ieee80211(netdev);
	struct ieee80211_security *secinfo = &ieee->sec;
	int keyidx;

	dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)), "\n");

	for (keyidx = 0; keyidx<WEP_KEYS; keyidx++)
		if (sec->flags & (1<<keyidx)) {
			secinfo->encode_alg[keyidx] = sec->encode_alg[keyidx];
			secinfo->key_sizes[keyidx] = sec->key_sizes[keyidx];
			memcpy(secinfo->keys[keyidx], sec->keys[keyidx],
			       SCM_KEY_LEN);
		}

	if (sec->flags & SEC_ACTIVE_KEY) {
		secinfo->active_key = sec->active_key;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .active_key = %d\n", sec->active_key);
	}
	if (sec->flags & SEC_UNICAST_GROUP) {
		secinfo->unicast_uses_group = sec->unicast_uses_group;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .unicast_uses_group = %d\n",
			sec->unicast_uses_group);
	}
	if (sec->flags & SEC_LEVEL) {
		secinfo->level = sec->level;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .level = %d\n", sec->level);
	}
	if (sec->flags & SEC_ENABLED) {
		secinfo->enabled = sec->enabled;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .enabled = %d\n", sec->enabled);
	}
	if (sec->flags & SEC_ENCRYPT) {
		secinfo->encrypt = sec->encrypt;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .encrypt = %d\n", sec->encrypt);
	}
	if (sec->flags & SEC_AUTH_MODE) {
		secinfo->auth_mode = sec->auth_mode;
		dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)),
			"   .auth_mode = %d\n", sec->auth_mode);
	}
}

static void ieee_init(struct ieee80211_device *ieee)
{
	ieee->mode = IEEE_B | IEEE_G;
	ieee->freq_band = IEEE80211_24GHZ_BAND;
	ieee->modulation = IEEE80211_OFDM_MODULATION | IEEE80211_CCK_MODULATION;
	ieee->tx_headroom = sizeof(struct zd_ctrlset);
	ieee->set_security = set_security;
	ieee->hard_start_xmit = netdev_tx;

	/* Software encryption/decryption for now */
	ieee->host_build_iv = 0;
	ieee->host_encrypt = 1;
	ieee->host_decrypt = 1;

	/* FIXME: default to managed mode, until ieee80211 and zd1211rw can
	 * correctly support AUTO */
	ieee->iw_mode = IW_MODE_INFRA;
}

static void softmac_init(struct ieee80211softmac_device *sm)
{
	sm->set_channel = set_channel;
}

struct iw_statistics *zd_mac_get_wireless_stats(struct net_device *ndev)
{
	struct zd_mac *mac = zd_netdev_mac(ndev);
	struct iw_statistics *iw_stats = &mac->iw_stats;
	unsigned int i, count, qual_total, rssi_total;

	memset(iw_stats, 0, sizeof(struct iw_statistics));
	/* We are not setting the status, because ieee->state is not updated
	 * at all and this driver doesn't track authentication state.
	 */
	spin_lock_irq(&mac->lock);
	count = mac->stats_count < ZD_MAC_STATS_BUFFER_SIZE ?
		mac->stats_count : ZD_MAC_STATS_BUFFER_SIZE;
	qual_total = rssi_total = 0;
	for (i = 0; i < count; i++) {
		qual_total += mac->qual_buffer[i];
		rssi_total += mac->rssi_buffer[i];
	}
	spin_unlock_irq(&mac->lock);
	iw_stats->qual.updated = IW_QUAL_NOISE_INVALID;
	if (count > 0) {
		iw_stats->qual.qual = qual_total / count;
		iw_stats->qual.level = rssi_total / count;
		iw_stats->qual.updated |=
			IW_QUAL_QUAL_UPDATED|IW_QUAL_LEVEL_UPDATED;
	} else {
		iw_stats->qual.updated |=
			IW_QUAL_QUAL_INVALID|IW_QUAL_LEVEL_INVALID;
	}
	/* TODO: update counter */
	return iw_stats;
}

#ifdef DEBUG
static const char* decryption_types[] = {
	[ZD_RX_NO_WEP] = "none",
	[ZD_RX_WEP64] = "WEP64",
	[ZD_RX_TKIP] = "TKIP",
	[ZD_RX_AES] = "AES",
	[ZD_RX_WEP128] = "WEP128",
	[ZD_RX_WEP256] = "WEP256",
};

static const char *decryption_type_string(u8 type)
{
	const char *s;

	if (type < ARRAY_SIZE(decryption_types)) {
		s = decryption_types[type];
	} else {
		s = NULL;
	}
	return s ? s : "unknown";
}

static int is_ofdm(u8 frame_status)
{
	return (frame_status & ZD_RX_OFDM);
}

void zd_dump_rx_status(const struct rx_status *status)
{
	const char* modulation;
	u8 quality;

	if (is_ofdm(status->frame_status)) {
		modulation = "ofdm";
		quality = status->signal_quality_ofdm;
	} else {
		modulation = "cck";
		quality = status->signal_quality_cck;
	}
	pr_debug("rx status %s strength %#04x qual %#04x decryption %s\n",
		modulation, status->signal_strength, quality,
		decryption_type_string(status->decryption_type));
	if (status->frame_status & ZD_RX_ERROR) {
		pr_debug("rx error %s%s%s%s%s%s\n",
			(status->frame_status & ZD_RX_TIMEOUT_ERROR) ?
				"timeout " : "",
			(status->frame_status & ZD_RX_FIFO_OVERRUN_ERROR) ?
				"fifo " : "",
			(status->frame_status & ZD_RX_DECRYPTION_ERROR) ?
				"decryption " : "",
			(status->frame_status & ZD_RX_CRC32_ERROR) ?
				"crc32 " : "",
			(status->frame_status & ZD_RX_NO_ADDR1_MATCH_ERROR) ?
				"addr1 " : "",
			(status->frame_status & ZD_RX_CRC16_ERROR) ?
				"crc16" : "");
	}
}
#endif /* DEBUG */

#define LINK_LED_WORK_DELAY HZ

static void link_led_handler(void *p)
{
	struct zd_mac *mac = p;
	struct zd_chip *chip = &mac->chip;
	struct ieee80211softmac_device *sm = ieee80211_priv(mac->netdev);
	int is_associated;
	int r;

	spin_lock_irq(&mac->lock);
	is_associated = sm->associnfo.associated != 0;
	spin_unlock_irq(&mac->lock);

	r = zd_chip_control_leds(chip,
		                 is_associated ? LED_ASSOCIATED : LED_SCANNING);
	if (r)
		dev_err(zd_mac_dev(mac), "zd_chip_control_leds error %d\n", r);

	queue_delayed_work(zd_workqueue, &mac->housekeeping.link_led_work,
		           LINK_LED_WORK_DELAY);
}

static void housekeeping_init(struct zd_mac *mac)
{
	INIT_WORK(&mac->housekeeping.link_led_work, link_led_handler, mac);
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
	cancel_rearming_delayed_workqueue(zd_workqueue,
		&mac->housekeeping.link_led_work);
	zd_chip_control_leds(&mac->chip, LED_OFF);
}
