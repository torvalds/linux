/*
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * Copyright (C) 2012 Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _UAPI_ASM_RISCV_ELF_H
#define _UAPI_ASM_RISCV_ELF_H

#include <asm/ptrace.h>

/* ELF register definitions */
typedef unsigned long elf_greg_t;
typedef struct user_regs_struct elf_gregset_t;
#define ELF_NGREG (sizeof(elf_gregset_t) / sizeof(elf_greg_t))

/* We don't support f without d, or q.  */
typedef __u64 elf_fpreg_t;
typedef union __riscv_fp_state elf_fpregset_t;
#define ELF_NFPREG (sizeof(struct __riscv_d_ext_state) / sizeof(elf_fpreg_t))

#if __riscv_xlen == 64
#define ELF_RISCV_R_SYM(r_info)		ELF64_R_SYM(r_info)
#define ELF_RISCV_R_TYPE(r_info)	ELF64_R_TYPE(r_info)
#else
#define ELF_RISCV_R_SYM(r_info)		ELF32_R_SYM(r_info)
#define ELF_RISCV_R_TYPE(r_info)	ELF32_R_TYPE(r_info)
#endif

/*
 * RISC-V relocation types
 */

/* Relocation types used by the dynamic linker */
#define R_RISCV_NONE		0
#define R_RISCV_32		1
#define R_RISCV_64		2
#define R_RISCV_RELATIVE	3
#define R_RISCV_COPY		4
#define R_RISCV_JUMP_SLOT	5
#define R_RISCV_TLS_DTPMOD32	6
#define R_RISCV_TLS_DTPMOD64	7
#define R_RISCV_TLS_DTPREL32	8
#define R_RISCV_TLS_DTPREL64	9
#define R_RISCV_TLS_TPREL32	10
#define R_RISCV_TLS_TPREL64	11

/* Relocation types not used by the dynamic linker */
#define R_RISCV_BRANCH		16
#define R_RISCV_JAL		17
#define R_RISCV_CALL		18
#define R_RISCV_CALL_PLT	19
#define R_RISCV_GOT_HI20	20
#define R_RISCV_TLS_GOT_HI20	21
#define R_RISCV_TLS_GD_HI20	22
#define R_RISCV_PCREL_HI20	23
#define R_RISCV_PCREL_LO12_I	24
#define R_RISCV_PCREL_LO12_S	25
#define R_RISCV_HI20		26
#define R_RISCV_LO12_I		27
#define R_RISCV_LO12_S		28
#define R_RISCV_TPREL_HI20	29
#define R_RISCV_TPREL_LO12_I	30
#define R_RISCV_TPREL_LO12_S	31
#define R_RISCV_TPREL_ADD	32
#define R_RISCV_ADD8		33
#define R_RISCV_ADD16		34
#define R_RISCV_ADD32		35
#define R_RISCV_ADD64		36
#define R_RISCV_SUB8		37
#define R_RISCV_SUB16		38
#define R_RISCV_SUB32		39
#define R_RISCV_SUB64		40
#define R_RISCV_GNU_VTINHERIT	41
#define R_RISCV_GNU_VTENTRY	42
#define R_RISCV_ALIGN		43
#define R_RISCV_RVC_BRANCH	44
#define R_RISCV_RVC_JUMP	45
#define R_RISCV_LUI		46
#define R_RISCV_GPREL_I		47
#define R_RISCV_GPREL_S		48
#define R_RISCV_TPREL_I		49
#define R_RISCV_TPREL_S		50
#define R_RISCV_RELAX		51
#define R_RISCV_SUB6		52
#define R_RISCV_SET6		53
#define R_RISCV_SET8		54
#define R_RISCV_SET16		55
#define R_RISCV_SET32		56
#define R_RISCV_32_PCREL	57


#endif /* _UAPI_ASM_RISCV_ELF_H */
