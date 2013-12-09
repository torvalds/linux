
#ifndef CREATE_SYSCALL_TABLE

# ifndef CONFIG_UID16
#  define OVERRIDE_32_sys_getgroups16
#  define OVERRIDE_32_sys_setgroups16
#  define OVERRIDE_32_sys_lchown16
#  define OVERRIDE_32_sys_getresuid16
#  define OVERRIDE_32_sys_getresgid16
#  define OVERRIDE_32_sys_chown16
# endif

#else	/* CREATE_SYSCALL_TABLE */

# ifndef CONFIG_UID16
#  define OVERRIDE_TABLE_32_sys_getgroups16
#  define OVERRIDE_TABLE_32_sys_setgroups16
#  define OVERRIDE_TABLE_32_sys_lchown16
#  define OVERRIDE_TABLE_32_sys_getresuid16
#  define OVERRIDE_TABLE_32_sys_getresgid16
#  define OVERRIDE_TABLE_32_sys_chown16
# endif

#define OVERRIDE_TABLE_32_sys_execve
TRACE_SYSCALL_TABLE(sys_execve, sys_execve, 11, 3)
#define OVERRIDE_TABLE_32_sys_clone
TRACE_SYSCALL_TABLE(sys_clone, sys_clone, 120, 5)
#define OVERRIDE_TABLE_32_sys_getcpu
TRACE_SYSCALL_TABLE(sys_getcpu, sys_getcpu, 318, 3)

#endif /* CREATE_SYSCALL_TABLE */


