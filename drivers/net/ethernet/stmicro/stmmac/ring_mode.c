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

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "stmmac.h"

static int stmmac_jumbo_frm(void *p, struct sk_buff *skb, int csum)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)p;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry = priv->cur_tx % txsize;
	struct dma_desc *desc;
	unsigned int nopaged_len = skb_headlen(skb);
	unsigned int bmax, len;

	if (priv->extend_desc)
		desc = (struct dma_desc *)(priv->dma_etx + entry);
	else
		desc = priv->dma_tx + entry;

	if (priv->plat->enh_desc)
		bmax = BUF_SIZE_8KiB;
	else
		bmax = BUF_SIZE_2KiB;

	len = nopaged_len - bmax;

	if (nopaged_len > BUF_SIZE_8KiB) {

		desc->des2 = dma_map_single(priv->device, skb->data,
					    bmax, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, desc->des2))
			return -1;

		priv->tx_skbuff_dma[entry].buf = desc->des2;
		desc->des3 = desc->des2 + BUF_SIZE_4KiB;
		priv->hw->desc->prepare_tx_desc(desc, 1, bmax, csum,
						STMMAC_RING_MODE);
		wmb();
		priv->tx_skbuff[entry] = NULL;
		entry = (++priv->cur_tx) % txsize;

		if (priv->extend_desc)
			desc = (struct dma_desc *)(priv->dma_etx + entry);
		else
			desc = priv->dma_tx + entry;

		desc->des2 = dma_map_single(priv->device, skb->data + bmax,
					    len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, desc->des2))
			return -1;
		priv->tx_skbuff_dma[entry].buf = desc->des2;
		desc->des3 = desc->des2 + BUF_SIZE_4KiB;
		priv->hw->desc->prepare_tx_desc(desc, 0, len, csum,
						STMMAC_RING_MODE);
		wmb();
		priv->hw->desc->set_tx_owner(desc);
	} else {
		desc->des2 = dma_map_single(priv->device, skb->data,
					    nopaged_len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, desc->des2))
			return -1;
		priv->tx_skbuff_dma[entry].buf = desc->des2;
		desc->des3 = desc->des2 + BUF_SIZE_4KiB;
		priv->hw->desc->prepare_tx_desc(desc, 1, nopaged_len, csum,
						STMMAC_RING_MODE);
	}

	return entry;
}

static unsigned int stmmac_is_jumbo_frm(int len, int enh_desc)
{
	unsigned int ret = 0;

	if (len >= BUF_SIZE_4KiB)
		ret = 1;

	return ret;
}

static void stmmac_refill_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)priv_ptr;

	/* Fill DES3 in case of RING mode */
	if (priv->dma_buf_sz >= BUF_SIZE_8KiB)
		p->des3 = p->des2 + BUF_SIZE_8KiB;
}

/* In ring mode we need to fill the desc3 because it is used as buffer */
static void stmmac_init_desc3(struct dma_desc *p)
{
	p->des3 = p->des2 + BUF_SIZE_8KiB;
}

static void stmmac_clean_desc3(void *priv_ptr, struct dma_desc *p)
{
	if (unlikely(p->des3))
		p->des3 = 0;
}

static int stmmac_set_16kib_bfsize(int mtu)
{
	int ret = 0;
	if (unlikely(mtu >= BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;
	return ret;
}

const struct stmmac_mode_ops ring_mode_ops = {
	.is_jumbo_frm = stmmac_is_jumbo_frm,
	.jumbo_frm = stmmac_jumbo_frm,
	.refill_desc3 = stmmac_refill_desc3,
	.init_desc3 = stmmac_init_desc3,
	.clean_desc3 = stmmac_clean_desc3,
	.set_16kib_bfsize = stmmac_set_16kib_bfsize,
};
