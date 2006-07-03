#include <linux/linkage.h>
/* Steal i386 syscall table for our purposes, but with some slight changes.*/

#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall

#define sys_vm86old sys_ni_syscall
#define sys_vm86 sys_ni_syscall

#define old_mmap old_mmap_i386

#include "../../i386/kernel/syscall_table.S"
