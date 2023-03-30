// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <net/xfrm.h>

#include "../nfp_net.h"
#include "nfd3.h"

void nfp_nfd3_ipsec_tx(struct nfp_nfd3_tx_desc *txd, struct sk_buff *skb)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct iphdr *iph = ip_hdr(skb);
	int l4_proto;

	if (x->xso.dev && (x->xso.dev->features & NETIF_F_HW_ESP_TX_CSUM)) {
		txd->flags |= NFD3_DESC_TX_CSUM;

		if (iph->version == 4)
			txd->flags |= NFD3_DESC_TX_IP4_CSUM;

		if (x->props.mode == XFRM_MODE_TRANSPORT)
			l4_proto = xo->proto;
		else if (x->props.mode == XFRM_MODE_TUNNEL)
			l4_proto = xo->inner_ipproto;
		else
			return;

		switch (l4_proto) {
		case IPPROTO_UDP:
			txd->flags |= NFD3_DESC_TX_UDP_CSUM;
			return;
		case IPPROTO_TCP:
			txd->flags |= NFD3_DESC_TX_TCP_CSUM;
			return;
		}
	}
}
