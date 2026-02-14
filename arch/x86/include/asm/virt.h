/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_VIRT_H
#define _ASM_X86_VIRT_H

#include <asm/reboot.h>

typedef void (cpu_emergency_virt_cb)(void);

#if IS_ENABLED(CONFIG_KVM_X86)
extern bool virt_rebooting;

void __init x86_virt_init(void);

int x86_virt_get_ref(int feat);
void x86_virt_put_ref(int feat);

int x86_virt_emergency_disable_virtualization_cpu(void);

void x86_virt_register_emergency_callback(cpu_emergency_virt_cb *callback);
void x86_virt_unregister_emergency_callback(cpu_emergency_virt_cb *callback);
#else
static __always_inline void x86_virt_init(void) {}
static inline int x86_virt_emergency_disable_virtualization_cpu(void) { return -ENOENT; }
#endif

#endif /* _ASM_X86_VIRT_H */
