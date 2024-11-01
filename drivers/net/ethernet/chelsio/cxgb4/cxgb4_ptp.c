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

/**
 * cxgb4_ptp_adjfreq - Adjust frequency of PHC cycle counter
 * @ptp: ptp clock structure
 * @ppb: Desired frequency change in parts per billion
 *
 * Adjust the frequency of the PHC cycle counter by the indicated ppb from
 * the base frequency.
 */
static int cxgb4_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct adapter *adapter = (struct adapter *)container_of(ptp,
				   struct adapter, ptp_clock_info);
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(0));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.ts.sc = FW_PTP_SC_ADJ_FREQ;
	c.u.ts.sign = (ppb < 0) ? 1 : 0;
	if (ppb < 0)
		ppb = -ppb;
	c.u.ts.ppb = cpu_to_be32(ppb);

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);

	return err;
}

/**
 * cxgb4_ptp_fineadjtime - Shift the time of the hardware clock
 * @adapter: board private structure
 * @delta: Desired change in nanoseconds
 *
 * Adjust the timer by resetting the timecounter structure.
 */
static int  cxgb4_ptp_fineadjtime(struct adapter *adapter, s64 delta)
{
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
			     FW_CMD_REQUEST_F |
			     FW_CMD_WRITE_F |
			     FW_PTP_CMD_PORTID_V(0));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.ts.sc = FW_PTP_SC_ADJ_FTIME;
	c.u.ts.sign = (delta < 0) ? 1 : 0;
	if (delta < 0)
		delta = -delta;
	c.u.ts.tm = cpu_to_be64(delta);

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);
	return err;
}

/**
 * cxgb4_ptp_adjtime - Shift the time of the hardware clock
 * @ptp: ptp clock structure
 * @delta: Desired change in nanoseconds
 *
 * Adjust the timer by resetting the timecounter structure.
 */
static int cxgb4_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct adapter *adapter =
		(struct adapter *)container_of(ptp, struct adapter,
					       ptp_clock_info);
	struct fw_ptp_cmd c;
	s64 sign = 1;
	int err;

	if (delta < 0)
		sign = -1;

	if (delta * sign > PTP_CLOCK_MAX_ADJTIME) {
		memset(&c, 0, sizeof(c));
		c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
					     FW_CMD_REQUEST_F |
					     FW_CMD_WRITE_F |
					     FW_PTP_CMD_PORTID_V(0));
		c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
		c.u.ts.sc = FW_PTP_SC_ADJ_TIME;
		c.u.ts.sign = (delta < 0) ? 1 : 0;
		if (delta < 0)
			delta = -delta;
		c.u.ts.tm = cpu_to_be64(delta);

		err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
		if (err < 0)
			dev_err(adapter->pdev_dev,
				"PTP: %s error %d\n", __func__, -err);
	} else {
		err = cxgb4_ptp_fineadjtime(adapter, delta);
	}

	return err;
}

/**
 * cxgb4_ptp_gettime - Reads the current time from the hardware clock
 * @ptp: ptp clock structure
 * @ts: timespec structure to hold the current time value
 *
 * Read the timecounter and return the correct value in ns after converting
 * it into a struct timespec.
 */
static int cxgb4_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct adapter *adapter = container_of(ptp, struct adapter,
					       ptp_clock_info);
	u64 ns;

	ns = t4_read_reg(adapter, T5_PORT_REG(0, MAC_PORT_PTP_SUM_LO_A));
	ns |= (u64)t4_read_reg(adapter,
			       T5_PORT_REG(0, MAC_PORT_PTP_SUM_HI_A)) << 32;

	/* convert to timespec*/
	*ts = ns_to_timespec64(ns);
	return 0;
}

/**
 *  cxgb4_ptp_settime - Set the current time on the hardware clock
 *  @ptp: ptp clock structure
 *  @ts: timespec containing the new time for the cycle counter
 *
 *  Reset value to new base value instead of the kernel
 *  wall timer value.
 */
static int cxgb4_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct adapter *adapter = (struct adapter *)container_of(ptp,
				   struct adapter, ptp_clock_info);
	struct fw_ptp_cmd c;
	u64 ns;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(0));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.ts.sc = FW_PTP_SC_SET_TIME;

	ns = timespec64_to_ns(ts);
	c.u.ts.tm = cpu_to_be64(ns);

	err =  t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);

	return err;
}

static void cxgb4_init_ptp_timer(struct adapter *adapter)
{
	struct fw_ptp_cmd c;
	int err;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PTP_CMD) |
				     FW_CMD_REQUEST_F |
				     FW_CMD_WRITE_F |
				     FW_PTP_CMD_PORTID_V(0));
	c.retval_len16 = cpu_to_be32(FW_CMD_LEN16_V(sizeof(c) / 16));
	c.u.scmd.sc = FW_PTP_SC_INIT_TIMER;

	err = t4_wr_mbox(adapter, adapter->mbox, &c, sizeof(c), NULL);
	if (err < 0)
		dev_err(adapter->pdev_dev,
			"PTP: %s error %d\n", __func__, -err);
}

/**
 * cxgb4_ptp_enable - enable or disable an ancillary feature
 * @ptp: ptp clock structure
 * @request: Desired resource to enable or disable
 * @on: Caller passes one to enable or zero to disable
 *
 * Enable (or disable) ancillary features of the PHC subsystem.
 * Currently, no ancillary features are supported.
 */
static int cxgb4_ptp_enable(struct ptp_clock_info __always_unused *ptp,
			    struct ptp_clock_request __always_unused *request,
			    int __always_unused on)
{
	return -ENOTSUPP;
}

static const struct ptp_clock_info cxgb4_ptp_clock_info = {
	.owner          = THIS_MODULE,
	.name           = "cxgb4_clock",
	.max_adj        = MAX_PTP_FREQ_ADJ,
	.n_alarm        = 0,
	.n_ext_ts       = 0,
	.n_per_out      = 0,
	.pps            = 0,
	.adjfreq        = cxgb4_ptp_adjfreq,
	.adjtime        = cxgb4_ptp_adjtime,
	.gettime64      = cxgb4_ptp_gettime,
	.settime64      = cxgb4_ptp_settime,
	.enable         = cxgb4_ptp_enable,
};

/**
 * cxgb4_ptp_init - initialize PTP for devices which support it
 * @adapter: board private structure
 *
 * This function performs the required steps for enabling PTP support.
 */
void cxgb4_ptp_init(struct adapter *adapter)
{
	struct timespec64 now;
	 /* no need to create a clock device if we already have one */
	if (!IS_ERR_OR_NULL(adapter->ptp_clock))
		return;

	adapter->ptp_tx_skb = NULL;
	adapter->ptp_clock_info = cxgb4_ptp_clock_info;
	spin_lock_init(&adapter->ptp_lock);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_clock_info,
						&adapter->pdev->dev);
	if (IS_ERR_OR_NULL(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		dev_err(adapter->pdev_dev,
			"PTP %s Clock registration has failed\n", __func__);
		return;
	}

	now = ktime_to_timespec64(ktime_get_real());
	cxgb4_init_ptp_timer(adapter);
	if (cxgb4_ptp_settime(&adapter->ptp_clock_info, &now) < 0) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
	}
}

/**
 * cxgb4_ptp_stop - disable PTP device and stop the overflow check
 * @adapter: board private structure
 *
 * Stop the PTP support.
 */
void cxgb4_ptp_stop(struct adapter *adapter)
{
	if (adapter->ptp_tx_skb) {
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
	}

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
	}
}
