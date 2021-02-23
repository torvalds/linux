/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

/* This must be outside ifdef _HNS3_TRACE_H */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hns3

#if !defined(_HNS3_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HNS3_TRACE_H_

#include <linux/tracepoint.h>

#define DESC_NR		(sizeof(struct hns3_desc) / sizeof(u32))

DECLARE_EVENT_CLASS(hns3_skb_template,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(unsigned int, headlen)
		__field(unsigned int, len)
		__field(__u8, nr_frags)
		__field(__u8, ip_summed)
		__field(unsigned int, hdr_len)
		__field(unsigned short, gso_size)
		__field(unsigned short, gso_segs)
		__field(unsigned int, gso_type)
		__field(bool, fraglist)
		__array(__u32, size, MAX_SKB_FRAGS)
	),

	TP_fast_assign(
		__entry->headlen = skb_headlen(skb);
		__entry->len = skb->len;
		__entry->nr_frags = skb_shinfo(skb)->nr_frags;
		__entry->gso_size = skb_shinfo(skb)->gso_size;
		__entry->gso_segs = skb_shinfo(skb)->gso_segs;
		__entry->gso_type = skb_shinfo(skb)->gso_type;
		__entry->hdr_len = skb->encapsulation ?
		skb_inner_transport_offset(skb) + inner_tcp_hdrlen(skb) :
		skb_transport_offset(skb) + tcp_hdrlen(skb);
		__entry->ip_summed = skb->ip_summed;
		__entry->fraglist = skb_has_frag_list(skb);
		hns3_shinfo_pack(skb_shinfo(skb), __entry->size);
	),

	TP_printk(
		"len: %u, %u, %u, cs: %u, gso: %u, %u, %x, frag(%d %u): %s",
		__entry->headlen, __entry->len, __entry->hdr_len,
		__entry->ip_summed, __entry->gso_size, __entry->gso_segs,
		__entry->gso_type, __entry->fraglist, __entry->nr_frags,
		__print_array(__entry->size, MAX_SKB_FRAGS, sizeof(__u32))
	)
);

DEFINE_EVENT(hns3_skb_template, hns3_over_max_bd,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb));

DEFINE_EVENT(hns3_skb_template, hns3_gro,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb));

DEFINE_EVENT(hns3_skb_template, hns3_tso,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb));

TRACE_EVENT(hns3_tx_desc,
	TP_PROTO(struct hns3_enet_ring *ring, int cur_ntu),
	TP_ARGS(ring, cur_ntu),

	TP_STRUCT__entry(
		__field(int, index)
		__field(int, ntu)
		__field(int, ntc)
		__field(dma_addr_t, desc_dma)
		__array(u32, desc, DESC_NR)
		__string(devname, ring->tqp->handle->kinfo.netdev->name)
	),

	TP_fast_assign(
		__entry->index = ring->tqp->tqp_index;
		__entry->ntu = ring->next_to_use;
		__entry->ntc = ring->next_to_clean;
		__entry->desc_dma = ring->desc_dma_addr,
		memcpy(__entry->desc, &ring->desc[cur_ntu],
		       sizeof(struct hns3_desc));
		__assign_str(devname, ring->tqp->handle->kinfo.netdev->name);
	),

	TP_printk(
		"%s-%d-%d/%d desc(%pad): %s",
		__get_str(devname), __entry->index, __entry->ntu,
		__entry->ntc, &__entry->desc_dma,
		__print_array(__entry->desc, DESC_NR, sizeof(u32))
	)
);

TRACE_EVENT(hns3_rx_desc,
	TP_PROTO(struct hns3_enet_ring *ring),
	TP_ARGS(ring),

	TP_STRUCT__entry(
		__field(int, index)
		__field(int, ntu)
		__field(int, ntc)
		__field(dma_addr_t, desc_dma)
		__field(dma_addr_t, buf_dma)
		__array(u32, desc, DESC_NR)
		__string(devname, ring->tqp->handle->kinfo.netdev->name)
	),

	TP_fast_assign(
		__entry->index = ring->tqp->tqp_index;
		__entry->ntu = ring->next_to_use;
		__entry->ntc = ring->next_to_clean;
		__entry->desc_dma = ring->desc_dma_addr;
		__entry->buf_dma = ring->desc_cb[ring->next_to_clean].dma;
		memcpy(__entry->desc, &ring->desc[ring->next_to_clean],
		       sizeof(struct hns3_desc));
		__assign_str(devname, ring->tqp->handle->kinfo.netdev->name);
	),

	TP_printk(
		"%s-%d-%d/%d desc(%pad) buf(%pad): %s",
		__get_str(devname), __entry->index, __entry->ntu,
		__entry->ntc, &__entry->desc_dma, &__entry->buf_dma,
		__print_array(__entry->desc, DESC_NR, sizeof(u32))
	)
);

#endif /* _HNS3_TRACE_H_ */

/* This must be outside ifdef _HNS3_TRACE_H */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hns3_trace
#include <trace/define_trace.h>
