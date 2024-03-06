// SPDX-License-Identifier: GPL-2.0-only
/* flash.c: Allow mmap access to the OBP Flash, for OBP updates.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/upa.h>

static DEFINE_MUTEX(flash_mutex);
static DEFINE_SPINLOCK(flash_lock);
static struct {
	unsigned long read_base;	/* Physical read address */
	unsigned long write_base;	/* Physical write address */
	unsigned long read_size;	/* Size of read area */
	unsigned long write_size;	/* Size of write area */
	unsigned long busy;		/* In use? */
} flash;

static int
flash_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long addr;
	unsigned long size;

	spin_lock(&flash_lock);
	if (flash.read_base == flash.write_base) {
		addr = flash.read_base;
		size = flash.read_size;
	} else {
		if ((vma->vm_flags & VM_READ) &&
		    (vma->vm_flags & VM_WRITE)) {
			spin_unlock(&flash_lock);
			return -EINVAL;
		}
		if (vma->vm_flags & VM_READ) {
			addr = flash.read_base;
			size = flash.read_size;
		} else if (vma->vm_flags & VM_WRITE) {
			addr = flash.write_base;
			size = flash.write_size;
		} else {
			spin_unlock(&flash_lock);
			return -ENXIO;
		}
	}
	spin_unlock(&flash_lock);

	if ((vma->vm_pgoff << PAGE_SHIFT) > size)
		return -ENXIO;
	addr = vma->vm_pgoff + (addr >> PAGE_SHIFT);

	if (vma->vm_end - (vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT)) > size)
		size = vma->vm_end - (vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT));

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, addr, size, vma->vm_page_prot))
		return -EAGAIN;
		
	return 0;
}

static long long
flash_llseek(struct file *file, long long offset, int origin)
{
	mutex_lock(&flash_mutex);
	switch (origin) {
		case 0:
			file->f_pos = offset;
			break;
		case 1:
			file->f_pos += offset;
			if (file->f_pos > flash.read_size)
				file->f_pos = flash.read_size;
			break;
		case 2:
			file->f_pos = flash.read_size;
			break;
		default:
			mutex_unlock(&flash_mutex);
			return -EINVAL;
	}
	mutex_unlock(&flash_mutex);
	return file->f_pos;
}

static ssize_t
flash_read(struct file * file, char __user * buf,
	   size_t count, loff_t *ppos)
{
	loff_t p = *ppos;
	int i;

	if (count > flash.read_size - p)
		count = flash.read_size - p;

	for (i = 0; i < count; i++) {
		u8 data = upa_readb(flash.read_base + p + i);
		if (put_user(data, buf))
			return -EFAULT;
		buf++;
	}

	*ppos += count;
	return count;
}

static int
flash_open(struct inode *inode, struct file *file)
{
	mutex_lock(&flash_mutex);
	if (test_and_set_bit(0, (void *)&flash.busy) != 0) {
		mutex_unlock(&flash_mutex);
		return -EBUSY;
	}

	mutex_unlock(&flash_mutex);
	return 0;
}

static int
flash_release(struct inode *inode, struct file *file)
{
	spin_lock(&flash_lock);
	flash.busy = 0;
	spin_unlock(&flash_lock);

	return 0;
}

static const struct file_operations flash_fops = {
	/* no write to the Flash, use mmap
	 * and play flash dependent tricks.
	 */
	.owner =	THIS_MODULE,
	.llseek =	flash_llseek,
	.read =		flash_read,
	.mmap =		flash_mmap,
	.open =		flash_open,
	.release =	flash_release,
};

static struct miscdevice flash_dev = { SBUS_FLASH_MINOR, "flash", &flash_fops };

static int flash_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	struct device_node *parent;

	parent = dp->parent;

	if (!of_node_name_eq(parent, "sbus") &&
	    !of_node_name_eq(parent, "sbi") &&
	    !of_node_name_eq(parent, "ebus"))
		return -ENODEV;

	flash.read_base = op->resource[0].start;
	flash.read_size = resource_size(&op->resource[0]);
	if (op->resource[1].flags) {
		flash.write_base = op->resource[1].start;
		flash.write_size = resource_size(&op->resource[1]);
	} else {
		flash.write_base = op->resource[0].start;
		flash.write_size = resource_size(&op->resource[0]);
	}
	flash.busy = 0;

	printk(KERN_INFO "%pOF: OBP Flash, RD %lx[%lx] WR %lx[%lx]\n",
	       op->dev.of_node,
	       flash.read_base, flash.read_size,
	       flash.write_base, flash.write_size);

	return misc_register(&flash_dev);
}

static int flash_remove(struct platform_device *op)
{
	misc_deregister(&flash_dev);

	return 0;
}

static const struct of_device_id flash_match[] = {
	{
		.name = "flashprom",
	},
	{},
};
MODULE_DEVICE_TABLE(of, flash_match);

static struct platform_driver flash_driver = {
	.driver = {
		.name = "flash",
		.of_match_table = flash_match,
	},
	.probe		= flash_probe,
	.remove		= flash_remove,
};

module_platform_driver(flash_driver);

MODULE_LICENSE("GPL");
