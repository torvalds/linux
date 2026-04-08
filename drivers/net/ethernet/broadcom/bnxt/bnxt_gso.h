/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Broadcom NetXtreme-C/E network driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_GSO_H
#define BNXT_GSO_H

/* Maximum segments the stack may send in a single SW USO skb.
 * This caps gso_max_segs for NICs without HW USO support.
 */
#define BNXT_SW_USO_MAX_SEGS	64

/* Worst-case TX descriptors consumed by one SW USO packet:
 * Each segment: 1 long BD + 1 ext BD + payload BDs.
 * Total payload BDs across all segs <= num_segs + nr_frags (each frag
 * boundary crossing adds at most 1 extra BD).
 * So: 3 * max_segs + MAX_SKB_FRAGS + 1 = 3 * 64 + 17 + 1 = 210.
 */
#define BNXT_SW_USO_MAX_DESCS	(3 * BNXT_SW_USO_MAX_SEGS + MAX_SKB_FRAGS + 1)

netdev_tx_t bnxt_sw_udp_gso_xmit(struct bnxt *bp,
				 struct bnxt_tx_ring_info *txr,
				 struct netdev_queue *txq,
				 struct sk_buff *skb);

#endif
