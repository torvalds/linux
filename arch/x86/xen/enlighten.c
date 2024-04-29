// SPDX-License-Identifier: GPL-2.0

#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG
#include <linux/memblock.h>
#endif
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/panic_notifier.h>

#include <xen/xen.h>
#include <xen/features.h>
#include <xen/interface/sched.h>
#include <xen/interface/version.h>
#include <xen/page.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/cpu.h>
#include <asm/e820/api.h> 
#include <asm/setup.h>

#include "xen-ops.h"
#include "smp.h"
#include "pmu.h"

EXPORT_SYMBOL_GPL(hypercall_page);

/*
 * Pointer to the xen_vcpu_info structure or
 * &HYPERVISOR_shared_info->vcpu_info[cpu]. See xen_hvm_init_shared_info
 * and xen_vcpu_setup for details. By default it points to share_info->vcpu_info
 * but during boot it is switched to point to xen_vcpu_info.
 * The pointer is used in xen_evtchn_do_upcall to acknowledge pending events.
 * Make sure that xen_vcpu_info doesn't cross a page boundary by making it
 * cache-line aligned (the struct is guaranteed to have a size of 64 bytes,
 * which matches the cache line size of 64-bit x86 processors).
 */
DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
DEFINE_PER_CPU_ALIGNED(struct vcpu_info, xen_vcpu_info);

/* Linux <-> Xen vCPU id mapping */
DEFINE_PER_CPU(uint32_t, xen_vcpu_id);
EXPORT_PER_CPU_SYMBOL(xen_vcpu_id);

unsigned long *machine_to_phys_mapping = (void *)MACH2PHYS_VIRT_START;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned long  machine_to_phys_nr;
EXPORT_SYMBOL(machine_to_phys_nr);

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

struct shared_info xen_dummy_shared_info;

__read_mostly bool xen_have_vector_callback = true;
EXPORT_SYMBOL_GPL(xen_have_vector_callback);

/*
 * NB: These need to live in .data or alike because they're used by
 * xen_prepare_pvh() which runs before clearing the bss.
 */
enum xen_domain_type __ro_after_init xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);
uint32_t __ro_after_init xen_start_flags;
EXPORT_SYMBOL(xen_start_flags);

/*
 * Point at some empty memory to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
struct shared_info *HYPERVISOR_shared_info = &xen_dummy_shared_info;

static int xen_cpu_up_online(unsigned int cpu)
{
	xen_init_lock_cpu(cpu);
	return 0;
}

int xen_cpuhp_setup(int (*cpu_up_prepare_cb)(unsigned int),
		    int (*cpu_dead_cb)(unsigned int))
{
	int rc;

	rc = cpuhp_setup_state_nocalls(CPUHP_XEN_PREPARE,
				       "x86/xen/guest:prepare",
				       cpu_up_prepare_cb, cpu_dead_cb);
	if (rc >= 0) {
		rc = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					       "x86/xen/guest:online",
					       xen_cpu_up_online, NULL);
		if (rc < 0)
			cpuhp_remove_state_nocalls(CPUHP_XEN_PREPARE);
	}

	return rc >= 0 ? 0 : rc;
}

static void xen_vcpu_setup_restore(int cpu)
{
	/* Any per_cpu(xen_vcpu) is stale, so reset it */
	xen_vcpu_info_reset(cpu);

	/*
	 * For PVH and PVHVM, setup online VCPUs only. The rest will
	 * be handled by hotplug.
	 */
	if (xen_pv_domain() ||
	    (xen_hvm_domain() && cpu_online(cpu)))
		xen_vcpu_setup(cpu);
}

/*
 * On restore, set the vcpu placement up again.
 * If it fails, then we're in a bad state, since
 * we can't back out from using it...
 */
void xen_vcpu_restore(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		bool other_cpu = (cpu != smp_processor_id());
		bool is_up;

		if (xen_vcpu_nr(cpu) == XEN_VCPU_ID_INVALID)
			continue;

		/* Only Xen 4.5 and higher support this. */
		is_up = HYPERVISOR_vcpu_op(VCPUOP_is_up,
					   xen_vcpu_nr(cpu), NULL) > 0;

		if (other_cpu && is_up &&
		    HYPERVISOR_vcpu_op(VCPUOP_down, xen_vcpu_nr(cpu), NULL))
			BUG();

		if (xen_pv_domain() || xen_feature(XENFEAT_hvm_safe_pvclock))
			xen_setup_runstate_info(cpu);

		xen_vcpu_setup_restore(cpu);

		if (other_cpu && is_up &&
		    HYPERVISOR_vcpu_op(VCPUOP_up, xen_vcpu_nr(cpu), NULL))
			BUG();
	}
}

void xen_vcpu_info_reset(int cpu)
{
	if (xen_vcpu_nr(cpu) < MAX_VIRT_CPUS) {
		per_cpu(xen_vcpu, cpu) =
			&HYPERVISOR_shared_info->vcpu_info[xen_vcpu_nr(cpu)];
	} else {
		/* Set to NULL so that if somebody accesses it we get an OOPS */
		per_cpu(xen_vcpu, cpu) = NULL;
	}
}

void xen_vcpu_setup(int cpu)
{
	struct vcpu_register_vcpu_info info;
	int err;
	struct vcpu_info *vcpup;

	BUILD_BUG_ON(sizeof(*vcpup) > SMP_CACHE_BYTES);
	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	/*
	 * This path is called on PVHVM at bootup (xen_hvm_smp_prepare_boot_cpu)
	 * and at restore (xen_vcpu_restore). Also called for hotplugged
	 * VCPUs (cpu_init -> xen_hvm_cpu_prepare_hvm).
	 * However, the hypercall can only be done once (see below) so if a VCPU
	 * is offlined and comes back online then let's not redo the hypercall.
	 *
	 * For PV it is called during restore (xen_vcpu_restore) and bootup
	 * (xen_setup_vcpu_info_placement). The hotplug mechanism does not
	 * use this function.
	 */
	if (xen_hvm_domain()) {
		if (per_cpu(xen_vcpu, cpu) == &per_cpu(xen_vcpu_info, cpu))
			return;
	}

	vcpup = &per_cpu(xen_vcpu_info, cpu);
	info.mfn = arbitrary_virt_to_mfn(vcpup);
	info.offset = offset_in_page(vcpup);

	/*
	 * N.B. This hypercall can _only_ be called once per CPU.
	 * Subsequent calls will error out with -EINVAL. This is due to
	 * the fact that hypervisor has no unregister variant and this
	 * hypercall does not allow to over-write info.mfn and
	 * info.offset.
	 */
	err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, xen_vcpu_nr(cpu),
				 &info);
	if (err)
		panic("register_vcpu_info failed: cpu=%d err=%d\n", cpu, err);

	per_cpu(xen_vcpu, cpu) = vcpup;
}

void __init xen_banner(void)
{
	unsigned version = HYPERVISOR_xen_version(XENVER_version, NULL);
	struct xen_extraversion extra;

	HYPERVISOR_xen_version(XENVER_extraversion, &extra);

	pr_info("Booting kernel on %s\n", pv_info.name);
	pr_info("Xen version: %u.%u%s%s\n",
		version >> 16, version & 0xffff, extra.extraversion,
		xen_feature(XENFEAT_mmu_pt_update_preserve_ad)
		? " (preserve-AD)" : "");
}

/* Check if running on Xen version (major, minor) or later */
bool xen_running_on_version_or_later(unsigned int major, unsigned int minor)
{
	unsigned int version;

	if (!xen_domain())
		return false;

	version = HYPERVISOR_xen_version(XENVER_version, NULL);
	if ((((version >> 16) == major) && ((version & 0xffff) >= minor)) ||
		((version >> 16) > major))
		return true;
	return false;
}

void __init xen_add_preferred_consoles(void)
{
	add_preferred_console("xenboot", 0, NULL);
	if (!boot_params.screen_info.orig_video_isVGA)
		add_preferred_console("tty", 0, NULL);
	add_preferred_console("hvc", 0, NULL);
	if (boot_params.screen_info.orig_video_isVGA)
		add_preferred_console("tty", 0, NULL);
}

void xen_reboot(int reason)
{
	struct sched_shutdown r = { .reason = reason };
	int cpu;

	for_each_online_cpu(cpu)
		xen_pmu_finish(cpu);

	if (HYPERVISOR_sched_op(SCHEDOP_shutdown, &r))
		BUG();
}

static int reboot_reason = SHUTDOWN_reboot;
static bool xen_legacy_crash;
void xen_emergency_restart(void)
{
	xen_reboot(reboot_reason);
}

static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	if (!kexec_crash_loaded()) {
		if (xen_legacy_crash)
			xen_reboot(SHUTDOWN_crash);

		reboot_reason = SHUTDOWN_crash;

		/*
		 * If panic_timeout==0 then we are supposed to wait forever.
		 * However, to preserve original dom0 behavior we have to drop
		 * into hypervisor. (domU behavior is controlled by its
		 * config file)
		 */
		if (panic_timeout == 0)
			panic_timeout = -1;
	}
	return NOTIFY_DONE;
}

static int __init parse_xen_legacy_crash(char *arg)
{
	xen_legacy_crash = true;
	return 0;
}
early_param("xen_legacy_crash", parse_xen_legacy_crash);

static struct notifier_block xen_panic_block = {
	.notifier_call = xen_panic_event,
	.priority = INT_MIN
};

int xen_panic_handler_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);
	return 0;
}

void xen_pin_vcpu(int cpu)
{
	static bool disable_pinning;
	struct sched_pin_override pin_override;
	int ret;

	if (disable_pinning)
		return;

	pin_override.pcpu = cpu;
	ret = HYPERVISOR_sched_op(SCHEDOP_pin_override, &pin_override);

	/* Ignore errors when removing override. */
	if (cpu < 0)
		return;

	switch (ret) {
	case -ENOSYS:
		pr_warn("Unable to pin on physical cpu %d. In case of problems consider vcpu pinning.\n",
			cpu);
		disable_pinning = true;
		break;
	case -EPERM:
		WARN(1, "Trying to pin vcpu without having privilege to do so\n");
		disable_pinning = true;
		break;
	case -EINVAL:
	case -EBUSY:
		pr_warn("Physical cpu %d not available for pinning. Check Xen cpu configuration.\n",
			cpu);
		break;
	case 0:
		break;
	default:
		WARN(1, "rc %d while trying to pin vcpu\n", ret);
		disable_pinning = true;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
void xen_arch_register_cpu(int num)
{
	arch_register_cpu(num);
}
EXPORT_SYMBOL(xen_arch_register_cpu);

void xen_arch_unregister_cpu(int num)
{
	arch_unregister_cpu(num);
}
EXPORT_SYMBOL(xen_arch_unregister_cpu);
#endif

/* Amount of extra memory space we add to the e820 ranges */
struct xen_memory_region xen_extra_mem[XEN_EXTRA_MEM_MAX_REGIONS] __initdata;

void __init xen_add_extra_mem(unsigned long start_pfn, unsigned long n_pfns)
{
	unsigned int i;

	/*
	 * No need to check for zero size, should happen rarely and will only
	 * write a new entry regarded to be unused due to zero size.
	 */
	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		/* Add new region. */
		if (xen_extra_mem[i].n_pfns == 0) {
			xen_extra_mem[i].start_pfn = start_pfn;
			xen_extra_mem[i].n_pfns = n_pfns;
			break;
		}
		/* Append to existing region. */
		if (xen_extra_mem[i].start_pfn + xen_extra_mem[i].n_pfns ==
		    start_pfn) {
			xen_extra_mem[i].n_pfns += n_pfns;
			break;
		}
	}
	if (i == XEN_EXTRA_MEM_MAX_REGIONS)
		printk(KERN_WARNING "Warning: not enough extra memory regions\n");

	memblock_reserve(PFN_PHYS(start_pfn), PFN_PHYS(n_pfns));
}

#ifdef CONFIG_XEN_UNPOPULATED_ALLOC
int __init arch_xen_unpopulated_init(struct resource **res)
{
	unsigned int i;

	if (!xen_domain())
		return -ENODEV;

	/* Must be set strictly before calling xen_free_unpopulated_pages(). */
	*res = &iomem_resource;

	/*
	 * Initialize with pages from the extra memory regions (see
	 * arch/x86/xen/setup.c).
	 */
	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		unsigned int j;

		for (j = 0; j < xen_extra_mem[i].n_pfns; j++) {
			struct page *pg =
				pfn_to_page(xen_extra_mem[i].start_pfn + j);

			xen_free_unpopulated_pages(1, &pg);
		}

		/* Zero so region is not also added to the balloon driver. */
		xen_extra_mem[i].n_pfns = 0;
	}

	return 0;
}
#endif
