/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_FTRACE_H
#define _ASM_S390_FTRACE_H

#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR
#define ARCH_SUPPORTS_FTRACE_OPS 1
#define MCOUNT_INSN_SIZE	6

#ifndef __ASSEMBLY__

#ifdef CONFIG_CC_IS_CLANG
/* https://bugs.llvm.org/show_bug.cgi?id=41424 */
#define ftrace_return_address(n) 0UL
#else
#define ftrace_return_address(n) __builtin_return_address(n)
#endif

void ftrace_caller(void);

extern char ftrace_graph_caller_end;
extern unsigned long ftrace_plt;
extern void *ftrace_func;

struct dyn_arch_ftrace { };

#define MCOUNT_ADDR 0
#define FTRACE_ADDR ((unsigned long)ftrace_caller)

#define KPROBE_ON_FTRACE_NOP	0
#define KPROBE_ON_FTRACE_CALL	1

struct module;
struct dyn_ftrace;
/*
 * Either -mhotpatch or -mnop-mcount is used - no explicit init is required
 */
static inline int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec) { return 0; }
#define ftrace_init_nop ftrace_init_nop

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct ftrace_insn {
	u16 opc;
	s32 disp;
} __packed;

static inline void ftrace_generate_nop_insn(struct ftrace_insn *insn)
{
#ifdef CONFIG_FUNCTION_TRACER
	/* brcl 0,0 */
	insn->opc = 0xc004;
	insn->disp = 0;
#endif
}

static inline int is_ftrace_nop(struct ftrace_insn *insn)
{
#ifdef CONFIG_FUNCTION_TRACER
	if (insn->disp == 0)
		return 1;
#endif
	return 0;
}

static inline void ftrace_generate_call_insn(struct ftrace_insn *insn,
					     unsigned long ip)
{
#ifdef CONFIG_FUNCTION_TRACER
	unsigned long target;

	/* brasl r0,ftrace_caller */
	target = is_module_addr((void *) ip) ? ftrace_plt : FTRACE_ADDR;
	insn->opc = 0xc005;
	insn->disp = (target - ip) / 2;
#endif
}

/*
 * Even though the system call numbers are identical for s390/s390x a
 * different system call table is used for compat tasks. This may lead
 * to e.g. incorrect or missing trace event sysfs files.
 * Therefore simply do not trace compat system calls at all.
 * See kernel/trace/trace_syscalls.c.
 */
#define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS
static inline bool arch_trace_is_compat_syscall(struct pt_regs *regs)
{
	return is_compat_task();
}

#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym,
					       const char *name)
{
	/*
	 * Skip __s390_ and __s390x_ prefix - due to compat wrappers
	 * and aliasing some symbols of 64 bit system call functions
	 * may get the __s390_ prefix instead of the __s390x_ prefix.
	 */
	return !strcmp(sym + 7, name) || !strcmp(sym + 8, name);
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_FTRACE_H */
