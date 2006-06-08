/** -*- linux-c -*- ***********************************************************
 * Linux PPP over Ethernet (PPPoX/PPPoE) Sockets
 *
 * PPPoX --- Generic PPP encapsulation socket family
 * PPPoE --- PPP over Ethernet (RFC 2516)
 *
 *
 * Version:	0.7.0
 *
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
#include <linux/if_ppp.h>
#include <linux/notifier.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <net/sock.h>

#include <asm/uaccess.h>

#define PPPOE_HASH_BITS 4
#define PPPOE_HASH_SIZE (1<<PPPOE_HASH_BITS)

static struct ppp_channel_ops pppoe_chan_ops;

static int pppoe_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
static int pppoe_xmit(struct ppp_channel *chan, struct sk_buff *skb);
static int __pppoe_xmit(struct sock *sk, struct sk_buff *skb);

static const struct proto_ops pppoe_ops;
static DEFINE_RWLOCK(pppoe_hash_lock);

static struct ppp_channel_ops pppoe_chan_ops;

static inline int cmp_2_addr(struct pppoe_addr *a, struct pppoe_addr *b)
{
	return (a->sid == b->sid &&
		(memcmp(a->remote, b->remote, ETH_ALEN) == 0));
}

static inline int cmp_addr(struct pppoe_addr *a, unsigned long sid, char *addr)
{
	return (a->sid == sid &&
		(memcmp(a->remote,addr,ETH_ALEN) == 0));
}

static int hash_item(unsigned long sid, unsigned char *addr)
{
	char hash = 0;
	int i, j;

	for (i = 0; i < ETH_ALEN ; ++i) {
		for (j = 0; j < 8/PPPOE_HASH_BITS ; ++j) {
			hash ^= addr[i] >> ( j * PPPOE_HASH_BITS );
		}
	}

	for (i = 0; i < (sizeof(unsigned long)*8) / PPPOE_HASH_BITS ; ++i)
		hash ^= sid >> (i*PPPOE_HASH_BITS);

	return hash & ( PPPOE_HASH_SIZE - 1 );
}

/* zeroed because its in .bss */
static struct pppox_sock *item_hash_table[PPPOE_HASH_SIZE];

/**********************************************************************
 *
 *  Set/get/delete/rehash items  (internal versions)
 *
 **********************************************************************/
static struct pppox_sock *__get_item(unsigned long sid, unsigned char *addr)
{
	int hash = hash_item(sid, addr);
	struct pppox_sock *ret;

	ret = item_hash_table[hash];

	while (ret && !cmp_addr(&ret->pppoe_pa, sid, addr))
		ret = ret->next;

	return ret;
}

static int __set_item(struct pppox_sock *po)
{
	int hash = hash_item(po->pppoe_pa.sid, po->pppoe_pa.remote);
	struct pppox_sock *ret;

	ret = item_hash_table[hash];
	while (ret) {
		if (cmp_2_addr(&ret->pppoe_pa, &po->pppoe_pa))
			return -EALREADY;

		ret = ret->next;
	}

	if (!ret) {
		po->next = item_hash_table[hash];
		item_hash_table[hash] = po;
	}

	return 0;
}

static struct pppox_sock *__delete_item(unsigned long sid, char *addr)
{
	int hash = hash_item(sid, addr);
	struct pppox_sock *ret, **src;

	ret = item_hash_table[hash];
	src = &item_hash_table[hash];

	while (ret) {
		if (cmp_addr(&ret->pppoe_pa, sid, addr)) {
			*src = ret->next;
			break;
		}

		src = &ret->next;
		ret = ret->next;
	}

	return ret;
}

/**********************************************************************
 *
 *  Set/get/delete/rehash items
 *
 **********************************************************************/
static inline struct pppox_sock *get_item(unsigned long sid,
					 unsigned char *addr)
{
	struct pppox_sock *po;

	read_lock_bh(&pppoe_hash_lock);
	po = __get_item(sid, addr);
	if (po)
		sock_hold(sk_pppox(po));
	read_unlock_bh(&pppoe_hash_lock);

	return po;
}

static inline struct pppox_sock *get_item_by_addr(struct sockaddr_pppox *sp)
{
	return get_item(sp->sa_addr.pppoe.sid, sp->sa_addr.pppoe.remote);
}

static inline int set_item(struct pppox_sock *po)
{
	int i;

	if (!po)
		return -EINVAL;

	write_lock_bh(&pppoe_hash_lock);
	i = __set_item(po);
	write_unlock_bh(&pppoe_hash_lock);

	return i;
}

static inline struct pppox_sock *delete_item(unsigned long sid, char *addr)
{
	struct pppox_sock *ret;

	write_lock_bh(&pppoe_hash_lock);
	ret = __delete_item(sid, addr);
	write_unlock_bh(&pppoe_hash_lock);

	return ret;
}



/***************************************************************************
 *
 *  Handler for device events.
 *  Certain device events require that sockets be unconnected.
 *
 **************************************************************************/

static void pppoe_flush_dev(struct net_device *dev)
{
	int hash;

	BUG_ON(dev == NULL);

	read_lock_bh(&pppoe_hash_lock);
	for (hash = 0; hash < PPPOE_HASH_SIZE; hash++) {
		struct pppox_sock *po = item_hash_table[hash];

		while (po != NULL) {
			if (po->pppoe_dev == dev) {
				struct sock *sk = sk_pppox(po);

				sock_hold(sk);
				po->pppoe_dev = NULL;

				/* We hold a reference to SK, now drop the
				 * hash table lock so that we may attempt
				 * to lock the socket (which can sleep).
				 */
				read_unlock_bh(&pppoe_hash_lock);

				lock_sock(sk);

				if (sk->sk_state &
				    (PPPOX_CONNECTED | PPPOX_BOUND)) {
					pppox_unbind_sock(sk);
					dev_put(dev);
					sk->sk_state = PPPOX_ZOMBIE;
					sk->sk_state_change(sk);
				}

				release_sock(sk);

				sock_put(sk);

				read_lock_bh(&pppoe_hash_lock);

				/* Now restart from the beginning of this
				 * hash chain.  We always NULL out pppoe_dev
				 * so we are guaranteed to make forward
				 * progress.
				 */
				po = item_hash_table[hash];
				continue;
			}
			po = po->next;
		}
	}
	read_unlock_bh(&pppoe_hash_lock);
}

static int pppoe_device_event(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	struct net_device *dev = (struct net_device *) ptr;

	/* Only look at sockets that are using this specific device. */
	switch (event) {
	case NETDEV_CHANGEMTU:
		/* A change in mtu is a bad thing, requiring
		 * LCP re-negotiation.
		 */

	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		/* Find every socket on this device and kill it. */
		pppoe_flush_dev(dev);
		break;

	default:
		break;
	};

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
	struct pppox_sock *relay_po = NULL;

	if (sk->sk_state & PPPOX_BOUND) {
		struct pppoe_hdr *ph = (struct pppoe_hdr *) skb->nh.raw;
		int len = ntohs(ph->length);
		skb_pull_rcsum(skb, sizeof(struct pppoe_hdr));
		if (pskb_trim_rcsum(skb, len))
			goto abort_kfree;

		ppp_input(&po->chan, skb);
	} else if (sk->sk_state & PPPOX_RELAY) {
		relay_po = get_item_by_addr(&po->pppoe_relay);

		if (relay_po == NULL)
			goto abort_kfree;

		if ((sk_pppox(relay_po)->sk_state & PPPOX_CONNECTED) == 0)
			goto abort_put;

		skb_pull(skb, sizeof(struct pppoe_hdr));
		if (!__pppoe_xmit(sk_pppox(relay_po), skb))
			goto abort_put;
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
static int pppoe_rcv(struct sk_buff *skb,
		     struct net_device *dev,
		     struct packet_type *pt,
		     struct net_device *orig_dev)

{
	struct pppoe_hdr *ph;
	struct pppox_sock *po;

	if (!pskb_may_pull(skb, sizeof(struct pppoe_hdr)))
		goto drop;

	if (!(skb = skb_share_check(skb, GFP_ATOMIC))) 
		goto out;

	ph = (struct pppoe_hdr *) skb->nh.raw;

	po = get_item((unsigned long) ph->sid, eth_hdr(skb)->h_source);
	if (po != NULL) 
		return sk_receive_skb(sk_pppox(po), skb);
drop:
	kfree_skb(skb);
out:
	return NET_RX_DROP;
}

/************************************************************************
 *
 * Receive a PPPoE Discovery frame.
 * This is solely for detection of PADT frames
 *
 ***********************************************************************/
static int pppoe_disc_rcv(struct sk_buff *skb,
			  struct net_device *dev,
			  struct packet_type *pt,
			  struct net_device *orig_dev)

{
	struct pppoe_hdr *ph;
	struct pppox_sock *po;

	if (!pskb_may_pull(skb, sizeof(struct pppoe_hdr)))
		goto abort;

	if (!(skb = skb_share_check(skb, GFP_ATOMIC))) 
		goto out;

	ph = (struct pppoe_hdr *) skb->nh.raw;
	if (ph->code != PADT_CODE)
		goto abort;

	po = get_item((unsigned long) ph->sid, eth_hdr(skb)->h_source);
	if (po) {
		struct sock *sk = sk_pppox(po);

		bh_lock_sock(sk);

		/* If the user has locked the socket, just ignore
		 * the packet.  With the way two rcv protocols hook into
		 * one socket family type, we cannot (easily) distinguish
		 * what kind of SKB it is during backlog rcv.
		 */
		if (sock_owned_by_user(sk) == 0) {
			/* We're no longer connect at the PPPOE layer,
			 * and must wait for ppp channel to disconnect us.
			 */
			sk->sk_state = PPPOX_ZOMBIE;
		}

		bh_unlock_sock(sk);
		sock_put(sk);
	}

abort:
	kfree_skb(skb);
out:
	return NET_RX_SUCCESS; /* Lies... :-) */
}

static struct packet_type pppoes_ptype = {
	.type	= __constant_htons(ETH_P_PPP_SES),
	.func	= pppoe_rcv,
};

static struct packet_type pppoed_ptype = {
	.type	= __constant_htons(ETH_P_PPP_DISC),
	.func	= pppoe_disc_rcv,
};

static struct proto pppoe_sk_proto = {
	.name	  = "PPPOE",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

/***********************************************************************
 *
 * Initialize a new struct sock.
 *
 **********************************************************************/
static int pppoe_create(struct socket *sock)
{
	int error = -ENOMEM;
	struct sock *sk;

	sk = sk_alloc(PF_PPPOX, GFP_KERNEL, &pppoe_sk_proto, 1);
	if (!sk)
		goto out;

	sock_init_data(sock, sk);

	sock->state = SS_UNCONNECTED;
	sock->ops   = &pppoe_ops;

	sk->sk_backlog_rcv = pppoe_rcv_core;
	sk->sk_state	   = PPPOX_NONE;
	sk->sk_type	   = SOCK_STREAM;
	sk->sk_family	   = PF_PPPOX;
	sk->sk_protocol	   = PX_PROTO_OE;

	error = 0;
out:	return error;
}

static int pppoe_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po;
	int error = 0;

	if (!sk)
		return 0;

	if (sock_flag(sk, SOCK_DEAD))
		return -EBADF;

	pppox_unbind_sock(sk);

	/* Signal the death of the socket. */
	sk->sk_state = PPPOX_DEAD;

	po = pppox_sk(sk);
	if (po->pppoe_pa.sid) {
		delete_item(po->pppoe_pa.sid, po->pppoe_pa.remote);
	}

	if (po->pppoe_dev)
		dev_put(po->pppoe_dev);

	po->pppoe_dev = NULL;

	sock_orphan(sk);
	sock->sk = NULL;

	skb_queue_purge(&sk->sk_receive_queue);
	sock_put(sk);

	return error;
}


static int pppoe_connect(struct socket *sock, struct sockaddr *uservaddr,
		  int sockaddr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct net_device *dev = NULL;
	struct sockaddr_pppox *sp = (struct sockaddr_pppox *) uservaddr;
	struct pppox_sock *po = pppox_sk(sk);
	int error;

	lock_sock(sk);

	error = -EINVAL;
	if (sp->sa_protocol != PX_PROTO_OE)
		goto end;

	/* Check for already bound sockets */
	error = -EBUSY;
	if ((sk->sk_state & PPPOX_CONNECTED) && sp->sa_addr.pppoe.sid)
		goto end;

	/* Check for already disconnected sockets, on attempts to disconnect */
	error = -EALREADY;
	if ((sk->sk_state & PPPOX_DEAD) && !sp->sa_addr.pppoe.sid )
		goto end;

	error = 0;
	if (po->pppoe_pa.sid) {
		pppox_unbind_sock(sk);

		/* Delete the old binding */
		delete_item(po->pppoe_pa.sid,po->pppoe_pa.remote);

		if(po->pppoe_dev)
			dev_put(po->pppoe_dev);

		memset(sk_pppox(po) + 1, 0,
		       sizeof(struct pppox_sock) - sizeof(struct sock));

		sk->sk_state = PPPOX_NONE;
	}

	/* Don't re-bind if sid==0 */
	if (sp->sa_addr.pppoe.sid != 0) {
		dev = dev_get_by_name(sp->sa_addr.pppoe.dev);

		error = -ENODEV;
		if (!dev)
			goto end;

		po->pppoe_dev = dev;

		if (!(dev->flags & IFF_UP))
			goto err_put;

		memcpy(&po->pppoe_pa,
		       &sp->sa_addr.pppoe,
		       sizeof(struct pppoe_addr));

		error = set_item(po);
		if (error < 0)
			goto err_put;

		po->chan.hdrlen = (sizeof(struct pppoe_hdr) +
				   dev->hard_header_len);

		po->chan.private = sk;
		po->chan.ops = &pppoe_chan_ops;

		error = ppp_register_channel(&po->chan);
		if (error)
			goto err_put;

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
		  int *usockaddr_len, int peer)
{
	int len = sizeof(struct sockaddr_pppox);
	struct sockaddr_pppox sp;

	sp.sa_family	= AF_PPPOX;
	sp.sa_protocol	= PX_PROTO_OE;
	memcpy(&sp.sa_addr.pppoe, &pppox_sk(sock->sk)->pppoe_pa,
	       sizeof(struct pppoe_addr));

	memcpy(uaddr, &sp, len);

	*usockaddr_len = len;

	return 0;
}


static int pppoe_ioctl(struct socket *sock, unsigned int cmd,
		unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	int val = 0;
	int err = 0;

	switch (cmd) {
	case PPPIOCGMRU:
		err = -ENXIO;

		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (put_user(po->pppoe_dev->mtu -
			     sizeof(struct pppoe_hdr) -
			     PPP_HDRLEN,
			     (int __user *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSMRU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (get_user(val,(int __user *) arg))
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
		if (get_user(val, (int __user *) arg))
			break;
		err = 0;
		break;

	case PPPOEIOCSFWD:
	{
		struct pppox_sock *relay_po;

		err = -EBUSY;
		if (sk->sk_state & (PPPOX_BOUND | PPPOX_ZOMBIE | PPPOX_DEAD))
			break;

		err = -ENOTCONN;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		/* PPPoE address from the user specifies an outbound
		   PPPoE address to which frames are forwarded to */
		err = -EFAULT;
		if (copy_from_user(&po->pppoe_relay,
				   (void __user *)arg,
				   sizeof(struct sockaddr_pppox)))
			break;

		err = -EINVAL;
		if (po->pppoe_relay.sa_family != AF_PPPOX ||
		    po->pppoe_relay.sa_protocol!= PX_PROTO_OE)
			break;

		/* Check that the socket referenced by the address
		   actually exists. */
		relay_po = get_item_by_addr(&po->pppoe_relay);

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

	default:;
	};

	return err;
}


static int pppoe_sendmsg(struct kiocb *iocb, struct socket *sock, 
		  struct msghdr *m, size_t total_len)
{
	struct sk_buff *skb = NULL;
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	int error = 0;
	struct pppoe_hdr hdr;
	struct pppoe_hdr *ph;
	struct net_device *dev;
	char *start;

	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED)) {
		error = -ENOTCONN;
		goto end;
	}

	hdr.ver = 1;
	hdr.type = 1;
	hdr.code = 0;
	hdr.sid = po->num;

	lock_sock(sk);

	dev = po->pppoe_dev;

	error = -EMSGSIZE;
 	if (total_len > (dev->mtu + dev->hard_header_len))
		goto end;


	skb = sock_wmalloc(sk, total_len + dev->hard_header_len + 32,
			   0, GFP_KERNEL);
	if (!skb) {
		error = -ENOMEM;
		goto end;
	}

	/* Reserve space for headers. */
	skb_reserve(skb, dev->hard_header_len);
	skb->nh.raw = skb->data;

	skb->dev = dev;

	skb->priority = sk->sk_priority;
	skb->protocol = __constant_htons(ETH_P_PPP_SES);

	ph = (struct pppoe_hdr *) skb_put(skb, total_len + sizeof(struct pppoe_hdr));
	start = (char *) &ph->tag[0];

	error = memcpy_fromiovec(start, m->msg_iov, total_len);

	if (error < 0) {
		kfree_skb(skb);
		goto end;
	}

	error = total_len;
	dev->hard_header(skb, dev, ETH_P_PPP_SES,
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
	struct pppoe_hdr hdr;
	struct pppoe_hdr *ph;
	int headroom = skb_headroom(skb);
	int data_len = skb->len;
	struct sk_buff *skb2;

	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED))
		goto abort;

	hdr.ver	= 1;
	hdr.type = 1;
	hdr.code = 0;
	hdr.sid	= po->num;
	hdr.length = htons(skb->len);

	if (!dev)
		goto abort;

	/* Copy the skb if there is no space for the header. */
	if (headroom < (sizeof(struct pppoe_hdr) + dev->hard_header_len)) {
		skb2 = dev_alloc_skb(32+skb->len +
				     sizeof(struct pppoe_hdr) +
				     dev->hard_header_len);

		if (skb2 == NULL)
			goto abort;

		skb_reserve(skb2, dev->hard_header_len + sizeof(struct pppoe_hdr));
		memcpy(skb_put(skb2, skb->len), skb->data, skb->len);
	} else {
		/* Make a clone so as to not disturb the original skb,
		 * give dev_queue_xmit something it can free.
		 */
		skb2 = skb_clone(skb, GFP_ATOMIC);

		if (skb2 == NULL)
			goto abort;
	}

	ph = (struct pppoe_hdr *) skb_push(skb2, sizeof(struct pppoe_hdr));
	memcpy(ph, &hdr, sizeof(struct pppoe_hdr));
	skb2->protocol = __constant_htons(ETH_P_PPP_SES);

	skb2->nh.raw = skb2->data;

	skb2->dev = dev;

	dev->hard_header(skb2, dev, ETH_P_PPP_SES,
			 po->pppoe_pa.remote, NULL, data_len);

	/* We're transmitting skb2, and assuming that dev_queue_xmit
	 * will free it.  The generic ppp layer however, is expecting
	 * that we give back 'skb' (not 'skb2') in case of failure,
	 * but free it in case of success.
	 */

	if (dev_queue_xmit(skb2) < 0)
		goto abort;

	kfree_skb(skb);
	return 1;

abort:
	return 0;
}


/************************************************************************
 *
 * xmit function called by generic PPP driver
 * sends PPP frame over PPPoE socket
 *
 ***********************************************************************/
static int pppoe_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *) chan->private;
	return __pppoe_xmit(sk, skb);
}


static struct ppp_channel_ops pppoe_chan_ops = { 
	.start_xmit = pppoe_xmit, 
};

static int pppoe_recvmsg(struct kiocb *iocb, struct socket *sock,
		  struct msghdr *m, size_t total_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb = NULL;
	int error = 0;
	int len;
	struct pppoe_hdr *ph = NULL;

	if (sk->sk_state & PPPOX_BOUND) {
		error = -EIO;
		goto end;
	}

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &error);

	if (error < 0) {
		goto end;
	}

	m->msg_namelen = 0;

	if (skb) {
		error = 0;
		ph = (struct pppoe_hdr *) skb->nh.raw;
		len = ntohs(ph->length);

		error = memcpy_toiovec(m->msg_iov, (unsigned char *) &ph->tag[0], len);
		if (error < 0)
			goto do_skb_free;
		error = len;
	}

do_skb_free:
	if (skb)
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

	seq_printf(seq, "%08X %02X:%02X:%02X:%02X:%02X:%02X %8s\n",
		   po->pppoe_pa.sid,
		   po->pppoe_pa.remote[0], po->pppoe_pa.remote[1],
		   po->pppoe_pa.remote[2], po->pppoe_pa.remote[3],
		   po->pppoe_pa.remote[4], po->pppoe_pa.remote[5], dev_name);
out:
	return 0;
}

static __inline__ struct pppox_sock *pppoe_get_idx(loff_t pos)
{
	struct pppox_sock *po = NULL;
	int i = 0;

	for (; i < PPPOE_HASH_SIZE; i++) {
		po = item_hash_table[i];
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
{
	loff_t l = *pos;

	read_lock_bh(&pppoe_hash_lock);
	return l ? pppoe_get_idx(--l) : SEQ_START_TOKEN;
}

static void *pppoe_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct pppox_sock *po;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		po = pppoe_get_idx(0);
		goto out;
	}
	po = v;
	if (po->next) 
		po = po->next;
	else {
		int hash = hash_item(po->pppoe_pa.sid, po->pppoe_pa.remote);

		while (++hash < PPPOE_HASH_SIZE) {
			po = item_hash_table[hash];
			if (po)
				break;
		}
	}
out:
	return po;
}

static void pppoe_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&pppoe_hash_lock);
}

static struct seq_operations pppoe_seq_ops = {
	.start		= pppoe_seq_start,
	.next		= pppoe_seq_next,
	.stop		= pppoe_seq_stop,
	.show		= pppoe_seq_show,
};

static int pppoe_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &pppoe_seq_ops);
}

static struct file_operations pppoe_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= pppoe_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init pppoe_proc_init(void)
{
	struct proc_dir_entry *p;

	p = create_proc_entry("net/pppoe", S_IRUGO, NULL);
	if (!p)
		return -ENOMEM;

	p->proc_fops = &pppoe_seq_fops;
	return 0;
}
#else /* CONFIG_PROC_FS */
static inline int pppoe_proc_init(void) { return 0; }
#endif /* CONFIG_PROC_FS */

static const struct proto_ops pppoe_ops = {
    .family		= AF_PPPOX,
    .owner		= THIS_MODULE,
    .release		= pppoe_release,
    .bind		= sock_no_bind,
    .connect		= pppoe_connect,
    .socketpair		= sock_no_socketpair,
    .accept		= sock_no_accept,
    .getname		= pppoe_getname,
    .poll		= datagram_poll,
    .listen		= sock_no_listen,
    .shutdown		= sock_no_shutdown,
    .setsockopt		= sock_no_setsockopt,
    .getsockopt		= sock_no_getsockopt,
    .sendmsg		= pppoe_sendmsg,
    .recvmsg		= pppoe_recvmsg,
    .mmap		= sock_no_mmap,
    .ioctl		= pppox_ioctl,
};

static struct pppox_proto pppoe_proto = {
    .create	= pppoe_create,
    .ioctl	= pppoe_ioctl,
    .owner	= THIS_MODULE,
};


static int __init pppoe_init(void)
{
	int err = proto_register(&pppoe_sk_proto, 0);

	if (err)
		goto out;

 	err = register_pppox_proto(PX_PROTO_OE, &pppoe_proto);
	if (err)
		goto out_unregister_pppoe_proto;

	err = pppoe_proc_init();
	if (err)
		goto out_unregister_pppox_proto;
	
	dev_add_pack(&pppoes_ptype);
	dev_add_pack(&pppoed_ptype);
	register_netdevice_notifier(&pppoe_notifier);
out:
	return err;
out_unregister_pppox_proto:
	unregister_pppox_proto(PX_PROTO_OE);
out_unregister_pppoe_proto:
	proto_unregister(&pppoe_sk_proto);
	goto out;
}

static void __exit pppoe_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OE);
	dev_remove_pack(&pppoes_ptype);
	dev_remove_pack(&pppoed_ptype);
	unregister_netdevice_notifier(&pppoe_notifier);
	remove_proc_entry("net/pppoe", NULL);
	proto_unregister(&pppoe_sk_proto);
}

module_init(pppoe_init);
module_exit(pppoe_exit);

MODULE_AUTHOR("Michal Ostrowski <mostrows@speakeasy.net>");
MODULE_DESCRIPTION("PPP over Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_PPPOX);
