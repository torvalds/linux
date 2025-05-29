/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_NETLINK_H_
#define _NET_OVPN_NETLINK_H_

int ovpn_nl_register(void);
void ovpn_nl_unregister(void);

int ovpn_nl_peer_del_notify(struct ovpn_peer *peer);
int ovpn_nl_key_swap_notify(struct ovpn_peer *peer, u8 key_id);

#endif /* _NET_OVPN_NETLINK_H_ */
