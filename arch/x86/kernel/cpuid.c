// SPDX-License-Identifier: GPL-2.0-or-later
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2000-2008 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * x86 CPUID access device
 *
 * This device is accessed by lseek() to the appropriate CPUID level
 * and then read in chunks of 16 bytes.  A larger size means multiple
 * reads of consecutive levels.
 *
 * The lower 32 bits of the file position is used as the incoming %eax,
 * and the upper 32 bits of the file position as the incoming %ecx,
 * the latter intended for "counting" eax levels like eax=4.
 *
 * This driver uses /dev/cpu/%d/cpuid where %d is the minor number, and on
 * an SMP box will direct the access to CPU %d.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/completion.h>

#include <asm/processor.h>
#include <asm/msr.h>

static struct class *cpuid_class;
static enum cpuhp_state cpuhp_cpuid_state;

struct cpuid_regs_done {
	struct cpuid_regs regs;
	struct completion done;
};

static void cpuid_smp_cpuid(void *cmd_block)
{
	struct cpuid_regs_done *cmd = cmd_block;

	cpuid_count(cmd->regs.eax, cmd->regs.ecx,
		    &cmd->regs.eax, &cmd->regs.ebx,
		    &cmd->regs.ecx, &cmd->regs.edx);

	complete(&cmd->done);
}

static ssize_t cpuid_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	char __user *tmp = buf;
	struct cpuid_regs_done cmd;
	int cpu = iminor(file_inode(file));
	u64 pos = *ppos;
	ssize_t bytes = 0;
	int err = 0;

	if (count % 16)
		return -EINVAL;	/* Invalid chunk size */

	init_completion(&cmd.done);
	for (; count; count -= 16) {
		call_single_data_t csd;

		INIT_CSD(&csd, cpuid_smp_cpuid, &cmd);

		cmd.regs.eax = pos;
		cmd.regs.ecx = pos >> 32;

		err = smp_call_function_single_async(cpu, &csd);
		if (err)
			break;
		wait_for_completion(&cmd.done);
		if (copy_to_user(tmp, &cmd.regs, 16)) {
			err = -EFAULT;
			break;
		}
		tmp += 16;
		bytes += 16;
		*ppos = ++pos;
		reinit_completion(&cmd.done);
	}

	return bytes ? bytes : err;
}

static int cpuid_open(struct inode *inode, struct file *file)
{
	unsigned int cpu;
	struct cpuinfo_x86 *c;

	cpu = iminor(file_inode(file));
	if (cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -ENXIO;	/* No such CPU */

	c = &cpu_data(cpu);
	if (c->cpuid_level < 0)
		return -EIO;	/* CPUID not supported */

	return 0;
}

/*
 * File operations we support
 */
static const struct file_operations cpuid_fops = {
	.owner = THIS_MODULE,
	.llseek = no_seek_end_llseek,
	.read = cpuid_read,
	.open = cpuid_open,
};

static int cpuid_device_create(unsigned int cpu)
{
	struct device *dev;

	dev = device_create(cpuid_class, NULL, MKDEV(CPUID_MAJOR, cpu), NULL,
			    "cpu%d", cpu);
	return PTR_ERR_OR_ZERO(dev);
}

static int cpuid_device_destroy(unsigned int cpu)
{
	device_destroy(cpuid_class, MKDEV(CPUID_MAJOR, cpu));
	return 0;
}

static char *cpuid_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "cpu/%u/cpuid", MINOR(dev->devt));
}

static int __init cpuid_init(void)
{
	int err;

	if (__register_chrdev(CPUID_MAJOR, 0, NR_CPUS,
			      "cpu/cpuid", &cpuid_fops)) {
		printk(KERN_ERR "cpuid: unable to get major %d for cpuid\n",
		       CPUID_MAJOR);
		return -EBUSY;
	}
	cpuid_class = class_create(THIS_MODULE, "cpuid");
	if (IS_ERR(cpuid_class)) {
		err = PTR_ERR(cpuid_class);
		goto out_chrdev;
	}
	cpuid_class->devnode = cpuid_devnode;

	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/cpuid:online",
				cpuid_device_create, cpuid_device_destroy);
	if (err < 0)
		goto out_class;

	cpuhp_cpuid_state = err;
	return 0;

out_class:
	class_destroy(cpuid_class);
out_chrdev:
	__unregister_chrdev(CPUID_MAJOR, 0, NR_CPUS, "cpu/cpuid");
	return err;
}
module_init(cpuid_init);

static void __exit cpuid_exit(void)
{
	cpuhp_remove_state(cpuhp_cpuid_state);
	class_destroy(cpuid_class);
	__unregister_chrdev(CPUID_MAJOR, 0, NR_CPUS, "cpu/cpuid");
}
module_exit(cpuid_exit);

MODULE_AUTHOR("H. Peter Anvin <hpa@zytor.com>");
MODULE_DESCRIPTION("x86 generic CPUID driver");
MODULE_LICENSE("GPL");
