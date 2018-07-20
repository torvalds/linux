/* Copyright (C) 2009 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * virtio-net server in host kernel.
 */

#include <linux/compat.h>
#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/virtio_net.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <linux/vmalloc.h>

#include <linux/net.h>
#include <linux/if_packet.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/if_macvlan.h>
#include <linux/if_tap.h>
#include <linux/if_vlan.h>
#include <linux/skb_array.h>
#include <linux/skbuff.h>

#include <net/sock.h>
#include <net/xdp.h>

#include "vhost.h"

static int experimental_zcopytx = 1;
module_param(experimental_zcopytx, int, 0444);
MODULE_PARM_DESC(experimental_zcopytx, "Enable Zero Copy TX;"
		                       " 1 -Enable; 0 - Disable");

/* Max number of bytes transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others. */
#define VHOST_NET_WEIGHT 0x80000

/* Max number of packets transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others with small
 * pkts.
 */
#define VHOST_NET_PKT_WEIGHT 256

/* MAX number of TX used buffers for outstanding zerocopy */
#define VHOST_MAX_PEND 128
#define VHOST_GOODCOPY_LEN 256

/*
 * For transmit, used buffer len is unused; we override it to track buffer
 * status internally; used for zerocopy tx only.
 */
/* Lower device DMA failed */
#define VHOST_DMA_FAILED_LEN	((__force __virtio32)3)
/* Lower device DMA done */
#define VHOST_DMA_DONE_LEN	((__force __virtio32)2)
/* Lower device DMA in progress */
#define VHOST_DMA_IN_PROGRESS	((__force __virtio32)1)
/* Buffer unused */
#define VHOST_DMA_CLEAR_LEN	((__force __virtio32)0)

#define VHOST_DMA_IS_DONE(len) ((__force u32)(len) >= (__force u32)VHOST_DMA_DONE_LEN)

enum {
	VHOST_NET_FEATURES = VHOST_FEATURES |
			 (1ULL << VHOST_NET_F_VIRTIO_NET_HDR) |
			 (1ULL << VIRTIO_NET_F_MRG_RXBUF) |
			 (1ULL << VIRTIO_F_IOMMU_PLATFORM)
};

enum {
	VHOST_NET_VQ_RX = 0,
	VHOST_NET_VQ_TX = 1,
	VHOST_NET_VQ_MAX = 2,
};

struct vhost_net_ubuf_ref {
	/* refcount follows semantics similar to kref:
	 *  0: object is released
	 *  1: no outstanding ubufs
	 * >1: outstanding ubufs
	 */
	atomic_t refcount;
	wait_queue_head_t wait;
	struct vhost_virtqueue *vq;
};

#define VHOST_RX_BATCH 64
struct vhost_net_buf {
	void **queue;
	int tail;
	int head;
};

struct vhost_net_virtqueue {
	struct vhost_virtqueue vq;
	size_t vhost_hlen;
	size_t sock_hlen;
	/* vhost zerocopy support fields below: */
	/* last used idx for outstanding DMA zerocopy buffers */
	int upend_idx;
	/* For TX, first used idx for DMA done zerocopy buffers
	 * For RX, number of batched heads
	 */
	int done_idx;
	/* an array of userspace buffers info */
	struct ubuf_info *ubuf_info;
	/* Reference counting for outstanding ubufs.
	 * Protected by vq mutex. Writers must also take device mutex. */
	struct vhost_net_ubuf_ref *ubufs;
	struct ptr_ring *rx_ring;
	struct vhost_net_buf rxq;
};

struct vhost_net {
	struct vhost_dev dev;
	struct vhost_net_virtqueue vqs[VHOST_NET_VQ_MAX];
	struct vhost_poll poll[VHOST_NET_VQ_MAX];
	/* Number of TX recently submitted.
	 * Protected by tx vq lock. */
	unsigned tx_packets;
	/* Number of times zerocopy TX recently failed.
	 * Protected by tx vq lock. */
	unsigned tx_zcopy_err;
	/* Flush in progress. Protected by tx vq lock. */
	bool tx_flush;
};

static unsigned vhost_net_zcopy_mask __read_mostly;

static void *vhost_net_buf_get_ptr(struct vhost_net_buf *rxq)
{
	if (rxq->tail != rxq->head)
		return rxq->queue[rxq->head];
	else
		return NULL;
}

static int vhost_net_buf_get_size(struct vhost_net_buf *rxq)
{
	return rxq->tail - rxq->head;
}

static int vhost_net_buf_is_empty(struct vhost_net_buf *rxq)
{
	return rxq->tail == rxq->head;
}

static void *vhost_net_buf_consume(struct vhost_net_buf *rxq)
{
	void *ret = vhost_net_buf_get_ptr(rxq);
	++rxq->head;
	return ret;
}

static int vhost_net_buf_produce(struct vhost_net_virtqueue *nvq)
{
	struct vhost_net_buf *rxq = &nvq->rxq;

	rxq->head = 0;
	rxq->tail = ptr_ring_consume_batched(nvq->rx_ring, rxq->queue,
					      VHOST_RX_BATCH);
	return rxq->tail;
}

static void vhost_net_buf_unproduce(struct vhost_net_virtqueue *nvq)
{
	struct vhost_net_buf *rxq = &nvq->rxq;

	if (nvq->rx_ring && !vhost_net_buf_is_empty(rxq)) {
		ptr_ring_unconsume(nvq->rx_ring, rxq->queue + rxq->head,
				   vhost_net_buf_get_size(rxq),
				   tun_ptr_free);
		rxq->head = rxq->tail = 0;
	}
}

static int vhost_net_buf_peek_len(void *ptr)
{
	if (tun_is_xdp_frame(ptr)) {
		struct xdp_frame *xdpf = tun_ptr_to_xdp(ptr);

		return xdpf->len;
	}

	return __skb_array_len_with_tag(ptr);
}

static int vhost_net_buf_peek(struct vhost_net_virtqueue *nvq)
{
	struct vhost_net_buf *rxq = &nvq->rxq;

	if (!vhost_net_buf_is_empty(rxq))
		goto out;

	if (!vhost_net_buf_produce(nvq))
		return 0;

out:
	return vhost_net_buf_peek_len(vhost_net_buf_get_ptr(rxq));
}

static void vhost_net_buf_init(struct vhost_net_buf *rxq)
{
	rxq->head = rxq->tail = 0;
}

static void vhost_net_enable_zcopy(int vq)
{
	vhost_net_zcopy_mask |= 0x1 << vq;
}

static struct vhost_net_ubuf_ref *
vhost_net_ubuf_alloc(struct vhost_virtqueue *vq, bool zcopy)
{
	struct vhost_net_ubuf_ref *ubufs;
	/* No zero copy backend? Nothing to count. */
	if (!zcopy)
		return NULL;
	ubufs = kmalloc(sizeof(*ubufs), GFP_KERNEL);
	if (!ubufs)
		return ERR_PTR(-ENOMEM);
	atomic_set(&ubufs->refcount, 1);
	init_waitqueue_head(&ubufs->wait);
	ubufs->vq = vq;
	return ubufs;
}

static int vhost_net_ubuf_put(struct vhost_net_ubuf_ref *ubufs)
{
	int r = atomic_sub_return(1, &ubufs->refcount);
	if (unlikely(!r))
		wake_up(&ubufs->wait);
	return r;
}

static void vhost_net_ubuf_put_and_wait(struct vhost_net_ubuf_ref *ubufs)
{
	vhost_net_ubuf_put(ubufs);
	wait_event(ubufs->wait, !atomic_read(&ubufs->refcount));
}

static void vhost_net_ubuf_put_wait_and_free(struct vhost_net_ubuf_ref *ubufs)
{
	vhost_net_ubuf_put_and_wait(ubufs);
	kfree(ubufs);
}

static void vhost_net_clear_ubuf_info(struct vhost_net *n)
{
	int i;

	for (i = 0; i < VHOST_NET_VQ_MAX; ++i) {
		kfree(n->vqs[i].ubuf_info);
		n->vqs[i].ubuf_info = NULL;
	}
}

static int vhost_net_set_ubuf_info(struct vhost_net *n)
{
	bool zcopy;
	int i;

	for (i = 0; i < VHOST_NET_VQ_MAX; ++i) {
		zcopy = vhost_net_zcopy_mask & (0x1 << i);
		if (!zcopy)
			continue;
		n->vqs[i].ubuf_info =
			kmalloc_array(UIO_MAXIOV,
				      sizeof(*n->vqs[i].ubuf_info),
				      GFP_KERNEL);
		if  (!n->vqs[i].ubuf_info)
			goto err;
	}
	return 0;

err:
	vhost_net_clear_ubuf_info(n);
	return -ENOMEM;
}

static void vhost_net_vq_reset(struct vhost_net *n)
{
	int i;

	vhost_net_clear_ubuf_info(n);

	for (i = 0; i < VHOST_NET_VQ_MAX; i++) {
		n->vqs[i].done_idx = 0;
		n->vqs[i].upend_idx = 0;
		n->vqs[i].ubufs = NULL;
		n->vqs[i].vhost_hlen = 0;
		n->vqs[i].sock_hlen = 0;
		vhost_net_buf_init(&n->vqs[i].rxq);
	}

}

static void vhost_net_tx_packet(struct vhost_net *net)
{
	++net->tx_packets;
	if (net->tx_packets < 1024)
		return;
	net->tx_packets = 0;
	net->tx_zcopy_err = 0;
}

static void vhost_net_tx_err(struct vhost_net *net)
{
	++net->tx_zcopy_err;
}

static bool vhost_net_tx_select_zcopy(struct vhost_net *net)
{
	/* TX flush waits for outstanding DMAs to be done.
	 * Don't start new DMAs.
	 */
	return !net->tx_flush &&
		net->tx_packets / 64 >= net->tx_zcopy_err;
}

static bool vhost_sock_zcopy(struct socket *sock)
{
	return unlikely(experimental_zcopytx) &&
		sock_flag(sock->sk, SOCK_ZEROCOPY);
}

/* In case of DMA done not in order in lower device driver for some reason.
 * upend_idx is used to track end of used idx, done_idx is used to track head
 * of used idx. Once lower device DMA done contiguously, we will signal KVM
 * guest used idx.
 */
static void vhost_zerocopy_signal_used(struct vhost_net *net,
				       struct vhost_virtqueue *vq)
{
	struct vhost_net_virtqueue *nvq =
		container_of(vq, struct vhost_net_virtqueue, vq);
	int i, add;
	int j = 0;

	for (i = nvq->done_idx; i != nvq->upend_idx; i = (i + 1) % UIO_MAXIOV) {
		if (vq->heads[i].len == VHOST_DMA_FAILED_LEN)
			vhost_net_tx_err(net);
		if (VHOST_DMA_IS_DONE(vq->heads[i].len)) {
			vq->heads[i].len = VHOST_DMA_CLEAR_LEN;
			++j;
		} else
			break;
	}
	while (j) {
		add = min(UIO_MAXIOV - nvq->done_idx, j);
		vhost_add_used_and_signal_n(vq->dev, vq,
					    &vq->heads[nvq->done_idx], add);
		nvq->done_idx = (nvq->done_idx + add) % UIO_MAXIOV;
		j -= add;
	}
}

static void vhost_zerocopy_callback(struct ubuf_info *ubuf, bool success)
{
	struct vhost_net_ubuf_ref *ubufs = ubuf->ctx;
	struct vhost_virtqueue *vq = ubufs->vq;
	int cnt;

	rcu_read_lock_bh();

	/* set len to mark this desc buffers done DMA */
	vq->heads[ubuf->desc].len = success ?
		VHOST_DMA_DONE_LEN : VHOST_DMA_FAILED_LEN;
	cnt = vhost_net_ubuf_put(ubufs);

	/*
	 * Trigger polling thread if guest stopped submitting new buffers:
	 * in this case, the refcount after decrement will eventually reach 1.
	 * We also trigger polling periodically after each 16 packets
	 * (the value 16 here is more or less arbitrary, it's tuned to trigger
	 * less than 10% of times).
	 */
	if (cnt <= 1 || !(cnt % 16))
		vhost_poll_queue(&vq->poll);

	rcu_read_unlock_bh();
}

static inline unsigned long busy_clock(void)
{
	return local_clock() >> 10;
}

static bool vhost_can_busy_poll(unsigned long endtime)
{
	return likely(!need_resched() && !time_after(busy_clock(), endtime) &&
		      !signal_pending(current));
}

static void vhost_net_disable_vq(struct vhost_net *n,
				 struct vhost_virtqueue *vq)
{
	struct vhost_net_virtqueue *nvq =
		container_of(vq, struct vhost_net_virtqueue, vq);
	struct vhost_poll *poll = n->poll + (nvq - n->vqs);
	if (!vq->private_data)
		return;
	vhost_poll_stop(poll);
}

static int vhost_net_enable_vq(struct vhost_net *n,
				struct vhost_virtqueue *vq)
{
	struct vhost_net_virtqueue *nvq =
		container_of(vq, struct vhost_net_virtqueue, vq);
	struct vhost_poll *poll = n->poll + (nvq - n->vqs);
	struct socket *sock;

	sock = vq->private_data;
	if (!sock)
		return 0;

	return vhost_poll_start(poll, sock->file);
}

static int vhost_net_tx_get_vq_desc(struct vhost_net *net,
				    struct vhost_virtqueue *vq,
				    unsigned int *out_num, unsigned int *in_num,
				    bool *busyloop_intr)
{
	unsigned long uninitialized_var(endtime);
	int r = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
				  out_num, in_num, NULL, NULL);

	if (r == vq->num && vq->busyloop_timeout) {
		preempt_disable();
		endtime = busy_clock() + vq->busyloop_timeout;
		while (vhost_can_busy_poll(endtime)) {
			if (vhost_has_work(vq->dev)) {
				*busyloop_intr = true;
				break;
			}
			if (!vhost_vq_avail_empty(vq->dev, vq))
				break;
			cpu_relax();
		}
		preempt_enable();
		r = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
				      out_num, in_num, NULL, NULL);
	}

	return r;
}

static bool vhost_exceeds_maxpend(struct vhost_net *net)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *vq = &nvq->vq;

	return (nvq->upend_idx + UIO_MAXIOV - nvq->done_idx) % UIO_MAXIOV >
	       min_t(unsigned int, VHOST_MAX_PEND, vq->num >> 2);
}

static size_t init_iov_iter(struct vhost_virtqueue *vq, struct iov_iter *iter,
			    size_t hdr_size, int out)
{
	/* Skip header. TODO: support TSO. */
	size_t len = iov_length(vq->iov, out);

	iov_iter_init(iter, WRITE, vq->iov, out, len);
	iov_iter_advance(iter, hdr_size);

	return iov_iter_count(iter);
}

static bool vhost_exceeds_weight(int pkts, int total_len)
{
	return total_len >= VHOST_NET_WEIGHT ||
	       pkts >= VHOST_NET_PKT_WEIGHT;
}

static int get_tx_bufs(struct vhost_net *net,
		       struct vhost_net_virtqueue *nvq,
		       struct msghdr *msg,
		       unsigned int *out, unsigned int *in,
		       size_t *len, bool *busyloop_intr)
{
	struct vhost_virtqueue *vq = &nvq->vq;
	int ret;

	ret = vhost_net_tx_get_vq_desc(net, vq, out, in, busyloop_intr);
	if (ret < 0 || ret == vq->num)
		return ret;

	if (*in) {
		vq_err(vq, "Unexpected descriptor format for TX: out %d, int %d\n",
			*out, *in);
		return -EFAULT;
	}

	/* Sanity check */
	*len = init_iov_iter(vq, &msg->msg_iter, nvq->vhost_hlen, *out);
	if (*len == 0) {
		vq_err(vq, "Unexpected header len for TX: %zd expected %zd\n",
			*len, nvq->vhost_hlen);
		return -EFAULT;
	}

	return ret;
}

static bool tx_can_batch(struct vhost_virtqueue *vq, size_t total_len)
{
	return total_len < VHOST_NET_WEIGHT &&
	       !vhost_vq_avail_empty(vq->dev, vq);
}

static void handle_tx_copy(struct vhost_net *net, struct socket *sock)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *vq = &nvq->vq;
	unsigned out, in;
	int head;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = MSG_DONTWAIT,
	};
	size_t len, total_len = 0;
	int err;
	int sent_pkts = 0;

	for (;;) {
		bool busyloop_intr = false;

		head = get_tx_bufs(net, nvq, &msg, &out, &in, &len,
				   &busyloop_intr);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			if (unlikely(busyloop_intr)) {
				vhost_poll_queue(&vq->poll);
			} else if (unlikely(vhost_enable_notify(&net->dev,
								vq))) {
				vhost_disable_notify(&net->dev, vq);
				continue;
			}
			break;
		}

		total_len += len;
		if (tx_can_batch(vq, total_len))
			msg.msg_flags |= MSG_MORE;
		else
			msg.msg_flags &= ~MSG_MORE;

		/* TODO: Check specific error and bomb out unless ENOBUFS? */
		err = sock->ops->sendmsg(sock, &msg, len);
		if (unlikely(err < 0)) {
			vhost_discard_vq_desc(vq, 1);
			vhost_net_enable_vq(net, vq);
			break;
		}
		if (err != len)
			pr_debug("Truncated TX packet: len %d != %zd\n",
				 err, len);
		vhost_add_used_and_signal(&net->dev, vq, head, 0);
		if (vhost_exceeds_weight(++sent_pkts, total_len)) {
			vhost_poll_queue(&vq->poll);
			break;
		}
	}
}

static void handle_tx_zerocopy(struct vhost_net *net, struct socket *sock)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *vq = &nvq->vq;
	unsigned out, in;
	int head;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = MSG_DONTWAIT,
	};
	size_t len, total_len = 0;
	int err;
	struct vhost_net_ubuf_ref *uninitialized_var(ubufs);
	bool zcopy_used;
	int sent_pkts = 0;

	for (;;) {
		bool busyloop_intr;

		/* Release DMAs done buffers first */
		vhost_zerocopy_signal_used(net, vq);

		busyloop_intr = false;
		head = get_tx_bufs(net, nvq, &msg, &out, &in, &len,
				   &busyloop_intr);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			if (unlikely(busyloop_intr)) {
				vhost_poll_queue(&vq->poll);
			} else if (unlikely(vhost_enable_notify(&net->dev, vq))) {
				vhost_disable_notify(&net->dev, vq);
				continue;
			}
			break;
		}

		zcopy_used = len >= VHOST_GOODCOPY_LEN
			     && !vhost_exceeds_maxpend(net)
			     && vhost_net_tx_select_zcopy(net);

		/* use msg_control to pass vhost zerocopy ubuf info to skb */
		if (zcopy_used) {
			struct ubuf_info *ubuf;
			ubuf = nvq->ubuf_info + nvq->upend_idx;

			vq->heads[nvq->upend_idx].id = cpu_to_vhost32(vq, head);
			vq->heads[nvq->upend_idx].len = VHOST_DMA_IN_PROGRESS;
			ubuf->callback = vhost_zerocopy_callback;
			ubuf->ctx = nvq->ubufs;
			ubuf->desc = nvq->upend_idx;
			refcount_set(&ubuf->refcnt, 1);
			msg.msg_control = ubuf;
			msg.msg_controllen = sizeof(ubuf);
			ubufs = nvq->ubufs;
			atomic_inc(&ubufs->refcount);
			nvq->upend_idx = (nvq->upend_idx + 1) % UIO_MAXIOV;
		} else {
			msg.msg_control = NULL;
			ubufs = NULL;
		}
		total_len += len;
		if (tx_can_batch(vq, total_len) &&
		    likely(!vhost_exceeds_maxpend(net))) {
			msg.msg_flags |= MSG_MORE;
		} else {
			msg.msg_flags &= ~MSG_MORE;
		}

		/* TODO: Check specific error and bomb out unless ENOBUFS? */
		err = sock->ops->sendmsg(sock, &msg, len);
		if (unlikely(err < 0)) {
			if (zcopy_used) {
				vhost_net_ubuf_put(ubufs);
				nvq->upend_idx = ((unsigned)nvq->upend_idx - 1)
					% UIO_MAXIOV;
			}
			vhost_discard_vq_desc(vq, 1);
			vhost_net_enable_vq(net, vq);
			break;
		}
		if (err != len)
			pr_debug("Truncated TX packet: "
				 " len %d != %zd\n", err, len);
		if (!zcopy_used)
			vhost_add_used_and_signal(&net->dev, vq, head, 0);
		else
			vhost_zerocopy_signal_used(net, vq);
		vhost_net_tx_packet(net);
		if (unlikely(vhost_exceeds_weight(++sent_pkts, total_len))) {
			vhost_poll_queue(&vq->poll);
			break;
		}
	}
}

/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_tx(struct vhost_net *net)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *vq = &nvq->vq;
	struct socket *sock;

	mutex_lock(&vq->mutex);
	sock = vq->private_data;
	if (!sock)
		goto out;

	if (!vq_iotlb_prefetch(vq))
		goto out;

	vhost_disable_notify(&net->dev, vq);
	vhost_net_disable_vq(net, vq);

	if (vhost_sock_zcopy(sock))
		handle_tx_zerocopy(net, sock);
	else
		handle_tx_copy(net, sock);

out:
	mutex_unlock(&vq->mutex);
}

static int peek_head_len(struct vhost_net_virtqueue *rvq, struct sock *sk)
{
	struct sk_buff *head;
	int len = 0;
	unsigned long flags;

	if (rvq->rx_ring)
		return vhost_net_buf_peek(rvq);

	spin_lock_irqsave(&sk->sk_receive_queue.lock, flags);
	head = skb_peek(&sk->sk_receive_queue);
	if (likely(head)) {
		len = head->len;
		if (skb_vlan_tag_present(head))
			len += VLAN_HLEN;
	}

	spin_unlock_irqrestore(&sk->sk_receive_queue.lock, flags);
	return len;
}

static int sk_has_rx_data(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;

	if (sock->ops->peek_len)
		return sock->ops->peek_len(sock);

	return skb_queue_empty(&sk->sk_receive_queue);
}

static void vhost_rx_signal_used(struct vhost_net_virtqueue *nvq)
{
	struct vhost_virtqueue *vq = &nvq->vq;
	struct vhost_dev *dev = vq->dev;

	if (!nvq->done_idx)
		return;

	vhost_add_used_and_signal_n(dev, vq, vq->heads, nvq->done_idx);
	nvq->done_idx = 0;
}

static int vhost_net_rx_peek_head_len(struct vhost_net *net, struct sock *sk,
				      bool *busyloop_intr)
{
	struct vhost_net_virtqueue *rnvq = &net->vqs[VHOST_NET_VQ_RX];
	struct vhost_net_virtqueue *tnvq = &net->vqs[VHOST_NET_VQ_TX];
	struct vhost_virtqueue *rvq = &rnvq->vq;
	struct vhost_virtqueue *tvq = &tnvq->vq;
	unsigned long uninitialized_var(endtime);
	int len = peek_head_len(rnvq, sk);

	if (!len && tvq->busyloop_timeout) {
		/* Flush batched heads first */
		vhost_rx_signal_used(rnvq);
		/* Both tx vq and rx socket were polled here */
		mutex_lock_nested(&tvq->mutex, 1);
		vhost_disable_notify(&net->dev, tvq);

		preempt_disable();
		endtime = busy_clock() + tvq->busyloop_timeout;

		while (vhost_can_busy_poll(endtime)) {
			if (vhost_has_work(&net->dev)) {
				*busyloop_intr = true;
				break;
			}
			if ((sk_has_rx_data(sk) &&
			     !vhost_vq_avail_empty(&net->dev, rvq)) ||
			    !vhost_vq_avail_empty(&net->dev, tvq))
				break;
			cpu_relax();
		}

		preempt_enable();

		if (!vhost_vq_avail_empty(&net->dev, tvq)) {
			vhost_poll_queue(&tvq->poll);
		} else if (unlikely(vhost_enable_notify(&net->dev, tvq))) {
			vhost_disable_notify(&net->dev, tvq);
			vhost_poll_queue(&tvq->poll);
		}

		mutex_unlock(&tvq->mutex);

		len = peek_head_len(rnvq, sk);
	}

	return len;
}

/* This is a multi-buffer version of vhost_get_desc, that works if
 *	vq has read descriptors only.
 * @vq		- the relevant virtqueue
 * @datalen	- data length we'll be reading
 * @iovcount	- returned count of io vectors we fill
 * @log		- vhost log
 * @log_num	- log offset
 * @quota       - headcount quota, 1 for big buffer
 *	returns number of buffer heads allocated, negative on error
 */
static int get_rx_bufs(struct vhost_virtqueue *vq,
		       struct vring_used_elem *heads,
		       int datalen,
		       unsigned *iovcount,
		       struct vhost_log *log,
		       unsigned *log_num,
		       unsigned int quota)
{
	unsigned int out, in;
	int seg = 0;
	int headcount = 0;
	unsigned d;
	int r, nlogs = 0;
	/* len is always initialized before use since we are always called with
	 * datalen > 0.
	 */
	u32 uninitialized_var(len);

	while (datalen > 0 && headcount < quota) {
		if (unlikely(seg >= UIO_MAXIOV)) {
			r = -ENOBUFS;
			goto err;
		}
		r = vhost_get_vq_desc(vq, vq->iov + seg,
				      ARRAY_SIZE(vq->iov) - seg, &out,
				      &in, log, log_num);
		if (unlikely(r < 0))
			goto err;

		d = r;
		if (d == vq->num) {
			r = 0;
			goto err;
		}
		if (unlikely(out || in <= 0)) {
			vq_err(vq, "unexpected descriptor format for RX: "
				"out %d, in %d\n", out, in);
			r = -EINVAL;
			goto err;
		}
		if (unlikely(log)) {
			nlogs += *log_num;
			log += *log_num;
		}
		heads[headcount].id = cpu_to_vhost32(vq, d);
		len = iov_length(vq->iov + seg, in);
		heads[headcount].len = cpu_to_vhost32(vq, len);
		datalen -= len;
		++headcount;
		seg += in;
	}
	heads[headcount - 1].len = cpu_to_vhost32(vq, len + datalen);
	*iovcount = seg;
	if (unlikely(log))
		*log_num = nlogs;

	/* Detect overrun */
	if (unlikely(datalen > 0)) {
		r = UIO_MAXIOV + 1;
		goto err;
	}
	return headcount;
err:
	vhost_discard_vq_desc(vq, headcount);
	return r;
}

/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_rx(struct vhost_net *net)
{
	struct vhost_net_virtqueue *nvq = &net->vqs[VHOST_NET_VQ_RX];
	struct vhost_virtqueue *vq = &nvq->vq;
	unsigned uninitialized_var(in), log;
	struct vhost_log *vq_log;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL, /* FIXME: get and handle RX aux data. */
		.msg_controllen = 0,
		.msg_flags = MSG_DONTWAIT,
	};
	struct virtio_net_hdr hdr = {
		.flags = 0,
		.gso_type = VIRTIO_NET_HDR_GSO_NONE
	};
	size_t total_len = 0;
	int err, mergeable;
	s16 headcount;
	size_t vhost_hlen, sock_hlen;
	size_t vhost_len, sock_len;
	bool busyloop_intr = false;
	struct socket *sock;
	struct iov_iter fixup;
	__virtio16 num_buffers;
	int recv_pkts = 0;

	mutex_lock_nested(&vq->mutex, 0);
	sock = vq->private_data;
	if (!sock)
		goto out;

	if (!vq_iotlb_prefetch(vq))
		goto out;

	vhost_disable_notify(&net->dev, vq);
	vhost_net_disable_vq(net, vq);

	vhost_hlen = nvq->vhost_hlen;
	sock_hlen = nvq->sock_hlen;

	vq_log = unlikely(vhost_has_feature(vq, VHOST_F_LOG_ALL)) ?
		vq->log : NULL;
	mergeable = vhost_has_feature(vq, VIRTIO_NET_F_MRG_RXBUF);

	while ((sock_len = vhost_net_rx_peek_head_len(net, sock->sk,
						      &busyloop_intr))) {
		sock_len += sock_hlen;
		vhost_len = sock_len + vhost_hlen;
		headcount = get_rx_bufs(vq, vq->heads + nvq->done_idx,
					vhost_len, &in, vq_log, &log,
					likely(mergeable) ? UIO_MAXIOV : 1);
		/* On error, stop handling until the next kick. */
		if (unlikely(headcount < 0))
			goto out;
		/* OK, now we need to know about added descriptors. */
		if (!headcount) {
			if (unlikely(busyloop_intr)) {
				vhost_poll_queue(&vq->poll);
			} else if (unlikely(vhost_enable_notify(&net->dev, vq))) {
				/* They have slipped one in as we were
				 * doing that: check again. */
				vhost_disable_notify(&net->dev, vq);
				continue;
			}
			/* Nothing new?  Wait for eventfd to tell us
			 * they refilled. */
			goto out;
		}
		busyloop_intr = false;
		if (nvq->rx_ring)
			msg.msg_control = vhost_net_buf_consume(&nvq->rxq);
		/* On overrun, truncate and discard */
		if (unlikely(headcount > UIO_MAXIOV)) {
			iov_iter_init(&msg.msg_iter, READ, vq->iov, 1, 1);
			err = sock->ops->recvmsg(sock, &msg,
						 1, MSG_DONTWAIT | MSG_TRUNC);
			pr_debug("Discarded rx packet: len %zd\n", sock_len);
			continue;
		}
		/* We don't need to be notified again. */
		iov_iter_init(&msg.msg_iter, READ, vq->iov, in, vhost_len);
		fixup = msg.msg_iter;
		if (unlikely((vhost_hlen))) {
			/* We will supply the header ourselves
			 * TODO: support TSO.
			 */
			iov_iter_advance(&msg.msg_iter, vhost_hlen);
		}
		err = sock->ops->recvmsg(sock, &msg,
					 sock_len, MSG_DONTWAIT | MSG_TRUNC);
		/* Userspace might have consumed the packet meanwhile:
		 * it's not supposed to do this usually, but might be hard
		 * to prevent. Discard data we got (if any) and keep going. */
		if (unlikely(err != sock_len)) {
			pr_debug("Discarded rx packet: "
				 " len %d, expected %zd\n", err, sock_len);
			vhost_discard_vq_desc(vq, headcount);
			continue;
		}
		/* Supply virtio_net_hdr if VHOST_NET_F_VIRTIO_NET_HDR */
		if (unlikely(vhost_hlen)) {
			if (copy_to_iter(&hdr, sizeof(hdr),
					 &fixup) != sizeof(hdr)) {
				vq_err(vq, "Unable to write vnet_hdr "
				       "at addr %p\n", vq->iov->iov_base);
				goto out;
			}
		} else {
			/* Header came from socket; we'll need to patch
			 * ->num_buffers over if VIRTIO_NET_F_MRG_RXBUF
			 */
			iov_iter_advance(&fixup, sizeof(hdr));
		}
		/* TODO: Should check and handle checksum. */

		num_buffers = cpu_to_vhost16(vq, headcount);
		if (likely(mergeable) &&
		    copy_to_iter(&num_buffers, sizeof num_buffers,
				 &fixup) != sizeof num_buffers) {
			vq_err(vq, "Failed num_buffers write");
			vhost_discard_vq_desc(vq, headcount);
			goto out;
		}
		nvq->done_idx += headcount;
		if (nvq->done_idx > VHOST_RX_BATCH)
			vhost_rx_signal_used(nvq);
		if (unlikely(vq_log))
			vhost_log_write(vq, vq_log, log, vhost_len);
		total_len += vhost_len;
		if (unlikely(vhost_exceeds_weight(++recv_pkts, total_len))) {
			vhost_poll_queue(&vq->poll);
			goto out;
		}
	}
	if (unlikely(busyloop_intr))
		vhost_poll_queue(&vq->poll);
	else
		vhost_net_enable_vq(net, vq);
out:
	vhost_rx_signal_used(nvq);
	mutex_unlock(&vq->mutex);
}

static void handle_tx_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_net *net = container_of(vq->dev, struct vhost_net, dev);

	handle_tx(net);
}

static void handle_rx_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_net *net = container_of(vq->dev, struct vhost_net, dev);

	handle_rx(net);
}

static void handle_tx_net(struct vhost_work *work)
{
	struct vhost_net *net = container_of(work, struct vhost_net,
					     poll[VHOST_NET_VQ_TX].work);
	handle_tx(net);
}

static void handle_rx_net(struct vhost_work *work)
{
	struct vhost_net *net = container_of(work, struct vhost_net,
					     poll[VHOST_NET_VQ_RX].work);
	handle_rx(net);
}

static int vhost_net_open(struct inode *inode, struct file *f)
{
	struct vhost_net *n;
	struct vhost_dev *dev;
	struct vhost_virtqueue **vqs;
	void **queue;
	int i;

	n = kvmalloc(sizeof *n, GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!n)
		return -ENOMEM;
	vqs = kmalloc_array(VHOST_NET_VQ_MAX, sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		kvfree(n);
		return -ENOMEM;
	}

	queue = kmalloc_array(VHOST_RX_BATCH, sizeof(void *),
			      GFP_KERNEL);
	if (!queue) {
		kfree(vqs);
		kvfree(n);
		return -ENOMEM;
	}
	n->vqs[VHOST_NET_VQ_RX].rxq.queue = queue;

	dev = &n->dev;
	vqs[VHOST_NET_VQ_TX] = &n->vqs[VHOST_NET_VQ_TX].vq;
	vqs[VHOST_NET_VQ_RX] = &n->vqs[VHOST_NET_VQ_RX].vq;
	n->vqs[VHOST_NET_VQ_TX].vq.handle_kick = handle_tx_kick;
	n->vqs[VHOST_NET_VQ_RX].vq.handle_kick = handle_rx_kick;
	for (i = 0; i < VHOST_NET_VQ_MAX; i++) {
		n->vqs[i].ubufs = NULL;
		n->vqs[i].ubuf_info = NULL;
		n->vqs[i].upend_idx = 0;
		n->vqs[i].done_idx = 0;
		n->vqs[i].vhost_hlen = 0;
		n->vqs[i].sock_hlen = 0;
		n->vqs[i].rx_ring = NULL;
		vhost_net_buf_init(&n->vqs[i].rxq);
	}
	vhost_dev_init(dev, vqs, VHOST_NET_VQ_MAX);

	vhost_poll_init(n->poll + VHOST_NET_VQ_TX, handle_tx_net, EPOLLOUT, dev);
	vhost_poll_init(n->poll + VHOST_NET_VQ_RX, handle_rx_net, EPOLLIN, dev);

	f->private_data = n;

	return 0;
}

static struct socket *vhost_net_stop_vq(struct vhost_net *n,
					struct vhost_virtqueue *vq)
{
	struct socket *sock;
	struct vhost_net_virtqueue *nvq =
		container_of(vq, struct vhost_net_virtqueue, vq);

	mutex_lock(&vq->mutex);
	sock = vq->private_data;
	vhost_net_disable_vq(n, vq);
	vq->private_data = NULL;
	vhost_net_buf_unproduce(nvq);
	nvq->rx_ring = NULL;
	mutex_unlock(&vq->mutex);
	return sock;
}

static void vhost_net_stop(struct vhost_net *n, struct socket **tx_sock,
			   struct socket **rx_sock)
{
	*tx_sock = vhost_net_stop_vq(n, &n->vqs[VHOST_NET_VQ_TX].vq);
	*rx_sock = vhost_net_stop_vq(n, &n->vqs[VHOST_NET_VQ_RX].vq);
}

static void vhost_net_flush_vq(struct vhost_net *n, int index)
{
	vhost_poll_flush(n->poll + index);
	vhost_poll_flush(&n->vqs[index].vq.poll);
}

static void vhost_net_flush(struct vhost_net *n)
{
	vhost_net_flush_vq(n, VHOST_NET_VQ_TX);
	vhost_net_flush_vq(n, VHOST_NET_VQ_RX);
	if (n->vqs[VHOST_NET_VQ_TX].ubufs) {
		mutex_lock(&n->vqs[VHOST_NET_VQ_TX].vq.mutex);
		n->tx_flush = true;
		mutex_unlock(&n->vqs[VHOST_NET_VQ_TX].vq.mutex);
		/* Wait for all lower device DMAs done. */
		vhost_net_ubuf_put_and_wait(n->vqs[VHOST_NET_VQ_TX].ubufs);
		mutex_lock(&n->vqs[VHOST_NET_VQ_TX].vq.mutex);
		n->tx_flush = false;
		atomic_set(&n->vqs[VHOST_NET_VQ_TX].ubufs->refcount, 1);
		mutex_unlock(&n->vqs[VHOST_NET_VQ_TX].vq.mutex);
	}
}

static int vhost_net_release(struct inode *inode, struct file *f)
{
	struct vhost_net *n = f->private_data;
	struct socket *tx_sock;
	struct socket *rx_sock;

	vhost_net_stop(n, &tx_sock, &rx_sock);
	vhost_net_flush(n);
	vhost_dev_stop(&n->dev);
	vhost_dev_cleanup(&n->dev);
	vhost_net_vq_reset(n);
	if (tx_sock)
		sockfd_put(tx_sock);
	if (rx_sock)
		sockfd_put(rx_sock);
	/* Make sure no callbacks are outstanding */
	synchronize_rcu_bh();
	/* We do an extra flush before freeing memory,
	 * since jobs can re-queue themselves. */
	vhost_net_flush(n);
	kfree(n->vqs[VHOST_NET_VQ_RX].rxq.queue);
	kfree(n->dev.vqs);
	kvfree(n);
	return 0;
}

static struct socket *get_raw_socket(int fd)
{
	struct {
		struct sockaddr_ll sa;
		char  buf[MAX_ADDR_LEN];
	} uaddr;
	int r;
	struct socket *sock = sockfd_lookup(fd, &r);

	if (!sock)
		return ERR_PTR(-ENOTSOCK);

	/* Parameter checking */
	if (sock->sk->sk_type != SOCK_RAW) {
		r = -ESOCKTNOSUPPORT;
		goto err;
	}

	r = sock->ops->getname(sock, (struct sockaddr *)&uaddr.sa, 0);
	if (r < 0)
		goto err;

	if (uaddr.sa.sll_family != AF_PACKET) {
		r = -EPFNOSUPPORT;
		goto err;
	}
	return sock;
err:
	sockfd_put(sock);
	return ERR_PTR(r);
}

static struct ptr_ring *get_tap_ptr_ring(int fd)
{
	struct ptr_ring *ring;
	struct file *file = fget(fd);

	if (!file)
		return NULL;
	ring = tun_get_tx_ring(file);
	if (!IS_ERR(ring))
		goto out;
	ring = tap_get_ptr_ring(file);
	if (!IS_ERR(ring))
		goto out;
	ring = NULL;
out:
	fput(file);
	return ring;
}

static struct socket *get_tap_socket(int fd)
{
	struct file *file = fget(fd);
	struct socket *sock;

	if (!file)
		return ERR_PTR(-EBADF);
	sock = tun_get_socket(file);
	if (!IS_ERR(sock))
		return sock;
	sock = tap_get_socket(file);
	if (IS_ERR(sock))
		fput(file);
	return sock;
}

static struct socket *get_socket(int fd)
{
	struct socket *sock;

	/* special case to disable backend */
	if (fd == -1)
		return NULL;
	sock = get_raw_socket(fd);
	if (!IS_ERR(sock))
		return sock;
	sock = get_tap_socket(fd);
	if (!IS_ERR(sock))
		return sock;
	return ERR_PTR(-ENOTSOCK);
}

static long vhost_net_set_backend(struct vhost_net *n, unsigned index, int fd)
{
	struct socket *sock, *oldsock;
	struct vhost_virtqueue *vq;
	struct vhost_net_virtqueue *nvq;
	struct vhost_net_ubuf_ref *ubufs, *oldubufs = NULL;
	int r;

	mutex_lock(&n->dev.mutex);
	r = vhost_dev_check_owner(&n->dev);
	if (r)
		goto err;

	if (index >= VHOST_NET_VQ_MAX) {
		r = -ENOBUFS;
		goto err;
	}
	vq = &n->vqs[index].vq;
	nvq = &n->vqs[index];
	mutex_lock(&vq->mutex);

	/* Verify that ring has been setup correctly. */
	if (!vhost_vq_access_ok(vq)) {
		r = -EFAULT;
		goto err_vq;
	}
	sock = get_socket(fd);
	if (IS_ERR(sock)) {
		r = PTR_ERR(sock);
		goto err_vq;
	}

	/* start polling new socket */
	oldsock = vq->private_data;
	if (sock != oldsock) {
		ubufs = vhost_net_ubuf_alloc(vq,
					     sock && vhost_sock_zcopy(sock));
		if (IS_ERR(ubufs)) {
			r = PTR_ERR(ubufs);
			goto err_ubufs;
		}

		vhost_net_disable_vq(n, vq);
		vq->private_data = sock;
		vhost_net_buf_unproduce(nvq);
		r = vhost_vq_init_access(vq);
		if (r)
			goto err_used;
		r = vhost_net_enable_vq(n, vq);
		if (r)
			goto err_used;
		if (index == VHOST_NET_VQ_RX)
			nvq->rx_ring = get_tap_ptr_ring(fd);

		oldubufs = nvq->ubufs;
		nvq->ubufs = ubufs;

		n->tx_packets = 0;
		n->tx_zcopy_err = 0;
		n->tx_flush = false;
	}

	mutex_unlock(&vq->mutex);

	if (oldubufs) {
		vhost_net_ubuf_put_wait_and_free(oldubufs);
		mutex_lock(&vq->mutex);
		vhost_zerocopy_signal_used(n, vq);
		mutex_unlock(&vq->mutex);
	}

	if (oldsock) {
		vhost_net_flush_vq(n, index);
		sockfd_put(oldsock);
	}

	mutex_unlock(&n->dev.mutex);
	return 0;

err_used:
	vq->private_data = oldsock;
	vhost_net_enable_vq(n, vq);
	if (ubufs)
		vhost_net_ubuf_put_wait_and_free(ubufs);
err_ubufs:
	if (sock)
		sockfd_put(sock);
err_vq:
	mutex_unlock(&vq->mutex);
err:
	mutex_unlock(&n->dev.mutex);
	return r;
}

static long vhost_net_reset_owner(struct vhost_net *n)
{
	struct socket *tx_sock = NULL;
	struct socket *rx_sock = NULL;
	long err;
	struct vhost_umem *umem;

	mutex_lock(&n->dev.mutex);
	err = vhost_dev_check_owner(&n->dev);
	if (err)
		goto done;
	umem = vhost_dev_reset_owner_prepare();
	if (!umem) {
		err = -ENOMEM;
		goto done;
	}
	vhost_net_stop(n, &tx_sock, &rx_sock);
	vhost_net_flush(n);
	vhost_dev_stop(&n->dev);
	vhost_dev_reset_owner(&n->dev, umem);
	vhost_net_vq_reset(n);
done:
	mutex_unlock(&n->dev.mutex);
	if (tx_sock)
		sockfd_put(tx_sock);
	if (rx_sock)
		sockfd_put(rx_sock);
	return err;
}

static int vhost_net_set_features(struct vhost_net *n, u64 features)
{
	size_t vhost_hlen, sock_hlen, hdr_len;
	int i;

	hdr_len = (features & ((1ULL << VIRTIO_NET_F_MRG_RXBUF) |
			       (1ULL << VIRTIO_F_VERSION_1))) ?
			sizeof(struct virtio_net_hdr_mrg_rxbuf) :
			sizeof(struct virtio_net_hdr);
	if (features & (1 << VHOST_NET_F_VIRTIO_NET_HDR)) {
		/* vhost provides vnet_hdr */
		vhost_hlen = hdr_len;
		sock_hlen = 0;
	} else {
		/* socket provides vnet_hdr */
		vhost_hlen = 0;
		sock_hlen = hdr_len;
	}
	mutex_lock(&n->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&n->dev))
		goto out_unlock;

	if ((features & (1ULL << VIRTIO_F_IOMMU_PLATFORM))) {
		if (vhost_init_device_iotlb(&n->dev, true))
			goto out_unlock;
	}

	for (i = 0; i < VHOST_NET_VQ_MAX; ++i) {
		mutex_lock(&n->vqs[i].vq.mutex);
		n->vqs[i].vq.acked_features = features;
		n->vqs[i].vhost_hlen = vhost_hlen;
		n->vqs[i].sock_hlen = sock_hlen;
		mutex_unlock(&n->vqs[i].vq.mutex);
	}
	mutex_unlock(&n->dev.mutex);
	return 0;

out_unlock:
	mutex_unlock(&n->dev.mutex);
	return -EFAULT;
}

static long vhost_net_set_owner(struct vhost_net *n)
{
	int r;

	mutex_lock(&n->dev.mutex);
	if (vhost_dev_has_owner(&n->dev)) {
		r = -EBUSY;
		goto out;
	}
	r = vhost_net_set_ubuf_info(n);
	if (r)
		goto out;
	r = vhost_dev_set_owner(&n->dev);
	if (r)
		vhost_net_clear_ubuf_info(n);
	vhost_net_flush(n);
out:
	mutex_unlock(&n->dev.mutex);
	return r;
}

static long vhost_net_ioctl(struct file *f, unsigned int ioctl,
			    unsigned long arg)
{
	struct vhost_net *n = f->private_data;
	void __user *argp = (void __user *)arg;
	u64 __user *featurep = argp;
	struct vhost_vring_file backend;
	u64 features;
	int r;

	switch (ioctl) {
	case VHOST_NET_SET_BACKEND:
		if (copy_from_user(&backend, argp, sizeof backend))
			return -EFAULT;
		return vhost_net_set_backend(n, backend.index, backend.fd);
	case VHOST_GET_FEATURES:
		features = VHOST_NET_FEATURES;
		if (copy_to_user(featurep, &features, sizeof features))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, featurep, sizeof features))
			return -EFAULT;
		if (features & ~VHOST_NET_FEATURES)
			return -EOPNOTSUPP;
		return vhost_net_set_features(n, features);
	case VHOST_RESET_OWNER:
		return vhost_net_reset_owner(n);
	case VHOST_SET_OWNER:
		return vhost_net_set_owner(n);
	default:
		mutex_lock(&n->dev.mutex);
		r = vhost_dev_ioctl(&n->dev, ioctl, argp);
		if (r == -ENOIOCTLCMD)
			r = vhost_vring_ioctl(&n->dev, ioctl, argp);
		else
			vhost_net_flush(n);
		mutex_unlock(&n->dev.mutex);
		return r;
	}
}

#ifdef CONFIG_COMPAT
static long vhost_net_compat_ioctl(struct file *f, unsigned int ioctl,
				   unsigned long arg)
{
	return vhost_net_ioctl(f, ioctl, (unsigned long)compat_ptr(arg));
}
#endif

static ssize_t vhost_net_chr_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct vhost_net *n = file->private_data;
	struct vhost_dev *dev = &n->dev;
	int noblock = file->f_flags & O_NONBLOCK;

	return vhost_chr_read_iter(dev, to, noblock);
}

static ssize_t vhost_net_chr_write_iter(struct kiocb *iocb,
					struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct vhost_net *n = file->private_data;
	struct vhost_dev *dev = &n->dev;

	return vhost_chr_write_iter(dev, from);
}

static __poll_t vhost_net_chr_poll(struct file *file, poll_table *wait)
{
	struct vhost_net *n = file->private_data;
	struct vhost_dev *dev = &n->dev;

	return vhost_chr_poll(file, dev, wait);
}

static const struct file_operations vhost_net_fops = {
	.owner          = THIS_MODULE,
	.release        = vhost_net_release,
	.read_iter      = vhost_net_chr_read_iter,
	.write_iter     = vhost_net_chr_write_iter,
	.poll           = vhost_net_chr_poll,
	.unlocked_ioctl = vhost_net_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = vhost_net_compat_ioctl,
#endif
	.open           = vhost_net_open,
	.llseek		= noop_llseek,
};

static struct miscdevice vhost_net_misc = {
	.minor = VHOST_NET_MINOR,
	.name = "vhost-net",
	.fops = &vhost_net_fops,
};

static int vhost_net_init(void)
{
	if (experimental_zcopytx)
		vhost_net_enable_zcopy(VHOST_NET_VQ_TX);
	return misc_register(&vhost_net_misc);
}
module_init(vhost_net_init);

static void vhost_net_exit(void)
{
	misc_deregister(&vhost_net_misc);
}
module_exit(vhost_net_exit);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael S. Tsirkin");
MODULE_DESCRIPTION("Host kernel accelerator for virtio net");
MODULE_ALIAS_MISCDEV(VHOST_NET_MINOR);
MODULE_ALIAS("devname:vhost-net");
