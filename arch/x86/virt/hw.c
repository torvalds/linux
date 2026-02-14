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

struct x86_virt_ops {
	int feature;
	int (*enable_virtualization_cpu)(void);
	int (*disable_virtualization_cpu)(void);
	void (*emergency_disable_virtualization_cpu)(void);
};
static struct x86_virt_ops virt_ops __ro_after_init;

__visible bool virt_rebooting;
EXPORT_SYMBOL_FOR_KVM(virt_rebooting);

static DEFINE_PER_CPU(int, virtualization_nr_users);

static cpu_emergency_virt_cb __rcu *kvm_emergency_callback;

void x86_virt_register_emergency_callback(cpu_emergency_virt_cb *callback)
{
	if (WARN_ON_ONCE(rcu_access_pointer(kvm_emergency_callback)))
		return;

	rcu_assign_pointer(kvm_emergency_callback, callback);
}
EXPORT_SYMBOL_FOR_KVM(x86_virt_register_emergency_callback);

void x86_virt_unregister_emergency_callback(cpu_emergency_virt_cb *callback)
{
	if (WARN_ON_ONCE(rcu_access_pointer(kvm_emergency_callback) != callback))
		return;

	rcu_assign_pointer(kvm_emergency_callback, NULL);
	synchronize_rcu();
}
EXPORT_SYMBOL_FOR_KVM(x86_virt_unregister_emergency_callback);

static void x86_virt_invoke_kvm_emergency_callback(void)
{
	cpu_emergency_virt_cb *kvm_callback;

	kvm_callback = rcu_dereference(kvm_emergency_callback);
	if (kvm_callback)
		kvm_callback();
}

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

static int x86_vmx_enable_virtualization_cpu(void)
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

/*
 * Disable VMX and clear CR4.VMXE (even if VMXOFF faults)
 *
 * Note, VMXOFF causes a #UD if the CPU is !post-VMXON, but it's impossible to
 * atomically track post-VMXON state, e.g. this may be called in NMI context.
 * Eat all faults as all other faults on VMXOFF faults are mode related, i.e.
 * faults are guaranteed to be due to the !post-VMXON check unless the CPU is
 * magically in RM, VM86, compat mode, or at CPL>0.
 */
static int x86_vmx_disable_virtualization_cpu(void)
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

static void x86_vmx_emergency_disable_virtualization_cpu(void)
{
	virt_rebooting = true;

	/*
	 * Note, CR4.VMXE can be _cleared_ in NMI context, but it can only be
	 * set in task context.  If this races with _another_ emergency call
	 * from NMI context, VMCLEAR (in KVM) and VMXOFF may #UD, but KVM and
	 * the kernel will eat those faults due to virt_rebooting being set by
	 * the interrupting NMI callback.
	 */
	if (!(__read_cr4() & X86_CR4_VMXE))
		return;

	x86_virt_invoke_kvm_emergency_callback();

	x86_vmx_disable_virtualization_cpu();
}

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
	const struct x86_virt_ops vmx_ops = {
		.feature = X86_FEATURE_VMX,
		.enable_virtualization_cpu = x86_vmx_enable_virtualization_cpu,
		.disable_virtualization_cpu = x86_vmx_disable_virtualization_cpu,
		.emergency_disable_virtualization_cpu = x86_vmx_emergency_disable_virtualization_cpu,
	};

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

	memcpy(&virt_ops, &vmx_ops, sizeof(virt_ops));
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
static __init void x86_vmx_exit(void) { }
#endif

#if IS_ENABLED(CONFIG_KVM_AMD)
static int x86_svm_enable_virtualization_cpu(void)
{
	u64 efer;

	rdmsrq(MSR_EFER, efer);
	if (efer & EFER_SVME)
		return -EBUSY;

	wrmsrq(MSR_EFER, efer | EFER_SVME);
	return 0;
}

static int x86_svm_disable_virtualization_cpu(void)
{
	int r = -EIO;
	u64 efer;

	/*
	 * Force GIF=1 prior to disabling SVM, e.g. to ensure INIT and
	 * NMI aren't blocked.
	 */
	asm goto("1: stgi\n\t"
		 _ASM_EXTABLE(1b, %l[fault])
		 ::: "memory" : fault);
	r = 0;

fault:
	rdmsrq(MSR_EFER, efer);
	wrmsrq(MSR_EFER, efer & ~EFER_SVME);
	return r;
}

static void x86_svm_emergency_disable_virtualization_cpu(void)
{
	u64 efer;

	virt_rebooting = true;

	rdmsrq(MSR_EFER, efer);
	if (!(efer & EFER_SVME))
		return;

	x86_virt_invoke_kvm_emergency_callback();

	x86_svm_disable_virtualization_cpu();
}

static __init int x86_svm_init(void)
{
	const struct x86_virt_ops svm_ops = {
		.feature = X86_FEATURE_SVM,
		.enable_virtualization_cpu = x86_svm_enable_virtualization_cpu,
		.disable_virtualization_cpu = x86_svm_disable_virtualization_cpu,
		.emergency_disable_virtualization_cpu = x86_svm_emergency_disable_virtualization_cpu,
	};

	if (!cpu_feature_enabled(X86_FEATURE_SVM))
		return -EOPNOTSUPP;

	memcpy(&virt_ops, &svm_ops, sizeof(virt_ops));
	return 0;
}
#else
static __init int x86_svm_init(void) { return -EOPNOTSUPP; }
#endif

int x86_virt_get_ref(int feat)
{
	int r;

	/* Ensure the !feature check can't get false positives. */
	BUILD_BUG_ON(!X86_FEATURE_SVM || !X86_FEATURE_VMX);

	if (!virt_ops.feature || virt_ops.feature != feat)
		return -EOPNOTSUPP;

	guard(preempt)();

	if (this_cpu_inc_return(virtualization_nr_users) > 1)
		return 0;

	r = virt_ops.enable_virtualization_cpu();
	if (r)
		WARN_ON_ONCE(this_cpu_dec_return(virtualization_nr_users));

	return r;
}
EXPORT_SYMBOL_FOR_KVM(x86_virt_get_ref);

void x86_virt_put_ref(int feat)
{
	guard(preempt)();

	if (WARN_ON_ONCE(!this_cpu_read(virtualization_nr_users)) ||
	    this_cpu_dec_return(virtualization_nr_users))
		return;

	BUG_ON(virt_ops.disable_virtualization_cpu() && !virt_rebooting);
}
EXPORT_SYMBOL_FOR_KVM(x86_virt_put_ref);

/*
 * Disable virtualization, i.e. VMX or SVM, to ensure INIT is recognized during
 * reboot.  VMX blocks INIT if the CPU is post-VMXON, and SVM blocks INIT if
 * GIF=0, i.e. if the crash occurred between CLGI and STGI.
 */
int x86_virt_emergency_disable_virtualization_cpu(void)
{
	if (!virt_ops.feature)
		return -EOPNOTSUPP;

	/*
	 * IRQs must be disabled as virtualization is enabled in hardware via
	 * function call IPIs, i.e. IRQs need to be disabled to guarantee
	 * virtualization stays disabled.
	 */
	lockdep_assert_irqs_disabled();

	/*
	 * Do the NMI shootdown even if virtualization is off on _this_ CPU, as
	 * other CPUs may have virtualization enabled.
	 *
	 * TODO: Track whether or not virtualization might be enabled on other
	 *	 CPUs?  May not be worth avoiding the NMI shootdown...
	 */
	virt_ops.emergency_disable_virtualization_cpu();
	return 0;
}

void __init x86_virt_init(void)
{
	/*
	 * Attempt to initialize both SVM and VMX, and simply use whichever one
	 * is present.  Rsefuse to enable/use SVM or VMX if both are somehow
	 * supported.  No known CPU supports both SVM and VMX.
	 */
	bool has_vmx = !x86_vmx_init();
	bool has_svm = !x86_svm_init();

	if (WARN_ON_ONCE(has_vmx && has_svm)) {
		x86_vmx_exit();
		memset(&virt_ops, 0, sizeof(virt_ops));
	}
}
