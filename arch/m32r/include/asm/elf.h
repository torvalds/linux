#ifndef _ASM_M32R__ELF_H
#define _ASM_M32R__ELF_H

/*
 * ELF-specific definitions.
 *
 * Copyright (C) 1999-2004, Renesas Technology Corp.
 *      Hirokazu Takata <takata at linux-m32r.org>
 */

#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/page.h>

/* M32R relocation types  */
#define	R_M32R_NONE		0
#define	R_M32R_16		1
#define	R_M32R_32		2
#define	R_M32R_24		3
#define	R_M32R_10_PCREL		4
#define	R_M32R_18_PCREL		5
#define	R_M32R_26_PCREL		6
#define	R_M32R_HI16_ULO		7
#define	R_M32R_HI16_SLO		8
#define	R_M32R_LO16		9
#define	R_M32R_SDA16		10
#define	R_M32R_GNU_VTINHERIT	11
#define	R_M32R_GNU_VTENTRY	12

#define R_M32R_16_RELA		33
#define R_M32R_32_RELA		34
#define R_M32R_24_RELA		35
#define R_M32R_10_PCREL_RELA	36
#define R_M32R_18_PCREL_RELA	37
#define R_M32R_26_PCREL_RELA	38
#define R_M32R_HI16_ULO_RELA	39
#define R_M32R_HI16_SLO_RELA	40
#define R_M32R_LO16_RELA	41
#define R_M32R_SDA16_RELA	42
#define	R_M32R_RELA_GNU_VTINHERIT	43
#define	R_M32R_RELA_GNU_VTENTRY	44

#define R_M32R_GOT24		48
#define R_M32R_26_PLTREL	49
#define R_M32R_COPY		50
#define R_M32R_GLOB_DAT		51
#define R_M32R_JMP_SLOT		52
#define R_M32R_RELATIVE		53
#define R_M32R_GOTOFF		54
#define R_M32R_GOTPC24		55
#define R_M32R_GOT16_HI_ULO	56
#define R_M32R_GOT16_HI_SLO	57
#define R_M32R_GOT16_LO		58
#define R_M32R_GOTPC_HI_ULO	59
#define R_M32R_GOTPC_HI_SLO	60
#define R_M32R_GOTPC_LO		61
#define R_M32R_GOTOFF_HI_ULO	62
#define R_M32R_GOTOFF_HI_SLO	63
#define R_M32R_GOTOFF_LO	64

#define R_M32R_NUM		256

/*
 * ELF register definitions..
 */
#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* We have no FP mumumu.  */
typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_M32R) || ((x)->e_machine == EM_CYGNUS_M32R))

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#if defined(__LITTLE_ENDIAN__)
#define ELF_DATA	ELFDATA2LSB
#elif defined(__BIG_ENDIAN__)
#define ELF_DATA	ELFDATA2MSB
#else
#error no endian defined
#endif
#define ELF_ARCH	EM_M32R

/* r0 is set by ld.so to a pointer to a function which might be
 * registered using 'atexit'.  This provides a mean for the dynamic
 * linker to call DT_FINI functions for shared libraries that have
 * been loaded before the code runs.
 *
 * So that we can use the same startup file with static executables,
 * we start programs with a value of 0 to indicate that there is no
 * such function.
 */
#define ELF_PLAT_INIT(_r, load_addr)	(_r)->r0 = 0

#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.
 * Typical use of this is to invoke "./ld.so someprog" to test out a
 * new version of the loader.  We need to make sure that it is out of
 * the way of the program that it will "exec", and that there is
 * sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE         (TASK_SIZE / 3 * 2)

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different) */

#define ELF_CORE_COPY_REGS(pr_reg, regs)  \
	memcpy((char *)pr_reg, (char *)regs, sizeof (struct pt_regs));

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  */
#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */
#define ELF_PLATFORM	(NULL)

#define SET_PERSONALITY(ex) \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK)))

#endif  /* _ASM_M32R__ELF_H */
