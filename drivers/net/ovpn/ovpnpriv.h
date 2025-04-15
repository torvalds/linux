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

#include <uapi/linux/if_link.h>
#include <uapi/linux/ovpn.h>

/**
 * struct ovpn_priv - per ovpn interface state
 * @dev: the actual netdev representing the tunnel
 * @mode: device operation mode (i.e. p2p, mp, ..)
 */
struct ovpn_priv {
	struct net_device *dev;
	enum ovpn_mode mode;
};

#endif /* _NET_OVPN_OVPNSTRUCT_H_ */
