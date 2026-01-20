// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/skbuff.h>
#include <net/hotdata.h>
#include <net/inet_common.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/transp_v6.h>
#include <net/route.h>
#include <trace/events/sock.h>

#include "ovpnpriv.h"
#include "main.h"
#include "io.h"
#include "peer.h"
#include "proto.h"
#include "skb.h"
#include "tcp.h"

#define OVPN_TCP_DEPTH_NESTING	2
#if OVPN_TCP_DEPTH_NESTING == SINGLE_DEPTH_NESTING
#error "OVPN TCP requires its own lockdep subclass"
#endif

static struct proto ovpn_tcp_prot __ro_after_init;
static struct proto_ops ovpn_tcp_ops __ro_after_init;
static struct proto ovpn_tcp6_prot __ro_after_init;
static struct proto_ops ovpn_tcp6_ops __ro_after_init;

static int ovpn_tcp_parse(struct strparser *strp, struct sk_buff *skb)
{
	struct strp_msg *rxm = strp_msg(skb);
	__be16 blen;
	u16 len;
	int err;

	/* when packets are written to the TCP stream, they are prepended with
	 * two bytes indicating the actual packet size.
	 * Parse accordingly and return the actual size (including the size
	 * header)
	 */

	if (skb->len < rxm->offset + 2)
		return 0;

	err = skb_copy_bits(skb, rxm->offset, &blen, sizeof(blen));
	if (err < 0)
		return err;

	len = be16_to_cpu(blen);
	if (len < 2)
		return -EINVAL;

	return len + 2;
}

/* queue skb for sending to userspace via recvmsg on the socket */
static void ovpn_tcp_to_userspace(struct ovpn_peer *peer, struct sock *sk,
				  struct sk_buff *skb)
{
	skb_set_owner_r(skb, sk);
	memset(skb->cb, 0, sizeof(skb->cb));
	skb_queue_tail(&peer->tcp.user_queue, skb);
	peer->tcp.sk_cb.sk_data_ready(sk);
}

static void ovpn_tcp_rcv(struct strparser *strp, struct sk_buff *skb)
{
	struct ovpn_peer *peer = container_of(strp, struct ovpn_peer, tcp.strp);
	struct strp_msg *msg = strp_msg(skb);
	size_t pkt_len = msg->full_len - 2;
	size_t off = msg->offset + 2;
	u8 opcode;

	/* ensure skb->data points to the beginning of the openvpn packet */
	if (!pskb_pull(skb, off)) {
		net_warn_ratelimited("%s: packet too small for peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		goto err;
	}

	/* strparser does not trim the skb for us, therefore we do it now */
	if (pskb_trim(skb, pkt_len) != 0) {
		net_warn_ratelimited("%s: trimming skb failed for peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		goto err;
	}

	/* we need the first 4 bytes of data to be accessible
	 * to extract the opcode and the key ID later on
	 */
	if (!pskb_may_pull(skb, OVPN_OPCODE_SIZE)) {
		net_warn_ratelimited("%s: packet too small to fetch opcode for peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		goto err;
	}

	/* DATA_V2 packets are handled in kernel, the rest goes to user space */
	opcode = ovpn_opcode_from_skb(skb, 0);
	if (unlikely(opcode != OVPN_DATA_V2)) {
		if (opcode == OVPN_DATA_V1) {
			net_warn_ratelimited("%s: DATA_V1 detected on the TCP stream\n",
					     netdev_name(peer->ovpn->dev));
			goto err;
		}

		/* The packet size header must be there when sending the packet
		 * to userspace, therefore we put it back
		 */
		skb_push(skb, 2);
		ovpn_tcp_to_userspace(peer, strp->sk, skb);
		return;
	}

	/* hold reference to peer as required by ovpn_recv().
	 *
	 * NOTE: in this context we should already be holding a reference to
	 * this peer, therefore ovpn_peer_hold() is not expected to fail
	 */
	if (WARN_ON(!ovpn_peer_hold(peer)))
		goto err_nopeer;

	ovpn_recv(peer, skb);
	return;
err:
	/* take reference for deferred peer deletion. should never fail */
	if (WARN_ON(!ovpn_peer_hold(peer)))
		goto err_nopeer;
	schedule_work(&peer->tcp.defer_del_work);
	dev_dstats_rx_dropped(peer->ovpn->dev);
err_nopeer:
	kfree_skb(skb);
}

static int ovpn_tcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			    int flags, int *addr_len)
{
	int err = 0, off, copied = 0, ret;
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;
	struct sk_buff *skb;

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (unlikely(!sock || !sock->peer || !ovpn_peer_hold(sock->peer))) {
		rcu_read_unlock();
		return -EBADF;
	}
	peer = sock->peer;
	rcu_read_unlock();

	skb = __skb_recv_datagram(sk, &peer->tcp.user_queue, flags, &off, &err);
	if (!skb) {
		if (err == -EAGAIN && sk->sk_shutdown & RCV_SHUTDOWN) {
			ret = 0;
			goto out;
		}
		ret = err;
		goto out;
	}

	copied = len;
	if (copied > skb->len)
		copied = skb->len;
	else if (copied < skb->len)
		msg->msg_flags |= MSG_TRUNC;

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (unlikely(err)) {
		kfree_skb(skb);
		ret = err;
		goto out;
	}

	if (flags & MSG_TRUNC)
		copied = skb->len;
	kfree_skb(skb);
	ret = copied;
out:
	ovpn_peer_put(peer);
	return ret;
}

void ovpn_tcp_socket_detach(struct ovpn_socket *ovpn_sock)
{
	struct ovpn_peer *peer = ovpn_sock->peer;
	struct sock *sk = ovpn_sock->sk;

	strp_stop(&peer->tcp.strp);
	skb_queue_purge(&peer->tcp.user_queue);

	/* restore CBs that were saved in ovpn_sock_set_tcp_cb() */
	sk->sk_data_ready = peer->tcp.sk_cb.sk_data_ready;
	sk->sk_write_space = peer->tcp.sk_cb.sk_write_space;
	sk->sk_prot = peer->tcp.sk_cb.prot;
	sk->sk_socket->ops = peer->tcp.sk_cb.ops;

	rcu_assign_sk_user_data(sk, NULL);
}

void ovpn_tcp_socket_wait_finish(struct ovpn_socket *sock)
{
	struct ovpn_peer *peer = sock->peer;

	/* NOTE: we don't wait for peer->tcp.defer_del_work to finish:
	 * either the worker is not running or this function
	 * was invoked by that worker.
	 */

	cancel_work_sync(&sock->tcp_tx_work);
	strp_done(&peer->tcp.strp);

	skb_queue_purge(&peer->tcp.out_queue);
	kfree_skb(peer->tcp.out_msg.skb);
	peer->tcp.out_msg.skb = NULL;
}

static void ovpn_tcp_send_sock(struct ovpn_peer *peer, struct sock *sk)
{
	struct sk_buff *skb = peer->tcp.out_msg.skb;
	int ret, flags;

	if (!skb)
		return;

	if (peer->tcp.tx_in_progress)
		return;

	peer->tcp.tx_in_progress = true;

	do {
		flags = ovpn_skb_cb(skb)->nosignal ? MSG_NOSIGNAL : 0;
		ret = skb_send_sock_locked_with_flags(sk, skb,
						      peer->tcp.out_msg.offset,
						      peer->tcp.out_msg.len,
						      flags);
		if (unlikely(ret < 0)) {
			if (ret == -EAGAIN)
				goto out;

			net_warn_ratelimited("%s: TCP error to peer %u: %d\n",
					     netdev_name(peer->ovpn->dev),
					     peer->id, ret);

			/* in case of TCP error we can't recover the VPN
			 * stream therefore we abort the connection
			 */
			ovpn_peer_hold(peer);
			schedule_work(&peer->tcp.defer_del_work);

			/* we bail out immediately and keep tx_in_progress set
			 * to true. This way we prevent more TX attempts
			 * which would lead to more invocations of
			 * schedule_work()
			 */
			return;
		}

		peer->tcp.out_msg.len -= ret;
		peer->tcp.out_msg.offset += ret;
	} while (peer->tcp.out_msg.len > 0);

	if (!peer->tcp.out_msg.len) {
		preempt_disable();
		dev_dstats_tx_add(peer->ovpn->dev, skb->len);
		preempt_enable();
	}

	kfree_skb(peer->tcp.out_msg.skb);
	peer->tcp.out_msg.skb = NULL;
	peer->tcp.out_msg.len = 0;
	peer->tcp.out_msg.offset = 0;

out:
	peer->tcp.tx_in_progress = false;
}

void ovpn_tcp_tx_work(struct work_struct *work)
{
	struct ovpn_socket *sock;

	sock = container_of(work, struct ovpn_socket, tcp_tx_work);

	lock_sock(sock->sk);
	if (sock->peer)
		ovpn_tcp_send_sock(sock->peer, sock->sk);
	release_sock(sock->sk);
}

static void ovpn_tcp_send_sock_skb(struct ovpn_peer *peer, struct sock *sk,
				   struct sk_buff *skb)
{
	if (peer->tcp.out_msg.skb)
		ovpn_tcp_send_sock(peer, sk);

	if (peer->tcp.out_msg.skb) {
		dev_dstats_tx_dropped(peer->ovpn->dev);
		kfree_skb(skb);
		return;
	}

	peer->tcp.out_msg.skb = skb;
	peer->tcp.out_msg.len = skb->len;
	peer->tcp.out_msg.offset = 0;
	ovpn_tcp_send_sock(peer, sk);
}

void ovpn_tcp_send_skb(struct ovpn_peer *peer, struct sock *sk,
		       struct sk_buff *skb)
{
	u16 len = skb->len;

	*(__be16 *)__skb_push(skb, sizeof(u16)) = htons(len);

	spin_lock_nested(&sk->sk_lock.slock, OVPN_TCP_DEPTH_NESTING);
	if (sock_owned_by_user(sk)) {
		if (skb_queue_len(&peer->tcp.out_queue) >=
		    READ_ONCE(net_hotdata.max_backlog)) {
			dev_dstats_tx_dropped(peer->ovpn->dev);
			kfree_skb(skb);
			goto unlock;
		}
		__skb_queue_tail(&peer->tcp.out_queue, skb);
	} else {
		ovpn_tcp_send_sock_skb(peer, sk, skb);
	}
unlock:
	spin_unlock(&sk->sk_lock.slock);
}

static void ovpn_tcp_release(struct sock *sk)
{
	struct sk_buff_head queue;
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;
	struct sk_buff *skb;

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (!sock) {
		rcu_read_unlock();
		return;
	}

	peer = sock->peer;

	/* during initialization this function is called before
	 * assigning sock->peer
	 */
	if (unlikely(!peer || !ovpn_peer_hold(peer))) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	__skb_queue_head_init(&queue);
	skb_queue_splice_init(&peer->tcp.out_queue, &queue);

	while ((skb = __skb_dequeue(&queue)))
		ovpn_tcp_send_sock_skb(peer, sk, skb);

	peer->tcp.sk_cb.prot->release_cb(sk);
	ovpn_peer_put(peer);
}

static int ovpn_tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct ovpn_socket *sock;
	int ret, linear = PAGE_SIZE;
	struct ovpn_peer *peer;
	struct sk_buff *skb;

	lock_sock(sk);
	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (unlikely(!sock || !sock->peer || !ovpn_peer_hold(sock->peer))) {
		rcu_read_unlock();
		release_sock(sk);
		return -EIO;
	}
	rcu_read_unlock();
	peer = sock->peer;

	if (msg->msg_flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) {
		ret = -EOPNOTSUPP;
		goto peer_free;
	}

	if (peer->tcp.out_msg.skb) {
		ret = -EAGAIN;
		goto peer_free;
	}

	if (size < linear)
		linear = size;

	skb = sock_alloc_send_pskb(sk, linear, size - linear,
				   msg->msg_flags & MSG_DONTWAIT, &ret, 0);
	if (!skb) {
		net_err_ratelimited("%s: skb alloc failed: %d\n",
				    netdev_name(peer->ovpn->dev), ret);
		goto peer_free;
	}

	skb_put(skb, linear);
	skb->len = size;
	skb->data_len = size - linear;

	ret = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, size);
	if (ret) {
		kfree_skb(skb);
		net_err_ratelimited("%s: skb copy from iter failed: %d\n",
				    netdev_name(peer->ovpn->dev), ret);
		goto peer_free;
	}

	ovpn_skb_cb(skb)->nosignal = msg->msg_flags & MSG_NOSIGNAL;
	ovpn_tcp_send_sock_skb(peer, sk, skb);
	ret = size;
peer_free:
	release_sock(sk);
	ovpn_peer_put(peer);
	return ret;
}

static int ovpn_tcp_disconnect(struct sock *sk, int flags)
{
	return -EBUSY;
}

static void ovpn_tcp_data_ready(struct sock *sk)
{
	struct ovpn_socket *sock;

	trace_sk_data_ready(sk);

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (likely(sock && sock->peer))
		strp_data_ready(&sock->peer->tcp.strp);
	rcu_read_unlock();
}

static void ovpn_tcp_write_space(struct sock *sk)
{
	struct ovpn_socket *sock;

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (likely(sock && sock->peer)) {
		schedule_work(&sock->tcp_tx_work);
		sock->peer->tcp.sk_cb.sk_write_space(sk);
	}
	rcu_read_unlock();
}

static void ovpn_tcp_build_protos(struct proto *new_prot,
				  struct proto_ops *new_ops,
				  const struct proto *orig_prot,
				  const struct proto_ops *orig_ops);

static void ovpn_tcp_peer_del_work(struct work_struct *work)
{
	struct ovpn_peer *peer = container_of(work, struct ovpn_peer,
					      tcp.defer_del_work);

	ovpn_peer_del(peer, OVPN_DEL_PEER_REASON_TRANSPORT_ERROR);
	ovpn_peer_put(peer);
}

/* Set TCP encapsulation callbacks */
int ovpn_tcp_socket_attach(struct ovpn_socket *ovpn_sock,
			   struct ovpn_peer *peer)
{
	struct strp_callbacks cb = {
		.rcv_msg = ovpn_tcp_rcv,
		.parse_msg = ovpn_tcp_parse,
	};
	int ret;

	/* make sure no pre-existing encapsulation handler exists */
	if (ovpn_sock->sk->sk_user_data)
		return -EBUSY;

	/* only a fully connected socket is expected. Connection should be
	 * handled in userspace
	 */
	if (ovpn_sock->sk->sk_state != TCP_ESTABLISHED) {
		net_err_ratelimited("%s: provided TCP socket is not in ESTABLISHED state: %d\n",
				    netdev_name(peer->ovpn->dev),
				    ovpn_sock->sk->sk_state);
		return -EINVAL;
	}

	ret = strp_init(&peer->tcp.strp, ovpn_sock->sk, &cb);
	if (ret < 0) {
		DEBUG_NET_WARN_ON_ONCE(1);
		return ret;
	}

	INIT_WORK(&peer->tcp.defer_del_work, ovpn_tcp_peer_del_work);

	__sk_dst_reset(ovpn_sock->sk);
	skb_queue_head_init(&peer->tcp.user_queue);
	skb_queue_head_init(&peer->tcp.out_queue);

	/* save current CBs so that they can be restored upon socket release */
	peer->tcp.sk_cb.sk_data_ready = ovpn_sock->sk->sk_data_ready;
	peer->tcp.sk_cb.sk_write_space = ovpn_sock->sk->sk_write_space;
	peer->tcp.sk_cb.prot = ovpn_sock->sk->sk_prot;
	peer->tcp.sk_cb.ops = ovpn_sock->sk->sk_socket->ops;

	/* assign our static CBs and prot/ops */
	ovpn_sock->sk->sk_data_ready = ovpn_tcp_data_ready;
	ovpn_sock->sk->sk_write_space = ovpn_tcp_write_space;

	if (ovpn_sock->sk->sk_family == AF_INET) {
		ovpn_sock->sk->sk_prot = &ovpn_tcp_prot;
		ovpn_sock->sk->sk_socket->ops = &ovpn_tcp_ops;
	} else {
		ovpn_sock->sk->sk_prot = &ovpn_tcp6_prot;
		ovpn_sock->sk->sk_socket->ops = &ovpn_tcp6_ops;
	}

	/* avoid using task_frag */
	ovpn_sock->sk->sk_allocation = GFP_ATOMIC;
	ovpn_sock->sk->sk_use_task_frag = false;

	/* enqueue the RX worker */
	strp_check_rcv(&peer->tcp.strp);

	return 0;
}

static void ovpn_tcp_close(struct sock *sk, long timeout)
{
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (!sock || !sock->peer || !ovpn_peer_hold(sock->peer)) {
		rcu_read_unlock();
		return;
	}
	peer = sock->peer;
	rcu_read_unlock();

	ovpn_peer_del(sock->peer, OVPN_DEL_PEER_REASON_TRANSPORT_DISCONNECT);
	peer->tcp.sk_cb.prot->close(sk, timeout);
	ovpn_peer_put(peer);
}

static __poll_t ovpn_tcp_poll(struct file *file, struct socket *sock,
			      poll_table *wait)
{
	struct sk_buff_head *queue = &sock->sk->sk_receive_queue;
	struct ovpn_socket *ovpn_sock;
	struct ovpn_peer *peer = NULL;
	__poll_t mask;

	rcu_read_lock();
	ovpn_sock = rcu_dereference_sk_user_data(sock->sk);
	/* if we landed in this callback, we expect to have a
	 * meaningful state. The ovpn_socket lifecycle would
	 * prevent it otherwise.
	 */
	if (WARN(!ovpn_sock || !ovpn_sock->peer,
		 "ovpn: null state in ovpn_tcp_poll!")) {
		rcu_read_unlock();
		return 0;
	}

	if (ovpn_peer_hold(ovpn_sock->peer)) {
		peer = ovpn_sock->peer;
		queue = &peer->tcp.user_queue;
	}
	rcu_read_unlock();

	mask = datagram_poll_queue(file, sock, wait, queue);

	if (peer)
		ovpn_peer_put(peer);

	return mask;
}

static void ovpn_tcp_build_protos(struct proto *new_prot,
				  struct proto_ops *new_ops,
				  const struct proto *orig_prot,
				  const struct proto_ops *orig_ops)
{
	memcpy(new_prot, orig_prot, sizeof(*new_prot));
	memcpy(new_ops, orig_ops, sizeof(*new_ops));
	new_prot->recvmsg = ovpn_tcp_recvmsg;
	new_prot->sendmsg = ovpn_tcp_sendmsg;
	new_prot->disconnect = ovpn_tcp_disconnect;
	new_prot->close = ovpn_tcp_close;
	new_prot->release_cb = ovpn_tcp_release;
	new_ops->poll = ovpn_tcp_poll;
}

/* Initialize TCP static objects */
void __init ovpn_tcp_init(void)
{
	ovpn_tcp_build_protos(&ovpn_tcp_prot, &ovpn_tcp_ops, &tcp_prot,
			      &inet_stream_ops);

#if IS_ENABLED(CONFIG_IPV6)
	ovpn_tcp_build_protos(&ovpn_tcp6_prot, &ovpn_tcp6_ops, &tcpv6_prot,
			      &inet6_stream_ops);
#endif
}
