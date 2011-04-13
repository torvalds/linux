/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
/*
 * SMD Packet Driver -- Provides userspace interface to SMD packet ports.
 */

#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/poll.h>

#include <mach/msm_smd.h>

#define NUM_SMD_PKT_PORTS 9
#define DEVICE_NAME "smdpkt"
#define MAX_BUF_SIZE 2048

struct smd_pkt_dev {
	struct cdev cdev;
	struct device *devicep;

	struct smd_channel *ch;
	int open_count;
	struct mutex ch_lock;
	struct mutex rx_lock;
	struct mutex tx_lock;
	wait_queue_head_t ch_read_wait_queue;
	wait_queue_head_t ch_opened_wait_queue;

	int i;

	unsigned char tx_buf[MAX_BUF_SIZE];
	unsigned char rx_buf[MAX_BUF_SIZE];
	int remote_open;

} *smd_pkt_devp[NUM_SMD_PKT_PORTS];

struct class *smd_pkt_classp;
static dev_t smd_pkt_number;

static int msm_smd_pkt_debug_enable;
module_param_named(debug_enable, msm_smd_pkt_debug_enable,
		   int, S_IRUGO | S_IWUSR | S_IWGRP);

#ifdef DEBUG
#define D_DUMP_BUFFER(prestr, cnt, buf) do {			\
		int i;						\
		if (msm_smd_pkt_debug_enable) {			\
			pr_debug("%s", prestr);			\
			for (i = 0; i < cnt; i++)		\
				pr_debug("%.2x", buf[i]);	\
			pr_debug("\n");				\
		}						\
	} while (0)
#else
#define D_DUMP_BUFFER(prestr, cnt, buf) do {} while (0)
#endif

#ifdef DEBUG
#define DBG(x...) do {			\
	if (msm_smd_pkt_debug_enable)	\
		pr_debug(x);		\
	} while (0)
#else
#define DBG(x...) do {} while (0)
#endif

static void check_and_wakeup_reader(struct smd_pkt_dev *smd_pkt_devp)
{
	int sz;

	if (!smd_pkt_devp || !smd_pkt_devp->ch)
		return;

	sz = smd_cur_packet_size(smd_pkt_devp->ch);
	if (sz == 0) {
		DBG("no packet\n");
		return;
	}
	if (sz > smd_read_avail(smd_pkt_devp->ch)) {
		DBG("incomplete packet\n");
		return;
	}

	DBG("waking up reader\n");
	wake_up_interruptible(&smd_pkt_devp->ch_read_wait_queue);
}

static int smd_pkt_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	int r, bytes_read;
	struct smd_pkt_dev *smd_pkt_devp;
	struct smd_channel *chl;

	DBG("read %d bytes\n", count);
	if (count > MAX_BUF_SIZE)
		return -EINVAL;

	smd_pkt_devp = file->private_data;
	if (!smd_pkt_devp || !smd_pkt_devp->ch)
		return -EINVAL;

	chl = smd_pkt_devp->ch;
wait_for_packet:
	r = wait_event_interruptible(smd_pkt_devp->ch_read_wait_queue,
				     (smd_cur_packet_size(chl) > 0 &&
				      smd_read_avail(chl) >=
				      smd_cur_packet_size(chl)));

	if (r < 0) {
		if (r != -ERESTARTSYS)
			pr_err("wait returned %d\n", r);
		return r;
	}

	mutex_lock(&smd_pkt_devp->rx_lock);

	bytes_read = smd_cur_packet_size(smd_pkt_devp->ch);
	if (bytes_read == 0 ||
	    bytes_read < smd_read_avail(smd_pkt_devp->ch)) {
		mutex_unlock(&smd_pkt_devp->rx_lock);
		DBG("Nothing to read\n");
		goto wait_for_packet;
	}

	if (bytes_read > count) {
		mutex_unlock(&smd_pkt_devp->rx_lock);
		pr_info("packet size %d > buffer size %d", bytes_read, count);
		return -EINVAL;
	}

	r = smd_read(smd_pkt_devp->ch, smd_pkt_devp->rx_buf, bytes_read);
	if (r != bytes_read) {
		mutex_unlock(&smd_pkt_devp->rx_lock);
		pr_err("smd_read failed to read %d bytes: %d\n", bytes_read, r);
		return -EIO;
	}

	D_DUMP_BUFFER("read: ", bytes_read, smd_pkt_devp->rx_buf);
	r = copy_to_user(buf, smd_pkt_devp->rx_buf, bytes_read);
	mutex_unlock(&smd_pkt_devp->rx_lock);
	if (r) {
		pr_err("copy_to_user failed %d\n", r);
		return -EFAULT;
	}

	DBG("read complete %d bytes\n", bytes_read);
	check_and_wakeup_reader(smd_pkt_devp);

	return bytes_read;
}

static int smd_pkt_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	int r;
	struct smd_pkt_dev *smd_pkt_devp;

	if (count > MAX_BUF_SIZE)
		return -EINVAL;

	DBG("writting %d bytes\n", count);

	smd_pkt_devp = file->private_data;
	if (!smd_pkt_devp || !smd_pkt_devp->ch)
		return -EINVAL;

	mutex_lock(&smd_pkt_devp->tx_lock);
	if (smd_write_avail(smd_pkt_devp->ch) < count) {
		mutex_unlock(&smd_pkt_devp->tx_lock);
		DBG("Not enough space to write\n");
		return -ENOMEM;
	}

	D_DUMP_BUFFER("write: ", count, buf);
	r = copy_from_user(smd_pkt_devp->tx_buf, buf, count);
	if (r) {
		mutex_unlock(&smd_pkt_devp->tx_lock);
		pr_err("copy_from_user failed %d\n", r);
		return -EFAULT;
	}

	r = smd_write(smd_pkt_devp->ch, smd_pkt_devp->tx_buf, count);
	if (r != count) {
		mutex_unlock(&smd_pkt_devp->tx_lock);
		pr_err("smd_write failed to write %d bytes: %d.\n", count, r);
		return -EIO;
	}
	mutex_unlock(&smd_pkt_devp->tx_lock);

	DBG("wrote %d bytes\n", count);
	return count;
}

static unsigned int smd_pkt_poll(struct file *file, poll_table *wait)
{
	struct smd_pkt_dev *smd_pkt_devp;
	unsigned int mask = 0;

	smd_pkt_devp = file->private_data;
	if (!smd_pkt_devp)
		return POLLERR;

	DBG("poll waiting\n");
	poll_wait(file, &smd_pkt_devp->ch_read_wait_queue, wait);
	if (smd_read_avail(smd_pkt_devp->ch))
		mask |= POLLIN | POLLRDNORM;

	DBG("poll return\n");
	return mask;
}

static void smd_pkt_ch_notify(void *priv, unsigned event)
{
	struct smd_pkt_dev *smd_pkt_devp = priv;

	if (smd_pkt_devp->ch == 0)
		return;

	switch (event) {
	case SMD_EVENT_DATA:
		DBG("data\n");
		check_and_wakeup_reader(smd_pkt_devp);
		break;

	case SMD_EVENT_OPEN:
		DBG("remote open\n");
		smd_pkt_devp->remote_open = 1;
		wake_up_interruptible(&smd_pkt_devp->ch_opened_wait_queue);
		break;

	case SMD_EVENT_CLOSE:
		smd_pkt_devp->remote_open = 0;
		pr_info("remote closed\n");
		break;

	default:
		pr_err("unknown event %d\n", event);
		break;
	}
}

static char *smd_pkt_dev_name[] = {
	"smdcntl0",
	"smdcntl1",
	"smdcntl2",
	"smdcntl3",
	"smdcntl4",
	"smdcntl5",
	"smdcntl6",
	"smdcntl7",
	"smd22",
};

static char *smd_ch_name[] = {
	"DATA5_CNTL",
	"DATA6_CNTL",
	"DATA7_CNTL",
	"DATA8_CNTL",
	"DATA9_CNTL",
	"DATA12_CNTL",
	"DATA13_CNTL",
	"DATA14_CNTL",
	"DATA22",
};

static int smd_pkt_open(struct inode *inode, struct file *file)
{
	int r = 0;
	struct smd_pkt_dev *smd_pkt_devp;

	smd_pkt_devp = container_of(inode->i_cdev, struct smd_pkt_dev, cdev);
	if (!smd_pkt_devp)
		return -EINVAL;

	file->private_data = smd_pkt_devp;

	mutex_lock(&smd_pkt_devp->ch_lock);
	if (smd_pkt_devp->open_count == 0) {
		r = smd_open(smd_ch_name[smd_pkt_devp->i],
			     &smd_pkt_devp->ch, smd_pkt_devp,
			     smd_pkt_ch_notify);
		if (r < 0) {
			pr_err("smd_open failed for %s, %d\n",
			       smd_ch_name[smd_pkt_devp->i], r);
			goto out;
		}

		r = wait_event_interruptible_timeout(
				smd_pkt_devp->ch_opened_wait_queue,
				smd_pkt_devp->remote_open,
				msecs_to_jiffies(2 * HZ));
		if (r == 0)
			r = -ETIMEDOUT;

		if (r < 0) {
			pr_err("wait returned %d\n", r);
			smd_close(smd_pkt_devp->ch);
			smd_pkt_devp->ch = 0;
		} else {
			smd_pkt_devp->open_count++;
			r = 0;
		}
	}
out:
	mutex_unlock(&smd_pkt_devp->ch_lock);
	return r;
}

static int smd_pkt_release(struct inode *inode, struct file *file)
{
	int r = 0;
	struct smd_pkt_dev *smd_pkt_devp = file->private_data;

	if (!smd_pkt_devp)
		return -EINVAL;

	mutex_lock(&smd_pkt_devp->ch_lock);
	if (--smd_pkt_devp->open_count == 0) {
		r = smd_close(smd_pkt_devp->ch);
		smd_pkt_devp->ch = 0;
	}
	mutex_unlock(&smd_pkt_devp->ch_lock);

	return r;
}

static const struct file_operations smd_pkt_fops = {
	.owner = THIS_MODULE,
	.open = smd_pkt_open,
	.release = smd_pkt_release,
	.read = smd_pkt_read,
	.write = smd_pkt_write,
	.poll = smd_pkt_poll,
};

static int __init smd_pkt_init(void)
{
	int i;
	int r;

	r = alloc_chrdev_region(&smd_pkt_number, 0,
				NUM_SMD_PKT_PORTS, DEVICE_NAME);
	if (r) {
		pr_err("alloc_chrdev_region() failed %d\n", r);
		return r;
	}

	smd_pkt_classp = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(smd_pkt_classp)) {
		r = PTR_ERR(smd_pkt_classp);
		pr_err("class_create() failed %d\n", r);
		goto unreg_chardev;
	}

	for (i = 0; i < NUM_SMD_PKT_PORTS; ++i) {
		smd_pkt_devp[i] = kzalloc(sizeof(struct smd_pkt_dev),
					  GFP_KERNEL);
		if (IS_ERR(smd_pkt_devp[i])) {
			r = PTR_ERR(smd_pkt_devp[i]);
			pr_err("kmalloc() failed %d\n", r);
			goto clean_cdevs;
		}

		smd_pkt_devp[i]->i = i;

		init_waitqueue_head(&smd_pkt_devp[i]->ch_read_wait_queue);
		smd_pkt_devp[i]->remote_open = 0;
		init_waitqueue_head(&smd_pkt_devp[i]->ch_opened_wait_queue);

		mutex_init(&smd_pkt_devp[i]->ch_lock);
		mutex_init(&smd_pkt_devp[i]->rx_lock);
		mutex_init(&smd_pkt_devp[i]->tx_lock);

		cdev_init(&smd_pkt_devp[i]->cdev, &smd_pkt_fops);
		smd_pkt_devp[i]->cdev.owner = THIS_MODULE;

		r = cdev_add(&smd_pkt_devp[i]->cdev,
			     (smd_pkt_number + i), 1);
		if (r) {
			pr_err("cdev_add() failed %d\n", r);
			kfree(smd_pkt_devp[i]);
			goto clean_cdevs;
		}

		smd_pkt_devp[i]->devicep =
			device_create(smd_pkt_classp, NULL,
				      (smd_pkt_number + i), NULL,
				      smd_pkt_dev_name[i]);
		if (IS_ERR(smd_pkt_devp[i]->devicep)) {
			r = PTR_ERR(smd_pkt_devp[i]->devicep);
			pr_err("device_create() failed %d\n", r);
			cdev_del(&smd_pkt_devp[i]->cdev);
			kfree(smd_pkt_devp[i]);
			goto clean_cdevs;
		}

	}

	pr_info("SMD Packet Port Driver Initialized.\n");
	return 0;

clean_cdevs:
	if (i > 0) {
		while (--i >= 0) {
			mutex_destroy(&smd_pkt_devp[i]->ch_lock);
			mutex_destroy(&smd_pkt_devp[i]->rx_lock);
			mutex_destroy(&smd_pkt_devp[i]->tx_lock);
			cdev_del(&smd_pkt_devp[i]->cdev);
			kfree(smd_pkt_devp[i]);
			device_destroy(smd_pkt_classp,
				       MKDEV(MAJOR(smd_pkt_number), i));
		}
	}

	class_destroy(smd_pkt_classp);
unreg_chardev:
	unregister_chrdev_region(MAJOR(smd_pkt_number), NUM_SMD_PKT_PORTS);
	return r;
}
module_init(smd_pkt_init);

static void __exit smd_pkt_cleanup(void)
{
	int i;

	for (i = 0; i < NUM_SMD_PKT_PORTS; ++i) {
		mutex_destroy(&smd_pkt_devp[i]->ch_lock);
		mutex_destroy(&smd_pkt_devp[i]->rx_lock);
		mutex_destroy(&smd_pkt_devp[i]->tx_lock);
		cdev_del(&smd_pkt_devp[i]->cdev);
		kfree(smd_pkt_devp[i]);
		device_destroy(smd_pkt_classp,
			       MKDEV(MAJOR(smd_pkt_number), i));
	}

	class_destroy(smd_pkt_classp);
	unregister_chrdev_region(MAJOR(smd_pkt_number), NUM_SMD_PKT_PORTS);
}
module_exit(smd_pkt_cleanup);

MODULE_DESCRIPTION("MSM Shared Memory Packet Port");
MODULE_LICENSE("GPL v2");
