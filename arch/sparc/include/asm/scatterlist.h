#ifndef ___ASM_SPARC_SCATTERLIST_H
#define ___ASM_SPARC_SCATTERLIST_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/scatterlist_64.h>
#else
#include <asm/scatterlist_32.h>
#endif
#endif
