// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2023 Corigine, Inc */

#include <net/xfrm.h>

#include "../nfp_net.h"
#include "nfdk.h"

u64 nfp_nfdk_ipsec_tx(u64 flags, struct sk_buff *skb)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	struct iphdr *iph = ip_hdr(skb);

	if (x->xso.dev && (x->xso.dev->features & NETIF_F_HW_ESP_TX_CSUM)) {
		if (iph->version == 4)
			flags |= NFDK_DESC_TX_L3_CSUM;
		flags |= NFDK_DESC_TX_L4_CSUM;
	}

	return flags;
}
