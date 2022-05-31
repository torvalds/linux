// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2009 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * test virtio server in host kernel.
 */

#include <linux/compat.h>
#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/file.h>
#include <linux/slab.h>

#include "test.h"
#include "vhost.h"

/* Max number of bytes transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others. */
#define VHOST_TEST_WEIGHT 0x80000

/* Max number of packets transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others with
 * pkts.
 */
#define VHOST_TEST_PKT_WEIGHT 256

enum {
	VHOST_TEST_VQ = 0,
	VHOST_TEST_VQ_MAX = 1,
};

struct vhost_test {
	struct vhost_dev dev;
	struct vhost_virtqueue vqs[VHOST_TEST_VQ_MAX];
};

/* Expects to be always run from workqueue - which acts as
 * read-size critical section for our kind of RCU. */
static void handle_vq(struct vhost_test *n)
{
	struct vhost_virtqueue *vq = &n->vqs[VHOST_TEST_VQ];
	unsigned out, in;
	int head;
	size_t len, total_len = 0;
	void *private;

	mutex_lock(&vq->mutex);
	private = vhost_vq_get_backend(vq);
	if (!private) {
		mutex_unlock(&vq->mutex);
		return;
	}

	vhost_disable_notify(&n->dev, vq);

	for (;;) {
		head = vhost_get_vq_desc(vq, vq->iov,
					 ARRAY_SIZE(vq->iov),
					 &out, &in,
					 NULL, NULL);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&n->dev, vq))) {
				vhost_disable_notify(&n->dev, vq);
				continue;
			}
			break;
		}
		if (in) {
			vq_err(vq, "Unexpected descriptor format for TX: "
			       "out %d, int %d\n", out, in);
			break;
		}
		len = iov_length(vq->iov, out);
		/* Sanity check */
		if (!len) {
			vq_err(vq, "Unexpected 0 len for TX\n");
			break;
		}
		vhost_add_used_and_signal(&n->dev, vq, head, 0);
		total_len += len;
		if (unlikely(vhost_exceeds_weight(vq, 0, total_len)))
			break;
	}

	mutex_unlock(&vq->mutex);
}

static void handle_vq_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_test *n = container_of(vq->dev, struct vhost_test, dev);

	handle_vq(n);
}

static int vhost_test_open(struct inode *inode, struct file *f)
{
	struct vhost_test *n = kmalloc(sizeof *n, GFP_KERNEL);
	struct vhost_dev *dev;
	struct vhost_virtqueue **vqs;

	if (!n)
		return -ENOMEM;
	vqs = kmalloc_array(VHOST_TEST_VQ_MAX, sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		kfree(n);
		return -ENOMEM;
	}

	dev = &n->dev;
	vqs[VHOST_TEST_VQ] = &n->vqs[VHOST_TEST_VQ];
	n->vqs[VHOST_TEST_VQ].handle_kick = handle_vq_kick;
	vhost_dev_init(dev, vqs, VHOST_TEST_VQ_MAX, UIO_MAXIOV,
		       VHOST_TEST_PKT_WEIGHT, VHOST_TEST_WEIGHT, true, NULL);

	f->private_data = n;

	return 0;
}

static void *vhost_test_stop_vq(struct vhost_test *n,
				struct vhost_virtqueue *vq)
{
	void *private;

	mutex_lock(&vq->mutex);
	private = vhost_vq_get_backend(vq);
	vhost_vq_set_backend(vq, NULL);
	mutex_unlock(&vq->mutex);
	return private;
}

static void vhost_test_stop(struct vhost_test *n, void **privatep)
{
	*privatep = vhost_test_stop_vq(n, n->vqs + VHOST_TEST_VQ);
}

static void vhost_test_flush_vq(struct vhost_test *n, int index)
{
	vhost_poll_flush(&n->vqs[index].poll);
}

static void vhost_test_flush(struct vhost_test *n)
{
	vhost_test_flush_vq(n, VHOST_TEST_VQ);
}

static int vhost_test_release(struct inode *inode, struct file *f)
{
	struct vhost_test *n = f->private_data;
	void  *private;

	vhost_test_stop(n, &private);
	vhost_test_flush(n);
	vhost_dev_stop(&n->dev);
	vhost_dev_cleanup(&n->dev);
	/* We do an extra flush before freeing memory,
	 * since jobs can re-queue themselves. */
	vhost_test_flush(n);
	kfree(n->dev.vqs);
	kfree(n);
	return 0;
}

static long vhost_test_run(struct vhost_test *n, int test)
{
	void *priv, *oldpriv;
	struct vhost_virtqueue *vq;
	int r, index;

	if (test < 0 || test > 1)
		return -EINVAL;

	mutex_lock(&n->dev.mutex);
	r = vhost_dev_check_owner(&n->dev);
	if (r)
		goto err;

	for (index = 0; index < n->dev.nvqs; ++index) {
		/* Verify that ring has been setup correctly. */
		if (!vhost_vq_access_ok(&n->vqs[index])) {
			r = -EFAULT;
			goto err;
		}
	}

	for (index = 0; index < n->dev.nvqs; ++index) {
		vq = n->vqs + index;
		mutex_lock(&vq->mutex);
		priv = test ? n : NULL;

		/* start polling new socket */
		oldpriv = vhost_vq_get_backend(vq);
		vhost_vq_set_backend(vq, priv);

		r = vhost_vq_init_access(&n->vqs[index]);

		mutex_unlock(&vq->mutex);

		if (r)
			goto err;

		if (oldpriv) {
			vhost_test_flush_vq(n, index);
		}
	}

	mutex_unlock(&n->dev.mutex);
	return 0;

err:
	mutex_unlock(&n->dev.mutex);
	return r;
}

static long vhost_test_reset_owner(struct vhost_test *n)
{
	void *priv = NULL;
	long err;
	struct vhost_iotlb *umem;

	mutex_lock(&n->dev.mutex);
	err = vhost_dev_check_owner(&n->dev);
	if (err)
		goto done;
	umem = vhost_dev_reset_owner_prepare();
	if (!umem) {
		err = -ENOMEM;
		goto done;
	}
	vhost_test_stop(n, &priv);
	vhost_test_flush(n);
	vhost_dev_stop(&n->dev);
	vhost_dev_reset_owner(&n->dev, umem);
done:
	mutex_unlock(&n->dev.mutex);
	return err;
}

static int vhost_test_set_features(struct vhost_test *n, u64 features)
{
	struct vhost_virtqueue *vq;

	mutex_lock(&n->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&n->dev)) {
		mutex_unlock(&n->dev.mutex);
		return -EFAULT;
	}
	vq = &n->vqs[VHOST_TEST_VQ];
	mutex_lock(&vq->mutex);
	vq->acked_features = features;
	mutex_unlock(&vq->mutex);
	mutex_unlock(&n->dev.mutex);
	return 0;
}

static long vhost_test_set_backend(struct vhost_test *n, unsigned index, int fd)
{
	static void *backend;

	const bool enable = fd != -1;
	struct vhost_virtqueue *vq;
	int r;

	mutex_lock(&n->dev.mutex);
	r = vhost_dev_check_owner(&n->dev);
	if (r)
		goto err;

	if (index >= VHOST_TEST_VQ_MAX) {
		r = -ENOBUFS;
		goto err;
	}
	vq = &n->vqs[index];
	mutex_lock(&vq->mutex);

	/* Verify that ring has been setup correctly. */
	if (!vhost_vq_access_ok(vq)) {
		r = -EFAULT;
		goto err_vq;
	}
	if (!enable) {
		vhost_poll_stop(&vq->poll);
		backend = vhost_vq_get_backend(vq);
		vhost_vq_set_backend(vq, NULL);
	} else {
		vhost_vq_set_backend(vq, backend);
		r = vhost_vq_init_access(vq);
		if (r == 0)
			r = vhost_poll_start(&vq->poll, vq->kick);
	}

	mutex_unlock(&vq->mutex);

	if (enable) {
		vhost_test_flush_vq(n, index);
	}

	mutex_unlock(&n->dev.mutex);
	return 0;

err_vq:
	mutex_unlock(&vq->mutex);
err:
	mutex_unlock(&n->dev.mutex);
	return r;
}

static long vhost_test_ioctl(struct file *f, unsigned int ioctl,
			     unsigned long arg)
{
	struct vhost_vring_file backend;
	struct vhost_test *n = f->private_data;
	void __user *argp = (void __user *)arg;
	u64 __user *featurep = argp;
	int test;
	u64 features;
	int r;
	switch (ioctl) {
	case VHOST_TEST_RUN:
		if (copy_from_user(&test, argp, sizeof test))
			return -EFAULT;
		return vhost_test_run(n, test);
	case VHOST_TEST_SET_BACKEND:
		if (copy_from_user(&backend, argp, sizeof backend))
			return -EFAULT;
		return vhost_test_set_backend(n, backend.index, backend.fd);
	case VHOST_GET_FEATURES:
		features = VHOST_FEATURES;
		if (copy_to_user(featurep, &features, sizeof features))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		printk(KERN_ERR "1\n");
		if (copy_from_user(&features, featurep, sizeof features))
			return -EFAULT;
		printk(KERN_ERR "2\n");
		if (features & ~VHOST_FEATURES)
			return -EOPNOTSUPP;
		printk(KERN_ERR "3\n");
		return vhost_test_set_features(n, features);
	case VHOST_RESET_OWNER:
		return vhost_test_reset_owner(n);
	default:
		mutex_lock(&n->dev.mutex);
		r = vhost_dev_ioctl(&n->dev, ioctl, argp);
                if (r == -ENOIOCTLCMD)
                        r = vhost_vring_ioctl(&n->dev, ioctl, argp);
		vhost_test_flush(n);
		mutex_unlock(&n->dev.mutex);
		return r;
	}
}

static const struct file_operations vhost_test_fops = {
	.owner          = THIS_MODULE,
	.release        = vhost_test_release,
	.unlocked_ioctl = vhost_test_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
	.open           = vhost_test_open,
	.llseek		= noop_llseek,
};

static struct miscdevice vhost_test_misc = {
	MISC_DYNAMIC_MINOR,
	"vhost-test",
	&vhost_test_fops,
};
module_misc_device(vhost_test_misc);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael S. Tsirkin");
MODULE_DESCRIPTION("Host kernel side for virtio simulator");
