/*
 * HSI character device driver, implements the character device
 * interface.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kmemleak.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/stat.h>
#include <linux/hsi/hsi.h>
#include <linux/hsi/hsi_char.h>

#define HSC_DEVS		16 /* Num of channels */
#define HSC_MSGS		4

#define HSC_RXBREAK		0

#define HSC_ID_BITS		6
#define HSC_PORT_ID_BITS	4
#define HSC_ID_MASK		3
#define HSC_PORT_ID_MASK	3
#define HSC_CH_MASK		0xf

/*
 * We support up to 4 controllers that can have up to 4
 * ports, which should currently be more than enough.
 */
#define HSC_BASEMINOR(id, port_id) \
		((((id) & HSC_ID_MASK) << HSC_ID_BITS) | \
		(((port_id) & HSC_PORT_ID_MASK) << HSC_PORT_ID_BITS))

enum {
	HSC_CH_OPEN,
	HSC_CH_READ,
	HSC_CH_WRITE,
	HSC_CH_WLINE,
};

enum {
	HSC_RX,
	HSC_TX,
};

struct hsc_client_data;
/**
 * struct hsc_channel - hsi_char internal channel data
 * @ch: channel number
 * @flags: Keeps state of the channel (open/close, reading, writing)
 * @free_msgs_list: List of free HSI messages/requests
 * @rx_msgs_queue: List of pending RX requests
 * @tx_msgs_queue: List of pending TX requests
 * @lock: Serialize access to the lists
 * @cl: reference to the associated hsi_client
 * @cl_data: reference to the client data that this channels belongs to
 * @rx_wait: RX requests wait queue
 * @tx_wait: TX requests wait queue
 */
struct hsc_channel {
	unsigned int		ch;
	unsigned long		flags;
	struct list_head	free_msgs_list;
	struct list_head	rx_msgs_queue;
	struct list_head	tx_msgs_queue;
	spinlock_t		lock;
	struct hsi_client	*cl;
	struct hsc_client_data *cl_data;
	wait_queue_head_t	rx_wait;
	wait_queue_head_t	tx_wait;
};

/**
 * struct hsc_client_data - hsi_char internal client data
 * @cdev: Characther device associated to the hsi_client
 * @lock: Lock to serialize open/close access
 * @flags: Keeps track of port state (rx hwbreak armed)
 * @usecnt: Use count for claiming the HSI port (mutex protected)
 * @cl: Referece to the HSI client
 * @channels: Array of channels accessible by the client
 */
struct hsc_client_data {
	struct cdev		cdev;
	struct mutex		lock;
	unsigned long		flags;
	unsigned int		usecnt;
	struct hsi_client	*cl;
	struct hsc_channel	channels[HSC_DEVS];
};

/* Stores the major number dynamically allocated for hsi_char */
static unsigned int hsc_major;
/* Maximum buffer size that hsi_char will accept from userspace */
static unsigned int max_data_size = 0x1000;
module_param(max_data_size, uint, 0);
MODULE_PARM_DESC(max_data_size, "max read/write data size [4,8..65536] (^2)");

static void hsc_add_tail(struct hsc_channel *channel, struct hsi_msg *msg,
							struct list_head *queue)
{
	unsigned long flags;

	spin_lock_irqsave(&channel->lock, flags);
	list_add_tail(&msg->link, queue);
	spin_unlock_irqrestore(&channel->lock, flags);
}

static struct hsi_msg *hsc_get_first_msg(struct hsc_channel *channel,
							struct list_head *queue)
{
	struct hsi_msg *msg = NULL;
	unsigned long flags;

	spin_lock_irqsave(&channel->lock, flags);

	if (list_empty(queue))
		goto out;

	msg = list_first_entry(queue, struct hsi_msg, link);
	list_del(&msg->link);
out:
	spin_unlock_irqrestore(&channel->lock, flags);

	return msg;
}

static inline void hsc_msg_free(struct hsi_msg *msg)
{
	kfree(sg_virt(msg->sgt.sgl));
	hsi_free_msg(msg);
}

static void hsc_free_list(struct list_head *list)
{
	struct hsi_msg *msg, *tmp;

	list_for_each_entry_safe(msg, tmp, list, link) {
		list_del(&msg->link);
		hsc_msg_free(msg);
	}
}

static void hsc_reset_list(struct hsc_channel *channel, struct list_head *l)
{
	unsigned long flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&channel->lock, flags);
	list_splice_init(l, &list);
	spin_unlock_irqrestore(&channel->lock, flags);

	hsc_free_list(&list);
}

static inline struct hsi_msg *hsc_msg_alloc(unsigned int alloc_size)
{
	struct hsi_msg *msg;
	void *buf;

	msg = hsi_alloc_msg(1, GFP_KERNEL);
	if (!msg)
		goto out;
	buf = kmalloc(alloc_size, GFP_KERNEL);
	if (!buf) {
		hsi_free_msg(msg);
		goto out;
	}
	sg_init_one(msg->sgt.sgl, buf, alloc_size);
	/* Ignore false positive, due to sg pointer handling */
	kmemleak_ignore(buf);

	return msg;
out:
	return NULL;
}

static inline int hsc_msgs_alloc(struct hsc_channel *channel)
{
	struct hsi_msg *msg;
	int i;

	for (i = 0; i < HSC_MSGS; i++) {
		msg = hsc_msg_alloc(max_data_size);
		if (!msg)
			goto out;
		msg->channel = channel->ch;
		list_add_tail(&msg->link, &channel->free_msgs_list);
	}

	return 0;
out:
	hsc_free_list(&channel->free_msgs_list);

	return -ENOMEM;
}

static inline unsigned int hsc_msg_len_get(struct hsi_msg *msg)
{
	return msg->sgt.sgl->length;
}

static inline void hsc_msg_len_set(struct hsi_msg *msg, unsigned int len)
{
	msg->sgt.sgl->length = len;
}

static void hsc_rx_completed(struct hsi_msg *msg)
{
	struct hsc_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsc_channel *channel = cl_data->channels + msg->channel;

	if (test_bit(HSC_CH_READ, &channel->flags)) {
		hsc_add_tail(channel, msg, &channel->rx_msgs_queue);
		wake_up(&channel->rx_wait);
	} else {
		hsc_add_tail(channel, msg, &channel->free_msgs_list);
	}
}

static void hsc_rx_msg_destructor(struct hsi_msg *msg)
{
	msg->status = HSI_STATUS_ERROR;
	hsc_msg_len_set(msg, 0);
	hsc_rx_completed(msg);
}

static void hsc_tx_completed(struct hsi_msg *msg)
{
	struct hsc_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsc_channel *channel = cl_data->channels + msg->channel;

	if (test_bit(HSC_CH_WRITE, &channel->flags)) {
		hsc_add_tail(channel, msg, &channel->tx_msgs_queue);
		wake_up(&channel->tx_wait);
	} else {
		hsc_add_tail(channel, msg, &channel->free_msgs_list);
	}
}

static void hsc_tx_msg_destructor(struct hsi_msg *msg)
{
	msg->status = HSI_STATUS_ERROR;
	hsc_msg_len_set(msg, 0);
	hsc_tx_completed(msg);
}

static void hsc_break_req_destructor(struct hsi_msg *msg)
{
	struct hsc_client_data *cl_data = hsi_client_drvdata(msg->cl);

	hsi_free_msg(msg);
	clear_bit(HSC_RXBREAK, &cl_data->flags);
}

static void hsc_break_received(struct hsi_msg *msg)
{
	struct hsc_client_data *cl_data = hsi_client_drvdata(msg->cl);
	struct hsc_channel *channel = cl_data->channels;
	int i, ret;

	/* Broadcast HWBREAK on all channels */
	for (i = 0; i < HSC_DEVS; i++, channel++) {
		struct hsi_msg *msg2;

		if (!test_bit(HSC_CH_READ, &channel->flags))
			continue;
		msg2 = hsc_get_first_msg(channel, &channel->free_msgs_list);
		if (!msg2)
			continue;
		clear_bit(HSC_CH_READ, &channel->flags);
		hsc_msg_len_set(msg2, 0);
		msg2->status = HSI_STATUS_COMPLETED;
		hsc_add_tail(channel, msg2, &channel->rx_msgs_queue);
		wake_up(&channel->rx_wait);
	}
	hsi_flush(msg->cl);
	ret = hsi_async_read(msg->cl, msg);
	if (ret < 0)
		hsc_break_req_destructor(msg);
}

static int hsc_break_request(struct hsi_client *cl)
{
	struct hsc_client_data *cl_data = hsi_client_drvdata(cl);
	struct hsi_msg *msg;
	int ret;

	if (test_and_set_bit(HSC_RXBREAK, &cl_data->flags))
		return -EBUSY;

	msg = hsi_alloc_msg(0, GFP_KERNEL);
	if (!msg) {
		clear_bit(HSC_RXBREAK, &cl_data->flags);
		return -ENOMEM;
	}
	msg->break_frame = 1;
	msg->complete = hsc_break_received;
	msg->destructor = hsc_break_req_destructor;
	ret = hsi_async_read(cl, msg);
	if (ret < 0)
		hsc_break_req_destructor(msg);

	return ret;
}

static int hsc_break_send(struct hsi_client *cl)
{
	struct hsi_msg *msg;
	int ret;

	msg = hsi_alloc_msg(0, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;
	msg->break_frame = 1;
	msg->complete = hsi_free_msg;
	msg->destructor = hsi_free_msg;
	ret = hsi_async_write(cl, msg);
	if (ret < 0)
		hsi_free_msg(msg);

	return ret;
}

static int hsc_rx_set(struct hsi_client *cl, struct hsc_rx_config *rxc)
{
	struct hsi_config tmp;
	int ret;

	if ((rxc->mode != HSI_MODE_STREAM) && (rxc->mode != HSI_MODE_FRAME))
		return -EINVAL;
	if ((rxc->channels == 0) || (rxc->channels > HSC_DEVS))
		return -EINVAL;
	if (rxc->channels & (rxc->channels - 1))
		return -EINVAL;
	if ((rxc->flow != HSI_FLOW_SYNC) && (rxc->flow != HSI_FLOW_PIPE))
		return -EINVAL;
	tmp = cl->rx_cfg;
	cl->rx_cfg.mode = rxc->mode;
	cl->rx_cfg.channels = rxc->channels;
	cl->rx_cfg.flow = rxc->flow;
	ret = hsi_setup(cl);
	if (ret < 0) {
		cl->rx_cfg = tmp;
		return ret;
	}
	if (rxc->mode == HSI_MODE_FRAME)
		hsc_break_request(cl);

	return ret;
}

static inline void hsc_rx_get(struct hsi_client *cl, struct hsc_rx_config *rxc)
{
	rxc->mode = cl->rx_cfg.mode;
	rxc->channels = cl->rx_cfg.channels;
	rxc->flow = cl->rx_cfg.flow;
}

static int hsc_tx_set(struct hsi_client *cl, struct hsc_tx_config *txc)
{
	struct hsi_config tmp;
	int ret;

	if ((txc->mode != HSI_MODE_STREAM) && (txc->mode != HSI_MODE_FRAME))
		return -EINVAL;
	if ((txc->channels == 0) || (txc->channels > HSC_DEVS))
		return -EINVAL;
	if (txc->channels & (txc->channels - 1))
		return -EINVAL;
	if ((txc->arb_mode != HSI_ARB_RR) && (txc->arb_mode != HSI_ARB_PRIO))
		return -EINVAL;
	tmp = cl->tx_cfg;
	cl->tx_cfg.mode = txc->mode;
	cl->tx_cfg.channels = txc->channels;
	cl->tx_cfg.speed = txc->speed;
	cl->tx_cfg.arb_mode = txc->arb_mode;
	ret = hsi_setup(cl);
	if (ret < 0) {
		cl->tx_cfg = tmp;
		return ret;
	}

	return ret;
}

static inline void hsc_tx_get(struct hsi_client *cl, struct hsc_tx_config *txc)
{
	txc->mode = cl->tx_cfg.mode;
	txc->channels = cl->tx_cfg.channels;
	txc->speed = cl->tx_cfg.speed;
	txc->arb_mode = cl->tx_cfg.arb_mode;
}

static ssize_t hsc_read(struct file *file, char __user *buf, size_t len,
						loff_t *ppos __maybe_unused)
{
	struct hsc_channel *channel = file->private_data;
	struct hsi_msg *msg;
	ssize_t ret;

	if (len == 0)
		return 0;
	if (!IS_ALIGNED(len, sizeof(u32)))
		return -EINVAL;
	if (len > max_data_size)
		len = max_data_size;
	if (channel->ch >= channel->cl->rx_cfg.channels)
		return -ECHRNG;
	if (test_and_set_bit(HSC_CH_READ, &channel->flags))
		return -EBUSY;
	msg = hsc_get_first_msg(channel, &channel->free_msgs_list);
	if (!msg) {
		ret = -ENOSPC;
		goto out;
	}
	hsc_msg_len_set(msg, len);
	msg->complete = hsc_rx_completed;
	msg->destructor = hsc_rx_msg_destructor;
	ret = hsi_async_read(channel->cl, msg);
	if (ret < 0) {
		hsc_add_tail(channel, msg, &channel->free_msgs_list);
		goto out;
	}

	ret = wait_event_interruptible(channel->rx_wait,
					!list_empty(&channel->rx_msgs_queue));
	if (ret < 0) {
		clear_bit(HSC_CH_READ, &channel->flags);
		hsi_flush(channel->cl);
		return -EINTR;
	}

	msg = hsc_get_first_msg(channel, &channel->rx_msgs_queue);
	if (msg) {
		if (msg->status != HSI_STATUS_ERROR) {
			ret = copy_to_user((void __user *)buf,
			sg_virt(msg->sgt.sgl), hsc_msg_len_get(msg));
			if (ret)
				ret = -EFAULT;
			else
				ret = hsc_msg_len_get(msg);
		} else {
			ret = -EIO;
		}
		hsc_add_tail(channel, msg, &channel->free_msgs_list);
	}
out:
	clear_bit(HSC_CH_READ, &channel->flags);

	return ret;
}

static ssize_t hsc_write(struct file *file, const char __user *buf, size_t len,
						loff_t *ppos __maybe_unused)
{
	struct hsc_channel *channel = file->private_data;
	struct hsi_msg *msg;
	ssize_t ret;

	if ((len == 0) || !IS_ALIGNED(len, sizeof(u32)))
		return -EINVAL;
	if (len > max_data_size)
		len = max_data_size;
	if (channel->ch >= channel->cl->tx_cfg.channels)
		return -ECHRNG;
	if (test_and_set_bit(HSC_CH_WRITE, &channel->flags))
		return -EBUSY;
	msg = hsc_get_first_msg(channel, &channel->free_msgs_list);
	if (!msg) {
		clear_bit(HSC_CH_WRITE, &channel->flags);
		return -ENOSPC;
	}
	if (copy_from_user(sg_virt(msg->sgt.sgl), (void __user *)buf, len)) {
		ret = -EFAULT;
		goto out;
	}
	hsc_msg_len_set(msg, len);
	msg->complete = hsc_tx_completed;
	msg->destructor = hsc_tx_msg_destructor;
	ret = hsi_async_write(channel->cl, msg);
	if (ret < 0)
		goto out;

	ret = wait_event_interruptible(channel->tx_wait,
					!list_empty(&channel->tx_msgs_queue));
	if (ret < 0) {
		clear_bit(HSC_CH_WRITE, &channel->flags);
		hsi_flush(channel->cl);
		return -EINTR;
	}

	msg = hsc_get_first_msg(channel, &channel->tx_msgs_queue);
	if (msg) {
		if (msg->status == HSI_STATUS_ERROR)
			ret = -EIO;
		else
			ret = hsc_msg_len_get(msg);

		hsc_add_tail(channel, msg, &channel->free_msgs_list);
	}
out:
	clear_bit(HSC_CH_WRITE, &channel->flags);

	return ret;
}

static long hsc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hsc_channel *channel = file->private_data;
	unsigned int state;
	struct hsc_rx_config rxc;
	struct hsc_tx_config txc;
	long ret = 0;

	switch (cmd) {
	case HSC_RESET:
		hsi_flush(channel->cl);
		break;
	case HSC_SET_PM:
		if (copy_from_user(&state, (void __user *)arg, sizeof(state)))
			return -EFAULT;
		if (state == HSC_PM_DISABLE) {
			if (test_and_set_bit(HSC_CH_WLINE, &channel->flags))
				return -EINVAL;
			ret = hsi_start_tx(channel->cl);
		} else if (state == HSC_PM_ENABLE) {
			if (!test_and_clear_bit(HSC_CH_WLINE, &channel->flags))
				return -EINVAL;
			ret = hsi_stop_tx(channel->cl);
		} else {
			ret = -EINVAL;
		}
		break;
	case HSC_SEND_BREAK:
		return hsc_break_send(channel->cl);
	case HSC_SET_RX:
		if (copy_from_user(&rxc, (void __user *)arg, sizeof(rxc)))
			return -EFAULT;
		return hsc_rx_set(channel->cl, &rxc);
	case HSC_GET_RX:
		hsc_rx_get(channel->cl, &rxc);
		if (copy_to_user((void __user *)arg, &rxc, sizeof(rxc)))
			return -EFAULT;
		break;
	case HSC_SET_TX:
		if (copy_from_user(&txc, (void __user *)arg, sizeof(txc)))
			return -EFAULT;
		return hsc_tx_set(channel->cl, &txc);
	case HSC_GET_TX:
		hsc_tx_get(channel->cl, &txc);
		if (copy_to_user((void __user *)arg, &txc, sizeof(txc)))
			return -EFAULT;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static inline void __hsc_port_release(struct hsc_client_data *cl_data)
{
	BUG_ON(cl_data->usecnt == 0);

	if (--cl_data->usecnt == 0) {
		hsi_flush(cl_data->cl);
		hsi_release_port(cl_data->cl);
	}
}

static int hsc_open(struct inode *inode, struct file *file)
{
	struct hsc_client_data *cl_data;
	struct hsc_channel *channel;
	int ret = 0;

	pr_debug("open, minor = %d\n", iminor(inode));

	cl_data = container_of(inode->i_cdev, struct hsc_client_data, cdev);
	mutex_lock(&cl_data->lock);
	channel = cl_data->channels + (iminor(inode) & HSC_CH_MASK);

	if (test_and_set_bit(HSC_CH_OPEN, &channel->flags)) {
		ret = -EBUSY;
		goto out;
	}
	/*
	 * Check if we have already claimed the port associated to the HSI
	 * client. If not then try to claim it, else increase its refcount
	 */
	if (cl_data->usecnt == 0) {
		ret = hsi_claim_port(cl_data->cl, 0);
		if (ret < 0)
			goto out;
		hsi_setup(cl_data->cl);
	}
	cl_data->usecnt++;

	ret = hsc_msgs_alloc(channel);
	if (ret < 0) {
		__hsc_port_release(cl_data);
		goto out;
	}

	file->private_data = channel;
	mutex_unlock(&cl_data->lock);

	return ret;
out:
	mutex_unlock(&cl_data->lock);

	return ret;
}

static int hsc_release(struct inode *inode __maybe_unused, struct file *file)
{
	struct hsc_channel *channel = file->private_data;
	struct hsc_client_data *cl_data = channel->cl_data;

	mutex_lock(&cl_data->lock);
	file->private_data = NULL;
	if (test_and_clear_bit(HSC_CH_WLINE, &channel->flags))
		hsi_stop_tx(channel->cl);
	__hsc_port_release(cl_data);
	hsc_reset_list(channel, &channel->rx_msgs_queue);
	hsc_reset_list(channel, &channel->tx_msgs_queue);
	hsc_reset_list(channel, &channel->free_msgs_list);
	clear_bit(HSC_CH_READ, &channel->flags);
	clear_bit(HSC_CH_WRITE, &channel->flags);
	clear_bit(HSC_CH_OPEN, &channel->flags);
	wake_up(&channel->rx_wait);
	wake_up(&channel->tx_wait);
	mutex_unlock(&cl_data->lock);

	return 0;
}

static const struct file_operations hsc_fops = {
	.owner		= THIS_MODULE,
	.read		= hsc_read,
	.write		= hsc_write,
	.unlocked_ioctl	= hsc_ioctl,
	.open		= hsc_open,
	.release	= hsc_release,
};

static void hsc_channel_init(struct hsc_channel *channel)
{
	init_waitqueue_head(&channel->rx_wait);
	init_waitqueue_head(&channel->tx_wait);
	spin_lock_init(&channel->lock);
	INIT_LIST_HEAD(&channel->free_msgs_list);
	INIT_LIST_HEAD(&channel->rx_msgs_queue);
	INIT_LIST_HEAD(&channel->tx_msgs_queue);
}

static int hsc_probe(struct device *dev)
{
	const char devname[] = "hsi_char";
	struct hsc_client_data *cl_data;
	struct hsc_channel *channel;
	struct hsi_client *cl = to_hsi_client(dev);
	unsigned int hsc_baseminor;
	dev_t hsc_dev;
	int ret;
	int i;

	cl_data = kzalloc(sizeof(*cl_data), GFP_KERNEL);
	if (!cl_data) {
		dev_err(dev, "Could not allocate hsc_client_data\n");
		return -ENOMEM;
	}
	hsc_baseminor = HSC_BASEMINOR(hsi_id(cl), hsi_port_id(cl));
	if (!hsc_major) {
		ret = alloc_chrdev_region(&hsc_dev, hsc_baseminor,
						HSC_DEVS, devname);
		if (ret > 0)
			hsc_major = MAJOR(hsc_dev);
	} else {
		hsc_dev = MKDEV(hsc_major, hsc_baseminor);
		ret = register_chrdev_region(hsc_dev, HSC_DEVS, devname);
	}
	if (ret < 0) {
		dev_err(dev, "Device %s allocation failed %d\n",
					hsc_major ? "minor" : "major", ret);
		goto out1;
	}
	mutex_init(&cl_data->lock);
	hsi_client_set_drvdata(cl, cl_data);
	cdev_init(&cl_data->cdev, &hsc_fops);
	cl_data->cdev.owner = THIS_MODULE;
	cl_data->cl = cl;
	for (i = 0, channel = cl_data->channels; i < HSC_DEVS; i++, channel++) {
		hsc_channel_init(channel);
		channel->ch = i;
		channel->cl = cl;
		channel->cl_data = cl_data;
	}

	/* 1 hsi client -> N char devices (one for each channel) */
	ret = cdev_add(&cl_data->cdev, hsc_dev, HSC_DEVS);
	if (ret) {
		dev_err(dev, "Could not add char device %d\n", ret);
		goto out2;
	}

	return 0;
out2:
	unregister_chrdev_region(hsc_dev, HSC_DEVS);
out1:
	kfree(cl_data);

	return ret;
}

static int hsc_remove(struct device *dev)
{
	struct hsi_client *cl = to_hsi_client(dev);
	struct hsc_client_data *cl_data = hsi_client_drvdata(cl);
	dev_t hsc_dev = cl_data->cdev.dev;

	cdev_del(&cl_data->cdev);
	unregister_chrdev_region(hsc_dev, HSC_DEVS);
	hsi_client_set_drvdata(cl, NULL);
	kfree(cl_data);

	return 0;
}

static struct hsi_client_driver hsc_driver = {
	.driver = {
		.name	= "hsi_char",
		.owner	= THIS_MODULE,
		.probe	= hsc_probe,
		.remove	= hsc_remove,
	},
};

static int __init hsc_init(void)
{
	int ret;

	if ((max_data_size < 4) || (max_data_size > 0x10000) ||
		(max_data_size & (max_data_size - 1))) {
		pr_err("Invalid max read/write data size");
		return -EINVAL;
	}

	ret = hsi_register_client_driver(&hsc_driver);
	if (ret) {
		pr_err("Error while registering HSI/SSI driver %d", ret);
		return ret;
	}

	pr_info("HSI/SSI char device loaded\n");

	return 0;
}
module_init(hsc_init);

static void __exit hsc_exit(void)
{
	hsi_unregister_client_driver(&hsc_driver);
	pr_info("HSI char device removed\n");
}
module_exit(hsc_exit);

MODULE_AUTHOR("Andras Domokos <andras.domokos@nokia.com>");
MODULE_ALIAS("hsi:hsi_char");
MODULE_DESCRIPTION("HSI character device");
MODULE_LICENSE("GPL v2");
