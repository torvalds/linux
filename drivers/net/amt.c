// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2021 Taehee Yoo <ap420073@gmail.com> */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/jhash.h>
#include <linux/if_tunnel.h>
#include <linux/net.h>
#include <linux/igmp.h>
#include <linux/workqueue.h>
#include <net/sch_generic.h>
#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/icmp.h>
#include <net/mld.h>
#include <net/amt.h>
#include <uapi/linux/amt.h>
#include <linux/security.h>
#include <net/gro_cells.h>
#include <net/ipv6.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>
#include <net/ip6_checksum.h>

static struct workqueue_struct *amt_wq;

static HLIST_HEAD(source_gc_list);
/* Lock for source_gc_list */
static spinlock_t source_gc_lock;
static struct delayed_work source_gc_wq;
static char *status_str[] = {
	"AMT_STATUS_INIT",
	"AMT_STATUS_SENT_DISCOVERY",
	"AMT_STATUS_RECEIVED_DISCOVERY",
	"AMT_STATUS_SENT_ADVERTISEMENT",
	"AMT_STATUS_RECEIVED_ADVERTISEMENT",
	"AMT_STATUS_SENT_REQUEST",
	"AMT_STATUS_RECEIVED_REQUEST",
	"AMT_STATUS_SENT_QUERY",
	"AMT_STATUS_RECEIVED_QUERY",
	"AMT_STATUS_SENT_UPDATE",
	"AMT_STATUS_RECEIVED_UPDATE",
};

static char *type_str[] = {
	"", /* Type 0 is not defined */
	"AMT_MSG_DISCOVERY",
	"AMT_MSG_ADVERTISEMENT",
	"AMT_MSG_REQUEST",
	"AMT_MSG_MEMBERSHIP_QUERY",
	"AMT_MSG_MEMBERSHIP_UPDATE",
	"AMT_MSG_MULTICAST_DATA",
	"AMT_MSG_TEARDOWN",
};

static char *action_str[] = {
	"AMT_ACT_GMI",
	"AMT_ACT_GMI_ZERO",
	"AMT_ACT_GT",
	"AMT_ACT_STATUS_FWD_NEW",
	"AMT_ACT_STATUS_D_FWD_NEW",
	"AMT_ACT_STATUS_NONE_NEW",
};

static struct igmpv3_grec igmpv3_zero_grec;

#if IS_ENABLED(CONFIG_IPV6)
#define MLD2_ALL_NODE_INIT { { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 } } }
static struct in6_addr mld2_all_node = MLD2_ALL_NODE_INIT;
static struct mld2_grec mldv2_zero_grec;
#endif

static struct amt_skb_cb *amt_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct amt_skb_cb) + sizeof(struct qdisc_skb_cb) >
		     sizeof_field(struct sk_buff, cb));

	return (struct amt_skb_cb *)((void *)skb->cb +
		sizeof(struct qdisc_skb_cb));
}

static void __amt_source_gc_work(void)
{
	struct amt_source_node *snode;
	struct hlist_head gc_list;
	struct hlist_node *t;

	spin_lock_bh(&source_gc_lock);
	hlist_move_list(&source_gc_list, &gc_list);
	spin_unlock_bh(&source_gc_lock);

	hlist_for_each_entry_safe(snode, t, &gc_list, node) {
		hlist_del_rcu(&snode->node);
		kfree_rcu(snode, rcu);
	}
}

static void amt_source_gc_work(struct work_struct *work)
{
	__amt_source_gc_work();

	spin_lock_bh(&source_gc_lock);
	mod_delayed_work(amt_wq, &source_gc_wq,
			 msecs_to_jiffies(AMT_GC_INTERVAL));
	spin_unlock_bh(&source_gc_lock);
}

static bool amt_addr_equal(union amt_addr *a, union amt_addr *b)
{
	return !memcmp(a, b, sizeof(union amt_addr));
}

static u32 amt_source_hash(struct amt_tunnel_list *tunnel, union amt_addr *src)
{
	u32 hash = jhash(src, sizeof(*src), tunnel->amt->hash_seed);

	return reciprocal_scale(hash, tunnel->amt->hash_buckets);
}

static bool amt_status_filter(struct amt_source_node *snode,
			      enum amt_filter filter)
{
	bool rc = false;

	switch (filter) {
	case AMT_FILTER_FWD:
		if (snode->status == AMT_SOURCE_STATUS_FWD &&
		    snode->flags == AMT_SOURCE_OLD)
			rc = true;
		break;
	case AMT_FILTER_D_FWD:
		if (snode->status == AMT_SOURCE_STATUS_D_FWD &&
		    snode->flags == AMT_SOURCE_OLD)
			rc = true;
		break;
	case AMT_FILTER_FWD_NEW:
		if (snode->status == AMT_SOURCE_STATUS_FWD &&
		    snode->flags == AMT_SOURCE_NEW)
			rc = true;
		break;
	case AMT_FILTER_D_FWD_NEW:
		if (snode->status == AMT_SOURCE_STATUS_D_FWD &&
		    snode->flags == AMT_SOURCE_NEW)
			rc = true;
		break;
	case AMT_FILTER_ALL:
		rc = true;
		break;
	case AMT_FILTER_NONE_NEW:
		if (snode->status == AMT_SOURCE_STATUS_NONE &&
		    snode->flags == AMT_SOURCE_NEW)
			rc = true;
		break;
	case AMT_FILTER_BOTH:
		if ((snode->status == AMT_SOURCE_STATUS_D_FWD ||
		     snode->status == AMT_SOURCE_STATUS_FWD) &&
		    snode->flags == AMT_SOURCE_OLD)
			rc = true;
		break;
	case AMT_FILTER_BOTH_NEW:
		if ((snode->status == AMT_SOURCE_STATUS_D_FWD ||
		     snode->status == AMT_SOURCE_STATUS_FWD) &&
		    snode->flags == AMT_SOURCE_NEW)
			rc = true;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return rc;
}

static struct amt_source_node *amt_lookup_src(struct amt_tunnel_list *tunnel,
					      struct amt_group_node *gnode,
					      enum amt_filter filter,
					      union amt_addr *src)
{
	u32 hash = amt_source_hash(tunnel, src);
	struct amt_source_node *snode;

	hlist_for_each_entry_rcu(snode, &gnode->sources[hash], node)
		if (amt_status_filter(snode, filter) &&
		    amt_addr_equal(&snode->source_addr, src))
			return snode;

	return NULL;
}

static u32 amt_group_hash(struct amt_tunnel_list *tunnel, union amt_addr *group)
{
	u32 hash = jhash(group, sizeof(*group), tunnel->amt->hash_seed);

	return reciprocal_scale(hash, tunnel->amt->hash_buckets);
}

static struct amt_group_node *amt_lookup_group(struct amt_tunnel_list *tunnel,
					       union amt_addr *group,
					       union amt_addr *host,
					       bool v6)
{
	u32 hash = amt_group_hash(tunnel, group);
	struct amt_group_node *gnode;

	hlist_for_each_entry_rcu(gnode, &tunnel->groups[hash], node) {
		if (amt_addr_equal(&gnode->group_addr, group) &&
		    amt_addr_equal(&gnode->host_addr, host) &&
		    gnode->v6 == v6)
			return gnode;
	}

	return NULL;
}

static void amt_destroy_source(struct amt_source_node *snode)
{
	struct amt_group_node *gnode = snode->gnode;
	struct amt_tunnel_list *tunnel;

	tunnel = gnode->tunnel_list;

	if (!gnode->v6) {
		netdev_dbg(snode->gnode->amt->dev,
			   "Delete source %pI4 from %pI4\n",
			   &snode->source_addr.ip4,
			   &gnode->group_addr.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		netdev_dbg(snode->gnode->amt->dev,
			   "Delete source %pI6 from %pI6\n",
			   &snode->source_addr.ip6,
			   &gnode->group_addr.ip6);
#endif
	}

	cancel_delayed_work(&snode->source_timer);
	hlist_del_init_rcu(&snode->node);
	tunnel->nr_sources--;
	gnode->nr_sources--;
	spin_lock_bh(&source_gc_lock);
	hlist_add_head_rcu(&snode->node, &source_gc_list);
	spin_unlock_bh(&source_gc_lock);
}

static void amt_del_group(struct amt_dev *amt, struct amt_group_node *gnode)
{
	struct amt_source_node *snode;
	struct hlist_node *t;
	int i;

	if (cancel_delayed_work(&gnode->group_timer))
		dev_put(amt->dev);
	hlist_del_rcu(&gnode->node);
	gnode->tunnel_list->nr_groups--;

	if (!gnode->v6)
		netdev_dbg(amt->dev, "Leave group %pI4\n",
			   &gnode->group_addr.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	else
		netdev_dbg(amt->dev, "Leave group %pI6\n",
			   &gnode->group_addr.ip6);
#endif
	for (i = 0; i < amt->hash_buckets; i++)
		hlist_for_each_entry_safe(snode, t, &gnode->sources[i], node)
			amt_destroy_source(snode);

	/* tunnel->lock was acquired outside of amt_del_group()
	 * But rcu_read_lock() was acquired too so It's safe.
	 */
	kfree_rcu(gnode, rcu);
}

/* If a source timer expires with a router filter-mode for the group of
 * INCLUDE, the router concludes that traffic from this particular
 * source is no longer desired on the attached network, and deletes the
 * associated source record.
 */
static void amt_source_work(struct work_struct *work)
{
	struct amt_source_node *snode = container_of(to_delayed_work(work),
						     struct amt_source_node,
						     source_timer);
	struct amt_group_node *gnode = snode->gnode;
	struct amt_dev *amt = gnode->amt;
	struct amt_tunnel_list *tunnel;

	tunnel = gnode->tunnel_list;
	spin_lock_bh(&tunnel->lock);
	rcu_read_lock();
	if (gnode->filter_mode == MCAST_INCLUDE) {
		amt_destroy_source(snode);
		if (!gnode->nr_sources)
			amt_del_group(amt, gnode);
	} else {
		/* When a router filter-mode for a group is EXCLUDE,
		 * source records are only deleted when the group timer expires
		 */
		snode->status = AMT_SOURCE_STATUS_D_FWD;
	}
	rcu_read_unlock();
	spin_unlock_bh(&tunnel->lock);
}

static void amt_act_src(struct amt_tunnel_list *tunnel,
			struct amt_group_node *gnode,
			struct amt_source_node *snode,
			enum amt_act act)
{
	struct amt_dev *amt = tunnel->amt;

	switch (act) {
	case AMT_ACT_GMI:
		mod_delayed_work(amt_wq, &snode->source_timer,
				 msecs_to_jiffies(amt_gmi(amt)));
		break;
	case AMT_ACT_GMI_ZERO:
		cancel_delayed_work(&snode->source_timer);
		break;
	case AMT_ACT_GT:
		mod_delayed_work(amt_wq, &snode->source_timer,
				 gnode->group_timer.timer.expires);
		break;
	case AMT_ACT_STATUS_FWD_NEW:
		snode->status = AMT_SOURCE_STATUS_FWD;
		snode->flags = AMT_SOURCE_NEW;
		break;
	case AMT_ACT_STATUS_D_FWD_NEW:
		snode->status = AMT_SOURCE_STATUS_D_FWD;
		snode->flags = AMT_SOURCE_NEW;
		break;
	case AMT_ACT_STATUS_NONE_NEW:
		cancel_delayed_work(&snode->source_timer);
		snode->status = AMT_SOURCE_STATUS_NONE;
		snode->flags = AMT_SOURCE_NEW;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	if (!gnode->v6)
		netdev_dbg(amt->dev, "Source %pI4 from %pI4 Acted %s\n",
			   &snode->source_addr.ip4,
			   &gnode->group_addr.ip4,
			   action_str[act]);
#if IS_ENABLED(CONFIG_IPV6)
	else
		netdev_dbg(amt->dev, "Source %pI6 from %pI6 Acted %s\n",
			   &snode->source_addr.ip6,
			   &gnode->group_addr.ip6,
			   action_str[act]);
#endif
}

static struct amt_source_node *amt_alloc_snode(struct amt_group_node *gnode,
					       union amt_addr *src)
{
	struct amt_source_node *snode;

	snode = kzalloc(sizeof(*snode), GFP_ATOMIC);
	if (!snode)
		return NULL;

	memcpy(&snode->source_addr, src, sizeof(union amt_addr));
	snode->gnode = gnode;
	snode->status = AMT_SOURCE_STATUS_NONE;
	snode->flags = AMT_SOURCE_NEW;
	INIT_HLIST_NODE(&snode->node);
	INIT_DELAYED_WORK(&snode->source_timer, amt_source_work);

	return snode;
}

/* RFC 3810 - 7.2.2.  Definition of Filter Timers
 *
 *  Router Mode          Filter Timer         Actions/Comments
 *  -----------       -----------------       ----------------
 *
 *    INCLUDE             Not Used            All listeners in
 *                                            INCLUDE mode.
 *
 *    EXCLUDE             Timer > 0           At least one listener
 *                                            in EXCLUDE mode.
 *
 *    EXCLUDE             Timer == 0          No more listeners in
 *                                            EXCLUDE mode for the
 *                                            multicast address.
 *                                            If the Requested List
 *                                            is empty, delete
 *                                            Multicast Address
 *                                            Record.  If not, switch
 *                                            to INCLUDE filter mode;
 *                                            the sources in the
 *                                            Requested List are
 *                                            moved to the Include
 *                                            List, and the Exclude
 *                                            List is deleted.
 */
static void amt_group_work(struct work_struct *work)
{
	struct amt_group_node *gnode = container_of(to_delayed_work(work),
						    struct amt_group_node,
						    group_timer);
	struct amt_tunnel_list *tunnel = gnode->tunnel_list;
	struct amt_dev *amt = gnode->amt;
	struct amt_source_node *snode;
	bool delete_group = true;
	struct hlist_node *t;
	int i, buckets;

	buckets = amt->hash_buckets;

	spin_lock_bh(&tunnel->lock);
	if (gnode->filter_mode == MCAST_INCLUDE) {
		/* Not Used */
		spin_unlock_bh(&tunnel->lock);
		goto out;
	}

	rcu_read_lock();
	for (i = 0; i < buckets; i++) {
		hlist_for_each_entry_safe(snode, t,
					  &gnode->sources[i], node) {
			if (!delayed_work_pending(&snode->source_timer) ||
			    snode->status == AMT_SOURCE_STATUS_D_FWD) {
				amt_destroy_source(snode);
			} else {
				delete_group = false;
				snode->status = AMT_SOURCE_STATUS_FWD;
			}
		}
	}
	if (delete_group)
		amt_del_group(amt, gnode);
	else
		gnode->filter_mode = MCAST_INCLUDE;
	rcu_read_unlock();
	spin_unlock_bh(&tunnel->lock);
out:
	dev_put(amt->dev);
}

/* Non-existant group is created as INCLUDE {empty}:
 *
 * RFC 3376 - 5.1. Action on Change of Interface State
 *
 * If no interface state existed for that multicast address before
 * the change (i.e., the change consisted of creating a new
 * per-interface record), or if no state exists after the change
 * (i.e., the change consisted of deleting a per-interface record),
 * then the "non-existent" state is considered to have a filter mode
 * of INCLUDE and an empty source list.
 */
static struct amt_group_node *amt_add_group(struct amt_dev *amt,
					    struct amt_tunnel_list *tunnel,
					    union amt_addr *group,
					    union amt_addr *host,
					    bool v6)
{
	struct amt_group_node *gnode;
	u32 hash;
	int i;

	if (tunnel->nr_groups >= amt->max_groups)
		return ERR_PTR(-ENOSPC);

	gnode = kzalloc(sizeof(*gnode) +
			(sizeof(struct hlist_head) * amt->hash_buckets),
			GFP_ATOMIC);
	if (unlikely(!gnode))
		return ERR_PTR(-ENOMEM);

	gnode->amt = amt;
	gnode->group_addr = *group;
	gnode->host_addr = *host;
	gnode->v6 = v6;
	gnode->tunnel_list = tunnel;
	gnode->filter_mode = MCAST_INCLUDE;
	INIT_HLIST_NODE(&gnode->node);
	INIT_DELAYED_WORK(&gnode->group_timer, amt_group_work);
	for (i = 0; i < amt->hash_buckets; i++)
		INIT_HLIST_HEAD(&gnode->sources[i]);

	hash = amt_group_hash(tunnel, group);
	hlist_add_head_rcu(&gnode->node, &tunnel->groups[hash]);
	tunnel->nr_groups++;

	if (!gnode->v6)
		netdev_dbg(amt->dev, "Join group %pI4\n",
			   &gnode->group_addr.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	else
		netdev_dbg(amt->dev, "Join group %pI6\n",
			   &gnode->group_addr.ip6);
#endif

	return gnode;
}

static struct sk_buff *amt_build_igmp_gq(struct amt_dev *amt)
{
	u8 ra[AMT_IPHDR_OPTS] = { IPOPT_RA, 4, 0, 0 };
	int hlen = LL_RESERVED_SPACE(amt->dev);
	int tlen = amt->dev->needed_tailroom;
	struct igmpv3_query *ihv3;
	void *csum_start = NULL;
	__sum16 *csum = NULL;
	struct sk_buff *skb;
	struct ethhdr *eth;
	struct iphdr *iph;
	unsigned int len;
	int offset;

	len = hlen + tlen + sizeof(*iph) + AMT_IPHDR_OPTS + sizeof(*ihv3);
	skb = netdev_alloc_skb_ip_align(amt->dev, len);
	if (!skb)
		return NULL;

	skb_reserve(skb, hlen);
	skb_push(skb, sizeof(*eth));
	skb->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(skb);
	skb->priority = TC_PRIO_CONTROL;
	skb_put(skb, sizeof(*iph));
	skb_put_data(skb, ra, sizeof(ra));
	skb_put(skb, sizeof(*ihv3));
	skb_pull(skb, sizeof(*eth));
	skb_reset_network_header(skb);

	iph		= ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= (sizeof(struct iphdr) + AMT_IPHDR_OPTS) >> 2;
	iph->tos	= AMT_TOS;
	iph->tot_len	= htons(sizeof(*iph) + AMT_IPHDR_OPTS + sizeof(*ihv3));
	iph->frag_off	= htons(IP_DF);
	iph->ttl	= 1;
	iph->id		= 0;
	iph->protocol	= IPPROTO_IGMP;
	iph->daddr	= htonl(INADDR_ALLHOSTS_GROUP);
	iph->saddr	= htonl(INADDR_ANY);
	ip_send_check(iph);

	eth = eth_hdr(skb);
	ether_addr_copy(eth->h_source, amt->dev->dev_addr);
	ip_eth_mc_map(htonl(INADDR_ALLHOSTS_GROUP), eth->h_dest);
	eth->h_proto = htons(ETH_P_IP);

	ihv3		= skb_pull(skb, sizeof(*iph) + AMT_IPHDR_OPTS);
	skb_reset_transport_header(skb);
	ihv3->type	= IGMP_HOST_MEMBERSHIP_QUERY;
	ihv3->code	= 1;
	ihv3->group	= 0;
	ihv3->qqic	= amt->qi;
	ihv3->nsrcs	= 0;
	ihv3->resv	= 0;
	ihv3->suppress	= false;
	ihv3->qrv	= READ_ONCE(amt->net->ipv4.sysctl_igmp_qrv);
	ihv3->csum	= 0;
	csum		= &ihv3->csum;
	csum_start	= (void *)ihv3;
	*csum		= ip_compute_csum(csum_start, sizeof(*ihv3));
	offset		= skb_transport_offset(skb);
	skb->csum	= skb_checksum(skb, offset, skb->len - offset, 0);
	skb->ip_summed	= CHECKSUM_NONE;

	skb_push(skb, sizeof(*eth) + sizeof(*iph) + AMT_IPHDR_OPTS);

	return skb;
}

static void amt_update_gw_status(struct amt_dev *amt, enum amt_status status,
				 bool validate)
{
	if (validate && amt->status >= status)
		return;
	netdev_dbg(amt->dev, "Update GW status %s -> %s",
		   status_str[amt->status], status_str[status]);
	WRITE_ONCE(amt->status, status);
}

static void __amt_update_relay_status(struct amt_tunnel_list *tunnel,
				      enum amt_status status,
				      bool validate)
{
	if (validate && tunnel->status >= status)
		return;
	netdev_dbg(tunnel->amt->dev,
		   "Update Tunnel(IP = %pI4, PORT = %u) status %s -> %s",
		   &tunnel->ip4, ntohs(tunnel->source_port),
		   status_str[tunnel->status], status_str[status]);
	tunnel->status = status;
}

static void amt_update_relay_status(struct amt_tunnel_list *tunnel,
				    enum amt_status status, bool validate)
{
	spin_lock_bh(&tunnel->lock);
	__amt_update_relay_status(tunnel, status, validate);
	spin_unlock_bh(&tunnel->lock);
}

static void amt_send_discovery(struct amt_dev *amt)
{
	struct amt_header_discovery *amtd;
	int hlen, tlen, offset;
	struct socket *sock;
	struct udphdr *udph;
	struct sk_buff *skb;
	struct iphdr *iph;
	struct rtable *rt;
	struct flowi4 fl4;
	u32 len;
	int err;

	rcu_read_lock();
	sock = rcu_dereference(amt->sock);
	if (!sock)
		goto out;

	if (!netif_running(amt->stream_dev) || !netif_running(amt->dev))
		goto out;

	rt = ip_route_output_ports(amt->net, &fl4, sock->sk,
				   amt->discovery_ip, amt->local_ip,
				   amt->gw_port, amt->relay_port,
				   IPPROTO_UDP, 0,
				   amt->stream_dev->ifindex);
	if (IS_ERR(rt)) {
		amt->dev->stats.tx_errors++;
		goto out;
	}

	hlen = LL_RESERVED_SPACE(amt->dev);
	tlen = amt->dev->needed_tailroom;
	len = hlen + tlen + sizeof(*iph) + sizeof(*udph) + sizeof(*amtd);
	skb = netdev_alloc_skb_ip_align(amt->dev, len);
	if (!skb) {
		ip_rt_put(rt);
		amt->dev->stats.tx_errors++;
		goto out;
	}

	skb->priority = TC_PRIO_CONTROL;
	skb_dst_set(skb, &rt->dst);

	len = sizeof(*iph) + sizeof(*udph) + sizeof(*amtd);
	skb_reset_network_header(skb);
	skb_put(skb, len);
	amtd = skb_pull(skb, sizeof(*iph) + sizeof(*udph));
	amtd->version	= 0;
	amtd->type	= AMT_MSG_DISCOVERY;
	amtd->reserved	= 0;
	amtd->nonce	= amt->nonce;
	skb_push(skb, sizeof(*udph));
	skb_reset_transport_header(skb);
	udph		= udp_hdr(skb);
	udph->source	= amt->gw_port;
	udph->dest	= amt->relay_port;
	udph->len	= htons(sizeof(*udph) + sizeof(*amtd));
	udph->check	= 0;
	offset = skb_transport_offset(skb);
	skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);
	udph->check = csum_tcpudp_magic(amt->local_ip, amt->discovery_ip,
					sizeof(*udph) + sizeof(*amtd),
					IPPROTO_UDP, skb->csum);

	skb_push(skb, sizeof(*iph));
	iph		= ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= (sizeof(struct iphdr)) >> 2;
	iph->tos	= AMT_TOS;
	iph->frag_off	= 0;
	iph->ttl	= ip4_dst_hoplimit(&rt->dst);
	iph->daddr	= amt->discovery_ip;
	iph->saddr	= amt->local_ip;
	iph->protocol	= IPPROTO_UDP;
	iph->tot_len	= htons(len);

	skb->ip_summed = CHECKSUM_NONE;
	ip_select_ident(amt->net, skb, NULL);
	ip_send_check(iph);
	err = ip_local_out(amt->net, sock->sk, skb);
	if (unlikely(net_xmit_eval(err)))
		amt->dev->stats.tx_errors++;

	amt_update_gw_status(amt, AMT_STATUS_SENT_DISCOVERY, true);
out:
	rcu_read_unlock();
}

static void amt_send_request(struct amt_dev *amt, bool v6)
{
	struct amt_header_request *amtrh;
	int hlen, tlen, offset;
	struct socket *sock;
	struct udphdr *udph;
	struct sk_buff *skb;
	struct iphdr *iph;
	struct rtable *rt;
	struct flowi4 fl4;
	u32 len;
	int err;

	rcu_read_lock();
	sock = rcu_dereference(amt->sock);
	if (!sock)
		goto out;

	if (!netif_running(amt->stream_dev) || !netif_running(amt->dev))
		goto out;

	rt = ip_route_output_ports(amt->net, &fl4, sock->sk,
				   amt->remote_ip, amt->local_ip,
				   amt->gw_port, amt->relay_port,
				   IPPROTO_UDP, 0,
				   amt->stream_dev->ifindex);
	if (IS_ERR(rt)) {
		amt->dev->stats.tx_errors++;
		goto out;
	}

	hlen = LL_RESERVED_SPACE(amt->dev);
	tlen = amt->dev->needed_tailroom;
	len = hlen + tlen + sizeof(*iph) + sizeof(*udph) + sizeof(*amtrh);
	skb = netdev_alloc_skb_ip_align(amt->dev, len);
	if (!skb) {
		ip_rt_put(rt);
		amt->dev->stats.tx_errors++;
		goto out;
	}

	skb->priority = TC_PRIO_CONTROL;
	skb_dst_set(skb, &rt->dst);

	len = sizeof(*iph) + sizeof(*udph) + sizeof(*amtrh);
	skb_reset_network_header(skb);
	skb_put(skb, len);
	amtrh = skb_pull(skb, sizeof(*iph) + sizeof(*udph));
	amtrh->version	 = 0;
	amtrh->type	 = AMT_MSG_REQUEST;
	amtrh->reserved1 = 0;
	amtrh->p	 = v6;
	amtrh->reserved2 = 0;
	amtrh->nonce	 = amt->nonce;
	skb_push(skb, sizeof(*udph));
	skb_reset_transport_header(skb);
	udph		= udp_hdr(skb);
	udph->source	= amt->gw_port;
	udph->dest	= amt->relay_port;
	udph->len	= htons(sizeof(*amtrh) + sizeof(*udph));
	udph->check	= 0;
	offset = skb_transport_offset(skb);
	skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);
	udph->check = csum_tcpudp_magic(amt->local_ip, amt->remote_ip,
					sizeof(*udph) + sizeof(*amtrh),
					IPPROTO_UDP, skb->csum);

	skb_push(skb, sizeof(*iph));
	iph		= ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= (sizeof(struct iphdr)) >> 2;
	iph->tos	= AMT_TOS;
	iph->frag_off	= 0;
	iph->ttl	= ip4_dst_hoplimit(&rt->dst);
	iph->daddr	= amt->remote_ip;
	iph->saddr	= amt->local_ip;
	iph->protocol	= IPPROTO_UDP;
	iph->tot_len	= htons(len);

	skb->ip_summed = CHECKSUM_NONE;
	ip_select_ident(amt->net, skb, NULL);
	ip_send_check(iph);
	err = ip_local_out(amt->net, sock->sk, skb);
	if (unlikely(net_xmit_eval(err)))
		amt->dev->stats.tx_errors++;

out:
	rcu_read_unlock();
}

static void amt_send_igmp_gq(struct amt_dev *amt,
			     struct amt_tunnel_list *tunnel)
{
	struct sk_buff *skb;

	skb = amt_build_igmp_gq(amt);
	if (!skb)
		return;

	amt_skb_cb(skb)->tunnel = tunnel;
	dev_queue_xmit(skb);
}

#if IS_ENABLED(CONFIG_IPV6)
static struct sk_buff *amt_build_mld_gq(struct amt_dev *amt)
{
	u8 ra[AMT_IP6HDR_OPTS] = { IPPROTO_ICMPV6, 0, IPV6_TLV_ROUTERALERT,
				   2, 0, 0, IPV6_TLV_PAD1, IPV6_TLV_PAD1 };
	int hlen = LL_RESERVED_SPACE(amt->dev);
	int tlen = amt->dev->needed_tailroom;
	struct mld2_query *mld2q;
	void *csum_start = NULL;
	struct ipv6hdr *ip6h;
	struct sk_buff *skb;
	struct ethhdr *eth;
	u32 len;

	len = hlen + tlen + sizeof(*ip6h) + sizeof(ra) + sizeof(*mld2q);
	skb = netdev_alloc_skb_ip_align(amt->dev, len);
	if (!skb)
		return NULL;

	skb_reserve(skb, hlen);
	skb_push(skb, sizeof(*eth));
	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);
	skb->priority = TC_PRIO_CONTROL;
	skb->protocol = htons(ETH_P_IPV6);
	skb_put_zero(skb, sizeof(*ip6h));
	skb_put_data(skb, ra, sizeof(ra));
	skb_put_zero(skb, sizeof(*mld2q));
	skb_pull(skb, sizeof(*eth));
	skb_reset_network_header(skb);
	ip6h			= ipv6_hdr(skb);
	ip6h->payload_len	= htons(sizeof(ra) + sizeof(*mld2q));
	ip6h->nexthdr		= NEXTHDR_HOP;
	ip6h->hop_limit		= 1;
	ip6h->daddr		= mld2_all_node;
	ip6_flow_hdr(ip6h, 0, 0);

	if (ipv6_dev_get_saddr(amt->net, amt->dev, &ip6h->daddr, 0,
			       &ip6h->saddr)) {
		amt->dev->stats.tx_errors++;
		kfree_skb(skb);
		return NULL;
	}

	eth->h_proto = htons(ETH_P_IPV6);
	ether_addr_copy(eth->h_source, amt->dev->dev_addr);
	ipv6_eth_mc_map(&mld2_all_node, eth->h_dest);

	skb_pull(skb, sizeof(*ip6h) + sizeof(ra));
	skb_reset_transport_header(skb);
	mld2q			= (struct mld2_query *)icmp6_hdr(skb);
	mld2q->mld2q_mrc	= htons(1);
	mld2q->mld2q_type	= ICMPV6_MGM_QUERY;
	mld2q->mld2q_code	= 0;
	mld2q->mld2q_cksum	= 0;
	mld2q->mld2q_resv1	= 0;
	mld2q->mld2q_resv2	= 0;
	mld2q->mld2q_suppress	= 0;
	mld2q->mld2q_qrv	= amt->qrv;
	mld2q->mld2q_nsrcs	= 0;
	mld2q->mld2q_qqic	= amt->qi;
	csum_start		= (void *)mld2q;
	mld2q->mld2q_cksum = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					     sizeof(*mld2q),
					     IPPROTO_ICMPV6,
					     csum_partial(csum_start,
							  sizeof(*mld2q), 0));

	skb->ip_summed = CHECKSUM_NONE;
	skb_push(skb, sizeof(*eth) + sizeof(*ip6h) + sizeof(ra));
	return skb;
}

static void amt_send_mld_gq(struct amt_dev *amt, struct amt_tunnel_list *tunnel)
{
	struct sk_buff *skb;

	skb = amt_build_mld_gq(amt);
	if (!skb)
		return;

	amt_skb_cb(skb)->tunnel = tunnel;
	dev_queue_xmit(skb);
}
#else
static void amt_send_mld_gq(struct amt_dev *amt, struct amt_tunnel_list *tunnel)
{
}
#endif

static bool amt_queue_event(struct amt_dev *amt, enum amt_event event,
			    struct sk_buff *skb)
{
	int index;

	spin_lock_bh(&amt->lock);
	if (amt->nr_events >= AMT_MAX_EVENTS) {
		spin_unlock_bh(&amt->lock);
		return 1;
	}

	index = (amt->event_idx + amt->nr_events) % AMT_MAX_EVENTS;
	amt->events[index].event = event;
	amt->events[index].skb = skb;
	amt->nr_events++;
	amt->event_idx %= AMT_MAX_EVENTS;
	queue_work(amt_wq, &amt->event_wq);
	spin_unlock_bh(&amt->lock);

	return 0;
}

static void amt_secret_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(to_delayed_work(work),
					   struct amt_dev,
					   secret_wq);

	spin_lock_bh(&amt->lock);
	get_random_bytes(&amt->key, sizeof(siphash_key_t));
	spin_unlock_bh(&amt->lock);
	mod_delayed_work(amt_wq, &amt->secret_wq,
			 msecs_to_jiffies(AMT_SECRET_TIMEOUT));
}

static void amt_event_send_discovery(struct amt_dev *amt)
{
	if (amt->status > AMT_STATUS_SENT_DISCOVERY)
		goto out;
	get_random_bytes(&amt->nonce, sizeof(__be32));

	amt_send_discovery(amt);
out:
	mod_delayed_work(amt_wq, &amt->discovery_wq,
			 msecs_to_jiffies(AMT_DISCOVERY_TIMEOUT));
}

static void amt_discovery_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(to_delayed_work(work),
					   struct amt_dev,
					   discovery_wq);

	if (amt_queue_event(amt, AMT_EVENT_SEND_DISCOVERY, NULL))
		mod_delayed_work(amt_wq, &amt->discovery_wq,
				 msecs_to_jiffies(AMT_DISCOVERY_TIMEOUT));
}

static void amt_event_send_request(struct amt_dev *amt)
{
	u32 exp;

	if (amt->status < AMT_STATUS_RECEIVED_ADVERTISEMENT)
		goto out;

	if (amt->req_cnt > AMT_MAX_REQ_COUNT) {
		netdev_dbg(amt->dev, "Gateway is not ready");
		amt->qi = AMT_INIT_REQ_TIMEOUT;
		WRITE_ONCE(amt->ready4, false);
		WRITE_ONCE(amt->ready6, false);
		amt->remote_ip = 0;
		amt_update_gw_status(amt, AMT_STATUS_INIT, false);
		amt->req_cnt = 0;
		amt->nonce = 0;
		goto out;
	}

	if (!amt->req_cnt) {
		WRITE_ONCE(amt->ready4, false);
		WRITE_ONCE(amt->ready6, false);
		get_random_bytes(&amt->nonce, sizeof(__be32));
	}

	amt_send_request(amt, false);
	amt_send_request(amt, true);
	amt_update_gw_status(amt, AMT_STATUS_SENT_REQUEST, true);
	amt->req_cnt++;
out:
	exp = min_t(u32, (1 * (1 << amt->req_cnt)), AMT_MAX_REQ_TIMEOUT);
	mod_delayed_work(amt_wq, &amt->req_wq, msecs_to_jiffies(exp * 1000));
}

static void amt_req_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(to_delayed_work(work),
					   struct amt_dev,
					   req_wq);

	if (amt_queue_event(amt, AMT_EVENT_SEND_REQUEST, NULL))
		mod_delayed_work(amt_wq, &amt->req_wq,
				 msecs_to_jiffies(100));
}

static bool amt_send_membership_update(struct amt_dev *amt,
				       struct sk_buff *skb,
				       bool v6)
{
	struct amt_header_membership_update *amtmu;
	struct socket *sock;
	struct iphdr *iph;
	struct flowi4 fl4;
	struct rtable *rt;
	int err;

	sock = rcu_dereference_bh(amt->sock);
	if (!sock)
		return true;

	err = skb_cow_head(skb, LL_RESERVED_SPACE(amt->dev) + sizeof(*amtmu) +
			   sizeof(*iph) + sizeof(struct udphdr));
	if (err)
		return true;

	skb_reset_inner_headers(skb);
	memset(&fl4, 0, sizeof(struct flowi4));
	fl4.flowi4_oif         = amt->stream_dev->ifindex;
	fl4.daddr              = amt->remote_ip;
	fl4.saddr              = amt->local_ip;
	fl4.flowi4_tos         = AMT_TOS;
	fl4.flowi4_proto       = IPPROTO_UDP;
	rt = ip_route_output_key(amt->net, &fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(amt->dev, "no route to %pI4\n", &amt->remote_ip);
		return true;
	}

	amtmu			= skb_push(skb, sizeof(*amtmu));
	amtmu->version		= 0;
	amtmu->type		= AMT_MSG_MEMBERSHIP_UPDATE;
	amtmu->reserved		= 0;
	amtmu->nonce		= amt->nonce;
	amtmu->response_mac	= amt->mac;

	if (!v6)
		skb_set_inner_protocol(skb, htons(ETH_P_IP));
	else
		skb_set_inner_protocol(skb, htons(ETH_P_IPV6));
	udp_tunnel_xmit_skb(rt, sock->sk, skb,
			    fl4.saddr,
			    fl4.daddr,
			    AMT_TOS,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    amt->gw_port,
			    amt->relay_port,
			    false,
			    false);
	amt_update_gw_status(amt, AMT_STATUS_SENT_UPDATE, true);
	return false;
}

static void amt_send_multicast_data(struct amt_dev *amt,
				    const struct sk_buff *oskb,
				    struct amt_tunnel_list *tunnel,
				    bool v6)
{
	struct amt_header_mcast_data *amtmd;
	struct socket *sock;
	struct sk_buff *skb;
	struct iphdr *iph;
	struct flowi4 fl4;
	struct rtable *rt;

	sock = rcu_dereference_bh(amt->sock);
	if (!sock)
		return;

	skb = skb_copy_expand(oskb, sizeof(*amtmd) + sizeof(*iph) +
			      sizeof(struct udphdr), 0, GFP_ATOMIC);
	if (!skb)
		return;

	skb_reset_inner_headers(skb);
	memset(&fl4, 0, sizeof(struct flowi4));
	fl4.flowi4_oif         = amt->stream_dev->ifindex;
	fl4.daddr              = tunnel->ip4;
	fl4.saddr              = amt->local_ip;
	fl4.flowi4_proto       = IPPROTO_UDP;
	rt = ip_route_output_key(amt->net, &fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(amt->dev, "no route to %pI4\n", &tunnel->ip4);
		kfree_skb(skb);
		return;
	}

	amtmd = skb_push(skb, sizeof(*amtmd));
	amtmd->version = 0;
	amtmd->reserved = 0;
	amtmd->type = AMT_MSG_MULTICAST_DATA;

	if (!v6)
		skb_set_inner_protocol(skb, htons(ETH_P_IP));
	else
		skb_set_inner_protocol(skb, htons(ETH_P_IPV6));
	udp_tunnel_xmit_skb(rt, sock->sk, skb,
			    fl4.saddr,
			    fl4.daddr,
			    AMT_TOS,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    amt->relay_port,
			    tunnel->source_port,
			    false,
			    false);
}

static bool amt_send_membership_query(struct amt_dev *amt,
				      struct sk_buff *skb,
				      struct amt_tunnel_list *tunnel,
				      bool v6)
{
	struct amt_header_membership_query *amtmq;
	struct socket *sock;
	struct rtable *rt;
	struct flowi4 fl4;
	int err;

	sock = rcu_dereference_bh(amt->sock);
	if (!sock)
		return true;

	err = skb_cow_head(skb, LL_RESERVED_SPACE(amt->dev) + sizeof(*amtmq) +
			   sizeof(struct iphdr) + sizeof(struct udphdr));
	if (err)
		return true;

	skb_reset_inner_headers(skb);
	memset(&fl4, 0, sizeof(struct flowi4));
	fl4.flowi4_oif         = amt->stream_dev->ifindex;
	fl4.daddr              = tunnel->ip4;
	fl4.saddr              = amt->local_ip;
	fl4.flowi4_tos         = AMT_TOS;
	fl4.flowi4_proto       = IPPROTO_UDP;
	rt = ip_route_output_key(amt->net, &fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(amt->dev, "no route to %pI4\n", &tunnel->ip4);
		return true;
	}

	amtmq		= skb_push(skb, sizeof(*amtmq));
	amtmq->version	= 0;
	amtmq->type	= AMT_MSG_MEMBERSHIP_QUERY;
	amtmq->reserved = 0;
	amtmq->l	= 0;
	amtmq->g	= 0;
	amtmq->nonce	= tunnel->nonce;
	amtmq->response_mac = tunnel->mac;

	if (!v6)
		skb_set_inner_protocol(skb, htons(ETH_P_IP));
	else
		skb_set_inner_protocol(skb, htons(ETH_P_IPV6));
	udp_tunnel_xmit_skb(rt, sock->sk, skb,
			    fl4.saddr,
			    fl4.daddr,
			    AMT_TOS,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    amt->relay_port,
			    tunnel->source_port,
			    false,
			    false);
	amt_update_relay_status(tunnel, AMT_STATUS_SENT_QUERY, true);
	return false;
}

static netdev_tx_t amt_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	struct amt_tunnel_list *tunnel;
	struct amt_group_node *gnode;
	union amt_addr group = {0,};
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *ip6h;
	struct mld_msg *mld;
#endif
	bool report = false;
	struct igmphdr *ih;
	bool query = false;
	struct iphdr *iph;
	bool data = false;
	bool v6 = false;
	u32 hash;

	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (!ipv4_is_multicast(iph->daddr))
			goto free;

		if (!ip_mc_check_igmp(skb)) {
			ih = igmp_hdr(skb);
			switch (ih->type) {
			case IGMPV3_HOST_MEMBERSHIP_REPORT:
			case IGMP_HOST_MEMBERSHIP_REPORT:
				report = true;
				break;
			case IGMP_HOST_MEMBERSHIP_QUERY:
				query = true;
				break;
			default:
				goto free;
			}
		} else {
			data = true;
		}
		v6 = false;
		group.ip4 = iph->daddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (iph->version == 6) {
		ip6h = ipv6_hdr(skb);
		if (!ipv6_addr_is_multicast(&ip6h->daddr))
			goto free;

		if (!ipv6_mc_check_mld(skb)) {
			mld = (struct mld_msg *)skb_transport_header(skb);
			switch (mld->mld_type) {
			case ICMPV6_MGM_REPORT:
			case ICMPV6_MLD2_REPORT:
				report = true;
				break;
			case ICMPV6_MGM_QUERY:
				query = true;
				break;
			default:
				goto free;
			}
		} else {
			data = true;
		}
		v6 = true;
		group.ip6 = ip6h->daddr;
#endif
	} else {
		dev->stats.tx_errors++;
		goto free;
	}

	if (!pskb_may_pull(skb, sizeof(struct ethhdr)))
		goto free;

	skb_pull(skb, sizeof(struct ethhdr));

	if (amt->mode == AMT_MODE_GATEWAY) {
		/* Gateway only passes IGMP/MLD packets */
		if (!report)
			goto free;
		if ((!v6 && !READ_ONCE(amt->ready4)) ||
		    (v6 && !READ_ONCE(amt->ready6)))
			goto free;
		if (amt_send_membership_update(amt, skb,  v6))
			goto free;
		goto unlock;
	} else if (amt->mode == AMT_MODE_RELAY) {
		if (query) {
			tunnel = amt_skb_cb(skb)->tunnel;
			if (!tunnel) {
				WARN_ON(1);
				goto free;
			}

			/* Do not forward unexpected query */
			if (amt_send_membership_query(amt, skb, tunnel, v6))
				goto free;
			goto unlock;
		}

		if (!data)
			goto free;
		list_for_each_entry_rcu(tunnel, &amt->tunnel_list, list) {
			hash = amt_group_hash(tunnel, &group);
			hlist_for_each_entry_rcu(gnode, &tunnel->groups[hash],
						 node) {
				if (!v6) {
					if (gnode->group_addr.ip4 == iph->daddr)
						goto found;
#if IS_ENABLED(CONFIG_IPV6)
				} else {
					if (ipv6_addr_equal(&gnode->group_addr.ip6,
							    &ip6h->daddr))
						goto found;
#endif
				}
			}
			continue;
found:
			amt_send_multicast_data(amt, skb, tunnel, v6);
		}
	}

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
free:
	dev_kfree_skb(skb);
unlock:
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static int amt_parse_type(struct sk_buff *skb)
{
	struct amt_header *amth;

	if (!pskb_may_pull(skb, sizeof(struct udphdr) +
			   sizeof(struct amt_header)))
		return -1;

	amth = (struct amt_header *)(udp_hdr(skb) + 1);

	if (amth->version != 0)
		return -1;

	if (amth->type >= __AMT_MSG_MAX || !amth->type)
		return -1;
	return amth->type;
}

static void amt_clear_groups(struct amt_tunnel_list *tunnel)
{
	struct amt_dev *amt = tunnel->amt;
	struct amt_group_node *gnode;
	struct hlist_node *t;
	int i;

	spin_lock_bh(&tunnel->lock);
	rcu_read_lock();
	for (i = 0; i < amt->hash_buckets; i++)
		hlist_for_each_entry_safe(gnode, t, &tunnel->groups[i], node)
			amt_del_group(amt, gnode);
	rcu_read_unlock();
	spin_unlock_bh(&tunnel->lock);
}

static void amt_tunnel_expire(struct work_struct *work)
{
	struct amt_tunnel_list *tunnel = container_of(to_delayed_work(work),
						      struct amt_tunnel_list,
						      gc_wq);
	struct amt_dev *amt = tunnel->amt;

	spin_lock_bh(&amt->lock);
	rcu_read_lock();
	list_del_rcu(&tunnel->list);
	amt->nr_tunnels--;
	amt_clear_groups(tunnel);
	rcu_read_unlock();
	spin_unlock_bh(&amt->lock);
	kfree_rcu(tunnel, rcu);
}

static void amt_cleanup_srcs(struct amt_dev *amt,
			     struct amt_tunnel_list *tunnel,
			     struct amt_group_node *gnode)
{
	struct amt_source_node *snode;
	struct hlist_node *t;
	int i;

	/* Delete old sources */
	for (i = 0; i < amt->hash_buckets; i++) {
		hlist_for_each_entry_safe(snode, t, &gnode->sources[i], node) {
			if (snode->flags == AMT_SOURCE_OLD)
				amt_destroy_source(snode);
		}
	}

	/* switch from new to old */
	for (i = 0; i < amt->hash_buckets; i++)  {
		hlist_for_each_entry_rcu(snode, &gnode->sources[i], node) {
			snode->flags = AMT_SOURCE_OLD;
			if (!gnode->v6)
				netdev_dbg(snode->gnode->amt->dev,
					   "Add source as OLD %pI4 from %pI4\n",
					   &snode->source_addr.ip4,
					   &gnode->group_addr.ip4);
#if IS_ENABLED(CONFIG_IPV6)
			else
				netdev_dbg(snode->gnode->amt->dev,
					   "Add source as OLD %pI6 from %pI6\n",
					   &snode->source_addr.ip6,
					   &gnode->group_addr.ip6);
#endif
		}
	}
}

static void amt_add_srcs(struct amt_dev *amt, struct amt_tunnel_list *tunnel,
			 struct amt_group_node *gnode, void *grec,
			 bool v6)
{
	struct igmpv3_grec *igmp_grec;
	struct amt_source_node *snode;
#if IS_ENABLED(CONFIG_IPV6)
	struct mld2_grec *mld_grec;
#endif
	union amt_addr src = {0,};
	u16 nsrcs;
	u32 hash;
	int i;

	if (!v6) {
		igmp_grec = (struct igmpv3_grec *)grec;
		nsrcs = ntohs(igmp_grec->grec_nsrcs);
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		mld_grec = (struct mld2_grec *)grec;
		nsrcs = ntohs(mld_grec->grec_nsrcs);
#else
	return;
#endif
	}
	for (i = 0; i < nsrcs; i++) {
		if (tunnel->nr_sources >= amt->max_sources)
			return;
		if (!v6)
			src.ip4 = igmp_grec->grec_src[i];
#if IS_ENABLED(CONFIG_IPV6)
		else
			memcpy(&src.ip6, &mld_grec->grec_src[i],
			       sizeof(struct in6_addr));
#endif
		if (amt_lookup_src(tunnel, gnode, AMT_FILTER_ALL, &src))
			continue;

		snode = amt_alloc_snode(gnode, &src);
		if (snode) {
			hash = amt_source_hash(tunnel, &snode->source_addr);
			hlist_add_head_rcu(&snode->node, &gnode->sources[hash]);
			tunnel->nr_sources++;
			gnode->nr_sources++;

			if (!gnode->v6)
				netdev_dbg(snode->gnode->amt->dev,
					   "Add source as NEW %pI4 from %pI4\n",
					   &snode->source_addr.ip4,
					   &gnode->group_addr.ip4);
#if IS_ENABLED(CONFIG_IPV6)
			else
				netdev_dbg(snode->gnode->amt->dev,
					   "Add source as NEW %pI6 from %pI6\n",
					   &snode->source_addr.ip6,
					   &gnode->group_addr.ip6);
#endif
		}
	}
}

/* Router State   Report Rec'd New Router State
 * ------------   ------------ ----------------
 * EXCLUDE (X,Y)  IS_IN (A)    EXCLUDE (X+A,Y-A)
 *
 * -----------+-----------+-----------+
 *            |    OLD    |    NEW    |
 * -----------+-----------+-----------+
 *    FWD     |     X     |    X+A    |
 * -----------+-----------+-----------+
 *    D_FWD   |     Y     |    Y-A    |
 * -----------+-----------+-----------+
 *    NONE    |           |     A     |
 * -----------+-----------+-----------+
 *
 * a) Received sources are NONE/NEW
 * b) All NONE will be deleted by amt_cleanup_srcs().
 * c) All OLD will be deleted by amt_cleanup_srcs().
 * d) After delete, NEW source will be switched to OLD.
 */
static void amt_lookup_act_srcs(struct amt_tunnel_list *tunnel,
				struct amt_group_node *gnode,
				void *grec,
				enum amt_ops ops,
				enum amt_filter filter,
				enum amt_act act,
				bool v6)
{
	struct amt_dev *amt = tunnel->amt;
	struct amt_source_node *snode;
	struct igmpv3_grec *igmp_grec;
#if IS_ENABLED(CONFIG_IPV6)
	struct mld2_grec *mld_grec;
#endif
	union amt_addr src = {0,};
	struct hlist_node *t;
	u16 nsrcs;
	int i, j;

	if (!v6) {
		igmp_grec = (struct igmpv3_grec *)grec;
		nsrcs = ntohs(igmp_grec->grec_nsrcs);
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		mld_grec = (struct mld2_grec *)grec;
		nsrcs = ntohs(mld_grec->grec_nsrcs);
#else
	return;
#endif
	}

	memset(&src, 0, sizeof(union amt_addr));
	switch (ops) {
	case AMT_OPS_INT:
		/* A*B */
		for (i = 0; i < nsrcs; i++) {
			if (!v6)
				src.ip4 = igmp_grec->grec_src[i];
#if IS_ENABLED(CONFIG_IPV6)
			else
				memcpy(&src.ip6, &mld_grec->grec_src[i],
				       sizeof(struct in6_addr));
#endif
			snode = amt_lookup_src(tunnel, gnode, filter, &src);
			if (!snode)
				continue;
			amt_act_src(tunnel, gnode, snode, act);
		}
		break;
	case AMT_OPS_UNI:
		/* A+B */
		for (i = 0; i < amt->hash_buckets; i++) {
			hlist_for_each_entry_safe(snode, t, &gnode->sources[i],
						  node) {
				if (amt_status_filter(snode, filter))
					amt_act_src(tunnel, gnode, snode, act);
			}
		}
		for (i = 0; i < nsrcs; i++) {
			if (!v6)
				src.ip4 = igmp_grec->grec_src[i];
#if IS_ENABLED(CONFIG_IPV6)
			else
				memcpy(&src.ip6, &mld_grec->grec_src[i],
				       sizeof(struct in6_addr));
#endif
			snode = amt_lookup_src(tunnel, gnode, filter, &src);
			if (!snode)
				continue;
			amt_act_src(tunnel, gnode, snode, act);
		}
		break;
	case AMT_OPS_SUB:
		/* A-B */
		for (i = 0; i < amt->hash_buckets; i++) {
			hlist_for_each_entry_safe(snode, t, &gnode->sources[i],
						  node) {
				if (!amt_status_filter(snode, filter))
					continue;
				for (j = 0; j < nsrcs; j++) {
					if (!v6)
						src.ip4 = igmp_grec->grec_src[j];
#if IS_ENABLED(CONFIG_IPV6)
					else
						memcpy(&src.ip6,
						       &mld_grec->grec_src[j],
						       sizeof(struct in6_addr));
#endif
					if (amt_addr_equal(&snode->source_addr,
							   &src))
						goto out_sub;
				}
				amt_act_src(tunnel, gnode, snode, act);
				continue;
out_sub:;
			}
		}
		break;
	case AMT_OPS_SUB_REV:
		/* B-A */
		for (i = 0; i < nsrcs; i++) {
			if (!v6)
				src.ip4 = igmp_grec->grec_src[i];
#if IS_ENABLED(CONFIG_IPV6)
			else
				memcpy(&src.ip6, &mld_grec->grec_src[i],
				       sizeof(struct in6_addr));
#endif
			snode = amt_lookup_src(tunnel, gnode, AMT_FILTER_ALL,
					       &src);
			if (!snode) {
				snode = amt_lookup_src(tunnel, gnode,
						       filter, &src);
				if (snode)
					amt_act_src(tunnel, gnode, snode, act);
			}
		}
		break;
	default:
		netdev_dbg(amt->dev, "Invalid type\n");
		return;
	}
}

static void amt_mcast_is_in_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * INCLUDE (A)    IS_IN (B)    INCLUDE (A+B)           (B)=GMI
 */
		/* Update IS_IN (B) as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_NONE_NEW,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* Update INCLUDE (A) as NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* (B)=GMI */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD_NEW,
				    AMT_ACT_GMI,
				    v6);
	} else {
/* State        Actions
 * ------------   ------------ ----------------        -------
 * EXCLUDE (X,Y)  IS_IN (A)    EXCLUDE (X+A,Y-A)       (A)=GMI
 */
		/* Update (A) in (X, Y) as NONE/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_BOTH,
				    AMT_ACT_STATUS_NONE_NEW,
				    v6);
		/* Update FWD/OLD as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, zero_grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* Update IS_IN (A) as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_NONE_NEW,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* Update EXCLUDE (, Y-A) as D_FWD_NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
	}
}

static void amt_mcast_is_ex_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd  New Router State         Actions
 * ------------   ------------  ----------------         -------
 * INCLUDE (A)    IS_EX (B)     EXCLUDE (A*B,B-A)        (B-A)=0
 *                                                       Delete (A-B)
 *                                                       Group Timer=GMI
 */
		/* EXCLUDE(A*B, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE(, B-A) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* (B-A)=0 */
		amt_lookup_act_srcs(tunnel, gnode, zero_grec, AMT_OPS_UNI,
				    AMT_FILTER_D_FWD_NEW,
				    AMT_ACT_GMI_ZERO,
				    v6);
		/* Group Timer=GMI */
		if (!mod_delayed_work(amt_wq, &gnode->group_timer,
				      msecs_to_jiffies(amt_gmi(amt))))
			dev_hold(amt->dev);
		gnode->filter_mode = MCAST_EXCLUDE;
		/* Delete (A-B) will be worked by amt_cleanup_srcs(). */
	} else {
/* Router State   Report Rec'd  New Router State	Actions
 * ------------   ------------  ----------------	-------
 * EXCLUDE (X,Y)  IS_EX (A)     EXCLUDE (A-Y,Y*A)	(A-X-Y)=GMI
 *							Delete (X-A)
 *							Delete (Y-A)
 *							Group Timer=GMI
 */
		/* EXCLUDE (A-Y, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, Y*A ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* (A-X-Y)=GMI */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_BOTH_NEW,
				    AMT_ACT_GMI,
				    v6);
		/* Group Timer=GMI */
		if (!mod_delayed_work(amt_wq, &gnode->group_timer,
				      msecs_to_jiffies(amt_gmi(amt))))
			dev_hold(amt->dev);
		/* Delete (X-A), (Y-A) will be worked by amt_cleanup_srcs(). */
	}
}

static void amt_mcast_to_in_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * INCLUDE (A)    TO_IN (B)    INCLUDE (A+B)           (B)=GMI
 *						       Send Q(G,A-B)
 */
		/* Update TO_IN (B) sources as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_NONE_NEW,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* Update INCLUDE (A) sources as NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* (B)=GMI */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD_NEW,
				    AMT_ACT_GMI,
				    v6);
	} else {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * EXCLUDE (X,Y)  TO_IN (A)    EXCLUDE (X+A,Y-A)       (A)=GMI
 *						       Send Q(G,X-A)
 *						       Send Q(G)
 */
		/* Update TO_IN (A) sources as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_NONE_NEW,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* Update EXCLUDE(X,) sources as FWD/NEW */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, Y-A)
		 * (A) are already switched to FWD_NEW.
		 * So, D_FWD/OLD -> D_FWD/NEW is okay.
		 */
		amt_lookup_act_srcs(tunnel, gnode, zero_grec, AMT_OPS_UNI,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* (A)=GMI
		 * Only FWD_NEW will have (A) sources.
		 */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD_NEW,
				    AMT_ACT_GMI,
				    v6);
	}
}

static void amt_mcast_to_ex_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * INCLUDE (A)    TO_EX (B)    EXCLUDE (A*B,B-A)       (B-A)=0
 *						       Delete (A-B)
 *						       Send Q(G,A*B)
 *						       Group Timer=GMI
 */
		/* EXCLUDE (A*B, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, B-A) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* (B-A)=0 */
		amt_lookup_act_srcs(tunnel, gnode, zero_grec, AMT_OPS_UNI,
				    AMT_FILTER_D_FWD_NEW,
				    AMT_ACT_GMI_ZERO,
				    v6);
		/* Group Timer=GMI */
		if (!mod_delayed_work(amt_wq, &gnode->group_timer,
				      msecs_to_jiffies(amt_gmi(amt))))
			dev_hold(amt->dev);
		gnode->filter_mode = MCAST_EXCLUDE;
		/* Delete (A-B) will be worked by amt_cleanup_srcs(). */
	} else {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * EXCLUDE (X,Y)  TO_EX (A)    EXCLUDE (A-Y,Y*A)       (A-X-Y)=Group Timer
 *						       Delete (X-A)
 *						       Delete (Y-A)
 *						       Send Q(G,A-Y)
 *						       Group Timer=GMI
 */
		/* Update (A-X-Y) as NONE/OLD */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_BOTH,
				    AMT_ACT_GT,
				    v6);
		/* EXCLUDE (A-Y, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, Y*A) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* Group Timer=GMI */
		if (!mod_delayed_work(amt_wq, &gnode->group_timer,
				      msecs_to_jiffies(amt_gmi(amt))))
			dev_hold(amt->dev);
		/* Delete (X-A), (Y-A) will be worked by amt_cleanup_srcs(). */
	}
}

static void amt_mcast_allow_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * INCLUDE (A)    ALLOW (B)    INCLUDE (A+B)	       (B)=GMI
 */
		/* INCLUDE (A+B) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* (B)=GMI */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD_NEW,
				    AMT_ACT_GMI,
				    v6);
	} else {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * EXCLUDE (X,Y)  ALLOW (A)    EXCLUDE (X+A,Y-A)       (A)=GMI
 */
		/* EXCLUDE (X+A, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, Y-A) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
		/* (A)=GMI
		 * All (A) source are now FWD/NEW status.
		 */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_INT,
				    AMT_FILTER_FWD_NEW,
				    AMT_ACT_GMI,
				    v6);
	}
}

static void amt_mcast_block_handler(struct amt_dev *amt,
				    struct amt_tunnel_list *tunnel,
				    struct amt_group_node *gnode,
				    void *grec, void *zero_grec, bool v6)
{
	if (gnode->filter_mode == MCAST_INCLUDE) {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * INCLUDE (A)    BLOCK (B)    INCLUDE (A)             Send Q(G,A*B)
 */
		/* INCLUDE (A) */
		amt_lookup_act_srcs(tunnel, gnode, zero_grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
	} else {
/* Router State   Report Rec'd New Router State        Actions
 * ------------   ------------ ----------------        -------
 * EXCLUDE (X,Y)  BLOCK (A)    EXCLUDE (X+(A-Y),Y)     (A-X-Y)=Group Timer
 *						       Send Q(G,A-Y)
 */
		/* (A-X-Y)=Group Timer */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_BOTH,
				    AMT_ACT_GT,
				    v6);
		/* EXCLUDE (X, ) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (X+(A-Y) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_SUB_REV,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_FWD_NEW,
				    v6);
		/* EXCLUDE (, Y) */
		amt_lookup_act_srcs(tunnel, gnode, grec, AMT_OPS_UNI,
				    AMT_FILTER_D_FWD,
				    AMT_ACT_STATUS_D_FWD_NEW,
				    v6);
	}
}

/* RFC 3376
 * 7.3.2. In the Presence of Older Version Group Members
 *
 * When Group Compatibility Mode is IGMPv2, a router internally
 * translates the following IGMPv2 messages for that group to their
 * IGMPv3 equivalents:
 *
 * IGMPv2 Message                IGMPv3 Equivalent
 * --------------                -----------------
 * Report                        IS_EX( {} )
 * Leave                         TO_IN( {} )
 */
static void amt_igmpv2_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				      struct amt_tunnel_list *tunnel)
{
	struct igmphdr *ih = igmp_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;

	memset(&group, 0, sizeof(union amt_addr));
	group.ip4 = ih->group;
	memset(&host, 0, sizeof(union amt_addr));
	host.ip4 = iph->saddr;

	gnode = amt_lookup_group(tunnel, &group, &host, false);
	if (!gnode) {
		gnode = amt_add_group(amt, tunnel, &group, &host, false);
		if (!IS_ERR(gnode)) {
			gnode->filter_mode = MCAST_EXCLUDE;
			if (!mod_delayed_work(amt_wq, &gnode->group_timer,
					      msecs_to_jiffies(amt_gmi(amt))))
				dev_hold(amt->dev);
		}
	}
}

/* RFC 3376
 * 7.3.2. In the Presence of Older Version Group Members
 *
 * When Group Compatibility Mode is IGMPv2, a router internally
 * translates the following IGMPv2 messages for that group to their
 * IGMPv3 equivalents:
 *
 * IGMPv2 Message                IGMPv3 Equivalent
 * --------------                -----------------
 * Report                        IS_EX( {} )
 * Leave                         TO_IN( {} )
 */
static void amt_igmpv2_leave_handler(struct amt_dev *amt, struct sk_buff *skb,
				     struct amt_tunnel_list *tunnel)
{
	struct igmphdr *ih = igmp_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;

	memset(&group, 0, sizeof(union amt_addr));
	group.ip4 = ih->group;
	memset(&host, 0, sizeof(union amt_addr));
	host.ip4 = iph->saddr;

	gnode = amt_lookup_group(tunnel, &group, &host, false);
	if (gnode)
		amt_del_group(amt, gnode);
}

static void amt_igmpv3_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				      struct amt_tunnel_list *tunnel)
{
	struct igmpv3_report *ihrv3 = igmpv3_report_hdr(skb);
	int len = skb_transport_offset(skb) + sizeof(*ihrv3);
	void *zero_grec = (void *)&igmpv3_zero_grec;
	struct iphdr *iph = ip_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;
	struct igmpv3_grec *grec;
	u16 nsrcs;
	int i;

	for (i = 0; i < ntohs(ihrv3->ngrec); i++) {
		len += sizeof(*grec);
		if (!ip_mc_may_pull(skb, len))
			break;

		grec = (void *)(skb->data + len - sizeof(*grec));
		nsrcs = ntohs(grec->grec_nsrcs);

		len += nsrcs * sizeof(__be32);
		if (!ip_mc_may_pull(skb, len))
			break;

		memset(&group, 0, sizeof(union amt_addr));
		group.ip4 = grec->grec_mca;
		memset(&host, 0, sizeof(union amt_addr));
		host.ip4 = iph->saddr;
		gnode = amt_lookup_group(tunnel, &group, &host, false);
		if (!gnode) {
			gnode = amt_add_group(amt, tunnel, &group, &host,
					      false);
			if (IS_ERR(gnode))
				continue;
		}

		amt_add_srcs(amt, tunnel, gnode, grec, false);
		switch (grec->grec_type) {
		case IGMPV3_MODE_IS_INCLUDE:
			amt_mcast_is_in_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		case IGMPV3_MODE_IS_EXCLUDE:
			amt_mcast_is_ex_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		case IGMPV3_CHANGE_TO_INCLUDE:
			amt_mcast_to_in_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		case IGMPV3_CHANGE_TO_EXCLUDE:
			amt_mcast_to_ex_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		case IGMPV3_ALLOW_NEW_SOURCES:
			amt_mcast_allow_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		case IGMPV3_BLOCK_OLD_SOURCES:
			amt_mcast_block_handler(amt, tunnel, gnode, grec,
						zero_grec, false);
			break;
		default:
			break;
		}
		amt_cleanup_srcs(amt, tunnel, gnode);
	}
}

/* caller held tunnel->lock */
static void amt_igmp_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				    struct amt_tunnel_list *tunnel)
{
	struct igmphdr *ih = igmp_hdr(skb);

	switch (ih->type) {
	case IGMPV3_HOST_MEMBERSHIP_REPORT:
		amt_igmpv3_report_handler(amt, skb, tunnel);
		break;
	case IGMPV2_HOST_MEMBERSHIP_REPORT:
		amt_igmpv2_report_handler(amt, skb, tunnel);
		break;
	case IGMP_HOST_LEAVE_MESSAGE:
		amt_igmpv2_leave_handler(amt, skb, tunnel);
		break;
	default:
		break;
	}
}

#if IS_ENABLED(CONFIG_IPV6)
/* RFC 3810
 * 8.3.2. In the Presence of MLDv1 Multicast Address Listeners
 *
 * When Multicast Address Compatibility Mode is MLDv2, a router acts
 * using the MLDv2 protocol for that multicast address.  When Multicast
 * Address Compatibility Mode is MLDv1, a router internally translates
 * the following MLDv1 messages for that multicast address to their
 * MLDv2 equivalents:
 *
 * MLDv1 Message                 MLDv2 Equivalent
 * --------------                -----------------
 * Report                        IS_EX( {} )
 * Done                          TO_IN( {} )
 */
static void amt_mldv1_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				     struct amt_tunnel_list *tunnel)
{
	struct mld_msg *mld = (struct mld_msg *)icmp6_hdr(skb);
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;

	memcpy(&group.ip6, &mld->mld_mca, sizeof(struct in6_addr));
	memcpy(&host.ip6, &ip6h->saddr, sizeof(struct in6_addr));

	gnode = amt_lookup_group(tunnel, &group, &host, true);
	if (!gnode) {
		gnode = amt_add_group(amt, tunnel, &group, &host, true);
		if (!IS_ERR(gnode)) {
			gnode->filter_mode = MCAST_EXCLUDE;
			if (!mod_delayed_work(amt_wq, &gnode->group_timer,
					      msecs_to_jiffies(amt_gmi(amt))))
				dev_hold(amt->dev);
		}
	}
}

/* RFC 3810
 * 8.3.2. In the Presence of MLDv1 Multicast Address Listeners
 *
 * When Multicast Address Compatibility Mode is MLDv2, a router acts
 * using the MLDv2 protocol for that multicast address.  When Multicast
 * Address Compatibility Mode is MLDv1, a router internally translates
 * the following MLDv1 messages for that multicast address to their
 * MLDv2 equivalents:
 *
 * MLDv1 Message                 MLDv2 Equivalent
 * --------------                -----------------
 * Report                        IS_EX( {} )
 * Done                          TO_IN( {} )
 */
static void amt_mldv1_leave_handler(struct amt_dev *amt, struct sk_buff *skb,
				    struct amt_tunnel_list *tunnel)
{
	struct mld_msg *mld = (struct mld_msg *)icmp6_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;

	memcpy(&group.ip6, &mld->mld_mca, sizeof(struct in6_addr));
	memset(&host, 0, sizeof(union amt_addr));
	host.ip4 = iph->saddr;

	gnode = amt_lookup_group(tunnel, &group, &host, true);
	if (gnode) {
		amt_del_group(amt, gnode);
		return;
	}
}

static void amt_mldv2_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				     struct amt_tunnel_list *tunnel)
{
	struct mld2_report *mld2r = (struct mld2_report *)icmp6_hdr(skb);
	int len = skb_transport_offset(skb) + sizeof(*mld2r);
	void *zero_grec = (void *)&mldv2_zero_grec;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct amt_group_node *gnode;
	union amt_addr group, host;
	struct mld2_grec *grec;
	u16 nsrcs;
	int i;

	for (i = 0; i < ntohs(mld2r->mld2r_ngrec); i++) {
		len += sizeof(*grec);
		if (!ipv6_mc_may_pull(skb, len))
			break;

		grec = (void *)(skb->data + len - sizeof(*grec));
		nsrcs = ntohs(grec->grec_nsrcs);

		len += nsrcs * sizeof(struct in6_addr);
		if (!ipv6_mc_may_pull(skb, len))
			break;

		memset(&group, 0, sizeof(union amt_addr));
		group.ip6 = grec->grec_mca;
		memset(&host, 0, sizeof(union amt_addr));
		host.ip6 = ip6h->saddr;
		gnode = amt_lookup_group(tunnel, &group, &host, true);
		if (!gnode) {
			gnode = amt_add_group(amt, tunnel, &group, &host,
					      ETH_P_IPV6);
			if (IS_ERR(gnode))
				continue;
		}

		amt_add_srcs(amt, tunnel, gnode, grec, true);
		switch (grec->grec_type) {
		case MLD2_MODE_IS_INCLUDE:
			amt_mcast_is_in_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		case MLD2_MODE_IS_EXCLUDE:
			amt_mcast_is_ex_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		case MLD2_CHANGE_TO_INCLUDE:
			amt_mcast_to_in_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		case MLD2_CHANGE_TO_EXCLUDE:
			amt_mcast_to_ex_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		case MLD2_ALLOW_NEW_SOURCES:
			amt_mcast_allow_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		case MLD2_BLOCK_OLD_SOURCES:
			amt_mcast_block_handler(amt, tunnel, gnode, grec,
						zero_grec, true);
			break;
		default:
			break;
		}
		amt_cleanup_srcs(amt, tunnel, gnode);
	}
}

/* caller held tunnel->lock */
static void amt_mld_report_handler(struct amt_dev *amt, struct sk_buff *skb,
				   struct amt_tunnel_list *tunnel)
{
	struct mld_msg *mld = (struct mld_msg *)icmp6_hdr(skb);

	switch (mld->mld_type) {
	case ICMPV6_MGM_REPORT:
		amt_mldv1_report_handler(amt, skb, tunnel);
		break;
	case ICMPV6_MLD2_REPORT:
		amt_mldv2_report_handler(amt, skb, tunnel);
		break;
	case ICMPV6_MGM_REDUCTION:
		amt_mldv1_leave_handler(amt, skb, tunnel);
		break;
	default:
		break;
	}
}
#endif

static bool amt_advertisement_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_advertisement *amta;
	int hdr_size;

	hdr_size = sizeof(*amta) + sizeof(struct udphdr);
	if (!pskb_may_pull(skb, hdr_size))
		return true;

	amta = (struct amt_header_advertisement *)(udp_hdr(skb) + 1);
	if (!amta->ip4)
		return true;

	if (amta->reserved || amta->version)
		return true;

	if (ipv4_is_loopback(amta->ip4) || ipv4_is_multicast(amta->ip4) ||
	    ipv4_is_zeronet(amta->ip4))
		return true;

	if (amt->status != AMT_STATUS_SENT_DISCOVERY ||
	    amt->nonce != amta->nonce)
		return true;

	amt->remote_ip = amta->ip4;
	netdev_dbg(amt->dev, "advertised remote ip = %pI4\n", &amt->remote_ip);
	mod_delayed_work(amt_wq, &amt->req_wq, 0);

	amt_update_gw_status(amt, AMT_STATUS_RECEIVED_ADVERTISEMENT, true);
	return false;
}

static bool amt_multicast_data_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_mcast_data *amtmd;
	int hdr_size, len, err;
	struct ethhdr *eth;
	struct iphdr *iph;

	if (READ_ONCE(amt->status) != AMT_STATUS_SENT_UPDATE)
		return true;

	hdr_size = sizeof(*amtmd) + sizeof(struct udphdr);
	if (!pskb_may_pull(skb, hdr_size))
		return true;

	amtmd = (struct amt_header_mcast_data *)(udp_hdr(skb) + 1);
	if (amtmd->reserved || amtmd->version)
		return true;

	if (iptunnel_pull_header(skb, hdr_size, htons(ETH_P_IP), false))
		return true;

	skb_reset_network_header(skb);
	skb_push(skb, sizeof(*eth));
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(*eth));
	eth = eth_hdr(skb);

	if (!pskb_may_pull(skb, sizeof(*iph)))
		return true;
	iph = ip_hdr(skb);

	if (iph->version == 4) {
		if (!ipv4_is_multicast(iph->daddr))
			return true;
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (iph->version == 6) {
		struct ipv6hdr *ip6h;

		if (!pskb_may_pull(skb, sizeof(*ip6h)))
			return true;

		ip6h = ipv6_hdr(skb);
		if (!ipv6_addr_is_multicast(&ip6h->daddr))
			return true;
		skb->protocol = htons(ETH_P_IPV6);
		eth->h_proto = htons(ETH_P_IPV6);
		ipv6_eth_mc_map(&ip6h->daddr, eth->h_dest);
#endif
	} else {
		return true;
	}

	skb->pkt_type = PACKET_MULTICAST;
	skb->ip_summed = CHECKSUM_NONE;
	len = skb->len;
	err = gro_cells_receive(&amt->gro_cells, skb);
	if (likely(err == NET_RX_SUCCESS))
		dev_sw_netstats_rx_add(amt->dev, len);
	else
		amt->dev->stats.rx_dropped++;

	return false;
}

static bool amt_membership_query_handler(struct amt_dev *amt,
					 struct sk_buff *skb)
{
	struct amt_header_membership_query *amtmq;
	struct igmpv3_query *ihv3;
	struct ethhdr *eth, *oeth;
	struct iphdr *iph;
	int hdr_size, len;

	hdr_size = sizeof(*amtmq) + sizeof(struct udphdr);
	if (!pskb_may_pull(skb, hdr_size))
		return true;

	amtmq = (struct amt_header_membership_query *)(udp_hdr(skb) + 1);
	if (amtmq->reserved || amtmq->version)
		return true;

	if (amtmq->nonce != amt->nonce)
		return true;

	hdr_size -= sizeof(*eth);
	if (iptunnel_pull_header(skb, hdr_size, htons(ETH_P_TEB), false))
		return true;

	oeth = eth_hdr(skb);
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(*eth));
	skb_reset_network_header(skb);
	eth = eth_hdr(skb);
	if (!pskb_may_pull(skb, sizeof(*iph)))
		return true;

	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (READ_ONCE(amt->ready4))
			return true;

		if (!pskb_may_pull(skb, sizeof(*iph) + AMT_IPHDR_OPTS +
				   sizeof(*ihv3)))
			return true;

		if (!ipv4_is_multicast(iph->daddr))
			return true;

		ihv3 = skb_pull(skb, sizeof(*iph) + AMT_IPHDR_OPTS);
		skb_reset_transport_header(skb);
		skb_push(skb, sizeof(*iph) + AMT_IPHDR_OPTS);
		WRITE_ONCE(amt->ready4, true);
		amt->mac = amtmq->response_mac;
		amt->req_cnt = 0;
		amt->qi = ihv3->qqic;
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (iph->version == 6) {
		struct mld2_query *mld2q;
		struct ipv6hdr *ip6h;

		if (READ_ONCE(amt->ready6))
			return true;

		if (!pskb_may_pull(skb, sizeof(*ip6h) + AMT_IP6HDR_OPTS +
				   sizeof(*mld2q)))
			return true;

		ip6h = ipv6_hdr(skb);
		if (!ipv6_addr_is_multicast(&ip6h->daddr))
			return true;

		mld2q = skb_pull(skb, sizeof(*ip6h) + AMT_IP6HDR_OPTS);
		skb_reset_transport_header(skb);
		skb_push(skb, sizeof(*ip6h) + AMT_IP6HDR_OPTS);
		WRITE_ONCE(amt->ready6, true);
		amt->mac = amtmq->response_mac;
		amt->req_cnt = 0;
		amt->qi = mld2q->mld2q_qqic;
		skb->protocol = htons(ETH_P_IPV6);
		eth->h_proto = htons(ETH_P_IPV6);
		ipv6_eth_mc_map(&ip6h->daddr, eth->h_dest);
#endif
	} else {
		return true;
	}

	ether_addr_copy(eth->h_source, oeth->h_source);
	skb->pkt_type = PACKET_MULTICAST;
	skb->ip_summed = CHECKSUM_NONE;
	len = skb->len;
	local_bh_disable();
	if (__netif_rx(skb) == NET_RX_SUCCESS) {
		amt_update_gw_status(amt, AMT_STATUS_RECEIVED_QUERY, true);
		dev_sw_netstats_rx_add(amt->dev, len);
	} else {
		amt->dev->stats.rx_dropped++;
	}
	local_bh_enable();

	return false;
}

static bool amt_update_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_membership_update *amtmu;
	struct amt_tunnel_list *tunnel;
	struct ethhdr *eth;
	struct iphdr *iph;
	int len, hdr_size;

	iph = ip_hdr(skb);

	hdr_size = sizeof(*amtmu) + sizeof(struct udphdr);
	if (!pskb_may_pull(skb, hdr_size))
		return true;

	amtmu = (struct amt_header_membership_update *)(udp_hdr(skb) + 1);
	if (amtmu->reserved || amtmu->version)
		return true;

	if (iptunnel_pull_header(skb, hdr_size, skb->protocol, false))
		return true;

	skb_reset_network_header(skb);

	list_for_each_entry_rcu(tunnel, &amt->tunnel_list, list) {
		if (tunnel->ip4 == iph->saddr) {
			if ((amtmu->nonce == tunnel->nonce &&
			     amtmu->response_mac == tunnel->mac)) {
				mod_delayed_work(amt_wq, &tunnel->gc_wq,
						 msecs_to_jiffies(amt_gmi(amt))
								  * 3);
				goto report;
			} else {
				netdev_dbg(amt->dev, "Invalid MAC\n");
				return true;
			}
		}
	}

	return true;

report:
	if (!pskb_may_pull(skb, sizeof(*iph)))
		return true;

	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (ip_mc_check_igmp(skb)) {
			netdev_dbg(amt->dev, "Invalid IGMP\n");
			return true;
		}

		spin_lock_bh(&tunnel->lock);
		amt_igmp_report_handler(amt, skb, tunnel);
		spin_unlock_bh(&tunnel->lock);

		skb_push(skb, sizeof(struct ethhdr));
		skb_reset_mac_header(skb);
		eth = eth_hdr(skb);
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (iph->version == 6) {
		struct ipv6hdr *ip6h = ipv6_hdr(skb);

		if (ipv6_mc_check_mld(skb)) {
			netdev_dbg(amt->dev, "Invalid MLD\n");
			return true;
		}

		spin_lock_bh(&tunnel->lock);
		amt_mld_report_handler(amt, skb, tunnel);
		spin_unlock_bh(&tunnel->lock);

		skb_push(skb, sizeof(struct ethhdr));
		skb_reset_mac_header(skb);
		eth = eth_hdr(skb);
		skb->protocol = htons(ETH_P_IPV6);
		eth->h_proto = htons(ETH_P_IPV6);
		ipv6_eth_mc_map(&ip6h->daddr, eth->h_dest);
#endif
	} else {
		netdev_dbg(amt->dev, "Unsupported Protocol\n");
		return true;
	}

	skb_pull(skb, sizeof(struct ethhdr));
	skb->pkt_type = PACKET_MULTICAST;
	skb->ip_summed = CHECKSUM_NONE;
	len = skb->len;
	if (__netif_rx(skb) == NET_RX_SUCCESS) {
		amt_update_relay_status(tunnel, AMT_STATUS_RECEIVED_UPDATE,
					true);
		dev_sw_netstats_rx_add(amt->dev, len);
	} else {
		amt->dev->stats.rx_dropped++;
	}

	return false;
}

static void amt_send_advertisement(struct amt_dev *amt, __be32 nonce,
				   __be32 daddr, __be16 dport)
{
	struct amt_header_advertisement *amta;
	int hlen, tlen, offset;
	struct socket *sock;
	struct udphdr *udph;
	struct sk_buff *skb;
	struct iphdr *iph;
	struct rtable *rt;
	struct flowi4 fl4;
	u32 len;
	int err;

	rcu_read_lock();
	sock = rcu_dereference(amt->sock);
	if (!sock)
		goto out;

	if (!netif_running(amt->stream_dev) || !netif_running(amt->dev))
		goto out;

	rt = ip_route_output_ports(amt->net, &fl4, sock->sk,
				   daddr, amt->local_ip,
				   dport, amt->relay_port,
				   IPPROTO_UDP, 0,
				   amt->stream_dev->ifindex);
	if (IS_ERR(rt)) {
		amt->dev->stats.tx_errors++;
		goto out;
	}

	hlen = LL_RESERVED_SPACE(amt->dev);
	tlen = amt->dev->needed_tailroom;
	len = hlen + tlen + sizeof(*iph) + sizeof(*udph) + sizeof(*amta);
	skb = netdev_alloc_skb_ip_align(amt->dev, len);
	if (!skb) {
		ip_rt_put(rt);
		amt->dev->stats.tx_errors++;
		goto out;
	}

	skb->priority = TC_PRIO_CONTROL;
	skb_dst_set(skb, &rt->dst);

	len = sizeof(*iph) + sizeof(*udph) + sizeof(*amta);
	skb_reset_network_header(skb);
	skb_put(skb, len);
	amta = skb_pull(skb, sizeof(*iph) + sizeof(*udph));
	amta->version	= 0;
	amta->type	= AMT_MSG_ADVERTISEMENT;
	amta->reserved	= 0;
	amta->nonce	= nonce;
	amta->ip4	= amt->local_ip;
	skb_push(skb, sizeof(*udph));
	skb_reset_transport_header(skb);
	udph		= udp_hdr(skb);
	udph->source	= amt->relay_port;
	udph->dest	= dport;
	udph->len	= htons(sizeof(*amta) + sizeof(*udph));
	udph->check	= 0;
	offset = skb_transport_offset(skb);
	skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);
	udph->check = csum_tcpudp_magic(amt->local_ip, daddr,
					sizeof(*udph) + sizeof(*amta),
					IPPROTO_UDP, skb->csum);

	skb_push(skb, sizeof(*iph));
	iph		= ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= (sizeof(struct iphdr)) >> 2;
	iph->tos	= AMT_TOS;
	iph->frag_off	= 0;
	iph->ttl	= ip4_dst_hoplimit(&rt->dst);
	iph->daddr	= daddr;
	iph->saddr	= amt->local_ip;
	iph->protocol	= IPPROTO_UDP;
	iph->tot_len	= htons(len);

	skb->ip_summed = CHECKSUM_NONE;
	ip_select_ident(amt->net, skb, NULL);
	ip_send_check(iph);
	err = ip_local_out(amt->net, sock->sk, skb);
	if (unlikely(net_xmit_eval(err)))
		amt->dev->stats.tx_errors++;

out:
	rcu_read_unlock();
}

static bool amt_discovery_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_discovery *amtd;
	struct udphdr *udph;
	struct iphdr *iph;

	if (!pskb_may_pull(skb, sizeof(*udph) + sizeof(*amtd)))
		return true;

	iph = ip_hdr(skb);
	udph = udp_hdr(skb);
	amtd = (struct amt_header_discovery *)(udp_hdr(skb) + 1);

	if (amtd->reserved || amtd->version)
		return true;

	amt_send_advertisement(amt, amtd->nonce, iph->saddr, udph->source);

	return false;
}

static bool amt_request_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_request *amtrh;
	struct amt_tunnel_list *tunnel;
	unsigned long long key;
	struct udphdr *udph;
	struct iphdr *iph;
	u64 mac;
	int i;

	if (!pskb_may_pull(skb, sizeof(*udph) + sizeof(*amtrh)))
		return true;

	iph = ip_hdr(skb);
	udph = udp_hdr(skb);
	amtrh = (struct amt_header_request *)(udp_hdr(skb) + 1);

	if (amtrh->reserved1 || amtrh->reserved2 || amtrh->version)
		return true;

	list_for_each_entry_rcu(tunnel, &amt->tunnel_list, list)
		if (tunnel->ip4 == iph->saddr)
			goto send;

	spin_lock_bh(&amt->lock);
	if (amt->nr_tunnels >= amt->max_tunnels) {
		spin_unlock_bh(&amt->lock);
		icmp_ndo_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
		return true;
	}

	tunnel = kzalloc(sizeof(*tunnel) +
			 (sizeof(struct hlist_head) * amt->hash_buckets),
			 GFP_ATOMIC);
	if (!tunnel) {
		spin_unlock_bh(&amt->lock);
		return true;
	}

	tunnel->source_port = udph->source;
	tunnel->ip4 = iph->saddr;

	memcpy(&key, &tunnel->key, sizeof(unsigned long long));
	tunnel->amt = amt;
	spin_lock_init(&tunnel->lock);
	for (i = 0; i < amt->hash_buckets; i++)
		INIT_HLIST_HEAD(&tunnel->groups[i]);

	INIT_DELAYED_WORK(&tunnel->gc_wq, amt_tunnel_expire);

	list_add_tail_rcu(&tunnel->list, &amt->tunnel_list);
	tunnel->key = amt->key;
	__amt_update_relay_status(tunnel, AMT_STATUS_RECEIVED_REQUEST, true);
	amt->nr_tunnels++;
	mod_delayed_work(amt_wq, &tunnel->gc_wq,
			 msecs_to_jiffies(amt_gmi(amt)));
	spin_unlock_bh(&amt->lock);

send:
	tunnel->nonce = amtrh->nonce;
	mac = siphash_3u32((__force u32)tunnel->ip4,
			   (__force u32)tunnel->source_port,
			   (__force u32)tunnel->nonce,
			   &tunnel->key);
	tunnel->mac = mac >> 16;

	if (!netif_running(amt->dev) || !netif_running(amt->stream_dev))
		return true;

	if (!amtrh->p)
		amt_send_igmp_gq(amt, tunnel);
	else
		amt_send_mld_gq(amt, tunnel);

	return false;
}

static void amt_gw_rcv(struct amt_dev *amt, struct sk_buff *skb)
{
	int type = amt_parse_type(skb);
	int err = 1;

	if (type == -1)
		goto drop;

	if (amt->mode == AMT_MODE_GATEWAY) {
		switch (type) {
		case AMT_MSG_ADVERTISEMENT:
			err = amt_advertisement_handler(amt, skb);
			break;
		case AMT_MSG_MEMBERSHIP_QUERY:
			err = amt_membership_query_handler(amt, skb);
			if (!err)
				return;
			break;
		default:
			netdev_dbg(amt->dev, "Invalid type of Gateway\n");
			break;
		}
	}
drop:
	if (err) {
		amt->dev->stats.rx_dropped++;
		kfree_skb(skb);
	} else {
		consume_skb(skb);
	}
}

static int amt_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct amt_dev *amt;
	struct iphdr *iph;
	int type;
	bool err;

	rcu_read_lock_bh();
	amt = rcu_dereference_sk_user_data(sk);
	if (!amt) {
		err = true;
		kfree_skb(skb);
		goto out;
	}

	skb->dev = amt->dev;
	iph = ip_hdr(skb);
	type = amt_parse_type(skb);
	if (type == -1) {
		err = true;
		goto drop;
	}

	if (amt->mode == AMT_MODE_GATEWAY) {
		switch (type) {
		case AMT_MSG_ADVERTISEMENT:
			if (iph->saddr != amt->discovery_ip) {
				netdev_dbg(amt->dev, "Invalid Relay IP\n");
				err = true;
				goto drop;
			}
			if (amt_queue_event(amt, AMT_EVENT_RECEIVE, skb)) {
				netdev_dbg(amt->dev, "AMT Event queue full\n");
				err = true;
				goto drop;
			}
			goto out;
		case AMT_MSG_MULTICAST_DATA:
			if (iph->saddr != amt->remote_ip) {
				netdev_dbg(amt->dev, "Invalid Relay IP\n");
				err = true;
				goto drop;
			}
			err = amt_multicast_data_handler(amt, skb);
			if (err)
				goto drop;
			else
				goto out;
		case AMT_MSG_MEMBERSHIP_QUERY:
			if (iph->saddr != amt->remote_ip) {
				netdev_dbg(amt->dev, "Invalid Relay IP\n");
				err = true;
				goto drop;
			}
			if (amt_queue_event(amt, AMT_EVENT_RECEIVE, skb)) {
				netdev_dbg(amt->dev, "AMT Event queue full\n");
				err = true;
				goto drop;
			}
			goto out;
		default:
			err = true;
			netdev_dbg(amt->dev, "Invalid type of Gateway\n");
			break;
		}
	} else {
		switch (type) {
		case AMT_MSG_DISCOVERY:
			err = amt_discovery_handler(amt, skb);
			break;
		case AMT_MSG_REQUEST:
			err = amt_request_handler(amt, skb);
			break;
		case AMT_MSG_MEMBERSHIP_UPDATE:
			err = amt_update_handler(amt, skb);
			if (err)
				goto drop;
			else
				goto out;
		default:
			err = true;
			netdev_dbg(amt->dev, "Invalid type of relay\n");
			break;
		}
	}
drop:
	if (err) {
		amt->dev->stats.rx_dropped++;
		kfree_skb(skb);
	} else {
		consume_skb(skb);
	}
out:
	rcu_read_unlock_bh();
	return 0;
}

static void amt_event_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(work, struct amt_dev, event_wq);
	struct sk_buff *skb;
	u8 event;
	int i;

	for (i = 0; i < AMT_MAX_EVENTS; i++) {
		spin_lock_bh(&amt->lock);
		if (amt->nr_events == 0) {
			spin_unlock_bh(&amt->lock);
			return;
		}
		event = amt->events[amt->event_idx].event;
		skb = amt->events[amt->event_idx].skb;
		amt->events[amt->event_idx].event = AMT_EVENT_NONE;
		amt->events[amt->event_idx].skb = NULL;
		amt->nr_events--;
		amt->event_idx++;
		amt->event_idx %= AMT_MAX_EVENTS;
		spin_unlock_bh(&amt->lock);

		switch (event) {
		case AMT_EVENT_RECEIVE:
			amt_gw_rcv(amt, skb);
			break;
		case AMT_EVENT_SEND_DISCOVERY:
			amt_event_send_discovery(amt);
			break;
		case AMT_EVENT_SEND_REQUEST:
			amt_event_send_request(amt);
			break;
		default:
			if (skb)
				kfree_skb(skb);
			break;
		}
	}
}

static int amt_err_lookup(struct sock *sk, struct sk_buff *skb)
{
	struct amt_dev *amt;
	int type;

	rcu_read_lock_bh();
	amt = rcu_dereference_sk_user_data(sk);
	if (!amt)
		goto out;

	if (amt->mode != AMT_MODE_GATEWAY)
		goto drop;

	type = amt_parse_type(skb);
	if (type == -1)
		goto drop;

	netdev_dbg(amt->dev, "Received IGMP Unreachable of %s\n",
		   type_str[type]);
	switch (type) {
	case AMT_MSG_DISCOVERY:
		break;
	case AMT_MSG_REQUEST:
	case AMT_MSG_MEMBERSHIP_UPDATE:
		if (READ_ONCE(amt->status) >= AMT_STATUS_RECEIVED_ADVERTISEMENT)
			mod_delayed_work(amt_wq, &amt->req_wq, 0);
		break;
	default:
		goto drop;
	}
out:
	rcu_read_unlock_bh();
	return 0;
drop:
	rcu_read_unlock_bh();
	amt->dev->stats.rx_dropped++;
	return 0;
}

static struct socket *amt_create_sock(struct net *net, __be16 port)
{
	struct udp_port_cfg udp_conf;
	struct socket *sock;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));
	udp_conf.family = AF_INET;
	udp_conf.local_ip.s_addr = htonl(INADDR_ANY);

	udp_conf.local_udp_port = port;

	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	return sock;
}

static int amt_socket_create(struct amt_dev *amt)
{
	struct udp_tunnel_sock_cfg tunnel_cfg;
	struct socket *sock;

	sock = amt_create_sock(amt->net, amt->relay_port);
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	/* Mark socket as an encapsulation socket */
	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.sk_user_data = amt;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.encap_rcv = amt_rcv;
	tunnel_cfg.encap_err_lookup = amt_err_lookup;
	tunnel_cfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(amt->net, sock, &tunnel_cfg);

	rcu_assign_pointer(amt->sock, sock);
	return 0;
}

static int amt_dev_open(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err;

	amt->ready4 = false;
	amt->ready6 = false;
	amt->event_idx = 0;
	amt->nr_events = 0;

	err = amt_socket_create(amt);
	if (err)
		return err;

	amt->req_cnt = 0;
	amt->remote_ip = 0;
	amt->nonce = 0;
	get_random_bytes(&amt->key, sizeof(siphash_key_t));

	amt->status = AMT_STATUS_INIT;
	if (amt->mode == AMT_MODE_GATEWAY) {
		mod_delayed_work(amt_wq, &amt->discovery_wq, 0);
		mod_delayed_work(amt_wq, &amt->req_wq, 0);
	} else if (amt->mode == AMT_MODE_RELAY) {
		mod_delayed_work(amt_wq, &amt->secret_wq,
				 msecs_to_jiffies(AMT_SECRET_TIMEOUT));
	}
	return err;
}

static int amt_dev_stop(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	struct amt_tunnel_list *tunnel, *tmp;
	struct socket *sock;
	struct sk_buff *skb;
	int i;

	cancel_delayed_work_sync(&amt->req_wq);
	cancel_delayed_work_sync(&amt->discovery_wq);
	cancel_delayed_work_sync(&amt->secret_wq);

	/* shutdown */
	sock = rtnl_dereference(amt->sock);
	RCU_INIT_POINTER(amt->sock, NULL);
	synchronize_net();
	if (sock)
		udp_tunnel_sock_release(sock);

	cancel_work_sync(&amt->event_wq);
	for (i = 0; i < AMT_MAX_EVENTS; i++) {
		skb = amt->events[i].skb;
		if (skb)
			kfree_skb(skb);
		amt->events[i].event = AMT_EVENT_NONE;
		amt->events[i].skb = NULL;
	}

	amt->ready4 = false;
	amt->ready6 = false;
	amt->req_cnt = 0;
	amt->remote_ip = 0;

	list_for_each_entry_safe(tunnel, tmp, &amt->tunnel_list, list) {
		list_del_rcu(&tunnel->list);
		amt->nr_tunnels--;
		cancel_delayed_work_sync(&tunnel->gc_wq);
		amt_clear_groups(tunnel);
		kfree_rcu(tunnel, rcu);
	}

	return 0;
}

static const struct device_type amt_type = {
	.name = "amt",
};

static int amt_dev_init(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err;

	amt->dev = dev;
	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&amt->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	return 0;
}

static void amt_dev_uninit(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);

	gro_cells_destroy(&amt->gro_cells);
	free_percpu(dev->tstats);
}

static const struct net_device_ops amt_netdev_ops = {
	.ndo_init               = amt_dev_init,
	.ndo_uninit             = amt_dev_uninit,
	.ndo_open		= amt_dev_open,
	.ndo_stop		= amt_dev_stop,
	.ndo_start_xmit         = amt_dev_xmit,
	.ndo_get_stats64        = dev_get_tstats64,
};

static void amt_link_setup(struct net_device *dev)
{
	dev->netdev_ops         = &amt_netdev_ops;
	dev->needs_free_netdev  = true;
	SET_NETDEV_DEVTYPE(dev, &amt_type);
	dev->min_mtu		= ETH_MIN_MTU;
	dev->max_mtu		= ETH_MAX_MTU;
	dev->type		= ARPHRD_NONE;
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->priv_flags		|= IFF_NO_QUEUE;
	dev->features		|= NETIF_F_LLTX;
	dev->features		|= NETIF_F_GSO_SOFTWARE;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->hw_features	|= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->hw_features	|= NETIF_F_FRAGLIST | NETIF_F_RXCSUM;
	dev->hw_features	|= NETIF_F_GSO_SOFTWARE;
	eth_hw_addr_random(dev);
	eth_zero_addr(dev->broadcast);
	ether_setup(dev);
}

static const struct nla_policy amt_policy[IFLA_AMT_MAX + 1] = {
	[IFLA_AMT_MODE]		= { .type = NLA_U32 },
	[IFLA_AMT_RELAY_PORT]	= { .type = NLA_U16 },
	[IFLA_AMT_GATEWAY_PORT]	= { .type = NLA_U16 },
	[IFLA_AMT_LINK]		= { .type = NLA_U32 },
	[IFLA_AMT_LOCAL_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_REMOTE_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_DISCOVERY_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_MAX_TUNNELS]	= { .type = NLA_U32 },
};

static int amt_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	if (!data)
		return -EINVAL;

	if (!data[IFLA_AMT_LINK]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_LINK],
				    "Link attribute is required");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_MODE]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_MODE],
				    "Mode attribute is required");
		return -EINVAL;
	}

	if (nla_get_u32(data[IFLA_AMT_MODE]) > AMT_MODE_MAX) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_MODE],
				    "Mode attribute is not valid");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_LOCAL_IP]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_DISCOVERY_IP],
				    "Local attribute is required");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_DISCOVERY_IP] &&
	    nla_get_u32(data[IFLA_AMT_MODE]) == AMT_MODE_GATEWAY) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_LOCAL_IP],
				    "Discovery attribute is required");
		return -EINVAL;
	}

	return 0;
}

static int amt_newlink(struct net *net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err = -EINVAL;

	amt->net = net;
	amt->mode = nla_get_u32(data[IFLA_AMT_MODE]);

	if (data[IFLA_AMT_MAX_TUNNELS] &&
	    nla_get_u32(data[IFLA_AMT_MAX_TUNNELS]))
		amt->max_tunnels = nla_get_u32(data[IFLA_AMT_MAX_TUNNELS]);
	else
		amt->max_tunnels = AMT_MAX_TUNNELS;

	spin_lock_init(&amt->lock);
	amt->max_groups = AMT_MAX_GROUP;
	amt->max_sources = AMT_MAX_SOURCE;
	amt->hash_buckets = AMT_HSIZE;
	amt->nr_tunnels = 0;
	get_random_bytes(&amt->hash_seed, sizeof(amt->hash_seed));
	amt->stream_dev = dev_get_by_index(net,
					   nla_get_u32(data[IFLA_AMT_LINK]));
	if (!amt->stream_dev) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LINK],
				    "Can't find stream device");
		return -ENODEV;
	}

	if (amt->stream_dev->type != ARPHRD_ETHER) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LINK],
				    "Invalid stream device type");
		goto err;
	}

	amt->local_ip = nla_get_in_addr(data[IFLA_AMT_LOCAL_IP]);
	if (ipv4_is_loopback(amt->local_ip) ||
	    ipv4_is_zeronet(amt->local_ip) ||
	    ipv4_is_multicast(amt->local_ip)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LOCAL_IP],
				    "Invalid Local address");
		goto err;
	}

	if (data[IFLA_AMT_RELAY_PORT])
		amt->relay_port = nla_get_be16(data[IFLA_AMT_RELAY_PORT]);
	else
		amt->relay_port = htons(IANA_AMT_UDP_PORT);

	if (data[IFLA_AMT_GATEWAY_PORT])
		amt->gw_port = nla_get_be16(data[IFLA_AMT_GATEWAY_PORT]);
	else
		amt->gw_port = htons(IANA_AMT_UDP_PORT);

	if (!amt->relay_port) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
				    "relay port must not be 0");
		goto err;
	}
	if (amt->mode == AMT_MODE_RELAY) {
		amt->qrv = READ_ONCE(amt->net->ipv4.sysctl_igmp_qrv);
		amt->qri = 10;
		dev->needed_headroom = amt->stream_dev->needed_headroom +
				       AMT_RELAY_HLEN;
		dev->mtu = amt->stream_dev->mtu - AMT_RELAY_HLEN;
		dev->max_mtu = dev->mtu;
		dev->min_mtu = ETH_MIN_MTU + AMT_RELAY_HLEN;
	} else {
		if (!data[IFLA_AMT_DISCOVERY_IP]) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "discovery must be set in gateway mode");
			goto err;
		}
		if (!amt->gw_port) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "gateway port must not be 0");
			goto err;
		}
		amt->remote_ip = 0;
		amt->discovery_ip = nla_get_in_addr(data[IFLA_AMT_DISCOVERY_IP]);
		if (ipv4_is_loopback(amt->discovery_ip) ||
		    ipv4_is_zeronet(amt->discovery_ip) ||
		    ipv4_is_multicast(amt->discovery_ip)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "discovery must be unicast");
			goto err;
		}

		dev->needed_headroom = amt->stream_dev->needed_headroom +
				       AMT_GW_HLEN;
		dev->mtu = amt->stream_dev->mtu - AMT_GW_HLEN;
		dev->max_mtu = dev->mtu;
		dev->min_mtu = ETH_MIN_MTU + AMT_GW_HLEN;
	}
	amt->qi = AMT_INIT_QUERY_INTERVAL;

	err = register_netdevice(dev);
	if (err < 0) {
		netdev_dbg(dev, "failed to register new netdev %d\n", err);
		goto err;
	}

	err = netdev_upper_dev_link(amt->stream_dev, dev, extack);
	if (err < 0) {
		unregister_netdevice(dev);
		goto err;
	}

	INIT_DELAYED_WORK(&amt->discovery_wq, amt_discovery_work);
	INIT_DELAYED_WORK(&amt->req_wq, amt_req_work);
	INIT_DELAYED_WORK(&amt->secret_wq, amt_secret_work);
	INIT_WORK(&amt->event_wq, amt_event_work);
	INIT_LIST_HEAD(&amt->tunnel_list);
	return 0;
err:
	dev_put(amt->stream_dev);
	return err;
}

static void amt_dellink(struct net_device *dev, struct list_head *head)
{
	struct amt_dev *amt = netdev_priv(dev);

	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(amt->stream_dev, dev);
	dev_put(amt->stream_dev);
}

static size_t amt_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) + /* IFLA_AMT_MODE */
	       nla_total_size(sizeof(__u16)) + /* IFLA_AMT_RELAY_PORT */
	       nla_total_size(sizeof(__u16)) + /* IFLA_AMT_GATEWAY_PORT */
	       nla_total_size(sizeof(__u32)) + /* IFLA_AMT_LINK */
	       nla_total_size(sizeof(__u32)) + /* IFLA_MAX_TUNNELS */
	       nla_total_size(sizeof(struct iphdr)) + /* IFLA_AMT_DISCOVERY_IP */
	       nla_total_size(sizeof(struct iphdr)) + /* IFLA_AMT_REMOTE_IP */
	       nla_total_size(sizeof(struct iphdr)); /* IFLA_AMT_LOCAL_IP */
}

static int amt_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_AMT_MODE, amt->mode))
		goto nla_put_failure;
	if (nla_put_be16(skb, IFLA_AMT_RELAY_PORT, amt->relay_port))
		goto nla_put_failure;
	if (nla_put_be16(skb, IFLA_AMT_GATEWAY_PORT, amt->gw_port))
		goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_AMT_LINK, amt->stream_dev->ifindex))
		goto nla_put_failure;
	if (nla_put_in_addr(skb, IFLA_AMT_LOCAL_IP, amt->local_ip))
		goto nla_put_failure;
	if (nla_put_in_addr(skb, IFLA_AMT_DISCOVERY_IP, amt->discovery_ip))
		goto nla_put_failure;
	if (amt->remote_ip)
		if (nla_put_in_addr(skb, IFLA_AMT_REMOTE_IP, amt->remote_ip))
			goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_AMT_MAX_TUNNELS, amt->max_tunnels))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops amt_link_ops __read_mostly = {
	.kind		= "amt",
	.maxtype	= IFLA_AMT_MAX,
	.policy		= amt_policy,
	.priv_size	= sizeof(struct amt_dev),
	.setup		= amt_link_setup,
	.validate	= amt_validate,
	.newlink	= amt_newlink,
	.dellink	= amt_dellink,
	.get_size       = amt_get_size,
	.fill_info      = amt_fill_info,
};

static struct net_device *amt_lookup_upper_dev(struct net_device *dev)
{
	struct net_device *upper_dev;
	struct amt_dev *amt;

	for_each_netdev(dev_net(dev), upper_dev) {
		if (netif_is_amt(upper_dev)) {
			amt = netdev_priv(upper_dev);
			if (amt->stream_dev == dev)
				return upper_dev;
		}
	}

	return NULL;
}

static int amt_device_event(struct notifier_block *unused,
			    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net_device *upper_dev;
	struct amt_dev *amt;
	LIST_HEAD(list);
	int new_mtu;

	upper_dev = amt_lookup_upper_dev(dev);
	if (!upper_dev)
		return NOTIFY_DONE;
	amt = netdev_priv(upper_dev);

	switch (event) {
	case NETDEV_UNREGISTER:
		amt_dellink(amt->dev, &list);
		unregister_netdevice_many(&list);
		break;
	case NETDEV_CHANGEMTU:
		if (amt->mode == AMT_MODE_RELAY)
			new_mtu = dev->mtu - AMT_RELAY_HLEN;
		else
			new_mtu = dev->mtu - AMT_GW_HLEN;

		dev_set_mtu(amt->dev, new_mtu);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block amt_notifier_block __read_mostly = {
	.notifier_call = amt_device_event,
};

static int __init amt_init(void)
{
	int err;

	err = register_netdevice_notifier(&amt_notifier_block);
	if (err < 0)
		goto err;

	err = rtnl_link_register(&amt_link_ops);
	if (err < 0)
		goto unregister_notifier;

	amt_wq = alloc_workqueue("amt", WQ_UNBOUND, 0);
	if (!amt_wq) {
		err = -ENOMEM;
		goto rtnl_unregister;
	}

	spin_lock_init(&source_gc_lock);
	spin_lock_bh(&source_gc_lock);
	INIT_DELAYED_WORK(&source_gc_wq, amt_source_gc_work);
	mod_delayed_work(amt_wq, &source_gc_wq,
			 msecs_to_jiffies(AMT_GC_INTERVAL));
	spin_unlock_bh(&source_gc_lock);

	return 0;

rtnl_unregister:
	rtnl_link_unregister(&amt_link_ops);
unregister_notifier:
	unregister_netdevice_notifier(&amt_notifier_block);
err:
	pr_err("error loading AMT module loaded\n");
	return err;
}
late_initcall(amt_init);

static void __exit amt_fini(void)
{
	rtnl_link_unregister(&amt_link_ops);
	unregister_netdevice_notifier(&amt_notifier_block);
	cancel_delayed_work_sync(&source_gc_wq);
	__amt_source_gc_work();
	destroy_workqueue(amt_wq);
}
module_exit(amt_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Taehee Yoo <ap420073@gmail.com>");
MODULE_ALIAS_RTNL_LINK("amt");
