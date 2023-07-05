// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/platform_device.h>
#include <linux/ipc_logging.h>
#include <linux/refcount.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/rpmsg.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/termios.h>
#include <linux/string.h>

#include "qcom_glink_native.h"

/* Define IPC Logging Macros */
#define GLINK_PKT_IPC_LOG_PAGE_CNT 2
static void *glink_pkt_ilctxt;

#define GLINK_PKT_INFO(x, ...)						     \
	ipc_log_string(glink_pkt_ilctxt, "[%s]: "x, __func__, ##__VA_ARGS__)

#define GLINK_PKT_ERR(x, ...)						     \
do {									     \
	printk_ratelimited("%s[%s]: " x, KERN_ERR, __func__, ##__VA_ARGS__); \
	ipc_log_string(glink_pkt_ilctxt, "%s[%s]: " x, "", __func__,         \
			##__VA_ARGS__);\
} while (0)

#define GLINK_PKT_IOCTL_MAGIC (0xC3)

#define GLINK_PKT_IOCTL_QUEUE_RX_INTENT \
	_IOW(GLINK_PKT_IOCTL_MAGIC, 0, unsigned int)

struct glink_pkt_zerocopy_receive {
	__u64 address;      /* in: address of mapping */
	__u32 length;       /* out: number of bytes to map/mapped */
	__u32 offset;   /* out: amount of bytes to skip */
};

#define GLINK_PKT_IOCTL_ZC_RECV \
	_IOWR(GLINK_PKT_IOCTL_MAGIC, 1, struct glink_pkt_zerocopy_receive)

#define GLINK_PKT_IOCTL_ZC_DONE \
	_IOWR(GLINK_PKT_IOCTL_MAGIC, 2, struct glink_pkt_zerocopy_receive)

#define MODULE_NAME "glink_pkt"
static dev_t glink_pkt_major;
static struct class *glink_pkt_class;
static int num_glink_pkt_devs;

static DEFINE_IDA(glink_pkt_minor_ida);

/**
 * struct glink_pkt - driver context, relates rpdev to cdev
 * @dev:	glink pkt device
 * @cdev:	cdev for the glink pkt device
 * @lock:	synchronization of @rpdev and @open_tout modifications
 * @ch_open:	wait object for opening the glink channel
 * @refcount:	count how many userspace clients have handles
 * @rpdev:	underlaying rpmsg device
 * @rx_done:	cache whether rpdev can support external rx done
 * @queue_lock:	synchronization of @queue operations
 * @queue:	incoming message queue
 * @readq:	wait object for incoming queue
 * @sig_change:	flag to indicate serial signal change
 * @fragmented_read: set from dt node for partial read
 * @enable_ch_close: set from dt node for unregister driver on close syscall
 * @drv_lock:	lock to protect rpmsg driver variable
 * @drv:	rpmsg driver for registering to rpmsg bus
 * @drv_registered: status of rpmsg driver
 * @dev_name:	/dev/@dev_name for glink_pkt device
 * @ch_name:	glink channel to match to
 * @edge:	glink edge to match to
 * @open_tout:	timeout for open syscall, configurable in sysfs
 * @rskb_read_lock: Lock to protect rskb during read syscalls
 * @rskb:       current skb being read
 * @rdata:      data pointer in current skb
 * @rdata_len:  remaining data to be read from skb
 */
struct glink_pkt_device {
	struct device dev;
	struct cdev cdev;

	struct mutex lock;
	struct completion ch_open;
	refcount_t refcount;
	struct rpmsg_device *rpdev;
	bool rx_done;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	struct sk_buff_head pending;
	wait_queue_head_t readq;
	int sig_change;
	bool fragmented_read;
	bool enable_ch_close;

	struct mutex drv_lock;
	struct rpmsg_driver drv;
	bool drv_registered;

	const char *dev_name;
	const char *ch_name;
	const char *edge;
	int open_tout;

	struct mutex rskb_read_lock;
	struct sk_buff *rskb;
	unsigned char *rdata;
	size_t rdata_len;
};

#define dev_to_gpdev(_dev) container_of(_dev, struct glink_pkt_device, dev)
#define cdev_to_gpdev(_cdev) container_of(_cdev, struct glink_pkt_device, cdev)
#define drv_to_rpdrv(_drv) container_of(_drv, struct rpmsg_driver, drv)
#define rpdrv_to_gpdev(_rdrv) container_of(_rdrv, struct glink_pkt_device, drv)

static ssize_t open_timeout_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t n)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	long tmp;

	mutex_lock(&gpdev->lock);
	if (kstrtol(buf, 0, &tmp)) {
		mutex_unlock(&gpdev->lock);
		GLINK_PKT_ERR("unable to convert string to int for /dev/%s\n",
			      gpdev->dev_name);
		return -EINVAL;
	}
	gpdev->open_tout = tmp;
	mutex_unlock(&gpdev->lock);

	return n;
}

static ssize_t open_timeout_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	ssize_t ret;

	mutex_lock(&gpdev->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", gpdev->open_tout);
	mutex_unlock(&gpdev->lock);

	return ret;
}

static DEVICE_ATTR_RW(open_timeout);

static void glink_pkt_kfree_skb(struct glink_pkt_device *gpdev, struct sk_buff *skb)
{
	if (gpdev->rx_done) {
		qcom_glink_rx_done(gpdev->rpdev->ept, skb->data);
		/*
		 * Data memory is freed by qcom_glink_rx_done(), reset the
		 * skb data pointers so kfree_skb() does not try to free
		 * a second time.
		 */
		skb->head = NULL;
		skb->data = NULL;
	}
	kfree_skb(skb);
}

static void glink_pkt_clear_queues(struct glink_pkt_device *gpdev)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (gpdev->rskb) {
		glink_pkt_kfree_skb(gpdev, gpdev->rskb);
		gpdev->rskb = NULL;
		gpdev->rdata = NULL;
		gpdev->rdata_len = 0;
	}

	while ((skb = skb_dequeue(&gpdev->queue)))
		glink_pkt_kfree_skb(gpdev, skb);
	while ((skb = skb_dequeue(&gpdev->pending)))
		glink_pkt_kfree_skb(gpdev, skb);

	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
}

static int glink_pkt_rpdev_no_copy_cb(struct rpmsg_device *rpdev, void *buf,
				      int len, void *priv, u32 addr)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;
	struct sk_buff *skb;

	skb = alloc_skb(0, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb->head = buf;
	skb->data = buf;
	skb_reset_tail_pointer(skb);
	skb_set_end_offset(skb, len);
	skb_put(skb, len);

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb_queue_tail(&gpdev->queue, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&gpdev->readq);

	return RPMSG_DEFER;
}

static int glink_pkt_rpdev_copy_cb(struct rpmsg_device *rpdev, void *buf,
				   int len, void *priv, u32 addr)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;
	struct sk_buff *skb;

	if (!gpdev) {
		GLINK_PKT_ERR("channel is in reset\n");
		return -ENETRESET;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, len);

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb_queue_tail(&gpdev->queue, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&gpdev->readq);

	return 0;
}

static int glink_pkt_rpdev_cb(struct rpmsg_device *rpdev, void *buf, int len,
			      void *priv, u32 addr)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	rpmsg_rx_cb_t cb = (gpdev->rx_done) ? glink_pkt_rpdev_no_copy_cb : glink_pkt_rpdev_copy_cb;

	return cb(rpdev, buf, len, priv, addr);
}

static int glink_pkt_rpdev_sigs(struct rpmsg_device *rpdev, void *priv,
				u32 old, u32 new)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);
	unsigned long flags;

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	gpdev->sig_change = true;
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&gpdev->readq);

	return 0;
}

static int glink_pkt_rpdev_probe(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	mutex_lock(&gpdev->lock);
	gpdev->rpdev = rpdev;
	gpdev->rx_done = (qcom_glink_rx_done_supported(rpdev->ept) > 0) ? true : false;
	qcom_glink_register_signals_cb(rpdev->ept, glink_pkt_rpdev_sigs);
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, gpdev);
	complete_all(&gpdev->ch_open);

	return 0;
}

static void glink_pkt_rpdev_remove(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	mutex_lock(&gpdev->lock);
	glink_pkt_clear_queues(gpdev);
	gpdev->rpdev = NULL;
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, NULL);

	/* wake up any blocked readers */
	reinit_completion(&gpdev->ch_open);
	wake_up_interruptible(&gpdev->readq);
}

static int glink_pkt_drv_try_register(struct glink_pkt_device *gpdev)
{
	int ret = 0;

	mutex_lock(&gpdev->drv_lock);
	if (!gpdev->drv_registered) {
		ret = register_rpmsg_driver(&gpdev->drv);
		if (!ret)
			gpdev->drv_registered = true;
	}
	mutex_unlock(&gpdev->drv_lock);

	return ret;
}

static void glink_pkt_drv_try_unregister(struct glink_pkt_device *gpdev)
{
	mutex_lock(&gpdev->drv_lock);
	if (gpdev->drv_registered) {
		unregister_rpmsg_driver(&gpdev->drv);
		gpdev->drv_registered = false;
	}
	mutex_unlock(&gpdev->drv_lock);
}

/**
 * glink_pkt_open() - open() syscall for the glink_pkt device
 * inode:	Pointer to the inode structure.
 * file:	Pointer to the file structure.
 *
 * This function is used to open the glink pkt device when
 * userspace client do a open() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static int glink_pkt_open(struct inode *inode, struct file *file)
{
	struct glink_pkt_device *gpdev = cdev_to_gpdev(inode->i_cdev);
	int tout = msecs_to_jiffies(gpdev->open_tout * 1000);
	struct device *dev = &gpdev->dev;
	int ret;

	refcount_inc(&gpdev->refcount);
	get_device(dev);

	GLINK_PKT_INFO("begin for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	if (gpdev->enable_ch_close)
		glink_pkt_drv_try_register(gpdev);

	ret = wait_for_completion_interruptible_timeout(&gpdev->ch_open, tout);
	if (ret <= 0) {
		if (gpdev->enable_ch_close)
			glink_pkt_drv_try_unregister(gpdev);

		refcount_dec(&gpdev->refcount);
		put_device(dev);
		GLINK_PKT_INFO("timeout for %s by %s:%d\n", gpdev->ch_name,
			       current->comm, task_pid_nr(current));
		return -ETIMEDOUT;
	}
	file->private_data = gpdev;

	GLINK_PKT_INFO("end for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	return 0;
}

/**
 * glink_pkt_release() - release operation on glink_pkt device
 * inode:	Pointer to the inode structure.
 * file:	Pointer to the file structure.
 *
 * This function is used to release the glink pkt device when
 * userspace client do a close() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static int glink_pkt_release(struct inode *inode, struct file *file)
{
	struct glink_pkt_device *gpdev = cdev_to_gpdev(inode->i_cdev);
	struct device *dev = &gpdev->dev;
	unsigned long flags;

	GLINK_PKT_INFO("for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	refcount_dec(&gpdev->refcount);
	if (refcount_read(&gpdev->refcount) == 1) {
		glink_pkt_clear_queues(gpdev);

		spin_lock_irqsave(&gpdev->queue_lock, flags);
		gpdev->sig_change = false;
		wake_up_interruptible(&gpdev->readq);
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);

		if (gpdev->enable_ch_close)
			glink_pkt_drv_try_unregister(gpdev);
	}
	put_device(dev);

	return 0;
}

/**
 * glink_pkt_read() - read() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to Read the data from glink pkt device when
 * userspace client do a read() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t glink_pkt_read(struct file *file,
			char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct sk_buff *skb = NULL;
	int ret = 0;
	int use;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		return -ENETRESET;
	}

	GLINK_PKT_INFO("begin for %s by %s:%d ref_cnt[%d], remaining[%d], count[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount),
			   gpdev->rdata_len, count);

	/* Wait for data in the queue */
	spin_lock_irq(&gpdev->queue_lock);
	if (skb_queue_empty(&gpdev->queue) && !gpdev->rskb) {
		if (file->f_flags & O_NONBLOCK) {
			spin_unlock_irq(&gpdev->queue_lock);
			return -EAGAIN;
		}

		/* Wait until we get data or the endpoint goes away */
		ret = wait_event_interruptible_lock_irq(gpdev->readq,
							!skb_queue_empty(&gpdev->queue) ||
							!completion_done(&gpdev->ch_open),
							gpdev->queue_lock);
	}
	spin_unlock_irq(&gpdev->queue_lock);

	if (ret)
		return -ERESTARTSYS;
	if (!completion_done(&gpdev->ch_open))
		return -ENETRESET;

	mutex_lock(&gpdev->rskb_read_lock);
	spin_lock_irq(&gpdev->queue_lock);
	if (!gpdev->rskb) {
		gpdev->rskb = skb_dequeue(&gpdev->queue);
		if (!gpdev->rskb) {
			spin_unlock_irq(&gpdev->queue_lock);
			mutex_unlock(&gpdev->rskb_read_lock);
			return 0;
		}
		gpdev->rdata = gpdev->rskb->data;
		gpdev->rdata_len = gpdev->rskb->len;
	}
	spin_unlock_irq(&gpdev->queue_lock);

	use = min_t(size_t, count, gpdev->rdata_len);

	if (copy_to_user(buf, gpdev->rdata, use))
		ret = -EFAULT;

	spin_lock_irq(&gpdev->queue_lock);
	gpdev->rdata += use;
	gpdev->rdata_len -= use;

	if (!gpdev->fragmented_read || !gpdev->rdata_len) {
		skb = gpdev->rskb;

		gpdev->rskb = NULL;
		gpdev->rdata = NULL;
		gpdev->rdata_len = 0;
	}
	spin_unlock_irq(&gpdev->queue_lock);

	if (skb)
		glink_pkt_kfree_skb(gpdev, skb);
	mutex_unlock(&gpdev->rskb_read_lock);

	ret = (ret < 0) ? ret : use;
	GLINK_PKT_INFO("end for %s by %s:%d ret[%d], remaining[%d]\n", gpdev->ch_name,
		       current->comm, task_pid_nr(current), ret, gpdev->rdata_len);

	return ret;
}

/**
 * glink_pkt_write() - write() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to write the data to glink pkt device when
 * userspace client do a write() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t glink_pkt_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	void *kbuf;
	int ret;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	GLINK_PKT_INFO("begin to %s buffer_size %zu\n", gpdev->ch_name, count);
	kbuf = vmemdup_user(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (mutex_lock_interruptible(&gpdev->lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}
	if (!completion_done(&gpdev->ch_open) || !gpdev->rpdev) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		ret = -ENETRESET;
		goto unlock_ch;
	}

	if (file->f_flags & O_NONBLOCK)
		ret = rpmsg_trysend(gpdev->rpdev->ept, kbuf, count);
	else
		ret = rpmsg_send(gpdev->rpdev->ept, kbuf, count);

unlock_ch:
	mutex_unlock(&gpdev->lock);

free_kbuf:
	kvfree(kbuf);
	GLINK_PKT_INFO("finish to %s ret %d\n", gpdev->ch_name, ret);
	return ret < 0 ? ret : count;
}

/**
 * glink_pkt_poll() - poll() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * wait:	pointer to Poll table.
 *
 * This function is used to poll on the glink pkt device when
 * userspace client do a poll() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static __poll_t glink_pkt_poll(struct file *file, poll_table *wait)
{
	struct glink_pkt_device *gpdev = file->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return POLLERR;
	}
	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		return POLLHUP | POLLPRI;
	}

	poll_wait(file, &gpdev->readq, wait);

	mutex_lock(&gpdev->lock);

	if (!completion_done(&gpdev->ch_open) || !gpdev->rpdev) {
		GLINK_PKT_ERR("%s channel reset after wait\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return POLLHUP;
	}

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (!skb_queue_empty(&gpdev->queue) || gpdev->rskb)
		mask |= POLLIN | POLLRDNORM;

	if (gpdev->sig_change)
		mask |= POLLPRI;
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	mask |= rpmsg_poll(gpdev->rpdev->ept, file, wait);

	mutex_unlock(&gpdev->lock);

	return mask;
}

/**
 * glink_pkt_tiocmset() - set the signals for glink_pkt device
 * devp:	Pointer to the glink_pkt device structure.
 * cmd:		IOCTL command.
 * arg:		Arguments to the ioctl call.
 *
 * This function is used to set the signals on the glink pkt device
 * when userspace client do a ioctl() system call with TIOCMBIS,
 * TIOCMBIC and TICOMSET.
 */
static int glink_pkt_tiocmset(struct glink_pkt_device *gpdev, unsigned int cmd,
			      int __user *arg)
{
	u32 set, clear, val;
	int ret;

	ret = get_user(val, arg);
	if (ret)
		return ret;
	set = clear = 0;
	switch (cmd) {
	case TIOCMBIS:
		set = val;
		break;
	case TIOCMBIC:
		clear = val;
		break;
	case TIOCMSET:
		set = val;
		clear = ~val;
		break;
	}

	set &= TIOCM_DTR | TIOCM_RTS | TIOCM_CD | TIOCM_RI;
	clear &= TIOCM_DTR | TIOCM_RTS | TIOCM_CD | TIOCM_RI;
	GLINK_PKT_INFO("set[0x%x] clear[0x%x]\n", set, clear);

	return qcom_glink_set_signals(gpdev->rpdev->ept, set, clear);
}

static const struct vm_operations_struct glink_pkt_vm_ops = {
};

static int glink_pkt_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (vma->vm_flags & (VM_WRITE | VM_EXEC))
		return -EPERM;

	/* Instruct vm_insert_page() to not mmap_read_lock(mm) */
	vm_flags_mod(vma, VM_MIXEDMAP, ~(VM_MAYWRITE | VM_MAYEXEC));

	vma->vm_ops = &glink_pkt_vm_ops;
	return 0;
}

struct glink_pkt_zerocopy_cb {
	unsigned long leading_page;
	unsigned long trailing_page;
	unsigned long address;
	unsigned long length;
};

static int glink_pkt_zerocopy_done(struct glink_pkt_device *gpdev,
				   struct glink_pkt_zerocopy_receive *zc)
{
	unsigned long address = (unsigned long)zc->address;
	struct glink_pkt_zerocopy_cb *cb = NULL;
	struct vm_area_struct *vma;
	struct sk_buff *skb;
	unsigned long flags;

	if (!PAGE_ALIGNED(address) || address != zc->address)
		return -EINVAL;

	if (!gpdev->rx_done)
		return -EINVAL;

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, address);
	if (!vma || vma->vm_ops != &glink_pkt_vm_ops) {
		mmap_read_unlock(current->mm);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb = skb_peek(&gpdev->pending);
	if (skb) {
		do {
			cb = (struct glink_pkt_zerocopy_cb *)skb->cb;
			if (address == cb->address) {
				skb_unlink(skb, &gpdev->pending);
				break;
			}
		} while ((skb = skb_peek_next(skb, &gpdev->pending)));
	}
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	if (cb && cb->address == address)
		zap_vma_ptes(vma, address, cb->length);
	mmap_read_unlock(current->mm);
	if (!skb)
		return -EINVAL;

	if (cb->trailing_page)
		free_page(cb->trailing_page);
	if (cb->leading_page)
		free_page(cb->leading_page);
	glink_pkt_kfree_skb(gpdev, skb);

	return 0;
}

static struct page *glink_pkt_vaddr_to_page(void *cpu_addr)
{
	if (is_vmalloc_addr(cpu_addr))
		return vmalloc_to_page(cpu_addr);
	return virt_to_page(cpu_addr);
}

static int glink_pkt_zerocopy_receive(struct glink_pkt_device *gpdev,
				      struct glink_pkt_zerocopy_receive *zc)
{
	unsigned long address = (unsigned long)zc->address;
	struct glink_pkt_zerocopy_cb *cb;
	unsigned long trailing_page = 0;
	unsigned long leading_page = 0;
	unsigned long data_address;
	struct vm_area_struct *vma;
	unsigned int pages_to_map;
	u32 total_bytes_to_map;
	struct sk_buff *skb;
	unsigned long flags;
	u32 data_len;
	u32 vma_len;
	int rc;

	if (!PAGE_ALIGNED(address) || address != zc->address)
		return -EINVAL;

	if (!gpdev->rx_done)
		return -EINVAL;

	zc->offset = 0;
	zc->length = 0;

	/* Check if address is being used in any of the pending mappings */
	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb = skb_peek(&gpdev->pending);
	if (skb) {
		do {
			cb = (struct glink_pkt_zerocopy_cb *)skb->cb;

			if (address >= cb->address && address <= (cb->address + cb->length)) {
				spin_unlock_irqrestore(&gpdev->queue_lock, flags);
				return -EINVAL;
			}
		} while ((skb = skb_peek_next(skb, &gpdev->pending)));
	}
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, address);
	if (!vma || vma->vm_ops != &glink_pkt_vm_ops) {
		rc = -EINVAL;
		goto error_out;
	}
	vma_len = vma->vm_end - address;

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb = skb_dequeue(&gpdev->queue);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
	if (!skb) {
		rc = -EIO;
		goto error_out;
	}
	data_address = (unsigned long)skb->data;
	data_len = skb->len;

	/* Pass sanity checks, start actual mapping procedure */
	total_bytes_to_map = data_len;

	/*
	 * If the skb data is not page aligned, then a blank page needs to be
	 * allocated, zeroed out and the data copied to prevent information
	 * leaks
	 */
	if (!PAGE_ALIGNED(data_address)) {
		u32 copy_size;
		u32 offset;
		void *buf;

		leading_page = get_zeroed_page(GFP_KERNEL);
		if (!leading_page) {
			rc = -ENOMEM;
			goto skb_repush;
		}

		offset = data_address - ALIGN_DOWN(data_address, PAGE_SIZE);
		copy_size = PAGE_SIZE - offset;
		buf = (void *)leading_page;

		memcpy(buf + offset, (void *)data_address, copy_size);
		total_bytes_to_map = total_bytes_to_map - copy_size + PAGE_SIZE;

		zc->offset = offset;
	}

	/*
	 * If the data does not end of the page boundary, then we need to copy
	 * the trailing data into a zeroed out page, similar to the first page
	 */
	if (!PAGE_ALIGNED(data_address + data_len)) {
		u32 copy_size;
		void *dst;
		unsigned long end;

		trailing_page = get_zeroed_page(GFP_KERNEL);
		if (!trailing_page) {
			rc = -ENOMEM;
			goto free_leading;
		}

		end = data_address + data_len;
		copy_size = end - ALIGN_DOWN(end, PAGE_SIZE);
		dst = (void *)trailing_page;

		memcpy(dst, (void *)ALIGN_DOWN(end, PAGE_SIZE), copy_size);
		total_bytes_to_map = total_bytes_to_map - copy_size + PAGE_SIZE;
	}

	if (vma_len < total_bytes_to_map) {
		rc = -ENOSPC;
		goto free_trailing;
	}
	if (!PAGE_ALIGNED(total_bytes_to_map)) {
		rc = -EINVAL;
		goto free_trailing;
	}
	zap_vma_ptes(vma, address, total_bytes_to_map);

	pages_to_map = total_bytes_to_map / PAGE_SIZE;
	if (leading_page) {
		rc = vm_insert_page(vma, address, virt_to_page(leading_page));
		if (rc)
			goto zap_pages;

		address += PAGE_SIZE;
	}

	data_address = ALIGN(data_address, PAGE_SIZE);
	while (pages_to_map) {
		struct page *page;

		page = glink_pkt_vaddr_to_page((void *)data_address);
		prefetchw(page);
		rc = vm_insert_page(vma, address, page);
		if (rc)
			goto zap_pages;

		address += PAGE_SIZE;
		data_address += PAGE_SIZE;
		pages_to_map--;
	}
	if (trailing_page) {
		rc = vm_insert_page(vma, address, virt_to_page(trailing_page));
		if (rc)
			goto zap_pages;

		address += PAGE_SIZE;
	}
	zc->length = data_len;

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	cb = (struct glink_pkt_zerocopy_cb *)skb->cb;
	cb->leading_page = leading_page;
	cb->trailing_page = trailing_page;
	cb->address = zc->address;
	cb->length = total_bytes_to_map;
	skb_queue_tail(&gpdev->pending, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
	mmap_read_unlock(current->mm);

	return 0;

zap_pages:
	zap_vma_ptes(vma, zc->address, total_bytes_to_map);
free_trailing:
	if (trailing_page)
		free_page(trailing_page);
free_leading:
	if (leading_page)
		free_page(leading_page);
skb_repush:
	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb_queue_head(&gpdev->queue, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
error_out:
	mmap_read_unlock(current->mm);
	return rc;
}

/**
 * glink_pkt_ioctl() - ioctl() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * cmd:		IOCTL command.
 * arg:		Arguments to the ioctl call.
 *
 * This function is used to ioctl on the glink pkt device when
 * userspace client do a ioctl() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static long glink_pkt_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct glink_pkt_zerocopy_receive zc;
	struct glink_pkt_device *gpdev;
	unsigned long flags;
	int ret;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}
	if (mutex_lock_interruptible(&gpdev->lock))
		return -ERESTARTSYS;

	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return -ENETRESET;
	}

	switch (cmd) {
	case TIOCMGET:
		spin_lock_irqsave(&gpdev->queue_lock, flags);
		gpdev->sig_change = false;
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);

		ret = qcom_glink_get_signals(gpdev->rpdev->ept);
		if (ret >= 0)
			ret = put_user(ret, (int __user *)arg);
		break;
	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		ret = glink_pkt_tiocmset(gpdev, cmd, (int __user *)arg);
		break;
	case GLINK_PKT_IOCTL_QUEUE_RX_INTENT:
		/* Return success to not break userspace client logic */
		ret = 0;
		break;
	case GLINK_PKT_IOCTL_ZC_RECV:
		if (copy_from_user(&zc, (void __user *)arg, sizeof(zc))) {
			ret = -EFAULT;
			break;
		}
		ret = glink_pkt_zerocopy_receive(gpdev, &zc);

		if (copy_to_user((void __user *)arg, &zc, sizeof(zc)))
			ret = -EFAULT;
		break;
	case GLINK_PKT_IOCTL_ZC_DONE:
		if (copy_from_user(&zc, (void __user *)arg, sizeof(zc))) {
			ret = -EFAULT;
			break;
		}
		ret = glink_pkt_zerocopy_done(gpdev, &zc);
		break;
	default:
		GLINK_PKT_ERR("unrecognized ioctl command 0x%x\n", cmd);
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&gpdev->lock);

	return ret;
}

static const struct file_operations glink_pkt_fops = {
	.owner = THIS_MODULE,
	.open = glink_pkt_open,
	.release = glink_pkt_release,
	.read = glink_pkt_read,
	.write = glink_pkt_write,
	.poll = glink_pkt_poll,
	.unlocked_ioctl = glink_pkt_ioctl,
	.mmap = glink_pkt_mmap,
	.compat_ioctl = glink_pkt_ioctl,
};

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	return scnprintf(buf, RPMSG_NAME_SIZE, "%s\n", gpdev->ch_name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *glink_pkt_device_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(glink_pkt_device);

/**
 * parse_glinkpkt_devicetree() - parse device tree binding for a subnode
 *
 * np:		pointer to a device tree node
 * gpdev:	pointer to GLINK PACKET device
 *
 * Return:	0 on success, standard Linux error codes on error.
 */
static int glink_pkt_parse_devicetree(struct device_node *np,
				      struct glink_pkt_device *gpdev)
{
	char *key;
	int ret;

	key = "qcom,glinkpkt-edge";
	ret = of_property_read_string(np, key, &gpdev->edge);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-ch-name";
	ret = of_property_read_string(np, key, &gpdev->ch_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-dev-name";
	ret = of_property_read_string(np, key, &gpdev->dev_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-enable-ch-close";
	gpdev->enable_ch_close = of_property_read_bool(np, key);

	key = "qcom,glinkpkt-fragmented-read";
	gpdev->fragmented_read = of_property_read_bool(np, key);

	GLINK_PKT_INFO(
	"Parsed %s:%s /dev/%s enable channel close:%d fragmented-read:%d\n",
		      gpdev->edge, gpdev->ch_name, gpdev->dev_name,
		      gpdev->enable_ch_close, gpdev->fragmented_read);

	return 0;

error:
	GLINK_PKT_ERR("%s: missing key: %s\n", __func__, key);
	return ret;
}

static void glink_pkt_release_device(struct device *dev)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	GLINK_PKT_INFO("for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	ida_simple_remove(&glink_pkt_minor_ida, MINOR(gpdev->dev.devt));
	cdev_del(&gpdev->cdev);
	kfree(gpdev);
}

static int glink_pkt_init_rpmsg(struct glink_pkt_device *gpdev)
{
	struct rpmsg_driver *rpdrv = &gpdev->drv;
	struct device *dev = &gpdev->dev;
	struct rpmsg_device_id *match;
	char *drv_name;

	match = devm_kzalloc(dev, sizeof(*match) * 2, GFP_KERNEL);
	if (!match)
		return -ENOMEM;
	strscpy(match->name, gpdev->ch_name, RPMSG_NAME_SIZE);

	drv_name = devm_kasprintf(dev, GFP_KERNEL,
				   "%s_%s", "glink_pkt", gpdev->dev_name);
	if (!drv_name)
		return -ENOMEM;

	rpdrv->probe = glink_pkt_rpdev_probe;
	rpdrv->remove = glink_pkt_rpdev_remove;
	rpdrv->callback = glink_pkt_rpdev_cb;
	rpdrv->id_table = match;
	rpdrv->drv.name = drv_name;

	return glink_pkt_drv_try_register(gpdev);
}

/**
 * glink_pkt_add_device() - Create glink packet device and add cdev
 * parent:	pointer to the parent device of this glink packet device
 * np:		pointer to device node this glink packet device represents
 *
 * return:	0 for success, Standard Linux errors
 */
static int glink_pkt_create_device(struct device *parent,
				   struct device_node *np)
{
	struct glink_pkt_device *gpdev;
	struct device *dev;
	int ret, minor;

	gpdev = kzalloc(sizeof(*gpdev), GFP_KERNEL);
	if (!gpdev)
		return -ENOMEM;

	minor = ida_simple_get(&glink_pkt_minor_ida, 0, num_glink_pkt_devs,
			     GFP_KERNEL);
	if (minor < 0) {
		kfree(gpdev);
		return minor;
	}

	dev = &gpdev->dev;

	ret = glink_pkt_parse_devicetree(np, gpdev);
	if (ret < 0) {
		GLINK_PKT_ERR("failed to parse dt ret:%d\n", ret);
		ida_simple_remove(&glink_pkt_minor_ida, MINOR(dev->devt));
		kfree(gpdev);
		return ret;
	}

	mutex_init(&gpdev->lock);
	mutex_init(&gpdev->drv_lock);
	mutex_init(&gpdev->rskb_read_lock);
	refcount_set(&gpdev->refcount, 1);
	init_completion(&gpdev->ch_open);

	/* Default open timeout for open is 120 sec */
	gpdev->open_tout = 120;
	gpdev->sig_change = false;
	gpdev->rx_done = false;

	spin_lock_init(&gpdev->queue_lock);

	gpdev->rskb = NULL;
	gpdev->rdata = NULL;
	gpdev->rdata_len = 0;

	skb_queue_head_init(&gpdev->queue);
	skb_queue_head_init(&gpdev->pending);
	init_waitqueue_head(&gpdev->readq);

	device_initialize(dev);
	dev->class = glink_pkt_class;
	dev->parent = parent;
	dev->groups = glink_pkt_device_groups;
	dev_set_drvdata(dev, gpdev);

	cdev_init(&gpdev->cdev, &glink_pkt_fops);
	gpdev->cdev.owner = THIS_MODULE;

	dev->devt = MKDEV(MAJOR(glink_pkt_major), minor);
	dev_set_name(dev, gpdev->dev_name, minor);

	ret = cdev_add(&gpdev->cdev, dev->devt, 1);
	if (ret) {
		GLINK_PKT_ERR("cdev_add failed for %s ret:%d\n",
			      gpdev->dev_name, ret);
		ida_simple_remove(&glink_pkt_minor_ida, MINOR(dev->devt));
		kfree(gpdev);
		return ret;
	}

	dev->release = glink_pkt_release_device;
	ret = device_add(dev);
	if (ret) {
		GLINK_PKT_ERR("device_create failed for %s ret:%d\n",
			      gpdev->dev_name, ret);
		goto free_dev;
	}

	if (device_create_file(dev, &dev_attr_open_timeout))
		GLINK_PKT_ERR("device_create_file failed for %s\n",
			      gpdev->dev_name);

	ret = glink_pkt_init_rpmsg(gpdev);
	if (ret)
		goto free_dev;

	return 0;

free_dev:
	put_device(dev);

	return ret;
}

/**
 * glink_pkt_deinit() - De-initialize this module
 *
 * This function frees all the memory and unregisters the char device region.
 */
static void glink_pkt_deinit(void)
{
	class_destroy(glink_pkt_class);
	unregister_chrdev_region(MAJOR(glink_pkt_major), num_glink_pkt_devs);
}

/**
 * glink_pkt_probe() - Probe a GLINK packet device
 *
 * pdev:	Pointer to platform device.
 *
 * return:	0 on success, standard Linux error codes on error.
 *
 * This function is called when the underlying device tree driver registers
 * a platform device, mapped to a G-Link packet device.
 */
static int glink_pkt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cn;
	int ret;

	num_glink_pkt_devs = of_get_child_count(dev->of_node);
	ret = alloc_chrdev_region(&glink_pkt_major, 0, num_glink_pkt_devs,
				  "glinkpkt");
	if (ret < 0) {
		GLINK_PKT_ERR("alloc_chrdev_region failed ret:%d\n", ret);
		return ret;
	}
	glink_pkt_class = class_create(THIS_MODULE, "glinkpkt");
	if (IS_ERR(glink_pkt_class)) {
		ret = PTR_ERR(glink_pkt_class);
		GLINK_PKT_ERR("class_create failed ret:%d\n", ret);
		goto error_deinit;
	}

	for_each_child_of_node(dev->of_node, cn) {
		glink_pkt_create_device(dev, cn);
	}

	GLINK_PKT_INFO("G-Link Packet Port Driver Initialized\n");
	return 0;

error_deinit:
	glink_pkt_deinit();
	return ret;
}

static const struct of_device_id glink_pkt_match_table[] = {
	{ .compatible = "qcom,glinkpkt" },
	{},
};

static struct platform_driver glink_pkt_driver = {
	.probe = glink_pkt_probe,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = glink_pkt_match_table,
	 },
};

/**
 * glink_pkt_init() - Initialization function for this module
 *
 * returns:	0 on success, standard Linux error code otherwise.
 */
static int __init glink_pkt_init(void)
{
	int ret;

	ret = platform_driver_register(&glink_pkt_driver);
	if (ret) {
		GLINK_PKT_ERR("%s: glink_pkt register failed %d\n",
			__func__, ret);
		return ret;
	}
	glink_pkt_ilctxt = ipc_log_context_create(GLINK_PKT_IPC_LOG_PAGE_CNT,
						  "glink_pkt", 0);
	return 0;
}

/**
 * glink_pkt_exit() - Exit function for this module
 *
 * This function is used to cleanup the module during the exit.
 */
static void __exit glink_pkt_exit(void)
{
	glink_pkt_deinit();
	platform_driver_unregister(&glink_pkt_driver);
}

module_init(glink_pkt_init);
module_exit(glink_pkt_exit);

MODULE_DESCRIPTION("MSM G-Link Packet Port");
MODULE_LICENSE("GPL");
