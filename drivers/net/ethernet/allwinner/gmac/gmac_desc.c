/*******************************************************************************
  Copyright © 2012, Shuge
		Author: shuge  <shugeLinux@gmail.com>

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
*******************************************************************************/
#include "gmac_ethtool.h"
#include "gmac_desc.h"
#include "sunxi_gmac.h"

int desc_get_tx_status(void *data, struct gmac_extra_stats *x,
			       dma_desc_t *p, void __iomem *ioaddr)
{
	int ret = 0;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->desc0.tx.err_sum)) {
		if (unlikely(p->desc0.tx.under_err)) {
			x->tx_underflow++;
			stats->tx_fifo_errors++;
		}
		if (unlikely(p->desc0.tx.no_carr)) {
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->desc0.tx.loss_carr)) {
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely((p->desc0.tx.ex_deferral) ||
			     (p->desc0.tx.ex_coll) ||
			     (p->desc0.tx.late_coll)))
			stats->collisions += p->desc0.tx.coll_cnt;
		ret = -1;
	}

	if (p->desc0.tx.vlan_tag) {
		printk(KERN_INFO "GMAC TX status: VLAN frame\n");
		x->tx_vlan++;
	}

	if (unlikely(p->desc0.tx.deferred))
		x->tx_deferred++;

	return ret;
}

int desc_get_tx_len(dma_desc_t *p)
{
	return p->desc1.tx.buf1_size;
}

/* This function verifies if each incoming frame has some errors
 * and, if required, updates the multicast statistics.
 * In case of success, it returns good_frame because the GMAC device
 * is supposed to be able to compute the csum in HW. */
int desc_get_rx_status(void *data, struct gmac_extra_stats *x, dma_desc_t *p)
{
	int ret = good_frame;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->desc0.rx.last_desc == 0)) {
		pr_warning("ndesc Error: Oversized Ethernet "
			   "frame spanned multiple buffers\n");
		stats->rx_length_errors++;
		return discard_frame;
	}

	if (unlikely(p->desc0.rx.err_sum)) {
		if (unlikely(p->desc0.rx.desc_err))
			x->rx_desc++;
		if (unlikely(p->desc0.rx.sou_filter))
			x->sa_filter_fail++;
		if (unlikely(p->desc0.rx.over_err))
			x->overflow_error++;
		if (unlikely(p->desc0.rx.ipch_err))
			x->ipc_csum_error++;
		if (unlikely(p->desc0.rx.late_coll)) {
			x->rx_collision++;
			stats->collisions++;
		}
		if (unlikely(p->desc0.rx.crc_err)) {
			x->rx_crc++;
			stats->rx_crc_errors++;
		}
		ret = discard_frame;
	}
	if (unlikely(p->desc0.rx.dribbling))
		x->dribbling_bit++;

	if (unlikely(p->desc0.rx.len_err)) {
		x->rx_length++;
		ret = discard_frame;
	}
	if (unlikely(p->desc0.rx.mii_err)) {
		x->rx_mii++;
		ret = discard_frame;
	}
#ifdef GMAC_VLAN_TAG_USED
	if (p->desc0.rx.vlan_tag)
		x->vlan_tag++;
#endif
	return ret;
}

void desc_init_rx(dma_desc_t *p, unsigned int ring_size,
			       int disable_rx_ic)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->desc0.rx.own = 1;
		p->desc1.rx.buf1_size = BUF_SIZE_2KiB - 1;

		desc_rx_set_on_ring_chain(p, (i == ring_size - 1));

		if (disable_rx_ic)
			p->desc1.rx.dis_ic = 1;
		p++;
	}
}

void desc_init_tx(dma_desc_t *p, unsigned int ring_size)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->desc0.tx.own = 0;
		desc_tx_set_on_ring_chain(p, (i == (ring_size - 1)));
		p++;
	}
}

int desc_get_tx_own(dma_desc_t *p)
{
	return p->desc0.tx.own;
}

int desc_get_rx_own(dma_desc_t *p)
{
	return p->desc0.rx.own;
}

void desc_set_tx_own(dma_desc_t *p)
{
	p->desc0.tx.own = 1;
}

void desc_set_rx_own(dma_desc_t *p)
{
	p->desc0.rx.own = 1;
}

int desc_get_tx_ls(dma_desc_t *p)
{
	return p->desc1.tx.last_seg;
}

void desc_release_tx(dma_desc_t *p)
{
	int ter = p->desc1.tx.end_ring;

	memset(p, 0, offsetof(dma_desc_t, desc2));
	desc_end_tx_desc(p, ter);
}

void desc_prepare_tx(dma_desc_t *p, int is_fs, int len, int csum_flag)
{
	p->desc1.tx.first_sg = is_fs;
	norm_set_tx_desc_len(p, len);

	if (likely(csum_flag))
		p->desc1.tx.cic = cic_full;
}

void desc_clear_tx_ic(dma_desc_t *p)
{
	p->desc1.tx.interrupt = 0;
}

void desc_close_tx(dma_desc_t *p)
{
	p->desc1.tx.last_seg = 1;
	p->desc1.tx.interrupt = 1;
}

int desc_get_rx_frame_len(dma_desc_t *p)
{
	return p->desc0.rx.frm_len;
}

#if defined(CONFIG_GMAC_RING)
unsigned int gmac_jumbo_frm(void *p, struct sk_buff *skb, int csum)
{
	struct gmac_priv *priv = (struct gmac_priv *) p;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry = priv->cur_tx % txsize;
	dma_desc_t *desc = priv->dma_tx + entry;
	unsigned int nopaged_len = skb_headlen(skb);
	unsigned int bmax, len;

	bmax = BUF_SIZE_2KiB;

	len = nopaged_len - bmax;

	if (nopaged_len > BUF_SIZE_8KiB) {

		desc->desc2 = dma_map_single(priv->device, skb->data,
					    bmax, DMA_TO_DEVICE);
		desc->desc3 = desc->desc2 + BUF_SIZE_4KiB;
		desc_prepare_tx(desc, 1, bmax, csum);

		entry = (++priv->cur_tx) % txsize;
		desc = priv->dma_tx + entry;

		desc->desc2 = dma_map_single(priv->device, skb->data + bmax,
					    len, DMA_TO_DEVICE);
		desc->desc3 = desc->desc2 + BUF_SIZE_4KiB;
		desc_prepare_tx(desc, 0, len, csum);
		desc_set_tx_own(desc);
		priv->tx_skbuff[entry] = NULL;
	} else {
		desc->desc2 = dma_map_single(priv->device, skb->data,
					    nopaged_len, DMA_TO_DEVICE);
		desc->desc3 = desc->desc2 + BUF_SIZE_4KiB;
		desc_prepare_tx(desc, 1, nopaged_len, csum);
	}

	return entry;
}

unsigned int gmac_is_jumbo_frm(int len)
{
	unsigned int ret = 0;

	if (len >= BUF_SIZE_4KiB)
		ret = 1;

	return ret;
}

void gmac_refill_desc3(int bfsize, dma_desc_t *p)
{
	/* Fill DES3 in case of RING mode */
	if (bfsize >= BUF_SIZE_8KiB)
		p->desc3 = p->desc2 + BUF_SIZE_8KiB;
}

/* In ring mode we need to fill the desc3 because it is used
 * as buffer */
void gmac_init_desc3(int desc3_as_data_buf, dma_desc_t *p)
{
	if (unlikely(desc3_as_data_buf))
		p->desc3 = p->desc2 + BUF_SIZE_8KiB;
}

void gmac_init_dma_chain(dma_desc_t *des, dma_addr_t phy_addr,
				  unsigned int size)
{
}

void gmac_clean_desc3(dma_desc_t *p)
{
	if (unlikely(p->desc3))
		p->desc3 = 0;
}

int gmac_set_16kib_bfsize(int mtu)
{
	int ret = 0;
	if (unlikely(mtu >= BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;
	return ret;
}

#else

unsigned int gmac_jumbo_frm(void *p, struct sk_buff *skb, int csum)
{
	struct gmac_priv *priv = (struct gmac_priv *) p;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry = priv->cur_tx % txsize;
	dma_desc_t *desc = priv->dma_tx + entry;
	unsigned int nopaged_len = skb_headlen(skb);
	unsigned int bmax;
	unsigned int i = 1, len;

	bmax = BUF_SIZE_2KiB;

	len = nopaged_len - bmax;

	desc->desc2 = dma_map_single(priv->device, skb->data,
				    bmax, DMA_TO_DEVICE);
	desc_prepare_tx(desc, 1, bmax, csum);

	while (len != 0) {
		entry = (++priv->cur_tx) % txsize;
		desc = priv->dma_tx + entry;

		if (len > bmax) {
			desc->desc2 = dma_map_single(priv->device,
						    (skb->data + bmax * i),
						    bmax, DMA_TO_DEVICE);
			desc_prepare_tx(desc, 0, bmax, csum);
			desc_set_tx_own(desc);
			priv->tx_skbuff[entry] = NULL;
			len -= bmax;
			i++;
		} else {
			desc->desc2 = dma_map_single(priv->device,
						    (skb->data + bmax * i), len,
						    DMA_TO_DEVICE);
			desc_prepare_tx(desc, 0, len, csum);
			desc_set_tx_own(desc);
			priv->tx_skbuff[entry] = NULL;
			len = 0;
		}
	}
	return entry;
}

unsigned int gmac_is_jumbo_frm(int len)
{
	unsigned int ret = 0;

	if (len > BUF_SIZE_2KiB) {
		ret = 1;
	}

	return ret;
}

void gmac_refill_desc3(int bfsize, dma_desc_t *p)
{
}

void gmac_init_desc3(int desc3_as_data_buf, dma_desc_t *p)
{
}

void gmac_clean_desc3(dma_desc_t *p)
{
}

void gmac_init_dma_chain(dma_desc_t *des, dma_addr_t phy_addr,
				  unsigned int size)
{
	/*
	 * In chained mode the desc3 points to the next element in the ring.
	 * The latest element has to point to the head.
	 */
	int i;
	dma_desc_t *p = des;
	dma_addr_t dma_phy = phy_addr;

	for (i = 0; i < (size - 1); i++) {
		dma_phy += sizeof(dma_desc_t);
		p->desc3 = (unsigned int)dma_phy;
		p++;
	}
	p->desc3 = (unsigned int)phy_addr;
}

int gmac_set_16kib_bfsize(int mtu)
{
	/* Not supported */
	return 0;
}

#if 0
const struct gmac_ring_mode_ops ring_mode_ops = {
	.is_jumbo_frm = gmac_is_jumbo_frm,
	.jumbo_frm = gmac_jumbo_frm,
	.refill_desc3 = gmac_refill_desc3,
	.init_desc3 = gmac_init_desc3,
	.init_dma_chain = gmac_init_dma_chain,
	.clean_desc3 = gmac_clean_desc3,
	.set_16kib_bfsize = gmac_set_16kib_bfsize,
};
#endif

#endif
