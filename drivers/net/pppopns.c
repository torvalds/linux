/* drivers/net/pppopns.c
 *
 * Driver for PPP on PPTP Network Server / PPPoPNS Socket (RFC 2637)
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Chia-chi Yeh <chiachi@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* This driver handles PPTP data packets between a RAW socket and a PPP channel.
 * The socket is created in the kernel space and connected to the same address
 * of the control socket. To keep things simple, packets are always sent with
 * sequence but without acknowledgement. This driver should work on both IPv4
 * and IPv6. */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/ppp_defs.h>
#include <linux/if.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/ppp_channel.h>

#define GRE_HEADER_SIZE		8

#define PPTP_GRE_MASK		htons(0x2001)
#define PPTP_GRE_SEQ_MASK	htons(0x1000)
#define PPTP_GRE_ACK_MASK	htons(0x0080)
#define PPTP_GRE_TYPE		htons(0x880B)

#define PPP_ADDR	0xFF
#define PPP_CTRL	0x03

struct header {
	__u16	bits;
	__u16	type;
	__u16	length;
	__u16	call;
	__u32	sequence;
} __attribute__((packed));

static void pppopns_recv(struct sock *sk_raw, int length)
{
	struct sock *sk;
	struct pppopns_opt *opt;
	struct sk_buff *skb;
	struct header *hdr;

	/* Lock sk_raw to prevent sk from being closed. */
	lock_sock(sk_raw);
	sk = (struct sock *)sk_raw->sk_user_data;
	if (!sk) {
		release_sock(sk_raw);
		return;
	}
	sock_hold(sk);
	release_sock(sk_raw);
	opt = &pppox_sk(sk)->proto.pns;

	/* Process packets from the receive queue. */
	while ((skb = skb_dequeue(&sk_raw->sk_receive_queue))) {
		skb_pull(skb, skb_transport_header(skb) - skb->data);

		/* Drop the packet if it is too short. */
		if (skb->len < GRE_HEADER_SIZE)
			goto drop;

		/* Check the header. */
		hdr = (struct header *)skb->data;
		if (hdr->type != PPTP_GRE_TYPE || hdr->call != opt->local ||
				(hdr->bits & PPTP_GRE_MASK) != PPTP_GRE_MASK)
			goto drop;

		/* Skip all fields including optional ones. */
		if (!skb_pull(skb, GRE_HEADER_SIZE +
				(hdr->bits & PPTP_GRE_SEQ_MASK ? 4 : 0) +
				(hdr->bits & PPTP_GRE_ACK_MASK ? 4 : 0)))
			goto drop;

		/* Check the length. */
		if (skb->len != ntohs(hdr->length))
			goto drop;

		/* Skip PPP address and control if they are present. */
		if (skb->len >= 2 && skb->data[0] == PPP_ADDR &&
				skb->data[1] == PPP_CTRL)
			skb_pull(skb, 2);

		/* Fix PPP protocol if it is compressed. */
		if (skb->len >= 1 && skb->data[0] & 1)
			skb_push(skb, 1)[0] = 0;

		/* Deliver the packet to PPP channel. We have to lock sk to
		 * prevent another thread from calling pppox_unbind_sock(). */
		skb_orphan(skb);
		lock_sock(sk);
		ppp_input(&pppox_sk(sk)->chan, skb);
		release_sock(sk);
		continue;
drop:
		kfree_skb(skb);
	}
	sock_put(sk);
}

static int pppopns_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk_raw = (struct sock *)chan->private;
	struct pppopns_opt *opt = &pppox_sk(sk_raw->sk_user_data)->proto.pns;
	struct msghdr msg = {.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT};
	struct kvec iov;
	struct header *hdr;
	__u16 length;

	/* Install PPP address and control. */
	skb_push(skb, 2);
	skb->data[0] = PPP_ADDR;
	skb->data[1] = PPP_CTRL;
	length = skb->len;

	/* Install PPTP GRE header. */
	hdr = (struct header *)skb_push(skb, 12);
	hdr->bits = PPTP_GRE_MASK | PPTP_GRE_SEQ_MASK;
	hdr->type = PPTP_GRE_TYPE;
	hdr->length = htons(length);
	hdr->call = opt->remote;
	hdr->sequence = htonl(opt->sequence);
	opt->sequence++;

	/* Now send the packet via RAW socket. */
	iov.iov_base = skb->data;
	iov.iov_len = skb->len;
	kernel_sendmsg(sk_raw->sk_socket, &msg, &iov, 1, skb->len);
	kfree_skb(skb);
	return 1;
}

/******************************************************************************/

static struct ppp_channel_ops pppopns_channel_ops = {
	.start_xmit = pppopns_xmit,
};

static int pppopns_connect(struct socket *sock, struct sockaddr *useraddr,
	int addrlen, int flags)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	struct sockaddr_pppopns *addr = (struct sockaddr_pppopns *)useraddr;
	struct sockaddr_storage ss;
	struct socket *sock_tcp = NULL;
	struct socket *sock_raw = NULL;
	struct sock *sk_raw;
	int error;

	if (addrlen != sizeof(struct sockaddr_pppopns))
		return -EINVAL;

	lock_sock(sk);
	error = -EALREADY;
	if (sk->sk_state != PPPOX_NONE)
		goto out;

	sock_tcp = sockfd_lookup(addr->tcp_socket, &error);
	if (!sock_tcp)
		goto out;
	error = -EPROTONOSUPPORT;
	if (sock_tcp->sk->sk_protocol != IPPROTO_TCP)
		goto out;
	addrlen = sizeof(struct sockaddr_storage);
	error = kernel_getpeername(sock_tcp, (struct sockaddr *)&ss, &addrlen);
	if (error)
		goto out;

	error = sock_create(ss.ss_family, SOCK_RAW, IPPROTO_GRE, &sock_raw);
	if (error)
		goto out;
	error = kernel_connect(sock_raw, (struct sockaddr *)&ss, addrlen, 0);
	if (error)
		goto out;
	sk_raw = sock_raw->sk;

	po->chan.hdrlen = 14;
	po->chan.private = sk_raw;
	po->chan.ops = &pppopns_channel_ops;
	po->chan.mtu = PPP_MTU - 80;
	po->proto.pns.local = addr->local;
	po->proto.pns.remote = addr->remote;

	error = ppp_register_channel(&po->chan);
	if (error)
		goto out;

	sk->sk_state = PPPOX_CONNECTED;
	sk_raw->sk_user_data = sk;
	sk_raw->sk_data_ready = pppopns_recv;

out:
	if (sock_tcp)
		sockfd_put(sock_tcp);
	if (error && sock_raw)
		sock_release(sock_raw);
	release_sock(sk);
	return error;
}

static int pppopns_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_DEAD)) {
		release_sock(sk);
		return -EBADF;
	}

	if (sk->sk_state != PPPOX_NONE) {
		struct sock *sk_raw = (struct sock *)pppox_sk(sk)->chan.private;
		lock_sock(sk_raw);
		pppox_unbind_sock(sk);
		sk_raw->sk_user_data = NULL;
		release_sock(sk_raw);
		sock_release(sk_raw->sk_socket);
	}

	sock_orphan(sk);
	sock->sk = NULL;
	release_sock(sk);
	sock_put(sk);
	return 0;
}

/******************************************************************************/

static struct proto pppopns_proto = {
	.name = "PPPOPNS",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

static struct proto_ops pppopns_proto_ops = {
	.family = PF_PPPOX,
	.owner = THIS_MODULE,
	.release = pppopns_release,
	.bind = sock_no_bind,
	.connect = pppopns_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = sock_no_getname,
	.poll = sock_no_poll,
	.ioctl = pppox_ioctl,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = sock_no_setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = sock_no_sendmsg,
	.recvmsg = sock_no_recvmsg,
	.mmap = sock_no_mmap,
};

static int pppopns_create(struct net *net, struct socket *sock)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_PPPOX, GFP_KERNEL, &pppopns_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->state = SS_UNCONNECTED;
	sock->ops = &pppopns_proto_ops;
	sk->sk_protocol = PX_PROTO_OPNS;
	sk->sk_state = PPPOX_NONE;
	return 0;
}

/******************************************************************************/

static struct pppox_proto pppopns_pppox_proto = {
	.create = pppopns_create,
	.owner = THIS_MODULE,
};

static int __init pppopns_init(void)
{
	int error;

	error = proto_register(&pppopns_proto, 0);
	if (error)
		return error;

	error = register_pppox_proto(PX_PROTO_OPNS, &pppopns_pppox_proto);
	if (error)
		proto_unregister(&pppopns_proto);
	return error;
}

static void __exit pppopns_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OPNS);
	proto_unregister(&pppopns_proto);
}

module_init(pppopns_init);
module_exit(pppopns_exit);

MODULE_DESCRIPTION("PPP on PPTP Network Server (PPPoPNS)");
MODULE_AUTHOR("Chia-chi Yeh <chiachi@android.com>");
MODULE_LICENSE("GPL");
