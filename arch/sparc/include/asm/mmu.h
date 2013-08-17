#ifndef ___ASM_SPARC_MMU_H
#define ___ASM_SPARC_MMU_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/mmu_64.h>
#else
#include <asm/mmu_32.h>
#endif
#endif
