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
 *	http://developer.intel.com/Assets/PDF/manual/253668.pdf	
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/syscore_ops.h>

#include <asm/microcode.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

#define MICROCODE_VERSION	"2.00"

static struct microcode_ops	*microcode_ops;

/*
 * Synchronization.
 *
 * All non cpu-hotplug-callback call sites use:
 *
 * - microcode_mutex to synchronize with each other;
 * - get/put_online_cpus() to synchronize with
 *   the cpu-hotplug-callback call sites.
 *
 * We guarantee that only a single cpu is being
 * updated at any particular moment of time.
 */
static DEFINE_MUTEX(microcode_mutex);

struct ucode_cpu_info		ucode_cpu_info[NR_CPUS];
EXPORT_SYMBOL_GPL(ucode_cpu_info);

/*
 * Operations that are run on a target cpu:
 */

struct cpu_info_ctx {
	struct cpu_signature	*cpu_sig;
	int			err;
};

static void collect_cpu_info_local(void *arg)
{
	struct cpu_info_ctx *ctx = arg;

	ctx->err = microcode_ops->collect_cpu_info(smp_processor_id(),
						   ctx->cpu_sig);
}

static int collect_cpu_info_on_target(int cpu, struct cpu_signature *cpu_sig)
{
	struct cpu_info_ctx ctx = { .cpu_sig = cpu_sig, .err = 0 };
	int ret;

	ret = smp_call_function_single(cpu, collect_cpu_info_local, &ctx, 1);
	if (!ret)
		ret = ctx.err;

	return ret;
}

static int collect_cpu_info(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	int ret;

	memset(uci, 0, sizeof(*uci));

	ret = collect_cpu_info_on_target(cpu, &uci->cpu_sig);
	if (!ret)
		uci->valid = 1;

	return ret;
}

struct apply_microcode_ctx {
	int err;
};

static void apply_microcode_local(void *arg)
{
	struct apply_microcode_ctx *ctx = arg;

	ctx->err = microcode_ops->apply_microcode(smp_processor_id());
}

static int apply_microcode_on_target(int cpu)
{
	struct apply_microcode_ctx ctx = { .err = 0 };
	int ret;

	ret = smp_call_function_single(cpu, apply_microcode_local, &ctx, 1);
	if (!ret)
		ret = ctx.err;

	return ret;
}

#ifdef CONFIG_MICROCODE_OLD_INTERFACE
static int do_microcode_update(const void __user *buf, size_t size)
{
	int error = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
		enum ucode_state ustate;

		if (!uci->valid)
			continue;

		ustate = microcode_ops->request_microcode_user(cpu, buf, size);
		if (ustate == UCODE_ERROR) {
			error = -1;
			break;
		} else if (ustate == UCODE_OK)
			apply_microcode_on_target(cpu);
	}

	return error;
}

static int microcode_open(struct inode *inode, struct file *file)
{
	return capable(CAP_SYS_RAWIO) ? nonseekable_open(inode, file) : -EPERM;
}

static ssize_t microcode_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *ppos)
{
	ssize_t ret = -EINVAL;

	if ((len >> PAGE_SHIFT) > totalram_pages) {
		pr_err("too much data (max %ld pages)\n", totalram_pages);
		return ret;
	}

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	if (do_microcode_update(buf, len) == 0)
		ret = (ssize_t)len;

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	return ret;
}

static const struct file_operations microcode_fops = {
	.owner			= THIS_MODULE,
	.write			= microcode_write,
	.open			= microcode_open,
	.llseek		= no_llseek,
};

static struct miscdevice microcode_dev = {
	.minor			= MICROCODE_MINOR,
	.name			= "microcode",
	.nodename		= "cpu/microcode",
	.fops			= &microcode_fops,
};

static int __init microcode_dev_init(void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error) {
		pr_err("can't misc_register on minor=%d\n", MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void __exit microcode_dev_exit(void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
MODULE_ALIAS("devname:cpu/microcode");
#else
#define microcode_dev_init()	0
#define microcode_dev_exit()	do { } while (0)
#endif

/* fake device for request_firmware */
static struct platform_device	*microcode_pdev;

static int reload_for_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	int err = 0;

	mutex_lock(&microcode_mutex);
	if (uci->valid) {
		enum ucode_state ustate;

		ustate = microcode_ops->request_microcode_fw(cpu, &microcode_pdev->dev);
		if (ustate == UCODE_OK)
			apply_microcode_on_target(cpu);
		else
			if (ustate == UCODE_ERROR)
				err = -EINVAL;
	}
	mutex_unlock(&microcode_mutex);

	return err;
}

static ssize_t reload_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	unsigned long val;
	int cpu = dev->id;
	int ret = 0;
	char *end;

	val = simple_strtoul(buf, &end, 0);
	if (end == buf)
		return -EINVAL;

	if (val == 1) {
		get_online_cpus();
		if (cpu_online(cpu))
			ret = reload_for_cpu(cpu);
		put_online_cpus();
	}

	if (!ret)
		ret = size;

	return ret;
}

static ssize_t version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.rev);
}

static ssize_t pf_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.pf);
}

static DEVICE_ATTR(reload, 0200, NULL, reload_store);
static DEVICE_ATTR(version, 0400, version_show, NULL);
static DEVICE_ATTR(processor_flags, 0400, pf_show, NULL);

static struct attribute *mc_default_attrs[] = {
	&dev_attr_reload.attr,
	&dev_attr_version.attr,
	&dev_attr_processor_flags.attr,
	NULL
};

static struct attribute_group mc_attr_group = {
	.attrs			= mc_default_attrs,
	.name			= "microcode",
};

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	microcode_ops->microcode_fini_cpu(cpu);
	uci->valid = 0;
}

static enum ucode_state microcode_resume_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (!uci->mc)
		return UCODE_NFOUND;

	pr_debug("CPU%d updated upon resume\n", cpu);
	apply_microcode_on_target(cpu);

	return UCODE_OK;
}

static enum ucode_state microcode_init_cpu(int cpu)
{
	enum ucode_state ustate;

	if (collect_cpu_info(cpu))
		return UCODE_ERROR;

	/* --dimm. Trigger a delayed update? */
	if (system_state != SYSTEM_RUNNING)
		return UCODE_NFOUND;

	ustate = microcode_ops->request_microcode_fw(cpu, &microcode_pdev->dev);

	if (ustate == UCODE_OK) {
		pr_debug("CPU%d updated upon init\n", cpu);
		apply_microcode_on_target(cpu);
	}

	return ustate;
}

static enum ucode_state microcode_update_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	enum ucode_state ustate;

	if (uci->valid)
		ustate = microcode_resume_cpu(cpu);
	else
		ustate = microcode_init_cpu(cpu);

	return ustate;
}

static int mc_device_add(struct device *dev, struct subsys_interface *sif)
{
	int err, cpu = dev->id;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("CPU%d added\n", cpu);

	err = sysfs_create_group(&dev->kobj, &mc_attr_group);
	if (err)
		return err;

	if (microcode_init_cpu(cpu) == UCODE_ERROR) {
		sysfs_remove_group(&dev->kobj, &mc_attr_group);
		return -EINVAL;
	}

	return err;
}

static int mc_device_remove(struct device *dev, struct subsys_interface *sif)
{
	int cpu = dev->id;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("CPU%d removed\n", cpu);
	microcode_fini_cpu(cpu);
	sysfs_remove_group(&dev->kobj, &mc_attr_group);
	return 0;
}

static struct subsys_interface mc_cpu_interface = {
	.name			= "microcode",
	.subsys			= &cpu_subsys,
	.add_dev		= mc_device_add,
	.remove_dev		= mc_device_remove,
};

/**
 * mc_bp_resume - Update boot CPU microcode during resume.
 */
static void mc_bp_resume(void)
{
	int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (uci->valid && uci->mc)
		microcode_ops->apply_microcode(cpu);
}

static struct syscore_ops mc_syscore_ops = {
	.resume			= mc_bp_resume,
};

static __cpuinit int
mc_cpu_callback(struct notifier_block *nb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct device *dev;

	dev = get_cpu_device(cpu);
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		microcode_update_cpu(cpu);
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		pr_debug("CPU%d added\n", cpu);
		if (sysfs_create_group(&dev->kobj, &mc_attr_group))
			pr_err("Failed to create group for CPU%d\n", cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		/* Suspend is in progress, only remove the interface */
		sysfs_remove_group(&dev->kobj, &mc_attr_group);
		pr_debug("CPU%d removed\n", cpu);
		break;

	/*
	 * When a CPU goes offline, don't free up or invalidate the copy of
	 * the microcode in kernel memory, so that we can reuse it when the
	 * CPU comes back online without unnecessarily requesting the userspace
	 * for it again.
	 */
	case CPU_UP_CANCELED_FROZEN:
		/* The CPU refused to come up during a system resume */
		microcode_fini_cpu(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata mc_cpu_notifier = {
	.notifier_call	= mc_cpu_callback,
};

static int __init microcode_init(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);
	int error;

	if (c->x86_vendor == X86_VENDOR_INTEL)
		microcode_ops = init_intel_microcode();
	else if (c->x86_vendor == X86_VENDOR_AMD)
		microcode_ops = init_amd_microcode();

	if (!microcode_ops) {
		pr_err("no support for this CPU vendor\n");
		return -ENODEV;
	}

	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev))
		return PTR_ERR(microcode_pdev);

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	error = subsys_interface_register(&mc_cpu_interface);

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	if (error)
		goto out_pdev;

	error = microcode_dev_init();
	if (error)
		goto out_driver;

	register_syscore_ops(&mc_syscore_ops);
	register_hotcpu_notifier(&mc_cpu_notifier);

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION
		" <tigran@aivazian.fsnet.co.uk>, Peter Oruba\n");

	return 0;

out_driver:
	get_online_cpus();
	mutex_lock(&microcode_mutex);

	subsys_interface_unregister(&mc_cpu_interface);

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

out_pdev:
	platform_device_unregister(microcode_pdev);
	return error;

}
module_init(microcode_init);

static void __exit microcode_exit(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);

	microcode_dev_exit();

	unregister_hotcpu_notifier(&mc_cpu_notifier);
	unregister_syscore_ops(&mc_syscore_ops);

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	subsys_interface_unregister(&mc_cpu_interface);

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	platform_device_unregister(microcode_pdev);

	microcode_ops = NULL;

	if (c->x86_vendor == X86_VENDOR_AMD)
		exit_amd_microcode();

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION " removed.\n");
}
module_exit(microcode_exit);
