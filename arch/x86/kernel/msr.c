// SPDX-License-Identifier: GPL-2.0-or-later
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2000-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author: H. Peter Anvin
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/security.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>

static struct class *msr_class;
static enum cpuhp_state cpuhp_msr_state;

enum allow_write_msrs {
	MSR_WRITES_ON,
	MSR_WRITES_OFF,
	MSR_WRITES_DEFAULT,
};

static enum allow_write_msrs allow_writes = MSR_WRITES_DEFAULT;

static ssize_t msr_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	u32 __user *tmp = (u32 __user *) buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file_inode(file));
	int err = 0;
	ssize_t bytes = 0;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		err = rdmsr_safe_on_cpu(cpu, reg, &data[0], &data[1]);
		if (err)
			break;
		if (copy_to_user(tmp, &data, 8)) {
			err = -EFAULT;
			break;
		}
		tmp += 2;
		bytes += 8;
	}

	return bytes ? bytes : err;
}

static int filter_write(u32 reg)
{
	/*
	 * MSRs writes usually happen all at once, and can easily saturate kmsg.
	 * Only allow one message every 30 seconds.
	 *
	 * It's possible to be smarter here and do it (for example) per-MSR, but
	 * it would certainly be more complex, and this is enough at least to
	 * avoid saturating the ring buffer.
	 */
	static DEFINE_RATELIMIT_STATE(fw_rs, 30 * HZ, 1);

	switch (allow_writes) {
	case MSR_WRITES_ON:  return 0;
	case MSR_WRITES_OFF: return -EPERM;
	default: break;
	}

	if (!__ratelimit(&fw_rs))
		return 0;

	if (reg == MSR_IA32_ENERGY_PERF_BIAS)
		return 0;

	pr_err("Write to unrecognized MSR 0x%x by %s (pid: %d). Please report to x86@kernel.org.\n",
	       reg, current->comm, current->pid);

	return 0;
}

static ssize_t msr_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file_inode(file));
	int err = 0;
	ssize_t bytes = 0;

	err = security_locked_down(LOCKDOWN_MSR);
	if (err)
		return err;

	err = filter_write(reg);
	if (err)
		return err;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		if (copy_from_user(&data, tmp, 8)) {
			err = -EFAULT;
			break;
		}

		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);

		err = wrmsr_safe_on_cpu(cpu, reg, data[0], data[1]);
		if (err)
			break;

		tmp += 2;
		bytes += 8;
	}

	return bytes ? bytes : err;
}

static long msr_ioctl(struct file *file, unsigned int ioc, unsigned long arg)
{
	u32 __user *uregs = (u32 __user *)arg;
	u32 regs[8];
	int cpu = iminor(file_inode(file));
	int err;

	switch (ioc) {
	case X86_IOC_RDMSR_REGS:
		if (!(file->f_mode & FMODE_READ)) {
			err = -EBADF;
			break;
		}
		if (copy_from_user(&regs, uregs, sizeof(regs))) {
			err = -EFAULT;
			break;
		}
		err = rdmsr_safe_regs_on_cpu(cpu, regs);
		if (err)
			break;
		if (copy_to_user(uregs, &regs, sizeof(regs)))
			err = -EFAULT;
		break;

	case X86_IOC_WRMSR_REGS:
		if (!(file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
			break;
		}
		if (copy_from_user(&regs, uregs, sizeof(regs))) {
			err = -EFAULT;
			break;
		}
		err = security_locked_down(LOCKDOWN_MSR);
		if (err)
			break;

		err = filter_write(regs[1]);
		if (err)
			return err;

		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);

		err = wrmsr_safe_regs_on_cpu(cpu, regs);
		if (err)
			break;
		if (copy_to_user(uregs, &regs, sizeof(regs)))
			err = -EFAULT;
		break;

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int msr_open(struct inode *inode, struct file *file)
{
	unsigned int cpu = iminor(file_inode(file));
	struct cpuinfo_x86 *c;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -ENXIO;	/* No such CPU */

	c = &cpu_data(cpu);
	if (!cpu_has(c, X86_FEATURE_MSR))
		return -EIO;	/* MSR not supported */

	return 0;
}

/*
 * File operations we support
 */
static const struct file_operations msr_fops = {
	.owner = THIS_MODULE,
	.llseek = no_seek_end_llseek,
	.read = msr_read,
	.write = msr_write,
	.open = msr_open,
	.unlocked_ioctl = msr_ioctl,
	.compat_ioctl = msr_ioctl,
};

static int msr_device_create(unsigned int cpu)
{
	struct device *dev;

	dev = device_create(msr_class, NULL, MKDEV(MSR_MAJOR, cpu), NULL,
			    "msr%d", cpu);
	return PTR_ERR_OR_ZERO(dev);
}

static int msr_device_destroy(unsigned int cpu)
{
	device_destroy(msr_class, MKDEV(MSR_MAJOR, cpu));
	return 0;
}

static char *msr_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "cpu/%u/msr", MINOR(dev->devt));
}

static int __init msr_init(void)
{
	int err;

	if (__register_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr", &msr_fops)) {
		pr_err("unable to get major %d for msr\n", MSR_MAJOR);
		return -EBUSY;
	}
	msr_class = class_create(THIS_MODULE, "msr");
	if (IS_ERR(msr_class)) {
		err = PTR_ERR(msr_class);
		goto out_chrdev;
	}
	msr_class->devnode = msr_devnode;

	err  = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/msr:online",
				 msr_device_create, msr_device_destroy);
	if (err < 0)
		goto out_class;
	cpuhp_msr_state = err;
	return 0;

out_class:
	class_destroy(msr_class);
out_chrdev:
	__unregister_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr");
	return err;
}
module_init(msr_init);

static void __exit msr_exit(void)
{
	cpuhp_remove_state(cpuhp_msr_state);
	class_destroy(msr_class);
	__unregister_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr");
}
module_exit(msr_exit)

static int set_allow_writes(const char *val, const struct kernel_param *cp)
{
	/* val is NUL-terminated, see kernfs_fop_write() */
	char *s = strstrip((char *)val);

	if (!strcmp(s, "on"))
		allow_writes = MSR_WRITES_ON;
	else if (!strcmp(s, "off"))
		allow_writes = MSR_WRITES_OFF;
	else
		allow_writes = MSR_WRITES_DEFAULT;

	return 0;
}

static int get_allow_writes(char *buf, const struct kernel_param *kp)
{
	const char *res;

	switch (allow_writes) {
	case MSR_WRITES_ON:  res = "on"; break;
	case MSR_WRITES_OFF: res = "off"; break;
	default: res = "default"; break;
	}

	return sprintf(buf, "%s\n", res);
}

static const struct kernel_param_ops allow_writes_ops = {
	.set = set_allow_writes,
	.get = get_allow_writes
};

module_param_cb(allow_writes, &allow_writes_ops, NULL, 0600);

MODULE_AUTHOR("H. Peter Anvin <hpa@zytor.com>");
MODULE_DESCRIPTION("x86 generic MSR driver");
MODULE_LICENSE("GPL");
