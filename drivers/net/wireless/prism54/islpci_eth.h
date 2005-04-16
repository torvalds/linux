/*
 *  
 *  Copyright (C) 2002 Intersil Americas Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ISLPCI_ETH_H
#define _ISLPCI_ETH_H

#include "isl_38xx.h"
#include "islpci_dev.h"

struct rfmon_header {
	u16 unk0;		/* = 0x0000 */
	u16 length;		/* = 0x1400 */
	u32 clock;		/* 1MHz clock */
	u8 flags;
	u8 unk1;
	u8 rate;
	u8 unk2;
	u16 freq;
	u16 unk3;
	u8 rssi;
	u8 padding[3];
} __attribute__ ((packed));

struct rx_annex_header {
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	struct rfmon_header rfmon;
} __attribute__ ((packed));

/* wlan-ng (and hopefully others) AVS header, version one.  Fields in
 * network byte order. */
#define P80211CAPTURE_VERSION 0x80211001

struct avs_80211_1_header {
	uint32_t version;
	uint32_t length;
	uint64_t mactime;
	uint64_t hosttime;
	uint32_t phytype;
	uint32_t channel;
	uint32_t datarate;
	uint32_t antenna;
	uint32_t priority;
	uint32_t ssi_type;
	int32_t ssi_signal;
	int32_t ssi_noise;
	uint32_t preamble;
	uint32_t encoding;
};

void islpci_eth_cleanup_transmit(islpci_private *, isl38xx_control_block *);
int islpci_eth_transmit(struct sk_buff *, struct net_device *);
int islpci_eth_receive(islpci_private *);
void islpci_eth_tx_timeout(struct net_device *);
void islpci_do_reset_and_wake(void *data);

#endif				/* _ISL_GEN_H */
