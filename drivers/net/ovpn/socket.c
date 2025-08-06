// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/udp.h>

#include "ovpnpriv.h"
#include "main.h"
#include "io.h"
#include "peer.h"
#include "socket.h"
#include "tcp.h"
#include "udp.h"

static void ovpn_socket_release_kref(struct kref *kref)
{
	struct ovpn_socket *sock = container_of(kref, struct ovpn_socket,
						refcount);

	if (sock->sk->sk_protocol == IPPROTO_UDP)
		ovpn_udp_socket_detach(sock);
	else if (sock->sk->sk_protocol == IPPROTO_TCP)
		ovpn_tcp_socket_detach(sock);
}

/**
 * ovpn_socket_put - decrease reference counter
 * @peer: peer whose socket reference counter should be decreased
 * @sock: the RCU protected peer socket
 *
 * This function is only used internally. Users willing to release
 * references to the ovpn_socket should use ovpn_socket_release()
 *
 * Return: true if the socket was released, false otherwise
 */
static bool ovpn_socket_put(struct ovpn_peer *peer, struct ovpn_socket *sock)
{
	return kref_put(&sock->refcount, ovpn_socket_release_kref);
}

/**
 * ovpn_socket_release - release resources owned by socket user
 * @peer: peer whose socket should be released
 *
 * This function should be invoked when the peer is being removed
 * and wants to drop its link to the socket.
 *
 * In case of UDP, the detach routine will drop a reference to the
 * ovpn netdev, pointed by the ovpn_socket.
 *
 * In case of TCP, releasing the socket will cause dropping
 * the refcounter for the peer it is linked to, thus allowing the peer
 * disappear as well.
 *
 * This function is expected to be invoked exactly once per peer
 *
 * NOTE: this function may sleep
 */
void ovpn_socket_release(struct ovpn_peer *peer)
{
	struct ovpn_socket *sock;
	bool released;

	might_sleep();

	sock = rcu_replace_pointer(peer->sock, NULL, true);
	/* release may be invoked after socket was detached */
	if (!sock)
		return;

	/* Drop the reference while holding the sock lock to avoid
	 * concurrent ovpn_socket_new call to mess up with a partially
	 * detached socket.
	 *
	 * Holding the lock ensures that a socket with refcnt 0 is fully
	 * detached before it can be picked by a concurrent reader.
	 */
	lock_sock(sock->sk);
	released = ovpn_socket_put(peer, sock);
	release_sock(sock->sk);

	/* align all readers with sk_user_data being NULL */
	synchronize_rcu();

	/* following cleanup should happen with lock released */
	if (released) {
		if (sock->sk->sk_protocol == IPPROTO_UDP) {
			netdev_put(sock->ovpn->dev, &sock->dev_tracker);
		} else if (sock->sk->sk_protocol == IPPROTO_TCP) {
			/* wait for TCP jobs to terminate */
			ovpn_tcp_socket_wait_finish(sock);
			ovpn_peer_put(sock->peer);
		}
		/* drop reference acquired in ovpn_socket_new() */
		sock_put(sock->sk);
		/* we can call plain kfree() because we already waited one RCU
		 * period due to synchronize_rcu()
		 */
		kfree(sock);
	}
}

static bool ovpn_socket_hold(struct ovpn_socket *sock)
{
	return kref_get_unless_zero(&sock->refcount);
}

static int ovpn_socket_attach(struct ovpn_socket *ovpn_sock,
			      struct socket *sock,
			      struct ovpn_peer *peer)
{
	if (sock->sk->sk_protocol == IPPROTO_UDP)
		return ovpn_udp_socket_attach(ovpn_sock, sock, peer->ovpn);
	else if (sock->sk->sk_protocol == IPPROTO_TCP)
		return ovpn_tcp_socket_attach(ovpn_sock, peer);

	return -EOPNOTSUPP;
}

/**
 * ovpn_socket_new - create a new socket and initialize it
 * @sock: the kernel socket to embed
 * @peer: the peer reachable via this socket
 *
 * Return: an openvpn socket on success or a negative error code otherwise
 */
struct ovpn_socket *ovpn_socket_new(struct socket *sock, struct ovpn_peer *peer)
{
	struct ovpn_socket *ovpn_sock;
	struct sock *sk = sock->sk;
	int ret;

	lock_sock(sk);

	/* a TCP socket can only be owned by a single peer, therefore there
	 * can't be any other user
	 */
	if (sk->sk_protocol == IPPROTO_TCP && sk->sk_user_data) {
		ovpn_sock = ERR_PTR(-EBUSY);
		goto sock_release;
	}

	/* a UDP socket can be shared across multiple peers, but we must make
	 * sure it is not owned by something else
	 */
	if (sk->sk_protocol == IPPROTO_UDP) {
		u8 type = READ_ONCE(udp_sk(sk)->encap_type);

		/* socket owned by other encapsulation module */
		if (type && type != UDP_ENCAP_OVPNINUDP) {
			ovpn_sock = ERR_PTR(-EBUSY);
			goto sock_release;
		}

		rcu_read_lock();
		ovpn_sock = rcu_dereference_sk_user_data(sk);
		if (ovpn_sock) {
			/* socket owned by another ovpn instance, we can't use it */
			if (ovpn_sock->ovpn != peer->ovpn) {
				ovpn_sock = ERR_PTR(-EBUSY);
				rcu_read_unlock();
				goto sock_release;
			}

			/* this socket is already owned by this instance,
			 * therefore we can increase the refcounter and
			 * use it as expected
			 */
			if (WARN_ON(!ovpn_socket_hold(ovpn_sock))) {
				/* this should never happen because setting
				 * the refcnt to 0 and detaching the socket
				 * is expected to be atomic
				 */
				ovpn_sock = ERR_PTR(-EAGAIN);
				rcu_read_unlock();
				goto sock_release;
			}

			rcu_read_unlock();
			goto sock_release;
		}
		rcu_read_unlock();
	}

	/* socket is not owned: attach to this ovpn instance */

	ovpn_sock = kzalloc(sizeof(*ovpn_sock), GFP_KERNEL);
	if (!ovpn_sock) {
		ovpn_sock = ERR_PTR(-ENOMEM);
		goto sock_release;
	}

	ovpn_sock->sk = sk;
	kref_init(&ovpn_sock->refcount);

	/* the newly created ovpn_socket is holding reference to sk,
	 * therefore we increase its refcounter.
	 *
	 * This ovpn_socket instance is referenced by all peers
	 * using the same socket.
	 *
	 * ovpn_socket_release() will take care of dropping the reference.
	 */
	sock_hold(sk);

	ret = ovpn_socket_attach(ovpn_sock, sock, peer);
	if (ret < 0) {
		sock_put(sk);
		kfree(ovpn_sock);
		ovpn_sock = ERR_PTR(ret);
		goto sock_release;
	}

	/* TCP sockets are per-peer, therefore they are linked to their unique
	 * peer
	 */
	if (sk->sk_protocol == IPPROTO_TCP) {
		INIT_WORK(&ovpn_sock->tcp_tx_work, ovpn_tcp_tx_work);
		ovpn_sock->peer = peer;
		ovpn_peer_hold(peer);
	} else if (sk->sk_protocol == IPPROTO_UDP) {
		/* in UDP we only link the ovpn instance since the socket is
		 * shared among multiple peers
		 */
		ovpn_sock->ovpn = peer->ovpn;
		netdev_hold(peer->ovpn->dev, &ovpn_sock->dev_tracker,
			    GFP_KERNEL);
	}

	rcu_assign_sk_user_data(sk, ovpn_sock);
sock_release:
	release_sock(sk);
	return ovpn_sock;
}
