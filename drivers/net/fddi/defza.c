// SPDX-License-Identifier: GPL-2.0+
/*	FDDI network adapter driver for DEC FDDIcontroller 700/700-C devices.
 *
 *	Copyright (c) 2018  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	References:
 *
 *	Dave Sawyer & Phil Weeks & Frank Itkowsky,
 *	"DEC FDDIcontroller 700 Port Specification",
 *	Revision 1.1, Digital Equipment Corporation
 */

/* ------------------------------------------------------------------------- */
/* FZA configurable parameters.                                              */

/* The number of transmit ring descriptors; either 0 for 512 or 1 for 1024.  */
#define FZA_RING_TX_MODE 0

/* The number of receive ring descriptors; from 2 up to 256.  */
#define FZA_RING_RX_SIZE 256

/* End of FZA configurable parameters.  No need to change anything below.    */
/* ------------------------------------------------------------------------- */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/fddidevice.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/tc.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/barrier.h>

#include "defza.h"

#define DRV_NAME "defza"
#define DRV_VERSION "v.1.1.4"
#define DRV_RELDATE "Oct  6 2018"

static const char version[] =
	DRV_NAME ": " DRV_VERSION "  " DRV_RELDATE "  Maciej W. Rozycki\n";

MODULE_AUTHOR("Maciej W. Rozycki <macro@orcam.me.uk>");
MODULE_DESCRIPTION("DEC FDDIcontroller 700 (DEFZA-xx) driver");
MODULE_LICENSE("GPL");

static int loopback;
module_param(loopback, int, 0644);

/* Ring Purger Multicast */
static u8 hw_addr_purger[8] = { 0x09, 0x00, 0x2b, 0x02, 0x01, 0x05 };
/* Directed Beacon Multicast */
static u8 hw_addr_beacon[8] = { 0x01, 0x80, 0xc2, 0x00, 0x01, 0x00 };

/* Shorthands for MMIO accesses that we require to be strongly ordered
 * WRT preceding MMIO accesses.
 */
#define readw_o readw_relaxed
#define readl_o readl_relaxed

#define writew_o writew_relaxed
#define writel_o writel_relaxed

/* Shorthands for MMIO accesses that we are happy with being weakly ordered
 * WRT preceding MMIO accesses.
 */
#define readw_u readw_relaxed
#define readl_u readl_relaxed
#define readq_u readq_relaxed

#define writew_u writew_relaxed
#define writel_u writel_relaxed
#define writeq_u writeq_relaxed

static inline struct sk_buff *fza_alloc_skb_irq(struct net_device *dev,
						unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}

static inline struct sk_buff *fza_alloc_skb(struct net_device *dev,
					    unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_KERNEL);
}

static inline void fza_skb_align(struct sk_buff *skb, unsigned int v)
{
	unsigned long x, y;

	x = (unsigned long)skb->data;
	y = ALIGN(x, v);

	skb_reserve(skb, y - x);
}

static inline void fza_reads(const void __iomem *from, void *to,
			     unsigned long size)
{
	if (sizeof(unsigned long) == 8) {
		const u64 __iomem *src = from;
		const u32 __iomem *src_trail;
		u64 *dst = to;
		u32 *dst_trail;

		for (size = (size + 3) / 4; size > 1; size -= 2)
			*dst++ = readq_u(src++);
		if (size) {
			src_trail = (u32 __iomem *)src;
			dst_trail = (u32 *)dst;
			*dst_trail = readl_u(src_trail);
		}
	} else {
		const u32 __iomem *src = from;
		u32 *dst = to;

		for (size = (size + 3) / 4; size; size--)
			*dst++ = readl_u(src++);
	}
}

static inline void fza_writes(const void *from, void __iomem *to,
			      unsigned long size)
{
	if (sizeof(unsigned long) == 8) {
		const u64 *src = from;
		const u32 *src_trail;
		u64 __iomem *dst = to;
		u32 __iomem *dst_trail;

		for (size = (size + 3) / 4; size > 1; size -= 2)
			writeq_u(*src++, dst++);
		if (size) {
			src_trail = (u32 *)src;
			dst_trail = (u32 __iomem *)dst;
			writel_u(*src_trail, dst_trail);
		}
	} else {
		const u32 *src = from;
		u32 __iomem *dst = to;

		for (size = (size + 3) / 4; size; size--)
			writel_u(*src++, dst++);
	}
}

static inline void fza_moves(const void __iomem *from, void __iomem *to,
			     unsigned long size)
{
	if (sizeof(unsigned long) == 8) {
		const u64 __iomem *src = from;
		const u32 __iomem *src_trail;
		u64 __iomem *dst = to;
		u32 __iomem *dst_trail;

		for (size = (size + 3) / 4; size > 1; size -= 2)
			writeq_u(readq_u(src++), dst++);
		if (size) {
			src_trail = (u32 __iomem *)src;
			dst_trail = (u32 __iomem *)dst;
			writel_u(readl_u(src_trail), dst_trail);
		}
	} else {
		const u32 __iomem *src = from;
		u32 __iomem *dst = to;

		for (size = (size + 3) / 4; size; size--)
			writel_u(readl_u(src++), dst++);
	}
}

static inline void fza_zeros(void __iomem *to, unsigned long size)
{
	if (sizeof(unsigned long) == 8) {
		u64 __iomem *dst = to;
		u32 __iomem *dst_trail;

		for (size = (size + 3) / 4; size > 1; size -= 2)
			writeq_u(0, dst++);
		if (size) {
			dst_trail = (u32 __iomem *)dst;
			writel_u(0, dst_trail);
		}
	} else {
		u32 __iomem *dst = to;

		for (size = (size + 3) / 4; size; size--)
			writel_u(0, dst++);
	}
}

static inline void fza_regs_dump(struct fza_private *fp)
{
	pr_debug("%s: iomem registers:\n", fp->name);
	pr_debug(" reset:           0x%04x\n", readw_o(&fp->regs->reset));
	pr_debug(" interrupt event: 0x%04x\n", readw_u(&fp->regs->int_event));
	pr_debug(" status:          0x%04x\n", readw_u(&fp->regs->status));
	pr_debug(" interrupt mask:  0x%04x\n", readw_u(&fp->regs->int_mask));
	pr_debug(" control A:       0x%04x\n", readw_u(&fp->regs->control_a));
	pr_debug(" control B:       0x%04x\n", readw_u(&fp->regs->control_b));
}

static inline void fza_do_reset(struct fza_private *fp)
{
	/* Reset the board. */
	writew_o(FZA_RESET_INIT, &fp->regs->reset);
	readw_o(&fp->regs->reset);	/* Synchronize. */
	readw_o(&fp->regs->reset);	/* Read it back for a small delay. */
	writew_o(FZA_RESET_CLR, &fp->regs->reset);

	/* Enable all interrupt events we handle. */
	writew_o(fp->int_mask, &fp->regs->int_mask);
	readw_o(&fp->regs->int_mask);	/* Synchronize. */
}

static inline void fza_do_shutdown(struct fza_private *fp)
{
	/* Disable the driver mode. */
	writew_o(FZA_CONTROL_B_IDLE, &fp->regs->control_b);

	/* And reset the board. */
	writew_o(FZA_RESET_INIT, &fp->regs->reset);
	readw_o(&fp->regs->reset);	/* Synchronize. */
	writew_o(FZA_RESET_CLR, &fp->regs->reset);
	readw_o(&fp->regs->reset);	/* Synchronize. */
}

static int fza_reset(struct fza_private *fp)
{
	unsigned long flags;
	uint status, state;
	long t;

	pr_info("%s: resetting the board...\n", fp->name);

	spin_lock_irqsave(&fp->lock, flags);
	fp->state_chg_flag = 0;
	fza_do_reset(fp);
	spin_unlock_irqrestore(&fp->lock, flags);

	/* DEC says RESET needs up to 30 seconds to complete.  My DEFZA-AA
	 * rev. C03 happily finishes in 9.7 seconds. :-)  But we need to
	 * be on the safe side...
	 */
	t = wait_event_timeout(fp->state_chg_wait, fp->state_chg_flag,
			       45 * HZ);
	status = readw_u(&fp->regs->status);
	state = FZA_STATUS_GET_STATE(status);
	if (fp->state_chg_flag == 0) {
		pr_err("%s: RESET timed out!, state %x\n", fp->name, state);
		return -EIO;
	}
	if (state != FZA_STATE_UNINITIALIZED) {
		pr_err("%s: RESET failed!, state %x, failure ID %x\n",
		       fp->name, state, FZA_STATUS_GET_TEST(status));
		return -EIO;
	}
	pr_info("%s: OK\n", fp->name);
	pr_debug("%s: RESET: %lums elapsed\n", fp->name,
		 (45 * HZ - t) * 1000 / HZ);

	return 0;
}

static struct fza_ring_cmd __iomem *fza_cmd_send(struct net_device *dev,
						 int command)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_ring_cmd __iomem *ring = fp->ring_cmd + fp->ring_cmd_index;
	unsigned int old_mask, new_mask;
	union fza_cmd_buf __iomem *buf;
	struct netdev_hw_addr *ha;
	int i;

	old_mask = fp->int_mask;
	new_mask = old_mask & ~FZA_MASK_STATE_CHG;
	writew_u(new_mask, &fp->regs->int_mask);
	readw_o(&fp->regs->int_mask);			/* Synchronize. */
	fp->int_mask = new_mask;

	buf = fp->mmio + readl_u(&ring->buffer);

	if ((readl_u(&ring->cmd_own) & FZA_RING_OWN_MASK) !=
	    FZA_RING_OWN_HOST) {
		pr_warn("%s: command buffer full, command: %u!\n", fp->name,
			command);
		return NULL;
	}

	switch (command) {
	case FZA_RING_CMD_INIT:
		writel_u(FZA_RING_TX_MODE, &buf->init.tx_mode);
		writel_u(FZA_RING_RX_SIZE, &buf->init.hst_rx_size);
		fza_zeros(&buf->init.counters, sizeof(buf->init.counters));
		break;

	case FZA_RING_CMD_MODCAM:
		i = 0;
		fza_writes(&hw_addr_purger, &buf->cam.hw_addr[i++],
			   sizeof(*buf->cam.hw_addr));
		fza_writes(&hw_addr_beacon, &buf->cam.hw_addr[i++],
			   sizeof(*buf->cam.hw_addr));
		netdev_for_each_mc_addr(ha, dev) {
			if (i >= FZA_CMD_CAM_SIZE)
				break;
			fza_writes(ha->addr, &buf->cam.hw_addr[i++],
				   sizeof(*buf->cam.hw_addr));
		}
		while (i < FZA_CMD_CAM_SIZE)
			fza_zeros(&buf->cam.hw_addr[i++],
				  sizeof(*buf->cam.hw_addr));
		break;

	case FZA_RING_CMD_PARAM:
		writel_u(loopback, &buf->param.loop_mode);
		writel_u(fp->t_max, &buf->param.t_max);
		writel_u(fp->t_req, &buf->param.t_req);
		writel_u(fp->tvx, &buf->param.tvx);
		writel_u(fp->lem_threshold, &buf->param.lem_threshold);
		fza_writes(&fp->station_id, &buf->param.station_id,
			   sizeof(buf->param.station_id));
		/* Convert to milliseconds due to buggy firmware. */
		writel_u(fp->rtoken_timeout / 12500,
			 &buf->param.rtoken_timeout);
		writel_u(fp->ring_purger, &buf->param.ring_purger);
		break;

	case FZA_RING_CMD_MODPROM:
		if (dev->flags & IFF_PROMISC) {
			writel_u(1, &buf->modprom.llc_prom);
			writel_u(1, &buf->modprom.smt_prom);
		} else {
			writel_u(0, &buf->modprom.llc_prom);
			writel_u(0, &buf->modprom.smt_prom);
		}
		if (dev->flags & IFF_ALLMULTI ||
		    netdev_mc_count(dev) > FZA_CMD_CAM_SIZE - 2)
			writel_u(1, &buf->modprom.llc_multi);
		else
			writel_u(0, &buf->modprom.llc_multi);
		writel_u(1, &buf->modprom.llc_bcast);
		break;
	}

	/* Trigger the command. */
	writel_u(FZA_RING_OWN_FZA | command, &ring->cmd_own);
	writew_o(FZA_CONTROL_A_CMD_POLL, &fp->regs->control_a);

	fp->ring_cmd_index = (fp->ring_cmd_index + 1) % FZA_RING_CMD_SIZE;

	fp->int_mask = old_mask;
	writew_u(fp->int_mask, &fp->regs->int_mask);

	return ring;
}

static int fza_init_send(struct net_device *dev,
			 struct fza_cmd_init *__iomem *init)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_ring_cmd __iomem *ring;
	unsigned long flags;
	u32 stat;
	long t;

	spin_lock_irqsave(&fp->lock, flags);
	fp->cmd_done_flag = 0;
	ring = fza_cmd_send(dev, FZA_RING_CMD_INIT);
	spin_unlock_irqrestore(&fp->lock, flags);
	if (!ring)
		/* This should never happen in the uninitialized state,
		 * so do not try to recover and just consider it fatal.
		 */
		return -ENOBUFS;

	/* INIT may take quite a long time (160ms for my C03). */
	t = wait_event_timeout(fp->cmd_done_wait, fp->cmd_done_flag, 3 * HZ);
	if (fp->cmd_done_flag == 0) {
		pr_err("%s: INIT command timed out!, state %x\n", fp->name,
		       FZA_STATUS_GET_STATE(readw_u(&fp->regs->status)));
		return -EIO;
	}
	stat = readl_u(&ring->stat);
	if (stat != FZA_RING_STAT_SUCCESS) {
		pr_err("%s: INIT command failed!, status %02x, state %x\n",
		       fp->name, stat,
		       FZA_STATUS_GET_STATE(readw_u(&fp->regs->status)));
		return -EIO;
	}
	pr_debug("%s: INIT: %lums elapsed\n", fp->name,
		 (3 * HZ - t) * 1000 / HZ);

	if (init)
		*init = fp->mmio + readl_u(&ring->buffer);
	return 0;
}

static void fza_rx_init(struct fza_private *fp)
{
	int i;

	/* Fill the host receive descriptor ring. */
	for (i = 0; i < FZA_RING_RX_SIZE; i++) {
		writel_o(0, &fp->ring_hst_rx[i].rmc);
		writel_o((fp->rx_dma[i] + 0x1000) >> 9,
			 &fp->ring_hst_rx[i].buffer1);
		writel_o(fp->rx_dma[i] >> 9 | FZA_RING_OWN_FZA,
			 &fp->ring_hst_rx[i].buf0_own);
	}
}

static void fza_set_rx_mode(struct net_device *dev)
{
	fza_cmd_send(dev, FZA_RING_CMD_MODCAM);
	fza_cmd_send(dev, FZA_RING_CMD_MODPROM);
}

union fza_buffer_txp {
	struct fza_buffer_tx *data_ptr;
	struct fza_buffer_tx __iomem *mmio_ptr;
};

static int fza_do_xmit(union fza_buffer_txp ub, int len,
		       struct net_device *dev, int smt)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_buffer_tx __iomem *rmc_tx_ptr;
	int i, first, frag_len, left_len;
	u32 own, rmc;

	if (((((fp->ring_rmc_txd_index - 1 + fp->ring_rmc_tx_size) -
	       fp->ring_rmc_tx_index) % fp->ring_rmc_tx_size) *
	     FZA_TX_BUFFER_SIZE) < len)
		return 1;

	first = fp->ring_rmc_tx_index;

	left_len = len;
	frag_len = FZA_TX_BUFFER_SIZE;
	/* First descriptor is relinquished last. */
	own = FZA_RING_TX_OWN_HOST;
	/* First descriptor carries frame length; we don't use cut-through. */
	rmc = FZA_RING_TX_SOP | FZA_RING_TX_VBC | len;
	do {
		i = fp->ring_rmc_tx_index;
		rmc_tx_ptr = &fp->buffer_tx[i];

		if (left_len < FZA_TX_BUFFER_SIZE)
			frag_len = left_len;
		left_len -= frag_len;

		/* Length must be a multiple of 4 as only word writes are
		 * permitted!
		 */
		frag_len = (frag_len + 3) & ~3;
		if (smt)
			fza_moves(ub.mmio_ptr, rmc_tx_ptr, frag_len);
		else
			fza_writes(ub.data_ptr, rmc_tx_ptr, frag_len);

		if (left_len == 0)
			rmc |= FZA_RING_TX_EOP;		/* Mark last frag. */

		writel_o(rmc, &fp->ring_rmc_tx[i].rmc);
		writel_o(own, &fp->ring_rmc_tx[i].own);

		ub.data_ptr++;
		fp->ring_rmc_tx_index = (fp->ring_rmc_tx_index + 1) %
					fp->ring_rmc_tx_size;

		/* Settings for intermediate frags. */
		own = FZA_RING_TX_OWN_RMC;
		rmc = 0;
	} while (left_len > 0);

	if (((((fp->ring_rmc_txd_index - 1 + fp->ring_rmc_tx_size) -
	       fp->ring_rmc_tx_index) % fp->ring_rmc_tx_size) *
	     FZA_TX_BUFFER_SIZE) < dev->mtu + dev->hard_header_len) {
		netif_stop_queue(dev);
		pr_debug("%s: queue stopped\n", fp->name);
	}

	writel_o(FZA_RING_TX_OWN_RMC, &fp->ring_rmc_tx[first].own);

	/* Go, go, go! */
	writew_o(FZA_CONTROL_A_TX_POLL, &fp->regs->control_a);

	return 0;
}

static int fza_do_recv_smt(struct fza_buffer_tx *data_ptr, int len,
			   u32 rmc, struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_buffer_tx __iomem *smt_rx_ptr;
	u32 own;
	int i;

	i = fp->ring_smt_rx_index;
	own = readl_o(&fp->ring_smt_rx[i].own);
	if ((own & FZA_RING_OWN_MASK) == FZA_RING_OWN_FZA)
		return 1;

	smt_rx_ptr = fp->mmio + readl_u(&fp->ring_smt_rx[i].buffer);

	/* Length must be a multiple of 4 as only word writes are permitted! */
	fza_writes(data_ptr, smt_rx_ptr, (len + 3) & ~3);

	writel_o(rmc, &fp->ring_smt_rx[i].rmc);
	writel_o(FZA_RING_OWN_FZA, &fp->ring_smt_rx[i].own);

	fp->ring_smt_rx_index =
		(fp->ring_smt_rx_index + 1) % fp->ring_smt_rx_size;

	/* Grab it! */
	writew_o(FZA_CONTROL_A_SMT_RX_POLL, &fp->regs->control_a);

	return 0;
}

static void fza_tx(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	u32 own, rmc;
	int i;

	while (1) {
		i = fp->ring_rmc_txd_index;
		if (i == fp->ring_rmc_tx_index)
			break;
		own = readl_o(&fp->ring_rmc_tx[i].own);
		if ((own & FZA_RING_OWN_MASK) == FZA_RING_TX_OWN_RMC)
			break;

		rmc = readl_u(&fp->ring_rmc_tx[i].rmc);
		/* Only process the first descriptor. */
		if ((rmc & FZA_RING_TX_SOP) != 0) {
			if ((rmc & FZA_RING_TX_DCC_MASK) ==
			    FZA_RING_TX_DCC_SUCCESS) {
				int pkt_len = (rmc & FZA_RING_PBC_MASK) - 3;
								/* Omit PRH. */

				fp->stats.tx_packets++;
				fp->stats.tx_bytes += pkt_len;
			} else {
				fp->stats.tx_errors++;
				switch (rmc & FZA_RING_TX_DCC_MASK) {
				case FZA_RING_TX_DCC_DTP_SOP:
				case FZA_RING_TX_DCC_DTP:
				case FZA_RING_TX_DCC_ABORT:
					fp->stats.tx_aborted_errors++;
					break;
				case FZA_RING_TX_DCC_UNDRRUN:
					fp->stats.tx_fifo_errors++;
					break;
				case FZA_RING_TX_DCC_PARITY:
				default:
					break;
				}
			}
		}

		fp->ring_rmc_txd_index = (fp->ring_rmc_txd_index + 1) %
					 fp->ring_rmc_tx_size;
	}

	if (((((fp->ring_rmc_txd_index - 1 + fp->ring_rmc_tx_size) -
	       fp->ring_rmc_tx_index) % fp->ring_rmc_tx_size) *
	     FZA_TX_BUFFER_SIZE) >= dev->mtu + dev->hard_header_len) {
		if (fp->queue_active) {
			netif_wake_queue(dev);
			pr_debug("%s: queue woken\n", fp->name);
		}
	}
}

static inline int fza_rx_err(struct fza_private *fp,
			     const u32 rmc, const u8 fc)
{
	int len, min_len, max_len;

	len = rmc & FZA_RING_PBC_MASK;

	if (unlikely((rmc & FZA_RING_RX_BAD) != 0)) {
		fp->stats.rx_errors++;

		/* Check special status codes. */
		if ((rmc & (FZA_RING_RX_CRC | FZA_RING_RX_RRR_MASK |
			    FZA_RING_RX_DA_MASK | FZA_RING_RX_SA_MASK)) ==
		     (FZA_RING_RX_CRC | FZA_RING_RX_RRR_DADDR |
		      FZA_RING_RX_DA_CAM | FZA_RING_RX_SA_ALIAS)) {
			if (len >= 8190)
				fp->stats.rx_length_errors++;
			return 1;
		}
		if ((rmc & (FZA_RING_RX_CRC | FZA_RING_RX_RRR_MASK |
			    FZA_RING_RX_DA_MASK | FZA_RING_RX_SA_MASK)) ==
		     (FZA_RING_RX_CRC | FZA_RING_RX_RRR_DADDR |
		      FZA_RING_RX_DA_CAM | FZA_RING_RX_SA_CAM)) {
			/* Halt the interface to trigger a reset. */
			writew_o(FZA_CONTROL_A_HALT, &fp->regs->control_a);
			readw_o(&fp->regs->control_a);	/* Synchronize. */
			return 1;
		}

		/* Check the MAC status. */
		switch (rmc & FZA_RING_RX_RRR_MASK) {
		case FZA_RING_RX_RRR_OK:
			if ((rmc & FZA_RING_RX_CRC) != 0)
				fp->stats.rx_crc_errors++;
			else if ((rmc & FZA_RING_RX_FSC_MASK) == 0 ||
				 (rmc & FZA_RING_RX_FSB_ERR) != 0)
				fp->stats.rx_frame_errors++;
			return 1;
		case FZA_RING_RX_RRR_SADDR:
		case FZA_RING_RX_RRR_DADDR:
		case FZA_RING_RX_RRR_ABORT:
			/* Halt the interface to trigger a reset. */
			writew_o(FZA_CONTROL_A_HALT, &fp->regs->control_a);
			readw_o(&fp->regs->control_a);	/* Synchronize. */
			return 1;
		case FZA_RING_RX_RRR_LENGTH:
			fp->stats.rx_frame_errors++;
			return 1;
		default:
			return 1;
		}
	}

	/* Packet received successfully; validate the length. */
	switch (fc & FDDI_FC_K_FORMAT_MASK) {
	case FDDI_FC_K_FORMAT_MANAGEMENT:
		if ((fc & FDDI_FC_K_CLASS_MASK) == FDDI_FC_K_CLASS_ASYNC)
			min_len = 37;
		else
			min_len = 17;
		break;
	case FDDI_FC_K_FORMAT_LLC:
		min_len = 20;
		break;
	default:
		min_len = 17;
		break;
	}
	max_len = 4495;
	if (len < min_len || len > max_len) {
		fp->stats.rx_errors++;
		fp->stats.rx_length_errors++;
		return 1;
	}

	return 0;
}

static void fza_rx(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	struct sk_buff *skb, *newskb;
	struct fza_fddihdr *frame;
	dma_addr_t dma, newdma;
	u32 own, rmc, buf;
	int i, len;
	u8 fc;

	while (1) {
		i = fp->ring_hst_rx_index;
		own = readl_o(&fp->ring_hst_rx[i].buf0_own);
		if ((own & FZA_RING_OWN_MASK) == FZA_RING_OWN_FZA)
			break;

		rmc = readl_u(&fp->ring_hst_rx[i].rmc);
		skb = fp->rx_skbuff[i];
		dma = fp->rx_dma[i];

		/* The RMC doesn't count the preamble and the starting
		 * delimiter.  We fix it up here for a total of 3 octets.
		 */
		dma_rmb();
		len = (rmc & FZA_RING_PBC_MASK) + 3;
		frame = (struct fza_fddihdr *)skb->data;

		/* We need to get at real FC. */
		dma_sync_single_for_cpu(fp->bdev,
					dma +
					((u8 *)&frame->hdr.fc - (u8 *)frame),
					sizeof(frame->hdr.fc),
					DMA_FROM_DEVICE);
		fc = frame->hdr.fc;

		if (fza_rx_err(fp, rmc, fc))
			goto err_rx;

		/* We have to 512-byte-align RX buffers... */
		newskb = fza_alloc_skb_irq(dev, FZA_RX_BUFFER_SIZE + 511);
		if (newskb) {
			fza_skb_align(newskb, 512);
			newdma = dma_map_single(fp->bdev, newskb->data,
						FZA_RX_BUFFER_SIZE,
						DMA_FROM_DEVICE);
			if (dma_mapping_error(fp->bdev, newdma)) {
				dev_kfree_skb_irq(newskb);
				newskb = NULL;
			}
		}
		if (newskb) {
			int pkt_len = len - 7;	/* Omit P, SD and FCS. */
			int is_multi;
			int rx_stat;

			dma_unmap_single(fp->bdev, dma, FZA_RX_BUFFER_SIZE,
					 DMA_FROM_DEVICE);

			/* Queue SMT frames to the SMT receive ring. */
			if ((fc & (FDDI_FC_K_CLASS_MASK |
				   FDDI_FC_K_FORMAT_MASK)) ==
			     (FDDI_FC_K_CLASS_ASYNC |
			      FDDI_FC_K_FORMAT_MANAGEMENT) &&
			    (rmc & FZA_RING_RX_DA_MASK) !=
			     FZA_RING_RX_DA_PROM) {
				if (fza_do_recv_smt((struct fza_buffer_tx *)
						    skb->data, len, rmc,
						    dev)) {
					writel_o(FZA_CONTROL_A_SMT_RX_OVFL,
						 &fp->regs->control_a);
				}
			}

			is_multi = ((frame->hdr.daddr[0] & 0x01) != 0);

			skb_reserve(skb, 3);	/* Skip over P and SD. */
			skb_put(skb, pkt_len);	/* And cut off FCS. */
			skb->protocol = fddi_type_trans(skb, dev);

			rx_stat = netif_rx(skb);
			if (rx_stat != NET_RX_DROP) {
				fp->stats.rx_packets++;
				fp->stats.rx_bytes += pkt_len;
				if (is_multi)
					fp->stats.multicast++;
			} else {
				fp->stats.rx_dropped++;
			}

			skb = newskb;
			dma = newdma;
			fp->rx_skbuff[i] = skb;
			fp->rx_dma[i] = dma;
		} else {
			fp->stats.rx_dropped++;
			pr_notice("%s: memory squeeze, dropping packet\n",
				  fp->name);
		}

err_rx:
		writel_o(0, &fp->ring_hst_rx[i].rmc);
		buf = (dma + 0x1000) >> 9;
		writel_o(buf, &fp->ring_hst_rx[i].buffer1);
		buf = dma >> 9 | FZA_RING_OWN_FZA;
		writel_o(buf, &fp->ring_hst_rx[i].buf0_own);
		fp->ring_hst_rx_index =
			(fp->ring_hst_rx_index + 1) % fp->ring_hst_rx_size;
	}
}

static void fza_tx_smt(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_buffer_tx __iomem *smt_tx_ptr;
	int i, len;
	u32 own;

	while (1) {
		i = fp->ring_smt_tx_index;
		own = readl_o(&fp->ring_smt_tx[i].own);
		if ((own & FZA_RING_OWN_MASK) == FZA_RING_OWN_FZA)
			break;

		smt_tx_ptr = fp->mmio + readl_u(&fp->ring_smt_tx[i].buffer);
		len = readl_u(&fp->ring_smt_tx[i].rmc) & FZA_RING_PBC_MASK;

		if (!netif_queue_stopped(dev)) {
			if (dev_nit_active(dev)) {
				struct fza_buffer_tx *skb_data_ptr;
				struct sk_buff *skb;

				/* Length must be a multiple of 4 as only word
				 * reads are permitted!
				 */
				skb = fza_alloc_skb_irq(dev, (len + 3) & ~3);
				if (!skb)
					goto err_no_skb;	/* Drop. */

				skb_data_ptr = (struct fza_buffer_tx *)
					       skb->data;

				fza_reads(smt_tx_ptr, skb_data_ptr,
					  (len + 3) & ~3);
				skb->dev = dev;
				skb_reserve(skb, 3);	/* Skip over PRH. */
				skb_put(skb, len - 3);
				skb_reset_network_header(skb);

				dev_queue_xmit_nit(skb, dev);

				dev_kfree_skb_irq(skb);

err_no_skb:
				;
			}

			/* Queue the frame to the RMC transmit ring. */
			fza_do_xmit((union fza_buffer_txp)
				    { .mmio_ptr = smt_tx_ptr },
				    len, dev, 1);
		}

		writel_o(FZA_RING_OWN_FZA, &fp->ring_smt_tx[i].own);
		fp->ring_smt_tx_index =
			(fp->ring_smt_tx_index + 1) % fp->ring_smt_tx_size;
	}
}

static void fza_uns(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	u32 own;
	int i;

	while (1) {
		i = fp->ring_uns_index;
		own = readl_o(&fp->ring_uns[i].own);
		if ((own & FZA_RING_OWN_MASK) == FZA_RING_OWN_FZA)
			break;

		if (readl_u(&fp->ring_uns[i].id) == FZA_RING_UNS_RX_OVER) {
			fp->stats.rx_errors++;
			fp->stats.rx_over_errors++;
		}

		writel_o(FZA_RING_OWN_FZA, &fp->ring_uns[i].own);
		fp->ring_uns_index =
			(fp->ring_uns_index + 1) % FZA_RING_UNS_SIZE;
	}
}

static void fza_tx_flush(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	u32 own;
	int i;

	/* Clean up the SMT TX ring. */
	i = fp->ring_smt_tx_index;
	do {
		writel_o(FZA_RING_OWN_FZA, &fp->ring_smt_tx[i].own);
		fp->ring_smt_tx_index =
			(fp->ring_smt_tx_index + 1) % fp->ring_smt_tx_size;

	} while (i != fp->ring_smt_tx_index);

	/* Clean up the RMC TX ring. */
	i = fp->ring_rmc_tx_index;
	do {
		own = readl_o(&fp->ring_rmc_tx[i].own);
		if ((own & FZA_RING_OWN_MASK) == FZA_RING_TX_OWN_RMC) {
			u32 rmc = readl_u(&fp->ring_rmc_tx[i].rmc);

			writel_u(rmc | FZA_RING_TX_DTP,
				 &fp->ring_rmc_tx[i].rmc);
		}
		fp->ring_rmc_tx_index =
			(fp->ring_rmc_tx_index + 1) % fp->ring_rmc_tx_size;

	} while (i != fp->ring_rmc_tx_index);

	/* Done. */
	writew_o(FZA_CONTROL_A_FLUSH_DONE, &fp->regs->control_a);
}

static irqreturn_t fza_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct fza_private *fp = netdev_priv(dev);
	uint int_event;

	/* Get interrupt events. */
	int_event = readw_o(&fp->regs->int_event) & fp->int_mask;
	if (int_event == 0)
		return IRQ_NONE;

	/* Clear the events. */
	writew_u(int_event, &fp->regs->int_event);

	/* Now handle the events.  The order matters. */

	/* Command finished interrupt. */
	if ((int_event & FZA_EVENT_CMD_DONE) != 0) {
		fp->irq_count_cmd_done++;

		spin_lock(&fp->lock);
		fp->cmd_done_flag = 1;
		wake_up(&fp->cmd_done_wait);
		spin_unlock(&fp->lock);
	}

	/* Transmit finished interrupt. */
	if ((int_event & FZA_EVENT_TX_DONE) != 0) {
		fp->irq_count_tx_done++;
		fza_tx(dev);
	}

	/* Host receive interrupt. */
	if ((int_event & FZA_EVENT_RX_POLL) != 0) {
		fp->irq_count_rx_poll++;
		fza_rx(dev);
	}

	/* SMT transmit interrupt. */
	if ((int_event & FZA_EVENT_SMT_TX_POLL) != 0) {
		fp->irq_count_smt_tx_poll++;
		fza_tx_smt(dev);
	}

	/* Transmit ring flush request. */
	if ((int_event & FZA_EVENT_FLUSH_TX) != 0) {
		fp->irq_count_flush_tx++;
		fza_tx_flush(dev);
	}

	/* Link status change interrupt. */
	if ((int_event & FZA_EVENT_LINK_ST_CHG) != 0) {
		uint status;

		fp->irq_count_link_st_chg++;
		status = readw_u(&fp->regs->status);
		if (FZA_STATUS_GET_LINK(status) == FZA_LINK_ON) {
			netif_carrier_on(dev);
			pr_info("%s: link available\n", fp->name);
		} else {
			netif_carrier_off(dev);
			pr_info("%s: link unavailable\n", fp->name);
		}
	}

	/* Unsolicited event interrupt. */
	if ((int_event & FZA_EVENT_UNS_POLL) != 0) {
		fp->irq_count_uns_poll++;
		fza_uns(dev);
	}

	/* State change interrupt. */
	if ((int_event & FZA_EVENT_STATE_CHG) != 0) {
		uint status, state;

		fp->irq_count_state_chg++;

		status = readw_u(&fp->regs->status);
		state = FZA_STATUS_GET_STATE(status);
		pr_debug("%s: state change: %x\n", fp->name, state);
		switch (state) {
		case FZA_STATE_RESET:
			break;

		case FZA_STATE_UNINITIALIZED:
			netif_carrier_off(dev);
			timer_delete_sync(&fp->reset_timer);
			fp->ring_cmd_index = 0;
			fp->ring_uns_index = 0;
			fp->ring_rmc_tx_index = 0;
			fp->ring_rmc_txd_index = 0;
			fp->ring_hst_rx_index = 0;
			fp->ring_smt_tx_index = 0;
			fp->ring_smt_rx_index = 0;
			if (fp->state > state) {
				pr_info("%s: OK\n", fp->name);
				fza_cmd_send(dev, FZA_RING_CMD_INIT);
			}
			break;

		case FZA_STATE_INITIALIZED:
			if (fp->state > state) {
				fza_set_rx_mode(dev);
				fza_cmd_send(dev, FZA_RING_CMD_PARAM);
			}
			break;

		case FZA_STATE_RUNNING:
		case FZA_STATE_MAINTENANCE:
			fp->state = state;
			fza_rx_init(fp);
			fp->queue_active = 1;
			netif_wake_queue(dev);
			pr_debug("%s: queue woken\n", fp->name);
			break;

		case FZA_STATE_HALTED:
			fp->queue_active = 0;
			netif_stop_queue(dev);
			pr_debug("%s: queue stopped\n", fp->name);
			timer_delete_sync(&fp->reset_timer);
			pr_warn("%s: halted, reason: %x\n", fp->name,
				FZA_STATUS_GET_HALT(status));
			fza_regs_dump(fp);
			pr_info("%s: resetting the board...\n", fp->name);
			fza_do_reset(fp);
			fp->timer_state = 0;
			fp->reset_timer.expires = jiffies + 45 * HZ;
			add_timer(&fp->reset_timer);
			break;

		default:
			pr_warn("%s: undefined state: %x\n", fp->name, state);
			break;
		}

		spin_lock(&fp->lock);
		fp->state_chg_flag = 1;
		wake_up(&fp->state_chg_wait);
		spin_unlock(&fp->lock);
	}

	return IRQ_HANDLED;
}

static void fza_reset_timer(struct timer_list *t)
{
	struct fza_private *fp = from_timer(fp, t, reset_timer);

	if (!fp->timer_state) {
		pr_err("%s: RESET timed out!\n", fp->name);
		pr_info("%s: trying harder...\n", fp->name);

		/* Assert the board reset. */
		writew_o(FZA_RESET_INIT, &fp->regs->reset);
		readw_o(&fp->regs->reset);		/* Synchronize. */

		fp->timer_state = 1;
		fp->reset_timer.expires = jiffies + HZ;
	} else {
		/* Clear the board reset. */
		writew_u(FZA_RESET_CLR, &fp->regs->reset);

		/* Enable all interrupt events we handle. */
		writew_o(fp->int_mask, &fp->regs->int_mask);
		readw_o(&fp->regs->int_mask);		/* Synchronize. */

		fp->timer_state = 0;
		fp->reset_timer.expires = jiffies + 45 * HZ;
	}
	add_timer(&fp->reset_timer);
}

static int fza_set_mac_address(struct net_device *dev, void *addr)
{
	return -EOPNOTSUPP;
}

static netdev_tx_t fza_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	unsigned int old_mask, new_mask;
	int ret;
	u8 fc;

	skb_push(skb, 3);			/* Make room for PRH. */

	/* Decode FC to set PRH. */
	fc = skb->data[3];
	skb->data[0] = 0;
	skb->data[1] = 0;
	skb->data[2] = FZA_PRH2_NORMAL;
	if ((fc & FDDI_FC_K_CLASS_MASK) == FDDI_FC_K_CLASS_SYNC)
		skb->data[0] |= FZA_PRH0_FRAME_SYNC;
	switch (fc & FDDI_FC_K_FORMAT_MASK) {
	case FDDI_FC_K_FORMAT_MANAGEMENT:
		if ((fc & FDDI_FC_K_CONTROL_MASK) == 0) {
			/* Token. */
			skb->data[0] |= FZA_PRH0_TKN_TYPE_IMM;
			skb->data[1] |= FZA_PRH1_TKN_SEND_NONE;
		} else {
			/* SMT or MAC. */
			skb->data[0] |= FZA_PRH0_TKN_TYPE_UNR;
			skb->data[1] |= FZA_PRH1_TKN_SEND_UNR;
		}
		skb->data[1] |= FZA_PRH1_CRC_NORMAL;
		break;
	case FDDI_FC_K_FORMAT_LLC:
	case FDDI_FC_K_FORMAT_FUTURE:
		skb->data[0] |= FZA_PRH0_TKN_TYPE_UNR;
		skb->data[1] |= FZA_PRH1_CRC_NORMAL | FZA_PRH1_TKN_SEND_UNR;
		break;
	case FDDI_FC_K_FORMAT_IMPLEMENTOR:
		skb->data[0] |= FZA_PRH0_TKN_TYPE_UNR;
		skb->data[1] |= FZA_PRH1_TKN_SEND_ORIG;
		break;
	}

	/* SMT transmit interrupts may sneak frames into the RMC
	 * transmit ring.  We disable them while queueing a frame
	 * to maintain consistency.
	 */
	old_mask = fp->int_mask;
	new_mask = old_mask & ~FZA_MASK_SMT_TX_POLL;
	writew_u(new_mask, &fp->regs->int_mask);
	readw_o(&fp->regs->int_mask);			/* Synchronize. */
	fp->int_mask = new_mask;
	ret = fza_do_xmit((union fza_buffer_txp)
			  { .data_ptr = (struct fza_buffer_tx *)skb->data },
			  skb->len, dev, 0);
	fp->int_mask = old_mask;
	writew_u(fp->int_mask, &fp->regs->int_mask);

	if (ret) {
		/* Probably an SMT packet filled the remaining space,
		 * so just stop the queue, but don't report it as an error.
		 */
		netif_stop_queue(dev);
		pr_debug("%s: queue stopped\n", fp->name);
		fp->stats.tx_dropped++;
	}

	dev_kfree_skb(skb);

	return ret;
}

static int fza_open(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	struct fza_ring_cmd __iomem *ring;
	struct sk_buff *skb;
	unsigned long flags;
	dma_addr_t dma;
	int ret, i;
	u32 stat;
	long t;

	for (i = 0; i < FZA_RING_RX_SIZE; i++) {
		/* We have to 512-byte-align RX buffers... */
		skb = fza_alloc_skb(dev, FZA_RX_BUFFER_SIZE + 511);
		if (skb) {
			fza_skb_align(skb, 512);
			dma = dma_map_single(fp->bdev, skb->data,
					     FZA_RX_BUFFER_SIZE,
					     DMA_FROM_DEVICE);
			if (dma_mapping_error(fp->bdev, dma)) {
				dev_kfree_skb(skb);
				skb = NULL;
			}
		}
		if (!skb) {
			for (--i; i >= 0; i--) {
				dma_unmap_single(fp->bdev, fp->rx_dma[i],
						 FZA_RX_BUFFER_SIZE,
						 DMA_FROM_DEVICE);
				dev_kfree_skb(fp->rx_skbuff[i]);
				fp->rx_dma[i] = 0;
				fp->rx_skbuff[i] = NULL;
			}
			return -ENOMEM;
		}
		fp->rx_skbuff[i] = skb;
		fp->rx_dma[i] = dma;
	}

	ret = fza_init_send(dev, NULL);
	if (ret != 0)
		return ret;

	/* Purger and Beacon multicasts need to be supplied before PARAM. */
	fza_set_rx_mode(dev);

	spin_lock_irqsave(&fp->lock, flags);
	fp->cmd_done_flag = 0;
	ring = fza_cmd_send(dev, FZA_RING_CMD_PARAM);
	spin_unlock_irqrestore(&fp->lock, flags);
	if (!ring)
		return -ENOBUFS;

	t = wait_event_timeout(fp->cmd_done_wait, fp->cmd_done_flag, 3 * HZ);
	if (fp->cmd_done_flag == 0) {
		pr_err("%s: PARAM command timed out!, state %x\n", fp->name,
		       FZA_STATUS_GET_STATE(readw_u(&fp->regs->status)));
		return -EIO;
	}
	stat = readl_u(&ring->stat);
	if (stat != FZA_RING_STAT_SUCCESS) {
		pr_err("%s: PARAM command failed!, status %02x, state %x\n",
		       fp->name, stat,
		       FZA_STATUS_GET_STATE(readw_u(&fp->regs->status)));
		return -EIO;
	}
	pr_debug("%s: PARAM: %lums elapsed\n", fp->name,
		 (3 * HZ - t) * 1000 / HZ);

	return 0;
}

static int fza_close(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);
	unsigned long flags;
	uint state;
	long t;
	int i;

	netif_stop_queue(dev);
	pr_debug("%s: queue stopped\n", fp->name);

	timer_delete_sync(&fp->reset_timer);
	spin_lock_irqsave(&fp->lock, flags);
	fp->state = FZA_STATE_UNINITIALIZED;
	fp->state_chg_flag = 0;
	/* Shut the interface down. */
	writew_o(FZA_CONTROL_A_SHUT, &fp->regs->control_a);
	readw_o(&fp->regs->control_a);			/* Synchronize. */
	spin_unlock_irqrestore(&fp->lock, flags);

	/* DEC says SHUT needs up to 10 seconds to complete. */
	t = wait_event_timeout(fp->state_chg_wait, fp->state_chg_flag,
			       15 * HZ);
	state = FZA_STATUS_GET_STATE(readw_o(&fp->regs->status));
	if (fp->state_chg_flag == 0) {
		pr_err("%s: SHUT timed out!, state %x\n", fp->name, state);
		return -EIO;
	}
	if (state != FZA_STATE_UNINITIALIZED) {
		pr_err("%s: SHUT failed!, state %x\n", fp->name, state);
		return -EIO;
	}
	pr_debug("%s: SHUT: %lums elapsed\n", fp->name,
		 (15 * HZ - t) * 1000 / HZ);

	for (i = 0; i < FZA_RING_RX_SIZE; i++)
		if (fp->rx_skbuff[i]) {
			dma_unmap_single(fp->bdev, fp->rx_dma[i],
					 FZA_RX_BUFFER_SIZE, DMA_FROM_DEVICE);
			dev_kfree_skb(fp->rx_skbuff[i]);
			fp->rx_dma[i] = 0;
			fp->rx_skbuff[i] = NULL;
		}

	return 0;
}

static struct net_device_stats *fza_get_stats(struct net_device *dev)
{
	struct fza_private *fp = netdev_priv(dev);

	return &fp->stats;
}

static int fza_probe(struct device *bdev)
{
	static const struct net_device_ops netdev_ops = {
		.ndo_open = fza_open,
		.ndo_stop = fza_close,
		.ndo_start_xmit = fza_start_xmit,
		.ndo_set_rx_mode = fza_set_rx_mode,
		.ndo_set_mac_address = fza_set_mac_address,
		.ndo_get_stats = fza_get_stats,
	};
	static int version_printed;
	char rom_rev[4], fw_rev[4], rmc_rev[4];
	struct tc_dev *tdev = to_tc_dev(bdev);
	struct fza_cmd_init __iomem *init;
	resource_size_t start, len;
	struct net_device *dev;
	struct fza_private *fp;
	uint smt_ver, pmd_type;
	void __iomem *mmio;
	uint hw_addr[2];
	int ret, i;

	if (!version_printed) {
		pr_info("%s", version);
		version_printed = 1;
	}

	dev = alloc_fddidev(sizeof(*fp));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, bdev);

	fp = netdev_priv(dev);
	dev_set_drvdata(bdev, dev);

	fp->bdev = bdev;
	fp->name = dev_name(bdev);

	/* Request the I/O MEM resource. */
	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	if (!request_mem_region(start, len, dev_name(bdev))) {
		pr_err("%s: cannot reserve MMIO region\n", fp->name);
		ret = -EBUSY;
		goto err_out_kfree;
	}

	/* MMIO mapping setup. */
	mmio = ioremap(start, len);
	if (!mmio) {
		pr_err("%s: cannot map MMIO\n", fp->name);
		ret = -ENOMEM;
		goto err_out_resource;
	}

	/* Initialize the new device structure. */
	switch (loopback) {
	case FZA_LOOP_NORMAL:
	case FZA_LOOP_INTERN:
	case FZA_LOOP_EXTERN:
		break;
	default:
		loopback = FZA_LOOP_NORMAL;
	}

	fp->mmio = mmio;
	dev->irq = tdev->interrupt;

	pr_info("%s: DEC FDDIcontroller 700 or 700-C at 0x%08llx, irq %d\n",
		fp->name, (long long)tdev->resource.start, dev->irq);
	pr_debug("%s: mapped at: 0x%p\n", fp->name, mmio);

	fp->regs = mmio + FZA_REG_BASE;
	fp->ring_cmd = mmio + FZA_RING_CMD;
	fp->ring_uns = mmio + FZA_RING_UNS;

	init_waitqueue_head(&fp->state_chg_wait);
	init_waitqueue_head(&fp->cmd_done_wait);
	spin_lock_init(&fp->lock);
	fp->int_mask = FZA_MASK_NORMAL;

	timer_setup(&fp->reset_timer, fza_reset_timer, 0);

	/* Sanitize the board. */
	fza_regs_dump(fp);
	fza_do_shutdown(fp);

	ret = request_irq(dev->irq, fza_interrupt, IRQF_SHARED, fp->name, dev);
	if (ret != 0) {
		pr_err("%s: unable to get IRQ %d!\n", fp->name, dev->irq);
		goto err_out_map;
	}

	/* Enable the driver mode. */
	writew_o(FZA_CONTROL_B_DRIVER, &fp->regs->control_b);

	/* For some reason transmit done interrupts can trigger during
	 * reset.  This avoids a division error in the handler.
	 */
	fp->ring_rmc_tx_size = FZA_RING_TX_SIZE;

	ret = fza_reset(fp);
	if (ret != 0)
		goto err_out_irq;

	ret = fza_init_send(dev, &init);
	if (ret != 0)
		goto err_out_irq;

	fza_reads(&init->hw_addr, &hw_addr, sizeof(hw_addr));
	dev_addr_set(dev, (u8 *)&hw_addr);

	fza_reads(&init->rom_rev, &rom_rev, sizeof(rom_rev));
	fza_reads(&init->fw_rev, &fw_rev, sizeof(fw_rev));
	fza_reads(&init->rmc_rev, &rmc_rev, sizeof(rmc_rev));
	for (i = 3; i >= 0 && rom_rev[i] == ' '; i--)
		rom_rev[i] = 0;
	for (i = 3; i >= 0 && fw_rev[i] == ' '; i--)
		fw_rev[i] = 0;
	for (i = 3; i >= 0 && rmc_rev[i] == ' '; i--)
		rmc_rev[i] = 0;

	fp->ring_rmc_tx = mmio + readl_u(&init->rmc_tx);
	fp->ring_rmc_tx_size = readl_u(&init->rmc_tx_size);
	fp->ring_hst_rx = mmio + readl_u(&init->hst_rx);
	fp->ring_hst_rx_size = readl_u(&init->hst_rx_size);
	fp->ring_smt_tx = mmio + readl_u(&init->smt_tx);
	fp->ring_smt_tx_size = readl_u(&init->smt_tx_size);
	fp->ring_smt_rx = mmio + readl_u(&init->smt_rx);
	fp->ring_smt_rx_size = readl_u(&init->smt_rx_size);

	fp->buffer_tx = mmio + FZA_TX_BUFFER_ADDR(readl_u(&init->rmc_tx));

	fp->t_max = readl_u(&init->def_t_max);
	fp->t_req = readl_u(&init->def_t_req);
	fp->tvx = readl_u(&init->def_tvx);
	fp->lem_threshold = readl_u(&init->lem_threshold);
	fza_reads(&init->def_station_id, &fp->station_id,
		  sizeof(fp->station_id));
	fp->rtoken_timeout = readl_u(&init->rtoken_timeout);
	fp->ring_purger = readl_u(&init->ring_purger);

	smt_ver = readl_u(&init->smt_ver);
	pmd_type = readl_u(&init->pmd_type);

	pr_debug("%s: INIT parameters:\n", fp->name);
	pr_debug("        tx_mode: %u\n", readl_u(&init->tx_mode));
	pr_debug("    hst_rx_size: %u\n", readl_u(&init->hst_rx_size));
	pr_debug("        rmc_rev: %.4s\n", rmc_rev);
	pr_debug("        rom_rev: %.4s\n", rom_rev);
	pr_debug("         fw_rev: %.4s\n", fw_rev);
	pr_debug("       mop_type: %u\n", readl_u(&init->mop_type));
	pr_debug("         hst_rx: 0x%08x\n", readl_u(&init->hst_rx));
	pr_debug("         rmc_tx: 0x%08x\n", readl_u(&init->rmc_tx));
	pr_debug("    rmc_tx_size: %u\n", readl_u(&init->rmc_tx_size));
	pr_debug("         smt_tx: 0x%08x\n", readl_u(&init->smt_tx));
	pr_debug("    smt_tx_size: %u\n", readl_u(&init->smt_tx_size));
	pr_debug("         smt_rx: 0x%08x\n", readl_u(&init->smt_rx));
	pr_debug("    smt_rx_size: %u\n", readl_u(&init->smt_rx_size));
	/* TC systems are always LE, so don't bother swapping. */
	pr_debug("        hw_addr: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		 (readl_u(&init->hw_addr[0]) >> 0) & 0xff,
		 (readl_u(&init->hw_addr[0]) >> 8) & 0xff,
		 (readl_u(&init->hw_addr[0]) >> 16) & 0xff,
		 (readl_u(&init->hw_addr[0]) >> 24) & 0xff,
		 (readl_u(&init->hw_addr[1]) >> 0) & 0xff,
		 (readl_u(&init->hw_addr[1]) >> 8) & 0xff,
		 (readl_u(&init->hw_addr[1]) >> 16) & 0xff,
		 (readl_u(&init->hw_addr[1]) >> 24) & 0xff);
	pr_debug("      def_t_req: %u\n", readl_u(&init->def_t_req));
	pr_debug("        def_tvx: %u\n", readl_u(&init->def_tvx));
	pr_debug("      def_t_max: %u\n", readl_u(&init->def_t_max));
	pr_debug("  lem_threshold: %u\n", readl_u(&init->lem_threshold));
	/* Don't bother swapping, see above. */
	pr_debug(" def_station_id: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		 (readl_u(&init->def_station_id[0]) >> 0) & 0xff,
		 (readl_u(&init->def_station_id[0]) >> 8) & 0xff,
		 (readl_u(&init->def_station_id[0]) >> 16) & 0xff,
		 (readl_u(&init->def_station_id[0]) >> 24) & 0xff,
		 (readl_u(&init->def_station_id[1]) >> 0) & 0xff,
		 (readl_u(&init->def_station_id[1]) >> 8) & 0xff,
		 (readl_u(&init->def_station_id[1]) >> 16) & 0xff,
		 (readl_u(&init->def_station_id[1]) >> 24) & 0xff);
	pr_debug("   pmd_type_alt: %u\n", readl_u(&init->pmd_type_alt));
	pr_debug("        smt_ver: %u\n", readl_u(&init->smt_ver));
	pr_debug(" rtoken_timeout: %u\n", readl_u(&init->rtoken_timeout));
	pr_debug("    ring_purger: %u\n", readl_u(&init->ring_purger));
	pr_debug("    smt_ver_max: %u\n", readl_u(&init->smt_ver_max));
	pr_debug("    smt_ver_min: %u\n", readl_u(&init->smt_ver_min));
	pr_debug("       pmd_type: %u\n", readl_u(&init->pmd_type));

	pr_info("%s: model %s, address %pMF\n",
		fp->name,
		pmd_type == FZA_PMD_TYPE_TW ?
			"700-C (DEFZA-CA), ThinWire PMD selected" :
			pmd_type == FZA_PMD_TYPE_STP ?
				"700-C (DEFZA-CA), STP PMD selected" :
				"700 (DEFZA-AA), MMF PMD",
		dev->dev_addr);
	pr_info("%s: ROM rev. %.4s, firmware rev. %.4s, RMC rev. %.4s, "
		"SMT ver. %u\n", fp->name, rom_rev, fw_rev, rmc_rev, smt_ver);

	/* Now that we fetched initial parameters just shut the interface
	 * until opened.
	 */
	ret = fza_close(dev);
	if (ret != 0)
		goto err_out_irq;

	/* The FZA-specific entries in the device structure. */
	dev->netdev_ops = &netdev_ops;

	ret = register_netdev(dev);
	if (ret != 0)
		goto err_out_irq;

	pr_info("%s: registered as %s\n", fp->name, dev->name);
	fp->name = (const char *)dev->name;

	get_device(bdev);
	return 0;

err_out_irq:
	timer_delete_sync(&fp->reset_timer);
	fza_do_shutdown(fp);
	free_irq(dev->irq, dev);

err_out_map:
	iounmap(mmio);

err_out_resource:
	release_mem_region(start, len);

err_out_kfree:
	pr_err("%s: initialization failure, aborting!\n", fp->name);
	free_netdev(dev);
	return ret;
}

static int fza_remove(struct device *bdev)
{
	struct net_device *dev = dev_get_drvdata(bdev);
	struct fza_private *fp = netdev_priv(dev);
	struct tc_dev *tdev = to_tc_dev(bdev);
	resource_size_t start, len;

	put_device(bdev);

	unregister_netdev(dev);

	timer_delete_sync(&fp->reset_timer);
	fza_do_shutdown(fp);
	free_irq(dev->irq, dev);

	iounmap(fp->mmio);

	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	release_mem_region(start, len);

	free_netdev(dev);

	return 0;
}

static struct tc_device_id const fza_tc_table[] = {
	{ "DEC     ", "PMAF-AA " },
	{ }
};
MODULE_DEVICE_TABLE(tc, fza_tc_table);

static struct tc_driver fza_driver = {
	.id_table	= fza_tc_table,
	.driver		= {
		.name	= "defza",
		.bus	= &tc_bus_type,
		.probe	= fza_probe,
		.remove	= fza_remove,
	},
};

static int fza_init(void)
{
	return tc_register_driver(&fza_driver);
}

static void fza_exit(void)
{
	tc_unregister_driver(&fza_driver);
}

module_init(fza_init);
module_exit(fza_exit);
