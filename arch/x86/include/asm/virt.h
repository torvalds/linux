/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_VIRT_H
#define _ASM_X86_VIRT_H

#include <asm/reboot.h>

#if IS_ENABLED(CONFIG_KVM_X86)
extern bool virt_rebooting;

void __init x86_virt_init(void);

#if IS_ENABLED(CONFIG_KVM_INTEL)
int x86_vmx_enable_virtualization_cpu(void);
int x86_vmx_disable_virtualization_cpu(void);
void x86_vmx_emergency_disable_virtualization_cpu(void);
#endif

#if IS_ENABLED(CONFIG_KVM_AMD)
int x86_svm_enable_virtualization_cpu(void);
int x86_svm_disable_virtualization_cpu(void);
void x86_svm_emergency_disable_virtualization_cpu(void);
#endif

#else
static __always_inline void x86_virt_init(void) {}
#endif

#endif /* _ASM_X86_VIRT_H */
