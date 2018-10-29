/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
#define ARCH_SUPPORTS_FTRACE_OPS 1
#endif

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((unsigned long)(__gnu_mcount_nc))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
extern void __gnu_mcount_nc(void);

#ifdef CONFIG_DYNAMIC_FTRACE
struct dyn_arch_ftrace {
};

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/* With Thumb-2, the recorded addresses have the lsb set */
	return addr & ~1;
}

extern void ftrace_caller_old(void);
extern void ftrace_call_old(void);
#endif

#endif

#endif

#ifndef __ASSEMBLY__

#if defined(CONFIG_FRAME_POINTER) && !defined(CONFIG_ARM_UNWIND)
/*
 * return_address uses walk_stackframe to do it's work.  If both
 * CONFIG_FRAME_POINTER=y and CONFIG_ARM_UNWIND=y walk_stackframe uses unwind
 * information.  For this to work in the function tracer many functions would
 * have to be marked with __notrace.  So for now just depend on
 * !CONFIG_ARM_UNWIND.
 */

void *return_address(unsigned int);

#else

static inline void *return_address(unsigned int level)
{
	return NULL;
}

#endif

#define ftrace_return_address(n) return_address(n)

#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME

static inline bool arch_syscall_match_sym_name(const char *sym,
					       const char *name)
{
	if (!strcmp(sym, "sys_mmap2"))
		sym = "sys_mmap_pgoff";
	else if (!strcmp(sym, "sys_statfs64_wrapper"))
		sym = "sys_statfs64";
	else if (!strcmp(sym, "sys_fstatfs64_wrapper"))
		sym = "sys_fstatfs64";
	else if (!strcmp(sym, "sys_arm_fadvise64_64"))
		sym = "sys_fadvise64_64";

	/* Ignore case since sym may start with "SyS" instead of "sys" */
	return !strcasecmp(sym, name);
}

#endif /* ifndef __ASSEMBLY__ */

#endif /* _ASM_ARM_FTRACE */
