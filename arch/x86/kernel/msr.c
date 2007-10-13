/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 2000 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * x86 MSR access device
 *
 * This device is accessed by lseek() to the appropriate register number
 * and then read/write in chunks of 8 bytes.  A larger size means multiple
 * reads or writes of the same register.
 *
 * This driver uses /dev/cpu/%d/msr where %d is the minor number, and on
 * an SMP box will direct the access to CPU %d.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/system.h>

static struct class *msr_class;

static loff_t msr_seek(struct file *file, loff_t offset, int orig)
{
	loff_t ret = -EINVAL;

	lock_kernel();
	switch (orig) {
	case 0:
		file->f_pos = offset;
		ret = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		ret = file->f_pos;
	}
	unlock_kernel();
	return ret;
}

static ssize_t msr_read(struct file *file, char __user * buf,
			size_t count, loff_t * ppos)
{
	u32 __user *tmp = (u32 __user *) buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file->f_path.dentry->d_inode);
	int err;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		err = rdmsr_safe_on_cpu(cpu, reg, &data[0], &data[1]);
		if (err)
			return -EIO;
		if (copy_to_user(tmp, &data, 8))
			return -EFAULT;
		tmp += 2;
	}

	return ((char __user *)tmp) - buf;
}

static ssize_t msr_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file->f_path.dentry->d_inode);
	int err;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		if (copy_from_user(&data, tmp, 8))
			return -EFAULT;
		err = wrmsr_safe_on_cpu(cpu, reg, data[0], data[1]);
		if (err)
			return -EIO;
		tmp += 2;
	}

	return ((char __user *)tmp) - buf;
}

static int msr_open(struct inode *inode, struct file *file)
{
	unsigned int cpu = iminor(file->f_path.dentry->d_inode);
	struct cpuinfo_x86 *c = &(cpu_data)[cpu];

	if (cpu >= NR_CPUS || !cpu_online(cpu))
		return -ENXIO;	/* No such CPU */
	if (!cpu_has(c, X86_FEATURE_MSR))
		return -EIO;	/* MSR not supported */

	return 0;
}

/*
 * File operations we support
 */
static const struct file_operations msr_fops = {
	.owner = THIS_MODULE,
	.llseek = msr_seek,
	.read = msr_read,
	.write = msr_write,
	.open = msr_open,
};

static int msr_device_create(int i)
{
	int err = 0;
	struct device *dev;

	dev = device_create(msr_class, NULL, MKDEV(MSR_MAJOR, i), "msr%d",i);
	if (IS_ERR(dev))
		err = PTR_ERR(dev);
	return err;
}

static int msr_class_cpu_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		msr_device_create(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		device_destroy(msr_class, MKDEV(MSR_MAJOR, cpu));
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata msr_class_cpu_notifier =
{
	.notifier_call = msr_class_cpu_callback,
};

static int __init msr_init(void)
{
	int i, err = 0;
	i = 0;

	if (register_chrdev(MSR_MAJOR, "cpu/msr", &msr_fops)) {
		printk(KERN_ERR "msr: unable to get major %d for msr\n",
		       MSR_MAJOR);
		err = -EBUSY;
		goto out;
	}
	msr_class = class_create(THIS_MODULE, "msr");
	if (IS_ERR(msr_class)) {
		err = PTR_ERR(msr_class);
		goto out_chrdev;
	}
	for_each_online_cpu(i) {
		err = msr_device_create(i);
		if (err != 0)
			goto out_class;
	}
	register_hotcpu_notifier(&msr_class_cpu_notifier);

	err = 0;
	goto out;

out_class:
	i = 0;
	for_each_online_cpu(i)
		device_destroy(msr_class, MKDEV(MSR_MAJOR, i));
	class_destroy(msr_class);
out_chrdev:
	unregister_chrdev(MSR_MAJOR, "cpu/msr");
out:
	return err;
}

static void __exit msr_exit(void)
{
	int cpu = 0;
	for_each_online_cpu(cpu)
		device_destroy(msr_class, MKDEV(MSR_MAJOR, cpu));
	class_destroy(msr_class);
	unregister_chrdev(MSR_MAJOR, "cpu/msr");
	unregister_hotcpu_notifier(&msr_class_cpu_notifier);
}

module_init(msr_init);
module_exit(msr_exit)

MODULE_AUTHOR("H. Peter Anvin <hpa@zytor.com>");
MODULE_DESCRIPTION("x86 generic MSR driver");
MODULE_LICENSE("GPL");
