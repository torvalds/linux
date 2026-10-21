/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: 802.11n Aggregation
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_11N_AGGR_H_
#define _NXPWIFI_11N_AGGR_H_

#define PKT_TYPE_AMSDU	0xE6
#define MIN_NUM_AMSDU 2

int nxpwifi_11n_deaggregate_pkt(struct nxpwifi_private *priv,
				struct sk_buff *skb);
int nxpwifi_11n_aggregate_pkt(struct nxpwifi_private *priv,
			      struct nxpwifi_ra_list_tbl *ptr,
			      int ptr_index)
			      __releases(&priv->wmm.ra_list_spinlock);

#endif /* !_NXPWIFI_11N_AGGR_H_ */
