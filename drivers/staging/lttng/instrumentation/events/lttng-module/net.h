#undef TRACE_SYSTEM
#define TRACE_SYSTEM net

#if !defined(_TRACE_NET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NET_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

TRACE_EVENT(net_dev_xmit,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
	TP_PROTO(struct sk_buff *skb,
		 int rc,
		 struct net_device *dev,
		 unsigned int skb_len),

	TP_ARGS(skb, rc, dev, skb_len),
#else
	TP_PROTO(struct sk_buff *skb,
		 int rc),

	TP_ARGS(skb, rc),
#endif

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	unsigned int,	len		)
		__field(	int,		rc		)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
		__string(	name,		dev->name	)
#else
		__string(	name,		skb->dev->name	)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
	TP_fast_assign(
		tp_assign(skbaddr, skb)
		tp_assign(len, skb_len)
		tp_assign(rc, rc)
		tp_strcpy(name, dev->name)
	),
#else
	TP_fast_assign(
		tp_assign(skbaddr, skb)
		tp_assign(len, skb->len)
		tp_assign(rc, rc)
		tp_strcpy(name, skb->dev->name)
	),
#endif

	TP_printk("dev=%s skbaddr=%p len=%u rc=%d",
		__get_str(name), __entry->skbaddr, __entry->len, __entry->rc)
)

DECLARE_EVENT_CLASS(net_dev_template,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	unsigned int,	len		)
		__string(	name,		skb->dev->name	)
	),

	TP_fast_assign(
		tp_assign(skbaddr, skb)
		tp_assign(len, skb->len)
		tp_strcpy(name, skb->dev->name)
	),

	TP_printk("dev=%s skbaddr=%p len=%u",
		__get_str(name), __entry->skbaddr, __entry->len)
)

DEFINE_EVENT(net_dev_template, net_dev_queue,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
)

DEFINE_EVENT(net_dev_template, netif_receive_skb,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
)

DEFINE_EVENT(net_dev_template, netif_rx,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
)
#endif /* _TRACE_NET_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
