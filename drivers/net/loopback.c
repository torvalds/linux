/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Pseudo-driver for the loopback interface.
 *
 * Version:	@(#)loopback.c	1.0.4b	08/16/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@scyld.com>
 *
 *		Alan Cox	:	Fixed oddments for NET3.014
 *		Alan Cox	:	Rejig for NET3.029 snap #3
 *		Alan Cox	: 	Fixed NET3.029 bugs and sped up
 *		Larry McVoy	:	Tiny tweak to double performance
 *		Alan Cox	:	Backed out LMV's tweak - the linux mm
 *					can't take it...
 *              Michael Griffith:       Don't bother computing the checksums
 *                                      on packets received on the loopback
 *                                      interface.
 *		Alexey Kuznetsov:	Potential hang under some extreme
 *					cases removed.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <linux/if_ether.h>	/* For the statistics structure. */
#include <linux/if_arp.h>	/* For ARPHRD_ETHER */
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/percpu.h>

struct pcpu_lstats {
	unsigned long packets;
	unsigned long bytes;
};
static DEFINE_PER_CPU(struct pcpu_lstats, pcpu_lstats);

#define LOOPBACK_OVERHEAD (128 + MAX_HEADER + 16 + 16)

/* KISS: just allocate small chunks and copy bits.
 *
 * So, in fact, this is documentation, explaining what we expect
 * of largesending device modulo TCP checksum, which is ignored for loopback.
 */

#ifdef LOOPBACK_TSO
static void emulate_large_send_offload(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th = (struct tcphdr *)(skb_network_header(skb) +
					      (iph->ihl * 4));
	unsigned int doffset = (iph->ihl + th->doff) * 4;
	unsigned int mtu = skb_shinfo(skb)->gso_size + doffset;
	unsigned int offset = 0;
	u32 seq = ntohl(th->seq);
	u16 id  = ntohs(iph->id);

	while (offset + doffset < skb->len) {
		unsigned int frag_size = min(mtu, skb->len - offset) - doffset;
		struct sk_buff *nskb = alloc_skb(mtu + 32, GFP_ATOMIC);

		if (!nskb)
			break;
		skb_reserve(nskb, 32);
		skb_set_mac_header(nskb, -ETH_HLEN);
		skb_reset_network_header(nskb);
		iph = ip_hdr(nskb);
		skb_copy_to_linear_data(nskb, skb_network_header(skb),
					doffset);
		if (skb_copy_bits(skb,
				  doffset + offset,
				  nskb->data + doffset,
				  frag_size))
			BUG();
		skb_put(nskb, doffset + frag_size);
		nskb->ip_summed = CHECKSUM_UNNECESSARY;
		nskb->dev = skb->dev;
		nskb->priority = skb->priority;
		nskb->protocol = skb->protocol;
		nskb->dst = dst_clone(skb->dst);
		memcpy(nskb->cb, skb->cb, sizeof(skb->cb));
		nskb->pkt_type = skb->pkt_type;

		th = (struct tcphdr *)(skb_network_header(nskb) + iph->ihl * 4);
		iph->tot_len = htons(frag_size + doffset);
		iph->id = htons(id);
		iph->check = 0;
		iph->check = ip_fast_csum((unsigned char *) iph, iph->ihl);
		th->seq = htonl(seq);
		if (offset + doffset + frag_size < skb->len)
			th->fin = th->psh = 0;
		netif_rx(nskb);
		offset += frag_size;
		seq += frag_size;
		id++;
	}

	dev_kfree_skb(skb);
}
#endif /* LOOPBACK_TSO */

/*
 * The higher levels take care of making this non-reentrant (it's
 * called with bh's disabled).
 */
static int loopback_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pcpu_lstats *lb_stats;

	skb_orphan(skb);

	skb->protocol = eth_type_trans(skb,dev);
#ifndef LOOPBACK_MUST_CHECKSUM
	skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif

#ifdef LOOPBACK_TSO
	if (skb_is_gso(skb)) {
		BUG_ON(skb->protocol != htons(ETH_P_IP));
		BUG_ON(ip_hdr(skb)->protocol != IPPROTO_TCP);

		emulate_large_send_offload(skb);
		return 0;
	}
#endif
	dev->last_rx = jiffies;

	/* it's OK to use __get_cpu_var() because BHs are off */
	lb_stats = &__get_cpu_var(pcpu_lstats);
	lb_stats->bytes += skb->len;
	lb_stats->packets++;

	netif_rx(skb);

	return 0;
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	unsigned long bytes = 0;
	unsigned long packets = 0;
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_lstats *lb_stats;

		lb_stats = &per_cpu(pcpu_lstats, i);
		bytes   += lb_stats->bytes;
		packets += lb_stats->packets;
	}
	stats->rx_packets = packets;
	stats->tx_packets = packets;
	stats->rx_bytes = bytes;
	stats->tx_bytes = bytes;
	return stats;
}

static u32 always_on(struct net_device *dev)
{
	return 1;
}

static const struct ethtool_ops loopback_ethtool_ops = {
	.get_link		= always_on,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
	.get_tx_csum		= always_on,
	.get_sg			= always_on,
	.get_rx_csum		= always_on,
};

/*
 * The loopback device is special. There is only one instance and
 * it is statically allocated. Don't do this for other devices.
 */
struct net_device loopback_dev = {
	.name	 		= "lo",
	.get_stats		= &get_stats,
	.mtu			= (16 * 1024) + 20 + 20 + 12,
	.hard_start_xmit	= loopback_xmit,
	.hard_header		= eth_header,
	.hard_header_cache	= eth_header_cache,
	.header_cache_update	= eth_header_cache_update,
	.hard_header_len	= ETH_HLEN,	/* 14	*/
	.addr_len		= ETH_ALEN,	/* 6	*/
	.tx_queue_len		= 0,
	.type			= ARPHRD_LOOPBACK,	/* 0x0001*/
	.rebuild_header		= eth_rebuild_header,
	.flags			= IFF_LOOPBACK,
	.features 		= NETIF_F_SG | NETIF_F_FRAGLIST
#ifdef LOOPBACK_TSO
				  | NETIF_F_TSO
#endif
				  | NETIF_F_NO_CSUM | NETIF_F_HIGHDMA
				  | NETIF_F_LLTX,
	.ethtool_ops		= &loopback_ethtool_ops,
};

/* Setup and register the loopback device. */
static int __init loopback_init(void)
{
	return register_netdev(&loopback_dev);
};

module_init(loopback_init);

EXPORT_SYMBOL(loopback_dev);
