// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  TUN - Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2002 Maxim Krasnyansky <maxk@qualcomm.com>
 *
 *  $Id: tun.c,v 1.15 2002/03/01 02:44:24 maxk Exp $
 */

/*
 *  Changes:
 *
 *  Mike Kershaw <dragorn@kismetwireless.net> 2005/08/14
 *    Add TUNSETLINK ioctl to set the link encapsulation
 *
 *  Mark Smith <markzzzsmith@yahoo.com.au>
 *    Use eth_random_addr() for tap MAC address.
 *
 *  Harald Roelle <harald.roelle@ifi.lmu.de>  2004/04/20
 *    Fixes in packet dropping, queue length setting and queue wakeup.
 *    Increased default tx queue length.
 *    Added ethtool API.
 *    Minor cleanups
 *
 *  Daniel Podlejski <underley@underley.eu.org>
 *    Modifications for 2.3.99-pre5 kernel.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"tun"
#define DRV_VERSION	"1.6"
#define DRV_DESCRIPTION	"Universal TUN/TAP device driver"
#define DRV_COPYRIGHT	"(C) 1999-2004 Max Krasnyansky <maxk@qualcomm.com>"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/miscdevice.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/compat.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/nsproxy.h>
#include <linux/virtio_net.h>
#include <linux/rcupdate.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/sock.h>
#include <net/xdp.h>
#include <net/ip_tunnels.h>
#include <linux/seq_file.h>
#include <linux/uio.h>
#include <linux/skb_array.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/mutex.h>
#include <linux/ieee802154.h>
#include <linux/if_ltalk.h>
#include <uapi/linux/if_fddi.h>
#include <uapi/linux/if_hippi.h>
#include <uapi/linux/if_fc.h>
#include <net/ax25.h>
#include <net/rose.h>
#include <net/6lowpan.h>

#include <linux/uaccess.h>
#include <linux/proc_fs.h>

static void tun_default_link_ksettings(struct net_device *dev,
				       struct ethtool_link_ksettings *cmd);

#define TUN_RX_PAD (NET_IP_ALIGN + NET_SKB_PAD)

/* TUN device flags */

/* IFF_ATTACH_QUEUE is never stored in device flags,
 * overload it to mean fasync when stored there.
 */
#define TUN_FASYNC	IFF_ATTACH_QUEUE
/* High bits in flags field are unused. */
#define TUN_VNET_LE     0x80000000
#define TUN_VNET_BE     0x40000000

#define TUN_FEATURES (IFF_NO_PI | IFF_ONE_QUEUE | IFF_VNET_HDR | \
		      IFF_MULTI_QUEUE | IFF_NAPI | IFF_NAPI_FRAGS)

#define GOODCOPY_LEN 128

#define FLT_EXACT_COUNT 8
struct tap_filter {
	unsigned int    count;    /* Number of addrs. Zero means disabled */
	u32             mask[2];  /* Mask of the hashed addrs */
	unsigned char	addr[FLT_EXACT_COUNT][ETH_ALEN];
};

/* MAX_TAP_QUEUES 256 is chosen to allow rx/tx queues to be equal
 * to max number of VCPUs in guest. */
#define MAX_TAP_QUEUES 256
#define MAX_TAP_FLOWS  4096

#define TUN_FLOW_EXPIRE (3 * HZ)

/* A tun_file connects an open character device to a tuntap netdevice. It
 * also contains all socket related structures (except sock_fprog and tap_filter)
 * to serve as one transmit queue for tuntap device. The sock_fprog and
 * tap_filter were kept in tun_struct since they were used for filtering for the
 * netdevice not for a specific queue (at least I didn't see the requirement for
 * this).
 *
 * RCU usage:
 * The tun_file and tun_struct are loosely coupled, the pointer from one to the
 * other can only be read while rcu_read_lock or rtnl_lock is held.
 */
struct tun_file {
	struct sock sk;
	struct socket socket;
	struct tun_struct __rcu *tun;
	struct fasync_struct *fasync;
	/* only used for fasnyc */
	unsigned int flags;
	union {
		u16 queue_index;
		unsigned int ifindex;
	};
	struct napi_struct napi;
	bool napi_enabled;
	bool napi_frags_enabled;
	struct mutex napi_mutex;	/* Protects access to the above napi */
	struct list_head next;
	struct tun_struct *detached;
	struct ptr_ring tx_ring;
	struct xdp_rxq_info xdp_rxq;
};

struct tun_page {
	struct page *page;
	int count;
};

struct tun_flow_entry {
	struct hlist_node hash_link;
	struct rcu_head rcu;
	struct tun_struct *tun;

	u32 rxhash;
	u32 rps_rxhash;
	int queue_index;
	unsigned long updated ____cacheline_aligned_in_smp;
};

#define TUN_NUM_FLOW_ENTRIES 1024
#define TUN_MASK_FLOW_ENTRIES (TUN_NUM_FLOW_ENTRIES - 1)

struct tun_prog {
	struct rcu_head rcu;
	struct bpf_prog *prog;
};

/* Since the socket were moved to tun_file, to preserve the behavior of persist
 * device, socket filter, sndbuf and vnet header size were restore when the
 * file were attached to a persist device.
 */
struct tun_struct {
	struct tun_file __rcu	*tfiles[MAX_TAP_QUEUES];
	unsigned int            numqueues;
	unsigned int 		flags;
	kuid_t			owner;
	kgid_t			group;

	struct net_device	*dev;
	netdev_features_t	set_features;
#define TUN_USER_FEATURES (NETIF_F_HW_CSUM|NETIF_F_TSO_ECN|NETIF_F_TSO| \
			  NETIF_F_TSO6)

	int			align;
	int			vnet_hdr_sz;
	int			sndbuf;
	struct tap_filter	txflt;
	struct sock_fprog	fprog;
	/* protected by rtnl lock */
	bool			filter_attached;
	u32			msg_enable;
	spinlock_t lock;
	struct hlist_head flows[TUN_NUM_FLOW_ENTRIES];
	struct timer_list flow_gc_timer;
	unsigned long ageing_time;
	unsigned int numdisabled;
	struct list_head disabled;
	void *security;
	u32 flow_count;
	u32 rx_batched;
	atomic_long_t rx_frame_errors;
	struct bpf_prog __rcu *xdp_prog;
	struct tun_prog __rcu *steering_prog;
	struct tun_prog __rcu *filter_prog;
	struct ethtool_link_ksettings link_ksettings;
	/* init args */
	struct file *file;
	struct ifreq *ifr;
};

struct veth {
	__be16 h_vlan_proto;
	__be16 h_vlan_TCI;
};

static void tun_flow_init(struct tun_struct *tun);
static void tun_flow_uninit(struct tun_struct *tun);

static int tun_napi_receive(struct napi_struct *napi, int budget)
{
	struct tun_file *tfile = container_of(napi, struct tun_file, napi);
	struct sk_buff_head *queue = &tfile->sk.sk_write_queue;
	struct sk_buff_head process_queue;
	struct sk_buff *skb;
	int received = 0;

	__skb_queue_head_init(&process_queue);

	spin_lock(&queue->lock);
	skb_queue_splice_tail_init(queue, &process_queue);
	spin_unlock(&queue->lock);

	while (received < budget && (skb = __skb_dequeue(&process_queue))) {
		napi_gro_receive(napi, skb);
		++received;
	}

	if (!skb_queue_empty(&process_queue)) {
		spin_lock(&queue->lock);
		skb_queue_splice(&process_queue, queue);
		spin_unlock(&queue->lock);
	}

	return received;
}

static int tun_napi_poll(struct napi_struct *napi, int budget)
{
	unsigned int received;

	received = tun_napi_receive(napi, budget);

	if (received < budget)
		napi_complete_done(napi, received);

	return received;
}

static void tun_napi_init(struct tun_struct *tun, struct tun_file *tfile,
			  bool napi_en, bool napi_frags)
{
	tfile->napi_enabled = napi_en;
	tfile->napi_frags_enabled = napi_en && napi_frags;
	if (napi_en) {
		netif_tx_napi_add(tun->dev, &tfile->napi, tun_napi_poll,
				  NAPI_POLL_WEIGHT);
		napi_enable(&tfile->napi);
	}
}

static void tun_napi_disable(struct tun_file *tfile)
{
	if (tfile->napi_enabled)
		napi_disable(&tfile->napi);
}

static void tun_napi_del(struct tun_file *tfile)
{
	if (tfile->napi_enabled)
		netif_napi_del(&tfile->napi);
}

static bool tun_napi_frags_enabled(const struct tun_file *tfile)
{
	return tfile->napi_frags_enabled;
}

#ifdef CONFIG_TUN_VNET_CROSS_LE
static inline bool tun_legacy_is_little_endian(struct tun_struct *tun)
{
	return tun->flags & TUN_VNET_BE ? false :
		virtio_legacy_is_little_endian();
}

static long tun_get_vnet_be(struct tun_struct *tun, int __user *argp)
{
	int be = !!(tun->flags & TUN_VNET_BE);

	if (put_user(be, argp))
		return -EFAULT;

	return 0;
}

static long tun_set_vnet_be(struct tun_struct *tun, int __user *argp)
{
	int be;

	if (get_user(be, argp))
		return -EFAULT;

	if (be)
		tun->flags |= TUN_VNET_BE;
	else
		tun->flags &= ~TUN_VNET_BE;

	return 0;
}
#else
static inline bool tun_legacy_is_little_endian(struct tun_struct *tun)
{
	return virtio_legacy_is_little_endian();
}

static long tun_get_vnet_be(struct tun_struct *tun, int __user *argp)
{
	return -EINVAL;
}

static long tun_set_vnet_be(struct tun_struct *tun, int __user *argp)
{
	return -EINVAL;
}
#endif /* CONFIG_TUN_VNET_CROSS_LE */

static inline bool tun_is_little_endian(struct tun_struct *tun)
{
	return tun->flags & TUN_VNET_LE ||
		tun_legacy_is_little_endian(tun);
}

static inline u16 tun16_to_cpu(struct tun_struct *tun, __virtio16 val)
{
	return __virtio16_to_cpu(tun_is_little_endian(tun), val);
}

static inline __virtio16 cpu_to_tun16(struct tun_struct *tun, u16 val)
{
	return __cpu_to_virtio16(tun_is_little_endian(tun), val);
}

static inline u32 tun_hashfn(u32 rxhash)
{
	return rxhash & TUN_MASK_FLOW_ENTRIES;
}

static struct tun_flow_entry *tun_flow_find(struct hlist_head *head, u32 rxhash)
{
	struct tun_flow_entry *e;

	hlist_for_each_entry_rcu(e, head, hash_link) {
		if (e->rxhash == rxhash)
			return e;
	}
	return NULL;
}

static struct tun_flow_entry *tun_flow_create(struct tun_struct *tun,
					      struct hlist_head *head,
					      u32 rxhash, u16 queue_index)
{
	struct tun_flow_entry *e = kmalloc(sizeof(*e), GFP_ATOMIC);

	if (e) {
		netif_info(tun, tx_queued, tun->dev,
			   "create flow: hash %u index %u\n",
			   rxhash, queue_index);
		e->updated = jiffies;
		e->rxhash = rxhash;
		e->rps_rxhash = 0;
		e->queue_index = queue_index;
		e->tun = tun;
		hlist_add_head_rcu(&e->hash_link, head);
		++tun->flow_count;
	}
	return e;
}

static void tun_flow_delete(struct tun_struct *tun, struct tun_flow_entry *e)
{
	netif_info(tun, tx_queued, tun->dev, "delete flow: hash %u index %u\n",
		   e->rxhash, e->queue_index);
	hlist_del_rcu(&e->hash_link);
	kfree_rcu(e, rcu);
	--tun->flow_count;
}

static void tun_flow_flush(struct tun_struct *tun)
{
	int i;

	spin_lock_bh(&tun->lock);
	for (i = 0; i < TUN_NUM_FLOW_ENTRIES; i++) {
		struct tun_flow_entry *e;
		struct hlist_node *n;

		hlist_for_each_entry_safe(e, n, &tun->flows[i], hash_link)
			tun_flow_delete(tun, e);
	}
	spin_unlock_bh(&tun->lock);
}

static void tun_flow_delete_by_queue(struct tun_struct *tun, u16 queue_index)
{
	int i;

	spin_lock_bh(&tun->lock);
	for (i = 0; i < TUN_NUM_FLOW_ENTRIES; i++) {
		struct tun_flow_entry *e;
		struct hlist_node *n;

		hlist_for_each_entry_safe(e, n, &tun->flows[i], hash_link) {
			if (e->queue_index == queue_index)
				tun_flow_delete(tun, e);
		}
	}
	spin_unlock_bh(&tun->lock);
}

static void tun_flow_cleanup(struct timer_list *t)
{
	struct tun_struct *tun = from_timer(tun, t, flow_gc_timer);
	unsigned long delay = tun->ageing_time;
	unsigned long next_timer = jiffies + delay;
	unsigned long count = 0;
	int i;

	spin_lock(&tun->lock);
	for (i = 0; i < TUN_NUM_FLOW_ENTRIES; i++) {
		struct tun_flow_entry *e;
		struct hlist_node *n;

		hlist_for_each_entry_safe(e, n, &tun->flows[i], hash_link) {
			unsigned long this_timer;

			this_timer = e->updated + delay;
			if (time_before_eq(this_timer, jiffies)) {
				tun_flow_delete(tun, e);
				continue;
			}
			count++;
			if (time_before(this_timer, next_timer))
				next_timer = this_timer;
		}
	}

	if (count)
		mod_timer(&tun->flow_gc_timer, round_jiffies_up(next_timer));
	spin_unlock(&tun->lock);
}

static void tun_flow_update(struct tun_struct *tun, u32 rxhash,
			    struct tun_file *tfile)
{
	struct hlist_head *head;
	struct tun_flow_entry *e;
	unsigned long delay = tun->ageing_time;
	u16 queue_index = tfile->queue_index;

	head = &tun->flows[tun_hashfn(rxhash)];

	rcu_read_lock();

	e = tun_flow_find(head, rxhash);
	if (likely(e)) {
		/* TODO: keep queueing to old queue until it's empty? */
		if (READ_ONCE(e->queue_index) != queue_index)
			WRITE_ONCE(e->queue_index, queue_index);
		if (e->updated != jiffies)
			e->updated = jiffies;
		sock_rps_record_flow_hash(e->rps_rxhash);
	} else {
		spin_lock_bh(&tun->lock);
		if (!tun_flow_find(head, rxhash) &&
		    tun->flow_count < MAX_TAP_FLOWS)
			tun_flow_create(tun, head, rxhash, queue_index);

		if (!timer_pending(&tun->flow_gc_timer))
			mod_timer(&tun->flow_gc_timer,
				  round_jiffies_up(jiffies + delay));
		spin_unlock_bh(&tun->lock);
	}

	rcu_read_unlock();
}

/* Save the hash received in the stack receive path and update the
 * flow_hash table accordingly.
 */
static inline void tun_flow_save_rps_rxhash(struct tun_flow_entry *e, u32 hash)
{
	if (unlikely(e->rps_rxhash != hash))
		e->rps_rxhash = hash;
}

/* We try to identify a flow through its rxhash. The reason that
 * we do not check rxq no. is because some cards(e.g 82599), chooses
 * the rxq based on the txq where the last packet of the flow comes. As
 * the userspace application move between processors, we may get a
 * different rxq no. here.
 */
static u16 tun_automq_select_queue(struct tun_struct *tun, struct sk_buff *skb)
{
	struct tun_flow_entry *e;
	u32 txq = 0;
	u32 numqueues = 0;

	numqueues = READ_ONCE(tun->numqueues);

	txq = __skb_get_hash_symmetric(skb);
	e = tun_flow_find(&tun->flows[tun_hashfn(txq)], txq);
	if (e) {
		tun_flow_save_rps_rxhash(e, txq);
		txq = e->queue_index;
	} else {
		/* use multiply and shift instead of expensive divide */
		txq = ((u64)txq * numqueues) >> 32;
	}

	return txq;
}

static u16 tun_ebpf_select_queue(struct tun_struct *tun, struct sk_buff *skb)
{
	struct tun_prog *prog;
	u32 numqueues;
	u16 ret = 0;

	numqueues = READ_ONCE(tun->numqueues);
	if (!numqueues)
		return 0;

	prog = rcu_dereference(tun->steering_prog);
	if (prog)
		ret = bpf_prog_run_clear_cb(prog->prog, skb);

	return ret % numqueues;
}

static u16 tun_select_queue(struct net_device *dev, struct sk_buff *skb,
			    struct net_device *sb_dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	u16 ret;

	rcu_read_lock();
	if (rcu_dereference(tun->steering_prog))
		ret = tun_ebpf_select_queue(tun, skb);
	else
		ret = tun_automq_select_queue(tun, skb);
	rcu_read_unlock();

	return ret;
}

static inline bool tun_not_capable(struct tun_struct *tun)
{
	const struct cred *cred = current_cred();
	struct net *net = dev_net(tun->dev);

	return ((uid_valid(tun->owner) && !uid_eq(cred->euid, tun->owner)) ||
		  (gid_valid(tun->group) && !in_egroup_p(tun->group))) &&
		!ns_capable(net->user_ns, CAP_NET_ADMIN);
}

static void tun_set_real_num_queues(struct tun_struct *tun)
{
	netif_set_real_num_tx_queues(tun->dev, tun->numqueues);
	netif_set_real_num_rx_queues(tun->dev, tun->numqueues);
}

static void tun_disable_queue(struct tun_struct *tun, struct tun_file *tfile)
{
	tfile->detached = tun;
	list_add_tail(&tfile->next, &tun->disabled);
	++tun->numdisabled;
}

static struct tun_struct *tun_enable_queue(struct tun_file *tfile)
{
	struct tun_struct *tun = tfile->detached;

	tfile->detached = NULL;
	list_del_init(&tfile->next);
	--tun->numdisabled;
	return tun;
}

void tun_ptr_free(void *ptr)
{
	if (!ptr)
		return;
	if (tun_is_xdp_frame(ptr)) {
		struct xdp_frame *xdpf = tun_ptr_to_xdp(ptr);

		xdp_return_frame(xdpf);
	} else {
		__skb_array_destroy_skb(ptr);
	}
}
EXPORT_SYMBOL_GPL(tun_ptr_free);

static void tun_queue_purge(struct tun_file *tfile)
{
	void *ptr;

	while ((ptr = ptr_ring_consume(&tfile->tx_ring)) != NULL)
		tun_ptr_free(ptr);

	skb_queue_purge(&tfile->sk.sk_write_queue);
	skb_queue_purge(&tfile->sk.sk_error_queue);
}

static void __tun_detach(struct tun_file *tfile, bool clean)
{
	struct tun_file *ntfile;
	struct tun_struct *tun;

	tun = rtnl_dereference(tfile->tun);

	if (tun && clean) {
		tun_napi_disable(tfile);
		tun_napi_del(tfile);
	}

	if (tun && !tfile->detached) {
		u16 index = tfile->queue_index;
		BUG_ON(index >= tun->numqueues);

		rcu_assign_pointer(tun->tfiles[index],
				   tun->tfiles[tun->numqueues - 1]);
		ntfile = rtnl_dereference(tun->tfiles[index]);
		ntfile->queue_index = index;
		rcu_assign_pointer(tun->tfiles[tun->numqueues - 1],
				   NULL);

		--tun->numqueues;
		if (clean) {
			RCU_INIT_POINTER(tfile->tun, NULL);
			sock_put(&tfile->sk);
		} else
			tun_disable_queue(tun, tfile);

		synchronize_net();
		tun_flow_delete_by_queue(tun, tun->numqueues + 1);
		/* Drop read queue */
		tun_queue_purge(tfile);
		tun_set_real_num_queues(tun);
	} else if (tfile->detached && clean) {
		tun = tun_enable_queue(tfile);
		sock_put(&tfile->sk);
	}

	if (clean) {
		if (tun && tun->numqueues == 0 && tun->numdisabled == 0) {
			netif_carrier_off(tun->dev);

			if (!(tun->flags & IFF_PERSIST) &&
			    tun->dev->reg_state == NETREG_REGISTERED)
				unregister_netdevice(tun->dev);
		}
		if (tun)
			xdp_rxq_info_unreg(&tfile->xdp_rxq);
		ptr_ring_cleanup(&tfile->tx_ring, tun_ptr_free);
		sock_put(&tfile->sk);
	}
}

static void tun_detach(struct tun_file *tfile, bool clean)
{
	struct tun_struct *tun;
	struct net_device *dev;

	rtnl_lock();
	tun = rtnl_dereference(tfile->tun);
	dev = tun ? tun->dev : NULL;
	__tun_detach(tfile, clean);
	if (dev)
		netdev_state_change(dev);
	rtnl_unlock();
}

static void tun_detach_all(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	struct tun_file *tfile, *tmp;
	int i, n = tun->numqueues;

	for (i = 0; i < n; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		BUG_ON(!tfile);
		tun_napi_disable(tfile);
		tfile->socket.sk->sk_shutdown = RCV_SHUTDOWN;
		tfile->socket.sk->sk_data_ready(tfile->socket.sk);
		RCU_INIT_POINTER(tfile->tun, NULL);
		--tun->numqueues;
	}
	list_for_each_entry(tfile, &tun->disabled, next) {
		tfile->socket.sk->sk_shutdown = RCV_SHUTDOWN;
		tfile->socket.sk->sk_data_ready(tfile->socket.sk);
		RCU_INIT_POINTER(tfile->tun, NULL);
	}
	BUG_ON(tun->numqueues != 0);

	synchronize_net();
	for (i = 0; i < n; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		tun_napi_del(tfile);
		/* Drop read queue */
		tun_queue_purge(tfile);
		xdp_rxq_info_unreg(&tfile->xdp_rxq);
		sock_put(&tfile->sk);
	}
	list_for_each_entry_safe(tfile, tmp, &tun->disabled, next) {
		tun_enable_queue(tfile);
		tun_queue_purge(tfile);
		xdp_rxq_info_unreg(&tfile->xdp_rxq);
		sock_put(&tfile->sk);
	}
	BUG_ON(tun->numdisabled != 0);

	if (tun->flags & IFF_PERSIST)
		module_put(THIS_MODULE);
}

static int tun_attach(struct tun_struct *tun, struct file *file,
		      bool skip_filter, bool napi, bool napi_frags,
		      bool publish_tun)
{
	struct tun_file *tfile = file->private_data;
	struct net_device *dev = tun->dev;
	int err;

	err = security_tun_dev_attach(tfile->socket.sk, tun->security);
	if (err < 0)
		goto out;

	err = -EINVAL;
	if (rtnl_dereference(tfile->tun) && !tfile->detached)
		goto out;

	err = -EBUSY;
	if (!(tun->flags & IFF_MULTI_QUEUE) && tun->numqueues == 1)
		goto out;

	err = -E2BIG;
	if (!tfile->detached &&
	    tun->numqueues + tun->numdisabled == MAX_TAP_QUEUES)
		goto out;

	err = 0;

	/* Re-attach the filter to persist device */
	if (!skip_filter && (tun->filter_attached == true)) {
		lock_sock(tfile->socket.sk);
		err = sk_attach_filter(&tun->fprog, tfile->socket.sk);
		release_sock(tfile->socket.sk);
		if (!err)
			goto out;
	}

	if (!tfile->detached &&
	    ptr_ring_resize(&tfile->tx_ring, dev->tx_queue_len,
			    GFP_KERNEL, tun_ptr_free)) {
		err = -ENOMEM;
		goto out;
	}

	tfile->queue_index = tun->numqueues;
	tfile->socket.sk->sk_shutdown &= ~RCV_SHUTDOWN;

	if (tfile->detached) {
		/* Re-attach detached tfile, updating XDP queue_index */
		WARN_ON(!xdp_rxq_info_is_reg(&tfile->xdp_rxq));

		if (tfile->xdp_rxq.queue_index    != tfile->queue_index)
			tfile->xdp_rxq.queue_index = tfile->queue_index;
	} else {
		/* Setup XDP RX-queue info, for new tfile getting attached */
		err = xdp_rxq_info_reg(&tfile->xdp_rxq,
				       tun->dev, tfile->queue_index, 0);
		if (err < 0)
			goto out;
		err = xdp_rxq_info_reg_mem_model(&tfile->xdp_rxq,
						 MEM_TYPE_PAGE_SHARED, NULL);
		if (err < 0) {
			xdp_rxq_info_unreg(&tfile->xdp_rxq);
			goto out;
		}
		err = 0;
	}

	if (tfile->detached) {
		tun_enable_queue(tfile);
	} else {
		sock_hold(&tfile->sk);
		tun_napi_init(tun, tfile, napi, napi_frags);
	}

	if (rtnl_dereference(tun->xdp_prog))
		sock_set_flag(&tfile->sk, SOCK_XDP);

	/* device is allowed to go away first, so no need to hold extra
	 * refcnt.
	 */

	/* Publish tfile->tun and tun->tfiles only after we've fully
	 * initialized tfile; otherwise we risk using half-initialized
	 * object.
	 */
	if (publish_tun)
		rcu_assign_pointer(tfile->tun, tun);
	rcu_assign_pointer(tun->tfiles[tun->numqueues], tfile);
	tun->numqueues++;
	tun_set_real_num_queues(tun);
out:
	return err;
}

static struct tun_struct *tun_get(struct tun_file *tfile)
{
	struct tun_struct *tun;

	rcu_read_lock();
	tun = rcu_dereference(tfile->tun);
	if (tun)
		dev_hold(tun->dev);
	rcu_read_unlock();

	return tun;
}

static void tun_put(struct tun_struct *tun)
{
	dev_put(tun->dev);
}

/* TAP filtering */
static void addr_hash_set(u32 *mask, const u8 *addr)
{
	int n = ether_crc(ETH_ALEN, addr) >> 26;
	mask[n >> 5] |= (1 << (n & 31));
}

static unsigned int addr_hash_test(const u32 *mask, const u8 *addr)
{
	int n = ether_crc(ETH_ALEN, addr) >> 26;
	return mask[n >> 5] & (1 << (n & 31));
}

static int update_filter(struct tap_filter *filter, void __user *arg)
{
	struct { u8 u[ETH_ALEN]; } *addr;
	struct tun_filter uf;
	int err, alen, n, nexact;

	if (copy_from_user(&uf, arg, sizeof(uf)))
		return -EFAULT;

	if (!uf.count) {
		/* Disabled */
		filter->count = 0;
		return 0;
	}

	alen = ETH_ALEN * uf.count;
	addr = memdup_user(arg + sizeof(uf), alen);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	/* The filter is updated without holding any locks. Which is
	 * perfectly safe. We disable it first and in the worst
	 * case we'll accept a few undesired packets. */
	filter->count = 0;
	wmb();

	/* Use first set of addresses as an exact filter */
	for (n = 0; n < uf.count && n < FLT_EXACT_COUNT; n++)
		memcpy(filter->addr[n], addr[n].u, ETH_ALEN);

	nexact = n;

	/* Remaining multicast addresses are hashed,
	 * unicast will leave the filter disabled. */
	memset(filter->mask, 0, sizeof(filter->mask));
	for (; n < uf.count; n++) {
		if (!is_multicast_ether_addr(addr[n].u)) {
			err = 0; /* no filter */
			goto free_addr;
		}
		addr_hash_set(filter->mask, addr[n].u);
	}

	/* For ALLMULTI just set the mask to all ones.
	 * This overrides the mask populated above. */
	if ((uf.flags & TUN_FLT_ALLMULTI))
		memset(filter->mask, ~0, sizeof(filter->mask));

	/* Now enable the filter */
	wmb();
	filter->count = nexact;

	/* Return the number of exact filters */
	err = nexact;
free_addr:
	kfree(addr);
	return err;
}

/* Returns: 0 - drop, !=0 - accept */
static int run_filter(struct tap_filter *filter, const struct sk_buff *skb)
{
	/* Cannot use eth_hdr(skb) here because skb_mac_hdr() is incorrect
	 * at this point. */
	struct ethhdr *eh = (struct ethhdr *) skb->data;
	int i;

	/* Exact match */
	for (i = 0; i < filter->count; i++)
		if (ether_addr_equal(eh->h_dest, filter->addr[i]))
			return 1;

	/* Inexact match (multicast only) */
	if (is_multicast_ether_addr(eh->h_dest))
		return addr_hash_test(filter->mask, eh->h_dest);

	return 0;
}

/*
 * Checks whether the packet is accepted or not.
 * Returns: 0 - drop, !=0 - accept
 */
static int check_filter(struct tap_filter *filter, const struct sk_buff *skb)
{
	if (!filter->count)
		return 1;

	return run_filter(filter, skb);
}

/* Network device part of the driver */

static const struct ethtool_ops tun_ethtool_ops;

static int tun_net_init(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	struct ifreq *ifr = tun->ifr;
	int err;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	spin_lock_init(&tun->lock);

	err = security_tun_dev_alloc_security(&tun->security);
	if (err < 0) {
		free_percpu(dev->tstats);
		return err;
	}

	tun_flow_init(tun);

	dev->hw_features = NETIF_F_SG | NETIF_F_FRAGLIST |
			   TUN_USER_FEATURES | NETIF_F_HW_VLAN_CTAG_TX |
			   NETIF_F_HW_VLAN_STAG_TX;
	dev->features = dev->hw_features | NETIF_F_LLTX;
	dev->vlan_features = dev->features &
			     ~(NETIF_F_HW_VLAN_CTAG_TX |
			       NETIF_F_HW_VLAN_STAG_TX);

	tun->flags = (tun->flags & ~TUN_FEATURES) |
		      (ifr->ifr_flags & TUN_FEATURES);

	INIT_LIST_HEAD(&tun->disabled);
	err = tun_attach(tun, tun->file, false, ifr->ifr_flags & IFF_NAPI,
			 ifr->ifr_flags & IFF_NAPI_FRAGS, false);
	if (err < 0) {
		tun_flow_uninit(tun);
		security_tun_dev_free_security(tun->security);
		free_percpu(dev->tstats);
		return err;
	}
	return 0;
}

/* Net device detach from fd. */
static void tun_net_uninit(struct net_device *dev)
{
	tun_detach_all(dev);
}

/* Net device open. */
static int tun_net_open(struct net_device *dev)
{
	netif_tx_start_all_queues(dev);

	return 0;
}

/* Net device close. */
static int tun_net_close(struct net_device *dev)
{
	netif_tx_stop_all_queues(dev);
	return 0;
}

/* Net device start xmit */
static void tun_automq_xmit(struct tun_struct *tun, struct sk_buff *skb)
{
#ifdef CONFIG_RPS
	if (tun->numqueues == 1 && static_branch_unlikely(&rps_needed)) {
		/* Select queue was not called for the skbuff, so we extract the
		 * RPS hash and save it into the flow_table here.
		 */
		struct tun_flow_entry *e;
		__u32 rxhash;

		rxhash = __skb_get_hash_symmetric(skb);
		e = tun_flow_find(&tun->flows[tun_hashfn(rxhash)], rxhash);
		if (e)
			tun_flow_save_rps_rxhash(e, rxhash);
	}
#endif
}

static unsigned int run_ebpf_filter(struct tun_struct *tun,
				    struct sk_buff *skb,
				    int len)
{
	struct tun_prog *prog = rcu_dereference(tun->filter_prog);

	if (prog)
		len = bpf_prog_run_clear_cb(prog->prog, skb);

	return len;
}

/* Net device start xmit */
static netdev_tx_t tun_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	int txq = skb->queue_mapping;
	struct netdev_queue *queue;
	struct tun_file *tfile;
	int len = skb->len;

	rcu_read_lock();
	tfile = rcu_dereference(tun->tfiles[txq]);

	/* Drop packet if interface is not attached */
	if (!tfile)
		goto drop;

	if (!rcu_dereference(tun->steering_prog))
		tun_automq_xmit(tun, skb);

	netif_info(tun, tx_queued, tun->dev, "%s %d\n", __func__, skb->len);

	/* Drop if the filter does not like it.
	 * This is a noop if the filter is disabled.
	 * Filter can be enabled only for the TAP devices. */
	if (!check_filter(&tun->txflt, skb))
		goto drop;

	if (tfile->socket.sk->sk_filter &&
	    sk_filter(tfile->socket.sk, skb))
		goto drop;

	len = run_ebpf_filter(tun, skb, len);
	if (len == 0 || pskb_trim(skb, len))
		goto drop;

	if (unlikely(skb_orphan_frags_rx(skb, GFP_ATOMIC)))
		goto drop;

	skb_tx_timestamp(skb);

	/* Orphan the skb - required as we might hang on to it
	 * for indefinite time.
	 */
	skb_orphan(skb);

	nf_reset_ct(skb);

	if (ptr_ring_produce(&tfile->tx_ring, skb))
		goto drop;

	/* NETIF_F_LLTX requires to do our own update of trans_start */
	queue = netdev_get_tx_queue(dev, txq);
	queue->trans_start = jiffies;

	/* Notify and wake up reader process */
	if (tfile->flags & TUN_FASYNC)
		kill_fasync(&tfile->fasync, SIGIO, POLL_IN);
	tfile->socket.sk->sk_data_ready(tfile->socket.sk);

	rcu_read_unlock();
	return NETDEV_TX_OK;

drop:
	atomic_long_inc(&dev->tx_dropped);
	skb_tx_error(skb);
	kfree_skb(skb);
	rcu_read_unlock();
	return NET_XMIT_DROP;
}

static void tun_net_mclist(struct net_device *dev)
{
	/*
	 * This callback is supposed to deal with mc filter in
	 * _rx_ path and has nothing to do with the _tx_ path.
	 * In rx path we always accept everything userspace gives us.
	 */
}

static netdev_features_t tun_net_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct tun_struct *tun = netdev_priv(dev);

	return (features & tun->set_features) | (features & ~TUN_USER_FEATURES);
}

static void tun_set_headroom(struct net_device *dev, int new_hr)
{
	struct tun_struct *tun = netdev_priv(dev);

	if (new_hr < NET_SKB_PAD)
		new_hr = NET_SKB_PAD;

	tun->align = new_hr;
}

static void
tun_net_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct tun_struct *tun = netdev_priv(dev);

	dev_get_tstats64(dev, stats);

	stats->rx_frame_errors +=
		(unsigned long)atomic_long_read(&tun->rx_frame_errors);
}

static int tun_xdp_set(struct net_device *dev, struct bpf_prog *prog,
		       struct netlink_ext_ack *extack)
{
	struct tun_struct *tun = netdev_priv(dev);
	struct tun_file *tfile;
	struct bpf_prog *old_prog;
	int i;

	old_prog = rtnl_dereference(tun->xdp_prog);
	rcu_assign_pointer(tun->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	for (i = 0; i < tun->numqueues; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		if (prog)
			sock_set_flag(&tfile->sk, SOCK_XDP);
		else
			sock_reset_flag(&tfile->sk, SOCK_XDP);
	}
	list_for_each_entry(tfile, &tun->disabled, next) {
		if (prog)
			sock_set_flag(&tfile->sk, SOCK_XDP);
		else
			sock_reset_flag(&tfile->sk, SOCK_XDP);
	}

	return 0;
}

static int tun_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return tun_xdp_set(dev, xdp->prog, xdp->extack);
	default:
		return -EINVAL;
	}
}

static int tun_net_change_carrier(struct net_device *dev, bool new_carrier)
{
	if (new_carrier) {
		struct tun_struct *tun = netdev_priv(dev);

		if (!tun->numqueues)
			return -EPERM;

		netif_carrier_on(dev);
	} else {
		netif_carrier_off(dev);
	}
	return 0;
}

static const struct net_device_ops tun_netdev_ops = {
	.ndo_init		= tun_net_init,
	.ndo_uninit		= tun_net_uninit,
	.ndo_open		= tun_net_open,
	.ndo_stop		= tun_net_close,
	.ndo_start_xmit		= tun_net_xmit,
	.ndo_fix_features	= tun_net_fix_features,
	.ndo_select_queue	= tun_select_queue,
	.ndo_set_rx_headroom	= tun_set_headroom,
	.ndo_get_stats64	= tun_net_get_stats64,
	.ndo_change_carrier	= tun_net_change_carrier,
};

static void __tun_xdp_flush_tfile(struct tun_file *tfile)
{
	/* Notify and wake up reader process */
	if (tfile->flags & TUN_FASYNC)
		kill_fasync(&tfile->fasync, SIGIO, POLL_IN);
	tfile->socket.sk->sk_data_ready(tfile->socket.sk);
}

static int tun_xdp_xmit(struct net_device *dev, int n,
			struct xdp_frame **frames, u32 flags)
{
	struct tun_struct *tun = netdev_priv(dev);
	struct tun_file *tfile;
	u32 numqueues;
	int nxmit = 0;
	int i;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	rcu_read_lock();

resample:
	numqueues = READ_ONCE(tun->numqueues);
	if (!numqueues) {
		rcu_read_unlock();
		return -ENXIO; /* Caller will free/return all frames */
	}

	tfile = rcu_dereference(tun->tfiles[smp_processor_id() %
					    numqueues]);
	if (unlikely(!tfile))
		goto resample;

	spin_lock(&tfile->tx_ring.producer_lock);
	for (i = 0; i < n; i++) {
		struct xdp_frame *xdp = frames[i];
		/* Encode the XDP flag into lowest bit for consumer to differ
		 * XDP buffer from sk_buff.
		 */
		void *frame = tun_xdp_to_ptr(xdp);

		if (__ptr_ring_produce(&tfile->tx_ring, frame)) {
			atomic_long_inc(&dev->tx_dropped);
			break;
		}
		nxmit++;
	}
	spin_unlock(&tfile->tx_ring.producer_lock);

	if (flags & XDP_XMIT_FLUSH)
		__tun_xdp_flush_tfile(tfile);

	rcu_read_unlock();
	return nxmit;
}

static int tun_xdp_tx(struct net_device *dev, struct xdp_buff *xdp)
{
	struct xdp_frame *frame = xdp_convert_buff_to_frame(xdp);
	int nxmit;

	if (unlikely(!frame))
		return -EOVERFLOW;

	nxmit = tun_xdp_xmit(dev, 1, &frame, XDP_XMIT_FLUSH);
	if (!nxmit)
		xdp_return_frame_rx_napi(frame);
	return nxmit;
}

static const struct net_device_ops tap_netdev_ops = {
	.ndo_init		= tun_net_init,
	.ndo_uninit		= tun_net_uninit,
	.ndo_open		= tun_net_open,
	.ndo_stop		= tun_net_close,
	.ndo_start_xmit		= tun_net_xmit,
	.ndo_fix_features	= tun_net_fix_features,
	.ndo_set_rx_mode	= tun_net_mclist,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_select_queue	= tun_select_queue,
	.ndo_features_check	= passthru_features_check,
	.ndo_set_rx_headroom	= tun_set_headroom,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_bpf		= tun_xdp,
	.ndo_xdp_xmit		= tun_xdp_xmit,
	.ndo_change_carrier	= tun_net_change_carrier,
};

static void tun_flow_init(struct tun_struct *tun)
{
	int i;

	for (i = 0; i < TUN_NUM_FLOW_ENTRIES; i++)
		INIT_HLIST_HEAD(&tun->flows[i]);

	tun->ageing_time = TUN_FLOW_EXPIRE;
	timer_setup(&tun->flow_gc_timer, tun_flow_cleanup, 0);
	mod_timer(&tun->flow_gc_timer,
		  round_jiffies_up(jiffies + tun->ageing_time));
}

static void tun_flow_uninit(struct tun_struct *tun)
{
	del_timer_sync(&tun->flow_gc_timer);
	tun_flow_flush(tun);
}

#define MIN_MTU 68
#define MAX_MTU 65535

/* Initialize net device. */
static void tun_net_initialize(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	switch (tun->flags & TUN_TYPE_MASK) {
	case IFF_TUN:
		dev->netdev_ops = &tun_netdev_ops;
		dev->header_ops = &ip_tunnel_header_ops;

		/* Point-to-Point TUN Device */
		dev->hard_header_len = 0;
		dev->addr_len = 0;
		dev->mtu = 1500;

		/* Zero header length */
		dev->type = ARPHRD_NONE;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		break;

	case IFF_TAP:
		dev->netdev_ops = &tap_netdev_ops;
		/* Ethernet TAP Device */
		ether_setup(dev);
		dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

		eth_hw_addr_random(dev);

		break;
	}

	dev->min_mtu = MIN_MTU;
	dev->max_mtu = MAX_MTU - dev->hard_header_len;
}

static bool tun_sock_writeable(struct tun_struct *tun, struct tun_file *tfile)
{
	struct sock *sk = tfile->socket.sk;

	return (tun->dev->flags & IFF_UP) && sock_writeable(sk);
}

/* Character device part */

/* Poll */
static __poll_t tun_chr_poll(struct file *file, poll_table *wait)
{
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun = tun_get(tfile);
	struct sock *sk;
	__poll_t mask = 0;

	if (!tun)
		return EPOLLERR;

	sk = tfile->socket.sk;

	poll_wait(file, sk_sleep(sk), wait);

	if (!ptr_ring_empty(&tfile->tx_ring))
		mask |= EPOLLIN | EPOLLRDNORM;

	/* Make sure SOCKWQ_ASYNC_NOSPACE is set if not writable to
	 * guarantee EPOLLOUT to be raised by either here or
	 * tun_sock_write_space(). Then process could get notification
	 * after it writes to a down device and meets -EIO.
	 */
	if (tun_sock_writeable(tun, tfile) ||
	    (!test_and_set_bit(SOCKWQ_ASYNC_NOSPACE, &sk->sk_socket->flags) &&
	     tun_sock_writeable(tun, tfile)))
		mask |= EPOLLOUT | EPOLLWRNORM;

	if (tun->dev->reg_state != NETREG_REGISTERED)
		mask = EPOLLERR;

	tun_put(tun);
	return mask;
}

static struct sk_buff *tun_napi_alloc_frags(struct tun_file *tfile,
					    size_t len,
					    const struct iov_iter *it)
{
	struct sk_buff *skb;
	size_t linear;
	int err;
	int i;

	if (it->nr_segs > MAX_SKB_FRAGS + 1)
		return ERR_PTR(-EMSGSIZE);

	local_bh_disable();
	skb = napi_get_frags(&tfile->napi);
	local_bh_enable();
	if (!skb)
		return ERR_PTR(-ENOMEM);

	linear = iov_iter_single_seg_count(it);
	err = __skb_grow(skb, linear);
	if (err)
		goto free;

	skb->len = len;
	skb->data_len = len - linear;
	skb->truesize += skb->data_len;

	for (i = 1; i < it->nr_segs; i++) {
		size_t fragsz = it->iov[i].iov_len;
		struct page *page;
		void *frag;

		if (fragsz == 0 || fragsz > PAGE_SIZE) {
			err = -EINVAL;
			goto free;
		}
		frag = netdev_alloc_frag(fragsz);
		if (!frag) {
			err = -ENOMEM;
			goto free;
		}
		page = virt_to_head_page(frag);
		skb_fill_page_desc(skb, i - 1, page,
				   frag - page_address(page), fragsz);
	}

	return skb;
free:
	/* frees skb and all frags allocated with napi_alloc_frag() */
	napi_free_frags(&tfile->napi);
	return ERR_PTR(err);
}

/* prepad is the amount to reserve at front.  len is length after that.
 * linear is a hint as to how much to copy (usually headers). */
static struct sk_buff *tun_alloc_skb(struct tun_file *tfile,
				     size_t prepad, size_t len,
				     size_t linear, int noblock)
{
	struct sock *sk = tfile->socket.sk;
	struct sk_buff *skb;
	int err;

	/* Under a page?  Don't bother with paged skb. */
	if (prepad + len < PAGE_SIZE || !linear)
		linear = len;

	skb = sock_alloc_send_pskb(sk, prepad + linear, len - linear, noblock,
				   &err, 0);
	if (!skb)
		return ERR_PTR(err);

	skb_reserve(skb, prepad);
	skb_put(skb, linear);
	skb->data_len = len - linear;
	skb->len += len - linear;

	return skb;
}

static void tun_rx_batched(struct tun_struct *tun, struct tun_file *tfile,
			   struct sk_buff *skb, int more)
{
	struct sk_buff_head *queue = &tfile->sk.sk_write_queue;
	struct sk_buff_head process_queue;
	u32 rx_batched = tun->rx_batched;
	bool rcv = false;

	if (!rx_batched || (!more && skb_queue_empty(queue))) {
		local_bh_disable();
		skb_record_rx_queue(skb, tfile->queue_index);
		netif_receive_skb(skb);
		local_bh_enable();
		return;
	}

	spin_lock(&queue->lock);
	if (!more || skb_queue_len(queue) == rx_batched) {
		__skb_queue_head_init(&process_queue);
		skb_queue_splice_tail_init(queue, &process_queue);
		rcv = true;
	} else {
		__skb_queue_tail(queue, skb);
	}
	spin_unlock(&queue->lock);

	if (rcv) {
		struct sk_buff *nskb;

		local_bh_disable();
		while ((nskb = __skb_dequeue(&process_queue))) {
			skb_record_rx_queue(nskb, tfile->queue_index);
			netif_receive_skb(nskb);
		}
		skb_record_rx_queue(skb, tfile->queue_index);
		netif_receive_skb(skb);
		local_bh_enable();
	}
}

static bool tun_can_build_skb(struct tun_struct *tun, struct tun_file *tfile,
			      int len, int noblock, bool zerocopy)
{
	if ((tun->flags & TUN_TYPE_MASK) != IFF_TAP)
		return false;

	if (tfile->socket.sk->sk_sndbuf != INT_MAX)
		return false;

	if (!noblock)
		return false;

	if (zerocopy)
		return false;

	if (SKB_DATA_ALIGN(len + TUN_RX_PAD) +
	    SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) > PAGE_SIZE)
		return false;

	return true;
}

static struct sk_buff *__tun_build_skb(struct tun_file *tfile,
				       struct page_frag *alloc_frag, char *buf,
				       int buflen, int len, int pad)
{
	struct sk_buff *skb = build_skb(buf, buflen);

	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_reserve(skb, pad);
	skb_put(skb, len);
	skb_set_owner_w(skb, tfile->socket.sk);

	get_page(alloc_frag->page);
	alloc_frag->offset += buflen;

	return skb;
}

static int tun_xdp_act(struct tun_struct *tun, struct bpf_prog *xdp_prog,
		       struct xdp_buff *xdp, u32 act)
{
	int err;

	switch (act) {
	case XDP_REDIRECT:
		err = xdp_do_redirect(tun->dev, xdp, xdp_prog);
		if (err)
			return err;
		break;
	case XDP_TX:
		err = tun_xdp_tx(tun->dev, xdp);
		if (err < 0)
			return err;
		break;
	case XDP_PASS:
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(tun->dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		atomic_long_inc(&tun->dev->rx_dropped);
		break;
	}

	return act;
}

static struct sk_buff *tun_build_skb(struct tun_struct *tun,
				     struct tun_file *tfile,
				     struct iov_iter *from,
				     struct virtio_net_hdr *hdr,
				     int len, int *skb_xdp)
{
	struct page_frag *alloc_frag = &current->task_frag;
	struct bpf_prog *xdp_prog;
	int buflen = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	char *buf;
	size_t copied;
	int pad = TUN_RX_PAD;
	int err = 0;

	rcu_read_lock();
	xdp_prog = rcu_dereference(tun->xdp_prog);
	if (xdp_prog)
		pad += XDP_PACKET_HEADROOM;
	buflen += SKB_DATA_ALIGN(len + pad);
	rcu_read_unlock();

	alloc_frag->offset = ALIGN((u64)alloc_frag->offset, SMP_CACHE_BYTES);
	if (unlikely(!skb_page_frag_refill(buflen, alloc_frag, GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);

	buf = (char *)page_address(alloc_frag->page) + alloc_frag->offset;
	copied = copy_page_from_iter(alloc_frag->page,
				     alloc_frag->offset + pad,
				     len, from);
	if (copied != len)
		return ERR_PTR(-EFAULT);

	/* There's a small window that XDP may be set after the check
	 * of xdp_prog above, this should be rare and for simplicity
	 * we do XDP on skb in case the headroom is not enough.
	 */
	if (hdr->gso_type || !xdp_prog) {
		*skb_xdp = 1;
		return __tun_build_skb(tfile, alloc_frag, buf, buflen, len,
				       pad);
	}

	*skb_xdp = 0;

	local_bh_disable();
	rcu_read_lock();
	xdp_prog = rcu_dereference(tun->xdp_prog);
	if (xdp_prog) {
		struct xdp_buff xdp;
		u32 act;

		xdp_init_buff(&xdp, buflen, &tfile->xdp_rxq);
		xdp_prepare_buff(&xdp, buf, pad, len, false);

		act = bpf_prog_run_xdp(xdp_prog, &xdp);
		if (act == XDP_REDIRECT || act == XDP_TX) {
			get_page(alloc_frag->page);
			alloc_frag->offset += buflen;
		}
		err = tun_xdp_act(tun, xdp_prog, &xdp, act);
		if (err < 0) {
			if (act == XDP_REDIRECT || act == XDP_TX)
				put_page(alloc_frag->page);
			goto out;
		}

		if (err == XDP_REDIRECT)
			xdp_do_flush();
		if (err != XDP_PASS)
			goto out;

		pad = xdp.data - xdp.data_hard_start;
		len = xdp.data_end - xdp.data;
	}
	rcu_read_unlock();
	local_bh_enable();

	return __tun_build_skb(tfile, alloc_frag, buf, buflen, len, pad);

out:
	rcu_read_unlock();
	local_bh_enable();
	return NULL;
}

/* Get packet from user space buffer */
static ssize_t tun_get_user(struct tun_struct *tun, struct tun_file *tfile,
			    void *msg_control, struct iov_iter *from,
			    int noblock, bool more)
{
	struct tun_pi pi = { 0, cpu_to_be16(ETH_P_IP) };
	struct sk_buff *skb;
	size_t total_len = iov_iter_count(from);
	size_t len = total_len, align = tun->align, linear;
	struct virtio_net_hdr gso = { 0 };
	int good_linear;
	int copylen;
	bool zerocopy = false;
	int err;
	u32 rxhash = 0;
	int skb_xdp = 1;
	bool frags = tun_napi_frags_enabled(tfile);

	if (!(tun->flags & IFF_NO_PI)) {
		if (len < sizeof(pi))
			return -EINVAL;
		len -= sizeof(pi);

		if (!copy_from_iter_full(&pi, sizeof(pi), from))
			return -EFAULT;
	}

	if (tun->flags & IFF_VNET_HDR) {
		int vnet_hdr_sz = READ_ONCE(tun->vnet_hdr_sz);

		if (len < vnet_hdr_sz)
			return -EINVAL;
		len -= vnet_hdr_sz;

		if (!copy_from_iter_full(&gso, sizeof(gso), from))
			return -EFAULT;

		if ((gso.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) &&
		    tun16_to_cpu(tun, gso.csum_start) + tun16_to_cpu(tun, gso.csum_offset) + 2 > tun16_to_cpu(tun, gso.hdr_len))
			gso.hdr_len = cpu_to_tun16(tun, tun16_to_cpu(tun, gso.csum_start) + tun16_to_cpu(tun, gso.csum_offset) + 2);

		if (tun16_to_cpu(tun, gso.hdr_len) > len)
			return -EINVAL;
		iov_iter_advance(from, vnet_hdr_sz - sizeof(gso));
	}

	if ((tun->flags & TUN_TYPE_MASK) == IFF_TAP) {
		align += NET_IP_ALIGN;
		if (unlikely(len < ETH_HLEN ||
			     (gso.hdr_len && tun16_to_cpu(tun, gso.hdr_len) < ETH_HLEN)))
			return -EINVAL;
	}

	good_linear = SKB_MAX_HEAD(align);

	if (msg_control) {
		struct iov_iter i = *from;

		/* There are 256 bytes to be copied in skb, so there is
		 * enough room for skb expand head in case it is used.
		 * The rest of the buffer is mapped from userspace.
		 */
		copylen = gso.hdr_len ? tun16_to_cpu(tun, gso.hdr_len) : GOODCOPY_LEN;
		if (copylen > good_linear)
			copylen = good_linear;
		linear = copylen;
		iov_iter_advance(&i, copylen);
		if (iov_iter_npages(&i, INT_MAX) <= MAX_SKB_FRAGS)
			zerocopy = true;
	}

	if (!frags && tun_can_build_skb(tun, tfile, len, noblock, zerocopy)) {
		/* For the packet that is not easy to be processed
		 * (e.g gso or jumbo packet), we will do it at after
		 * skb was created with generic XDP routine.
		 */
		skb = tun_build_skb(tun, tfile, from, &gso, len, &skb_xdp);
		if (IS_ERR(skb)) {
			atomic_long_inc(&tun->dev->rx_dropped);
			return PTR_ERR(skb);
		}
		if (!skb)
			return total_len;
	} else {
		if (!zerocopy) {
			copylen = len;
			if (tun16_to_cpu(tun, gso.hdr_len) > good_linear)
				linear = good_linear;
			else
				linear = tun16_to_cpu(tun, gso.hdr_len);
		}

		if (frags) {
			mutex_lock(&tfile->napi_mutex);
			skb = tun_napi_alloc_frags(tfile, copylen, from);
			/* tun_napi_alloc_frags() enforces a layout for the skb.
			 * If zerocopy is enabled, then this layout will be
			 * overwritten by zerocopy_sg_from_iter().
			 */
			zerocopy = false;
		} else {
			skb = tun_alloc_skb(tfile, align, copylen, linear,
					    noblock);
		}

		if (IS_ERR(skb)) {
			if (PTR_ERR(skb) != -EAGAIN)
				atomic_long_inc(&tun->dev->rx_dropped);
			if (frags)
				mutex_unlock(&tfile->napi_mutex);
			return PTR_ERR(skb);
		}

		if (zerocopy)
			err = zerocopy_sg_from_iter(skb, from);
		else
			err = skb_copy_datagram_from_iter(skb, 0, from, len);

		if (err) {
			err = -EFAULT;
drop:
			atomic_long_inc(&tun->dev->rx_dropped);
			kfree_skb(skb);
			if (frags) {
				tfile->napi.skb = NULL;
				mutex_unlock(&tfile->napi_mutex);
			}

			return err;
		}
	}

	if (virtio_net_hdr_to_skb(skb, &gso, tun_is_little_endian(tun))) {
		atomic_long_inc(&tun->rx_frame_errors);
		kfree_skb(skb);
		if (frags) {
			tfile->napi.skb = NULL;
			mutex_unlock(&tfile->napi_mutex);
		}

		return -EINVAL;
	}

	switch (tun->flags & TUN_TYPE_MASK) {
	case IFF_TUN:
		if (tun->flags & IFF_NO_PI) {
			u8 ip_version = skb->len ? (skb->data[0] >> 4) : 0;

			switch (ip_version) {
			case 4:
				pi.proto = htons(ETH_P_IP);
				break;
			case 6:
				pi.proto = htons(ETH_P_IPV6);
				break;
			default:
				atomic_long_inc(&tun->dev->rx_dropped);
				kfree_skb(skb);
				return -EINVAL;
			}
		}

		skb_reset_mac_header(skb);
		skb->protocol = pi.proto;
		skb->dev = tun->dev;
		break;
	case IFF_TAP:
		if (frags && !pskb_may_pull(skb, ETH_HLEN)) {
			err = -ENOMEM;
			goto drop;
		}
		skb->protocol = eth_type_trans(skb, tun->dev);
		break;
	}

	/* copy skb_ubuf_info for callback when skb has no error */
	if (zerocopy) {
		skb_zcopy_init(skb, msg_control);
	} else if (msg_control) {
		struct ubuf_info *uarg = msg_control;
		uarg->callback(NULL, uarg, false);
	}

	skb_reset_network_header(skb);
	skb_probe_transport_header(skb);
	skb_record_rx_queue(skb, tfile->queue_index);

	if (skb_xdp) {
		struct bpf_prog *xdp_prog;
		int ret;

		local_bh_disable();
		rcu_read_lock();
		xdp_prog = rcu_dereference(tun->xdp_prog);
		if (xdp_prog) {
			ret = do_xdp_generic(xdp_prog, skb);
			if (ret != XDP_PASS) {
				rcu_read_unlock();
				local_bh_enable();
				if (frags) {
					tfile->napi.skb = NULL;
					mutex_unlock(&tfile->napi_mutex);
				}
				return total_len;
			}
		}
		rcu_read_unlock();
		local_bh_enable();
	}

	/* Compute the costly rx hash only if needed for flow updates.
	 * We may get a very small possibility of OOO during switching, not
	 * worth to optimize.
	 */
	if (!rcu_access_pointer(tun->steering_prog) && tun->numqueues > 1 &&
	    !tfile->detached)
		rxhash = __skb_get_hash_symmetric(skb);

	rcu_read_lock();
	if (unlikely(!(tun->dev->flags & IFF_UP))) {
		err = -EIO;
		rcu_read_unlock();
		goto drop;
	}

	if (frags) {
		u32 headlen;

		/* Exercise flow dissector code path. */
		skb_push(skb, ETH_HLEN);
		headlen = eth_get_headlen(tun->dev, skb->data,
					  skb_headlen(skb));

		if (unlikely(headlen > skb_headlen(skb))) {
			atomic_long_inc(&tun->dev->rx_dropped);
			napi_free_frags(&tfile->napi);
			rcu_read_unlock();
			mutex_unlock(&tfile->napi_mutex);
			WARN_ON(1);
			return -ENOMEM;
		}

		local_bh_disable();
		napi_gro_frags(&tfile->napi);
		local_bh_enable();
		mutex_unlock(&tfile->napi_mutex);
	} else if (tfile->napi_enabled) {
		struct sk_buff_head *queue = &tfile->sk.sk_write_queue;
		int queue_len;

		spin_lock_bh(&queue->lock);
		__skb_queue_tail(queue, skb);
		queue_len = skb_queue_len(queue);
		spin_unlock(&queue->lock);

		if (!more || queue_len > NAPI_POLL_WEIGHT)
			napi_schedule(&tfile->napi);

		local_bh_enable();
	} else if (!IS_ENABLED(CONFIG_4KSTACKS)) {
		tun_rx_batched(tun, tfile, skb, more);
	} else {
		netif_rx_ni(skb);
	}
	rcu_read_unlock();

	preempt_disable();
	dev_sw_netstats_rx_add(tun->dev, len);
	preempt_enable();

	if (rxhash)
		tun_flow_update(tun, rxhash, tfile);

	return total_len;
}

static ssize_t tun_chr_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun = tun_get(tfile);
	ssize_t result;
	int noblock = 0;

	if (!tun)
		return -EBADFD;

	if ((file->f_flags & O_NONBLOCK) || (iocb->ki_flags & IOCB_NOWAIT))
		noblock = 1;

	result = tun_get_user(tun, tfile, NULL, from, noblock, false);

	tun_put(tun);
	return result;
}

static ssize_t tun_put_user_xdp(struct tun_struct *tun,
				struct tun_file *tfile,
				struct xdp_frame *xdp_frame,
				struct iov_iter *iter)
{
	int vnet_hdr_sz = 0;
	size_t size = xdp_frame->len;
	size_t ret;

	if (tun->flags & IFF_VNET_HDR) {
		struct virtio_net_hdr gso = { 0 };

		vnet_hdr_sz = READ_ONCE(tun->vnet_hdr_sz);
		if (unlikely(iov_iter_count(iter) < vnet_hdr_sz))
			return -EINVAL;
		if (unlikely(copy_to_iter(&gso, sizeof(gso), iter) !=
			     sizeof(gso)))
			return -EFAULT;
		iov_iter_advance(iter, vnet_hdr_sz - sizeof(gso));
	}

	ret = copy_to_iter(xdp_frame->data, size, iter) + vnet_hdr_sz;

	preempt_disable();
	dev_sw_netstats_tx_add(tun->dev, 1, ret);
	preempt_enable();

	return ret;
}

/* Put packet to the user space buffer */
static ssize_t tun_put_user(struct tun_struct *tun,
			    struct tun_file *tfile,
			    struct sk_buff *skb,
			    struct iov_iter *iter)
{
	struct tun_pi pi = { 0, skb->protocol };
	ssize_t total;
	int vlan_offset = 0;
	int vlan_hlen = 0;
	int vnet_hdr_sz = 0;

	if (skb_vlan_tag_present(skb))
		vlan_hlen = VLAN_HLEN;

	if (tun->flags & IFF_VNET_HDR)
		vnet_hdr_sz = READ_ONCE(tun->vnet_hdr_sz);

	total = skb->len + vlan_hlen + vnet_hdr_sz;

	if (!(tun->flags & IFF_NO_PI)) {
		if (iov_iter_count(iter) < sizeof(pi))
			return -EINVAL;

		total += sizeof(pi);
		if (iov_iter_count(iter) < total) {
			/* Packet will be striped */
			pi.flags |= TUN_PKT_STRIP;
		}

		if (copy_to_iter(&pi, sizeof(pi), iter) != sizeof(pi))
			return -EFAULT;
	}

	if (vnet_hdr_sz) {
		struct virtio_net_hdr gso;

		if (iov_iter_count(iter) < vnet_hdr_sz)
			return -EINVAL;

		if (virtio_net_hdr_from_skb(skb, &gso,
					    tun_is_little_endian(tun), true,
					    vlan_hlen)) {
			struct skb_shared_info *sinfo = skb_shinfo(skb);
			pr_err("unexpected GSO type: "
			       "0x%x, gso_size %d, hdr_len %d\n",
			       sinfo->gso_type, tun16_to_cpu(tun, gso.gso_size),
			       tun16_to_cpu(tun, gso.hdr_len));
			print_hex_dump(KERN_ERR, "tun: ",
				       DUMP_PREFIX_NONE,
				       16, 1, skb->head,
				       min((int)tun16_to_cpu(tun, gso.hdr_len), 64), true);
			WARN_ON_ONCE(1);
			return -EINVAL;
		}

		if (copy_to_iter(&gso, sizeof(gso), iter) != sizeof(gso))
			return -EFAULT;

		iov_iter_advance(iter, vnet_hdr_sz - sizeof(gso));
	}

	if (vlan_hlen) {
		int ret;
		struct veth veth;

		veth.h_vlan_proto = skb->vlan_proto;
		veth.h_vlan_TCI = htons(skb_vlan_tag_get(skb));

		vlan_offset = offsetof(struct vlan_ethhdr, h_vlan_proto);

		ret = skb_copy_datagram_iter(skb, 0, iter, vlan_offset);
		if (ret || !iov_iter_count(iter))
			goto done;

		ret = copy_to_iter(&veth, sizeof(veth), iter);
		if (ret != sizeof(veth) || !iov_iter_count(iter))
			goto done;
	}

	skb_copy_datagram_iter(skb, vlan_offset, iter, skb->len - vlan_offset);

done:
	/* caller is in process context, */
	preempt_disable();
	dev_sw_netstats_tx_add(tun->dev, 1, skb->len + vlan_hlen);
	preempt_enable();

	return total;
}

static void *tun_ring_recv(struct tun_file *tfile, int noblock, int *err)
{
	DECLARE_WAITQUEUE(wait, current);
	void *ptr = NULL;
	int error = 0;

	ptr = ptr_ring_consume(&tfile->tx_ring);
	if (ptr)
		goto out;
	if (noblock) {
		error = -EAGAIN;
		goto out;
	}

	add_wait_queue(&tfile->socket.wq.wait, &wait);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		ptr = ptr_ring_consume(&tfile->tx_ring);
		if (ptr)
			break;
		if (signal_pending(current)) {
			error = -ERESTARTSYS;
			break;
		}
		if (tfile->socket.sk->sk_shutdown & RCV_SHUTDOWN) {
			error = -EFAULT;
			break;
		}

		schedule();
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&tfile->socket.wq.wait, &wait);

out:
	*err = error;
	return ptr;
}

static ssize_t tun_do_read(struct tun_struct *tun, struct tun_file *tfile,
			   struct iov_iter *to,
			   int noblock, void *ptr)
{
	ssize_t ret;
	int err;

	if (!iov_iter_count(to)) {
		tun_ptr_free(ptr);
		return 0;
	}

	if (!ptr) {
		/* Read frames from ring */
		ptr = tun_ring_recv(tfile, noblock, &err);
		if (!ptr)
			return err;
	}

	if (tun_is_xdp_frame(ptr)) {
		struct xdp_frame *xdpf = tun_ptr_to_xdp(ptr);

		ret = tun_put_user_xdp(tun, tfile, xdpf, to);
		xdp_return_frame(xdpf);
	} else {
		struct sk_buff *skb = ptr;

		ret = tun_put_user(tun, tfile, skb, to);
		if (unlikely(ret < 0))
			kfree_skb(skb);
		else
			consume_skb(skb);
	}

	return ret;
}

static ssize_t tun_chr_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun = tun_get(tfile);
	ssize_t len = iov_iter_count(to), ret;
	int noblock = 0;

	if (!tun)
		return -EBADFD;

	if ((file->f_flags & O_NONBLOCK) || (iocb->ki_flags & IOCB_NOWAIT))
		noblock = 1;

	ret = tun_do_read(tun, tfile, to, noblock, NULL);
	ret = min_t(ssize_t, ret, len);
	if (ret > 0)
		iocb->ki_pos = ret;
	tun_put(tun);
	return ret;
}

static void tun_prog_free(struct rcu_head *rcu)
{
	struct tun_prog *prog = container_of(rcu, struct tun_prog, rcu);

	bpf_prog_destroy(prog->prog);
	kfree(prog);
}

static int __tun_set_ebpf(struct tun_struct *tun,
			  struct tun_prog __rcu **prog_p,
			  struct bpf_prog *prog)
{
	struct tun_prog *old, *new = NULL;

	if (prog) {
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			return -ENOMEM;
		new->prog = prog;
	}

	spin_lock_bh(&tun->lock);
	old = rcu_dereference_protected(*prog_p,
					lockdep_is_held(&tun->lock));
	rcu_assign_pointer(*prog_p, new);
	spin_unlock_bh(&tun->lock);

	if (old)
		call_rcu(&old->rcu, tun_prog_free);

	return 0;
}

static void tun_free_netdev(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	BUG_ON(!(list_empty(&tun->disabled)));

	free_percpu(dev->tstats);
	tun_flow_uninit(tun);
	security_tun_dev_free_security(tun->security);
	__tun_set_ebpf(tun, &tun->steering_prog, NULL);
	__tun_set_ebpf(tun, &tun->filter_prog, NULL);
}

static void tun_setup(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	tun->owner = INVALID_UID;
	tun->group = INVALID_GID;
	tun_default_link_ksettings(dev, &tun->link_ksettings);

	dev->ethtool_ops = &tun_ethtool_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = tun_free_netdev;
	/* We prefer our own queue length */
	dev->tx_queue_len = TUN_READQ_SIZE;
}

/* Trivial set of netlink ops to allow deleting tun or tap
 * device with netlink.
 */
static int tun_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack,
		       "tun/tap creation via rtnetlink is not supported.");
	return -EOPNOTSUPP;
}

static size_t tun_get_size(const struct net_device *dev)
{
	BUILD_BUG_ON(sizeof(u32) != sizeof(uid_t));
	BUILD_BUG_ON(sizeof(u32) != sizeof(gid_t));

	return nla_total_size(sizeof(uid_t)) + /* OWNER */
	       nla_total_size(sizeof(gid_t)) + /* GROUP */
	       nla_total_size(sizeof(u8)) + /* TYPE */
	       nla_total_size(sizeof(u8)) + /* PI */
	       nla_total_size(sizeof(u8)) + /* VNET_HDR */
	       nla_total_size(sizeof(u8)) + /* PERSIST */
	       nla_total_size(sizeof(u8)) + /* MULTI_QUEUE */
	       nla_total_size(sizeof(u32)) + /* NUM_QUEUES */
	       nla_total_size(sizeof(u32)) + /* NUM_DISABLED_QUEUES */
	       0;
}

static int tun_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	if (nla_put_u8(skb, IFLA_TUN_TYPE, tun->flags & TUN_TYPE_MASK))
		goto nla_put_failure;
	if (uid_valid(tun->owner) &&
	    nla_put_u32(skb, IFLA_TUN_OWNER,
			from_kuid_munged(current_user_ns(), tun->owner)))
		goto nla_put_failure;
	if (gid_valid(tun->group) &&
	    nla_put_u32(skb, IFLA_TUN_GROUP,
			from_kgid_munged(current_user_ns(), tun->group)))
		goto nla_put_failure;
	if (nla_put_u8(skb, IFLA_TUN_PI, !(tun->flags & IFF_NO_PI)))
		goto nla_put_failure;
	if (nla_put_u8(skb, IFLA_TUN_VNET_HDR, !!(tun->flags & IFF_VNET_HDR)))
		goto nla_put_failure;
	if (nla_put_u8(skb, IFLA_TUN_PERSIST, !!(tun->flags & IFF_PERSIST)))
		goto nla_put_failure;
	if (nla_put_u8(skb, IFLA_TUN_MULTI_QUEUE,
		       !!(tun->flags & IFF_MULTI_QUEUE)))
		goto nla_put_failure;
	if (tun->flags & IFF_MULTI_QUEUE) {
		if (nla_put_u32(skb, IFLA_TUN_NUM_QUEUES, tun->numqueues))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_TUN_NUM_DISABLED_QUEUES,
				tun->numdisabled))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops tun_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct tun_struct),
	.setup		= tun_setup,
	.validate	= tun_validate,
	.get_size       = tun_get_size,
	.fill_info      = tun_fill_info,
};

static void tun_sock_write_space(struct sock *sk)
{
	struct tun_file *tfile;
	wait_queue_head_t *wqueue;

	if (!sock_writeable(sk))
		return;

	if (!test_and_clear_bit(SOCKWQ_ASYNC_NOSPACE, &sk->sk_socket->flags))
		return;

	wqueue = sk_sleep(sk);
	if (wqueue && waitqueue_active(wqueue))
		wake_up_interruptible_sync_poll(wqueue, EPOLLOUT |
						EPOLLWRNORM | EPOLLWRBAND);

	tfile = container_of(sk, struct tun_file, sk);
	kill_fasync(&tfile->fasync, SIGIO, POLL_OUT);
}

static void tun_put_page(struct tun_page *tpage)
{
	if (tpage->page)
		__page_frag_cache_drain(tpage->page, tpage->count);
}

static int tun_xdp_one(struct tun_struct *tun,
		       struct tun_file *tfile,
		       struct xdp_buff *xdp, int *flush,
		       struct tun_page *tpage)
{
	unsigned int datasize = xdp->data_end - xdp->data;
	struct tun_xdp_hdr *hdr = xdp->data_hard_start;
	struct virtio_net_hdr *gso = &hdr->gso;
	struct bpf_prog *xdp_prog;
	struct sk_buff *skb = NULL;
	u32 rxhash = 0, act;
	int buflen = hdr->buflen;
	int err = 0;
	bool skb_xdp = false;
	struct page *page;

	xdp_prog = rcu_dereference(tun->xdp_prog);
	if (xdp_prog) {
		if (gso->gso_type) {
			skb_xdp = true;
			goto build;
		}

		xdp_init_buff(xdp, buflen, &tfile->xdp_rxq);
		xdp_set_data_meta_invalid(xdp);

		act = bpf_prog_run_xdp(xdp_prog, xdp);
		err = tun_xdp_act(tun, xdp_prog, xdp, act);
		if (err < 0) {
			put_page(virt_to_head_page(xdp->data));
			return err;
		}

		switch (err) {
		case XDP_REDIRECT:
			*flush = true;
			fallthrough;
		case XDP_TX:
			return 0;
		case XDP_PASS:
			break;
		default:
			page = virt_to_head_page(xdp->data);
			if (tpage->page == page) {
				++tpage->count;
			} else {
				tun_put_page(tpage);
				tpage->page = page;
				tpage->count = 1;
			}
			return 0;
		}
	}

build:
	skb = build_skb(xdp->data_hard_start, buflen);
	if (!skb) {
		err = -ENOMEM;
		goto out;
	}

	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	skb_put(skb, xdp->data_end - xdp->data);

	if (virtio_net_hdr_to_skb(skb, gso, tun_is_little_endian(tun))) {
		atomic_long_inc(&tun->rx_frame_errors);
		kfree_skb(skb);
		err = -EINVAL;
		goto out;
	}

	skb->protocol = eth_type_trans(skb, tun->dev);
	skb_reset_network_header(skb);
	skb_probe_transport_header(skb);
	skb_record_rx_queue(skb, tfile->queue_index);

	if (skb_xdp) {
		err = do_xdp_generic(xdp_prog, skb);
		if (err != XDP_PASS)
			goto out;
	}

	if (!rcu_dereference(tun->steering_prog) && tun->numqueues > 1 &&
	    !tfile->detached)
		rxhash = __skb_get_hash_symmetric(skb);

	netif_receive_skb(skb);

	/* No need to disable preemption here since this function is
	 * always called with bh disabled
	 */
	dev_sw_netstats_rx_add(tun->dev, datasize);

	if (rxhash)
		tun_flow_update(tun, rxhash, tfile);

out:
	return err;
}

static int tun_sendmsg(struct socket *sock, struct msghdr *m, size_t total_len)
{
	int ret, i;
	struct tun_file *tfile = container_of(sock, struct tun_file, socket);
	struct tun_struct *tun = tun_get(tfile);
	struct tun_msg_ctl *ctl = m->msg_control;
	struct xdp_buff *xdp;

	if (!tun)
		return -EBADFD;

	if (m->msg_controllen == sizeof(struct tun_msg_ctl) &&
	    ctl && ctl->type == TUN_MSG_PTR) {
		struct tun_page tpage;
		int n = ctl->num;
		int flush = 0;

		memset(&tpage, 0, sizeof(tpage));

		local_bh_disable();
		rcu_read_lock();

		for (i = 0; i < n; i++) {
			xdp = &((struct xdp_buff *)ctl->ptr)[i];
			tun_xdp_one(tun, tfile, xdp, &flush, &tpage);
		}

		if (flush)
			xdp_do_flush();

		rcu_read_unlock();
		local_bh_enable();

		tun_put_page(&tpage);

		ret = total_len;
		goto out;
	}

	ret = tun_get_user(tun, tfile, ctl ? ctl->ptr : NULL, &m->msg_iter,
			   m->msg_flags & MSG_DONTWAIT,
			   m->msg_flags & MSG_MORE);
out:
	tun_put(tun);
	return ret;
}

static int tun_recvmsg(struct socket *sock, struct msghdr *m, size_t total_len,
		       int flags)
{
	struct tun_file *tfile = container_of(sock, struct tun_file, socket);
	struct tun_struct *tun = tun_get(tfile);
	void *ptr = m->msg_control;
	int ret;

	if (!tun) {
		ret = -EBADFD;
		goto out_free;
	}

	if (flags & ~(MSG_DONTWAIT|MSG_TRUNC|MSG_ERRQUEUE)) {
		ret = -EINVAL;
		goto out_put_tun;
	}
	if (flags & MSG_ERRQUEUE) {
		ret = sock_recv_errqueue(sock->sk, m, total_len,
					 SOL_PACKET, TUN_TX_TIMESTAMP);
		goto out;
	}
	ret = tun_do_read(tun, tfile, &m->msg_iter, flags & MSG_DONTWAIT, ptr);
	if (ret > (ssize_t)total_len) {
		m->msg_flags |= MSG_TRUNC;
		ret = flags & MSG_TRUNC ? ret : total_len;
	}
out:
	tun_put(tun);
	return ret;

out_put_tun:
	tun_put(tun);
out_free:
	tun_ptr_free(ptr);
	return ret;
}

static int tun_ptr_peek_len(void *ptr)
{
	if (likely(ptr)) {
		if (tun_is_xdp_frame(ptr)) {
			struct xdp_frame *xdpf = tun_ptr_to_xdp(ptr);

			return xdpf->len;
		}
		return __skb_array_len_with_tag(ptr);
	} else {
		return 0;
	}
}

static int tun_peek_len(struct socket *sock)
{
	struct tun_file *tfile = container_of(sock, struct tun_file, socket);
	struct tun_struct *tun;
	int ret = 0;

	tun = tun_get(tfile);
	if (!tun)
		return 0;

	ret = PTR_RING_PEEK_CALL(&tfile->tx_ring, tun_ptr_peek_len);
	tun_put(tun);

	return ret;
}

/* Ops structure to mimic raw sockets with tun */
static const struct proto_ops tun_socket_ops = {
	.peek_len = tun_peek_len,
	.sendmsg = tun_sendmsg,
	.recvmsg = tun_recvmsg,
};

static struct proto tun_proto = {
	.name		= "tun",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct tun_file),
};

static int tun_flags(struct tun_struct *tun)
{
	return tun->flags & (TUN_FEATURES | IFF_PERSIST | IFF_TUN | IFF_TAP);
}

static ssize_t tun_flags_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct tun_struct *tun = netdev_priv(to_net_dev(dev));
	return sprintf(buf, "0x%x\n", tun_flags(tun));
}

static ssize_t owner_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tun_struct *tun = netdev_priv(to_net_dev(dev));
	return uid_valid(tun->owner)?
		sprintf(buf, "%u\n",
			from_kuid_munged(current_user_ns(), tun->owner)):
		sprintf(buf, "-1\n");
}

static ssize_t group_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tun_struct *tun = netdev_priv(to_net_dev(dev));
	return gid_valid(tun->group) ?
		sprintf(buf, "%u\n",
			from_kgid_munged(current_user_ns(), tun->group)):
		sprintf(buf, "-1\n");
}

static DEVICE_ATTR_RO(tun_flags);
static DEVICE_ATTR_RO(owner);
static DEVICE_ATTR_RO(group);

static struct attribute *tun_dev_attrs[] = {
	&dev_attr_tun_flags.attr,
	&dev_attr_owner.attr,
	&dev_attr_group.attr,
	NULL
};

static const struct attribute_group tun_attr_group = {
	.attrs = tun_dev_attrs
};

static int tun_set_iff(struct net *net, struct file *file, struct ifreq *ifr)
{
	struct tun_struct *tun;
	struct tun_file *tfile = file->private_data;
	struct net_device *dev;
	int err;

	if (tfile->detached)
		return -EINVAL;

	if ((ifr->ifr_flags & IFF_NAPI_FRAGS)) {
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (!(ifr->ifr_flags & IFF_NAPI) ||
		    (ifr->ifr_flags & TUN_TYPE_MASK) != IFF_TAP)
			return -EINVAL;
	}

	dev = __dev_get_by_name(net, ifr->ifr_name);
	if (dev) {
		if (ifr->ifr_flags & IFF_TUN_EXCL)
			return -EBUSY;
		if ((ifr->ifr_flags & IFF_TUN) && dev->netdev_ops == &tun_netdev_ops)
			tun = netdev_priv(dev);
		else if ((ifr->ifr_flags & IFF_TAP) && dev->netdev_ops == &tap_netdev_ops)
			tun = netdev_priv(dev);
		else
			return -EINVAL;

		if (!!(ifr->ifr_flags & IFF_MULTI_QUEUE) !=
		    !!(tun->flags & IFF_MULTI_QUEUE))
			return -EINVAL;

		if (tun_not_capable(tun))
			return -EPERM;
		err = security_tun_dev_open(tun->security);
		if (err < 0)
			return err;

		err = tun_attach(tun, file, ifr->ifr_flags & IFF_NOFILTER,
				 ifr->ifr_flags & IFF_NAPI,
				 ifr->ifr_flags & IFF_NAPI_FRAGS, true);
		if (err < 0)
			return err;

		if (tun->flags & IFF_MULTI_QUEUE &&
		    (tun->numqueues + tun->numdisabled > 1)) {
			/* One or more queue has already been attached, no need
			 * to initialize the device again.
			 */
			netdev_state_change(dev);
			return 0;
		}

		tun->flags = (tun->flags & ~TUN_FEATURES) |
			      (ifr->ifr_flags & TUN_FEATURES);

		netdev_state_change(dev);
	} else {
		char *name;
		unsigned long flags = 0;
		int queues = ifr->ifr_flags & IFF_MULTI_QUEUE ?
			     MAX_TAP_QUEUES : 1;

		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;
		err = security_tun_dev_create();
		if (err < 0)
			return err;

		/* Set dev type */
		if (ifr->ifr_flags & IFF_TUN) {
			/* TUN device */
			flags |= IFF_TUN;
			name = "tun%d";
		} else if (ifr->ifr_flags & IFF_TAP) {
			/* TAP device */
			flags |= IFF_TAP;
			name = "tap%d";
		} else
			return -EINVAL;

		if (*ifr->ifr_name)
			name = ifr->ifr_name;

		dev = alloc_netdev_mqs(sizeof(struct tun_struct), name,
				       NET_NAME_UNKNOWN, tun_setup, queues,
				       queues);

		if (!dev)
			return -ENOMEM;

		dev_net_set(dev, net);
		dev->rtnl_link_ops = &tun_link_ops;
		dev->ifindex = tfile->ifindex;
		dev->sysfs_groups[0] = &tun_attr_group;

		tun = netdev_priv(dev);
		tun->dev = dev;
		tun->flags = flags;
		tun->txflt.count = 0;
		tun->vnet_hdr_sz = sizeof(struct virtio_net_hdr);

		tun->align = NET_SKB_PAD;
		tun->filter_attached = false;
		tun->sndbuf = tfile->socket.sk->sk_sndbuf;
		tun->rx_batched = 0;
		RCU_INIT_POINTER(tun->steering_prog, NULL);

		tun->ifr = ifr;
		tun->file = file;

		tun_net_initialize(dev);

		err = register_netdevice(tun->dev);
		if (err < 0) {
			free_netdev(dev);
			return err;
		}
		/* free_netdev() won't check refcnt, to avoid race
		 * with dev_put() we need publish tun after registration.
		 */
		rcu_assign_pointer(tfile->tun, tun);
	}

	netif_carrier_on(tun->dev);

	/* Make sure persistent devices do not get stuck in
	 * xoff state.
	 */
	if (netif_running(tun->dev))
		netif_tx_wake_all_queues(tun->dev);

	strcpy(ifr->ifr_name, tun->dev->name);
	return 0;
}

static void tun_get_iff(struct tun_struct *tun, struct ifreq *ifr)
{
	strcpy(ifr->ifr_name, tun->dev->name);

	ifr->ifr_flags = tun_flags(tun);

}

/* This is like a cut-down ethtool ops, except done via tun fd so no
 * privs required. */
static int set_offload(struct tun_struct *tun, unsigned long arg)
{
	netdev_features_t features = 0;

	if (arg & TUN_F_CSUM) {
		features |= NETIF_F_HW_CSUM;
		arg &= ~TUN_F_CSUM;

		if (arg & (TUN_F_TSO4|TUN_F_TSO6)) {
			if (arg & TUN_F_TSO_ECN) {
				features |= NETIF_F_TSO_ECN;
				arg &= ~TUN_F_TSO_ECN;
			}
			if (arg & TUN_F_TSO4)
				features |= NETIF_F_TSO;
			if (arg & TUN_F_TSO6)
				features |= NETIF_F_TSO6;
			arg &= ~(TUN_F_TSO4|TUN_F_TSO6);
		}

		arg &= ~TUN_F_UFO;
	}

	/* This gives the user a way to test for new features in future by
	 * trying to set them. */
	if (arg)
		return -EINVAL;

	tun->set_features = features;
	tun->dev->wanted_features &= ~TUN_USER_FEATURES;
	tun->dev->wanted_features |= features;
	netdev_update_features(tun->dev);

	return 0;
}

static void tun_detach_filter(struct tun_struct *tun, int n)
{
	int i;
	struct tun_file *tfile;

	for (i = 0; i < n; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		lock_sock(tfile->socket.sk);
		sk_detach_filter(tfile->socket.sk);
		release_sock(tfile->socket.sk);
	}

	tun->filter_attached = false;
}

static int tun_attach_filter(struct tun_struct *tun)
{
	int i, ret = 0;
	struct tun_file *tfile;

	for (i = 0; i < tun->numqueues; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		lock_sock(tfile->socket.sk);
		ret = sk_attach_filter(&tun->fprog, tfile->socket.sk);
		release_sock(tfile->socket.sk);
		if (ret) {
			tun_detach_filter(tun, i);
			return ret;
		}
	}

	tun->filter_attached = true;
	return ret;
}

static void tun_set_sndbuf(struct tun_struct *tun)
{
	struct tun_file *tfile;
	int i;

	for (i = 0; i < tun->numqueues; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		tfile->socket.sk->sk_sndbuf = tun->sndbuf;
	}
}

static int tun_set_queue(struct file *file, struct ifreq *ifr)
{
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun;
	int ret = 0;

	rtnl_lock();

	if (ifr->ifr_flags & IFF_ATTACH_QUEUE) {
		tun = tfile->detached;
		if (!tun) {
			ret = -EINVAL;
			goto unlock;
		}
		ret = security_tun_dev_attach_queue(tun->security);
		if (ret < 0)
			goto unlock;
		ret = tun_attach(tun, file, false, tun->flags & IFF_NAPI,
				 tun->flags & IFF_NAPI_FRAGS, true);
	} else if (ifr->ifr_flags & IFF_DETACH_QUEUE) {
		tun = rtnl_dereference(tfile->tun);
		if (!tun || !(tun->flags & IFF_MULTI_QUEUE) || tfile->detached)
			ret = -EINVAL;
		else
			__tun_detach(tfile, false);
	} else
		ret = -EINVAL;

	if (ret >= 0)
		netdev_state_change(tun->dev);

unlock:
	rtnl_unlock();
	return ret;
}

static int tun_set_ebpf(struct tun_struct *tun, struct tun_prog __rcu **prog_p,
			void __user *data)
{
	struct bpf_prog *prog;
	int fd;

	if (copy_from_user(&fd, data, sizeof(fd)))
		return -EFAULT;

	if (fd == -1) {
		prog = NULL;
	} else {
		prog = bpf_prog_get_type(fd, BPF_PROG_TYPE_SOCKET_FILTER);
		if (IS_ERR(prog))
			return PTR_ERR(prog);
	}

	return __tun_set_ebpf(tun, prog_p, prog);
}

/* Return correct value for tun->dev->addr_len based on tun->dev->type. */
static unsigned char tun_get_addr_len(unsigned short type)
{
	switch (type) {
	case ARPHRD_IP6GRE:
	case ARPHRD_TUNNEL6:
		return sizeof(struct in6_addr);
	case ARPHRD_IPGRE:
	case ARPHRD_TUNNEL:
	case ARPHRD_SIT:
		return 4;
	case ARPHRD_ETHER:
		return ETH_ALEN;
	case ARPHRD_IEEE802154:
	case ARPHRD_IEEE802154_MONITOR:
		return IEEE802154_EXTENDED_ADDR_LEN;
	case ARPHRD_PHONET_PIPE:
	case ARPHRD_PPP:
	case ARPHRD_NONE:
		return 0;
	case ARPHRD_6LOWPAN:
		return EUI64_ADDR_LEN;
	case ARPHRD_FDDI:
		return FDDI_K_ALEN;
	case ARPHRD_HIPPI:
		return HIPPI_ALEN;
	case ARPHRD_IEEE802:
		return FC_ALEN;
	case ARPHRD_ROSE:
		return ROSE_ADDR_LEN;
	case ARPHRD_NETROM:
		return AX25_ADDR_LEN;
	case ARPHRD_LOCALTLK:
		return LTALK_ALEN;
	default:
		return 0;
	}
}

static long __tun_chr_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg, int ifreq_len)
{
	struct tun_file *tfile = file->private_data;
	struct net *net = sock_net(&tfile->sk);
	struct tun_struct *tun;
	void __user* argp = (void __user*)arg;
	unsigned int ifindex, carrier;
	struct ifreq ifr;
	kuid_t owner;
	kgid_t group;
	int sndbuf;
	int vnet_hdr_sz;
	int le;
	int ret;
	bool do_notify = false;

	if (cmd == TUNSETIFF || cmd == TUNSETQUEUE ||
	    (_IOC_TYPE(cmd) == SOCK_IOC_TYPE && cmd != SIOCGSKNS)) {
		if (copy_from_user(&ifr, argp, ifreq_len))
			return -EFAULT;
	} else {
		memset(&ifr, 0, sizeof(ifr));
	}
	if (cmd == TUNGETFEATURES) {
		/* Currently this just means: "what IFF flags are valid?".
		 * This is needed because we never checked for invalid flags on
		 * TUNSETIFF.
		 */
		return put_user(IFF_TUN | IFF_TAP | TUN_FEATURES,
				(unsigned int __user*)argp);
	} else if (cmd == TUNSETQUEUE) {
		return tun_set_queue(file, &ifr);
	} else if (cmd == SIOCGSKNS) {
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;
		return open_related_ns(&net->ns, get_net_ns);
	}

	rtnl_lock();

	tun = tun_get(tfile);
	if (cmd == TUNSETIFF) {
		ret = -EEXIST;
		if (tun)
			goto unlock;

		ifr.ifr_name[IFNAMSIZ-1] = '\0';

		ret = tun_set_iff(net, file, &ifr);

		if (ret)
			goto unlock;

		if (copy_to_user(argp, &ifr, ifreq_len))
			ret = -EFAULT;
		goto unlock;
	}
	if (cmd == TUNSETIFINDEX) {
		ret = -EPERM;
		if (tun)
			goto unlock;

		ret = -EFAULT;
		if (copy_from_user(&ifindex, argp, sizeof(ifindex)))
			goto unlock;

		ret = 0;
		tfile->ifindex = ifindex;
		goto unlock;
	}

	ret = -EBADFD;
	if (!tun)
		goto unlock;

	netif_info(tun, drv, tun->dev, "tun_chr_ioctl cmd %u\n", cmd);

	net = dev_net(tun->dev);
	ret = 0;
	switch (cmd) {
	case TUNGETIFF:
		tun_get_iff(tun, &ifr);

		if (tfile->detached)
			ifr.ifr_flags |= IFF_DETACH_QUEUE;
		if (!tfile->socket.sk->sk_filter)
			ifr.ifr_flags |= IFF_NOFILTER;

		if (copy_to_user(argp, &ifr, ifreq_len))
			ret = -EFAULT;
		break;

	case TUNSETNOCSUM:
		/* Disable/Enable checksum */

		/* [unimplemented] */
		netif_info(tun, drv, tun->dev, "ignored: set checksum %s\n",
			   arg ? "disabled" : "enabled");
		break;

	case TUNSETPERSIST:
		/* Disable/Enable persist mode. Keep an extra reference to the
		 * module to prevent the module being unprobed.
		 */
		if (arg && !(tun->flags & IFF_PERSIST)) {
			tun->flags |= IFF_PERSIST;
			__module_get(THIS_MODULE);
			do_notify = true;
		}
		if (!arg && (tun->flags & IFF_PERSIST)) {
			tun->flags &= ~IFF_PERSIST;
			module_put(THIS_MODULE);
			do_notify = true;
		}

		netif_info(tun, drv, tun->dev, "persist %s\n",
			   arg ? "enabled" : "disabled");
		break;

	case TUNSETOWNER:
		/* Set owner of the device */
		owner = make_kuid(current_user_ns(), arg);
		if (!uid_valid(owner)) {
			ret = -EINVAL;
			break;
		}
		tun->owner = owner;
		do_notify = true;
		netif_info(tun, drv, tun->dev, "owner set to %u\n",
			   from_kuid(&init_user_ns, tun->owner));
		break;

	case TUNSETGROUP:
		/* Set group of the device */
		group = make_kgid(current_user_ns(), arg);
		if (!gid_valid(group)) {
			ret = -EINVAL;
			break;
		}
		tun->group = group;
		do_notify = true;
		netif_info(tun, drv, tun->dev, "group set to %u\n",
			   from_kgid(&init_user_ns, tun->group));
		break;

	case TUNSETLINK:
		/* Only allow setting the type when the interface is down */
		if (tun->dev->flags & IFF_UP) {
			netif_info(tun, drv, tun->dev,
				   "Linktype set failed because interface is up\n");
			ret = -EBUSY;
		} else {
			ret = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE,
						       tun->dev);
			ret = notifier_to_errno(ret);
			if (ret) {
				netif_info(tun, drv, tun->dev,
					   "Refused to change device type\n");
				break;
			}
			tun->dev->type = (int) arg;
			tun->dev->addr_len = tun_get_addr_len(tun->dev->type);
			netif_info(tun, drv, tun->dev, "linktype set to %d\n",
				   tun->dev->type);
			call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE,
						 tun->dev);
		}
		break;

	case TUNSETDEBUG:
		tun->msg_enable = (u32)arg;
		break;

	case TUNSETOFFLOAD:
		ret = set_offload(tun, arg);
		break;

	case TUNSETTXFILTER:
		/* Can be set only for TAPs */
		ret = -EINVAL;
		if ((tun->flags & TUN_TYPE_MASK) != IFF_TAP)
			break;
		ret = update_filter(&tun->txflt, (void __user *)arg);
		break;

	case SIOCGIFHWADDR:
		/* Get hw address */
		dev_get_mac_address(&ifr.ifr_hwaddr, net, tun->dev->name);
		if (copy_to_user(argp, &ifr, ifreq_len))
			ret = -EFAULT;
		break;

	case SIOCSIFHWADDR:
		/* Set hw address */
		ret = dev_set_mac_address_user(tun->dev, &ifr.ifr_hwaddr, NULL);
		break;

	case TUNGETSNDBUF:
		sndbuf = tfile->socket.sk->sk_sndbuf;
		if (copy_to_user(argp, &sndbuf, sizeof(sndbuf)))
			ret = -EFAULT;
		break;

	case TUNSETSNDBUF:
		if (copy_from_user(&sndbuf, argp, sizeof(sndbuf))) {
			ret = -EFAULT;
			break;
		}
		if (sndbuf <= 0) {
			ret = -EINVAL;
			break;
		}

		tun->sndbuf = sndbuf;
		tun_set_sndbuf(tun);
		break;

	case TUNGETVNETHDRSZ:
		vnet_hdr_sz = tun->vnet_hdr_sz;
		if (copy_to_user(argp, &vnet_hdr_sz, sizeof(vnet_hdr_sz)))
			ret = -EFAULT;
		break;

	case TUNSETVNETHDRSZ:
		if (copy_from_user(&vnet_hdr_sz, argp, sizeof(vnet_hdr_sz))) {
			ret = -EFAULT;
			break;
		}
		if (vnet_hdr_sz < (int)sizeof(struct virtio_net_hdr)) {
			ret = -EINVAL;
			break;
		}

		tun->vnet_hdr_sz = vnet_hdr_sz;
		break;

	case TUNGETVNETLE:
		le = !!(tun->flags & TUN_VNET_LE);
		if (put_user(le, (int __user *)argp))
			ret = -EFAULT;
		break;

	case TUNSETVNETLE:
		if (get_user(le, (int __user *)argp)) {
			ret = -EFAULT;
			break;
		}
		if (le)
			tun->flags |= TUN_VNET_LE;
		else
			tun->flags &= ~TUN_VNET_LE;
		break;

	case TUNGETVNETBE:
		ret = tun_get_vnet_be(tun, argp);
		break;

	case TUNSETVNETBE:
		ret = tun_set_vnet_be(tun, argp);
		break;

	case TUNATTACHFILTER:
		/* Can be set only for TAPs */
		ret = -EINVAL;
		if ((tun->flags & TUN_TYPE_MASK) != IFF_TAP)
			break;
		ret = -EFAULT;
		if (copy_from_user(&tun->fprog, argp, sizeof(tun->fprog)))
			break;

		ret = tun_attach_filter(tun);
		break;

	case TUNDETACHFILTER:
		/* Can be set only for TAPs */
		ret = -EINVAL;
		if ((tun->flags & TUN_TYPE_MASK) != IFF_TAP)
			break;
		ret = 0;
		tun_detach_filter(tun, tun->numqueues);
		break;

	case TUNGETFILTER:
		ret = -EINVAL;
		if ((tun->flags & TUN_TYPE_MASK) != IFF_TAP)
			break;
		ret = -EFAULT;
		if (copy_to_user(argp, &tun->fprog, sizeof(tun->fprog)))
			break;
		ret = 0;
		break;

	case TUNSETSTEERINGEBPF:
		ret = tun_set_ebpf(tun, &tun->steering_prog, argp);
		break;

	case TUNSETFILTEREBPF:
		ret = tun_set_ebpf(tun, &tun->filter_prog, argp);
		break;

	case TUNSETCARRIER:
		ret = -EFAULT;
		if (copy_from_user(&carrier, argp, sizeof(carrier)))
			goto unlock;

		ret = tun_net_change_carrier(tun->dev, (bool)carrier);
		break;

	case TUNGETDEVNETNS:
		ret = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto unlock;
		ret = open_related_ns(&net->ns, get_net_ns);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (do_notify)
		netdev_state_change(tun->dev);

unlock:
	rtnl_unlock();
	if (tun)
		tun_put(tun);
	return ret;
}

static long tun_chr_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	return __tun_chr_ioctl(file, cmd, arg, sizeof (struct ifreq));
}

#ifdef CONFIG_COMPAT
static long tun_chr_compat_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TUNSETIFF:
	case TUNGETIFF:
	case TUNSETTXFILTER:
	case TUNGETSNDBUF:
	case TUNSETSNDBUF:
	case SIOCGIFHWADDR:
	case SIOCSIFHWADDR:
		arg = (unsigned long)compat_ptr(arg);
		break;
	default:
		arg = (compat_ulong_t)arg;
		break;
	}

	/*
	 * compat_ifreq is shorter than ifreq, so we must not access beyond
	 * the end of that structure. All fields that are used in this
	 * driver are compatible though, we don't need to convert the
	 * contents.
	 */
	return __tun_chr_ioctl(file, cmd, arg, sizeof(struct compat_ifreq));
}
#endif /* CONFIG_COMPAT */

static int tun_chr_fasync(int fd, struct file *file, int on)
{
	struct tun_file *tfile = file->private_data;
	int ret;

	if ((ret = fasync_helper(fd, file, on, &tfile->fasync)) < 0)
		goto out;

	if (on) {
		__f_setown(file, task_pid(current), PIDTYPE_TGID, 0);
		tfile->flags |= TUN_FASYNC;
	} else
		tfile->flags &= ~TUN_FASYNC;
	ret = 0;
out:
	return ret;
}

static int tun_chr_open(struct inode *inode, struct file * file)
{
	struct net *net = current->nsproxy->net_ns;
	struct tun_file *tfile;

	tfile = (struct tun_file *)sk_alloc(net, AF_UNSPEC, GFP_KERNEL,
					    &tun_proto, 0);
	if (!tfile)
		return -ENOMEM;
	if (ptr_ring_init(&tfile->tx_ring, 0, GFP_KERNEL)) {
		sk_free(&tfile->sk);
		return -ENOMEM;
	}

	mutex_init(&tfile->napi_mutex);
	RCU_INIT_POINTER(tfile->tun, NULL);
	tfile->flags = 0;
	tfile->ifindex = 0;

	init_waitqueue_head(&tfile->socket.wq.wait);

	tfile->socket.file = file;
	tfile->socket.ops = &tun_socket_ops;

	sock_init_data(&tfile->socket, &tfile->sk);

	tfile->sk.sk_write_space = tun_sock_write_space;
	tfile->sk.sk_sndbuf = INT_MAX;

	file->private_data = tfile;
	INIT_LIST_HEAD(&tfile->next);

	sock_set_flag(&tfile->sk, SOCK_ZEROCOPY);

	return 0;
}

static int tun_chr_close(struct inode *inode, struct file *file)
{
	struct tun_file *tfile = file->private_data;

	tun_detach(tfile, true);

	return 0;
}

#ifdef CONFIG_PROC_FS
static void tun_chr_show_fdinfo(struct seq_file *m, struct file *file)
{
	struct tun_file *tfile = file->private_data;
	struct tun_struct *tun;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));

	rtnl_lock();
	tun = tun_get(tfile);
	if (tun)
		tun_get_iff(tun, &ifr);
	rtnl_unlock();

	if (tun)
		tun_put(tun);

	seq_printf(m, "iff:\t%s\n", ifr.ifr_name);
}
#endif

static const struct file_operations tun_fops = {
	.owner	= THIS_MODULE,
	.llseek = no_llseek,
	.read_iter  = tun_chr_read_iter,
	.write_iter = tun_chr_write_iter,
	.poll	= tun_chr_poll,
	.unlocked_ioctl	= tun_chr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tun_chr_compat_ioctl,
#endif
	.open	= tun_chr_open,
	.release = tun_chr_close,
	.fasync = tun_chr_fasync,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = tun_chr_show_fdinfo,
#endif
};

static struct miscdevice tun_miscdev = {
	.minor = TUN_MINOR,
	.name = "tun",
	.nodename = "net/tun",
	.fops = &tun_fops,
};

/* ethtool interface */

static void tun_default_link_ksettings(struct net_device *dev,
				       struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	cmd->base.speed		= SPEED_10;
	cmd->base.duplex	= DUPLEX_FULL;
	cmd->base.port		= PORT_TP;
	cmd->base.phy_address	= 0;
	cmd->base.autoneg	= AUTONEG_DISABLE;
}

static int tun_get_link_ksettings(struct net_device *dev,
				  struct ethtool_link_ksettings *cmd)
{
	struct tun_struct *tun = netdev_priv(dev);

	memcpy(cmd, &tun->link_ksettings, sizeof(*cmd));
	return 0;
}

static int tun_set_link_ksettings(struct net_device *dev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct tun_struct *tun = netdev_priv(dev);

	memcpy(&tun->link_ksettings, cmd, sizeof(*cmd));
	return 0;
}

static void tun_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tun_struct *tun = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));

	switch (tun->flags & TUN_TYPE_MASK) {
	case IFF_TUN:
		strlcpy(info->bus_info, "tun", sizeof(info->bus_info));
		break;
	case IFF_TAP:
		strlcpy(info->bus_info, "tap", sizeof(info->bus_info));
		break;
	}
}

static u32 tun_get_msglevel(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	return tun->msg_enable;
}

static void tun_set_msglevel(struct net_device *dev, u32 value)
{
	struct tun_struct *tun = netdev_priv(dev);

	tun->msg_enable = value;
}

static int tun_get_coalesce(struct net_device *dev,
			    struct ethtool_coalesce *ec,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct tun_struct *tun = netdev_priv(dev);

	ec->rx_max_coalesced_frames = tun->rx_batched;

	return 0;
}

static int tun_set_coalesce(struct net_device *dev,
			    struct ethtool_coalesce *ec,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct tun_struct *tun = netdev_priv(dev);

	if (ec->rx_max_coalesced_frames > NAPI_POLL_WEIGHT)
		tun->rx_batched = NAPI_POLL_WEIGHT;
	else
		tun->rx_batched = ec->rx_max_coalesced_frames;

	return 0;
}

static const struct ethtool_ops tun_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_MAX_FRAMES,
	.get_drvinfo	= tun_get_drvinfo,
	.get_msglevel	= tun_get_msglevel,
	.set_msglevel	= tun_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= ethtool_op_get_ts_info,
	.get_coalesce   = tun_get_coalesce,
	.set_coalesce   = tun_set_coalesce,
	.get_link_ksettings = tun_get_link_ksettings,
	.set_link_ksettings = tun_set_link_ksettings,
};

static int tun_queue_resize(struct tun_struct *tun)
{
	struct net_device *dev = tun->dev;
	struct tun_file *tfile;
	struct ptr_ring **rings;
	int n = tun->numqueues + tun->numdisabled;
	int ret, i;

	rings = kmalloc_array(n, sizeof(*rings), GFP_KERNEL);
	if (!rings)
		return -ENOMEM;

	for (i = 0; i < tun->numqueues; i++) {
		tfile = rtnl_dereference(tun->tfiles[i]);
		rings[i] = &tfile->tx_ring;
	}
	list_for_each_entry(tfile, &tun->disabled, next)
		rings[i++] = &tfile->tx_ring;

	ret = ptr_ring_resize_multiple(rings, n,
				       dev->tx_queue_len, GFP_KERNEL,
				       tun_ptr_free);

	kfree(rings);
	return ret;
}

static int tun_device_event(struct notifier_block *unused,
			    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tun_struct *tun = netdev_priv(dev);
	int i;

	if (dev->rtnl_link_ops != &tun_link_ops)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGE_TX_QUEUE_LEN:
		if (tun_queue_resize(tun))
			return NOTIFY_BAD;
		break;
	case NETDEV_UP:
		for (i = 0; i < tun->numqueues; i++) {
			struct tun_file *tfile;

			tfile = rtnl_dereference(tun->tfiles[i]);
			tfile->socket.sk->sk_write_space(tfile->socket.sk);
		}
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block tun_notifier_block __read_mostly = {
	.notifier_call	= tun_device_event,
};

static int __init tun_init(void)
{
	int ret = 0;

	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);

	ret = rtnl_link_register(&tun_link_ops);
	if (ret) {
		pr_err("Can't register link_ops\n");
		goto err_linkops;
	}

	ret = misc_register(&tun_miscdev);
	if (ret) {
		pr_err("Can't register misc device %d\n", TUN_MINOR);
		goto err_misc;
	}

	ret = register_netdevice_notifier(&tun_notifier_block);
	if (ret) {
		pr_err("Can't register netdevice notifier\n");
		goto err_notifier;
	}

	return  0;

err_notifier:
	misc_deregister(&tun_miscdev);
err_misc:
	rtnl_link_unregister(&tun_link_ops);
err_linkops:
	return ret;
}

static void tun_cleanup(void)
{
	misc_deregister(&tun_miscdev);
	rtnl_link_unregister(&tun_link_ops);
	unregister_netdevice_notifier(&tun_notifier_block);
}

/* Get an underlying socket object from tun file.  Returns error unless file is
 * attached to a device.  The returned object works like a packet socket, it
 * can be used for sock_sendmsg/sock_recvmsg.  The caller is responsible for
 * holding a reference to the file for as long as the socket is in use. */
struct socket *tun_get_socket(struct file *file)
{
	struct tun_file *tfile;
	if (file->f_op != &tun_fops)
		return ERR_PTR(-EINVAL);
	tfile = file->private_data;
	if (!tfile)
		return ERR_PTR(-EBADFD);
	return &tfile->socket;
}
EXPORT_SYMBOL_GPL(tun_get_socket);

struct ptr_ring *tun_get_tx_ring(struct file *file)
{
	struct tun_file *tfile;

	if (file->f_op != &tun_fops)
		return ERR_PTR(-EINVAL);
	tfile = file->private_data;
	if (!tfile)
		return ERR_PTR(-EBADFD);
	return &tfile->tx_ring;
}
EXPORT_SYMBOL_GPL(tun_get_tx_ring);

module_init(tun_init);
module_exit(tun_cleanup);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(TUN_MINOR);
MODULE_ALIAS("devname:net/tun");
