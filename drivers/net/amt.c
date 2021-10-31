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
#include <net/net_namespace.h>
#include <net/protocol.h>
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
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>

static struct workqueue_struct *amt_wq;

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
	"AMT_MSG_DISCOVERY",
	"AMT_MSG_ADVERTISEMENT",
	"AMT_MSG_REQUEST",
	"AMT_MSG_MEMBERSHIP_QUERY",
	"AMT_MSG_MEMBERSHIP_UPDATE",
	"AMT_MSG_MULTICAST_DATA",
	"AMT_MSG_TEARDOWM",
};

static struct amt_skb_cb *amt_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct amt_skb_cb) + sizeof(struct qdisc_skb_cb) >
		     sizeof_field(struct sk_buff, cb));

	return (struct amt_skb_cb *)((void *)skb->cb +
		sizeof(struct qdisc_skb_cb));
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
	ihv3->qrv	= amt->net->ipv4.sysctl_igmp_qrv;
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

static void __amt_update_gw_status(struct amt_dev *amt, enum amt_status status,
				   bool validate)
{
	if (validate && amt->status >= status)
		return;
	netdev_dbg(amt->dev, "Update GW status %s -> %s",
		   status_str[amt->status], status_str[status]);
	amt->status = status;
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

static void amt_update_gw_status(struct amt_dev *amt, enum amt_status status,
				 bool validate)
{
	spin_lock_bh(&amt->lock);
	__amt_update_gw_status(amt, status, validate);
	spin_unlock_bh(&amt->lock);
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

	spin_lock_bh(&amt->lock);
	__amt_update_gw_status(amt, AMT_STATUS_SENT_DISCOVERY, true);
	spin_unlock_bh(&amt->lock);
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

static void amt_discovery_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(to_delayed_work(work),
					   struct amt_dev,
					   discovery_wq);

	spin_lock_bh(&amt->lock);
	if (amt->status > AMT_STATUS_SENT_DISCOVERY)
		goto out;
	get_random_bytes(&amt->nonce, sizeof(__be32));
	spin_unlock_bh(&amt->lock);

	amt_send_discovery(amt);
	spin_lock_bh(&amt->lock);
out:
	mod_delayed_work(amt_wq, &amt->discovery_wq,
			 msecs_to_jiffies(AMT_DISCOVERY_TIMEOUT));
	spin_unlock_bh(&amt->lock);
}

static void amt_req_work(struct work_struct *work)
{
	struct amt_dev *amt = container_of(to_delayed_work(work),
					   struct amt_dev,
					   req_wq);
	u32 exp;

	spin_lock_bh(&amt->lock);
	if (amt->status < AMT_STATUS_RECEIVED_ADVERTISEMENT)
		goto out;

	if (amt->req_cnt++ > AMT_MAX_REQ_COUNT) {
		netdev_dbg(amt->dev, "Gateway is not ready");
		amt->qi = AMT_INIT_REQ_TIMEOUT;
		amt->ready4 = false;
		amt->ready6 = false;
		amt->remote_ip = 0;
		__amt_update_gw_status(amt, AMT_STATUS_INIT, false);
		amt->req_cnt = 0;
	}
	spin_unlock_bh(&amt->lock);

	amt_send_request(amt, false);
	amt_send_request(amt, true);
	amt_update_gw_status(amt, AMT_STATUS_SENT_REQUEST, true);
	spin_lock_bh(&amt->lock);
out:
	exp = min_t(u32, (1 * (1 << amt->req_cnt)), AMT_MAX_REQ_TIMEOUT);
	mod_delayed_work(amt_wq, &amt->req_wq, msecs_to_jiffies(exp * 1000));
	spin_unlock_bh(&amt->lock);
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
		return -1;
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
	bool report = false;
	struct igmphdr *ih;
	bool query = false;
	struct iphdr *iph;
	bool data = false;
	bool v6 = false;

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
		if ((!v6 && !amt->ready4) || (v6 && !amt->ready6))
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
		list_for_each_entry_rcu(tunnel, &amt->tunnel_list, list)
			amt_send_multicast_data(amt, skb, tunnel, v6);
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
	rcu_read_unlock();
	spin_unlock_bh(&amt->lock);
	kfree_rcu(tunnel, rcu);
}

static bool amt_advertisement_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_advertisement *amta;
	int hdr_size;

	hdr_size = sizeof(*amta) - sizeof(struct amt_header);

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

	amtmd = (struct amt_header_mcast_data *)(udp_hdr(skb) + 1);
	if (amtmd->reserved || amtmd->version)
		return true;

	hdr_size = sizeof(*amtmd) + sizeof(struct udphdr);
	if (iptunnel_pull_header(skb, hdr_size, htons(ETH_P_IP), false))
		return true;
	skb_reset_network_header(skb);
	skb_push(skb, sizeof(*eth));
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(*eth));
	eth = eth_hdr(skb);
	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (!ipv4_is_multicast(iph->daddr))
			return true;
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
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

	hdr_size = sizeof(*amtmq) - sizeof(struct amt_header);

	if (!pskb_may_pull(skb, hdr_size))
		return true;

	amtmq = (struct amt_header_membership_query *)(udp_hdr(skb) + 1);
	if (amtmq->reserved || amtmq->version)
		return true;

	hdr_size = sizeof(*amtmq) + sizeof(struct udphdr) - sizeof(*eth);
	if (iptunnel_pull_header(skb, hdr_size, htons(ETH_P_TEB), false))
		return true;
	oeth = eth_hdr(skb);
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(*eth));
	skb_reset_network_header(skb);
	eth = eth_hdr(skb);
	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (!ipv4_is_multicast(iph->daddr))
			return true;
		if (!pskb_may_pull(skb, sizeof(*iph) + AMT_IPHDR_OPTS +
				   sizeof(*ihv3)))
			return true;

		ihv3 = skb_pull(skb, sizeof(*iph) + AMT_IPHDR_OPTS);
		skb_reset_transport_header(skb);
		skb_push(skb, sizeof(*iph) + AMT_IPHDR_OPTS);
		spin_lock_bh(&amt->lock);
		amt->ready4 = true;
		amt->mac = amtmq->response_mac;
		amt->req_cnt = 0;
		amt->qi = ihv3->qqic;
		spin_unlock_bh(&amt->lock);
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
	} else {
		return true;
	}

	ether_addr_copy(eth->h_source, oeth->h_source);
	skb->pkt_type = PACKET_MULTICAST;
	skb->ip_summed = CHECKSUM_NONE;
	len = skb->len;
	if (netif_rx(skb) == NET_RX_SUCCESS) {
		amt_update_gw_status(amt, AMT_STATUS_RECEIVED_QUERY, true);
		dev_sw_netstats_rx_add(amt->dev, len);
	} else {
		amt->dev->stats.rx_dropped++;
	}

	return false;
}

static bool amt_update_handler(struct amt_dev *amt, struct sk_buff *skb)
{
	struct amt_header_membership_update *amtmu;
	struct amt_tunnel_list *tunnel;
	struct udphdr *udph;
	struct ethhdr *eth;
	struct iphdr *iph;
	int len;

	iph = ip_hdr(skb);
	udph = udp_hdr(skb);

	if (__iptunnel_pull_header(skb, sizeof(*udph), skb->protocol,
				   false, false))
		return true;

	amtmu = (struct amt_header_membership_update *)skb->data;
	if (amtmu->reserved || amtmu->version)
		return true;

	skb_pull(skb, sizeof(*amtmu));
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

	return false;

report:
	iph = ip_hdr(skb);
	if (iph->version == 4) {
		if (ip_mc_check_igmp(skb)) {
			netdev_dbg(amt->dev, "Invalid IGMP\n");
			return true;
		}

		skb_push(skb, sizeof(struct ethhdr));
		skb_reset_mac_header(skb);
		eth = eth_hdr(skb);
		skb->protocol = htons(ETH_P_IP);
		eth->h_proto = htons(ETH_P_IP);
		ip_eth_mc_map(iph->daddr, eth->h_dest);
	} else {
		netdev_dbg(amt->dev, "Unsupported Protocol\n");
		return true;
	}

	skb_pull(skb, sizeof(struct ethhdr));
	skb->pkt_type = PACKET_MULTICAST;
	skb->ip_summed = CHECKSUM_NONE;
	len = skb->len;
	if (netif_rx(skb) == NET_RX_SUCCESS) {
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

	if (amt->nr_tunnels >= amt->max_tunnels) {
		icmp_ndo_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
		return true;
	}

	tunnel = kzalloc(sizeof(*tunnel) +
			 (sizeof(struct hlist_head) * amt->hash_buckets),
			 GFP_ATOMIC);
	if (!tunnel)
		return true;

	tunnel->source_port = udph->source;
	tunnel->ip4 = iph->saddr;

	memcpy(&key, &tunnel->key, sizeof(unsigned long long));
	tunnel->amt = amt;
	spin_lock_init(&tunnel->lock);
	for (i = 0; i < amt->hash_buckets; i++)
		INIT_HLIST_HEAD(&tunnel->groups[i]);

	INIT_DELAYED_WORK(&tunnel->gc_wq, amt_tunnel_expire);

	spin_lock_bh(&amt->lock);
	list_add_tail_rcu(&tunnel->list, &amt->tunnel_list);
	tunnel->key = amt->key;
	amt_update_relay_status(tunnel, AMT_STATUS_RECEIVED_REQUEST, true);
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

	return false;
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
			if (amt_advertisement_handler(amt, skb))
				amt->dev->stats.rx_dropped++;
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
			err = amt_membership_query_handler(amt, skb);
			if (err)
				goto drop;
			else
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

static int amt_err_lookup(struct sock *sk, struct sk_buff *skb)
{
	struct amt_dev *amt;
	int type;

	rcu_read_lock_bh();
	amt = rcu_dereference_sk_user_data(sk);
	if (!amt)
		goto drop;

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
		if (amt->status >= AMT_STATUS_RECEIVED_ADVERTISEMENT)
			mod_delayed_work(amt_wq, &amt->req_wq, 0);
		break;
	default:
		goto drop;
	}
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

	err = amt_socket_create(amt);
	if (err)
		return err;

	amt->req_cnt = 0;
	amt->remote_ip = 0;
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

	cancel_delayed_work_sync(&amt->req_wq);
	cancel_delayed_work_sync(&amt->discovery_wq);
	cancel_delayed_work_sync(&amt->secret_wq);

	/* shutdown */
	sock = rtnl_dereference(amt->sock);
	RCU_INIT_POINTER(amt->sock, NULL);
	synchronize_net();
	if (sock)
		udp_tunnel_sock_release(sock);

	amt->ready4 = false;
	amt->ready6 = false;
	amt->req_cnt = 0;
	amt->remote_ip = 0;

	list_for_each_entry_safe(tunnel, tmp, &amt->tunnel_list, list) {
		list_del_rcu(&tunnel->list);
		amt->nr_tunnels--;
		cancel_delayed_work_sync(&tunnel->gc_wq);
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
		amt->qrv = amt->net->ipv4.sysctl_igmp_qrv;
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

	amt_wq = alloc_workqueue("amt", WQ_UNBOUND, 1);
	if (!amt_wq)
		goto rtnl_unregister;

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
	destroy_workqueue(amt_wq);
}
module_exit(amt_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Taehee Yoo <ap420073@gmail.com>");
MODULE_ALIAS_RTNL_LINK("amt");
