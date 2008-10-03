#ifndef ___ASM_SPARC_STRING_H
#define ___ASM_SPARC_STRING_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/string_64.h>
#else
#include <asm/string_32.h>
#endif
#endif
