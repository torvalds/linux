/*
 * Marvell Wireless LAN device driver: utility functions
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef _MWIFIEX_UTIL_H_
#define _MWIFIEX_UTIL_H_

struct mwifiex_dma_mapping {
	dma_addr_t addr;
	size_t len;
};

struct mwifiex_cb {
	struct mwifiex_dma_mapping dma_mapping;
	union {
		struct mwifiex_rxinfo rx_info;
		struct mwifiex_txinfo tx_info;
	};
};

static inline struct mwifiex_rxinfo *MWIFIEX_SKB_RXCB(struct sk_buff *skb)
{
	struct mwifiex_cb *cb = (struct mwifiex_cb *)skb->cb;

	BUILD_BUG_ON(sizeof(struct mwifiex_cb) > sizeof(skb->cb));
	return &cb->rx_info;
}

static inline struct mwifiex_txinfo *MWIFIEX_SKB_TXCB(struct sk_buff *skb)
{
	struct mwifiex_cb *cb = (struct mwifiex_cb *)skb->cb;

	return &cb->tx_info;
}

static inline void mwifiex_store_mapping(struct sk_buff *skb,
					 struct mwifiex_dma_mapping *mapping)
{
	struct mwifiex_cb *cb = (struct mwifiex_cb *)skb->cb;

	memcpy(&cb->dma_mapping, mapping, sizeof(*mapping));
}

static inline void mwifiex_get_mapping(struct sk_buff *skb,
				       struct mwifiex_dma_mapping *mapping)
{
	struct mwifiex_cb *cb = (struct mwifiex_cb *)skb->cb;

	memcpy(mapping, &cb->dma_mapping, sizeof(*mapping));
}

static inline dma_addr_t MWIFIEX_SKB_DMA_ADDR(struct sk_buff *skb)
{
	struct mwifiex_dma_mapping mapping;

	mwifiex_get_mapping(skb, &mapping);

	return mapping.addr;
}

#endif /* !_MWIFIEX_UTIL_H_ */
