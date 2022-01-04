/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/* p80211conv.h
 *
 * Ether/802.11 conversions and packet buffer routines
 *
 * Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * --------------------------------------------------------------------
 *
 * linux-wlan
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the linux-wlan Open Source project can be
 * made directly to:
 *
 * AbsoluteValue Systems Inc.
 * info@linux-wlan.com
 * http://www.linux-wlan.com
 *
 * --------------------------------------------------------------------
 *
 * Portions of the development of this software were funded by
 * Intersil Corporation as part of PRISM(R) chipset product development.
 *
 * --------------------------------------------------------------------
 *
 * This file declares the functions, types and macros that perform
 * Ethernet to/from 802.11 frame conversions.
 *
 * --------------------------------------------------------------------
 */

#ifndef _LINUX_P80211CONV_H
#define _LINUX_P80211CONV_H

#define WLAN_IEEE_OUI_LEN	3

#define WLAN_ETHCONV_ENCAP	1
#define WLAN_ETHCONV_8021h	3

#define P80211CAPTURE_VERSION	0x80211001

#define	P80211_FRMMETA_MAGIC	0x802110

struct p80211_rxmeta {
	struct wlandevice *wlandev;

	u64 mactime;		/* Hi-rez MAC-supplied time value */
	u64 hosttime;		/* Best-rez host supplied time value */

	unsigned int rxrate;	/* Receive data rate in 100kbps */
	unsigned int priority;	/* 0-15, 0=contention, 6=CF */
	int signal;		/* An SSI, see p80211netdev.h */
	int noise;		/* An SSI, see p80211netdev.h */
	unsigned int channel;	/* Receive channel (mostly for snifs) */
	unsigned int preamble;	/* P80211ENUM_preambletype_* */
	unsigned int encoding;	/* P80211ENUM_encoding_* */

};

struct p80211_frmmeta {
	unsigned int magic;
	struct p80211_rxmeta *rx;
};

void p80211skb_free(struct wlandevice *wlandev, struct sk_buff *skb);
int p80211skb_rxmeta_attach(struct wlandevice *wlandev, struct sk_buff *skb);
void p80211skb_rxmeta_detach(struct sk_buff *skb);

static inline struct p80211_frmmeta *p80211skb_frmmeta(struct sk_buff *skb)
{
	struct p80211_frmmeta *frmmeta = (struct p80211_frmmeta *)skb->cb;

	return frmmeta->magic == P80211_FRMMETA_MAGIC ? frmmeta : NULL;
}

static inline struct p80211_rxmeta *p80211skb_rxmeta(struct sk_buff *skb)
{
	struct p80211_frmmeta *frmmeta = p80211skb_frmmeta(skb);

	return frmmeta ? frmmeta->rx : NULL;
}

/*
 * Frame capture header.  (See doc/capturefrm.txt)
 */
struct p80211_caphdr {
	__be32 version;
	__be32 length;
	__be64 mactime;
	__be64 hosttime;
	__be32 phytype;
	__be32 channel;
	__be32 datarate;
	__be32 antenna;
	__be32 priority;
	__be32 ssi_type;
	__be32 ssi_signal;
	__be32 ssi_noise;
	__be32 preamble;
	__be32 encoding;
};

struct p80211_metawep {
	void *data;
	u8 iv[4];
	u8 icv[4];
};

/* local ether header type */
struct wlan_ethhdr {
	u8 daddr[ETH_ALEN];
	u8 saddr[ETH_ALEN];
	__be16 type;
} __packed;

/* local llc header type */
struct wlan_llc {
	u8 dsap;
	u8 ssap;
	u8 ctl;
} __packed;

/* local snap header type */
struct wlan_snap {
	u8 oui[WLAN_IEEE_OUI_LEN];
	__be16 type;
} __packed;

/* Circular include trick */
struct wlandevice;

int skb_p80211_to_ether(struct wlandevice *wlandev, u32 ethconv,
			struct sk_buff *skb);
int skb_ether_to_p80211(struct wlandevice *wlandev, u32 ethconv,
			struct sk_buff *skb, struct p80211_hdr *p80211_hdr,
			struct p80211_metawep *p80211_wep);

int p80211_stt_findproto(u16 proto);

#endif
