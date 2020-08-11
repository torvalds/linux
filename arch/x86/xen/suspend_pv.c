// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

#include <asm/fixmap.h>

#include "xen-ops.h"

void xen_pv_pre_suspend(void)
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

void xen_pv_post_suspend(int suspend_cancelled)
{
	xen_build_mfn_list_list();
	set_fixmap(FIX_PARAVIRT_BOOTMAP, xen_start_info->shared_info);
	HYPERVISOR_shared_info = (void *)fix_to_virt(FIX_PARAVIRT_BOOTMAP);
	xen_setup_mfn_list_list();

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
