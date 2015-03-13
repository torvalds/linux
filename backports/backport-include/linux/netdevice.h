#ifndef __BACKPORT_NETDEVICE_H
#define __BACKPORT_NETDEVICE_H
#include_next <linux/netdevice.h>
#include <linux/netdev_features.h>
#include <linux/version.h>

/*
 * This is declared implicitly in newer kernels by netdevice.h using
 * this pointer in struct net_device, but declare it here anyway so
 * pointers to it are accepted as function arguments without warning.
 */
struct inet6_dev;

/* older kernels don't include this here, we need it */
#include <linux/ethtool.h>
#include <linux/rculist.h>
/*
 * new kernels include <net/netprio_cgroup.h> which
 * has this ... and some drivers rely on it :-(
 */
#include <linux/hardirq.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
/*
 * Backports note: if in-kernel support is provided we could then just
 * take the kernel's implementation of __dev_kfree_skb_irq() as it requires
 * raise_softirq_irqoff() which is not exported. For the backport case we
 * just use slightly less optimized version and we don't get the ability
 * to distinguish the two different reasons to free the skb -- whether it
 * was consumed or dropped.
 *
 * The upstream documentation for this:
 *
 * It is not allowed to call kfree_skb() or consume_skb() from hardware
 * interrupt context or with hardware interrupts being disabled.
 * (in_irq() || irqs_disabled())
 *
 * We provide four helpers that can be used in following contexts :
 *
 * dev_kfree_skb_irq(skb) when caller drops a packet from irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_irq(skb) when caller consumes a packet from irq context.
 *  Typically used in place of consume_skb(skb) in TX completion path
 *
 * dev_kfree_skb_any(skb) when caller doesn't know its current irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_any(skb) when caller doesn't know its current irq context,
 *  and consumed a packet. Used in place of consume_skb(skb)
 */
#define skb_free_reason LINUX_BACKPORT(skb_free_reason)
enum skb_free_reason {
	SKB_REASON_CONSUMED,
	SKB_REASON_DROPPED,
};

#define __dev_kfree_skb_irq LINUX_BACKPORT(__dev_kfree_skb_irq)
static inline void __dev_kfree_skb_irq(struct sk_buff *skb,
				       enum skb_free_reason reason)
{
	dev_kfree_skb_irq(skb);
}

#define __dev_kfree_skb_any LINUX_BACKPORT(__dev_kfree_skb_any)
static inline void __dev_kfree_skb_any(struct sk_buff *skb,
				       enum skb_free_reason reason)
{
	dev_kfree_skb_any(skb);
}

#define dev_consume_skb_irq LINUX_BACKPORT(dev_consume_skb_irq)
static inline void dev_consume_skb_irq(struct sk_buff *skb)
{
	dev_kfree_skb_irq(skb);
}

#define dev_consume_skb_any LINUX_BACKPORT(dev_consume_skb_any)
static inline void dev_consume_skb_any(struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,8)
#define netdev_set_default_ethtool_ops LINUX_BACKPORT(netdev_set_default_ethtool_ops)
extern void netdev_set_default_ethtool_ops(struct net_device *dev,
					   const struct ethtool_ops *ops);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
/*
 * BQL was added as of v3.3 but some Linux distributions
 * have backported BQL to their v3.2 kernels or older. To
 * address this we assume that they also enabled CONFIG_BQL
 * and test for that here and simply avoid adding the static
 * inlines if it was defined
 */
#ifndef CONFIG_BQL
#define netdev_tx_sent_queue LINUX_BACKPORT(netdev_tx_sent_queue)
static inline void netdev_tx_sent_queue(struct netdev_queue *dev_queue,
					unsigned int bytes)
{
}

#define netdev_sent_queue LINUX_BACKPORT(netdev_sent_queue)
static inline void netdev_sent_queue(struct net_device *dev, unsigned int bytes)
{
}

#define netdev_tx_completed_queue LINUX_BACKPORT(netdev_tx_completed_queue)
static inline void netdev_tx_completed_queue(struct netdev_queue *dev_queue,
					     unsigned pkts, unsigned bytes)
{
}

#define netdev_completed_queue LINUX_BACKPORT(netdev_completed_queue)
static inline void netdev_completed_queue(struct net_device *dev,
					  unsigned pkts, unsigned bytes)
{
}

#define netdev_tx_reset_queue LINUX_BACKPORT(netdev_tx_reset_queue)
static inline void netdev_tx_reset_queue(struct netdev_queue *q)
{
}

#define netdev_reset_queue LINUX_BACKPORT(netdev_reset_queue)
static inline void netdev_reset_queue(struct net_device *dev_queue)
{
}
#endif /* CONFIG_BQL */
#endif /* < 3.3 */

#ifndef NETDEV_PRE_UP
#define NETDEV_PRE_UP		0x000D
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
#define netdev_notifier_info_to_dev(ndev) ndev
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#define netdev_notify_peers(dev) netif_notify_peers(dev)
#define napi_gro_flush(napi, old) napi_gro_flush(napi)
#endif

#ifndef IFF_LIVE_ADDR_CHANGE
#define IFF_LIVE_ADDR_CHANGE 0x100000
#endif

#ifndef IFF_SUPP_NOFCS
#define IFF_SUPP_NOFCS	0x80000		/* device supports sending custom FCS */
#endif

#ifndef IFF_UNICAST_FLT
#define IFF_UNICAST_FLT	0x20000		/* Supports unicast filtering	*/
#endif

#ifndef QUEUE_STATE_ANY_XOFF
#define __QUEUE_STATE_DRV_XOFF __QUEUE_STATE_XOFF
#define __QUEUE_STATE_STACK_XOFF __QUEUE_STATE_XOFF
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
#define alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, txqs, rxqs) \
	alloc_netdev_mqs(sizeof_priv, name, setup, txqs, rxqs)

#undef alloc_netdev
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, 1, 1)

#undef alloc_netdev_mq
#define alloc_netdev_mq(sizeof_priv, name, name_assign_type, setup, count) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, count, \
			 count)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0) */

/*
 * This backports this commit from upstream:
 * commit 87757a917b0b3c0787e0563c679762152be81312
 * net: force a list_del() in unregister_netdevice_many()
 */
#if (!(LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,45) && \
       LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)) && \
     !(LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,23) && \
       LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)) && \
     !(LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,9) && \
       LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)) && \
     !(LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,2) && \
       LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)) && \
     (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)))
static inline void backport_unregister_netdevice_many(struct list_head *head)
{
	unregister_netdevice_many(head);

	if (!(head->next == LIST_POISON1 && head->prev == LIST_POISON2))
		list_del(head);
}
#define unregister_netdevice_many LINUX_BACKPORT(unregister_netdevice_many)
#endif

/*
 * Complicated way of saying: We only backport netdev_rss_key stuff on kernels
 * that either already have net_get_random_once() (>= 3.13) or where we've been
 * brave enough to backport it due to static keys, refer to backports commit
 * 8cb8816d for details on difficulty to backport that further down.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
#define __BACKPORT_NETDEV_RSS_KEY_FILL 1
#else
#ifdef __BACKPORT_NET_GET_RANDOM_ONCE
#define __BACKPORT_NETDEV_RSS_KEY_FILL 1
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0) */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) */

#ifdef __BACKPORT_NETDEV_RSS_KEY_FILL
/* RSS keys are 40 or 52 bytes long */
#define NETDEV_RSS_KEY_LEN 52
#define netdev_rss_key LINUX_BACKPORT(netdev_rss_key)
extern u8 netdev_rss_key[NETDEV_RSS_KEY_LEN];
#define netdev_rss_key_fill LINUX_BACKPORT(netdev_rss_key_fill)
void netdev_rss_key_fill(void *buffer, size_t len);
#endif /* __BACKPORT_NETDEV_RSS_KEY_FILL */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
#define napi_alloc_skb LINUX_BACKPORT(napi_alloc_skb)
static inline struct sk_buff *napi_alloc_skb(struct napi_struct *napi,
					     unsigned int length)
{
	return netdev_alloc_skb_ip_align(napi->dev, length);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) */

#ifndef IFF_TX_SKB_SHARING
#define IFF_TX_SKB_SHARING 0
#endif

#endif /* __BACKPORT_NETDEVICE_H */
