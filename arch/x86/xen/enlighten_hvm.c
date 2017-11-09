#include <linux/cpu.h>
#include <linux/kexec.h>
#include <linux/memblock.h>

#include <xen/features.h>
#include <xen/events.h>
#include <xen/interface/memory.h>

#include <asm/cpu.h>
#include <asm/smp.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/hypervisor.h>
#include <asm/e820/api.h>
#include <asm/early_ioremap.h>

#include <asm/xen/cpuid.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/page.h>

#include "xen-ops.h"
#include "mmu.h"
#include "smp.h"

static unsigned long shared_info_pfn;

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

#ifdef CONFIG_KEXEC_CORE
static void xen_hvm_shutdown(void)
{
	native_machine_shutdown();
	if (kexec_in_progress)
		xen_reboot(SHUTDOWN_soft_reset);
}

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
	 * This can happen if CPU was offlined earlier and
	 * offlining timed out in common_cpu_die().
	 */
	if (cpu_report_state(cpu) == CPU_DEAD_FROZEN) {
		xen_smp_intr_free(cpu);
		xen_uninit_lock_cpu(cpu);
	}

	if (cpu_acpi_id(cpu) != U32_MAX)
		per_cpu(xen_vcpu_id, cpu) = cpu_acpi_id(cpu);
	else
		per_cpu(xen_vcpu_id, cpu) = cpu;
	rc = xen_vcpu_setup(cpu);
	if (rc)
		return rc;

	if (xen_have_vector_callback && xen_feature(XENFEAT_hvm_safe_pvclock))
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

	if (xen_feature(XENFEAT_hvm_callback_vector))
		xen_have_vector_callback = 1;

	xen_hvm_smp_init();
	WARN_ON(xen_cpuhp_setup(xen_cpu_up_prepare_hvm, xen_cpu_dead_hvm));
	xen_unplug_emulated_devices();
	x86_init.irqs.intr_init = xen_init_IRQ;
	xen_hvm_init_time_ops();
	xen_hvm_init_mmu_ops();

	if (xen_pvh_domain())
		machine_ops.emergency_restart = xen_emergency_restart;
#ifdef CONFIG_KEXEC_CORE
	machine_ops.shutdown = xen_hvm_shutdown;
	machine_ops.crash_shutdown = xen_hvm_crash_shutdown;
#endif
}

static bool xen_nopv;
static __init int xen_parse_nopv(char *arg)
{
       xen_nopv = true;
       return 0;
}
early_param("xen_nopv", xen_parse_nopv);

bool xen_hvm_need_lapic(void)
{
	if (xen_nopv)
		return false;
	if (xen_pv_domain())
		return false;
	if (!xen_hvm_domain())
		return false;
	if (xen_feature(XENFEAT_hvm_pirqs) && xen_have_vector_callback)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(xen_hvm_need_lapic);

static uint32_t __init xen_platform_hvm(void)
{
	if (xen_pv_domain() || xen_nopv)
		return 0;

	return xen_cpuid_base();
}

const struct hypervisor_x86 x86_hyper_xen_hvm = {
	.name                   = "Xen HVM",
	.detect                 = xen_platform_hvm,
	.init.init_platform     = xen_hvm_guest_init,
	.init.x2apic_available  = xen_x2apic_para_available,
	.init.init_mem_mapping	= xen_hvm_init_mem_mapping,
	.runtime.pin_vcpu       = xen_pin_vcpu,
};
EXPORT_SYMBOL(x86_hyper_xen_hvm);
