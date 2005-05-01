#include <linux/linkage.h>
/* Steal i386 syscall table for our purposes, but with some slight changes.*/

#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall

#define sys_vm86old sys_ni_syscall
#define sys_vm86 sys_ni_syscall
#define sys_set_thread_area sys_ni_syscall
#define sys_get_thread_area sys_ni_syscall

#define sys_stime um_stime
#define sys_time um_time
#define old_mmap old_mmap_i386

#include "../../i386/kernel/syscall_table.S"
