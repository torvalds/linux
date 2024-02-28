// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/ipc_logging.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/refcount.h>
#include <linux/rpmsg.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/uaccess.h>
#include <linux/msm_smd_pkt.h>
#include <linux/rpmsg/qcom_smd.h>

#define MODULE_NAME "msm_smdpkt"
#define DEVICE_NAME "smdpkt"
#define SMD_PKT_IPC_LOG_PAGE_CNT 2

/**
 * struct smd_pkt - driver context, relates rpdev to cdev
 * @dev:        smd pkt device
 * @cdev:       cdev for the smd pkt device
 * @drv:        rpmsg driver for registering to rpmsg bus
 * @lock:       synchronization of @rpdev and @open_tout modifications
 * @ch_open:    wait object for opening the smd channel
 * @refcount:   count how many userspace clients have handles
 * @rpdev:      underlaying rpmsg device
 * @queue_lock: synchronization of @queue operations
 * @queue:      incoming message queue
 * @readq:      wait object for incoming queue
 * @sig_change: flag to indicate serial signal change
 * @dev_name:   /dev/@dev_name for smd_pkt device
 * @ch_name:    smd channel to match to
 * @edge:       smd edge to match to
 * @open_tout:  timeout for open syscall, configurable in sysfs
 */
struct smd_pkt_dev {

	struct device dev;
	struct cdev cdev;
	struct rpmsg_driver drv;
	struct mutex lock;
	struct completion ch_open;
	refcount_t refcount;
	struct rpmsg_device *rpdev;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	wait_queue_head_t readq;
	int sig_change;
	const char *dev_name;
	const char *ch_name;
	const char *edge;
	int open_tout;
};

#define dev_to_smd_pkt_devp(_dev) container_of(_dev, struct smd_pkt_dev, dev)
#define cdev_to_smd_pkt_devp(_cdev) container_of(_cdev, \
				struct smd_pkt_dev, cdev)
#define drv_to_rpdrv(_drv) container_of(_drv, struct rpmsg_driver, drv)
#define rpdrv_to_smd_pkt_devp(_rdrv) container_of(_rdrv, \
				 struct smd_pkt_dev, drv)

static void *smd_pkt_ilctxt;

#define SMD_PKT_INFO(x, ...)                                          \
	ipc_log_string(smd_pkt_ilctxt,                        \
		"[%s]: "x, __func__, ##__VA_ARGS__)

#define SMD_PKT_ERR(x, ...)                                                 \
do {                                                                          \
	pr_err_ratelimited("[%s]: "x, __func__, ##__VA_ARGS__);               \
	ipc_log_string(smd_pkt_ilctxt, "[%s]: "x, __func__, ##__VA_ARGS__); \
} while (0)

#define SMD_PKT_IOCTL_QUEUE_RX_INTENT \
	_IOW(SMD_PKT_IOCTL_MAGIC, 0, unsigned int)

static dev_t smd_pkt_major;
static struct class *smd_pkt_class;
static int num_smd_pkt_devs;

static DEFINE_IDA(smd_pkt_minor_ida);
static ssize_t open_timeout_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t n)
{
	struct smd_pkt_dev *smd_pkt_devp = dev_to_smd_pkt_devp(dev);
	long tmp;

	mutex_lock(&smd_pkt_devp->lock);
	if (kstrtol(buf, 0, &tmp)) {
		mutex_unlock(&smd_pkt_devp->lock);
		SMD_PKT_ERR("unable to convert:%s to an int for /dev/%s\n",
				buf, smd_pkt_devp->dev_name);
		return -EINVAL;
	}
	smd_pkt_devp->open_tout = tmp;
	mutex_unlock(&smd_pkt_devp->lock);

	return n;
}

static ssize_t open_timeout_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct smd_pkt_dev *smd_pkt_devp = dev_to_smd_pkt_devp(dev);
	ssize_t ret;

	mutex_lock(&smd_pkt_devp->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", smd_pkt_devp->open_tout);
	mutex_unlock(&smd_pkt_devp->lock);

	return ret;
}

static DEVICE_ATTR_RW(open_timeout);

static int smd_pkt_rpdev_probe(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct smd_pkt_dev *smd_pkt_devp = rpdrv_to_smd_pkt_devp(rpdrv);

	mutex_lock(&smd_pkt_devp->lock);
	smd_pkt_devp->rpdev = rpdev;
	qcom_smd_register_signals_cb(rpdev->ept, smd_pkt_rpdev_sigs);
	mutex_unlock(&smd_pkt_devp->lock);
	dev_set_drvdata(&rpdev->dev, smd_pkt_devp);
	complete_all(&smd_pkt_devp->ch_open);
	return 0;
}

static int smd_pkt_rpdev_cb(struct rpmsg_device *rpdev, void *buf, int len,
				void *priv, u32 addr)
{
	struct smd_pkt_dev *smd_pkt_devp = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;
	struct sk_buff *skb;

	if (!smd_pkt_devp)
		return -EINVAL;
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	skb_put_data(skb, buf, len);
	spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);
	skb_queue_tail(&smd_pkt_devp->queue, skb);
	spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);
	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&smd_pkt_devp->readq);
	return 0;
}

static int smd_pkt_rpdev_sigs(struct rpmsg_device *rpdev, void *priv, u32 old, u32 new)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct smd_pkt_dev *smd_pkt_devp = rpdrv_to_smd_pkt_devp(rpdrv);
	unsigned long flags;

	spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);
	smd_pkt_devp->sig_change = true;
	spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&smd_pkt_devp->readq);

	return 0;
}

/**
 * smd_pkt_tiocmset() - set the signals for smd_pkt device
 * smd_pkt_devp:        Pointer to the smd_pkt device structure.
 * cmd:         IOCTL command.
 * arg:         Arguments to the ioctl call.
 *
 * This function is used to set the signals on the smd pkt device
 * when userspace client do a ioctl() system call with TIOCMBIS,
 * TIOCMBIC and TICOMSET.
 */
static int smd_pkt_tiocmset(struct smd_pkt_dev *smd_pkt_devp, unsigned int cmd,
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
	SMD_PKT_INFO("set[0x%x] clear[0x%x]\n", set, clear);
	return qcom_smd_set_sigs(smd_pkt_devp->rpdev->ept, set, clear);
}

/**
 * smd_pkt_ioctl() - ioctl() syscall for the smd_pkt device
 * file:        Pointer to the file structure.
 * cmd:         IOCTL command.
 * arg:         Arguments to the ioctl call.
 *
 * This function is used to ioctl on the smd pkt device when
 * userspace client do a ioctl() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static long smd_pkt_ioctl(struct file *file, unsigned int cmd,
					     unsigned long arg)
{
	struct smd_pkt_dev *smd_pkt_devp;
	unsigned long flags;
	int ret = 0;

	smd_pkt_devp = file->private_data;

	if (!smd_pkt_devp || refcount_read(&smd_pkt_devp->refcount) == 1) {
		SMD_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&smd_pkt_devp->lock))
		return -ERESTARTSYS;

	if (!completion_done(&smd_pkt_devp->ch_open)) {
		SMD_PKT_ERR("%s channel in reset\n", smd_pkt_devp->ch_name);
		mutex_unlock(&smd_pkt_devp->lock);
		return -ENETRESET;
	}

	switch (cmd) {
	case TIOCMGET:
		spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);
		smd_pkt_devp->sig_change = false;
		spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);

		ret = qcom_smd_get_sigs(smd_pkt_devp->rpdev->ept);
		if (ret >= 0)
			ret = put_user(ret, (int __user *)arg);
		break;
	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		ret = smd_pkt_tiocmset(smd_pkt_devp, cmd, (int __user *)arg);
		break;
	case SMD_PKT_IOCTL_QUEUE_RX_INTENT:
		/* Return success to not break userspace client logic */
		ret = 0;
		break;
	default:
		SMD_PKT_ERR("unrecognized ioctl command 0x%x\n", cmd);
		ret = -ENOIOCTLCMD;
		break;
	}

	mutex_unlock(&smd_pkt_devp->lock);

	return ret;
}

/**
 * smd_pkt_read() - read() syscall for the smd_pkt device
 * file:        Pointer to the file structure.
 * buf:         Pointer to the userspace buffer.
 * count:       Number bytes to read from the file.
 * ppos:        Pointer to the position into the file.
 *
 * This function is used to Read the data from smd pkt device when
 * userspace client do a read() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
ssize_t smd_pkt_read(struct file *file,
		       char __user *buf,
		       size_t count,
		       loff_t *ppos)
{

	struct smd_pkt_dev *smd_pkt_devp = file->private_data;
	unsigned long flags;
	struct sk_buff *skb;
	int use;

	if (!smd_pkt_devp ||
			 refcount_read(&smd_pkt_devp->refcount) == 1) {
		SMD_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	if (!completion_done(&smd_pkt_devp->ch_open)) {
		SMD_PKT_ERR("%s channel in reset\n", smd_pkt_devp->ch_name);
		return -ENETRESET;
	}

	SMD_PKT_INFO("begin for %s by %s:%d ref_cnt[%d]\n",
	smd_pkt_devp->ch_name, current->comm,
	task_pid_nr(current),
	refcount_read(&smd_pkt_devp->refcount));

	spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);

	/* Wait for data in the queue */
	if (skb_queue_empty(&smd_pkt_devp->queue)) {
		spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Wait until we get data or the endpoint goes away */
		if (wait_event_interruptible(smd_pkt_devp->readq,
			!skb_queue_empty(&smd_pkt_devp->queue) ||
				!completion_done(&smd_pkt_devp->ch_open)))
			return -ERESTARTSYS;

		spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);
	}

	skb = skb_dequeue(&smd_pkt_devp->queue);
	spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);
	if (!skb)
		return -EFAULT;

	use = min_t(size_t, count, skb->len);
	if (copy_to_user(buf, skb->data, use))
		use = -EFAULT;

	kfree_skb(skb);

	SMD_PKT_INFO("end for %s by %s:%d ret[%d]\n", smd_pkt_devp->ch_name,
			current->comm, task_pid_nr(current), use);

	return use;
}

/**
 * smd_pkt_write() - write() syscall for the smd_pkt device
 * file:        Pointer to the file structure.
 * buf:         Pointer to the userspace buffer.
 * count:       Number bytes to read from the file.
 * ppos:        Pointer to the position into the file.
 *
 * This function is used to write the data to smd pkt device when
 * userspace client do a write() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
ssize_t smd_pkt_write(struct file *file,
		       const char __user *buf,
		       size_t count,
		       loff_t *ppos)
{
	struct smd_pkt_dev *smd_pkt_devp = file->private_data;
	void *kbuf;
	int ret;

	smd_pkt_devp = file->private_data;

	if (!smd_pkt_devp || refcount_read(&smd_pkt_devp->refcount) == 1) {
		SMD_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	SMD_PKT_INFO("begin to %s buffer_size %zu\n",
			smd_pkt_devp->ch_name, count);
	kbuf = memdup_user(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (mutex_lock_interruptible(&smd_pkt_devp->lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}

	if (!completion_done(&smd_pkt_devp->ch_open) ||
				!smd_pkt_devp->rpdev) {
		SMD_PKT_ERR("%s channel in reset\n", smd_pkt_devp->ch_name);
		ret = -ENETRESET;
		goto unlock_ch;
	}

	if (file->f_flags & O_NONBLOCK)
		ret = rpmsg_trysend(smd_pkt_devp->rpdev->ept, kbuf, count);
	else
		ret = rpmsg_send(smd_pkt_devp->rpdev->ept, kbuf, count);

unlock_ch:
	mutex_unlock(&smd_pkt_devp->lock);

free_kbuf:
	kfree(kbuf);
	SMD_PKT_INFO("finish to %s ret %d\n", smd_pkt_devp->ch_name, ret);
	return ret < 0 ? ret : count;
}

/**
 * smd_pkt_poll() - poll() syscall for the smd_pkt device
 * file:        Pointer to the file structure.
 * wait:        pointer to Poll table.
 *
 * This function is used to poll on the smd pkt device when
 * userspace client do a poll() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static unsigned int smd_pkt_poll(struct file *file, poll_table *wait)

{
	struct smd_pkt_dev *smd_pkt_devp = file->private_data;
	unsigned int mask = 0;
	unsigned long flags;

	smd_pkt_devp = file->private_data;

	if (!smd_pkt_devp || refcount_read(&smd_pkt_devp->refcount) == 1) {
		SMD_PKT_ERR("invalid device handle\n");
		return POLLERR;
	}

	if (!completion_done(&smd_pkt_devp->ch_open)) {
		SMD_PKT_ERR("%s channel in reset\n", smd_pkt_devp->ch_name);
		return POLLHUP;
	}

	poll_wait(file, &smd_pkt_devp->readq, wait);

	mutex_lock(&smd_pkt_devp->lock);

	if (!completion_done(&smd_pkt_devp->ch_open) ||
						!smd_pkt_devp->rpdev) {
		SMD_PKT_ERR("%s channel reset after wait\n",
						 smd_pkt_devp->ch_name);
		mutex_unlock(&smd_pkt_devp->lock);
		return POLLHUP;
	}

	spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);
	if (!skb_queue_empty(&smd_pkt_devp->queue))
		mask |= POLLIN | POLLRDNORM;

	if (smd_pkt_devp->sig_change)
		mask |= POLLPRI;
	spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);

	mask |= rpmsg_poll(smd_pkt_devp->rpdev->ept, file, wait);

	mutex_unlock(&smd_pkt_devp->lock);

	return mask;
}

static void smd_pkt_rpdev_remove(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct smd_pkt_dev *smd_pkt_devp = rpdrv_to_smd_pkt_devp(rpdrv);

	mutex_lock(&smd_pkt_devp->lock);
	smd_pkt_devp->rpdev = NULL;
	mutex_unlock(&smd_pkt_devp->lock);

	dev_set_drvdata(&rpdev->dev, NULL);

	/* wake up any blocked readers */
	reinit_completion(&smd_pkt_devp->ch_open);
	wake_up_interruptible(&smd_pkt_devp->readq);
}

/**
 * smd_pkt_open() - open() syscall for the smd_pkt device
 * inode:       Pointer to the inode structure.
 * file:        Pointer to the file structure.
 *
 * This function is used to open the smd pkt device when
 * userspace client do a open() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
int smd_pkt_open(struct inode *inode, struct file *file)
{
	struct smd_pkt_dev *smd_pkt_devp = cdev_to_smd_pkt_devp(inode->i_cdev);
	int tout = msecs_to_jiffies(smd_pkt_devp->open_tout * 1000);
	struct device *dev = &smd_pkt_devp->dev;
	int ret;

	refcount_inc(&smd_pkt_devp->refcount);
	get_device(dev);

	SMD_PKT_INFO("begin for %s by %s:%d ref_cnt[%d]\n",
			smd_pkt_devp->ch_name, current->comm,
			task_pid_nr(current),
			refcount_read(&smd_pkt_devp->refcount));

	ret = wait_for_completion_interruptible_timeout(&smd_pkt_devp->ch_open,
								tout);
	if (ret <= 0) {
		refcount_dec(&smd_pkt_devp->refcount);
		put_device(dev);
		SMD_PKT_INFO("timeout for %s by %s:%d\n", smd_pkt_devp->ch_name,
				current->comm, task_pid_nr(current));
		return -ETIMEDOUT;
	}
	file->private_data = smd_pkt_devp;

	SMD_PKT_INFO("end for %s by %s:%d ref_cnt[%d]\n",
			smd_pkt_devp->ch_name, current->comm,
			task_pid_nr(current),
				refcount_read(&smd_pkt_devp->refcount));
	return 0;
}

/**
 * smd_pkt_release() - release operation on smd_pkt device
 * inode:       Pointer to the inode structure.
 * file:        Pointer to the file structure.
 *
 * This function is used to release the smd pkt device when
 * userspace client do a close() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
int smd_pkt_release(struct inode *inode, struct file *file)
{
	struct smd_pkt_dev *smd_pkt_devp = cdev_to_smd_pkt_devp(inode->i_cdev);
	struct device *dev = &smd_pkt_devp->dev;
	struct sk_buff *skb;
	unsigned long flags;

	SMD_PKT_INFO("for %s by %s:%d ref_cnt[%d]\n",
			smd_pkt_devp->ch_name, current->comm,
			task_pid_nr(current),
			refcount_read(&smd_pkt_devp->refcount));

	refcount_dec(&smd_pkt_devp->refcount);
	if (refcount_read(&smd_pkt_devp->refcount) == 1) {
		spin_lock_irqsave(&smd_pkt_devp->queue_lock, flags);

		/* Discard all SKBs */
		while (!skb_queue_empty(&smd_pkt_devp->queue)) {
			skb = skb_dequeue(&smd_pkt_devp->queue);
			kfree_skb(skb);
		}
		wake_up_interruptible(&smd_pkt_devp->readq);
		smd_pkt_devp->sig_change = false;
		spin_unlock_irqrestore(&smd_pkt_devp->queue_lock, flags);
	}

	put_device(dev);

	return 0;
}

static const struct file_operations smd_pkt_fops = {
	.owner = THIS_MODULE,
	.open = smd_pkt_open,
	.release = smd_pkt_release,
	.read = smd_pkt_read,
	.write = smd_pkt_write,
	.poll = smd_pkt_poll,
	.unlocked_ioctl = smd_pkt_ioctl,
	.compat_ioctl = smd_pkt_ioctl,
};

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct smd_pkt_dev *smd_pkt_devp = dev_to_smd_pkt_devp(dev);

	return scnprintf(buf, RPMSG_NAME_SIZE, "%s\n", smd_pkt_devp->ch_name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *smd_pkt_device_attrs[] = {
	&dev_attr_name.attr,
	NULL
};
ATTRIBUTE_GROUPS(smd_pkt_device);

/**
 * parse_smdpkt_devicetree() - parse device tree binding for a subnode
 *
 * np:          pointer to a device tree node
 * smd_pkt_devp:       pointer to SMD PACKET device
 *
 * Return:      0 on success, standard Linux error codes on error.
 */
static int smd_pkt_parse_devicetree(struct device_node *np,
					struct smd_pkt_dev *smd_pkt_devp)
{
	char *key;
	int ret;

	ret = of_property_read_string(np, "qcom,smdpkt-edge", &smd_pkt_devp->edge);
	if (ret < 0) {
		key = "qcom,smdpkt-edge";
		goto error;
	}

	ret = of_property_read_string(np, "qcom,smdpkt-ch-name", &smd_pkt_devp->ch_name);
	if (ret < 0) {
		key = "qcom,smdpkt-ch-name";
		goto error;
	}

	ret = of_property_read_string(np, "qcom,smdpkt-dev-name", &smd_pkt_devp->dev_name);
	if (ret < 0) {
		key = "qcom,smdpkt-dev-name";
		goto error;
	}

	SMD_PKT_INFO("Parsed %s:%s /dev/%s\n", smd_pkt_devp->edge,
						smd_pkt_devp->ch_name,
						smd_pkt_devp->dev_name);
	return 0;

error:
	SMD_PKT_ERR("missing key: %s\n", key);
	return ret;
}

static void smd_pkt_release_device(struct device *dev)
{
	struct smd_pkt_dev *smd_pkt_devp = dev_to_smd_pkt_devp(dev);

	ida_simple_remove(&smd_pkt_minor_ida, MINOR(smd_pkt_devp->dev.devt));
	cdev_del(&smd_pkt_devp->cdev);
}

static int smd_pkt_init_rpmsg(struct smd_pkt_dev *smd_pkt_devp)
{
	struct rpmsg_driver *rpdrv = &smd_pkt_devp->drv;
	struct device *dev = &smd_pkt_devp->dev;
	struct rpmsg_device_id *match;
	char *drv_name;

	/* zalloc array of two to NULL terminate the match list */
	match = devm_kzalloc(dev, 2 * sizeof(*match), GFP_KERNEL);
	if (!match)
		return -ENOMEM;

	snprintf(match->name, RPMSG_NAME_SIZE, "%s", smd_pkt_devp->ch_name);

	drv_name = devm_kasprintf(dev, GFP_KERNEL,
			"%s_%s", "msm_smd_pkt", smd_pkt_devp->dev_name);
	if (!drv_name)
		return -ENOMEM;

	rpdrv->probe = smd_pkt_rpdev_probe;
	rpdrv->remove = smd_pkt_rpdev_remove;
	rpdrv->callback = smd_pkt_rpdev_cb;
	rpdrv->id_table = match;
	rpdrv->drv.name = drv_name;

	register_rpmsg_driver(rpdrv);

	return 0;
}

/**
 * smdpkt       - Create smd packet device and add cdev
 * parent:      pointer to the parent device of this smd packet device
 * np:          pointer to device node this smd packet device represents
 *
 * return:      0 for success, Standard Linux errors
 */
static int smd_pkt_create_device(struct device *parent,
						struct device_node *np)
{
	struct smd_pkt_dev *smd_pkt_devp;
	struct device *dev;
	int ret;

	smd_pkt_devp = devm_kzalloc(parent, sizeof(*smd_pkt_devp), GFP_KERNEL);
	if (!smd_pkt_devp)
		return -ENOMEM;

	ret = smd_pkt_parse_devicetree(np, smd_pkt_devp);
	if (ret < 0) {
		SMD_PKT_ERR("failed to parse dt ret:%d\n", ret);
		goto free_smd_pkt_devp;
	}

	dev = &smd_pkt_devp->dev;
	mutex_init(&smd_pkt_devp->lock);
	refcount_set(&smd_pkt_devp->refcount, 1);
	init_completion(&smd_pkt_devp->ch_open);

	/* Default open timeout for open is 120 sec */
	smd_pkt_devp->open_tout = 120;
	smd_pkt_devp->sig_change = false;

	spin_lock_init(&smd_pkt_devp->queue_lock);
	skb_queue_head_init(&smd_pkt_devp->queue);
	init_waitqueue_head(&smd_pkt_devp->readq);

	device_initialize(dev);
	dev->class = smd_pkt_class;
	dev->parent = parent;
	dev->groups = smd_pkt_device_groups;
	dev_set_drvdata(dev, smd_pkt_devp);

	cdev_init(&smd_pkt_devp->cdev, &smd_pkt_fops);
	smd_pkt_devp->cdev.owner = THIS_MODULE;
	ret = ida_simple_get(&smd_pkt_minor_ida, 0, num_smd_pkt_devs,
					GFP_KERNEL);
	if (ret < 0)
		goto free_dev;

	dev->devt = MKDEV(MAJOR(smd_pkt_major), ret);
	dev_set_name(dev, smd_pkt_devp->dev_name, ret);

	ret = cdev_add(&smd_pkt_devp->cdev, dev->devt, 1);
	if (ret) {
		SMD_PKT_ERR("cdev_add failed for %s ret:%d\n",
				smd_pkt_devp->dev_name, ret);
		goto free_minor_ida;
	}

	dev->release = smd_pkt_release_device;
	ret = device_add(dev);
	if (ret) {
		SMD_PKT_ERR("device_create failed for %s ret:%d\n",
				smd_pkt_devp->dev_name, ret);
		goto free_minor_ida;
	}

	if (device_create_file(dev, &dev_attr_open_timeout))
		SMD_PKT_ERR("device_create_file failed for %s\n",
				smd_pkt_devp->dev_name);

	if (smd_pkt_init_rpmsg(smd_pkt_devp))
		goto free_minor_ida;

	return 0;

free_minor_ida:
	ida_simple_remove(&smd_pkt_minor_ida, MINOR(dev->devt));
free_dev:
	put_device(dev);

free_smd_pkt_devp:
	return ret;
}

/**
 * smd_pkt_deinit() - De-initialize this module
 *
 * This function frees all the memory and unregisters the char device region.
 */
static void smd_pkt_deinit(void)
{
	class_destroy(smd_pkt_class);
	unregister_chrdev_region(MAJOR(smd_pkt_major), num_smd_pkt_devs);
}

/**
 * smd_pkt_probe() - Probe a SMD packet device
 *
 * pdev:        Pointer to platform device.
 *
 * return:      0 on success, standard Linux error codes on error.
 *
 * This function is called when the underlying device tree driver registers
 * a platform device, mapped to a SMD packet device.
 */
static int msm_smd_pkt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cn;
	int ret;

	num_smd_pkt_devs = of_get_child_count(dev->of_node);
	ret = alloc_chrdev_region(&smd_pkt_major, 0, num_smd_pkt_devs,
					"smdpkt");

	if (ret < 0) {
		SMD_PKT_ERR("alloc_chrdev_region failed ret:%d\n", ret);
		return ret;
	}

	smd_pkt_class = class_create(THIS_MODULE, "smdpkt");
	if (IS_ERR(smd_pkt_class)) {
		SMD_PKT_ERR("class_create failed ret:%ld\n",
		PTR_ERR(smd_pkt_class));
		goto error_deinit;
	}

	for_each_available_child_of_node(dev->of_node, cn)
		smd_pkt_create_device(dev, cn);

	SMD_PKT_INFO("smd Packet Port Driver Initialized\n");
	return 0;

error_deinit:
	smd_pkt_deinit();
	return ret;
}

static const struct of_device_id msm_smd_pkt_match_table[] = {
	{ .compatible = "qcom,smdpkt" },
	{},
};

static struct platform_driver msm_smd_pkt_driver = {
	.probe = msm_smd_pkt_probe,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = msm_smd_pkt_match_table,
	 },
};

/**
 * smd_pkt_init() - Initialization function for this module
 *
 * returns:     0 on success, standard Linux error code otherwise.
 */
static int __init smd_pkt_init(void)
{
	int rc;

	rc = platform_driver_register(&msm_smd_pkt_driver);
	if (rc) {
		SMD_PKT_ERR("msm_smd_pkt driver register failed %d\n", rc);
		return rc;
	}

	smd_pkt_ilctxt = ipc_log_context_create(SMD_PKT_IPC_LOG_PAGE_CNT,
						"smd_pkt", 0);
	return 0;
}
module_init(smd_pkt_init);

/**
 * smd_pkt_exit() - Exit function for this module
 *
 * This function is used to cleanup the module during the exit.
 */
static void __exit smd_pkt_exit(void)
{
	smd_pkt_deinit();
}
module_exit(smd_pkt_exit);

MODULE_DESCRIPTION("MSM Shared Memory Packet Port");
MODULE_LICENSE("GPL");
