/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_FTRACE
#define _ASM_POWERPC_FTRACE

#include <asm/types.h>

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((unsigned long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifdef __ASSEMBLY__

/* Based off of objdump optput from glibc */

#define MCOUNT_SAVE_FRAME			\
	stwu	r1,-48(r1);			\
	stw	r3, 12(r1);			\
	stw	r4, 16(r1);			\
	stw	r5, 20(r1);			\
	stw	r6, 24(r1);			\
	mflr	r3;				\
	lwz	r4, 52(r1);			\
	mfcr	r5;				\
	stw	r7, 28(r1);			\
	stw	r8, 32(r1);			\
	stw	r9, 36(r1);			\
	stw	r10,40(r1);			\
	stw	r3, 44(r1);			\
	stw	r5, 8(r1)

#define MCOUNT_RESTORE_FRAME			\
	lwz	r6, 8(r1);			\
	lwz	r0, 44(r1);			\
	lwz	r3, 12(r1);			\
	mtctr	r0;				\
	lwz	r4, 16(r1);			\
	mtcr	r6;				\
	lwz	r5, 20(r1);			\
	lwz	r6, 24(r1);			\
	lwz	r0, 52(r1);			\
	lwz	r7, 28(r1);			\
	lwz	r8, 32(r1);			\
	mtlr	r0;				\
	lwz	r9, 36(r1);			\
	lwz	r10,40(r1);			\
	addi	r1, r1, 48

#else /* !__ASSEMBLY__ */
extern void _mcount(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
       /* reloction of mcount call site is the same as the address */
       return addr;
}

struct dyn_arch_ftrace {
	struct module *mod;
};
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
#ifdef PPC64_ELF_ABI_v1
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
#endif /* PPC64_ELF_ABI_v1 */
#endif /* CONFIG_FTRACE_SYSCALLS */

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline void this_cpu_disable_ftrace(void)
{
	get_paca()->ftrace_enabled = 0;
}

static inline void this_cpu_enable_ftrace(void)
{
	get_paca()->ftrace_enabled = 1;
}
#else /* CONFIG_PPC64 */
static inline void this_cpu_disable_ftrace(void) { }
static inline void this_cpu_enable_ftrace(void) { }
#endif /* CONFIG_PPC64 */
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_FTRACE */
