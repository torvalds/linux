/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * Bluetooth HCI virtual device driver.
 *
 * $Id: hci_vhci.c,v 1.3 2002/04/17 17:37:20 maxk Exp $ 
 */
#define VERSION "1.1"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/random.h>

#include <linux/skbuff.h>
#include <linux/miscdevice.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_vhci.h"

/* HCI device part */

static int hci_vhci_open(struct hci_dev *hdev)
{
	set_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

static int hci_vhci_flush(struct hci_dev *hdev)
{
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) hdev->driver_data;
	skb_queue_purge(&hci_vhci->readq);
	return 0;
}

static int hci_vhci_close(struct hci_dev *hdev)
{
	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	hci_vhci_flush(hdev);
	return 0;
}

static void hci_vhci_destruct(struct hci_dev *hdev)
{
	struct hci_vhci_struct *vhci;

	if (!hdev) return;

	vhci = (struct hci_vhci_struct *) hdev->driver_data;
	kfree(vhci);
}

static int hci_vhci_send_frame(struct sk_buff *skb)
{
	struct hci_dev* hdev = (struct hci_dev *) skb->dev;
	struct hci_vhci_struct *hci_vhci;

	if (!hdev) {
		BT_ERR("Frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	hci_vhci = (struct hci_vhci_struct *) hdev->driver_data;

	memcpy(skb_push(skb, 1), &skb->pkt_type, 1);
	skb_queue_tail(&hci_vhci->readq, skb);

	if (hci_vhci->flags & VHCI_FASYNC)
		kill_fasync(&hci_vhci->fasync, SIGIO, POLL_IN);
	wake_up_interruptible(&hci_vhci->read_wait);

	return 0;
}

/* Character device part */

/* Poll */
static unsigned int hci_vhci_chr_poll(struct file *file, poll_table * wait)
{  
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) file->private_data;

	poll_wait(file, &hci_vhci->read_wait, wait);
 
	if (!skb_queue_empty(&hci_vhci->readq))
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

/* Get packet from user space buffer(already verified) */
static inline ssize_t hci_vhci_get_user(struct hci_vhci_struct *hci_vhci, const char __user *buf, size_t count)
{
	struct sk_buff *skb;

	if (count > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	if (!(skb = bt_skb_alloc(count, GFP_KERNEL)))
		return -ENOMEM;
	
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	skb->dev = (void *) hci_vhci->hdev;
	skb->pkt_type = *((__u8 *) skb->data);
	skb_pull(skb, 1);

	hci_recv_frame(skb);

	return count;
} 

/* Write */
static ssize_t hci_vhci_chr_write(struct file * file, const char __user * buf, 
			     size_t count, loff_t *pos)
{
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) file->private_data;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	return hci_vhci_get_user(hci_vhci, buf, count);
}

/* Put packet to user space buffer(already verified) */
static inline ssize_t hci_vhci_put_user(struct hci_vhci_struct *hci_vhci,
				       struct sk_buff *skb, char __user *buf,
				       int count)
{
	int len = count, total = 0;
	char __user *ptr = buf;

	len = min_t(unsigned int, skb->len, len);
	if (copy_to_user(ptr, skb->data, len))
		return -EFAULT;
	total += len;

	hci_vhci->hdev->stat.byte_tx += len;
	switch (skb->pkt_type) {
		case HCI_COMMAND_PKT:
			hci_vhci->hdev->stat.cmd_tx++;
			break;

		case HCI_ACLDATA_PKT:
			hci_vhci->hdev->stat.acl_tx++;
			break;

		case HCI_SCODATA_PKT:
			hci_vhci->hdev->stat.cmd_tx++;
			break;
	};

	return total;
}

/* Read */
static ssize_t hci_vhci_chr_read(struct file * file, char __user * buf, size_t count, loff_t *pos)
{
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t ret = 0;

	add_wait_queue(&hci_vhci->read_wait, &wait);
	while (count) {
		set_current_state(TASK_INTERRUPTIBLE);

		/* Read frames from device queue */
		if (!(skb = skb_dequeue(&hci_vhci->readq))) {
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			/* Nothing to read, let's sleep */
			schedule();
			continue;
		}

		if (access_ok(VERIFY_WRITE, buf, count))
			ret = hci_vhci_put_user(hci_vhci, skb, buf, count);
		else
			ret = -EFAULT;

		kfree_skb(skb);
		break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&hci_vhci->read_wait, &wait);

	return ret;
}

static loff_t hci_vhci_chr_lseek(struct file * file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static int hci_vhci_chr_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int hci_vhci_chr_fasync(int fd, struct file *file, int on)
{
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) file->private_data;
	int ret;

	if ((ret = fasync_helper(fd, file, on, &hci_vhci->fasync)) < 0)
		return ret; 
 
	if (on)
		hci_vhci->flags |= VHCI_FASYNC;
	else 
		hci_vhci->flags &= ~VHCI_FASYNC;

	return 0;
}

static int hci_vhci_chr_open(struct inode *inode, struct file * file)
{
	struct hci_vhci_struct *hci_vhci = NULL; 
	struct hci_dev *hdev;

	if (!(hci_vhci = kmalloc(sizeof(struct hci_vhci_struct), GFP_KERNEL)))
		return -ENOMEM;

	memset(hci_vhci, 0, sizeof(struct hci_vhci_struct));

	skb_queue_head_init(&hci_vhci->readq);
	init_waitqueue_head(&hci_vhci->read_wait);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree(hci_vhci);
		return -ENOMEM;
	}

	hci_vhci->hdev = hdev;

	hdev->type = HCI_VHCI;
	hdev->driver_data = hci_vhci;

	hdev->open  = hci_vhci_open;
	hdev->close = hci_vhci_close;
	hdev->flush = hci_vhci_flush;
	hdev->send  = hci_vhci_send_frame;
	hdev->destruct = hci_vhci_destruct;

	hdev->owner = THIS_MODULE;
	
	if (hci_register_dev(hdev) < 0) {
		kfree(hci_vhci);
		hci_free_dev(hdev);
		return -EBUSY;
	}

	file->private_data = hci_vhci;
	return nonseekable_open(inode, file);   
}

static int hci_vhci_chr_close(struct inode *inode, struct file *file)
{
	struct hci_vhci_struct *hci_vhci = (struct hci_vhci_struct *) file->private_data;
	struct hci_dev *hdev = hci_vhci->hdev;

	if (hci_unregister_dev(hdev) < 0) {
		BT_ERR("Can't unregister HCI device %s", hdev->name);
	}

	hci_free_dev(hdev);

	file->private_data = NULL;
	return 0;
}

static struct file_operations hci_vhci_fops = {
	.owner	= THIS_MODULE,	
	.llseek	= hci_vhci_chr_lseek,
	.read	= hci_vhci_chr_read,
	.write	= hci_vhci_chr_write,
	.poll	= hci_vhci_chr_poll,
	.ioctl	= hci_vhci_chr_ioctl,
	.open	= hci_vhci_chr_open,
	.release	= hci_vhci_chr_close,
	.fasync	= hci_vhci_chr_fasync		
};

static struct miscdevice hci_vhci_miscdev=
{
        VHCI_MINOR,
        "hci_vhci",
        &hci_vhci_fops
};

static int __init hci_vhci_init(void)
{
	BT_INFO("VHCI driver ver %s", VERSION);

	if (misc_register(&hci_vhci_miscdev)) {
		BT_ERR("Can't register misc device %d\n", VHCI_MINOR);
		return -EIO;
	}

	return 0;
}

static void hci_vhci_cleanup(void)
{
	misc_deregister(&hci_vhci_miscdev);
}

module_init(hci_vhci_init);
module_exit(hci_vhci_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth VHCI driver ver " VERSION);
MODULE_LICENSE("GPL"); 
