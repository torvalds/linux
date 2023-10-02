// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPU Microcode Update Driver for Linux
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <aivazian.tigran@gmail.com>
 *	      2006	Shaohua Li <shaohua.li@intel.com>
 *	      2013-2016	Borislav Petkov <bp@alien8.de>
 *
 * X86 CPU microcode early update for Linux:
 *
 *	Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *			   H Peter Anvin" <hpa@zytor.com>
 *		  (C) 2015 Borislav Petkov <bp@alien8.de>
 *
 * This driver allows to upgrade microcode on x86 processors.
 */

#define pr_fmt(fmt) "microcode: " fmt

#include <linux/platform_device.h>
#include <linux/stop_machine.h>
#include <linux/syscore_ops.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/cpu_device_id.h>
#include <asm/perf_event.h>
#include <asm/processor.h>
#include <asm/cmdline.h>
#include <asm/setup.h>

#include "internal.h"

#define DRIVER_VERSION	"2.2"

static struct microcode_ops	*microcode_ops;
bool dis_ucode_ldr = true;

/*
 * Synchronization.
 *
 * All non cpu-hotplug-callback call sites use:
 *
 * - cpus_read_lock/unlock() to synchronize with
 *   the cpu-hotplug-callback call sites.
 *
 * We guarantee that only a single cpu is being
 * updated at any particular moment of time.
 */
struct ucode_cpu_info		ucode_cpu_info[NR_CPUS];

struct cpu_info_ctx {
	struct cpu_signature	*cpu_sig;
	int			err;
};

/*
 * Those patch levels cannot be updated to newer ones and thus should be final.
 */
static u32 final_levels[] = {
	0x01000098,
	0x0100009f,
	0x010000af,
	0, /* T-101 terminator */
};

/*
 * Check the current patch level on this CPU.
 *
 * Returns:
 *  - true: if update should stop
 *  - false: otherwise
 */
static bool amd_check_current_patch_level(void)
{
	u32 lvl, dummy, i;
	u32 *levels;

	native_rdmsr(MSR_AMD64_PATCH_LEVEL, lvl, dummy);

	levels = final_levels;

	for (i = 0; levels[i]; i++) {
		if (lvl == levels[i])
			return true;
	}
	return false;
}

static bool __init check_loader_disabled_bsp(void)
{
	static const char *__dis_opt_str = "dis_ucode_ldr";
	const char *cmdline = boot_command_line;
	const char *option  = __dis_opt_str;

	/*
	 * CPUID(1).ECX[31]: reserved for hypervisor use. This is still not
	 * completely accurate as xen pv guests don't see that CPUID bit set but
	 * that's good enough as they don't land on the BSP path anyway.
	 */
	if (native_cpuid_ecx(1) & BIT(31))
		return true;

	if (x86_cpuid_vendor() == X86_VENDOR_AMD) {
		if (amd_check_current_patch_level())
			return true;
	}

	if (cmdline_find_option_bool(cmdline, option) <= 0)
		dis_ucode_ldr = false;

	return dis_ucode_ldr;
}

void __init load_ucode_bsp(void)
{
	unsigned int cpuid_1_eax;
	bool intel = true;

	if (!have_cpuid_p())
		return;

	cpuid_1_eax = native_cpuid_eax(1);

	switch (x86_cpuid_vendor()) {
	case X86_VENDOR_INTEL:
		if (x86_family(cpuid_1_eax) < 6)
			return;
		break;

	case X86_VENDOR_AMD:
		if (x86_family(cpuid_1_eax) < 0x10)
			return;
		intel = false;
		break;

	default:
		return;
	}

	if (check_loader_disabled_bsp())
		return;

	if (intel)
		load_ucode_intel_bsp();
	else
		load_ucode_amd_bsp(cpuid_1_eax);
}

void load_ucode_ap(void)
{
	unsigned int cpuid_1_eax;

	if (dis_ucode_ldr)
		return;

	cpuid_1_eax = native_cpuid_eax(1);

	switch (x86_cpuid_vendor()) {
	case X86_VENDOR_INTEL:
		if (x86_family(cpuid_1_eax) >= 6)
			load_ucode_intel_ap();
		break;
	case X86_VENDOR_AMD:
		if (x86_family(cpuid_1_eax) >= 0x10)
			load_ucode_amd_ap(cpuid_1_eax);
		break;
	default:
		break;
	}
}

struct cpio_data __init find_microcode_in_initrd(const char *path)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long start = 0;
	size_t size;

#ifdef CONFIG_X86_32
	size = boot_params.hdr.ramdisk_size;
	/* Early load on BSP has a temporary mapping. */
	if (size)
		start = initrd_start_early;

#else /* CONFIG_X86_64 */
	size  = (unsigned long)boot_params.ext_ramdisk_size << 32;
	size |= boot_params.hdr.ramdisk_size;

	if (size) {
		start  = (unsigned long)boot_params.ext_ramdisk_image << 32;
		start |= boot_params.hdr.ramdisk_image;
		start += PAGE_OFFSET;
	}
#endif

	/*
	 * Fixup the start address: after reserve_initrd() runs, initrd_start
	 * has the virtual address of the beginning of the initrd. It also
	 * possibly relocates the ramdisk. In either case, initrd_start contains
	 * the updated address so use that instead.
	 */
	if (initrd_start)
		start = initrd_start;

	return find_cpio_data(path, (void *)start, size, NULL);
#else /* !CONFIG_BLK_DEV_INITRD */
	return (struct cpio_data){ NULL, 0, "" };
#endif
}

static void reload_early_microcode(unsigned int cpu)
{
	int vendor, family;

	vendor = x86_cpuid_vendor();
	family = x86_cpuid_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (family >= 6)
			reload_ucode_intel();
		break;
	case X86_VENDOR_AMD:
		if (family >= 0x10)
			reload_ucode_amd(cpu);
		break;
	default:
		break;
	}
}

/* fake device for request_firmware */
static struct platform_device	*microcode_pdev;

#ifdef CONFIG_MICROCODE_LATE_LOADING
/*
 * Late loading dance. Why the heavy-handed stomp_machine effort?
 *
 * - HT siblings must be idle and not execute other code while the other sibling
 *   is loading microcode in order to avoid any negative interactions caused by
 *   the loading.
 *
 * - In addition, microcode update on the cores must be serialized until this
 *   requirement can be relaxed in the future. Right now, this is conservative
 *   and good.
 */
#define SPINUNIT 100 /* 100 nsec */


static atomic_t late_cpus_in;
static atomic_t late_cpus_out;

static int __wait_for_cpus(atomic_t *t, long long timeout)
{
	int all_cpus = num_online_cpus();

	atomic_inc(t);

	while (atomic_read(t) < all_cpus) {
		if (timeout < SPINUNIT) {
			pr_err("Timeout while waiting for CPUs rendezvous, remaining: %d\n",
				all_cpus - atomic_read(t));
			return 1;
		}

		ndelay(SPINUNIT);
		timeout -= SPINUNIT;

		touch_nmi_watchdog();
	}
	return 0;
}

/*
 * Returns:
 * < 0 - on error
 *   0 - success (no update done or microcode was updated)
 */
static int __reload_late(void *info)
{
	int cpu = smp_processor_id();
	enum ucode_state err;
	int ret = 0;

	/*
	 * Wait for all CPUs to arrive. A load will not be attempted unless all
	 * CPUs show up.
	 * */
	if (__wait_for_cpus(&late_cpus_in, NSEC_PER_SEC))
		return -1;

	/*
	 * On an SMT system, it suffices to load the microcode on one sibling of
	 * the core because the microcode engine is shared between the threads.
	 * Synchronization still needs to take place so that no concurrent
	 * loading attempts happen on multiple threads of an SMT core. See
	 * below.
	 */
	if (cpumask_first(topology_sibling_cpumask(cpu)) == cpu)
		err = microcode_ops->apply_microcode(cpu);
	else
		goto wait_for_siblings;

	if (err >= UCODE_NFOUND) {
		if (err == UCODE_ERROR) {
			pr_warn("Error reloading microcode on CPU %d\n", cpu);
			ret = -1;
		}
	}

wait_for_siblings:
	if (__wait_for_cpus(&late_cpus_out, NSEC_PER_SEC))
		panic("Timeout during microcode update!\n");

	/*
	 * At least one thread has completed update on each core.
	 * For others, simply call the update to make sure the
	 * per-cpu cpuinfo can be updated with right microcode
	 * revision.
	 */
	if (cpumask_first(topology_sibling_cpumask(cpu)) != cpu)
		err = microcode_ops->apply_microcode(cpu);

	return ret;
}

/*
 * Reload microcode late on all CPUs. Wait for a sec until they
 * all gather together.
 */
static int microcode_reload_late(void)
{
	int old = boot_cpu_data.microcode, ret;
	struct cpuinfo_x86 prev_info;

	pr_err("Attempting late microcode loading - it is dangerous and taints the kernel.\n");
	pr_err("You should switch to early loading, if possible.\n");

	atomic_set(&late_cpus_in,  0);
	atomic_set(&late_cpus_out, 0);

	/*
	 * Take a snapshot before the microcode update in order to compare and
	 * check whether any bits changed after an update.
	 */
	store_cpu_caps(&prev_info);

	ret = stop_machine_cpuslocked(__reload_late, NULL, cpu_online_mask);

	if (microcode_ops->finalize_late_load)
		microcode_ops->finalize_late_load(ret);

	if (!ret) {
		pr_info("Reload succeeded, microcode revision: 0x%x -> 0x%x\n",
			old, boot_cpu_data.microcode);
		microcode_check(&prev_info);
		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
	} else {
		pr_info("Reload failed, current microcode revision: 0x%x\n",
			boot_cpu_data.microcode);
	}
	return ret;
}

/*
 *  Ensure that all required CPUs which are present and have been booted
 *  once are online.
 *
 *    To pass this check, all primary threads must be online.
 *
 *    If the microcode load is not safe against NMI then all SMT threads
 *    must be online as well because they still react to NMIs when they are
 *    soft-offlined and parked in one of the play_dead() variants. So if a
 *    NMI hits while the primary thread updates the microcode the resulting
 *    behaviour is undefined. The default play_dead() implementation on
 *    modern CPUs uses MWAIT, which is also not guaranteed to be safe
 *    against a microcode update which affects MWAIT.
 */
static bool ensure_cpus_are_online(void)
{
	unsigned int cpu;

	for_each_cpu_and(cpu, cpu_present_mask, &cpus_booted_once_mask) {
		if (!cpu_online(cpu)) {
			if (topology_is_primary_thread(cpu) || !microcode_ops->nmi_safe) {
				pr_err("CPU %u not online\n", cpu);
				return false;
			}
		}
	}
	return true;
}

static int ucode_load_late_locked(void)
{
	if (!ensure_cpus_are_online())
		return -EBUSY;

	switch (microcode_ops->request_microcode_fw(0, &microcode_pdev->dev)) {
	case UCODE_NEW:
		return microcode_reload_late();
	case UCODE_NFOUND:
		return -ENOENT;
	default:
		return -EBADFD;
	}
}

static ssize_t reload_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret || val != 1)
		return -EINVAL;

	cpus_read_lock();
	ret = ucode_load_late_locked();
	cpus_read_unlock();

	return ret ? : size;
}

static DEVICE_ATTR_WO(reload);
#endif

static ssize_t version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.rev);
}

static ssize_t processor_flags_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->cpu_sig.pf);
}

static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_RO(processor_flags);

static struct attribute *mc_default_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_processor_flags.attr,
	NULL
};

static const struct attribute_group mc_attr_group = {
	.attrs			= mc_default_attrs,
	.name			= "microcode",
};

static void microcode_fini_cpu(int cpu)
{
	if (microcode_ops->microcode_fini_cpu)
		microcode_ops->microcode_fini_cpu(cpu);
}

/**
 * microcode_bsp_resume - Update boot CPU microcode during resume.
 */
void microcode_bsp_resume(void)
{
	int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (uci->mc)
		microcode_ops->apply_microcode(cpu);
	else
		reload_early_microcode(cpu);
}

static struct syscore_ops mc_syscore_ops = {
	.resume	= microcode_bsp_resume,
};

static int mc_cpu_online(unsigned int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct device *dev = get_cpu_device(cpu);

	memset(uci, 0, sizeof(*uci));

	microcode_ops->collect_cpu_info(cpu, &uci->cpu_sig);
	cpu_data(cpu).microcode = uci->cpu_sig.rev;
	if (!cpu)
		boot_cpu_data.microcode = uci->cpu_sig.rev;

	if (sysfs_create_group(&dev->kobj, &mc_attr_group))
		pr_err("Failed to create group for CPU%d\n", cpu);
	return 0;
}

static int mc_cpu_down_prep(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	microcode_fini_cpu(cpu);
	sysfs_remove_group(&dev->kobj, &mc_attr_group);
	return 0;
}

static struct attribute *cpu_root_microcode_attrs[] = {
#ifdef CONFIG_MICROCODE_LATE_LOADING
	&dev_attr_reload.attr,
#endif
	NULL
};

static const struct attribute_group cpu_root_microcode_group = {
	.name  = "microcode",
	.attrs = cpu_root_microcode_attrs,
};

static int __init microcode_init(void)
{
	struct device *dev_root;
	struct cpuinfo_x86 *c = &boot_cpu_data;
	int error;

	if (dis_ucode_ldr)
		return -EINVAL;

	if (c->x86_vendor == X86_VENDOR_INTEL)
		microcode_ops = init_intel_microcode();
	else if (c->x86_vendor == X86_VENDOR_AMD)
		microcode_ops = init_amd_microcode();
	else
		pr_err("no support for this CPU vendor\n");

	if (!microcode_ops)
		return -ENODEV;

	microcode_pdev = platform_device_register_simple("microcode", -1, NULL, 0);
	if (IS_ERR(microcode_pdev))
		return PTR_ERR(microcode_pdev);

	dev_root = bus_get_dev_root(&cpu_subsys);
	if (dev_root) {
		error = sysfs_create_group(&dev_root->kobj, &cpu_root_microcode_group);
		put_device(dev_root);
		if (error) {
			pr_err("Error creating microcode group!\n");
			goto out_pdev;
		}
	}

	register_syscore_ops(&mc_syscore_ops);
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/microcode:online",
			  mc_cpu_online, mc_cpu_down_prep);

	pr_info("Microcode Update Driver: v%s.", DRIVER_VERSION);

	return 0;

 out_pdev:
	platform_device_unregister(microcode_pdev);
	return error;

}
late_initcall(microcode_init);
