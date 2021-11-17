// SPDX-License-Identifier: GPL-2.0
#include "relocs.h"

#define ELF_BITS 32

#define ELF_MACHINE		EM_386
#define ELF_MACHINE_NAME	"i386"
#define SHT_REL_TYPE		SHT_REL
#define Elf_Rel			ElfW(Rel)

#define ELF_CLASS		ELFCLASS32
#define ELF_R_SYM(val)		ELF32_R_SYM(val)
#define ELF_R_TYPE(val)		ELF32_R_TYPE(val)
#define ELF_ST_TYPE(o)		ELF32_ST_TYPE(o)
#define ELF_ST_BIND(o)		ELF32_ST_BIND(o)
#define ELF_ST_VISIBILITY(o)	ELF32_ST_VISIBILITY(o)

#include "relocs.c"
