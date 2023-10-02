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
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/apic.h>
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
enum sibling_ctrl {
	/* Spinwait with timeout */
	SCTRL_WAIT,
	/* Invoke the microcode_apply() callback */
	SCTRL_APPLY,
	/* Proceed without invoking the microcode_apply() callback */
	SCTRL_DONE,
};

struct microcode_ctrl {
	enum sibling_ctrl	ctrl;
	enum ucode_state	result;
	unsigned int		ctrl_cpu;
	bool			nmi_enabled;
};

DEFINE_STATIC_KEY_FALSE(microcode_nmi_handler_enable);
static DEFINE_PER_CPU(struct microcode_ctrl, ucode_ctrl);
static atomic_t late_cpus_in, offline_in_nmi;
static unsigned int loops_per_usec;
static cpumask_t cpu_offline_mask;

static noinstr bool wait_for_cpus(atomic_t *cnt)
{
	unsigned int timeout, loops;

	WARN_ON_ONCE(raw_atomic_dec_return(cnt) < 0);

	for (timeout = 0; timeout < USEC_PER_SEC; timeout++) {
		if (!raw_atomic_read(cnt))
			return true;

		for (loops = 0; loops < loops_per_usec; loops++)
			cpu_relax();

		/* If invoked directly, tickle the NMI watchdog */
		if (!microcode_ops->use_nmi && !(timeout % USEC_PER_MSEC)) {
			instrumentation_begin();
			touch_nmi_watchdog();
			instrumentation_end();
		}
	}
	/* Prevent the late comers from making progress and let them time out */
	raw_atomic_inc(cnt);
	return false;
}

static noinstr bool wait_for_ctrl(void)
{
	unsigned int timeout, loops;

	for (timeout = 0; timeout < USEC_PER_SEC; timeout++) {
		if (raw_cpu_read(ucode_ctrl.ctrl) != SCTRL_WAIT)
			return true;

		for (loops = 0; loops < loops_per_usec; loops++)
			cpu_relax();

		/* If invoked directly, tickle the NMI watchdog */
		if (!microcode_ops->use_nmi && !(timeout % USEC_PER_MSEC)) {
			instrumentation_begin();
			touch_nmi_watchdog();
			instrumentation_end();
		}
	}
	return false;
}

/*
 * Protected against instrumentation up to the point where the primary
 * thread completed the update. See microcode_nmi_handler() for details.
 */
static noinstr bool load_secondary_wait(unsigned int ctrl_cpu)
{
	/* Initial rendezvous to ensure that all CPUs have arrived */
	if (!wait_for_cpus(&late_cpus_in)) {
		raw_cpu_write(ucode_ctrl.result, UCODE_TIMEOUT);
		return false;
	}

	/*
	 * Wait for primary threads to complete. If one of them hangs due
	 * to the update, there is no way out. This is non-recoverable
	 * because the CPU might hold locks or resources and confuse the
	 * scheduler, watchdogs etc. There is no way to safely evacuate the
	 * machine.
	 */
	if (wait_for_ctrl())
		return true;

	instrumentation_begin();
	panic("Microcode load: Primary CPU %d timed out\n", ctrl_cpu);
	instrumentation_end();
}

/*
 * Protected against instrumentation up to the point where the primary
 * thread completed the update. See microcode_nmi_handler() for details.
 */
static noinstr void load_secondary(unsigned int cpu)
{
	unsigned int ctrl_cpu = raw_cpu_read(ucode_ctrl.ctrl_cpu);
	enum ucode_state ret;

	if (!load_secondary_wait(ctrl_cpu)) {
		instrumentation_begin();
		pr_err_once("load: %d CPUs timed out\n",
			    atomic_read(&late_cpus_in) - 1);
		instrumentation_end();
		return;
	}

	/* Primary thread completed. Allow to invoke instrumentable code */
	instrumentation_begin();
	/*
	 * If the primary succeeded then invoke the apply() callback,
	 * otherwise copy the state from the primary thread.
	 */
	if (this_cpu_read(ucode_ctrl.ctrl) == SCTRL_APPLY)
		ret = microcode_ops->apply_microcode(cpu);
	else
		ret = per_cpu(ucode_ctrl.result, ctrl_cpu);

	this_cpu_write(ucode_ctrl.result, ret);
	this_cpu_write(ucode_ctrl.ctrl, SCTRL_DONE);
	instrumentation_end();
}

static void __load_primary(unsigned int cpu)
{
	struct cpumask *secondaries = topology_sibling_cpumask(cpu);
	enum sibling_ctrl ctrl;
	enum ucode_state ret;
	unsigned int sibling;

	/* Initial rendezvous to ensure that all CPUs have arrived */
	if (!wait_for_cpus(&late_cpus_in)) {
		this_cpu_write(ucode_ctrl.result, UCODE_TIMEOUT);
		pr_err_once("load: %d CPUs timed out\n", atomic_read(&late_cpus_in) - 1);
		return;
	}

	ret = microcode_ops->apply_microcode(cpu);
	this_cpu_write(ucode_ctrl.result, ret);
	this_cpu_write(ucode_ctrl.ctrl, SCTRL_DONE);

	/*
	 * If the update was successful, let the siblings run the apply()
	 * callback. If not, tell them it's done. This also covers the
	 * case where the CPU has uniform loading at package or system
	 * scope implemented but does not advertise it.
	 */
	if (ret == UCODE_UPDATED || ret == UCODE_OK)
		ctrl = SCTRL_APPLY;
	else
		ctrl = SCTRL_DONE;

	for_each_cpu(sibling, secondaries) {
		if (sibling != cpu)
			per_cpu(ucode_ctrl.ctrl, sibling) = ctrl;
	}
}

static bool kick_offline_cpus(unsigned int nr_offl)
{
	unsigned int cpu, timeout;

	for_each_cpu(cpu, &cpu_offline_mask) {
		/* Enable the rendezvous handler and send NMI */
		per_cpu(ucode_ctrl.nmi_enabled, cpu) = true;
		apic_send_nmi_to_offline_cpu(cpu);
	}

	/* Wait for them to arrive */
	for (timeout = 0; timeout < (USEC_PER_SEC / 2); timeout++) {
		if (atomic_read(&offline_in_nmi) == nr_offl)
			return true;
		udelay(1);
	}
	/* Let the others time out */
	return false;
}

static void release_offline_cpus(void)
{
	unsigned int cpu;

	for_each_cpu(cpu, &cpu_offline_mask)
		per_cpu(ucode_ctrl.ctrl, cpu) = SCTRL_DONE;
}

static void load_primary(unsigned int cpu)
{
	unsigned int nr_offl = cpumask_weight(&cpu_offline_mask);
	bool proceed = true;

	/* Kick soft-offlined SMT siblings if required */
	if (!cpu && nr_offl)
		proceed = kick_offline_cpus(nr_offl);

	/* If the soft-offlined CPUs did not respond, abort */
	if (proceed)
		__load_primary(cpu);

	/* Unconditionally release soft-offlined SMT siblings if required */
	if (!cpu && nr_offl)
		release_offline_cpus();
}

/*
 * Minimal stub rendezvous handler for soft-offlined CPUs which participate
 * in the NMI rendezvous to protect against a concurrent NMI on affected
 * CPUs.
 */
void noinstr microcode_offline_nmi_handler(void)
{
	if (!raw_cpu_read(ucode_ctrl.nmi_enabled))
		return;
	raw_cpu_write(ucode_ctrl.nmi_enabled, false);
	raw_cpu_write(ucode_ctrl.result, UCODE_OFFLINE);
	raw_atomic_inc(&offline_in_nmi);
	wait_for_ctrl();
}

static noinstr bool microcode_update_handler(void)
{
	unsigned int cpu = raw_smp_processor_id();

	if (raw_cpu_read(ucode_ctrl.ctrl_cpu) == cpu) {
		instrumentation_begin();
		load_primary(cpu);
		instrumentation_end();
	} else {
		load_secondary(cpu);
	}

	instrumentation_begin();
	touch_nmi_watchdog();
	instrumentation_end();

	return true;
}

/*
 * Protection against instrumentation is required for CPUs which are not
 * safe against an NMI which is delivered to the secondary SMT sibling
 * while the primary thread updates the microcode. Instrumentation can end
 * up in #INT3, #DB and #PF. The IRET from those exceptions reenables NMI
 * which is the opposite of what the NMI rendezvous is trying to achieve.
 *
 * The primary thread is safe versus instrumentation as the actual
 * microcode update handles this correctly. It's only the sibling code
 * path which must be NMI safe until the primary thread completed the
 * update.
 */
bool noinstr microcode_nmi_handler(void)
{
	if (!raw_cpu_read(ucode_ctrl.nmi_enabled))
		return false;

	raw_cpu_write(ucode_ctrl.nmi_enabled, false);
	return microcode_update_handler();
}

static int load_cpus_stopped(void *unused)
{
	if (microcode_ops->use_nmi) {
		/* Enable the NMI handler and raise NMI */
		this_cpu_write(ucode_ctrl.nmi_enabled, true);
		apic->send_IPI(smp_processor_id(), NMI_VECTOR);
	} else {
		/* Just invoke the handler directly */
		microcode_update_handler();
	}
	return 0;
}

static int load_late_stop_cpus(void)
{
	unsigned int cpu, updated = 0, failed = 0, timedout = 0, siblings = 0;
	unsigned int nr_offl, offline = 0;
	int old_rev = boot_cpu_data.microcode;
	struct cpuinfo_x86 prev_info;

	pr_err("Attempting late microcode loading - it is dangerous and taints the kernel.\n");
	pr_err("You should switch to early loading, if possible.\n");

	atomic_set(&late_cpus_in, num_online_cpus());
	atomic_set(&offline_in_nmi, 0);
	loops_per_usec = loops_per_jiffy / (TICK_NSEC / 1000);

	/*
	 * Take a snapshot before the microcode update in order to compare and
	 * check whether any bits changed after an update.
	 */
	store_cpu_caps(&prev_info);

	if (microcode_ops->use_nmi)
		static_branch_enable_cpuslocked(&microcode_nmi_handler_enable);

	stop_machine_cpuslocked(load_cpus_stopped, NULL, cpu_online_mask);

	if (microcode_ops->use_nmi)
		static_branch_disable_cpuslocked(&microcode_nmi_handler_enable);

	/* Analyze the results */
	for_each_cpu_and(cpu, cpu_present_mask, &cpus_booted_once_mask) {
		switch (per_cpu(ucode_ctrl.result, cpu)) {
		case UCODE_UPDATED:	updated++; break;
		case UCODE_TIMEOUT:	timedout++; break;
		case UCODE_OK:		siblings++; break;
		case UCODE_OFFLINE:	offline++; break;
		default:		failed++; break;
		}
	}

	if (microcode_ops->finalize_late_load)
		microcode_ops->finalize_late_load(!updated);

	if (!updated) {
		/* Nothing changed. */
		if (!failed && !timedout)
			return 0;

		nr_offl = cpumask_weight(&cpu_offline_mask);
		if (offline < nr_offl) {
			pr_warn("%u offline siblings did not respond.\n",
				nr_offl - atomic_read(&offline_in_nmi));
			return -EIO;
		}
		pr_err("update failed: %u CPUs failed %u CPUs timed out\n",
		       failed, timedout);
		return -EIO;
	}

	add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
	pr_info("load: updated on %u primary CPUs with %u siblings\n", updated, siblings);
	if (failed || timedout) {
		pr_err("load incomplete. %u CPUs timed out or failed\n",
		       num_online_cpus() - (updated + siblings));
	}
	pr_info("revision: 0x%x -> 0x%x\n", old_rev, boot_cpu_data.microcode);
	microcode_check(&prev_info);

	return updated + siblings == num_online_cpus() ? 0 : -EIO;
}

/*
 * This function does two things:
 *
 * 1) Ensure that all required CPUs which are present and have been booted
 *    once are online.
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
 *
 *    As soft-offlined CPUs still react on NMIs, the SMT sibling
 *    restriction can be lifted when the vendor driver signals to use NMI
 *    for rendezvous and the APIC provides a mechanism to send an NMI to a
 *    soft-offlined CPU. The soft-offlined CPUs are then able to
 *    participate in the rendezvous in a trivial stub handler.
 *
 * 2) Initialize the per CPU control structure and create a cpumask
 *    which contains "offline"; secondary threads, so they can be handled
 *    correctly by a control CPU.
 */
static bool setup_cpus(void)
{
	struct microcode_ctrl ctrl = { .ctrl = SCTRL_WAIT, .result = -1, };
	bool allow_smt_offline;
	unsigned int cpu;

	allow_smt_offline = microcode_ops->nmi_safe ||
		(microcode_ops->use_nmi && apic->nmi_to_offline_cpu);

	cpumask_clear(&cpu_offline_mask);

	for_each_cpu_and(cpu, cpu_present_mask, &cpus_booted_once_mask) {
		/*
		 * Offline CPUs sit in one of the play_dead() functions
		 * with interrupts disabled, but they still react on NMIs
		 * and execute arbitrary code. Also MWAIT being updated
		 * while the offline CPU sits there is not necessarily safe
		 * on all CPU variants.
		 *
		 * Mark them in the offline_cpus mask which will be handled
		 * by CPU0 later in the update process.
		 *
		 * Ensure that the primary thread is online so that it is
		 * guaranteed that all cores are updated.
		 */
		if (!cpu_online(cpu)) {
			if (topology_is_primary_thread(cpu) || !allow_smt_offline) {
				pr_err("CPU %u not online, loading aborted\n", cpu);
				return false;
			}
			cpumask_set_cpu(cpu, &cpu_offline_mask);
			per_cpu(ucode_ctrl, cpu) = ctrl;
			continue;
		}

		/*
		 * Initialize the per CPU state. This is core scope for now,
		 * but prepared to take package or system scope into account.
		 */
		ctrl.ctrl_cpu = cpumask_first(topology_sibling_cpumask(cpu));
		per_cpu(ucode_ctrl, cpu) = ctrl;
	}
	return true;
}

static int load_late_locked(void)
{
	if (!setup_cpus())
		return -EBUSY;

	switch (microcode_ops->request_microcode_fw(0, &microcode_pdev->dev)) {
	case UCODE_NEW:
		return load_late_stop_cpus();
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
	ret = load_late_locked();
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
