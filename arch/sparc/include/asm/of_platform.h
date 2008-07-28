#ifndef ___ASM_SPARC_OF_PLATFORM_H
#define ___ASM_SPARC_OF_PLATFORM_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/of_platform_64.h>
#else
#include <asm/of_platform_32.h>
#endif
#endif
