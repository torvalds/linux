#include <linux/linkage.h>
/* Steal i386 syscall table for our purposes, but with some slight changes.*/

#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall

#define sys_vm86old sys_ni_syscall
#define sys_vm86 sys_ni_syscall

#define old_mmap sys_old_mmap

#define ptregs_fork sys_fork
#define ptregs_execve sys_execve
#define ptregs_iopl sys_iopl
#define ptregs_vm86old sys_vm86old
#define ptregs_sigreturn sys_sigreturn
#define ptregs_clone sys_clone
#define ptregs_vm86 sys_vm86
#define ptregs_rt_sigreturn sys_rt_sigreturn
#define ptregs_sigaltstack sys_sigaltstack
#define ptregs_vfork sys_vfork

.section .rodata,"a"

#include "../../x86/kernel/syscall_table_32.S"

ENTRY(syscall_table_size)
.long .-sys_call_table
