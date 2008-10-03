#ifndef ___ASM_SPARC_SHMPARAM_H
#define ___ASM_SPARC_SHMPARAM_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/shmparam_64.h>
#else
#include <asm/shmparam_32.h>
#endif
#endif
