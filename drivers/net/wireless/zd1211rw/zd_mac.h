/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
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

#ifndef _ZD_MAC_H
#define _ZD_MAC_H

#include <linux/kernel.h>
#include <net/mac80211.h>

#include "zd_chip.h"
#include "zd_ieee80211.h"

struct zd_ctrlset {
	u8     modulation;
	__le16 tx_length;
	u8     control;
	/* stores only the difference to tx_length on ZD1211B */
	__le16 packet_length;
	__le16 current_length;
	u8     service;
	__le16  next_frame_length;
} __attribute__((packed));

#define ZD_CS_RESERVED_SIZE	25

/* The field modulation of struct zd_ctrlset controls the bit rate, the use
 * of short or long preambles in 802.11b (CCK mode) or the use of 802.11a or
 * 802.11g in OFDM mode.
 *
 * The term zd-rate is used for the combination of the modulation type flag
 * and the "pure" rate value.
 */
#define ZD_PURE_RATE_MASK       0x0f
#define ZD_MODULATION_TYPE_MASK 0x10
#define ZD_RATE_MASK            (ZD_PURE_RATE_MASK|ZD_MODULATION_TYPE_MASK)
#define ZD_PURE_RATE(modulation) ((modulation) & ZD_PURE_RATE_MASK)
#define ZD_MODULATION_TYPE(modulation) ((modulation) & ZD_MODULATION_TYPE_MASK)
#define ZD_RATE(modulation) ((modulation) & ZD_RATE_MASK)

/* The two possible modulation types. Notify that 802.11b doesn't use the CCK
 * codeing for the 1 and 2 MBit/s rate. We stay with the term here to remain
 * consistent with uses the term at other places.
 */
#define ZD_CCK                  0x00
#define ZD_OFDM                 0x10

/* The ZD1211 firmware uses proprietary encodings of the 802.11b (CCK) rates.
 * For OFDM the PLCP rate encodings are used. We combine these "pure" rates
 * with the modulation type flag and call the resulting values zd-rates.
 */
#define ZD_CCK_RATE_1M          (ZD_CCK|0x00)
#define ZD_CCK_RATE_2M          (ZD_CCK|0x01)
#define ZD_CCK_RATE_5_5M        (ZD_CCK|0x02)
#define ZD_CCK_RATE_11M         (ZD_CCK|0x03)
#define ZD_OFDM_RATE_6M         (ZD_OFDM|ZD_OFDM_PLCP_RATE_6M)
#define ZD_OFDM_RATE_9M         (ZD_OFDM|ZD_OFDM_PLCP_RATE_9M)
#define ZD_OFDM_RATE_12M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_12M)
#define ZD_OFDM_RATE_18M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_18M)
#define ZD_OFDM_RATE_24M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_24M)
#define ZD_OFDM_RATE_36M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_36M)
#define ZD_OFDM_RATE_48M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_48M)
#define ZD_OFDM_RATE_54M        (ZD_OFDM|ZD_OFDM_PLCP_RATE_54M)

/* The bit 5 of the zd_ctrlset modulation field controls the preamble in CCK
 * mode or the 802.11a/802.11g selection in OFDM mode.
 */
#define ZD_CCK_PREA_LONG        0x00
#define ZD_CCK_PREA_SHORT       0x20
#define ZD_OFDM_MODE_11G        0x00
#define ZD_OFDM_MODE_11A        0x20

/* zd_ctrlset control field */
#define ZD_CS_NEED_RANDOM_BACKOFF	0x01
#define ZD_CS_MULTICAST			0x02

#define ZD_CS_FRAME_TYPE_MASK		0x0c
#define ZD_CS_DATA_FRAME		0x00
#define ZD_CS_PS_POLL_FRAME		0x04
#define ZD_CS_MANAGEMENT_FRAME		0x08
#define ZD_CS_NO_SEQUENCE_CTL_FRAME	0x0c

#define ZD_CS_WAKE_DESTINATION		0x10
#define ZD_CS_RTS			0x20
#define ZD_CS_ENCRYPT			0x40
#define ZD_CS_SELF_CTS			0x80

/* Incoming frames are prepended by a PLCP header */
#define ZD_PLCP_HEADER_SIZE		5

struct rx_length_info {
	__le16 length[3];
	__le16 tag;
} __attribute__((packed));

#define RX_LENGTH_INFO_TAG		0x697e

struct rx_status {
	u8 signal_quality_cck;
	/* rssi */
	u8 signal_strength;
	u8 signal_quality_ofdm;
	u8 decryption_type;
	u8 frame_status;
} __attribute__((packed));

/* rx_status field decryption_type */
#define ZD_RX_NO_WEP	0
#define ZD_RX_WEP64	1
#define ZD_RX_TKIP	2
#define ZD_RX_AES	4
#define ZD_RX_WEP128	5
#define ZD_RX_WEP256	6

/* rx_status field frame_status */
#define ZD_RX_FRAME_MODULATION_MASK	0x01
#define ZD_RX_CCK			0x00
#define ZD_RX_OFDM			0x01

#define ZD_RX_TIMEOUT_ERROR		0x02
#define ZD_RX_FIFO_OVERRUN_ERROR	0x04
#define ZD_RX_DECRYPTION_ERROR		0x08
#define ZD_RX_CRC32_ERROR		0x10
#define ZD_RX_NO_ADDR1_MATCH_ERROR	0x20
#define ZD_RX_CRC16_ERROR		0x40
#define ZD_RX_ERROR			0x80

enum mac_flags {
	MAC_FIXED_CHANNEL = 0x01,
};

struct housekeeping {
	struct delayed_work link_led_work;
};

/**
 * struct zd_tx_skb_control_block - control block for tx skbuffs
 * @control: &struct ieee80211_tx_control pointer
 * @context: context pointer
 *
 * This structure is used to fill the cb field in an &sk_buff to transmit.
 * The control field is NULL, if there is no requirement from the mac80211
 * stack to report about the packet ACK. This is the case if the flag
 * IEEE80211_TXCTL_NO_ACK is not set in &struct ieee80211_tx_control.
 */
struct zd_tx_skb_control_block {
	struct ieee80211_tx_control *control;
	struct ieee80211_hw *hw;
	void *context;
};

#define ZD_MAC_STATS_BUFFER_SIZE 16

#define ZD_MAC_MAX_ACK_WAITERS 10

struct zd_mac {
	struct zd_chip chip;
	spinlock_t lock;
	spinlock_t intr_lock;
	struct ieee80211_hw *hw;
	struct housekeeping housekeeping;
	struct work_struct set_multicast_hash_work;
	struct work_struct set_rts_cts_work;
	struct work_struct set_rx_filter_work;
	struct work_struct process_intr;
	struct zd_mc_hash multicast_hash;
	u8 intr_buffer[USB_MAX_EP_INT_BUFFER];
	u8 regdomain;
	u8 default_regdomain;
	int type;
	int associated;
	struct sk_buff_head ack_wait_queue;
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_supported_band band;

	/* Short preamble (used for RTS/CTS) */
	unsigned int short_preamble:1;

	/* flags to indicate update in progress */
	unsigned int updating_rts_rate:1;

	/* whether to pass frames with CRC errors to stack */
	unsigned int pass_failed_fcs:1;

	/* whether to pass control frames to stack */
	unsigned int pass_ctrl:1;
};

static inline struct zd_mac *zd_hw_mac(struct ieee80211_hw *hw)
{
	return hw->priv;
}

static inline struct zd_mac *zd_chip_to_mac(struct zd_chip *chip)
{
	return container_of(chip, struct zd_mac, chip);
}

static inline struct zd_mac *zd_usb_to_mac(struct zd_usb *usb)
{
	return zd_chip_to_mac(zd_usb_to_chip(usb));
}

static inline u8 *zd_mac_get_perm_addr(struct zd_mac *mac)
{
	return mac->hw->wiphy->perm_addr;
}

#define zd_mac_dev(mac) (zd_chip_dev(&(mac)->chip))

struct ieee80211_hw *zd_mac_alloc_hw(struct usb_interface *intf);
void zd_mac_clear(struct zd_mac *mac);

int zd_mac_preinit_hw(struct ieee80211_hw *hw);
int zd_mac_init_hw(struct ieee80211_hw *hw);

int zd_mac_rx(struct ieee80211_hw *hw, const u8 *buffer, unsigned int length);
void zd_mac_tx_failed(struct ieee80211_hw *hw);
void zd_mac_tx_to_dev(struct sk_buff *skb, int error);

#ifdef DEBUG
void zd_dump_rx_status(const struct rx_status *status);
#else
#define zd_dump_rx_status(status)
#endif /* DEBUG */

#endif /* _ZD_MAC_H */
