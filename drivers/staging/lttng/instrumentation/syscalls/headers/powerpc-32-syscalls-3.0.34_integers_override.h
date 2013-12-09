#ifndef CREATE_SYSCALL_TABLE

#else	/* CREATE_SYSCALL_TABLE */

#define OVVERRIDE_TABLE_32_sys_mmap
TRACE_SYSCALL_TABLE(sys_mmap, sys_mmap, 90, 6)

#endif /* CREATE_SYSCALL_TABLE */

