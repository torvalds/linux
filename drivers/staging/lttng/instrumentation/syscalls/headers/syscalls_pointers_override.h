#define OVERRIDE_32_sys_execve
#define OVERRIDE_64_sys_execve

#ifndef CREATE_SYSCALL_TABLE

SC_TRACE_EVENT(sys_execve,
	TP_PROTO(const char *filename, char *const *argv, char *const *envp),
	TP_ARGS(filename, argv, envp),
	TP_STRUCT__entry(__string_from_user(filename, filename)
		__field_hex(char *const *, argv)
		__field_hex(char *const *, envp)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename)
		tp_assign(argv, argv)
		tp_assign(envp, envp)),
	TP_printk()
)

SC_TRACE_EVENT(sys_clone,
	TP_PROTO(unsigned long clone_flags, unsigned long newsp,
		void __user *parent_tid,
		void __user *child_tid),
	TP_ARGS(clone_flags, newsp, parent_tid, child_tid),
	TP_STRUCT__entry(
		__field_hex(unsigned long, clone_flags)
		__field_hex(unsigned long, newsp)
		__field_hex(void *, parent_tid)
		__field_hex(void *, child_tid)),
	TP_fast_assign(
		tp_assign(clone_flags, clone_flags)
		tp_assign(newsp, newsp)
		tp_assign(parent_tid, parent_tid)
		tp_assign(child_tid, child_tid)),
	TP_printk()
)

/* present in 32, missing in 64 due to old kernel headers */
#define OVERRIDE_32_sys_getcpu
#define OVERRIDE_64_sys_getcpu
SC_TRACE_EVENT(sys_getcpu,
	TP_PROTO(unsigned __user *cpup, unsigned __user *nodep, void *tcache),
	TP_ARGS(cpup, nodep, tcache),
	TP_STRUCT__entry(
		__field_hex(unsigned *, cpup)
		__field_hex(unsigned *, nodep)
		__field_hex(void *, tcache)),
	TP_fast_assign(
		tp_assign(cpup, cpup)
		tp_assign(nodep, nodep)
		tp_assign(tcache, tcache)),
	TP_printk()
)

#endif /* CREATE_SYSCALL_TABLE */
