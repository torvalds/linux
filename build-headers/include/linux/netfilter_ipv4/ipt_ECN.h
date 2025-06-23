/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Header file for iptables ipt_ECN target
 *
 * (C) 2002 by Harald Welte <laforge@gnumonks.org>
 *
 * This software is distributed under GNU GPL v2, 1991
 * 
 * ipt_ECN.h,v 1.3 2002/05/29 12:17:40 laforge Exp
*/
#ifndef _IPT_ECN_TARGET_H
#define _IPT_ECN_TARGET_H

#include <linux/types.h>
#include <linux/netfilter/xt_DSCP.h>

#define IPT_ECN_IP_MASK	(~XT_DSCP_MASK)

#define IPT_ECN_OP_SET_IP	0x01	/* set ECN bits of IPv4 header */
#define IPT_ECN_OP_SET_ECE	0x10	/* set ECE bit of TCP header */
#define IPT_ECN_OP_SET_CWR	0x20	/* set CWR bit of TCP header */

#define IPT_ECN_OP_MASK		0xce

struct ipt_ECN_info {
	__u8 operation;	/* bitset of operations */
	__u8 ip_ect;	/* ECT codepoint of IPv4 header, pre-shifted */
	union {
		struct {
			__u8 ece:1, cwr:1; /* TCP ECT bits */
		} tcp;
	} proto;
};

#endif /* _IPT_ECN_TARGET_H */
