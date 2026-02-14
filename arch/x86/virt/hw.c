// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/kvm_types.h>
#include <linux/list.h>
#include <linux/percpu.h>

#include <asm/perf_event.h>
#include <asm/processor.h>
#include <asm/virt.h>
#include <asm/vmx.h>

__visible bool virt_rebooting;
EXPORT_SYMBOL_FOR_KVM(virt_rebooting);

#if IS_ENABLED(CONFIG_KVM_INTEL)
DEFINE_PER_CPU(struct vmcs *, root_vmcs);
EXPORT_PER_CPU_SYMBOL(root_vmcs);

static __init void x86_vmx_exit(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		free_page((unsigned long)per_cpu(root_vmcs, cpu));
		per_cpu(root_vmcs, cpu) = NULL;
	}
}

static __init int __x86_vmx_init(void)
{
	u64 basic_msr;
	u32 rev_id;
	int cpu;

	if (!cpu_feature_enabled(X86_FEATURE_VMX))
		return -EOPNOTSUPP;

	rdmsrq(MSR_IA32_VMX_BASIC, basic_msr);

	/* IA-32 SDM Vol 3B: VMCS size is never greater than 4kB. */
	if (WARN_ON_ONCE(vmx_basic_vmcs_size(basic_msr) > PAGE_SIZE))
		return -EIO;

	/*
	 * Even if eVMCS is enabled (or will be enabled?), and even though not
	 * explicitly documented by TLFS, the root VMCS  passed to VMXON should
	 * still be marked with the revision_id reported by the physical CPU.
	 */
	rev_id = vmx_basic_vmcs_revision_id(basic_msr);

	for_each_possible_cpu(cpu) {
		int node = cpu_to_node(cpu);
		struct page *page;
		struct vmcs *vmcs;

		page = __alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
		if (WARN_ON_ONCE(!page)) {
			x86_vmx_exit();
			return -ENOMEM;
		}

		vmcs = page_address(page);
		vmcs->hdr.revision_id = rev_id;
		per_cpu(root_vmcs, cpu) = vmcs;
	}

	return 0;
}

static __init int x86_vmx_init(void)
{
	int r;

	r = __x86_vmx_init();
	if (r)
		setup_clear_cpu_cap(X86_FEATURE_VMX);
	return r;
}
#else
static __init int x86_vmx_init(void) { return -EOPNOTSUPP; }
#endif

void __init x86_virt_init(void)
{
	x86_vmx_init();
}
