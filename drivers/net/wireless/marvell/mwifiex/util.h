/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: utility functions
 *
 * Copyright 2011-2020 NXP
 */

#ifndef _MWIFIEX_UTIL_H_
#define _MWIFIEX_UTIL_H_

struct mwifiex_private;

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

/* size/addr for mwifiex_debug_info */
#define item_size(n)		(sizeof_field(struct mwifiex_debug_info, n))
#define item_addr(n)		(offsetof(struct mwifiex_debug_info, n))

/* size/addr for struct mwifiex_adapter */
#define adapter_item_size(n)	(sizeof_field(struct mwifiex_adapter, n))
#define adapter_item_addr(n)	(offsetof(struct mwifiex_adapter, n))

struct mwifiex_debug_data {
	char name[32];		/* variable/array name */
	u32 size;		/* size of the variable/array */
	size_t addr;		/* address of the variable/array */
	int num;		/* number of variables in an array */
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

int mwifiex_debug_info_to_buffer(struct mwifiex_private *priv, char *buf,
				 struct mwifiex_debug_info *info);

static inline void le16_unaligned_add_cpu(__le16 *var, u16 val)
{
	put_unaligned_le16(get_unaligned_le16(var) + val, var);
}

#endif /* !_MWIFIEX_UTIL_H_ */
