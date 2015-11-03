/*
 * CPU Microcode Update Driver for Linux
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *	      2006	Shaohua Li <shaohua.li@intel.com>
 *	      2013-2015	Borislav Petkov <bp@alien8.de>
 *
 * This driver allows to upgrade microcode on x86 processors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include <asm/cpu_device_id.h>
#include <asm/perf_event.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

#define MICROCODE_VERSION	"2.00"

static struct microcode_ops	*microcode_ops;

bool dis_ucode_ldr;
module_param(dis_ucode_ldr, bool, 0);

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

	if (ret > 0)
		perf_check_microcode();

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
	enum ucode_state ustate;
	int err = 0;

	if (!uci->valid)
		return err;

	ustate = microcode_ops->request_microcode_fw(cpu, &microcode_pdev->dev, true);
	if (ustate == UCODE_OK)
		apply_microcode_on_target(cpu);
	else
		if (ustate == UCODE_ERROR)
			err = -EINVAL;
	return err;
}

static ssize_t reload_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	unsigned long val;
	int cpu;
	ssize_t ret = 0, tmp_ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val != 1)
		return size;

	get_online_cpus();
	mutex_lock(&microcode_mutex);
	for_each_online_cpu(cpu) {
		tmp_ret = reload_for_cpu(cpu);
		if (tmp_ret != 0)
			pr_warn("Error reloading microcode on CPU %d\n", cpu);

		/* save retval of the first encountered reload error */
		if (!ret)
			ret = tmp_ret;
	}
	if (!ret)
		perf_check_microcode();
	mutex_unlock(&microcode_mutex);
	put_online_cpus();

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
	microcode_ops->microcode_fini_cpu(cpu);
}

static enum ucode_state microcode_resume_cpu(int cpu)
{
	pr_debug("CPU%d updated upon resume\n", cpu);

	if (apply_microcode_on_target(cpu))
		return UCODE_ERROR;

	return UCODE_OK;
}

static enum ucode_state microcode_init_cpu(int cpu, bool refresh_fw)
{
	enum ucode_state ustate;
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (uci && uci->valid)
		return UCODE_OK;

	if (collect_cpu_info(cpu))
		return UCODE_ERROR;

	/* --dimm. Trigger a delayed update? */
	if (system_state != SYSTEM_RUNNING)
		return UCODE_NFOUND;

	ustate = microcode_ops->request_microcode_fw(cpu, &microcode_pdev->dev,
						     refresh_fw);

	if (ustate == UCODE_OK) {
		pr_debug("CPU%d updated upon init\n", cpu);
		apply_microcode_on_target(cpu);
	}

	return ustate;
}

static enum ucode_state microcode_update_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (uci->valid)
		return microcode_resume_cpu(cpu);

	return microcode_init_cpu(cpu, false);
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

	if (microcode_init_cpu(cpu, true) == UCODE_ERROR)
		return -EINVAL;

	return err;
}

static void mc_device_remove(struct device *dev, struct subsys_interface *sif)
{
	int cpu = dev->id;

	if (!cpu_online(cpu))
		return;

	pr_debug("CPU%d removed\n", cpu);
	microcode_fini_cpu(cpu);
	sysfs_remove_group(&dev->kobj, &mc_attr_group);
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
	else if (!uci->mc)
		reload_early_microcode();
}

static struct syscore_ops mc_syscore_ops = {
	.resume			= mc_bp_resume,
};

static int
mc_cpu_callback(struct notifier_block *nb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct device *dev;

	dev = get_cpu_device(cpu);

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		microcode_update_cpu(cpu);
		pr_debug("CPU%d added\n", cpu);
		/*
		 * "break" is missing on purpose here because we want to fall
		 * through in order to create the sysfs group.
		 */

	case CPU_DOWN_FAILED:
		if (sysfs_create_group(&dev->kobj, &mc_attr_group))
			pr_err("Failed to create group for CPU%d\n", cpu);
		break;

	case CPU_DOWN_PREPARE:
		/* Suspend is in progress, only remove the interface */
		sysfs_remove_group(&dev->kobj, &mc_attr_group);
		pr_debug("CPU%d removed\n", cpu);
		break;

	/*
	 * case CPU_DEAD:
	 *
	 * When a CPU goes offline, don't free up or invalidate the copy of
	 * the microcode in kernel memory, so that we can reuse it when the
	 * CPU comes back online without unnecessarily requesting the userspace
	 * for it again.
	 */
	}

	/* The CPU refused to come up during a system resume */
	if (action == CPU_UP_CANCELED_FROZEN)
		microcode_fini_cpu(cpu);

	return NOTIFY_OK;
}

static struct notifier_block mc_cpu_notifier = {
	.notifier_call	= mc_cpu_callback,
};

#ifdef MODULE
/* Autoload on Intel and AMD systems */
static const struct x86_cpu_id __initconst microcode_id[] = {
#ifdef CONFIG_MICROCODE_INTEL
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, },
#endif
#ifdef CONFIG_MICROCODE_AMD
	{ X86_VENDOR_AMD, X86_FAMILY_ANY, X86_MODEL_ANY, },
#endif
	{}
};
MODULE_DEVICE_TABLE(x86cpu, microcode_id);
#endif

static struct attribute *cpu_root_microcode_attrs[] = {
	&dev_attr_reload.attr,
	NULL
};

static struct attribute_group cpu_root_microcode_group = {
	.name  = "microcode",
	.attrs = cpu_root_microcode_attrs,
};

static int __init microcode_init(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);
	int error;

	if (paravirt_enabled() || dis_ucode_ldr)
		return -EINVAL;

	if (c->x86_vendor == X86_VENDOR_INTEL)
		microcode_ops = init_intel_microcode();
	else if (c->x86_vendor == X86_VENDOR_AMD)
		microcode_ops = init_amd_microcode();
	else
		pr_err("no support for this CPU vendor\n");

	if (!microcode_ops)
		return -ENODEV;

	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev))
		return PTR_ERR(microcode_pdev);

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	error = subsys_interface_register(&mc_cpu_interface);
	if (!error)
		perf_check_microcode();
	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	if (error)
		goto out_pdev;

	error = sysfs_create_group(&cpu_subsys.dev_root->kobj,
				   &cpu_root_microcode_group);

	if (error) {
		pr_err("Error creating microcode group!\n");
		goto out_driver;
	}

	error = microcode_dev_init();
	if (error)
		goto out_ucode_group;

	register_syscore_ops(&mc_syscore_ops);
	register_hotcpu_notifier(&mc_cpu_notifier);

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION
		" <tigran@aivazian.fsnet.co.uk>, Peter Oruba\n");

	return 0;

 out_ucode_group:
	sysfs_remove_group(&cpu_subsys.dev_root->kobj,
			   &cpu_root_microcode_group);

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

	sysfs_remove_group(&cpu_subsys.dev_root->kobj,
			   &cpu_root_microcode_group);

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
