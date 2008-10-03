/*
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mISDNif.h>
#include "core.h"

static int	*debug;

static struct proto mISDN_proto = {
	.name		= "misdn",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mISDN_sock)
};

#define _pms(sk)	((struct mISDN_sock *)sk)

static struct mISDN_sock_list	data_sockets = {
	.lock = __RW_LOCK_UNLOCKED(data_sockets.lock)
};

static struct mISDN_sock_list	base_sockets = {
	.lock = __RW_LOCK_UNLOCKED(base_sockets.lock)
};

#define L2_HEADER_LEN	4

static inline struct sk_buff *
_l2_alloc_skb(unsigned int len, gfp_t gfp_mask)
{
	struct sk_buff  *skb;

	skb = alloc_skb(len + L2_HEADER_LEN, gfp_mask);
	if (likely(skb))
		skb_reserve(skb, L2_HEADER_LEN);
	return skb;
}

static void
mISDN_sock_link(struct mISDN_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}

static void mISDN_sock_unlink(struct mISDN_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}

static int
mISDN_send(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDN_sock *msk;
	int	err;

	msk = container_of(ch, struct mISDN_sock, ch);
	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s len %d %p\n", __func__, skb->len, skb);
	if (msk->sk.sk_state == MISDN_CLOSED)
		return -EUNATCH;
	__net_timestamp(skb);
	err = sock_queue_rcv_skb(&msk->sk, skb);
	if (err)
		printk(KERN_WARNING "%s: error %d\n", __func__, err);
	return err;
}

static int
mISDN_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDN_sock *msk;

	msk = container_of(ch, struct mISDN_sock, ch);
	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p, %x, %p)\n", __func__, ch, cmd, arg);
	switch (cmd) {
	case CLOSE_CHANNEL:
		msk->sk.sk_state = MISDN_CLOSED;
		break;
	}
	return 0;
}

static inline void
mISDN_sock_cmsg(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	struct timeval	tv;

	if (_pms(sk)->cmask & MISDN_TIME_STAMP) {
		skb_get_timestamp(skb, &tv);
		put_cmsg(msg, SOL_MISDN, MISDN_TIME_STAMP, sizeof(tv), &tv);
	}
}

static int
mISDN_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
    struct msghdr *msg, size_t len, int flags)
{
	struct sk_buff		*skb;
	struct sock		*sk = sock->sk;
	struct sockaddr_mISDN	*maddr;

	int		copied, err;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %d, flags %x ch.nr %d, proto %x\n",
			__func__, (int)len, flags, _pms(sk)->ch.nr,
			sk->sk_protocol);
	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (sk->sk_state == MISDN_CLOSED)
		return 0;

	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	if (msg->msg_namelen >= sizeof(struct sockaddr_mISDN)) {
		msg->msg_namelen = sizeof(struct sockaddr_mISDN);
		maddr = (struct sockaddr_mISDN *)msg->msg_name;
		maddr->family = AF_ISDN;
		maddr->dev = _pms(sk)->dev->id;
		if ((sk->sk_protocol == ISDN_P_LAPD_TE) ||
		    (sk->sk_protocol == ISDN_P_LAPD_NT)) {
			maddr->channel = (mISDN_HEAD_ID(skb) >> 16) & 0xff;
			maddr->tei =  (mISDN_HEAD_ID(skb) >> 8) & 0xff;
			maddr->sapi = mISDN_HEAD_ID(skb) & 0xff;
		} else {
			maddr->channel = _pms(sk)->ch.nr;
			maddr->sapi = _pms(sk)->ch.addr & 0xFF;
			maddr->tei =  (_pms(sk)->ch.addr >> 8) & 0xFF;
		}
	} else {
		if (msg->msg_namelen)
			printk(KERN_WARNING "%s: too small namelen %d\n",
			    __func__, msg->msg_namelen);
		msg->msg_namelen = 0;
	}

	copied = skb->len + MISDN_HEADER_LEN;
	if (len < copied) {
		if (flags & MSG_PEEK)
			atomic_dec(&skb->users);
		else
			skb_queue_head(&sk->sk_receive_queue, skb);
		return -ENOSPC;
	}
	memcpy(skb_push(skb, MISDN_HEADER_LEN), mISDN_HEAD_P(skb),
	    MISDN_HEADER_LEN);

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	mISDN_sock_cmsg(sk, msg, skb);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

static int
mISDN_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
    struct msghdr *msg, size_t len)
{
	struct sock		*sk = sock->sk;
	struct sk_buff		*skb;
	int			err = -ENOMEM;
	struct sockaddr_mISDN	*maddr;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %d flags %x ch %d proto %x\n",
		     __func__, (int)len, msg->msg_flags, _pms(sk)->ch.nr,
		     sk->sk_protocol);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_NOSIGNAL|MSG_ERRQUEUE))
		return -EINVAL;

	if (len < MISDN_HEADER_LEN)
		return -EINVAL;

	if (sk->sk_state != MISDN_BOUND)
		return -EBADFD;

	lock_sock(sk);

	skb = _l2_alloc_skb(len, GFP_KERNEL);
	if (!skb)
		goto done;

	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		goto drop;
	}

	memcpy(mISDN_HEAD_P(skb), skb->data, MISDN_HEADER_LEN);
	skb_pull(skb, MISDN_HEADER_LEN);

	if (msg->msg_namelen >= sizeof(struct sockaddr_mISDN)) {
		/* if we have a address, we use it */
		maddr = (struct sockaddr_mISDN *)msg->msg_name;
		mISDN_HEAD_ID(skb) = maddr->channel;
	} else { /* use default for L2 messages */
		if ((sk->sk_protocol == ISDN_P_LAPD_TE) ||
		    (sk->sk_protocol == ISDN_P_LAPD_NT))
		    mISDN_HEAD_ID(skb) = _pms(sk)->ch.nr;
	}

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: ID:%x\n",
		     __func__, mISDN_HEAD_ID(skb));

	err = -ENODEV;
	if (!_pms(sk)->ch.peer ||
	    (err = _pms(sk)->ch.recv(_pms(sk)->ch.peer, skb)))
		goto drop;

	err = len;

done:
	release_sock(sk);
	return err;

drop:
	kfree_skb(skb);
	goto done;
}

static int
data_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p) sk=%p\n", __func__, sock, sk);
	if (!sk)
		return 0;
	switch (sk->sk_protocol) {
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	case ISDN_P_TE_E1:
	case ISDN_P_NT_E1:
		if (sk->sk_state == MISDN_BOUND)
			delete_channel(&_pms(sk)->ch);
		else
			mISDN_sock_unlink(&data_sockets, sk);
		break;
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
	case ISDN_P_B_X75SLP:
	case ISDN_P_B_L2DTMF:
	case ISDN_P_B_L2DSP:
	case ISDN_P_B_L2DSPHDLC:
		delete_channel(&_pms(sk)->ch);
		mISDN_sock_unlink(&data_sockets, sk);
		break;
	}

	lock_sock(sk);

	sock_orphan(sk);
	skb_queue_purge(&sk->sk_receive_queue);

	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int
data_sock_ioctl_bound(struct sock *sk, unsigned int cmd, void __user *p)
{
	struct mISDN_ctrl_req	cq;
	int			err = -EINVAL, val;
	struct mISDNchannel	*bchan, *next;

	lock_sock(sk);
	if (!_pms(sk)->dev) {
		err = -ENODEV;
		goto done;
	}
	switch (cmd) {
	case IMCTRLREQ:
		if (copy_from_user(&cq, p, sizeof(cq))) {
			err = -EFAULT;
			break;
		}
		if ((sk->sk_protocol & ~ISDN_P_B_MASK) == ISDN_P_B_START) {
			list_for_each_entry_safe(bchan, next,
				&_pms(sk)->dev->bchannels, list) {
				if (bchan->nr == cq.channel) {
					err = bchan->ctrl(bchan,
						CONTROL_CHANNEL, &cq);
					break;
				}
			}
		} else
			err = _pms(sk)->dev->D.ctrl(&_pms(sk)->dev->D,
				CONTROL_CHANNEL, &cq);
		if (err)
			break;
		if (copy_to_user(p, &cq, sizeof(cq)))
			err = -EFAULT;
		break;
	case IMCLEAR_L2:
		if (sk->sk_protocol != ISDN_P_LAPD_NT) {
			err = -EINVAL;
			break;
		}
		if (get_user(val, (int __user *)p)) {
			err = -EFAULT;
			break;
		}
		err = _pms(sk)->dev->teimgr->ctrl(_pms(sk)->dev->teimgr,
		    CONTROL_CHANNEL, &val);
		break;
	default:
		err = -EINVAL;
		break;
	}
done:
	release_sock(sk);
	return err;
}

static int
data_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int 			err = 0, id;
	struct sock		*sk = sock->sk;
	struct mISDNdevice	*dev;
	struct mISDNversion	ver;

	switch (cmd) {
	case IMGETVERSION:
		ver.major = MISDN_MAJOR_VERSION;
		ver.minor = MISDN_MINOR_VERSION;
		ver.release = MISDN_RELEASE;
		if (copy_to_user((void __user *)arg, &ver, sizeof(ver)))
			err = -EFAULT;
		break;
	case IMGETCOUNT:
		id = get_mdevice_count();
		if (put_user(id, (int __user *)arg))
			err = -EFAULT;
		break;
	case IMGETDEVINFO:
		if (get_user(id, (int __user *)arg)) {
			err = -EFAULT;
			break;
		}
		dev = get_mdevice(id);
		if (dev) {
			struct mISDN_devinfo di;

			di.id = dev->id;
			di.Dprotocols = dev->Dprotocols;
			di.Bprotocols = dev->Bprotocols | get_all_Bprotocols();
			di.protocol = dev->D.protocol;
			memcpy(di.channelmap, dev->channelmap,
				sizeof(di.channelmap));
			di.nrbchan = dev->nrbchan;
			strcpy(di.name, dev->name);
			if (copy_to_user((void __user *)arg, &di, sizeof(di)))
				err = -EFAULT;
		} else
			err = -ENODEV;
		break;
	default:
		if (sk->sk_state == MISDN_BOUND)
			err = data_sock_ioctl_bound(sk, cmd,
				(void __user *)arg);
		else
			err = -ENOTCONN;
	}
	return err;
}

static int data_sock_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int len)
{
	struct sock *sk = sock->sk;
	int err = 0, opt = 0;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p, %d, %x, %p, %d)\n", __func__, sock,
		    level, optname, optval, len);

	lock_sock(sk);

	switch (optname) {
	case MISDN_TIME_STAMP:
		if (get_user(opt, (int __user *)optval)) {
			err = -EFAULT;
			break;
		}

		if (opt)
			_pms(sk)->cmask |= MISDN_TIME_STAMP;
		else
			_pms(sk)->cmask &= ~MISDN_TIME_STAMP;
		break;
	default:
		err = -ENOPROTOOPT;
		break;
	}
	release_sock(sk);
	return err;
}

static int data_sock_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int len, opt;

	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
	case MISDN_TIME_STAMP:
		if (_pms(sk)->cmask & MISDN_TIME_STAMP)
			opt = 1;
		else
			opt = 0;

		if (put_user(opt, optval))
			return -EFAULT;
		break;
	default:
		return -ENOPROTOOPT;
	}

	return 0;
}

static int
data_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_mISDN *maddr = (struct sockaddr_mISDN *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p) sk=%p\n", __func__, sock, sk);
	if (addr_len != sizeof(struct sockaddr_mISDN))
		return -EINVAL;
	if (!maddr || maddr->family != AF_ISDN)
		return -EINVAL;

	lock_sock(sk);

	if (_pms(sk)->dev) {
		err = -EALREADY;
		goto done;
	}
	_pms(sk)->dev = get_mdevice(maddr->dev);
	if (!_pms(sk)->dev) {
		err = -ENODEV;
		goto done;
	}
	_pms(sk)->ch.send = mISDN_send;
	_pms(sk)->ch.ctrl = mISDN_ctrl;

	switch (sk->sk_protocol) {
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	case ISDN_P_TE_E1:
	case ISDN_P_NT_E1:
		mISDN_sock_unlink(&data_sockets, sk);
		err = connect_layer1(_pms(sk)->dev, &_pms(sk)->ch,
		    sk->sk_protocol, maddr);
		if (err)
			mISDN_sock_link(&data_sockets, sk);
		break;
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
		err = create_l2entity(_pms(sk)->dev, &_pms(sk)->ch,
		    sk->sk_protocol, maddr);
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
	case ISDN_P_B_X75SLP:
	case ISDN_P_B_L2DTMF:
	case ISDN_P_B_L2DSP:
	case ISDN_P_B_L2DSPHDLC:
		err = connect_Bstack(_pms(sk)->dev, &_pms(sk)->ch,
		    sk->sk_protocol, maddr);
		break;
	default:
		err = -EPROTONOSUPPORT;
	}
	if (err)
		goto done;
	sk->sk_state = MISDN_BOUND;
	_pms(sk)->ch.protocol = sk->sk_protocol;

done:
	release_sock(sk);
	return err;
}

static int
data_sock_getname(struct socket *sock, struct sockaddr *addr,
    int *addr_len, int peer)
{
	struct sockaddr_mISDN 	*maddr = (struct sockaddr_mISDN *) addr;
	struct sock		*sk = sock->sk;

	if (!_pms(sk)->dev)
		return -EBADFD;

	lock_sock(sk);

	*addr_len = sizeof(*maddr);
	maddr->dev = _pms(sk)->dev->id;
	maddr->channel = _pms(sk)->ch.nr;
	maddr->sapi = _pms(sk)->ch.addr & 0xff;
	maddr->tei = (_pms(sk)->ch.addr >> 8) & 0xff;
	release_sock(sk);
	return 0;
}

static const struct proto_ops data_sock_ops = {
	.family		= PF_ISDN,
	.owner		= THIS_MODULE,
	.release	= data_sock_release,
	.ioctl		= data_sock_ioctl,
	.bind		= data_sock_bind,
	.getname	= data_sock_getname,
	.sendmsg	= mISDN_sock_sendmsg,
	.recvmsg	= mISDN_sock_recvmsg,
	.poll		= datagram_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= data_sock_setsockopt,
	.getsockopt	= data_sock_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static int
data_sock_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, PF_ISDN, GFP_KERNEL, &mISDN_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->ops = &data_sock_ops;
	sock->state = SS_UNCONNECTED;
	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state    = MISDN_OPEN;
	mISDN_sock_link(&data_sockets, sk);

	return 0;
}

static int
base_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	printk(KERN_DEBUG "%s(%p) sk=%p\n", __func__, sock, sk);
	if (!sk)
		return 0;

	mISDN_sock_unlink(&base_sockets, sk);
	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int
base_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int 			err = 0, id;
	struct mISDNdevice	*dev;
	struct mISDNversion	ver;

	switch (cmd) {
	case IMGETVERSION:
		ver.major = MISDN_MAJOR_VERSION;
		ver.minor = MISDN_MINOR_VERSION;
		ver.release = MISDN_RELEASE;
		if (copy_to_user((void __user *)arg, &ver, sizeof(ver)))
			err = -EFAULT;
		break;
	case IMGETCOUNT:
		id = get_mdevice_count();
		if (put_user(id, (int __user *)arg))
			err = -EFAULT;
		break;
	case IMGETDEVINFO:
		if (get_user(id, (int __user *)arg)) {
			err = -EFAULT;
			break;
		}
		dev = get_mdevice(id);
		if (dev) {
			struct mISDN_devinfo di;

			di.id = dev->id;
			di.Dprotocols = dev->Dprotocols;
			di.Bprotocols = dev->Bprotocols | get_all_Bprotocols();
			di.protocol = dev->D.protocol;
			memcpy(di.channelmap, dev->channelmap,
				sizeof(di.channelmap));
			di.nrbchan = dev->nrbchan;
			strcpy(di.name, dev->name);
			if (copy_to_user((void __user *)arg, &di, sizeof(di)))
				err = -EFAULT;
		} else
			err = -ENODEV;
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static int
base_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_mISDN *maddr = (struct sockaddr_mISDN *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	if (!maddr || maddr->family != AF_ISDN)
		return -EINVAL;

	lock_sock(sk);

	if (_pms(sk)->dev) {
		err = -EALREADY;
		goto done;
	}

	_pms(sk)->dev = get_mdevice(maddr->dev);
	if (!_pms(sk)->dev) {
		err = -ENODEV;
		goto done;
	}
	sk->sk_state = MISDN_BOUND;

done:
	release_sock(sk);
	return err;
}

static const struct proto_ops base_sock_ops = {
	.family		= PF_ISDN,
	.owner		= THIS_MODULE,
	.release	= base_sock_release,
	.ioctl		= base_sock_ioctl,
	.bind		= base_sock_bind,
	.getname	= sock_no_getname,
	.sendmsg	= sock_no_sendmsg,
	.recvmsg	= sock_no_recvmsg,
	.poll		= sock_no_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};


static int
base_sock_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, PF_ISDN, GFP_KERNEL, &mISDN_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->ops = &base_sock_ops;
	sock->state = SS_UNCONNECTED;
	sock_reset_flag(sk, SOCK_ZAPPED);
	sk->sk_protocol = protocol;
	sk->sk_state    = MISDN_OPEN;
	mISDN_sock_link(&base_sockets, sk);

	return 0;
}

static int
mISDN_sock_create(struct net *net, struct socket *sock, int proto)
{
	int err = -EPROTONOSUPPORT;

	switch	(proto) {
	case ISDN_P_BASE:
		err = base_sock_create(net, sock, proto);
		break;
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	case ISDN_P_TE_E1:
	case ISDN_P_NT_E1:
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
	case ISDN_P_B_X75SLP:
	case ISDN_P_B_L2DTMF:
	case ISDN_P_B_L2DSP:
	case ISDN_P_B_L2DSPHDLC:
		err = data_sock_create(net, sock, proto);
		break;
	default:
		return err;
	}

	return err;
}

static struct
net_proto_family mISDN_sock_family_ops = {
	.owner  = THIS_MODULE,
	.family = PF_ISDN,
	.create = mISDN_sock_create,
};

int
misdn_sock_init(u_int *deb)
{
	int err;

	debug = deb;
	err = sock_register(&mISDN_sock_family_ops);
	if (err)
		printk(KERN_ERR "%s: error(%d)\n", __func__, err);
	return err;
}

void
misdn_sock_cleanup(void)
{
	sock_unregister(PF_ISDN);
}

