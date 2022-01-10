/*
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/tcp.h>
#include <linux/ipv6.h>
#include <net/inet_ecn.h>
#include <net/route.h>
#include <net/ip6_route.h>

#include "libcxgb_cm.h"

void
cxgb_get_4tuple(struct cpl_pass_accept_req *req, enum chip_type type,
		int *iptype, __u8 *local_ip, __u8 *peer_ip,
		__be16 *local_port, __be16 *peer_port)
{
	int eth_len = (CHELSIO_CHIP_VERSION(type) <= CHELSIO_T5) ?
		      ETH_HDR_LEN_G(be32_to_cpu(req->hdr_len)) :
		      T6_ETH_HDR_LEN_G(be32_to_cpu(req->hdr_len));
	int ip_len = (CHELSIO_CHIP_VERSION(type) <= CHELSIO_T5) ?
		     IP_HDR_LEN_G(be32_to_cpu(req->hdr_len)) :
		     T6_IP_HDR_LEN_G(be32_to_cpu(req->hdr_len));
	struct iphdr *ip = (struct iphdr *)((u8 *)(req + 1) + eth_len);
	struct ipv6hdr *ip6 = (struct ipv6hdr *)((u8 *)(req + 1) + eth_len);
	struct tcphdr *tcp = (struct tcphdr *)
			     ((u8 *)(req + 1) + eth_len + ip_len);

	if (ip->version == 4) {
		pr_debug("%s saddr 0x%x daddr 0x%x sport %u dport %u\n",
			 __func__, ntohl(ip->saddr), ntohl(ip->daddr),
			 ntohs(tcp->source), ntohs(tcp->dest));
		*iptype = 4;
		memcpy(peer_ip, &ip->saddr, 4);
		memcpy(local_ip, &ip->daddr, 4);
	} else {
		pr_debug("%s saddr %pI6 daddr %pI6 sport %u dport %u\n",
			 __func__, ip6->saddr.s6_addr, ip6->daddr.s6_addr,
			 ntohs(tcp->source), ntohs(tcp->dest));
		*iptype = 6;
		memcpy(peer_ip, ip6->saddr.s6_addr, 16);
		memcpy(local_ip, ip6->daddr.s6_addr, 16);
	}
	*peer_port = tcp->source;
	*local_port = tcp->dest;
}
EXPORT_SYMBOL(cxgb_get_4tuple);

static bool
cxgb_our_interface(struct cxgb4_lld_info *lldi,
		   struct net_device *(*get_real_dev)(struct net_device *),
		   struct net_device *egress_dev)
{
	int i;

	egress_dev = get_real_dev(egress_dev);
	for (i = 0; i < lldi->nports; i++)
		if (lldi->ports[i] == egress_dev)
			return true;
	return false;
}

struct dst_entry *
cxgb_find_route(struct cxgb4_lld_info *lldi,
		struct net_device *(*get_real_dev)(struct net_device *),
		__be32 local_ip, __be32 peer_ip, __be16 local_port,
		__be16 peer_port, u8 tos)
{
	struct rtable *rt;
	struct flowi4 fl4;
	struct neighbour *n;

	rt = ip_route_output_ports(&init_net, &fl4, NULL, peer_ip, local_ip,
				   peer_port, local_port, IPPROTO_TCP,
				   tos & ~INET_ECN_MASK, 0);
	if (IS_ERR(rt))
		return NULL;
	n = dst_neigh_lookup(&rt->dst, &peer_ip);
	if (!n)
		return NULL;
	if (!cxgb_our_interface(lldi, get_real_dev, n->dev) &&
	    !(n->dev->flags & IFF_LOOPBACK)) {
		neigh_release(n);
		dst_release(&rt->dst);
		return NULL;
	}
	neigh_release(n);
	return &rt->dst;
}
EXPORT_SYMBOL(cxgb_find_route);

struct dst_entry *
cxgb_find_route6(struct cxgb4_lld_info *lldi,
		 struct net_device *(*get_real_dev)(struct net_device *),
		 __u8 *local_ip, __u8 *peer_ip, __be16 local_port,
		 __be16 peer_port, u8 tos, __u32 sin6_scope_id)
{
	struct dst_entry *dst = NULL;

	if (IS_ENABLED(CONFIG_IPV6)) {
		struct flowi6 fl6;

		memset(&fl6, 0, sizeof(fl6));
		memcpy(&fl6.daddr, peer_ip, 16);
		memcpy(&fl6.saddr, local_ip, 16);
		if (ipv6_addr_type(&fl6.daddr) & IPV6_ADDR_LINKLOCAL)
			fl6.flowi6_oif = sin6_scope_id;
		dst = ip6_route_output(&init_net, NULL, &fl6);
		if (dst->error ||
		    (!cxgb_our_interface(lldi, get_real_dev,
					 ip6_dst_idev(dst)->dev) &&
		     !(ip6_dst_idev(dst)->dev->flags & IFF_LOOPBACK))) {
			dst_release(dst);
			return NULL;
		}
	}

	return dst;
}
EXPORT_SYMBOL(cxgb_find_route6);
