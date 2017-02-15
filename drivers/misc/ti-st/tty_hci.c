/*
 *  TTY emulation for user-space Bluetooth stacks over HCI-H4
 *  Copyright (C) 2011-2012 Texas Instruments
 *  Author: Pavan Savoy <pavan_savoy@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/** define one of the following for debugging
#define DEBUG
#define VERBOSE
*/

#define pr_fmt(fmt) "(hci_tty): " fmt
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>

#include <linux/uaccess.h>
#include <linux/tty.h>
#include <linux/sched.h>

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>

#include <linux/ti_wilink_st.h>

/* Number of seconds to wait for registration completion
 * when ST returns PENDING status.
 */
#define BT_REGISTER_TIMEOUT   6000	/* 6 sec */

/**
 * struct ti_st - driver operation structure
 * @hdev: hci device pointer which binds to bt driver
 * @reg_status: ST registration callback status
 * @st_write: write function provided by the ST driver
 *	to be used by the driver during send_frame.
 * @wait_reg_completion - completion sync between ti_st_open
 *	and st_reg_completion_cb.
 */
struct ti_st {
	struct hci_dev *hdev;
	char reg_status;
	long (*st_write)(struct sk_buff *);
	struct completion wait_reg_completion;
	wait_queue_head_t data_q;
	struct sk_buff_head rx_list;
};

#define DEVICE_NAME     "hci_tty"

/***********Functions called from ST driver**********************************/
/* Called by Shared Transport layer when receive data is
 * available */
static long st_receive(void *priv_data, struct sk_buff *skb)
{
	struct ti_st	*hst = (void *)priv_data;

	pr_debug("@ %s", __func__);
#ifdef VERBOSE
	print_hex_dump(KERN_INFO, ">rx>", DUMP_PREFIX_NONE,
		       16, 1, skb->data, skb->len, 0);
#endif
	skb_queue_tail(&hst->rx_list, skb);
	wake_up_interruptible(&hst->data_q);
	return 0;
}

/* Called by ST layer to indicate protocol registration completion
 * status.ti_st_open() function will wait for signal from this
 * API when st_register() function returns ST_PENDING.
 */
static void st_reg_completion_cb(void *priv_data, int data)
{
	struct ti_st	*lhst = (void *)priv_data;

	pr_info("@ %s\n", __func__);
	/* Save registration status for use in ti_st_open() */
	lhst->reg_status = data;
	/* complete the wait in ti_st_open() */
	complete(&lhst->wait_reg_completion);
}

/* protocol structure registered with shared transport */
#define MAX_BT_CHNL_IDS 3
static struct st_proto_s ti_st_proto[MAX_BT_CHNL_IDS] = {
	{
		.chnl_id = 0x04, /* HCI Events */
		.hdr_len = 2,
		.offset_len_in_hdr = 1,
		.len_size = 1, /* sizeof(plen) in struct hci_event_hdr */
		.reserve = 8,
	},
	{
		.chnl_id = 0x02, /* ACL */
		.hdr_len = 4,
		.offset_len_in_hdr = 2,
		.len_size = 2,	/* sizeof(dlen) in struct hci_acl_hdr */
		.reserve = 8,
	},
	{
		.chnl_id = 0x03, /* SCO */
		.hdr_len = 3,
		.offset_len_in_hdr = 2,
		.len_size = 1, /* sizeof(dlen) in struct hci_sco_hdr */
		.reserve = 8,
	},
};
/** hci_tty_open Function
 *  This function will perform an register on ST driver.
 *
 *  Parameters :
 *  @file  : File pointer for BT char driver
 *  @inod  :
 *  Returns  0 -  on success
 *           else suitable error code
 */
int hci_tty_open(struct inode *inod, struct file *file)
{
	int i = 0, err = 0;
	unsigned long timeleft;
	struct ti_st *hst;

	pr_info("inside %s (%p, %p)\n", __func__, inod, file);

	hst = kzalloc(sizeof(*hst), GFP_KERNEL);
	file->private_data = hst;
	hst = file->private_data;

	for (i = 0; i < MAX_BT_CHNL_IDS; i++) {
		ti_st_proto[i].priv_data = hst;
		ti_st_proto[i].max_frame_size = 1026;
		ti_st_proto[i].recv = st_receive;
		ti_st_proto[i].reg_complete_cb = st_reg_completion_cb;

		/* Prepare wait-for-completion handler */
		init_completion(&hst->wait_reg_completion);
		/* Reset ST registration callback status flag,
		 * this value will be updated in
		 * st_reg_completion_cb()
		 * function whenever it called from ST driver.
		 */
		hst->reg_status = -EINPROGRESS;

		err = st_register(&ti_st_proto[i]);
		if (!err)
			goto done;

		if (err != -EINPROGRESS) {
			pr_err("st_register failed %d", err);
			goto error;
		}

		/* ST is busy with either protocol
		 * registration or firmware download.
		 */
		pr_debug("waiting for registration completion signal from ST");
		timeleft = wait_for_completion_timeout
			(&hst->wait_reg_completion,
			 msecs_to_jiffies(BT_REGISTER_TIMEOUT));
		if (!timeleft) {
			pr_err("Timeout(%d sec),didn't get reg completion signal from ST",
			       BT_REGISTER_TIMEOUT / 1000);
			err = -ETIMEDOUT;
			goto error;
		}

		/* Is ST registration callback
		 * called with ERROR status? */
		if (hst->reg_status != 0) {
			pr_err("ST registration completed with invalid status %d",
			       hst->reg_status);
			err = -EAGAIN;
			goto error;
		}

done:
		hst->st_write = ti_st_proto[i].write;
		if (!hst->st_write) {
			pr_err("undefined ST write function");
			for (i = 0; i < MAX_BT_CHNL_IDS; i++) {
				/* Undo registration with ST */
				err = st_unregister(&ti_st_proto[i]);
				if (err)
					pr_err("st_unregister() failed with error %d",
					       err);
				hst->st_write = NULL;
			}
			return -EIO;
		}
	}

	skb_queue_head_init(&hst->rx_list);
	init_waitqueue_head(&hst->data_q);

	return 0;

error:
	kfree(hst);
	return err;
}

/** hci_tty_release Function
 *  This function will un-registers from the ST driver.
 *
 *  Parameters :
 *  @file  : File pointer for BT char driver
 *  @inod  :
 *  Returns  0 -  on success
 *           else suitable error code
 */
int hci_tty_release(struct inode *inod, struct file *file)
{
	int err, i;
	struct ti_st *hst = file->private_data;

	pr_info("inside %s (%p, %p)\n", __func__, inod, file);

	for (i = 0; i < MAX_BT_CHNL_IDS; i++) {
		err = st_unregister(&ti_st_proto[i]);
		if (err)
			pr_err("st_unregister(%d) failed with error %d",
			       ti_st_proto[i].chnl_id, err);
	}

	hst->st_write = NULL;
	skb_queue_purge(&hst->rx_list);
	kfree(hst);
	return err;
}

/** hci_tty_read Function
 *
 *  Parameters :
 *  @file  : File pointer for BT char driver
 *  @data  : Data which needs to be passed to APP
 *  @size  : Length of the data passesd
 *  offset :
 *  Returns  Size of packet received -  on success
 *           else suitable error code
 */
ssize_t hci_tty_read(struct file *file, char __user *data, size_t size,
		loff_t *offset)
{
	int len = 0, tout;
	struct sk_buff *skb = NULL, *rskb = NULL;
	struct ti_st	*hst;

	pr_debug("inside %s (%p, %p, %zu, %p)\n",
		 __func__, file, data, size, offset);

	/* Validate input parameters */
	if ((NULL == file) || (((NULL == data) || (0 == size)))) {
		pr_err("Invalid input params passed to %s", __func__);
		return -EINVAL;
	}

	hst = file->private_data;

	/* cannot come here if poll-ed before reading
	 * if not poll-ed wait on the same wait_q
	 */
	tout = wait_event_interruptible_timeout(hst->data_q,
			!skb_queue_empty(&hst->rx_list),
				msecs_to_jiffies(1000));
	/* Check for timed out condition */
	if (0 == tout) {
		pr_err("Device Read timed out\n");
		return -ETIMEDOUT;
	}

	/* hst->rx_list not empty skb already present */
	skb = skb_dequeue(&hst->rx_list);
	if (!skb) {
		pr_err("dequed skb is null?\n");
		return -EIO;
	}

#ifdef VERBOSE
	print_hex_dump(KERN_INFO, ">in>", DUMP_PREFIX_NONE,
		       16, 1, skb->data, skb->len, 0);
#endif

	/* Forward the data to the user */
	if (skb->len >= size) {
		pr_err("FIONREAD not done before read\n");
		return -ENOMEM;
	} else {
		/* returning skb */
		rskb = alloc_skb(size, GFP_KERNEL);
		if (!rskb) {
			pr_err("alloc_skb error\n");
			return -ENOMEM;
		}

		/* cb[0] has the pkt_type 0x04 or 0x02 or 0x03 */
		memcpy(skb_put(rskb, 1), &skb->cb[0], 1);
		memcpy(skb_put(rskb, skb->len), skb->data, skb->len);

		if (copy_to_user(data, rskb->data, rskb->len)) {
			pr_err("unable to copy to user space\n");
			/* Queue the skb back to head */
			skb_queue_head(&hst->rx_list, skb);
			kfree_skb(rskb);
			return -EIO;
		}
	}

	len = rskb->len;	/* len of returning skb */
	kfree_skb(skb);
	kfree_skb(rskb);
	pr_debug("total size read= %d\n", len);
	return len;
}

/* hci_tty_write Function
 *
 *  Parameters :
 *  @file   : File pointer for BT char driver
 *  @data   : packet data from BT application
 *  @size   : Size of the packet data
 *  @offset :
 *  Returns  Size of packet on success
 *           else suitable error code
 */
ssize_t hci_tty_write(struct file *file, const char __user *data,
		size_t size, loff_t *offset)
{
	struct ti_st *hst = file->private_data;
	struct	sk_buff *skb;

	pr_debug("inside %s (%p, %p, %zu, %p)\n",
		 __func__, file, data, size, offset);

	if (!hst->st_write) {
		pr_err(" Can't write to ST, hhci_tty->st_write null ?");
		return -EINVAL;
	}

	skb = alloc_skb(size, GFP_KERNEL);
	/* Validate Created SKB */
	if (NULL == skb) {
		pr_err("Error aaloacting SKB");
		return -ENOMEM;
	}

	/* Forward the data from the user space to ST core */
	if (copy_from_user(skb_put(skb, size), data, size)) {
		pr_err(" Unable to copy from user space");
		kfree_skb(skb);
		return -EIO;
	}

#ifdef VERBOSE
	pr_debug("start data..");
	print_hex_dump(KERN_INFO, "<out<", DUMP_PREFIX_NONE,
		       16, 1, skb->data, size, 0);
	pr_debug("\n..end data");
#endif

	hst->st_write(skb);
	return size;
}

/** hci_tty_ioctl Function
 *  This will peform the functions as directed by the command and command
 *  argument.
 *
 *  Parameters :
 *  @file  : File pointer for BT char driver
 *  @cmd   : IOCTL Command
 *  @arg   : Command argument for IOCTL command
 *  Returns  0 on success
 *           else suitable error code
 */
static long hci_tty_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct sk_buff *skb = NULL;
	int		retcode = 0;
	struct ti_st	*hst;
	unsigned int	value = 0;

	pr_debug("inside %s (%p, %u, %lx)", __func__, file, cmd, arg);

	/* Validate input parameters */
	if ((NULL == file) || (0 == cmd)) {
		pr_err("invalid input parameters passed to %s", __func__);
		return -EINVAL;
	}

	hst = file->private_data;

	switch (cmd) {
	case FIONREAD:
		/* Deque the SKB from the head if rx_list is not empty
		 * update the argument with skb->len to provide amount of data
		 * available in the available SKB +1 for the PKT_TYPE
		 * field not provided in data by TI-ST.
		 */
		skb = skb_dequeue(&hst->rx_list);
		if (skb != NULL) {
			value = skb->len + 1;
			/* Re-Store the SKB for furtur Read operations */
			skb_queue_head(&hst->rx_list, skb);
		}
		pr_debug("returning %d\n", value);
		if (copy_to_user((void __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		break;
	default:
		pr_debug("Un-Identified IOCTL %d", cmd);
		retcode = 0;
		break;
	}

	return retcode;
}

/** hci_tty_poll Function
 *  This function will wait till some data is received to the hci_tty driver from ST
 *
 *  Parameters :
 *  @file  : File pointer for BT char driver
 *  @wait  : POLL wait information
 *  Returns  status of POLL on success
 *           else suitable error code
 */
static unsigned int hci_tty_poll(struct file *file, poll_table *wait)
{
	struct ti_st	*hst = file->private_data;
	unsigned long mask = 0;

	pr_debug("@ %s\n", __func__);

	/* wait to be completed by st_receive */
	poll_wait(file, &hst->data_q, wait);
	pr_debug("poll broke\n");

	if (!skb_queue_empty(&hst->rx_list)) {
		pr_debug("rx list que !empty\n");
		mask |= POLLIN;	/* TODO: check app for mask */
	}

	return mask;
}

/* BT Char driver function pointers
 * These functions are called from USER space by pefroming File Operations
 * on /dev/hci_tty node exposed by this driver during init
 */
const struct file_operations hci_tty_chrdev_ops = {
	.owner = THIS_MODULE,
	.open = hci_tty_open,
	.read = hci_tty_read,
	.write = hci_tty_write,
	.unlocked_ioctl = hci_tty_ioctl,
	.poll = hci_tty_poll,
	.release = hci_tty_release,
};

/*********Functions called during insmod and delmod****************************/

static int hci_tty_major;		/* major number */
static struct class *hci_tty_class;	/* class during class_create */
static struct device *hci_tty_dev;	/* dev during device_create */
/** hci_tty_init Function
 *  This function Initializes the hci_tty driver parametes and exposes
 *  /dev/hci_tty node to user space
 *
 *  Parameters : NULL
 *  Returns  0 on success
 *           else suitable error code
 */
static int __init hci_tty_init(void)
{
	pr_info("inside %s\n", __func__);

	/* Expose the device DEVICE_NAME to user space
	 * And obtain the major number for the device
	 */
	hci_tty_major = register_chrdev(0, DEVICE_NAME, &hci_tty_chrdev_ops);

	if (0 > hci_tty_major) {
		pr_err("Error when registering to char dev");
		return hci_tty_major;
	}

	/*  udev */
	hci_tty_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(hci_tty_class)) {
		pr_err("Something went wrong in class_create");
		unregister_chrdev(hci_tty_major, DEVICE_NAME);
		return -1;
	}

	hci_tty_dev =
		device_create(hci_tty_class, NULL, MKDEV(hci_tty_major, 0),
			      NULL, DEVICE_NAME);
	if (IS_ERR(hci_tty_dev)) {
		pr_err("Error in device create");
		unregister_chrdev(hci_tty_major, DEVICE_NAME);
		class_destroy(hci_tty_class);
		return -1;
	}
	pr_info("allocated %d, %d\n", hci_tty_major, 0);
	return 0;
}

/** hci_tty_exit Function
 *  This function Destroys the hci_tty driver parametes and /dev/hci_tty node
 *
 *  Parameters : NULL
 *  Returns   NULL
 */
static void __exit hci_tty_exit(void)
{
	pr_info("inside %s\n", __func__);
	pr_info("bye.. freeing up %d\n", hci_tty_major);

	device_destroy(hci_tty_class, MKDEV(hci_tty_major, 0));
	class_destroy(hci_tty_class);
	unregister_chrdev(hci_tty_major, DEVICE_NAME);
}

module_init(hci_tty_init);
module_exit(hci_tty_exit);

MODULE_AUTHOR("Pavan Savoy <pavan_savoy@ti.com>");
MODULE_LICENSE("GPL");
