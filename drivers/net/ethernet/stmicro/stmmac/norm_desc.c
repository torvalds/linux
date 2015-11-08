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

#include <linux/stmmac.h>
#include "common.h"
#include "descs_com.h"

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

	if (p->des01.etx.vlan_frame)
		x->tx_vlan++;

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
 * In case of success, it returns good_frame because the GMAC device
 * is supposed to be able to compute the csum in HW. */
static int ndesc_get_rx_status(void *data, struct stmmac_extra_stats *x,
			       struct dma_desc *p)
{
	int ret = good_frame;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.rx.last_descriptor == 0)) {
		pr_warn("%s: Oversized frame spanned multiple buffers\n",
			__func__);
		stats->rx_length_errors++;
		return discard_frame;
	}

	if (unlikely(p->des01.rx.error_summary)) {
		if (unlikely(p->des01.rx.descriptor_error))
			x->rx_desc++;
		if (unlikely(p->des01.rx.sa_filter_fail))
			x->sa_filter_fail++;
		if (unlikely(p->des01.rx.overflow_error))
			x->overflow_error++;
		if (unlikely(p->des01.rx.ipc_csum_error))
			x->ipc_csum_error++;
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
		x->dribbling_bit++;

	if (unlikely(p->des01.rx.length_error)) {
		x->rx_length++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.rx.mii_error)) {
		x->rx_mii++;
		ret = discard_frame;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (p->des01.rx.vlan_tag)
		x->vlan_tag++;
#endif
	return ret;
}

static void ndesc_init_rx_desc(struct dma_desc *p, int disable_rx_ic, int mode,
			       int end)
{
	p->des01.all_flags = 0;
	p->des01.rx.own = 1;
	p->des01.rx.buffer1_size = BUF_SIZE_2KiB - 1;

	if (mode == STMMAC_CHAIN_MODE)
		ndesc_rx_set_on_chain(p, end);
	else
		ndesc_rx_set_on_ring(p, end);

	if (disable_rx_ic)
		p->des01.rx.disable_ic = 1;
}

static void ndesc_init_tx_desc(struct dma_desc *p, int mode, int end)
{
	p->des01.all_flags = 0;
	if (mode == STMMAC_CHAIN_MODE)
		ndesc_tx_set_on_chain(p, end);
	else
		ndesc_tx_set_on_ring(p, end);
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

static void ndesc_release_tx_desc(struct dma_desc *p, int mode)
{
	int ter = p->des01.tx.end_ring;

	memset(p, 0, offsetof(struct dma_desc, des2));
	if (mode == STMMAC_CHAIN_MODE)
		ndesc_end_tx_desc_on_chain(p, ter);
	else
		ndesc_end_tx_desc_on_ring(p, ter);
}

static void ndesc_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				  int csum_flag, int mode)
{
	p->des01.tx.first_segment = is_fs;
	if (mode == STMMAC_CHAIN_MODE)
		norm_set_tx_desc_len_on_chain(p, len);
	else
		norm_set_tx_desc_len_on_ring(p, len);

	if (likely(csum_flag))
		p->des01.tx.checksum_insertion = cic_full;
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

static int ndesc_get_rx_frame_len(struct dma_desc *p, int rx_coe_type)
{
	/* The type-1 checksum offload engines append the checksum at
	 * the end of frame and the two bytes of checksum are added in
	 * the length.
	 * Adjust for that in the framelen for type-1 checksum offload
	 * engines. */
	if (rx_coe_type == STMMAC_RX_COE_TYPE1)
		return p->des01.rx.frame_length - 2;
	else
		return p->des01.rx.frame_length;
}

static void ndesc_enable_tx_timestamp(struct dma_desc *p)
{
	p->des01.tx.time_stamp_enable = 1;
}

static int ndesc_get_tx_timestamp_status(struct dma_desc *p)
{
	return p->des01.tx.time_stamp_status;
}

static u64 ndesc_get_timestamp(void *desc, u32 ats)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	u64 ns;

	ns = p->des2;
	/* convert high/sec time stamp value to nanosecond */
	ns += p->des3 * 1000000000ULL;

	return ns;
}

static int ndesc_get_rx_timestamp_status(void *desc, u32 ats)
{
	struct dma_desc *p = (struct dma_desc *)desc;

	if ((p->des2 == 0xffffffff) && (p->des3 == 0xffffffff))
		/* timestamp is corrupted, hence don't store it */
		return 0;
	else
		return 1;
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
	.enable_tx_timestamp = ndesc_enable_tx_timestamp,
	.get_tx_timestamp_status = ndesc_get_tx_timestamp_status,
	.get_timestamp = ndesc_get_timestamp,
	.get_rx_timestamp_status = ndesc_get_rx_timestamp_status,
};
