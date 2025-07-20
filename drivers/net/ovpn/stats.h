/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 *		Lev Stipakov <lev@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNSTATS_H_
#define _NET_OVPN_OVPNSTATS_H_

/* one stat */
struct ovpn_peer_stat {
	atomic64_t bytes;
	atomic64_t packets;
};

/* rx and tx stats combined */
struct ovpn_peer_stats {
	struct ovpn_peer_stat rx;
	struct ovpn_peer_stat tx;
};

void ovpn_peer_stats_init(struct ovpn_peer_stats *ps);

static inline void ovpn_peer_stats_increment(struct ovpn_peer_stat *stat,
					     const unsigned int n)
{
	atomic64_add(n, &stat->bytes);
	atomic64_inc(&stat->packets);
}

static inline void ovpn_peer_stats_increment_rx(struct ovpn_peer_stats *stats,
						const unsigned int n)
{
	ovpn_peer_stats_increment(&stats->rx, n);
}

static inline void ovpn_peer_stats_increment_tx(struct ovpn_peer_stats *stats,
						const unsigned int n)
{
	ovpn_peer_stats_increment(&stats->tx, n);
}

#endif /* _NET_OVPN_OVPNSTATS_H_ */
