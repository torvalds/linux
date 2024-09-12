// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtual NCI device simulation driver
 *
 * Copyright (C) 2020 Samsung Electrnoics
 * Bongsu Jeon <bongsu.jeon@samsung.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <net/nfc/nci_core.h>

enum virtual_ncidev_mode {
	virtual_ncidev_enabled,
	virtual_ncidev_disabled,
	virtual_ncidev_disabling,
};

#define IOCTL_GET_NCIDEV_IDX    0
#define VIRTUAL_NFC_PROTOCOLS	(NFC_PROTO_JEWEL_MASK | \
				 NFC_PROTO_MIFARE_MASK | \
				 NFC_PROTO_FELICA_MASK | \
				 NFC_PROTO_ISO14443_MASK | \
				 NFC_PROTO_ISO14443_B_MASK | \
				 NFC_PROTO_ISO15693_MASK)

static enum virtual_ncidev_mode state;
static DECLARE_WAIT_QUEUE_HEAD(wq);
static struct miscdevice miscdev;
static struct sk_buff *send_buff;
static struct nci_dev *ndev;
static DEFINE_MUTEX(nci_mutex);

static int virtual_nci_open(struct nci_dev *ndev)
{
	return 0;
}

static int virtual_nci_close(struct nci_dev *ndev)
{
	mutex_lock(&nci_mutex);
	kfree_skb(send_buff);
	send_buff = NULL;
	mutex_unlock(&nci_mutex);

	return 0;
}

static int virtual_nci_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	mutex_lock(&nci_mutex);
	if (state != virtual_ncidev_enabled) {
		mutex_unlock(&nci_mutex);
		kfree_skb(skb);
		return 0;
	}

	if (send_buff) {
		mutex_unlock(&nci_mutex);
		kfree_skb(skb);
		return -1;
	}
	send_buff = skb_copy(skb, GFP_KERNEL);
	mutex_unlock(&nci_mutex);
	wake_up_interruptible(&wq);
	consume_skb(skb);

	return 0;
}

static const struct nci_ops virtual_nci_ops = {
	.open = virtual_nci_open,
	.close = virtual_nci_close,
	.send = virtual_nci_send
};

static ssize_t virtual_ncidev_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	size_t actual_len;

	mutex_lock(&nci_mutex);
	while (!send_buff) {
		mutex_unlock(&nci_mutex);
		if (wait_event_interruptible(wq, send_buff))
			return -EFAULT;
		mutex_lock(&nci_mutex);
	}

	actual_len = min_t(size_t, count, send_buff->len);

	if (copy_to_user(buf, send_buff->data, actual_len)) {
		mutex_unlock(&nci_mutex);
		return -EFAULT;
	}

	skb_pull(send_buff, actual_len);
	if (send_buff->len == 0) {
		consume_skb(send_buff);
		send_buff = NULL;
	}
	mutex_unlock(&nci_mutex);

	return actual_len;
}

static ssize_t virtual_ncidev_write(struct file *file,
				    const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct sk_buff *skb;

	skb = alloc_skb(count, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	if (strnlen(skb->data, count) != count) {
		kfree_skb(skb);
		return -EINVAL;
	}

	nci_recv_frame(ndev, skb);
	return count;
}

static int virtual_ncidev_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&nci_mutex);
	if (state != virtual_ncidev_disabled) {
		mutex_unlock(&nci_mutex);
		return -EBUSY;
	}

	ndev = nci_allocate_device(&virtual_nci_ops, VIRTUAL_NFC_PROTOCOLS,
				   0, 0);
	if (!ndev) {
		mutex_unlock(&nci_mutex);
		return -ENOMEM;
	}

	ret = nci_register_device(ndev);
	if (ret < 0) {
		nci_free_device(ndev);
		mutex_unlock(&nci_mutex);
		return ret;
	}
	state = virtual_ncidev_enabled;
	mutex_unlock(&nci_mutex);

	return 0;
}

static int virtual_ncidev_close(struct inode *inode, struct file *file)
{
	mutex_lock(&nci_mutex);

	if (state == virtual_ncidev_enabled) {
		state = virtual_ncidev_disabling;
		mutex_unlock(&nci_mutex);

		nci_unregister_device(ndev);
		nci_free_device(ndev);

		mutex_lock(&nci_mutex);
	}

	state = virtual_ncidev_disabled;
	mutex_unlock(&nci_mutex);

	return 0;
}

static long virtual_ncidev_ioctl(struct file *flip, unsigned int cmd,
				 unsigned long arg)
{
	const struct nfc_dev *nfc_dev = ndev->nfc_dev;
	void __user *p = (void __user *)arg;

	if (cmd != IOCTL_GET_NCIDEV_IDX)
		return -ENOTTY;

	if (copy_to_user(p, &nfc_dev->idx, sizeof(nfc_dev->idx)))
		return -EFAULT;

	return 0;
}

static const struct file_operations virtual_ncidev_fops = {
	.owner = THIS_MODULE,
	.read = virtual_ncidev_read,
	.write = virtual_ncidev_write,
	.open = virtual_ncidev_open,
	.release = virtual_ncidev_close,
	.unlocked_ioctl = virtual_ncidev_ioctl
};

static int __init virtual_ncidev_init(void)
{
	state = virtual_ncidev_disabled;
	miscdev.minor = MISC_DYNAMIC_MINOR;
	miscdev.name = "virtual_nci";
	miscdev.fops = &virtual_ncidev_fops;
	miscdev.mode = 0600;

	return misc_register(&miscdev);
}

static void __exit virtual_ncidev_exit(void)
{
	misc_deregister(&miscdev);
}

module_init(virtual_ncidev_init);
module_exit(virtual_ncidev_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual NCI device simulation driver");
MODULE_AUTHOR("Bongsu Jeon <bongsu.jeon@samsung.com>");
