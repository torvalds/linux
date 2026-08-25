/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_MSRS_H
#define ARCH_X86_KVM_MSRS_H

#include <linux/kvm_host.h>
#include <linux/user-return-notifier.h>

#include "cpuid.h"
#include "regs.h"

extern bool report_ignored_msrs;
extern bool ignore_msrs;

extern u32 __read_mostly kvm_nr_uret_msrs;

static inline void kvm_pr_unimpl_wrmsr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	if (report_ignored_msrs)
		vcpu_unimpl(vcpu, "Unhandled WRMSR(0x%x) = 0x%llx\n", msr, data);
}

static inline void kvm_pr_unimpl_rdmsr(struct kvm_vcpu *vcpu, u32 msr)
{
	if (report_ignored_msrs)
		vcpu_unimpl(vcpu, "Unhandled RDMSR(0x%x)\n", msr);
}

/*
 * The first...last VMX feature MSRs that are emulated by KVM.  This may or may
 * not cover all known VMX MSRs, as KVM doesn't emulate an MSR until there's an
 * associated feature that KVM supports for nested virtualization.
 */
#define KVM_FIRST_EMULATED_VMX_MSR	MSR_IA32_VMX_BASIC
#define KVM_LAST_EMULATED_VMX_MSR	MSR_IA32_VMX_VMFUNC

/*
 * KVM's internal, non-ABI indices for synthetic MSRs. The values themselves
 * are arbitrary and have no meaning, the only requirement is that they don't
 * conflict with "real" MSRs that KVM supports. Use values at the upper end
 * of KVM's reserved paravirtual MSR range to minimize churn, i.e. these values
 * will be usable until KVM exhausts its supply of paravirtual MSR indices.
 */
#define MSR_KVM_INTERNAL_GUEST_SSP	0x4b564dff

#define MSR_IA32_CR_PAT_DEFAULT	\
	PAT_VALUE(WB, WT, UC_MINUS, UC, WB, WT, UC_MINUS, UC)

void kvm_init_msr_lists(void);
int kvm_get_msr_index_list(struct kvm_msr_list __user *user_msr_list);
int kvm_get_feature_msr_index_list(struct kvm_msr_list __user *user_msr_list);
int kvm_get_feature_msrs(struct kvm_msrs __user *user_msrs);

int kvm_get_msrs(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs);
int kvm_set_msrs(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs);

int kvm_get_set_one_reg(struct kvm_vcpu *vcpu, unsigned int ioctl,
			void __user *argp);
int kvm_get_reg_list(struct kvm_vcpu *vcpu,
		     struct kvm_reg_list __user *user_list);

bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer);
int kvm_emulate_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data);
int kvm_emulate_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data);
int __kvm_emulate_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data);
int __kvm_emulate_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data);
int kvm_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data);
int kvm_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data);
int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu);
int kvm_emulate_rdmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg);
int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu);
int kvm_emulate_wrmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg);

fastpath_t handle_fastpath_wrmsr(struct kvm_vcpu *vcpu);
fastpath_t handle_fastpath_wrmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg);

int kvm_get_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);

int kvm_add_user_return_msr(u32 msr);
int kvm_find_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned index, u64 val, u64 mask);
u64 kvm_get_user_return_msr(unsigned int slot);

static inline bool kvm_is_supported_user_return_msr(u32 msr)
{
	return kvm_find_user_return_msr(msr) >= 0;
}

void kvm_user_return_msr_cpu_online(void);
void drop_user_return_notifiers(void);
void kvm_destroy_user_return_msrs(void);

int kvm_emulator_get_msr_with_filter(struct kvm_vcpu *vcpu, u32 msr_index,
				     u64 *pdata);
int kvm_emulator_set_msr_with_filter(struct kvm_vcpu *vcpu, u32 msr_index,
				     u64 data);
int kvm_emulator_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata);

bool kvm_msr_allowed(struct kvm_vcpu *vcpu, u32 index, u32 type);

enum kvm_msr_access {
	MSR_TYPE_R	= BIT(0),
	MSR_TYPE_W	= BIT(1),
	MSR_TYPE_RW	= MSR_TYPE_R | MSR_TYPE_W,
};

/*
 * Internal error codes that are used to indicate that MSR emulation encountered
 * an error that should result in #GP in the guest, unless userspace handles it.
 * Note, '1', '0', and negative numbers are off limits, as they are used by KVM
 * as part of KVM's lightly documented internal KVM_RUN return codes.
 *
 * UNSUPPORTED	- The MSR isn't supported, either because it is completely
 *		  unknown to KVM, or because the MSR should not exist according
 *		  to the vCPU model.
 *
 * FILTERED	- Access to the MSR is denied by a userspace MSR filter.
 */
#define  KVM_MSR_RET_UNSUPPORTED	2
#define  KVM_MSR_RET_FILTERED		3

int kvm_vm_ioctl_set_msr_filter(struct kvm *kvm, struct kvm_msr_filter *filter);
void kvm_free_msr_filter(struct kvm_x86_msr_filter *msr_filter);

int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);

u64 kvm_get_arch_capabilities(void);
int kvm_spec_ctrl_test_value(u64 value);

#define CET_US_RESERVED_BITS		GENMASK(9, 6)
#define CET_US_SHSTK_MASK_BITS		GENMASK(1, 0)
#define CET_US_IBT_MASK_BITS		(GENMASK_ULL(5, 2) | GENMASK_ULL(63, 10))
#define CET_US_LEGACY_BITMAP_BASE(data)	((data) >> 12)

static inline bool kvm_is_valid_u_s_cet(struct kvm_vcpu *vcpu, u64 data)
{
	if (data & CET_US_RESERVED_BITS)
		return false;
	if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) &&
	    (data & CET_US_SHSTK_MASK_BITS))
		return false;
	if (!guest_cpu_cap_has(vcpu, X86_FEATURE_IBT) &&
	    (data & CET_US_IBT_MASK_BITS))
		return false;
	if (!IS_ALIGNED(CET_US_LEGACY_BITMAP_BASE(data), 4))
		return false;
	/* IBT can be suppressed iff the TRACKER isn't WAIT_ENDBR. */
	if ((data & CET_SUPPRESS) && (data & CET_WAIT_ENDBR))
		return false;

	return true;
}

#endif
