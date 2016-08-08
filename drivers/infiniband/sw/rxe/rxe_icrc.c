/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
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

#include "rxe.h"
#include "rxe_loc.h"

/* Compute a partial ICRC for all the IB transport headers. */
u32 rxe_icrc_hdr(struct rxe_pkt_info *pkt, struct sk_buff *skb)
{
	unsigned int bth_offset = 0;
	struct iphdr *ip4h = NULL;
	struct ipv6hdr *ip6h = NULL;
	struct udphdr *udph;
	struct rxe_bth *bth;
	int crc;
	int length;
	int hdr_size = sizeof(struct udphdr) +
		(skb->protocol == htons(ETH_P_IP) ?
		sizeof(struct iphdr) : sizeof(struct ipv6hdr));
	/* pseudo header buffer size is calculate using ipv6 header size since
	 * it is bigger than ipv4
	 */
	u8 pshdr[sizeof(struct udphdr) +
		sizeof(struct ipv6hdr) +
		RXE_BTH_BYTES];

	/* This seed is the result of computing a CRC with a seed of
	 * 0xfffffff and 8 bytes of 0xff representing a masked LRH.
	 */
	crc = 0xdebb20e3;

	if (skb->protocol == htons(ETH_P_IP)) { /* IPv4 */
		memcpy(pshdr, ip_hdr(skb), hdr_size);
		ip4h = (struct iphdr *)pshdr;
		udph = (struct udphdr *)(ip4h + 1);

		ip4h->ttl = 0xff;
		ip4h->check = CSUM_MANGLED_0;
		ip4h->tos = 0xff;
	} else {				/* IPv6 */
		memcpy(pshdr, ipv6_hdr(skb), hdr_size);
		ip6h = (struct ipv6hdr *)pshdr;
		udph = (struct udphdr *)(ip6h + 1);

		memset(ip6h->flow_lbl, 0xff, sizeof(ip6h->flow_lbl));
		ip6h->priority = 0xf;
		ip6h->hop_limit = 0xff;
	}
	udph->check = CSUM_MANGLED_0;

	bth_offset += hdr_size;

	memcpy(&pshdr[bth_offset], pkt->hdr, RXE_BTH_BYTES);
	bth = (struct rxe_bth *)&pshdr[bth_offset];

	/* exclude bth.resv8a */
	bth->qpn |= cpu_to_be32(~BTH_QPN_MASK);

	length = hdr_size + RXE_BTH_BYTES;
	crc = crc32_le(crc, pshdr, length);

	/* And finish to compute the CRC on the remainder of the headers. */
	crc = crc32_le(crc, pkt->hdr + RXE_BTH_BYTES,
		       rxe_opcode[pkt->opcode].length - RXE_BTH_BYTES);
	return crc;
}
