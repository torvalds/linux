/*
 * vhost transport for vsock
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <net/sock.h>
#include <linux/virtio_vsock.h>
#include <linux/vhost.h>

#include <net/af_vsock.h>
#include "vhost.h"

#define VHOST_VSOCK_DEFAULT_HOST_CID	2

enum {
	VHOST_VSOCK_FEATURES = VHOST_FEATURES,
};

/* Used to track all the vhost_vsock instances on the system. */
static DEFINE_SPINLOCK(vhost_vsock_lock);
static LIST_HEAD(vhost_vsock_list);

struct vhost_vsock {
	struct vhost_dev dev;
	struct vhost_virtqueue vqs[2];

	/* Link to global vhost_vsock_list, protected by vhost_vsock_lock */
	struct list_head list;

	struct vhost_work send_pkt_work;
	spinlock_t send_pkt_list_lock;
	struct list_head send_pkt_list;	/* host->guest pending packets */

	atomic_t queued_replies;

	u32 guest_cid;
};

static u32 vhost_transport_get_local_cid(void)
{
	return VHOST_VSOCK_DEFAULT_HOST_CID;
}

static struct vhost_vsock *__vhost_vsock_get(u32 guest_cid)
{
	struct vhost_vsock *vsock;

	list_for_each_entry(vsock, &vhost_vsock_list, list) {
		u32 other_cid = vsock->guest_cid;

		/* Skip instances that have no CID yet */
		if (other_cid == 0)
			continue;

		if (other_cid == guest_cid)
			return vsock;

	}

	return NULL;
}

static struct vhost_vsock *vhost_vsock_get(u32 guest_cid)
{
	struct vhost_vsock *vsock;

	spin_lock_bh(&vhost_vsock_lock);
	vsock = __vhost_vsock_get(guest_cid);
	spin_unlock_bh(&vhost_vsock_lock);

	return vsock;
}

static void
vhost_transport_do_send_pkt(struct vhost_vsock *vsock,
			    struct vhost_virtqueue *vq)
{
	struct vhost_virtqueue *tx_vq = &vsock->vqs[VSOCK_VQ_TX];
	bool added = false;
	bool restart_tx = false;

	mutex_lock(&vq->mutex);

	if (!vq->private_data)
		goto out;

	/* Avoid further vmexits, we're already processing the virtqueue */
	vhost_disable_notify(&vsock->dev, vq);

	for (;;) {
		struct virtio_vsock_pkt *pkt;
		struct iov_iter iov_iter;
		unsigned out, in;
		size_t nbytes;
		size_t len;
		int head;

		spin_lock_bh(&vsock->send_pkt_list_lock);
		if (list_empty(&vsock->send_pkt_list)) {
			spin_unlock_bh(&vsock->send_pkt_list_lock);
			vhost_enable_notify(&vsock->dev, vq);
			break;
		}

		pkt = list_first_entry(&vsock->send_pkt_list,
				       struct virtio_vsock_pkt, list);
		list_del_init(&pkt->list);
		spin_unlock_bh(&vsock->send_pkt_list_lock);

		head = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
					 &out, &in, NULL, NULL);
		if (head < 0) {
			spin_lock_bh(&vsock->send_pkt_list_lock);
			list_add(&pkt->list, &vsock->send_pkt_list);
			spin_unlock_bh(&vsock->send_pkt_list_lock);
			break;
		}

		if (head == vq->num) {
			spin_lock_bh(&vsock->send_pkt_list_lock);
			list_add(&pkt->list, &vsock->send_pkt_list);
			spin_unlock_bh(&vsock->send_pkt_list_lock);

			/* We cannot finish yet if more buffers snuck in while
			 * re-enabling notify.
			 */
			if (unlikely(vhost_enable_notify(&vsock->dev, vq))) {
				vhost_disable_notify(&vsock->dev, vq);
				continue;
			}
			break;
		}

		if (out) {
			virtio_transport_free_pkt(pkt);
			vq_err(vq, "Expected 0 output buffers, got %u\n", out);
			break;
		}

		len = iov_length(&vq->iov[out], in);
		iov_iter_init(&iov_iter, READ, &vq->iov[out], in, len);

		nbytes = copy_to_iter(&pkt->hdr, sizeof(pkt->hdr), &iov_iter);
		if (nbytes != sizeof(pkt->hdr)) {
			virtio_transport_free_pkt(pkt);
			vq_err(vq, "Faulted on copying pkt hdr\n");
			break;
		}

		nbytes = copy_to_iter(pkt->buf, pkt->len, &iov_iter);
		if (nbytes != pkt->len) {
			virtio_transport_free_pkt(pkt);
			vq_err(vq, "Faulted on copying pkt buf\n");
			break;
		}

		vhost_add_used(vq, head, sizeof(pkt->hdr) + pkt->len);
		added = true;

		if (pkt->reply) {
			int val;

			val = atomic_dec_return(&vsock->queued_replies);

			/* Do we have resources to resume tx processing? */
			if (val + 1 == tx_vq->num)
				restart_tx = true;
		}

		/* Deliver to monitoring devices all correctly transmitted
		 * packets.
		 */
		virtio_transport_deliver_tap_pkt(pkt);

		virtio_transport_free_pkt(pkt);
	}
	if (added)
		vhost_signal(&vsock->dev, vq);

out:
	mutex_unlock(&vq->mutex);

	if (restart_tx)
		vhost_poll_queue(&tx_vq->poll);
}

static void vhost_transport_send_pkt_work(struct vhost_work *work)
{
	struct vhost_virtqueue *vq;
	struct vhost_vsock *vsock;

	vsock = container_of(work, struct vhost_vsock, send_pkt_work);
	vq = &vsock->vqs[VSOCK_VQ_RX];

	vhost_transport_do_send_pkt(vsock, vq);
}

static int
vhost_transport_send_pkt(struct virtio_vsock_pkt *pkt)
{
	struct vhost_vsock *vsock;
	int len = pkt->len;

	/* Find the vhost_vsock according to guest context id  */
	vsock = vhost_vsock_get(le64_to_cpu(pkt->hdr.dst_cid));
	if (!vsock) {
		virtio_transport_free_pkt(pkt);
		return -ENODEV;
	}

	if (pkt->reply)
		atomic_inc(&vsock->queued_replies);

	spin_lock_bh(&vsock->send_pkt_list_lock);
	list_add_tail(&pkt->list, &vsock->send_pkt_list);
	spin_unlock_bh(&vsock->send_pkt_list_lock);

	vhost_work_queue(&vsock->dev, &vsock->send_pkt_work);
	return len;
}

static int
vhost_transport_cancel_pkt(struct vsock_sock *vsk)
{
	struct vhost_vsock *vsock;
	struct virtio_vsock_pkt *pkt, *n;
	int cnt = 0;
	LIST_HEAD(freeme);

	/* Find the vhost_vsock according to guest context id  */
	vsock = vhost_vsock_get(vsk->remote_addr.svm_cid);
	if (!vsock)
		return -ENODEV;

	spin_lock_bh(&vsock->send_pkt_list_lock);
	list_for_each_entry_safe(pkt, n, &vsock->send_pkt_list, list) {
		if (pkt->vsk != vsk)
			continue;
		list_move(&pkt->list, &freeme);
	}
	spin_unlock_bh(&vsock->send_pkt_list_lock);

	list_for_each_entry_safe(pkt, n, &freeme, list) {
		if (pkt->reply)
			cnt++;
		list_del(&pkt->list);
		virtio_transport_free_pkt(pkt);
	}

	if (cnt) {
		struct vhost_virtqueue *tx_vq = &vsock->vqs[VSOCK_VQ_TX];
		int new_cnt;

		new_cnt = atomic_sub_return(cnt, &vsock->queued_replies);
		if (new_cnt + cnt >= tx_vq->num && new_cnt < tx_vq->num)
			vhost_poll_queue(&tx_vq->poll);
	}

	return 0;
}

static struct virtio_vsock_pkt *
vhost_vsock_alloc_pkt(struct vhost_virtqueue *vq,
		      unsigned int out, unsigned int in)
{
	struct virtio_vsock_pkt *pkt;
	struct iov_iter iov_iter;
	size_t nbytes;
	size_t len;

	if (in != 0) {
		vq_err(vq, "Expected 0 input buffers, got %u\n", in);
		return NULL;
	}

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return NULL;

	len = iov_length(vq->iov, out);
	iov_iter_init(&iov_iter, WRITE, vq->iov, out, len);

	nbytes = copy_from_iter(&pkt->hdr, sizeof(pkt->hdr), &iov_iter);
	if (nbytes != sizeof(pkt->hdr)) {
		vq_err(vq, "Expected %zu bytes for pkt->hdr, got %zu bytes\n",
		       sizeof(pkt->hdr), nbytes);
		kfree(pkt);
		return NULL;
	}

	if (le16_to_cpu(pkt->hdr.type) == VIRTIO_VSOCK_TYPE_STREAM)
		pkt->len = le32_to_cpu(pkt->hdr.len);

	/* No payload */
	if (!pkt->len)
		return pkt;

	/* The pkt is too big */
	if (pkt->len > VIRTIO_VSOCK_MAX_PKT_BUF_SIZE) {
		kfree(pkt);
		return NULL;
	}

	pkt->buf = kmalloc(pkt->len, GFP_KERNEL);
	if (!pkt->buf) {
		kfree(pkt);
		return NULL;
	}

	nbytes = copy_from_iter(pkt->buf, pkt->len, &iov_iter);
	if (nbytes != pkt->len) {
		vq_err(vq, "Expected %u byte payload, got %zu bytes\n",
		       pkt->len, nbytes);
		virtio_transport_free_pkt(pkt);
		return NULL;
	}

	return pkt;
}

/* Is there space left for replies to rx packets? */
static bool vhost_vsock_more_replies(struct vhost_vsock *vsock)
{
	struct vhost_virtqueue *vq = &vsock->vqs[VSOCK_VQ_TX];
	int val;

	smp_rmb(); /* paired with atomic_inc() and atomic_dec_return() */
	val = atomic_read(&vsock->queued_replies);

	return val < vq->num;
}

static void vhost_vsock_handle_tx_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_vsock *vsock = container_of(vq->dev, struct vhost_vsock,
						 dev);
	struct virtio_vsock_pkt *pkt;
	int head;
	unsigned int out, in;
	bool added = false;

	mutex_lock(&vq->mutex);

	if (!vq->private_data)
		goto out;

	vhost_disable_notify(&vsock->dev, vq);
	for (;;) {
		u32 len;

		if (!vhost_vsock_more_replies(vsock)) {
			/* Stop tx until the device processes already
			 * pending replies.  Leave tx virtqueue
			 * callbacks disabled.
			 */
			goto no_more_replies;
		}

		head = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
					 &out, &in, NULL, NULL);
		if (head < 0)
			break;

		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&vsock->dev, vq))) {
				vhost_disable_notify(&vsock->dev, vq);
				continue;
			}
			break;
		}

		pkt = vhost_vsock_alloc_pkt(vq, out, in);
		if (!pkt) {
			vq_err(vq, "Faulted on pkt\n");
			continue;
		}

		len = pkt->len;

		/* Deliver to monitoring devices all received packets */
		virtio_transport_deliver_tap_pkt(pkt);

		/* Only accept correctly addressed packets */
		if (le64_to_cpu(pkt->hdr.src_cid) == vsock->guest_cid)
			virtio_transport_recv_pkt(pkt);
		else
			virtio_transport_free_pkt(pkt);

		vhost_add_used(vq, head, sizeof(pkt->hdr) + len);
		added = true;
	}

no_more_replies:
	if (added)
		vhost_signal(&vsock->dev, vq);

out:
	mutex_unlock(&vq->mutex);
}

static void vhost_vsock_handle_rx_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						poll.work);
	struct vhost_vsock *vsock = container_of(vq->dev, struct vhost_vsock,
						 dev);

	vhost_transport_do_send_pkt(vsock, vq);
}

static int vhost_vsock_start(struct vhost_vsock *vsock)
{
	struct vhost_virtqueue *vq;
	size_t i;
	int ret;

	mutex_lock(&vsock->dev.mutex);

	ret = vhost_dev_check_owner(&vsock->dev);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(vsock->vqs); i++) {
		vq = &vsock->vqs[i];

		mutex_lock(&vq->mutex);

		if (!vhost_vq_access_ok(vq)) {
			ret = -EFAULT;
			goto err_vq;
		}

		if (!vq->private_data) {
			vq->private_data = vsock;
			ret = vhost_vq_init_access(vq);
			if (ret)
				goto err_vq;
		}

		mutex_unlock(&vq->mutex);
	}

	mutex_unlock(&vsock->dev.mutex);
	return 0;

err_vq:
	vq->private_data = NULL;
	mutex_unlock(&vq->mutex);

	for (i = 0; i < ARRAY_SIZE(vsock->vqs); i++) {
		vq = &vsock->vqs[i];

		mutex_lock(&vq->mutex);
		vq->private_data = NULL;
		mutex_unlock(&vq->mutex);
	}
err:
	mutex_unlock(&vsock->dev.mutex);
	return ret;
}

static int vhost_vsock_stop(struct vhost_vsock *vsock)
{
	size_t i;
	int ret;

	mutex_lock(&vsock->dev.mutex);

	ret = vhost_dev_check_owner(&vsock->dev);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(vsock->vqs); i++) {
		struct vhost_virtqueue *vq = &vsock->vqs[i];

		mutex_lock(&vq->mutex);
		vq->private_data = NULL;
		mutex_unlock(&vq->mutex);
	}

err:
	mutex_unlock(&vsock->dev.mutex);
	return ret;
}

static void vhost_vsock_free(struct vhost_vsock *vsock)
{
	kvfree(vsock);
}

static int vhost_vsock_dev_open(struct inode *inode, struct file *file)
{
	struct vhost_virtqueue **vqs;
	struct vhost_vsock *vsock;
	int ret;

	/* This struct is large and allocation could fail, fall back to vmalloc
	 * if there is no other way.
	 */
	vsock = kvmalloc(sizeof(*vsock), GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!vsock)
		return -ENOMEM;

	vqs = kmalloc_array(ARRAY_SIZE(vsock->vqs), sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		ret = -ENOMEM;
		goto out;
	}

	vsock->guest_cid = 0; /* no CID assigned yet */

	atomic_set(&vsock->queued_replies, 0);

	vqs[VSOCK_VQ_TX] = &vsock->vqs[VSOCK_VQ_TX];
	vqs[VSOCK_VQ_RX] = &vsock->vqs[VSOCK_VQ_RX];
	vsock->vqs[VSOCK_VQ_TX].handle_kick = vhost_vsock_handle_tx_kick;
	vsock->vqs[VSOCK_VQ_RX].handle_kick = vhost_vsock_handle_rx_kick;

	vhost_dev_init(&vsock->dev, vqs, ARRAY_SIZE(vsock->vqs));

	file->private_data = vsock;
	spin_lock_init(&vsock->send_pkt_list_lock);
	INIT_LIST_HEAD(&vsock->send_pkt_list);
	vhost_work_init(&vsock->send_pkt_work, vhost_transport_send_pkt_work);

	spin_lock_bh(&vhost_vsock_lock);
	list_add_tail(&vsock->list, &vhost_vsock_list);
	spin_unlock_bh(&vhost_vsock_lock);
	return 0;

out:
	vhost_vsock_free(vsock);
	return ret;
}

static void vhost_vsock_flush(struct vhost_vsock *vsock)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsock->vqs); i++)
		if (vsock->vqs[i].handle_kick)
			vhost_poll_flush(&vsock->vqs[i].poll);
	vhost_work_flush(&vsock->dev, &vsock->send_pkt_work);
}

static void vhost_vsock_reset_orphans(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	/* vmci_transport.c doesn't take sk_lock here either.  At least we're
	 * under vsock_table_lock so the sock cannot disappear while we're
	 * executing.
	 */

	if (!vhost_vsock_get(vsk->remote_addr.svm_cid)) {
		sock_set_flag(sk, SOCK_DONE);
		vsk->peer_shutdown = SHUTDOWN_MASK;
		sk->sk_state = SS_UNCONNECTED;
		sk->sk_err = ECONNRESET;
		sk->sk_error_report(sk);
	}
}

static int vhost_vsock_dev_release(struct inode *inode, struct file *file)
{
	struct vhost_vsock *vsock = file->private_data;

	spin_lock_bh(&vhost_vsock_lock);
	list_del(&vsock->list);
	spin_unlock_bh(&vhost_vsock_lock);

	/* Iterating over all connections for all CIDs to find orphans is
	 * inefficient.  Room for improvement here. */
	vsock_for_each_connected_socket(vhost_vsock_reset_orphans);

	vhost_vsock_stop(vsock);
	vhost_vsock_flush(vsock);
	vhost_dev_stop(&vsock->dev);

	spin_lock_bh(&vsock->send_pkt_list_lock);
	while (!list_empty(&vsock->send_pkt_list)) {
		struct virtio_vsock_pkt *pkt;

		pkt = list_first_entry(&vsock->send_pkt_list,
				struct virtio_vsock_pkt, list);
		list_del_init(&pkt->list);
		virtio_transport_free_pkt(pkt);
	}
	spin_unlock_bh(&vsock->send_pkt_list_lock);

	vhost_dev_cleanup(&vsock->dev);
	kfree(vsock->dev.vqs);
	vhost_vsock_free(vsock);
	return 0;
}

static int vhost_vsock_set_cid(struct vhost_vsock *vsock, u64 guest_cid)
{
	struct vhost_vsock *other;

	/* Refuse reserved CIDs */
	if (guest_cid <= VMADDR_CID_HOST ||
	    guest_cid == U32_MAX)
		return -EINVAL;

	/* 64-bit CIDs are not yet supported */
	if (guest_cid > U32_MAX)
		return -EINVAL;

	/* Refuse if CID is already in use */
	spin_lock_bh(&vhost_vsock_lock);
	other = __vhost_vsock_get(guest_cid);
	if (other && other != vsock) {
		spin_unlock_bh(&vhost_vsock_lock);
		return -EADDRINUSE;
	}
	vsock->guest_cid = guest_cid;
	spin_unlock_bh(&vhost_vsock_lock);

	return 0;
}

static int vhost_vsock_set_features(struct vhost_vsock *vsock, u64 features)
{
	struct vhost_virtqueue *vq;
	int i;

	if (features & ~VHOST_VSOCK_FEATURES)
		return -EOPNOTSUPP;

	mutex_lock(&vsock->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&vsock->dev)) {
		mutex_unlock(&vsock->dev.mutex);
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(vsock->vqs); i++) {
		vq = &vsock->vqs[i];
		mutex_lock(&vq->mutex);
		vq->acked_features = features;
		mutex_unlock(&vq->mutex);
	}
	mutex_unlock(&vsock->dev.mutex);
	return 0;
}

static long vhost_vsock_dev_ioctl(struct file *f, unsigned int ioctl,
				  unsigned long arg)
{
	struct vhost_vsock *vsock = f->private_data;
	void __user *argp = (void __user *)arg;
	u64 guest_cid;
	u64 features;
	int start;
	int r;

	switch (ioctl) {
	case VHOST_VSOCK_SET_GUEST_CID:
		if (copy_from_user(&guest_cid, argp, sizeof(guest_cid)))
			return -EFAULT;
		return vhost_vsock_set_cid(vsock, guest_cid);
	case VHOST_VSOCK_SET_RUNNING:
		if (copy_from_user(&start, argp, sizeof(start)))
			return -EFAULT;
		if (start)
			return vhost_vsock_start(vsock);
		else
			return vhost_vsock_stop(vsock);
	case VHOST_GET_FEATURES:
		features = VHOST_VSOCK_FEATURES;
		if (copy_to_user(argp, &features, sizeof(features)))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, argp, sizeof(features)))
			return -EFAULT;
		return vhost_vsock_set_features(vsock, features);
	default:
		mutex_lock(&vsock->dev.mutex);
		r = vhost_dev_ioctl(&vsock->dev, ioctl, argp);
		if (r == -ENOIOCTLCMD)
			r = vhost_vring_ioctl(&vsock->dev, ioctl, argp);
		else
			vhost_vsock_flush(vsock);
		mutex_unlock(&vsock->dev.mutex);
		return r;
	}
}

static const struct file_operations vhost_vsock_fops = {
	.owner          = THIS_MODULE,
	.open           = vhost_vsock_dev_open,
	.release        = vhost_vsock_dev_release,
	.llseek		= noop_llseek,
	.unlocked_ioctl = vhost_vsock_dev_ioctl,
};

static struct miscdevice vhost_vsock_misc = {
	.minor = VHOST_VSOCK_MINOR,
	.name = "vhost-vsock",
	.fops = &vhost_vsock_fops,
};

static struct virtio_transport vhost_transport = {
	.transport = {
		.get_local_cid            = vhost_transport_get_local_cid,

		.init                     = virtio_transport_do_socket_init,
		.destruct                 = virtio_transport_destruct,
		.release                  = virtio_transport_release,
		.connect                  = virtio_transport_connect,
		.shutdown                 = virtio_transport_shutdown,
		.cancel_pkt               = vhost_transport_cancel_pkt,

		.dgram_enqueue            = virtio_transport_dgram_enqueue,
		.dgram_dequeue            = virtio_transport_dgram_dequeue,
		.dgram_bind               = virtio_transport_dgram_bind,
		.dgram_allow              = virtio_transport_dgram_allow,

		.stream_enqueue           = virtio_transport_stream_enqueue,
		.stream_dequeue           = virtio_transport_stream_dequeue,
		.stream_has_data          = virtio_transport_stream_has_data,
		.stream_has_space         = virtio_transport_stream_has_space,
		.stream_rcvhiwat          = virtio_transport_stream_rcvhiwat,
		.stream_is_active         = virtio_transport_stream_is_active,
		.stream_allow             = virtio_transport_stream_allow,

		.notify_poll_in           = virtio_transport_notify_poll_in,
		.notify_poll_out          = virtio_transport_notify_poll_out,
		.notify_recv_init         = virtio_transport_notify_recv_init,
		.notify_recv_pre_block    = virtio_transport_notify_recv_pre_block,
		.notify_recv_pre_dequeue  = virtio_transport_notify_recv_pre_dequeue,
		.notify_recv_post_dequeue = virtio_transport_notify_recv_post_dequeue,
		.notify_send_init         = virtio_transport_notify_send_init,
		.notify_send_pre_block    = virtio_transport_notify_send_pre_block,
		.notify_send_pre_enqueue  = virtio_transport_notify_send_pre_enqueue,
		.notify_send_post_enqueue = virtio_transport_notify_send_post_enqueue,

		.set_buffer_size          = virtio_transport_set_buffer_size,
		.set_min_buffer_size      = virtio_transport_set_min_buffer_size,
		.set_max_buffer_size      = virtio_transport_set_max_buffer_size,
		.get_buffer_size          = virtio_transport_get_buffer_size,
		.get_min_buffer_size      = virtio_transport_get_min_buffer_size,
		.get_max_buffer_size      = virtio_transport_get_max_buffer_size,
	},

	.send_pkt = vhost_transport_send_pkt,
};

static int __init vhost_vsock_init(void)
{
	int ret;

	ret = vsock_core_init(&vhost_transport.transport);
	if (ret < 0)
		return ret;
	return misc_register(&vhost_vsock_misc);
};

static void __exit vhost_vsock_exit(void)
{
	misc_deregister(&vhost_vsock_misc);
	vsock_core_exit();
};

module_init(vhost_vsock_init);
module_exit(vhost_vsock_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("vhost transport for vsock ");
MODULE_ALIAS_MISCDEV(VHOST_VSOCK_MINOR);
MODULE_ALIAS("devname:vhost-vsock");
