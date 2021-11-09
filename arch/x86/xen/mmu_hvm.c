// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/crash_dump.h>

#include <xen/interface/xen.h>
#include <xen/hvm.h>

#include "mmu.h"

#ifdef CONFIG_PROC_VMCORE
/*
 * The kdump kernel has to check whether a pfn of the crashed kernel
 * was a ballooned page. vmcore is using this function to decide
 * whether to access a pfn of the crashed kernel.
 * Returns 0 if the pfn is not backed by a RAM page, the caller may
 * handle the pfn special in this case.
 */
static int xen_oldmem_pfn_is_ram(unsigned long pfn)
{
	struct xen_hvm_get_mem_type a = {
		.domid = DOMID_SELF,
		.pfn = pfn,
	};

	if (HYPERVISOR_hvm_op(HVMOP_get_mem_type, &a)) {
		pr_warn_once("Unexpected HVMOP_get_mem_type failure\n");
		return -ENXIO;
	}
	return a.mem_type != HVMMEM_mmio_dm;
}
#endif

static void xen_hvm_exit_mmap(struct mm_struct *mm)
{
	struct xen_hvm_pagetable_dying a;
	int rc;

	a.domid = DOMID_SELF;
	a.gpa = __pa(mm->pgd);
	rc = HYPERVISOR_hvm_op(HVMOP_pagetable_dying, &a);
	WARN_ON_ONCE(rc < 0);
}

static int is_pagetable_dying_supported(void)
{
	struct xen_hvm_pagetable_dying a;
	int rc = 0;

	a.domid = DOMID_SELF;
	a.gpa = 0x00;
	rc = HYPERVISOR_hvm_op(HVMOP_pagetable_dying, &a);
	if (rc < 0) {
		printk(KERN_DEBUG "HVMOP_pagetable_dying not supported\n");
		return 0;
	}
	return 1;
}

void __init xen_hvm_init_mmu_ops(void)
{
	if (is_pagetable_dying_supported())
		pv_ops.mmu.exit_mmap = xen_hvm_exit_mmap;
#ifdef CONFIG_PROC_VMCORE
	WARN_ON(register_oldmem_pfn_is_ram(&xen_oldmem_pfn_is_ram));
#endif
}
