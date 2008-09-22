#ifndef ___ASM_SPARC_ELF_H
#define ___ASM_SPARC_ELF_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/elf_64.h>
#else
#include <asm/elf_32.h>
#endif
#endif
