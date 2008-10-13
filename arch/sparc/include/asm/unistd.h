#ifndef ___ASM_SPARC_UNISTD_H
#define ___ASM_SPARC_UNISTD_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/unistd_64.h>
#else
#include <asm/unistd_32.h>
#endif
#endif
