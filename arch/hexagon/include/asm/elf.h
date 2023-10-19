/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ELF definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef __ASM_ELF_H
#define __ASM_ELF_H

#include <asm/ptrace.h>
#include <asm/user.h>
#include <linux/elf-em.h>

struct elf32_hdr;

/*
 * ELF header e_flags defines.
 */

/*  should have stuff like "CPU type" and maybe "ABI version", etc  */

/* Hexagon relocations */
  /* V2 */
#define R_HEXAGON_NONE           0
#define R_HEXAGON_B22_PCREL      1
#define R_HEXAGON_B15_PCREL      2
#define R_HEXAGON_B7_PCREL       3
#define R_HEXAGON_LO16           4
#define R_HEXAGON_HI16           5
#define R_HEXAGON_32             6
#define R_HEXAGON_16             7
#define R_HEXAGON_8              8
#define R_HEXAGON_GPREL16_0      9
#define R_HEXAGON_GPREL16_1     10
#define R_HEXAGON_GPREL16_2     11
#define R_HEXAGON_GPREL16_3     12
#define R_HEXAGON_HL16          13
  /* V3 */
#define R_HEXAGON_B13_PCREL     14
  /* V4 */
#define R_HEXAGON_B9_PCREL      15
  /* V4 (extenders) */
#define R_HEXAGON_B32_PCREL_X   16
#define R_HEXAGON_32_6_X        17
  /* V4 (extended) */
#define R_HEXAGON_B22_PCREL_X   18
#define R_HEXAGON_B15_PCREL_X   19
#define R_HEXAGON_B13_PCREL_X   20
#define R_HEXAGON_B9_PCREL_X    21
#define R_HEXAGON_B7_PCREL_X    22
#define R_HEXAGON_16_X          23
#define R_HEXAGON_12_X          24
#define R_HEXAGON_11_X          25
#define R_HEXAGON_10_X          26
#define R_HEXAGON_9_X           27
#define R_HEXAGON_8_X           28
#define R_HEXAGON_7_X           29
#define R_HEXAGON_6_X           30
  /* V2 PIC */
#define R_HEXAGON_32_PCREL      31
#define R_HEXAGON_COPY          32
#define R_HEXAGON_GLOB_DAT      33
#define R_HEXAGON_JMP_SLOT      34
#define R_HEXAGON_RELATIVE      35
#define R_HEXAGON_PLT_B22_PCREL 36
#define R_HEXAGON_GOTOFF_LO16   37
#define R_HEXAGON_GOTOFF_HI16   38
#define R_HEXAGON_GOTOFF_32     39
#define R_HEXAGON_GOT_LO16      40
#define R_HEXAGON_GOT_HI16      41
#define R_HEXAGON_GOT_32        42
#define R_HEXAGON_GOT_16        43

/*
 * ELF register definitions..
 */
typedef unsigned long elf_greg_t;

typedef struct user_regs_struct elf_gregset_t;
#define ELF_NGREG (sizeof(elf_gregset_t)/sizeof(unsigned long))

/*  Placeholder  */
typedef unsigned long elf_fpregset_t;

/*
 * Bypass the whole "regsets" thing for now and use the define.
 */

#if CONFIG_HEXAGON_ARCH_VERSION >= 4
#define CS_COPYREGS(DEST,REGS) \
do {\
	DEST.cs0 = REGS->cs0;\
	DEST.cs1 = REGS->cs1;\
} while (0)
#else
#define CS_COPYREGS(DEST,REGS)
#endif

#define ELF_CORE_COPY_REGS(DEST, REGS)	\
do {					\
	DEST.r0 = REGS->r00;		\
	DEST.r1 = REGS->r01;		\
	DEST.r2 = REGS->r02;		\
	DEST.r3 = REGS->r03;		\
	DEST.r4 = REGS->r04;		\
	DEST.r5 = REGS->r05;		\
	DEST.r6 = REGS->r06;		\
	DEST.r7 = REGS->r07;		\
	DEST.r8 = REGS->r08;		\
	DEST.r9 = REGS->r09;		\
	DEST.r10 = REGS->r10;		\
	DEST.r11 = REGS->r11;		\
	DEST.r12 = REGS->r12;		\
	DEST.r13 = REGS->r13;		\
	DEST.r14 = REGS->r14;		\
	DEST.r15 = REGS->r15;		\
	DEST.r16 = REGS->r16;		\
	DEST.r17 = REGS->r17;		\
	DEST.r18 = REGS->r18;		\
	DEST.r19 = REGS->r19;		\
	DEST.r20 = REGS->r20;		\
	DEST.r21 = REGS->r21;		\
	DEST.r22 = REGS->r22;		\
	DEST.r23 = REGS->r23;		\
	DEST.r24 = REGS->r24;		\
	DEST.r25 = REGS->r25;		\
	DEST.r26 = REGS->r26;		\
	DEST.r27 = REGS->r27;		\
	DEST.r28 = REGS->r28;		\
	DEST.r29 = pt_psp(REGS);	\
	DEST.r30 = REGS->r30;		\
	DEST.r31 = REGS->r31;		\
	DEST.sa0 = REGS->sa0;		\
	DEST.lc0 = REGS->lc0;		\
	DEST.sa1 = REGS->sa1;		\
	DEST.lc1 = REGS->lc1;		\
	DEST.m0 = REGS->m0;		\
	DEST.m1 = REGS->m1;		\
	DEST.usr = REGS->usr;		\
	DEST.p3_0 = REGS->preds;	\
	DEST.gp = REGS->gp;		\
	DEST.ugp = REGS->ugp;		\
	CS_COPYREGS(DEST,REGS);		\
	DEST.pc = pt_elr(REGS);		\
	DEST.cause = pt_cause(REGS);	\
	DEST.badva = pt_badva(REGS);	\
} while (0);

/*
 * This is used to ensure we don't load something for the wrong architecture.
 * Checks the machine and ABI type.
 */
#define elf_check_arch(hdr)	((hdr)->e_machine == EM_HEXAGON)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_HEXAGON

#if CONFIG_HEXAGON_ARCH_VERSION == 2
#define ELF_CORE_EFLAGS 0x1
#endif

#if CONFIG_HEXAGON_ARCH_VERSION == 3
#define ELF_CORE_EFLAGS 0x2
#endif

#if CONFIG_HEXAGON_ARCH_VERSION == 4
#define ELF_CORE_EFLAGS 0x3
#endif

/*
 * Some architectures have ld.so set up a pointer to a function
 * to be registered using atexit, to facilitate cleanup.  So that
 * static executables will be well-behaved, we would null the register
 * in question here, in the pt_regs structure passed.  For now,
 * leave it a null macro.
 */
#define ELF_PLAT_INIT(regs, load_addr) do { } while (0)

#define CORE_DUMP_USE_REGSET

/* Hrm is this going to cause problems for changing PAGE_SIZE?  */
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE         0x08000000UL

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP	(0)

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */
#define ELF_PLATFORM  (NULL)

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);


#endif
