#ifndef ___ASM_SPARC_SMP_H
#define ___ASM_SPARC_SMP_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/smp_64.h>
#else
#include <asm/smp_32.h>
#endif
#endif
