/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNSTRUCT_H_
#define _NET_OVPN_OVPNSTRUCT_H_

#include <linux/workqueue.h>
#include <net/gro_cells.h>
#include <uapi/linux/if_link.h>
#include <uapi/linux/ovpn.h>

/**
 * struct ovpn_peer_collection - container of peers for MultiPeer mode
 * @by_id: table of peers index by ID
 * @by_vpn_addr4: table of peers indexed by VPN IPv4 address (items can be
 *		  rehashed on the fly due to peer IP change)
 * @by_vpn_addr6: table of peers indexed by VPN IPv6 address (items can be
 *		  rehashed on the fly due to peer IP change)
 * @by_transp_addr: table of peers indexed by transport address (items can be
 *		    rehashed on the fly due to peer IP change)
 */
struct ovpn_peer_collection {
	DECLARE_HASHTABLE(by_id, 12);
	struct hlist_nulls_head by_vpn_addr4[1 << 12];
	struct hlist_nulls_head by_vpn_addr6[1 << 12];
	struct hlist_nulls_head by_transp_addr[1 << 12];
};

/**
 * struct ovpn_priv - per ovpn interface state
 * @dev: the actual netdev representing the tunnel
 * @mode: device operation mode (i.e. p2p, mp, ..)
 * @lock: protect this object
 * @peers: data structures holding multi-peer references
 * @peer: in P2P mode, this is the only remote peer
 * @gro_cells: pointer to the Generic Receive Offload cell
 * @keepalive_work: struct used to schedule keepalive periodic job
 */
struct ovpn_priv {
	struct net_device *dev;
	enum ovpn_mode mode;
	spinlock_t lock; /* protect writing to the ovpn_priv object */
	struct ovpn_peer_collection *peers;
	struct ovpn_peer __rcu *peer;
	struct gro_cells gro_cells;
	struct delayed_work keepalive_work;
};

#endif /* _NET_OVPN_OVPNSTRUCT_H_ */
