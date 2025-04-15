// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2012-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/netdevice.h>
#include <linux/socket.h>

#include "ovpnpriv.h"
#include "bind.h"
#include "peer.h"

/**
 * ovpn_bind_from_sockaddr - retrieve binding matching sockaddr
 * @ss: the sockaddr to match
 *
 * Return: the bind matching the passed sockaddr if found, NULL otherwise
 */
struct ovpn_bind *ovpn_bind_from_sockaddr(const struct sockaddr_storage *ss)
{
	struct ovpn_bind *bind;
	size_t sa_len;

	if (ss->ss_family == AF_INET)
		sa_len = sizeof(struct sockaddr_in);
	else if (ss->ss_family == AF_INET6)
		sa_len = sizeof(struct sockaddr_in6);
	else
		return ERR_PTR(-EAFNOSUPPORT);

	bind = kzalloc(sizeof(*bind), GFP_ATOMIC);
	if (unlikely(!bind))
		return ERR_PTR(-ENOMEM);

	memcpy(&bind->remote, ss, sa_len);

	return bind;
}

/**
 * ovpn_bind_reset - assign new binding to peer
 * @peer: the peer whose binding has to be replaced
 * @new: the new bind to assign
 */
void ovpn_bind_reset(struct ovpn_peer *peer, struct ovpn_bind *new)
{
	lockdep_assert_held(&peer->lock);

	kfree_rcu(rcu_replace_pointer(peer->bind, new,
				      lockdep_is_held(&peer->lock)), rcu);
}
