#include <linux/types.h>
#include <linux/tick.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/grant_table.h>
#include <xen/events.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>
#include <asm/fixmap.h>

#include "xen-ops.h"
#include "mmu.h"
#include "pmu.h"

static void xen_pv_pre_suspend(void)
{
	xen_mm_pin_all();

	xen_start_info->store_mfn = mfn_to_pfn(xen_start_info->store_mfn);
	xen_start_info->console.domU.mfn =
		mfn_to_pfn(xen_start_info->console.domU.mfn);

	BUG_ON(!irqs_disabled());

	HYPERVISOR_shared_info = &xen_dummy_shared_info;
	if (HYPERVISOR_update_va_mapping(fix_to_virt(FIX_PARAVIRT_BOOTMAP),
					 __pte_ma(0), 0))
		BUG();
}

static void xen_hvm_post_suspend(int suspend_cancelled)
{
#ifdef CONFIG_XEN_PVHVM
	int cpu;
	xen_hvm_init_shared_info();
	xen_callback_vector();
	xen_unplug_emulated_devices();
	if (xen_feature(XENFEAT_hvm_safe_pvclock)) {
		for_each_online_cpu(cpu) {
			xen_setup_runstate_info(cpu);
		}
	}
#endif
}

static void xen_pv_post_suspend(int suspend_cancelled)
{
	xen_build_mfn_list_list();

	xen_setup_shared_info();

	if (suspend_cancelled) {
		xen_start_info->store_mfn =
			pfn_to_mfn(xen_start_info->store_mfn);
		xen_start_info->console.domU.mfn =
			pfn_to_mfn(xen_start_info->console.domU.mfn);
	} else {
#ifdef CONFIG_SMP
		BUG_ON(xen_cpu_initialized_map == NULL);
		cpumask_copy(xen_cpu_initialized_map, cpu_online_mask);
#endif
		xen_vcpu_restore();
	}

	xen_mm_unpin_all();
}

void xen_arch_pre_suspend(void)
{
	if (xen_pv_domain())
		xen_pv_pre_suspend();
}

void xen_arch_post_suspend(int cancelled)
{
	if (xen_pv_domain())
		xen_pv_post_suspend(cancelled);
	else
		xen_hvm_post_suspend(cancelled);
}

static void xen_vcpu_notify_restore(void *data)
{
	/* Boot processor notified via generic timekeeping_resume() */
	if (smp_processor_id() == 0)
		return;

	tick_resume_local();
}

static void xen_vcpu_notify_suspend(void *data)
{
	tick_suspend_local();
}

void xen_arch_resume(void)
{
	int cpu;

	on_each_cpu(xen_vcpu_notify_restore, NULL, 1);

	for_each_online_cpu(cpu)
		xen_pmu_init(cpu);
}

void xen_arch_suspend(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		xen_pmu_finish(cpu);

	on_each_cpu(xen_vcpu_notify_suspend, NULL, 1);
}
