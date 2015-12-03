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
#include <linux/module.h>
#include <linux/mutex.h>
#include <net/sock.h>
#include <linux/virtio_vsock.h>
#include <linux/vhost.h>

#include <net/af_vsock.h>
#include "vhost.h"
#include "vsock.h"

#define VHOST_VSOCK_DEFAULT_HOST_CID	2

static int vhost_transport_socket_init(struct vsock_sock *vsk,
				       struct vsock_sock *psk);

enum {
	VHOST_VSOCK_FEATURES = VHOST_FEATURES,
};

/* Used to track all the vhost_vsock instances on the system. */
static LIST_HEAD(vhost_vsock_list);
static DEFINE_MUTEX(vhost_vsock_mutex);

struct vhost_vsock_virtqueue {
	struct vhost_virtqueue vq;
};

struct vhost_vsock {
	/* Vhost device */
	struct vhost_dev dev;
	/* Vhost vsock virtqueue*/
	struct vhost_vsock_virtqueue vqs[VSOCK_VQ_MAX];
	/* Link to global vhost_vsock_list*/
	struct list_head list;
	/* Head for pkt from host to guest */
	struct list_head send_pkt_list;
	/* Work item to send pkt */
	struct vhost_work send_pkt_work;
	/* Wait queue for send pkt */
	wait_queue_head_t queue_wait;
	/* Used for global tx buf limitation */
	u32 total_tx_buf;
	/* Guest contex id this vhost_vsock instance handles */
	u32 guest_cid;
};

static u32 vhost_transport_get_local_cid(void)
{
	u32 cid = VHOST_VSOCK_DEFAULT_HOST_CID;
	return cid;
}

static struct vhost_vsock *vhost_vsock_get(u32 guest_cid)
{
	struct vhost_vsock *vsock;

	mutex_lock(&vhost_vsock_mutex);
	list_for_each_entry(vsock, &vhost_vsock_list, list) {
		if (vsock->guest_cid == guest_cid) {
			mutex_unlock(&vhost_vsock_mutex);
			return vsock;
		}
	}
	mutex_unlock(&vhost_vsock_mutex);

	return NULL;
}

static void
vhost_transport_do_send_pkt(struct vhost_vsock *vsock,
			    struct vhost_virtqueue *vq)
{
	bool added = false;

	mutex_lock(&vq->mutex);
	vhost_disable_notify(&vsock->dev, vq);
	for (;;) {
		struct virtio_vsock_pkt *pkt;
		struct iov_iter iov_iter;
		unsigned out, in;
		struct sock *sk;
		size_t nbytes;
		size_t len;
		int head;

		if (list_empty(&vsock->send_pkt_list)) {
			vhost_enable_notify(&vsock->dev, vq);
			break;
		}

		head = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
					 &out, &in, NULL, NULL);
		pr_debug("%s: head = %d\n", __func__, head);
		if (head < 0)
			break;

		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&vsock->dev, vq))) {
				vhost_disable_notify(&vsock->dev, vq);
				continue;
			}
			break;
		}

		pkt = list_first_entry(&vsock->send_pkt_list,
				       struct virtio_vsock_pkt, list);
		list_del_init(&pkt->list);

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

		vhost_add_used(vq, head, pkt->len); /* TODO should this be sizeof(pkt->hdr) + pkt->len? */
		added = true;

		virtio_transport_dec_tx_pkt(pkt);
		vsock->total_tx_buf -= pkt->len;

		sk = sk_vsock(pkt->trans->vsk);
		/* Release refcnt taken in vhost_transport_send_pkt */
		sock_put(sk);

		virtio_transport_free_pkt(pkt);
	}
	if (added)
		vhost_signal(&vsock->dev, vq);
	mutex_unlock(&vq->mutex);

	if (added)
		wake_up(&vsock->queue_wait);
}

static void vhost_transport_send_pkt_work(struct vhost_work *work)
{
	struct vhost_virtqueue *vq;
	struct vhost_vsock *vsock;

	vsock = container_of(work, struct vhost_vsock, send_pkt_work);
	vq = &vsock->vqs[VSOCK_VQ_RX].vq;

	vhost_transport_do_send_pkt(vsock, vq);
}

static int
vhost_transport_send_pkt(struct vsock_sock *vsk,
			 struct virtio_vsock_pkt_info *info)
{
	u32 src_cid, src_port, dst_cid, dst_port;
	struct virtio_transport *trans;
	struct virtio_vsock_pkt *pkt;
	struct vhost_virtqueue *vq;
	struct vhost_vsock *vsock;
	u32 pkt_len = info->pkt_len;
	DEFINE_WAIT(wait);

	src_cid = vhost_transport_get_local_cid();
	src_port = vsk->local_addr.svm_port;
	if (!info->remote_cid) {
		dst_cid	= vsk->remote_addr.svm_cid;
		dst_port = vsk->remote_addr.svm_port;
	} else {
		dst_cid = info->remote_cid;
		dst_port = info->remote_port;
	}

	/* Find the vhost_vsock according to guest context id  */
	vsock = vhost_vsock_get(dst_cid);
	if (!vsock)
		return -ENODEV;

	trans = vsk->trans;
	vq = &vsock->vqs[VSOCK_VQ_RX].vq;

	/* we can send less than pkt_len bytes */
	if (pkt_len > VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE)
		pkt_len = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;

	/* virtio_transport_get_credit might return less than pkt_len credit */
	pkt_len = virtio_transport_get_credit(trans, pkt_len);

	/* Do not send zero length OP_RW pkt*/
	if (pkt_len == 0 && info->op == VIRTIO_VSOCK_OP_RW)
		return pkt_len;

	/* Respect global tx buf limitation */
	mutex_lock(&vq->mutex);
	while (pkt_len + vsock->total_tx_buf > VIRTIO_VSOCK_MAX_TX_BUF_SIZE) {
		prepare_to_wait_exclusive(&vsock->queue_wait, &wait,
					  TASK_UNINTERRUPTIBLE);
		mutex_unlock(&vq->mutex);
		schedule();
		mutex_lock(&vq->mutex);
		finish_wait(&vsock->queue_wait, &wait);
	}
	vsock->total_tx_buf += pkt_len;
	mutex_unlock(&vq->mutex);

	pkt = virtio_transport_alloc_pkt(vsk, info, pkt_len,
					 src_cid, src_port,
					 dst_cid, dst_port);
	if (!pkt) {
		mutex_lock(&vq->mutex);
		vsock->total_tx_buf -= pkt_len;
		mutex_unlock(&vq->mutex);
		virtio_transport_put_credit(trans, pkt_len);
		return -ENOMEM;
	}

	pr_debug("%s:info->pkt_len= %d\n", __func__, pkt_len);
	/* Released in vhost_transport_do_send_pkt */
	sock_hold(&trans->vsk->sk);
	virtio_transport_inc_tx_pkt(pkt);

	/* Queue it up in vhost work */
	mutex_lock(&vq->mutex);
	list_add_tail(&pkt->list, &vsock->send_pkt_list);
	vhost_work_queue(&vsock->dev, &vsock->send_pkt_work);
	mutex_unlock(&vq->mutex);

	return pkt_len;
}

static struct virtio_transport_pkt_ops vhost_ops = {
	.send_pkt = vhost_transport_send_pkt,
};

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

	if (le16_to_cpu(pkt->hdr.type) == VIRTIO_VSOCK_TYPE_DGRAM)
		pkt->len = le32_to_cpu(pkt->hdr.len) & 0XFFFF;
	else if (le16_to_cpu(pkt->hdr.type) == VIRTIO_VSOCK_TYPE_STREAM)
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

static void vhost_vsock_handle_ctl_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_vsock *vsock = container_of(vq->dev, struct vhost_vsock,
						 dev);

	pr_debug("%s vq=%p, vsock=%p\n", __func__, vq, vsock);
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
	u32 len;

	mutex_lock(&vq->mutex);
	vhost_disable_notify(&vsock->dev, vq);
	for (;;) {
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

		/* Only accept correctly addressed packets */
		if (le32_to_cpu(pkt->hdr.src_cid) == vsock->guest_cid &&
		    le32_to_cpu(pkt->hdr.dst_cid) == vhost_transport_get_local_cid())
			virtio_transport_recv_pkt(pkt);
		else
			virtio_transport_free_pkt(pkt);

		vhost_add_used(vq, head, len);
		added = true;
	}
	if (added)
		vhost_signal(&vsock->dev, vq);
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

static int vhost_vsock_dev_open(struct inode *inode, struct file *file)
{
	struct vhost_virtqueue **vqs;
	struct vhost_vsock *vsock;
	int ret;

	vsock = kzalloc(sizeof(*vsock), GFP_KERNEL);
	if (!vsock)
		return -ENOMEM;

	pr_debug("%s:vsock=%p\n", __func__, vsock);

	vqs = kmalloc(VSOCK_VQ_MAX * sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		ret = -ENOMEM;
		goto out;
	}

	vqs[VSOCK_VQ_CTRL] = &vsock->vqs[VSOCK_VQ_CTRL].vq;
	vqs[VSOCK_VQ_TX] = &vsock->vqs[VSOCK_VQ_TX].vq;
	vqs[VSOCK_VQ_RX] = &vsock->vqs[VSOCK_VQ_RX].vq;
	vsock->vqs[VSOCK_VQ_CTRL].vq.handle_kick = vhost_vsock_handle_ctl_kick;
	vsock->vqs[VSOCK_VQ_TX].vq.handle_kick = vhost_vsock_handle_tx_kick;
	vsock->vqs[VSOCK_VQ_RX].vq.handle_kick = vhost_vsock_handle_rx_kick;

	vhost_dev_init(&vsock->dev, vqs, VSOCK_VQ_MAX);

	file->private_data = vsock;
	init_waitqueue_head(&vsock->queue_wait);
	INIT_LIST_HEAD(&vsock->send_pkt_list);
	vhost_work_init(&vsock->send_pkt_work, vhost_transport_send_pkt_work);

	mutex_lock(&vhost_vsock_mutex);
	list_add_tail(&vsock->list, &vhost_vsock_list);
	mutex_unlock(&vhost_vsock_mutex);
	return 0;

out:
	kfree(vsock);
	return ret;
}

static void vhost_vsock_flush(struct vhost_vsock *vsock)
{
	int i;

	for (i = 0; i < VSOCK_VQ_MAX; i++)
		vhost_poll_flush(&vsock->vqs[i].vq.poll);
	vhost_work_flush(&vsock->dev, &vsock->send_pkt_work);
}

static int vhost_vsock_dev_release(struct inode *inode, struct file *file)
{
	struct vhost_vsock *vsock = file->private_data;

	mutex_lock(&vhost_vsock_mutex);
	list_del(&vsock->list);
	mutex_unlock(&vhost_vsock_mutex);

	vhost_dev_stop(&vsock->dev);
	vhost_vsock_flush(vsock);
	vhost_dev_cleanup(&vsock->dev, false);
	kfree(vsock->dev.vqs);
	kfree(vsock);
	return 0;
}

static int vhost_vsock_set_cid(struct vhost_vsock *vsock, u32 guest_cid)
{
	struct vhost_vsock *other;

	/* Refuse reserved CIDs */
	if (guest_cid <= VMADDR_CID_HOST) {
		return -EINVAL;
	}

	/* Refuse if CID is already in use */
	other = vhost_vsock_get(guest_cid);
	if (other && other != vsock) {
		return -EADDRINUSE;
	}

	mutex_lock(&vhost_vsock_mutex);
	vsock->guest_cid = guest_cid;
	pr_debug("%s:guest_cid=%d\n", __func__, guest_cid);
	mutex_unlock(&vhost_vsock_mutex);

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

	for (i = 0; i < VSOCK_VQ_MAX; i++) {
		vq = &vsock->vqs[i].vq;
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
	u64 __user *featurep = argp;
	u32 __user *cidp = argp;
	u32 guest_cid;
	u64 features;
	int r;

	switch (ioctl) {
	case VHOST_VSOCK_SET_GUEST_CID:
		if (get_user(guest_cid, cidp))
			return -EFAULT;
		return vhost_vsock_set_cid(vsock, guest_cid);
	case VHOST_GET_FEATURES:
		features = VHOST_VSOCK_FEATURES;
		if (copy_to_user(featurep, &features, sizeof(features)))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, featurep, sizeof(features)))
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
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vhost-vsock",
	.fops = &vhost_vsock_fops,
};

static int
vhost_transport_socket_init(struct vsock_sock *vsk, struct vsock_sock *psk)
{
	struct virtio_transport *trans;
	int ret;

	ret = virtio_transport_do_socket_init(vsk, psk);
	if (ret)
		return ret;

	trans = vsk->trans;
	trans->ops = &vhost_ops;

	return ret;
}

static struct vsock_transport vhost_transport = {
	.get_local_cid            = vhost_transport_get_local_cid,

	.init                     = vhost_transport_socket_init,
	.destruct                 = virtio_transport_destruct,
	.release                  = virtio_transport_release,
	.connect                  = virtio_transport_connect,
	.shutdown                 = virtio_transport_shutdown,

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
};

static int __init vhost_vsock_init(void)
{
	int ret;

	ret = vsock_core_init(&vhost_transport);
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
