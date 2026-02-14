/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_VIRT_H
#define _ASM_X86_VIRT_H

#include <linux/percpu-defs.h>

#include <asm/reboot.h>

#if IS_ENABLED(CONFIG_KVM_X86)
extern bool virt_rebooting;

void __init x86_virt_init(void);

#if IS_ENABLED(CONFIG_KVM_INTEL)
DECLARE_PER_CPU(struct vmcs *, root_vmcs);
#endif

#else
static __always_inline void x86_virt_init(void) {}
#endif

#endif /* _ASM_X86_VIRT_H */
