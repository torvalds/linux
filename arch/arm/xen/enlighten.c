#include <xen/xen.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/hvm.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/hvm/params.h>
#include <xen/features.h>
#include <xen/platform_pci.h>
#include <xen/xenbus.h>
#include <xen/page.h>
#include <xen/interface/sched.h>
#include <xen/xen-ops.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/xen-ops.h>
#include <asm/system_misc.h>
#include <asm/efi.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/cpuidle.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/console.h>
#include <linux/pvclock_gtod.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/timekeeper_internal.h>
#include <linux/acpi.h>

#include <linux/mm.h>

struct start_info _xen_start_info;
struct start_info *xen_start_info = &_xen_start_info;
EXPORT_SYMBOL(xen_start_info);

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL(xen_domain_type);

struct shared_info xen_dummy_shared_info;
struct shared_info *HYPERVISOR_shared_info = (void *)&xen_dummy_shared_info;

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
static struct vcpu_info __percpu *xen_vcpu_info;

/* Linux <-> Xen vCPU id mapping */
DEFINE_PER_CPU(uint32_t, xen_vcpu_id);
EXPORT_PER_CPU_SYMBOL(xen_vcpu_id);

/* These are unused until we support booting "pre-ballooned" */
unsigned long xen_released_pages;
struct xen_memory_region xen_extra_mem[XEN_EXTRA_MEM_MAX_REGIONS] __initdata;

static __read_mostly unsigned int xen_events_irq;

int xen_remap_domain_gfn_array(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t *gfn, int nr,
			       int *err_ptr, pgprot_t prot,
			       unsigned domid,
			       struct page **pages)
{
	return xen_xlate_remap_gfn_array(vma, addr, gfn, nr, err_ptr,
					 prot, domid, pages);
}
EXPORT_SYMBOL_GPL(xen_remap_domain_gfn_array);

/* Not used by XENFEAT_auto_translated guests. */
int xen_remap_domain_gfn_range(struct vm_area_struct *vma,
                              unsigned long addr,
                              xen_pfn_t gfn, int nr,
                              pgprot_t prot, unsigned domid,
                              struct page **pages)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(xen_remap_domain_gfn_range);

int xen_unmap_domain_gfn_range(struct vm_area_struct *vma,
			       int nr, struct page **pages)
{
	return xen_xlate_unmap_gfn_range(vma, nr, pages);
}
EXPORT_SYMBOL_GPL(xen_unmap_domain_gfn_range);

static void xen_read_wallclock(struct timespec64 *ts)
{
	u32 version;
	struct timespec64 now, ts_monotonic;
	struct shared_info *s = HYPERVISOR_shared_info;
	struct pvclock_wall_clock *wall_clock = &(s->wc);

	/* get wallclock at system boot */
	do {
		version = wall_clock->version;
		rmb();		/* fetch version before time */
		now.tv_sec  = ((uint64_t)wall_clock->sec_hi << 32) | wall_clock->sec;
		now.tv_nsec = wall_clock->nsec;
		rmb();		/* fetch time before checking version */
	} while ((wall_clock->version & 1) || (version != wall_clock->version));

	/* time since system boot */
	ktime_get_ts64(&ts_monotonic);
	*ts = timespec64_add(now, ts_monotonic);
}

static int xen_pvclock_gtod_notify(struct notifier_block *nb,
				   unsigned long was_set, void *priv)
{
	/* Protected by the calling core code serialization */
	static struct timespec64 next_sync;

	struct xen_platform_op op;
	struct timespec64 now, system_time;
	struct timekeeper *tk = priv;

	now.tv_sec = tk->xtime_sec;
	now.tv_nsec = (long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);
	system_time = timespec64_add(now, tk->wall_to_monotonic);

	/*
	 * We only take the expensive HV call when the clock was set
	 * or when the 11 minutes RTC synchronization time elapsed.
	 */
	if (!was_set && timespec64_compare(&now, &next_sync) < 0)
		return NOTIFY_OK;

	op.cmd = XENPF_settime64;
	op.u.settime64.mbz = 0;
	op.u.settime64.secs = now.tv_sec;
	op.u.settime64.nsecs = now.tv_nsec;
	op.u.settime64.system_time = timespec64_to_ns(&system_time);
	(void)HYPERVISOR_platform_op(&op);

	/*
	 * Move the next drift compensation time 11 minutes
	 * ahead. That's emulating the sync_cmos_clock() update for
	 * the hardware RTC.
	 */
	next_sync = now;
	next_sync.tv_sec += 11 * 60;

	return NOTIFY_OK;
}

static struct notifier_block xen_pvclock_gtod_notifier = {
	.notifier_call = xen_pvclock_gtod_notify,
};

static int xen_starting_cpu(unsigned int cpu)
{
	struct vcpu_register_vcpu_info info;
	struct vcpu_info *vcpup;
	int err;

	/* 
	 * VCPUOP_register_vcpu_info cannot be called twice for the same
	 * vcpu, so if vcpu_info is already registered, just get out. This
	 * can happen with cpu-hotplug.
	 */
	if (per_cpu(xen_vcpu, cpu) != NULL)
		goto after_register_vcpu_info;

	pr_info("Xen: initializing cpu%d\n", cpu);
	vcpup = per_cpu_ptr(xen_vcpu_info, cpu);

	info.mfn = virt_to_gfn(vcpup);
	info.offset = xen_offset_in_page(vcpup);

	err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, xen_vcpu_nr(cpu),
				 &info);
	BUG_ON(err);
	per_cpu(xen_vcpu, cpu) = vcpup;

	xen_setup_runstate_info(cpu);

after_register_vcpu_info:
	enable_percpu_irq(xen_events_irq, 0);
	return 0;
}

static int xen_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(xen_events_irq);
	return 0;
}

static void xen_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_reboot };
	int rc;
	rc = HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);
	BUG_ON(rc);
}

static void xen_power_off(void)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_poweroff };
	int rc;
	rc = HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);
	BUG_ON(rc);
}

static irqreturn_t xen_arm_callback(int irq, void *arg)
{
	xen_hvm_evtchn_do_upcall();
	return IRQ_HANDLED;
}

static __initdata struct {
	const char *compat;
	const char *prefix;
	const char *version;
	bool found;
} hyper_node = {"xen,xen", "xen,xen-", NULL, false};

static int __init fdt_find_hyper_node(unsigned long node, const char *uname,
				      int depth, void *data)
{
	const void *s = NULL;
	int len;

	if (depth != 1 || strcmp(uname, "hypervisor") != 0)
		return 0;

	if (of_flat_dt_is_compatible(node, hyper_node.compat))
		hyper_node.found = true;

	s = of_get_flat_dt_prop(node, "compatible", &len);
	if (strlen(hyper_node.prefix) + 3  < len &&
	    !strncmp(hyper_node.prefix, s, strlen(hyper_node.prefix)))
		hyper_node.version = s + strlen(hyper_node.prefix);

	/*
	 * Check if Xen supports EFI by checking whether there is the
	 * "/hypervisor/uefi" node in DT. If so, runtime services are available
	 * through proxy functions (e.g. in case of Xen dom0 EFI implementation
	 * they call special hypercall which executes relevant EFI functions)
	 * and that is why they are always enabled.
	 */
	if (IS_ENABLED(CONFIG_XEN_EFI)) {
		if ((of_get_flat_dt_subnode_by_name(node, "uefi") > 0) &&
		    !efi_runtime_disabled())
			set_bit(EFI_RUNTIME_SERVICES, &efi.flags);
	}

	return 0;
}

/*
 * see Documentation/devicetree/bindings/arm/xen.txt for the
 * documentation of the Xen Device Tree format.
 */
#define GRANT_TABLE_PHYSADDR 0
void __init xen_early_init(void)
{
	of_scan_flat_dt(fdt_find_hyper_node, NULL);
	if (!hyper_node.found) {
		pr_debug("No Xen support\n");
		return;
	}

	if (hyper_node.version == NULL) {
		pr_debug("Xen version not found\n");
		return;
	}

	pr_info("Xen %s support found\n", hyper_node.version);

	xen_domain_type = XEN_HVM_DOMAIN;

	xen_setup_features();

	if (xen_feature(XENFEAT_dom0))
		xen_start_info->flags |= SIF_INITDOMAIN|SIF_PRIVILEGED;
	else
		xen_start_info->flags &= ~(SIF_INITDOMAIN|SIF_PRIVILEGED);

	if (!console_set_on_cmdline && !xen_initial_domain())
		add_preferred_console("hvc", 0, NULL);
}

static void __init xen_acpi_guest_init(void)
{
#ifdef CONFIG_ACPI
	struct xen_hvm_param a;
	int interrupt, trigger, polarity;

	a.domid = DOMID_SELF;
	a.index = HVM_PARAM_CALLBACK_IRQ;

	if (HYPERVISOR_hvm_op(HVMOP_get_param, &a)
	    || (a.value >> 56) != HVM_PARAM_CALLBACK_TYPE_PPI) {
		xen_events_irq = 0;
		return;
	}

	interrupt = a.value & 0xff;
	trigger = ((a.value >> 8) & 0x1) ? ACPI_EDGE_SENSITIVE
					 : ACPI_LEVEL_SENSITIVE;
	polarity = ((a.value >> 8) & 0x2) ? ACPI_ACTIVE_LOW
					  : ACPI_ACTIVE_HIGH;
	xen_events_irq = acpi_register_gsi(NULL, interrupt, trigger, polarity);
#endif
}

static void __init xen_dt_guest_init(void)
{
	struct device_node *xen_node;

	xen_node = of_find_compatible_node(NULL, NULL, "xen,xen");
	if (!xen_node) {
		pr_err("Xen support was detected before, but it has disappeared\n");
		return;
	}

	xen_events_irq = irq_of_parse_and_map(xen_node, 0);
}

static int __init xen_guest_init(void)
{
	struct xen_add_to_physmap xatp;
	struct shared_info *shared_info_page = NULL;
	int cpu;

	if (!xen_domain())
		return 0;

	if (!acpi_disabled)
		xen_acpi_guest_init();
	else
		xen_dt_guest_init();

	if (!xen_events_irq) {
		pr_err("Xen event channel interrupt not found\n");
		return -ENODEV;
	}

	/*
	 * The fdt parsing codes have set EFI_RUNTIME_SERVICES if Xen EFI
	 * parameters are found. Force enable runtime services.
	 */
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		xen_efi_runtime_setup();

	shared_info_page = (struct shared_info *)get_zeroed_page(GFP_KERNEL);

	if (!shared_info_page) {
		pr_err("not enough memory\n");
		return -ENOMEM;
	}
	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = virt_to_gfn(shared_info_page);
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
		BUG();

	HYPERVISOR_shared_info = (struct shared_info *)shared_info_page;

	/* xen_vcpu is a pointer to the vcpu_info struct in the shared_info
	 * page, we use it in the event channel upcall and in some pvclock
	 * related functions. 
	 * The shared info contains exactly 1 CPU (the boot CPU). The guest
	 * is required to use VCPUOP_register_vcpu_info to place vcpu info
	 * for secondary CPUs as they are brought up.
	 * For uniformity we use VCPUOP_register_vcpu_info even on cpu0.
	 */
	xen_vcpu_info = alloc_percpu(struct vcpu_info);
	if (xen_vcpu_info == NULL)
		return -ENOMEM;

	/* Direct vCPU id mapping for ARM guests. */
	for_each_possible_cpu(cpu)
		per_cpu(xen_vcpu_id, cpu) = cpu;

	xen_auto_xlat_grant_frames.count = gnttab_max_grant_frames();
	if (xen_xlate_map_ballooned_pages(&xen_auto_xlat_grant_frames.pfn,
					  &xen_auto_xlat_grant_frames.vaddr,
					  xen_auto_xlat_grant_frames.count)) {
		free_percpu(xen_vcpu_info);
		return -ENOMEM;
	}
	gnttab_init();
	if (!xen_initial_domain())
		xenbus_probe(NULL);

	/*
	 * Making sure board specific code will not set up ops for
	 * cpu idle and cpu freq.
	 */
	disable_cpuidle();
	disable_cpufreq();

	xen_init_IRQ();

	if (request_percpu_irq(xen_events_irq, xen_arm_callback,
			       "events", &xen_vcpu)) {
		pr_err("Error request IRQ %d\n", xen_events_irq);
		return -EINVAL;
	}

	xen_time_setup_guest();

	if (xen_initial_domain())
		pvclock_gtod_register_notifier(&xen_pvclock_gtod_notifier);

	return cpuhp_setup_state(CPUHP_AP_ARM_XEN_STARTING,
				 "arm/xen:starting", xen_starting_cpu,
				 xen_dying_cpu);
}
early_initcall(xen_guest_init);

static int __init xen_pm_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pm_power_off = xen_power_off;
	arm_pm_restart = xen_restart;
	if (!xen_initial_domain()) {
		struct timespec64 ts;
		xen_read_wallclock(&ts);
		do_settimeofday64(&ts);
	}

	return 0;
}
late_initcall(xen_pm_init);


/* empty stubs */
void xen_arch_pre_suspend(void) { }
void xen_arch_post_suspend(int suspend_cancelled) { }
void xen_timer_resume(void) { }
void xen_arch_resume(void) { }
void xen_arch_suspend(void) { }


/* In the hypercall.S file. */
EXPORT_SYMBOL_GPL(HYPERVISOR_event_channel_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_grant_table_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_xen_version);
EXPORT_SYMBOL_GPL(HYPERVISOR_console_io);
EXPORT_SYMBOL_GPL(HYPERVISOR_sched_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_hvm_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_memory_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_physdev_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_vcpu_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_tmem_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_platform_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_multicall);
EXPORT_SYMBOL_GPL(HYPERVISOR_vm_assist);
EXPORT_SYMBOL_GPL(HYPERVISOR_dm_op);
EXPORT_SYMBOL_GPL(privcmd_call);
