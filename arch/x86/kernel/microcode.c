/*
 *	Intel CPU Microcode Update Driver for Linux
 *
 *	Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		      2006	Shaohua Li <shaohua.li@intel.com>
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.11 of Volume 3a, IA-32 Intel? Architecture
 *	Software Developer's Manual
 *	Order Number 253668 or free download from:
 *
 *	http://developer.intel.com/design/pentium4/manuals/253668.htm
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non Intel & no suitable microcode).
 *	1.06	03 Aug 2000, Tigran Aivazian <tigran@veritas.com>
 *		Removed ->release(). Removed exclusive open and status bitmap.
 *		Added microcode_rwsem to serialize read()/write()/ioctl().
 *		Removed global kernel lock usage.
 *	1.07	07 Sep 2000, Tigran Aivazian <tigran@veritas.com>
 *		Write 0 to 0x8B msr and then cpuid before reading revision,
 *		so that it works even if there were no update done by the
 *		BIOS. Otherwise, reading from 0x8B gives junk (which happened
 *		to be 0 on my machine which is why it worked even when I
 *		disabled update by the BIOS)
 *		Thanks to Eric W. Biederman <ebiederman@lnxi.com> for the fix.
 *	1.08	11 Dec 2000, Richard Schaal <richard.schaal@intel.com> and
 *			     Tigran Aivazian <tigran@veritas.com>
 *		Intel Pentium 4 processor support and bugfixes.
 *	1.09	30 Oct 2001, Tigran Aivazian <tigran@veritas.com>
 *		Bugfix for HT (Hyper-Threading) enabled processors
 *		whereby processor resources are shared by all logical processors
 *		in a single CPU package.
 *	1.10	28 Feb 2002 Asit K Mallick <asit.k.mallick@intel.com> and
 *		Tigran Aivazian <tigran@veritas.com>,
 *		Serialize updates as required on HT processors due to
 *		speculative nature of implementation.
 *	1.11	22 Mar 2002 Tigran Aivazian <tigran@veritas.com>
 *		Fix the panic when writing zero-length microcode chunk.
 *	1.12	29 Sep 2003 Nitin Kamble <nitin.a.kamble@intel.com>,
 *		Jun Nakajima <jun.nakajima@intel.com>
 *		Support for the microcode updates in the new format.
 *	1.13	10 Oct 2003 Tigran Aivazian <tigran@veritas.com>
 *		Removed ->read() method and obsoleted MICROCODE_IOCFREE ioctl
 *		because we no longer hold a copy of applied microcode
 *		in kernel memory.
 *	1.14	25 Jun 2004 Tigran Aivazian <tigran@veritas.com>
 *		Fix sigmatch() macro to handle old CPUs with pf == 0.
 *		Thanks to Stuart Swales for pointing out this bug.
 */
#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/microcode.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

#define MICROCODE_VERSION 	"2.00"

struct microcode_ops *microcode_ops;

/* no concurrent ->write()s are allowed on /dev/cpu/microcode */
static DEFINE_MUTEX(microcode_mutex);

struct ucode_cpu_info ucode_cpu_info[NR_CPUS];
EXPORT_SYMBOL_GPL(ucode_cpu_info);

#ifdef CONFIG_MICROCODE_OLD_INTERFACE
static int do_microcode_update(const void __user *buf, size_t size)
{
	cpumask_t old;
	int error = 0;
	int cpu;

	old = current->cpus_allowed;

	for_each_online_cpu(cpu) {
		struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

		if (!uci->valid)
			continue;

		set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
		error = microcode_ops->request_microcode_user(cpu, buf, size);
		if (error < 0)
			goto out;
		if (!error)
			microcode_ops->apply_microcode(cpu);
	}
out:
	set_cpus_allowed_ptr(current, &old);
	return error;
}

static int microcode_open(struct inode *unused1, struct file *unused2)
{
	cycle_kernel_lock();
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

static ssize_t microcode_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *ppos)
{
	ssize_t ret;

	if ((len >> PAGE_SHIFT) > num_physpages) {
		printk(KERN_ERR "microcode: too much data (max %ld pages)\n",
		       num_physpages);
		return -EINVAL;
	}

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	ret = do_microcode_update(buf, len);
	if (!ret)
		ret = (ssize_t)len;

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	return ret;
}

static const struct file_operations microcode_fops = {
	.owner		= THIS_MODULE,
	.write		= microcode_write,
	.open		= microcode_open,
};

static struct miscdevice microcode_dev = {
	.minor		= MICROCODE_MINOR,
	.name		= "microcode",
	.fops		= &microcode_fops,
};

static int __init microcode_dev_init(void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error) {
		printk(KERN_ERR
			"microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void microcode_dev_exit(void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
#else
#define microcode_dev_init() 0
#define microcode_dev_exit() do { } while (0)
#endif

/* fake device for request_firmware */
struct platform_device *microcode_pdev;

static ssize_t reload_store(struct sys_device *dev,
			    struct sysdev_attribute *attr,
			    const char *buf, size_t sz)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;
	char *end;
	unsigned long val = simple_strtoul(buf, &end, 0);
	int err = 0;
	int cpu = dev->id;

	if (end == buf)
		return -EINVAL;
	if (val == 1) {
		cpumask_t old = current->cpus_allowed;

		get_online_cpus();
		if (cpu_online(cpu)) {
			set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
			mutex_lock(&microcode_mutex);
			if (uci->valid) {
				err = microcode_ops->request_microcode_fw(cpu,
						&microcode_pdev->dev);
				if (!err)
					microcode_ops->apply_microcode(cpu);
			}
			mutex_unlock(&microcode_mutex);
			set_cpus_allowed_ptr(current, &old);
		}
		put_online_cpus();
	}
	if (err)
		return err;
	return sz;
}

static ssize_t version_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.rev);
}

static ssize_t pf_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.pf);
}

static SYSDEV_ATTR(reload, 0200, NULL, reload_store);
static SYSDEV_ATTR(version, 0400, version_show, NULL);
static SYSDEV_ATTR(processor_flags, 0400, pf_show, NULL);

static struct attribute *mc_default_attrs[] = {
	&attr_reload.attr,
	&attr_version.attr,
	&attr_processor_flags.attr,
	NULL
};

static struct attribute_group mc_attr_group = {
	.attrs = mc_default_attrs,
	.name = "microcode",
};

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	mutex_lock(&microcode_mutex);
	microcode_ops->microcode_fini_cpu(cpu);
	uci->valid = 0;
	mutex_unlock(&microcode_mutex);
}

static void collect_cpu_info(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	memset(uci, 0, sizeof(*uci));
	if (!microcode_ops->collect_cpu_info(cpu, &uci->cpu_sig))
		uci->valid = 1;
}

static int microcode_resume_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct cpu_signature nsig;

	pr_debug("microcode: CPU%d resumed\n", cpu);

	if (!uci->mc.valid_mc)
		return 1;

	/*
	 * Let's verify that the 'cached' ucode does belong
	 * to this cpu (a bit of paranoia):
	 */
	if (microcode_ops->collect_cpu_info(cpu, &nsig)) {
		microcode_fini_cpu(cpu);
		return -1;
	}

	if (memcmp(&nsig, &uci->cpu_sig, sizeof(nsig))) {
		microcode_fini_cpu(cpu);
		/* Should we look for a new ucode here? */
		return 1;
	}

	return 0;
}

void microcode_update_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	int err = 0;

	/* We should bind the task to the CPU */
	BUG_ON(raw_smp_processor_id() != cpu);

	mutex_lock(&microcode_mutex);
	/*
	 * Check if the system resume is in progress (uci->valid != NULL),
	 * otherwise just request a firmware:
	 */
	if (uci->valid) {
		err = microcode_resume_cpu(cpu);
	} else {	
		collect_cpu_info(cpu);
		if (uci->valid && system_state == SYSTEM_RUNNING)
			err = microcode_ops->request_microcode_fw(cpu,
					&microcode_pdev->dev);
	}

	if (!err)
		microcode_ops->apply_microcode(cpu);

	mutex_unlock(&microcode_mutex);
}

static void microcode_init_cpu(int cpu)
{
	cpumask_t old = current->cpus_allowed;

	set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
	microcode_update_cpu(cpu);
	set_cpus_allowed_ptr(current, &old);
}

static int mc_sysdev_add(struct sys_device *sys_dev)
{
	int err, cpu = sys_dev->id;
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("microcode: CPU%d added\n", cpu);
	memset(uci, 0, sizeof(*uci));

	err = sysfs_create_group(&sys_dev->kobj, &mc_attr_group);
	if (err)
		return err;

	microcode_init_cpu(cpu);
	return 0;
}

static int mc_sysdev_remove(struct sys_device *sys_dev)
{
	int cpu = sys_dev->id;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("microcode: CPU%d removed\n", cpu);
	microcode_fini_cpu(cpu);
	sysfs_remove_group(&sys_dev->kobj, &mc_attr_group);
	return 0;
}

static int mc_sysdev_resume(struct sys_device *dev)
{
	int cpu = dev->id;

	if (!cpu_online(cpu))
		return 0;

	/* only CPU 0 will apply ucode here */
	microcode_update_cpu(0);
	return 0;
}

static struct sysdev_driver mc_sysdev_driver = {
	.add = mc_sysdev_add,
	.remove = mc_sysdev_remove,
	.resume = mc_sysdev_resume,
};

static __cpuinit int
mc_cpu_callback(struct notifier_block *nb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct sys_device *sys_dev;

	sys_dev = get_cpu_sysdev(cpu);
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		microcode_init_cpu(cpu);
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		pr_debug("microcode: CPU%d added\n", cpu);
		if (sysfs_create_group(&sys_dev->kobj, &mc_attr_group))
			printk(KERN_ERR "microcode: Failed to create the sysfs "
				"group for CPU%d\n", cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		/* Suspend is in progress, only remove the interface */
		sysfs_remove_group(&sys_dev->kobj, &mc_attr_group);
		pr_debug("microcode: CPU%d removed\n", cpu);
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED_FROZEN:
		/* The CPU refused to come up during a system resume */
		microcode_fini_cpu(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata mc_cpu_notifier = {
	.notifier_call = mc_cpu_callback,
};

int microcode_init(void *opaque, struct module *module)
{
	struct microcode_ops *ops = (struct microcode_ops *)opaque;
	int error;

	if (microcode_ops) {
		printk(KERN_ERR "microcode: already loaded the other module\n");
		return -EEXIST;
	}

	microcode_ops = ops;

	error = microcode_dev_init();
	if (error)
		return error;
	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev)) {
		microcode_dev_exit();
		return PTR_ERR(microcode_pdev);
	}

	get_online_cpus();
	error = sysdev_driver_register(&cpu_sysdev_class, &mc_sysdev_driver);
	put_online_cpus();
	if (error) {
		microcode_dev_exit();
		platform_device_unregister(microcode_pdev);
		return error;
	}

	register_hotcpu_notifier(&mc_cpu_notifier);

	printk(KERN_INFO
	       "Microcode Update Driver: v" MICROCODE_VERSION
	       " <tigran@aivazian.fsnet.co.uk>"
	       " <peter.oruba@amd.com>\n");

	return 0;
}
EXPORT_SYMBOL_GPL(microcode_init);

void __exit microcode_exit(void)
{
	microcode_dev_exit();

	unregister_hotcpu_notifier(&mc_cpu_notifier);

	get_online_cpus();
	sysdev_driver_unregister(&cpu_sysdev_class, &mc_sysdev_driver);
	put_online_cpus();

	platform_device_unregister(microcode_pdev);

	microcode_ops = NULL;

	printk(KERN_INFO
	       "Microcode Update Driver: v" MICROCODE_VERSION " removed.\n");
}
EXPORT_SYMBOL_GPL(microcode_exit);
