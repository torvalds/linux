/*******************************************************************************
  This contains the functions to handle the normal descriptors.

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "common.h"

static int ndesc_get_tx_status(void *data, struct stmmac_extra_stats *x,
			       struct dma_desc *p, void __iomem *ioaddr)
{
	int ret = 0;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.tx.error_summary)) {
		if (unlikely(p->des01.tx.underflow_error)) {
			x->tx_underflow++;
			stats->tx_fifo_errors++;
		}
		if (unlikely(p->des01.tx.no_carrier)) {
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->des01.tx.loss_carrier)) {
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely((p->des01.tx.excessive_deferral) ||
			     (p->des01.tx.excessive_collisions) ||
			     (p->des01.tx.late_collision)))
			stats->collisions += p->des01.tx.collision_count;
		ret = -1;
	}
	if (unlikely(p->des01.tx.heartbeat_fail)) {
		x->tx_heartbeat++;
		stats->tx_heartbeat_errors++;
		ret = -1;
	}
	if (unlikely(p->des01.tx.deferred))
		x->tx_deferred++;

	return ret;
}

static int ndesc_get_tx_len(struct dma_desc *p)
{
	return p->des01.tx.buffer1_size;
}

/* This function verifies if each incoming frame has some errors
 * and, if required, updates the multicast statistics.
 * In case of success, it returns csum_none because the device
 * is not able to compute the csum in HW. */
static int ndesc_get_rx_status(void *data, struct stmmac_extra_stats *x,
			       struct dma_desc *p)
{
	int ret = csum_none;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.rx.last_descriptor == 0)) {
		pr_warning("ndesc Error: Oversized Ethernet "
			   "frame spanned multiple buffers\n");
		stats->rx_length_errors++;
		return discard_frame;
	}

	if (unlikely(p->des01.rx.error_summary)) {
		if (unlikely(p->des01.rx.descriptor_error))
			x->rx_desc++;
		if (unlikely(p->des01.rx.partial_frame_error))
			x->rx_partial++;
		if (unlikely(p->des01.rx.run_frame))
			x->rx_runt++;
		if (unlikely(p->des01.rx.frame_too_long))
			x->rx_toolong++;
		if (unlikely(p->des01.rx.collision)) {
			x->rx_collision++;
			stats->collisions++;
		}
		if (unlikely(p->des01.rx.crc_error)) {
			x->rx_crc++;
			stats->rx_crc_errors++;
		}
		ret = discard_frame;
	}
	if (unlikely(p->des01.rx.dribbling))
		ret = discard_frame;

	if (unlikely(p->des01.rx.length_error)) {
		x->rx_length++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.rx.mii_error)) {
		x->rx_mii++;
		ret = discard_frame;
	}
	if (p->des01.rx.multicast_frame) {
		x->rx_multicast++;
		stats->multicast++;
	}
	return ret;
}

static void ndesc_init_rx_desc(struct dma_desc *p, unsigned int ring_size,
			       int disable_rx_ic)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->des01.rx.own = 1;
		p->des01.rx.buffer1_size = BUF_SIZE_2KiB - 1;
		if (i == ring_size - 1)
			p->des01.rx.end_ring = 1;
		if (disable_rx_ic)
			p->des01.rx.disable_ic = 1;
		p++;
	}
}

static void ndesc_init_tx_desc(struct dma_desc *p, unsigned int ring_size)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->des01.tx.own = 0;
		if (i == ring_size - 1)
			p->des01.tx.end_ring = 1;
		p++;
	}
}

static int ndesc_get_tx_owner(struct dma_desc *p)
{
	return p->des01.tx.own;
}

static int ndesc_get_rx_owner(struct dma_desc *p)
{
	return p->des01.rx.own;
}

static void ndesc_set_tx_owner(struct dma_desc *p)
{
	p->des01.tx.own = 1;
}

static void ndesc_set_rx_owner(struct dma_desc *p)
{
	p->des01.rx.own = 1;
}

static int ndesc_get_tx_ls(struct dma_desc *p)
{
	return p->des01.tx.last_segment;
}

static void ndesc_release_tx_desc(struct dma_desc *p)
{
	int ter = p->des01.tx.end_ring;

	memset(p, 0, offsetof(struct dma_desc, des2));
	/* set termination field */
	p->des01.tx.end_ring = ter;
}

static void ndesc_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				  int csum_flag)
{
	p->des01.tx.first_segment = is_fs;
	p->des01.tx.buffer1_size = len;
}

static void ndesc_clear_tx_ic(struct dma_desc *p)
{
	p->des01.tx.interrupt = 0;
}

static void ndesc_close_tx_desc(struct dma_desc *p)
{
	p->des01.tx.last_segment = 1;
	p->des01.tx.interrupt = 1;
}

static int ndesc_get_rx_frame_len(struct dma_desc *p)
{
	return p->des01.rx.frame_length;
}

const struct stmmac_desc_ops ndesc_ops = {
	.tx_status = ndesc_get_tx_status,
	.rx_status = ndesc_get_rx_status,
	.get_tx_len = ndesc_get_tx_len,
	.init_rx_desc = ndesc_init_rx_desc,
	.init_tx_desc = ndesc_init_tx_desc,
	.get_tx_owner = ndesc_get_tx_owner,
	.get_rx_owner = ndesc_get_rx_owner,
	.release_tx_desc = ndesc_release_tx_desc,
	.prepare_tx_desc = ndesc_prepare_tx_desc,
	.clear_tx_ic = ndesc_clear_tx_ic,
	.close_tx_desc = ndesc_close_tx_desc,
	.get_tx_ls = ndesc_get_tx_ls,
	.set_tx_owner = ndesc_set_tx_owner,
	.set_rx_owner = ndesc_set_rx_owner,
	.get_rx_frame_len = ndesc_get_rx_frame_len,
};
