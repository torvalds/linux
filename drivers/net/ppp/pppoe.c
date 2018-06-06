/** -*- linux-c -*- ***********************************************************
 * Linux PPP over Ethernet (PPPoX/PPPoE) Sockets
 *
 * PPPoX --- Generic PPP encapsulation socket family
 * PPPoE --- PPP over Ethernet (RFC 2516)
 *
 *
 * Version:	0.7.0
 *
 * 070228 :	Fix to allow multiple sessions with same remote MAC and same
 *		session id by including the local device ifindex in the
 *		tuple identifying a session. This also ensures packets can't
 *		be injected into a session from interfaces other than the one
 *		specified by userspace. Florian Zumbiehl <florz@florz.de>
 *		(Oh, BTW, this one is YYMMDD, in case you were wondering ...)
 * 220102 :	Fix module use count on failure in pppoe_create, pppox_sk -acme
 * 030700 :	Fixed connect logic to allow for disconnect.
 * 270700 :	Fixed potential SMP problems; we must protect against
 *		simultaneous invocation of ppp_input
 *		and ppp_unregister_channel.
 * 040800 :	Respect reference count mechanisms on net-devices.
 * 200800 :	fix kfree(skb) in pppoe_rcv (acme)
 *		Module reference count is decremented in the right spot now,
 *		guards against sock_put not actually freeing the sk
 *		in pppoe_release.
 * 051000 :	Initialization cleanup.
 * 111100 :	Fix recvmsg.
 * 050101 :	Fix PADT procesing.
 * 140501 :	Use pppoe_rcv_core to handle all backlog. (Alexey)
 * 170701 :	Do not lock_sock with rwlock held. (DaveM)
 *		Ignore discovery frames if user has socket
 *		locked. (DaveM)
 *		Ignore return value of dev_queue_xmit in __pppoe_xmit
 *		or else we may kfree an SKB twice. (DaveM)
 * 190701 :	When doing copies of skb's in __pppoe_xmit, always delete
 *		the original skb that was passed in on success, never on
 *		failure.  Delete the copy of the skb on failure to avoid
 *		a memory leak.
 * 081001 :	Misc. cleanup (licence string, non-blocking, prevent
 *		reference of device on close).
 * 121301 :	New ppp channels interface; cannot unregister a channel
 *		from interrupts.  Thus, we mark the socket as a ZOMBIE
 *		and do the unregistration later.
 * 081002 :	seq_file support for proc stuff -acme
 * 111602 :	Merge all 2.4 fixes into 2.5/2.6 tree.  Label 2.5/2.6
 *		as version 0.7.  Spacing cleanup.
 * Author:	Michal Ostrowski <mostrows@speakeasy.net>
 * Contributors:
 * 		Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *		David S. Miller (davem@redhat.com)
 *
 * License:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/if_pppox.h>
#include <linux/ppp_channel.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>
#include <linux/notifier.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/sock.h>

#include <linux/uaccess.h>

#define PPPOE_HASH_BITS 4
#define PPPOE_HASH_SIZE (1 << PPPOE_HASH_BITS)
#define PPPOE_HASH_MASK	(PPPOE_HASH_SIZE - 1)

static int __pppoe_xmit(struct sock *sk, struct sk_buff *skb);

static const struct proto_ops pppoe_ops;
static const struct ppp_channel_ops pppoe_chan_ops;

/* per-net private data for this module */
static unsigned int pppoe_net_id __read_mostly;
struct pppoe_net {
	/*
	 * we could use _single_ hash table for all
	 * nets by injecting net id into the hash but
	 * it would increase hash chains and add
	 * a few additional math comparations messy
	 * as well, moreover in case of SMP less locking
	 * controversy here
	 */
	struct pppox_sock *hash_table[PPPOE_HASH_SIZE];
	rwlock_t hash_lock;
};

/*
 * PPPoE could be in the following stages:
 * 1) Discovery stage (to obtain remote MAC and Session ID)
 * 2) Session stage (MAC and SID are known)
 *
 * Ethernet frames have a special tag for this but
 * we use simpler approach based on session id
 */
static inline bool stage_session(__be16 sid)
{
	return sid != 0;
}

static inline struct pppoe_net *pppoe_pernet(struct net *net)
{
	BUG_ON(!net);

	return net_generic(net, pppoe_net_id);
}

static inline int cmp_2_addr(struct pppoe_addr *a, struct pppoe_addr *b)
{
	return a->sid == b->sid && ether_addr_equal(a->remote, b->remote);
}

static inline int cmp_addr(struct pppoe_addr *a, __be16 sid, char *addr)
{
	return a->sid == sid && ether_addr_equal(a->remote, addr);
}

#if 8 % PPPOE_HASH_BITS
#error 8 must be a multiple of PPPOE_HASH_BITS
#endif

static int hash_item(__be16 sid, unsigned char *addr)
{
	unsigned char hash = 0;
	unsigned int i;

	for (i = 0; i < ETH_ALEN; i++)
		hash ^= addr[i];
	for (i = 0; i < sizeof(sid_t) * 8; i += 8)
		hash ^= (__force __u32)sid >> i;
	for (i = 8; (i >>= 1) >= PPPOE_HASH_BITS;)
		hash ^= hash >> i;

	return hash & PPPOE_HASH_MASK;
}

/**********************************************************************
 *
 *  Set/get/delete/rehash items  (internal versions)
 *
 **********************************************************************/
static struct pppox_sock *__get_item(struct pppoe_net *pn, __be16 sid,
				unsigned char *addr, int ifindex)
{
	int hash = hash_item(sid, addr);
	struct pppox_sock *ret;

	ret = pn->hash_table[hash];
	while (ret) {
		if (cmp_addr(&ret->pppoe_pa, sid, addr) &&
		    ret->pppoe_ifindex == ifindex)
			return ret;

		ret = ret->next;
	}

	return NULL;
}

static int __set_item(struct pppoe_net *pn, struct pppox_sock *po)
{
	int hash = hash_item(po->pppoe_pa.sid, po->pppoe_pa.remote);
	struct pppox_sock *ret;

	ret = pn->hash_table[hash];
	while (ret) {
		if (cmp_2_addr(&ret->pppoe_pa, &po->pppoe_pa) &&
		    ret->pppoe_ifindex == po->pppoe_ifindex)
			return -EALREADY;

		ret = ret->next;
	}

	po->next = pn->hash_table[hash];
	pn->hash_table[hash] = po;

	return 0;
}

static void __delete_item(struct pppoe_net *pn, __be16 sid,
					char *addr, int ifindex)
{
	int hash = hash_item(sid, addr);
	struct pppox_sock *ret, **src;

	ret = pn->hash_table[hash];
	src = &pn->hash_table[hash];

	while (ret) {
		if (cmp_addr(&ret->pppoe_pa, sid, addr) &&
		    ret->pppoe_ifindex == ifindex) {
			*src = ret->next;
			break;
		}

		src = &ret->next;
		ret = ret->next;
	}
}

/**********************************************************************
 *
 *  Set/get/delete/rehash items
 *
 **********************************************************************/
static inline struct pppox_sock *get_item(struct pppoe_net *pn, __be16 sid,
					unsigned char *addr, int ifindex)
{
	struct pppox_sock *po;

	read_lock_bh(&pn->hash_lock);
	po = __get_item(pn, sid, addr, ifindex);
	if (po)
		sock_hold(sk_pppox(po));
	read_unlock_bh(&pn->hash_lock);

	return po;
}

static inline struct pppox_sock *get_item_by_addr(struct net *net,
						struct sockaddr_pppox *sp)
{
	struct net_device *dev;
	struct pppoe_net *pn;
	struct pppox_sock *pppox_sock = NULL;

	int ifindex;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(net, sp->sa_addr.pppoe.dev);
	if (dev) {
		ifindex = dev->ifindex;
		pn = pppoe_pernet(net);
		pppox_sock = get_item(pn, sp->sa_addr.pppoe.sid,
				sp->sa_addr.pppoe.remote, ifindex);
	}
	rcu_read_unlock();
	return pppox_sock;
}

static inline void delete_item(struct pppoe_net *pn, __be16 sid,
					char *addr, int ifindex)
{
	write_lock_bh(&pn->hash_lock);
	__delete_item(pn, sid, addr, ifindex);
	write_unlock_bh(&pn->hash_lock);
}

/***************************************************************************
 *
 *  Handler for device events.
 *  Certain device events require that sockets be unconnected.
 *
 **************************************************************************/

static void pppoe_flush_dev(struct net_device *dev)
{
	struct pppoe_net *pn;
	int i;

	pn = pppoe_pernet(dev_net(dev));
	write_lock_bh(&pn->hash_lock);
	for (i = 0; i < PPPOE_HASH_SIZE; i++) {
		struct pppox_sock *po = pn->hash_table[i];
		struct sock *sk;

		while (po) {
			while (po && po->pppoe_dev != dev) {
				po = po->next;
			}

			if (!po)
				break;

			sk = sk_pppox(po);

			/* We always grab the socket lock, followed by the
			 * hash_lock, in that order.  Since we should hold the
			 * sock lock while doing any unbinding, we need to
			 * release the lock we're holding.  Hold a reference to
			 * the sock so it doesn't disappear as we're jumping
			 * between locks.
			 */

			sock_hold(sk);
			write_unlock_bh(&pn->hash_lock);
			lock_sock(sk);

			if (po->pppoe_dev == dev &&
			    sk->sk_state & (PPPOX_CONNECTED | PPPOX_BOUND)) {
				pppox_unbind_sock(sk);
				sk->sk_state_change(sk);
				po->pppoe_dev = NULL;
				dev_put(dev);
			}

			release_sock(sk);
			sock_put(sk);

			/* Restart the process from the start of the current
			 * hash chain. We dropped locks so the world may have
			 * change from underneath us.
			 */

			BUG_ON(pppoe_pernet(dev_net(dev)) == NULL);
			write_lock_bh(&pn->hash_lock);
			po = pn->hash_table[i];
		}
	}
	write_unlock_bh(&pn->hash_lock);
}

static int pppoe_device_event(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	/* Only look at sockets that are using this specific device. */
	switch (event) {
	case NETDEV_CHANGEADDR:
	case NETDEV_CHANGEMTU:
		/* A change in mtu or address is a bad thing, requiring
		 * LCP re-negotiation.
		 */

	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		/* Find every socket on this device and kill it. */
		pppoe_flush_dev(dev);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block pppoe_notifier = {
	.notifier_call = pppoe_device_event,
};

/************************************************************************
 *
 * Do the real work of receiving a PPPoE Session frame.
 *
 ***********************************************************************/
static int pppoe_rcv_core(struct sock *sk, struct sk_buff *skb)
{
	struct pppox_sock *po = pppox_sk(sk);
	struct pppox_sock *relay_po;

	/* Backlog receive. Semantics of backlog rcv preclude any code from
	 * executing in lock_sock()/release_sock() bounds; meaning sk->sk_state
	 * can't change.
	 */

	if (skb->pkt_type == PACKET_OTHERHOST)
		goto abort_kfree;

	if (sk->sk_state & PPPOX_BOUND) {
		ppp_input(&po->chan, skb);
	} else if (sk->sk_state & PPPOX_RELAY) {
		relay_po = get_item_by_addr(sock_net(sk),
					    &po->pppoe_relay);
		if (relay_po == NULL)
			goto abort_kfree;

		if ((sk_pppox(relay_po)->sk_state & PPPOX_CONNECTED) == 0)
			goto abort_put;

		if (!__pppoe_xmit(sk_pppox(relay_po), skb))
			goto abort_put;

		sock_put(sk_pppox(relay_po));
	} else {
		if (sock_queue_rcv_skb(sk, skb))
			goto abort_kfree;
	}

	return NET_RX_SUCCESS;

abort_put:
	sock_put(sk_pppox(relay_po));

abort_kfree:
	kfree_skb(skb);
	return NET_RX_DROP;
}

/************************************************************************
 *
 * Receive wrapper called in BH context.
 *
 ***********************************************************************/
static int pppoe_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt, struct net_device *orig_dev)
{
	struct pppoe_hdr *ph;
	struct pppox_sock *po;
	struct pppoe_net *pn;
	int len;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (!pskb_may_pull(skb, sizeof(struct pppoe_hdr)))
		goto drop;

	ph = pppoe_hdr(skb);
	len = ntohs(ph->length);

	skb_pull_rcsum(skb, sizeof(*ph));
	if (skb->len < len)
		goto drop;

	if (pskb_trim_rcsum(skb, len))
		goto drop;

	pn = pppoe_pernet(dev_net(dev));

	/* Note that get_item does a sock_hold(), so sk_pppox(po)
	 * is known to be safe.
	 */
	po = get_item(pn, ph->sid, eth_hdr(skb)->h_source, dev->ifindex);
	if (!po)
		goto drop;

	return sk_receive_skb(sk_pppox(po), skb, 0);

drop:
	kfree_skb(skb);
out:
	return NET_RX_DROP;
}

static void pppoe_unbind_sock_work(struct work_struct *work)
{
	struct pppox_sock *po = container_of(work, struct pppox_sock,
					     proto.pppoe.padt_work);
	struct sock *sk = sk_pppox(po);

	lock_sock(sk);
	if (po->pppoe_dev) {
		dev_put(po->pppoe_dev);
		po->pppoe_dev = NULL;
	}
	pppox_unbind_sock(sk);
	release_sock(sk);
	sock_put(sk);
}

/************************************************************************
 *
 * Receive a PPPoE Discovery frame.
 * This is solely for detection of PADT frames
 *
 ***********************************************************************/
static int pppoe_disc_rcv(struct sk_buff *skb, struct net_device *dev,
			  struct packet_type *pt, struct net_device *orig_dev)

{
	struct pppoe_hdr *ph;
	struct pppox_sock *po;
	struct pppoe_net *pn;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (!pskb_may_pull(skb, sizeof(struct pppoe_hdr)))
		goto abort;

	ph = pppoe_hdr(skb);
	if (ph->code != PADT_CODE)
		goto abort;

	pn = pppoe_pernet(dev_net(dev));
	po = get_item(pn, ph->sid, eth_hdr(skb)->h_source, dev->ifindex);
	if (po)
		if (!schedule_work(&po->proto.pppoe.padt_work))
			sock_put(sk_pppox(po));

abort:
	kfree_skb(skb);
out:
	return NET_RX_SUCCESS; /* Lies... :-) */
}

static struct packet_type pppoes_ptype __read_mostly = {
	.type	= cpu_to_be16(ETH_P_PPP_SES),
	.func	= pppoe_rcv,
};

static struct packet_type pppoed_ptype __read_mostly = {
	.type	= cpu_to_be16(ETH_P_PPP_DISC),
	.func	= pppoe_disc_rcv,
};

static struct proto pppoe_sk_proto __read_mostly = {
	.name	  = "PPPOE",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

/***********************************************************************
 *
 * Initialize a new struct sock.
 *
 **********************************************************************/
static int pppoe_create(struct net *net, struct socket *sock, int kern)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_PPPOX, GFP_KERNEL, &pppoe_sk_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->state	= SS_UNCONNECTED;
	sock->ops	= &pppoe_ops;

	sk->sk_backlog_rcv	= pppoe_rcv_core;
	sk->sk_state		= PPPOX_NONE;
	sk->sk_type		= SOCK_STREAM;
	sk->sk_family		= PF_PPPOX;
	sk->sk_protocol		= PX_PROTO_OE;

	INIT_WORK(&pppox_sk(sk)->proto.pppoe.padt_work,
		  pppoe_unbind_sock_work);

	return 0;
}

static int pppoe_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po;
	struct pppoe_net *pn;
	struct net *net = NULL;

	if (!sk)
		return 0;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_DEAD)) {
		release_sock(sk);
		return -EBADF;
	}

	po = pppox_sk(sk);

	if (po->pppoe_dev) {
		dev_put(po->pppoe_dev);
		po->pppoe_dev = NULL;
	}

	pppox_unbind_sock(sk);

	/* Signal the death of the socket. */
	sk->sk_state = PPPOX_DEAD;

	net = sock_net(sk);
	pn = pppoe_pernet(net);

	/*
	 * protect "po" from concurrent updates
	 * on pppoe_flush_dev
	 */
	delete_item(pn, po->pppoe_pa.sid, po->pppoe_pa.remote,
		    po->pppoe_ifindex);

	sock_orphan(sk);
	sock->sk = NULL;

	skb_queue_purge(&sk->sk_receive_queue);
	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int pppoe_connect(struct socket *sock, struct sockaddr *uservaddr,
		  int sockaddr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_pppox *sp = (struct sockaddr_pppox *)uservaddr;
	struct pppox_sock *po = pppox_sk(sk);
	struct net_device *dev = NULL;
	struct pppoe_net *pn;
	struct net *net = NULL;
	int error;

	lock_sock(sk);

	error = -EINVAL;

	if (sockaddr_len != sizeof(struct sockaddr_pppox))
		goto end;

	if (sp->sa_protocol != PX_PROTO_OE)
		goto end;

	/* Check for already bound sockets */
	error = -EBUSY;
	if ((sk->sk_state & PPPOX_CONNECTED) &&
	     stage_session(sp->sa_addr.pppoe.sid))
		goto end;

	/* Check for already disconnected sockets, on attempts to disconnect */
	error = -EALREADY;
	if ((sk->sk_state & PPPOX_DEAD) &&
	     !stage_session(sp->sa_addr.pppoe.sid))
		goto end;

	error = 0;

	/* Delete the old binding */
	if (stage_session(po->pppoe_pa.sid)) {
		pppox_unbind_sock(sk);
		pn = pppoe_pernet(sock_net(sk));
		delete_item(pn, po->pppoe_pa.sid,
			    po->pppoe_pa.remote, po->pppoe_ifindex);
		if (po->pppoe_dev) {
			dev_put(po->pppoe_dev);
			po->pppoe_dev = NULL;
		}

		po->pppoe_ifindex = 0;
		memset(&po->pppoe_pa, 0, sizeof(po->pppoe_pa));
		memset(&po->pppoe_relay, 0, sizeof(po->pppoe_relay));
		memset(&po->chan, 0, sizeof(po->chan));
		po->next = NULL;
		po->num = 0;

		sk->sk_state = PPPOX_NONE;
	}

	/* Re-bind in session stage only */
	if (stage_session(sp->sa_addr.pppoe.sid)) {
		error = -ENODEV;
		net = sock_net(sk);
		dev = dev_get_by_name(net, sp->sa_addr.pppoe.dev);
		if (!dev)
			goto err_put;

		po->pppoe_dev = dev;
		po->pppoe_ifindex = dev->ifindex;
		pn = pppoe_pernet(net);
		if (!(dev->flags & IFF_UP)) {
			goto err_put;
		}

		memcpy(&po->pppoe_pa,
		       &sp->sa_addr.pppoe,
		       sizeof(struct pppoe_addr));

		write_lock_bh(&pn->hash_lock);
		error = __set_item(pn, po);
		write_unlock_bh(&pn->hash_lock);
		if (error < 0)
			goto err_put;

		po->chan.hdrlen = (sizeof(struct pppoe_hdr) +
				   dev->hard_header_len);

		po->chan.mtu = dev->mtu - sizeof(struct pppoe_hdr) - 2;
		po->chan.private = sk;
		po->chan.ops = &pppoe_chan_ops;

		error = ppp_register_net_channel(dev_net(dev), &po->chan);
		if (error) {
			delete_item(pn, po->pppoe_pa.sid,
				    po->pppoe_pa.remote, po->pppoe_ifindex);
			goto err_put;
		}

		sk->sk_state = PPPOX_CONNECTED;
	}

	po->num = sp->sa_addr.pppoe.sid;

end:
	release_sock(sk);
	return error;
err_put:
	if (po->pppoe_dev) {
		dev_put(po->pppoe_dev);
		po->pppoe_dev = NULL;
	}
	goto end;
}

static int pppoe_getname(struct socket *sock, struct sockaddr *uaddr,
		  int peer)
{
	int len = sizeof(struct sockaddr_pppox);
	struct sockaddr_pppox sp;

	sp.sa_family	= AF_PPPOX;
	sp.sa_protocol	= PX_PROTO_OE;
	memcpy(&sp.sa_addr.pppoe, &pppox_sk(sock->sk)->pppoe_pa,
	       sizeof(struct pppoe_addr));

	memcpy(uaddr, &sp, len);

	return len;
}

static int pppoe_ioctl(struct socket *sock, unsigned int cmd,
		unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	int val;
	int err;

	switch (cmd) {
	case PPPIOCGMRU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (put_user(po->pppoe_dev->mtu -
			     sizeof(struct pppoe_hdr) -
			     PPP_HDRLEN,
			     (int __user *)arg))
			break;
		err = 0;
		break;

	case PPPIOCSMRU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (get_user(val, (int __user *)arg))
			break;

		if (val < (po->pppoe_dev->mtu
			   - sizeof(struct pppoe_hdr)
			   - PPP_HDRLEN))
			err = 0;
		else
			err = -EINVAL;
		break;

	case PPPIOCSFLAGS:
		err = -EFAULT;
		if (get_user(val, (int __user *)arg))
			break;
		err = 0;
		break;

	case PPPOEIOCSFWD:
	{
		struct pppox_sock *relay_po;

		err = -EBUSY;
		if (sk->sk_state & (PPPOX_BOUND | PPPOX_DEAD))
			break;

		err = -ENOTCONN;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		/* PPPoE address from the user specifies an outbound
		   PPPoE address which frames are forwarded to */
		err = -EFAULT;
		if (copy_from_user(&po->pppoe_relay,
				   (void __user *)arg,
				   sizeof(struct sockaddr_pppox)))
			break;

		err = -EINVAL;
		if (po->pppoe_relay.sa_family != AF_PPPOX ||
		    po->pppoe_relay.sa_protocol != PX_PROTO_OE)
			break;

		/* Check that the socket referenced by the address
		   actually exists. */
		relay_po = get_item_by_addr(sock_net(sk), &po->pppoe_relay);
		if (!relay_po)
			break;

		sock_put(sk_pppox(relay_po));
		sk->sk_state |= PPPOX_RELAY;
		err = 0;
		break;
	}

	case PPPOEIOCDFWD:
		err = -EALREADY;
		if (!(sk->sk_state & PPPOX_RELAY))
			break;

		sk->sk_state &= ~PPPOX_RELAY;
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

static int pppoe_sendmsg(struct socket *sock, struct msghdr *m,
			 size_t total_len)
{
	struct sk_buff *skb;
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	int error;
	struct pppoe_hdr hdr;
	struct pppoe_hdr *ph;
	struct net_device *dev;
	char *start;
	int hlen;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED)) {
		error = -ENOTCONN;
		goto end;
	}

	hdr.ver = 1;
	hdr.type = 1;
	hdr.code = 0;
	hdr.sid = po->num;

	dev = po->pppoe_dev;

	error = -EMSGSIZE;
	if (total_len > (dev->mtu + dev->hard_header_len))
		goto end;

	hlen = LL_RESERVED_SPACE(dev);
	skb = sock_wmalloc(sk, hlen + sizeof(*ph) + total_len +
			   dev->needed_tailroom, 0, GFP_KERNEL);
	if (!skb) {
		error = -ENOMEM;
		goto end;
	}

	/* Reserve space for headers. */
	skb_reserve(skb, hlen);
	skb_reset_network_header(skb);

	skb->dev = dev;

	skb->priority = sk->sk_priority;
	skb->protocol = cpu_to_be16(ETH_P_PPP_SES);

	ph = skb_put(skb, total_len + sizeof(struct pppoe_hdr));
	start = (char *)&ph->tag[0];

	error = memcpy_from_msg(start, m, total_len);
	if (error < 0) {
		kfree_skb(skb);
		goto end;
	}

	error = total_len;
	dev_hard_header(skb, dev, ETH_P_PPP_SES,
			po->pppoe_pa.remote, NULL, total_len);

	memcpy(ph, &hdr, sizeof(struct pppoe_hdr));

	ph->length = htons(total_len);

	dev_queue_xmit(skb);

end:
	release_sock(sk);
	return error;
}

/************************************************************************
 *
 * xmit function for internal use.
 *
 ***********************************************************************/
static int __pppoe_xmit(struct sock *sk, struct sk_buff *skb)
{
	struct pppox_sock *po = pppox_sk(sk);
	struct net_device *dev = po->pppoe_dev;
	struct pppoe_hdr *ph;
	int data_len = skb->len;

	/* The higher-level PPP code (ppp_unregister_channel()) ensures the PPP
	 * xmit operations conclude prior to an unregistration call.  Thus
	 * sk->sk_state cannot change, so we don't need to do lock_sock().
	 * But, we also can't do a lock_sock since that introduces a potential
	 * deadlock as we'd reverse the lock ordering used when calling
	 * ppp_unregister_channel().
	 */

	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED))
		goto abort;

	if (!dev)
		goto abort;

	/* Copy the data if there is no space for the header or if it's
	 * read-only.
	 */
	if (skb_cow_head(skb, LL_RESERVED_SPACE(dev) + sizeof(*ph)))
		goto abort;

	__skb_push(skb, sizeof(*ph));
	skb_reset_network_header(skb);

	ph = pppoe_hdr(skb);
	ph->ver	= 1;
	ph->type = 1;
	ph->code = 0;
	ph->sid	= po->num;
	ph->length = htons(data_len);

	skb->protocol = cpu_to_be16(ETH_P_PPP_SES);
	skb->dev = dev;

	dev_hard_header(skb, dev, ETH_P_PPP_SES,
			po->pppoe_pa.remote, NULL, data_len);

	dev_queue_xmit(skb);
	return 1;

abort:
	kfree_skb(skb);
	return 1;
}

/************************************************************************
 *
 * xmit function called by generic PPP driver
 * sends PPP frame over PPPoE socket
 *
 ***********************************************************************/
static int pppoe_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *)chan->private;
	return __pppoe_xmit(sk, skb);
}

static const struct ppp_channel_ops pppoe_chan_ops = {
	.start_xmit = pppoe_xmit,
};

static int pppoe_recvmsg(struct socket *sock, struct msghdr *m,
			 size_t total_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int error = 0;

	if (sk->sk_state & PPPOX_BOUND) {
		error = -EIO;
		goto end;
	}

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &error);
	if (error < 0)
		goto end;

	if (skb) {
		total_len = min_t(size_t, total_len, skb->len);
		error = skb_copy_datagram_msg(skb, 0, m, total_len);
		if (error == 0) {
			consume_skb(skb);
			return total_len;
		}
	}

	kfree_skb(skb);
end:
	return error;
}

#ifdef CONFIG_PROC_FS
static int pppoe_seq_show(struct seq_file *seq, void *v)
{
	struct pppox_sock *po;
	char *dev_name;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Id       Address              Device\n");
		goto out;
	}

	po = v;
	dev_name = po->pppoe_pa.dev;

	seq_printf(seq, "%08X %pM %8s\n",
		po->pppoe_pa.sid, po->pppoe_pa.remote, dev_name);
out:
	return 0;
}

static inline struct pppox_sock *pppoe_get_idx(struct pppoe_net *pn, loff_t pos)
{
	struct pppox_sock *po;
	int i;

	for (i = 0; i < PPPOE_HASH_SIZE; i++) {
		po = pn->hash_table[i];
		while (po) {
			if (!pos--)
				goto out;
			po = po->next;
		}
	}

out:
	return po;
}

static void *pppoe_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(pn->hash_lock)
{
	struct pppoe_net *pn = pppoe_pernet(seq_file_net(seq));
	loff_t l = *pos;

	read_lock_bh(&pn->hash_lock);
	return l ? pppoe_get_idx(pn, --l) : SEQ_START_TOKEN;
}

static void *pppoe_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct pppoe_net *pn = pppoe_pernet(seq_file_net(seq));
	struct pppox_sock *po;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		po = pppoe_get_idx(pn, 0);
		goto out;
	}
	po = v;
	if (po->next)
		po = po->next;
	else {
		int hash = hash_item(po->pppoe_pa.sid, po->pppoe_pa.remote);

		po = NULL;
		while (++hash < PPPOE_HASH_SIZE) {
			po = pn->hash_table[hash];
			if (po)
				break;
		}
	}

out:
	return po;
}

static void pppoe_seq_stop(struct seq_file *seq, void *v)
	__releases(pn->hash_lock)
{
	struct pppoe_net *pn = pppoe_pernet(seq_file_net(seq));
	read_unlock_bh(&pn->hash_lock);
}

static const struct seq_operations pppoe_seq_ops = {
	.start		= pppoe_seq_start,
	.next		= pppoe_seq_next,
	.stop		= pppoe_seq_stop,
	.show		= pppoe_seq_show,
};

static int pppoe_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &pppoe_seq_ops,
			sizeof(struct seq_net_private));
}

static const struct file_operations pppoe_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= pppoe_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

#endif /* CONFIG_PROC_FS */

static const struct proto_ops pppoe_ops = {
	.family		= AF_PPPOX,
	.owner		= THIS_MODULE,
	.release	= pppoe_release,
	.bind		= sock_no_bind,
	.connect	= pppoe_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= pppoe_getname,
	.poll		= datagram_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.sendmsg	= pppoe_sendmsg,
	.recvmsg	= pppoe_recvmsg,
	.mmap		= sock_no_mmap,
	.ioctl		= pppox_ioctl,
};

static const struct pppox_proto pppoe_proto = {
	.create	= pppoe_create,
	.ioctl	= pppoe_ioctl,
	.owner	= THIS_MODULE,
};

static __net_init int pppoe_init_net(struct net *net)
{
	struct pppoe_net *pn = pppoe_pernet(net);
	struct proc_dir_entry *pde;

	rwlock_init(&pn->hash_lock);

	pde = proc_create("pppoe", 0444, net->proc_net, &pppoe_seq_fops);
#ifdef CONFIG_PROC_FS
	if (!pde)
		return -ENOMEM;
#endif

	return 0;
}

static __net_exit void pppoe_exit_net(struct net *net)
{
	remove_proc_entry("pppoe", net->proc_net);
}

static struct pernet_operations pppoe_net_ops = {
	.init = pppoe_init_net,
	.exit = pppoe_exit_net,
	.id   = &pppoe_net_id,
	.size = sizeof(struct pppoe_net),
};

static int __init pppoe_init(void)
{
	int err;

	err = register_pernet_device(&pppoe_net_ops);
	if (err)
		goto out;

	err = proto_register(&pppoe_sk_proto, 0);
	if (err)
		goto out_unregister_net_ops;

	err = register_pppox_proto(PX_PROTO_OE, &pppoe_proto);
	if (err)
		goto out_unregister_pppoe_proto;

	dev_add_pack(&pppoes_ptype);
	dev_add_pack(&pppoed_ptype);
	register_netdevice_notifier(&pppoe_notifier);

	return 0;

out_unregister_pppoe_proto:
	proto_unregister(&pppoe_sk_proto);
out_unregister_net_ops:
	unregister_pernet_device(&pppoe_net_ops);
out:
	return err;
}

static void __exit pppoe_exit(void)
{
	unregister_netdevice_notifier(&pppoe_notifier);
	dev_remove_pack(&pppoed_ptype);
	dev_remove_pack(&pppoes_ptype);
	unregister_pppox_proto(PX_PROTO_OE);
	proto_unregister(&pppoe_sk_proto);
	unregister_pernet_device(&pppoe_net_ops);
}

module_init(pppoe_init);
module_exit(pppoe_exit);

MODULE_AUTHOR("Michal Ostrowski <mostrows@speakeasy.net>");
MODULE_DESCRIPTION("PPP over Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_PPPOX, PX_PROTO_OE);
