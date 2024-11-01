// SPDX-License-Identifier: GPL-2.0
#include "relocs.h"

#define ELF_BITS 64

#define ELF_MACHINE             EM_X86_64
#define ELF_MACHINE_NAME        "x86_64"
#define SHT_REL_TYPE            SHT_RELA
#define Elf_Rel                 Elf64_Rela

#define ELF_CLASS               ELFCLASS64
#define ELF_R_SYM(val)          ELF64_R_SYM(val)
#define ELF_R_TYPE(val)         ELF64_R_TYPE(val)
#define ELF_ST_TYPE(o)          ELF64_ST_TYPE(o)
#define ELF_ST_BIND(o)          ELF64_ST_BIND(o)
#define ELF_ST_VISIBILITY(o)    ELF64_ST_VISIBILITY(o)

#include "relocs.c"
