#include <linux/etherdevice.h>
#include <linux/if_tap.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/nsproxy.h>
#include <linux/compat.h>
#include <linux/if_tun.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/cache.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/uio.h>

#include <net/net_namespace.h>
#include <net/rtnetlink.h>
#include <net/sock.h>
#include <linux/virtio_net.h>
#include <linux/skb_array.h>

#define TAP_IFFEATURES (IFF_VNET_HDR | IFF_MULTI_QUEUE)

#define TAP_VNET_LE 0x80000000
#define TAP_VNET_BE 0x40000000

#ifdef CONFIG_TUN_VNET_CROSS_LE
static inline bool tap_legacy_is_little_endian(struct tap_queue *q)
{
	return q->flags & TAP_VNET_BE ? false :
		virtio_legacy_is_little_endian();
}

static long tap_get_vnet_be(struct tap_queue *q, int __user *sp)
{
	int s = !!(q->flags & TAP_VNET_BE);

	if (put_user(s, sp))
		return -EFAULT;

	return 0;
}

static long tap_set_vnet_be(struct tap_queue *q, int __user *sp)
{
	int s;

	if (get_user(s, sp))
		return -EFAULT;

	if (s)
		q->flags |= TAP_VNET_BE;
	else
		q->flags &= ~TAP_VNET_BE;

	return 0;
}
#else
static inline bool tap_legacy_is_little_endian(struct tap_queue *q)
{
	return virtio_legacy_is_little_endian();
}

static long tap_get_vnet_be(struct tap_queue *q, int __user *argp)
{
	return -EINVAL;
}

static long tap_set_vnet_be(struct tap_queue *q, int __user *argp)
{
	return -EINVAL;
}
#endif /* CONFIG_TUN_VNET_CROSS_LE */

static inline bool tap_is_little_endian(struct tap_queue *q)
{
	return q->flags & TAP_VNET_LE ||
		tap_legacy_is_little_endian(q);
}

static inline u16 tap16_to_cpu(struct tap_queue *q, __virtio16 val)
{
	return __virtio16_to_cpu(tap_is_little_endian(q), val);
}

static inline __virtio16 cpu_to_tap16(struct tap_queue *q, u16 val)
{
	return __cpu_to_virtio16(tap_is_little_endian(q), val);
}

static struct proto tap_proto = {
	.name = "tap",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct tap_queue),
};

#define TAP_NUM_DEVS (1U << MINORBITS)

static LIST_HEAD(major_list);

struct major_info {
	struct rcu_head rcu;
	dev_t major;
	struct idr minor_idr;
	spinlock_t minor_lock;
	const char *device_name;
	struct list_head next;
};

#define GOODCOPY_LEN 128

static const struct proto_ops tap_socket_ops;

#define RX_OFFLOADS (NETIF_F_GRO | NETIF_F_LRO)
#define TAP_FEATURES (NETIF_F_GSO | NETIF_F_SG | NETIF_F_FRAGLIST)

static struct tap_dev *tap_dev_get_rcu(const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler_data);
}

/*
 * RCU usage:
 * The tap_queue and the macvlan_dev are loosely coupled, the
 * pointers from one to the other can only be read while rcu_read_lock
 * or rtnl is held.
 *
 * Both the file and the macvlan_dev hold a reference on the tap_queue
 * through sock_hold(&q->sk). When the macvlan_dev goes away first,
 * q->vlan becomes inaccessible. When the files gets closed,
 * tap_get_queue() fails.
 *
 * There may still be references to the struct sock inside of the
 * queue from outbound SKBs, but these never reference back to the
 * file or the dev. The data structure is freed through __sk_free
 * when both our references and any pending SKBs are gone.
 */

static int tap_enable_queue(struct tap_dev *tap, struct file *file,
			    struct tap_queue *q)
{
	int err = -EINVAL;

	ASSERT_RTNL();

	if (q->enabled)
		goto out;

	err = 0;
	rcu_assign_pointer(tap->taps[tap->numvtaps], q);
	q->queue_index = tap->numvtaps;
	q->enabled = true;

	tap->numvtaps++;
out:
	return err;
}

/* Requires RTNL */
static int tap_set_queue(struct tap_dev *tap, struct file *file,
			 struct tap_queue *q)
{
	if (tap->numqueues == MAX_TAP_QUEUES)
		return -EBUSY;

	rcu_assign_pointer(q->tap, tap);
	rcu_assign_pointer(tap->taps[tap->numvtaps], q);
	sock_hold(&q->sk);

	q->file = file;
	q->queue_index = tap->numvtaps;
	q->enabled = true;
	file->private_data = q;
	list_add_tail(&q->next, &tap->queue_list);

	tap->numvtaps++;
	tap->numqueues++;

	return 0;
}

static int tap_disable_queue(struct tap_queue *q)
{
	struct tap_dev *tap;
	struct tap_queue *nq;

	ASSERT_RTNL();
	if (!q->enabled)
		return -EINVAL;

	tap = rtnl_dereference(q->tap);

	if (tap) {
		int index = q->queue_index;
		BUG_ON(index >= tap->numvtaps);
		nq = rtnl_dereference(tap->taps[tap->numvtaps - 1]);
		nq->queue_index = index;

		rcu_assign_pointer(tap->taps[index], nq);
		RCU_INIT_POINTER(tap->taps[tap->numvtaps - 1], NULL);
		q->enabled = false;

		tap->numvtaps--;
	}

	return 0;
}

/*
 * The file owning the queue got closed, give up both
 * the reference that the files holds as well as the
 * one from the macvlan_dev if that still exists.
 *
 * Using the spinlock makes sure that we don't get
 * to the queue again after destroying it.
 */
static void tap_put_queue(struct tap_queue *q)
{
	struct tap_dev *tap;

	rtnl_lock();
	tap = rtnl_dereference(q->tap);

	if (tap) {
		if (q->enabled)
			BUG_ON(tap_disable_queue(q));

		tap->numqueues--;
		RCU_INIT_POINTER(q->tap, NULL);
		sock_put(&q->sk);
		list_del_init(&q->next);
	}

	rtnl_unlock();

	synchronize_rcu();
	sock_put(&q->sk);
}

/*
 * Select a queue based on the rxq of the device on which this packet
 * arrived. If the incoming device is not mq, calculate a flow hash
 * to select a queue. If all fails, find the first available queue.
 * Cache vlan->numvtaps since it can become zero during the execution
 * of this function.
 */
static struct tap_queue *tap_get_queue(struct tap_dev *tap,
				       struct sk_buff *skb)
{
	struct tap_queue *queue = NULL;
	/* Access to taps array is protected by rcu, but access to numvtaps
	 * isn't. Below we use it to lookup a queue, but treat it as a hint
	 * and validate that the result isn't NULL - in case we are
	 * racing against queue removal.
	 */
	int numvtaps = READ_ONCE(tap->numvtaps);
	__u32 rxq;

	if (!numvtaps)
		goto out;

	if (numvtaps == 1)
		goto single;

	/* Check if we can use flow to select a queue */
	rxq = skb_get_hash(skb);
	if (rxq) {
		queue = rcu_dereference(tap->taps[rxq % numvtaps]);
		goto out;
	}

	if (likely(skb_rx_queue_recorded(skb))) {
		rxq = skb_get_rx_queue(skb);

		while (unlikely(rxq >= numvtaps))
			rxq -= numvtaps;

		queue = rcu_dereference(tap->taps[rxq]);
		goto out;
	}

single:
	queue = rcu_dereference(tap->taps[0]);
out:
	return queue;
}

/*
 * The net_device is going away, give up the reference
 * that it holds on all queues and safely set the pointer
 * from the queues to NULL.
 */
void tap_del_queues(struct tap_dev *tap)
{
	struct tap_queue *q, *tmp;

	ASSERT_RTNL();
	list_for_each_entry_safe(q, tmp, &tap->queue_list, next) {
		list_del_init(&q->next);
		RCU_INIT_POINTER(q->tap, NULL);
		if (q->enabled)
			tap->numvtaps--;
		tap->numqueues--;
		sock_put(&q->sk);
	}
	BUG_ON(tap->numvtaps);
	BUG_ON(tap->numqueues);
	/* guarantee that any future tap_set_queue will fail */
	tap->numvtaps = MAX_TAP_QUEUES;
}
EXPORT_SYMBOL_GPL(tap_del_queues);

rx_handler_result_t tap_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct net_device *dev = skb->dev;
	struct tap_dev *tap;
	struct tap_queue *q;
	netdev_features_t features = TAP_FEATURES;

	tap = tap_dev_get_rcu(dev);
	if (!tap)
		return RX_HANDLER_PASS;

	q = tap_get_queue(tap, skb);
	if (!q)
		return RX_HANDLER_PASS;

	skb_push(skb, ETH_HLEN);

	/* Apply the forward feature mask so that we perform segmentation
	 * according to users wishes.  This only works if VNET_HDR is
	 * enabled.
	 */
	if (q->flags & IFF_VNET_HDR)
		features |= tap->tap_features;
	if (netif_needs_gso(skb, features)) {
		struct sk_buff *segs = __skb_gso_segment(skb, features, false);

		if (IS_ERR(segs))
			goto drop;

		if (!segs) {
			if (ptr_ring_produce(&q->ring, skb))
				goto drop;
			goto wake_up;
		}

		consume_skb(skb);
		while (segs) {
			struct sk_buff *nskb = segs->next;

			segs->next = NULL;
			if (ptr_ring_produce(&q->ring, segs)) {
				kfree_skb(segs);
				kfree_skb_list(nskb);
				break;
			}
			segs = nskb;
		}
	} else {
		/* If we receive a partial checksum and the tap side
		 * doesn't support checksum offload, compute the checksum.
		 * Note: it doesn't matter which checksum feature to
		 *	  check, we either support them all or none.
		 */
		if (skb->ip_summed == CHECKSUM_PARTIAL &&
		    !(features & NETIF_F_CSUM_MASK) &&
		    skb_checksum_help(skb))
			goto drop;
		if (ptr_ring_produce(&q->ring, skb))
			goto drop;
	}

wake_up:
	wake_up_interruptible_poll(sk_sleep(&q->sk), EPOLLIN | EPOLLRDNORM | EPOLLRDBAND);
	return RX_HANDLER_CONSUMED;

drop:
	/* Count errors/drops only here, thus don't care about args. */
	if (tap->count_rx_dropped)
		tap->count_rx_dropped(tap);
	kfree_skb(skb);
	return RX_HANDLER_CONSUMED;
}
EXPORT_SYMBOL_GPL(tap_handle_frame);

static struct major_info *tap_get_major(int major)
{
	struct major_info *tap_major;

	list_for_each_entry_rcu(tap_major, &major_list, next) {
		if (tap_major->major == major)
			return tap_major;
	}

	return NULL;
}

int tap_get_minor(dev_t major, struct tap_dev *tap)
{
	int retval = -ENOMEM;
	struct major_info *tap_major;

	rcu_read_lock();
	tap_major = tap_get_major(MAJOR(major));
	if (!tap_major) {
		retval = -EINVAL;
		goto unlock;
	}

	spin_lock(&tap_major->minor_lock);
	retval = idr_alloc(&tap_major->minor_idr, tap, 1, TAP_NUM_DEVS, GFP_ATOMIC);
	if (retval >= 0) {
		tap->minor = retval;
	} else if (retval == -ENOSPC) {
		netdev_err(tap->dev, "Too many tap devices\n");
		retval = -EINVAL;
	}
	spin_unlock(&tap_major->minor_lock);

unlock:
	rcu_read_unlock();
	return retval < 0 ? retval : 0;
}
EXPORT_SYMBOL_GPL(tap_get_minor);

void tap_free_minor(dev_t major, struct tap_dev *tap)
{
	struct major_info *tap_major;

	rcu_read_lock();
	tap_major = tap_get_major(MAJOR(major));
	if (!tap_major) {
		goto unlock;
	}

	spin_lock(&tap_major->minor_lock);
	if (tap->minor) {
		idr_remove(&tap_major->minor_idr, tap->minor);
		tap->minor = 0;
	}
	spin_unlock(&tap_major->minor_lock);

unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(tap_free_minor);

static struct tap_dev *dev_get_by_tap_file(int major, int minor)
{
	struct net_device *dev = NULL;
	struct tap_dev *tap;
	struct major_info *tap_major;

	rcu_read_lock();
	tap_major = tap_get_major(major);
	if (!tap_major) {
		tap = NULL;
		goto unlock;
	}

	spin_lock(&tap_major->minor_lock);
	tap = idr_find(&tap_major->minor_idr, minor);
	if (tap) {
		dev = tap->dev;
		dev_hold(dev);
	}
	spin_unlock(&tap_major->minor_lock);

unlock:
	rcu_read_unlock();
	return tap;
}

static void tap_sock_write_space(struct sock *sk)
{
	wait_queue_head_t *wqueue;

	if (!sock_writeable(sk) ||
	    !test_and_clear_bit(SOCKWQ_ASYNC_NOSPACE, &sk->sk_socket->flags))
		return;

	wqueue = sk_sleep(sk);
	if (wqueue && waitqueue_active(wqueue))
		wake_up_interruptible_poll(wqueue, EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND);
}

static void tap_sock_destruct(struct sock *sk)
{
	struct tap_queue *q = container_of(sk, struct tap_queue, sk);

	ptr_ring_cleanup(&q->ring, __skb_array_destroy_skb);
}

static int tap_open(struct inode *inode, struct file *file)
{
	struct net *net = current->nsproxy->net_ns;
	struct tap_dev *tap;
	struct tap_queue *q;
	int err = -ENODEV;

	rtnl_lock();
	tap = dev_get_by_tap_file(imajor(inode), iminor(inode));
	if (!tap)
		goto err;

	err = -ENOMEM;
	q = (struct tap_queue *)sk_alloc(net, AF_UNSPEC, GFP_KERNEL,
					     &tap_proto, 0);
	if (!q)
		goto err;
	if (ptr_ring_init(&q->ring, tap->dev->tx_queue_len, GFP_KERNEL)) {
		sk_free(&q->sk);
		goto err;
	}

	RCU_INIT_POINTER(q->sock.wq, &q->wq);
	init_waitqueue_head(&q->wq.wait);
	q->sock.type = SOCK_RAW;
	q->sock.state = SS_CONNECTED;
	q->sock.file = file;
	q->sock.ops = &tap_socket_ops;
	sock_init_data(&q->sock, &q->sk);
	q->sk.sk_write_space = tap_sock_write_space;
	q->sk.sk_destruct = tap_sock_destruct;
	q->flags = IFF_VNET_HDR | IFF_NO_PI | IFF_TAP;
	q->vnet_hdr_sz = sizeof(struct virtio_net_hdr);

	/*
	 * so far only KVM virtio_net uses tap, enable zero copy between
	 * guest kernel and host kernel when lower device supports zerocopy
	 *
	 * The macvlan supports zerocopy iff the lower device supports zero
	 * copy so we don't have to look at the lower device directly.
	 */
	if ((tap->dev->features & NETIF_F_HIGHDMA) && (tap->dev->features & NETIF_F_SG))
		sock_set_flag(&q->sk, SOCK_ZEROCOPY);

	err = tap_set_queue(tap, file, q);
	if (err) {
		/* tap_sock_destruct() will take care of freeing ptr_ring */
		goto err_put;
	}

	dev_put(tap->dev);

	rtnl_unlock();
	return err;

err_put:
	sock_put(&q->sk);
err:
	if (tap)
		dev_put(tap->dev);

	rtnl_unlock();
	return err;
}

static int tap_release(struct inode *inode, struct file *file)
{
	struct tap_queue *q = file->private_data;
	tap_put_queue(q);
	return 0;
}

static __poll_t tap_poll(struct file *file, poll_table *wait)
{
	struct tap_queue *q = file->private_data;
	__poll_t mask = EPOLLERR;

	if (!q)
		goto out;

	mask = 0;
	poll_wait(file, &q->wq.wait, wait);

	if (!ptr_ring_empty(&q->ring))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (sock_writeable(&q->sk) ||
	    (!test_and_set_bit(SOCKWQ_ASYNC_NOSPACE, &q->sock.flags) &&
	     sock_writeable(&q->sk)))
		mask |= EPOLLOUT | EPOLLWRNORM;

out:
	return mask;
}

static inline struct sk_buff *tap_alloc_skb(struct sock *sk, size_t prepad,
					    size_t len, size_t linear,
						int noblock, int *err)
{
	struct sk_buff *skb;

	/* Under a page?  Don't bother with paged skb. */
	if (prepad + len < PAGE_SIZE || !linear)
		linear = len;

	skb = sock_alloc_send_pskb(sk, prepad + linear, len - linear, noblock,
				   err, 0);
	if (!skb)
		return NULL;

	skb_reserve(skb, prepad);
	skb_put(skb, linear);
	skb->data_len = len - linear;
	skb->len += len - linear;

	return skb;
}

/* Neighbour code has some assumptions on HH_DATA_MOD alignment */
#define TAP_RESERVE HH_DATA_OFF(ETH_HLEN)

/* Get packet from user space buffer */
static ssize_t tap_get_user(struct tap_queue *q, void *msg_control,
			    struct iov_iter *from, int noblock)
{
	int good_linear = SKB_MAX_HEAD(TAP_RESERVE);
	struct sk_buff *skb;
	struct tap_dev *tap;
	unsigned long total_len = iov_iter_count(from);
	unsigned long len = total_len;
	int err;
	struct virtio_net_hdr vnet_hdr = { 0 };
	int vnet_hdr_len = 0;
	int copylen = 0;
	int depth;
	bool zerocopy = false;
	size_t linear;

	if (q->flags & IFF_VNET_HDR) {
		vnet_hdr_len = READ_ONCE(q->vnet_hdr_sz);

		err = -EINVAL;
		if (len < vnet_hdr_len)
			goto err;
		len -= vnet_hdr_len;

		err = -EFAULT;
		if (!copy_from_iter_full(&vnet_hdr, sizeof(vnet_hdr), from))
			goto err;
		iov_iter_advance(from, vnet_hdr_len - sizeof(vnet_hdr));
		if ((vnet_hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) &&
		     tap16_to_cpu(q, vnet_hdr.csum_start) +
		     tap16_to_cpu(q, vnet_hdr.csum_offset) + 2 >
			     tap16_to_cpu(q, vnet_hdr.hdr_len))
			vnet_hdr.hdr_len = cpu_to_tap16(q,
				 tap16_to_cpu(q, vnet_hdr.csum_start) +
				 tap16_to_cpu(q, vnet_hdr.csum_offset) + 2);
		err = -EINVAL;
		if (tap16_to_cpu(q, vnet_hdr.hdr_len) > len)
			goto err;
	}

	err = -EINVAL;
	if (unlikely(len < ETH_HLEN))
		goto err;

	if (msg_control && sock_flag(&q->sk, SOCK_ZEROCOPY)) {
		struct iov_iter i;

		copylen = vnet_hdr.hdr_len ?
			tap16_to_cpu(q, vnet_hdr.hdr_len) : GOODCOPY_LEN;
		if (copylen > good_linear)
			copylen = good_linear;
		else if (copylen < ETH_HLEN)
			copylen = ETH_HLEN;
		linear = copylen;
		i = *from;
		iov_iter_advance(&i, copylen);
		if (iov_iter_npages(&i, INT_MAX) <= MAX_SKB_FRAGS)
			zerocopy = true;
	}

	if (!zerocopy) {
		copylen = len;
		linear = tap16_to_cpu(q, vnet_hdr.hdr_len);
		if (linear > good_linear)
			linear = good_linear;
		else if (linear < ETH_HLEN)
			linear = ETH_HLEN;
	}

	skb = tap_alloc_skb(&q->sk, TAP_RESERVE, copylen,
			    linear, noblock, &err);
	if (!skb)
		goto err;

	if (zerocopy)
		err = zerocopy_sg_from_iter(skb, from);
	else
		err = skb_copy_datagram_from_iter(skb, 0, from, len);

	if (err)
		goto err_kfree;

	skb_set_network_header(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	skb->protocol = eth_hdr(skb)->h_proto;

	if (vnet_hdr_len) {
		err = virtio_net_hdr_to_skb(skb, &vnet_hdr,
					    tap_is_little_endian(q));
		if (err)
			goto err_kfree;
	}

	skb_probe_transport_header(skb, ETH_HLEN);

	/* Move network header to the right position for VLAN tagged packets */
	if ((skb->protocol == htons(ETH_P_8021Q) ||
	     skb->protocol == htons(ETH_P_8021AD)) &&
	    __vlan_get_protocol(skb, skb->protocol, &depth) != 0)
		skb_set_network_header(skb, depth);

	rcu_read_lock();
	tap = rcu_dereference(q->tap);
	/* copy skb_ubuf_info for callback when skb has no error */
	if (zerocopy) {
		skb_shinfo(skb)->destructor_arg = msg_control;
		skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
		skb_shinfo(skb)->tx_flags |= SKBTX_SHARED_FRAG;
	} else if (msg_control) {
		struct ubuf_info *uarg = msg_control;
		uarg->callback(uarg, false);
	}

	if (tap) {
		skb->dev = tap->dev;
		dev_queue_xmit(skb);
	} else {
		kfree_skb(skb);
	}
	rcu_read_unlock();

	return total_len;

err_kfree:
	kfree_skb(skb);

err:
	rcu_read_lock();
	tap = rcu_dereference(q->tap);
	if (tap && tap->count_tx_dropped)
		tap->count_tx_dropped(tap);
	rcu_read_unlock();

	return err;
}

static ssize_t tap_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct tap_queue *q = file->private_data;

	return tap_get_user(q, NULL, from, file->f_flags & O_NONBLOCK);
}

/* Put packet to the user space buffer */
static ssize_t tap_put_user(struct tap_queue *q,
			    const struct sk_buff *skb,
			    struct iov_iter *iter)
{
	int ret;
	int vnet_hdr_len = 0;
	int vlan_offset = 0;
	int total;

	if (q->flags & IFF_VNET_HDR) {
		int vlan_hlen = skb_vlan_tag_present(skb) ? VLAN_HLEN : 0;
		struct virtio_net_hdr vnet_hdr;

		vnet_hdr_len = READ_ONCE(q->vnet_hdr_sz);
		if (iov_iter_count(iter) < vnet_hdr_len)
			return -EINVAL;

		if (virtio_net_hdr_from_skb(skb, &vnet_hdr,
					    tap_is_little_endian(q), true,
					    vlan_hlen))
			BUG();

		if (copy_to_iter(&vnet_hdr, sizeof(vnet_hdr), iter) !=
		    sizeof(vnet_hdr))
			return -EFAULT;

		iov_iter_advance(iter, vnet_hdr_len - sizeof(vnet_hdr));
	}
	total = vnet_hdr_len;
	total += skb->len;

	if (skb_vlan_tag_present(skb)) {
		struct {
			__be16 h_vlan_proto;
			__be16 h_vlan_TCI;
		} veth;
		veth.h_vlan_proto = skb->vlan_proto;
		veth.h_vlan_TCI = htons(skb_vlan_tag_get(skb));

		vlan_offset = offsetof(struct vlan_ethhdr, h_vlan_proto);
		total += VLAN_HLEN;

		ret = skb_copy_datagram_iter(skb, 0, iter, vlan_offset);
		if (ret || !iov_iter_count(iter))
			goto done;

		ret = copy_to_iter(&veth, sizeof(veth), iter);
		if (ret != sizeof(veth) || !iov_iter_count(iter))
			goto done;
	}

	ret = skb_copy_datagram_iter(skb, vlan_offset, iter,
				     skb->len - vlan_offset);

done:
	return ret ? ret : total;
}

static ssize_t tap_do_read(struct tap_queue *q,
			   struct iov_iter *to,
			   int noblock, struct sk_buff *skb)
{
	DEFINE_WAIT(wait);
	ssize_t ret = 0;

	if (!iov_iter_count(to)) {
		kfree_skb(skb);
		return 0;
	}

	if (skb)
		goto put;

	while (1) {
		if (!noblock)
			prepare_to_wait(sk_sleep(&q->sk), &wait,
					TASK_INTERRUPTIBLE);

		/* Read frames from the queue */
		skb = ptr_ring_consume(&q->ring);
		if (skb)
			break;
		if (noblock) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		/* Nothing to read, let's sleep */
		schedule();
	}
	if (!noblock)
		finish_wait(sk_sleep(&q->sk), &wait);

put:
	if (skb) {
		ret = tap_put_user(q, skb, to);
		if (unlikely(ret < 0))
			kfree_skb(skb);
		else
			consume_skb(skb);
	}
	return ret;
}

static ssize_t tap_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct tap_queue *q = file->private_data;
	ssize_t len = iov_iter_count(to), ret;

	ret = tap_do_read(q, to, file->f_flags & O_NONBLOCK, NULL);
	ret = min_t(ssize_t, ret, len);
	if (ret > 0)
		iocb->ki_pos = ret;
	return ret;
}

static struct tap_dev *tap_get_tap_dev(struct tap_queue *q)
{
	struct tap_dev *tap;

	ASSERT_RTNL();
	tap = rtnl_dereference(q->tap);
	if (tap)
		dev_hold(tap->dev);

	return tap;
}

static void tap_put_tap_dev(struct tap_dev *tap)
{
	dev_put(tap->dev);
}

static int tap_ioctl_set_queue(struct file *file, unsigned int flags)
{
	struct tap_queue *q = file->private_data;
	struct tap_dev *tap;
	int ret;

	tap = tap_get_tap_dev(q);
	if (!tap)
		return -EINVAL;

	if (flags & IFF_ATTACH_QUEUE)
		ret = tap_enable_queue(tap, file, q);
	else if (flags & IFF_DETACH_QUEUE)
		ret = tap_disable_queue(q);
	else
		ret = -EINVAL;

	tap_put_tap_dev(tap);
	return ret;
}

static int set_offload(struct tap_queue *q, unsigned long arg)
{
	struct tap_dev *tap;
	netdev_features_t features;
	netdev_features_t feature_mask = 0;

	tap = rtnl_dereference(q->tap);
	if (!tap)
		return -ENOLINK;

	features = tap->dev->features;

	if (arg & TUN_F_CSUM) {
		feature_mask = NETIF_F_HW_CSUM;

		if (arg & (TUN_F_TSO4 | TUN_F_TSO6)) {
			if (arg & TUN_F_TSO_ECN)
				feature_mask |= NETIF_F_TSO_ECN;
			if (arg & TUN_F_TSO4)
				feature_mask |= NETIF_F_TSO;
			if (arg & TUN_F_TSO6)
				feature_mask |= NETIF_F_TSO6;
		}
	}

	/* tun/tap driver inverts the usage for TSO offloads, where
	 * setting the TSO bit means that the userspace wants to
	 * accept TSO frames and turning it off means that user space
	 * does not support TSO.
	 * For tap, we have to invert it to mean the same thing.
	 * When user space turns off TSO, we turn off GSO/LRO so that
	 * user-space will not receive TSO frames.
	 */
	if (feature_mask & (NETIF_F_TSO | NETIF_F_TSO6))
		features |= RX_OFFLOADS;
	else
		features &= ~RX_OFFLOADS;

	/* tap_features are the same as features on tun/tap and
	 * reflect user expectations.
	 */
	tap->tap_features = feature_mask;
	if (tap->update_features)
		tap->update_features(tap, features);

	return 0;
}

/*
 * provide compatibility with generic tun/tap interface
 */
static long tap_ioctl(struct file *file, unsigned int cmd,
		      unsigned long arg)
{
	struct tap_queue *q = file->private_data;
	struct tap_dev *tap;
	void __user *argp = (void __user *)arg;
	struct ifreq __user *ifr = argp;
	unsigned int __user *up = argp;
	unsigned short u;
	int __user *sp = argp;
	struct sockaddr sa;
	int s;
	int ret;

	switch (cmd) {
	case TUNSETIFF:
		/* ignore the name, just look at flags */
		if (get_user(u, &ifr->ifr_flags))
			return -EFAULT;

		ret = 0;
		if ((u & ~TAP_IFFEATURES) != (IFF_NO_PI | IFF_TAP))
			ret = -EINVAL;
		else
			q->flags = (q->flags & ~TAP_IFFEATURES) | u;

		return ret;

	case TUNGETIFF:
		rtnl_lock();
		tap = tap_get_tap_dev(q);
		if (!tap) {
			rtnl_unlock();
			return -ENOLINK;
		}

		ret = 0;
		u = q->flags;
		if (copy_to_user(&ifr->ifr_name, tap->dev->name, IFNAMSIZ) ||
		    put_user(u, &ifr->ifr_flags))
			ret = -EFAULT;
		tap_put_tap_dev(tap);
		rtnl_unlock();
		return ret;

	case TUNSETQUEUE:
		if (get_user(u, &ifr->ifr_flags))
			return -EFAULT;
		rtnl_lock();
		ret = tap_ioctl_set_queue(file, u);
		rtnl_unlock();
		return ret;

	case TUNGETFEATURES:
		if (put_user(IFF_TAP | IFF_NO_PI | TAP_IFFEATURES, up))
			return -EFAULT;
		return 0;

	case TUNSETSNDBUF:
		if (get_user(s, sp))
			return -EFAULT;
		if (s <= 0)
			return -EINVAL;

		q->sk.sk_sndbuf = s;
		return 0;

	case TUNGETVNETHDRSZ:
		s = q->vnet_hdr_sz;
		if (put_user(s, sp))
			return -EFAULT;
		return 0;

	case TUNSETVNETHDRSZ:
		if (get_user(s, sp))
			return -EFAULT;
		if (s < (int)sizeof(struct virtio_net_hdr))
			return -EINVAL;

		q->vnet_hdr_sz = s;
		return 0;

	case TUNGETVNETLE:
		s = !!(q->flags & TAP_VNET_LE);
		if (put_user(s, sp))
			return -EFAULT;
		return 0;

	case TUNSETVNETLE:
		if (get_user(s, sp))
			return -EFAULT;
		if (s)
			q->flags |= TAP_VNET_LE;
		else
			q->flags &= ~TAP_VNET_LE;
		return 0;

	case TUNGETVNETBE:
		return tap_get_vnet_be(q, sp);

	case TUNSETVNETBE:
		return tap_set_vnet_be(q, sp);

	case TUNSETOFFLOAD:
		/* let the user check for future flags */
		if (arg & ~(TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6 |
			    TUN_F_TSO_ECN | TUN_F_UFO))
			return -EINVAL;

		rtnl_lock();
		ret = set_offload(q, arg);
		rtnl_unlock();
		return ret;

	case SIOCGIFHWADDR:
		rtnl_lock();
		tap = tap_get_tap_dev(q);
		if (!tap) {
			rtnl_unlock();
			return -ENOLINK;
		}
		ret = 0;
		u = tap->dev->type;
		if (copy_to_user(&ifr->ifr_name, tap->dev->name, IFNAMSIZ) ||
		    copy_to_user(&ifr->ifr_hwaddr.sa_data, tap->dev->dev_addr, ETH_ALEN) ||
		    put_user(u, &ifr->ifr_hwaddr.sa_family))
			ret = -EFAULT;
		tap_put_tap_dev(tap);
		rtnl_unlock();
		return ret;

	case SIOCSIFHWADDR:
		if (copy_from_user(&sa, &ifr->ifr_hwaddr, sizeof(sa)))
			return -EFAULT;
		rtnl_lock();
		tap = tap_get_tap_dev(q);
		if (!tap) {
			rtnl_unlock();
			return -ENOLINK;
		}
		ret = dev_set_mac_address(tap->dev, &sa, NULL);
		tap_put_tap_dev(tap);
		rtnl_unlock();
		return ret;

	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static long tap_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return tap_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations tap_fops = {
	.owner		= THIS_MODULE,
	.open		= tap_open,
	.release	= tap_release,
	.read_iter	= tap_read_iter,
	.write_iter	= tap_write_iter,
	.poll		= tap_poll,
	.llseek		= no_llseek,
	.unlocked_ioctl	= tap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= tap_compat_ioctl,
#endif
};

static int tap_get_user_xdp(struct tap_queue *q, struct xdp_buff *xdp)
{
	struct tun_xdp_hdr *hdr = xdp->data_hard_start;
	struct virtio_net_hdr *gso = &hdr->gso;
	int buflen = hdr->buflen;
	int vnet_hdr_len = 0;
	struct tap_dev *tap;
	struct sk_buff *skb;
	int err, depth;

	if (q->flags & IFF_VNET_HDR)
		vnet_hdr_len = READ_ONCE(q->vnet_hdr_sz);

	skb = build_skb(xdp->data_hard_start, buflen);
	if (!skb) {
		err = -ENOMEM;
		goto err;
	}

	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	skb_put(skb, xdp->data_end - xdp->data);

	skb_set_network_header(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	skb->protocol = eth_hdr(skb)->h_proto;

	if (vnet_hdr_len) {
		err = virtio_net_hdr_to_skb(skb, gso, tap_is_little_endian(q));
		if (err)
			goto err_kfree;
	}

	skb_probe_transport_header(skb, ETH_HLEN);

	/* Move network header to the right position for VLAN tagged packets */
	if ((skb->protocol == htons(ETH_P_8021Q) ||
	     skb->protocol == htons(ETH_P_8021AD)) &&
	    __vlan_get_protocol(skb, skb->protocol, &depth) != 0)
		skb_set_network_header(skb, depth);

	rcu_read_lock();
	tap = rcu_dereference(q->tap);
	if (tap) {
		skb->dev = tap->dev;
		dev_queue_xmit(skb);
	} else {
		kfree_skb(skb);
	}
	rcu_read_unlock();

	return 0;

err_kfree:
	kfree_skb(skb);
err:
	rcu_read_lock();
		tap = rcu_dereference(q->tap);
	if (tap && tap->count_tx_dropped)
		tap->count_tx_dropped(tap);
	rcu_read_unlock();
	return err;
}

static int tap_sendmsg(struct socket *sock, struct msghdr *m,
		       size_t total_len)
{
	struct tap_queue *q = container_of(sock, struct tap_queue, sock);
	struct tun_msg_ctl *ctl = m->msg_control;
	struct xdp_buff *xdp;
	int i;

	if (ctl && (ctl->type == TUN_MSG_PTR)) {
		for (i = 0; i < ctl->num; i++) {
			xdp = &((struct xdp_buff *)ctl->ptr)[i];
			tap_get_user_xdp(q, xdp);
		}
		return 0;
	}

	return tap_get_user(q, ctl ? ctl->ptr : NULL, &m->msg_iter,
			    m->msg_flags & MSG_DONTWAIT);
}

static int tap_recvmsg(struct socket *sock, struct msghdr *m,
		       size_t total_len, int flags)
{
	struct tap_queue *q = container_of(sock, struct tap_queue, sock);
	struct sk_buff *skb = m->msg_control;
	int ret;
	if (flags & ~(MSG_DONTWAIT|MSG_TRUNC)) {
		kfree_skb(skb);
		return -EINVAL;
	}
	ret = tap_do_read(q, &m->msg_iter, flags & MSG_DONTWAIT, skb);
	if (ret > total_len) {
		m->msg_flags |= MSG_TRUNC;
		ret = flags & MSG_TRUNC ? ret : total_len;
	}
	return ret;
}

static int tap_peek_len(struct socket *sock)
{
	struct tap_queue *q = container_of(sock, struct tap_queue,
					       sock);
	return PTR_RING_PEEK_CALL(&q->ring, __skb_array_len_with_tag);
}

/* Ops structure to mimic raw sockets with tun */
static const struct proto_ops tap_socket_ops = {
	.sendmsg = tap_sendmsg,
	.recvmsg = tap_recvmsg,
	.peek_len = tap_peek_len,
};

/* Get an underlying socket object from tun file.  Returns error unless file is
 * attached to a device.  The returned object works like a packet socket, it
 * can be used for sock_sendmsg/sock_recvmsg.  The caller is responsible for
 * holding a reference to the file for as long as the socket is in use. */
struct socket *tap_get_socket(struct file *file)
{
	struct tap_queue *q;
	if (file->f_op != &tap_fops)
		return ERR_PTR(-EINVAL);
	q = file->private_data;
	if (!q)
		return ERR_PTR(-EBADFD);
	return &q->sock;
}
EXPORT_SYMBOL_GPL(tap_get_socket);

struct ptr_ring *tap_get_ptr_ring(struct file *file)
{
	struct tap_queue *q;

	if (file->f_op != &tap_fops)
		return ERR_PTR(-EINVAL);
	q = file->private_data;
	if (!q)
		return ERR_PTR(-EBADFD);
	return &q->ring;
}
EXPORT_SYMBOL_GPL(tap_get_ptr_ring);

int tap_queue_resize(struct tap_dev *tap)
{
	struct net_device *dev = tap->dev;
	struct tap_queue *q;
	struct ptr_ring **rings;
	int n = tap->numqueues;
	int ret, i = 0;

	rings = kmalloc_array(n, sizeof(*rings), GFP_KERNEL);
	if (!rings)
		return -ENOMEM;

	list_for_each_entry(q, &tap->queue_list, next)
		rings[i++] = &q->ring;

	ret = ptr_ring_resize_multiple(rings, n,
				       dev->tx_queue_len, GFP_KERNEL,
				       __skb_array_destroy_skb);

	kfree(rings);
	return ret;
}
EXPORT_SYMBOL_GPL(tap_queue_resize);

static int tap_list_add(dev_t major, const char *device_name)
{
	struct major_info *tap_major;

	tap_major = kzalloc(sizeof(*tap_major), GFP_ATOMIC);
	if (!tap_major)
		return -ENOMEM;

	tap_major->major = MAJOR(major);

	idr_init(&tap_major->minor_idr);
	spin_lock_init(&tap_major->minor_lock);

	tap_major->device_name = device_name;

	list_add_tail_rcu(&tap_major->next, &major_list);
	return 0;
}

int tap_create_cdev(struct cdev *tap_cdev, dev_t *tap_major,
		    const char *device_name, struct module *module)
{
	int err;

	err = alloc_chrdev_region(tap_major, 0, TAP_NUM_DEVS, device_name);
	if (err)
		goto out1;

	cdev_init(tap_cdev, &tap_fops);
	tap_cdev->owner = module;
	err = cdev_add(tap_cdev, *tap_major, TAP_NUM_DEVS);
	if (err)
		goto out2;

	err =  tap_list_add(*tap_major, device_name);
	if (err)
		goto out3;

	return 0;

out3:
	cdev_del(tap_cdev);
out2:
	unregister_chrdev_region(*tap_major, TAP_NUM_DEVS);
out1:
	return err;
}
EXPORT_SYMBOL_GPL(tap_create_cdev);

void tap_destroy_cdev(dev_t major, struct cdev *tap_cdev)
{
	struct major_info *tap_major, *tmp;

	cdev_del(tap_cdev);
	unregister_chrdev_region(major, TAP_NUM_DEVS);
	list_for_each_entry_safe(tap_major, tmp, &major_list, next) {
		if (tap_major->major == MAJOR(major)) {
			idr_destroy(&tap_major->minor_idr);
			list_del_rcu(&tap_major->next);
			kfree_rcu(tap_major, rcu);
		}
	}
}
EXPORT_SYMBOL_GPL(tap_destroy_cdev);

MODULE_AUTHOR("Arnd Bergmann <arnd@arndb.de>");
MODULE_AUTHOR("Sainath Grandhi <sainath.grandhi@intel.com>");
MODULE_LICENSE("GPL");
