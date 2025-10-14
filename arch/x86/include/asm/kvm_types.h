/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_TYPES_H
#define _ASM_X86_KVM_TYPES_H

#if IS_MODULE(CONFIG_KVM_AMD) && IS_MODULE(CONFIG_KVM_INTEL)
#define KVM_SUB_MODULES kvm-amd,kvm-intel
#elif IS_MODULE(CONFIG_KVM_AMD)
#define KVM_SUB_MODULES kvm-amd
#elif IS_MODULE(CONFIG_KVM_INTEL)
#define KVM_SUB_MODULES kvm-intel
#else
#undef KVM_SUB_MODULES
#endif

#define KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE 40

#endif /* _ASM_X86_KVM_TYPES_H */
