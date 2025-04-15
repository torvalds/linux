// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/atomic.h>

#include "stats.h"

void ovpn_peer_stats_init(struct ovpn_peer_stats *ps)
{
	atomic64_set(&ps->rx.bytes, 0);
	atomic64_set(&ps->rx.packets, 0);

	atomic64_set(&ps->tx.bytes, 0);
	atomic64_set(&ps->tx.packets, 0);
}
