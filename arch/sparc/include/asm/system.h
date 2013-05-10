#ifndef ___ASM_SPARC_SYSTEM_H
#define ___ASM_SPARC_SYSTEM_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/system_64.h>
#else
#include <asm/system_32.h>
#endif
#endif
