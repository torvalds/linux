/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoints for Thunderbolt/USB4 networking driver
 *
 * Copyright (C) 2023, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM thunderbolt_net

#if !defined(__TRACE_THUNDERBOLT_NET_H) || defined(TRACE_HEADER_MULTI_READ)
#define __TRACE_THUNDERBOLT_NET_H

#include <linux/dma-direction.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>

#define DMA_DATA_DIRECTION_NAMES			\
	{ DMA_BIDIRECTIONAL, "DMA_BIDIRECTIONAL" },	\
	{ DMA_TO_DEVICE, "DMA_TO_DEVICE" },		\
	{ DMA_FROM_DEVICE, "DMA_FROM_DEVICE" },		\
	{ DMA_NONE, "DMA_NONE" }

DECLARE_EVENT_CLASS(tbnet_frame,
	TP_PROTO(unsigned int index, const void *page, dma_addr_t phys,
		 enum dma_data_direction dir),
	TP_ARGS(index, page, phys, dir),
	TP_STRUCT__entry(
		__field(unsigned int, index)
		__field(const void *, page)
		__field(dma_addr_t, phys)
		__field(enum dma_data_direction, dir)
	),
	TP_fast_assign(
		__entry->index = index;
		__entry->page = page;
		__entry->phys = phys;
		__entry->dir = dir;
	),
	TP_printk("index=%u page=%p phys=%pad dir=%s",
		  __entry->index, __entry->page, &__entry->phys,
		__print_symbolic(__entry->dir, DMA_DATA_DIRECTION_NAMES))
);

DEFINE_EVENT(tbnet_frame, tbnet_alloc_rx_frame,
	TP_PROTO(unsigned int index, const void *page, dma_addr_t phys,
		 enum dma_data_direction dir),
	TP_ARGS(index, page, phys, dir)
);

DEFINE_EVENT(tbnet_frame, tbnet_alloc_tx_frame,
	TP_PROTO(unsigned int index, const void *page, dma_addr_t phys,
		 enum dma_data_direction dir),
	TP_ARGS(index, page, phys, dir)
);

DEFINE_EVENT(tbnet_frame, tbnet_free_frame,
	TP_PROTO(unsigned int index, const void *page, dma_addr_t phys,
		 enum dma_data_direction dir),
	TP_ARGS(index, page, phys, dir)
);

DECLARE_EVENT_CLASS(tbnet_ip_frame,
	TP_PROTO(__le32 size, __le16 id, __le16 index, __le32 count),
	TP_ARGS(size, id, index, count),
	TP_STRUCT__entry(
		__field(u32, size)
		__field(u16, id)
		__field(u16, index)
		__field(u32, count)
	),
	TP_fast_assign(
		__entry->size = le32_to_cpu(size);
		__entry->id = le16_to_cpu(id);
		__entry->index = le16_to_cpu(index);
		__entry->count = le32_to_cpu(count);
	),
	TP_printk("id=%u size=%u index=%u count=%u",
		  __entry->id, __entry->size, __entry->index, __entry->count)
);

DEFINE_EVENT(tbnet_ip_frame, tbnet_rx_ip_frame,
	TP_PROTO(__le32 size, __le16 id, __le16 index, __le32 count),
	TP_ARGS(size, id, index, count)
);

DEFINE_EVENT(tbnet_ip_frame, tbnet_invalid_rx_ip_frame,
	TP_PROTO(__le32 size, __le16 id, __le16 index, __le32 count),
	TP_ARGS(size, id, index, count)
);

DEFINE_EVENT(tbnet_ip_frame, tbnet_tx_ip_frame,
	TP_PROTO(__le32 size, __le16 id, __le16 index, __le32 count),
	TP_ARGS(size, id, index, count)
);

DECLARE_EVENT_CLASS(tbnet_skb,
	TP_PROTO(const struct sk_buff *skb),
	TP_ARGS(skb),
	TP_STRUCT__entry(
		__field(const void *, addr)
		__field(unsigned int, len)
		__field(unsigned int, data_len)
		__field(unsigned int, nr_frags)
	),
	TP_fast_assign(
		__entry->addr = skb;
		__entry->len = skb->len;
		__entry->data_len = skb->data_len;
		__entry->nr_frags = skb_shinfo(skb)->nr_frags;
	),
	TP_printk("skb=%p len=%u data_len=%u nr_frags=%u",
		  __entry->addr, __entry->len, __entry->data_len,
		  __entry->nr_frags)
);

DEFINE_EVENT(tbnet_skb, tbnet_rx_skb,
	TP_PROTO(const struct sk_buff *skb),
	TP_ARGS(skb)
);

DEFINE_EVENT(tbnet_skb, tbnet_tx_skb,
	TP_PROTO(const struct sk_buff *skb),
	TP_ARGS(skb)
);

DEFINE_EVENT(tbnet_skb, tbnet_consume_skb,
	TP_PROTO(const struct sk_buff *skb),
	TP_ARGS(skb)
);

#endif /* _TRACE_THUNDERBOLT_NET_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
