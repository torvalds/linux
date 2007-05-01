/* $Id: flash.c,v 1.25 2001/12/21 04:56:16 davem Exp $
 * flash.c: Allow mmap access to the OBP Flash, for OBP updates.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/ebus.h>
#include <asm/upa.h>

static DEFINE_SPINLOCK(flash_lock);
static struct {
	unsigned long read_base;	/* Physical read address */
	unsigned long write_base;	/* Physical write address */
	unsigned long read_size;	/* Size of read area */
	unsigned long write_size;	/* Size of write area */
	unsigned long busy;		/* In use? */
} flash;

#define FLASH_MINOR	152

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
	lock_kernel();
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
			unlock_kernel();
			return -EINVAL;
	}
	unlock_kernel();
	return file->f_pos;
}

static ssize_t
flash_read(struct file * file, char __user * buf,
	   size_t count, loff_t *ppos)
{
	unsigned long p = file->f_pos;
	int i;
	
	if (count > flash.read_size - p)
		count = flash.read_size - p;

	for (i = 0; i < count; i++) {
		u8 data = upa_readb(flash.read_base + p + i);
		if (put_user(data, buf))
			return -EFAULT;
		buf++;
	}

	file->f_pos += count;
	return count;
}

static int
flash_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, (void *)&flash.busy) != 0)
		return -EBUSY;

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

static struct miscdevice flash_dev = { FLASH_MINOR, "flash", &flash_fops };

static int __init flash_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev = NULL;
#ifdef CONFIG_PCI
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev = NULL;
	struct linux_prom_registers regs[2];
	int len, nregs;
#endif
	int err;

	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "flashprom")) {
			if (sdev->reg_addrs[0].phys_addr == sdev->reg_addrs[1].phys_addr) {
				flash.read_base = ((unsigned long)sdev->reg_addrs[0].phys_addr) |
					(((unsigned long)sdev->reg_addrs[0].which_io)<<32UL);
				flash.read_size = sdev->reg_addrs[0].reg_size;
				flash.write_base = flash.read_base;
				flash.write_size = flash.read_size;
			} else {
				flash.read_base = ((unsigned long)sdev->reg_addrs[0].phys_addr) |
					(((unsigned long)sdev->reg_addrs[0].which_io)<<32UL);
				flash.read_size = sdev->reg_addrs[0].reg_size;
				flash.write_base = ((unsigned long)sdev->reg_addrs[1].phys_addr) |
					(((unsigned long)sdev->reg_addrs[1].which_io)<<32UL);
				flash.write_size = sdev->reg_addrs[1].reg_size;
			}
			flash.busy = 0;
			break;
		}
	}
	if (!sdev) {
#ifdef CONFIG_PCI
		const struct linux_prom_registers *ebus_regs;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_node->name, "flashprom"))
					goto ebus_done;
			}
		}
	ebus_done:
		if (!edev)
			return -ENODEV;

		ebus_regs = of_get_property(edev->prom_node, "reg", &len);
		if (!ebus_regs || (len % sizeof(regs[0])) != 0) {
			printk("flash: Strange reg property size %d\n", len);
			return -ENODEV;
		}

		nregs = len / sizeof(ebus_regs[0]);

		flash.read_base = edev->resource[0].start;
		flash.read_size = ebus_regs[0].reg_size;

		if (nregs == 1) {
			flash.write_base = edev->resource[0].start;
			flash.write_size = ebus_regs[0].reg_size;
		} else if (nregs == 2) {
			flash.write_base = edev->resource[1].start;
			flash.write_size = ebus_regs[1].reg_size;
		} else {
			printk("flash: Strange number of regs %d\n", nregs);
			return -ENODEV;
		}

		flash.busy = 0;

#else
		return -ENODEV;
#endif
	}

	printk("OBP Flash: RD %lx[%lx] WR %lx[%lx]\n",
	       flash.read_base, flash.read_size,
	       flash.write_base, flash.write_size);

	err = misc_register(&flash_dev);
	if (err) {
		printk(KERN_ERR "flash: unable to get misc minor\n");
		return err;
	}

	return 0;
}

static void __exit flash_cleanup(void)
{
	misc_deregister(&flash_dev);
}

module_init(flash_init);
module_exit(flash_cleanup);
MODULE_LICENSE("GPL");
