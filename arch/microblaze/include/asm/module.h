/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_MODULE_H
#define _ASM_MICROBLAZE_MODULE_H

/* Microblaze Relocations */
#define R_MICROBLAZE_NONE 0
#define R_MICROBLAZE_32 1
#define R_MICROBLAZE_32_PCREL 2
#define R_MICROBLAZE_64_PCREL 3
#define R_MICROBLAZE_32_PCREL_LO 4
#define R_MICROBLAZE_64 5
#define R_MICROBLAZE_32_LO 6
#define R_MICROBLAZE_SRO32 7
#define R_MICROBLAZE_SRW32 8
#define R_MICROBLAZE_64_NONE 9
#define R_MICROBLAZE_32_SYM_OP_SYM 10
/* Keep this the last entry. */
#define R_MICROBLAZE_NUM 11

struct mod_arch_specific {
	int foo;
};

#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define Elf_Ehdr	Elf32_Ehdr

typedef struct { volatile int counter; } module_t;

#endif /* _ASM_MICROBLAZE_MODULE_H */
