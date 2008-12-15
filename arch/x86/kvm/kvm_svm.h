#ifndef __KVM_SVM_H
#define __KVM_SVM_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/kvm_host.h>
#include <asm/msr.h>

#include <asm/svm.h>

static const u32 host_save_user_msrs[] = {
#ifdef CONFIG_X86_64
	MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SYSCALL_MASK, MSR_KERNEL_GS_BASE,
	MSR_FS_BASE,
#endif
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
};

#define NR_HOST_SAVE_USER_MSRS ARRAY_SIZE(host_save_user_msrs)

struct kvm_vcpu;

struct vcpu_svm {
	struct kvm_vcpu vcpu;
	struct vmcb *vmcb;
	unsigned long vmcb_pa;
	struct svm_cpu_data *svm_data;
	uint64_t asid_generation;

	u64 next_rip;

	u64 host_user_msrs[NR_HOST_SAVE_USER_MSRS];
	u64 host_gs_base;
	unsigned long host_cr2;

	u32 *msrpm;
	struct vmcb *hsave;
	u64 hsave_msr;

	u64 nested_vmcb;

	/* These are the merged vectors */
	u32 *nested_msrpm;

	/* gpa pointers to the real vectors */
	u64 nested_vmcb_msrpm;
};

#endif

