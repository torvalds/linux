/*
 * linux/arch/unicore32/include/asm/elf.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_ELF_H__
#define __UNICORE_ELF_H__

#include <asm/hwcap.h>

/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>
#include <linux/elf-em.h>

typedef unsigned long elf_greg_t;
typedef unsigned long elf_freg_t[3];

#define ELF_NGREG (sizeof(struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct fp_state elf_fpregset_t;

#define R_UNICORE_NONE		0
#define R_UNICORE_PC24		1
#define R_UNICORE_ABS32		2
#define R_UNICORE_CALL		28
#define R_UNICORE_JUMP24	29

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_UNICORE

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 *
 */
#define ELF_PLATFORM_SIZE 8
#define ELF_PLATFORM	(elf_platform)

extern char elf_platform[];

struct elf32_hdr;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
extern int elf_check_arch(const struct elf32_hdr *);
#define elf_check_arch elf_check_arch

struct task_struct;
int dump_task_regs(struct task_struct *t, elf_gregset_t *elfregs);
#define ELF_CORE_COPY_TASK_REGS dump_task_regs

#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE	(2 * TASK_SIZE / 3)

/* When the program starts, a1 contains a pointer to a function to be
   registered with atexit, as per the SVR4 ABI.  A value of 0 means we
   have no such handler.  */
#define ELF_PLAT_INIT(_r, load_addr)	{(_r)->UCreg_00 = 0; }

extern void elf_set_personality(const struct elf32_hdr *);
#define SET_PERSONALITY(ex)	elf_set_personality(&(ex))

struct mm_struct;
extern unsigned long arch_randomize_brk(struct mm_struct *mm);
#define arch_randomize_brk arch_randomize_brk

extern int vectors_user_mapping(void);
#define arch_setup_additional_pages(bprm, uses_interp) vectors_user_mapping()
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES

#endif
