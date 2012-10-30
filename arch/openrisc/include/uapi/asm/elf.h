/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _UAPI__ASM_OPENRISC_ELF_H
#define _UAPI__ASM_OPENRISC_ELF_H

/*
 * This files is partially exported to userspace.  This allows us to keep
 * the ELF bits in one place which should assist in keeping the kernel and
 * userspace in sync.
 */

/*
 * ELF register definitions..
 */

/* for struct user_regs_struct definition */
#include <asm/ptrace.h>

/* The OR1K relocation types... not all relevant for module loader */
#define R_OR32_NONE	0
#define R_OR32_32	1
#define R_OR32_16	2
#define R_OR32_8	3
#define R_OR32_CONST	4
#define R_OR32_CONSTH	5
#define R_OR32_JUMPTARG	6
#define R_OR32_VTINHERIT 7
#define R_OR32_VTENTRY	8

typedef unsigned long elf_greg_t;

/*
 * Note that NGREG is defined to ELF_NGREG in include/linux/elfcore.h, and is
 * thus exposed to user-space.
 */
#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* A placeholder; OR32 does not have fp support yes, so no fp regs for now.  */
typedef unsigned long elf_fpregset_t;

/* This should be moved to include/linux/elf.h */
#define EM_OR32         0x8472
#define EM_OPENRISC     92     /* OpenRISC 32-bit embedded processor */

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_OR32
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB

#endif /* _UAPI__ASM_OPENRISC_ELF_H */
