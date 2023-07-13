/*
 * Copyright 2020 StarFive, Inc <samin.guo@starfivetech.com>
 *
 * DW AXI dma driver for StarFive SoC VIC7100.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <asm/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define DRIVER_NAME			"dwdma"
#define AXIDMA_IOC_MAGIC		'A'
#define AXIDMA_IOCGETCHN		_IO(AXIDMA_IOC_MAGIC, 0)
#define AXIDMA_IOCCFGANDSTART		_IO(AXIDMA_IOC_MAGIC, 1)
#define AXIDMA_IOCGETSTATUS		_IO(AXIDMA_IOC_MAGIC, 2)
#define AXIDMA_IOCRELEASECHN		_IO(AXIDMA_IOC_MAGIC, 3)

#define AXI_DMA_MAX_CHANS		20

#define DMA_CHN_UNUSED			0
#define DMA_CHN_USED			1
#define DMA_STATUS_UNFINISHED		0
#define DMA_STATUS_FINISHED		1
#define DMA_MAX_TIMEOUT_MS		20000

static DECLARE_WAIT_QUEUE_HEAD(wq);

struct axidma_chncfg {
	unsigned long src_addr;	/*dma addr*/
	unsigned long dst_addr;	/*dma addr*/
	unsigned long virt_src;	/*mmap src addr*/
	unsigned long virt_dst;	/*mmap dst addr*/
	unsigned long phys;	/*desc phys addr*/
	unsigned int len;	/*transport lenth*/
	int mem_fd;		/*fd*/
	unsigned char chn_num;	/*dma channels number*/
	unsigned char status;	/*dma transport status*/
};

struct axidma_chns {
	struct dma_chan *dma_chan;
	unsigned char used;
	unsigned char status;
	unsigned char reserve[2];
};

struct axidma_chns channels[AXI_DMA_MAX_CHANS];

static int axidma_open(struct inode *inode, struct file *file)
{
	/*Open: do nothing*/
	return 0;
}

static int axidma_release(struct inode *inode, struct file *file)
{
	/* Release: do nothing */
	return 0;
}

static ssize_t axidma_write(struct file *file, const char __user *data,
			size_t len, loff_t *ppos)
{
	/* Write: do nothing */
	return 0;
}

static void dma_complete_func(void *status)
{
	*(char *)status = DMA_STATUS_FINISHED;
	wake_up_interruptible(&wq);
}

static long axidma_unlocked_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int i, ret;
	dma_cap_mask_t mask;
	dma_cookie_t cookie;
	struct dma_device *dma_dev;
	struct axidma_chncfg chncfg;
	struct dma_async_tx_descriptor *tx;
	enum dma_status	status;

	memset(&chncfg, 0, sizeof(struct axidma_chncfg));

	switch(cmd) {
	case AXIDMA_IOCGETCHN:
		for (i = 0; i < AXI_DMA_MAX_CHANS; i++) {
			if(DMA_CHN_UNUSED == channels[i].used)
				break;
		}

		if (AXI_DMA_MAX_CHANS == i) {
			pr_err("Get dma chn failed, because no idle channel\n");
			goto error;
		} else {
			channels[i].used = DMA_CHN_USED;
			channels[i].status = DMA_STATUS_UNFINISHED;
			chncfg.status = DMA_STATUS_UNFINISHED;
			chncfg.chn_num = i;
		}

		dma_cap_zero(mask);
		dma_cap_set(DMA_MEMCPY, mask);

		channels[i].dma_chan = dma_request_channel(mask, NULL, NULL);
		if (!channels[i].dma_chan) {
			pr_err("dma request channel failed\n");
			channels[i].used = DMA_CHN_UNUSED;
			goto error;
		}

		ret = copy_to_user((void __user *)arg, &chncfg,
				sizeof(struct axidma_chncfg));
		if (ret) {
			pr_err("Copy to user failed\n");
			goto error;
		}
		break;

	case AXIDMA_IOCCFGANDSTART:
		ret = copy_from_user(&chncfg, (void __user *)arg,
				     sizeof(struct axidma_chncfg));
		if (ret) {
			pr_err("Copy from user failed\n");
			goto error;
		}

		if ((chncfg.chn_num >= AXI_DMA_MAX_CHANS) ||
		   (!channels[chncfg.chn_num].dma_chan)) {
			pr_err("chn_num[%d] is invalid\n", chncfg.chn_num);
			goto error;
		}
		dma_dev = channels[chncfg.chn_num].dma_chan->device;

#ifdef CONFIG_SOC_STARFIVE_VIC7100
		starfive_flush_dcache(chncfg.src_addr, chncfg.len);
#endif
		tx = dma_dev->device_prep_dma_memcpy(
			channels[chncfg.chn_num].dma_chan,
			chncfg.dst_addr, chncfg.src_addr, chncfg.len,
			DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
		if (!tx) {
			pr_err("Failed to prepare DMA memcpy\n");
			goto error;
		}

		channels[chncfg.chn_num].status = DMA_STATUS_UNFINISHED;

		tx->callback_param = &channels[chncfg.chn_num].status;
		tx->callback = dma_complete_func;
		cookie = tx->tx_submit(tx);

		if (dma_submit_error(cookie)) {
			pr_err("Failed to dma tx_submit\n");
			goto error;
		}

		dma_async_issue_pending(channels[chncfg.chn_num].dma_chan);

		break;

	case AXIDMA_IOCGETSTATUS:
		ret = copy_from_user(&chncfg, (void __user *)arg,
			sizeof(struct axidma_chncfg));
		if (ret) {
			pr_info("Copy from user failed\n");
			goto error;
		}

		if (chncfg.chn_num >= AXI_DMA_MAX_CHANS) {
			pr_info("chn_num[%d] is invalid\n", chncfg.chn_num);
			goto error;
		}

		wait_event_interruptible_timeout(wq,
				channels[chncfg.chn_num].status,
				msecs_to_jiffies(DMA_MAX_TIMEOUT_MS));
#ifdef CONFIG_SOC_STARFIVE_VIC7100
		/*flush dcache*/
		starfive_flush_dcache(chncfg.dst_addr, chncfg.len);
#endif
		status = dma_async_is_tx_complete(channels[chncfg.chn_num].dma_chan,
						cookie, NULL, NULL);
		if (status != DMA_COMPLETE) {
			pr_err("dma: not complete! status:%d \n", status);
			dmaengine_terminate_sync(channels[chncfg.chn_num].dma_chan);

			channels[chncfg.chn_num].used = DMA_CHN_UNUSED;
			channels[chncfg.chn_num].status = DMA_STATUS_UNFINISHED;
			return -EBUSY;
		}

		chncfg.status = channels[chncfg.chn_num].status;

		ret = copy_to_user((void __user *)arg, &chncfg,
				   sizeof(struct axidma_chncfg));
		if(ret) {
			pr_info("Copy to user failed\n");
			goto error;
		}
		break;

	case AXIDMA_IOCRELEASECHN:
		ret = copy_from_user(&chncfg, (void __user *)arg,
				     sizeof(struct axidma_chncfg));
		if(ret) {
			pr_info("Copy from user failed\n");
			goto error;
		}

		if((chncfg.chn_num >= AXI_DMA_MAX_CHANS) ||
		   (!channels[chncfg.chn_num].dma_chan)) {
			pr_info("chn_num[%d] is invalid\n", chncfg.chn_num);
			goto error;
		}

		dma_release_channel(channels[chncfg.chn_num].dma_chan);
		channels[chncfg.chn_num].used = DMA_CHN_UNUSED;
		channels[chncfg.chn_num].status = DMA_STATUS_UNFINISHED;
		break;

	default:
		pr_info("Don't support cmd [%d]\n", cmd);
		break;
	}
	return 0;

error:
	return -EFAULT;
}

/*
 *	Kernel Interfaces
 */
static struct file_operations axidma_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= axidma_write,
	.unlocked_ioctl	= axidma_unlocked_ioctl,
	.open		= axidma_open,
	.release	= axidma_release,
};

static struct miscdevice axidma_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DRIVER_NAME,
	.fops		= &axidma_fops,
};

static int __init axidma_init(void)
{
	int ret = misc_register(&axidma_miscdev);
	if(ret) {
		pr_info(KERN_ERR "cannot register miscdev (err=%d)\n", ret);
		return ret;
	}

	memset(&channels, 0, sizeof(channels));

	return 0;
}

static void __exit axidma_exit(void)
{
	misc_deregister(&axidma_miscdev);
}

module_init(axidma_init);
module_exit(axidma_exit);

MODULE_AUTHOR("samin <samin.guo@starfivetech.com>");
MODULE_DESCRIPTION("DW Axi Dmac Driver");
MODULE_LICENSE("GPL v2");
