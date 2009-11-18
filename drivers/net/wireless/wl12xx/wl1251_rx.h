/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1251_RX_H__
#define __WL1251_RX_H__

#include <linux/bitops.h>

#include "wl1251.h"

/*
 * RX PATH
 *
 * The Rx path uses a double buffer and an rx_contro structure, each located
 * at a fixed address in the device memory. The host keeps track of which
 * buffer is available and alternates between them on a per packet basis.
 * The size of each of the two buffers is large enough to hold the longest
 * 802.3 packet.
 * The RX path goes like that:
 * 1) The target generates an interrupt each time a new packet is received.
 *   There are 2 RX interrupts, one for each buffer.
 * 2) The host reads the received packet from one of the double buffers.
 * 3) The host triggers a target interrupt.
 * 4) The target prepares the next RX packet.
 */

#define WL1251_RX_MAX_RSSI -30
#define WL1251_RX_MIN_RSSI -95

#define WL1251_RX_ALIGN_TO 4
#define WL1251_RX_ALIGN(len) (((len) + WL1251_RX_ALIGN_TO - 1) & \
			     ~(WL1251_RX_ALIGN_TO - 1))

#define SHORT_PREAMBLE_BIT   BIT(0)
#define OFDM_RATE_BIT        BIT(6)
#define PBCC_RATE_BIT        BIT(7)

#define PLCP_HEADER_LENGTH 8
#define RX_DESC_PACKETID_SHIFT 11
#define RX_MAX_PACKET_ID 3

#define RX_DESC_VALID_FCS         0x0001
#define RX_DESC_MATCH_RXADDR1     0x0002
#define RX_DESC_MCAST             0x0004
#define RX_DESC_STAINTIM          0x0008
#define RX_DESC_VIRTUAL_BM        0x0010
#define RX_DESC_BCAST             0x0020
#define RX_DESC_MATCH_SSID        0x0040
#define RX_DESC_MATCH_BSSID       0x0080
#define RX_DESC_ENCRYPTION_MASK   0x0300
#define RX_DESC_MEASURMENT        0x0400
#define RX_DESC_SEQNUM_MASK       0x1800
#define	RX_DESC_MIC_FAIL	  0x2000
#define	RX_DESC_DECRYPT_FAIL	  0x4000

struct wl1251_rx_descriptor {
	u32 timestamp; /* In microseconds */
	u16 length; /* Paylod length, including headers */
	u16 flags;

	/*
	 * 0 - 802.11
	 * 1 - 802.3
	 * 2 - IP
	 * 3 - Raw Codec
	 */
	u8 type;

	/*
	 * Received Rate:
	 * 0x0A - 1MBPS
	 * 0x14 - 2MBPS
	 * 0x37 - 5_5MBPS
	 * 0x0B - 6MBPS
	 * 0x0F - 9MBPS
	 * 0x6E - 11MBPS
	 * 0x0A - 12MBPS
	 * 0x0E - 18MBPS
	 * 0xDC - 22MBPS
	 * 0x09 - 24MBPS
	 * 0x0D - 36MBPS
	 * 0x08 - 48MBPS
	 * 0x0C - 54MBPS
	 */
	u8 rate;

	u8 mod_pre; /* Modulation and preamble */
	u8 channel;

	/*
	 * 0 - 2.4 Ghz
	 * 1 - 5 Ghz
	 */
	u8 band;

	s8 rssi; /* in dB */
	u8 rcpi; /* in dB */
	u8 snr; /* in dB */
} __attribute__ ((packed));

void wl1251_rx(struct wl1251 *wl);

#endif
