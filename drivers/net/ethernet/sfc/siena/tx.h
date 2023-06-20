/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2015 Solarflare Communications Inc.
 */

#ifndef EFX_TX_H
#define EFX_TX_H

#include <linux/types.h>

/* Driver internal tx-path related declarations. */
/* What TXQ type will satisfy the checksum offloads required for this skb? */
static inline unsigned int efx_tx_csum_type_skb(struct sk_buff *skb)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0; /* no checksum offload */

	if (skb->encapsulation &&
	    skb_checksum_start_offset(skb) == skb_inner_transport_offset(skb)) {
		/* we only advertise features for IPv4 and IPv6 checksums on
		 * encapsulated packets, so if the checksum is for the inner
		 * packet, it must be one of them; no further checking required.
		 */

		/* Do we also need to offload the outer header checksum? */
		if (skb_shinfo(skb)->gso_segs > 1 &&
		    !(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL) &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM))
			return EFX_TXQ_TYPE_OUTER_CSUM | EFX_TXQ_TYPE_INNER_CSUM;
		return EFX_TXQ_TYPE_INNER_CSUM;
	}

	/* similarly, we only advertise features for IPv4 and IPv6 checksums,
	 * so it must be one of them. No need for further checks.
	 */
	return EFX_TXQ_TYPE_OUTER_CSUM;
}
#endif /* EFX_TX_H */
