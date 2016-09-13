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

#ifndef __LIBCXGB_CM_H__
#define __LIBCXGB_CM_H__


#include <net/tcp.h>

#include <cxgb4.h>
#include <t4_msg.h>

void
cxgb_get_4tuple(struct cpl_pass_accept_req *, enum chip_type,
		int *, __u8 *, __u8 *, __be16 *, __be16 *);
struct dst_entry *
cxgb_find_route(struct cxgb4_lld_info *,
		struct net_device *(*)(struct net_device *),
		__be32, __be32, __be16,	__be16, u8);
struct dst_entry *
cxgb_find_route6(struct cxgb4_lld_info *,
		 struct net_device *(*)(struct net_device *),
		 __u8 *, __u8 *, __be16, __be16, u8, __u32);

/* Returns whether a CPL status conveys negative advice.
 */
static inline bool cxgb_is_neg_adv(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
	       status == CPL_ERR_PERSIST_NEG_ADVICE ||
	       status == CPL_ERR_KEEPALV_NEG_ADVICE;
}

static inline void
cxgb_best_mtu(const unsigned short *mtus, unsigned short mtu,
	      unsigned int *idx, int use_ts, int ipv6)
{
	unsigned short hdr_size = (ipv6 ?
				   sizeof(struct ipv6hdr) :
				   sizeof(struct iphdr)) +
				  sizeof(struct tcphdr) +
				  (use_ts ?
				   round_up(TCPOLEN_TIMESTAMP, 4) : 0);
	unsigned short data_size = mtu - hdr_size;

	cxgb4_best_aligned_mtu(mtus, hdr_size, data_size, 8, idx);
}
#endif
