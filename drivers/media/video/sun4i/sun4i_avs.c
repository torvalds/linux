/*
 * drivers/media/video/sun4i/sun4i_avs.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * sun4i_avs.c
 * driver for av-sync counter
 * Author : Truby.Zong <truby.zhuang@chipsbank.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <asm/system.h>
#include <linux/rmap.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/spinlock.h>

#include "sun4i_cedar.h"

#define DRV_VERSION "0.01alpha"

#ifndef AVSDEV_MAJOR
#define AVSDEV_MAJOR (151)
#endif
#ifndef AVSDEV_MINOR
#define AVSDEV_MINOR (0)
#endif

#undef _info
#ifdef CEDAR_DEBUG
#  define _info(fmt, args...) printk( KERN_DEBUG "[avs]: " fmt, ## args)
#else
#  define _info(fmt, args...)
#endif

int avs_dev_major = AVSDEV_MAJOR;
int avs_dev_minor = AVSDEV_MINOR;
module_param(avs_dev_major, int, S_IRUGO);
module_param(avs_dev_minor, int, S_IRUGO);

struct iomap_resource{
	struct resource *io_sram;
	struct resource *io_ccmu;
	struct resource *io_macc;
	struct resource *io_mpeg;
	struct resource *io_vc1;
	struct resource *io_memc;
};
struct iomap_resource iomap_res;

struct iomap_para{
	volatile char*	regs_sram;
	volatile char*	regs_ccmu;
	volatile char*	regs_macc;
	volatile char*	regs_mpeg;
	volatile char*	regs_vc1;
	volatile char*	regs_memc;
};

struct avs_dev {
	struct cdev cdev;	             /* char device struct                 */
	struct device *dev;              /* ptr to class device struct         */
	struct class  *class;            /* class for auto create device node  */

	struct semaphore sem;            /* mutual exclusion semaphore         */
	spinlock_t lock;                 /* spinlock to pretect ioctl access   */

	struct iomap_para iomap_addrs;   /* io remap addrs                     */
    struct iomap_resource iomap_res; /* io remap resources                 */
};
struct avs_dev *avs_devp;

/*
 * ioremap and request iomem
 */
static void avs_register_iomem(struct avs_dev *devp)
{
    struct resource *res;
    char *addr;

    memset(&devp->iomap_res,   0, sizeof(struct iomap_resource));
    memset(&devp->iomap_addrs, 0, sizeof(struct iomap_para));

    /* request mem for ccmu */
    res = request_mem_region(CCMU_REGS_pBASE, 1024, "ccmu");
    if (res == NULL)    {
        printk("Cannot reserve region for ccmu\n");
        goto err_out;
    }
    devp->iomap_res.io_ccmu = res;

	/* ioremap for ccmu */
    addr = ioremap(CCMU_REGS_pBASE, 4096);
    if (!addr){
        printk("cannot map region for ccmu");
        goto err_out;
    }
    devp->iomap_addrs.regs_ccmu = addr;

    return;

err_out:
    if (devp->iomap_addrs.regs_ccmu)
		iounmap(devp->iomap_addrs.regs_ccmu);
}

/*
 * unmap/release iomem
 */
static void avs_iomem_unregister(struct avs_dev *devp)
{
    if (devp->iomap_res.io_ccmu) {
		release_resource(devp->iomap_res.io_ccmu);
		devp->iomap_res.io_ccmu = NULL;
	}

	/* iounmap */
    if (devp->iomap_addrs.regs_ccmu) {
		iounmap(devp->iomap_addrs.regs_ccmu);
		devp->iomap_addrs.regs_ccmu = NULL;
	}
}

/*
 * ioctl function
 * including : wait video engine done,
 *             AVS Counter control,
 *             Physical memory control,
 *             module clock/freq control.
 */
long avsdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long   ret;
    unsigned int v;

	spinlock_t *lock;
	struct avs_dev *devp;

	ret = 0;
	devp = filp->private_data;
	lock = &devp->lock;

    switch (cmd)
    {
        case IOCTL_GETVALUE_AVS2:
			spin_lock(lock);

            v = readl(devp->iomap_addrs.regs_ccmu + 0xc40);

			spin_unlock(lock);
			return v;

        case IOCTL_CONFIG_AVS2:
			spin_lock(lock);

            v = readl(devp->iomap_addrs.regs_ccmu + 0xc44);
            v = 239<<16 | (v&0xffff);
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc44);
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc38);
            v |= 1<<9 | 1<<1;
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc38);
            writel(0, devp->iomap_addrs.regs_ccmu + 0xc40);

			spin_unlock(lock);
            break;

        case IOCTL_RESET_AVS2:
			spin_lock(lock);

            writel(0, devp->iomap_addrs.regs_ccmu + 0xc40);

			spin_unlock(lock);
            break;

        case IOCTL_PAUSE_AVS2:
			spin_lock(lock);

            v = readl(devp->iomap_addrs.regs_ccmu + 0xc38);
            v |= 1<<9;
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc38);

			spin_unlock(lock);
            break;

        case IOCTL_START_AVS2:
			spin_lock(lock);

            v = readl(devp->iomap_addrs.regs_ccmu + 0xc38);
            v &= ~(1<<9);
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc38);

			spin_unlock(lock);
            break;

        default:
            break;
    }

    return ret;
}

static int avsdev_open(struct inode *inode, struct file *filp)
{
	struct avs_dev *devp;
	devp = container_of(inode->i_cdev, struct avs_dev, cdev);
	filp->private_data = devp;

	if (down_interruptible(&devp->sem)) {
		return -ERESTARTSYS;
	}

	// init other resource here

	up(&devp->sem);

	nonseekable_open(inode, filp);
	return 0;
}

static int avsdev_release(struct inode *inode, struct file *filp)
{
	struct avs_dev *devp;

	devp = filp->private_data;

	if (down_interruptible(&devp->sem)) {
		return -ERESTARTSYS;
	}

	/* release other resource here */

	up(&devp->sem);
	return 0;
}

void avsdev_vma_open(struct vm_area_struct *vma)
{
    printk(KERN_NOTICE "avsdev VMA open, virt %lx, phys %lx\n",
		vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
	return;
}

void avsdev_vma_close(struct vm_area_struct *vma)
{
    printk(KERN_NOTICE "avsdev VMA close.\n");
	return;
}

static struct vm_operations_struct avsdev_remap_vm_ops = {
    .open  = avsdev_vma_open,
    .close = avsdev_vma_close,
};

static int avsdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long temp_pfn;
    unsigned int  VAddr;
	struct iomap_para addrs;

	unsigned int io_ram = 0;
    VAddr = vma->vm_pgoff << 12;

	addrs = avs_devp->iomap_addrs;

    if(VAddr == (unsigned int)addrs.regs_ccmu) {
        temp_pfn = CCMU_REGS_pBASE >> 12;
        io_ram = 1;
    } else {
        temp_pfn = (__pa(vma->vm_pgoff << 12))>>12;
        io_ram = 0;
    }

    if (io_ram == 0) {
        /* Set reserved and I/O flag for the area. */
        vma->vm_flags |= VM_RESERVED | VM_IO;

        /* Select uncached access. */
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        if (remap_pfn_range(vma, vma->vm_start, temp_pfn,
                            vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            return -EAGAIN;
        }
    } else {
        /* Set reserved and I/O flag for the area. */
        vma->vm_flags |= VM_RESERVED | VM_IO;

        /* Select uncached access. */
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        if (io_remap_pfn_range(vma, vma->vm_start, temp_pfn,
                               vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            return -EAGAIN;
        }
    }

    vma->vm_ops = &avsdev_remap_vm_ops;
    avsdev_vma_open(vma);

    return 0;
}

static struct file_operations avsdev_fops = {
    .owner   = THIS_MODULE,
    .unlocked_ioctl   = avsdev_ioctl,
    .mmap    = avsdev_mmap,
    .open    = avsdev_open,
    .release = avsdev_release,
	.llseek  = no_llseek,
};

static int __init avsdev_init(void)
{
	int ret;
	int devno;
	dev_t dev = 0;

	printk("[tt]----- avs_dev driver load... ----\n");
	if (avs_dev_major) {
		dev = MKDEV(avs_dev_major, avs_dev_minor);
		ret = register_chrdev_region(dev, 1, "avs_dev");
	} else {
		ret = alloc_chrdev_region(&dev, avs_dev_minor, 1, "avs_dev");
		avs_dev_major = MAJOR(dev);
		avs_dev_minor = MINOR(dev);
	}

	if (ret < 0) {
		printk(KERN_WARNING "avs_dev: can't get major %d\n", avs_dev_major);
		return ret;
	}

	avs_devp = kmalloc(sizeof(struct avs_dev), GFP_KERNEL);
	if (avs_devp == NULL) {
		printk("malloc mem for avs device err\n");
		return -ENOMEM;
	}
	memset(avs_devp, 0, sizeof(struct avs_dev));

	sema_init(&avs_devp->sem, 0);

	/* request resources and ioremap */
	printk("[tt]-----      register iomem      ----\n");
	avs_register_iomem(avs_devp);

	/* init lock for protect ioctl access */
	spin_lock_init(&avs_devp->lock);

	devno = MKDEV(avs_dev_major, avs_dev_minor);
	cdev_init(&avs_devp->cdev, &avsdev_fops);
	avs_devp->cdev.owner = THIS_MODULE;
	avs_devp->cdev.ops = &avsdev_fops;
	ret = cdev_add(&avs_devp->cdev, devno, 1);
	if (ret) {
		printk(KERN_NOTICE "Err:%d add avsdev", ret);
	}

    avs_devp->class = class_create(THIS_MODULE, "avs_dev");
    avs_devp->dev   = device_create(avs_devp->class, NULL, devno, NULL, "avs_dev");

	printk("[tt]--- avs_dev driver load ok!! -----\n");
	return 0;
}
module_init(avsdev_init);

static void __exit avsdev_exit(void)
{
	dev_t dev;
	dev = MKDEV(avs_dev_major, avs_dev_minor);

	/* Unregister iomem and iounmap */
	avs_iomem_unregister(avs_devp);

	if (avs_devp) {
		cdev_del(&avs_devp->cdev);
		device_destroy(avs_devp->class, dev);
		class_destroy(avs_devp->class);
	}

	unregister_chrdev_region(dev, 1);

	if (avs_devp) {
		kfree(avs_devp);
	}
}
module_exit(avsdev_exit);

MODULE_AUTHOR("Soft-Allwinner");
MODULE_DESCRIPTION("avs device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
