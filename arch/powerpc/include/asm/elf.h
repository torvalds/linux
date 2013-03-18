/*
 * ELF register definitions..
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_ELF_H
#define _ASM_POWERPC_ELF_H

#include <linux/sched.h>	/* for task_struct */
#include <asm/page.h>
#include <asm/string.h>
#include <uapi/asm/elf.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == ELF_ARCH)
#define compat_elf_check_arch(x)	((x)->e_machine == EM_PPC)

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

extern unsigned long randomize_et_dyn(unsigned long base);
#define ELF_ET_DYN_BASE		(randomize_et_dyn(0x20000000))

/*
 * Our registers are always unsigned longs, whether we're a 32 bit
 * process or 64 bit, on either a 64 bit or 32 bit kernel.
 *
 * This macro relies on elf_regs[i] having the right type to truncate to,
 * either u32 or u64.  It defines the body of the elf_core_copy_regs
 * function, either the native one with elf_gregset_t elf_regs or
 * the 32-bit one with elf_gregset_t32 elf_regs.
 */
#define PPC_ELF_CORE_COPY_REGS(elf_regs, regs) \
	int i, nregs = min(sizeof(*regs) / sizeof(unsigned long), \
			   (size_t)ELF_NGREG);			  \
	for (i = 0; i < nregs; i++) \
		elf_regs[i] = ((unsigned long *) regs)[i]; \
	memset(&elf_regs[i], 0, (ELF_NGREG - i) * sizeof(elf_regs[0]))

/* Common routine for both 32-bit and 64-bit native processes */
static inline void ppc_elf_core_copy_regs(elf_gregset_t elf_regs,
					  struct pt_regs *regs)
{
	PPC_ELF_CORE_COPY_REGS(elf_regs, regs);
}
#define ELF_CORE_COPY_REGS(gregs, regs) ppc_elf_core_copy_regs(gregs, regs);

typedef elf_vrregset_t elf_fpxregset_t;

/* ELF_HWCAP yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */
# define ELF_HWCAP	(cur_cpu_spec->cpu_user_features)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM	(cur_cpu_spec->platform)

/* While ELF_PLATFORM indicates the ISA supported by the platform, it
 * may not accurately reflect the underlying behavior of the hardware
 * (as in the case of running in Power5+ compatibility mode on a
 * Power6 machine).  ELF_BASE_PLATFORM allows ld.so to load libraries
 * that are tuned for the real hardware.
 */
#define ELF_BASE_PLATFORM (powerpc_base_platform)

#ifdef __powerpc64__
# define ELF_PLAT_INIT(_r, load_addr)	do {	\
	_r->gpr[2] = load_addr; 		\
} while (0)
#endif /* __powerpc64__ */

#ifdef __powerpc64__
# define SET_PERSONALITY(ex)					\
do {								\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)		\
		set_thread_flag(TIF_32BIT);			\
	else							\
		clear_thread_flag(TIF_32BIT);			\
	if (personality(current->personality) != PER_LINUX32)	\
		set_personality(PER_LINUX |			\
			(current->personality & (~PER_MASK)));	\
} while (0)
/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically. This
 * is only required to work around bugs in old 32bit toolchains. Since
 * the 64bit ABI has never had these issues dont enable the workaround
 * even if we have an executable stack.
 */
# define elf_read_implies_exec(ex, exec_stk) (is_32bit_task() ? \
		(exec_stk == EXSTACK_DEFAULT) : 0)
#else 
# define elf_read_implies_exec(ex, exec_stk) (exec_stk == EXSTACK_DEFAULT)
#endif /* __powerpc64__ */

extern int dcache_bsize;
extern int icache_bsize;
extern int ucache_bsize;

/* vDSO has arch_setup_additional_pages */
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);
#define VDSO_AUX_ENT(a,b) NEW_AUX_ENT(a,b)

/* 1GB for 64bit, 8MB for 32bit */
#define STACK_RND_MASK (is_32bit_task() ? \
	(0x7ff >> (PAGE_SHIFT - 12)) : \
	(0x3ffff >> (PAGE_SHIFT - 12)))

extern unsigned long arch_randomize_brk(struct mm_struct *mm);
#define arch_randomize_brk arch_randomize_brk


#ifdef CONFIG_SPU_BASE
/* Notes used in ET_CORE. Note name is "SPU/<fd>/<filename>". */
#define NT_SPU		1

#define ARCH_HAVE_EXTRA_ELF_NOTES

#endif /* CONFIG_SPU_BASE */

#endif /* _ASM_POWERPC_ELF_H */
