/*
 *
 *  Bluetooth virtual HCI driver
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2006  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <asm/unaligned.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include <linux/skbuff.h>
#include <linux/miscdevice.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#define VERSION "1.5"

static bool amp;

struct vhci_data {
	struct hci_dev *hdev;

	wait_queue_head_t read_wait;
	struct sk_buff_head readq;

	struct mutex open_mutex;
	struct delayed_work open_timeout;
};

static int vhci_open_dev(struct hci_dev *hdev)
{
	return 0;
}

static int vhci_close_dev(struct hci_dev *hdev)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	skb_queue_purge(&data->readq);

	return 0;
}

static int vhci_flush(struct hci_dev *hdev)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	skb_queue_purge(&data->readq);

	return 0;
}

static int vhci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&data->readq, skb);

	wake_up_interruptible(&data->read_wait);
	return 0;
}

static int __vhci_create_device(struct vhci_data *data, __u8 opcode)
{
	struct hci_dev *hdev;
	struct sk_buff *skb;
	__u8 dev_type;

	if (data->hdev)
		return -EBADFD;

	/* bits 0-1 are dev_type (Primary or AMP) */
	dev_type = opcode & 0x03;

	if (dev_type != HCI_PRIMARY && dev_type != HCI_AMP)
		return -EINVAL;

	/* bits 2-5 are reserved (must be zero) */
	if (opcode & 0x3c)
		return -EINVAL;

	skb = bt_skb_alloc(4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	data->hdev = hdev;

	hdev->bus = HCI_VIRTUAL;
	hdev->dev_type = dev_type;
	hci_set_drvdata(hdev, data);

	hdev->open  = vhci_open_dev;
	hdev->close = vhci_close_dev;
	hdev->flush = vhci_flush;
	hdev->send  = vhci_send_frame;

	/* bit 6 is for external configuration */
	if (opcode & 0x40)
		set_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks);

	/* bit 7 is for raw device */
	if (opcode & 0x80)
		set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		data->hdev = NULL;
		kfree_skb(skb);
		return -EBUSY;
	}

	hci_skb_pkt_type(skb) = HCI_VENDOR_PKT;

	skb_put_u8(skb, 0xff);
	skb_put_u8(skb, opcode);
	put_unaligned_le16(hdev->id, skb_put(skb, 2));
	skb_queue_tail(&data->readq, skb);

	wake_up_interruptible(&data->read_wait);
	return 0;
}

static int vhci_create_device(struct vhci_data *data, __u8 opcode)
{
	int err;

	mutex_lock(&data->open_mutex);
	err = __vhci_create_device(data, opcode);
	mutex_unlock(&data->open_mutex);

	return err;
}

static inline ssize_t vhci_get_user(struct vhci_data *data,
				    struct iov_iter *from)
{
	size_t len = iov_iter_count(from);
	struct sk_buff *skb;
	__u8 pkt_type, opcode;
	int ret;

	if (len < 2 || len > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (!copy_from_iter_full(skb_put(skb, len), len, from)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	pkt_type = *((__u8 *) skb->data);
	skb_pull(skb, 1);

	switch (pkt_type) {
	case HCI_EVENT_PKT:
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
		if (!data->hdev) {
			kfree_skb(skb);
			return -ENODEV;
		}

		hci_skb_pkt_type(skb) = pkt_type;

		ret = hci_recv_frame(data->hdev, skb);
		break;

	case HCI_VENDOR_PKT:
		cancel_delayed_work_sync(&data->open_timeout);

		opcode = *((__u8 *) skb->data);
		skb_pull(skb, 1);

		if (skb->len > 0) {
			kfree_skb(skb);
			return -EINVAL;
		}

		kfree_skb(skb);

		ret = vhci_create_device(data, opcode);
		break;

	default:
		kfree_skb(skb);
		return -EINVAL;
	}

	return (ret < 0) ? ret : len;
}

static inline ssize_t vhci_put_user(struct vhci_data *data,
				    struct sk_buff *skb,
				    char __user *buf, int count)
{
	char __user *ptr = buf;
	int len;

	len = min_t(unsigned int, skb->len, count);

	if (copy_to_user(ptr, skb->data, len))
		return -EFAULT;

	if (!data->hdev)
		return len;

	data->hdev->stat.byte_tx += len;

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		data->hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		data->hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		data->hdev->stat.sco_tx++;
		break;
	}

	return len;
}

static ssize_t vhci_read(struct file *file,
			 char __user *buf, size_t count, loff_t *pos)
{
	struct vhci_data *data = file->private_data;
	struct sk_buff *skb;
	ssize_t ret = 0;

	while (count) {
		skb = skb_dequeue(&data->readq);
		if (skb) {
			ret = vhci_put_user(data, skb, buf, count);
			if (ret < 0)
				skb_queue_head(&data->readq, skb);
			else
				kfree_skb(skb);
			break;
		}

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		ret = wait_event_interruptible(data->read_wait,
					       !skb_queue_empty(&data->readq));
		if (ret < 0)
			break;
	}

	return ret;
}

static ssize_t vhci_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct vhci_data *data = file->private_data;

	return vhci_get_user(data, from);
}

static __poll_t vhci_poll(struct file *file, poll_table *wait)
{
	struct vhci_data *data = file->private_data;

	poll_wait(file, &data->read_wait, wait);

	if (!skb_queue_empty(&data->readq))
		return EPOLLIN | EPOLLRDNORM;

	return EPOLLOUT | EPOLLWRNORM;
}

static void vhci_open_timeout(struct work_struct *work)
{
	struct vhci_data *data = container_of(work, struct vhci_data,
					      open_timeout.work);

	vhci_create_device(data, amp ? HCI_AMP : HCI_PRIMARY);
}

static int vhci_open(struct inode *inode, struct file *file)
{
	struct vhci_data *data;

	data = kzalloc(sizeof(struct vhci_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	skb_queue_head_init(&data->readq);
	init_waitqueue_head(&data->read_wait);

	mutex_init(&data->open_mutex);
	INIT_DELAYED_WORK(&data->open_timeout, vhci_open_timeout);

	file->private_data = data;
	nonseekable_open(inode, file);

	schedule_delayed_work(&data->open_timeout, msecs_to_jiffies(1000));

	return 0;
}

static int vhci_release(struct inode *inode, struct file *file)
{
	struct vhci_data *data = file->private_data;
	struct hci_dev *hdev;

	cancel_delayed_work_sync(&data->open_timeout);

	hdev = data->hdev;

	if (hdev) {
		hci_unregister_dev(hdev);
		hci_free_dev(hdev);
	}

	skb_queue_purge(&data->readq);
	file->private_data = NULL;
	kfree(data);

	return 0;
}

static const struct file_operations vhci_fops = {
	.owner		= THIS_MODULE,
	.read		= vhci_read,
	.write_iter	= vhci_write,
	.poll		= vhci_poll,
	.open		= vhci_open,
	.release	= vhci_release,
	.llseek		= no_llseek,
};

static struct miscdevice vhci_miscdev = {
	.name	= "vhci",
	.fops	= &vhci_fops,
	.minor	= VHCI_MINOR,
};
module_misc_device(vhci_miscdev);

module_param(amp, bool, 0644);
MODULE_PARM_DESC(amp, "Create AMP controller device");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth virtual HCI driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("devname:vhci");
MODULE_ALIAS_MISCDEV(VHCI_MINOR);
