/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_ELF_H
#define _ASM_TILE_ELF_H

/*
 * ELF register definitions.
 */

#include <arch/chip.h>

#include <linux/ptrace.h>
#include <linux/elf-em.h>
#include <asm/byteorder.h>
#include <asm/page.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof(struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* Provide a nominal data structure. */
#define ELF_NFPREG	0
typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#ifdef __tilegx__
#define ELF_CLASS	ELFCLASS64
#else
#define ELF_CLASS	ELFCLASS32
#endif
#ifdef __BIG_ENDIAN__
#define ELF_DATA	ELFDATA2MSB
#else
#define ELF_DATA	ELFDATA2LSB
#endif

/*
 * There seems to be a bug in how compat_binfmt_elf.c works: it
 * #undefs ELF_ARCH, but it is then used in binfmt_elf.c for fill_note_info().
 * Hack around this by providing an enum value of ELF_ARCH.
 */
enum { ELF_ARCH = CHIP_ELF_TYPE() };
#define ELF_ARCH ELF_ARCH

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)  \
	((x)->e_ident[EI_CLASS] == ELF_CLASS && \
	 (x)->e_ident[EI_DATA] == ELF_DATA && \
	 (x)->e_machine == CHIP_ELF_TYPE())

/* The module loader only handles a few relocation types. */
#ifndef __tilegx__
#define R_TILE_32                 1
#define R_TILE_JOFFLONG_X1       15
#define R_TILE_IMM16_X0_LO       25
#define R_TILE_IMM16_X1_LO       26
#define R_TILE_IMM16_X0_HA       29
#define R_TILE_IMM16_X1_HA       30
#else
#define R_TILEGX_64                       1
#define R_TILEGX_JUMPOFF_X1              21
#define R_TILEGX_IMM16_X0_HW0            36
#define R_TILEGX_IMM16_X1_HW0            37
#define R_TILEGX_IMM16_X0_HW1            38
#define R_TILEGX_IMM16_X1_HW1            39
#define R_TILEGX_IMM16_X0_HW2_LAST       48
#define R_TILEGX_IMM16_X1_HW2_LAST       49
#endif

/* Use standard page size for core dumps. */
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE         (TASK_SIZE / 3 * 2)

#define ELF_CORE_COPY_REGS(_dest, _regs)			\
	memcpy((char *) &_dest, (char *) _regs,			\
	       sizeof(struct pt_regs));

/* No additional FP registers to copy. */
#define ELF_CORE_COPY_FPREGS(t, fpu) 0

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */
#define ELF_HWCAP	(0)

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */
#define ELF_PLATFORM  (NULL)

extern void elf_plat_init(struct pt_regs *regs, unsigned long load_addr);

#define ELF_PLAT_INIT(_r, load_addr) elf_plat_init(_r, load_addr)

extern int dump_task_regs(struct task_struct *, elf_gregset_t *);
#define ELF_CORE_COPY_TASK_REGS(tsk, elf_regs) dump_task_regs(tsk, elf_regs)

/* Tilera Linux has no personalities currently, so no need to do anything. */
#define SET_PERSONALITY(ex) do { } while (0)

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
/* Support auto-mapping of the user interrupt vectors. */
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int executable_stack);
/* update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes */
#define ARCH_DLINFO \
do { \
	NEW_AUX_ENT(AT_SYSINFO_EHDR, VDSO_BASE); \
} while (0)

struct mm_struct;
extern unsigned long arch_randomize_brk(struct mm_struct *mm);
#define arch_randomize_brk arch_randomize_brk

#ifdef CONFIG_COMPAT

#define COMPAT_ELF_PLATFORM "tilegx-m32"

/*
 * "Compat" binaries have the same machine type, but 32-bit class,
 * since they're not a separate machine type, but just a 32-bit
 * variant of the standard 64-bit architecture.
 */
#define compat_elf_check_arch(x)  \
	((x)->e_ident[EI_CLASS] == ELFCLASS32 && \
	 (x)->e_machine == CHIP_ELF_TYPE())

#define compat_start_thread(regs, ip, usp) do { \
		regs->pc = ptr_to_compat_reg((void *)(ip)); \
		regs->sp = ptr_to_compat_reg((void *)(usp)); \
		single_step_execve();	\
	} while (0)

/*
 * Use SET_PERSONALITY to indicate compatibility via TS_COMPAT.
 */
#undef SET_PERSONALITY
#define SET_PERSONALITY(ex) \
do { \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK))); \
	current_thread_info()->status &= ~TS_COMPAT; \
} while (0)
#define COMPAT_SET_PERSONALITY(ex) \
do { \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK))); \
	current_thread_info()->status |= TS_COMPAT; \
} while (0)

#define COMPAT_ELF_ET_DYN_BASE (0xffffffff / 3 * 2)

#endif /* CONFIG_COMPAT */

#define CORE_DUMP_USE_REGSET

#endif /* _ASM_TILE_ELF_H */
