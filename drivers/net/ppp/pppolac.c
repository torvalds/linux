/* drivers/net/pppolac.c
 *
 * Driver for PPP on L2TP Access Concentrator / PPPoLAC Socket (RFC 2661)
 *
 * Copyright (C) 2009 Google, Inc.
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

/* This driver handles L2TP data packets between a UDP socket and a PPP channel.
 * To keep things simple, only one session per socket is permitted. Packets are
 * sent via the socket, so it must keep connected to the same address. One must
 * not set sequencing in ICCN but let LNS controll it. Currently this driver
 * only works on IPv4 due to the lack of UDP encapsulation support in IPv6. */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/udp.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/ppp_channel.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>

#define L2TP_CONTROL_BIT	0x80
#define L2TP_LENGTH_BIT		0x40
#define L2TP_SEQUENCE_BIT	0x08
#define L2TP_OFFSET_BIT		0x02
#define L2TP_VERSION		0x02
#define L2TP_VERSION_MASK	0x0F

#define PPP_ADDR	0xFF
#define PPP_CTRL	0x03

union unaligned {
	__u32 u32;
} __attribute__((packed));

static inline union unaligned *unaligned(void *ptr)
{
	return (union unaligned *)ptr;
}

static int pppolac_recv_core(struct sock *sk_udp, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *)sk_udp->sk_user_data;
	struct pppolac_opt *opt = &pppox_sk(sk)->proto.lac;
	__u8 bits;
	__u8 *ptr;

	/* Drop the packet if it is too short. */
	if (skb->len < sizeof(struct udphdr) + 6)
		goto drop;

	/* Put it back if it is a control packet. */
	if (skb->data[sizeof(struct udphdr)] & L2TP_CONTROL_BIT)
		return opt->backlog_rcv(sk_udp, skb);

	/* Skip UDP header. */
	skb_pull(skb, sizeof(struct udphdr));

	/* Check the version. */
	if ((skb->data[1] & L2TP_VERSION_MASK) != L2TP_VERSION)
		goto drop;
	bits = skb->data[0];
	ptr = &skb->data[2];

	/* Check the length if it is present. */
	if (bits & L2TP_LENGTH_BIT) {
		if ((ptr[0] << 8 | ptr[1]) != skb->len)
			goto drop;
		ptr += 2;
	}

	/* Skip all fields including optional ones. */
	if (!skb_pull(skb, 6 + (bits & L2TP_SEQUENCE_BIT ? 4 : 0) +
			(bits & L2TP_LENGTH_BIT ? 2 : 0) +
			(bits & L2TP_OFFSET_BIT ? 2 : 0)))
		goto drop;

	/* Skip the offset padding if it is present. */
	if (bits & L2TP_OFFSET_BIT &&
			!skb_pull(skb, skb->data[-2] << 8 | skb->data[-1]))
		goto drop;

	/* Check the tunnel and the session. */
	if (unaligned(ptr)->u32 != opt->local)
		goto drop;

	/* Check the sequence if it is present. According to RFC 2661 section
	 * 5.4, the only thing to do is to update opt->sequencing. */
	opt->sequencing = bits & L2TP_SEQUENCE_BIT;

	/* Skip PPP address and control if they are present. */
	if (skb->len >= 2 && skb->data[0] == PPP_ADDR &&
			skb->data[1] == PPP_CTRL)
		skb_pull(skb, 2);

	/* Fix PPP protocol if it is compressed. */
	if (skb->len >= 1 && skb->data[0] & 1)
		skb_push(skb, 1)[0] = 0;

	/* Finally, deliver the packet to PPP channel. */
	skb_orphan(skb);
	ppp_input(&pppox_sk(sk)->chan, skb);
	return NET_RX_SUCCESS;
drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static int pppolac_recv(struct sock *sk_udp, struct sk_buff *skb)
{
	sock_hold(sk_udp);
	sk_receive_skb(sk_udp, skb, 0);
	return 0;
}

static struct sk_buff_head delivery_queue;

static void pppolac_xmit_core(struct work_struct *delivery_work)
{
	mm_segment_t old_fs = get_fs();
	struct sk_buff *skb;

	set_fs(KERNEL_DS);
	while ((skb = skb_dequeue(&delivery_queue))) {
		struct sock *sk_udp = skb->sk;
		struct kvec iov = {.iov_base = skb->data, .iov_len = skb->len};
		struct msghdr msg = {
			.msg_iov = (struct iovec *)&iov,
			.msg_iovlen = 1,
			.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT,
		};
		sk_udp->sk_prot->sendmsg(NULL, sk_udp, &msg, skb->len);
		kfree_skb(skb);
	}
	set_fs(old_fs);
}

static DECLARE_WORK(delivery_work, pppolac_xmit_core);

static int pppolac_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk_udp = (struct sock *)chan->private;
	struct pppolac_opt *opt = &pppox_sk(sk_udp->sk_user_data)->proto.lac;

	/* Install PPP address and control. */
	skb_push(skb, 2);
	skb->data[0] = PPP_ADDR;
	skb->data[1] = PPP_CTRL;

	/* Install L2TP header. */
	if (opt->sequencing) {
		skb_push(skb, 10);
		skb->data[0] = L2TP_SEQUENCE_BIT;
		skb->data[6] = opt->sequence >> 8;
		skb->data[7] = opt->sequence;
		skb->data[8] = 0;
		skb->data[9] = 0;
		opt->sequence++;
	} else {
		skb_push(skb, 6);
		skb->data[0] = 0;
	}
	skb->data[1] = L2TP_VERSION;
	unaligned(&skb->data[2])->u32 = opt->remote;

	/* Now send the packet via the delivery queue. */
	skb_set_owner_w(skb, sk_udp);
	skb_queue_tail(&delivery_queue, skb);
	schedule_work(&delivery_work);
	return 1;
}

/******************************************************************************/

static struct ppp_channel_ops pppolac_channel_ops = {
	.start_xmit = pppolac_xmit,
};

static int pppolac_connect(struct socket *sock, struct sockaddr *useraddr,
	int addrlen, int flags)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	struct sockaddr_pppolac *addr = (struct sockaddr_pppolac *)useraddr;
	struct socket *sock_udp = NULL;
	struct sock *sk_udp;
	int error;

	if (addrlen != sizeof(struct sockaddr_pppolac) ||
			!addr->local.tunnel || !addr->local.session ||
			!addr->remote.tunnel || !addr->remote.session) {
		return -EINVAL;
	}

	lock_sock(sk);
	error = -EALREADY;
	if (sk->sk_state != PPPOX_NONE)
		goto out;

	sock_udp = sockfd_lookup(addr->udp_socket, &error);
	if (!sock_udp)
		goto out;
	sk_udp = sock_udp->sk;
	lock_sock(sk_udp);

	/* Remove this check when IPv6 supports UDP encapsulation. */
	error = -EAFNOSUPPORT;
	if (sk_udp->sk_family != AF_INET)
		goto out;
	error = -EPROTONOSUPPORT;
	if (sk_udp->sk_protocol != IPPROTO_UDP)
		goto out;
	error = -EDESTADDRREQ;
	if (sk_udp->sk_state != TCP_ESTABLISHED)
		goto out;
	error = -EBUSY;
	if (udp_sk(sk_udp)->encap_type || sk_udp->sk_user_data)
		goto out;
	if (!sk_udp->sk_bound_dev_if) {
		struct dst_entry *dst = sk_dst_get(sk_udp);
		error = -ENODEV;
		if (!dst)
			goto out;
		sk_udp->sk_bound_dev_if = dst->dev->ifindex;
		dst_release(dst);
	}

	po->chan.hdrlen = 12;
	po->chan.private = sk_udp;
	po->chan.ops = &pppolac_channel_ops;
	po->chan.mtu = PPP_MTU - 80;
	po->proto.lac.local = unaligned(&addr->local)->u32;
	po->proto.lac.remote = unaligned(&addr->remote)->u32;
	po->proto.lac.backlog_rcv = sk_udp->sk_backlog_rcv;

	error = ppp_register_channel(&po->chan);
	if (error)
		goto out;

	sk->sk_state = PPPOX_CONNECTED;
	udp_sk(sk_udp)->encap_type = UDP_ENCAP_L2TPINUDP;
	udp_sk(sk_udp)->encap_rcv = pppolac_recv;
	sk_udp->sk_backlog_rcv = pppolac_recv_core;
	sk_udp->sk_user_data = sk;
out:
	if (sock_udp) {
		release_sock(sk_udp);
		if (error)
			sockfd_put(sock_udp);
	}
	release_sock(sk);
	return error;
}

static int pppolac_release(struct socket *sock)
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
		struct sock *sk_udp = (struct sock *)pppox_sk(sk)->chan.private;
		lock_sock(sk_udp);
		pppox_unbind_sock(sk);
		udp_sk(sk_udp)->encap_type = 0;
		udp_sk(sk_udp)->encap_rcv = NULL;
		sk_udp->sk_backlog_rcv = pppox_sk(sk)->proto.lac.backlog_rcv;
		sk_udp->sk_user_data = NULL;
		release_sock(sk_udp);
		sockfd_put(sk_udp->sk_socket);
	}

	sock_orphan(sk);
	sock->sk = NULL;
	release_sock(sk);
	sock_put(sk);
	return 0;
}

/******************************************************************************/

static struct proto pppolac_proto = {
	.name = "PPPOLAC",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

static struct proto_ops pppolac_proto_ops = {
	.family = PF_PPPOX,
	.owner = THIS_MODULE,
	.release = pppolac_release,
	.bind = sock_no_bind,
	.connect = pppolac_connect,
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

static int pppolac_create(struct net *net, struct socket *sock)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_PPPOX, GFP_KERNEL, &pppolac_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->state = SS_UNCONNECTED;
	sock->ops = &pppolac_proto_ops;
	sk->sk_protocol = PX_PROTO_OLAC;
	sk->sk_state = PPPOX_NONE;
	return 0;
}

/******************************************************************************/

static struct pppox_proto pppolac_pppox_proto = {
	.create = pppolac_create,
	.owner = THIS_MODULE,
};

static int __init pppolac_init(void)
{
	int error;

	error = proto_register(&pppolac_proto, 0);
	if (error)
		return error;

	error = register_pppox_proto(PX_PROTO_OLAC, &pppolac_pppox_proto);
	if (error)
		proto_unregister(&pppolac_proto);
	else
		skb_queue_head_init(&delivery_queue);
	return error;
}

static void __exit pppolac_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OLAC);
	proto_unregister(&pppolac_proto);
}

module_init(pppolac_init);
module_exit(pppolac_exit);

MODULE_DESCRIPTION("PPP on L2TP Access Concentrator (PPPoLAC)");
MODULE_AUTHOR("Chia-chi Yeh <chiachi@android.com>");
MODULE_LICENSE("GPL");
