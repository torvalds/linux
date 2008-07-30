#ifndef ___ASM_SPARC_POSIX_TYPES_H
#define ___ASM_SPARC_POSIX_TYPES_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/posix_types_64.h>
#else
#include <asm/posix_types_32.h>
#endif
#endif
