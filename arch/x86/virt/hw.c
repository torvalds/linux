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
static DEFINE_PER_CPU(struct vmcs *, root_vmcs);

static int x86_virt_cpu_vmxon(void)
{
	u64 vmxon_pointer = __pa(per_cpu(root_vmcs, raw_smp_processor_id()));
	u64 msr;

	cr4_set_bits(X86_CR4_VMXE);

	asm goto("1: vmxon %[vmxon_pointer]\n\t"
			  _ASM_EXTABLE(1b, %l[fault])
			  : : [vmxon_pointer] "m"(vmxon_pointer)
			  : : fault);
	return 0;

fault:
	WARN_ONCE(1, "VMXON faulted, MSR_IA32_FEAT_CTL (0x3a) = 0x%llx\n",
		  rdmsrq_safe(MSR_IA32_FEAT_CTL, &msr) ? 0xdeadbeef : msr);
	cr4_clear_bits(X86_CR4_VMXE);

	return -EFAULT;
}

int x86_vmx_enable_virtualization_cpu(void)
{
	int r;

	if (cr4_read_shadow() & X86_CR4_VMXE)
		return -EBUSY;

	intel_pt_handle_vmx(1);

	r = x86_virt_cpu_vmxon();
	if (r) {
		intel_pt_handle_vmx(0);
		return r;
	}

	return 0;
}
EXPORT_SYMBOL_FOR_KVM(x86_vmx_enable_virtualization_cpu);

/*
 * Disable VMX and clear CR4.VMXE (even if VMXOFF faults)
 *
 * Note, VMXOFF causes a #UD if the CPU is !post-VMXON, but it's impossible to
 * atomically track post-VMXON state, e.g. this may be called in NMI context.
 * Eat all faults as all other faults on VMXOFF faults are mode related, i.e.
 * faults are guaranteed to be due to the !post-VMXON check unless the CPU is
 * magically in RM, VM86, compat mode, or at CPL>0.
 */
int x86_vmx_disable_virtualization_cpu(void)
{
	int r = -EIO;

	asm goto("1: vmxoff\n\t"
		 _ASM_EXTABLE(1b, %l[fault])
		 ::: "cc", "memory" : fault);
	r = 0;

fault:
	cr4_clear_bits(X86_CR4_VMXE);
	intel_pt_handle_vmx(0);
	return r;
}
EXPORT_SYMBOL_FOR_KVM(x86_vmx_disable_virtualization_cpu);

void x86_vmx_emergency_disable_virtualization_cpu(void)
{
	virt_rebooting = true;

	/*
	 * Note, CR4.VMXE can be _cleared_ in NMI context, but it can only be
	 * set in task context.  If this races with _another_ emergency call
	 * from NMI context, VMXOFF may #UD, but kernel will eat those faults
	 * due to virt_rebooting being set by the interrupting NMI callback.
	 */
	if (!(__read_cr4() & X86_CR4_VMXE))
		return;

	x86_vmx_disable_virtualization_cpu();
}
EXPORT_SYMBOL_FOR_KVM(x86_vmx_emergency_disable_virtualization_cpu);

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
