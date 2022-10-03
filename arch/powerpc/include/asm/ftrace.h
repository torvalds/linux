/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_FTRACE
#define _ASM_POWERPC_FTRACE

#include <asm/types.h>

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((unsigned long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

#ifndef __ASSEMBLY__
extern void _mcount(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
       /* relocation of mcount call site is the same as the address */
       return addr;
}

unsigned long prepare_ftrace_return(unsigned long parent, unsigned long ip,
				    unsigned long sp);

struct dyn_arch_ftrace {
	struct module *mod;
};

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
struct ftrace_regs {
	struct pt_regs regs;
};

static __always_inline struct pt_regs *arch_ftrace_get_regs(struct ftrace_regs *fregs)
{
	/* We clear regs.msr in ftrace_call */
	return fregs->regs.msr ? &fregs->regs : NULL;
}

static __always_inline void ftrace_instruction_pointer_set(struct ftrace_regs *fregs,
							   unsigned long ip)
{
	regs_set_return_ip(&fregs->regs, ip);
}

struct ftrace_ops;

#define ftrace_graph_func ftrace_graph_func
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs);
#endif
#endif /* __ASSEMBLY__ */

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
#define ARCH_SUPPORTS_FTRACE_OPS 1
#endif
#endif /* CONFIG_FUNCTION_TRACER */

#ifndef __ASSEMBLY__
#ifdef CONFIG_FTRACE_SYSCALLS
/*
 * Some syscall entry functions on powerpc start with "ppc_" (fork and clone,
 * for instance) or ppc32_/ppc64_. We should also match the sys_ variant with
 * those.
 */
#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME
#ifdef CONFIG_PPC64_ELF_ABI_V1
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	/* We need to skip past the initial dot, and the __se_sys alias */
	return !strcmp(sym + 1, name) ||
		(!strncmp(sym, ".__se_sys", 9) && !strcmp(sym + 6, name)) ||
		(!strncmp(sym, ".ppc_", 5) && !strcmp(sym + 5, name + 4)) ||
		(!strncmp(sym, ".ppc32_", 7) && !strcmp(sym + 7, name + 4)) ||
		(!strncmp(sym, ".ppc64_", 7) && !strcmp(sym + 7, name + 4));
}
#else
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	return !strcmp(sym, name) ||
		(!strncmp(sym, "__se_sys", 8) && !strcmp(sym + 5, name)) ||
		(!strncmp(sym, "ppc_", 4) && !strcmp(sym + 4, name + 4)) ||
		(!strncmp(sym, "ppc32_", 6) && !strcmp(sym + 6, name + 4)) ||
		(!strncmp(sym, "ppc64_", 6) && !strcmp(sym + 6, name + 4));
}
#endif /* CONFIG_PPC64_ELF_ABI_V1 */
#endif /* CONFIG_FTRACE_SYSCALLS */

#if defined(CONFIG_PPC64) && defined(CONFIG_FUNCTION_TRACER)
#include <asm/paca.h>

static inline void this_cpu_disable_ftrace(void)
{
	get_paca()->ftrace_enabled = 0;
}

static inline void this_cpu_enable_ftrace(void)
{
	get_paca()->ftrace_enabled = 1;
}

/* Disable ftrace on this CPU if possible (may not be implemented) */
static inline void this_cpu_set_ftrace_enabled(u8 ftrace_enabled)
{
	get_paca()->ftrace_enabled = ftrace_enabled;
}

static inline u8 this_cpu_get_ftrace_enabled(void)
{
	return get_paca()->ftrace_enabled;
}

void ftrace_free_init_tramp(void);
#else /* CONFIG_PPC64 */
static inline void this_cpu_disable_ftrace(void) { }
static inline void this_cpu_enable_ftrace(void) { }
static inline void this_cpu_set_ftrace_enabled(u8 ftrace_enabled) { }
static inline u8 this_cpu_get_ftrace_enabled(void) { return 1; }
static inline void ftrace_free_init_tramp(void) { }
#endif /* CONFIG_PPC64 */
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_FTRACE */
