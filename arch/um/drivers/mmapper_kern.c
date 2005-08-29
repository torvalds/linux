/*
 * arch/um/drivers/mmapper_kern.c
 *
 * BRIEF MODULE DESCRIPTION
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon glonnon@ridgerun.com or info@ridgerun.com
 *
 */

#include <linux/init.h> 
#include <linux/module.h>
#include <linux/mm.h> 
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include "mem_user.h"
#include "user_util.h"
 
/* These are set in mmapper_init, which is called at boot time */
static unsigned long mmapper_size;
static unsigned long p_buf = 0;
static char *v_buf = NULL;

static ssize_t
mmapper_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, v_buf, mmapper_size);
}

static ssize_t
mmapper_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (*ppos > mmapper_size)
		return -EINVAL;

	if (count > mmapper_size - *ppos)
		count = mmapper_size - *ppos;

	if (copy_from_user(&v_buf[*ppos], buf, count))
		return -EFAULT;
	
	return count;
}

static int 
mmapper_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	 unsigned long arg)
{
	return(-ENOIOCTLCMD);
}

static int 
mmapper_mmap(struct file *file, struct vm_area_struct * vma)
{
	int ret = -EINVAL;
	int size;

	if (vma->vm_pgoff != 0)
		goto out;
	
	size = vma->vm_end - vma->vm_start;
	if(size > mmapper_size) return(-EFAULT);

	/* XXX A comment above remap_pfn_range says it should only be
	 * called when the mm semaphore is held
	 */
	if (remap_pfn_range(vma, vma->vm_start, p_buf >> PAGE_SHIFT, size,
			     vma->vm_page_prot))
		goto out;
	ret = 0;
out:
	return ret;
}

static int
mmapper_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int 
mmapper_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations mmapper_fops = {
	.owner		= THIS_MODULE,
	.read		= mmapper_read,
	.write		= mmapper_write,
	.ioctl		= mmapper_ioctl,
	.mmap		= mmapper_mmap,
	.open		= mmapper_open,
	.release	= mmapper_release,
};

static struct miscdevice mmapper_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "mmapper",
	.fops		= &mmapper_fops
};

static int __init mmapper_init(void)
{
	int err;

	printk(KERN_INFO "Mapper v0.1\n");

	v_buf = (char *) find_iomem("mmapper", &mmapper_size);
	if(mmapper_size == 0){
		printk(KERN_ERR "mmapper_init - find_iomem failed\n");
		goto out;
	}

	err = misc_register(&mmapper_dev);
	if(err){
		printk(KERN_ERR "mmapper - misc_register failed, err = %d\n",
		       err);
		goto out;
	}

	p_buf = __pa(v_buf);
out:
	return 0;
}

static void mmapper_exit(void)
{
	misc_deregister(&mmapper_dev);
}

module_init(mmapper_init);
module_exit(mmapper_exit);

MODULE_AUTHOR("Greg Lonnon <glonnon@ridgerun.com>");
MODULE_DESCRIPTION("DSPLinux simulator mmapper driver");
/*
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
