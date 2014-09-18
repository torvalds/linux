/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_ELF_H
#define _ASM_C6X_ELF_H

/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>

typedef unsigned long elf_greg_t;
typedef unsigned long elf_fpreg_t;

#define ELF_NGREG  58
#define ELF_NFPREG 1

typedef elf_greg_t elf_gregset_t[ELF_NGREG];
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_TI_C6000)

#define elf_check_fdpic(x) (1)
#define elf_check_const_displacement(x) (0)

#define ELF_FDPIC_PLAT_INIT(_regs, _exec_map, _interp_map, _dynamic_addr) \
do {								\
	_regs->b4	= (_exec_map);				\
	_regs->a6	= (_interp_map);			\
	_regs->b6	= (_dynamic_addr);			\
} while (0)

#define ELF_FDPIC_CORE_EFLAGS	0

#define ELF_CORE_COPY_FPREGS(...) 0 /* No FPU regs to copy */

/*
 * These are used to set parameters in the core dumps.
 */
#ifdef __LITTLE_ENDIAN__
#define ELF_DATA	ELFDATA2LSB
#else
#define ELF_DATA	ELFDATA2MSB
#endif

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_TI_C6000

/* Nothing for now. Need to setup DP... */
#define ELF_PLAT_INIT(_r)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(_dest, _regs)		\
	memcpy((char *) &_dest, (char *) _regs,		\
	sizeof(struct pt_regs));

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM  (NULL)

/* C6X specific section types */
#define SHT_C6000_UNWIND	0x70000001
#define SHT_C6000_PREEMPTMAP	0x70000002
#define SHT_C6000_ATTRIBUTES	0x70000003

/* C6X specific DT_ tags */
#define DT_C6000_DSBT_BASE	0x70000000
#define DT_C6000_DSBT_SIZE	0x70000001
#define DT_C6000_PREEMPTMAP	0x70000002
#define DT_C6000_DSBT_INDEX	0x70000003

/* C6X specific relocs */
#define R_C6000_NONE		0
#define R_C6000_ABS32		1
#define R_C6000_ABS16		2
#define R_C6000_ABS8		3
#define R_C6000_PCR_S21		4
#define R_C6000_PCR_S12		5
#define R_C6000_PCR_S10		6
#define R_C6000_PCR_S7		7
#define R_C6000_ABS_S16		8
#define R_C6000_ABS_L16		9
#define R_C6000_ABS_H16		10
#define R_C6000_SBR_U15_B	11
#define R_C6000_SBR_U15_H	12
#define R_C6000_SBR_U15_W	13
#define R_C6000_SBR_S16		14
#define R_C6000_SBR_L16_B	15
#define R_C6000_SBR_L16_H	16
#define R_C6000_SBR_L16_W	17
#define R_C6000_SBR_H16_B	18
#define R_C6000_SBR_H16_H	19
#define R_C6000_SBR_H16_W	20
#define R_C6000_SBR_GOT_U15_W	21
#define R_C6000_SBR_GOT_L16_W	22
#define R_C6000_SBR_GOT_H16_W	23
#define R_C6000_DSBT_INDEX	24
#define R_C6000_PREL31		25
#define R_C6000_COPY		26
#define R_C6000_ALIGN		253
#define R_C6000_FPHEAD		254
#define R_C6000_NOCMP		255

#endif /*_ASM_C6X_ELF_H */
