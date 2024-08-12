/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
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
#define R_OR1K_NONE		0
#define R_OR1K_32		1
#define R_OR1K_16		2
#define R_OR1K_8		3
#define R_OR1K_LO_16_IN_INSN	4
#define R_OR1K_HI_16_IN_INSN	5
#define R_OR1K_INSN_REL_26	6
#define R_OR1K_GNU_VTENTRY	7
#define R_OR1K_GNU_VTINHERIT	8
#define R_OR1K_32_PCREL		9
#define R_OR1K_16_PCREL		10
#define R_OR1K_8_PCREL		11
#define R_OR1K_GOTPC_HI16	12
#define R_OR1K_GOTPC_LO16	13
#define R_OR1K_GOT16		14
#define R_OR1K_PLT26		15
#define R_OR1K_GOTOFF_HI16	16
#define R_OR1K_GOTOFF_LO16	17
#define R_OR1K_COPY		18
#define R_OR1K_GLOB_DAT		19
#define R_OR1K_JMP_SLOT		20
#define R_OR1K_RELATIVE		21
#define R_OR1K_TLS_GD_HI16	22
#define R_OR1K_TLS_GD_LO16	23
#define R_OR1K_TLS_LDM_HI16	24
#define R_OR1K_TLS_LDM_LO16	25
#define R_OR1K_TLS_LDO_HI16	26
#define R_OR1K_TLS_LDO_LO16	27
#define R_OR1K_TLS_IE_HI16	28
#define R_OR1K_TLS_IE_LO16	29
#define R_OR1K_TLS_LE_HI16	30
#define R_OR1K_TLS_LE_LO16	31
#define R_OR1K_TLS_TPOFF	32
#define R_OR1K_TLS_DTPOFF	33
#define R_OR1K_TLS_DTPMOD	34
#define R_OR1K_AHI16		35
#define R_OR1K_GOTOFF_AHI16	36
#define R_OR1K_TLS_IE_AHI16	37
#define R_OR1K_TLS_LE_AHI16	38
#define R_OR1K_SLO16		39
#define R_OR1K_GOTOFF_SLO16	40
#define R_OR1K_TLS_LE_SLO16	41
#define R_OR1K_PCREL_PG21	42
#define R_OR1K_GOT_PG21		43
#define R_OR1K_TLS_GD_PG21	44
#define R_OR1K_TLS_LDM_PG21	45
#define R_OR1K_TLS_IE_PG21	46
#define R_OR1K_LO13		47
#define R_OR1K_GOT_LO13		48
#define R_OR1K_TLS_GD_LO13	49
#define R_OR1K_TLS_LDM_LO13	50
#define R_OR1K_TLS_IE_LO13	51
#define R_OR1K_SLO13		52
#define R_OR1K_PLTA26		53
#define R_OR1K_GOT_AHI16	54

/* Old relocation names */
#define R_OR32_NONE	R_OR1K_NONE
#define R_OR32_32	R_OR1K_32
#define R_OR32_16	R_OR1K_16
#define R_OR32_8	R_OR1K_8
#define R_OR32_CONST	R_OR1K_LO_16_IN_INSN
#define R_OR32_CONSTH	R_OR1K_HI_16_IN_INSN
#define R_OR32_JUMPTARG	R_OR1K_INSN_REL_26
#define R_OR32_VTENTRY	R_OR1K_GNU_VTENTRY
#define R_OR32_VTINHERIT R_OR1K_GNU_VTINHERIT

typedef unsigned long elf_greg_t;

/*
 * Note that NGREG is defined to ELF_NGREG in include/linux/elfcore.h, and is
 * thus exposed to user-space.
 */
#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct __or1k_fpu_state elf_fpregset_t;

/* EM_OPENRISC is defined in linux/elf-em.h */
#define EM_OR32         0x8472

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_OR32
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB

#endif /* _UAPI__ASM_OPENRISC_ELF_H */
