/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014 Mahesh Bandewar <maheshb@google.com>
 */
#ifndef __IPVLAN_H
#define __IPVLAN_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_link.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/inetdevice.h>
#include <linux/netfilter.h>
#include <net/ip.h>
#include <net/ip6_route.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/route.h>
#include <net/addrconf.h>
#include <net/l3mdev.h>

#define IPVLAN_DRV	"ipvlan"
#define IPV_DRV_VER	"0.1"

#define IPVLAN_HASH_SIZE	(1 << BITS_PER_BYTE)
#define IPVLAN_HASH_MASK	(IPVLAN_HASH_SIZE - 1)

#define IPVLAN_MAC_FILTER_BITS	8
#define IPVLAN_MAC_FILTER_SIZE	(1 << IPVLAN_MAC_FILTER_BITS)
#define IPVLAN_MAC_FILTER_MASK	(IPVLAN_MAC_FILTER_SIZE - 1)

#define IPVLAN_QBACKLOG_LIMIT	1000

typedef enum {
	IPVL_IPV6 = 0,
	IPVL_ICMPV6,
	IPVL_IPV4,
	IPVL_ARP,
} ipvl_hdr_type;

struct ipvl_pcpu_stats {
	u64			rx_pkts;
	u64			rx_bytes;
	u64			rx_mcast;
	u64			tx_pkts;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
	u32			rx_errs;
	u32			tx_drps;
};

struct ipvl_port;

struct ipvl_dev {
	struct net_device	*dev;
	struct list_head	pnode;
	struct ipvl_port	*port;
	struct net_device	*phy_dev;
	struct list_head	addrs;
	struct ipvl_pcpu_stats	__percpu *pcpu_stats;
	DECLARE_BITMAP(mac_filters, IPVLAN_MAC_FILTER_SIZE);
	netdev_features_t	sfeatures;
	u32			msg_enable;
	spinlock_t		addrs_lock;
};

struct ipvl_addr {
	struct ipvl_dev		*master; /* Back pointer to master */
	union {
		struct in6_addr	ip6;	 /* IPv6 address on logical interface */
		struct in_addr	ip4;	 /* IPv4 address on logical interface */
	} ipu;
#define ip6addr	ipu.ip6
#define ip4addr ipu.ip4
	struct hlist_node	hlnode;  /* Hash-table linkage */
	struct list_head	anode;   /* logical-interface linkage */
	ipvl_hdr_type		atype;
	struct rcu_head		rcu;
};

struct ipvl_port {
	struct net_device	*dev;
	possible_net_t		pnet;
	struct hlist_head	hlhead[IPVLAN_HASH_SIZE];
	struct list_head	ipvlans;
	u16			mode;
	u16			flags;
	u16			dev_id_start;
	struct work_struct	wq;
	struct sk_buff_head	backlog;
	int			count;
	struct ida		ida;
};

struct ipvl_skb_cb {
	bool tx_pkt;
};
#define IPVL_SKB_CB(_skb) ((struct ipvl_skb_cb *)&((_skb)->cb[0]))

static inline struct ipvl_port *ipvlan_port_get_rcu(const struct net_device *d)
{
	return rcu_dereference(d->rx_handler_data);
}

static inline struct ipvl_port *ipvlan_port_get_rcu_bh(const struct net_device *d)
{
	return rcu_dereference_bh(d->rx_handler_data);
}

static inline struct ipvl_port *ipvlan_port_get_rtnl(const struct net_device *d)
{
	return rtnl_dereference(d->rx_handler_data);
}

static inline bool ipvlan_is_private(const struct ipvl_port *port)
{
	return !!(port->flags & IPVLAN_F_PRIVATE);
}

static inline void ipvlan_mark_private(struct ipvl_port *port)
{
	port->flags |= IPVLAN_F_PRIVATE;
}

static inline void ipvlan_clear_private(struct ipvl_port *port)
{
	port->flags &= ~IPVLAN_F_PRIVATE;
}

static inline bool ipvlan_is_vepa(const struct ipvl_port *port)
{
	return !!(port->flags & IPVLAN_F_VEPA);
}

static inline void ipvlan_mark_vepa(struct ipvl_port *port)
{
	port->flags |= IPVLAN_F_VEPA;
}

static inline void ipvlan_clear_vepa(struct ipvl_port *port)
{
	port->flags &= ~IPVLAN_F_VEPA;
}

void ipvlan_init_secret(void);
unsigned int ipvlan_mac_hash(const unsigned char *addr);
rx_handler_result_t ipvlan_handle_frame(struct sk_buff **pskb);
void ipvlan_process_multicast(struct work_struct *work);
int ipvlan_queue_xmit(struct sk_buff *skb, struct net_device *dev);
void ipvlan_ht_addr_add(struct ipvl_dev *ipvlan, struct ipvl_addr *addr);
struct ipvl_addr *ipvlan_find_addr(const struct ipvl_dev *ipvlan,
				   const void *iaddr, bool is_v6);
bool ipvlan_addr_busy(struct ipvl_port *port, void *iaddr, bool is_v6);
void ipvlan_ht_addr_del(struct ipvl_addr *addr);
struct ipvl_addr *ipvlan_addr_lookup(struct ipvl_port *port, void *lyr3h,
				     int addr_type, bool use_dest);
void *ipvlan_get_L3_hdr(struct ipvl_port *port, struct sk_buff *skb, int *type);
void ipvlan_count_rx(const struct ipvl_dev *ipvlan,
		     unsigned int len, bool success, bool mcast);
int ipvlan_link_new(struct net *src_net, struct net_device *dev,
		    struct nlattr *tb[], struct nlattr *data[],
		    struct netlink_ext_ack *extack);
void ipvlan_link_delete(struct net_device *dev, struct list_head *head);
void ipvlan_link_setup(struct net_device *dev);
int ipvlan_link_register(struct rtnl_link_ops *ops);
#ifdef CONFIG_IPVLAN_L3S
int ipvlan_l3s_register(struct ipvl_port *port);
void ipvlan_l3s_unregister(struct ipvl_port *port);
void ipvlan_migrate_l3s_hook(struct net *oldnet, struct net *newnet);
int ipvlan_l3s_init(void);
void ipvlan_l3s_cleanup(void);
#else
static inline int ipvlan_l3s_register(struct ipvl_port *port)
{
	return -ENOTSUPP;
}

static inline void ipvlan_l3s_unregister(struct ipvl_port *port)
{
}

static inline void ipvlan_migrate_l3s_hook(struct net *oldnet,
					   struct net *newnet)
{
}

static inline int ipvlan_l3s_init(void)
{
	return 0;
}

static inline void ipvlan_l3s_cleanup(void)
{
}
#endif /* CONFIG_IPVLAN_L3S */

static inline bool netif_is_ipvlan_port(const struct net_device *dev)
{
	return rcu_access_pointer(dev->rx_handler) == ipvlan_handle_frame;
}

#endif /* __IPVLAN_H */
