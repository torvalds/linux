/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ELF register definitions..
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

/*
 * This is the base location for PIE (ET_DYN with INTERP) loads. On
 * 64-bit, this is raised to 4GB to leave the entire 32-bit address
 * space open for things that want to use the area for 32-bit pointers.
 */
#define ELF_ET_DYN_BASE		(is_32bit_task() ? 0x000400000UL : \
						   0x100000000UL)

#define ELF_CORE_EFLAGS (is_elf2_task() ? 2 : 0)

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

/* ELF_HWCAP yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */
# define ELF_HWCAP	(cur_cpu_spec->cpu_user_features)
# define ELF_HWCAP2	(cur_cpu_spec->cpu_user_features2)

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
	if (((ex).e_flags & 0x3) == 2)				\
		set_thread_flag(TIF_ELF2ABI);			\
	else							\
		clear_thread_flag(TIF_ELF2ABI);			\
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

#ifdef CONFIG_SPU_BASE
/* Notes used in ET_CORE. Note name is "SPU/<fd>/<filename>". */
#define NT_SPU		1

#define ARCH_HAVE_EXTRA_ELF_NOTES

#endif /* CONFIG_SPU_BASE */

#ifdef CONFIG_PPC64

#define get_cache_geometry(level) \
	(ppc64_caches.level.assoc << 16 | ppc64_caches.level.line_size)

#define ARCH_DLINFO_CACHE_GEOMETRY					\
	NEW_AUX_ENT(AT_L1I_CACHESIZE, ppc64_caches.l1i.size);		\
	NEW_AUX_ENT(AT_L1I_CACHEGEOMETRY, get_cache_geometry(l1i));	\
	NEW_AUX_ENT(AT_L1D_CACHESIZE, ppc64_caches.l1d.size);		\
	NEW_AUX_ENT(AT_L1D_CACHEGEOMETRY, get_cache_geometry(l1d));	\
	NEW_AUX_ENT(AT_L2_CACHESIZE, ppc64_caches.l2.size);		\
	NEW_AUX_ENT(AT_L2_CACHEGEOMETRY, get_cache_geometry(l2));	\
	NEW_AUX_ENT(AT_L3_CACHESIZE, ppc64_caches.l3.size);		\
	NEW_AUX_ENT(AT_L3_CACHEGEOMETRY, get_cache_geometry(l3))

#else
#define ARCH_DLINFO_CACHE_GEOMETRY
#endif

/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 * update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes
 */
#define COMMON_ARCH_DLINFO						\
do {									\
	/* Handle glibc compatibility. */				\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	/* Cache size items */						\
	NEW_AUX_ENT(AT_DCACHEBSIZE, dcache_bsize);			\
	NEW_AUX_ENT(AT_ICACHEBSIZE, icache_bsize);			\
	NEW_AUX_ENT(AT_UCACHEBSIZE, 0);					\
	VDSO_AUX_ENT(AT_SYSINFO_EHDR, (unsigned long)current->mm->context.vdso);\
	ARCH_DLINFO_CACHE_GEOMETRY;					\
} while (0)

#define ARCH_DLINFO							\
do {									\
	COMMON_ARCH_DLINFO;						\
	NEW_AUX_ENT(AT_MINSIGSTKSZ, get_min_sigframe_size());		\
} while (0)

#define COMPAT_ARCH_DLINFO						\
do {									\
	COMMON_ARCH_DLINFO;						\
	NEW_AUX_ENT(AT_MINSIGSTKSZ, get_min_sigframe_size_compat());	\
} while (0)

/* Relocate the kernel image to @final_address */
void relocate(unsigned long final_address);

struct func_desc {
	unsigned long addr;
	unsigned long toc;
	unsigned long env;
};

#endif /* _ASM_POWERPC_ELF_H */
