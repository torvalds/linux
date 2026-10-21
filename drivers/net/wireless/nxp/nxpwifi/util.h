/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: utility functions
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_UTIL_H_
#define _NXPWIFI_UTIL_H_
#include "fw.h"

struct nxpwifi_adapter;

struct nxpwifi_private;

struct nxpwifi_dma_mapping {
	dma_addr_t addr;
	size_t len;
};

struct nxpwifi_cb {
	struct nxpwifi_dma_mapping dma_mapping;
	union {
		struct nxpwifi_rxinfo rx_info;
		struct nxpwifi_txinfo tx_info;
	};
};

/* size/addr for nxpwifi_debug_info */
#define item_size(n)		(sizeof_field(struct nxpwifi_debug_info, n))
#define item_addr(n)		(offsetof(struct nxpwifi_debug_info, n))

/* size/addr for struct nxpwifi_adapter */
#define adapter_item_size(n)	(sizeof_field(struct nxpwifi_adapter, n))
#define adapter_item_addr(n)	(offsetof(struct nxpwifi_adapter, n))

struct nxpwifi_debug_data {
	char name[32];		/* variable/array name */
	u32 size;		/* size of the variable/array */
	size_t addr;		/* address of the variable/array */
	int num;		/* number of variables in an array */
};

static inline struct nxpwifi_rxinfo *NXPWIFI_SKB_RXCB(struct sk_buff *skb)
{
	struct nxpwifi_cb *cb = (struct nxpwifi_cb *)skb->cb;

	BUILD_BUG_ON(sizeof(struct nxpwifi_cb) > sizeof(skb->cb));
	return &cb->rx_info;
}

static inline struct nxpwifi_txinfo *NXPWIFI_SKB_TXCB(struct sk_buff *skb)
{
	struct nxpwifi_cb *cb = (struct nxpwifi_cb *)skb->cb;

	return &cb->tx_info;
}

static inline void nxpwifi_store_mapping(struct sk_buff *skb,
					 struct nxpwifi_dma_mapping *mapping)
{
	struct nxpwifi_cb *cb = (struct nxpwifi_cb *)skb->cb;

	memcpy(&cb->dma_mapping, mapping, sizeof(*mapping));
}

static inline void nxpwifi_get_mapping(struct sk_buff *skb,
				       struct nxpwifi_dma_mapping *mapping)
{
	struct nxpwifi_cb *cb = (struct nxpwifi_cb *)skb->cb;

	memcpy(mapping, &cb->dma_mapping, sizeof(*mapping));
}

static inline dma_addr_t NXPWIFI_SKB_DMA_ADDR(struct sk_buff *skb)
{
	struct nxpwifi_dma_mapping mapping;

	nxpwifi_get_mapping(skb, &mapping);

	return mapping.addr;
}

int nxpwifi_debug_info_to_buffer(struct nxpwifi_private *priv, char *buf,
				 struct nxpwifi_debug_info *info);

static inline void le16_unaligned_add_cpu(__le16 *var, u16 val)
{
	put_unaligned_le16(get_unaligned_le16(var) + val, var);
}

/*
 * Iterate over TLVs safely.
 * Ensures no out-of-bound access even if firmware sends malformed data.
 */
#define nxpwifi_for_each_tlv(tlv, buf, buf_len)				\
	for (tlv = (const struct nxpwifi_tlv *)(buf);				\
	     (u8 *)(tlv) + sizeof(*tlv) <= (u8 *)(buf) + (buf_len) &&		\
	     (u8 *)(tlv) + sizeof(*tlv) + le16_to_cpu(tlv->len) <=		\
	     (u8 *)(buf) + (buf_len);					\
	     tlv = (const struct nxpwifi_tlv *)((u8 *)(tlv) + sizeof(*tlv) +	\
	     le16_to_cpu(tlv->len)))

/* Return first TLV matching @type in given buffer. */
static inline const struct nxpwifi_tlv *
nxpwifi_find_tlv(u16 type, const u8 *buf, u32 buf_len)
{
	const struct nxpwifi_tlv *tlv;

	nxpwifi_for_each_tlv(tlv, buf, buf_len)	{
		if (le16_to_cpu(tlv->type) == type)
			return tlv;
	}

	return NULL;
}

int nxpwifi_append_data_tlv(u16 id, u8 *data, int len, u8 *pos, u8 *cmd_end);

int nxpwifi_download_vdll_block(struct nxpwifi_adapter *adapter,
				u8 *block, u16 block_len);

int nxpwifi_process_vdll_event(struct nxpwifi_private *priv,
			       struct sk_buff *skb);

u64 nxpwifi_roc_cookie(struct nxpwifi_adapter *adapter);

void nxpwifi_queue_work(struct nxpwifi_adapter *adapter,
			struct work_struct *work);

void nxpwifi_queue_delayed_work(struct nxpwifi_adapter *adapter,
				struct delayed_work *dwork,
				unsigned long delay);

void nxpwifi_queue_wiphy_work(struct nxpwifi_adapter *adapter,
			      struct wiphy_work *work);

void nxpwifi_queue_delayed_wiphy_work(struct nxpwifi_adapter *adapter,
				      struct wiphy_delayed_work *dwork,
				      unsigned long delay);

/*
 * Firmware cannot run AP and STA on different channels simultaneously,
 * and doing so may trigger a crash. Check whether check_chan can be set
 * safely; return true if allowed, false if another channel is already
 * active in firmware.
 */
bool nxpwifi_is_channel_setting_allowable(struct nxpwifi_private *priv,
					  struct ieee80211_channel *check_chan);

void nxpwifi_convert_chan_to_band_cfg(struct nxpwifi_private *priv,
				      u8 *band_cfg,
				      struct cfg80211_chan_def *chan_def);

#endif /* !_NXPWIFI_UTIL_H_ */
