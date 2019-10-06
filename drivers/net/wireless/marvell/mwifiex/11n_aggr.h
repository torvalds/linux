/*
 * Marvell Wireless LAN device driver: 802.11n Aggregation
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef _MWIFIEX_11N_AGGR_H_
#define _MWIFIEX_11N_AGGR_H_

#define PKT_TYPE_AMSDU	0xE6
#define MIN_NUM_AMSDU 2

int mwifiex_11n_deaggregate_pkt(struct mwifiex_private *priv,
				struct sk_buff *skb);
int mwifiex_11n_aggregate_pkt(struct mwifiex_private *priv,
			      struct mwifiex_ra_list_tbl *ptr,
			      int ptr_index)
			      __releases(&priv->wmm.ra_list_spinlock);

#endif /* !_MWIFIEX_11N_AGGR_H_ */
