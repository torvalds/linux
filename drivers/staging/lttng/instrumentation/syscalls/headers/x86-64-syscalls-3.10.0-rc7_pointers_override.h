#ifndef CREATE_SYSCALL_TABLE

#else	/* CREATE_SYSCALL_TABLE */

#define OVERRIDE_TABLE_64_sys_clone
TRACE_SYSCALL_TABLE(sys_clone, sys_clone, 56, 5)
#define OVERRIDE_TABLE_64_sys_execve
TRACE_SYSCALL_TABLE(sys_execve, sys_execve, 59, 3)
#define OVERRIDE_TABLE_64_sys_getcpu
TRACE_SYSCALL_TABLE(sys_getcpu, sys_getcpu, 309, 3)

#endif /* CREATE_SYSCALL_TABLE */
