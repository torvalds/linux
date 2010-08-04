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
#include <linux/mmu_context.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/rcupdate.h>
#include <linux/file.h>
#include <linux/slab.h>

#include <linux/net.h>
#include <linux/if_packet.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/if_macvlan.h>

#include <net/sock.h>

#include "vhost.h"

/* Max number of bytes transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others. */
#define VHOST_NET_WEIGHT 0x80000

enum {
	VHOST_NET_VQ_RX = 0,
	VHOST_NET_VQ_TX = 1,
	VHOST_NET_VQ_MAX = 2,
};

enum vhost_net_poll_state {
	VHOST_NET_POLL_DISABLED = 0,
	VHOST_NET_POLL_STARTED = 1,
	VHOST_NET_POLL_STOPPED = 2,
};

struct vhost_net {
	struct vhost_dev dev;
	struct vhost_virtqueue vqs[VHOST_NET_VQ_MAX];
	struct vhost_poll poll[VHOST_NET_VQ_MAX];
	/* Tells us whether we are polling a socket for TX.
	 * We only do this when socket buffer fills up.
	 * Protected by tx vq lock. */
	enum vhost_net_poll_state tx_poll_state;
};

/* Pop first len bytes from iovec. Return number of segments used. */
static int move_iovec_hdr(struct iovec *from, struct iovec *to,
			  size_t len, int iov_count)
{
	int seg = 0;
	size_t size;
	while (len && seg < iov_count) {
		size = min(from->iov_len, len);
		to->iov_base = from->iov_base;
		to->iov_len = size;
		from->iov_len -= size;
		from->iov_base += size;
		len -= size;
		++from;
		++to;
		++seg;
	}
	return seg;
}

/* Caller must have TX VQ lock */
static void tx_poll_stop(struct vhost_net *net)
{
	if (likely(net->tx_poll_state != VHOST_NET_POLL_STARTED))
		return;
	vhost_poll_stop(net->poll + VHOST_NET_VQ_TX);
	net->tx_poll_state = VHOST_NET_POLL_STOPPED;
}

/* Caller must have TX VQ lock */
static void tx_poll_start(struct vhost_net *net, struct socket *sock)
{
	if (unlikely(net->tx_poll_state != VHOST_NET_POLL_STOPPED))
		return;
	vhost_poll_start(net->poll + VHOST_NET_VQ_TX, sock->file);
	net->tx_poll_state = VHOST_NET_POLL_STARTED;
}

/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_tx(struct vhost_net *net)
{
	struct vhost_virtqueue *vq = &net->dev.vqs[VHOST_NET_VQ_TX];
	unsigned out, in, s;
	int head;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_iov = vq->iov,
		.msg_flags = MSG_DONTWAIT,
	};
	size_t len, total_len = 0;
	int err, wmem;
	size_t hdr_size;
	struct socket *sock = rcu_dereference(vq->private_data);
	if (!sock)
		return;

	wmem = atomic_read(&sock->sk->sk_wmem_alloc);
	if (wmem >= sock->sk->sk_sndbuf) {
		mutex_lock(&vq->mutex);
		tx_poll_start(net, sock);
		mutex_unlock(&vq->mutex);
		return;
	}

	use_mm(net->dev.mm);
	mutex_lock(&vq->mutex);
	vhost_disable_notify(vq);

	if (wmem < sock->sk->sk_sndbuf / 2)
		tx_poll_stop(net);
	hdr_size = vq->hdr_size;

	for (;;) {
		head = vhost_get_vq_desc(&net->dev, vq, vq->iov,
					 ARRAY_SIZE(vq->iov),
					 &out, &in,
					 NULL, NULL);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			wmem = atomic_read(&sock->sk->sk_wmem_alloc);
			if (wmem >= sock->sk->sk_sndbuf * 3 / 4) {
				tx_poll_start(net, sock);
				set_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
				break;
			}
			if (unlikely(vhost_enable_notify(vq))) {
				vhost_disable_notify(vq);
				continue;
			}
			break;
		}
		if (in) {
			vq_err(vq, "Unexpected descriptor format for TX: "
			       "out %d, int %d\n", out, in);
			break;
		}
		/* Skip header. TODO: support TSO. */
		s = move_iovec_hdr(vq->iov, vq->hdr, hdr_size, out);
		msg.msg_iovlen = out;
		len = iov_length(vq->iov, out);
		/* Sanity check */
		if (!len) {
			vq_err(vq, "Unexpected header len for TX: "
			       "%zd expected %zd\n",
			       iov_length(vq->hdr, s), hdr_size);
			break;
		}
		/* TODO: Check specific error and bomb out unless ENOBUFS? */
		err = sock->ops->sendmsg(NULL, sock, &msg, len);
		if (unlikely(err < 0)) {
			vhost_discard_vq_desc(vq);
			tx_poll_start(net, sock);
			break;
		}
		if (err != len)
			pr_debug("Truncated TX packet: "
				 " len %d != %zd\n", err, len);
		vhost_add_used_and_signal(&net->dev, vq, head, 0);
		total_len += len;
		if (unlikely(total_len >= VHOST_NET_WEIGHT)) {
			vhost_poll_queue(&vq->poll);
			break;
		}
	}

	mutex_unlock(&vq->mutex);
	unuse_mm(net->dev.mm);
}

/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_rx(struct vhost_net *net)
{
	struct vhost_virtqueue *vq = &net->dev.vqs[VHOST_NET_VQ_RX];
	unsigned out, in, log, s;
	int head;
	struct vhost_log *vq_log;
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_control = NULL, /* FIXME: get and handle RX aux data. */
		.msg_controllen = 0,
		.msg_iov = vq->iov,
		.msg_flags = MSG_DONTWAIT,
	};

	struct virtio_net_hdr hdr = {
		.flags = 0,
		.gso_type = VIRTIO_NET_HDR_GSO_NONE
	};

	size_t len, total_len = 0;
	int err;
	size_t hdr_size;
	struct socket *sock = rcu_dereference(vq->private_data);
	if (!sock || skb_queue_empty(&sock->sk->sk_receive_queue))
		return;

	use_mm(net->dev.mm);
	mutex_lock(&vq->mutex);
	vhost_disable_notify(vq);
	hdr_size = vq->hdr_size;

	vq_log = unlikely(vhost_has_feature(&net->dev, VHOST_F_LOG_ALL)) ?
		vq->log : NULL;

	for (;;) {
		head = vhost_get_vq_desc(&net->dev, vq, vq->iov,
					 ARRAY_SIZE(vq->iov),
					 &out, &in,
					 vq_log, &log);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* OK, now we need to know about added descriptors. */
		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(vq))) {
				/* They have slipped one in as we were
				 * doing that: check again. */
				vhost_disable_notify(vq);
				continue;
			}
			/* Nothing new?  Wait for eventfd to tell us
			 * they refilled. */
			break;
		}
		/* We don't need to be notified again. */
		if (out) {
			vq_err(vq, "Unexpected descriptor format for RX: "
			       "out %d, int %d\n",
			       out, in);
			break;
		}
		/* Skip header. TODO: support TSO/mergeable rx buffers. */
		s = move_iovec_hdr(vq->iov, vq->hdr, hdr_size, in);
		msg.msg_iovlen = in;
		len = iov_length(vq->iov, in);
		/* Sanity check */
		if (!len) {
			vq_err(vq, "Unexpected header len for RX: "
			       "%zd expected %zd\n",
			       iov_length(vq->hdr, s), hdr_size);
			break;
		}
		err = sock->ops->recvmsg(NULL, sock, &msg,
					 len, MSG_DONTWAIT | MSG_TRUNC);
		/* TODO: Check specific error and bomb out unless EAGAIN? */
		if (err < 0) {
			vhost_discard_vq_desc(vq);
			break;
		}
		/* TODO: Should check and handle checksum. */
		if (err > len) {
			pr_debug("Discarded truncated rx packet: "
				 " len %d > %zd\n", err, len);
			vhost_discard_vq_desc(vq);
			continue;
		}
		len = err;
		err = memcpy_toiovec(vq->hdr, (unsigned char *)&hdr, hdr_size);
		if (err) {
			vq_err(vq, "Unable to write vnet_hdr at addr %p: %d\n",
			       vq->iov->iov_base, err);
			break;
		}
		len += hdr_size;
		vhost_add_used_and_signal(&net->dev, vq, head, len);
		if (unlikely(vq_log))
			vhost_log_write(vq, vq_log, log, len);
		total_len += len;
		if (unlikely(total_len >= VHOST_NET_WEIGHT)) {
			vhost_poll_queue(&vq->poll);
			break;
		}
	}

	mutex_unlock(&vq->mutex);
	unuse_mm(net->dev.mm);
}

static void handle_tx_kick(struct work_struct *work)
{
	struct vhost_virtqueue *vq;
	struct vhost_net *net;
	vq = container_of(work, struct vhost_virtqueue, poll.work);
	net = container_of(vq->dev, struct vhost_net, dev);
	handle_tx(net);
}

static void handle_rx_kick(struct work_struct *work)
{
	struct vhost_virtqueue *vq;
	struct vhost_net *net;
	vq = container_of(work, struct vhost_virtqueue, poll.work);
	net = container_of(vq->dev, struct vhost_net, dev);
	handle_rx(net);
}

static void handle_tx_net(struct work_struct *work)
{
	struct vhost_net *net;
	net = container_of(work, struct vhost_net, poll[VHOST_NET_VQ_TX].work);
	handle_tx(net);
}

static void handle_rx_net(struct work_struct *work)
{
	struct vhost_net *net;
	net = container_of(work, struct vhost_net, poll[VHOST_NET_VQ_RX].work);
	handle_rx(net);
}

static int vhost_net_open(struct inode *inode, struct file *f)
{
	struct vhost_net *n = kmalloc(sizeof *n, GFP_KERNEL);
	int r;
	if (!n)
		return -ENOMEM;
	n->vqs[VHOST_NET_VQ_TX].handle_kick = handle_tx_kick;
	n->vqs[VHOST_NET_VQ_RX].handle_kick = handle_rx_kick;
	r = vhost_dev_init(&n->dev, n->vqs, VHOST_NET_VQ_MAX);
	if (r < 0) {
		kfree(n);
		return r;
	}

	vhost_poll_init(n->poll + VHOST_NET_VQ_TX, handle_tx_net, POLLOUT);
	vhost_poll_init(n->poll + VHOST_NET_VQ_RX, handle_rx_net, POLLIN);
	n->tx_poll_state = VHOST_NET_POLL_DISABLED;

	f->private_data = n;

	return 0;
}

static void vhost_net_disable_vq(struct vhost_net *n,
				 struct vhost_virtqueue *vq)
{
	if (!vq->private_data)
		return;
	if (vq == n->vqs + VHOST_NET_VQ_TX) {
		tx_poll_stop(n);
		n->tx_poll_state = VHOST_NET_POLL_DISABLED;
	} else
		vhost_poll_stop(n->poll + VHOST_NET_VQ_RX);
}

static void vhost_net_enable_vq(struct vhost_net *n,
				struct vhost_virtqueue *vq)
{
	struct socket *sock = vq->private_data;
	if (!sock)
		return;
	if (vq == n->vqs + VHOST_NET_VQ_TX) {
		n->tx_poll_state = VHOST_NET_POLL_STOPPED;
		tx_poll_start(n, sock);
	} else
		vhost_poll_start(n->poll + VHOST_NET_VQ_RX, sock->file);
}

static struct socket *vhost_net_stop_vq(struct vhost_net *n,
					struct vhost_virtqueue *vq)
{
	struct socket *sock;

	mutex_lock(&vq->mutex);
	sock = vq->private_data;
	vhost_net_disable_vq(n, vq);
	rcu_assign_pointer(vq->private_data, NULL);
	mutex_unlock(&vq->mutex);
	return sock;
}

static void vhost_net_stop(struct vhost_net *n, struct socket **tx_sock,
			   struct socket **rx_sock)
{
	*tx_sock = vhost_net_stop_vq(n, n->vqs + VHOST_NET_VQ_TX);
	*rx_sock = vhost_net_stop_vq(n, n->vqs + VHOST_NET_VQ_RX);
}

static void vhost_net_flush_vq(struct vhost_net *n, int index)
{
	vhost_poll_flush(n->poll + index);
	vhost_poll_flush(&n->dev.vqs[index].poll);
}

static void vhost_net_flush(struct vhost_net *n)
{
	vhost_net_flush_vq(n, VHOST_NET_VQ_TX);
	vhost_net_flush_vq(n, VHOST_NET_VQ_RX);
}

static int vhost_net_release(struct inode *inode, struct file *f)
{
	struct vhost_net *n = f->private_data;
	struct socket *tx_sock;
	struct socket *rx_sock;

	vhost_net_stop(n, &tx_sock, &rx_sock);
	vhost_net_flush(n);
	vhost_dev_cleanup(&n->dev);
	if (tx_sock)
		fput(tx_sock->file);
	if (rx_sock)
		fput(rx_sock->file);
	/* We do an extra flush before freeing memory,
	 * since jobs can re-queue themselves. */
	vhost_net_flush(n);
	kfree(n);
	return 0;
}

static struct socket *get_raw_socket(int fd)
{
	struct {
		struct sockaddr_ll sa;
		char  buf[MAX_ADDR_LEN];
	} uaddr;
	int uaddr_len = sizeof uaddr, r;
	struct socket *sock = sockfd_lookup(fd, &r);
	if (!sock)
		return ERR_PTR(-ENOTSOCK);

	/* Parameter checking */
	if (sock->sk->sk_type != SOCK_RAW) {
		r = -ESOCKTNOSUPPORT;
		goto err;
	}

	r = sock->ops->getname(sock, (struct sockaddr *)&uaddr.sa,
			       &uaddr_len, 0);
	if (r)
		goto err;

	if (uaddr.sa.sll_family != AF_PACKET) {
		r = -EPFNOSUPPORT;
		goto err;
	}
	return sock;
err:
	fput(sock->file);
	return ERR_PTR(r);
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
	sock = macvtap_get_socket(file);
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
	int r;

	mutex_lock(&n->dev.mutex);
	r = vhost_dev_check_owner(&n->dev);
	if (r)
		goto err;

	if (index >= VHOST_NET_VQ_MAX) {
		r = -ENOBUFS;
		goto err;
	}
	vq = n->vqs + index;
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
	if (sock == oldsock)
		goto done;

	vhost_net_disable_vq(n, vq);
	rcu_assign_pointer(vq->private_data, sock);
	vhost_net_enable_vq(n, vq);
done:
	mutex_unlock(&vq->mutex);

	if (oldsock) {
		vhost_net_flush_vq(n, index);
		fput(oldsock->file);
	}

	mutex_unlock(&n->dev.mutex);
	return 0;

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
	mutex_lock(&n->dev.mutex);
	err = vhost_dev_check_owner(&n->dev);
	if (err)
		goto done;
	vhost_net_stop(n, &tx_sock, &rx_sock);
	vhost_net_flush(n);
	err = vhost_dev_reset_owner(&n->dev);
done:
	mutex_unlock(&n->dev.mutex);
	if (tx_sock)
		fput(tx_sock->file);
	if (rx_sock)
		fput(rx_sock->file);
	return err;
}

static int vhost_net_set_features(struct vhost_net *n, u64 features)
{
	size_t hdr_size = features & (1 << VHOST_NET_F_VIRTIO_NET_HDR) ?
		sizeof(struct virtio_net_hdr) : 0;
	int i;
	mutex_lock(&n->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&n->dev)) {
		mutex_unlock(&n->dev.mutex);
		return -EFAULT;
	}
	n->dev.acked_features = features;
	smp_wmb();
	for (i = 0; i < VHOST_NET_VQ_MAX; ++i) {
		mutex_lock(&n->vqs[i].mutex);
		n->vqs[i].hdr_size = hdr_size;
		mutex_unlock(&n->vqs[i].mutex);
	}
	vhost_net_flush(n);
	mutex_unlock(&n->dev.mutex);
	return 0;
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
		features = VHOST_FEATURES;
		if (copy_to_user(featurep, &features, sizeof features))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, featurep, sizeof features))
			return -EFAULT;
		if (features & ~VHOST_FEATURES)
			return -EOPNOTSUPP;
		return vhost_net_set_features(n, features);
	case VHOST_RESET_OWNER:
		return vhost_net_reset_owner(n);
	default:
		mutex_lock(&n->dev.mutex);
		r = vhost_dev_ioctl(&n->dev, ioctl, arg);
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

const static struct file_operations vhost_net_fops = {
	.owner          = THIS_MODULE,
	.release        = vhost_net_release,
	.unlocked_ioctl = vhost_net_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = vhost_net_compat_ioctl,
#endif
	.open           = vhost_net_open,
};

static struct miscdevice vhost_net_misc = {
	MISC_DYNAMIC_MINOR,
	"vhost-net",
	&vhost_net_fops,
};

static int vhost_net_init(void)
{
	int r = vhost_init();
	if (r)
		goto err_init;
	r = misc_register(&vhost_net_misc);
	if (r)
		goto err_reg;
	return 0;
err_reg:
	vhost_cleanup();
err_init:
	return r;

}
module_init(vhost_net_init);

static void vhost_net_exit(void)
{
	misc_deregister(&vhost_net_misc);
	vhost_cleanup();
}
module_exit(vhost_net_exit);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael S. Tsirkin");
MODULE_DESCRIPTION("Host kernel accelerator for virtio net");
