/*
 * cxgb4_ptp.c:Chelsio PTP support for T5/T6
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
 *
 * Written by: Atul Gupta (atul.gupta@chelsio.com)
 */

#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/pps_kernel.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_classify.h>
#include <linux/udp.h>

#include "cxgb4.h"
#include "t4_hw.h"
#include "t4_regs.h"
#include "t4_msg.h"
#include "t4fw_api.h"
#include "cxgb4_ptp.h"

/**
 * cxgb4_ptp_is_ptp_tx - determine whether TX packet is PTP or not
 * @skb: skb of outgoing ptp request
 *
 */
bool cxgb4_ptp_is_ptp_tx(struct sk_buff *skb)
{
	struct udphdr *uh;

	uh = udp_hdr(skb);
	return skb->len >= PTP_MIN_LENGTH &&
		skb->len <= PTP_IN_TRANSMIT_PACKET_MAXNUM &&
		likely(skb->protocol == htons(ETH_P_IP)) &&
		ip_hdr(skb)->protocol == IPPROTO_UDP &&
		uh->dest == htons(PTP_EVENT_PORT);
}

bool is_ptp_enabled(struct sk_buff *skb, struct net_device *dev)
{
	struct port_info *pi;

	pi = netdev_priv(dev);
	return (pi->ptp_enable && cxgb4_xmit_with_hwtstamp(skb) &&
		cxgb4_ptp_is_ptp_tx(skb));
}

/**
 * cxgb4_ptp_is_ptp_rx - determine whether RX packet is PTP or not
 * @skb: skb of incoming ptp request
 *
 */
bool cxgb4_ptp_is_ptp_rx(struct sk_buff *skb)
{
	struct udphdr *uh = (struct udphdr *)(skb->data + ETH_HLEN +
					      IPV4_HLEN(skb->data));

	return  uh->dest == htons(PTP_EVENT_PORT) &&
		uh->source == htons(PTP_EVENT_PORT);
}

/**
 * cxgb4_ptp_read_hwstamp - read timestamp for TX event PTP message
 * @adapter: board private structure
 * @pi: port private structure
 *
 */
void cxgb4_ptp_read_hwstamp(struct adapter *adapter, struct port_info *pi)
{
	struct skb_shared_hwtstamps *skb_ts = NULL;
	u64 tx_ts;

	skb_ts = skb_hwtstamps(adapter->ptp_tx_skb);

	tx_ts = t4_read_reg(adapter,
			    T5_PORT_REG(pi->port_id, MAC_PORT_TX_TS_VAL_LO));

	tx_ts |= (u64)t4_read_reg(adapter,
				  T5_PORT_REG(pi->port_id,
					      MAC_PORT_TX_TS_VAL_HI)) << 32;
	skb_ts->hwtstamp = ns_to_ktime(tx_ts);
	skb_tstamp_tx(adapter->ptp_tx_skb, skb_ts);
	dev_kfree_skb_any(adapter->ptp_tx_skb);
	spin_lock(&adapter->ptp_lock);
	adapter->ptp_tx_skb = NULL;
	spin_unlock(&adapter->ptp_lock);
}

/**
 * cxgb4_ptprx_timestamping - Enable Timestamp for RX PTP event message
 * @pi: port private structure
 * @port: pot number
 * @mode: RX mode
 *
 */
int cxgb4_ptprx_timestamping(struct port_info *pi, u8 port, u16 mode)
{
	struct adapter *adapter = pi->adapter;
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(port));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.init.sc = FW_PTP_SC_RXTIME_STAMP;
	c.u.init.mode = cpu_to_be16(mode);

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);
	return err;
}

int cxgb4_ptp_txtype(struct adapter *adapter, u8 port)
{
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(port));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.init.sc = FW_PTP_SC_TX_TYPE;
	c.u.init.mode = cpu_to_be16(PTP_TS_NONE);

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);

	return err;
}

int cxgb4_ptp_redirect_rx_packet(struct adapter *adapter, struct port_info *pi)
{
	struct sge *s = &adapter->sge;
	struct sge_eth_rxq *receive_q =  &s->ethrxq[pi->first_qset];
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(pi->port_id));

	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.init.sc = FW_PTP_SC_RDRX_TYPE;
	c.u.init.txchan = pi->tx_chan;
	c.u.init.absid = cpu_to_be16(receive_q->rspq.abs_id);

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);
	return err;
}
