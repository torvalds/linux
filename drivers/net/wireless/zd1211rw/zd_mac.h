/* zd_mac.h
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

#include <linux/wireless.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <net/ieee80211.h>
#include <net/ieee80211softmac.h>

#include "zd_chip.h"
#include "zd_netdev.h"

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

struct housekeeping {
	struct delayed_work link_led_work;
};

#define ZD_MAC_STATS_BUFFER_SIZE 16

struct zd_mac {
	struct zd_chip chip;
	spinlock_t lock;
	struct net_device *netdev;

	/* Unlocked reading possible */
	struct iw_statistics iw_stats;

	struct housekeeping housekeeping;
	struct work_struct set_multicast_hash_work;
	struct zd_mc_hash multicast_hash;
	struct delayed_work set_rts_cts_work;
	struct delayed_work set_basic_rates_work;

	struct tasklet_struct rx_tasklet;
	struct sk_buff_head rx_queue;

	unsigned int stats_count;
	u8 qual_buffer[ZD_MAC_STATS_BUFFER_SIZE];
	u8 rssi_buffer[ZD_MAC_STATS_BUFFER_SIZE];
	u8 regdomain;
	u8 default_regdomain;
	u8 requested_channel;

	/* A bitpattern of cr_rates */
	u16 basic_rates;

	/* A zd_rate */
	u8 rts_rate;

	/* Short preamble (used for RTS/CTS) */
	unsigned int short_preamble:1;

	/* flags to indicate update in progress */
	unsigned int updating_rts_rate:1;
	unsigned int updating_basic_rates:1;
};

static inline struct ieee80211_device *zd_mac_to_ieee80211(struct zd_mac *mac)
{
	return zd_netdev_ieee80211(mac->netdev);
}

static inline struct zd_mac *zd_netdev_mac(struct net_device *netdev)
{
	return ieee80211softmac_priv(netdev);
}

static inline struct zd_mac *zd_chip_to_mac(struct zd_chip *chip)
{
	return container_of(chip, struct zd_mac, chip);
}

static inline struct zd_mac *zd_usb_to_mac(struct zd_usb *usb)
{
	return zd_chip_to_mac(zd_usb_to_chip(usb));
}

#define zd_mac_dev(mac) (zd_chip_dev(&(mac)->chip))

int zd_mac_init(struct zd_mac *mac,
                struct net_device *netdev,
		struct usb_interface *intf);
void zd_mac_clear(struct zd_mac *mac);

int zd_mac_preinit_hw(struct zd_mac *mac);
int zd_mac_init_hw(struct zd_mac *mac);

int zd_mac_open(struct net_device *netdev);
int zd_mac_stop(struct net_device *netdev);
int zd_mac_set_mac_address(struct net_device *dev, void *p);
void zd_mac_set_multicast_list(struct net_device *netdev);

int zd_mac_rx_irq(struct zd_mac *mac, const u8 *buffer, unsigned int length);

int zd_mac_set_regdomain(struct zd_mac *zd_mac, u8 regdomain);
u8 zd_mac_get_regdomain(struct zd_mac *zd_mac);

int zd_mac_request_channel(struct zd_mac *mac, u8 channel);
u8 zd_mac_get_channel(struct zd_mac *mac);

int zd_mac_set_mode(struct zd_mac *mac, u32 mode);
int zd_mac_get_mode(struct zd_mac *mac, u32 *mode);

int zd_mac_get_range(struct zd_mac *mac, struct iw_range *range);

struct iw_statistics *zd_mac_get_wireless_stats(struct net_device *ndev);

#ifdef DEBUG
void zd_dump_rx_status(const struct rx_status *status);
#else
#define zd_dump_rx_status(status)
#endif /* DEBUG */

#endif /* _ZD_MAC_H */
