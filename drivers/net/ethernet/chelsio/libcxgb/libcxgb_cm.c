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
