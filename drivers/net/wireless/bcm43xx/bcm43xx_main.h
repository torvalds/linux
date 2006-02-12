/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#ifndef BCM43xx_MAIN_H_
#define BCM43xx_MAIN_H_

#include "bcm43xx.h"

#ifdef CONFIG_BCM947XX
#define atoi(str) simple_strtoul(((str != NULL) ? str : ""), NULL, 0)

static inline void e_aton(char *str, char *dest)
{
	int i = 0;
	u16 *d = (u16 *) dest;

	for (;;) {
		dest[i++] = (char) simple_strtoul(str, NULL, 16);
		str += 2;
		if (!*str++ || i == 6)
			break;
	}
	for (i = 0; i < 3; i++)
		d[i] = cpu_to_be16(d[i]);
}
#endif


#define _bcm43xx_declare_plcp_hdr(size) \
	struct bcm43xx_plcp_hdr##size {		\
		union {				\
			__le32 data;		\
			__u8 raw[size];		\
		} __attribute__((__packed__));	\
	} __attribute__((__packed__))

/* struct bcm43xx_plcp_hdr4 */
_bcm43xx_declare_plcp_hdr(4);
/* struct bcm430c_plcp_hdr6 */
_bcm43xx_declare_plcp_hdr(6);

#undef _bcm43xx_declare_plcp_hdr


#define P4D_BYT3S(magic, nr_bytes)	u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)	P4D_BYT3S(line, nr_bytes)
/* Magic helper macro to pad structures. Ignore those above. It's magic. */
#define PAD_BYTES(nr_bytes)		P4D_BYTES( __LINE__ , (nr_bytes))


/* Device specific TX header. To be prepended to TX frames. */
struct bcm43xx_txhdr {
	union {
		struct {
			u16 flags;
			u16 wsec_rate;
			u16 frame_control;
			u16 unknown_zeroed_0;
			u16 control;
			unsigned char wep_iv[10];
			unsigned char unknown_wsec_tkip_data[3]; //FIXME
			PAD_BYTES(3);
			unsigned char mac1[6];
			u16 unknown_zeroed_1;
			struct bcm43xx_plcp_hdr4 rts_cts_fallback_plcp;
			u16 rts_cts_dur_fallback;
			struct bcm43xx_plcp_hdr4 fallback_plcp;
			u16 fallback_dur_id;
			PAD_BYTES(2);
			u16 cookie;
			u16 unknown_scb_stuff; //FIXME
			struct bcm43xx_plcp_hdr6 rts_cts_plcp;
			u16 rts_cts_frame_type;
			u16 rts_cts_dur;
			unsigned char rts_cts_mac1[6];
			unsigned char rts_cts_mac2[6];
			PAD_BYTES(2);
			struct bcm43xx_plcp_hdr6 plcp;
		} __attribute__((__packed__));

		unsigned char raw[82];
	} __attribute__((__packed__));
} __attribute__((__packed__));

struct sk_buff;

void bcm43xx_generate_txhdr(struct bcm43xx_private *bcm,
			    struct bcm43xx_txhdr *txhdr,
			    const unsigned char *fragment_data,
			    const unsigned int fragment_len,
			    const int is_first_fragment,
			    const u16 cookie);

/* RX header as received from the hardware. */
struct bcm43xx_rxhdr {
	/* Frame Length. Must be generated explicitely in PIO mode. */
	__le16 frame_length;
	PAD_BYTES(2);
	/* Flags field 1 */
	__le16 flags1;
	u8 rssi;
	u8 signal_quality;
	PAD_BYTES(2);
	/* Flags field 3 */
	__le16 flags3;
	/* Flags field 2 */
	__le16 flags2;
	/* Lower 16bits of the TSF at the time the frame started. */
	__le16 mactime;
	PAD_BYTES(14);
} __attribute__((__packed__));

#define BCM43xx_RXHDR_FLAGS1_OFDM		(1 << 0)
/*#define BCM43xx_RXHDR_FLAGS1_SIGNAL???	(1 << 3) FIXME */
#define BCM43xx_RXHDR_FLAGS1_SHORTPREAMBLE	(1 << 7)
#define BCM43xx_RXHDR_FLAGS1_2053RSSIADJ	(1 << 14)

#define BCM43xx_RXHDR_FLAGS2_INVALIDFRAME	(1 << 0)
#define BCM43xx_RXHDR_FLAGS2_TYPE2FRAME		(1 << 2)
/*FIXME: WEP related flags */

#define BCM43xx_RXHDR_FLAGS3_2050RSSIADJ	(1 << 10)

/* Transmit Status as received from the hardware. */
struct bcm43xx_hwxmitstatus {
	PAD_BYTES(4);
	__le16 cookie;
	u8 flags;
	u8 cnt1:4,
	   cnt2:4;
	PAD_BYTES(2);
	__le16 seq;
	__le16 unknown; //FIXME
} __attribute__((__packed__));

/* Transmit Status in CPU byteorder. */
struct bcm43xx_xmitstatus {
	u16 cookie;
	u8 flags;
	u8 cnt1:4,
	   cnt2:4;
	u16 seq;
	u16 unknown; //FIXME
};

#define BCM43xx_TXSTAT_FLAG_ACK		0x01
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x02
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x04
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x08
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x10
#define BCM43xx_TXSTAT_FLAG_IGNORE	0x20
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x40
//TODO #define BCM43xx_TXSTAT_FLAG_???	0x80

struct bcm43xx_xmitstatus_queue {
	struct list_head list;
	struct bcm43xx_hwxmitstatus status;
};


/* Lightweight function to convert a frequency (in Mhz) to a channel number. */
static inline
u8 bcm43xx_freq_to_channel(struct bcm43xx_private *bcm,
			   int freq)
{
	u8 channel;

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A) {
		channel = (freq - 5000) / 5;
	} else {
		if (freq == 2484)
			channel = 14;
		else
			channel = (freq - 2407) / 5;
	}

	return channel;
}

/* Lightweight function to convert a channel number to a frequency (in Mhz). */
static inline
int bcm43xx_channel_to_freq(struct bcm43xx_private *bcm,
			    u8 channel)
{
	int freq;

	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A) {
		freq = 5000 + (5 * channel);
	} else {
		if (channel == 14)
			freq = 2484;
		else
			freq = 2407 + (5 * channel);
	}

	return freq;
}

/* Lightweight function to check if a channel number is valid.
 * Note that this does _NOT_ check for geographical restrictions!
 */
static inline
int bcm43xx_is_valid_channel(struct bcm43xx_private *bcm,
			    u8 channel)
{
	if (bcm->current_core->phy->type == BCM43xx_PHYTYPE_A) {
		if (channel <= 200)
			return 1;
	} else {
		if (channel >= 1 && channel <= 14)
			return 1;
	}

	return 0;
}

void bcm43xx_tsf_read(struct bcm43xx_private *bcm, u64 *tsf);
void bcm43xx_tsf_write(struct bcm43xx_private *bcm, u64 tsf);

int bcm43xx_rx(struct bcm43xx_private *bcm,
	       struct sk_buff *skb,
	       struct bcm43xx_rxhdr *rxhdr);

void bcm43xx_set_iwmode(struct bcm43xx_private *bcm,
			int iw_mode);

u32 bcm43xx_shm_read32(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset);
u16 bcm43xx_shm_read16(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset);
void bcm43xx_shm_write32(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u32 value);
void bcm43xx_shm_write16(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u16 value);

void bcm43xx_dummy_transmission(struct bcm43xx_private *bcm);

int bcm43xx_switch_core(struct bcm43xx_private *bcm, struct bcm43xx_coreinfo *new_core);

void bcm43xx_wireless_core_reset(struct bcm43xx_private *bcm, int connect_phy);

int bcm43xx_pci_read_config_16(struct pci_dev *pdev, u16 offset, u16 *val);
int bcm43xx_pci_read_config_32(struct pci_dev *pdev, u16 offset, u32 *val);
int bcm43xx_pci_write_config_16(struct pci_dev *pdev, int offset, u16 val);
int bcm43xx_pci_write_config_32(struct pci_dev *pdev, int offset, u32 val);

void bcm43xx_mac_suspend(struct bcm43xx_private *bcm);
void bcm43xx_mac_enable(struct bcm43xx_private *bcm);

u8 bcm43xx_sprom_crc(const u16 *sprom);

void bcm43xx_controller_restart(struct bcm43xx_private *bcm, const char *reason);

#endif /* BCM43xx_MAIN_H_ */
