#undef TRACE_SYSTEM
#define TRACE_SYSTEM skb

#if !defined(_TRACE_SKB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SKB_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

/*
 * Tracepoint for free an sk_buff:
 */
TRACE_EVENT_MAP(kfree_skb,

	skb_kfree,

	TP_PROTO(struct sk_buff *skb, void *location),

	TP_ARGS(skb, location),

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	void *,		location	)
		__field(	unsigned short,	protocol	)
	),

	TP_fast_assign(
		tp_assign(skbaddr, skb)
		tp_assign(location, location)
		tp_assign(protocol, ntohs(skb->protocol))
	),

	TP_printk("skbaddr=%p protocol=%u location=%p",
		__entry->skbaddr, __entry->protocol, __entry->location)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
TRACE_EVENT_MAP(consume_skb,

	skb_consume,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(	void *,	skbaddr	)
	),

	TP_fast_assign(
		tp_assign(skbaddr, skb)
	),

	TP_printk("skbaddr=%p", __entry->skbaddr)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
TRACE_EVENT(skb_copy_datagram_iovec,

	TP_PROTO(const struct sk_buff *skb, int len),

	TP_ARGS(skb, len),

	TP_STRUCT__entry(
		__field(	const void *,		skbaddr		)
		__field(	int,			len		)
	),

	TP_fast_assign(
		tp_assign(skbaddr, skb)
		tp_assign(len, len)
	),

	TP_printk("skbaddr=%p len=%d", __entry->skbaddr, __entry->len)
)
#endif

#endif /* _TRACE_SKB_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
