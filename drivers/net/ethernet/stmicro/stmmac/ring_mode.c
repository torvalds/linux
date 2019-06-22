/*******************************************************************************
  Specialised functions for managing Ring mode

  Copyright(C) 2011  STMicroelectronics Ltd

  It defines all the functions used to handle the normal/enhanced
  descriptors in case of the DMA is configured to work in chained or
  in ring mode.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "stmmac.h"

static int jumbo_frm(void *p, struct sk_buff *skb, int csum)
{
	struct stmmac_tx_queue *tx_q = (struct stmmac_tx_queue *)p;
	unsigned int nopaged_len = skb_headlen(skb);
	struct stmmac_priv *priv = tx_q->priv_data;
	unsigned int entry = tx_q->cur_tx;
	unsigned int bmax, len, des2;
	struct dma_desc *desc;

	if (priv->extend_desc)
		desc = (struct dma_desc *)(tx_q->dma_etx + entry);
	else
		desc = tx_q->dma_tx + entry;

	if (priv->plat->enh_desc)
		bmax = BUF_SIZE_8KiB;
	else
		bmax = BUF_SIZE_2KiB;

	len = nopaged_len - bmax;

	if (nopaged_len > BUF_SIZE_8KiB) {

		des2 = dma_map_single(priv->device, skb->data, bmax,
				      DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		if (dma_mapping_error(priv->device, des2))
			return -1;

		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = bmax;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;

		desc->des3 = cpu_to_le32(des2 + BUF_SIZE_4KiB);
		stmmac_prepare_tx_desc(priv, desc, 1, bmax, csum,
				STMMAC_RING_MODE, 1, false, skb->len);
		tx_q->tx_skbuff[entry] = NULL;
		entry = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);

		if (priv->extend_desc)
			desc = (struct dma_desc *)(tx_q->dma_etx + entry);
		else
			desc = tx_q->dma_tx + entry;

		des2 = dma_map_single(priv->device, skb->data + bmax, len,
				      DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		if (dma_mapping_error(priv->device, des2))
			return -1;
		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;

		desc->des3 = cpu_to_le32(des2 + BUF_SIZE_4KiB);
		stmmac_prepare_tx_desc(priv, desc, 0, len, csum,
				STMMAC_RING_MODE, 1, true, skb->len);
	} else {
		des2 = dma_map_single(priv->device, skb->data,
				      nopaged_len, DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		if (dma_mapping_error(priv->device, des2))
			return -1;
		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = nopaged_len;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;
		desc->des3 = cpu_to_le32(des2 + BUF_SIZE_4KiB);
		stmmac_prepare_tx_desc(priv, desc, 1, nopaged_len, csum,
				STMMAC_RING_MODE, 1, true, skb->len);
	}

	tx_q->cur_tx = entry;

	return entry;
}

static unsigned int is_jumbo_frm(int len, int enh_desc)
{
	unsigned int ret = 0;

	if (len >= BUF_SIZE_4KiB)
		ret = 1;

	return ret;
}

static void refill_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_rx_queue *rx_q = priv_ptr;
	struct stmmac_priv *priv = rx_q->priv_data;

	/* Fill DES3 in case of RING mode */
	if (priv->dma_buf_sz == BUF_SIZE_16KiB)
		p->des3 = cpu_to_le32(le32_to_cpu(p->des2) + BUF_SIZE_8KiB);
}

/* In ring mode we need to fill the desc3 because it is used as buffer */
static void init_desc3(struct dma_desc *p)
{
	p->des3 = cpu_to_le32(le32_to_cpu(p->des2) + BUF_SIZE_8KiB);
}

static void clean_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_tx_queue *tx_q = (struct stmmac_tx_queue *)priv_ptr;
	struct stmmac_priv *priv = tx_q->priv_data;
	unsigned int entry = tx_q->dirty_tx;

	/* des3 is only used for jumbo frames tx or time stamping */
	if (unlikely(tx_q->tx_skbuff_dma[entry].is_jumbo ||
		     (tx_q->tx_skbuff_dma[entry].last_segment &&
		      !priv->extend_desc && priv->hwts_tx_en)))
		p->des3 = 0;
}

static int set_16kib_bfsize(int mtu)
{
	int ret = 0;
	if (unlikely(mtu > BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;
	return ret;
}

const struct stmmac_mode_ops ring_mode_ops = {
	.is_jumbo_frm = is_jumbo_frm,
	.jumbo_frm = jumbo_frm,
	.refill_desc3 = refill_desc3,
	.init_desc3 = init_desc3,
	.clean_desc3 = clean_desc3,
	.set_16kib_bfsize = set_16kib_bfsize,
};
