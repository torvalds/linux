// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <net/xfrm.h>

#include "../nfp_net.h"
#include "nfd3.h"

void nfp_nfd3_ipsec_tx(struct nfp_nfd3_tx_desc *txd, struct sk_buff *skb)
{
	struct xfrm_state *x = xfrm_input_state(skb);

	if (x->xso.dev && (x->xso.dev->features & NETIF_F_HW_ESP_TX_CSUM)) {
		txd->flags |= NFD3_DESC_TX_CSUM | NFD3_DESC_TX_IP4_CSUM |
			      NFD3_DESC_TX_TCP_CSUM | NFD3_DESC_TX_UDP_CSUM;
	}
}
