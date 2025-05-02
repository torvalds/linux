/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#include <asm/ldt.h>

struct task_struct;

/* misc architecture specific prototypes */

void syscall_init(void);

#ifdef CONFIG_X86_64
void entry_SYSCALL_64(void);
void entry_SYSCALL_64_safe_stack(void);
void entry_SYSRETQ_unsafe_stack(void);
void entry_SYSRETQ_end(void);
#endif

#ifdef CONFIG_X86_32
void entry_INT80_32(void);
void entry_SYSENTER_32(void);
void __begin_SYSENTER_singlestep_region(void);
void __end_SYSENTER_singlestep_region(void);
#endif

#ifdef CONFIG_IA32_EMULATION
void entry_SYSENTER_compat(void);
void __end_entry_SYSENTER_compat(void);
void entry_SYSCALL_compat(void);
void entry_SYSCALL_compat_safe_stack(void);
void entry_SYSRETL_compat_unsafe_stack(void);
void entry_SYSRETL_compat_end(void);
#else /* !CONFIG_IA32_EMULATION */
#define entry_SYSCALL_compat NULL
#define entry_SYSENTER_compat NULL
#endif

void x86_configure_nx(void);

extern int reboot_force;

long do_arch_prctl_64(struct task_struct *task, int option, unsigned long arg2);

#endif /* _ASM_X86_PROTO_H */
