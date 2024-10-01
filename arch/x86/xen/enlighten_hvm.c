// SPDX-License-Identifier: GPL-2.0

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/virtio_anchor.h>

#include <xen/features.h>
#include <xen/events.h>
#include <xen/hvm.h>
#include <xen/interface/hvm/hvm_op.h>
#include <xen/interface/memory.h>

#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/smp.h>
#include <asm/io_apic.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/idtentry.h>
#include <asm/hypervisor.h>
#include <asm/e820/api.h>
#include <asm/early_ioremap.h>

#include <asm/xen/cpuid.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/page.h>

#include "xen-ops.h"

static unsigned long shared_info_pfn;

__ro_after_init bool xen_percpu_upcall;
EXPORT_SYMBOL_GPL(xen_percpu_upcall);

void xen_hvm_init_shared_info(void)
{
	struct xen_add_to_physmap xatp;

	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = shared_info_pfn;
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
		BUG();
}

static void __init reserve_shared_info(void)
{
	u64 pa;

	/*
	 * Search for a free page starting at 4kB physical address.
	 * Low memory is preferred to avoid an EPT large page split up
	 * by the mapping.
	 * Starting below X86_RESERVE_LOW (usually 64kB) is fine as
	 * the BIOS used for HVM guests is well behaved and won't
	 * clobber memory other than the first 4kB.
	 */
	for (pa = PAGE_SIZE;
	     !e820__mapped_all(pa, pa + PAGE_SIZE, E820_TYPE_RAM) ||
	     memblock_is_reserved(pa);
	     pa += PAGE_SIZE)
		;

	shared_info_pfn = PHYS_PFN(pa);

	memblock_reserve(pa, PAGE_SIZE);
	HYPERVISOR_shared_info = early_memremap(pa, PAGE_SIZE);
}

static void __init xen_hvm_init_mem_mapping(void)
{
	early_memunmap(HYPERVISOR_shared_info, PAGE_SIZE);
	HYPERVISOR_shared_info = __va(PFN_PHYS(shared_info_pfn));

	/*
	 * The virtual address of the shared_info page has changed, so
	 * the vcpu_info pointer for VCPU 0 is now stale.
	 *
	 * The prepare_boot_cpu callback will re-initialize it via
	 * xen_vcpu_setup, but we can't rely on that to be called for
	 * old Xen versions (xen_have_vector_callback == 0).
	 *
	 * It is, in any case, bad to have a stale vcpu_info pointer
	 * so reset it now.
	 */
	xen_vcpu_info_reset(0);
}

static void __init init_hvm_pv_info(void)
{
	int major, minor;
	uint32_t eax, ebx, ecx, edx, base;

	base = xen_cpuid_base();
	eax = cpuid_eax(base + 1);

	major = eax >> 16;
	minor = eax & 0xffff;
	printk(KERN_INFO "Xen version %d.%d.\n", major, minor);

	xen_domain_type = XEN_HVM_DOMAIN;

	/* PVH set up hypercall page in xen_prepare_pvh(). */
	if (xen_pvh_domain())
		pv_info.name = "Xen PVH";
	else {
		u64 pfn;
		uint32_t msr;

		pv_info.name = "Xen HVM";
		msr = cpuid_ebx(base + 2);
		pfn = __pa(hypercall_page);
		wrmsr_safe(msr, (u32)pfn, (u32)(pfn >> 32));
	}

	xen_setup_features();

	cpuid(base + 4, &eax, &ebx, &ecx, &edx);
	if (eax & XEN_HVM_CPUID_VCPU_ID_PRESENT)
		this_cpu_write(xen_vcpu_id, ebx);
	else
		this_cpu_write(xen_vcpu_id, smp_processor_id());
}

DEFINE_IDTENTRY_SYSVEC(sysvec_xen_hvm_callback)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	if (xen_percpu_upcall)
		apic_eoi();

	inc_irq_stat(irq_hv_callback_count);

	xen_evtchn_do_upcall();

	set_irq_regs(old_regs);
}

#ifdef CONFIG_KEXEC_CORE
static void xen_hvm_shutdown(void)
{
	native_machine_shutdown();
	if (kexec_in_progress)
		xen_reboot(SHUTDOWN_soft_reset);
}
#endif

#ifdef CONFIG_CRASH_DUMP
static void xen_hvm_crash_shutdown(struct pt_regs *regs)
{
	native_machine_crash_shutdown(regs);
	xen_reboot(SHUTDOWN_soft_reset);
}
#endif

static int xen_cpu_up_prepare_hvm(unsigned int cpu)
{
	int rc = 0;

	/*
	 * If a CPU was offlined earlier and offlining timed out then the
	 * lock mechanism is still initialized. Uninit it unconditionally
	 * as it's safe to call even if already uninited. Interrupts and
	 * timer have already been handled in xen_cpu_dead_hvm().
	 */
	xen_uninit_lock_cpu(cpu);

	if (cpu_acpi_id(cpu) != CPU_ACPIID_INVALID)
		per_cpu(xen_vcpu_id, cpu) = cpu_acpi_id(cpu);
	else
		per_cpu(xen_vcpu_id, cpu) = cpu;
	xen_vcpu_setup(cpu);
	if (!xen_have_vector_callback)
		return 0;

	if (xen_percpu_upcall) {
		rc = xen_set_upcall_vector(cpu);
		if (rc) {
			WARN(1, "HVMOP_set_evtchn_upcall_vector"
			     " for CPU %d failed: %d\n", cpu, rc);
			return rc;
		}
	}

	if (xen_feature(XENFEAT_hvm_safe_pvclock))
		xen_setup_timer(cpu);

	rc = xen_smp_intr_init(cpu);
	if (rc) {
		WARN(1, "xen_smp_intr_init() for CPU %d failed: %d\n",
		     cpu, rc);
	}
	return rc;
}

static int xen_cpu_dead_hvm(unsigned int cpu)
{
	xen_smp_intr_free(cpu);

	if (xen_have_vector_callback && xen_feature(XENFEAT_hvm_safe_pvclock))
		xen_teardown_timer(cpu);
	return 0;
}

static void __init xen_hvm_guest_init(void)
{
	if (xen_pv_domain())
		return;

	if (IS_ENABLED(CONFIG_XEN_VIRTIO_FORCE_GRANT))
		virtio_set_mem_acc_cb(xen_virtio_restricted_mem_acc);

	init_hvm_pv_info();

	reserve_shared_info();
	xen_hvm_init_shared_info();

	/*
	 * xen_vcpu is a pointer to the vcpu_info struct in the shared_info
	 * page, we use it in the event channel upcall and in some pvclock
	 * related functions.
	 */
	xen_vcpu_info_reset(0);

	xen_panic_handler_init();

	xen_hvm_smp_init();
	WARN_ON(xen_cpuhp_setup(xen_cpu_up_prepare_hvm, xen_cpu_dead_hvm));
	xen_unplug_emulated_devices();
	x86_init.irqs.intr_init = xen_init_IRQ;
	xen_hvm_init_time_ops();
	xen_hvm_init_mmu_ops();

#ifdef CONFIG_KEXEC_CORE
	machine_ops.shutdown = xen_hvm_shutdown;
#endif
#ifdef CONFIG_CRASH_DUMP
	machine_ops.crash_shutdown = xen_hvm_crash_shutdown;
#endif
}

static __init int xen_parse_nopv(char *arg)
{
	pr_notice("\"xen_nopv\" is deprecated, please use \"nopv\" instead\n");

	if (xen_cpuid_base())
		nopv = true;
	return 0;
}
early_param("xen_nopv", xen_parse_nopv);

static __init int xen_parse_no_vector_callback(char *arg)
{
	xen_have_vector_callback = false;
	return 0;
}
early_param("xen_no_vector_callback", xen_parse_no_vector_callback);

static __init bool xen_x2apic_available(void)
{
	return x2apic_supported();
}

static bool __init msi_ext_dest_id(void)
{
       return cpuid_eax(xen_cpuid_base() + 4) & XEN_HVM_CPUID_EXT_DEST_ID;
}

static __init void xen_hvm_guest_late_init(void)
{
#ifdef CONFIG_XEN_PVH
	/* Test for PVH domain (PVH boot path taken overrides ACPI flags). */
	if (!xen_pvh &&
	    (x86_platform.legacy.rtc || !x86_platform.legacy.no_vga))
		return;

	/* PVH detected. */
	xen_pvh = true;

	if (nopv)
		panic("\"nopv\" and \"xen_nopv\" parameters are unsupported in PVH guest.");

	/* Make sure we don't fall back to (default) ACPI_IRQ_MODEL_PIC. */
	if (!nr_ioapics && acpi_irq_model == ACPI_IRQ_MODEL_PIC)
		acpi_irq_model = ACPI_IRQ_MODEL_PLATFORM;

	machine_ops.emergency_restart = xen_emergency_restart;
	pv_info.name = "Xen PVH";
#endif
}

static uint32_t __init xen_platform_hvm(void)
{
	uint32_t xen_domain = xen_cpuid_base();
	struct x86_hyper_init *h = &x86_hyper_xen_hvm.init;

	if (xen_pv_domain())
		return 0;

	if (xen_pvh_domain() && nopv) {
		/* Guest booting via the Xen-PVH boot entry goes here */
		pr_info("\"nopv\" parameter is ignored in PVH guest\n");
		nopv = false;
	} else if (nopv && xen_domain) {
		/*
		 * Guest booting via normal boot entry (like via grub2) goes
		 * here.
		 *
		 * Use interface functions for bare hardware if nopv,
		 * xen_hvm_guest_late_init is an exception as we need to
		 * detect PVH and panic there.
		 */
		h->init_platform = x86_init_noop;
		h->x2apic_available = bool_x86_init_noop;
		h->init_mem_mapping = x86_init_noop;
		h->init_after_bootmem = x86_init_noop;
		h->guest_late_init = xen_hvm_guest_late_init;
		x86_hyper_xen_hvm.runtime.pin_vcpu = x86_op_int_noop;
	}
	return xen_domain;
}

struct hypervisor_x86 x86_hyper_xen_hvm __initdata = {
	.name                   = "Xen HVM",
	.detect                 = xen_platform_hvm,
	.type			= X86_HYPER_XEN_HVM,
	.init.init_platform     = xen_hvm_guest_init,
	.init.x2apic_available  = xen_x2apic_available,
	.init.init_mem_mapping	= xen_hvm_init_mem_mapping,
	.init.guest_late_init	= xen_hvm_guest_late_init,
	.init.msi_ext_dest_id   = msi_ext_dest_id,
	.runtime.pin_vcpu       = xen_pin_vcpu,
	.ignore_nopv            = true,
};
