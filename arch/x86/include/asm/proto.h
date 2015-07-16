#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#include <asm/ldt.h>

/* misc architecture specific prototypes */

void syscall_init(void);

void entry_SYSCALL_64(void);
void entry_SYSCALL_compat(void);
void entry_INT80_32(void);
void entry_INT80_compat(void);
void entry_SYSENTER_32(void);
void entry_SYSENTER_compat(void);

void x86_configure_nx(void);
void x86_report_nx(void);

extern int reboot_force;

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr);

#endif /* _ASM_X86_PROTO_H */
