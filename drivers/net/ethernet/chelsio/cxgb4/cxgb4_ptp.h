/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2017 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CXGB4_PTP_H__
#define __CXGB4_PTP_H__

/* Maximum parts-per-billion adjustment that is acceptable */
#define MAX_PTP_FREQ_ADJ		1000000
#define PTP_CLOCK_MAX_ADJTIME		10000000 /* 10 ms */

#define PTP_MIN_LENGTH			63
#define PTP_IN_TRANSMIT_PACKET_MAXNUM	240
#define PTP_EVENT_PORT			319

enum ptp_rx_filter_mode {
	PTP_TS_NONE = 0,
	PTP_TS_L2,
	PTP_TS_L4,
	PTP_TS_L2_L4
};

struct port_info;

static inline bool cxgb4_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;
}

static inline void cxgb4_xmit_hwtstamp_pending(struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}

void cxgb4_ptp_init(struct adapter *adap);
void cxgb4_ptp_stop(struct adapter *adap);
bool cxgb4_ptp_is_ptp_tx(struct sk_buff *skb);
bool cxgb4_ptp_is_ptp_rx(struct sk_buff *skb);
int cxgb4_ptprx_timestamping(struct port_info *pi, u8 port, u16 mode);
int cxgb4_ptp_redirect_rx_packet(struct adapter *adap, struct port_info *pi);
int cxgb4_ptp_txtype(struct adapter *adap, u8 port_id);
void cxgb4_ptp_read_hwstamp(struct adapter *adap, struct port_info *pi);
bool is_ptp_enabled(struct sk_buff *skb, struct net_device *dev);
#endif /* __CXGB4_PTP_H__ */
