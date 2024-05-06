/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_KVM_PARA_H
#define _ASM_LOONGARCH_KVM_PARA_H

/*
 * LoongArch hypercall return code
 */
#define KVM_HCALL_SUCCESS		0
#define KVM_HCALL_INVALID_CODE		-1UL
#define KVM_HCALL_INVALID_PARAMETER	-2UL

static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

static inline unsigned int kvm_arch_para_hints(void)
{
	return 0;
}

static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

#endif /* _ASM_LOONGARCH_KVM_PARA_H */
