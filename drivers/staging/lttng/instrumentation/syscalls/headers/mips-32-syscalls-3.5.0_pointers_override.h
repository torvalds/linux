#ifndef CREATE_SYSCALL_TABLE

#else	/* CREATE_SYSCALL_TABLE */

#define OVERRIDE_TABLE_32_sys_clone
TRACE_SYSCALL_TABLE(sys_clone, sys_clone, 4120, 0)

#endif /* CREATE_SYSCALL_TABLE */
