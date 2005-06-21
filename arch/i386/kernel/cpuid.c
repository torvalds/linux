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
 * cpuid.c
 *
 * x86 CPUID access device
 *
 * This device is accessed by lseek() to the appropriate CPUID level
 * and then read in chunks of 16 bytes.  A larger size means multiple
 * reads of consecutive levels.
 *
 * This driver uses /dev/cpu/%d/cpuid where %d is the minor number, and on
 * an SMP box will direct the access to CPU %d.
 */

#include <linux/module.h>
#include <linux/config.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/system.h>

static struct class *cpuid_class;

#ifdef CONFIG_SMP

struct cpuid_command {
	int cpu;
	u32 reg;
	u32 *data;
};

static void cpuid_smp_cpuid(void *cmd_block)
{
	struct cpuid_command *cmd = (struct cpuid_command *)cmd_block;

	if (cmd->cpu == smp_processor_id())
		cpuid(cmd->reg, &cmd->data[0], &cmd->data[1], &cmd->data[2],
		      &cmd->data[3]);
}

static inline void do_cpuid(int cpu, u32 reg, u32 * data)
{
	struct cpuid_command cmd;

	preempt_disable();
	if (cpu == smp_processor_id()) {
		cpuid(reg, &data[0], &data[1], &data[2], &data[3]);
	} else {
		cmd.cpu = cpu;
		cmd.reg = reg;
		cmd.data = data;

		smp_call_function(cpuid_smp_cpuid, &cmd, 1, 1);
	}
	preempt_enable();
}
#else				/* ! CONFIG_SMP */

static inline void do_cpuid(int cpu, u32 reg, u32 * data)
{
	cpuid(reg, &data[0], &data[1], &data[2], &data[3]);
}

#endif				/* ! CONFIG_SMP */

static loff_t cpuid_seek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

	lock_kernel();

	switch (orig) {
	case 0:
		file->f_pos = offset;
		ret = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		ret = file->f_pos;
		break;
	default:
		ret = -EINVAL;
	}

	unlock_kernel();
	return ret;
}

static ssize_t cpuid_read(struct file *file, char __user *buf,
			  size_t count, loff_t * ppos)
{
	char __user *tmp = buf;
	u32 data[4];
	size_t rv;
	u32 reg = *ppos;
	int cpu = iminor(file->f_dentry->d_inode);

	if (count % 16)
		return -EINVAL;	/* Invalid chunk size */

	for (rv = 0; count; count -= 16) {
		do_cpuid(cpu, reg, data);
		if (copy_to_user(tmp, &data, 16))
			return -EFAULT;
		tmp += 16;
		*ppos = reg++;
	}

	return tmp - buf;
}

static int cpuid_open(struct inode *inode, struct file *file)
{
	unsigned int cpu = iminor(file->f_dentry->d_inode);
	struct cpuinfo_x86 *c = &(cpu_data)[cpu];

	if (cpu >= NR_CPUS || !cpu_online(cpu))
		return -ENXIO;	/* No such CPU */
	if (c->cpuid_level < 0)
		return -EIO;	/* CPUID not supported */

	return 0;
}

/*
 * File operations we support
 */
static struct file_operations cpuid_fops = {
	.owner = THIS_MODULE,
	.llseek = cpuid_seek,
	.read = cpuid_read,
	.open = cpuid_open,
};

static int cpuid_class_device_create(int i)
{
	int err = 0;
	struct class_device *class_err;

	class_err = class_device_create(cpuid_class, MKDEV(CPUID_MAJOR, i), NULL, "cpu%d",i);
	if (IS_ERR(class_err))
		err = PTR_ERR(class_err);
	return err;
}

static int __devinit cpuid_class_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		cpuid_class_device_create(cpu);
		break;
	case CPU_DEAD:
		class_device_destroy(cpuid_class, MKDEV(CPUID_MAJOR, cpu));
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block cpuid_class_cpu_notifier =
{
	.notifier_call = cpuid_class_cpu_callback,
};

static int __init cpuid_init(void)
{
	int i, err = 0;
	i = 0;

	if (register_chrdev(CPUID_MAJOR, "cpu/cpuid", &cpuid_fops)) {
		printk(KERN_ERR "cpuid: unable to get major %d for cpuid\n",
		       CPUID_MAJOR);
		err = -EBUSY;
		goto out;
	}
	cpuid_class = class_create(THIS_MODULE, "cpuid");
	if (IS_ERR(cpuid_class)) {
		err = PTR_ERR(cpuid_class);
		goto out_chrdev;
	}
	for_each_online_cpu(i) {
		err = cpuid_class_device_create(i);
		if (err != 0) 
			goto out_class;
	}
	register_cpu_notifier(&cpuid_class_cpu_notifier);

	err = 0;
	goto out;

out_class:
	i = 0;
	for_each_online_cpu(i) {
		class_device_destroy(cpuid_class, MKDEV(CPUID_MAJOR, i));
	}
	class_destroy(cpuid_class);
out_chrdev:
	unregister_chrdev(CPUID_MAJOR, "cpu/cpuid");	
out:
	return err;
}

static void __exit cpuid_exit(void)
{
	int cpu = 0;

	for_each_online_cpu(cpu)
		class_device_destroy(cpuid_class, MKDEV(CPUID_MAJOR, cpu));
	class_destroy(cpuid_class);
	unregister_chrdev(CPUID_MAJOR, "cpu/cpuid");
	unregister_cpu_notifier(&cpuid_class_cpu_notifier);
}

module_init(cpuid_init);
module_exit(cpuid_exit);

MODULE_AUTHOR("H. Peter Anvin <hpa@zytor.com>");
MODULE_DESCRIPTION("x86 generic CPUID driver");
MODULE_LICENSE("GPL");
