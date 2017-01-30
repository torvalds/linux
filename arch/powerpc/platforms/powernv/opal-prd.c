/*
 * OPAL Runtime Diagnostics interface driver
 * Supported on POWERNV platform
 *
 * Copyright IBM Corporation 2015
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "opal-prd: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/opal-prd.h>
#include <asm/opal.h>
#include <asm/io.h>
#include <linux/uaccess.h>


/**
 * The msg member must be at the end of the struct, as it's followed by the
 * message data.
 */
struct opal_prd_msg_queue_item {
	struct list_head		list;
	struct opal_prd_msg_header	msg;
};

static struct device_node *prd_node;
static LIST_HEAD(opal_prd_msg_queue);
static DEFINE_SPINLOCK(opal_prd_msg_queue_lock);
static DECLARE_WAIT_QUEUE_HEAD(opal_prd_msg_wait);
static atomic_t prd_usage;

static bool opal_prd_range_is_valid(uint64_t addr, uint64_t size)
{
	struct device_node *parent, *node;
	bool found;

	if (addr + size < addr)
		return false;

	parent = of_find_node_by_path("/reserved-memory");
	if (!parent)
		return false;

	found = false;

	for_each_child_of_node(parent, node) {
		uint64_t range_addr, range_size, range_end;
		const __be32 *addrp;
		const char *label;

		addrp = of_get_address(node, 0, &range_size, NULL);

		range_addr = of_read_number(addrp, 2);
		range_end = range_addr + range_size;

		label = of_get_property(node, "ibm,prd-label", NULL);

		/* PRD ranges need a label */
		if (!label)
			continue;

		if (range_end <= range_addr)
			continue;

		if (addr >= range_addr && addr + size <= range_end) {
			found = true;
			of_node_put(node);
			break;
		}
	}

	of_node_put(parent);
	return found;
}

static int opal_prd_open(struct inode *inode, struct file *file)
{
	/*
	 * Prevent multiple (separate) processes from concurrent interactions
	 * with the FW PRD channel
	 */
	if (atomic_xchg(&prd_usage, 1) == 1)
		return -EBUSY;

	return 0;
}

/*
 * opal_prd_mmap - maps firmware-provided ranges into userspace
 * @file: file structure for the device
 * @vma: VMA to map the registers into
 */

static int opal_prd_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t addr, size;
	pgprot_t page_prot;
	int rc;

	pr_devel("opal_prd_mmap(0x%016lx, 0x%016lx, 0x%lx, 0x%lx)\n",
			vma->vm_start, vma->vm_end, vma->vm_pgoff,
			vma->vm_flags);

	addr = vma->vm_pgoff << PAGE_SHIFT;
	size = vma->vm_end - vma->vm_start;

	/* ensure we're mapping within one of the allowable ranges */
	if (!opal_prd_range_is_valid(addr, size))
		return -EINVAL;

	page_prot = phys_mem_access_prot(file, vma->vm_pgoff,
					 size, vma->vm_page_prot);

	rc = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
				page_prot);

	return rc;
}

static bool opal_msg_queue_empty(void)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&opal_prd_msg_queue_lock, flags);
	ret = list_empty(&opal_prd_msg_queue);
	spin_unlock_irqrestore(&opal_prd_msg_queue_lock, flags);

	return ret;
}

static unsigned int opal_prd_poll(struct file *file,
		struct poll_table_struct *wait)
{
	poll_wait(file, &opal_prd_msg_wait, wait);

	if (!opal_msg_queue_empty())
		return POLLIN | POLLRDNORM;

	return 0;
}

static ssize_t opal_prd_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct opal_prd_msg_queue_item *item;
	unsigned long flags;
	ssize_t size, err;
	int rc;

	/* we need at least a header's worth of data */
	if (count < sizeof(item->msg))
		return -EINVAL;

	if (*ppos)
		return -ESPIPE;

	item = NULL;

	for (;;) {

		spin_lock_irqsave(&opal_prd_msg_queue_lock, flags);
		if (!list_empty(&opal_prd_msg_queue)) {
			item = list_first_entry(&opal_prd_msg_queue,
					struct opal_prd_msg_queue_item, list);
			list_del(&item->list);
		}
		spin_unlock_irqrestore(&opal_prd_msg_queue_lock, flags);

		if (item)
			break;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		rc = wait_event_interruptible(opal_prd_msg_wait,
				!opal_msg_queue_empty());
		if (rc)
			return -EINTR;
	}

	size = be16_to_cpu(item->msg.size);
	if (size > count) {
		err = -EINVAL;
		goto err_requeue;
	}

	rc = copy_to_user(buf, &item->msg, size);
	if (rc) {
		err = -EFAULT;
		goto err_requeue;
	}

	kfree(item);

	return size;

err_requeue:
	/* eep! re-queue at the head of the list */
	spin_lock_irqsave(&opal_prd_msg_queue_lock, flags);
	list_add(&item->list, &opal_prd_msg_queue);
	spin_unlock_irqrestore(&opal_prd_msg_queue_lock, flags);
	return err;
}

static ssize_t opal_prd_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct opal_prd_msg_header hdr;
	ssize_t size;
	void *msg;
	int rc;

	size = sizeof(hdr);

	if (count < size)
		return -EINVAL;

	/* grab the header */
	rc = copy_from_user(&hdr, buf, sizeof(hdr));
	if (rc)
		return -EFAULT;

	size = be16_to_cpu(hdr.size);

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rc = copy_from_user(msg, buf, size);
	if (rc) {
		size = -EFAULT;
		goto out_free;
	}

	rc = opal_prd_msg(msg);
	if (rc) {
		pr_warn("write: opal_prd_msg returned %d\n", rc);
		size = -EIO;
	}

out_free:
	kfree(msg);

	return size;
}

static int opal_prd_release(struct inode *inode, struct file *file)
{
	struct opal_prd_msg_header msg;

	msg.size = cpu_to_be16(sizeof(msg));
	msg.type = OPAL_PRD_MSG_TYPE_FINI;

	opal_prd_msg((struct opal_prd_msg *)&msg);

	atomic_xchg(&prd_usage, 0);

	return 0;
}

static long opal_prd_ioctl(struct file *file, unsigned int cmd,
		unsigned long param)
{
	struct opal_prd_info info;
	struct opal_prd_scom scom;
	int rc = 0;

	switch (cmd) {
	case OPAL_PRD_GET_INFO:
		memset(&info, 0, sizeof(info));
		info.version = OPAL_PRD_KERNEL_VERSION;
		rc = copy_to_user((void __user *)param, &info, sizeof(info));
		if (rc)
			return -EFAULT;
		break;

	case OPAL_PRD_SCOM_READ:
		rc = copy_from_user(&scom, (void __user *)param, sizeof(scom));
		if (rc)
			return -EFAULT;

		scom.rc = opal_xscom_read(scom.chip, scom.addr,
				(__be64 *)&scom.data);
		scom.data = be64_to_cpu(scom.data);
		pr_devel("ioctl SCOM_READ: chip %llx addr %016llx data %016llx rc %lld\n",
				scom.chip, scom.addr, scom.data, scom.rc);

		rc = copy_to_user((void __user *)param, &scom, sizeof(scom));
		if (rc)
			return -EFAULT;
		break;

	case OPAL_PRD_SCOM_WRITE:
		rc = copy_from_user(&scom, (void __user *)param, sizeof(scom));
		if (rc)
			return -EFAULT;

		scom.rc = opal_xscom_write(scom.chip, scom.addr, scom.data);
		pr_devel("ioctl SCOM_WRITE: chip %llx addr %016llx data %016llx rc %lld\n",
				scom.chip, scom.addr, scom.data, scom.rc);

		rc = copy_to_user((void __user *)param, &scom, sizeof(scom));
		if (rc)
			return -EFAULT;
		break;

	default:
		rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations opal_prd_fops = {
	.open		= opal_prd_open,
	.mmap		= opal_prd_mmap,
	.poll		= opal_prd_poll,
	.read		= opal_prd_read,
	.write		= opal_prd_write,
	.unlocked_ioctl	= opal_prd_ioctl,
	.release	= opal_prd_release,
	.owner		= THIS_MODULE,
};

static struct miscdevice opal_prd_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "opal-prd",
	.fops		= &opal_prd_fops,
};

/* opal interface */
static int opal_prd_msg_notifier(struct notifier_block *nb,
		unsigned long msg_type, void *_msg)
{
	struct opal_prd_msg_queue_item *item;
	struct opal_prd_msg_header *hdr;
	struct opal_msg *msg = _msg;
	int msg_size, item_size;
	unsigned long flags;

	if (msg_type != OPAL_MSG_PRD)
		return 0;

	/* Calculate total size of the message and item we need to store. The
	 * 'size' field in the header includes the header itself. */
	hdr = (void *)msg->params;
	msg_size = be16_to_cpu(hdr->size);
	item_size = msg_size + sizeof(*item) - sizeof(item->msg);

	item = kzalloc(item_size, GFP_ATOMIC);
	if (!item)
		return -ENOMEM;

	memcpy(&item->msg, msg->params, msg_size);

	spin_lock_irqsave(&opal_prd_msg_queue_lock, flags);
	list_add_tail(&item->list, &opal_prd_msg_queue);
	spin_unlock_irqrestore(&opal_prd_msg_queue_lock, flags);

	wake_up_interruptible(&opal_prd_msg_wait);

	return 0;
}

static struct notifier_block opal_prd_event_nb = {
	.notifier_call	= opal_prd_msg_notifier,
	.next		= NULL,
	.priority	= 0,
};

static int opal_prd_probe(struct platform_device *pdev)
{
	int rc;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	/* We should only have one prd driver instance per machine; ensure
	 * that we only get a valid probe on a single OF node.
	 */
	if (prd_node)
		return -EBUSY;

	prd_node = pdev->dev.of_node;

	rc = opal_message_notifier_register(OPAL_MSG_PRD, &opal_prd_event_nb);
	if (rc) {
		pr_err("Couldn't register event notifier\n");
		return rc;
	}

	rc = misc_register(&opal_prd_dev);
	if (rc) {
		pr_err("failed to register miscdev\n");
		opal_message_notifier_unregister(OPAL_MSG_PRD,
				&opal_prd_event_nb);
		return rc;
	}

	return 0;
}

static int opal_prd_remove(struct platform_device *pdev)
{
	misc_deregister(&opal_prd_dev);
	opal_message_notifier_unregister(OPAL_MSG_PRD, &opal_prd_event_nb);
	return 0;
}

static const struct of_device_id opal_prd_match[] = {
	{ .compatible = "ibm,opal-prd" },
	{ },
};

static struct platform_driver opal_prd_driver = {
	.driver = {
		.name		= "opal-prd",
		.of_match_table	= opal_prd_match,
	},
	.probe	= opal_prd_probe,
	.remove	= opal_prd_remove,
};

module_platform_driver(opal_prd_driver);

MODULE_DEVICE_TABLE(of, opal_prd_match);
MODULE_DESCRIPTION("PowerNV OPAL runtime diagnostic driver");
MODULE_LICENSE("GPL");
