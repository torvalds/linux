/*****************************************************************************
 * Linux PPP over L2TP (PPPoX/PPPoL2TP) Sockets
 *
 * PPPoX    --- Generic PPP encapsulation socket family
 * PPPoL2TP --- PPP over L2TP (RFC 2661)
 *
 * Version:	1.0.0
 *
 * Authors:	Martijn van Oosterhout <kleptog@svana.org>
 *		James Chapman (jchapman@katalix.com)
 * Contributors:
 *		Michal Ostrowski <mostrows@speakeasy.net>
 *		Arnaldo Carvalho de Melo <acme@xconectiva.com.br>
 *		David S. Miller (davem@redhat.com)
 *
 * License:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

/* This driver handles only L2TP data frames; control frames are handled by a
 * userspace application.
 *
 * To send data in an L2TP session, userspace opens a PPPoL2TP socket and
 * attaches it to a bound UDP socket with local tunnel_id / session_id and
 * peer tunnel_id / session_id set. Data can then be sent or received using
 * regular socket sendmsg() / recvmsg() calls. Kernel parameters of the socket
 * can be read or modified using ioctl() or [gs]etsockopt() calls.
 *
 * When a PPPoL2TP socket is connected with local and peer session_id values
 * zero, the socket is treated as a special tunnel management socket.
 *
 * Here's example userspace code to create a socket for sending/receiving data
 * over an L2TP session:-
 *
 *	struct sockaddr_pppol2tp sax;
 *	int fd;
 *	int session_fd;
 *
 *	fd = socket(AF_PPPOX, SOCK_DGRAM, PX_PROTO_OL2TP);
 *
 *	sax.sa_family = AF_PPPOX;
 *	sax.sa_protocol = PX_PROTO_OL2TP;
 *	sax.pppol2tp.fd = tunnel_fd;	// bound UDP socket
 *	sax.pppol2tp.addr.sin_addr.s_addr = addr->sin_addr.s_addr;
 *	sax.pppol2tp.addr.sin_port = addr->sin_port;
 *	sax.pppol2tp.addr.sin_family = AF_INET;
 *	sax.pppol2tp.s_tunnel  = tunnel_id;
 *	sax.pppol2tp.s_session = session_id;
 *	sax.pppol2tp.d_tunnel  = peer_tunnel_id;
 *	sax.pppol2tp.d_session = peer_session_id;
 *
 *	session_fd = connect(fd, (struct sockaddr *)&sax, sizeof(sax));
 *
 * A pppd plugin that allows PPP traffic to be carried over L2TP using
 * this driver is available from the OpenL2TP project at
 * http://openl2tp.sourceforge.net.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/jiffies.h>

#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/if_pppox.h>
#include <linux/if_pppol2tp.h>
#include <net/sock.h>
#include <linux/ppp_channel.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/file.h>
#include <linux/hash.h>
#include <linux/sort.h>
#include <linux/proc_fs.h>
#include <net/net_namespace.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/xfrm.h>

#include <asm/byteorder.h>
#include <asm/atomic.h>


#define PPPOL2TP_DRV_VERSION	"V1.0"

/* L2TP header constants */
#define L2TP_HDRFLAG_T	   0x8000
#define L2TP_HDRFLAG_L	   0x4000
#define L2TP_HDRFLAG_S	   0x0800
#define L2TP_HDRFLAG_O	   0x0200
#define L2TP_HDRFLAG_P	   0x0100

#define L2TP_HDR_VER_MASK  0x000F
#define L2TP_HDR_VER	   0x0002

/* Space for UDP, L2TP and PPP headers */
#define PPPOL2TP_HEADER_OVERHEAD	40

/* Just some random numbers */
#define L2TP_TUNNEL_MAGIC	0x42114DDA
#define L2TP_SESSION_MAGIC	0x0C04EB7D

#define PPPOL2TP_HASH_BITS	4
#define PPPOL2TP_HASH_SIZE	(1 << PPPOL2TP_HASH_BITS)

/* Default trace flags */
#define PPPOL2TP_DEFAULT_DEBUG_FLAGS	0

#define PRINTK(_mask, _type, _lvl, _fmt, args...)			\
	do {								\
		if ((_mask) & (_type))					\
			printk(_lvl "PPPOL2TP: " _fmt, ##args);		\
	} while(0)

/* Number of bytes to build transmit L2TP headers.
 * Unfortunately the size is different depending on whether sequence numbers
 * are enabled.
 */
#define PPPOL2TP_L2TP_HDR_SIZE_SEQ		10
#define PPPOL2TP_L2TP_HDR_SIZE_NOSEQ		6

struct pppol2tp_tunnel;

/* Describes a session. It is the sk_user_data field in the PPPoL2TP
 * socket. Contains information to determine incoming packets and transmit
 * outgoing ones.
 */
struct pppol2tp_session
{
	int			magic;		/* should be
						 * L2TP_SESSION_MAGIC */
	int			owner;		/* pid that opened the socket */

	struct sock		*sock;		/* Pointer to the session
						 * PPPoX socket */
	struct sock		*tunnel_sock;	/* Pointer to the tunnel UDP
						 * socket */

	struct pppol2tp_addr	tunnel_addr;	/* Description of tunnel */

	struct pppol2tp_tunnel	*tunnel;	/* back pointer to tunnel
						 * context */

	char			name[20];	/* "sess xxxxx/yyyyy", where
						 * x=tunnel_id, y=session_id */
	int			mtu;
	int			mru;
	int			flags;		/* accessed by PPPIOCGFLAGS.
						 * Unused. */
	unsigned		recv_seq:1;	/* expect receive packets with
						 * sequence numbers? */
	unsigned		send_seq:1;	/* send packets with sequence
						 * numbers? */
	unsigned		lns_mode:1;	/* behave as LNS? LAC enables
						 * sequence numbers under
						 * control of LNS. */
	int			debug;		/* bitmask of debug message
						 * categories */
	int			reorder_timeout; /* configured reorder timeout
						  * (in jiffies) */
	u16			nr;		/* session NR state (receive) */
	u16			ns;		/* session NR state (send) */
	struct sk_buff_head	reorder_q;	/* receive reorder queue */
	struct pppol2tp_ioc_stats stats;
	struct hlist_node	hlist;		/* Hash list node */
};

/* The sk_user_data field of the tunnel's UDP socket. It contains info to track
 * all the associated sessions so incoming packets can be sorted out
 */
struct pppol2tp_tunnel
{
	int			magic;		/* Should be L2TP_TUNNEL_MAGIC */
	rwlock_t		hlist_lock;	/* protect session_hlist */
	struct hlist_head	session_hlist[PPPOL2TP_HASH_SIZE];
						/* hashed list of sessions,
						 * hashed by id */
	int			debug;		/* bitmask of debug message
						 * categories */
	char			name[12];	/* "tunl xxxxx" */
	struct pppol2tp_ioc_stats stats;

	void (*old_sk_destruct)(struct sock *);

	struct sock		*sock;		/* Parent socket */
	struct list_head	list;		/* Keep a list of all open
						 * prepared sockets */

	atomic_t		ref_count;
};

/* Private data stored for received packets in the skb.
 */
struct pppol2tp_skb_cb {
	u16			ns;
	u16			nr;
	u16			has_seq;
	u16			length;
	unsigned long		expires;
};

#define PPPOL2TP_SKB_CB(skb)	((struct pppol2tp_skb_cb *) &skb->cb[sizeof(struct inet_skb_parm)])

static int pppol2tp_xmit(struct ppp_channel *chan, struct sk_buff *skb);
static void pppol2tp_tunnel_free(struct pppol2tp_tunnel *tunnel);

static atomic_t pppol2tp_tunnel_count;
static atomic_t pppol2tp_session_count;
static struct ppp_channel_ops pppol2tp_chan_ops = { pppol2tp_xmit , NULL };
static struct proto_ops pppol2tp_ops;
static LIST_HEAD(pppol2tp_tunnel_list);
static DEFINE_RWLOCK(pppol2tp_tunnel_list_lock);

/* Helpers to obtain tunnel/session contexts from sockets.
 */
static inline struct pppol2tp_session *pppol2tp_sock_to_session(struct sock *sk)
{
	struct pppol2tp_session *session;

	if (sk == NULL)
		return NULL;

	session = (struct pppol2tp_session *)(sk->sk_user_data);
	if (session == NULL)
		return NULL;

	BUG_ON(session->magic != L2TP_SESSION_MAGIC);

	return session;
}

static inline struct pppol2tp_tunnel *pppol2tp_sock_to_tunnel(struct sock *sk)
{
	struct pppol2tp_tunnel *tunnel;

	if (sk == NULL)
		return NULL;

	tunnel = (struct pppol2tp_tunnel *)(sk->sk_user_data);
	if (tunnel == NULL)
		return NULL;

	BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);

	return tunnel;
}

/* Tunnel reference counts. Incremented per session that is added to
 * the tunnel.
 */
static inline void pppol2tp_tunnel_inc_refcount(struct pppol2tp_tunnel *tunnel)
{
	atomic_inc(&tunnel->ref_count);
}

static inline void pppol2tp_tunnel_dec_refcount(struct pppol2tp_tunnel *tunnel)
{
	if (atomic_dec_and_test(&tunnel->ref_count))
		pppol2tp_tunnel_free(tunnel);
}

/* Session hash list.
 * The session_id SHOULD be random according to RFC2661, but several
 * L2TP implementations (Cisco and Microsoft) use incrementing
 * session_ids.  So we do a real hash on the session_id, rather than a
 * simple bitmask.
 */
static inline struct hlist_head *
pppol2tp_session_id_hash(struct pppol2tp_tunnel *tunnel, u16 session_id)
{
	unsigned long hash_val = (unsigned long) session_id;
	return &tunnel->session_hlist[hash_long(hash_val, PPPOL2TP_HASH_BITS)];
}

/* Lookup a session by id
 */
static struct pppol2tp_session *
pppol2tp_session_find(struct pppol2tp_tunnel *tunnel, u16 session_id)
{
	struct hlist_head *session_list =
		pppol2tp_session_id_hash(tunnel, session_id);
	struct pppol2tp_session *session;
	struct hlist_node *walk;

	read_lock(&tunnel->hlist_lock);
	hlist_for_each_entry(session, walk, session_list, hlist) {
		if (session->tunnel_addr.s_session == session_id) {
			read_unlock(&tunnel->hlist_lock);
			return session;
		}
	}
	read_unlock(&tunnel->hlist_lock);

	return NULL;
}

/* Lookup a tunnel by id
 */
static struct pppol2tp_tunnel *pppol2tp_tunnel_find(u16 tunnel_id)
{
	struct pppol2tp_tunnel *tunnel = NULL;

	read_lock(&pppol2tp_tunnel_list_lock);
	list_for_each_entry(tunnel, &pppol2tp_tunnel_list, list) {
		if (tunnel->stats.tunnel_id == tunnel_id) {
			read_unlock(&pppol2tp_tunnel_list_lock);
			return tunnel;
		}
	}
	read_unlock(&pppol2tp_tunnel_list_lock);

	return NULL;
}

/*****************************************************************************
 * Receive data handling
 *****************************************************************************/

/* Queue a skb in order. We come here only if the skb has an L2TP sequence
 * number.
 */
static void pppol2tp_recv_queue_skb(struct pppol2tp_session *session, struct sk_buff *skb)
{
	struct sk_buff *skbp;
	u16 ns = PPPOL2TP_SKB_CB(skb)->ns;

	spin_lock(&session->reorder_q.lock);
	skb_queue_walk(&session->reorder_q, skbp) {
		if (PPPOL2TP_SKB_CB(skbp)->ns > ns) {
			__skb_insert(skb, skbp->prev, skbp, &session->reorder_q);
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
			       "%s: pkt %hu, inserted before %hu, reorder_q len=%d\n",
			       session->name, ns, PPPOL2TP_SKB_CB(skbp)->ns,
			       skb_queue_len(&session->reorder_q));
			session->stats.rx_oos_packets++;
			goto out;
		}
	}

	__skb_queue_tail(&session->reorder_q, skb);

out:
	spin_unlock(&session->reorder_q.lock);
}

/* Dequeue a single skb.
 */
static void pppol2tp_recv_dequeue_skb(struct pppol2tp_session *session, struct sk_buff *skb)
{
	struct pppol2tp_tunnel *tunnel = session->tunnel;
	int length = PPPOL2TP_SKB_CB(skb)->length;
	struct sock *session_sock = NULL;

	/* We're about to requeue the skb, so unlink it and return resources
	 * to its current owner (a socket receive buffer).
	 */
	skb_unlink(skb, &session->reorder_q);
	skb_orphan(skb);

	tunnel->stats.rx_packets++;
	tunnel->stats.rx_bytes += length;
	session->stats.rx_packets++;
	session->stats.rx_bytes += length;

	if (PPPOL2TP_SKB_CB(skb)->has_seq) {
		/* Bump our Nr */
		session->nr++;
		PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: updated nr to %hu\n", session->name, session->nr);
	}

	/* If the socket is bound, send it in to PPP's input queue. Otherwise
	 * queue it on the session socket.
	 */
	session_sock = session->sock;
	if (session_sock->sk_state & PPPOX_BOUND) {
		struct pppox_sock *po;
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: recv %d byte data frame, passing to ppp\n",
		       session->name, length);

		/* We need to forget all info related to the L2TP packet
		 * gathered in the skb as we are going to reuse the same
		 * skb for the inner packet.
		 * Namely we need to:
		 * - reset xfrm (IPSec) information as it applies to
		 *   the outer L2TP packet and not to the inner one
		 * - release the dst to force a route lookup on the inner
		 *   IP packet since skb->dst currently points to the dst
		 *   of the UDP tunnel
		 * - reset netfilter information as it doesn't apply
		 *   to the inner packet either
		 */
		secpath_reset(skb);
		dst_release(skb->dst);
		skb->dst = NULL;
		nf_reset(skb);

		po = pppox_sk(session_sock);
		ppp_input(&po->chan, skb);
	} else {
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_INFO,
		       "%s: socket not bound\n", session->name);

		/* Not bound. Nothing we can do, so discard. */
		session->stats.rx_errors++;
		kfree_skb(skb);
	}

	sock_put(session->sock);
}

/* Dequeue skbs from the session's reorder_q, subject to packet order.
 * Skbs that have been in the queue for too long are simply discarded.
 */
static void pppol2tp_recv_dequeue(struct pppol2tp_session *session)
{
	struct sk_buff *skb;
	struct sk_buff *tmp;

	/* If the pkt at the head of the queue has the nr that we
	 * expect to send up next, dequeue it and any other
	 * in-sequence packets behind it.
	 */
	spin_lock(&session->reorder_q.lock);
	skb_queue_walk_safe(&session->reorder_q, skb, tmp) {
		if (time_after(jiffies, PPPOL2TP_SKB_CB(skb)->expires)) {
			session->stats.rx_seq_discards++;
			session->stats.rx_errors++;
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
			       "%s: oos pkt %hu len %d discarded (too old), "
			       "waiting for %hu, reorder_q_len=%d\n",
			       session->name, PPPOL2TP_SKB_CB(skb)->ns,
			       PPPOL2TP_SKB_CB(skb)->length, session->nr,
			       skb_queue_len(&session->reorder_q));
			__skb_unlink(skb, &session->reorder_q);
			kfree_skb(skb);
			sock_put(session->sock);
			continue;
		}

		if (PPPOL2TP_SKB_CB(skb)->has_seq) {
			if (PPPOL2TP_SKB_CB(skb)->ns != session->nr) {
				PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
				       "%s: holding oos pkt %hu len %d, "
				       "waiting for %hu, reorder_q_len=%d\n",
				       session->name, PPPOL2TP_SKB_CB(skb)->ns,
				       PPPOL2TP_SKB_CB(skb)->length, session->nr,
				       skb_queue_len(&session->reorder_q));
				goto out;
			}
		}
		spin_unlock(&session->reorder_q.lock);
		pppol2tp_recv_dequeue_skb(session, skb);
		spin_lock(&session->reorder_q.lock);
	}

out:
	spin_unlock(&session->reorder_q.lock);
}

/* Internal receive frame. Do the real work of receiving an L2TP data frame
 * here. The skb is not on a list when we get here.
 * Returns 0 if the packet was a data packet and was successfully passed on.
 * Returns 1 if the packet was not a good data packet and could not be
 * forwarded.  All such packets are passed up to userspace to deal with.
 */
static int pppol2tp_recv_core(struct sock *sock, struct sk_buff *skb)
{
	struct pppol2tp_session *session = NULL;
	struct pppol2tp_tunnel *tunnel;
	unsigned char *ptr, *optr;
	u16 hdrflags;
	u16 tunnel_id, session_id;
	int length;
	int offset;

	tunnel = pppol2tp_sock_to_tunnel(sock);
	if (tunnel == NULL)
		goto no_tunnel;

	/* UDP always verifies the packet length. */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Short packet? */
	if (!pskb_may_pull(skb, 12)) {
		PRINTK(tunnel->debug, PPPOL2TP_MSG_DATA, KERN_INFO,
		       "%s: recv short packet (len=%d)\n", tunnel->name, skb->len);
		goto error;
	}

	/* Point to L2TP header */
	optr = ptr = skb->data;

	/* Get L2TP header flags */
	hdrflags = ntohs(*(__be16*)ptr);

	/* Trace packet contents, if enabled */
	if (tunnel->debug & PPPOL2TP_MSG_DATA) {
		length = min(16u, skb->len);
		if (!pskb_may_pull(skb, length))
			goto error;

		printk(KERN_DEBUG "%s: recv: ", tunnel->name);

		offset = 0;
		do {
			printk(" %02X", ptr[offset]);
		} while (++offset < length);

		printk("\n");
	}

	/* Get length of L2TP packet */
	length = skb->len;

	/* If type is control packet, it is handled by userspace. */
	if (hdrflags & L2TP_HDRFLAG_T) {
		PRINTK(tunnel->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: recv control packet, len=%d\n", tunnel->name, length);
		goto error;
	}

	/* Skip flags */
	ptr += 2;

	/* If length is present, skip it */
	if (hdrflags & L2TP_HDRFLAG_L)
		ptr += 2;

	/* Extract tunnel and session ID */
	tunnel_id = ntohs(*(__be16 *) ptr);
	ptr += 2;
	session_id = ntohs(*(__be16 *) ptr);
	ptr += 2;

	/* Find the session context */
	session = pppol2tp_session_find(tunnel, session_id);
	if (!session) {
		/* Not found? Pass to userspace to deal with */
		PRINTK(tunnel->debug, PPPOL2TP_MSG_DATA, KERN_INFO,
		       "%s: no socket found (%hu/%hu). Passing up.\n",
		       tunnel->name, tunnel_id, session_id);
		goto error;
	}
	sock_hold(session->sock);

	/* The ref count on the socket was increased by the above call since
	 * we now hold a pointer to the session. Take care to do sock_put()
	 * when exiting this function from now on...
	 */

	/* Handle the optional sequence numbers.  If we are the LAC,
	 * enable/disable sequence numbers under the control of the LNS.  If
	 * no sequence numbers present but we were expecting them, discard
	 * frame.
	 */
	if (hdrflags & L2TP_HDRFLAG_S) {
		u16 ns, nr;
		ns = ntohs(*(__be16 *) ptr);
		ptr += 2;
		nr = ntohs(*(__be16 *) ptr);
		ptr += 2;

		/* Received a packet with sequence numbers. If we're the LNS,
		 * check if we sre sending sequence numbers and if not,
		 * configure it so.
		 */
		if ((!session->lns_mode) && (!session->send_seq)) {
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_INFO,
			       "%s: requested to enable seq numbers by LNS\n",
			       session->name);
			session->send_seq = -1;
		}

		/* Store L2TP info in the skb */
		PPPOL2TP_SKB_CB(skb)->ns = ns;
		PPPOL2TP_SKB_CB(skb)->nr = nr;
		PPPOL2TP_SKB_CB(skb)->has_seq = 1;

		PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: recv data ns=%hu, nr=%hu, session nr=%hu\n",
		       session->name, ns, nr, session->nr);
	} else {
		/* No sequence numbers.
		 * If user has configured mandatory sequence numbers, discard.
		 */
		if (session->recv_seq) {
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_WARNING,
			       "%s: recv data has no seq numbers when required. "
			       "Discarding\n", session->name);
			session->stats.rx_seq_discards++;
			goto discard;
		}

		/* If we're the LAC and we're sending sequence numbers, the
		 * LNS has requested that we no longer send sequence numbers.
		 * If we're the LNS and we're sending sequence numbers, the
		 * LAC is broken. Discard the frame.
		 */
		if ((!session->lns_mode) && (session->send_seq)) {
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_INFO,
			       "%s: requested to disable seq numbers by LNS\n",
			       session->name);
			session->send_seq = 0;
		} else if (session->send_seq) {
			PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_WARNING,
			       "%s: recv data has no seq numbers when required. "
			       "Discarding\n", session->name);
			session->stats.rx_seq_discards++;
			goto discard;
		}

		/* Store L2TP info in the skb */
		PPPOL2TP_SKB_CB(skb)->has_seq = 0;
	}

	/* If offset bit set, skip it. */
	if (hdrflags & L2TP_HDRFLAG_O) {
		offset = ntohs(*(__be16 *)ptr);
		ptr += 2 + offset;
	}

	offset = ptr - optr;
	if (!pskb_may_pull(skb, offset))
		goto discard;

	__skb_pull(skb, offset);

	/* Skip PPP header, if present.	 In testing, Microsoft L2TP clients
	 * don't send the PPP header (PPP header compression enabled), but
	 * other clients can include the header. So we cope with both cases
	 * here. The PPP header is always FF03 when using L2TP.
	 *
	 * Note that skb->data[] isn't dereferenced from a u16 ptr here since
	 * the field may be unaligned.
	 */
	if (!pskb_may_pull(skb, 2))
		goto discard;

	if ((skb->data[0] == 0xff) && (skb->data[1] == 0x03))
		skb_pull(skb, 2);

	/* Prepare skb for adding to the session's reorder_q.  Hold
	 * packets for max reorder_timeout or 1 second if not
	 * reordering.
	 */
	PPPOL2TP_SKB_CB(skb)->length = length;
	PPPOL2TP_SKB_CB(skb)->expires = jiffies +
		(session->reorder_timeout ? session->reorder_timeout : HZ);

	/* Add packet to the session's receive queue. Reordering is done here, if
	 * enabled. Saved L2TP protocol info is stored in skb->sb[].
	 */
	if (PPPOL2TP_SKB_CB(skb)->has_seq) {
		if (session->reorder_timeout != 0) {
			/* Packet reordering enabled. Add skb to session's
			 * reorder queue, in order of ns.
			 */
			pppol2tp_recv_queue_skb(session, skb);
		} else {
			/* Packet reordering disabled. Discard out-of-sequence
			 * packets
			 */
			if (PPPOL2TP_SKB_CB(skb)->ns != session->nr) {
				session->stats.rx_seq_discards++;
				PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
				       "%s: oos pkt %hu len %d discarded, "
				       "waiting for %hu, reorder_q_len=%d\n",
				       session->name, PPPOL2TP_SKB_CB(skb)->ns,
				       PPPOL2TP_SKB_CB(skb)->length, session->nr,
				       skb_queue_len(&session->reorder_q));
				goto discard;
			}
			skb_queue_tail(&session->reorder_q, skb);
		}
	} else {
		/* No sequence numbers. Add the skb to the tail of the
		 * reorder queue. This ensures that it will be
		 * delivered after all previous sequenced skbs.
		 */
		skb_queue_tail(&session->reorder_q, skb);
	}

	/* Try to dequeue as many skbs from reorder_q as we can. */
	pppol2tp_recv_dequeue(session);

	return 0;

discard:
	session->stats.rx_errors++;
	kfree_skb(skb);
	sock_put(session->sock);

	return 0;

error:
	/* Put UDP header back */
	__skb_push(skb, sizeof(struct udphdr));

no_tunnel:
	return 1;
}

/* UDP encapsulation receive handler. See net/ipv4/udp.c.
 * Return codes:
 * 0 : success.
 * <0: error
 * >0: skb should be passed up to userspace as UDP.
 */
static int pppol2tp_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct pppol2tp_tunnel *tunnel;

	tunnel = pppol2tp_sock_to_tunnel(sk);
	if (tunnel == NULL)
		goto pass_up;

	PRINTK(tunnel->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
	       "%s: received %d bytes\n", tunnel->name, skb->len);

	if (pppol2tp_recv_core(sk, skb))
		goto pass_up;

	return 0;

pass_up:
	return 1;
}

/* Receive message. This is the recvmsg for the PPPoL2TP socket.
 */
static int pppol2tp_recvmsg(struct kiocb *iocb, struct socket *sock,
			    struct msghdr *msg, size_t len,
			    int flags)
{
	int err;
	struct sk_buff *skb;
	struct sock *sk = sock->sk;

	err = -EIO;
	if (sk->sk_state & PPPOX_BOUND)
		goto end;

	msg->msg_namelen = 0;

	err = 0;
	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &err);
	if (skb) {
		err = memcpy_toiovec(msg->msg_iov, (unsigned char *) skb->data,
				     skb->len);
		if (err < 0)
			goto do_skb_free;
		err = skb->len;
	}
do_skb_free:
	kfree_skb(skb);
end:
	return err;
}

/************************************************************************
 * Transmit handling
 ***********************************************************************/

/* Tell how big L2TP headers are for a particular session. This
 * depends on whether sequence numbers are being used.
 */
static inline int pppol2tp_l2tp_header_len(struct pppol2tp_session *session)
{
	if (session->send_seq)
		return PPPOL2TP_L2TP_HDR_SIZE_SEQ;

	return PPPOL2TP_L2TP_HDR_SIZE_NOSEQ;
}

/* Build an L2TP header for the session into the buffer provided.
 */
static void pppol2tp_build_l2tp_header(struct pppol2tp_session *session,
				       void *buf)
{
	__be16 *bufp = buf;
	u16 flags = L2TP_HDR_VER;

	if (session->send_seq)
		flags |= L2TP_HDRFLAG_S;

	/* Setup L2TP header.
	 * FIXME: Can this ever be unaligned? Is direct dereferencing of
	 * 16-bit header fields safe here for all architectures?
	 */
	*bufp++ = htons(flags);
	*bufp++ = htons(session->tunnel_addr.d_tunnel);
	*bufp++ = htons(session->tunnel_addr.d_session);
	if (session->send_seq) {
		*bufp++ = htons(session->ns);
		*bufp++ = 0;
		session->ns++;
		PRINTK(session->debug, PPPOL2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: updated ns to %hu\n", session->name, session->ns);
	}
}

/* This is the sendmsg for the PPPoL2TP pppol2tp_session socket.  We come here
 * when a user application does a sendmsg() on the session socket. L2TP and
 * PPP headers must be inserted into the user's data.
 */
static int pppol2tp_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m,
			    size_t total_len)
{
	static const unsigned char ppph[2] = { 0xff, 0x03 };
	struct sock *sk = sock->sk;
	struct inet_sock *inet;
	__wsum csum = 0;
	struct sk_buff *skb;
	int error;
	int hdr_len;
	struct pppol2tp_session *session;
	struct pppol2tp_tunnel *tunnel;
	struct udphdr *uh;
	unsigned int len;

	error = -ENOTCONN;
	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED))
		goto error;

	/* Get session and tunnel contexts */
	error = -EBADF;
	session = pppol2tp_sock_to_session(sk);
	if (session == NULL)
		goto error;

	tunnel = pppol2tp_sock_to_tunnel(session->tunnel_sock);
	if (tunnel == NULL)
		goto error;

	/* What header length is configured for this session? */
	hdr_len = pppol2tp_l2tp_header_len(session);

	/* Allocate a socket buffer */
	error = -ENOMEM;
	skb = sock_wmalloc(sk, NET_SKB_PAD + sizeof(struct iphdr) +
			   sizeof(struct udphdr) + hdr_len +
			   sizeof(ppph) + total_len,
			   0, GFP_KERNEL);
	if (!skb)
		goto error;

	/* Reserve space for headers. */
	skb_reserve(skb, NET_SKB_PAD);
	skb_reset_network_header(skb);
	skb_reserve(skb, sizeof(struct iphdr));
	skb_reset_transport_header(skb);

	/* Build UDP header */
	inet = inet_sk(session->tunnel_sock);
	uh = (struct udphdr *) skb->data;
	uh->source = inet->sport;
	uh->dest = inet->dport;
	uh->len = htons(hdr_len + sizeof(ppph) + total_len);
	uh->check = 0;
	skb_put(skb, sizeof(struct udphdr));

	/* Build L2TP header */
	pppol2tp_build_l2tp_header(session, skb->data);
	skb_put(skb, hdr_len);

	/* Add PPP header */
	skb->data[0] = ppph[0];
	skb->data[1] = ppph[1];
	skb_put(skb, 2);

	/* Copy user data into skb */
	error = memcpy_fromiovec(skb->data, m->msg_iov, total_len);
	if (error < 0) {
		kfree_skb(skb);
		goto error;
	}
	skb_put(skb, total_len);

	/* Calculate UDP checksum if configured to do so */
	if (session->tunnel_sock->sk_no_check != UDP_CSUM_NOXMIT)
		csum = udp_csum_outgoing(sk, skb);

	/* Debug */
	if (session->send_seq)
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %Zd bytes, ns=%hu\n", session->name,
		       total_len, session->ns - 1);
	else
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %Zd bytes\n", session->name, total_len);

	if (session->debug & PPPOL2TP_MSG_DATA) {
		int i;
		unsigned char *datap = skb->data;

		printk(KERN_DEBUG "%s: xmit:", session->name);
		for (i = 0; i < total_len; i++) {
			printk(" %02X", *datap++);
			if (i == 15) {
				printk(" ...");
				break;
			}
		}
		printk("\n");
	}

	/* Queue the packet to IP for output */
	len = skb->len;
	error = ip_queue_xmit(skb, 1);

	/* Update stats */
	if (error >= 0) {
		tunnel->stats.tx_packets++;
		tunnel->stats.tx_bytes += len;
		session->stats.tx_packets++;
		session->stats.tx_bytes += len;
	} else {
		tunnel->stats.tx_errors++;
		session->stats.tx_errors++;
	}

error:
	return error;
}

/* Transmit function called by generic PPP driver.  Sends PPP frame
 * over PPPoL2TP socket.
 *
 * This is almost the same as pppol2tp_sendmsg(), but rather than
 * being called with a msghdr from userspace, it is called with a skb
 * from the kernel.
 *
 * The supplied skb from ppp doesn't have enough headroom for the
 * insertion of L2TP, UDP and IP headers so we need to allocate more
 * headroom in the skb. This will create a cloned skb. But we must be
 * careful in the error case because the caller will expect to free
 * the skb it supplied, not our cloned skb. So we take care to always
 * leave the original skb unfreed if we return an error.
 */
static int pppol2tp_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	static const u8 ppph[2] = { 0xff, 0x03 };
	struct sock *sk = (struct sock *) chan->private;
	struct sock *sk_tun;
	int hdr_len;
	struct pppol2tp_session *session;
	struct pppol2tp_tunnel *tunnel;
	int rc;
	int headroom;
	int data_len = skb->len;
	struct inet_sock *inet;
	__wsum csum = 0;
	struct udphdr *uh;
	unsigned int len;

	if (sock_flag(sk, SOCK_DEAD) || !(sk->sk_state & PPPOX_CONNECTED))
		goto abort;

	/* Get session and tunnel contexts from the socket */
	session = pppol2tp_sock_to_session(sk);
	if (session == NULL)
		goto abort;

	sk_tun = session->tunnel_sock;
	if (sk_tun == NULL)
		goto abort;
	tunnel = pppol2tp_sock_to_tunnel(sk_tun);
	if (tunnel == NULL)
		goto abort;

	/* What header length is configured for this session? */
	hdr_len = pppol2tp_l2tp_header_len(session);

	/* Check that there's enough headroom in the skb to insert IP,
	 * UDP and L2TP and PPP headers. If not enough, expand it to
	 * make room. Note that a new skb (or a clone) is
	 * allocated. If we return an error from this point on, make
	 * sure we free the new skb but do not free the original skb
	 * since that is done by the caller for the error case.
	 */
	headroom = NET_SKB_PAD + sizeof(struct iphdr) +
		sizeof(struct udphdr) + hdr_len + sizeof(ppph);
	if (skb_cow_head(skb, headroom))
		goto abort;

	/* Setup PPP header */
	__skb_push(skb, sizeof(ppph));
	skb->data[0] = ppph[0];
	skb->data[1] = ppph[1];

	/* Setup L2TP header */
	pppol2tp_build_l2tp_header(session, __skb_push(skb, hdr_len));

	/* Setup UDP header */
	inet = inet_sk(sk_tun);
	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);
	uh->source = inet->sport;
	uh->dest = inet->dport;
	uh->len = htons(sizeof(struct udphdr) + hdr_len + sizeof(ppph) + data_len);
	uh->check = 0;

	/* *BROKEN* Calculate UDP checksum if configured to do so */
	if (sk_tun->sk_no_check != UDP_CSUM_NOXMIT)
		csum = udp_csum_outgoing(sk_tun, skb);

	/* Debug */
	if (session->send_seq)
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %d bytes, ns=%hu\n", session->name,
		       data_len, session->ns - 1);
	else
		PRINTK(session->debug, PPPOL2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %d bytes\n", session->name, data_len);

	if (session->debug & PPPOL2TP_MSG_DATA) {
		int i;
		unsigned char *datap = skb->data;

		printk(KERN_DEBUG "%s: xmit:", session->name);
		for (i = 0; i < data_len; i++) {
			printk(" %02X", *datap++);
			if (i == 31) {
				printk(" ...");
				break;
			}
		}
		printk("\n");
	}

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	nf_reset(skb);

	/* Get routing info from the tunnel socket */
	dst_release(skb->dst);
	skb->dst = sk_dst_get(sk_tun);
	skb_orphan(skb);
	skb->sk = sk_tun;

	/* Queue the packet to IP for output */
	len = skb->len;
	rc = ip_queue_xmit(skb, 1);

	/* Update stats */
	if (rc >= 0) {
		tunnel->stats.tx_packets++;
		tunnel->stats.tx_bytes += len;
		session->stats.tx_packets++;
		session->stats.tx_bytes += len;
	} else {
		tunnel->stats.tx_errors++;
		session->stats.tx_errors++;
	}

	return 1;

abort:
	/* Free the original skb */
	kfree_skb(skb);
	return 1;
}

/*****************************************************************************
 * Session (and tunnel control) socket create/destroy.
 *****************************************************************************/

/* When the tunnel UDP socket is closed, all the attached sockets need to go
 * too.
 */
static void pppol2tp_tunnel_closeall(struct pppol2tp_tunnel *tunnel)
{
	int hash;
	struct hlist_node *walk;
	struct hlist_node *tmp;
	struct pppol2tp_session *session;
	struct sock *sk;

	if (tunnel == NULL)
		BUG();

	PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
	       "%s: closing all sessions...\n", tunnel->name);

	write_lock(&tunnel->hlist_lock);
	for (hash = 0; hash < PPPOL2TP_HASH_SIZE; hash++) {
again:
		hlist_for_each_safe(walk, tmp, &tunnel->session_hlist[hash]) {
			struct sk_buff *skb;

			session = hlist_entry(walk, struct pppol2tp_session, hlist);

			sk = session->sock;

			PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
			       "%s: closing session\n", session->name);

			hlist_del_init(&session->hlist);

			/* Since we should hold the sock lock while
			 * doing any unbinding, we need to release the
			 * lock we're holding before taking that lock.
			 * Hold a reference to the sock so it doesn't
			 * disappear as we're jumping between locks.
			 */
			sock_hold(sk);
			write_unlock(&tunnel->hlist_lock);
			lock_sock(sk);

			if (sk->sk_state & (PPPOX_CONNECTED | PPPOX_BOUND)) {
				pppox_unbind_sock(sk);
				sk->sk_state = PPPOX_DEAD;
				sk->sk_state_change(sk);
			}

			/* Purge any queued data */
			skb_queue_purge(&sk->sk_receive_queue);
			skb_queue_purge(&sk->sk_write_queue);
			while ((skb = skb_dequeue(&session->reorder_q))) {
				kfree_skb(skb);
				sock_put(sk);
			}

			release_sock(sk);
			sock_put(sk);

			/* Now restart from the beginning of this hash
			 * chain.  We always remove a session from the
			 * list so we are guaranteed to make forward
			 * progress.
			 */
			write_lock(&tunnel->hlist_lock);
			goto again;
		}
	}
	write_unlock(&tunnel->hlist_lock);
}

/* Really kill the tunnel.
 * Come here only when all sessions have been cleared from the tunnel.
 */
static void pppol2tp_tunnel_free(struct pppol2tp_tunnel *tunnel)
{
	/* Remove from socket list */
	write_lock(&pppol2tp_tunnel_list_lock);
	list_del_init(&tunnel->list);
	write_unlock(&pppol2tp_tunnel_list_lock);

	atomic_dec(&pppol2tp_tunnel_count);
	kfree(tunnel);
}

/* Tunnel UDP socket destruct hook.
 * The tunnel context is deleted only when all session sockets have been
 * closed.
 */
static void pppol2tp_tunnel_destruct(struct sock *sk)
{
	struct pppol2tp_tunnel *tunnel;

	tunnel = pppol2tp_sock_to_tunnel(sk);
	if (tunnel == NULL)
		goto end;

	PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
	       "%s: closing...\n", tunnel->name);

	/* Close all sessions */
	pppol2tp_tunnel_closeall(tunnel);

	/* No longer an encapsulation socket. See net/ipv4/udp.c */
	(udp_sk(sk))->encap_type = 0;
	(udp_sk(sk))->encap_rcv = NULL;

	/* Remove hooks into tunnel socket */
	tunnel->sock = NULL;
	sk->sk_destruct = tunnel->old_sk_destruct;
	sk->sk_user_data = NULL;

	/* Call original (UDP) socket descructor */
	if (sk->sk_destruct != NULL)
		(*sk->sk_destruct)(sk);

	pppol2tp_tunnel_dec_refcount(tunnel);

end:
	return;
}

/* Really kill the session socket. (Called from sock_put() if
 * refcnt == 0.)
 */
static void pppol2tp_session_destruct(struct sock *sk)
{
	struct pppol2tp_session *session = NULL;

	if (sk->sk_user_data != NULL) {
		struct pppol2tp_tunnel *tunnel;

		session = pppol2tp_sock_to_session(sk);
		if (session == NULL)
			goto out;

		/* Don't use pppol2tp_sock_to_tunnel() here to
		 * get the tunnel context because the tunnel
		 * socket might have already been closed (its
		 * sk->sk_user_data will be NULL) so use the
		 * session's private tunnel ptr instead.
		 */
		tunnel = session->tunnel;
		if (tunnel != NULL) {
			BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);

			/* If session_id is zero, this is a null
			 * session context, which was created for a
			 * socket that is being used only to manage
			 * tunnels.
			 */
			if (session->tunnel_addr.s_session != 0) {
				/* Delete the session socket from the
				 * hash
				 */
				write_lock(&tunnel->hlist_lock);
				hlist_del_init(&session->hlist);
				write_unlock(&tunnel->hlist_lock);

				atomic_dec(&pppol2tp_session_count);
			}

			/* This will delete the tunnel context if this
			 * is the last session on the tunnel.
			 */
			session->tunnel = NULL;
			session->tunnel_sock = NULL;
			pppol2tp_tunnel_dec_refcount(tunnel);
		}
	}

	kfree(session);
out:
	return;
}

/* Called when the PPPoX socket (session) is closed.
 */
static int pppol2tp_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int error;

	if (!sk)
		return 0;

	error = -EBADF;
	lock_sock(sk);
	if (sock_flag(sk, SOCK_DEAD) != 0)
		goto error;

	pppox_unbind_sock(sk);

	/* Signal the death of the socket. */
	sk->sk_state = PPPOX_DEAD;
	sock_orphan(sk);
	sock->sk = NULL;

	/* Purge any queued data */
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);

	release_sock(sk);

	/* This will delete the session context via
	 * pppol2tp_session_destruct() if the socket's refcnt drops to
	 * zero.
	 */
	sock_put(sk);

	return 0;

error:
	release_sock(sk);
	return error;
}

/* Internal function to prepare a tunnel (UDP) socket to have PPPoX
 * sockets attached to it.
 */
static struct sock *pppol2tp_prepare_tunnel_socket(int fd, u16 tunnel_id,
						   int *error)
{
	int err;
	struct socket *sock = NULL;
	struct sock *sk;
	struct pppol2tp_tunnel *tunnel;
	struct sock *ret = NULL;

	/* Get the tunnel UDP socket from the fd, which was opened by
	 * the userspace L2TP daemon.
	 */
	err = -EBADF;
	sock = sockfd_lookup(fd, &err);
	if (!sock) {
		PRINTK(-1, PPPOL2TP_MSG_CONTROL, KERN_ERR,
		       "tunl %hu: sockfd_lookup(fd=%d) returned %d\n",
		       tunnel_id, fd, err);
		goto err;
	}

	sk = sock->sk;

	/* Quick sanity checks */
	err = -EPROTONOSUPPORT;
	if (sk->sk_protocol != IPPROTO_UDP) {
		PRINTK(-1, PPPOL2TP_MSG_CONTROL, KERN_ERR,
		       "tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
		       tunnel_id, fd, sk->sk_protocol, IPPROTO_UDP);
		goto err;
	}
	err = -EAFNOSUPPORT;
	if (sock->ops->family != AF_INET) {
		PRINTK(-1, PPPOL2TP_MSG_CONTROL, KERN_ERR,
		       "tunl %hu: fd %d wrong family, got %d, expected %d\n",
		       tunnel_id, fd, sock->ops->family, AF_INET);
		goto err;
	}

	err = -ENOTCONN;

	/* Check if this socket has already been prepped */
	tunnel = (struct pppol2tp_tunnel *)sk->sk_user_data;
	if (tunnel != NULL) {
		/* User-data field already set */
		err = -EBUSY;
		BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);

		/* This socket has already been prepped */
		ret = tunnel->sock;
		goto out;
	}

	/* This socket is available and needs prepping. Create a new tunnel
	 * context and init it.
	 */
	sk->sk_user_data = tunnel = kzalloc(sizeof(struct pppol2tp_tunnel), GFP_KERNEL);
	if (sk->sk_user_data == NULL) {
		err = -ENOMEM;
		goto err;
	}

	tunnel->magic = L2TP_TUNNEL_MAGIC;
	sprintf(&tunnel->name[0], "tunl %hu", tunnel_id);

	tunnel->stats.tunnel_id = tunnel_id;
	tunnel->debug = PPPOL2TP_DEFAULT_DEBUG_FLAGS;

	/* Hook on the tunnel socket destructor so that we can cleanup
	 * if the tunnel socket goes away.
	 */
	tunnel->old_sk_destruct = sk->sk_destruct;
	sk->sk_destruct = &pppol2tp_tunnel_destruct;

	tunnel->sock = sk;
	sk->sk_allocation = GFP_ATOMIC;

	/* Misc init */
	rwlock_init(&tunnel->hlist_lock);

	/* Add tunnel to our list */
	INIT_LIST_HEAD(&tunnel->list);
	write_lock(&pppol2tp_tunnel_list_lock);
	list_add(&tunnel->list, &pppol2tp_tunnel_list);
	write_unlock(&pppol2tp_tunnel_list_lock);
	atomic_inc(&pppol2tp_tunnel_count);

	/* Bump the reference count. The tunnel context is deleted
	 * only when this drops to zero.
	 */
	pppol2tp_tunnel_inc_refcount(tunnel);

	/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
	(udp_sk(sk))->encap_type = UDP_ENCAP_L2TPINUDP;
	(udp_sk(sk))->encap_rcv = pppol2tp_udp_encap_recv;

	ret = tunnel->sock;

	*error = 0;
out:
	if (sock)
		sockfd_put(sock);

	return ret;

err:
	*error = err;
	goto out;
}

static struct proto pppol2tp_sk_proto = {
	.name	  = "PPPOL2TP",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

/* socket() handler. Initialize a new struct sock.
 */
static int pppol2tp_create(struct net *net, struct socket *sock)
{
	int error = -ENOMEM;
	struct sock *sk;

	sk = sk_alloc(net, PF_PPPOX, GFP_KERNEL, &pppol2tp_sk_proto);
	if (!sk)
		goto out;

	sock_init_data(sock, sk);

	sock->state  = SS_UNCONNECTED;
	sock->ops    = &pppol2tp_ops;

	sk->sk_backlog_rcv = pppol2tp_recv_core;
	sk->sk_protocol	   = PX_PROTO_OL2TP;
	sk->sk_family	   = PF_PPPOX;
	sk->sk_state	   = PPPOX_NONE;
	sk->sk_type	   = SOCK_STREAM;
	sk->sk_destruct	   = pppol2tp_session_destruct;

	error = 0;

out:
	return error;
}

/* connect() handler. Attach a PPPoX socket to a tunnel UDP socket
 */
static int pppol2tp_connect(struct socket *sock, struct sockaddr *uservaddr,
			    int sockaddr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_pppol2tp *sp = (struct sockaddr_pppol2tp *) uservaddr;
	struct pppox_sock *po = pppox_sk(sk);
	struct sock *tunnel_sock = NULL;
	struct pppol2tp_session *session = NULL;
	struct pppol2tp_tunnel *tunnel;
	struct dst_entry *dst;
	int error = 0;

	lock_sock(sk);

	error = -EINVAL;
	if (sp->sa_protocol != PX_PROTO_OL2TP)
		goto end;

	/* Check for already bound sockets */
	error = -EBUSY;
	if (sk->sk_state & PPPOX_CONNECTED)
		goto end;

	/* We don't supporting rebinding anyway */
	error = -EALREADY;
	if (sk->sk_user_data)
		goto end; /* socket is already attached */

	/* Don't bind if s_tunnel is 0 */
	error = -EINVAL;
	if (sp->pppol2tp.s_tunnel == 0)
		goto end;

	/* Special case: prepare tunnel socket if s_session and
	 * d_session is 0. Otherwise look up tunnel using supplied
	 * tunnel id.
	 */
	if ((sp->pppol2tp.s_session == 0) && (sp->pppol2tp.d_session == 0)) {
		tunnel_sock = pppol2tp_prepare_tunnel_socket(sp->pppol2tp.fd,
							     sp->pppol2tp.s_tunnel,
							     &error);
		if (tunnel_sock == NULL)
			goto end;

		tunnel = tunnel_sock->sk_user_data;
	} else {
		tunnel = pppol2tp_tunnel_find(sp->pppol2tp.s_tunnel);

		/* Error if we can't find the tunnel */
		error = -ENOENT;
		if (tunnel == NULL)
			goto end;

		tunnel_sock = tunnel->sock;
	}

	/* Check that this session doesn't already exist */
	error = -EEXIST;
	session = pppol2tp_session_find(tunnel, sp->pppol2tp.s_session);
	if (session != NULL)
		goto end;

	/* Allocate and initialize a new session context. */
	session = kzalloc(sizeof(struct pppol2tp_session), GFP_KERNEL);
	if (session == NULL) {
		error = -ENOMEM;
		goto end;
	}

	skb_queue_head_init(&session->reorder_q);

	session->magic	     = L2TP_SESSION_MAGIC;
	session->owner	     = current->pid;
	session->sock	     = sk;
	session->tunnel	     = tunnel;
	session->tunnel_sock = tunnel_sock;
	session->tunnel_addr = sp->pppol2tp;
	sprintf(&session->name[0], "sess %hu/%hu",
		session->tunnel_addr.s_tunnel,
		session->tunnel_addr.s_session);

	session->stats.tunnel_id  = session->tunnel_addr.s_tunnel;
	session->stats.session_id = session->tunnel_addr.s_session;

	INIT_HLIST_NODE(&session->hlist);

	/* Inherit debug options from tunnel */
	session->debug = tunnel->debug;

	/* Default MTU must allow space for UDP/L2TP/PPP
	 * headers.
	 */
	session->mtu = session->mru = 1500 - PPPOL2TP_HEADER_OVERHEAD;

	/* If PMTU discovery was enabled, use the MTU that was discovered */
	dst = sk_dst_get(sk);
	if (dst != NULL) {
		u32 pmtu = dst_mtu(__sk_dst_get(sk));
		if (pmtu != 0)
			session->mtu = session->mru = pmtu -
				PPPOL2TP_HEADER_OVERHEAD;
		dst_release(dst);
	}

	/* Special case: if source & dest session_id == 0x0000, this socket is
	 * being created to manage the tunnel. Don't add the session to the
	 * session hash list, just set up the internal context for use by
	 * ioctl() and sockopt() handlers.
	 */
	if ((session->tunnel_addr.s_session == 0) &&
	    (session->tunnel_addr.d_session == 0)) {
		error = 0;
		sk->sk_user_data = session;
		goto out_no_ppp;
	}

	/* Get tunnel context from the tunnel socket */
	tunnel = pppol2tp_sock_to_tunnel(tunnel_sock);
	if (tunnel == NULL) {
		error = -EBADF;
		goto end;
	}

	/* Right now, because we don't have a way to push the incoming skb's
	 * straight through the UDP layer, the only header we need to worry
	 * about is the L2TP header. This size is different depending on
	 * whether sequence numbers are enabled for the data channel.
	 */
	po->chan.hdrlen = PPPOL2TP_L2TP_HDR_SIZE_NOSEQ;

	po->chan.private = sk;
	po->chan.ops	 = &pppol2tp_chan_ops;
	po->chan.mtu	 = session->mtu;

	error = ppp_register_channel(&po->chan);
	if (error)
		goto end;

	/* This is how we get the session context from the socket. */
	sk->sk_user_data = session;

	/* Add session to the tunnel's hash list */
	write_lock(&tunnel->hlist_lock);
	hlist_add_head(&session->hlist,
		       pppol2tp_session_id_hash(tunnel,
						session->tunnel_addr.s_session));
	write_unlock(&tunnel->hlist_lock);

	atomic_inc(&pppol2tp_session_count);

out_no_ppp:
	pppol2tp_tunnel_inc_refcount(tunnel);
	sk->sk_state = PPPOX_CONNECTED;
	PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
	       "%s: created\n", session->name);

end:
	release_sock(sk);

	if (error != 0)
		PRINTK(session ? session->debug : -1, PPPOL2TP_MSG_CONTROL, KERN_WARNING,
		       "%s: connect failed: %d\n", session->name, error);

	return error;
}

/* getname() support.
 */
static int pppol2tp_getname(struct socket *sock, struct sockaddr *uaddr,
			    int *usockaddr_len, int peer)
{
	int len = sizeof(struct sockaddr_pppol2tp);
	struct sockaddr_pppol2tp sp;
	int error = 0;
	struct pppol2tp_session *session;

	error = -ENOTCONN;
	if (sock->sk->sk_state != PPPOX_CONNECTED)
		goto end;

	session = pppol2tp_sock_to_session(sock->sk);
	if (session == NULL) {
		error = -EBADF;
		goto end;
	}

	sp.sa_family	= AF_PPPOX;
	sp.sa_protocol	= PX_PROTO_OL2TP;
	memcpy(&sp.pppol2tp, &session->tunnel_addr,
	       sizeof(struct pppol2tp_addr));

	memcpy(uaddr, &sp, len);

	*usockaddr_len = len;

	error = 0;

end:
	return error;
}

/****************************************************************************
 * ioctl() handlers.
 *
 * The PPPoX socket is created for L2TP sessions: tunnels have their own UDP
 * sockets. However, in order to control kernel tunnel features, we allow
 * userspace to create a special "tunnel" PPPoX socket which is used for
 * control only.  Tunnel PPPoX sockets have session_id == 0 and simply allow
 * the user application to issue L2TP setsockopt(), getsockopt() and ioctl()
 * calls.
 ****************************************************************************/

/* Session ioctl helper.
 */
static int pppol2tp_session_ioctl(struct pppol2tp_session *session,
				  unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	int err = 0;
	struct sock *sk = session->sock;
	int val = (int) arg;

	PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_DEBUG,
	       "%s: pppol2tp_session_ioctl(cmd=%#x, arg=%#lx)\n",
	       session->name, cmd, arg);

	sock_hold(sk);

	switch (cmd) {
	case SIOCGIFMTU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (copy_from_user(&ifr, (void __user *) arg, sizeof(struct ifreq)))
			break;
		ifr.ifr_mtu = session->mtu;
		if (copy_to_user((void __user *) arg, &ifr, sizeof(struct ifreq)))
			break;

		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get mtu=%d\n", session->name, session->mtu);
		err = 0;
		break;

	case SIOCSIFMTU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (copy_from_user(&ifr, (void __user *) arg, sizeof(struct ifreq)))
			break;

		session->mtu = ifr.ifr_mtu;

		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set mtu=%d\n", session->name, session->mtu);
		err = 0;
		break;

	case PPPIOCGMRU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (put_user(session->mru, (int __user *) arg))
			break;

		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get mru=%d\n", session->name, session->mru);
		err = 0;
		break;

	case PPPIOCSMRU:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (get_user(val,(int __user *) arg))
			break;

		session->mru = val;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set mru=%d\n", session->name, session->mru);
		err = 0;
		break;

	case PPPIOCGFLAGS:
		err = -EFAULT;
		if (put_user(session->flags, (int __user *) arg))
			break;

		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get flags=%d\n", session->name, session->flags);
		err = 0;
		break;

	case PPPIOCSFLAGS:
		err = -EFAULT;
		if (get_user(val, (int __user *) arg))
			break;
		session->flags = val;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set flags=%d\n", session->name, session->flags);
		err = 0;
		break;

	case PPPIOCGL2TPSTATS:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		if (copy_to_user((void __user *) arg, &session->stats,
				 sizeof(session->stats)))
			break;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get L2TP stats\n", session->name);
		err = 0;
		break;

	default:
		err = -ENOSYS;
		break;
	}

	sock_put(sk);

	return err;
}

/* Tunnel ioctl helper.
 *
 * Note the special handling for PPPIOCGL2TPSTATS below. If the ioctl data
 * specifies a session_id, the session ioctl handler is called. This allows an
 * application to retrieve session stats via a tunnel socket.
 */
static int pppol2tp_tunnel_ioctl(struct pppol2tp_tunnel *tunnel,
				 unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct sock *sk = tunnel->sock;
	struct pppol2tp_ioc_stats stats_req;

	PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_DEBUG,
	       "%s: pppol2tp_tunnel_ioctl(cmd=%#x, arg=%#lx)\n", tunnel->name,
	       cmd, arg);

	sock_hold(sk);

	switch (cmd) {
	case PPPIOCGL2TPSTATS:
		err = -ENXIO;
		if (!(sk->sk_state & PPPOX_CONNECTED))
			break;

		if (copy_from_user(&stats_req, (void __user *) arg,
				   sizeof(stats_req))) {
			err = -EFAULT;
			break;
		}
		if (stats_req.session_id != 0) {
			/* resend to session ioctl handler */
			struct pppol2tp_session *session =
				pppol2tp_session_find(tunnel, stats_req.session_id);
			if (session != NULL)
				err = pppol2tp_session_ioctl(session, cmd, arg);
			else
				err = -EBADR;
			break;
		}
#ifdef CONFIG_XFRM
		tunnel->stats.using_ipsec = (sk->sk_policy[0] || sk->sk_policy[1]) ? 1 : 0;
#endif
		if (copy_to_user((void __user *) arg, &tunnel->stats,
				 sizeof(tunnel->stats))) {
			err = -EFAULT;
			break;
		}
		PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get L2TP stats\n", tunnel->name);
		err = 0;
		break;

	default:
		err = -ENOSYS;
		break;
	}

	sock_put(sk);

	return err;
}

/* Main ioctl() handler.
 * Dispatch to tunnel or session helpers depending on the socket.
 */
static int pppol2tp_ioctl(struct socket *sock, unsigned int cmd,
			  unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct pppol2tp_session *session;
	struct pppol2tp_tunnel *tunnel;
	int err;

	if (!sk)
		return 0;

	err = -EBADF;
	if (sock_flag(sk, SOCK_DEAD) != 0)
		goto end;

	err = -ENOTCONN;
	if ((sk->sk_user_data == NULL) ||
	    (!(sk->sk_state & (PPPOX_CONNECTED | PPPOX_BOUND))))
		goto end;

	/* Get session context from the socket */
	err = -EBADF;
	session = pppol2tp_sock_to_session(sk);
	if (session == NULL)
		goto end;

	/* Special case: if session's session_id is zero, treat ioctl as a
	 * tunnel ioctl
	 */
	if ((session->tunnel_addr.s_session == 0) &&
	    (session->tunnel_addr.d_session == 0)) {
		err = -EBADF;
		tunnel = pppol2tp_sock_to_tunnel(session->tunnel_sock);
		if (tunnel == NULL)
			goto end;

		err = pppol2tp_tunnel_ioctl(tunnel, cmd, arg);
		goto end;
	}

	err = pppol2tp_session_ioctl(session, cmd, arg);

end:
	return err;
}

/*****************************************************************************
 * setsockopt() / getsockopt() support.
 *
 * The PPPoX socket is created for L2TP sessions: tunnels have their own UDP
 * sockets. In order to control kernel tunnel features, we allow userspace to
 * create a special "tunnel" PPPoX socket which is used for control only.
 * Tunnel PPPoX sockets have session_id == 0 and simply allow the user
 * application to issue L2TP setsockopt(), getsockopt() and ioctl() calls.
 *****************************************************************************/

/* Tunnel setsockopt() helper.
 */
static int pppol2tp_tunnel_setsockopt(struct sock *sk,
				      struct pppol2tp_tunnel *tunnel,
				      int optname, int val)
{
	int err = 0;

	switch (optname) {
	case PPPOL2TP_SO_DEBUG:
		tunnel->debug = val;
		PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set debug=%x\n", tunnel->name, tunnel->debug);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	return err;
}

/* Session setsockopt helper.
 */
static int pppol2tp_session_setsockopt(struct sock *sk,
				       struct pppol2tp_session *session,
				       int optname, int val)
{
	int err = 0;

	switch (optname) {
	case PPPOL2TP_SO_RECVSEQ:
		if ((val != 0) && (val != 1)) {
			err = -EINVAL;
			break;
		}
		session->recv_seq = val ? -1 : 0;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set recv_seq=%d\n", session->name,
		       session->recv_seq);
		break;

	case PPPOL2TP_SO_SENDSEQ:
		if ((val != 0) && (val != 1)) {
			err = -EINVAL;
			break;
		}
		session->send_seq = val ? -1 : 0;
		{
			struct sock *ssk      = session->sock;
			struct pppox_sock *po = pppox_sk(ssk);
			po->chan.hdrlen = val ? PPPOL2TP_L2TP_HDR_SIZE_SEQ :
				PPPOL2TP_L2TP_HDR_SIZE_NOSEQ;
		}
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set send_seq=%d\n", session->name, session->send_seq);
		break;

	case PPPOL2TP_SO_LNSMODE:
		if ((val != 0) && (val != 1)) {
			err = -EINVAL;
			break;
		}
		session->lns_mode = val ? -1 : 0;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set lns_mode=%d\n", session->name,
		       session->lns_mode);
		break;

	case PPPOL2TP_SO_DEBUG:
		session->debug = val;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set debug=%x\n", session->name, session->debug);
		break;

	case PPPOL2TP_SO_REORDERTO:
		session->reorder_timeout = msecs_to_jiffies(val);
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: set reorder_timeout=%d\n", session->name,
		       session->reorder_timeout);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	return err;
}

/* Main setsockopt() entry point.
 * Does API checks, then calls either the tunnel or session setsockopt
 * handler, according to whether the PPPoL2TP socket is a for a regular
 * session or the special tunnel type.
 */
static int pppol2tp_setsockopt(struct socket *sock, int level, int optname,
			       char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct pppol2tp_session *session = sk->sk_user_data;
	struct pppol2tp_tunnel *tunnel;
	int val;
	int err;

	if (level != SOL_PPPOL2TP)
		return udp_prot.setsockopt(sk, level, optname, optval, optlen);

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	err = -ENOTCONN;
	if (sk->sk_user_data == NULL)
		goto end;

	/* Get session context from the socket */
	err = -EBADF;
	session = pppol2tp_sock_to_session(sk);
	if (session == NULL)
		goto end;

	/* Special case: if session_id == 0x0000, treat as operation on tunnel
	 */
	if ((session->tunnel_addr.s_session == 0) &&
	    (session->tunnel_addr.d_session == 0)) {
		err = -EBADF;
		tunnel = pppol2tp_sock_to_tunnel(session->tunnel_sock);
		if (tunnel == NULL)
			goto end;

		err = pppol2tp_tunnel_setsockopt(sk, tunnel, optname, val);
	} else
		err = pppol2tp_session_setsockopt(sk, session, optname, val);

	err = 0;

end:
	return err;
}

/* Tunnel getsockopt helper. Called with sock locked.
 */
static int pppol2tp_tunnel_getsockopt(struct sock *sk,
				      struct pppol2tp_tunnel *tunnel,
				      int optname, int *val)
{
	int err = 0;

	switch (optname) {
	case PPPOL2TP_SO_DEBUG:
		*val = tunnel->debug;
		PRINTK(tunnel->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get debug=%x\n", tunnel->name, tunnel->debug);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	return err;
}

/* Session getsockopt helper. Called with sock locked.
 */
static int pppol2tp_session_getsockopt(struct sock *sk,
				       struct pppol2tp_session *session,
				       int optname, int *val)
{
	int err = 0;

	switch (optname) {
	case PPPOL2TP_SO_RECVSEQ:
		*val = session->recv_seq;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get recv_seq=%d\n", session->name, *val);
		break;

	case PPPOL2TP_SO_SENDSEQ:
		*val = session->send_seq;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get send_seq=%d\n", session->name, *val);
		break;

	case PPPOL2TP_SO_LNSMODE:
		*val = session->lns_mode;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get lns_mode=%d\n", session->name, *val);
		break;

	case PPPOL2TP_SO_DEBUG:
		*val = session->debug;
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get debug=%d\n", session->name, *val);
		break;

	case PPPOL2TP_SO_REORDERTO:
		*val = (int) jiffies_to_msecs(session->reorder_timeout);
		PRINTK(session->debug, PPPOL2TP_MSG_CONTROL, KERN_INFO,
		       "%s: get reorder_timeout=%d\n", session->name, *val);
		break;

	default:
		err = -ENOPROTOOPT;
	}

	return err;
}

/* Main getsockopt() entry point.
 * Does API checks, then calls either the tunnel or session getsockopt
 * handler, according to whether the PPPoX socket is a for a regular session
 * or the special tunnel type.
 */
static int pppol2tp_getsockopt(struct socket *sock, int level,
			       int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct pppol2tp_session *session = sk->sk_user_data;
	struct pppol2tp_tunnel *tunnel;
	int val, len;
	int err;

	if (level != SOL_PPPOL2TP)
		return udp_prot.getsockopt(sk, level, optname, optval, optlen);

	if (get_user(len, (int __user *) optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));

	if (len < 0)
		return -EINVAL;

	err = -ENOTCONN;
	if (sk->sk_user_data == NULL)
		goto end;

	/* Get the session context */
	err = -EBADF;
	session = pppol2tp_sock_to_session(sk);
	if (session == NULL)
		goto end;

	/* Special case: if session_id == 0x0000, treat as operation on tunnel */
	if ((session->tunnel_addr.s_session == 0) &&
	    (session->tunnel_addr.d_session == 0)) {
		err = -EBADF;
		tunnel = pppol2tp_sock_to_tunnel(session->tunnel_sock);
		if (tunnel == NULL)
			goto end;

		err = pppol2tp_tunnel_getsockopt(sk, tunnel, optname, &val);
	} else
		err = pppol2tp_session_getsockopt(sk, session, optname, &val);

	err = -EFAULT;
	if (put_user(len, (int __user *) optlen))
		goto end;

	if (copy_to_user((void __user *) optval, &val, len))
		goto end;

	err = 0;
end:
	return err;
}

/*****************************************************************************
 * /proc filesystem for debug
 *****************************************************************************/

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>

struct pppol2tp_seq_data {
	struct pppol2tp_tunnel *tunnel; /* current tunnel */
	struct pppol2tp_session *session; /* NULL means get first session in tunnel */
};

static struct pppol2tp_session *next_session(struct pppol2tp_tunnel *tunnel, struct pppol2tp_session *curr)
{
	struct pppol2tp_session *session = NULL;
	struct hlist_node *walk;
	int found = 0;
	int next = 0;
	int i;

	read_lock(&tunnel->hlist_lock);
	for (i = 0; i < PPPOL2TP_HASH_SIZE; i++) {
		hlist_for_each_entry(session, walk, &tunnel->session_hlist[i], hlist) {
			if (curr == NULL) {
				found = 1;
				goto out;
			}
			if (session == curr) {
				next = 1;
				continue;
			}
			if (next) {
				found = 1;
				goto out;
			}
		}
	}
out:
	read_unlock(&tunnel->hlist_lock);
	if (!found)
		session = NULL;

	return session;
}

static struct pppol2tp_tunnel *next_tunnel(struct pppol2tp_tunnel *curr)
{
	struct pppol2tp_tunnel *tunnel = NULL;

	read_lock(&pppol2tp_tunnel_list_lock);
	if (list_is_last(&curr->list, &pppol2tp_tunnel_list)) {
		goto out;
	}
	tunnel = list_entry(curr->list.next, struct pppol2tp_tunnel, list);
out:
	read_unlock(&pppol2tp_tunnel_list_lock);

	return tunnel;
}

static void *pppol2tp_seq_start(struct seq_file *m, loff_t *offs)
{
	struct pppol2tp_seq_data *pd = SEQ_START_TOKEN;
	loff_t pos = *offs;

	if (!pos)
		goto out;

	BUG_ON(m->private == NULL);
	pd = m->private;

	if (pd->tunnel == NULL) {
		if (!list_empty(&pppol2tp_tunnel_list))
			pd->tunnel = list_entry(pppol2tp_tunnel_list.next, struct pppol2tp_tunnel, list);
	} else {
		pd->session = next_session(pd->tunnel, pd->session);
		if (pd->session == NULL) {
			pd->tunnel = next_tunnel(pd->tunnel);
		}
	}

	/* NULL tunnel and session indicates end of list */
	if ((pd->tunnel == NULL) && (pd->session == NULL))
		pd = NULL;

out:
	return pd;
}

static void *pppol2tp_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void pppol2tp_seq_stop(struct seq_file *p, void *v)
{
	/* nothing to do */
}

static void pppol2tp_seq_tunnel_show(struct seq_file *m, void *v)
{
	struct pppol2tp_tunnel *tunnel = v;

	seq_printf(m, "\nTUNNEL '%s', %c %d\n",
		   tunnel->name,
		   (tunnel == tunnel->sock->sk_user_data) ? 'Y':'N',
		   atomic_read(&tunnel->ref_count) - 1);
	seq_printf(m, " %08x %llu/%llu/%llu %llu/%llu/%llu\n",
		   tunnel->debug,
		   (unsigned long long)tunnel->stats.tx_packets,
		   (unsigned long long)tunnel->stats.tx_bytes,
		   (unsigned long long)tunnel->stats.tx_errors,
		   (unsigned long long)tunnel->stats.rx_packets,
		   (unsigned long long)tunnel->stats.rx_bytes,
		   (unsigned long long)tunnel->stats.rx_errors);
}

static void pppol2tp_seq_session_show(struct seq_file *m, void *v)
{
	struct pppol2tp_session *session = v;

	seq_printf(m, "  SESSION '%s' %08X/%d %04X/%04X -> "
		   "%04X/%04X %d %c\n",
		   session->name,
		   ntohl(session->tunnel_addr.addr.sin_addr.s_addr),
		   ntohs(session->tunnel_addr.addr.sin_port),
		   session->tunnel_addr.s_tunnel,
		   session->tunnel_addr.s_session,
		   session->tunnel_addr.d_tunnel,
		   session->tunnel_addr.d_session,
		   session->sock->sk_state,
		   (session == session->sock->sk_user_data) ?
		   'Y' : 'N');
	seq_printf(m, "   %d/%d/%c/%c/%s %08x %u\n",
		   session->mtu, session->mru,
		   session->recv_seq ? 'R' : '-',
		   session->send_seq ? 'S' : '-',
		   session->lns_mode ? "LNS" : "LAC",
		   session->debug,
		   jiffies_to_msecs(session->reorder_timeout));
	seq_printf(m, "   %hu/%hu %llu/%llu/%llu %llu/%llu/%llu\n",
		   session->nr, session->ns,
		   (unsigned long long)session->stats.tx_packets,
		   (unsigned long long)session->stats.tx_bytes,
		   (unsigned long long)session->stats.tx_errors,
		   (unsigned long long)session->stats.rx_packets,
		   (unsigned long long)session->stats.rx_bytes,
		   (unsigned long long)session->stats.rx_errors);
}

static int pppol2tp_seq_show(struct seq_file *m, void *v)
{
	struct pppol2tp_seq_data *pd = v;

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "PPPoL2TP driver info, " PPPOL2TP_DRV_VERSION "\n");
		seq_puts(m, "TUNNEL name, user-data-ok session-count\n");
		seq_puts(m, " debug tx-pkts/bytes/errs rx-pkts/bytes/errs\n");
		seq_puts(m, "  SESSION name, addr/port src-tid/sid "
			 "dest-tid/sid state user-data-ok\n");
		seq_puts(m, "   mtu/mru/rcvseq/sendseq/lns debug reorderto\n");
		seq_puts(m, "   nr/ns tx-pkts/bytes/errs rx-pkts/bytes/errs\n");
		goto out;
	}

	/* Show the tunnel or session context.
	 */
	if (pd->session == NULL)
		pppol2tp_seq_tunnel_show(m, pd->tunnel);
	else
		pppol2tp_seq_session_show(m, pd->session);

out:
	return 0;
}

static struct seq_operations pppol2tp_seq_ops = {
	.start		= pppol2tp_seq_start,
	.next		= pppol2tp_seq_next,
	.stop		= pppol2tp_seq_stop,
	.show		= pppol2tp_seq_show,
};

/* Called when our /proc file is opened. We allocate data for use when
 * iterating our tunnel / session contexts and store it in the private
 * data of the seq_file.
 */
static int pppol2tp_proc_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	struct pppol2tp_seq_data *pd;
	int ret = 0;

	ret = seq_open(file, &pppol2tp_seq_ops);
	if (ret < 0)
		goto out;

	m = file->private_data;

	/* Allocate and fill our proc_data for access later */
	ret = -ENOMEM;
	m->private = kzalloc(sizeof(struct pppol2tp_seq_data), GFP_KERNEL);
	if (m->private == NULL)
		goto out;

	pd = m->private;
	ret = 0;

out:
	return ret;
}

/* Called when /proc file access completes.
 */
static int pppol2tp_proc_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;

	kfree(m->private);
	m->private = NULL;

	return seq_release(inode, file);
}

static struct file_operations pppol2tp_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pppol2tp_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= pppol2tp_proc_release,
};

static struct proc_dir_entry *pppol2tp_proc;

#endif /* CONFIG_PROC_FS */

/*****************************************************************************
 * Init and cleanup
 *****************************************************************************/

static struct proto_ops pppol2tp_ops = {
	.family		= AF_PPPOX,
	.owner		= THIS_MODULE,
	.release	= pppol2tp_release,
	.bind		= sock_no_bind,
	.connect	= pppol2tp_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= pppol2tp_getname,
	.poll		= datagram_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= pppol2tp_setsockopt,
	.getsockopt	= pppol2tp_getsockopt,
	.sendmsg	= pppol2tp_sendmsg,
	.recvmsg	= pppol2tp_recvmsg,
	.mmap		= sock_no_mmap,
	.ioctl		= pppox_ioctl,
};

static struct pppox_proto pppol2tp_proto = {
	.create		= pppol2tp_create,
	.ioctl		= pppol2tp_ioctl
};

static int __init pppol2tp_init(void)
{
	int err;

	err = proto_register(&pppol2tp_sk_proto, 0);
	if (err)
		goto out;
	err = register_pppox_proto(PX_PROTO_OL2TP, &pppol2tp_proto);
	if (err)
		goto out_unregister_pppol2tp_proto;

#ifdef CONFIG_PROC_FS
	pppol2tp_proc = create_proc_entry("pppol2tp", 0, init_net.proc_net);
	if (!pppol2tp_proc) {
		err = -ENOMEM;
		goto out_unregister_pppox_proto;
	}
	pppol2tp_proc->proc_fops = &pppol2tp_proc_fops;
#endif /* CONFIG_PROC_FS */
	printk(KERN_INFO "PPPoL2TP kernel driver, %s\n",
	       PPPOL2TP_DRV_VERSION);

out:
	return err;
#ifdef CONFIG_PROC_FS
out_unregister_pppox_proto:
	unregister_pppox_proto(PX_PROTO_OL2TP);
#endif
out_unregister_pppol2tp_proto:
	proto_unregister(&pppol2tp_sk_proto);
	goto out;
}

static void __exit pppol2tp_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OL2TP);

#ifdef CONFIG_PROC_FS
	remove_proc_entry("pppol2tp", init_net.proc_net);
#endif
	proto_unregister(&pppol2tp_sk_proto);
}

module_init(pppol2tp_init);
module_exit(pppol2tp_exit);

MODULE_AUTHOR("Martijn van Oosterhout <kleptog@svana.org>, "
	      "James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("PPP over L2TP over UDP");
MODULE_LICENSE("GPL");
MODULE_VERSION(PPPOL2TP_DRV_VERSION);
