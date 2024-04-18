// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include <net/libeth/rx.h>

/* Converting abstract packet type numbers into a software structure with
 * the packet parameters to do O(1) lookup on Rx.
 */

static const u16 libeth_rx_pt_xdp_oip[] = {
	[LIBETH_RX_PT_OUTER_L2]		= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_OUTER_IPV4]	= XDP_RSS_L3_IPV4,
	[LIBETH_RX_PT_OUTER_IPV6]	= XDP_RSS_L3_IPV6,
};

static const u16 libeth_rx_pt_xdp_iprot[] = {
	[LIBETH_RX_PT_INNER_NONE]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_INNER_UDP]	= XDP_RSS_L4_UDP,
	[LIBETH_RX_PT_INNER_TCP]	= XDP_RSS_L4_TCP,
	[LIBETH_RX_PT_INNER_SCTP]	= XDP_RSS_L4_SCTP,
	[LIBETH_RX_PT_INNER_ICMP]	= XDP_RSS_L4_ICMP,
	[LIBETH_RX_PT_INNER_TIMESYNC]	= XDP_RSS_TYPE_NONE,
};

static const u16 libeth_rx_pt_xdp_pl[] = {
	[LIBETH_RX_PT_PAYLOAD_NONE]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L2]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L3]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L4]	= XDP_RSS_L4,
};

/**
 * libeth_rx_pt_gen_hash_type - generate an XDP RSS hash type for a PT
 * @pt: PT structure to evaluate
 *
 * Generates ```hash_type``` field with XDP RSS type values from the parsed
 * packet parameters if they're obtained dynamically at runtime.
 */
void libeth_rx_pt_gen_hash_type(struct libeth_rx_pt *pt)
{
	pt->hash_type = 0;
	pt->hash_type |= libeth_rx_pt_xdp_oip[pt->outer_ip];
	pt->hash_type |= libeth_rx_pt_xdp_iprot[pt->inner_prot];
	pt->hash_type |= libeth_rx_pt_xdp_pl[pt->payload_layer];
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_pt_gen_hash_type, LIBETH);

/* Module */

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Common Ethernet library");
MODULE_LICENSE("GPL");
