#include <linux/types.h>
#include <linux/clockchips.h>

#include <xen/interface/xen.h>
#include <xen/grant_table.h>
#include <xen/events.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>
#include <asm/fixmap.h>

#include "xen-ops.h"
#include "mmu.h"

void xen_pre_suspend(void)
{
	xen_start_info->store_mfn = mfn_to_pfn(xen_start_info->store_mfn);
	xen_start_info->console.domU.mfn =
		mfn_to_pfn(xen_start_info->console.domU.mfn);

	BUG_ON(!irqs_disabled());

	HYPERVISOR_shared_info = &xen_dummy_shared_info;
	if (HYPERVISOR_update_va_mapping(fix_to_virt(FIX_PARAVIRT_BOOTMAP),
					 __pte_ma(0), 0))
		BUG();
}

void xen_post_suspend(int suspend_cancelled)
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

}

static void xen_vcpu_notify_restore(void *data)
{
	unsigned long reason = (unsigned long)data;

	/* Boot processor notified via generic timekeeping_resume() */
	if ( smp_processor_id() == 0)
		return;

	clockevents_notify(reason, NULL);
}

void xen_arch_resume(void)
{
	on_each_cpu(xen_vcpu_notify_restore,
		    (void *)CLOCK_EVT_NOTIFY_RESUME, 1);
}
