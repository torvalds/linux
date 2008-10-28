#ifndef ___ASM_SPARC_SECTIONS_H
#define ___ASM_SPARC_SECTIONS_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/sections_64.h>
#else
#include <asm/sections_32.h>
#endif
#endif
